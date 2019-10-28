# SPDX-License-Identifier: GPL-2.0
KASAN_SANITIZE := n
UBSAN_SANITIZE_common.o := n
UBSAN_SANITIZE_generic.o := n
UBSAN_SANITIZE_generic_report.o := n
UBSAN_SANITIZE_tags.o := n
KCOV_INSTRUMENT := n

CFLAGS_REMOVE_common.o = -pg
CFLAGS_REMOVE_generic.o = -pg
CFLAGS_REMOVE_generic_report.o = -pg
CFLAGS_REMOVE_tags.o = -pg

# Function splitter causes unnecessary splits in __asan_load1/__asan_store1
# see: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63533

CFLAGS_common.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)
CFLAGS_generic.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)
CFLAGS_generic_report.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)
CFLAGS_tags.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)

obj-$(CONFIG_KASAN) := common.o init.o report.o
obj-$(CONFIG_KASAN_GENERIC) += generic.o generic_report.o quarantine.o
obj-$(CONFIG_KASAN_SW_TAGS) += tags.o tags_report.o
