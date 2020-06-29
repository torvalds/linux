# SPDX-License-Identifier: GPL-2.0
KCSAN_SANITIZE := n
KCOV_INSTRUMENT := n
UBSAN_SANITIZE := n

CFLAGS_REMOVE_core.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_debugfs.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_report.o = $(CC_FLAGS_FTRACE)

CFLAGS_core.o := $(call cc-option,-fno-conserve-stack,) \
	$(call cc-option,-fno-stack-protector,)

obj-y := core.o debugfs.o report.o
obj-$(CONFIG_KCSAN_SELFTEST) += test.o
