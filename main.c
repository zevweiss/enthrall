#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <getopt.h>
#include <syslog.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>

#include "types.h"
#include "misc.h"
#include "message.h"
#include "platform.h"
#include "keycodes.h"

#include "cfg-parse.tab.h"

/* Default config values are zero for all but a few things. */
static struct config global_cfg = {
	.log.level = LL_INFO,
	.reconnect = {
		.max_tries = 10,
		.max_interval = 30 * 1000 * 1000,
	},
};
static struct config* config = &global_cfg;

struct node* focused_node;
static struct node* last_focused_node;
opmode_t opmode;

static char* progname;
static int orig_argc;
static char** orig_argv;

static FILE* logfile;

#define for_each_remote(r) for (r = config->remotes; r; r = r->next)

static void focus_master(void);
static void setup_remote(struct remote* rmt);
static void handle_message(struct remote* rmt, const struct message* msg);

#define SYSLOG_FACILITY LOG_USER

static void init_logfile(void)
{
	switch (config->log.file.type) {
	case LF_NONE:
		break;

	case LF_STDERR:
		logfile = stderr;
		break;

	case LF_FILE:
		logfile = fopen(config->log.file.path, "a");
		if (!logfile) {
			fprintf(stderr, "Failed to open log file %s\n",
			        config->log.file.path);
			exit(1);
		}
		setlinebuf(logfile);
		break;

	case LF_SYSLOG:
		openlog(progname, LOG_PID, SYSLOG_FACILITY);
		break;

	default:
		fprintf(stderr, "Bad log file type %d\n", config->log.file.type);
		abort();
	}
}

/* Small hack to let remote.c set the log level... */
void set_loglevel(unsigned int level)
{
	config->log.level = level;
}

__printf(1, 2) void initerr(const char* fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}

static void vlog(const char* fmt, va_list va)
{
	char datestr[128];
	time_t now;
	struct tm tm;

	switch (config->log.file.type) {
	case LF_NONE:
		break;

	case LF_SYSLOG:
		/* TODO: maybe carry different log levels through to syslog? */
		vsyslog(SYSLOG_FACILITY|LOG_NOTICE, fmt, va);
		break;

	case LF_FILE:
	case LF_STDERR:
		now = time(NULL);
		if (!localtime_r(&now, &tm)) {
			fprintf(stderr, "localtime_r() failed\n");
			abort();
		}
		if (!strftime(datestr, sizeof(datestr), "%F %T", &tm)) {
			fprintf(stderr, "strftime() failed\n");
			abort();
		}
		fprintf(logfile, "[%d] %s: ", getpid(), datestr);
		vfprintf(logfile, fmt, va);
		break;

	default:
		fprintf(stderr, "bad logfile type %d\n", config->log.file.type);
		abort();
	}
}

static void log_direct(const char* fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vlog(fmt, va);
	va_end(va);
}

__printf(2, 3) void mlog(unsigned int level, const char* fmt, ...)
{
	va_list va;
	struct message* msg;

	if (config->log.level < level)
		return;

	va_start(va, fmt);
	if (opmode == MASTER) {
		vlog(fmt, va);
	} else {
		msg = new_message(MT_LOGMSG);
		MB(msg, logmsg).msg = xvasprintf(fmt, va);
		mc_enqueue_message(&stdio_msgchan, msg);
	}
	va_end(va);
}

