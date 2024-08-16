// SPDX-License-Identifier: GPL-2.0
/*
 * Runtime test cases for CONFIG_FORTIFY_SOURCE. For additional memcpy()
 * testing see FORTIFY_MEM_* tests in LKDTM (drivers/misc/lkdtm/fortify.c).
 *
 * For corner cases with UBSAN, try testing with:
 *
 * ./tools/testing/kunit/kunit.py run --arch=x86_64 \
 *	--kconfig_add CONFIG_FORTIFY_SOURCE=y \
 *	--kconfig_add CONFIG_UBSAN=y \
 *	--kconfig_add CONFIG_UBSAN_TRAP=y \
 *	--kconfig_add CONFIG_UBSAN_BOUNDS=y \
 *	--kconfig_add CONFIG_UBSAN_LOCAL_BOUNDS=y \
 *	--make_options LLVM=1 fortify
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* We don't need to fill dmesg with the fortify WARNs during testing. */
#ifdef DEBUG
# define FORTIFY_REPORT_KUNIT(x...) __fortify_report(x)
# define FORTIFY_WARN_KUNIT(x...)   WARN_ONCE(x)
#else
# define FORTIFY_REPORT_KUNIT(x...) do { } while (0)
# define FORTIFY_WARN_KUNIT(x...)   do { } while (0)
#endif

/* Redefine fortify_panic() to track failures. */
void fortify_add_kunit_error(int write);
#define fortify_panic(func, write, avail, size, retfail) do {		\
	FORTIFY_REPORT_KUNIT(FORTIFY_REASON(func, write), avail, size);	\
	fortify_add_kunit_error(write);					\
	return (retfail);						\
} while (0)

/* Redefine fortify_warn_once() to track memcpy() failures. */
#define fortify_warn_once(chk_func, x...) do {				\
	bool __result = chk_func;					\
	FORTIFY_WARN_KUNIT(__result, x);				\
	if (__result)							\
		fortify_add_kunit_error(1);				\
} while (0)

#include <kunit/device.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

/* Handle being built without CONFIG_FORTIFY_SOURCE */
#ifndef __compiletime_strlen
# define __compiletime_strlen __builtin_strlen
#endif

static struct kunit_resource read_resource;
static struct kunit_resource write_resource;
static int fortify_read_overflows;
static int fortify_write_overflows;

static const char array_of_10[] = "this is 10";
static const char *ptr_of_11 = "this is 11!";
static char array_unknown[] = "compiler thinks I might change";

void fortify_add_kunit_error(int write)
{
	struct kunit_resource *resource;
	struct kunit *current_test;

	current_test = kunit_get_current_test();
	if (!current_test)
		return;

	resource = kunit_find_named_resource(current_test,
			write ? "fortify_write_overflows"
			      : "fortify_read_overflows");
	if (!resource)
		return;

	(*(int *)resource->data)++;
	kunit_put_resource(resource);
}

static void fortify_test_known_sizes(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, __compiletime_strlen("88888888"), 8);
	KUNIT_EXPECT_EQ(test, __compiletime_strlen(array_of_10), 10);
	KUNIT_EXPECT_EQ(test, __compiletime_strlen(ptr_of_11), 11);

	KUNIT_EXPECT_EQ(test, __compiletime_strlen(array_unknown), SIZE_MAX);
	/* Externally defined and dynamically sized string pointer: */
	KUNIT_EXPECT_EQ(test, __compiletime_strlen(test->name), SIZE_MAX);
}

/* This is volatile so the optimizer can't perform DCE below. */
static volatile int pick;

/* Not inline to keep optimizer from figuring out which string we want. */
static noinline size_t want_minus_one(int pick)
{
	const char *str;

	switch (pick) {
	case 1:
		str = "4444";
		break;
	case 2:
		str = "333";
		break;
	default:
		str = "1";
		break;
	}
	return __compiletime_strlen(str);
}

static void fortify_test_control_flow_split(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, want_minus_one(pick), SIZE_MAX);
}

#define KUNIT_EXPECT_BOS(test, p, expected, name)			\
	KUNIT_EXPECT_EQ_MSG(test, __builtin_object_size(p, 1),		\
		expected,						\
		"__alloc_size() not working with __bos on " name "\n")

#if !__has_builtin(__builtin_dynamic_object_size)
#define KUNIT_EXPECT_BDOS(test, p, expected, name)			\
	/* Silence "unused variable 'expected'" warning. */		\
	KUNIT_EXPECT_EQ(test, expected, expected)
#else
#define KUNIT_EXPECT_BDOS(test, p, expected, name)			\
	KUNIT_EXPECT_EQ_MSG(test, __builtin_dynamic_object_size(p, 1),	\
		expected,						\
		"__alloc_size() not working with __bdos on " name "\n")
