%code requires {

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>

#include "types.h"
#include "misc.h"

extern int cfg_debug;

struct cfg_pstate {
	struct config* cfg;
	struct remote* nextrmt;
};

int parse_cfg(FILE* cfgfile, struct config* cfg);

}

%union {
	char* str;
	int64_t i;
	double d;
	direction_t dir;
	struct noderef noderef;
	struct action action;
	struct focus_hint focushint;
	struct mouse_switch mouseswitch;
	struct focus_target focus_target;
	struct link link;
	struct logfile logfile;
	struct {
		float duration;
		int numsteps;
	} dim_fade;
};

%code {

#include "misc.h"

#include "cfg-lex.yy.h"

static void cfg_error(struct cfg_pstate* st, char const*);

}

%{

#define fail_parse(st, msg) do { \
		cfg_error(st, msg); \
		YYABORT; \
	} while (0)

static struct remote* new_uninit_remote(void)
{
	struct remote* rmt = xcalloc(sizeof(*rmt));

	rmt->sshpid = -1;
	rmt->msgchan.send.fd = rmt->msgchan.recv.fd = -1;
	rmt->state = CS_NEW;
	rmt->params = new_kvmap();
	rmt->node.remote = rmt;

	return rmt;
}

%}

%token HASH
%token NEWLINE
%token EQ
%token LBRACE RBRACE
%token LBRACKET RBRACKET
%token KW_YES KW_NO

%token <i> INTEGER
%token <d> DECIMAL
%token <str> STRING
%token <dir> DIRECTION

%token KW_MASTER KW_REMOTE KW_TOPOLOGY

%token KW_REMOTESHELL KW_BINDADDR KW_HOTKEY KW_FOCUS KW_RECONNECT KW_SLIDE
%token KW_IDENTITYFILE KW_PARAM KW_SHOWFOCUS KW_DIMINACTIVE KW_FLASHACTIVE
%token KW_NONE KW_MOUSESWITCH KW_MULTITAP KW_SHOWNULLSWITCH KW_HOTKEYONLY KW_QUIT
%token KW_PREVIOUS KW_RECONMAXINT KW_RECONMAXTRIES

%token KW_USER KW_HOSTNAME KW_PORT KW_REMOTECMD

%token KW_LOGFILE KW_LOGLEVEL KW_SYSLOG KW_STDERR
%token KW_ERROR KW_WARN KW_INFO KW_VERBOSE KW_DEBUG KW_DEBUG2


%token END 0 "EOF"

%type <d> realnum
%type <noderef> node
%type <action> action
%type <focushint> focushint
%type <mouseswitch> mouseswitch
%type <dim_fade> dim_fade
%type <focus_target> focus_target
%type <link> link
%type <dir> opt_direction
%type <str> opt_string
%type <d> loglevel
%type <logfile> logfile

%type <i> port_setting fade_steps show_nullswitch
%type <str> bindaddr_setting user_setting remotecmd_setting remoteshell_setting
%type <str> identityfile_setting

%debug

%name-prefix "cfg_"
%parse-param {struct cfg_pstate* st}

%error-verbose

%%

input: blocks;

/*
 * Bison compat hack: Bison 3.x warns about empty rules not explicitly marked
 * with '%empty', but earlier versions don't support the '%empty' marker, and
 * also don't support -Wno-empty-rule to avoid the warning.  This is a
 * best-effort kludge at supporting both with as few warnings as possible
 * (just the one empty-rule warning for this, which serves as kind of a
 * pseudo-%empty marker in other rules).
 */
EMPTY: /* empty -- this would be %empty if older bisons supported it. */;

blocks: EMPTY
| blocks block {
};

block: master_block {
}
| topology_block {
}
| remote_block {
};

opt_string: EMPTY { $$ = NULL; }
| STRING { $$ = $1; };

master_block: KW_MASTER opt_string LBRACE master_opts RBRACE {
	st->cfg->master.name = $2;
};

master_opts: EMPTY
| master_opt master_opts {
};

realnum: INTEGER { $$ = (double)$1; }
| DECIMAL { $$ = $1; };

