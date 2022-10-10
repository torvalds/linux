/*
 * Test cases for lib/hexdump.c module.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/string.h>

static const unsigned char data_b[] = {
	'\xbe', '\x32', '\xdb', '\x7b', '\x0a', '\x18', '\x93', '\xb2',	/* 00 - 07 */
	'\x70', '\xba', '\xc4', '\x24', '\x7d', '\x83', '\x34', '\x9b',	/* 08 - 0f */
	'\xa6', '\x9c', '\x31', '\xad', '\x9c', '\x0f', '\xac', '\xe9',	/* 10 - 17 */
	'\x4c', '\xd1', '\x19', '\x99', '\x43', '\xb1', '\xaf', '\x0c',	/* 18 - 1f */
};

static const unsigned char data_a[] = ".2.{....p..$}.4...1.....L...C...";

static const char * const test_data_1[] __initconst = {
	"be", "32", "db", "7b", "0a", "18", "93", "b2",
	"70", "ba", "c4", "24", "7d", "83", "34", "9b",
	"a6", "9c", "31", "ad", "9c", "0f", "ac", "e9",
	"4c", "d1", "19", "99", "43", "b1", "af", "0c",
};

static const char * const test_data_2_le[] __initconst = {
	"32be", "7bdb", "180a", "b293",
	"ba70", "24c4", "837d", "9b34",
	"9ca6", "ad31", "0f9c", "e9ac",
	"d14c", "9919", "b143", "0caf",
};

static const char * const test_data_2_be[] __initconst = {
	"be32", "db7b", "0a18", "93b2",
	"70ba", "c424", "7d83", "349b",
	"a69c", "31ad", "9c0f", "ace9",
	"4cd1", "1999", "43b1", "af0c",
};

static const char * const test_data_4_le[] __initconst = {
	"7bdb32be", "b293180a", "24c4ba70", "9b34837d",
	"ad319ca6", "e9ac0f9c", "9919d14c", "0cafb143",
};

static const char * const test_data_4_be[] __initconst = {
	"be32db7b", "0a1893b2", "70bac424", "7d83349b",
	"a69c31ad", "9c0face9", "4cd11999", "43b1af0c",
};

static const char * const test_data_8_le[] __initconst = {
	"b293180a7bdb32be", "9b34837d24c4ba70",
	"e9ac0f9cad319ca6", "0cafb1439919d14c",
};

static const char * const test_data_8_be[] __initconst = {
	"be32db7b0a1893b2", "70bac4247d83349b",
	"a69c31ad9c0face9", "4cd1199943b1af0c",
};

#define FILL_CHAR	'#'

static unsigned total_tests __initdata;
static unsigned failed_tests __initdata;

static void __init test_hexdump_prepare_test(size_t len, int rowsize,
					     int groupsize, char *test,
					     size_t testlen, bool ascii)
{
	char *p;
	const char * const *result;
	size_t l = len;
	int gs = groupsize, rs = rowsize;
	unsigned int i;
	const bool is_be = IS_ENABLED(CONFIG_CPU_BIG_ENDIAN);

	if (rs != 16 && rs != 32)
		rs = 16;

	if (l > rs)
		l = rs;

	if (!is_power_of_2(gs) || gs > 8 || (len % gs != 0))
		gs = 1;

	if (gs == 8)
		result = is_be ? test_data_8_be : test_data_8_le;
	else if (gs == 4)
		result = is_be ? test_data_4_be : test_data_4_le;
	else if (gs == 2)
		result = is_be ? test_data_2_be : test_data_2_le;
	else
		result = test_data_1;

	/* hex dump */
	p = test;
	for (i = 0; i < l / gs; i++) {
		const char *q = *result++;
		size_t amount = strlen(q);

		memcpy(p, q, amount);
		p += amount;

		*p++ = ' ';
	}
	if (i)
		p--;

	/* ASCII part */
	if (ascii) {
		do {
			*p++ = ' ';
		} while (p < test + rs * 2 + rs / gs + 1);

		strncpy(p, data_a, l);
		p += l;
	}

	*p = '\0';
}

#define TEST_HEXDUMP_BUF_SIZE		(32 * 3 + 2 + 32 + 1)

