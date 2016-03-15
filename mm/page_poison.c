#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/page_ext.h>
#include <linux/poison.h>
#include <linux/ratelimit.h>

static bool __page_poisoning_enabled __read_mostly;
static bool want_page_poisoning __read_mostly;

static int early_page_poison_param(char *buf)
{
	if (!buf)
		return -EINVAL;

	if (strcmp(buf, "on") == 0)
		want_page_poisoning = true;
	else if (strcmp(buf, "off") == 0)
		want_page_poisoning = false;

	return 0;
}
early_param("page_poison", early_page_poison_param);

bool page_poisoning_enabled(void)
{
	return __page_poisoning_enabled;
}

static bool need_page_poisoning(void)
{
	return want_page_poisoning;
}

static void init_page_poisoning(void)
{
	/*
	 * page poisoning is debug page alloc for some arches. If either
	 * of those options are enabled, enable poisoning
	 */
	if (!IS_ENABLED(CONFIG_ARCH_SUPPORTS_DEBUG_PAGEALLOC)) {
		if (!want_page_poisoning && !debug_pagealloc_enabled())
			return;
	} else {
		if (!want_page_poisoning)
			return;
	}

	__page_poisoning_enabled = true;
}

struct page_ext_operations page_poisoning_ops = {
	.need = need_page_poisoning,
	.init = init_page_poisoning,
};

static inline void set_page_poison(struct page *page)
{
	struct page_ext *page_ext;

	page_ext = lookup_page_ext(page);
	__set_bit(PAGE_EXT_DEBUG_POISON, &page_ext->flags);
}

static inline void clear_page_poison(struct page *page)
{
	struct page_ext *page_ext;

	page_ext = lookup_page_ext(page);
	__clear_bit(PAGE_EXT_DEBUG_POISON, &page_ext->flags);
}

static inline bool page_poison(struct page *page)
{
	struct page_ext *page_ext;

	page_ext = lookup_page_ext(page);
	return test_bit(PAGE_EXT_DEBUG_POISON, &page_ext->flags);
}

static void poison_page(struct page *page)
{
	void *addr = kmap_atomic(page);

	set_page_poison(page);
	memset(addr, PAGE_POISON, PAGE_SIZE);
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

	if (!page_poison(page))
		return;

	addr = kmap_atomic(page);
	check_poison_mem(addr, PAGE_SIZE);
	clear_page_poison(page);
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
