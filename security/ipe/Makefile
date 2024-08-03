# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
#
# Makefile for building the IPE module as part of the kernel tree.
#

quiet_cmd_polgen = IPE_POL $(2)
      cmd_polgen = scripts/ipe/polgen/polgen security/ipe/boot_policy.c $(2)

targets += boot_policy.c

$(obj)/boot_policy.c: scripts/ipe/polgen/polgen $(CONFIG_IPE_BOOT_POLICY) FORCE
	$(call if_changed,polgen,$(CONFIG_IPE_BOOT_POLICY))

obj-$(CONFIG_SECURITY_IPE) += \
	boot_policy.o \
	digest.o \
	eval.o \
	hooks.o \
	fs.o \
	ipe.o \
	policy.o \
	policy_fs.o \
	policy_parser.o \
	audit.o \

clean-files := boot_policy.c \

obj-$(CONFIG_SECURITY_IPE_KUNIT_TEST) += \
	policy_tests.o \
