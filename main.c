#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <getopt.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>

#include "types.h"
#include "misc.h"
#include "proto.h"
#include "platform.h"
#include "keycodes.h"

#include "cfg-parse.tab.h"

struct node* focused_node;
opmode_t opmode;

static char* progname;

static struct config* config;

#define for_each_remote(r) for (r = config->remotes; r; r = r->next)

struct scheduled_call {
	void (*fn)(void* arg);
	void* arg;
	uint64_t calltime;
	struct scheduled_call* next;
};

static struct scheduled_call* scheduled_calls;

void elog(const char* fmt, ...)
{
	va_list va;
	struct message* msg;

	va_start(va, fmt);
	if (opmode == MASTER) {
		vfprintf(stderr, fmt, va);
	} else {
		msg = new_message(MT_LOGMSG);
		msg->extra.buf = xvasprintf(fmt, va);
		msg->extra.len = strlen(msg->extra.buf);

		/*
		 * There are a few potential error messages during setup
		 * before we go O_NONBLOCK; handle both situations here.
		 */
		if (get_fd_nonblock(STDOUT_FILENO)) {
			mc_enqueue_message(&stdio_msgchan, msg);
		} else {
			write_message(STDOUT_FILENO, msg);
			free_message(msg);
		}
	}
	va_end(va);
}

static void schedule_call(void (*fn)(void* arg), void* arg, uint64_t when)
{
	struct scheduled_call* call;
	struct scheduled_call** prevnext;
	struct scheduled_call* newcall = xmalloc(sizeof(*newcall));
	newcall->fn = fn;
	newcall->arg = arg;
	newcall->calltime = when;

	for (prevnext = &scheduled_calls, call = *prevnext;
	     call;
	     prevnext = &call->next, call = call->next) {
		if (newcall->calltime < call->calltime)
			break;
	}

	newcall->next = call;
	*prevnext = newcall;
}

static void run_scheduled_calls(uint64_t when)
{
	struct scheduled_call* call;

	while (scheduled_calls && scheduled_calls->calltime <= when) {
		call = scheduled_calls;
		scheduled_calls = call->next;
		call->fn(call->arg);
		xfree(call);
	}
}

static void focus_master(void);

static void disconnect_remote(struct remote* rmt)
{
	pid_t pid;
	int status;
	struct message* msg;

	/* Close fds and reset send & receive queues/buffers */
	mc_close(&rmt->msgchan);

	/* Clear out scheduled messages */
	while (rmt->scheduled_messages) {
		msg = rmt->scheduled_messages;
		rmt->scheduled_messages = msg->next;
		free_message(msg);
	}

	/*
	 * A note on signal choice here: initially this used SIGTERM (which
	 * seemed more appropriate), but it appears ssh has a tendency to
	 * (under certain connection-failure conditions) block for long
	 * periods of time with SIGTERM blocked/ignored, meaning we end up
	 * blocking in wait().  So instead we just skip straight to the big
	 * gun here.  I don't think it's likely to have any terribly important
	 * cleanup to do anyway (at least in this case).
	 */
	if (rmt->sshpid > 0) {
		if (kill(rmt->sshpid, SIGKILL) && errno != ESRCH)
			perror("failed to kill remote shell");
		pid = waitpid(rmt->sshpid, &status, 0);
		if (pid != rmt->sshpid)
			perror("wait() on remote shell");
	}

	rmt->sshpid = -1;

	if (rmt == focused_node->remote)
		focus_master();
}

#define RECONNECT_INTERVAL_UNIT (500 * 1000) /* half a second */
#define MAX_RECONNECT_INTERVAL ((30 * 1000 * 1000) / RECONNECT_INTERVAL_UNIT)
#define MAX_RECONNECT_ATTEMPTS 10

