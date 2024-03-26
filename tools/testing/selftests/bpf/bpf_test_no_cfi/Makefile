BPF_TEST_NO_CFI_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= $(abspath $(BPF_TEST_NO_CFI_DIR)/../../../../..)

ifeq ($(V),1)
Q =
else
Q = @
endif

MODULES = bpf_test_no_cfi.ko

obj-m += bpf_test_no_cfi.o

all:
	+$(Q)make -C $(KDIR) M=$(BPF_TEST_NO_CFI_DIR) modules

clean:
	+$(Q)make -C $(KDIR) M=$(BPF_TEST_NO_CFI_DIR) clean

