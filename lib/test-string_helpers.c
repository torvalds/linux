/*
 * Test cases for lib/string_helpers.c module.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
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

	pr_warn("Test '%s' failed: flags = %u\n", name, flags);

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
	char in[256];
	char out_test[256];
	char out_real[256];
	int i, p = 0, q_test = 0, q_real = sizeof(out_real);

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
}

static int __init test_string_helpers_init(void)
{
	unsigned int i;

	pr_info("Running tests...\n");
	for (i = 0; i < UNESCAPE_ANY + 1; i++)
		test_string_unescape("unescape", i, false);
	test_string_unescape("unescape inplace",
			     get_random_int() % (UNESCAPE_ANY + 1), true);

	return -EINVAL;
}
module_init(test_string_helpers_init);
MODULE_LICENSE("Dual BSD/GPL");