static void fail_remote(struct remote* rmt, const char* reason)
{
	uint64_t tmp, lshift;

	elog("disconnecting remote '%s': %s\n", rmt->node.name, reason);
	disconnect_remote(rmt);
	rmt->failcount += 1;

	if (rmt->failcount > MAX_RECONNECT_ATTEMPTS) {
		elog("remote '%s' exceeds failure limits, permfailing.\n",
		     rmt->node.name);
		rmt->state = CS_PERMFAILED;
		return;
	}

	rmt->state = CS_FAILED;

	/* 0.5s, 1s, 2s, 4s, 8s...capped at MAX_RECONNECT_INTERVAL */
	lshift = rmt->failcount - 1;
	if (lshift > (CHAR_BIT * sizeof(uint64_t) - 1))
		lshift = (CHAR_BIT * sizeof(uint64_t)) - 1;
	tmp = (1ULL << lshift);
	if (tmp > MAX_RECONNECT_INTERVAL)
		tmp = MAX_RECONNECT_INTERVAL;

	rmt->next_reconnect_time = get_microtime() + (tmp * RECONNECT_INTERVAL_UNIT);
}

static void enqueue_message(struct remote* rmt, struct message* msg)
{
	if (mc_enqueue_message(&rmt->msgchan, msg))
		fail_remote(rmt, "send backlog exceeded");
}

static void schedule_message(struct remote* rmt, struct message* newmsg)
{
	struct message* msg;
	struct message** prevnext;

	for (prevnext = &rmt->scheduled_messages, msg = *prevnext;
	     msg;
	     prevnext = &msg->next, msg = msg->next) {
		if (newmsg->sendtime < msg->sendtime)
			break;
	}

	newmsg->next = msg;
	*prevnext = newmsg;
}

#define SSH_DEFAULT(type, name) \
	static inline type get_##name(const struct remote* rmt) \
	{ \
		return rmt->sshcfg.name ? rmt->sshcfg.name \
			: config->ssh_defaults.name; \
	}

SSH_DEFAULT(char*, remoteshell)
SSH_DEFAULT(int, port)
SSH_DEFAULT(char*, bindaddr)
SSH_DEFAULT(char*, identityfile)
SSH_DEFAULT(char*, username)
SSH_DEFAULT(char*, remotecmd)

static void exec_remote_shell(const struct remote* rmt)
{
	int nargs;
	char* remote_shell = get_remoteshell(rmt) ? get_remoteshell(rmt) : "ssh";
	char* argv[] = {
		remote_shell,
		"-oBatchMode=yes",
		"-oServerAliveInterval=2",
		"-oServerAliveCountMax=3",

		/* placeholders */
		NULL, /* -b */
		NULL, /* bind address */
		NULL, /* -oIdentitiesOnly=yes */
		NULL, /* -i */
		NULL, /* identity file */
		NULL, /* -p */
		NULL, /* port */
		NULL, /* -l */
		NULL, /* username */
		NULL, /* hostname */
		NULL, /* remote command */

		NULL, /* argv terminator */
	};

	for (nargs = 0; argv[nargs]; nargs++) /* just find first NULL entry */;

	if (get_port(rmt)) {
		argv[nargs++] = "-p";
		argv[nargs++] = xasprintf("%d", get_port(rmt));
	}

	if (get_bindaddr(rmt)) {
		argv[nargs++] = "-b";
		argv[nargs++] = get_bindaddr(rmt);
	}

	if (get_identityfile(rmt)) {
		argv[nargs++] = "-oIdentitiesOnly=yes";
		argv[nargs++] = "-i";
		argv[nargs++] = get_identityfile(rmt);
	}

	if (get_username(rmt)) {
		argv[nargs++] = "-l";
		argv[nargs++] = get_username(rmt);
	}

	argv[nargs++] = rmt->hostname;

	argv[nargs++] = get_remotecmd(rmt) ? get_remotecmd(rmt) : progname;

	assert(nargs < ARR_LEN(argv));

	execvp(remote_shell, argv);
	perror("execvp");
	exit(1);
}

static void setup_remote(struct remote* rmt)
{
	int sockfds[2];
	struct message* setupmsg;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockfds)) {
		perror("socketpair");
		exit(1);
	}

	rmt->sshpid = fork();
	if (rmt->sshpid < 0) {
		perror("fork");
		exit(1);
	}

	rmt->state = CS_SETTINGUP;

	if (!rmt->sshpid) {
		/* ssh child */
		if (dup2(sockfds[1], STDIN_FILENO) < 0
		    || dup2(sockfds[1], STDOUT_FILENO) < 0) {
			perror("dup2");
			exit(1);
		}

		if (close(sockfds[0]))
			perror("close");

		exec_remote_shell(rmt);
	}

	set_fd_nonblock(sockfds[0], 1);
	set_fd_cloexec(sockfds[0], 1);

	mc_init(&rmt->msgchan, sockfds[0], sockfds[0]);

	if (close(sockfds[1]))
		perror("close");

	setupmsg = new_message(MT_SETUP);
	setupmsg->type = MT_SETUP;
	setupmsg->setup.prot_vers = PROT_VERSION;
	setupmsg->extra.buf = flatten_kvmap(rmt->params, &setupmsg->extra.len);

	enqueue_message(rmt, setupmsg);
}

