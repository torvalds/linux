/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/kvm_util.h
 *
 * Copyright (C) 2018, Google LLC.
 */
#ifndef SELFTEST_KVM_UCALL_COMMON_H
#define SELFTEST_KVM_UCALL_COMMON_H
#include "test_util.h"

/* Common ucalls */
enum {
	UCALL_NONE,
	UCALL_SYNC,
	UCALL_ABORT,
	UCALL_DONE,
	UCALL_UNHANDLED,
};

#define UCALL_MAX_ARGS 7

struct ucall {
	uint64_t cmd;
	uint64_t args[UCALL_MAX_ARGS];
};

void ucall_init(struct kvm_vm *vm, void *arg);
void ucall_uninit(struct kvm_vm *vm);
void ucall(uint64_t cmd, int nargs, ...);
uint64_t get_ucall(struct kvm_vcpu *vcpu, struct ucall *uc);

#define GUEST_SYNC_ARGS(stage, arg1, arg2, arg3, arg4)	\
				ucall(UCALL_SYNC, 6, "hello", stage, arg1, arg2, arg3, arg4)
#define GUEST_SYNC(stage)	ucall(UCALL_SYNC, 2, "hello", stage)
#define GUEST_DONE()		ucall(UCALL_DONE, 0)

enum guest_assert_builtin_args {
	GUEST_ERROR_STRING,
	GUEST_FILE,
	GUEST_LINE,
	GUEST_ASSERT_BUILTIN_NARGS
};

#define __GUEST_ASSERT(_condition, _condstr, _nargs, _args...)		\
do {									\
	if (!(_condition))						\
		ucall(UCALL_ABORT, GUEST_ASSERT_BUILTIN_NARGS + _nargs,	\
		      "Failed guest assert: " _condstr,			\
		      __FILE__, __LINE__, ##_args);			\
} while (0)

#define GUEST_ASSERT(_condition) \
	__GUEST_ASSERT(_condition, #_condition, 0, 0)

#define GUEST_ASSERT_1(_condition, arg1) \
	__GUEST_ASSERT(_condition, #_condition, 1, (arg1))

#define GUEST_ASSERT_2(_condition, arg1, arg2) \
	__GUEST_ASSERT(_condition, #_condition, 2, (arg1), (arg2))

#define GUEST_ASSERT_3(_condition, arg1, arg2, arg3) \
	__GUEST_ASSERT(_condition, #_condition, 3, (arg1), (arg2), (arg3))

#define GUEST_ASSERT_4(_condition, arg1, arg2, arg3, arg4) \
	__GUEST_ASSERT(_condition, #_condition, 4, (arg1), (arg2), (arg3), (arg4))

#define GUEST_ASSERT_EQ(a, b) __GUEST_ASSERT((a) == (b), #a " == " #b, 2, a, b)

#define __REPORT_GUEST_ASSERT(_ucall, fmt, _args...)			\
	TEST_FAIL("%s at %s:%ld\n" fmt,					\
		  (const char *)(_ucall).args[GUEST_ERROR_STRING],	\
		  (const char *)(_ucall).args[GUEST_FILE],		\
		  (_ucall).args[GUEST_LINE],				\
		  ##_args)

#define GUEST_ASSERT_ARG(ucall, i) ((ucall).args[GUEST_ASSERT_BUILTIN_NARGS + i])

#define REPORT_GUEST_ASSERT(ucall)		\
	__REPORT_GUEST_ASSERT((ucall), "")

#define REPORT_GUEST_ASSERT_1(ucall, fmt)			\
	__REPORT_GUEST_ASSERT((ucall),				\
			      fmt,				\
			      GUEST_ASSERT_ARG((ucall), 0))

#define REPORT_GUEST_ASSERT_2(ucall, fmt)			\
	__REPORT_GUEST_ASSERT((ucall),				\
			      fmt,				\
			      GUEST_ASSERT_ARG((ucall), 0),	\
			      GUEST_ASSERT_ARG((ucall), 1))

#define REPORT_GUEST_ASSERT_3(ucall, fmt)			\
	__REPORT_GUEST_ASSERT((ucall),				\
			      fmt,				\
			      GUEST_ASSERT_ARG((ucall), 0),	\
			      GUEST_ASSERT_ARG((ucall), 1),	\
			      GUEST_ASSERT_ARG((ucall), 2))

#define REPORT_GUEST_ASSERT_4(ucall, fmt)			\
	__REPORT_GUEST_ASSERT((ucall),				\
			      fmt,				\
			      GUEST_ASSERT_ARG((ucall), 0),	\
			      GUEST_ASSERT_ARG((ucall), 1),	\
			      GUEST_ASSERT_ARG((ucall), 2),	\
			      GUEST_ASSERT_ARG((ucall), 3))

#define REPORT_GUEST_ASSERT_N(ucall, fmt, args...)	\
	__REPORT_GUEST_ASSERT((ucall), fmt, ##args)

#endif /* SELFTEST_KVM_UCALL_COMMON_H */
