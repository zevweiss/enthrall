#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/fcntl.h>

#include "types.h"
#include "misc.h"
#include "proto.h"
#include "platform.h"

#include "cfg-parse.tab.h"

#define TODO() do { fprintf(stderr, "%s:%d: TODO\n", __FILE__, __LINE__); abort(); } while (0)

struct config* config;

struct remote* active_remote = NULL;

char* default_remote_command;

static void handle_message(void)
{
	struct message msg;

	if (receive_message(STDIN_FILENO, &msg))
		TODO();

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

	default:
		TODO();
	}
}

static void server_mode(void)
{
	int errfd;
	struct message readymsg = {
		.type = MT_READY,
		{ .ready = { .prot_vers = PROT_VERSION, }, },
	};

	fclose(stderr);
	if ((errfd = open("/tmp/hench.err", O_WRONLY|O_CREAT|O_TRUNC, 0644)) < 0
	    || dup2(errfd, STDERR_FILENO) < 0
	    || !(stderr = fdopen(errfd, "w")))
		abort();

	if (send_message(STDOUT_FILENO, &readymsg))
		TODO();

	for (;;)
		handle_message();
}

static void exec_remote_shell(const struct remote* rmt)
{
	char portbuf[32];
	char* argv[] = {
		config->remote_shell,
		"-oPasswordAuthentication=no",
		"-oNumberOfPasswordPrompts=0",

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
	int nargs = 3;

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

	execvp(config->remote_shell, argv);
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

static void mark_reachable(struct neighbor* n)
{
	int seen;
	direction_t dir;
	struct remote* rmt;

	switch (n->type) {
	case NT_REMOTE:
		rmt = n->node;
		break;
	case NT_REMOTE_TMPNAME:
		rmt = find_remote(n->name);
		if (!rmt) {
			fprintf(stderr, "No such remote: '%s'\n", n->name);
			exit(1);
		}
		n->type = NT_REMOTE;
		n->node = rmt;
		break;
	default:
		return;
	}

	seen = rmt->reachable;

	rmt->reachable = 1;

	if (!seen) {
		for (dir = 0; dir < NUM_DIRECTIONS; dir++)
			mark_reachable(&rmt->neighbors[dir]);
	}
}

static void check_remotes(void)
{
	direction_t dir;
	struct remote* rmt;

	for (dir = 0; dir < NUM_DIRECTIONS; dir++)
		mark_reachable(&config->neighbors[dir]);

	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		if (!rmt->reachable)
			fprintf(stderr, "Warning: remote '%s' is not reachable\n", rmt->alias);
	}
}

static void setup_remote(struct remote* rmt)
{
	int send_pipe[2], recv_pipe[2];

	if (pipe(send_pipe) || pipe(recv_pipe)) {
		perror("pipe");
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
		if (dup2(send_pipe[0], STDIN_FILENO) < 0
		    || dup2(recv_pipe[1], STDOUT_FILENO) < 0) {
			perror("dup2");
			exit(1);
		}

		if (close(send_pipe[1]) || close(recv_pipe[0]))
			perror("close");

		exec_remote_shell(rmt);
	} else {
		rmt->send_fd = send_pipe[1];
		rmt->recv_fd = recv_pipe[0];

		if (close(send_pipe[0]) || close(recv_pipe[1]))
			perror("close");
	}
}

static void disconnect_remote(struct remote* rmt, connstate_t state)
{
	pid_t pid;
	int status;

	if (state == CS_FAILED)
		fprintf(stderr, "disconnecting failed remote '%s'\n", rmt->alias);

	close(rmt->recv_fd);
	rmt->recv_fd = -1;

	close(rmt->send_fd);
	rmt->send_fd = -1;

	if (rmt->sshpid > 0 && kill(rmt->sshpid, SIGTERM) && errno != ESRCH)
		perror("failed to kill ssh");

	pid = waitpid(rmt->sshpid, &status, 0);
	if (pid != rmt->sshpid)
		perror("wait() on ssh");

	rmt->sshpid = -1;

	rmt->state = state;
}

static void handle_ready(struct remote* rmt)
{
	struct message msg;
	if (receive_message(rmt->recv_fd, &msg)) {
		disconnect_remote(rmt, CS_FAILED);
		return;
	}

	if (msg.type != MT_READY || msg.ready.prot_vers != PROT_VERSION) {
		disconnect_remote(rmt, CS_FAILED);
		return;
	}

	rmt->state = CS_CONNECTED;

	fprintf(stderr, "Remote %s becomes ready...\n", rmt->alias);
}

static void handle_command(void)
{
	char cmdbuf[256];
	char namebuf[256];
	struct message msg;
	struct remote* rmt;

	fgets(cmdbuf, sizeof(cmdbuf), stdin);

	if (sscanf(cmdbuf, "s %256s\n", namebuf) == 1) {
		if ((rmt = find_remote(namebuf))) {
			active_remote = rmt;
			printf("selected %s as active remote, entering remote mode...\n", namebuf);
			remote_mode();
		} else {
			fprintf(stderr, "unknown remote: %s\n", namebuf);
		}
	} else if (sscanf(cmdbuf, "r %256s\n", namebuf) == 1) {
		if ((rmt = find_remote(namebuf)))
			setup_remote(rmt);
		else
			fprintf(stderr, "unknown remote: %s\n", namebuf);
	} else if (sscanf(cmdbuf, "m %d %d\n", &msg.moverel.dx, &msg.moverel.dy) == 2) {
		msg.type = MT_MOVEREL;
		if (!active_remote)
			fprintf(stderr, "no remote selected\n");
		else if (send_message(active_remote->send_fd, &msg))
			TODO();
		else
			printf("sent MOVEREL(%d, %d)\n", msg.moverel.dx, msg.moverel.dy);
	} else if (!strcmp(cmdbuf, "q\n")) {
		msg.type = MT_SHUTDOWN;
		if (!active_remote)
			fprintf(stderr, "no remote selected\n");
		else if (send_message(active_remote->send_fd, &msg))
			TODO();
		else {
			printf("sent SHUTDOWN\n");
			disconnect_remote(active_remote, CS_DISCONNECTED);
		}
	} else {
		fprintf(stderr, "unrecognized command: %s\n", cmdbuf);
	}
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
		switch (rmt->state) {
		case CS_SETTINGUP:
			fdset_add(rmt->recv_fd, &rfds, &nfds);
			break;

		default:
			break;
		}
	}

	fdset_add(STDIN_FILENO, &rfds, &nfds);

	status = select(nfds, &rfds, &wfds, NULL, NULL);
	if (status < 0) {
		perror("select");
		exit(1);
	}

	for (rmt = config->remotes; rmt; rmt = rmt->next) {
		switch (rmt->state) {
		case CS_SETTINGUP:
			if (FD_ISSET(rmt->recv_fd, &rfds))
				handle_ready(rmt);
			break;

		default:
			break;
		}
	}

	if (FD_ISSET(STDIN_FILENO, &rfds))
		handle_command();
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
			fprintf(stderr, "Unrecognized option: %c\n", opt);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	platform_init();

	if (!argc)
		server_mode();
	else if (argc == 1) {
		memset(&cfg, 0, sizeof(cfg));
		if (parse_cfg(argv[0], &cfg))
			exit(1);
		config = &cfg;
	} else {
		fprintf(stderr, "excess arguments\n");
		exit(1);
	}

	check_remotes();

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