#endif

/* If the execpted size is a constant value, __bos can see it. */
#define check_const(_expected, alloc, free)		do {		\
	size_t expected = (_expected);					\
	void *p = alloc;						\
	KUNIT_EXPECT_TRUE_MSG(test, p != NULL, #alloc " failed?!\n");	\
	KUNIT_EXPECT_BOS(test, p, expected, #alloc);			\
	KUNIT_EXPECT_BDOS(test, p, expected, #alloc);			\
	free;								\
} while (0)

/* If the execpted size is NOT a constant value, __bos CANNOT see it. */
#define check_dynamic(_expected, alloc, free)		do {		\
	size_t expected = (_expected);					\
	void *p = alloc;						\
	KUNIT_EXPECT_TRUE_MSG(test, p != NULL, #alloc " failed?!\n");	\
	KUNIT_EXPECT_BOS(test, p, SIZE_MAX, #alloc);			\
	KUNIT_EXPECT_BDOS(test, p, expected, #alloc);			\
	free;								\
} while (0)

/* Assortment of constant-value kinda-edge cases. */
#define CONST_TEST_BODY(TEST_alloc)	do {				\
	/* Special-case vmalloc()-family to skip 0-sized allocs. */	\
	if (strcmp(#TEST_alloc, "TEST_vmalloc") != 0)			\
		TEST_alloc(check_const, 0, 0);				\
	TEST_alloc(check_const, 1, 1);					\
	TEST_alloc(check_const, 128, 128);				\
	TEST_alloc(check_const, 1023, 1023);				\
	TEST_alloc(check_const, 1025, 1025);				\
	TEST_alloc(check_const, 4096, 4096);				\
	TEST_alloc(check_const, 4097, 4097);				\
} while (0)

static volatile size_t zero_size;
static volatile size_t unknown_size = 50;

#if !__has_builtin(__builtin_dynamic_object_size)
#define DYNAMIC_TEST_BODY(TEST_alloc)					\
	kunit_skip(test, "Compiler is missing __builtin_dynamic_object_size() support\n")
#else
#define DYNAMIC_TEST_BODY(TEST_alloc)	do {				\
	size_t size = unknown_size;					\
									\
	/*								\
	 * Expected size is "size" in each test, before it is then	\
	 * internally incremented in each test.	Requires we disable	\
	 * -Wunsequenced.						\
	 */								\
	TEST_alloc(check_dynamic, size, size++);			\
	/* Make sure incrementing actually happened. */			\
	KUNIT_EXPECT_NE(test, size, unknown_size);			\
} while (0)
#endif

#define DEFINE_ALLOC_SIZE_TEST_PAIR(allocator)				\
static void fortify_test_alloc_size_##allocator##_const(struct kunit *test) \
{									\
	CONST_TEST_BODY(TEST_##allocator);				\
}									\
static void fortify_test_alloc_size_##allocator##_dynamic(struct kunit *test) \
{									\
	DYNAMIC_TEST_BODY(TEST_##allocator);				\
}

#define TEST_kmalloc(checker, expected_size, alloc_size)	do {	\
	gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;				\
	void *orig;							\
	size_t len;							\
									\
	checker(expected_size, kmalloc(alloc_size, gfp),		\
		kfree(p));						\
	checker(expected_size,						\
		kmalloc_node(alloc_size, gfp, NUMA_NO_NODE),		\
		kfree(p));						\
	checker(expected_size, kzalloc(alloc_size, gfp),		\
		kfree(p));						\
	checker(expected_size,						\
		kzalloc_node(alloc_size, gfp, NUMA_NO_NODE),		\
		kfree(p));						\
	checker(expected_size, kcalloc(1, alloc_size, gfp),		\
		kfree(p));						\
	checker(expected_size, kcalloc(alloc_size, 1, gfp),		\
		kfree(p));						\
	checker(expected_size,						\
		kcalloc_node(1, alloc_size, gfp, NUMA_NO_NODE),		\
		kfree(p));						\
	checker(expected_size,						\
		kcalloc_node(alloc_size, 1, gfp, NUMA_NO_NODE),		\
		kfree(p));						\
	checker(expected_size, kmalloc_array(1, alloc_size, gfp),	\
		kfree(p));						\
	checker(expected_size, kmalloc_array(alloc_size, 1, gfp),	\
		kfree(p));						\
	checker(expected_size,						\
		kmalloc_array_node(1, alloc_size, gfp, NUMA_NO_NODE),	\
		kfree(p));						\
	checker(expected_size,						\
		kmalloc_array_node(alloc_size, 1, gfp, NUMA_NO_NODE),	\
		kfree(p));						\
									\
	orig = kmalloc(alloc_size, gfp);				\
	KUNIT_EXPECT_TRUE(test, orig != NULL);				\
	checker((expected_size) * 2,					\
		krealloc(orig, (alloc_size) * 2, gfp),			\
		kfree(p));						\
	orig = kmalloc(alloc_size, gfp);				\
	KUNIT_EXPECT_TRUE(test, orig != NULL);				\
	checker((expected_size) * 2,					\
		krealloc_array(orig, 1, (alloc_size) * 2, gfp),		\
		kfree(p));						\
	orig = kmalloc(alloc_size, gfp);				\
	KUNIT_EXPECT_TRUE(test, orig != NULL);				\
	checker((expected_size) * 2,					\
		krealloc_array(orig, (alloc_size) * 2, 1, gfp),		\
		kfree(p));						\
									\
	len = 11;							\
	/* Using memdup() with fixed size, so force unknown length. */	\
	if (!__builtin_constant_p(expected_size))			\
		len += zero_size;					\
	checker(len, kmemdup("hello there", len, gfp), kfree(p));	\
} while (0)
DEFINE_ALLOC_SIZE_TEST_PAIR(kmalloc)

/* Sizes are in pages, not bytes. */
#define TEST_vmalloc(checker, expected_pages, alloc_pages)	do {	\
	gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;				\
	checker((expected_pages) * PAGE_SIZE,				\
		vmalloc((alloc_pages) * PAGE_SIZE),	   vfree(p));	\
	checker((expected_pages) * PAGE_SIZE,				\
		vzalloc((alloc_pages) * PAGE_SIZE),	   vfree(p));	\
	checker((expected_pages) * PAGE_SIZE,				\
		__vmalloc((alloc_pages) * PAGE_SIZE, gfp), vfree(p));	\
} while (0)
DEFINE_ALLOC_SIZE_TEST_PAIR(vmalloc)

/* Sizes are in pages (and open-coded for side-effects), not bytes. */
#define TEST_kvmalloc(checker, expected_pages, alloc_pages)	do {	\
	gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;				\
	size_t prev_size;						\
	void *orig;							\
									\
	checker((expected_pages) * PAGE_SIZE,				\
		kvmalloc((alloc_pages) * PAGE_SIZE, gfp),		\
		kvfree(p));						\
	checker((expected_pages) * PAGE_SIZE,				\
		kvmalloc_node((alloc_pages) * PAGE_SIZE, gfp, NUMA_NO_NODE), \
		kvfree(p));						\
	checker((expected_pages) * PAGE_SIZE,				\
		kvzalloc((alloc_pages) * PAGE_SIZE, gfp),		\
		kvfree(p));						\
	checker((expected_pages) * PAGE_SIZE,				\
		kvzalloc_node((alloc_pages) * PAGE_SIZE, gfp, NUMA_NO_NODE), \
		kvfree(p));						\
	checker((expected_pages) * PAGE_SIZE,				\
		kvcalloc(1, (alloc_pages) * PAGE_SIZE, gfp),		\
		kvfree(p));						\
	checker((expected_pages) * PAGE_SIZE,				\
		kvcalloc((alloc_pages) * PAGE_SIZE, 1, gfp),		\
		kvfree(p));						\
	checker((expected_pages) * PAGE_SIZE,				\
		kvmalloc_array(1, (alloc_pages) * PAGE_SIZE, gfp),	\
		kvfree(p));						\
	checker((expected_pages) * PAGE_SIZE,				\
		kvmalloc_array((alloc_pages) * PAGE_SIZE, 1, gfp),	\
		kvfree(p));						\
									\
	prev_size = (expected_pages) * PAGE_SIZE;			\
	orig = kvmalloc(prev_size, gfp);				\
	KUNIT_EXPECT_TRUE(test, orig != NULL);				\
	checker(((expected_pages) * PAGE_SIZE) * 2,			\
		kvrealloc(orig, prev_size,				\
			  ((alloc_pages) * PAGE_SIZE) * 2, gfp),	\
		kvfree(p));						\
} while (0)
DEFINE_ALLOC_SIZE_TEST_PAIR(kvmalloc)

#define TEST_devm_kmalloc(checker, expected_size, alloc_size)	do {	\
	gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;				\
	const char dev_name[] = "fortify-test";				\
	struct device *dev;						\
	void *orig;							\
	size_t len;							\
									\
	/* Create dummy device for devm_kmalloc()-family tests. */	\
	dev = kunit_device_register(test, dev_name);			\
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR(dev),			\
			       "Cannot register test device\n");	\
									\
	checker(expected_size, devm_kmalloc(dev, alloc_size, gfp),	\
		devm_kfree(dev, p));					\
	checker(expected_size, devm_kzalloc(dev, alloc_size, gfp),	\
		devm_kfree(dev, p));					\
	checker(expected_size,						\
		devm_kmalloc_array(dev, 1, alloc_size, gfp),		\
		devm_kfree(dev, p));					\
	checker(expected_size,						\
		devm_kmalloc_array(dev, alloc_size, 1, gfp),		\
		devm_kfree(dev, p));					\
	checker(expected_size,						\
		devm_kcalloc(dev, 1, alloc_size, gfp),			\
		devm_kfree(dev, p));					\
	checker(expected_size,						\
		devm_kcalloc(dev, alloc_size, 1, gfp),			\
		devm_kfree(dev, p));					\
									\
	orig = devm_kmalloc(dev, alloc_size, gfp);			\
	KUNIT_EXPECT_TRUE(test, orig != NULL);				\
	checker((expected_size) * 2,					\
		devm_krealloc(dev, orig, (alloc_size) * 2, gfp),	\
		devm_kfree(dev, p));					\
									\
	len = 4;							\
	/* Using memdup() with fixed size, so force unknown length. */	\
	if (!__builtin_constant_p(expected_size))			\
		len += zero_size;					\
	checker(len, devm_kmemdup(dev, "Ohai", len, gfp),		\
		devm_kfree(dev, p));					\
									\
	kunit_device_unregister(test, dev);				\
} while (0)
DEFINE_ALLOC_SIZE_TEST_PAIR(devm_kmalloc)

static const char * const test_strs[] = {
	"",
	"Hello there",
	"A longer string, just for variety",
};

#define TEST_realloc(checker)	do {					\
	gfp_t gfp = GFP_KERNEL;						\
	size_t len;							\
	int i;								\
									\
	for (i = 0; i < ARRAY_SIZE(test_strs); i++) {			\
		len = strlen(test_strs[i]);				\
		KUNIT_EXPECT_EQ(test, __builtin_constant_p(len), 0);	\
		checker(len, kmemdup_array(test_strs[i], 1, len, gfp),	\
			kfree(p));					\
		checker(len, kmemdup(test_strs[i], len, gfp),		\
			kfree(p));					\
	}								\
} while (0)
static void fortify_test_realloc_size(struct kunit *test)
{
	TEST_realloc(check_dynamic);
}

