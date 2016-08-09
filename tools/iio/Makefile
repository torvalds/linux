CC = $(CROSS_COMPILE)gcc
CFLAGS += -Wall -g -D_GNU_SOURCE

BINDIR=usr/bin
INSTALL_PROGRAM=install -m 755 -p
DEL_FILE=rm -f

all: iio_event_monitor lsiio iio_generic_buffer

iio_event_monitor: iio_event_monitor.o iio_utils.o

lsiio: lsiio.o iio_utils.o

iio_generic_buffer: iio_generic_buffer.o iio_utils.o

%.o: %.c iio_utils.h

install:
	- mkdir -p $(INSTALL_ROOT)/$(BINDIR)
	- $(INSTALL_PROGRAM) "iio_event_monitor" "$(INSTALL_ROOT)/$(BINDIR)/iio_event_monitor"
	- $(INSTALL_PROGRAM) "lsiio" "$(INSTALL_ROOT)/$(BINDIR)/lsiio"
	- $(INSTALL_PROGRAM) "iio_generic_buffer" "$(INSTALL_ROOT)/$(BINDIR)/iio_generic_buffer"

uninstall:
	$(DEL_FILE) "$(INSTALL_ROOT)/$(BINDIR)/iio_event_monitor"
	$(DEL_FILE) "$(INSTALL_ROOT)/$(BINDIR)/lsiio"
	$(DEL_FILE) "$(INSTALL_ROOT)/$(BINDIR)/iio_generic_buffer"

.PHONY: clean
clean:
	rm -f *.o iio_event_monitor lsiio iio_generic_buffer
