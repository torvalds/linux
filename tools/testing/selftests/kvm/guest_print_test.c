// SPDX-License-Identifier: GPL-2.0
/*
 * A test for GUEST_PRINTF
 *
 * Copyright 2022, Google, Inc. and/or its affiliates.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "ucall_common.h"

struct guest_vals {
	uint64_t a;
	uint64_t b;
	uint64_t type;
};

static struct guest_vals vals;

/* GUEST_PRINTF()/GUEST_ASSERT_FMT() does not support float or double. */
#define TYPE_LIST					\
TYPE(test_type_i64,  I64,  "%ld",   int64_t)		\
TYPE(test_type_u64,  U64u, "%lu",   uint64_t)		\
TYPE(test_type_x64,  U64x, "0x%lx", uint64_t)		\
TYPE(test_type_X64,  U64X, "0x%lX", uint64_t)		\
TYPE(test_type_u32,  U32u, "%u",    uint32_t)		\
TYPE(test_type_x32,  U32x, "0x%x",  uint32_t)		\
TYPE(test_type_X32,  U32X, "0x%X",  uint32_t)		\
TYPE(test_type_int,  INT,  "%d",    int)		\
TYPE(test_type_char, CHAR, "%c",    char)		\
TYPE(test_type_str,  STR,  "'%s'",  const char *)	\
TYPE(test_type_ptr,  PTR,  "%p",    uintptr_t)

enum args_type {
#define TYPE(fn, ext, fmt_t, T) TYPE_##ext,
	TYPE_LIST
#undef TYPE
};

static void run_test(struct kvm_vcpu *vcpu, const char *expected_printf,
		     const char *expected_assert);