static void disconnect_remote(struct remote* rmt)
{
	pid_t pid;
	int status;

	/* Close fds and reset send & receive queues/buffers */
	mc_close(&rmt->msgchan);

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

/*
 * Reconnection time-interval computations are done scaled by this factor to
 * avoid potential overflows
 */
#define RECONNECT_INTERVAL_UNIT (500 * 1000) /* half a second */

static void reconnect_remote_cb(void* arg)
{
	struct remote* rmt = arg;

	rmt->reconnect_timer = NULL;
	setup_remote(rmt);
}

static void fail_remote(struct remote* rmt, const char* reason)
{
	uint64_t tmp, lshift, next_reconnect_delay;

	errlog("disconnecting remote '%s': %s\n", rmt->node.name, reason);
	disconnect_remote(rmt);
	rmt->failcount += 1;

	if (rmt->failcount > config->reconnect.max_tries) {
		errlog("remote '%s' exceeds failure limits, permfailing.\n",
		       rmt->node.name);
		rmt->state = CS_PERMFAILED;
		return;
	}

	rmt->state = CS_FAILED;

	/* 0.5s, 1s, 2s, 4s, 8s...capped at config->reconnect.max_interval */
	lshift = rmt->failcount - 1;
	if (lshift > (CHAR_BIT * sizeof(uint64_t) - 1))
		lshift = (CHAR_BIT * sizeof(uint64_t)) - 1;
	tmp = (1ULL << lshift);
	if (tmp > (config->reconnect.max_interval / RECONNECT_INTERVAL_UNIT))
		tmp = config->reconnect.max_interval / RECONNECT_INTERVAL_UNIT;

	next_reconnect_delay = tmp * RECONNECT_INTERVAL_UNIT;

	rmt->reconnect_timer = schedule_call(reconnect_remote_cb, rmt,
	                                     next_reconnect_delay);
}

static void enqueue_message(struct remote* rmt, struct message* msg)
{
	if (mc_enqueue_message(&rmt->msgchan, msg))
		fail_remote(rmt, "send backlog exceeded");
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
		"-oConnectTimeout=2",

		/* placeholders */
		NULL, /* -q */
		NULL, /* -E */
		NULL, /* logfile */
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

	if (config->log.level < LL_WARN)
		argv[nargs++] = "-q";

	if (config->log.file.type == LF_FILE) {
		argv[nargs++] = "-E";
		argv[nargs++] = config->log.file.path;
	} else if (config->log.file.type == LF_SYSLOG || config->log.file.type == LF_NONE) {
		/*
		 * TODO: fork a logger(1) and attach its stdin to ssh's stderr
		 * for the LF_SYSLOG case?
		 */
		argv[nargs++] = "-E";
		argv[nargs++] = "/dev/null";
	}

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

static void rmt_mc_read_cb(struct msgchan* mc, struct message* msg, void* arg)
{
	struct remote* rmt = arg;

	handle_message(rmt, msg);
}

static void rmt_mc_err_cb(struct msgchan* mc, void* arg)
{
	struct remote* rmt = arg;

	fail_remote(rmt, "msgchan error");
}

static void setup_remote(struct remote* rmt)
{
	int sockfds[2];
	struct message* setupmsg;
	int sndbuf_sz;

	info("initiating connection attempt to remote %s...\n", rmt->node.name);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockfds)) {
		perror("socketpair");
		exit(1);
	}

	/*
	 * If a remote goes offline, we want to detect it sooner rather than
	 * later (which happens via ssh getting backed up, thus allowing our
	 * send backlog to reach its limit), so we shrink our send-buffer size
	 * on the socket we'll be sending messages through.  Granted, ssh's
	 * network-facing socket probably still has a much larger send buffer,
	 * so the effectiveness of this is likely to be pretty limited, but we
	 * might as well try.
	 */
	sndbuf_sz = 1024;
	if (setsockopt(sockfds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf_sz,
	               sizeof(sndbuf_sz)))
		warn("setsockopt(SO_SNDBUF) failed: %s\n", strerror(errno));

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

		if (close(sockfds[1]))
			perror("close");

		exec_remote_shell(rmt);
	}

	set_fd_nonblock(sockfds[0], 1);
	set_fd_cloexec(sockfds[0], 1);

	mc_init(&rmt->msgchan, sockfds[0], sockfds[0], rmt_mc_read_cb,
	        rmt_mc_err_cb, rmt);

	if (close(sockfds[1]))
		perror("close");

	setupmsg = new_message(MT_SETUP);
	setupmsg->body.type = MT_SETUP;
	MB(setupmsg, setup).prot_vers = PROT_VERSION;
	MB(setupmsg, setup).loglevel = config->log.level;
	MB(setupmsg, setup).params.params_val = flatten_kvmap(rmt->params,
	                                                      &MB(setupmsg, setup).params.params_len);

	enqueue_message(rmt, setupmsg);
}

static struct node* find_node(const char* name)
{
	struct remote* rmt;

	if (!name || !strcmp(name, config->master.name))
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
			initerr("No such remote: '%s'\n", n->name);
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
		initerr("Warning: %s %s neighbor already specified\n",
		        ln->a.nr.node->name, dirnames[ln->a.dir]);
	ln->a.nr.node->neighbors[ln->a.dir] = ln->b.nr.node;

