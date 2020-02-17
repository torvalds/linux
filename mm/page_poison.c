// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/page_ext.h>
#include <linux/poison.h>
#include <linux/ratelimit.h>
#include <linux/kasan.h>

static bool want_page_poisoning __read_mostly;

static int __init early_page_poison_param(char *buf)
{
	if (!buf)
		return -EINVAL;
	return strtobool(buf, &want_page_poisoning);
}
early_param("page_poison", early_page_poison_param);

/**
 * page_poisoning_enabled - check if page poisoning is enabled
 *
 * Return true if page poisoning is enabled, or false if not.
 */
bool page_poisoning_enabled(void)
{
	/*
	 * Assumes that debug_pagealloc_enabled is set before
	 * free_all_bootmem.
	 * Page poisoning is debug page alloc for some arches. If
	 * either of those options are enabled, enable poisoning.
	 */
	return (want_page_poisoning ||
		(!IS_ENABLED(CONFIG_ARCH_SUPPORTS_DEBUG_PAGEALLOC) &&
		debug_pagealloc_enabled()));
}
EXPORT_SYMBOL_GPL(page_poisoning_enabled);

static void poison_page(struct page *page)
{
	void *addr = kmap_atomic(page);

	/* KASAN still think the page is in-use, so skip it. */
	kasan_disable_current();
	memset(addr, PAGE_POISON, PAGE_SIZE);
	kasan_enable_current();
	kunmap_atomic(addr);
}

static void poison_pages(struct page *page, int n)
{
	int i;

	for (i = 0; i < n; i++)
		poison_page(page + i);
}

static bool single_bit_flip(unsigned char a, unsigned char b)
{
	unsigned char error = a ^ b;

	return error && !(error & (error - 1));
}

static void check_poison_mem(unsigned char *mem, size_t bytes)
{
	static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 10);
	unsigned char *start;
	unsigned char *end;

	if (IS_ENABLED(CONFIG_PAGE_POISONING_NO_SANITY))
		return;

	start = memchr_inv(mem, PAGE_POISON, bytes);
	if (!start)
		return;

	for (end = mem + bytes - 1; end > start; end--) {
		if (*end != PAGE_POISON)
			break;
	}

	if (!__ratelimit(&ratelimit))
		return;
	else if (start == end && single_bit_flip(*start, PAGE_POISON))
		pr_err("pagealloc: single bit error\n");
	else
		pr_err("pagealloc: memory corruption\n");

	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1, start,
			end - start + 1, 1);
	dump_stack();
}

static void unpoison_page(struct page *page)
{
	void *addr;

	addr = kmap_atomic(page);
	/*
	 * Page poisoning when enabled poisons each and every page
	 * that is freed to buddy. Thus no extra check is done to
	 * see if a page was posioned.
	 */
	check_poison_mem(addr, PAGE_SIZE);
	kunmap_atomic(addr);
}

static void unpoison_pages(struct page *page, int n)
{
	int i;

	for (i = 0; i < n; i++)
		unpoison_page(page + i);
}

void kernel_poison_pages(struct page *page, int numpages, int enable)
{
	if (!page_poisoning_enabled())
		return;

	if (enable)
		unpoison_pages(page, numpages);
	else
		poison_pages(page, numpages);
}

#ifndef CONFIG_ARCH_SUPPORTS_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	/* This function does nothing, all work is done via poison pages */
}
#endif