static struct node* find_node(const char* name)
{
	struct remote* rmt;

	if (!name)
		return &config->master;

	/* First search by alias */
	for_each_remote (rmt) {
		if (!strcmp(name, rmt->node.name))
			return &rmt->node;
	}

	/* if that fails, try hostnames */
	for_each_remote (rmt) {
		if (!strcmp(name, rmt->hostname))
			return &rmt->node;
	}

	return NULL;
}

static void resolve_noderef(struct noderef* n)
{
	char* name;
	struct node* node;

	if (n->type == NT_TMPNAME) {
		name = n->name;
		node = find_node(name);
		if (!node) {
			elog("No such remote: '%s'\n", n->name);
			exit(1);
		}
		n->type = NT_NODE;
		n->node = node;
		xfree(name);
	}
}

static const char* dirnames[] = {
	[LEFT] = "left",
	[RIGHT] = "right",
	[UP] = "up",
	[DOWN] = "down",
};

static void apply_link(struct link* ln)
{
	resolve_noderef(&ln->a.nr);
	resolve_noderef(&ln->b.nr);

	assert(ln->a.dir != NO_DIR);
	if (ln->a.nr.node->neighbors[ln->a.dir])
		elog("Warning: %s %s neighbor already specified\n",
		     ln->a.nr.node->name, dirnames[ln->a.dir]);
	ln->a.nr.node->neighbors[ln->a.dir] = ln->b.nr.node;

	if (ln->b.dir != NO_DIR) {
		if (ln->b.nr.node->neighbors[ln->b.dir])
			elog("Warning: %s %s neighbor already specified\n",
			     ln->b.nr.node->name, dirnames[ln->b.dir]);
		ln->b.nr.node->neighbors[ln->b.dir] = ln->a.nr.node;
	}
}

static void apply_topology(void)
{
	struct link* ln;

	for (ln = config->topology; ln; ln = ln->next)
		apply_link(ln);
}

static void mark_reachable(struct node* n)
{
	int seen;
	direction_t dir;

	if (!n || is_master(n))
		return;

	seen = n->remote->reachable;
	n->remote->reachable = 1;

	if (!seen) {
		for_each_direction (dir)
			mark_reachable(n->neighbors[dir]);
	}
}

static void check_remotes(void)
{
	direction_t dir;
	struct remote* rmt;
	int num_neighbors;

	for_each_direction (dir)
		mark_reachable(config->master.neighbors[dir]);

	for_each_remote (rmt) {
		if (!rmt->reachable)
			elog("Warning: remote '%s' is not reachable\n", rmt->node.name);

		num_neighbors = 0;
		for_each_direction (dir) {
			if (rmt->node.neighbors[dir])
				num_neighbors += 1;
		}

		if (!num_neighbors)
			elog("Warning: remote '%s' has no neighbors\n", rmt->node.name);
	}
}


static void transfer_clipboard(struct node* from, struct node* to)
{
	struct message* msg;

	if (is_master(from) && is_master(to)) {
		elog("switching from master to master??\n");
		return;
	}

	if (is_remote(from)) {
		msg = new_message(MT_GETCLIPBOARD);
		enqueue_message(from->remote, msg);
	} else if (is_remote(to)) {
		msg = new_message(MT_SETCLIPBOARD);
		msg->extra.buf = get_clipboard_text();
		msg->extra.len = strlen(msg->extra.buf);
		assert(msg->extra.len <= UINT32_MAX);
		enqueue_message(to->remote, msg);
	}
}

