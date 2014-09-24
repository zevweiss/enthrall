
FLEX = flex
BISON = bison

CFLAGS = -Wall -Werror -ggdb3

ifeq ($(shell uname -s),Darwin)
	PLATFORM = osx
	CC = clang
	OSXVER = MacOSX$(shell sw_vers -productVersion | sed -E 's/^([0-9]+\.[0-9]+).*/\1/')
	PLATDIR = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform
	SDKDIR = $(PLATDIR)/Developer/SDKs/$(OSXVER).sdk
	FMWKDIR = $(SDKDIR)/System/Library/Frameworks/
	FRAMEWORKS = CoreFoundation CoreGraphics Carbon
	CFLAGS += -iframework $(FMWKDIR) $(foreach f,$(FRAMEWORKS),-framework $f)
else
	PLATFORM = x11
	CC = gcc
	CFLAGS += -lX11
endif

CFGSRCS = cfg-lex.yy.c cfg-lex.yy.h cfg-parse.tab.c cfg-parse.tab.h

hench: main.c proto.c $(PLATFORM).c proto.h misc.h platform.h $(CFGSRCS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c, $^)

%.yy.h: %.yy.c
	@touch $@

%.yy.c: %.l
	$(FLEX) --header-file=$*.yy.h -o $@ $<

%.tab.h: %.tab.c
	@touch $@

%.tab.c: %.y
	$(BISON) -Wall --defines=$*.tab.h -o $@ $<

.PHONY: clean
clean:
	rm -f hench *.yy.[ch] *.tab.[ch]
