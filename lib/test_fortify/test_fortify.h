/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

void do_fortify_tests(void);

#define __BUF_SMALL	16
#define __BUF_LARGE	32
struct fortify_object {
	int a;
	char buf[__BUF_SMALL];
	int c;
};

#define LITERAL_SMALL "AAAAAAAAAAAAAAA"
#define LITERAL_LARGE "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
const char small_src[__BUF_SMALL] = LITERAL_SMALL;
const char large_src[__BUF_LARGE] = LITERAL_LARGE;

char small[__BUF_SMALL];
char large[__BUF_LARGE];
struct fortify_object instance;
size_t size;

void do_fortify_tests(void)
{
	/* Normal initializations. */
	memset(&instance, 0x32, sizeof(instance));
	memset(small, 0xA5, sizeof(small));
	memset(large, 0x5A, sizeof(large));

	TEST;
}
