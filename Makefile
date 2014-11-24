
CC = cc
FLEX = flex
BISON = bison

CFLAGS = -Wall -Werror
LIBS = -lm

ifneq ($(DEBUG),)
	CFLAGS += -ggdb3
else
	CFLAGS += -O2
endif

OS := $(shell uname -s)

# Annoyingly, different versions of Mac OS X keep their SDK
# directories in different places (older ones in /Developer/SDKs,
# newer ones under /Applications/Xcode.app/...).  Thus the little
# SDKDIR dance below.
ifeq ($(OS),Darwin)
	PLATFORM = osx
	OSXVERS = MacOSX$(shell sw_vers -productVersion | cut -d. -f1,2)
	PLATPFX = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform
	SDKSUBDIR = /Developer/SDKs/$(OSXVERS).sdk

	SDKDIR = $(shell [ -d $(PLATPFX)$(SDKSUBDIR) ] && echo $(PLATPFX)$(SDKSUBDIR))
	ifeq ($(SDKDIR),)
		SDKDIR = $(shell [ -d $(SDKSUBDIR) ] && echo $(SDKSUBDIR))
	endif

	ifeq ($(SDKDIR),)
		_ := $(error "Can't find an appropriate $(OSXVERS) SDK directory")
	endif

	FMWKDIR = $(SDKDIR)/System/Library/Frameworks
	FRAMEWORKS = CoreFoundation ApplicationServices Carbon IOKit
	CFLAGS += -iframework$(FMWKDIR) -iframework$(FMWKDIR)/ApplicationServices.framework/Frameworks
	CFLAGS += $(foreach f,$(FRAMEWORKS),-framework $f)
else
	PLATFORM = x11
	XSUBLIBS = x11 xtst xrandr xi
	X11CFLAGS := $(shell pkg-config --cflags $(XSUBLIBS))
	X11LIBS := $(shell pkg-config --libs $(XSUBLIBS))
	LIBS += $(X11LIBS)
	CFLAGS += $(X11CFLAGS)

	ifeq ($(OS),Linux)
		CFLAGS += -D_GNU_SOURCE
		LIBS += -lrt
	endif
endif

CFGSRCS = cfg-lex.yy.c cfg-lex.yy.h cfg-parse.tab.c cfg-parse.tab.h

HEADERS = misc.h types.h proto.h msgchan.h events.h platform.h kvmap.h \
	keycodes.h $(PLATFORM)-keycodes.h

SRCS = main.c remote.c proto.c msgchan.c kvmap.c misc.c \
	$(PLATFORM).c $(PLATFORM)-keycodes.c

enthrall: $(SRCS) $(HEADERS) $(CFGSRCS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c, $^) $(LIBS)

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
	rm -f enthrall *.yy.[ch] *.tab.[ch]