/*
 * We can't have an array at the end of a structure or else
 * builds without -fstrict-flex-arrays=3 will report them as
 * being an unknown length. Additionally, add bytes before
 * and after the string to catch over/underflows if tests
 * fail.
 */
struct fortify_padding {
	unsigned long bytes_before;
	char buf[32];
	unsigned long bytes_after;
};
/* Force compiler into not being able to resolve size at compile-time. */
static volatile int unconst;

static void fortify_test_strlen(struct kunit *test)
{
	struct fortify_padding pad = { };
	int i, end = sizeof(pad.buf) - 1;

	/* Fill 31 bytes with valid characters. */
	for (i = 0; i < sizeof(pad.buf) - 1; i++)
		pad.buf[i] = i + '0';
	/* Trailing bytes are still %NUL. */
	KUNIT_EXPECT_EQ(test, pad.buf[end], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* String is terminated, so strlen() is valid. */
	KUNIT_EXPECT_EQ(test, strlen(pad.buf), end);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);

	/* Make string unterminated, and recount. */
	pad.buf[end] = 'A';
	end = sizeof(pad.buf);
	KUNIT_EXPECT_EQ(test, strlen(pad.buf), end);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
}

static void fortify_test_strnlen(struct kunit *test)
{
	struct fortify_padding pad = { };
	int i, end = sizeof(pad.buf) - 1;

	/* Fill 31 bytes with valid characters. */
	for (i = 0; i < sizeof(pad.buf) - 1; i++)
		pad.buf[i] = i + '0';
	/* Trailing bytes are still %NUL. */
	KUNIT_EXPECT_EQ(test, pad.buf[end], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* String is terminated, so strnlen() is valid. */
	KUNIT_EXPECT_EQ(test, strnlen(pad.buf, sizeof(pad.buf)), end);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	/* A truncated strnlen() will be safe, too. */
	KUNIT_EXPECT_EQ(test, strnlen(pad.buf, sizeof(pad.buf) / 2),
					sizeof(pad.buf) / 2);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);

	/* Make string unterminated, and recount. */
	pad.buf[end] = 'A';
	end = sizeof(pad.buf);
	/* Reading beyond with strncpy() will fail. */
	KUNIT_EXPECT_EQ(test, strnlen(pad.buf, end + 1), end);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
	KUNIT_EXPECT_EQ(test, strnlen(pad.buf, end + 2), end);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);

	/* Early-truncated is safe still, though. */
	KUNIT_EXPECT_EQ(test, strnlen(pad.buf, end), end);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);

	end = sizeof(pad.buf) / 2;
	KUNIT_EXPECT_EQ(test, strnlen(pad.buf, end), end);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);
}