port_setting: KW_PORT EQ INTEGER {
	$$ = $3;
	if ($$ < 1 || $$ > USHRT_MAX)
		fail_parse(st, "invalid port number");
};
user_setting: KW_USER EQ STRING { $$ = $3; };
bindaddr_setting: KW_BINDADDR EQ STRING { $$ = $3; };
remotecmd_setting: KW_REMOTECMD EQ STRING { $$ = $3; };

remoteshell_setting: KW_REMOTESHELL EQ STRING {
	$$ = expand_word($3);
	if (!$$)
		fail_parse(st, "bad syntax in remote-shell");
};

identityfile_setting: KW_IDENTITYFILE EQ STRING {
	$$ = expand_word($3);
	if (!$$)
		fail_parse(st, "bad syntax in identity-file");
};

fade_steps: EMPTY { $$ = 1; }
| INTEGER { $$ = $1; };

dim_fade: EMPTY {
	$$.duration = 0.0;
	$$.numsteps = 1;
}
| realnum INTEGER {
	$$.duration = $1;
	$$.numsteps = $2;
};

focushint: KW_DIMINACTIVE realnum dim_fade {
	$$.type = FH_DIM_INACTIVE;
	$$.brightness = $2;
	$$.duration = (uint64_t)($3.duration * 1000000);
	$$.fade_steps = $3.numsteps;
	if ($$.brightness < 0.0 || $3.duration < 0.0)
		fail_parse(st, "dim-inactive arguments must be >= 0");
	if ($$.fade_steps < 1)
		fail_parse(st, "dim-inactive fade-steps must be >= 1");
}
| KW_FLASHACTIVE realnum realnum fade_steps {
	$$.type = FH_FLASH_ACTIVE;
	$$.brightness = $2;
	$$.duration = (uint64_t)($3 * 1000000);
	$$.fade_steps = $4;
	if ($$.brightness < 0.0 || $3 < 0.0)
		fail_parse(st, "flash-active arguments must be >= 0");
	if ($$.fade_steps < 1)
		fail_parse(st, "flash-active fade-steps must be >= 1");
}
| KW_NONE {
	$$.type = FH_NONE;
};

mouseswitch: KW_MULTITAP INTEGER realnum {
	$$.type = MS_MULTITAP;
	$$.num = $2;
	$$.window = (uint64_t)($3 * 1000000);

	if ($$.num <= 0 || $$.num > ((EDGESTATE_HISTLEN+1) / 2))
		fail_parse(st, "invalid multi-tap count");

	if ($3 < 0.0)
		fail_parse(st, "multi-tap time window must be >= 0");
}
| KW_SLIDE {
	/* 'slide' is just shorthand for 'multi-tap 1 ...' */
	$$.type = MS_MULTITAP;
	$$.num = 1;
	$$.window = 0;
}
| KW_NONE {
	$$.type = MS_NONE;
};

show_nullswitch: KW_YES { $$ = NS_YES; }
| KW_NO { $$ = NS_NO; }
| KW_HOTKEYONLY { $$ = NS_HOTKEYONLY; };

loglevel: KW_ERROR { $$ = LL_ERROR; }
| KW_WARN { $$ = LL_WARN; }
| KW_INFO { $$ = LL_INFO; }
| KW_VERBOSE { $$ = LL_VERBOSE; }
| KW_DEBUG { $$ = LL_DEBUG; }
| KW_DEBUG2 { $$ = LL_DEBUG2; };

logfile: KW_SYSLOG { $$.type = LF_SYSLOG; }
| KW_STDERR { $$.type = LF_STDERR; }
| KW_NONE { $$.type = LF_NONE; }
| STRING { $$.type = LF_FILE; $$.path = $1; };