static void transfer_modifiers(struct node* from, struct node* to,
                               const keycode_t* modkeys)
{
	int i;
	struct message* msg;

	if (is_remote(from)) {
		for (i = 0; modkeys[i] != ET_null; i++) {
			msg = new_message(MT_KEYEVENT);
			msg->keyevent.pressrel = PR_RELEASE;
			msg->keyevent.keycode = modkeys[i];
			enqueue_message(from->remote, msg);
		}
	}

	if (is_remote(to)) {
		for (i = 0; modkeys[i] != ET_null; i++) {
			msg = new_message(MT_KEYEVENT);
			msg->keyevent.pressrel = PR_PRESS;
			msg->keyevent.keycode = modkeys[i];
			enqueue_message(to->remote, msg);
		}
	}
}

void send_keyevent(struct remote* rmt, keycode_t kc, pressrel_t pr)
{
	struct message* msg;

	if (!rmt)
		return;

	msg = new_message(MT_KEYEVENT);

	msg->keyevent.keycode = kc;
	msg->keyevent.pressrel = pr;

	enqueue_message(rmt, msg);
}

void send_moverel(struct remote* rmt, int32_t dx, int32_t dy)
{
	struct message* msg;

	if (!rmt)
		return;

	msg = new_message(MT_MOVEREL);

	msg->moverel.dx = dx;
	msg->moverel.dy = dy;

	enqueue_message(rmt, msg);
}

void send_clickevent(struct remote* rmt, mousebutton_t button, pressrel_t pr)
{
	struct message* msg;

	if (!rmt)
		return;

	msg = new_message(MT_CLICKEVENT);

	msg->clickevent.button = button;
	msg->clickevent.pressrel = pr;

	enqueue_message(rmt, msg);
}

void send_setbrightness(struct remote* rmt, float f)
{
	struct message* msg;

	if (!rmt)
		return;

	msg = new_message(MT_SETBRIGHTNESS);

	msg->setbrightness.brightness = f;

	enqueue_message(rmt, msg);
}

static void set_node_display_brightness(struct node* node, float f)
{
	if (is_master(node))
		set_display_brightness(f);
	else
		send_setbrightness(node->remote, f);
}

static void set_brightness_cb(void* arg)
{
	float* fp = arg;

	set_display_brightness(*fp);

	xfree(fp);
}

static void schedule_brightness_change(struct node* node, float f, uint64_t when)
{
	struct message* msg;
	float* fp;

	if (is_remote(node)) {
		msg = new_message(MT_SETBRIGHTNESS);
		msg->setbrightness.brightness = f;
		msg->sendtime = when;
		schedule_message(node->remote, msg);
	} else {
		fp = xmalloc(sizeof(*fp));
		*fp = f;
		schedule_call(set_brightness_cb, fp, when);
	}
}

static void transition_brightness(struct node* node, float from, float to,
                                  uint64_t duration, int steps)
{
	int i;
	float frac, level;
	uint64_t time, now_us = get_microtime();

	set_node_display_brightness(node, from);
	for (i = 1; i < steps; i++) {
		frac = (float)i / (float)steps;
		time = now_us + (uint64_t)(frac * (float)duration);
		level = from + (frac * (to - from));
		schedule_brightness_change(node, level, time);
	}
	schedule_brightness_change(node, to, now_us + duration);
}

static void indicate_switch(struct node* from, struct node* to)
{
	struct focus_hint* fh = &config->focus_hint;

	switch (fh->type) {
	case FH_NONE:
		break;

	case FH_DIM_INACTIVE:
		if (from != to)
			transition_brightness(from, 1.0, fh->brightness, fh->duration,
			                      fh->fade_steps);
		transition_brightness(to, fh->brightness, 1.0, fh->duration,
		                      fh->fade_steps);
		break;

	case FH_FLASH_ACTIVE:
		transition_brightness(to, fh->brightness, 1.0, fh->duration,
		                      fh->fade_steps);
		break;

	default:
		elog("unknown focus_hint type %d\n", fh->type);
		break;
	}
}

static struct xypoint saved_master_mousepos;

/*
 * Returns non-zero on a successful "real" switch, or zero if no actual switch
 * was performed (i.e. the switched-to node is the same as the current node,
 * or the remote we tried to switch to is currently disconnected).
 */