static void fortify_test_strcpy(struct kunit *test)
{
	struct fortify_padding pad = { };
	char src[sizeof(pad.buf) + 1] = { };
	int i;

	/* Fill 31 bytes with valid characters. */
	for (i = 0; i < sizeof(src) - 2; i++)
		src[i] = i + '0';

	/* Destination is %NUL-filled to start with. */
	KUNIT_EXPECT_EQ(test, pad.bytes_before, 0);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Legitimate strcpy() 1 less than of max size. */
	KUNIT_ASSERT_TRUE(test, strcpy(pad.buf, src)
				== pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Only last byte should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	src[sizeof(src) - 2] = 'A';
	/* But now we trip the overflow checking. */
	KUNIT_ASSERT_TRUE(test, strcpy(pad.buf, src)
				== pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);
	/* Trailing %NUL -- thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	/* And we will not have gone beyond. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	src[sizeof(src) - 1] = 'A';
	/* And for sure now, two bytes past. */
	KUNIT_ASSERT_TRUE(test, strcpy(pad.buf, src)
				== pad.buf);
	/*
	 * Which trips both the strlen() on the unterminated src,
	 * and the resulting copy attempt.
	 */
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);
	/* Trailing %NUL -- thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	/* And we will not have gone beyond. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);
}

static void fortify_test_strncpy(struct kunit *test)
{
	struct fortify_padding pad = { };
	char src[] = "Copy me fully into a small buffer and I will overflow!";

	/* Destination is %NUL-filled to start with. */
	KUNIT_EXPECT_EQ(test, pad.bytes_before, 0);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Legitimate strncpy() 1 less than of max size. */
	KUNIT_ASSERT_TRUE(test, strncpy(pad.buf, src,
					sizeof(pad.buf) + unconst - 1)
				== pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Only last byte should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* Legitimate (though unterminated) max-size strncpy. */
	KUNIT_ASSERT_TRUE(test, strncpy(pad.buf, src,
					sizeof(pad.buf) + unconst)
				== pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* No trailing %NUL -- thanks strncpy API. */
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	/* But we will not have gone beyond. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Now verify that FORTIFY is working... */
	KUNIT_ASSERT_TRUE(test, strncpy(pad.buf, src,
					sizeof(pad.buf) + unconst + 1)
				== pad.buf);
	/* Should catch the overflow. */
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	/* And we will not have gone beyond. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* And further... */
	KUNIT_ASSERT_TRUE(test, strncpy(pad.buf, src,
					sizeof(pad.buf) + unconst + 2)
				== pad.buf);
	/* Should catch the overflow. */
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	/* And we will not have gone beyond. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);
}

static void fortify_test_strscpy(struct kunit *test)
{
	struct fortify_padding pad = { };
	char src[] = "Copy me fully into a small buffer and I will overflow!";

	/* Destination is %NUL-filled to start with. */
	KUNIT_EXPECT_EQ(test, pad.bytes_before, 0);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Legitimate strscpy() 1 less than of max size. */
	KUNIT_ASSERT_EQ(test, strscpy(pad.buf, src,
				      sizeof(pad.buf) + unconst - 1),
			-E2BIG);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Keeping space for %NUL, last two bytes should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* Legitimate max-size strscpy. */
	KUNIT_ASSERT_EQ(test, strscpy(pad.buf, src,
				      sizeof(pad.buf) + unconst),
			-E2BIG);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* A trailing %NUL will exist. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');

	/* Now verify that FORTIFY is working... */
	KUNIT_ASSERT_EQ(test, strscpy(pad.buf, src,
				      sizeof(pad.buf) + unconst + 1),
			-E2BIG);
	/* Should catch the overflow. */
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	/* And we will not have gone beyond. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* And much further... */
	KUNIT_ASSERT_EQ(test, strscpy(pad.buf, src,
				      sizeof(src) * 2 + unconst),
			-E2BIG);
	/* Should catch the overflow. */
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	/* And we will not have gone beyond. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);
}

static void fortify_test_strcat(struct kunit *test)
{
	struct fortify_padding pad = { };
	char src[sizeof(pad.buf) / 2] = { };
	char one[] = "A";
	char two[] = "BC";
	int i;

	/* Fill 15 bytes with valid characters. */
	for (i = 0; i < sizeof(src) - 1; i++)
		src[i] = i + 'A';

	/* Destination is %NUL-filled to start with. */
	KUNIT_EXPECT_EQ(test, pad.bytes_before, 0);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Legitimate strcat() using less than half max size. */
	KUNIT_ASSERT_TRUE(test, strcat(pad.buf, src) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Legitimate strcat() now 2 bytes shy of end. */
	KUNIT_ASSERT_TRUE(test, strcat(pad.buf, src) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Last two bytes should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* Add one more character to the end. */
	KUNIT_ASSERT_TRUE(test, strcat(pad.buf, one) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Last byte should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* And this one char will overflow. */
	KUNIT_ASSERT_TRUE(test, strcat(pad.buf, one) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);
	/* Last byte should be %NUL thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* And adding two will overflow more. */
	KUNIT_ASSERT_TRUE(test, strcat(pad.buf, two) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);
	/* Last byte should be %NUL thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);
}

static void fortify_test_strncat(struct kunit *test)
{
	struct fortify_padding pad = { };
	char src[sizeof(pad.buf)] = { };
	int i, partial;

	/* Fill 31 bytes with valid characters. */
	partial = sizeof(src) / 2 - 1;
	for (i = 0; i < partial; i++)
		src[i] = i + 'A';

	/* Destination is %NUL-filled to start with. */
	KUNIT_EXPECT_EQ(test, pad.bytes_before, 0);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Legitimate strncat() using less than half max size. */
	KUNIT_ASSERT_TRUE(test, strncat(pad.buf, src, partial) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Legitimate strncat() now 2 bytes shy of end. */
	KUNIT_ASSERT_TRUE(test, strncat(pad.buf, src, partial) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Last two bytes should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* Add one more character to the end. */
	KUNIT_ASSERT_TRUE(test, strncat(pad.buf, src, 1) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Last byte should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* And this one char will overflow. */
	KUNIT_ASSERT_TRUE(test, strncat(pad.buf, src, 1) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);
	/* Last byte should be %NUL thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* And adding two will overflow more. */
	KUNIT_ASSERT_TRUE(test, strncat(pad.buf, src, 2) == pad.buf);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);
	/* Last byte should be %NUL thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Force an unterminated destination, and overflow. */
	pad.buf[sizeof(pad.buf) - 1] = 'A';
	KUNIT_ASSERT_TRUE(test, strncat(pad.buf, src, 1) == pad.buf);
	/* This will have tripped both strlen() and strcat(). */
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 3);
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	/* But we should not go beyond the end. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);
}

static void fortify_test_strlcat(struct kunit *test)
{
	struct fortify_padding pad = { };
	char src[sizeof(pad.buf)] = { };
	int i, partial;
	int len = sizeof(pad.buf) + unconst;

	/* Fill 15 bytes with valid characters. */
	partial = sizeof(src) / 2 - 1;
	for (i = 0; i < partial; i++)
		src[i] = i + 'A';

	/* Destination is %NUL-filled to start with. */
	KUNIT_EXPECT_EQ(test, pad.bytes_before, 0);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Legitimate strlcat() using less than half max size. */
	KUNIT_ASSERT_EQ(test, strlcat(pad.buf, src, len), partial);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Legitimate strlcat() now 2 bytes shy of end. */
	KUNIT_ASSERT_EQ(test, strlcat(pad.buf, src, len), partial * 2);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Last two bytes should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* Add one more character to the end. */
	KUNIT_ASSERT_EQ(test, strlcat(pad.buf, "Q", len), partial * 2 + 1);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);
	/* Last byte should be %NUL */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');

	/* And this one char will overflow. */
	KUNIT_ASSERT_EQ(test, strlcat(pad.buf, "V", len * 2), len);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);
	/* Last byte should be %NUL thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* And adding two will overflow more. */
	KUNIT_ASSERT_EQ(test, strlcat(pad.buf, "QQ", len * 2), len + 1);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);
	/* Last byte should be %NUL thanks to FORTIFY. */
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Force an unterminated destination, and overflow. */
	pad.buf[sizeof(pad.buf) - 1] = 'A';
	KUNIT_ASSERT_EQ(test, strlcat(pad.buf, "TT", len * 2), len + 2);
	/* This will have tripped both strlen() and strlcat(). */
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 2], '\0');
	KUNIT_EXPECT_NE(test, pad.buf[sizeof(pad.buf) - 3], '\0');
	/* But we should not go beyond the end. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);

	/* Force an unterminated source, and overflow. */
	memset(src, 'B', sizeof(src));
	pad.buf[sizeof(pad.buf) - 1] = '\0';
	KUNIT_ASSERT_EQ(test, strlcat(pad.buf, src, len * 3), len - 1 + sizeof(src));
	/* This will have tripped both strlen() and strlcat(). */
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 3);
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 3);
	KUNIT_EXPECT_EQ(test, pad.buf[sizeof(pad.buf) - 1], '\0');
	/* But we should not go beyond the end. */
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);
}