#define BUILD_TYPE_STRINGS_AND_HELPER(fn, ext, fmt_t, T)		     \
const char *PRINTF_FMT_##ext = "Got params a = " fmt_t " and b = " fmt_t;    \
const char *ASSERT_FMT_##ext = "Expected " fmt_t ", got " fmt_t " instead";  \
static void fn(struct kvm_vcpu *vcpu, T a, T b)				     \
{									     \
	char expected_printf[UCALL_BUFFER_LEN];				     \
	char expected_assert[UCALL_BUFFER_LEN];				     \
									     \
	snprintf(expected_printf, UCALL_BUFFER_LEN, PRINTF_FMT_##ext, a, b); \
	snprintf(expected_assert, UCALL_BUFFER_LEN, ASSERT_FMT_##ext, a, b); \
	vals = (struct guest_vals){ (uint64_t)a, (uint64_t)b, TYPE_##ext };  \
	sync_global_to_guest(vcpu->vm, vals);				     \
	run_test(vcpu, expected_printf, expected_assert);		     \
}

#define TYPE(fn, ext, fmt_t, T) \
		BUILD_TYPE_STRINGS_AND_HELPER(fn, ext, fmt_t, T)
	TYPE_LIST
#undef TYPE

static void guest_code(void)
{
	while (1) {
		switch (vals.type) {
#define TYPE(fn, ext, fmt_t, T)							\
		case TYPE_##ext:						\
			GUEST_PRINTF(PRINTF_FMT_##ext, vals.a, vals.b);		\
			__GUEST_ASSERT(vals.a == vals.b,			\
				       ASSERT_FMT_##ext, vals.a, vals.b);	\
			break;
		TYPE_LIST
#undef TYPE
		default:
			GUEST_SYNC(vals.type);
		}

		GUEST_DONE();
	}
}

/*
 * Unfortunately this gets a little messy because 'assert_msg' doesn't
 * just contains the matching string, it also contains additional assert
 * info.  Fortunately the part that matches should be at the very end of
 * 'assert_msg'.
 */
static void ucall_abort(const char *assert_msg, const char *expected_assert_msg)
{
	int len_str = strlen(assert_msg);
	int len_substr = strlen(expected_assert_msg);
	int offset = len_str - len_substr;

	TEST_ASSERT(len_substr <= len_str,
		    "Expected '%s' to be a substring of '%s'",
		    assert_msg, expected_assert_msg);

	TEST_ASSERT(strcmp(&assert_msg[offset], expected_assert_msg) == 0,
		    "Unexpected mismatch. Expected: '%s', got: '%s'",
		    expected_assert_msg, &assert_msg[offset]);
}

static void run_test(struct kvm_vcpu *vcpu, const char *expected_printf,
		     const char *expected_assert)
{
	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	while (1) {
		vcpu_run(vcpu);

		TEST_ASSERT(run->exit_reason == UCALL_EXIT_REASON,
			    "Unexpected exit reason: %u (%s),",
			    run->exit_reason, exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			TEST_FAIL("Unknown 'args_type' = %lu", uc.args[1]);
			break;
		case UCALL_PRINTF:
			TEST_ASSERT(strcmp(uc.buffer, expected_printf) == 0,
				    "Unexpected mismatch. Expected: '%s', got: '%s'",
				    expected_printf, uc.buffer);
			break;
		case UCALL_ABORT:
			ucall_abort(uc.buffer, expected_assert);
			break;
		case UCALL_DONE:
			return;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}
}

static void guest_code_limits(void)
{
	char test_str[UCALL_BUFFER_LEN + 10];

	memset(test_str, 'a', sizeof(test_str));
	test_str[sizeof(test_str) - 1] = 0;

	GUEST_PRINTF("%s", test_str);
}

static void test_limits(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code_limits);
	run = vcpu->run;
	vcpu_run(vcpu);

	TEST_ASSERT(run->exit_reason == UCALL_EXIT_REASON,
		    "Unexpected exit reason: %u (%s),",
		    run->exit_reason, exit_reason_str(run->exit_reason));

	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_ABORT,
		    "Unexpected ucall command: %lu,  Expected: %u (UCALL_ABORT)",
		    uc.cmd, UCALL_ABORT);

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	test_type_i64(vcpu, -1, -1);
	test_type_i64(vcpu, -1,  1);
	test_type_i64(vcpu, 0x1234567890abcdef, 0x1234567890abcdef);
	test_type_i64(vcpu, 0x1234567890abcdef, 0x1234567890abcdee);

	test_type_u64(vcpu, 0x1234567890abcdef, 0x1234567890abcdef);
	test_type_u64(vcpu, 0x1234567890abcdef, 0x1234567890abcdee);
	test_type_x64(vcpu, 0x1234567890abcdef, 0x1234567890abcdef);
	test_type_x64(vcpu, 0x1234567890abcdef, 0x1234567890abcdee);
	test_type_X64(vcpu, 0x1234567890abcdef, 0x1234567890abcdef);
	test_type_X64(vcpu, 0x1234567890abcdef, 0x1234567890abcdee);

	test_type_u32(vcpu, 0x90abcdef, 0x90abcdef);
	test_type_u32(vcpu, 0x90abcdef, 0x90abcdee);
	test_type_x32(vcpu, 0x90abcdef, 0x90abcdef);
	test_type_x32(vcpu, 0x90abcdef, 0x90abcdee);
	test_type_X32(vcpu, 0x90abcdef, 0x90abcdef);
	test_type_X32(vcpu, 0x90abcdef, 0x90abcdee);

	test_type_int(vcpu, -1, -1);
	test_type_int(vcpu, -1,  1);
	test_type_int(vcpu,  1,  1);

	test_type_char(vcpu, 'a', 'a');
	test_type_char(vcpu, 'a', 'A');
	test_type_char(vcpu, 'a', 'b');

	test_type_str(vcpu, "foo", "foo");
	test_type_str(vcpu, "foo", "bar");

	test_type_ptr(vcpu, 0x1234567890abcdef, 0x1234567890abcdef);
	test_type_ptr(vcpu, 0x1234567890abcdef, 0x1234567890abcdee);

	kvm_vm_free(vm);

	test_limits();

	return 0;
}
