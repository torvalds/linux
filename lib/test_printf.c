// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for printf facility.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/bitmap.h>
#include <linux/dcache.h>
#include <linux/socket.h>
#include <linux/in.h>

#include <linux/gfp.h>
#include <linux/mm.h>

#include <linux/property.h>

#include "../tools/testing/selftests/kselftest_module.h"

#define BUF_SIZE 256
#define PAD_SIZE 16
#define FILL_CHAR '$'

KSTM_MODULE_GLOBALS();

static char *test_buffer __initdata;
static char *alloced_buffer __initdata;

extern bool no_hash_pointers;

static int __printf(4, 0) __init
do_test(int bufsize, const char *expect, int elen,
	const char *fmt, va_list ap)
{
	va_list aq;
	int ret, written;

	total_tests++;

	memset(alloced_buffer, FILL_CHAR, BUF_SIZE + 2*PAD_SIZE);
	va_copy(aq, ap);
	ret = vsnprintf(test_buffer, bufsize, fmt, aq);
	va_end(aq);

	if (ret != elen) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) returned %d, expected %d\n",
			bufsize, fmt, ret, elen);
		return 1;
	}

	if (memchr_inv(alloced_buffer, FILL_CHAR, PAD_SIZE)) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) wrote before buffer\n", bufsize, fmt);
		return 1;
	}

	if (!bufsize) {
		if (memchr_inv(test_buffer, FILL_CHAR, BUF_SIZE + PAD_SIZE)) {
			pr_warn("vsnprintf(buf, 0, \"%s\", ...) wrote to buffer\n",
				fmt);
			return 1;
		}
		return 0;
	}

	written = min(bufsize-1, elen);
	if (test_buffer[written]) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) did not nul-terminate buffer\n",
			bufsize, fmt);
		return 1;
	}

	if (memchr_inv(test_buffer + written + 1, FILL_CHAR, BUF_SIZE + PAD_SIZE - (written + 1))) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) wrote beyond the nul-terminator\n",
			bufsize, fmt);
		return 1;
	}

	if (memcmp(test_buffer, expect, written)) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) wrote '%s', expected '%.*s'\n",
			bufsize, fmt, test_buffer, written, expect);
		return 1;
	}
	return 0;
}

static void __printf(3, 4) __init
__test(const char *expect, int elen, const char *fmt, ...)
{
	va_list ap;
	int rand;
	char *p;

	if (elen >= BUF_SIZE) {
		pr_err("error in test suite: expected output length %d too long. Format was '%s'.\n",
		       elen, fmt);
		failed_tests++;
		return;
	}

	va_start(ap, fmt);

	/*
	 * Every fmt+args is subjected to four tests: Three where we
	 * tell vsnprintf varying buffer sizes (plenty, not quite
	 * enough and 0), and then we also test that kvasprintf would
	 * be able to print it as expected.
	 */
	failed_tests += do_test(BUF_SIZE, expect, elen, fmt, ap);
	rand = 1 + prandom_u32_max(elen+1);
	/* Since elen < BUF_SIZE, we have 1 <= rand <= BUF_SIZE. */
	failed_tests += do_test(rand, expect, elen, fmt, ap);
	failed_tests += do_test(0, expect, elen, fmt, ap);

	p = kvasprintf(GFP_KERNEL, fmt, ap);
	if (p) {
		total_tests++;
		if (memcmp(p, expect, elen+1)) {
			pr_warn("kvasprintf(..., \"%s\", ...) returned '%s', expected '%s'\n",
				fmt, p, expect);
			failed_tests++;
		}
		kfree(p);
	}
	va_end(ap);
}

