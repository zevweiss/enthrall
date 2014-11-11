
#include <string.h>
#include <errno.h>

#include "types.h"
#include "platform.h"
#include "misc.h"

/* msgchan attached to stdin & stdout  */
struct msgchan stdio_msgchan;

struct kvmap* remote_params;

static void shutdown_remote(void)
{
	mc_close(&stdio_msgchan);
	destroy_kvmap(remote_params);
	platform_exit();
}

static void handle_message(const struct message* msg)
{
	struct message* resp;

	switch (msg->type) {
	case MT_SHUTDOWN:
		shutdown_remote();
		exit(0);

	case MT_MOVEREL:
		move_mousepos(msg->moverel.dx, msg->moverel.dy);
		resp = new_message(MT_MOUSEPOS);
		resp->mousepos.pt = get_mousepos();
		mc_enqueue_message(&stdio_msgchan, resp);
		break;

	case MT_MOVEABS:
		set_mousepos(msg->moveabs.pt);
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

	default:
		errlog("unhandled message type: %u\n", msg->type);
		shutdown_remote();
		exit(1);
	}
}

static void handle_setup_msg(const struct message* msg)
{
	struct message* readymsg;

	if (msg->type != MT_SETUP) {
		errlog("unexpected message type %u instead of SETUP\n", msg->type);
		exit(1);
	}

	if (msg->setup.prot_vers != PROT_VERSION) {
		errlog("unsupported protocol version %d\n", msg->setup.prot_vers);
		exit(1);
	}

	remote_params = unflatten_kvmap(msg->extra.buf, msg->extra.len);
	if (!remote_params) {
		errlog("failed to unflatted remote-params kvmap\n");
		exit(1);
	}

	if (platform_init(NULL) < 0) {
		errlog("platform_init() failed\n");
		exit(1);
	}

	readymsg = new_message(MT_READY);
	get_screen_dimensions(&readymsg->ready.screendim);
	mc_enqueue_message(&stdio_msgchan, readymsg);
}

static void mc_read_cb(struct msgchan* mc, struct message* msg, void* arg)
{
	static int initialized = 0;

	if (!initialized) {
		handle_setup_msg(msg);
		initialized = 1;
	} else {
		handle_message(msg);
	}
}

static void mc_err_cb(struct msgchan* mc, void* arg)
{
	errlog("msgchan error, remote terminating\n");
	shutdown_remote();
	exit(1);
}

void run_remote(void)
{
	mc_init(&stdio_msgchan, STDOUT_FILENO, STDIN_FILENO,
	        mc_read_cb, mc_err_cb, NULL);

	run_event_loop();
}
