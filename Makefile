
# Disable annoying built-in rules
MAKEFLAGS += -rR

CC = cc
FLEX = flex
BISON = bison
RPCGEN = rpcgen
LD = $(CC)

LIBS = -lm

CFLAGS = -Wall -Werror
LDFLAGS = $(LIBS)

EXE := enthrall

ifneq ($(USE_ASAN),)
	CFLAGS += -fsanitize=address
endif

ifneq ($(USE_UBSAN),)
	CFLAGS += -fsanitize=undefined
endif

ifneq ($(DEBUG),)
	CFLAGS += -ggdb3
else
	CFLAGS += -O2
endif

OS := $(shell uname -s)

ifeq ($(OS),Darwin)
include osx.mk
else
include x11.mk
endif

# OSX compile commands can get quite unreadably long; this keeps it
# pretty unless explicitly requested.
ifneq ($V,)
	I = @:
	Q =
else
	I = @printf "\t%8s: %s\n"
	Q = @
endif

default: all
all: $(EXE)

GENSRCS = cfg-lex.yy.c cfg-parse.tab.c proto.c
GENHDRS = $(GENSRCS:.c=.h)

GEN = $(GENSRCS) $(GENHDRS)

# So make doesn't obnoxiously delete generated files
.SECONDARY: $(GEN)

SRCS = main.c remote.c message.c msgchan.c kvmap.c misc.c \
	$(PLATFORM).c $(PLATFORM)-keycodes.c $(GENSRCS)

OBJS = $(SRCS:.c=.o)
DEPS = $(foreach o,$(OBJS),.$(o:.o=.d))

%.yy.h: %.yy.c
	@touch $@

%.yy.c: %.l
	$I LEX $@
	$Q$(FLEX) --header-file=$(@:.c=.h) -o $@ $<

%.tab.h: %.tab.c
	@touch $@

%.tab.c: %.y
	$I YACC $@
	$Q$(BISON) -Wall --defines=$(@:.c=.h) -o $@ $<

# Sigh.  rpcgen is kind of brain-damaged in ways that make it difficult to
# work with nicely from makefiles.  This is a manual workaround that:
#
#   - ensures if rpcgen succeeds, the output file is updated even if it
#     already existed (not achieved by 'rpcgen -h -o foo.h foo.x')
#
#   - ensures that if rpcgen fails, the output file remains untouched with its
#     original timestamp (not achieved by 'rpcgen -h foo.x > foo.h')
#
#   - doesn't cause rpcgen to generate stupid (syntactically invalid) names for
#     the include-guard macros in generated in its '-h' output (not achieved by
#     'rpcgen -h -o foo.h.tmp foo.x && mv foo.h.tmp foo.h')
#
# $1: codegen mode (-h or -c)
# $2: input file (foo.x)
# $3: output file (foo.h or foo.c)
rpcgen = $(RPCGEN) $1 $2 > .$3.tmp && mv .$3.tmp $3 || { rm -f .$3.tmp; false; }

%.h: %.x
	$I RPCGEN $@
	$Q$(call rpcgen,-h,$<,$@)

%.c: %.x
	$I RPCGEN $@
	$Q$(call rpcgen,-c,$<,$@)

%.o: %.c .%.d
	$I CC $@
	$Q$(CC) -c $(CFLAGS) -o $@ $<

.%.d: %.c
	@$(CC) $(CFLAGS) -MM -MG -MT "$@ $*.o" -MF $@ $<

$(EXE): $(OBJS)
	$I LD $@
	$Q$(LD) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(EXE) $(OBJS) $(GEN) $(DEPS)

deps: $(DEPS)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
