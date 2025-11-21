# SPDX-License-Identifier: GPL-2.0
#
# Makefile for tests of kernel library functions.

# KUnit tests
CFLAGS_bitfield_kunit.o := $(DISABLE_STRUCTLEAK_PLUGIN)
obj-$(CONFIG_BITFIELD_KUNIT) += bitfield_kunit.o
obj-$(CONFIG_BITS_TEST) += test_bits.o
obj-$(CONFIG_BLACKHOLE_DEV_KUNIT_TEST) += blackhole_dev_kunit.o
obj-$(CONFIG_CHECKSUM_KUNIT) += checksum_kunit.o
obj-$(CONFIG_CMDLINE_KUNIT_TEST) += cmdline_kunit.o
obj-$(CONFIG_CPUMASK_KUNIT_TEST) += cpumask_kunit.o
obj-$(CONFIG_FFS_KUNIT_TEST) += ffs_kunit.o
CFLAGS_fortify_kunit.o += $(call cc-disable-warning, unsequenced)
CFLAGS_fortify_kunit.o += $(call cc-disable-warning, stringop-overread)
CFLAGS_fortify_kunit.o += $(call cc-disable-warning, stringop-truncation)
CFLAGS_fortify_kunit.o += $(DISABLE_STRUCTLEAK_PLUGIN)
obj-$(CONFIG_FORTIFY_KUNIT_TEST) += fortify_kunit.o
CFLAGS_test_fprobe.o += $(CC_FLAGS_FTRACE)
obj-$(CONFIG_FPROBE_SANITY_TEST) += test_fprobe.o
obj-$(CONFIG_HASHTABLE_KUNIT_TEST) += hashtable_test.o
obj-$(CONFIG_HASH_KUNIT_TEST) += test_hash.o
obj-$(CONFIG_TEST_IOV_ITER) += kunit_iov_iter.o
obj-$(CONFIG_IS_SIGNED_TYPE_KUNIT_TEST) += is_signed_type_kunit.o
obj-$(CONFIG_KPROBES_SANITY_TEST) += test_kprobes.o
obj-$(CONFIG_LIST_KUNIT_TEST) += list-test.o
obj-$(CONFIG_KFIFO_KUNIT_TEST) += kfifo_kunit.o
obj-$(CONFIG_TEST_LIST_SORT) += test_list_sort.o
obj-$(CONFIG_LINEAR_RANGES_TEST) += test_linear_ranges.o

CFLAGS_longest_symbol_kunit.o += $(call cc-disable-warning, missing-prototypes)
obj-$(CONFIG_LONGEST_SYM_KUNIT_TEST) += longest_symbol_kunit.o

obj-$(CONFIG_MEMCPY_KUNIT_TEST) += memcpy_kunit.o
CFLAGS_overflow_kunit.o = $(call cc-disable-warning, tautological-constant-out-of-range-compare)
obj-$(CONFIG_OVERFLOW_KUNIT_TEST) += overflow_kunit.o
obj-$(CONFIG_PRINTF_KUNIT_TEST) += printf_kunit.o
obj-$(CONFIG_RANDSTRUCT_KUNIT_TEST) += randstruct_kunit.o
obj-$(CONFIG_SCANF_KUNIT_TEST) += scanf_kunit.o
obj-$(CONFIG_SEQ_BUF_KUNIT_TEST) += seq_buf_kunit.o
obj-$(CONFIG_SIPHASH_KUNIT_TEST) += siphash_kunit.o
obj-$(CONFIG_SLUB_KUNIT_TEST) += slub_kunit.o
obj-$(CONFIG_TEST_SORT) += test_sort.o
CFLAGS_stackinit_kunit.o += $(call cc-disable-warning, switch-unreachable)
obj-$(CONFIG_STACKINIT_KUNIT_TEST) += stackinit_kunit.o
obj-$(CONFIG_STRING_KUNIT_TEST) += string_kunit.o
obj-$(CONFIG_STRING_HELPERS_KUNIT_TEST) += string_helpers_kunit.o
obj-$(CONFIG_USERCOPY_KUNIT_TEST) += usercopy_kunit.o
obj-$(CONFIG_UTIL_MACROS_KUNIT) += util_macros_kunit.o
obj-$(CONFIG_RATELIMIT_KUNIT_TEST) += test_ratelimit.o

obj-$(CONFIG_TEST_RUNTIME_MODULE)		+= module/
