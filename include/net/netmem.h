/* SPDX-License-Identifier: GPL-2.0
 *
 *	Network memory
 *
 *	Author:	Mina Almasry <almasrymina@google.com>
 */

#ifndef _NET_NETMEM_H
#define _NET_NETMEM_H

/**
 * typedef netmem_ref - a nonexistent type marking a reference to generic
 * network memory.
 *
 * A netmem_ref currently is always a reference to a struct page. This
 * abstraction is introduced so support for new memory types can be added.
 *
 * Use the supplied helpers to obtain the underlying memory pointer and fields.
 */
typedef unsigned long __bitwise netmem_ref;

/* This conversion fails (returns NULL) if the netmem_ref is not struct page
 * backed.
 *
 * Currently struct page is the only possible netmem, and this helper never
 * fails.
 */
static inline struct page *netmem_to_page(netmem_ref netmem)
{
	return (__force struct page *)netmem;
}

/* Converting from page to netmem is always safe, because a page can always be
 * a netmem.
 */
static inline netmem_ref page_to_netmem(struct page *page)
{
	return (__force netmem_ref)page;
}

static inline int netmem_ref_count(netmem_ref netmem)
{
	return page_ref_count(netmem_to_page(netmem));
}

static inline unsigned long netmem_to_pfn(netmem_ref netmem)
{
	return page_to_pfn(netmem_to_page(netmem));
}

static inline netmem_ref netmem_compound_head(netmem_ref netmem)
{
	return page_to_netmem(compound_head(netmem_to_page(netmem)));
}

#endif /* _NET_NETMEM_H */
