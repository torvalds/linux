/*
 * Test cases for lib/string_helpers.c module.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/string_helpers.h>

static __init bool test_string_check_buf(const char *name, unsigned int flags,
					 char *in, size_t p,
					 char *out_real, size_t q_real,
					 char *out_test, size_t q_test)
{
	if (q_real == q_test && !memcmp(out_test, out_real, q_test))
		return true;

	pr_warn("Test '%s' failed: flags = %#x\n", name, flags);

	print_hex_dump(KERN_WARNING, "Input: ", DUMP_PREFIX_NONE, 16, 1,
		       in, p, true);
	print_hex_dump(KERN_WARNING, "Expected: ", DUMP_PREFIX_NONE, 16, 1,
		       out_test, q_test, true);
	print_hex_dump(KERN_WARNING, "Got: ", DUMP_PREFIX_NONE, 16, 1,
		       out_real, q_real, true);

	return false;
}

struct test_string {
	const char *in;
	const char *out;
	unsigned int flags;
};

static const struct test_string strings[] __initconst = {
	{
		.in = "\\f\\ \\n\\r\\t\\v",
		.out = "\f\\ \n\r\t\v",
		.flags = UNESCAPE_SPACE,
	},
	{
		.in = "\\40\\1\\387\\0064\\05\\040\\8a\\110\\777",
		.out = " \001\00387\0064\005 \\8aH?7",
		.flags = UNESCAPE_OCTAL,
	},
	{
		.in = "\\xv\\xa\\x2c\\xD\\x6f2",
		.out = "\\xv\n,\ro2",
		.flags = UNESCAPE_HEX,
	},
	{
		.in = "\\h\\\\\\\"\\a\\e\\",
		.out = "\\h\\\"\a\e\\",
		.flags = UNESCAPE_SPECIAL,
	},
};

static void __init test_string_unescape(const char *name, unsigned int flags,
					bool inplace)
{
	int q_real = 256;
	char *in = kmalloc(q_real, GFP_KERNEL);
	char *out_test = kmalloc(q_real, GFP_KERNEL);
	char *out_real = kmalloc(q_real, GFP_KERNEL);
	int i, p = 0, q_test = 0;

	if (!in || !out_test || !out_real)
		goto out;

	for (i = 0; i < ARRAY_SIZE(strings); i++) {
		const char *s = strings[i].in;
		int len = strlen(strings[i].in);

		/* Copy string to in buffer */
		memcpy(&in[p], s, len);
		p += len;

		/* Copy expected result for given flags */
		if (flags & strings[i].flags) {
			s = strings[i].out;
			len = strlen(strings[i].out);
		}
		memcpy(&out_test[q_test], s, len);
		q_test += len;
	}
	in[p++] = '\0';

	/* Call string_unescape and compare result */
	if (inplace) {
		memcpy(out_real, in, p);
		if (flags == UNESCAPE_ANY)
			q_real = string_unescape_any_inplace(out_real);
		else
			q_real = string_unescape_inplace(out_real, flags);
	} else if (flags == UNESCAPE_ANY) {
		q_real = string_unescape_any(in, out_real, q_real);
	} else {
		q_real = string_unescape(in, out_real, q_real, flags);
	}

	test_string_check_buf(name, flags, in, p - 1, out_real, q_real,
			      out_test, q_test);
out:
	kfree(out_real);
	kfree(out_test);
	kfree(in);
}

struct test_string_1 {
	const char *out;
	unsigned int flags;
};

#define	TEST_STRING_2_MAX_S1		32
struct test_string_2 {
	const char *in;
	struct test_string_1 s1[TEST_STRING_2_MAX_S1];
};

