%code requires {

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <wordexp.h>

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
	direction_t dir;
	struct noderef noderef;
	struct action action;
};

%code {

#include "misc.h"

#include "cfg-lex.yy.h"

static void cfg_error(struct cfg_pstate* st, char const*);

}

%{

static struct remote* new_uninit_remote(void)
{
	struct remote* rmt = xcalloc(sizeof(*rmt));

	rmt->sock = rmt->sshpid = -1;
	rmt->state = CS_NEW;

	return rmt;
}

%}

%token HASH
%token NEWLINE
%token EQ
%token LBRACE RBRACE
%token LBRACKET RBRACKET

%token <i> INTEGER
%token <str> STRING
%token <dir> DIRECTION

%token KW_MASTER KW_REMOTE

%token KW_REMOTESHELL KW_BINDADDR KW_HOTKEY KW_SWITCH KW_SWITCHTO KW_RECONNECT
%token KW_IDENTITYFILE

%token KW_USER KW_HOSTNAME KW_PORT KW_REMOTECMD

%token END 0 "EOF"

%type <noderef> node
%type <action> action

%type <i> port_setting
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
EMPTY: /* empty */;

blocks: EMPTY
| blocks block {
};

block: master_block {
}
| remote_block {
};

master_block: KW_MASTER LBRACE master_opts RBRACE {
};

master_opts: EMPTY
| master_opt master_opts {
};

port_setting: KW_PORT EQ INTEGER { $$ = $3; }
user_setting: KW_USER EQ STRING { $$ = $3; }
bindaddr_setting: KW_BINDADDR EQ STRING { $$ = $3; }
remotecmd_setting: KW_REMOTECMD EQ STRING { $$ = $3; }
remoteshell_setting: KW_REMOTESHELL EQ STRING { $$ = $3; }

identityfile_setting: KW_IDENTITYFILE EQ STRING {
	wordexp_t exp;
	/*
	 * OSX's wordexp(3) sadly just ignores these flags, but I guess we
	 * might as well try...
	 */
	if (wordexp($3, &exp, WRDE_NOCMD|WRDE_UNDEF) || exp.we_wordc != 1) {
		cfg_error(st, "bad syntax in identity-file");
		YYABORT;
	}
	$$ = xstrdup(exp.we_wordv[0]);
	wordfree(&exp);
}

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
| KW_HOTKEY LBRACKET STRING RBRACKET EQ action {
	struct hotkey* hk = xmalloc(sizeof(*hk));
	hk->key_string = $3;
	hk->action = $6;
	hk->next = st->cfg->hotkeys;
	st->cfg->hotkeys = hk;
}
| DIRECTION EQ node {
	st->cfg->neighbors[$1] = $3;
};

action: KW_SWITCH DIRECTION {
	$$.type = AT_SWITCH;
	$$.dir = $2;
}
| KW_SWITCHTO node {
	$$.type = AT_SWITCHTO;
	$$.node = $2;
}
| KW_RECONNECT {
	$$.type = AT_RECONNECT;
};

node: STRING {
	$$.type = NT_REMOTE_TMPNAME;
	$$.name = $1;
}
| KW_MASTER {
	$$.type = NT_MASTER;
	$$.node = NULL;
};

remote_block: KW_REMOTE STRING remote_opts {
	struct remote* rmt = st->nextrmt;

	rmt->alias = $2;
	rmt->next = st->cfg->remotes;
	if (!rmt->hostname)
		rmt->hostname = rmt->alias;

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
| DIRECTION EQ node {
	st->nextrmt->neighbors[$1] = $3;
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

	xfree(pstate.nextrmt);

	return status;
}

static void cfg_error(struct cfg_pstate* st, char const* s)
{
	elog("config parse error: %s\n", s);
}
