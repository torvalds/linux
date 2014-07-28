VERSION = 1.0

BINDIR=usr/bin
WARNFLAGS=-Wall -Wshadow -W -Wformat -Wimplicit-function-declaration -Wimplicit-int
CFLAGS= -O1 ${WARNFLAGS} -fstack-protector
CC=gcc

CFLAGS+=-D VERSION=\"$(VERSION)\"
LDFLAGS+=
TARGET=tmon

INSTALL_PROGRAM=install -m 755 -p
DEL_FILE=rm -f

INSTALL_CONFIGFILE=install -m 644 -p
CONFIG_FILE=
CONFIG_PATH=


OBJS = tmon.o tui.o sysfs.o pid.o
OBJS +=

tmon: $(OBJS) Makefile tmon.h
	$(CC) ${CFLAGS} $(LDFLAGS) $(OBJS)  -o $(TARGET) -lm -lpanel -lncursesw -ltinfo -lpthread

valgrind: tmon
	 sudo valgrind -v --track-origins=yes --tool=memcheck --leak-check=yes --show-reachable=yes --num-callers=20 --track-fds=yes ./$(TARGET)  1> /dev/null

install:
	- mkdir -p $(INSTALL_ROOT)/$(BINDIR)
	- $(INSTALL_PROGRAM) "$(TARGET)" "$(INSTALL_ROOT)/$(BINDIR)/$(TARGET)"
	- mkdir -p $(INSTALL_ROOT)/$(CONFIG_PATH)
	- $(INSTALL_CONFIGFILE) "$(CONFIG_FILE)" "$(INSTALL_ROOT)/$(CONFIG_PATH)"

uninstall:
	$(DEL_FILE) "$(INSTALL_ROOT)/$(BINDIR)/$(TARGET)"
	$(CONFIG_FILE) "$(CONFIG_PATH)"


clean:
	find . -name "*.o" | xargs $(DEL_FILE)
	rm -f $(TARGET)

dist:
	git tag v$(VERSION)
	git archive --format=tar --prefix="$(TARGET)-$(VERSION)/" v$(VERSION) | \
		gzip > $(TARGET)-$(VERSION).tar.gz
