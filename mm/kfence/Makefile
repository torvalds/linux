# SPDX-License-Identifier: GPL-2.0

obj-y := core.o report.o

CFLAGS_kfence_test.o := -g -fno-omit-frame-pointer -fno-optimize-sibling-calls
obj-$(CONFIG_KFENCE_KUNIT_TEST) += kfence_test.o
