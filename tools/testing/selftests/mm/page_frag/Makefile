PAGE_FRAG_TEST_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= $(if $(O),$(O),$(realpath ../../../../..))

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