	if (ln->b.dir != NO_DIR) {
		if (ln->b.nr.node->neighbors[ln->b.dir])
			initerr("Warning: %s %s neighbor already specified\n",
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
			initerr("Warning: remote '%s' is not reachable\n",
			        rmt->node.name);

		num_neighbors = 0;
		for_each_direction (dir) {
			if (rmt->node.neighbors[dir])
				num_neighbors += 1;
		}

		if (!num_neighbors)
			initerr("Warning: remote '%s' has no neighbors\n",
			        rmt->node.name);
	}
}


static void transfer_clipboard(struct node* from, struct node* to)
{
	struct message* msg;

	if (is_master(from) && is_master(to)) {
		vinfo("switching from master to master??\n");
		return;
	}

	if (is_remote(from)) {
		msg = new_message(MT_GETCLIPBOARD);
		enqueue_message(from->remote, msg);
	} else if (is_remote(to)) {
		msg = new_message(MT_SETCLIPBOARD);
		MB(msg, setclipboard).text = get_clipboard_text();
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
			MB(msg, keyevent).pressrel = PR_RELEASE;
			MB(msg, keyevent).keycode = modkeys[i];
			enqueue_message(from->remote, msg);
		}
	}

	if (is_remote(to)) {
		for (i = 0; modkeys[i] != ET_null; i++) {
			msg = new_message(MT_KEYEVENT);
			MB(msg, keyevent).pressrel = PR_PRESS;
			MB(msg, keyevent).keycode = modkeys[i];
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

	MB(msg, keyevent).keycode = kc;
	MB(msg, keyevent).pressrel = pr;

	enqueue_message(rmt, msg);
}

void send_moverel(struct remote* rmt, int32_t dx, int32_t dy)
{
	struct message* msg;

	if (!rmt)
		return;

	msg = new_message(MT_MOVEREL);

	MB(msg, moverel).dx = dx;
	MB(msg, moverel).dy = dy;

	enqueue_message(rmt, msg);
}

void send_clickevent(struct remote* rmt, mousebutton_t button, pressrel_t pr)
{
	struct message* msg;

	if (!rmt)
		return;

	msg = new_message(MT_CLICKEVENT);

	MB(msg, clickevent).button = button;
	MB(msg, clickevent).pressrel = pr;

	enqueue_message(rmt, msg);
}

void send_setbrightness(struct remote* rmt, float f)
{
	struct message* msg;

	if (!rmt)
		return;

	msg = new_message(MT_SETBRIGHTNESS);

	MB(msg, setbrightness).brightness = f;

	enqueue_message(rmt, msg);
}

static void set_node_display_brightness(struct node* node, float f)
{
	if (is_master(node))
		set_display_brightness(f);
	else
		send_setbrightness(node->remote, f);
}

struct setbrightness_cb_args {
	struct node* node;
	float brightness;
};

static void set_brightness_cb(void* arg)
{
	struct setbrightness_cb_args* args = arg;

	/*
	 * There's a chance this can be called after a remote has been
	 * disconnected, in which case we need to not try to send the
	 * brightness-change message to avoid a use-after-free.
	 */
	if (!(is_remote(args->node) && args->node->remote->state != CS_CONNECTED))
		set_node_display_brightness(args->node, args->brightness);

	xfree(args);
}

static void schedule_brightness_change(struct node* node, float f, uint64_t delay)
{
	struct setbrightness_cb_args* args = xmalloc(sizeof(*args));

	args->node = node;
	args->brightness = f;

	schedule_call(set_brightness_cb, args, delay);
}

static void transition_brightness(struct node* node, float from, float to,
                                  uint64_t duration, int steps)
{
	int i;
	float frac, level;
	uint64_t delay;

	set_node_display_brightness(node, from);
	for (i = 1; i < steps; i++) {
		frac = (float)i / (float)steps;
		delay = (uint64_t)(frac * (float)duration);
		level = from + (frac * (to - from));
		schedule_brightness_change(node, level, delay);
	}
	schedule_brightness_change(node, to, duration);
}

static void indicate_switch(struct node* from, struct node* to)
{
	struct focus_hint* fh = &config->focus_hint;

	switch (fh->type) {
	case FH_NONE:
		break;

	case FH_DIM_INACTIVE:
		if (from && from != to)
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
		errlog("unknown focus_hint type %d\n", fh->type);
		break;
	}
}

static struct xypoint saved_master_mousepos;

/*
 * A special focus-switch for when the focused remote fails; in this case we
 * just revert focus directly to the master.
 */
static void focus_master(void)
{
	ungrab_inputs();
	set_mousepos(saved_master_mousepos);
	last_focused_node = focused_node;
	focused_node = &config->master;
	indicate_switch(NULL, &config->master);
}

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
		info("Remote %s not connected, can't focus\n", n->name);
		to = focused_node;
	} else {
		to = n;
	}