/* Check for 0-sized arrays... */
struct fortify_zero_sized {
	unsigned long bytes_before;
	char buf[0];
	unsigned long bytes_after;
};

#define __fortify_test(memfunc)					\
static void fortify_test_##memfunc(struct kunit *test)		\
{								\
	struct fortify_zero_sized zero = { };			\
	struct fortify_padding pad = { };			\
	char srcA[sizeof(pad.buf) + 2];				\
	char srcB[sizeof(pad.buf) + 2];				\
	size_t len = sizeof(pad.buf) + unconst;			\
								\
	memset(srcA, 'A', sizeof(srcA));			\
	KUNIT_ASSERT_EQ(test, srcA[0], 'A');			\
	memset(srcB, 'B', sizeof(srcB));			\
	KUNIT_ASSERT_EQ(test, srcB[0], 'B');			\
								\
	memfunc(pad.buf, srcA, 0 + unconst);			\
	KUNIT_EXPECT_EQ(test, pad.buf[0], '\0');		\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);	\
	memfunc(pad.buf + 1, srcB, 1 + unconst);		\
	KUNIT_EXPECT_EQ(test, pad.buf[0], '\0');		\
	KUNIT_EXPECT_EQ(test, pad.buf[1], 'B');			\
	KUNIT_EXPECT_EQ(test, pad.buf[2], '\0');		\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);	\
	memfunc(pad.buf, srcA, 1 + unconst);			\
	KUNIT_EXPECT_EQ(test, pad.buf[0], 'A');			\
	KUNIT_EXPECT_EQ(test, pad.buf[1], 'B');			\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);	\
	memfunc(pad.buf, srcA, len - 1);			\
	KUNIT_EXPECT_EQ(test, pad.buf[1], 'A');			\
	KUNIT_EXPECT_EQ(test, pad.buf[len - 1], '\0');		\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);	\
	memfunc(pad.buf, srcA, len);				\
	KUNIT_EXPECT_EQ(test, pad.buf[1], 'A');			\
	KUNIT_EXPECT_EQ(test, pad.buf[len - 1], 'A');		\
	KUNIT_EXPECT_EQ(test, pad.bytes_after, 0);		\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);	\
	memfunc(pad.buf, srcA, len + 1);			\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);	\
	memfunc(pad.buf + 1, srcB, len);			\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 2);	\
								\
	/* Reset error counter. */				\
	fortify_write_overflows = 0;				\
	/* Copy nothing into nothing: no errors. */		\
	memfunc(zero.buf, srcB, 0 + unconst);			\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 0);	\
	memfunc(zero.buf, srcB, 1 + unconst);			\
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);	\
	KUNIT_EXPECT_EQ(test, fortify_write_overflows, 1);	\
}
__fortify_test(memcpy)
__fortify_test(memmove)

