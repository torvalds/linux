// SPDX-Licenses: GPLv2
/*
 * Test cases for compiler-based stack variable zeroing via future
 * compiler flags or CONFIG_GCC_PLUGIN_STRUCTLEAK*.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

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

#define INIT_SCALAR_none		/**/
#define INIT_SCALAR_zero		= 0

#define INIT_STRING_none		[FILL_SIZE_STRING] /**/
#define INIT_STRING_zero		[FILL_SIZE_STRING] = { }

#define INIT_STRUCT_none		/**/
#define INIT_STRUCT_zero		= { }
#define INIT_STRUCT_static_partial	= { .two = 0, }
#define INIT_STRUCT_static_all		= { .one = arg->one,		\
					    .two = arg->two,		\
					    .three = arg->three,	\
					    .four = arg->four,		\
					}
#define INIT_STRUCT_dynamic_partial	= { .two = arg->two, }
#define INIT_STRUCT_dynamic_all		= { .one = arg->one,		\
					    .two = arg->two,		\
					    .three = arg->three,	\
					    .four = arg->four,		\
					}
#define INIT_STRUCT_runtime_partial	;				\
					var.two = 0
#define INIT_STRUCT_runtime_all		;				\
					var.one = 0;			\
					var.two = 0;			\
					var.three = 0;			\
					memset(&var.four, 0,		\
					       sizeof(var.four))

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
	memset(&zero, 0x00, sizeof(zero));			\
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
#define DEFINE_TEST(name, var_type, which, init_level)		\
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
	var_type var INIT_ ## which ## _ ## init_level;		\
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
DEFINE_TEST_DRIVER(name, var_type, which, 0)

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

/* Try to trigger unhandled padding in a structure. */
struct test_aligned {
	u32 internal1;
	u64 internal2;
} __aligned(64);

struct test_big_hole {
	u8 one;
	u8 two;
	u8 three;
	/* 61 byte padding hole here. */
	struct test_aligned four;
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

#define DEFINE_SCALAR_TEST(name, init)				\
		DEFINE_TEST(name ## _ ## init, name, SCALAR, init)

#define DEFINE_SCALAR_TESTS(init)				\
		DEFINE_SCALAR_TEST(u8, init);			\
		DEFINE_SCALAR_TEST(u16, init);			\
		DEFINE_SCALAR_TEST(u32, init);			\
		DEFINE_SCALAR_TEST(u64, init);			\
		DEFINE_TEST(char_array_ ## init, unsigned char, STRING, init)

#define DEFINE_STRUCT_TEST(name, init)				\
		DEFINE_TEST(name ## _ ## init,			\
			    struct test_ ## name, STRUCT, init)

#define DEFINE_STRUCT_TESTS(init)				\
		DEFINE_STRUCT_TEST(small_hole, init);		\
		DEFINE_STRUCT_TEST(big_hole, init);		\
		DEFINE_STRUCT_TEST(trailing_hole, init);	\
		DEFINE_STRUCT_TEST(packed, init)

/* These should be fully initialized all the time! */
DEFINE_SCALAR_TESTS(zero);
DEFINE_STRUCT_TESTS(zero);
/* Static initialization: padding may be left uninitialized. */
DEFINE_STRUCT_TESTS(static_partial);
DEFINE_STRUCT_TESTS(static_all);
/* Dynamic initialization: padding may be left uninitialized. */
DEFINE_STRUCT_TESTS(dynamic_partial);
DEFINE_STRUCT_TESTS(dynamic_all);
/* Runtime initialization: padding may be left uninitialized. */
DEFINE_STRUCT_TESTS(runtime_partial);
DEFINE_STRUCT_TESTS(runtime_all);
/* No initialization without compiler instrumentation. */
DEFINE_SCALAR_TESTS(none);
DEFINE_STRUCT_TESTS(none);
DEFINE_TEST(user, struct test_user, STRUCT, none);

/*
 * Check two uses through a variable declaration outside either path,
 * which was noticed as a special case in porting earlier stack init
 * compiler logic.
 */
static int noinline __leaf_switch_none(int path, bool fill)
{
	switch (path) {
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
DEFINE_TEST_DRIVER(switch_1_none, uint64_t, SCALAR, 1);
DEFINE_TEST_DRIVER(switch_2_none, uint64_t, SCALAR, 1);

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
	/* Padding initialization depends on compiler behaviors. */
	test_structs(static_partial);
	test_structs(static_all);
	test_structs(dynamic_all);
	test_structs(runtime_partial);
	test_structs(runtime_all);

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