static void __init test_hexdump(size_t len, int rowsize, int groupsize,
				bool ascii)
{
	char test[TEST_HEXDUMP_BUF_SIZE];
	char real[TEST_HEXDUMP_BUF_SIZE];

	total_tests++;

	memset(real, FILL_CHAR, sizeof(real));
	hex_dump_to_buffer(data_b, len, rowsize, groupsize, real, sizeof(real),
			   ascii);

	memset(test, FILL_CHAR, sizeof(test));
	test_hexdump_prepare_test(len, rowsize, groupsize, test, sizeof(test),
				  ascii);

	if (memcmp(test, real, TEST_HEXDUMP_BUF_SIZE)) {
		pr_err("Len: %zu row: %d group: %d\n", len, rowsize, groupsize);
		pr_err("Result: '%s'\n", real);
		pr_err("Expect: '%s'\n", test);
		failed_tests++;
	}
}

static void __init test_hexdump_set(int rowsize, bool ascii)
{
	size_t d = min_t(size_t, sizeof(data_b), rowsize);
	size_t len = get_random_u32_inclusive(1, d);

	test_hexdump(len, rowsize, 4, ascii);
	test_hexdump(len, rowsize, 2, ascii);
	test_hexdump(len, rowsize, 8, ascii);
	test_hexdump(len, rowsize, 1, ascii);
}

static void __init test_hexdump_overflow(size_t buflen, size_t len,
					 int rowsize, int groupsize,
					 bool ascii)
{
	char test[TEST_HEXDUMP_BUF_SIZE];
	char buf[TEST_HEXDUMP_BUF_SIZE];
	int rs = rowsize, gs = groupsize;
	int ae, he, e, f, r;
	bool a;

	total_tests++;

	memset(buf, FILL_CHAR, sizeof(buf));

	r = hex_dump_to_buffer(data_b, len, rs, gs, buf, buflen, ascii);

	/*
	 * Caller must provide the data length multiple of groupsize. The
	 * calculations below are made with that assumption in mind.
	 */
	ae = rs * 2 /* hex */ + rs / gs /* spaces */ + 1 /* space */ + len /* ascii */;
	he = (gs * 2 /* hex */ + 1 /* space */) * len / gs - 1 /* no trailing space */;

	if (ascii)
		e = ae;
	else
		e = he;

	f = min_t(int, e + 1, buflen);
	if (buflen) {
		test_hexdump_prepare_test(len, rs, gs, test, sizeof(test), ascii);
		test[f - 1] = '\0';
	}
	memset(test + f, FILL_CHAR, sizeof(test) - f);

	a = r == e && !memcmp(test, buf, TEST_HEXDUMP_BUF_SIZE);

	buf[sizeof(buf) - 1] = '\0';

	if (!a) {
		pr_err("Len: %zu buflen: %zu strlen: %zu\n",
			len, buflen, strnlen(buf, sizeof(buf)));
		pr_err("Result: %d '%s'\n", r, buf);
		pr_err("Expect: %d '%s'\n", e, test);
		failed_tests++;
	}
}

static void __init test_hexdump_overflow_set(size_t buflen, bool ascii)
{
	unsigned int i = 0;
	int rs = get_random_u32_inclusive(1, 2) * 16;

	do {
		int gs = 1 << i;
		size_t len = get_random_u32_below(rs) + gs;

		test_hexdump_overflow(buflen, rounddown(len, gs), rs, gs, ascii);
	} while (i++ < 3);
}

static int __init test_hexdump_init(void)
{
	unsigned int i;
	int rowsize;

	rowsize = get_random_u32_inclusive(1, 2) * 16;
	for (i = 0; i < 16; i++)
		test_hexdump_set(rowsize, false);

	rowsize = get_random_u32_inclusive(1, 2) * 16;
	for (i = 0; i < 16; i++)
		test_hexdump_set(rowsize, true);

	for (i = 0; i <= TEST_HEXDUMP_BUF_SIZE; i++)
		test_hexdump_overflow_set(i, false);

	for (i = 0; i <= TEST_HEXDUMP_BUF_SIZE; i++)
		test_hexdump_overflow_set(i, true);

	if (failed_tests == 0)
		pr_info("all %u tests passed\n", total_tests);
	else
		pr_err("failed %u out of %u tests\n", failed_tests, total_tests);

	return failed_tests ? -EINVAL : 0;
}
module_init(test_hexdump_init);

static void __exit test_hexdump_exit(void)
{
	/* do nothing */
}
module_exit(test_hexdump_exit);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_LICENSE("Dual BSD/GPL");