	from = focused_node;

	debug("focus switch: %s -> %s\n", from->name, to->name);

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

	last_focused_node = focused_node;
	focused_node = to;

	return 1;
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

static int run_command(const char* cmd, int must_succeed)
{
	int status = system(cmd);

	if (must_succeed)
		return status == -1 || WEXITSTATUS(status);
	else
		return 0;
}

/*
 * The environment variable used to indicate that we've re-execed ourselves
 * under a new ssh-agent.
 */
#define ENTHRALL_AGENT_ENV_VAR "__enthrall_private_agent__"

static void shutdown_master(void)
{
	struct remote* rmt;
	struct hotkey* hk;
	struct link* ln;

	while (config->remotes) {
		rmt = config->remotes;
		config->remotes = rmt->next;
		if (rmt->state == CS_CONNECTED || rmt->state == CS_SETTINGUP)
			disconnect_remote(rmt);
		free_remote(rmt);
	}

	while (config->hotkeys) {
		hk = config->hotkeys;
		config->hotkeys = hk->next;
		xfree(hk->key_string);
		xfree(hk);
	}

	while (config->topology) {
		ln = config->topology;
		config->topology = ln->next;
		xfree(ln);
	}

	clear_ssh_config(&config->ssh_defaults);
	xfree(config->master.name);

	platform_exit();

	/* If we re-execed under a private agent, unload keys & kill it now. */
	if (getenv(ENTHRALL_AGENT_ENV_VAR))
		run_command("ssh-add -D 2>/dev/null; ssh-agent -k >/dev/null", 0);

	if (config->log.file.type == LF_SYSLOG)
		closelog();
}

static int reconnect_remotes(void)
{
	struct remote* rmt;
	int count = 0;

	for_each_remote (rmt) {
		if (rmt->state == CS_CONNECTED)
			continue;

		if (rmt->reconnect_timer) {
			if (!cancel_call(rmt->reconnect_timer))
				warn("Failed to cancel reconnect_timer for remote %s\n",
				     rmt->node.name);
		}

		if (rmt->state == CS_SETTINGUP)
			disconnect_remote(rmt);

		rmt->failcount = 0;

		setup_remote(rmt);

		count += 1;
	}

	return count;
}

static void action_cb(hotkey_context_t ctx, void* arg)
{
	int count;
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
		case FT_PREVIOUS:
			focus_node(last_focused_node, modkeys, 1);
			break;
		default:
			errlog("bad focus-target type %u\n", a->target.type);
			break;
		}
		break;

	case AT_RECONNECT:
		count = reconnect_remotes();
		if (count)
			info("Attempting reconnection to %d remote%s\n", count,
			     count == 1 ? "" : "s");
		else
			info("All remotes are connected; nothing to do for reconnect.\n");
		break;

	case AT_QUIT:
		info("shutting down master on 'quit' action\n");
		xfree(modkeys);
		shutdown_master();
		exit(0);

