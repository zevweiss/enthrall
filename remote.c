
#include <string.h>
#include <errno.h>

#include "types.h"
#include "platform.h"
#include "misc.h"

/* msgchan attached to stdin & stdout  */
struct msgchan stdio_msgchan;

struct kvmap* remote_params;

static void handle_message(const struct message* msg)
{
	struct message* resp;

	switch (msg->type) {
	case MT_SHUTDOWN:
		mc_close(&stdio_msgchan);
		destroy_kvmap(remote_params);
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

	case MT_SETBRIGHTNESS:
		set_display_brightness(msg->setbrightness.brightness);
		break;

	case MT_SETMOUSEPOSSCREENREL:
		set_mousepos_screenrel(msg->setmouseposscreenrel.xpos,
		                       msg->setmouseposscreenrel.ypos);
		break;

	default:
		elog("unhandled message type: %u\n", msg->type);
		exit(1);
	}
}

static void handle_fds(int platform_event_fd)
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
			handle_message(&msg);
			if (msg.extra.len)
				xfree(msg.extra.buf);
		}
	}
}

static void send_edgemask_change_cb(dirmask_t old, dirmask_t new, float xpos, float ypos)
{
	struct message* msg = new_message(MT_EDGEMASKCHANGE);

	msg->edgemaskchange.old = old;
	msg->edgemaskchange.new = new;
	msg->edgemaskchange.xpos = xpos;
	msg->edgemaskchange.ypos = ypos;

	mc_enqueue_message(&stdio_msgchan, msg);
}

void run_remote(void)
{
	int platform_event_fd;
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

	remote_params = unflatten_kvmap(setupmsg.extra.buf, setupmsg.extra.len);
	if (!remote_params) {
		elog("failed to unflatted remote-params kvmap\n");
		exit(1);
	}

	if (platform_init(&platform_event_fd, send_edgemask_change_cb) < 0) {
		elog("platform_init() failed\n");
		exit(1);
	}

	set_fd_nonblock(STDIN_FILENO, 1);
	set_fd_nonblock(STDOUT_FILENO, 1);

	mc_init(&stdio_msgchan, STDOUT_FILENO, STDIN_FILENO);

	readymsg = new_message(MT_READY);
	mc_enqueue_message(&stdio_msgchan, readymsg);

	for (;;)
		handle_fds(platform_event_fd);
}
