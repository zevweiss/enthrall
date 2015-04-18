
#include <string.h>
#include <errno.h>

#include "types.h"
#include "platform.h"
#include "misc.h"

/* msgchan attached to stdin & stdout  */
struct msgchan stdio_msgchan;

static int initialized = 0;

static void shutdown_remote(void)
{
	mc_close(&stdio_msgchan);

	if (initialized)
		platform_exit();
}

static void handle_message(const struct message* msg)
{
	struct message* resp;

	switch (msg->body.type) {
	case MT_SHUTDOWN:
		shutdown_remote();
		exit(0);

	case MT_MOVEREL:
		move_mousepos(MB(msg, moverel).dx, MB(msg, moverel).dy);
		resp = new_message(MT_MOUSEPOS);
		MB(resp, mousepos).pt = get_mousepos();
		mc_enqueue_message(&stdio_msgchan, resp);
		break;

	case MT_MOVEABS:
		set_mousepos(MB(msg, moveabs).pt);
		break;

	case MT_CLICKEVENT:
		do_clickevent(MB(msg, clickevent).button,
		              MB(msg, clickevent).pressrel);
		break;

	case MT_KEYEVENT:
		do_keyevent(MB(msg, keyevent).keycode, MB(msg, keyevent).pressrel);
		break;

	case MT_GETCLIPBOARD:
		resp = new_message(MT_SETCLIPBOARD);
		MB(resp, setclipboard).text = get_clipboard_text();
		mc_enqueue_message(&stdio_msgchan, resp);
		break;

	case MT_SETCLIPBOARD:
		set_clipboard_text(MB(msg, setclipboard).text);
		break;

	case MT_SETBRIGHTNESS:
		set_display_brightness(MB(msg, setbrightness).brightness);
		break;

	default:
		errlog("unhandled message type: %u\n", msg->body.type);
		shutdown_remote();
		exit(1);
	}
}

/* Initialize the remote after receiving a SETUP message */
static void handle_setup_msg(const struct message* msg)
{
	struct message* readymsg;
	struct kvmap* params;

	if (msg->body.type != MT_SETUP) {
		errlog("unexpected message type %u instead of SETUP\n", msg->body.type);
		exit(1);
	}

	if (MB(msg, setup).prot_vers != PROT_VERSION) {
		errlog("unsupported protocol version %d\n", MB(msg, setup).prot_vers);
		exit(1);
	}

	set_loglevel(MB(msg, setup).loglevel);

	params = unflatten_kvmap(MB(msg, setup).params.params_val,
	                         MB(msg, setup).params.params_len);
	if (!params) {
		errlog("failed to unflatted remote-params kvmap\n");
		exit(1);
	}

	if (platform_init(params, NULL) < 0) {
		errlog("platform_init() failed\n");
		exit(1);
	}

	destroy_kvmap(params);

	readymsg = new_message(MT_READY);
	get_screen_dimensions(&MB(readymsg, ready).screendim);
	mc_enqueue_message(&stdio_msgchan, readymsg);
}

/* msgchan callback to handle received messages */
static void mc_read_cb(struct msgchan* mc, struct message* msg, void* arg)
{
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