#define	TEST_STRING_2_DICT_0		NULL
static const struct test_string_2 escape0[] __initconst = {{
	.in = "\f\\ \n\r\t\v",
	.s1 = {{
		.out = "\\f\\ \\n\\r\\t\\v",
		.flags = ESCAPE_SPACE,
	},{
		.out = "\\f\\134\\040\\n\\r\\t\\v",
		.flags = ESCAPE_SPACE | ESCAPE_OCTAL,
	},{
		.out = "\\f\\x5c\\x20\\n\\r\\t\\v",
		.flags = ESCAPE_SPACE | ESCAPE_HEX,
	},{
		/* terminator */
	}}
},{
	.in = "\\h\\\"\a\e\\",
	.s1 = {{
		.out = "\\\\h\\\\\"\\a\\e\\\\",
		.flags = ESCAPE_SPECIAL,
	},{
		.out = "\\\\\\150\\\\\\042\\a\\e\\\\",
		.flags = ESCAPE_SPECIAL | ESCAPE_OCTAL,
	},{
		.out = "\\\\\\x68\\\\\\x22\\a\\e\\\\",
		.flags = ESCAPE_SPECIAL | ESCAPE_HEX,
	},{
		/* terminator */
	}}
},{
	.in = "\eb \\C\007\"\x90\r]",
	.s1 = {{
		.out = "\eb \\C\007\"\x90\\r]",
		.flags = ESCAPE_SPACE,
	},{
		.out = "\\eb \\\\C\\a\"\x90\r]",
		.flags = ESCAPE_SPECIAL,
	},{
		.out = "\\eb \\\\C\\a\"\x90\\r]",
		.flags = ESCAPE_SPACE | ESCAPE_SPECIAL,
	},{
		.out = "\\033\\142\\040\\134\\103\\007\\042\\220\\015\\135",
		.flags = ESCAPE_OCTAL,
	},{
		.out = "\\033\\142\\040\\134\\103\\007\\042\\220\\r\\135",
		.flags = ESCAPE_SPACE | ESCAPE_OCTAL,
	},{
		.out = "\\e\\142\\040\\\\\\103\\a\\042\\220\\015\\135",
		.flags = ESCAPE_SPECIAL | ESCAPE_OCTAL,
	},{
		.out = "\\e\\142\\040\\\\\\103\\a\\042\\220\\r\\135",
		.flags = ESCAPE_SPACE | ESCAPE_SPECIAL | ESCAPE_OCTAL,
	},{
		.out = "\eb \\C\007\"\x90\r]",
		.flags = ESCAPE_NP,
	},{
		.out = "\eb \\C\007\"\x90\\r]",
		.flags = ESCAPE_SPACE | ESCAPE_NP,
	},{
		.out = "\\eb \\C\\a\"\x90\r]",
		.flags = ESCAPE_SPECIAL | ESCAPE_NP,
	},{
		.out = "\\eb \\C\\a\"\x90\\r]",
		.flags = ESCAPE_SPACE | ESCAPE_SPECIAL | ESCAPE_NP,
	},{
		.out = "\\033b \\C\\007\"\\220\\015]",
		.flags = ESCAPE_OCTAL | ESCAPE_NP,
	},{
		.out = "\\033b \\C\\007\"\\220\\r]",
		.flags = ESCAPE_SPACE | ESCAPE_OCTAL | ESCAPE_NP,
	},{
		.out = "\\eb \\C\\a\"\\220\\r]",
		.flags = ESCAPE_SPECIAL | ESCAPE_SPACE | ESCAPE_OCTAL |
			 ESCAPE_NP,
	},{
		.out = "\\x1bb \\C\\x07\"\\x90\\x0d]",
		.flags = ESCAPE_NP | ESCAPE_HEX,
	},{
		/* terminator */
	}}
},{
	/* terminator */
}};

#define	TEST_STRING_2_DICT_1		"b\\ \t\r"
static const struct test_string_2 escape1[] __initconst = {{
	.in = "\f\\ \n\r\t\v",
	.s1 = {{
		.out = "\f\\134\\040\n\\015\\011\v",
		.flags = ESCAPE_OCTAL,
	},{
		.out = "\f\\x5c\\x20\n\\x0d\\x09\v",
		.flags = ESCAPE_HEX,
	},{
		/* terminator */
	}}
},{
	.in = "\\h\\\"\a\e\\",
	.s1 = {{
		.out = "\\134h\\134\"\a\e\\134",
		.flags = ESCAPE_OCTAL,
	},{
		/* terminator */
	}}
},{
	.in = "\eb \\C\007\"\x90\r]",
	.s1 = {{
		.out = "\e\\142\\040\\134C\007\"\x90\\015]",
		.flags = ESCAPE_OCTAL,
	},{
		/* terminator */
	}}
},{
	/* terminator */
}};