static int focus_node(struct node* n, keycode_t* modkeys, int via_hotkey)
{
	struct node* to;
	struct node* from;

	if (!n) {
		to = focused_node;
	} else if (is_remote(n) && n->remote->state != CS_CONNECTED) {
		elog("Remote %s not connected, can't focus\n", n->name);
		to = focused_node;
	} else {
		to = n;
	}

	from = focused_node;

	/*
	 * If configured to do so, give visual indication even if no actual
	 * switch is performed.
	 */
	if (to != from
	    || config->show_nullswitch == NS_YES
	    || (config->show_nullswitch == NS_HOTKEYONLY && via_hotkey))
		indicate_switch(from, to);

	if (to == from)
		return 0;

	if (is_remote(from) && is_master(to)) {
		ungrab_inputs();
		set_mousepos(saved_master_mousepos);
	} else if (is_master(from) && is_remote(to)) {
		saved_master_mousepos = get_mousepos();
		grab_inputs();
	}

	if (is_remote(to))
		set_mousepos(screen_center);

	transfer_clipboard(from, to);
	transfer_modifiers(from, to, modkeys);

	focused_node = to;

	return 1;
}

static void focus_master(void)
{
	keycode_t* modkeys = get_current_modifiers();

	focus_node(&config->master, modkeys, 0);

	xfree(modkeys);
}

static int focus_neighbor(direction_t dir, keycode_t* modkeys, int via_hotkey)
{
	return focus_node(focused_node->neighbors[dir], modkeys, via_hotkey);
}

static void clear_ssh_config(struct ssh_config* c)
{
	xfree(c->remoteshell);
	xfree(c->bindaddr);
	xfree(c->identityfile);
	xfree(c->username);
	xfree(c->remotecmd);
	memset(c, 0, sizeof(*c));
}

static void free_remote(struct remote* rmt)
{
	xfree(rmt->node.name);
	xfree(rmt->hostname);
	destroy_kvmap(rmt->params);
	clear_ssh_config(&rmt->sshcfg);
	xfree(rmt);
}

static void shutdown_master(void)
{
	struct remote* rmt;
	struct scheduled_call* sc;
	struct hotkey* hk;

	while (config->remotes) {
		rmt = config->remotes;
		config->remotes = rmt->next;
		disconnect_remote(rmt);
		free_remote(rmt);
	}

	while (scheduled_calls) {
		sc = scheduled_calls;
		scheduled_calls = sc->next;
		xfree(sc);
	}

	while (config->hotkeys) {
		hk = config->hotkeys;
		config->hotkeys = hk->next;
		xfree(hk->key_string);
		xfree(hk);
	}

	clear_ssh_config(&config->ssh_defaults);

	platform_exit();
}

static void action_cb(hotkey_context_t ctx, void* arg)
{
	struct remote* rmt;
	uint64_t now_us;
	struct action* a = arg;
	keycode_t* modkeys = get_hotkey_modifiers(ctx);

	switch (a->type) {
	case AT_FOCUS:
		switch (a->target.type) {
		case FT_DIRECTION:
			focus_neighbor(a->target.dir, modkeys, 1);
			break;
		case FT_NODE:
			focus_node(a->target.nr.node, modkeys, 1);
			break;
		default:
			elog("bad focus-target type %s\n", a->target.type);
			break;
		}
		break;

	case AT_RECONNECT:
		now_us = get_microtime();
		for_each_remote (rmt) {
			if (rmt->state == CS_PERMFAILED)
				rmt->state = CS_FAILED;
			rmt->failcount = 0;
			rmt->next_reconnect_time = now_us;
		}
		break;

	case AT_QUIT:
		xfree(modkeys);
		shutdown_master();
		exit(0);

	default:
		elog("unknown action type %d\n", a->type);
		break;
	}

	xfree(modkeys);
}

static void bind_hotkeys(void)
{
	struct hotkey* k;

	for (k = config->hotkeys; k; k = k->next) {
		if (k->action.type == AT_FOCUS && k->action.target.type == FT_NODE)
			resolve_noderef(&k->action.target.nr);
		if (bind_hotkey(k->key_string, action_cb, &k->action))
			exit(1);
	}
}

static int record_edgeevent(struct edge_state* es, edgeevent_t evtype, uint64_t when)
{
	if (evtype == es->last_evtype)
		return 1;

	es->evidx = (es->evidx + 1) % EDGESTATE_HISTLEN;
	es->event_times[es->evidx] = when;
	es->last_evtype = evtype;
	return 0;
}

