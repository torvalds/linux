// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test cases for compiler-based stack variable zeroing via
 * -ftrivial-auto-var-init={zero,pattern} or CONFIG_GCC_PLUGIN_STRUCTLEAK*.
 *
 * External build example:
 *	clang -O2 -Wall -ftrivial-auto-var-init=pattern \
 *		-o test_stackinit test_stackinit.c
 */
#ifdef __KERNEL__
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#else

/* Userspace headers. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>

/* Linux kernel-ism stubs for stand-alone userspace build. */
#define KBUILD_MODNAME		"stackinit"
#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt
#define pr_err(fmt, ...)	fprintf(stderr, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...)	fprintf(stderr, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...)	fprintf(stdout, pr_fmt(fmt), ##__VA_ARGS__)
#define __init			/**/
#define __exit			/**/
#define __user			/**/
#define noinline		__attribute__((__noinline__))
#define __aligned(x)		__attribute__((__aligned__(x)))
#ifdef __clang__
# define __compiletime_error(message) /**/
#else
# define __compiletime_error(message) __attribute__((__error__(message)))
#endif
#define __compiletime_assert(condition, msg, prefix, suffix)		\
	do {								\
		extern void prefix ## suffix(void) __compiletime_error(msg); \
		if (!(condition))					\
			prefix ## suffix();				\
	} while (0)
#define _compiletime_assert(condition, msg, prefix, suffix) \
	__compiletime_assert(condition, msg, prefix, suffix)
#define compiletime_assert(condition, msg) \
	_compiletime_assert(condition, msg, __compiletime_assert_, __COUNTER__)
#define BUILD_BUG_ON_MSG(cond, msg) compiletime_assert(!(cond), msg)
#define BUILD_BUG_ON(condition) \
	BUILD_BUG_ON_MSG(condition, "BUILD_BUG_ON failed: " #condition)
typedef uint8_t			u8;
typedef uint16_t		u16;
typedef uint32_t		u32;
typedef uint64_t		u64;

#define module_init(func)	static int (*do_init)(void) = func
#define module_exit(func)	static void (*do_exit)(void) = func
#define MODULE_LICENSE(str)	int main(void) {		\
					int rc;			\
					/* License: str */	\
					rc = do_init();		\
					if (rc == 0)		\
						do_exit();	\
					return rc;		\
				}

#endif /* __KERNEL__ */

/* Exfiltration buffer. */
#define MAX_VAR_SIZE	128
static u8 check_buf[MAX_VAR_SIZE];

/* Character array to trigger stack protector in all functions. */
#define VAR_BUFFER	 32

/* Volatile mask to convince compiler to copy memory with 0xff. */
static volatile u8 forced_mask = 0xff;

/* Location and size tracking to validate fill and test are colocated. */
static void *fill_start, *target_start;
static size_t fill_size, target_size;

static bool range_contains(char *haystack_start, size_t haystack_size,
			   char *needle_start, size_t needle_size)
{
	if (needle_start >= haystack_start &&
	    needle_start + needle_size <= haystack_start + haystack_size)
		return true;
	return false;
}

/* Whether the test is expected to fail. */
#define WANT_SUCCESS				0
#define XFAIL					1

#define DO_NOTHING_TYPE_SCALAR(var_type)	var_type
#define DO_NOTHING_TYPE_STRING(var_type)	void
#define DO_NOTHING_TYPE_STRUCT(var_type)	void

#define DO_NOTHING_RETURN_SCALAR(ptr)		*(ptr)
#define DO_NOTHING_RETURN_STRING(ptr)		/**/
#define DO_NOTHING_RETURN_STRUCT(ptr)		/**/

#define DO_NOTHING_CALL_SCALAR(var, name)			\
		(var) = do_nothing_ ## name(&(var))
#define DO_NOTHING_CALL_STRING(var, name)			\
		do_nothing_ ## name(var)
#define DO_NOTHING_CALL_STRUCT(var, name)			\
		do_nothing_ ## name(&(var))

#define FETCH_ARG_SCALAR(var)		&var
#define FETCH_ARG_STRING(var)		var
#define FETCH_ARG_STRUCT(var)		&var

#define FILL_SIZE_STRING		16

#define INIT_CLONE_SCALAR		/**/
#define INIT_CLONE_STRING		[FILL_SIZE_STRING]
#define INIT_CLONE_STRUCT		/**/

#define ZERO_CLONE_SCALAR(zero)		memset(&(zero), 0x00, sizeof(zero))
#define ZERO_CLONE_STRING(zero)		memset(&(zero), 0x00, sizeof(zero))
/*
 * For the struct, intentionally poison padding to see if it gets
 * copied out in direct assignments.
 * */
