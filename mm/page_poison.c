// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/mmdebug.h>
#include <linux/highmem.h>
#include <linux/poison.h>
#include <linux/ratelimit.h>
#include <linux/kasan.h>

bool _page_poisoning_enabled_early;
EXPORT_SYMBOL(_page_poisoning_enabled_early);
DEFINE_STATIC_KEY_FALSE(_page_poisoning_enabled);
EXPORT_SYMBOL(_page_poisoning_enabled);

static int __init early_page_poison_param(char *buf)
{
	return kstrtobool(buf, &_page_poisoning_enabled_early);
}
early_param("page_poison", early_page_poison_param);

static void poison_page(struct page *page)
{
	void *addr = kmap_local_page(page);

	/* KASAN still think the page is in-use, so skip it. */
	kasan_disable_current();
	memset(kasan_reset_tag(addr), PAGE_POISON, PAGE_SIZE);
	kasan_enable_current();
	kunmap_local(addr);
}

void __kernel_poison_pages(struct page *page, int n)
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

static void check_poison_mem(struct page *page, unsigned char *mem, size_t bytes)
{
	static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 10);
	unsigned char *start;
	unsigned char *end;

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
	dump_page(page, "pagealloc: corrupted page details");
}

static void unpoison_page(struct page *page)
{
	void *addr;

	addr = kmap_local_page(page);
	kasan_disable_current();
	/*
	 * Page poisoning when enabled poisons each and every page
	 * that is freed to buddy. Thus no extra check is done to
	 * see if a page was poisoned.
	 */
	check_poison_mem(page, kasan_reset_tag(addr), PAGE_SIZE);
	kasan_enable_current();
	kunmap_local(addr);
}

void __kernel_unpoison_pages(struct page *page, int n)
{
	int i;

	for (i = 0; i < n; i++)
		unpoison_page(page + i);
}

#ifndef CONFIG_ARCH_SUPPORTS_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	/* This function does nothing, all work is done via poison pages */
}
#endif
