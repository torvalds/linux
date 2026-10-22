# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES

TESTMODS_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= $(if $(O),$(O),$(realpath ../../../../..))

obj-m += scx_enq_blocked_test.o

all:
	+$(Q)$(MAKE) -C $(KDIR) M=$(TESTMODS_DIR) modules

clean:
	+$(Q)$(MAKE) -C $(KDIR) M=$(TESTMODS_DIR) clean
