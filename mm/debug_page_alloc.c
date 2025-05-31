// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/page-isolation.h>

unsigned int _debug_guardpage_minorder;

bool _debug_pagealloc_enabled_early __read_mostly
			= IS_ENABLED(CONFIG_DEBUG_PAGEALLOC_ENABLE_DEFAULT);
EXPORT_SYMBOL(_debug_pagealloc_enabled_early);
DEFINE_STATIC_KEY_FALSE(_debug_pagealloc_enabled);
EXPORT_SYMBOL(_debug_pagealloc_enabled);

DEFINE_STATIC_KEY_FALSE(_debug_guardpage_enabled);

static int __init early_debug_pagealloc(char *buf)
{
	return kstrtobool(buf, &_debug_pagealloc_enabled_early);
}
early_param("debug_pagealloc", early_debug_pagealloc);

static int __init debug_guardpage_minorder_setup(char *buf)
{
	unsigned long res;

	if (kstrtoul(buf, 10, &res) < 0 ||  res > MAX_PAGE_ORDER / 2) {
		pr_err("Bad debug_guardpage_minorder value: %s\n", buf);
		return 0;
	}
	_debug_guardpage_minorder = res;
	pr_info("Setting debug_guardpage_minorder to %lu\n", res);
	return 0;
}
early_param("debug_guardpage_minorder", debug_guardpage_minorder_setup);

bool __set_page_guard(struct zone *zone, struct page *page, unsigned int order)
{
	if (order >= debug_guardpage_minorder())
		return false;

	__SetPageGuard(page);
	INIT_LIST_HEAD(&page->buddy_list);
	set_page_private(page, order);

	return true;
}

void __clear_page_guard(struct zone *zone, struct page *page, unsigned int order)
{
	__ClearPageGuard(page);
	set_page_private(page, 0);
}