static void fortify_test_memscan(struct kunit *test)
{
	char haystack[] = "Where oh where is my memory range?";
	char *mem = haystack + strlen("Where oh where is ");
	char needle = 'm';
	size_t len = sizeof(haystack) + unconst;

	KUNIT_ASSERT_PTR_EQ(test, memscan(haystack, needle, len),
				  mem);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	/* Catch too-large range. */
	KUNIT_ASSERT_PTR_EQ(test, memscan(haystack, needle, len + 1),
				  NULL);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
	KUNIT_ASSERT_PTR_EQ(test, memscan(haystack, needle, len * 2),
				  NULL);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);
}

static void fortify_test_memchr(struct kunit *test)
{
	char haystack[] = "Where oh where is my memory range?";
	char *mem = haystack + strlen("Where oh where is ");
	char needle = 'm';
	size_t len = sizeof(haystack) + unconst;

	KUNIT_ASSERT_PTR_EQ(test, memchr(haystack, needle, len),
				  mem);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	/* Catch too-large range. */
	KUNIT_ASSERT_PTR_EQ(test, memchr(haystack, needle, len + 1),
				  NULL);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
	KUNIT_ASSERT_PTR_EQ(test, memchr(haystack, needle, len * 2),
				  NULL);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);
}

static void fortify_test_memchr_inv(struct kunit *test)
{
	char haystack[] = "Where oh where is my memory range?";
	char *mem = haystack + 1;
	char needle = 'W';
	size_t len = sizeof(haystack) + unconst;

	/* Normal search is okay. */
	KUNIT_ASSERT_PTR_EQ(test, memchr_inv(haystack, needle, len),
				  mem);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	/* Catch too-large range. */
	KUNIT_ASSERT_PTR_EQ(test, memchr_inv(haystack, needle, len + 1),
				  NULL);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
	KUNIT_ASSERT_PTR_EQ(test, memchr_inv(haystack, needle, len * 2),
				  NULL);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);
}