#define ZERO_CLONE_STRUCT(zero)				\
	do {						\
		memset(&(zero), 0xFF, sizeof(zero));	\
		zero.one = 0;				\
		zero.two = 0;				\
		zero.three = 0;				\
		zero.four = 0;				\
	} while (0)

#define INIT_SCALAR_none(var_type)	/**/
#define INIT_SCALAR_zero(var_type)	= 0

#define INIT_STRING_none(var_type)	[FILL_SIZE_STRING] /**/
#define INIT_STRING_zero(var_type)	[FILL_SIZE_STRING] = { }

#define INIT_STRUCT_none(var_type)	/**/
#define INIT_STRUCT_zero(var_type)	= { }


#define __static_partial		{ .two = 0, }
#define __static_all			{ .one = 0,			\
					  .two = 0,			\
					  .three = 0,			\
					  .four = 0,			\
					}
#define __dynamic_partial		{ .two = arg->two, }
#define __dynamic_all			{ .one = arg->one,		\
					  .two = arg->two,		\
					  .three = arg->three,		\
					  .four = arg->four,		\
					}
#define __runtime_partial		var.two = 0
#define __runtime_all			var.one = 0;			\
					var.two = 0;			\
					var.three = 0;			\
					var.four = 0

#define INIT_STRUCT_static_partial(var_type)				\
					= __static_partial
#define INIT_STRUCT_static_all(var_type)				\
					= __static_all
#define INIT_STRUCT_dynamic_partial(var_type)				\
					= __dynamic_partial
#define INIT_STRUCT_dynamic_all(var_type)				\
					= __dynamic_all
#define INIT_STRUCT_runtime_partial(var_type)				\
					; __runtime_partial
#define INIT_STRUCT_runtime_all(var_type)				\
					; __runtime_all

#define INIT_STRUCT_assigned_static_partial(var_type)			\
					; var = (var_type)__static_partial
#define INIT_STRUCT_assigned_static_all(var_type)			\
					; var = (var_type)__static_all
#define INIT_STRUCT_assigned_dynamic_partial(var_type)			\
					; var = (var_type)__dynamic_partial
#define INIT_STRUCT_assigned_dynamic_all(var_type)			\
					; var = (var_type)__dynamic_all

#define INIT_STRUCT_assigned_copy(var_type)				\
					; var = *(arg)

/*
 * @name: unique string name for the test
 * @var_type: type to be tested for zeroing initialization
 * @which: is this a SCALAR, STRING, or STRUCT type?
 * @init_level: what kind of initialization is performed
 * @xfail: is this test expected to fail?
 */