static uint64_t get_edgehist_entry(const struct edge_state* es, int rel_idx)
{
	int idx;

	assert(rel_idx < EDGESTATE_HISTLEN && rel_idx >= 0);
	idx = (es->evidx - rel_idx + EDGESTATE_HISTLEN) % EDGESTATE_HISTLEN;

	return es->event_times[idx];
}

/*
 * Send the screen-relative reposition to make switch-by-mouse look more
 * "natural" -- so the mouse pointer slides semi-continuously from one node's
 * screen to a corresponding position on the next's, rather than jumping to
 * wherever it last was on the destination node.
 */
static void edgeswitch_reposition(direction_t dir, float src_x, float src_y)
{
	struct message* msg;
	struct xypoint pt;
	struct rectangle* screendim = &focused_node->dimensions;

	switch (dir) {
	case LEFT:
		pt.x = screendim->x.max;
		pt.y = lrintf(src_y * (float)screendim->y.max);
		break;

	case RIGHT:
		pt.x = screendim->x.min;
		pt.y = lrintf(src_y * (float)screendim->y.max);
		break;

	case UP:
		pt.x = lrintf(src_x * (float)screendim->x.max);
		pt.y = screendim->y.max;
		break;

	case DOWN:
		pt.x = lrintf(src_x * (float)screendim->x.max);
		pt.y = screendim->y.min;
		break;

	default:
		elog("bad direction %d in edgeswitch_reposition()\n", dir);
		return;
	}

	if (focused_node->remote) {
		msg = new_message(MT_MOVEABS);
		msg->moveabs.pt = pt;
		enqueue_message(focused_node->remote, msg);
	} else {
		set_mousepos(pt);
	}
}

static int trigger_edgeevent(struct edge_state* ehist, direction_t dir, edgeevent_t evtype,
                             float src_xpos, float src_ypos)
{
	int status, start_idx;
	keycode_t* modkeys;
	uint64_t duration, now_us = get_microtime();

	status = record_edgeevent(ehist, evtype, now_us);

	if (status)
		return status;

	if (config->mouseswitch.type == MS_MULTITAP && evtype == EE_ARRIVE) {
		/*
		 * How many entries back to look in the edge-event history to
		 * find the first event of the multi-tap sequence of which
		 * this might be the final element: single-tap looks at the
		 * just-recorded entry (#0), double tap looks back at #2
		 * (skipping over the EE_DEPART at #1), triple-tap looks at #4
		 * (skipping over two EE_DEPARTs and an EE_ARRIVE), etc.
		 */
		start_idx = (config->mouseswitch.num - 1) * 2;

		duration = now_us - get_edgehist_entry(ehist, start_idx);
		if (duration < config->mouseswitch.window) {
			modkeys = get_current_modifiers();
			if (focus_neighbor(dir, modkeys, 0))
				edgeswitch_reposition(dir, src_xpos, src_ypos);
			xfree(modkeys);
		}
	}

	return 0;
}

static dirmask_t point_edgemask(struct xypoint pt, const struct rectangle* screen)
{
	dirmask_t mask = 0;

	if (pt.x == screen->x.min)
		mask |= LEFTMASK;
	if (pt.x == screen->x.max)
		mask |= RIGHTMASK;
	if (pt.y == screen->y.min)
		mask |= UPMASK;
	if (pt.y == screen->y.max)
		mask |= DOWNMASK;

	return mask;
}

static void check_edgeevents(struct node* node, struct xypoint pt)
{
	direction_t dir;
	dirmask_t newmask, oldmask, dirmask;
	edgeevent_t edgeevtype;
	float xpos, ypos;

	newmask = point_edgemask(pt, &node->dimensions);
	oldmask = node->edgemask;

	node->edgemask = newmask;

	if (newmask == oldmask)
		return;

	xpos = (float)pt.x / (float)node->dimensions.x.max;
	ypos = (float)pt.y / (float)node->dimensions.y.max;

	for_each_direction (dir) {
		dirmask = 1U << dir;
		if ((oldmask & dirmask) != (newmask & dirmask)) {
			edgeevtype = (newmask & dirmask) ? EE_ARRIVE : EE_DEPART;
			if (trigger_edgeevent(&node->edgehist[dir], dir, edgeevtype, xpos, ypos))
				elog("out-of-sync edge event on %s ignored\n", node->name);
		}
	}
}

