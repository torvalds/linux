// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test cases for struct randomization, i.e. CONFIG_RANDSTRUCT=y.
 *
 * For example, see:
 * "Running tests with kunit_tool" at Documentation/dev-tools/kunit/start.rst
 *	./tools/testing/kunit/kunit.py run randstruct [--raw_output] \
 *		[--make_option LLVM=1] \
 *		--kconfig_add CONFIG_RANDSTRUCT_FULL=y
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define DO_MANY_MEMBERS(macro, args...)	\
	macro(a, args)			\
	macro(b, args)			\
	macro(c, args)			\
	macro(d, args)			\
	macro(e, args)			\
	macro(f, args)			\
	macro(g, args)			\
	macro(h, args)

#define do_enum(x, ignored)	MEMBER_NAME_ ## x,
enum randstruct_member_names {
	DO_MANY_MEMBERS(do_enum)
	MEMBER_NAME_MAX,
};
/* Make sure the macros are working: want 8 test members. */
_Static_assert(MEMBER_NAME_MAX == 8, "Number of test members changed?!");

/* This is an unsigned long member to match the function pointer size */
#define unsigned_long_member(x, ignored)	unsigned long x;
struct randstruct_untouched {
	DO_MANY_MEMBERS(unsigned_long_member)
};

/* Struct explicitly marked with __randomize_layout. */
struct randstruct_shuffled {
	DO_MANY_MEMBERS(unsigned_long_member)
} __randomize_layout;
#undef unsigned_long_member

/* Struct implicitly randomized from being all func ptrs. */
#define func_member(x, ignored)	size_t (*x)(int);
struct randstruct_funcs_untouched {
	DO_MANY_MEMBERS(func_member)
} __no_randomize_layout;

struct randstruct_funcs_shuffled {
	DO_MANY_MEMBERS(func_member)
};

#define func_body(x, ignored)					\
static noinline size_t func_##x(int arg)			\
{								\
	return offsetof(struct randstruct_funcs_untouched, x);	\
}
DO_MANY_MEMBERS(func_body)

/* Various mixed types. */
#define mixed_members					\
	bool a;						\
	short b;					\
	unsigned int c __aligned(16);			\
	size_t d;					\
	char e;						\
	u64 f;						\
	union {						\
		struct randstruct_shuffled shuffled;	\
		uintptr_t g;				\
	};						\
	union {						\
		void *ptr;				\
		char h;					\
	};

struct randstruct_mixed_untouched {
	mixed_members
};

struct randstruct_mixed_shuffled {
	mixed_members
} __randomize_layout;
#undef mixed_members

struct contains_randstruct_untouched {
	int before;
	struct randstruct_untouched untouched;
	int after;
};

struct contains_randstruct_shuffled {
	int before;
	struct randstruct_shuffled shuffled;
	int after;
};

struct contains_func_untouched {
	struct randstruct_funcs_shuffled inner;
	DO_MANY_MEMBERS(func_member)
} __no_randomize_layout;

struct contains_func_shuffled {
	struct randstruct_funcs_shuffled inner;
	DO_MANY_MEMBERS(func_member)
};
#undef func_member

#define check_mismatch(x, untouched, shuffled)	\
	if (offsetof(untouched, x) != offsetof(shuffled, x))	\
		mismatches++;					\
	kunit_info(test, #shuffled "::" #x " @ %zu (vs %zu)\n",	\
		   offsetof(shuffled, x),			\
		   offsetof(untouched, x));			\

#define check_pair(outcome, untouched, shuffled, checker...)	\
	mismatches = 0;						\
	DO_MANY_MEMBERS(checker, untouched, shuffled)	\
	kunit_info(test, "Differing " #untouched " vs " #shuffled " member positions: %d\n", \
		   mismatches);					\
	KUNIT_##outcome##_MSG(test, mismatches, 0,		\
			      #untouched " vs " #shuffled " layouts: unlucky or broken?\n");

static void randstruct_layout_same(struct kunit *test)
{
	int mismatches;

	check_pair(EXPECT_EQ, struct randstruct_untouched, struct randstruct_untouched,
		   check_mismatch)
	check_pair(EXPECT_GT, struct randstruct_untouched, struct randstruct_shuffled,
		   check_mismatch)
}

static void randstruct_layout_mixed(struct kunit *test)
{
	int mismatches;

	check_pair(EXPECT_EQ, struct randstruct_mixed_untouched, struct randstruct_mixed_untouched,
		   check_mismatch)
	check_pair(EXPECT_GT, struct randstruct_mixed_untouched, struct randstruct_mixed_shuffled,
		   check_mismatch)
}

static void randstruct_layout_fptr(struct kunit *test)
{
	int mismatches;

	check_pair(EXPECT_EQ, struct randstruct_untouched, struct randstruct_untouched,
		   check_mismatch)
	check_pair(EXPECT_GT, struct randstruct_untouched, struct randstruct_funcs_shuffled,
		   check_mismatch)
	check_pair(EXPECT_GT, struct randstruct_funcs_untouched, struct randstruct_funcs_shuffled,
		   check_mismatch)
}

#define check_mismatch_prefixed(x, prefix, untouched, shuffled)	\
	check_mismatch(prefix.x, untouched, shuffled)

