#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

static __init int memset16_selftest(void)
{
	unsigned i, j, k;
	u16 v, *p;

	p = kmalloc(256 * 2 * 2, GFP_KERNEL);
	if (!p)
		return -1;

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset16(p + i, 0xb1b2, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					if (v != 0xa1a1)
						goto fail;
				} else if (k < i + j) {
					if (v != 0xb1b2)
						goto fail;
				} else {
					if (v != 0xa1a1)
						goto fail;
				}
			}
		}
	}

fail:
	kfree(p);
	if (i < 256)
		return (i << 24) | (j << 16) | k | 0x8000;
	return 0;
}

static __init int memset32_selftest(void)
{
	unsigned i, j, k;
	u32 v, *p;

	p = kmalloc(256 * 2 * 4, GFP_KERNEL);
	if (!p)
		return -1;

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset32(p + i, 0xb1b2b3b4, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					if (v != 0xa1a1a1a1)
						goto fail;
				} else if (k < i + j) {
					if (v != 0xb1b2b3b4)
						goto fail;
				} else {
					if (v != 0xa1a1a1a1)
						goto fail;
				}
			}
		}
	}

fail:
	kfree(p);
	if (i < 256)
		return (i << 24) | (j << 16) | k | 0x8000;
	return 0;
}

static __init int memset64_selftest(void)
{
	unsigned i, j, k;
	u64 v, *p;

	p = kmalloc(256 * 2 * 8, GFP_KERNEL);
	if (!p)
		return -1;

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset64(p + i, 0xb1b2b3b4b5b6b7b8ULL, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					if (v != 0xa1a1a1a1a1a1a1a1ULL)
						goto fail;
				} else if (k < i + j) {
					if (v != 0xb1b2b3b4b5b6b7b8ULL)
						goto fail;
				} else {
					if (v != 0xa1a1a1a1a1a1a1a1ULL)
						goto fail;
				}
			}
		}
	}

fail:
	kfree(p);
	if (i < 256)
		return (i << 24) | (j << 16) | k | 0x8000;
	return 0;
}

static __init int string_selftest_init(void)
{
	int test, subtest;

	test = 1;
	subtest = memset16_selftest();
	if (subtest)
		goto fail;

	test = 2;
	subtest = memset32_selftest();
	if (subtest)
		goto fail;

	test = 3;
	subtest = memset64_selftest();
	if (subtest)
		goto fail;

	pr_info("String selftests succeeded\n");
	return 0;
fail:
	pr_crit("String selftest failure %d.%08x\n", test, subtest);
	return 0;
}

module_init(string_selftest_init);
MODULE_LICENSE("GPL v2");