master_opt: remoteshell_setting {
	st->cfg->ssh_defaults.remoteshell = $1;
}
| port_setting {
	st->cfg->ssh_defaults.port = $1;
}
| bindaddr_setting {
	st->cfg->ssh_defaults.bindaddr = $1;
}
| identityfile_setting {
	st->cfg->ssh_defaults.identityfile = $1;
}
| user_setting {
	st->cfg->ssh_defaults.username = $1;
}
| remotecmd_setting {
	st->cfg->ssh_defaults.remotecmd = $1;
}
| KW_SHOWFOCUS EQ focushint {
	st->cfg->focus_hint = $3;
}
| KW_MOUSESWITCH EQ mouseswitch {
	st->cfg->mouseswitch = $3;
}
| KW_SHOWNULLSWITCH EQ show_nullswitch {
	st->cfg->show_nullswitch = $3;
}
| KW_RECONMAXTRIES EQ INTEGER {
	st->cfg->reconnect.max_tries = $3;
}
| KW_RECONMAXINT EQ realnum {
	st->cfg->reconnect.max_interval = (uint64_t)($3 * 1000000);
}
| KW_LOGFILE EQ logfile {
	st->cfg->log.file = $3;
}
| KW_LOGLEVEL EQ loglevel {
	st->cfg->log.level = $3;
}
| KW_HOTKEY LBRACKET STRING RBRACKET EQ action {
	struct hotkey* hk = xmalloc(sizeof(*hk));
	hk->key_string = $3;
	hk->action = $6;
	hk->next = st->cfg->hotkeys;
	st->cfg->hotkeys = hk;
};

focus_target: DIRECTION {
	$$.type = FT_DIRECTION;
	$$.dir = $1;
}
| node {
	$$.type = FT_NODE;
	$$.nr = $1;
}
| KW_PREVIOUS {
	$$.type = FT_PREVIOUS;
};

action: KW_FOCUS focus_target {
	$$.type = AT_FOCUS;
	$$.target = $2;
}
| KW_RECONNECT {
	$$.type = AT_RECONNECT;
}
| KW_QUIT {
	$$.type = AT_QUIT;
};

topology_block: KW_TOPOLOGY LBRACE links RBRACE {
};

links: EMPTY {
}
| links link {
	struct link* ln = xmalloc(sizeof(*ln));
	*ln = $2;
	ln->next = st->cfg->topology;
	st->cfg->topology = ln;
};

opt_direction: EMPTY { $$ = NO_DIR; }
| DIRECTION { $$ = $1; };

link: node DIRECTION EQ node opt_direction {
	$$.a.nr = $1;
	$$.a.dir = $2;
	$$.b.nr = $4;
	$$.b.dir = $5;
};

node: STRING {
	$$.type = NT_TMPNAME;
	$$.name = $1;
}
| KW_MASTER {
	$$.type = NT_TMPNAME;
	$$.name = NULL;
};

remote_block: KW_REMOTE STRING remote_opts {
	struct remote* rmt = st->nextrmt;

	rmt->node.name = $2;
	rmt->next = st->cfg->remotes;
	if (!rmt->hostname)
		rmt->hostname = xstrdup(rmt->node.name);

	st->cfg->remotes = rmt;
	st->nextrmt = new_uninit_remote();
};

remote_opts: EMPTY
| LBRACE remote_optlist RBRACE {
};

remote_optlist: EMPTY
| remote_optlist remote_opt {
};

remote_opt: KW_HOSTNAME EQ STRING {
	st->nextrmt->hostname = $3;
}
| remoteshell_setting {
	st->nextrmt->sshcfg.remoteshell = $1;
}
| port_setting {
	st->nextrmt->sshcfg.port = $1;
}
| bindaddr_setting {
	st->nextrmt->sshcfg.bindaddr = $1;
}
| identityfile_setting {
	st->nextrmt->sshcfg.identityfile = $1;
}
| user_setting {
	st->nextrmt->sshcfg.username = $1;
}
| remotecmd_setting {
	st->nextrmt->sshcfg.remotecmd = $1;
}
| KW_PARAM LBRACKET STRING RBRACKET EQ STRING {
	kvmap_put(st->nextrmt->params, $3, $6);
	xfree($3);
	xfree($6);
};

%%

int parse_cfg(FILE* cfgfile, struct config* cfg)
{
	int status;
	struct cfg_pstate pstate;

	cfg_restart(cfgfile);

	pstate.cfg = cfg;
	pstate.nextrmt = new_uninit_remote();

	status = cfg_parse(&pstate);

	cfg_lex_destroy();

	xfree(pstate.nextrmt->params);
	xfree(pstate.nextrmt);

	return status;
}

static void cfg_error(struct cfg_pstate* st, char const* s)
{
	initerr("config parse error: %s\n", s);
}
