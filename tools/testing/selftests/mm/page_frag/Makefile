PAGE_FRAG_TEST_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= /lib/modules/$(shell uname -r)/build

ifeq ($(V),1)
Q =
else
Q = @
endif

MODULES = page_frag_test.ko

obj-m += page_frag_test.o

all:
	+$(Q)make -C $(KDIR) M=$(PAGE_FRAG_TEST_DIR) modules

clean:
	+$(Q)make -C $(KDIR) M=$(PAGE_FRAG_TEST_DIR) clean