#define test(expect, fmt, ...)					\
	__test(expect, strlen(expect), fmt, ##__VA_ARGS__)

static void __init
test_basic(void)
{
	/* Work around annoying "warning: zero-length gnu_printf format string". */
	char nul = '\0';

	test("", &nul);
	test("100%", "100%%");
	test("xxx%yyy", "xxx%cyyy", '%');
	__test("xxx\0yyy", 7, "xxx%cyyy", '\0');
}

static void __init
test_number(void)
{
	test("0x1234abcd  ", "%#-12x", 0x1234abcd);
	test("  0x1234abcd", "%#12x", 0x1234abcd);
	test("0|001| 12|+123| 1234|-123|-1234", "%d|%03d|%3d|%+d|% d|%+d|% d", 0, 1, 12, 123, 1234, -123, -1234);
	test("0|1|1|128|255", "%hhu|%hhu|%hhu|%hhu|%hhu", 0, 1, 257, 128, -1);
	test("0|1|1|-128|-1", "%hhd|%hhd|%hhd|%hhd|%hhd", 0, 1, 257, 128, -1);
	test("2015122420151225", "%ho%ho%#ho", 1037, 5282, -11627);
	/*
	 * POSIX/C99: »The result of converting zero with an explicit
	 * precision of zero shall be no characters.« Hence the output
	 * from the below test should really be "00|0||| ". However,
	 * the kernel's printf also produces a single 0 in that
	 * case. This test case simply documents the current
	 * behaviour.
	 */
	test("00|0|0|0|0", "%.2d|%.1d|%.0d|%.*d|%1.0d", 0, 0, 0, 0, 0, 0);
#ifndef __CHAR_UNSIGNED__
	{
		/*
		 * Passing a 'char' to a %02x specifier doesn't do
		 * what was presumably the intention when char is
		 * signed and the value is negative. One must either &
		 * with 0xff or cast to u8.
		 */
		char val = -16;
		test("0xfffffff0|0xf0|0xf0", "%#02x|%#02x|%#02x", val, val & 0xff, (u8)val);
	}
#endif
}

static void __init
test_string(void)
{
	test("", "%s%.0s", "", "123");
	test("ABCD|abc|123", "%s|%.3s|%.*s", "ABCD", "abcdef", 3, "123456");
	test("1  |  2|3  |  4|5  ", "%-3s|%3s|%-*s|%*s|%*s", "1", "2", 3, "3", 3, "4", -3, "5");
	test("1234      ", "%-10.4s", "123456");
	test("      1234", "%10.4s", "123456");
	/*
	 * POSIX and C99 say that a negative precision (which is only
	 * possible to pass via a * argument) should be treated as if
	 * the precision wasn't present, and that if the precision is
	 * omitted (as in %.s), the precision should be taken to be
	 * 0. However, the kernel's printf behave exactly opposite,
	 * treating a negative precision as 0 and treating an omitted
	 * precision specifier as if no precision was given.
	 *
	 * These test cases document the current behaviour; should
	 * anyone ever feel the need to follow the standards more
	 * closely, this can be revisited.
	 */
	test("    ", "%4.*s", -5, "123456");
	test("123456", "%.s", "123456");
	test("a||", "%.s|%.0s|%.*s", "a", "b", 0, "c");
	test("a  |   |   ", "%-3.s|%-3.0s|%-3.*s", "a", "b", 0, "c");
}

#define PLAIN_BUF_SIZE 64	/* leave some space so we don't oops */

#if BITS_PER_LONG == 64

#define PTR_WIDTH 16
#define PTR ((void *)0xffff0123456789abUL)
#define PTR_STR "ffff0123456789ab"
#define PTR_VAL_NO_CRNG "(____ptrval____)"
#define ZEROS "00000000"	/* hex 32 zero bits */
#define ONES "ffffffff"		/* hex 32 one bits */

static int __init
plain_format(void)
{
	char buf[PLAIN_BUF_SIZE];
	int nchars;

	nchars = snprintf(buf, PLAIN_BUF_SIZE, "%p", PTR);

	if (nchars != PTR_WIDTH)
		return -1;

	if (strncmp(buf, PTR_VAL_NO_CRNG, PTR_WIDTH) == 0) {
		pr_warn("crng possibly not yet initialized. plain 'p' buffer contains \"%s\"",
			PTR_VAL_NO_CRNG);
		return 0;
	}

	if (strncmp(buf, ZEROS, strlen(ZEROS)) != 0)
		return -1;

	return 0;
}

#else

#define PTR_WIDTH 8
#define PTR ((void *)0x456789ab)
#define PTR_STR "456789ab"
#define PTR_VAL_NO_CRNG "(ptrval)"
#define ZEROS ""
#define ONES ""

static int __init
plain_format(void)
{
	/* Format is implicitly tested for 32 bit machines by plain_hash() */
	return 0;
}

#endif	/* BITS_PER_LONG == 64 */

static int __init
plain_hash_to_buffer(const void *p, char *buf, size_t len)
{
	int nchars;

	nchars = snprintf(buf, len, "%p", p);

	if (nchars != PTR_WIDTH)
		return -1;

	if (strncmp(buf, PTR_VAL_NO_CRNG, PTR_WIDTH) == 0) {
		pr_warn("crng possibly not yet initialized. plain 'p' buffer contains \"%s\"",
			PTR_VAL_NO_CRNG);
		return 0;
	}

	return 0;
}

static int __init
plain_hash(void)
{
	char buf[PLAIN_BUF_SIZE];
	int ret;

	ret = plain_hash_to_buffer(PTR, buf, PLAIN_BUF_SIZE);
	if (ret)
		return ret;

	if (strncmp(buf, PTR_STR, PTR_WIDTH) == 0)
		return -1;

	return 0;
}

/*
 * We can't use test() to test %p because we don't know what output to expect
 * after an address is hashed.
 */
static void __init
plain(void)
{
	int err;

	if (no_hash_pointers) {
		pr_warn("skipping plain 'p' tests");
		skipped_tests += 2;
		return;
	}

	err = plain_hash();
	if (err) {
		pr_warn("plain 'p' does not appear to be hashed\n");
		failed_tests++;
		return;
	}

	err = plain_format();
	if (err) {
		pr_warn("hashing plain 'p' has unexpected format\n");
		failed_tests++;
	}
}

static void __init
test_hashed(const char *fmt, const void *p)
{
	char buf[PLAIN_BUF_SIZE];
	int ret;

	/*
	 * No need to increase failed test counter since this is assumed
	 * to be called after plain().
	 */
	ret = plain_hash_to_buffer(p, buf, PLAIN_BUF_SIZE);
	if (ret)
		return;

	test(buf, fmt, p);
}

/*
 * NULL pointers aren't hashed.
 */
static void __init
null_pointer(void)
{
	test(ZEROS "00000000", "%p", NULL);
	test(ZEROS "00000000", "%px", NULL);
	test("(null)", "%pE", NULL);
}

/*
 * Error pointers aren't hashed.
 */
static void __init
error_pointer(void)
{
	test(ONES "fffffff5", "%p", ERR_PTR(-11));
	test(ONES "fffffff5", "%px", ERR_PTR(-11));
	test("(efault)", "%pE", ERR_PTR(-11));
}

#define PTR_INVALID ((void *)0x000000ab)

static void __init
invalid_pointer(void)
{
	test_hashed("%p", PTR_INVALID);
	test(ZEROS "000000ab", "%px", PTR_INVALID);
	test("(efault)", "%pE", PTR_INVALID);
}

static void __init
symbol_ptr(void)
{
}

static void __init
kernel_ptr(void)
{
	/* We can't test this without access to kptr_restrict. */
}

static void __init
struct_resource(void)
{
}

static void __init
addr(void)
{
}

static void __init
escaped_str(void)
{
}

static void __init
hex_string(void)
{
	const char buf[3] = {0xc0, 0xff, 0xee};

	test("c0 ff ee|c0:ff:ee|c0-ff-ee|c0ffee",
	     "%3ph|%3phC|%3phD|%3phN", buf, buf, buf, buf);
	test("c0 ff ee|c0:ff:ee|c0-ff-ee|c0ffee",
	     "%*ph|%*phC|%*phD|%*phN", 3, buf, 3, buf, 3, buf, 3, buf);
}

static void __init
mac(void)
{
	const u8 addr[6] = {0x2d, 0x48, 0xd6, 0xfc, 0x7a, 0x05};

	test("2d:48:d6:fc:7a:05", "%pM", addr);
	test("05:7a:fc:d6:48:2d", "%pMR", addr);
	test("2d-48-d6-fc-7a-05", "%pMF", addr);
	test("2d48d6fc7a05", "%pm", addr);
	test("057afcd6482d", "%pmR", addr);
}

static void __init
ip4(void)
{
	struct sockaddr_in sa;

	sa.sin_family = AF_INET;
	sa.sin_port = cpu_to_be16(12345);
	sa.sin_addr.s_addr = cpu_to_be32(0x7f000001);

	test("127.000.000.001|127.0.0.1", "%pi4|%pI4", &sa.sin_addr, &sa.sin_addr);
	test("127.000.000.001|127.0.0.1", "%piS|%pIS", &sa, &sa);
	sa.sin_addr.s_addr = cpu_to_be32(0x01020304);
	test("001.002.003.004:12345|1.2.3.4:12345", "%piSp|%pISp", &sa, &sa);
}

static void __init
ip6(void)
{
}

static void __init
ip(void)
{
	ip4();
	ip6();
}

static void __init
uuid(void)
{
	const char uuid[16] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			       0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

	test("00010203-0405-0607-0809-0a0b0c0d0e0f", "%pUb", uuid);
	test("00010203-0405-0607-0809-0A0B0C0D0E0F", "%pUB", uuid);
	test("03020100-0504-0706-0809-0a0b0c0d0e0f", "%pUl", uuid);
	test("03020100-0504-0706-0809-0A0B0C0D0E0F", "%pUL", uuid);
}

static struct dentry test_dentry[4] __initdata = {
	{ .d_parent = &test_dentry[0],
	  .d_name = QSTR_INIT(test_dentry[0].d_iname, 3),
	  .d_iname = "foo" },
	{ .d_parent = &test_dentry[0],
	  .d_name = QSTR_INIT(test_dentry[1].d_iname, 5),
	  .d_iname = "bravo" },
	{ .d_parent = &test_dentry[1],
	  .d_name = QSTR_INIT(test_dentry[2].d_iname, 4),
	  .d_iname = "alfa" },
	{ .d_parent = &test_dentry[2],
	  .d_name = QSTR_INIT(test_dentry[3].d_iname, 5),
	  .d_iname = "romeo" },
};

static void __init
dentry(void)
{
	test("foo", "%pd", &test_dentry[0]);
	test("foo", "%pd2", &test_dentry[0]);

	test("(null)", "%pd", NULL);
	test("(efault)", "%pd", PTR_INVALID);
	test("(null)", "%pD", NULL);
	test("(efault)", "%pD", PTR_INVALID);

	test("romeo", "%pd", &test_dentry[3]);
	test("alfa/romeo", "%pd2", &test_dentry[3]);
	test("bravo/alfa/romeo", "%pd3", &test_dentry[3]);
	test("/bravo/alfa/romeo", "%pd4", &test_dentry[3]);
	test("/bravo/alfa", "%pd4", &test_dentry[2]);

	test("bravo/alfa  |bravo/alfa  ", "%-12pd2|%*pd2", &test_dentry[2], -12, &test_dentry[2]);
	test("  bravo/alfa|  bravo/alfa", "%12pd2|%*pd2", &test_dentry[2], 12, &test_dentry[2]);
}

static void __init
struct_va_format(void)
{
}

static void __init
time_and_date(void)
{
	/* 1543210543 */
	const struct rtc_time tm = {
		.tm_sec = 43,
		.tm_min = 35,
		.tm_hour = 5,
		.tm_mday = 26,
		.tm_mon = 10,
		.tm_year = 118,
	};
	/* 2019-01-04T15:32:23 */
	time64_t t = 1546615943;

	test("(%pt?)", "%pt", &tm);
	test("2018-11-26T05:35:43", "%ptR", &tm);
	test("0118-10-26T05:35:43", "%ptRr", &tm);
	test("05:35:43|2018-11-26", "%ptRt|%ptRd", &tm, &tm);
	test("05:35:43|0118-10-26", "%ptRtr|%ptRdr", &tm, &tm);
	test("05:35:43|2018-11-26", "%ptRttr|%ptRdtr", &tm, &tm);
	test("05:35:43 tr|2018-11-26 tr", "%ptRt tr|%ptRd tr", &tm, &tm);

	test("2019-01-04T15:32:23", "%ptT", &t);
	test("0119-00-04T15:32:23", "%ptTr", &t);
	test("15:32:23|2019-01-04", "%ptTt|%ptTd", &t, &t);
	test("15:32:23|0119-00-04", "%ptTtr|%ptTdr", &t, &t);

	test("2019-01-04 15:32:23", "%ptTs", &t);
	test("0119-00-04 15:32:23", "%ptTsr", &t);
	test("15:32:23|2019-01-04", "%ptTts|%ptTds", &t, &t);
	test("15:32:23|0119-00-04", "%ptTtrs|%ptTdrs", &t, &t);
}

static void __init
struct_clk(void)
{
}

static void __init
large_bitmap(void)
{
	const int nbits = 1 << 16;
	unsigned long *bits = bitmap_zalloc(nbits, GFP_KERNEL);
	if (!bits)
		return;

	bitmap_set(bits, 1, 20);
	bitmap_set(bits, 60000, 15);
	test("1-20,60000-60014", "%*pbl", nbits, bits);
	bitmap_free(bits);
}

static void __init
bitmap(void)
{
	DECLARE_BITMAP(bits, 20);
	const int primes[] = {2,3,5,7,11,13,17,19};
	int i;

	bitmap_zero(bits, 20);
	test("00000|00000", "%20pb|%*pb", bits, 20, bits);
	test("|", "%20pbl|%*pbl", bits, 20, bits);

	for (i = 0; i < ARRAY_SIZE(primes); ++i)
		set_bit(primes[i], bits);
	test("a28ac|a28ac", "%20pb|%*pb", bits, 20, bits);
	test("2-3,5,7,11,13,17,19|2-3,5,7,11,13,17,19", "%20pbl|%*pbl", bits, 20, bits);

	bitmap_fill(bits, 20);
	test("fffff|fffff", "%20pb|%*pb", bits, 20, bits);
	test("0-19|0-19", "%20pbl|%*pbl", bits, 20, bits);

	large_bitmap();
}

static void __init
netdev_features(void)
{
}

struct page_flags_test {
	int width;
	int shift;
	int mask;
	unsigned long value;
	const char *fmt;
	const char *name;
};

static struct page_flags_test pft[] = {
	{SECTIONS_WIDTH, SECTIONS_PGSHIFT, SECTIONS_MASK,
	 0, "%d", "section"},
	{NODES_WIDTH, NODES_PGSHIFT, NODES_MASK,
	 0, "%d", "node"},
	{ZONES_WIDTH, ZONES_PGSHIFT, ZONES_MASK,
	 0, "%d", "zone"},
	{LAST_CPUPID_WIDTH, LAST_CPUPID_PGSHIFT, LAST_CPUPID_MASK,
	 0, "%#x", "lastcpupid"},
	{KASAN_TAG_WIDTH, KASAN_TAG_PGSHIFT, KASAN_TAG_MASK,
	 0, "%#x", "kasantag"},
};

static void __init
page_flags_test(int section, int node, int zone, int last_cpupid,
		int kasan_tag, int flags, const char *name, char *cmp_buf)
{
	unsigned long values[] = {section, node, zone, last_cpupid, kasan_tag};
	unsigned long page_flags = 0;
	unsigned long size = 0;
	bool append = false;
	int i;

	flags &= PAGEFLAGS_MASK;
	if (flags) {
		page_flags |= flags;
		snprintf(cmp_buf + size, BUF_SIZE - size, "%s", name);
		size = strlen(cmp_buf);
#if SECTIONS_WIDTH || NODES_WIDTH || ZONES_WIDTH || \
	LAST_CPUPID_WIDTH || KASAN_TAG_WIDTH
		/* Other information also included in page flags */
		snprintf(cmp_buf + size, BUF_SIZE - size, "|");
		size = strlen(cmp_buf);
#endif
	}

	/* Set the test value */
	for (i = 0; i < ARRAY_SIZE(pft); i++)
		pft[i].value = values[i];

	for (i = 0; i < ARRAY_SIZE(pft); i++) {
		if (!pft[i].width)
			continue;

		if (append) {
			snprintf(cmp_buf + size, BUF_SIZE - size, "|");
			size = strlen(cmp_buf);
		}

		page_flags |= (pft[i].value & pft[i].mask) << pft[i].shift;
		snprintf(cmp_buf + size, BUF_SIZE - size, "%s=", pft[i].name);
		size = strlen(cmp_buf);
		snprintf(cmp_buf + size, BUF_SIZE - size, pft[i].fmt,
			 pft[i].value & pft[i].mask);
		size = strlen(cmp_buf);
		append = true;
	}

	test(cmp_buf, "%pGp", &page_flags);
}

static void __init
flags(void)
{
	unsigned long flags;
	char *cmp_buffer;
	gfp_t gfp;

	cmp_buffer = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (!cmp_buffer)
		return;

	flags = 0;
	page_flags_test(0, 0, 0, 0, 0, flags, "", cmp_buffer);

	flags = 1UL << NR_PAGEFLAGS;
	page_flags_test(0, 0, 0, 0, 0, flags, "", cmp_buffer);

	flags |= 1UL << PG_uptodate | 1UL << PG_dirty | 1UL << PG_lru
		| 1UL << PG_active | 1UL << PG_swapbacked;
	page_flags_test(1, 1, 1, 0x1fffff, 1, flags,
			"uptodate|dirty|lru|active|swapbacked",
			cmp_buffer);

	flags = VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	test("read|exec|mayread|maywrite|mayexec", "%pGv", &flags);

	gfp = GFP_TRANSHUGE;
	test("GFP_TRANSHUGE", "%pGg", &gfp);

	gfp = GFP_ATOMIC|__GFP_DMA;
	test("GFP_ATOMIC|GFP_DMA", "%pGg", &gfp);

	gfp = __GFP_ATOMIC;
	test("__GFP_ATOMIC", "%pGg", &gfp);

	/* Any flags not translated by the table should remain numeric */
	gfp = ~__GFP_BITS_MASK;
	snprintf(cmp_buffer, BUF_SIZE, "%#lx", (unsigned long) gfp);
	test(cmp_buffer, "%pGg", &gfp);

	snprintf(cmp_buffer, BUF_SIZE, "__GFP_ATOMIC|%#lx",
							(unsigned long) gfp);
	gfp |= __GFP_ATOMIC;
	test(cmp_buffer, "%pGg", &gfp);

	kfree(cmp_buffer);
}

static void __init fwnode_pointer(void)
{
	const struct software_node softnodes[] = {
		{ .name = "first", },
		{ .name = "second", .parent = &softnodes[0], },
		{ .name = "third", .parent = &softnodes[1], },
		{ NULL /* Guardian */ }
	};
	const char * const full_name = "first/second/third";
	const char * const full_name_second = "first/second";
	const char * const second_name = "second";
	const char * const third_name = "third";
	int rval;

	rval = software_node_register_nodes(softnodes);
	if (rval) {
		pr_warn("cannot register softnodes; rval %d\n", rval);
		return;
	}

	test(full_name_second, "%pfw", software_node_fwnode(&softnodes[1]));
	test(full_name, "%pfw", software_node_fwnode(&softnodes[2]));
	test(full_name, "%pfwf", software_node_fwnode(&softnodes[2]));
	test(second_name, "%pfwP", software_node_fwnode(&softnodes[1]));
	test(third_name, "%pfwP", software_node_fwnode(&softnodes[2]));

	software_node_unregister_nodes(softnodes);
}

static void __init fourcc_pointer(void)
{
	struct {
		u32 code;
		char *str;
	} const try[] = {
		{ 0x3231564e, "NV12 little-endian (0x3231564e)", },
		{ 0xb231564e, "NV12 big-endian (0xb231564e)", },
		{ 0x10111213, ".... little-endian (0x10111213)", },
		{ 0x20303159, "Y10  little-endian (0x20303159)", },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(try); i++)
		test(try[i].str, "%p4cc", &try[i].code);
}

static void __init
errptr(void)
{
	test("-1234", "%pe", ERR_PTR(-1234));

	/* Check that %pe with a non-ERR_PTR gets treated as ordinary %p. */
	BUILD_BUG_ON(IS_ERR(PTR));
	test_hashed("%pe", PTR);

#ifdef CONFIG_SYMBOLIC_ERRNAME
	test("(-ENOTSOCK)", "(%pe)", ERR_PTR(-ENOTSOCK));
	test("(-EAGAIN)", "(%pe)", ERR_PTR(-EAGAIN));
	BUILD_BUG_ON(EAGAIN != EWOULDBLOCK);
	test("(-EAGAIN)", "(%pe)", ERR_PTR(-EWOULDBLOCK));
	test("[-EIO    ]", "[%-8pe]", ERR_PTR(-EIO));
	test("[    -EIO]", "[%8pe]", ERR_PTR(-EIO));
	test("-EPROBE_DEFER", "%pe", ERR_PTR(-EPROBE_DEFER));
#endif
}

static void __init
test_pointer(void)
{
	plain();
	null_pointer();
	error_pointer();
	invalid_pointer();
	symbol_ptr();
	kernel_ptr();
	struct_resource();
	addr();
	escaped_str();
	hex_string();
	mac();
	ip();
	uuid();
	dentry();
	struct_va_format();
	time_and_date();
	struct_clk();
	bitmap();
	netdev_features();
	flags();
	errptr();
	fwnode_pointer();
	fourcc_pointer();
}

static void __init selftest(void)
{
	alloced_buffer = kmalloc(BUF_SIZE + 2*PAD_SIZE, GFP_KERNEL);
	if (!alloced_buffer)
		return;
	test_buffer = alloced_buffer + PAD_SIZE;

	test_basic();
	test_number();
	test_string();
	test_pointer();

	kfree(alloced_buffer);
}

KSTM_MODULE_LOADERS(test_printf);
MODULE_AUTHOR("Rasmus Villemoes <linux@rasmusvillemoes.dk>");
MODULE_LICENSE("GPL");
