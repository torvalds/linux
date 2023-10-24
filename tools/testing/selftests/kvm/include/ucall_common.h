/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018, Google LLC.
 */
#ifndef SELFTEST_KVM_UCALL_COMMON_H
#define SELFTEST_KVM_UCALL_COMMON_H
#include "test_util.h"
#include "ucall.h"

/* Common ucalls */
enum {
	UCALL_NONE,
	UCALL_SYNC,
	UCALL_ABORT,
	UCALL_PRINTF,
	UCALL_DONE,
	UCALL_UNHANDLED,
};

#define UCALL_MAX_ARGS 7
#define UCALL_BUFFER_LEN 1024

struct ucall {
	uint64_t cmd;
	uint64_t args[UCALL_MAX_ARGS];
	char buffer[UCALL_BUFFER_LEN];

	/* Host virtual address of this struct. */
	struct ucall *hva;
};

void ucall_arch_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa);
void ucall_arch_do_ucall(vm_vaddr_t uc);
void *ucall_arch_get_ucall(struct kvm_vcpu *vcpu);

void ucall(uint64_t cmd, int nargs, ...);
void ucall_fmt(uint64_t cmd, const char *fmt, ...);
void ucall_assert(uint64_t cmd, const char *exp, const char *file,
		  unsigned int line, const char *fmt, ...);
uint64_t get_ucall(struct kvm_vcpu *vcpu, struct ucall *uc);
void ucall_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa);
int ucall_nr_pages_required(uint64_t page_size);

/*
 * Perform userspace call without any associated data.  This bare call avoids
 * allocating a ucall struct, which can be useful if the atomic operations in
 * the full ucall() are problematic and/or unwanted.  Note, this will come out
 * as UCALL_NONE on the backend.
 */
#define GUEST_UCALL_NONE()	ucall_arch_do_ucall((vm_vaddr_t)NULL)

#define GUEST_SYNC_ARGS(stage, arg1, arg2, arg3, arg4)	\
				ucall(UCALL_SYNC, 6, "hello", stage, arg1, arg2, arg3, arg4)
#define GUEST_SYNC(stage)	ucall(UCALL_SYNC, 2, "hello", stage)
#define GUEST_PRINTF(_fmt, _args...) ucall_fmt(UCALL_PRINTF, _fmt, ##_args)
#define GUEST_DONE()		ucall(UCALL_DONE, 0)

#define REPORT_GUEST_PRINTF(ucall) pr_info("%s", (ucall).buffer)

enum guest_assert_builtin_args {
	GUEST_ERROR_STRING,
	GUEST_FILE,
	GUEST_LINE,
	GUEST_ASSERT_BUILTIN_NARGS
};

#define ____GUEST_ASSERT(_condition, _exp, _fmt, _args...)				\
do {											\
	if (!(_condition))								\
		ucall_assert(UCALL_ABORT, _exp, __FILE__, __LINE__, _fmt, ##_args);	\
} while (0)

#define __GUEST_ASSERT(_condition, _fmt, _args...)				\
	____GUEST_ASSERT(_condition, #_condition, _fmt, ##_args)

#define GUEST_ASSERT(_condition)						\
	__GUEST_ASSERT(_condition, #_condition)

#define GUEST_FAIL(_fmt, _args...)						\
	ucall_assert(UCALL_ABORT, "Unconditional guest failure",		\
		     __FILE__, __LINE__, _fmt, ##_args)

#define GUEST_ASSERT_EQ(a, b)							\
do {										\
	typeof(a) __a = (a);							\
	typeof(b) __b = (b);							\
	____GUEST_ASSERT(__a == __b, #a " == " #b, "%#lx != %#lx (%s != %s)",	\
			 (unsigned long)(__a), (unsigned long)(__b), #a, #b);	\
} while (0)

#define GUEST_ASSERT_NE(a, b)							\
do {										\
	typeof(a) __a = (a);							\
	typeof(b) __b = (b);							\
	____GUEST_ASSERT(__a != __b, #a " != " #b, "%#lx == %#lx (%s == %s)",	\
			 (unsigned long)(__a), (unsigned long)(__b), #a, #b);	\
} while (0)

#define REPORT_GUEST_ASSERT(ucall)						\
	test_assert(false, (const char *)(ucall).args[GUEST_ERROR_STRING],	\
		    (const char *)(ucall).args[GUEST_FILE],			\
		    (ucall).args[GUEST_LINE], "%s", (ucall).buffer)

#endif /* SELFTEST_KVM_UCALL_COMMON_H */