	default:
		errlog("unknown action type %d\n", a->type);
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
		errlog("bad direction %d in edgeswitch_reposition()\n", dir);
		return;
	}

	if (focused_node->remote) {
		msg = new_message(MT_MOVEABS);
		MB(msg, moveabs).pt = pt;
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
		if (duration <= config->mouseswitch.window) {
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
				warn("out-of-sync edge event on %s ignored\n", node->name);
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

	switch (msg->body.type) {
	case MT_READY:
		if (rmt->state != CS_SETTINGUP) {
			fail_remote(rmt, "unexpected READY message");
			break;
		}
		rmt->state = CS_CONNECTED;
		rmt->failcount = 0;
		info("remote %s becomes ready.\n", rmt->node.name);
		vinfo("%s screen dimensions: %ux%u\n", rmt->node.name,
		      MB(msg, ready).screendim.x.max, MB(msg, ready).screendim.y.max);
		rmt->node.dimensions = MB(msg, ready).screendim;
		if (config->focus_hint.type == FH_DIM_INACTIVE)
			transition_brightness(&rmt->node, 1.0, config->focus_hint.brightness,
			                      config->focus_hint.duration,
			                      config->focus_hint.fade_steps);
		break;

	case MT_SETCLIPBOARD:
		set_clipboard_text(MB(msg, setclipboard).text);
		if (focused_node->remote) {
			resp = new_message(MT_SETCLIPBOARD);
			MB(resp, setclipboard).text = get_clipboard_text();
			enqueue_message(focused_node->remote, resp);
		}
		break;

	case MT_LOGMSG:
		logmsg = MB(msg, logmsg).msg;
		loglen = strlen(logmsg);
		/*
		 * Log-level filtering is done on remotes, so anything the
		 * master receives goes directly to the log.
		 */
		log_direct("%s: %s%s", rmt->node.name, logmsg,
		           logmsg[loglen-1] == '\n' ? "" : "\n");
		break;

	case MT_MOUSEPOS:
		check_edgeevents(&rmt->node, MB(msg, mousepos).pt);
		break;

	default:
		fail_remote(rmt, "unexpected message type");
		break;
	}
}

/*
 * On success, returns a pointer to a NULL-terminated array of strings of the
 * paths of the keys currently loaded in to the ssh-agent.  On failure
 * (e.g. no agent found) returns NULL, though this is distinct from returning
 * an empty list (which ssh-add still regards as failure).
 */
static char** get_agent_keylist(void)
{
	char keypath[PATH_MAX+1];
	/* Generous field lengths for keysize, fingerprint, keyfile, and keytype */
	char linebuf[8 + 48 + PATH_MAX + 16];
	int i, status, numpaths = 0;
	char** allpaths = NULL;
	FILE* listpipe = popen("ssh-add -l", "r");

	/*
	 * Dynamically constructing a format string to work around lack of
	 * "%ms" support on pre-POSIX.1-2008 systems...irony?
	 */
	char* fmtstr = xasprintf("%%*d %%*s %%%ds %%*s\n", sizeof(keypath)-1);

	while (!feof(listpipe)) {
		if (!fgets(linebuf, sizeof(linebuf), listpipe)
		    || sscanf(linebuf, fmtstr, keypath) != 1)
			continue;

		allpaths = xrealloc(allpaths, sizeof(*allpaths) * ++numpaths);
		allpaths[numpaths-1] = xstrdup(keypath);
	}

	xfree(fmtstr);

	allpaths = xrealloc(allpaths, sizeof(*allpaths) * (numpaths+1));
	allpaths[numpaths] = NULL;

	status = pclose(listpipe);
	if (status == -1) {
		perror("pclose");
		exit(1);
	}
	status = WEXITSTATUS(status);

	/*
	 * From ssh-agent(1): "Exit status is 0 on success, 1 if the specified
	 * command fails, and 2 if ssh-add is unable to contact the
	 * authentication agent."
	 */
	switch (status) {
	case 0:
		break;

	case 2:
		initerr("failed to retrieve key list from ssh-agent\n");
		for (i = 0; allpaths[i]; i++)
			xfree(allpaths[i]);
		xfree(allpaths);
		allpaths = NULL;
		break;

	default:
		if (numpaths != 0)
			initerr("'ssh-add -l' exited %d despite listing %d keys?\n",
			        status, numpaths);
		break;
	}

	return allpaths;
}

/*
 * Re-exec ourselves under our own "private" ssh-agent.  We do this, rather
 * than just ssh-add any needed keys to whatever agent we may have started
 * under, in order to avoid treading on the user's "global" session state (if
 * they want the enthrall keys in their session's agent they can add them
 * manually before starting it; if they *don't* want that we shouldn't
 * override that and insert the enthrall key(s) into their agent, so we
 * instead start our own and add them to it instead).
 */
static void ssh_agent_reexec(void)
{
	int i;
	char** argv = xmalloc((orig_argc + 2) * sizeof(*argv));

	setenv(ENTHRALL_AGENT_ENV_VAR, "1", 1);

	argv[0] = "ssh-agent";
	for (i = 0; i < orig_argc; i++)
		argv[i+1] = orig_argv[i];
	argv[orig_argc+1] = NULL;

	execvp("ssh-agent", argv);

	perror("ssh-agent");
	exit(1);
}

/* Add the given ssh key file to the current ssh-agent */
static void load_id(char* keyfile, char*** keylist)
{
	int i, numkeys, status;
	pid_t pid;
	char* argv[3] = { "ssh-add", keyfile, NULL, };

	/* Check if we've already loaded this key */
	for (i = 0; (*keylist)[i]; i++) {
		if (!strcmp(keyfile, (*keylist)[i]))
			return;
	}
	numkeys = i;

	pid = fork();
	if (!pid) {
		execvp("ssh-add", argv);
		perror("ssh-add");
		exit(1);
	}

	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		exit(1);
	}

	if (WEXITSTATUS(status)) {
		initerr("failed to add ssh key %s\n", keyfile);
		exit(1);
	} else {
		/* Add keyfile to the list of loaded keys */
		*keylist = xrealloc(*keylist, sizeof(**keylist) * (numkeys+2));
		(*keylist)[numkeys] = xstrdup(keyfile);
		(*keylist)[numkeys+1] = NULL;
	}
}

