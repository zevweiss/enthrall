PLATFORM = x11
XSUBLIBS = x11 xtst xrandr xi

EXTRACFLAGS := $(shell pkg-config --cflags $(XSUBLIBS)) \
	$(shell pkg-config --exists libtirpc && pkg-config --cflags libtirpc && echo "-DUSE_TIRPC")
EXTRALIBS := $(shell pkg-config --libs $(XSUBLIBS)) \
	$(shell pkg-config --exists libtirpc && pkg-config --libs libtirpc)

CFLAGS += $(EXTRACFLAGS)
LIBS += $(EXTRALIBS)

ifeq ($(OS),Linux)
CFLAGS += -D_GNU_SOURCE
LIBS += -lrt
endif
