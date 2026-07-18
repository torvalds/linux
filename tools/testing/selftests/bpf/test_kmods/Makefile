TEST_KMOD_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SRCTREE_KDIR := $(abspath $(TEST_KMOD_DIR)/../../../../..)
# Honor O=/KBUILD_OUTPUT only if they point at a prepared kernel build
# directory (one containing Module.symvers); otherwise treat the value as a
# selftests-only output directory and fall back to in-tree or distro headers.
# The parent bpf/Makefile resolves O=/KBUILD_OUTPUT to absolute paths before
# invoking this sub-make so relative paths still anchor to the user's
# invocation directory.
KMOD_O := $(or $(O),$(KBUILD_OUTPUT))
KMOD_O_VALID := $(if $(KMOD_O),$(if $(wildcard $(KMOD_O)/Module.symvers),$(KMOD_O)))
KDIR ?= $(if $(KMOD_O_VALID),$(SRCTREE_KDIR), \
	    $(if $(wildcard $(SRCTREE_KDIR)/Module.symvers),$(SRCTREE_KDIR), \
		/lib/modules/$(shell uname -r)/build))

ifeq ($(V),1)
Q =
else
Q = @
endif

MODULES = bpf_testmod.ko bpf_test_no_cfi.ko bpf_test_modorder_x.ko \
	bpf_test_modorder_y.ko bpf_test_rqspinlock.ko

$(foreach m,$(MODULES),$(eval obj-m += $(m:.ko=.o)))

CFLAGS_bpf_testmod.o = -I$(src)

# When BPF_STRICT_BUILD != 0, a missing KDIR is fatal (the default).
# When permissive, skip silently.
PERMISSIVE := $(filter 0,$(BPF_STRICT_BUILD))

all:
ifeq ($(PERMISSIVE),)
	$(Q)$(MAKE) -C $(KDIR) $(if $(KMOD_O_VALID),O=$(KMOD_O_VALID) KBUILD_OUTPUT=$(KMOD_O_VALID),KBUILD_OUTPUT=) \
		M=$(TEST_KMOD_DIR) modules
else ifneq ("$(wildcard $(KDIR))", "")
	$(Q)$(MAKE) -C $(KDIR) $(if $(KMOD_O_VALID),O=$(KMOD_O_VALID) KBUILD_OUTPUT=$(KMOD_O_VALID),KBUILD_OUTPUT=) \
		M=$(TEST_KMOD_DIR) modules
endif

clean:
ifneq ("$(wildcard $(KDIR))", "")
	$(Q)$(MAKE) -C $(KDIR) $(if $(KMOD_O_VALID),O=$(KMOD_O_VALID) KBUILD_OUTPUT=$(KMOD_O_VALID),KBUILD_OUTPUT=) \
		M=$(TEST_KMOD_DIR) clean
endif
