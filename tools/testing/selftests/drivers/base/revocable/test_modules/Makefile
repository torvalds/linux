TESTMODS_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m += revocable_test.o

all:
	$(Q)$(MAKE) -C $(KDIR) M=$(TESTMODS_DIR)

clean:
	$(Q)$(MAKE) -C $(KDIR) M=$(TESTMODS_DIR) clean
