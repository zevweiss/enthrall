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

#include "types.h"
#include "misc.h"
#include "proto.h"
#include "platform.h"
#include "keycodes.h"

#include "cfg-parse.tab.h"

struct config* config;

struct remote* active_remote = NULL;

char* progname;

static int platform_event_fd;

enum {
	MASTER,
	REMOTE,
} opmode;

/* msgchan attached to stdin & stdout for use in remote mode */
struct msgchan stdio_msgchan;

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

static inline void fdset_add(int fd, fd_set* set, int* nfds)
{
	FD_SET(fd, set);
	if (fd >= *nfds)
		*nfds = fd + 1;
}

static void set_clipboard_from_buf(const void* buf, size_t len)
{
	char* tmp;

	/*
	 * extra intermediate malloc()ed area just to tack on the NUL
	 * terminator here is a bit inefficient...
	 */
	tmp = xmalloc(len + 1);
	memcpy(tmp, buf, len);
	tmp[len] = '\0';
	set_clipboard_text(tmp);
	xfree(tmp);
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

	/* FIXME: if (rmt == active_remote) { ... } */
}

#define MAX_RECONNECT_INTERVAL (30 * 1000 * 1000)
#define MAX_RECONNECT_ATTEMPTS 10

static void fail_remote(struct remote* rmt, const char* reason)
{
	uint64_t tmp, lshift;

	elog("disconnecting remote '%s': %s\n", rmt->alias, reason);
	disconnect_remote(rmt);
	rmt->failcount += 1;

	if (rmt->failcount > MAX_RECONNECT_ATTEMPTS) {
		elog("remote '%s' exceeds failure limits, permfailing.\n", rmt->alias);
		rmt->state = CS_PERMFAILED;
		return;
	}

	rmt->state = CS_FAILED;

	/* 0.5s, 1s, 2s, 4s, 8s...capped at MAX_RECONNECT_INTERVAL */
	lshift = rmt->failcount - 1;
	if (lshift > (CHAR_BIT * sizeof(uint64_t) - 1))
		lshift = (CHAR_BIT * sizeof(uint64_t)) - 1;
	tmp = (1ULL << rmt->failcount) * 500 * 1000;
	if (tmp > MAX_RECONNECT_INTERVAL)
		tmp = MAX_RECONNECT_INTERVAL;

	rmt->next_reconnect_time = get_microtime() + tmp;
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

		/* placeholders */
		NULL, /* -b */
		NULL, /* bind address */
		NULL, /* -oIdentitiesOnly */
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
	setupmsg->extra.buf = NULL;
	setupmsg->extra.len = 0;

	enqueue_message(rmt, setupmsg);
}

static void handle_remote_message(const struct message* msg)
{
	struct message* resp;

	switch (msg->type) {
	case MT_SHUTDOWN:
		mc_close(&stdio_msgchan);
		exit(0);

	case MT_MOVEREL:
		move_mousepos(msg->moverel.dx, msg->moverel.dy);
		break;

	case MT_CLICKEVENT:
		do_clickevent(msg->clickevent.button, msg->clickevent.pressrel);
		break;

	case MT_KEYEVENT:
		do_keyevent(msg->keyevent.keycode, msg->keyevent.pressrel);
		break;

	case MT_GETCLIPBOARD:
		resp = new_message(MT_SETCLIPBOARD);
		resp->extra.buf = get_clipboard_text();
		resp->extra.len = strlen(resp->extra.buf);
		mc_enqueue_message(&stdio_msgchan, resp);
		break;

	case MT_SETCLIPBOARD:
		set_clipboard_from_buf(msg->extra.buf, msg->extra.len);
		break;

	default:
		elog("unhandled message type: %u\n", msg->type);
		exit(1);
	}
}

