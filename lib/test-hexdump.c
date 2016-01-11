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

static const char * const test_data_1_le[] __initconst = {
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

static const char * const test_data_4_le[] __initconst = {
	"7bdb32be", "b293180a", "24c4ba70", "9b34837d",
	"ad319ca6", "e9ac0f9c", "9919d14c", "0cafb143",
};

static const char * const test_data_8_le[] __initconst = {
	"b293180a7bdb32be", "9b34837d24c4ba70",
	"e9ac0f9cad319ca6", "0cafb1439919d14c",
};

static void __init test_hexdump(size_t len, int rowsize, int groupsize,
				bool ascii)
{
	char test[32 * 3 + 2 + 32 + 1];
	char real[32 * 3 + 2 + 32 + 1];
	char *p;
	const char * const *result;
	size_t l = len;
	int gs = groupsize, rs = rowsize;
	unsigned int i;

	hex_dump_to_buffer(data_b, l, rs, gs, real, sizeof(real), ascii);

	if (rs != 16 && rs != 32)
		rs = 16;

	if (l > rs)
		l = rs;

	if (!is_power_of_2(gs) || gs > 8 || (len % gs != 0))
		gs = 1;

	if (gs == 8)
		result = test_data_8_le;
	else if (gs == 4)
		result = test_data_4_le;
	else if (gs == 2)
		result = test_data_2_le;
	else
		result = test_data_1_le;

	memset(test, ' ', sizeof(test));

	/* hex dump */
	p = test;
	for (i = 0; i < l / gs; i++) {
		const char *q = *result++;
		size_t amount = strlen(q);

		strncpy(p, q, amount);
		p += amount + 1;
	}
	if (i)
		p--;

	/* ASCII part */
	if (ascii) {
		p = test + rs * 2 + rs / gs + 1;
		strncpy(p, data_a, l);
		p += l;
	}

	*p = '\0';

	if (strcmp(test, real)) {
		pr_err("Len: %zu row: %d group: %d\n", len, rowsize, groupsize);
		pr_err("Result: '%s'\n", real);
		pr_err("Expect: '%s'\n", test);
	}
}

static void __init test_hexdump_set(int rowsize, bool ascii)
{
	size_t d = min_t(size_t, sizeof(data_b), rowsize);
	size_t len = get_random_int() % d + 1;

	test_hexdump(len, rowsize, 4, ascii);
	test_hexdump(len, rowsize, 2, ascii);
	test_hexdump(len, rowsize, 8, ascii);
	test_hexdump(len, rowsize, 1, ascii);
}

static void __init test_hexdump_overflow(bool ascii)
{
	char buf[56];
	const char *t = test_data_1_le[0];
	size_t l = get_random_int() % sizeof(buf);
	bool a;
	int e, r;

	memset(buf, ' ', sizeof(buf));

	r = hex_dump_to_buffer(data_b, 1, 16, 1, buf, l, ascii);

	if (ascii)
		e = 50;
	else
		e = 2;
	buf[e + 2] = '\0';

	if (!l) {
		a = r == e && buf[0] == ' ';
	} else if (l < 3) {
		a = r == e && buf[0] == '\0';
	} else if (l < 4) {
		a = r == e && !strcmp(buf, t);
	} else if (ascii) {
		if (l < 51)
			a = r == e && buf[l - 1] == '\0' && buf[l - 2] == ' ';
		else
			a = r == e && buf[50] == '\0' && buf[49] == '.';
	} else {
		a = r == e && buf[e] == '\0';
	}

	if (!a) {
		pr_err("Len: %zu rc: %u strlen: %zu\n", l, r, strlen(buf));
		pr_err("Result: '%s'\n", buf);
	}
}

static int __init test_hexdump_init(void)
{
	unsigned int i;
	int rowsize;

	pr_info("Running tests...\n");

	rowsize = (get_random_int() % 2 + 1) * 16;
	for (i = 0; i < 16; i++)
		test_hexdump_set(rowsize, false);

	rowsize = (get_random_int() % 2 + 1) * 16;
	for (i = 0; i < 16; i++)
		test_hexdump_set(rowsize, true);

	for (i = 0; i < 16; i++)
		test_hexdump_overflow(false);

	for (i = 0; i < 16; i++)
		test_hexdump_overflow(true);

	return -EINVAL;
}
module_init(test_hexdump_init);
MODULE_LICENSE("Dual BSD/GPL");
