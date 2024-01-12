TESTMODS_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m += test_klp_atomic_replace.o \
	test_klp_callbacks_busy.o \
	test_klp_callbacks_demo.o \
	test_klp_callbacks_demo2.o \
	test_klp_callbacks_mod.o \
	test_klp_livepatch.o \
	test_klp_state.o \
	test_klp_state2.o \
	test_klp_state3.o \
	test_klp_shadow_vars.o \
	test_klp_syscall.o

modules:
	$(Q)$(MAKE) -C $(KDIR) modules KBUILD_EXTMOD=$(TESTMODS_DIR)

clean:
	$(Q)$(MAKE) -C $(KDIR) clean KBUILD_EXTMOD=$(TESTMODS_DIR)