static void handle_remote_fds(void)
{
	int status, nfds = 0;
	fd_set rfds, wfds;
	struct message msg;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	fdset_add(stdio_msgchan.recv_fd, &rfds, &nfds);

	if (mc_have_outbound_data(&stdio_msgchan))
		fdset_add(stdio_msgchan.send_fd, &wfds, &nfds);

	if (platform_event_fd >= 0)
		fdset_add(platform_event_fd, &rfds, &nfds);

	status = select(nfds, &rfds, &wfds, NULL, NULL);
	if (status < 0) {
		elog("select() failed: %s\n", strerror(errno));
		exit(1);
	}

	if (FD_ISSET(stdio_msgchan.send_fd, &wfds)) {
		status = send_message(&stdio_msgchan);
		if (status < 0)
			exit(1);
		else
			assert(status > 0);
	}

	if (platform_event_fd >= 0 && FD_ISSET(platform_event_fd, &rfds))
		process_events();

	if (FD_ISSET(stdio_msgchan.recv_fd, &rfds)) {
		status = recv_message(&stdio_msgchan, &msg);
		if (status < 0) {
			elog("failed to receive valid message\n");
			exit(1);
		} else if (status > 0) {
			handle_remote_message(&msg);
			if (msg.extra.len)
				xfree(msg.extra.buf);
		}
	}
}

static void run_remote(void)
{
	struct message setupmsg;
	struct message* readymsg;

	/*
	 * Seems unlikely that sending log messages is going to work if some
	 * of these early setup steps failed, but I guess we might as well
	 * try...
	 */

	if (read_message(STDIN_FILENO, &setupmsg)) {
		elog("failed to receive setup message\n");
		exit(1);
	}

	if (setupmsg.type != MT_SETUP) {
		elog("unexpected message type %u instead of SETUP\n", setupmsg.type);
		exit(1);
	}

	if (setupmsg.setup.prot_vers != PROT_VERSION) {
		elog("unsupported protocol version %d\n", setupmsg.setup.prot_vers);
		exit(1);
	}

	if (platform_init(&platform_event_fd) < 0) {
		elog("platform_init() failed\n");
		exit(1);
	}

	set_fd_nonblock(STDIN_FILENO, 1);
	set_fd_nonblock(STDOUT_FILENO, 1);

	mc_init(&stdio_msgchan, STDOUT_FILENO, STDIN_FILENO);

	readymsg = new_message(MT_READY);
	mc_enqueue_message(&stdio_msgchan, readymsg);

	for (;;)
		handle_remote_fds();
}

static struct remote* find_remote(const char* name)
{
	struct remote* rmt;

	/* First search by alias */
	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		if (!strcmp(name, rmt->alias))
			return rmt;
	}

	/* if that fails, try hostnames */
	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		if (!strcmp(name, rmt->hostname))
			return rmt;
	}

	return NULL;
}

static void resolve_noderef(struct noderef* n)
{
	struct remote* rmt;
	if (n->type == NT_REMOTE_TMPNAME) {
		rmt = find_remote(n->name);
		if (!rmt) {
			elog("No such remote: '%s'\n", n->name);
			exit(1);
		}
		n->type = NT_REMOTE;
		n->node = rmt;
	}
}

static void mark_reachable(struct noderef* n)
{
	int seen;
	direction_t dir;
	struct remote* rmt;

	switch (n->type) {
	case NT_REMOTE_TMPNAME:
		resolve_noderef(n);
		/* fallthrough */
	case NT_REMOTE:
		rmt = n->node;
		break;
	default:
		return;
	}

	seen = rmt->reachable;

	rmt->reachable = 1;

	if (!seen) {
		for_each_direction (dir)
			mark_reachable(&rmt->neighbors[dir]);
	}
}

static void check_remotes(void)
{
	direction_t dir;
	struct remote* rmt;
	int num_neighbors;

	for_each_direction (dir)
		mark_reachable(&config->neighbors[dir]);

	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		if (!rmt->reachable)
			elog("Warning: remote '%s' is not reachable\n", rmt->alias);

		num_neighbors = 0;
		for_each_direction (dir) {
			if (rmt->neighbors[dir].type != NT_NONE)
				num_neighbors += 1;
		}

		if (!num_neighbors)
			elog("Warning: remote '%s' has no neighbors\n", rmt->alias);
	}
}


void transfer_clipboard(struct remote* from, struct remote* to)
{
	struct message* msg;

	if (!from && !to) {
		elog("switching from master to master??\n");
		return;
	}

	if (from) {
		msg = new_message(MT_GETCLIPBOARD);
		enqueue_message(from, msg);
	} else if (to) {
		msg = new_message(MT_SETCLIPBOARD);
		msg->extra.buf = get_clipboard_text();
		msg->extra.len = strlen(msg->extra.buf);
		assert(msg->extra.len <= UINT32_MAX);
		enqueue_message(to, msg);
	}
}