static void fortify_test_memcmp(struct kunit *test)
{
	char one[] = "My mind is going ...";
	char two[] = "My mind is going ... I can feel it.";
	size_t one_len = sizeof(one) + unconst - 1;
	size_t two_len = sizeof(two) + unconst - 1;

	/* We match the first string (ignoring the %NUL). */
	KUNIT_ASSERT_EQ(test, memcmp(one, two, one_len), 0);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	/* Still in bounds, but no longer matching. */
	KUNIT_ASSERT_LT(test, memcmp(one, two, one_len + 1), 0);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);

	/* Catch too-large ranges. */
	KUNIT_ASSERT_EQ(test, memcmp(one, two, one_len + 2), INT_MIN);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);

	KUNIT_ASSERT_EQ(test, memcmp(two, one, two_len + 2), INT_MIN);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);
}

static void fortify_test_kmemdup(struct kunit *test)
{
	char src[] = "I got Doom running on it!";
	char *copy;
	size_t len = sizeof(src) + unconst;

	/* Copy is within bounds. */
	copy = kmemdup(src, len, GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, copy);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	kfree(copy);

	/* Without %NUL. */
	copy = kmemdup(src, len - 1, GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, copy);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	kfree(copy);

	/* Tiny bounds. */
	copy = kmemdup(src, 1, GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, copy);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 0);
	kfree(copy);

	/* Out of bounds by 1 byte. */
	copy = kmemdup(src, len + 1, GFP_KERNEL);
	KUNIT_EXPECT_PTR_EQ(test, copy, ZERO_SIZE_PTR);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 1);
	kfree(copy);

	/* Way out of bounds. */
	copy = kmemdup(src, len * 2, GFP_KERNEL);
	KUNIT_EXPECT_PTR_EQ(test, copy, ZERO_SIZE_PTR);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 2);
	kfree(copy);

	/* Starting offset causing out of bounds. */
	copy = kmemdup(src + 1, len, GFP_KERNEL);
	KUNIT_EXPECT_PTR_EQ(test, copy, ZERO_SIZE_PTR);
	KUNIT_EXPECT_EQ(test, fortify_read_overflows, 3);
	kfree(copy);
}