static const struct test_string strings_upper[] __initconst = {
	{
		.in = "abcdefgh1234567890test",
		.out = "ABCDEFGH1234567890TEST",
	},
	{
		.in = "abCdeFgH1234567890TesT",
		.out = "ABCDEFGH1234567890TEST",
	},
};

static const struct test_string strings_lower[] __initconst = {
	{
		.in = "ABCDEFGH1234567890TEST",
		.out = "abcdefgh1234567890test",
	},
	{
		.in = "abCdeFgH1234567890TesT",
		.out = "abcdefgh1234567890test",
	},
};

static __init const char *test_string_find_match(const struct test_string_2 *s2,
						 unsigned int flags)
{
	const struct test_string_1 *s1 = s2->s1;
	unsigned int i;

	if (!flags)
		return s2->in;

	/* Test cases are NULL-aware */
	flags &= ~ESCAPE_NULL;

	/* ESCAPE_OCTAL has a higher priority */
	if (flags & ESCAPE_OCTAL)
		flags &= ~ESCAPE_HEX;

	for (i = 0; i < TEST_STRING_2_MAX_S1 && s1->out; i++, s1++)
		if (s1->flags == flags)
			return s1->out;
	return NULL;
}

static __init void
test_string_escape_overflow(const char *in, int p, unsigned int flags, const char *esc,
			    int q_test, const char *name)
{
	int q_real;

	q_real = string_escape_mem(in, p, NULL, 0, flags, esc);
	if (q_real != q_test)
		pr_warn("Test '%s' failed: flags = %#x, osz = 0, expected %d, got %d\n",
			name, flags, q_test, q_real);
}

static __init void test_string_escape(const char *name,
				      const struct test_string_2 *s2,
				      unsigned int flags, const char *esc)
{
	size_t out_size = 512;
	char *out_test = kmalloc(out_size, GFP_KERNEL);
	char *out_real = kmalloc(out_size, GFP_KERNEL);
	char *in = kmalloc(256, GFP_KERNEL);
	int p = 0, q_test = 0;
	int q_real;

	if (!out_test || !out_real || !in)
		goto out;

	for (; s2->in; s2++) {
		const char *out;
		int len;

		/* NULL injection */
		if (flags & ESCAPE_NULL) {
			in[p++] = '\0';
			out_test[q_test++] = '\\';
			out_test[q_test++] = '0';
		}

		/* Don't try strings that have no output */
		out = test_string_find_match(s2, flags);
		if (!out)
			continue;

		/* Copy string to in buffer */
		len = strlen(s2->in);
		memcpy(&in[p], s2->in, len);
		p += len;

		/* Copy expected result for given flags */
		len = strlen(out);
		memcpy(&out_test[q_test], out, len);
		q_test += len;
	}

	q_real = string_escape_mem(in, p, out_real, out_size, flags, esc);

	test_string_check_buf(name, flags, in, p, out_real, q_real, out_test,
			      q_test);

	test_string_escape_overflow(in, p, flags, esc, q_test, name);

out:
	kfree(in);
	kfree(out_real);
	kfree(out_test);
}

#define string_get_size_maxbuf 16
#define test_string_get_size_one(size, blk_size, exp_result10, exp_result2)    \
	do {                                                                   \
		BUILD_BUG_ON(sizeof(exp_result10) >= string_get_size_maxbuf);  \
		BUILD_BUG_ON(sizeof(exp_result2) >= string_get_size_maxbuf);   \
		__test_string_get_size((size), (blk_size), (exp_result10),     \
				       (exp_result2));                         \
	} while (0)


