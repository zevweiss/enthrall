%code requires {

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include "types.h"
#include "misc.h"

extern int cfg_debug;

struct cfg_pstate {
	struct config* cfg;
	struct remote* nextrmt;
};

int parse_cfg(const char* path, struct config* cfg);

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

%token KW_REMOTESHELL KW_BINDADDR KW_HOTKEY KW_SWITCH KW_SWITCHTO

%token KW_USER KW_HOSTNAME KW_PORT KW_REMOTECMD

%token END 0 "EOF"

%type <noderef> node
%type <action> action

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

master_opt: KW_REMOTESHELL EQ STRING {
	st->cfg->remote_shell = $3;
}
| KW_BINDADDR EQ STRING {
	st->cfg->bind_address = $3;
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
| KW_PORT EQ INTEGER {
	st->nextrmt->port = $3;
}
| KW_USER EQ STRING {
	st->nextrmt->username = $3;
}
| KW_REMOTECMD EQ STRING {
	st->nextrmt->remotecmd = $3;
}
| DIRECTION EQ node {
	st->nextrmt->neighbors[$1] = $3;
};

%%

int parse_cfg(const char* path, struct config* cfg)
{
	int status;
	struct cfg_pstate pstate;
	FILE* cfgfile = fopen(path, "r");

	if (!cfgfile) {
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
		return -1;
	}

	cfg_restart(cfgfile);

	pstate.cfg = cfg;
	pstate.nextrmt = new_uninit_remote();

	status = cfg_parse(&pstate);

	cfg_lex_destroy();

	xfree(pstate.nextrmt);
	fclose(cfgfile);

	return status;
}

static void cfg_error(struct cfg_pstate* st, char const* s)
{
	fprintf(stderr, "config parse error: %s\n", s);
}
