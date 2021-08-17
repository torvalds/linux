// SPDX-License-Identifier: GPL-2.0-only
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

static __init int strchr_selftest(void)
{
	const char *test_string = "abcdefghijkl";
	const char *empty_string = "";
	char *result;
	int i;

	for (i = 0; i < strlen(test_string) + 1; i++) {
		result = strchr(test_string, test_string[i]);
		if (result - test_string != i)
			return i + 'a';
	}

	result = strchr(empty_string, '\0');
	if (result != empty_string)
		return 0x101;

	result = strchr(empty_string, 'a');
	if (result)
		return 0x102;

	result = strchr(test_string, 'z');
	if (result)
		return 0x103;

	return 0;
}

static __init int strnchr_selftest(void)
{
	const char *test_string = "abcdefghijkl";
	const char *empty_string = "";
	char *result;
	int i, j;

	for (i = 0; i < strlen(test_string) + 1; i++) {
		for (j = 0; j < strlen(test_string) + 2; j++) {
			result = strnchr(test_string, j, test_string[i]);
			if (j <= i) {
				if (!result)
					continue;
				return ((i + 'a') << 8) | j;
			}
			if (result - test_string != i)
				return ((i + 'a') << 8) | j;
		}
	}

	result = strnchr(empty_string, 0, '\0');
	if (result)
		return 0x10001;

	result = strnchr(empty_string, 1, '\0');
	if (result != empty_string)
		return 0x10002;

	result = strnchr(empty_string, 1, 'a');
	if (result)
		return 0x10003;

	result = strnchr(NULL, 0, '\0');
	if (result)
		return 0x10004;

	return 0;
}

static __exit void string_selftest_remove(void)
{
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

	test = 4;
	subtest = strchr_selftest();
	if (subtest)
		goto fail;

	test = 5;
	subtest = strnchr_selftest();
	if (subtest)
		goto fail;

	pr_info("String selftests succeeded\n");
	return 0;
fail:
	pr_crit("String selftest failure %d.%08x\n", test, subtest);
	return 0;
}

module_init(string_selftest_init);
module_exit(string_selftest_remove);
MODULE_LICENSE("GPL v2");
