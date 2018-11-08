/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Xen memory reservation utilities.
 *
 * Copyright (c) 2003, B Dragovic
 * Copyright (c) 2003-2004, M Williamson, K Fraser
 * Copyright (c) 2005 Dan M. Smith, IBM Corporation
 * Copyright (c) 2010 Daniel Kiper
 * Copyright (c) 2018 Oleksandr Andrushchenko, EPAM Systems Inc.
 */

#ifndef _XENMEM_RESERVATION_H
#define _XENMEM_RESERVATION_H

#include <linux/highmem.h>

#include <xen/page.h>

extern bool xen_scrub_pages;

static inline void xenmem_reservation_scrub_page(struct page *page)
{
	if (xen_scrub_pages)
		clear_highpage(page);
}

#ifdef CONFIG_XEN_HAVE_PVMMU
void __xenmem_reservation_va_mapping_update(unsigned long count,
					    struct page **pages,
					    xen_pfn_t *frames);

void __xenmem_reservation_va_mapping_reset(unsigned long count,
					   struct page **pages);
#endif

static inline void xenmem_reservation_va_mapping_update(unsigned long count,
							struct page **pages,
							xen_pfn_t *frames)
{
#ifdef CONFIG_XEN_HAVE_PVMMU
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		__xenmem_reservation_va_mapping_update(count, pages, frames);
#endif
}

static inline void xenmem_reservation_va_mapping_reset(unsigned long count,
						       struct page **pages)
{
#ifdef CONFIG_XEN_HAVE_PVMMU
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		__xenmem_reservation_va_mapping_reset(count, pages);
#endif
}

int xenmem_reservation_increase(int count, xen_pfn_t *frames);

int xenmem_reservation_decrease(int count, xen_pfn_t *frames);

#endif
