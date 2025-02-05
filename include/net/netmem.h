/* SPDX-License-Identifier: GPL-2.0
 *
 *	Network memory
 *
 *	Author:	Mina Almasry <almasrymina@google.com>
 */

#ifndef _NET_NETMEM_H
#define _NET_NETMEM_H

#include <linux/mm.h>
#include <net/net_debug.h>

/* net_iov */

DECLARE_STATIC_KEY_FALSE(page_pool_mem_providers);

/*  We overload the LSB of the struct page pointer to indicate whether it's
 *  a page or net_iov.
 */
#define NET_IOV 0x01UL

struct net_iov {
	unsigned long __unused_padding;
	unsigned long pp_magic;
	struct page_pool *pp;
	struct dmabuf_genpool_chunk_owner *owner;
	unsigned long dma_addr;
	atomic_long_t pp_ref_count;
};

/* These fields in struct page are used by the page_pool and net stack:
 *
 *        struct {
 *                unsigned long pp_magic;
 *                struct page_pool *pp;
 *                unsigned long _pp_mapping_pad;
 *                unsigned long dma_addr;
 *                atomic_long_t pp_ref_count;
 *        };
 *
 * We mirror the page_pool fields here so the page_pool can access these fields
 * without worrying whether the underlying fields belong to a page or net_iov.
 *
 * The non-net stack fields of struct page are private to the mm stack and must
 * never be mirrored to net_iov.
 */
#define NET_IOV_ASSERT_OFFSET(pg, iov)             \
	static_assert(offsetof(struct page, pg) == \
		      offsetof(struct net_iov, iov))
NET_IOV_ASSERT_OFFSET(pp_magic, pp_magic);
NET_IOV_ASSERT_OFFSET(pp, pp);
NET_IOV_ASSERT_OFFSET(dma_addr, dma_addr);
NET_IOV_ASSERT_OFFSET(pp_ref_count, pp_ref_count);
#undef NET_IOV_ASSERT_OFFSET

/* netmem */

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

static inline bool netmem_is_net_iov(const netmem_ref netmem)
{
	return (__force unsigned long)netmem & NET_IOV;
}

/**
 * __netmem_to_page - unsafely get pointer to the &page backing @netmem
 * @netmem: netmem reference to convert
 *
 * Unsafe version of netmem_to_page(). When @netmem is always page-backed,
 * e.g. when it's a header buffer, performs faster and generates smaller
 * object code (no check for the LSB, no WARN). When @netmem points to IOV,
 * provokes undefined behaviour.
 *
 * Return: pointer to the &page (garbage if @netmem is not page-backed).
 */
static inline struct page *__netmem_to_page(netmem_ref netmem)
{
	return (__force struct page *)netmem;
}

/* This conversion fails (returns NULL) if the netmem_ref is not struct page
 * backed.
 */
static inline struct page *netmem_to_page(netmem_ref netmem)
{
	if (WARN_ON_ONCE(netmem_is_net_iov(netmem)))
		return NULL;

	return __netmem_to_page(netmem);
}

static inline struct net_iov *netmem_to_net_iov(netmem_ref netmem)
{
	if (netmem_is_net_iov(netmem))
		return (struct net_iov *)((__force unsigned long)netmem &
					  ~NET_IOV);

	DEBUG_NET_WARN_ON_ONCE(true);
	return NULL;
}

static inline netmem_ref net_iov_to_netmem(struct net_iov *niov)
{
	return (__force netmem_ref)((unsigned long)niov | NET_IOV);
}

static inline netmem_ref page_to_netmem(struct page *page)
{
	return (__force netmem_ref)page;
}

/**
 * virt_to_netmem - convert virtual memory pointer to a netmem reference
 * @data: host memory pointer to convert
 *
 * Return: netmem reference to the &page backing this virtual address.
 */
static inline netmem_ref virt_to_netmem(const void *data)
{
	return page_to_netmem(virt_to_page(data));
}

static inline int netmem_ref_count(netmem_ref netmem)
{
	/* The non-pp refcount of net_iov is always 1. On net_iov, we only
	 * support pp refcounting which uses the pp_ref_count field.
	 */
	if (netmem_is_net_iov(netmem))
		return 1;

	return page_ref_count(netmem_to_page(netmem));
}

static inline unsigned long netmem_pfn_trace(netmem_ref netmem)
{
	if (netmem_is_net_iov(netmem))
		return 0;

	return page_to_pfn(netmem_to_page(netmem));
}

static inline struct net_iov *__netmem_clear_lsb(netmem_ref netmem)
{
	return (struct net_iov *)((__force unsigned long)netmem & ~NET_IOV);
}

/**
 * __netmem_get_pp - unsafely get pointer to the &page_pool backing @netmem
 * @netmem: netmem reference to get the pointer from
 *
 * Unsafe version of netmem_get_pp(). When @netmem is always page-backed,
 * e.g. when it's a header buffer, performs faster and generates smaller
 * object code (avoids clearing the LSB). When @netmem points to IOV,
 * provokes invalid memory access.
 *
 * Return: pointer to the &page_pool (garbage if @netmem is not page-backed).
 */
static inline struct page_pool *__netmem_get_pp(netmem_ref netmem)
{
	return __netmem_to_page(netmem)->pp;
}

static inline struct page_pool *netmem_get_pp(netmem_ref netmem)
{
	return __netmem_clear_lsb(netmem)->pp;
}

static inline atomic_long_t *netmem_get_pp_ref_count_ref(netmem_ref netmem)
{
	return &__netmem_clear_lsb(netmem)->pp_ref_count;
}

static inline bool netmem_is_pref_nid(netmem_ref netmem, int pref_nid)
{
	/* NUMA node preference only makes sense if we're allocating
	 * system memory. Memory providers (which give us net_iovs)
	 * choose for us.
	 */
	if (netmem_is_net_iov(netmem))
		return true;

	return page_to_nid(netmem_to_page(netmem)) == pref_nid;
}

static inline netmem_ref netmem_compound_head(netmem_ref netmem)
{
	/* niov are never compounded */
	if (netmem_is_net_iov(netmem))
		return netmem;

	return page_to_netmem(compound_head(netmem_to_page(netmem)));
}

/**
 * __netmem_address - unsafely get pointer to the memory backing @netmem
 * @netmem: netmem reference to get the pointer for
 *
 * Unsafe version of netmem_address(). When @netmem is always page-backed,
 * e.g. when it's a header buffer, performs faster and generates smaller
 * object code (no check for the LSB). When @netmem points to IOV, provokes
 * undefined behaviour.
 *
 * Return: pointer to the memory (garbage if @netmem is not page-backed).
 */
static inline void *__netmem_address(netmem_ref netmem)
{
	return page_address(__netmem_to_page(netmem));
}

static inline void *netmem_address(netmem_ref netmem)
{
	if (netmem_is_net_iov(netmem))
		return NULL;

	return __netmem_address(netmem);
}

/**
 * netmem_is_pfmemalloc - check if @netmem was allocated under memory pressure
 * @netmem: netmem reference to check
 *
 * Return: true if @netmem is page-backed and the page was allocated under
 * memory pressure, false otherwise.
 */
static inline bool netmem_is_pfmemalloc(netmem_ref netmem)
{
	if (netmem_is_net_iov(netmem))
		return false;

	return page_is_pfmemalloc(netmem_to_page(netmem));
}

static inline unsigned long netmem_get_dma_addr(netmem_ref netmem)
{
	return __netmem_clear_lsb(netmem)->dma_addr;
}

#endif /* _NET_NETMEM_H */