static int fortify_test_init(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_FORTIFY_SOURCE))
		kunit_skip(test, "Not built with CONFIG_FORTIFY_SOURCE=y");

	fortify_read_overflows = 0;
	kunit_add_named_resource(test, NULL, NULL, &read_resource,
				 "fortify_read_overflows",
				 &fortify_read_overflows);
	fortify_write_overflows = 0;
	kunit_add_named_resource(test, NULL, NULL, &write_resource,
				 "fortify_write_overflows",
				 &fortify_write_overflows);
	return 0;
}

static struct kunit_case fortify_test_cases[] = {
	KUNIT_CASE(fortify_test_known_sizes),
	KUNIT_CASE(fortify_test_control_flow_split),
	KUNIT_CASE(fortify_test_alloc_size_kmalloc_const),
	KUNIT_CASE(fortify_test_alloc_size_kmalloc_dynamic),
	KUNIT_CASE(fortify_test_alloc_size_vmalloc_const),
	KUNIT_CASE(fortify_test_alloc_size_vmalloc_dynamic),
	KUNIT_CASE(fortify_test_alloc_size_kvmalloc_const),
	KUNIT_CASE(fortify_test_alloc_size_kvmalloc_dynamic),
	KUNIT_CASE(fortify_test_alloc_size_devm_kmalloc_const),
	KUNIT_CASE(fortify_test_alloc_size_devm_kmalloc_dynamic),
	KUNIT_CASE(fortify_test_realloc_size),
	KUNIT_CASE(fortify_test_strlen),
	KUNIT_CASE(fortify_test_strnlen),
	KUNIT_CASE(fortify_test_strcpy),
	KUNIT_CASE(fortify_test_strncpy),
	KUNIT_CASE(fortify_test_strscpy),
	KUNIT_CASE(fortify_test_strcat),
	KUNIT_CASE(fortify_test_strncat),
	KUNIT_CASE(fortify_test_strlcat),
	/* skip memset: performs bounds checking on whole structs */
	KUNIT_CASE(fortify_test_memcpy),
	KUNIT_CASE(fortify_test_memmove),
	KUNIT_CASE(fortify_test_memscan),
	KUNIT_CASE(fortify_test_memchr),
	KUNIT_CASE(fortify_test_memchr_inv),
	KUNIT_CASE(fortify_test_memcmp),
	KUNIT_CASE(fortify_test_kmemdup),
	{}
};

static struct kunit_suite fortify_test_suite = {
	.name = "fortify",
	.init = fortify_test_init,
	.test_cases = fortify_test_cases,
};

kunit_test_suite(fortify_test_suite);

MODULE_DESCRIPTION("Runtime test cases for CONFIG_FORTIFY_SOURCE");
MODULE_LICENSE("GPL");
