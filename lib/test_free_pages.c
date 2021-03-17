// SPDX-License-Identifier: GPL-2.0+
/*
 * test_free_pages.c: Check that free_pages() doesn't leak memory
 * Copyright (c) 2020 Oracle
 * Author: Matthew Wilcox <willy@infradead.org>
 */

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/module.h>

static void test_free_pages(gfp_t gfp)
{
	unsigned int i;

	for (i = 0; i < 1000 * 1000; i++) {
		unsigned long addr = __get_free_pages(gfp, 3);
		struct page *page = virt_to_page(addr);

		/* Simulate page cache getting a speculative reference */
		get_page(page);
		free_pages(addr, 3);
		put_page(page);
	}
}

static int m_in(void)
{
	test_free_pages(GFP_KERNEL);
	test_free_pages(GFP_KERNEL | __GFP_COMP);

	return 0;
}

static void m_ex(void)
{
}

module_init(m_in);
module_exit(m_ex);
MODULE_AUTHOR("Matthew Wilcox <willy@infradead.org>");
MODULE_LICENSE("GPL");
