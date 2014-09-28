%{

#include <stdlib.h>
#include <stdio.h>

#include "types.h"

#include "cfg-parse.tab.h"

extern YYSTYPE cfg_lval;

%}

%option nounput
%option noinput
%option nodefault
%option noyywrap
%option yylineno
%option prefix="cfg_"

string          ["][^\"]*["]
comment		[#][^\n]+
space		[ \t\n]+
integer		[-]?[0-9]+

%%

"{"	return LBRACE;
"}"	return RBRACE;
"["	return LBRACKET;
"]"	return RBRACKET;
"="	return EQ;

"remote-shell"	return KW_REMOTESHELL;
"bind-address"	return KW_BINDADDR;
"hotkey"	return KW_HOTKEY;
"switch"	return KW_SWITCH;
"switch-to"	return KW_SWITCHTO;

"master"	return KW_MASTER;
"remote"	return KW_REMOTE;

"hostname"		return KW_HOSTNAME;
"port"			return KW_PORT;
"user"			return KW_USER;
"remote-command"	return KW_REMOTECMD;

"left"	{ cfg_lval.dir = LEFT; return DIRECTION; }
"right"	{ cfg_lval.dir = RIGHT; return DIRECTION; }
"up"	{ cfg_lval.dir = UP; return DIRECTION; }
"down"	{ cfg_lval.dir = DOWN; return DIRECTION; }

{comment}
{space}

{string} {
	int cqidx = strlen(cfg_text) - 1;
	assert(cfg_text[0] == '"' && cfg_text[cqidx] == '"');
	cfg_lval.str = xstrdup(cfg_text + 1);
	cfg_lval.str[cqidx-1] = '\0';
	return STRING;
}

{integer} {
	char* tmp;
	cfg_lval.i = strtoll(cfg_text, &tmp, 10);
	return INTEGER;
}

<*>.|\n {
	fprintf(stderr, "unrecognized token at line %d: %s\n", cfg_lineno,
	        cfg_text);
	exit(1);
}

%%
