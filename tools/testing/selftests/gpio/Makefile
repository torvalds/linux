
TEST_PROGS := gpio-mockup.sh
TEST_FILES := gpio-mockup-sysfs.sh $(BINARIES)
BINARIES := gpio-mockup-chardev

include ../lib.mk

all: $(BINARIES)

clean:
	$(RM) $(BINARIES)

CFLAGS += -O2 -g -std=gnu99 -Wall -I../../../../usr/include/
LDLIBS += -lmount -I/usr/include/libmount

$(BINARIES): ../../../gpio/gpio-utils.o ../../../../usr/include/linux/gpio.h

../../../gpio/gpio-utils.o:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C ../../../gpio

../../../../usr/include/linux/gpio.h:
	make -C ../../../.. headers_install INSTALL_HDR_PATH=$(shell pwd)/../../../../usr/

