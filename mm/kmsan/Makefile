# SPDX-License-Identifier: GPL-2.0
#
# Makefile for KernelMemorySanitizer (KMSAN).
#
#
obj-y := core.o instrumentation.o init.o hooks.o report.o shadow.o

KMSAN_SANITIZE := n
KCOV_INSTRUMENT := n
UBSAN_SANITIZE := n

# Disable instrumentation of KMSAN runtime with other tools.
CC_FLAGS_KMSAN_RUNTIME := -fno-stack-protector
CC_FLAGS_KMSAN_RUNTIME += $(call cc-option,-fno-conserve-stack)
CC_FLAGS_KMSAN_RUNTIME += -DDISABLE_BRANCH_PROFILING

CFLAGS_REMOVE.o = $(CC_FLAGS_FTRACE)

CFLAGS_core.o := $(CC_FLAGS_KMSAN_RUNTIME)
CFLAGS_hooks.o := $(CC_FLAGS_KMSAN_RUNTIME)
CFLAGS_init.o := $(CC_FLAGS_KMSAN_RUNTIME)
CFLAGS_instrumentation.o := $(CC_FLAGS_KMSAN_RUNTIME)
CFLAGS_report.o := $(CC_FLAGS_KMSAN_RUNTIME)
CFLAGS_shadow.o := $(CC_FLAGS_KMSAN_RUNTIME)

obj-$(CONFIG_KMSAN_KUNIT_TEST) += kmsan_test.o
KMSAN_SANITIZE_kmsan_test.o := y
CFLAGS_kmsan_test.o += $(call cc-disable-warning, uninitialized)