static void mousepos_cb(struct xypoint pt)
{
	check_edgeevents(&config->master, pt);
}

static void handle_message(struct remote* rmt, const struct message* msg)
{
	int loglen;
	char* logmsg;
	struct message* resp;

	switch (msg->type) {
	case MT_READY:
		if (rmt->state != CS_SETTINGUP) {
			fail_remote(rmt, "unexpected READY message");
			break;
		}
		rmt->state = CS_CONNECTED;
		rmt->failcount = 0;
		elog("remote '%s' becomes ready...\n", rmt->node.name);
		rmt->node.dimensions = msg->ready.screendim;
		if (config->focus_hint.type == FH_DIM_INACTIVE)
			transition_brightness(&rmt->node, 1.0, config->focus_hint.brightness,
			                      config->focus_hint.duration,
			                      config->focus_hint.fade_steps);
		break;

	case MT_SETCLIPBOARD:
		set_clipboard_from_buf(msg->extra.buf, msg->extra.len);
		if (focused_node->remote) {
			resp = new_message(MT_SETCLIPBOARD);
			resp->extra.buf = get_clipboard_text();
			resp->extra.len = strlen(resp->extra.buf);
			enqueue_message(focused_node->remote, resp);
		}
		break;

	case MT_LOGMSG:
		loglen = msg->extra.len > INT_MAX ? INT_MAX : msg->extra.len;
		logmsg = msg->extra.buf;
		elog("%s: %.*s%s", rmt->node.name, loglen, logmsg,
		     logmsg[msg->extra.len-1] == '\n' ? "" : "\n");
		break;

	case MT_MOUSEPOS:
		check_edgeevents(&rmt->node, msg->mousepos.pt);
		break;

	default:
		fail_remote(rmt, "unexpected message type");
		break;
	}
}

static void read_rmtdata(struct remote* rmt)
{
	int status;
	struct message msg;

	status = recv_message(&rmt->msgchan, &msg);
	if (!status)
		return;

	if (status < 0) {
		fail_remote(rmt, "failed to receive valid message");
		return;
	}

	handle_message(rmt, &msg);

	if (msg.extra.len)
		xfree(msg.extra.buf);
}

static void write_rmtdata(struct remote* rmt)
{
	int status;

	status = send_message(&rmt->msgchan);
	if (status < 0)
		fail_remote(rmt, "failed to send message");
	else /* this function should only be called with pending send data */
		assert(status > 0);
}

static inline int have_outbound_data(const struct remote* rmt)
{
	return mc_have_outbound_data(&rmt->msgchan);
}

/*
 * Check if the given remote is in a state that would be eligible for sending
 * or receiving messages.  (remote_live() is admittedly not a great name for
 * this, but I can't think of anything better at the moment.)
 */
static inline int remote_live(const struct remote* rmt)
{
	return rmt->state == CS_CONNECTED || rmt->state == CS_SETTINGUP;
}

static void enqueue_scheduled_messages(struct remote* rmt, uint64_t when)
{
	struct message* msg;

	while (rmt->scheduled_messages && rmt->scheduled_messages->sendtime <= when) {
		msg = rmt->scheduled_messages;
		rmt->scheduled_messages = msg->next;
		enqueue_message(rmt, msg);
	}
}

static struct timeval* get_select_timeout(struct timeval* tv, uint64_t now_us)
{
	const struct remote* rmt;
	uint64_t maxwait_us;
	uint64_t next_scheduled_event = UINT64_MAX;

	if (scheduled_calls && scheduled_calls->calltime < next_scheduled_event)
		next_scheduled_event = scheduled_calls->calltime;

	for_each_remote (rmt) {
		if (rmt->state == CS_FAILED
		    && rmt->next_reconnect_time < next_scheduled_event)
			next_scheduled_event = rmt->next_reconnect_time;
		else if (rmt->scheduled_messages
		         && rmt->scheduled_messages->sendtime < next_scheduled_event)
			next_scheduled_event = rmt->scheduled_messages->sendtime;
	}

	if (next_scheduled_event == UINT64_MAX)
		return NULL;

