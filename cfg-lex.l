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

string  ["][^\"]*["]
comment [#][^\n]*
space   [ \t\n]+
integer [-]?[0-9]+
decimal [-]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)

%%

"{"                             return LBRACE;
"}"                             return RBRACE;
"["                             return LBRACKET;
"]"                             return RBRACKET;
"="                             return EQ;

"yes"                           return KW_YES;
"no"                            return KW_NO;

"remote-shell"                  return KW_REMOTESHELL;
"bind-address"                  return KW_BINDADDR;
"identity-file"                 return KW_IDENTITYFILE;
"hotkey"                        return KW_HOTKEY;
"param"                         return KW_PARAM;
"focus"                         return KW_FOCUS;
"clear-clipboard"               return KW_CLEARCLIPBOARD;
"reconnect"                     return KW_RECONNECT;
"halt-reconnects"               return KW_HALT_RECONNECTS;
"step-log-level"                return KW_STEP_LOGLEVEL;
"quit"                          return KW_QUIT;

"log-file"                      return KW_LOGFILE;
"log-level"                     return KW_LOGLEVEL;
"syslog"                        return KW_SYSLOG;
"stderr"                        return KW_STDERR;

"error"                         return KW_ERROR;
"warn"                          return KW_WARN;
"info"                          return KW_INFO;
"verbose"                       return KW_VERBOSE;
"debug"                         return KW_DEBUG;
"debug2"                        return KW_DEBUG2;

"show-focus"                    return KW_SHOWFOCUS;
"dim-inactive"                  return KW_DIMINACTIVE;
"flash-active"                  return KW_FLASHACTIVE;
"none"                          return KW_NONE;

"mouse-switch"                  return KW_MOUSESWITCH;
"multi-tap"                     return KW_MULTITAP;
"slide"                         return KW_SLIDE;

"show-null-switch"              return KW_SHOWNULLSWITCH;
"hotkey-only"                   return KW_HOTKEYONLY;

"reconnect-max-interval"        return KW_RECONMAXINT;
"reconnect-max-tries"           return KW_RECONMAXTRIES;

"use-private-ssh-agent"         return KW_USEPRIVATEAGENT;

"master"                        return KW_MASTER;
"remote"                        return KW_REMOTE;
"topology"                      return KW_TOPOLOGY;
"previous"                      return KW_PREVIOUS;

"hostname"                      return KW_HOSTNAME;
"port"                          return KW_PORT;
"user"                          return KW_USER;
"remote-command"                return KW_REMOTECMD;

"scroll-multiplier"             return KW_SCROLLMULT;

"left"                          return KW_LEFT;
"right"                         return KW_RIGHT;
"up"                            return KW_UP;
"down"                          return KW_DOWN;

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
	cfg_lval.i = strtoll(cfg_text, NULL, 10);
	return INTEGER;
}

{decimal} {
	char* tmp;
	cfg_lval.d = strtod(cfg_text, &tmp);
	return DECIMAL;
}

<*>.|\n {
	initerr("unrecognized token at line %d: %s\n", cfg_lineno, cfg_text);
	exit(1);
}

%%