void transfer_modifiers(struct remote* from, struct remote* to, const keycode_t* modkeys)
{
	int i;
	struct message* msg;

	if (from) {
		for (i = 0; modkeys[i] != ET_null; i++) {
			msg = new_message(MT_KEYEVENT);
			msg->keyevent.pressrel = PR_RELEASE;
			msg->keyevent.keycode = modkeys[i];
			enqueue_message(from, msg);
		}
	}

	if (to) {
		for (i = 0; modkeys[i] != ET_null; i++) {
			msg = new_message(MT_KEYEVENT);
			msg->keyevent.pressrel = PR_PRESS;
			msg->keyevent.keycode = modkeys[i];
			enqueue_message(to, msg);
		}
	}
}

void send_keyevent(keycode_t kc, pressrel_t pr)
{
	struct message* msg;

	if (!active_remote)
		return;

	msg = new_message(MT_KEYEVENT);

	msg->keyevent.keycode = kc;
	msg->keyevent.pressrel = pr;

	enqueue_message(active_remote, msg);
}

void send_moverel(int32_t dx, int32_t dy)
{
	struct message* msg;

	if (!active_remote)
		return;

	msg = new_message(MT_MOVEREL);

	msg->moverel.dx = dx;
	msg->moverel.dy = dy;

	enqueue_message(active_remote, msg);
}

void send_clickevent(mousebutton_t button, pressrel_t pr)
{
	struct message* msg;

	if (!active_remote)
		return;

	msg = new_message(MT_CLICKEVENT);

	msg->clickevent.button = button;
	msg->clickevent.pressrel = pr;

	enqueue_message(active_remote, msg);
}

static struct xypoint saved_master_mousepos;

static void switch_to_node(struct noderef* n, keycode_t* modkeys)
{
	struct remote* switch_to;

	switch (n->type) {
	case NT_NONE:
		return;

	case NT_MASTER:
		switch_to = NULL;
		break;

	case NT_REMOTE:
		switch_to = n->node;
		if (switch_to->state != CS_CONNECTED) {
			elog("remote '%s' not connected, can't switch to\n",
			     switch_to->alias);
			return;
		}
		break;

	default:
		elog("unexpected neighbor type %d\n", n->type);
		return;
	}

	if (switch_to == active_remote)
		return;

	if (active_remote && !switch_to) {
		ungrab_inputs();
		set_mousepos(saved_master_mousepos);
	} else if (!active_remote && switch_to) {
		saved_master_mousepos = get_mousepos();
		grab_inputs();
	}

	if (switch_to)
		set_mousepos(screen_center);

	transfer_clipboard(active_remote, switch_to);
	transfer_modifiers(active_remote, switch_to, modkeys);

	active_remote = switch_to;
}

static void switch_to_neighbor(direction_t dir, keycode_t* modkeys)
{
	struct noderef* n = &(active_remote ? active_remote->neighbors : config->neighbors)[dir];
	switch_to_node(n, modkeys);
}

static void action_cb(hotkey_context_t ctx, void* arg)
{
	struct remote* rmt;
	uint64_t now_us;
	struct action* a = arg;
	keycode_t* modkeys = get_hotkey_modifiers(ctx);

	switch (a->type) {
	case AT_SWITCH:
		switch_to_neighbor(a->dir, modkeys);
		break;

	case AT_SWITCHTO:
		switch_to_node(&a->node, modkeys);
		break;

	case AT_RECONNECT:
		now_us = get_microtime();
		for (rmt = config->remotes; rmt; rmt = rmt->next) {
			if (rmt->state == CS_PERMFAILED)
				rmt->state = CS_FAILED;
			rmt->failcount = 0;
			rmt->next_reconnect_time = now_us;
		}
		break;

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
		if (k->action.type == AT_SWITCHTO)
			resolve_noderef(&k->action.node);
		if (bind_hotkey(k->key_string, action_cb, &k->action))
			elog("Failed to bind hotkey %s\n", k->key_string);
	}
}