	maxwait_us = next_scheduled_event - now_us;
	tv->tv_sec = maxwait_us / 1000000;
	tv->tv_usec = maxwait_us % 1000000;
	return tv;
}

static void handle_fds(int platform_event_fd)
{
	int status, nfds = 0;
	fd_set rfds, wfds;
	struct remote* rmt;
	struct timeval sel_wait;
	uint64_t now_us;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	now_us = get_microtime();

	run_scheduled_calls(now_us);

	for_each_remote (rmt) {
		if (rmt->state == CS_FAILED && rmt->next_reconnect_time < now_us)
			setup_remote(rmt);

		if (remote_live(rmt)) {
			enqueue_scheduled_messages(rmt, now_us);
			fdset_add(rmt->msgchan.recv_fd, &rfds, &nfds);
			if (have_outbound_data(rmt))
				fdset_add(rmt->msgchan.send_fd, &wfds, &nfds);
		}
	}

	fdset_add(platform_event_fd, &rfds, &nfds);

	status = select(nfds, &rfds, &wfds, NULL, get_select_timeout(&sel_wait, now_us));
	if (status < 0) {
		perror("select");
		exit(1);
	}

	for_each_remote (rmt) {
		if (remote_live(rmt) && FD_ISSET(rmt->msgchan.recv_fd, &rfds))
			read_rmtdata(rmt);

		/*
		 * read_rmtdata() might have changed the remote's status, so
		 * check remote_live() again.
		 */
		if (remote_live(rmt) && FD_ISSET(rmt->msgchan.send_fd, &wfds))
			write_rmtdata(rmt);
	}

	if (FD_ISSET(platform_event_fd, &rfds))
		process_events();
}

void usage(FILE* out)
{
	fprintf(out, "Usage: %s CONFIGFILE\n", progname);
}

int main(int argc, char** argv)
{
	int opt;
	struct config cfg;
	struct remote* rmt;
	FILE* cfgfile;
	struct stat st;
	int platform_event_fd;

	static const struct option options[] = {
		{ "help", no_argument, NULL, 'h', },
		{ NULL, 0, NULL, 0, },
	};

	if (strrchr(argv[0], '/'))
		progname = strrchr(argv[0], '/') + 1;
	else
		progname = argv[0];

	while ((opt = getopt_long(argc, argv, "h", options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage(stdout);
			exit(0);

		default:
			elog("Unrecognized option: %c\n", opt);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (!argc) {
		/*
		 * If we've been properly invoked as a remote, stdin and
		 * stdout should not be TTYs...if they are, somebody's just
		 * run it without an argument not knowing any better and
		 * should get an error.
		 */
		if (isatty(STDIN_FILENO) || isatty(STDOUT_FILENO)) {
			usage(stderr);
			exit(1);
		}

		opmode = REMOTE;
		run_remote();
	} else if (argc == 1) {
		opmode = MASTER;
	} else {
		elog("excess arguments\n");
		exit(1);
	}

	cfgfile = fopen(argv[0], "r");
	if (!cfgfile) {
		elog("%s: %s\n", argv[0], strerror(errno));
		exit(1);
	}

	if (fstat(fileno(cfgfile), &st)) {
		elog("fstat(%s): %s\n", argv[0], strerror(errno));
		exit(1);
	}

	if (st.st_uid != getuid()) {
		elog("Error: bad ownership on %s\n", argv[0]);
		exit(1);
	}

	if (st.st_mode & (S_IWGRP|S_IWOTH)) {
		elog("Error: bad permissions on %s (writable by others)\n", argv[0]);
		exit(1);
	}

	memset(&cfg, 0, sizeof(cfg));
	if (parse_cfg(cfgfile, &cfg))
		exit(1);
	fclose(cfgfile);
	config = &cfg;

	if (platform_init(&platform_event_fd, mousepos_cb)) {
		elog("platform_init failed\n");
		exit(1);
	}

	cfg.master.name = "<master>";
	get_screen_dimensions(&cfg.master.dimensions);

	apply_topology();
	check_remotes();
	bind_hotkeys();

	focused_node = &config->master;

	for_each_remote (rmt)
		setup_remote(rmt);

	for (;;)
		handle_fds(platform_event_fd);
}