static void randstruct_layout_fptr_deep(struct kunit *test)
{
	int mismatches;

	if (IS_ENABLED(CONFIG_CC_IS_CLANG))
		kunit_skip(test, "Clang randstruct misses inner functions: https://github.com/llvm/llvm-project/issues/138355");

	check_pair(EXPECT_EQ, struct contains_func_untouched, struct contains_func_untouched,
			check_mismatch_prefixed, inner)

	check_pair(EXPECT_GT, struct contains_func_untouched, struct contains_func_shuffled,
			check_mismatch_prefixed, inner)
}

#undef check_pair
#undef check_mismatch

#define check_mismatch(x, ignore)				\
	KUNIT_EXPECT_EQ_MSG(test, untouched->x, shuffled->x,	\
			    "Mismatched member value in %s initializer\n", \
			    name);

static void test_check_init(struct kunit *test, const char *name,
			    struct randstruct_untouched *untouched,
			    struct randstruct_shuffled *shuffled)
{
	DO_MANY_MEMBERS(check_mismatch)
}

static void test_check_mixed_init(struct kunit *test, const char *name,
				  struct randstruct_mixed_untouched *untouched,
				  struct randstruct_mixed_shuffled *shuffled)
{
	DO_MANY_MEMBERS(check_mismatch)
}
#undef check_mismatch

#define check_mismatch(x, ignore)				\
	KUNIT_EXPECT_EQ_MSG(test, untouched->untouched.x,	\
			    shuffled->shuffled.x,		\
			    "Mismatched member value in %s initializer\n", \
			    name);
static void test_check_contained_init(struct kunit *test, const char *name,
				      struct contains_randstruct_untouched *untouched,
				      struct contains_randstruct_shuffled *shuffled)
{
	DO_MANY_MEMBERS(check_mismatch)
}
#undef check_mismatch

#define check_mismatch(x, ignore)					\
	KUNIT_EXPECT_PTR_EQ_MSG(test, untouched->x, shuffled->x,	\
			    "Mismatched member value in %s initializer\n", \
			    name);

static void test_check_funcs_init(struct kunit *test, const char *name,
				  struct randstruct_funcs_untouched *untouched,
				  struct randstruct_funcs_shuffled *shuffled)
{
	DO_MANY_MEMBERS(check_mismatch)
}
#undef check_mismatch

static void randstruct_initializers(struct kunit *test)
{
#define init_members		\
		.a = 1,		\
		.b = 3,		\
		.c = 5,		\
		.d = 7,		\
		.e = 11,	\
		.f = 13,	\
		.g = 17,	\
		.h = 19,
	struct randstruct_untouched untouched = {
		init_members
	};
	struct randstruct_shuffled shuffled = {
		init_members
	};
	struct randstruct_mixed_untouched mixed_untouched = {
		init_members
	};
	struct randstruct_mixed_shuffled mixed_shuffled = {
		init_members
	};
	struct contains_randstruct_untouched contains_untouched = {
		.untouched = {
			init_members
		},
	};
	struct contains_randstruct_shuffled contains_shuffled = {
		.shuffled = {
			init_members
		},
	};
#define func_member(x, ignored)	\
		.x = func_##x,
	struct randstruct_funcs_untouched funcs_untouched = {
		DO_MANY_MEMBERS(func_member)
	};
	struct randstruct_funcs_shuffled funcs_shuffled = {
		DO_MANY_MEMBERS(func_member)
	};

	test_check_init(test, "named", &untouched, &shuffled);
	test_check_init(test, "unnamed", &untouched,
		&(struct randstruct_shuffled){
			init_members
		});

	test_check_contained_init(test, "named", &contains_untouched, &contains_shuffled);
	test_check_contained_init(test, "unnamed", &contains_untouched,
		&(struct contains_randstruct_shuffled){
			.shuffled = (struct randstruct_shuffled){
				init_members
			},
		});

	test_check_contained_init(test, "named", &contains_untouched, &contains_shuffled);
	test_check_contained_init(test, "unnamed copy", &contains_untouched,
		&(struct contains_randstruct_shuffled){
			/* full struct copy initializer */
			.shuffled = shuffled,
		});

	test_check_mixed_init(test, "named", &mixed_untouched, &mixed_shuffled);
	test_check_mixed_init(test, "unnamed", &mixed_untouched,
		&(struct randstruct_mixed_shuffled){
			init_members
		});

	test_check_funcs_init(test, "named", &funcs_untouched, &funcs_shuffled);
	test_check_funcs_init(test, "unnamed", &funcs_untouched,
		&(struct randstruct_funcs_shuffled){
			DO_MANY_MEMBERS(func_member)
		});

#undef func_member
#undef init_members
}

static int randstruct_test_init(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_RANDSTRUCT))
		kunit_skip(test, "Not built with CONFIG_RANDSTRUCT=y");

	return 0;
}

static struct kunit_case randstruct_test_cases[] = {
	KUNIT_CASE(randstruct_layout_same),
	KUNIT_CASE(randstruct_layout_mixed),
	KUNIT_CASE(randstruct_layout_fptr),
	KUNIT_CASE(randstruct_layout_fptr_deep),
	KUNIT_CASE(randstruct_initializers),
	{}
};

static struct kunit_suite randstruct_test_suite = {
	.name = "randstruct",
	.init = randstruct_test_init,
	.test_cases = randstruct_test_cases,
};

kunit_test_suites(&randstruct_test_suite);

MODULE_DESCRIPTION("Test cases for struct randomization");
MODULE_LICENSE("GPL");
