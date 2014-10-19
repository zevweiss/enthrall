#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

#include "types.h"
#include "misc.h"
#include "proto.h"
#include "platform.h"
#include "keycodes.h"

#include "cfg-parse.tab.h"

struct config* config;

struct remote* active_remote = NULL;

char* default_remote_command;

static int platform_event_fd;

enum {
	MASTER,
	REMOTE,
} opmode;

void elog(const char* fmt, ...)
{
	va_list va;
	struct message msg;

	va_start(va, fmt);
	if (opmode == MASTER) {
		vfprintf(stderr, fmt, va);
	} else {
		msg.type = MT_LOGMSG;
		msg.extra.buf = xvasprintf(fmt, va);
		msg.extra.len = strlen(msg.extra.buf);
		send_message(STDOUT_FILENO, &msg);
		xfree(msg.extra.buf);
	}
	va_end(va);
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

static void handle_message(void)
{
	struct message msg, resp;

	if (receive_message(STDIN_FILENO, &msg)) {
		elog("receive_message() failed\n");
		exit(1);
	}

	switch (msg.type) {
	case MT_SHUTDOWN:
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		exit(0);

	case MT_MOVEREL:
		move_mousepos(msg.moverel.dx, msg.moverel.dy);
		break;

	case MT_CLICKEVENT:
		do_clickevent(msg.clickevent.button, msg.clickevent.pressrel);
		break;

	case MT_KEYEVENT:
		do_keyevent(msg.keyevent.keycode, msg.keyevent.pressrel);
		break;

	case MT_GETCLIPBOARD:
		resp.type = MT_SETCLIPBOARD;
		resp.extra.buf = get_clipboard_text();
		resp.extra.len = strlen(resp.extra.buf);
		send_message(STDOUT_FILENO, &resp);
		xfree(resp.extra.buf);
		break;

	case MT_SETCLIPBOARD:
		set_clipboard_from_buf(msg.extra.buf, msg.extra.len);
		break;

	default:
		elog("unhandled message type: %u\n", msg.type);
		exit(1);
	}

	xfree(msg.extra.buf);
}

static void server_mode(void)
{
	struct message readymsg = {
		.type = MT_READY,
		{ .ready = { .prot_vers = PROT_VERSION, }, },
		.extra.len = 0,
	};

	if (send_message(STDOUT_FILENO, &readymsg)) {
		/*
		 * Seems unlikely that sending a log message is going to work
		 * if sending the ready message didn't, but I guess we might
		 * as well try...
		 */
		elog("failed to send ready message\n");
		exit(1);
	}

	for (;;)
		handle_message();
}

static void exec_remote_shell(const struct remote* rmt)
{
	char* remote_shell = config->remote_shell ? config->remote_shell : "ssh";
	char portbuf[32];
	char* argv[] = {
		remote_shell,
		"-oBatchMode=yes",

		/* placeholders */
		NULL, /* -b */
		NULL, /* bind address */
		NULL, /* -p */
		NULL, /* port */
		NULL, /* -l */
		NULL, /* username */
		NULL, /* hostname */
		NULL, /* remote command */

		NULL, /* argv terminator */
	};
	int nargs = 2;

	if (config->bind_address) {
		argv[nargs++] = "-b";
		argv[nargs++] = config->bind_address;
	}

	if (rmt->port) {
		snprintf(portbuf, sizeof(portbuf), "%d", rmt->port);
		argv[nargs++] = "-p";
		argv[nargs++] = portbuf;
	}

	if (rmt->username) {
		argv[nargs++] = "-l";
		argv[nargs++] = rmt->username;
	}

	argv[nargs++] = rmt->hostname;

	argv[nargs++] = rmt->remotecmd ? rmt->remotecmd : default_remote_command;

	assert(nargs < ARR_LEN(argv));

	execvp(remote_shell, argv);
	perror("execvp");
	exit(1);
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

static void setup_remote(struct remote* rmt)
{
	int sockfds[2];

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
	} else {
		rmt->sock = sockfds[0];

		set_fd_nonblock(rmt->sock, 1);

		if (close(sockfds[1]))
			perror("close");
	}
}

void disconnect_remote(struct remote* rmt, connstate_t state)
{
	pid_t pid;
	int status;

	close(rmt->sock);
	rmt->sock = -1;

	if (rmt->sshpid > 0 && kill(rmt->sshpid, SIGTERM) && errno != ESRCH)
		perror("failed to kill remote shell");

	pid = waitpid(rmt->sshpid, &status, 0);
	if (pid != rmt->sshpid)
		perror("wait() on remote shell");

	rmt->sshpid = -1;

	rmt->state = state;

	/* FIXME: if (rmt == active_remote) { ... } */
}

static void fail_remote(struct remote* rmt, const char* reason)
{
	elog("disconnecting remote '%s': %s\n", rmt->alias, reason);
	disconnect_remote(rmt, CS_FAILED);
}

#define MAX_SEND_BACKLOG 512

static void enqueue_message(struct remote* rmt, struct message* msg)
{
	msg->next = NULL;
	if (rmt->sendqueue.tail)
		rmt->sendqueue.tail->next = msg;
	rmt->sendqueue.tail = msg;
	if (!rmt->sendqueue.head)
		rmt->sendqueue.head = msg;
	rmt->sendqueue.num_queued += 1;

	if (rmt->sendqueue.num_queued > MAX_SEND_BACKLOG)
		fail_remote(rmt, "send backlog exceeded");
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
	struct action* a = arg;
	keycode_t* modkeys = get_hotkey_modifiers(ctx);

	switch (a->type) {
	case AT_SWITCH:
		switch_to_neighbor(a->dir, modkeys);
		break;

	case AT_SWITCHTO:
		switch_to_node(&a->node, modkeys);
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

	status = fill_msgbuf(rmt->sock, &rmt->recv_msgbuf);
	if (!status)
		return;

	if (status < 0) {
		fail_remote(rmt, "failed to read valid message");
		return;
	}

	parse_message(&rmt->recv_msgbuf, &msg);

	switch (msg.type) {
	case MT_READY:
		if (rmt->state != CS_SETTINGUP) {
			fail_remote(rmt, "unexpected READY message");
			break;
		}
		if (msg.ready.prot_vers != PROT_VERSION) {
			fail_remote(rmt, "unsupported protocol version");
			break;
		}
		rmt->state = CS_CONNECTED;
		elog("Remote '%s' becomes ready...\n", rmt->alias);
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
	struct message* msg;

	if (!rmt->send_msgbuf.msgbuf) {
		assert(rmt->sendqueue.head);
		msg = rmt->sendqueue.head;
		rmt->sendqueue.head = msg->next;
		if (!msg->next)
			rmt->sendqueue.tail = NULL;
		rmt->sendqueue.num_queued -= 1;
		unparse_message(msg, &rmt->send_msgbuf);
		xfree(msg->extra.buf);
		xfree(msg);
	}

	status = drain_msgbuf(rmt->sock, &rmt->send_msgbuf);

	if (status < 0)
		fail_remote(rmt, "failed to send message");
}

static inline int have_outbound_data(const struct remote* rmt)
{
	return rmt->send_msgbuf.msgbuf || rmt->sendqueue.head;
}

static inline void fdset_add(int fd, fd_set* set, int* nfds)
{
	FD_SET(fd, set);
	if (fd >= *nfds)
		*nfds = fd + 1;
}

static void handle_fds(void)
{
	int status, nfds = 0;
	fd_set rfds, wfds;
	struct remote* rmt;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		fdset_add(rmt->sock, &rfds, &nfds);
		if (have_outbound_data(rmt))
			fdset_add(rmt->sock, &wfds, &nfds);
	}

	fdset_add(platform_event_fd, &rfds, &nfds);

	status = select(nfds, &rfds, &wfds, NULL, NULL);
	if (status < 0) {
		perror("select");
		exit(1);
	}

	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		if (FD_ISSET(rmt->sock, &rfds))
			read_rmtdata(rmt);
		if (FD_ISSET(rmt->sock, &wfds))
			write_rmtdata(rmt);
	}

	if (FD_ISSET(platform_event_fd, &rfds))
		process_events();
}

int main(int argc, char** argv)
{
	int opt, status;
	pid_t pid;
	struct config cfg;
	struct remote* rmt;

	default_remote_command = strrchr(argv[0], '/') + 1;
	if (!default_remote_command)
		default_remote_command = argv[0];

	while ((opt = getopt(argc, argv, "")) != -1) {
		switch (opt) {

		default:
			elog("Unrecognized option: %c\n", opt);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (!argc)
		opmode = REMOTE;
	else if (argc == 1)
		opmode = MASTER;
	else {
		elog("excess arguments\n");
		exit(1);
	}

	platform_event_fd = platform_init();
	if (platform_event_fd < 0) {
		elog("platform_init failed\n");
		exit(1);
	}

	if (opmode == REMOTE) {
		server_mode();
	}

	memset(&cfg, 0, sizeof(cfg));
	if (parse_cfg(argv[0], &cfg))
		exit(1);
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
