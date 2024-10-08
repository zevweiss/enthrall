PLATFORM = osx
OSXVERS = $(shell sw_vers -productVersion | cut -d. -f1,2)
OSXMAJOR = $(basename $(OSXVERS))

# Annoyingly, different versions of Mac OS X keep their SDK
# directories in different places (older ones in /Developer/SDKs,
# newer ones under /Applications/Xcode.app/... or
# /Library/Developer/CommandLineTools/...), so here we try to figure
# out which one to use.
ALL_SDKDIRS = \
	/Library/Developer/CommandLineTools/SDKs/MacOSX$(OSXVERS).sdk \
	/Library/Developer/CommandLineTools/SDKs/MacOSX$(OSXMAJOR).sdk \
	/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX$(OSXVERS).sdk \
	/Developer/SDKs/MacOSX$(OSXVERS).sdk

# realpath has the convenient side-effect of filtering out paths that
# don't exist
SDKDIRS = $(realpath $(ALL_SDKDIRS))

ifeq ($(SDKDIRS),)
$(error "Can't find an appropriate MacOSX$(OSXVERS) SDK directory")
endif

SDKDIR = $(firstword $(SDKDIRS))

ifneq ($(words $(SDKDIRS)),1)
$(warning "Multiple SDK directories found, using $(SDKDIR)")
endif

FMWKDIR = $(SDKDIR)/System/Library/Frameworks
FRAMEWORKS = CoreFoundation ApplicationServices Carbon IOKit
CFLAGS += -iframework$(FMWKDIR) -iframework$(FMWKDIR)/ApplicationServices.framework/Frameworks
LDFLAGS += $(foreach f,$(FRAMEWORKS),-framework $f)

# Some of the Carbon APIs used in osx.c are deprecated, sadly.
osx.o: CFLAGS += -Wno-error=deprecated-declarations

# rpcgen generates code with K&R-style function definitions, and only some
# versions of clang support -Wno-deprecated-non-prototype to silence the
# warnings about it, so just turn of -Werror for that file.  (Doing a compiler
# version check just to figure out whether or not we can pass that one flag
# seems like overkill, and it's generated code so warnings are hard to do
# anything about anyway).
proto.o: CFLAGS += -Wno-error