static __init void test_string_get_size_check(const char *units,
					      const char *exp,
					      char *res,
					      const u64 size,
					      const u64 blk_size)
{
	if (!memcmp(res, exp, strlen(exp) + 1))
		return;

	res[string_get_size_maxbuf - 1] = '\0';

	pr_warn("Test 'test_string_get_size' failed!\n");
	pr_warn("string_get_size(size = %llu, blk_size = %llu, units = %s)\n",
		size, blk_size, units);
	pr_warn("expected: '%s', got '%s'\n", exp, res);
}

static __init void __test_string_get_size(const u64 size, const u64 blk_size,
					  const char *exp_result10,
					  const char *exp_result2)
{
	char buf10[string_get_size_maxbuf];
	char buf2[string_get_size_maxbuf];

	string_get_size(size, blk_size, STRING_UNITS_10, buf10, sizeof(buf10));
	string_get_size(size, blk_size, STRING_UNITS_2, buf2, sizeof(buf2));

	test_string_get_size_check("STRING_UNITS_10", exp_result10, buf10,
				   size, blk_size);

	test_string_get_size_check("STRING_UNITS_2", exp_result2, buf2,
				   size, blk_size);
}

static __init void test_string_get_size(void)
{
	/* small values */
	test_string_get_size_one(0, 512, "0 B", "0 B");
	test_string_get_size_one(1, 512, "512 B", "512 B");
	test_string_get_size_one(1100, 1, "1.10 kB", "1.07 KiB");

	/* normal values */
	test_string_get_size_one(16384, 512, "8.39 MB", "8.00 MiB");
	test_string_get_size_one(500118192, 512, "256 GB", "238 GiB");
	test_string_get_size_one(8192, 4096, "33.6 MB", "32.0 MiB");

	/* weird block sizes */
	test_string_get_size_one(3000, 1900, "5.70 MB", "5.44 MiB");

	/* huge values */
	test_string_get_size_one(U64_MAX, 4096, "75.6 ZB", "64.0 ZiB");
	test_string_get_size_one(4096, U64_MAX, "75.6 ZB", "64.0 ZiB");
}

static void __init test_string_upper_lower(void)
{
	char *dst;
	int i;

	for (i = 0; i < ARRAY_SIZE(strings_upper); i++) {
		const char *s = strings_upper[i].in;
		int len = strlen(strings_upper[i].in) + 1;

		dst = kmalloc(len, GFP_KERNEL);
		if (!dst)
			return;

		string_upper(dst, s);
		if (memcmp(dst, strings_upper[i].out, len)) {
			pr_warn("Test 'string_upper' failed : expected %s, got %s!\n",
				strings_upper[i].out, dst);
			kfree(dst);
			return;
		}
		kfree(dst);
	}

	for (i = 0; i < ARRAY_SIZE(strings_lower); i++) {
		const char *s = strings_lower[i].in;
		int len = strlen(strings_lower[i].in) + 1;

		dst = kmalloc(len, GFP_KERNEL);
		if (!dst)
			return;

		string_lower(dst, s);
		if (memcmp(dst, strings_lower[i].out, len)) {
			pr_warn("Test 'string_lower failed : : expected %s, got %s!\n",
				strings_lower[i].out, dst);
			kfree(dst);
			return;
		}
		kfree(dst);
	}
}

static int __init test_string_helpers_init(void)
{
	unsigned int i;

	pr_info("Running tests...\n");
	for (i = 0; i < UNESCAPE_ANY + 1; i++)
		test_string_unescape("unescape", i, false);
	test_string_unescape("unescape inplace",
			     get_random_int() % (UNESCAPE_ANY + 1), true);

	/* Without dictionary */
	for (i = 0; i < (ESCAPE_ANY_NP | ESCAPE_HEX) + 1; i++)
		test_string_escape("escape 0", escape0, i, TEST_STRING_2_DICT_0);

	/* With dictionary */
	for (i = 0; i < (ESCAPE_ANY_NP | ESCAPE_HEX) + 1; i++)
		test_string_escape("escape 1", escape1, i, TEST_STRING_2_DICT_1);

	/* Test string_get_size() */
	test_string_get_size();

	/* Test string upper(), string_lower() */
	test_string_upper_lower();

	return -EINVAL;
}
module_init(test_string_helpers_init);
MODULE_LICENSE("Dual BSD/GPL");
