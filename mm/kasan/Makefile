# SPDX-License-Identifier: GPL-2.0
KASAN_SANITIZE := n
UBSAN_SANITIZE_common.o := n
UBSAN_SANITIZE_generic.o := n
KCOV_INSTRUMENT := n

CFLAGS_REMOVE_generic.o = -pg
# Function splitter causes unnecessary splits in __asan_load1/__asan_store1
# see: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63533

CFLAGS_common.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)
CFLAGS_generic.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)

obj-y := common.o generic.o report.o init.o quarantine.o