/* Ensure any ssh keys we'll be needing are loaded into an ssh-agent */
static void ssh_pubkey_setup(void)
{
	int i;
	struct remote* rmt;
	char** agentkeys = get_agent_keylist();

	if (!agentkeys) {
		if (!getenv(ENTHRALL_AGENT_ENV_VAR)) {
			initerr("re-execing under private ssh-agent\n");
			ssh_agent_reexec();
		} else {
			initerr("get_agent_keylist() failed under private ssh-agent??\n");
			exit(1);
		}
	}

	if (config->ssh_defaults.identityfile)
		load_id(config->ssh_defaults.identityfile, &agentkeys);

	for_each_remote (rmt) {
		if (rmt->sshcfg.identityfile)
			load_id(rmt->sshcfg.identityfile, &agentkeys);
	}

	if (!agentkeys[0]) {
		if (run_command("ssh-add", 1)) {
			initerr("failed to add keys to ssh agent\n");
			exit(1);
		}
	}

	for (i = 0; agentkeys[i]; i++)
		xfree(agentkeys[i]);
	xfree(agentkeys);
}

static void usage(FILE* out)
{
	fprintf(out, "Usage: %s CONFIGFILE\n", progname);
}

int main(int argc, char** argv)
{
	int opt;
	struct remote* rmt;
	FILE* cfgfile;
	struct stat st;

	static const struct option options[] = {
		{ "help", no_argument, NULL, 'h', },
		{ NULL, 0, NULL, 0, },
	};

	orig_argc = argc;
	orig_argv = argv;

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
			initerr("Unrecognized option: %c\n", opt);
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
		initerr("excess arguments\n");
		exit(1);
	}

	cfgfile = fopen(argv[0], "r");
	if (!cfgfile) {
		initerr("%s: %s\n", argv[0], strerror(errno));
		exit(1);
	}

	if (fstat(fileno(cfgfile), &st)) {
		initerr("fstat(%s): %s\n", argv[0], strerror(errno));
		exit(1);
	}

	if (st.st_uid != getuid()) {
		initerr("Error: bad ownership on %s\n", argv[0]);
		exit(1);
	}

	if (st.st_mode & (S_IWGRP|S_IWOTH)) {
		initerr("Error: bad permissions on %s (writable by others)\n", argv[0]);
		exit(1);
	}

	if (parse_cfg(cfgfile, config))
		exit(1);
	fclose(cfgfile);

	ssh_pubkey_setup();

	init_logfile();

	if (platform_init(NULL, mousepos_cb)) {
		initerr("platform_init failed\n");
		exit(1);
	}

	if (!config->master.name)
		config->master.name = xstrdup("<master>");
	get_screen_dimensions(&config->master.dimensions);

	apply_topology();
	check_remotes();
	bind_hotkeys();

	focused_node = &config->master;
	last_focused_node = focused_node;

	for_each_remote (rmt)
		setup_remote(rmt);

	run_event_loop();

	return 0;
}