static void read_rmtdata(struct remote* rmt)
{
	int status, loglen;
	char* logmsg;
	struct message msg;
	struct message* msg2;

	status = recv_message(&rmt->msgchan, &msg);
	if (!status)
		return;

	if (status < 0) {
		fail_remote(rmt, "failed to receive valid message");
		return;
	}

	switch (msg.type) {
	case MT_READY:
		if (rmt->state != CS_SETTINGUP) {
			fail_remote(rmt, "unexpected READY message");
			break;
		}
		rmt->state = CS_CONNECTED;
		rmt->failcount = 0;
		elog("remote '%s' becomes ready...\n", rmt->alias);
		break;

	case MT_SETCLIPBOARD:
		if (rmt->state != CS_CONNECTED) {
			elog("got unexpected SETCLIPBOARD from non-connected "
			     "remote '%s', ignoring.\n", rmt->alias);
			break;
		}
		set_clipboard_from_buf(msg.extra.buf, msg.extra.len);
		if (active_remote) {
			msg2 = new_message(MT_SETCLIPBOARD);
			msg2->extra.buf = get_clipboard_text();
			msg2->extra.len = strlen(msg2->extra.buf);
			enqueue_message(active_remote, msg2);
		}
		break;

	case MT_LOGMSG:
		loglen = msg.extra.len > INT_MAX ? INT_MAX : msg.extra.len;
		logmsg = msg.extra.buf;
		elog("%s: %.*s%s", rmt->alias, loglen, logmsg,
		     logmsg[msg.extra.len-1] == '\n' ? "" : "\n");
		break;

	default:
		fail_remote(rmt, "unexpected message type");
		break;
	}

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

static void handle_fds(void)
{
	int status, nfds = 0;
	fd_set rfds, wfds;
	struct remote* rmt;
	struct timeval recon_wait;
	struct timeval* sel_wait;
	uint64_t maxwait_us, now_us, next_reconnect_time = UINT64_MAX;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	now_us = get_microtime();

	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		if (rmt->state == CS_FAILED) {
			if (rmt->next_reconnect_time < now_us)
				setup_remote(rmt);
			else if (rmt->next_reconnect_time < next_reconnect_time)
				next_reconnect_time = rmt->next_reconnect_time;
		}

		if (rmt->state == CS_CONNECTED || rmt->state == CS_SETTINGUP) {
			fdset_add(rmt->msgchan.recv_fd, &rfds, &nfds);
			if (have_outbound_data(rmt))
				fdset_add(rmt->msgchan.send_fd, &wfds, &nfds);
		}
	}

	fdset_add(platform_event_fd, &rfds, &nfds);

	if (next_reconnect_time != UINT64_MAX) {
		maxwait_us = next_reconnect_time - now_us;
		recon_wait.tv_sec = maxwait_us / 1000000;
		recon_wait.tv_usec = maxwait_us % 1000000;
		sel_wait = &recon_wait;
	} else {
		sel_wait = NULL;
	}

	status = select(nfds, &rfds, &wfds, NULL, sel_wait);
	if (status < 0) {
		perror("select");
		exit(1);
	}

	now_us = get_microtime();

	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		if (rmt->state == CS_FAILED && rmt->next_reconnect_time < now_us) {
			setup_remote(rmt);
		} else {
			if (FD_ISSET(rmt->msgchan.recv_fd, &rfds))
				read_rmtdata(rmt);
			if (FD_ISSET(rmt->msgchan.send_fd, &wfds))
				write_rmtdata(rmt);
		}
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
	int opt, status;
	pid_t pid;
	struct config cfg;
	struct remote* rmt;
	FILE* cfgfile;
	struct stat st;

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
	} else if (argc == 1) {
		opmode = MASTER;
	} else {
		elog("excess arguments\n");
		exit(1);
	}

	if (opmode == REMOTE) {
		run_remote();
	}

	if (platform_init(&platform_event_fd)) {
		elog("platform_init failed\n");
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

	check_remotes();
	bind_hotkeys();

	for (rmt = config->remotes; rmt; rmt = rmt->next)
		setup_remote(rmt);

	for (;;)
		handle_fds();

	for (rmt = config->remotes; rmt; rmt = rmt->next)
		pid = wait(&status);

	(void)pid;

	platform_exit();

	return 0;
}
