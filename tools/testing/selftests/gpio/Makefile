
TEST_PROGS := gpio-mockup.sh
TEST_FILES := gpio-mockup-sysfs.sh $(BINARIES)
BINARIES := gpio-mockup-chardev
EXTRA_PROGS := ../gpiogpio-event-mon ../gpiogpio-hammer ../gpiolsgpio
EXTRA_DIRS := ../gpioinclude/
EXTRA_OBJS := ../gpiogpio-event-mon-in.o ../gpiogpio-event-mon.o
EXTRA_OBJS += ../gpiogpio-hammer-in.o ../gpiogpio-utils.o ../gpiolsgpio-in.o
EXTRA_OBJS += ../gpiolsgpio.o

include ../lib.mk

all: $(BINARIES)

override define CLEAN
	$(RM) $(BINARIES) $(EXTRA_PROGS) $(EXTRA_OBJS)
	$(RM) -r $(EXTRA_DIRS)
endef

CFLAGS += -O2 -g -std=gnu99 -Wall -I../../../../usr/include/
LDLIBS += -lmount -I/usr/include/libmount

$(BINARIES): ../../../gpio/gpio-utils.o ../../../../usr/include/linux/gpio.h

../../../gpio/gpio-utils.o:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C ../../../gpio

../../../../usr/include/linux/gpio.h:
	make -C ../../../.. headers_install INSTALL_HDR_PATH=$(shell pwd)/../../../../usr/

