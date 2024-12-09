BPF_TESTMOD_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= $(abspath $(BPF_TESTMOD_DIR)/../../../../..)

ifeq ($(V),1)
Q =
else
Q = @
endif

MODULES = bpf_test_modorder_x.ko

obj-m += bpf_test_modorder_x.o

all:
	+$(Q)make -C $(KDIR) M=$(BPF_TESTMOD_DIR) modules

clean:
	+$(Q)make -C $(KDIR) M=$(BPF_TESTMOD_DIR) clean

