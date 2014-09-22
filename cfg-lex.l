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
"="	return EQ;
";"	return SEMICOLON;

"remote-shell"	return KW_REMOTESHELL;
"bind-address"	return KW_BINDADDR;

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
	cfg_lval.str = strdup(cfg_text + 1);
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
