# Annoyingly, different versions of Mac OS X keep their SDK
# directories in different places (older ones in /Developer/SDKs,
# newer ones under /Applications/Xcode.app/...).  Thus the little
# SDKDIR dance here.

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
LDFLAGS += $(foreach f,$(FRAMEWORKS),-framework $f)
