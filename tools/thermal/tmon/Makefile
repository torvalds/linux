# SPDX-License-Identifier: GPL-2.0
# We need this for the "cc-option" macro.
include ../../build/Build.include

VERSION = 1.0

BINDIR=usr/bin
WARNFLAGS=-Wall -Wshadow -W -Wformat -Wimplicit-function-declaration -Wimplicit-int
override CFLAGS+= $(call cc-option,-O3,-O1) ${WARNFLAGS}
# Add "-fstack-protector" only if toolchain supports it.
override CFLAGS+= $(call cc-option,-fstack-protector-strong)
CC?= $(CROSS_COMPILE)gcc
PKG_CONFIG?= pkg-config

override CFLAGS+=-D VERSION=\"$(VERSION)\"
LDFLAGS+=
TARGET=tmon

INSTALL_PROGRAM=install -m 755 -p
DEL_FILE=rm -f

# Static builds might require -ltinfo, for instance
ifneq ($(findstring -static, $(LDFLAGS)),)
STATIC := --static
endif

TMON_LIBS=-lm -lpthread
TMON_LIBS += $(shell $(PKG_CONFIG) --libs $(STATIC) panelw ncursesw 2> /dev/null || \
		     $(PKG_CONFIG) --libs $(STATIC) panel ncurses 2> /dev/null || \
		     echo -lpanel -lncurses)

override CFLAGS += $(shell $(PKG_CONFIG) --cflags $(STATIC) panelw ncursesw 2> /dev/null || \
		     $(PKG_CONFIG) --cflags $(STATIC) panel ncurses 2> /dev/null)

OBJS = tmon.o tui.o sysfs.o pid.o
OBJS +=

tmon: $(OBJS) Makefile tmon.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS)  -o $(TARGET) $(TMON_LIBS)

valgrind: tmon
	 sudo valgrind -v --track-origins=yes --tool=memcheck --leak-check=yes --show-reachable=yes --num-callers=20 --track-fds=yes ./$(TARGET)  1> /dev/null

install:
	- mkdir -p $(INSTALL_ROOT)/$(BINDIR)
	- $(INSTALL_PROGRAM) "$(TARGET)" "$(INSTALL_ROOT)/$(BINDIR)/$(TARGET)"

uninstall:
	$(DEL_FILE) "$(INSTALL_ROOT)/$(BINDIR)/$(TARGET)"

clean:
	find . -name "*.o" | xargs $(DEL_FILE)
	rm -f $(TARGET)

dist:
	git tag v$(VERSION)
	git archive --format=tar --prefix="$(TARGET)-$(VERSION)/" v$(VERSION) | \
		gzip > $(TARGET)-$(VERSION).tar.gz
