// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for printf facility.
 */

#include <kunit/test.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/sprintf.h>
#include <linux/string.h>

#include <linux/bitmap.h>
#include <linux/dcache.h>
#include <linux/socket.h>
#include <linux/in.h>

#include <linux/gfp.h>
#include <linux/mm.h>

#include <linux/property.h>

#define BUF_SIZE 256
#define PAD_SIZE 16
#define FILL_CHAR '$'

#define NOWARN(option, comment, block) \
	__diag_push(); \
	__diag_ignore_all(#option, comment); \
	block \
	__diag_pop();

static unsigned int total_tests;

static char *test_buffer;
static char *alloced_buffer;

static void __printf(7, 0)
do_test(struct kunit *kunittest, const char *file, const int line, int bufsize, const char *expect,
	int elen, const char *fmt, va_list ap)
{
	va_list aq;
	int ret, written;

	total_tests++;

	memset(alloced_buffer, FILL_CHAR, BUF_SIZE + 2*PAD_SIZE);
	va_copy(aq, ap);
	ret = vsnprintf(test_buffer, bufsize, fmt, aq);
	va_end(aq);

	if (ret != elen) {
		KUNIT_FAIL(kunittest,
			   "%s:%d: vsnprintf(buf, %d, \"%s\", ...) returned %d, expected %d\n",
			   file, line, bufsize, fmt, ret, elen);
		return;
	}

	if (memchr_inv(alloced_buffer, FILL_CHAR, PAD_SIZE)) {
		KUNIT_FAIL(kunittest,
			   "%s:%d: vsnprintf(buf, %d, \"%s\", ...) wrote before buffer\n",
			   file, line, bufsize, fmt);
		return;
	}

	if (!bufsize) {
		if (memchr_inv(test_buffer, FILL_CHAR, BUF_SIZE + PAD_SIZE)) {
			KUNIT_FAIL(kunittest,
				   "%s:%d: vsnprintf(buf, 0, \"%s\", ...) wrote to buffer\n",
				   file, line, fmt);
		}
		return;
	}

	written = min(bufsize-1, elen);
	if (test_buffer[written]) {
		KUNIT_FAIL(kunittest,
			   "%s:%d: vsnprintf(buf, %d, \"%s\", ...) did not nul-terminate buffer\n",
			   file, line, bufsize, fmt);
		return;
	}

	if (memchr_inv(test_buffer + written + 1, FILL_CHAR, bufsize - (written + 1))) {
		KUNIT_FAIL(kunittest,
			   "%s:%d: vsnprintf(buf, %d, \"%s\", ...) wrote beyond the nul-terminator\n",
			   file, line, bufsize, fmt);
		return;
	}

	if (memchr_inv(test_buffer + bufsize, FILL_CHAR, BUF_SIZE + PAD_SIZE - bufsize)) {
		KUNIT_FAIL(kunittest,
			   "%s:%d: vsnprintf(buf, %d, \"%s\", ...) wrote beyond buffer\n",
			   file, line, bufsize, fmt);
		return;
	}

	if (memcmp(test_buffer, expect, written)) {
		KUNIT_FAIL(kunittest,
			   "%s:%d: vsnprintf(buf, %d, \"%s\", ...) wrote '%s', expected '%.*s'\n",
			   file, line, bufsize, fmt, test_buffer, written, expect);
		return;
	}
}

static void __printf(6, 7)
__test(struct kunit *kunittest, const char *file, const int line, const char *expect, int elen,
	const char *fmt, ...)
{
	va_list ap;
	int rand;
	char *p;

	if (elen >= BUF_SIZE) {
		KUNIT_FAIL(kunittest,
			   "%s:%d: error in test suite: expected length (%d) >= BUF_SIZE (%d). fmt=\"%s\"\n",
			   file, line, elen, BUF_SIZE, fmt);
		return;
	}

	va_start(ap, fmt);

	/*
	 * Every fmt+args is subjected to four tests: Three where we
	 * tell vsnprintf varying buffer sizes (plenty, not quite
	 * enough and 0), and then we also test that kvasprintf would
	 * be able to print it as expected.
	 */
	do_test(kunittest, file, line, BUF_SIZE, expect, elen, fmt, ap);
	rand = get_random_u32_inclusive(1, elen + 1);
	/* Since elen < BUF_SIZE, we have 1 <= rand <= BUF_SIZE. */
	do_test(kunittest, file, line, rand, expect, elen, fmt, ap);
	do_test(kunittest, file, line, 0, expect, elen, fmt, ap);

	p = kvasprintf(GFP_KERNEL, fmt, ap);
	if (p) {
		total_tests++;
		if (memcmp(p, expect, elen+1)) {
			KUNIT_FAIL(kunittest,
				   "%s:%d: kvasprintf(..., \"%s\", ...) returned '%s', expected '%s'\n",
				   file, line, fmt, p, expect);
		}
		kfree(p);
	}
	va_end(ap);
}

#define test(expect, fmt, ...)					\
	__test(kunittest, __FILE__, __LINE__, expect, strlen(expect), fmt, ##__VA_ARGS__)

static void
test_basic(struct kunit *kunittest)
{
	/* Work around annoying "warning: zero-length gnu_printf format string". */
	char nul = '\0';

	test("", &nul);
	test("100%", "100%%");
	test("xxx%yyy", "xxx%cyyy", '%');
	__test(kunittest, __FILE__, __LINE__, "xxx\0yyy", 7, "xxx%cyyy", '\0');
}

static void
test_number(struct kunit *kunittest)
{
	test("0x1234abcd  ", "%#-12x", 0x1234abcd);
	test("  0x1234abcd", "%#12x", 0x1234abcd);
	test("0|001| 12|+123| 1234|-123|-1234", "%d|%03d|%3d|%+d|% d|%+d|% d", 0, 1, 12, 123, 1234, -123, -1234);
	NOWARN(-Wformat, "Intentionally test narrowing conversion specifiers.", {
		test("0|1|1|128|255", "%hhu|%hhu|%hhu|%hhu|%hhu", 0, 1, 257, 128, -1);
		test("0|1|1|-128|-1", "%hhd|%hhd|%hhd|%hhd|%hhd", 0, 1, 257, 128, -1);
		test("2015122420151225", "%ho%ho%#ho", 1037, 5282, -11627);
	})
	/*
	 * POSIX/C99: »The result of converting zero with an explicit
	 * precision of zero shall be no characters.« Hence the output
	 * from the below test should really be "00|0||| ". However,
	 * the kernel's printf also produces a single 0 in that
	 * case. This test case simply documents the current
	 * behaviour.
	 */
	test("00|0|0|0|0", "%.2d|%.1d|%.0d|%.*d|%1.0d", 0, 0, 0, 0, 0, 0);
}

static void
test_string(struct kunit *kunittest)
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

#else

#define PTR_WIDTH 8
#define PTR ((void *)0x456789ab)
#define PTR_STR "456789ab"
#define PTR_VAL_NO_CRNG "(ptrval)"
#define ZEROS ""
#define ONES ""

#endif	/* BITS_PER_LONG == 64 */

static void
plain_hash_to_buffer(struct kunit *kunittest, const void *p, char *buf, size_t len)
{
	KUNIT_ASSERT_EQ(kunittest, snprintf(buf, len, "%p", p), PTR_WIDTH);

	if (strncmp(buf, PTR_VAL_NO_CRNG, PTR_WIDTH) == 0) {
		kunit_skip(kunittest,
			   "crng possibly not yet initialized. plain 'p' buffer contains \"%s\"\n",
			   PTR_VAL_NO_CRNG);
	}
}

static void
hash_pointer(struct kunit *kunittest)
{
	if (no_hash_pointers)
		kunit_skip(kunittest, "hash pointers disabled");

	char buf[PLAIN_BUF_SIZE];

	plain_hash_to_buffer(kunittest, PTR, buf, PLAIN_BUF_SIZE);

	/*
	 * The hash of %p is unpredictable, therefore test() cannot be used.
	 *
	 * Instead verify that the first 32 bits are zeros on a 64-bit system
	 * and that the non-hashed value is not printed.
	 */

	KUNIT_EXPECT_MEMEQ(kunittest, buf, ZEROS, strlen(ZEROS));
	KUNIT_EXPECT_MEMNEQ(kunittest, buf, PTR_STR, PTR_WIDTH);
}

static void
test_hashed(struct kunit *kunittest, const char *fmt, const void *p)
{
	char buf[PLAIN_BUF_SIZE];

	plain_hash_to_buffer(kunittest, p, buf, PLAIN_BUF_SIZE);

	test(buf, fmt, p);
}

/*
 * NULL pointers aren't hashed.
 */
static void
null_pointer(struct kunit *kunittest)
{
	test(ZEROS "00000000", "%p", NULL);
	test(ZEROS "00000000", "%px", NULL);
	test("(null)", "%pE", NULL);
}

/*
 * Error pointers aren't hashed.
 */
static void
error_pointer(struct kunit *kunittest)
{
	test(ONES "fffffff5", "%p", ERR_PTR(-11));
	test(ONES "fffffff5", "%px", ERR_PTR(-11));
	test("(efault)", "%pE", ERR_PTR(-11));
}

#define PTR_INVALID ((void *)0x000000ab)

static void
invalid_pointer(struct kunit *kunittest)
{
	test_hashed(kunittest, "%p", PTR_INVALID);
	test(ZEROS "000000ab", "%px", PTR_INVALID);
	test("(efault)", "%pE", PTR_INVALID);
}

static void
symbol_ptr(struct kunit *kunittest)
{
}

static void
kernel_ptr(struct kunit *kunittest)
{
	/* We can't test this without access to kptr_restrict. */
}

static void
struct_resource(struct kunit *kunittest)
{
	struct resource test_resource = {
		.start = 0xc0ffee00,
		.end = 0xc0ffee00,
		.flags = IORESOURCE_MEM,
	};

	test("[mem 0xc0ffee00 flags 0x200]",
	     "%pr", &test_resource);

	test_resource = (struct resource) {
		.start = 0xc0ffee,
		.end = 0xba5eba11,
		.flags = IORESOURCE_MEM,
	};
	test("[mem 0x00c0ffee-0xba5eba11 flags 0x200]",
	     "%pr", &test_resource);

	test_resource = (struct resource) {
		.start = 0xba5eba11,
		.end = 0xc0ffee,
		.flags = IORESOURCE_MEM,
	};
	test("[mem 0xba5eba11-0x00c0ffee flags 0x200]",
	     "%pr", &test_resource);

	test_resource = (struct resource) {
		.start = 0xba5eba11,
		.end = 0xba5eca11,
		.flags = IORESOURCE_MEM,
	};

	test("[mem 0xba5eba11-0xba5eca11 flags 0x200]",
	     "%pr", &test_resource);

	test_resource = (struct resource) {
		.start = 0xba11,
		.end = 0xca10,
		.flags = IORESOURCE_IO |
			 IORESOURCE_DISABLED |
			 IORESOURCE_UNSET,
	};

	test("[io  size 0x1000 disabled]",
	     "%pR", &test_resource);
}

static void
struct_range(struct kunit *kunittest)
{
	struct range test_range = DEFINE_RANGE(0xc0ffee00ba5eba11,
					       0xc0ffee00ba5eba11);
	test("[range 0xc0ffee00ba5eba11]", "%pra", &test_range);

	test_range = DEFINE_RANGE(0xc0ffee, 0xba5eba11);
	test("[range 0x0000000000c0ffee-0x00000000ba5eba11]",
	     "%pra", &test_range);

	test_range = DEFINE_RANGE(0xba5eba11, 0xc0ffee);
	test("[range 0x00000000ba5eba11-0x0000000000c0ffee]",
	     "%pra", &test_range);
}

static void
addr(struct kunit *kunittest)
{
}

static void
escaped_str(struct kunit *kunittest)
{
}

static void
hex_string(struct kunit *kunittest)
{
	const char buf[3] = {0xc0, 0xff, 0xee};

	test("c0 ff ee|c0:ff:ee|c0-ff-ee|c0ffee",
	     "%3ph|%3phC|%3phD|%3phN", buf, buf, buf, buf);
	test("c0 ff ee|c0:ff:ee|c0-ff-ee|c0ffee",
	     "%*ph|%*phC|%*phD|%*phN", 3, buf, 3, buf, 3, buf, 3, buf);
}

static void
mac(struct kunit *kunittest)
{
	const u8 addr[6] = {0x2d, 0x48, 0xd6, 0xfc, 0x7a, 0x05};

	test("2d:48:d6:fc:7a:05", "%pM", addr);
	test("05:7a:fc:d6:48:2d", "%pMR", addr);
	test("2d-48-d6-fc-7a-05", "%pMF", addr);
	test("2d48d6fc7a05", "%pm", addr);
	test("057afcd6482d", "%pmR", addr);
}

static void
ip4(struct kunit *kunittest)
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

static void
ip6(struct kunit *kunittest)
{
}

static void
uuid(struct kunit *kunittest)
{
	const char uuid[16] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			       0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

	test("00010203-0405-0607-0809-0a0b0c0d0e0f", "%pUb", uuid);
	test("00010203-0405-0607-0809-0A0B0C0D0E0F", "%pUB", uuid);
	test("03020100-0504-0706-0809-0a0b0c0d0e0f", "%pUl", uuid);
	test("03020100-0504-0706-0809-0A0B0C0D0E0F", "%pUL", uuid);
}

static struct dentry test_dentry[4] = {
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

static void
dentry(struct kunit *kunittest)
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

static void
struct_va_format(struct kunit *kunittest)
{
}

static void
time_and_date(struct kunit *kunittest)
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

static void
struct_clk(struct kunit *kunittest)
{
}

static void
large_bitmap(struct kunit *kunittest)
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

static void
bitmap(struct kunit *kunittest)
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

	large_bitmap(kunittest);
}

static void
netdev_features(struct kunit *kunittest)
{
}

struct page_flags_test {
	int width;
	int shift;
	int mask;
	const char *fmt;
	const char *name;
};

static const struct page_flags_test pft[] = {
	{SECTIONS_WIDTH, SECTIONS_PGSHIFT, SECTIONS_MASK,
	 "%d", "section"},
	{NODES_WIDTH, NODES_PGSHIFT, NODES_MASK,
	 "%d", "node"},
	{ZONES_WIDTH, ZONES_PGSHIFT, ZONES_MASK,
	 "%d", "zone"},
	{LAST_CPUPID_WIDTH, LAST_CPUPID_PGSHIFT, LAST_CPUPID_MASK,
	 "%#x", "lastcpupid"},
	{KASAN_TAG_WIDTH, KASAN_TAG_PGSHIFT, KASAN_TAG_MASK,
	 "%#x", "kasantag"},
};

static void
page_flags_test(struct kunit *kunittest, int section, int node, int zone,
		int last_cpupid, int kasan_tag, unsigned long flags, const char *name,
		char *cmp_buf)
{
	unsigned long values[] = {section, node, zone, last_cpupid, kasan_tag};
	unsigned long size;
	bool append = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(values); i++)
		flags |= (values[i] & pft[i].mask) << pft[i].shift;

	size = scnprintf(cmp_buf, BUF_SIZE, "%#lx(", flags);
	if (flags & PAGEFLAGS_MASK) {
		size += scnprintf(cmp_buf + size, BUF_SIZE - size, "%s", name);
		append = true;
	}

	for (i = 0; i < ARRAY_SIZE(pft); i++) {
		if (!pft[i].width)
			continue;

		if (append)
			size += scnprintf(cmp_buf + size, BUF_SIZE - size, "|");

		size += scnprintf(cmp_buf + size, BUF_SIZE - size, "%s=",
				pft[i].name);
		size += scnprintf(cmp_buf + size, BUF_SIZE - size, pft[i].fmt,
				values[i] & pft[i].mask);
		append = true;
	}

	snprintf(cmp_buf + size, BUF_SIZE - size, ")");

	test(cmp_buf, "%pGp", &flags);
}

static void
flags(struct kunit *kunittest)
{
	unsigned long flags;
	char *cmp_buffer;
	gfp_t gfp;

	cmp_buffer = kunit_kmalloc(kunittest, BUF_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(kunittest, cmp_buffer);

	flags = 0;
	page_flags_test(kunittest, 0, 0, 0, 0, 0, flags, "", cmp_buffer);

	flags = 1UL << NR_PAGEFLAGS;
	page_flags_test(kunittest, 0, 0, 0, 0, 0, flags, "", cmp_buffer);

	flags |= 1UL << PG_uptodate | 1UL << PG_dirty | 1UL << PG_lru
		| 1UL << PG_active | 1UL << PG_swapbacked;
	page_flags_test(kunittest, 1, 1, 1, 0x1fffff, 1, flags,
			"uptodate|dirty|lru|active|swapbacked",
			cmp_buffer);

	flags = VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	test("read|exec|mayread|maywrite|mayexec", "%pGv", &flags);

	gfp = GFP_TRANSHUGE;
	test("GFP_TRANSHUGE", "%pGg", &gfp);

	gfp = GFP_ATOMIC|__GFP_DMA;
	test("GFP_ATOMIC|GFP_DMA", "%pGg", &gfp);

	gfp = __GFP_HIGH;
	test("__GFP_HIGH", "%pGg", &gfp);

	/* Any flags not translated by the table should remain numeric */
	gfp = ~__GFP_BITS_MASK;
	snprintf(cmp_buffer, BUF_SIZE, "%#lx", (unsigned long) gfp);
	test(cmp_buffer, "%pGg", &gfp);

	snprintf(cmp_buffer, BUF_SIZE, "__GFP_HIGH|%#lx",
							(unsigned long) gfp);
	gfp |= __GFP_HIGH;
	test(cmp_buffer, "%pGg", &gfp);
}

static void fwnode_pointer(struct kunit *kunittest)
{
	const struct software_node first = { .name = "first" };
	const struct software_node second = { .name = "second", .parent = &first };
	const struct software_node third = { .name = "third", .parent = &second };
	const struct software_node *group[] = { &first, &second, &third, NULL };
	const char * const full_name_second = "first/second";
	const char * const full_name_third = "first/second/third";
	const char * const second_name = "second";
	const char * const third_name = "third";
	int rval;

	rval = software_node_register_node_group(group);
	if (rval) {
		kunit_skip(kunittest, "cannot register softnodes; rval %d\n", rval);
	}

	test(full_name_second, "%pfw", software_node_fwnode(&second));
	test(full_name_third, "%pfw", software_node_fwnode(&third));
	test(full_name_third, "%pfwf", software_node_fwnode(&third));
	test(second_name, "%pfwP", software_node_fwnode(&second));
	test(third_name, "%pfwP", software_node_fwnode(&third));

	software_node_unregister_node_group(group);
}

struct fourcc_struct {
	u32 code;
	const char *str;
};

static void fourcc_pointer_test(struct kunit *kunittest, const struct fourcc_struct *fc,
				size_t n, const char *fmt)
{
	size_t i;

	for (i = 0; i < n; i++)
		test(fc[i].str, fmt, &fc[i].code);
}

static void fourcc_pointer(struct kunit *kunittest)
{
	static const struct fourcc_struct try_cc[] = {
		{ 0x3231564e, "NV12 little-endian (0x3231564e)", },
		{ 0xb231564e, "NV12 big-endian (0xb231564e)", },
		{ 0x10111213, ".... little-endian (0x10111213)", },
		{ 0x20303159, "Y10  little-endian (0x20303159)", },
	};
	static const struct fourcc_struct try_ch[] = {
		{ 0x41424344, "ABCD (0x41424344)", },
	};
	static const struct fourcc_struct try_chR[] = {
		{ 0x41424344, "DCBA (0x44434241)", },
	};
	static const struct fourcc_struct try_cl[] = {
		{ (__force u32)cpu_to_le32(0x41424344), "ABCD (0x41424344)", },
	};
	static const struct fourcc_struct try_cb[] = {
		{ (__force u32)cpu_to_be32(0x41424344), "ABCD (0x41424344)", },
	};

	fourcc_pointer_test(kunittest, try_cc, ARRAY_SIZE(try_cc), "%p4cc");
	fourcc_pointer_test(kunittest, try_ch, ARRAY_SIZE(try_ch), "%p4ch");
	fourcc_pointer_test(kunittest, try_chR, ARRAY_SIZE(try_chR), "%p4chR");
	fourcc_pointer_test(kunittest, try_cl, ARRAY_SIZE(try_cl), "%p4cl");
	fourcc_pointer_test(kunittest, try_cb, ARRAY_SIZE(try_cb), "%p4cb");
}

static void
errptr(struct kunit *kunittest)
{
	test("-1234", "%pe", ERR_PTR(-1234));

	/* Check that %pe with a non-ERR_PTR gets treated as ordinary %p. */
	BUILD_BUG_ON(IS_ERR(PTR));
	test_hashed(kunittest, "%pe", PTR);

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

static int printf_suite_init(struct kunit_suite *suite)
{
	total_tests = 0;

	alloced_buffer = kmalloc(BUF_SIZE + 2*PAD_SIZE, GFP_KERNEL);
	if (!alloced_buffer)
		return -ENOMEM;
	test_buffer = alloced_buffer + PAD_SIZE;

	return 0;
}

static void printf_suite_exit(struct kunit_suite *suite)
{
	kfree(alloced_buffer);

	kunit_info(suite, "ran %u tests\n", total_tests);
}

static struct kunit_case printf_test_cases[] = {
	KUNIT_CASE(test_basic),
	KUNIT_CASE(test_number),
	KUNIT_CASE(test_string),
	KUNIT_CASE(hash_pointer),
	KUNIT_CASE(null_pointer),
	KUNIT_CASE(error_pointer),
	KUNIT_CASE(invalid_pointer),
	KUNIT_CASE(symbol_ptr),
	KUNIT_CASE(kernel_ptr),
	KUNIT_CASE(struct_resource),
	KUNIT_CASE(struct_range),
	KUNIT_CASE(addr),
	KUNIT_CASE(escaped_str),
	KUNIT_CASE(hex_string),
	KUNIT_CASE(mac),
	KUNIT_CASE(ip4),
	KUNIT_CASE(ip6),
	KUNIT_CASE(uuid),
	KUNIT_CASE(dentry),
	KUNIT_CASE(struct_va_format),
	KUNIT_CASE(time_and_date),
	KUNIT_CASE(struct_clk),
	KUNIT_CASE(bitmap),
	KUNIT_CASE(netdev_features),
	KUNIT_CASE(flags),
	KUNIT_CASE(errptr),
	KUNIT_CASE(fwnode_pointer),
	KUNIT_CASE(fourcc_pointer),
	{}
};

static struct kunit_suite printf_test_suite = {
	.name = "printf",
	.suite_init = printf_suite_init,
	.suite_exit = printf_suite_exit,
	.test_cases = printf_test_cases,
};

kunit_test_suite(printf_test_suite);

MODULE_AUTHOR("Rasmus Villemoes <linux@rasmusvillemoes.dk>");
MODULE_DESCRIPTION("Test cases for printf facility");
MODULE_LICENSE("GPL");