#define DEFINE_TEST_DRIVER(name, var_type, which, xfail)	\
/* Returns 0 on success, 1 on failure. */			\
static noinline __init int test_ ## name (void)			\
{								\
	var_type zero INIT_CLONE_ ## which;			\
	int ignored;						\
	u8 sum = 0, i;						\
								\
	/* Notice when a new test is larger than expected. */	\
	BUILD_BUG_ON(sizeof(zero) > MAX_VAR_SIZE);		\
								\
	/* Fill clone type with zero for per-field init. */	\
	ZERO_CLONE_ ## which(zero);				\
	/* Clear entire check buffer for 0xFF overlap test. */	\
	memset(check_buf, 0x00, sizeof(check_buf));		\
	/* Fill stack with 0xFF. */				\
	ignored = leaf_ ##name((unsigned long)&ignored, 1,	\
				FETCH_ARG_ ## which(zero));	\
	/* Verify all bytes overwritten with 0xFF. */		\
	for (sum = 0, i = 0; i < target_size; i++)		\
		sum += (check_buf[i] != 0xFF);			\
	if (sum) {						\
		pr_err(#name ": leaf fill was not 0xFF!?\n");	\
		return 1;					\
	}							\
	/* Clear entire check buffer for later bit tests. */	\
	memset(check_buf, 0x00, sizeof(check_buf));		\
	/* Extract stack-defined variable contents. */		\
	ignored = leaf_ ##name((unsigned long)&ignored, 0,	\
				FETCH_ARG_ ## which(zero));	\
								\
	/* Validate that compiler lined up fill and target. */	\
	if (!range_contains(fill_start, fill_size,		\
			    target_start, target_size)) {	\
		pr_err(#name ": stack fill missed target!?\n");	\
		pr_err(#name ": fill %zu wide\n", fill_size);	\
		pr_err(#name ": target offset by %d\n",	\
			(int)((ssize_t)(uintptr_t)fill_start -	\
			(ssize_t)(uintptr_t)target_start));	\
		return 1;					\
	}							\
								\
	/* Look for any bytes still 0xFF in check region. */	\
	for (sum = 0, i = 0; i < target_size; i++)		\
		sum += (check_buf[i] == 0xFF);			\
								\
	if (sum == 0) {						\
		pr_info(#name " ok\n");				\
		return 0;					\
	} else {						\
		pr_warn(#name " %sFAIL (uninit bytes: %d)\n",	\
			(xfail) ? "X" : "", sum);		\
		return (xfail) ? 0 : 1;				\
	}							\
}
#define DEFINE_TEST(name, var_type, which, init_level, xfail)	\
/* no-op to force compiler into ignoring "uninitialized" vars */\
static noinline __init DO_NOTHING_TYPE_ ## which(var_type)	\
do_nothing_ ## name(var_type *ptr)				\
{								\
	/* Will always be true, but compiler doesn't know. */	\
	if ((unsigned long)ptr > 0x2)				\
		return DO_NOTHING_RETURN_ ## which(ptr);	\
	else							\
		return DO_NOTHING_RETURN_ ## which(ptr + 1);	\
}								\
static noinline __init int leaf_ ## name(unsigned long sp,	\
					 bool fill,		\
					 var_type *arg)		\
{								\
	char buf[VAR_BUFFER];					\
	var_type var						\
		INIT_ ## which ## _ ## init_level(var_type);	\
								\
	target_start = &var;					\
	target_size = sizeof(var);				\
	/*							\
	 * Keep this buffer around to make sure we've got a	\
	 * stack frame of SOME kind...				\
	 */							\
	memset(buf, (char)(sp & 0xff), sizeof(buf));		\
	/* Fill variable with 0xFF. */				\
	if (fill) {						\
		fill_start = &var;				\
		fill_size = sizeof(var);			\
		memset(fill_start,				\
		       (char)((sp & 0xff) | forced_mask),	\
		       fill_size);				\
	}							\
								\
	/* Silence "never initialized" warnings. */		\
	DO_NOTHING_CALL_ ## which(var, name);			\
								\
	/* Exfiltrate "var". */					\
	memcpy(check_buf, target_start, target_size);		\
								\
	return (int)buf[0] | (int)buf[sizeof(buf) - 1];		\
}								\
DEFINE_TEST_DRIVER(name, var_type, which, xfail)

/* Structure with no padding. */
struct test_packed {
	unsigned long one;
	unsigned long two;
	unsigned long three;
	unsigned long four;
};

/* Simple structure with padding likely to be covered by compiler. */
struct test_small_hole {
	size_t one;
	char two;
	/* 3 byte padding hole here. */
	int three;
	unsigned long four;
};

/* Trigger unhandled padding in a structure. */
struct test_big_hole {
	u8 one;
	u8 two;
	u8 three;
	/* 61 byte padding hole here. */
	u8 four __aligned(64);
} __aligned(64);

struct test_trailing_hole {
	char *one;
	char *two;
	char *three;
	char four;
	/* "sizeof(unsigned long) - 1" byte padding hole here. */
};

/* Test if STRUCTLEAK is clearing structs with __user fields. */
struct test_user {
	u8 one;
	unsigned long two;
	char __user *three;
	unsigned long four;
};

#define DEFINE_SCALAR_TEST(name, init, xfail)			\
		DEFINE_TEST(name ## _ ## init, name, SCALAR,	\
			    init, xfail)

#define DEFINE_SCALAR_TESTS(init, xfail)			\
		DEFINE_SCALAR_TEST(u8, init, xfail);		\
		DEFINE_SCALAR_TEST(u16, init, xfail);		\
		DEFINE_SCALAR_TEST(u32, init, xfail);		\
		DEFINE_SCALAR_TEST(u64, init, xfail);		\
		DEFINE_TEST(char_array_ ## init, unsigned char,	\
			    STRING, init, xfail)

#define DEFINE_STRUCT_TEST(name, init, xfail)			\
		DEFINE_TEST(name ## _ ## init,			\
			    struct test_ ## name, STRUCT, init, \
			    xfail)

#define DEFINE_STRUCT_TESTS(init, xfail)			\
		DEFINE_STRUCT_TEST(small_hole, init, xfail);	\
		DEFINE_STRUCT_TEST(big_hole, init, xfail);	\
		DEFINE_STRUCT_TEST(trailing_hole, init, xfail);	\
		DEFINE_STRUCT_TEST(packed, init, xfail)

#define DEFINE_STRUCT_INITIALIZER_TESTS(base)			\
		DEFINE_STRUCT_TESTS(base ## _ ## partial,	\
				    WANT_SUCCESS);		\
		DEFINE_STRUCT_TESTS(base ## _ ## all,		\
				    WANT_SUCCESS)

/* These should be fully initialized all the time! */
DEFINE_SCALAR_TESTS(zero, WANT_SUCCESS);
DEFINE_STRUCT_TESTS(zero, WANT_SUCCESS);
/* Struct initializers: padding may be left uninitialized. */
DEFINE_STRUCT_INITIALIZER_TESTS(static);
DEFINE_STRUCT_INITIALIZER_TESTS(dynamic);
DEFINE_STRUCT_INITIALIZER_TESTS(runtime);
DEFINE_STRUCT_INITIALIZER_TESTS(assigned_static);
DEFINE_STRUCT_INITIALIZER_TESTS(assigned_dynamic);
DEFINE_STRUCT_TESTS(assigned_copy, XFAIL);
/* No initialization without compiler instrumentation. */
DEFINE_SCALAR_TESTS(none, WANT_SUCCESS);
DEFINE_STRUCT_TESTS(none, WANT_SUCCESS);
/* Initialization of members with __user attribute. */
DEFINE_TEST(user, struct test_user, STRUCT, none, WANT_SUCCESS);

/*
 * Check two uses through a variable declaration outside either path,
 * which was noticed as a special case in porting earlier stack init
 * compiler logic.
 */
static int noinline __leaf_switch_none(int path, bool fill)
{
	switch (path) {
		/*
		 * This is intentionally unreachable. To silence the
		 * warning, build with -Wno-switch-unreachable
		 */
		uint64_t var;

	case 1:
		target_start = &var;
		target_size = sizeof(var);
		if (fill) {
			fill_start = &var;
			fill_size = sizeof(var);

			memset(fill_start, forced_mask | 0x55, fill_size);
		}
		memcpy(check_buf, target_start, target_size);
		break;
	case 2:
		target_start = &var;
		target_size = sizeof(var);
		if (fill) {
			fill_start = &var;
			fill_size = sizeof(var);

			memset(fill_start, forced_mask | 0xaa, fill_size);
		}
		memcpy(check_buf, target_start, target_size);
		break;
	default:
		var = 5;
		return var & forced_mask;
	}
	return 0;
}

static noinline __init int leaf_switch_1_none(unsigned long sp, bool fill,
					      uint64_t *arg)
{
	return __leaf_switch_none(1, fill);
}

static noinline __init int leaf_switch_2_none(unsigned long sp, bool fill,
					      uint64_t *arg)
{
	return __leaf_switch_none(2, fill);
}

/*
 * These are expected to fail for most configurations because neither
 * GCC nor Clang have a way to perform initialization of variables in
 * non-code areas (i.e. in a switch statement before the first "case").
 * https://bugs.llvm.org/show_bug.cgi?id=44916
 */
DEFINE_TEST_DRIVER(switch_1_none, uint64_t, SCALAR, XFAIL);
DEFINE_TEST_DRIVER(switch_2_none, uint64_t, SCALAR, XFAIL);

static int __init test_stackinit_init(void)
{
	unsigned int failures = 0;

#define test_scalars(init)	do {				\
		failures += test_u8_ ## init ();		\
		failures += test_u16_ ## init ();		\
		failures += test_u32_ ## init ();		\
		failures += test_u64_ ## init ();		\
		failures += test_char_array_ ## init ();	\
	} while (0)

#define test_structs(init)	do {				\
		failures += test_small_hole_ ## init ();	\
		failures += test_big_hole_ ## init ();		\
		failures += test_trailing_hole_ ## init ();	\
		failures += test_packed_ ## init ();		\
	} while (0)

	/* These are explicitly initialized and should always pass. */
	test_scalars(zero);
	test_structs(zero);
	/* Padding here appears to be accidentally always initialized? */
	test_structs(dynamic_partial);
	test_structs(assigned_dynamic_partial);
	/* Padding initialization depends on compiler behaviors. */
	test_structs(static_partial);
	test_structs(static_all);
	test_structs(dynamic_all);
	test_structs(runtime_partial);
	test_structs(runtime_all);
	test_structs(assigned_static_partial);
	test_structs(assigned_static_all);
	test_structs(assigned_dynamic_all);
	/* Everything fails this since it effectively performs a memcpy(). */
	test_structs(assigned_copy);

	/* STRUCTLEAK_BYREF_ALL should cover everything from here down. */
	test_scalars(none);
	failures += test_switch_1_none();
	failures += test_switch_2_none();

	/* STRUCTLEAK_BYREF should cover from here down. */
	test_structs(none);

	/* STRUCTLEAK will only cover this. */
	failures += test_user();

	if (failures == 0)
		pr_info("all tests passed!\n");
	else
		pr_err("failures: %u\n", failures);

	return failures ? -EINVAL : 0;
}
module_init(test_stackinit_init);

static void __exit test_stackinit_exit(void)
{ }
module_exit(test_stackinit_exit);

MODULE_LICENSE("GPL");
