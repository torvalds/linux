// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Routines having to do with the 'struct sk_buff' memory handlers.
 *
 *	Authors:	Alan Cox <alan@lxorguk.ukuu.org.uk>
 *			Florian La Roche <rzsfl@rz.uni-sb.de>
 *
 *	Fixes:
 *		Alan Cox	:	Fixed the worst of the load
 *					balancer bugs.
 *		Dave Platt	:	Interrupt stacking fix.
 *	Richard Kooijman	:	Timestamp fixes.
 *		Alan Cox	:	Changed buffer format.
 *		Alan Cox	:	destructor hook for AF_UNIX etc.
 *		Linus Torvalds	:	Better skb_clone.
 *		Alan Cox	:	Added skb_copy.
 *		Alan Cox	:	Added all the changed routines Linus
 *					only put in the headers
 *		Ray VanTassle	:	Fixed --skb->lock in free
 *		Alan Cox	:	skb_copy copy arp field
 *		Andi Kleen	:	slabified it.
 *		Robert Olsson	:	Removed skb_head_pool
 *
 *	NOTE:
 *		The __skb_ routines should be called with interrupts
 *	disabled, or you better be *real* sure that the operation is atomic
 *	with respect to whatever list is being frobbed (e.g. via lock_sock()
 *	or via disabling bottom half handlers, etc).
 */

/*
 *	The functions in this file will not compile correctly with gcc 2.4.x
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/sctp.h>
#include <linux/netdevice.h>
#ifdef CONFIG_NET_CLS_ACT
#include <net/pkt_sched.h>
#endif
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/splice.h>
#include <linux/cache.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/scatterlist.h>
#include <linux/errqueue.h>
#include <linux/prefetch.h>
#include <linux/if_vlan.h>
#include <linux/mpls.h>

#include <net/protocol.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <net/xfrm.h>
#include <net/mpls.h>
#include <net/mptcp.h>

#include <linux/uaccess.h>
#include <trace/events/skb.h>
#include <linux/highmem.h>
#include <linux/capability.h>
#include <linux/user_namespace.h>
#include <linux/indirect_call_wrapper.h>
#include <trace/hooks/net.h>

#include "datagram.h"

struct kmem_cache *skbuff_head_cache __ro_after_init;
static struct kmem_cache *skbuff_fclone_cache __ro_after_init;
#ifdef CONFIG_SKB_EXTENSIONS
static struct kmem_cache *skbuff_ext_cache __ro_after_init;
#endif
int sysctl_max_skb_frags __read_mostly = MAX_SKB_FRAGS;
EXPORT_SYMBOL(sysctl_max_skb_frags);

/**
 *	skb_panic - private function for out-of-line support
 *	@skb:	buffer
 *	@sz:	size
 *	@addr:	address
 *	@msg:	skb_over_panic or skb_under_panic
 *
 *	Out-of-line support for skb_put() and skb_push().
 *	Called via the wrapper skb_over_panic() or skb_under_panic().
 *	Keep out of line to prevent kernel bloat.
 *	__builtin_return_address is not used because it is not always reliable.
 */
static void skb_panic(struct sk_buff *skb, unsigned int sz, void *addr,
		      const char msg[])
{
	pr_emerg("%s: text:%px len:%d put:%d head:%px data:%px tail:%#lx end:%#lx dev:%s\n",
		 msg, addr, skb->len, sz, skb->head, skb->data,
		 (unsigned long)skb->tail, (unsigned long)skb->end,
		 skb->dev ? skb->dev->name : "<NULL>");
	BUG();
}

static void skb_over_panic(struct sk_buff *skb, unsigned int sz, void *addr)
{
	skb_panic(skb, sz, addr, __func__);
}

static void skb_under_panic(struct sk_buff *skb, unsigned int sz, void *addr)
{
	skb_panic(skb, sz, addr, __func__);
}

/*
 * kmalloc_reserve is a wrapper around kmalloc_node_track_caller that tells
 * the caller if emergency pfmemalloc reserves are being used. If it is and
 * the socket is later found to be SOCK_MEMALLOC then PFMEMALLOC reserves
 * may be used. Otherwise, the packet data may be discarded until enough
 * memory is free
 */
#define kmalloc_reserve(size, gfp, node, pfmemalloc) \
	 __kmalloc_reserve(size, gfp, node, _RET_IP_, pfmemalloc)

static void *__kmalloc_reserve(size_t size, gfp_t flags, int node,
			       unsigned long ip, bool *pfmemalloc)
{
	void *obj;
	bool ret_pfmemalloc = false;

	/*
	 * Try a regular allocation, when that fails and we're not entitled
	 * to the reserves, fail.
	 */
	obj = kmalloc_node_track_caller(size,
					flags | __GFP_NOMEMALLOC | __GFP_NOWARN,
					node);
	if (obj || !(gfp_pfmemalloc_allowed(flags)))
		goto out;

	/* Try again but now we are using pfmemalloc reserves */
	ret_pfmemalloc = true;
	obj = kmalloc_node_track_caller(size, flags, node);

out:
	if (pfmemalloc)
		*pfmemalloc = ret_pfmemalloc;

	return obj;
}

/* 	Allocate a new skbuff. We do this ourselves so we can fill in a few
 *	'private' fields and also do memory statistics to find all the
 *	[BEEP] leaks.
 *
 */

/**
 *	__alloc_skb	-	allocate a network buffer
 *	@size: size to allocate
 *	@gfp_mask: allocation mask
 *	@flags: If SKB_ALLOC_FCLONE is set, allocate from fclone cache
 *		instead of head cache and allocate a cloned (child) skb.
 *		If SKB_ALLOC_RX is set, __GFP_MEMALLOC will be used for
 *		allocations in case the data is required for writeback
 *	@node: numa node to allocate memory on
 *
 *	Allocate a new &sk_buff. The returned buffer has no headroom and a
 *	tail room of at least size bytes. The object has a reference count
 *	of one. The return is the buffer. On a failure the return is %NULL.
 *
 *	Buffers may only be allocated from interrupts using a @gfp_mask of
 *	%GFP_ATOMIC.
 */
struct sk_buff *__alloc_skb(unsigned int size, gfp_t gfp_mask,
			    int flags, int node)
{
	struct kmem_cache *cache;
	struct skb_shared_info *shinfo;
	struct sk_buff *skb;
	u8 *data;
	bool pfmemalloc;

	cache = (flags & SKB_ALLOC_FCLONE)
		? skbuff_fclone_cache : skbuff_head_cache;

	if (sk_memalloc_socks() && (flags & SKB_ALLOC_RX))
		gfp_mask |= __GFP_MEMALLOC;

	/* Get the HEAD */
	skb = kmem_cache_alloc_node(cache, gfp_mask & ~__GFP_DMA, node);
	if (!skb)
		goto out;
	prefetchw(skb);

	/* We do our best to align skb_shared_info on a separate cache
	 * line. It usually works because kmalloc(X > SMP_CACHE_BYTES) gives
	 * aligned memory blocks, unless SLUB/SLAB debug is enabled.
	 * Both skb->head and skb_shared_info are cache line aligned.
	 */
	size = SKB_DATA_ALIGN(size);
	size += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	data = kmalloc_reserve(size, gfp_mask, node, &pfmemalloc);
	if (!data)
		goto nodata;
	/* kmalloc(size) might give us more room than requested.
	 * Put skb_shared_info exactly at the end of allocated zone,
	 * to allow max possible filling before reallocation.
	 */
	size = SKB_WITH_OVERHEAD(ksize(data));
	prefetchw(data + size);

	/*
	 * Only clear those fields we need to clear, not those that we will
	 * actually initialise below. Hence, don't put any more fields after
	 * the tail pointer in struct sk_buff!
	 */
	memset(skb, 0, offsetof(struct sk_buff, tail));
	/* Account for allocated memory : skb + skb->head */
	skb->truesize = SKB_TRUESIZE(size);
	skb->pfmemalloc = pfmemalloc;
	refcount_set(&skb->users, 1);
	skb->head = data;
	skb->data = data;
	skb_reset_tail_pointer(skb);
	skb->end = skb->tail + size;
	skb->mac_header = (typeof(skb->mac_header))~0U;
	skb->transport_header = (typeof(skb->transport_header))~0U;

	/* make sure we initialize shinfo sequentially */
	shinfo = skb_shinfo(skb);
	memset(shinfo, 0, offsetof(struct skb_shared_info, dataref));
	atomic_set(&shinfo->dataref, 1);

	if (flags & SKB_ALLOC_FCLONE) {
		struct sk_buff_fclones *fclones;

		fclones = container_of(skb, struct sk_buff_fclones, skb1);

		skb->fclone = SKB_FCLONE_ORIG;
		refcount_set(&fclones->fclone_ref, 1);

		fclones->skb2.fclone = SKB_FCLONE_CLONE;
	}

	skb_set_kcov_handle(skb, kcov_common_handle());

out:
	return skb;
nodata:
	kmem_cache_free(cache, skb);
	skb = NULL;
	goto out;
}
EXPORT_SYMBOL(__alloc_skb);

/* Caller must provide SKB that is memset cleared */
static struct sk_buff *__build_skb_around(struct sk_buff *skb,
					  void *data, unsigned int frag_size)
{
	struct skb_shared_info *shinfo;
	unsigned int size = frag_size ? : ksize(data);

	size -= SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	/* Assumes caller memset cleared SKB */
	skb->truesize = SKB_TRUESIZE(size);
	refcount_set(&skb->users, 1);
	skb->head = data;
	skb->data = data;
	skb_reset_tail_pointer(skb);
	skb->end = skb->tail + size;
	skb->mac_header = (typeof(skb->mac_header))~0U;
	skb->transport_header = (typeof(skb->transport_header))~0U;

	/* make sure we initialize shinfo sequentially */
	shinfo = skb_shinfo(skb);
	memset(shinfo, 0, offsetof(struct skb_shared_info, dataref));
	atomic_set(&shinfo->dataref, 1);

	skb_set_kcov_handle(skb, kcov_common_handle());

	return skb;
}

/**
 * __build_skb - build a network buffer
 * @data: data buffer provided by caller
 * @frag_size: size of data, or 0 if head was kmalloced
 *
 * Allocate a new &sk_buff. Caller provides space holding head and
 * skb_shared_info. @data must have been allocated by kmalloc() only if
 * @frag_size is 0, otherwise data should come from the page allocator
 *  or vmalloc()
 * The return is the new skb buffer.
 * On a failure the return is %NULL, and @data is not freed.
 * Notes :
 *  Before IO, driver allocates only data buffer where NIC put incoming frame
 *  Driver should add room at head (NET_SKB_PAD) and
 *  MUST add room at tail (SKB_DATA_ALIGN(skb_shared_info))
 *  After IO, driver calls build_skb(), to allocate sk_buff and populate it
 *  before giving packet to stack.
 *  RX rings only contains data buffers, not full skbs.
 */
struct sk_buff *__build_skb(void *data, unsigned int frag_size)
{
	struct sk_buff *skb;

	skb = kmem_cache_alloc(skbuff_head_cache, GFP_ATOMIC);
	if (unlikely(!skb))
		return NULL;

	memset(skb, 0, offsetof(struct sk_buff, tail));

	return __build_skb_around(skb, data, frag_size);
}

/* build_skb() is wrapper over __build_skb(), that specifically
 * takes care of skb->head and skb->pfmemalloc
 * This means that if @frag_size is not zero, then @data must be backed
 * by a page fragment, not kmalloc() or vmalloc()
 */
struct sk_buff *build_skb(void *data, unsigned int frag_size)
{
	struct sk_buff *skb = __build_skb(data, frag_size);

	if (skb && frag_size) {
		skb->head_frag = 1;
		if (page_is_pfmemalloc(virt_to_head_page(data)))
			skb->pfmemalloc = 1;
	}
	return skb;
}
EXPORT_SYMBOL(build_skb);

/**
 * build_skb_around - build a network buffer around provided skb
 * @skb: sk_buff provide by caller, must be memset cleared
 * @data: data buffer provided by caller
 * @frag_size: size of data, or 0 if head was kmalloced
 */
struct sk_buff *build_skb_around(struct sk_buff *skb,
				 void *data, unsigned int frag_size)
{
	if (unlikely(!skb))
		return NULL;

	skb = __build_skb_around(skb, data, frag_size);

	if (skb && frag_size) {
		skb->head_frag = 1;
		if (page_is_pfmemalloc(virt_to_head_page(data)))
			skb->pfmemalloc = 1;
	}
	return skb;
}
EXPORT_SYMBOL(build_skb_around);

#define NAPI_SKB_CACHE_SIZE	64

struct napi_alloc_cache {
	struct page_frag_cache page;
	unsigned int skb_count;
	void *skb_cache[NAPI_SKB_CACHE_SIZE];
};

static DEFINE_PER_CPU(struct page_frag_cache, netdev_alloc_cache);
static DEFINE_PER_CPU(struct napi_alloc_cache, napi_alloc_cache);

static void *__napi_alloc_frag(unsigned int fragsz, gfp_t gfp_mask)
{
	struct napi_alloc_cache *nc = this_cpu_ptr(&napi_alloc_cache);

	return page_frag_alloc(&nc->page, fragsz, gfp_mask);
}

void *napi_alloc_frag(unsigned int fragsz)
{
	fragsz = SKB_DATA_ALIGN(fragsz);

	return __napi_alloc_frag(fragsz, GFP_ATOMIC);
}
EXPORT_SYMBOL(napi_alloc_frag);

/**
 * netdev_alloc_frag - allocate a page fragment
 * @fragsz: fragment size
 *
 * Allocates a frag from a page for receive buffer.
 * Uses GFP_ATOMIC allocations.
 */
void *netdev_alloc_frag(unsigned int fragsz)
{
	struct page_frag_cache *nc;
	void *data;

	fragsz = SKB_DATA_ALIGN(fragsz);
	if (in_irq() || irqs_disabled()) {
		nc = this_cpu_ptr(&netdev_alloc_cache);
		data = page_frag_alloc(nc, fragsz, GFP_ATOMIC);
	} else {
		local_bh_disable();
		data = __napi_alloc_frag(fragsz, GFP_ATOMIC);
		local_bh_enable();
	}
	return data;
}
EXPORT_SYMBOL(netdev_alloc_frag);

/**
 *	__netdev_alloc_skb - allocate an skbuff for rx on a specific device
 *	@dev: network device to receive on
 *	@len: length to allocate
 *	@gfp_mask: get_free_pages mask, passed to alloc_skb
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has NET_SKB_PAD headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *	%NULL is returned if there is no free memory.
 */
struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int len,
				   gfp_t gfp_mask)
{
	struct page_frag_cache *nc;
	struct sk_buff *skb;
	bool pfmemalloc;
	void *data;

	len += NET_SKB_PAD;

	/* If requested length is either too small or too big,
	 * we use kmalloc() for skb->head allocation.
	 */
	if (len <= SKB_WITH_OVERHEAD(1024) ||
	    len > SKB_WITH_OVERHEAD(PAGE_SIZE) ||
	    (gfp_mask & (__GFP_DIRECT_RECLAIM | GFP_DMA))) {
		skb = __alloc_skb(len, gfp_mask, SKB_ALLOC_RX, NUMA_NO_NODE);
		if (!skb)
			goto skb_fail;
		goto skb_success;
	}

	len += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	len = SKB_DATA_ALIGN(len);

	if (sk_memalloc_socks())
		gfp_mask |= __GFP_MEMALLOC;

	if (in_irq() || irqs_disabled()) {
		nc = this_cpu_ptr(&netdev_alloc_cache);
		data = page_frag_alloc(nc, len, gfp_mask);
		pfmemalloc = nc->pfmemalloc;
	} else {
		local_bh_disable();
		nc = this_cpu_ptr(&napi_alloc_cache.page);
		data = page_frag_alloc(nc, len, gfp_mask);
		pfmemalloc = nc->pfmemalloc;
		local_bh_enable();
	}

	if (unlikely(!data))
		return NULL;

	skb = __build_skb(data, len);
	if (unlikely(!skb)) {
		skb_free_frag(data);
		return NULL;
	}

	if (pfmemalloc)
		skb->pfmemalloc = 1;
	skb->head_frag = 1;

skb_success:
	skb_reserve(skb, NET_SKB_PAD);
	skb->dev = dev;

skb_fail:
	return skb;
}
EXPORT_SYMBOL(__netdev_alloc_skb);

/**
 *	__napi_alloc_skb - allocate skbuff for rx in a specific NAPI instance
 *	@napi: napi instance this buffer was allocated for
 *	@len: length to allocate
 *	@gfp_mask: get_free_pages mask, passed to alloc_skb and alloc_pages
 *
 *	Allocate a new sk_buff for use in NAPI receive.  This buffer will
 *	attempt to allocate the head from a special reserved region used
 *	only for NAPI Rx allocation.  By doing this we can save several
 *	CPU cycles by avoiding having to disable and re-enable IRQs.
 *
 *	%NULL is returned if there is no free memory.
 */
struct sk_buff *__napi_alloc_skb(struct napi_struct *napi, unsigned int len,
				 gfp_t gfp_mask)
{
	struct napi_alloc_cache *nc;
	struct sk_buff *skb;
	void *data;

	len += NET_SKB_PAD + NET_IP_ALIGN;

	/* If requested length is either too small or too big,
	 * we use kmalloc() for skb->head allocation.
	 */
	if (len <= SKB_WITH_OVERHEAD(1024) ||
	    len > SKB_WITH_OVERHEAD(PAGE_SIZE) ||
	    (gfp_mask & (__GFP_DIRECT_RECLAIM | GFP_DMA))) {
		skb = __alloc_skb(len, gfp_mask, SKB_ALLOC_RX, NUMA_NO_NODE);
		if (!skb)
			goto skb_fail;
		goto skb_success;
	}

	nc = this_cpu_ptr(&napi_alloc_cache);
	len += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	len = SKB_DATA_ALIGN(len);

	if (sk_memalloc_socks())
		gfp_mask |= __GFP_MEMALLOC;

	data = page_frag_alloc(&nc->page, len, gfp_mask);
	if (unlikely(!data))
		return NULL;

	skb = __build_skb(data, len);
	if (unlikely(!skb)) {
		skb_free_frag(data);
		return NULL;
	}

	if (nc->page.pfmemalloc)
		skb->pfmemalloc = 1;
	skb->head_frag = 1;

skb_success:
	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	skb->dev = napi->dev;

skb_fail:
	return skb;
}
EXPORT_SYMBOL(__napi_alloc_skb);

void skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page, int off,
		     int size, unsigned int truesize)
{
	skb_fill_page_desc(skb, i, page, off, size);
	skb->len += size;
	skb->data_len += size;
	skb->truesize += truesize;
}
EXPORT_SYMBOL(skb_add_rx_frag);

void skb_coalesce_rx_frag(struct sk_buff *skb, int i, int size,
			  unsigned int truesize)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

	skb_frag_size_add(frag, size);
	skb->len += size;
	skb->data_len += size;
	skb->truesize += truesize;
}
EXPORT_SYMBOL(skb_coalesce_rx_frag);

static void skb_drop_list(struct sk_buff **listp)
{
	kfree_skb_list(*listp);
	*listp = NULL;
}

static inline void skb_drop_fraglist(struct sk_buff *skb)
{
	skb_drop_list(&skb_shinfo(skb)->frag_list);
}

static void skb_clone_fraglist(struct sk_buff *skb)
{
	struct sk_buff *list;

	skb_walk_frags(skb, list)
		skb_get(list);
}

static void skb_free_head(struct sk_buff *skb)
{
	unsigned char *head = skb->head;

	if (skb->head_frag)
		skb_free_frag(head);
	else
		kfree(head);
}

static void skb_release_data(struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int i;

	if (skb->cloned &&
	    atomic_sub_return(skb->nohdr ? (1 << SKB_DATAREF_SHIFT) + 1 : 1,
			      &shinfo->dataref))
		return;

	for (i = 0; i < shinfo->nr_frags; i++)
		__skb_frag_unref(&shinfo->frags[i]);

	if (shinfo->frag_list)
		kfree_skb_list(shinfo->frag_list);

	skb_zcopy_clear(skb, true);
	skb_free_head(skb);
}

/*
 *	Free an skbuff by memory without cleaning the state.
 */
static void kfree_skbmem(struct sk_buff *skb)
{
	struct sk_buff_fclones *fclones;

	switch (skb->fclone) {
	case SKB_FCLONE_UNAVAILABLE:
		kmem_cache_free(skbuff_head_cache, skb);
		return;

	case SKB_FCLONE_ORIG:
		fclones = container_of(skb, struct sk_buff_fclones, skb1);

		/* We usually free the clone (TX completion) before original skb
		 * This test would have no chance to be true for the clone,
		 * while here, branch prediction will be good.
		 */
		if (refcount_read(&fclones->fclone_ref) == 1)
			goto fastpath;
		break;

	default: /* SKB_FCLONE_CLONE */
		fclones = container_of(skb, struct sk_buff_fclones, skb2);
		break;
	}
	if (!refcount_dec_and_test(&fclones->fclone_ref))
		return;
fastpath:
	kmem_cache_free(skbuff_fclone_cache, fclones);
}

void skb_release_head_state(struct sk_buff *skb)
{
	nf_reset_ct(skb);
	skb_dst_drop(skb);
	if (skb->destructor) {
		WARN_ON(in_irq());
		skb->destructor(skb);
	}
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	nf_conntrack_put(skb_nfct(skb));
#endif
	skb_ext_put(skb);
}

/* Free everything but the sk_buff shell. */
static void skb_release_all(struct sk_buff *skb)
{
	skb_release_head_state(skb);
	if (likely(skb->head))
		skb_release_data(skb);
}

/**
 *	__kfree_skb - private function
 *	@skb: buffer
 *
 *	Free an sk_buff. Release anything attached to the buffer.
 *	Clean the state. This is an internal helper function. Users should
 *	always call kfree_skb
 */

void __kfree_skb(struct sk_buff *skb)
{
	skb_release_all(skb);
	kfree_skbmem(skb);
}
EXPORT_SYMBOL(__kfree_skb);

/**
 *	kfree_skb - free an sk_buff
 *	@skb: buffer to free
 *
 *	Drop a reference to the buffer and free it if the usage count has
 *	hit zero.
 */
void kfree_skb(struct sk_buff *skb)
{
	if (!skb_unref(skb))
		return;

	trace_android_vh_kfree_skb(skb);
	trace_kfree_skb(skb, __builtin_return_address(0));
	__kfree_skb(skb);
}
EXPORT_SYMBOL(kfree_skb);

void kfree_skb_list(struct sk_buff *segs)
{
	while (segs) {
		struct sk_buff *next = segs->next;

		kfree_skb(segs);
		segs = next;
	}
}
EXPORT_SYMBOL(kfree_skb_list);

/* Dump skb information and contents.
 *
 * Must only be called from net_ratelimit()-ed paths.
 *
 * Dumps whole packets if full_pkt, only headers otherwise.
 */
void skb_dump(const char *level, const struct sk_buff *skb, bool full_pkt)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	struct net_device *dev = skb->dev;
	struct sock *sk = skb->sk;
	struct sk_buff *list_skb;
	bool has_mac, has_trans;
	int headroom, tailroom;
	int i, len, seg_len;

	if (full_pkt)
		len = skb->len;
	else
		len = min_t(int, skb->len, MAX_HEADER + 128);

	headroom = skb_headroom(skb);
	tailroom = skb_tailroom(skb);

	has_mac = skb_mac_header_was_set(skb);
	has_trans = skb_transport_header_was_set(skb);

	printk("%sskb len=%u headroom=%u headlen=%u tailroom=%u\n"
	       "mac=(%d,%d) net=(%d,%d) trans=%d\n"
	       "shinfo(txflags=%u nr_frags=%u gso(size=%hu type=%u segs=%hu))\n"
	       "csum(0x%x ip_summed=%u complete_sw=%u valid=%u level=%u)\n"
	       "hash(0x%x sw=%u l4=%u) proto=0x%04x pkttype=%u iif=%d\n",
	       level, skb->len, headroom, skb_headlen(skb), tailroom,
	       has_mac ? skb->mac_header : -1,
	       has_mac ? skb_mac_header_len(skb) : -1,
	       skb->network_header,
	       has_trans ? skb_network_header_len(skb) : -1,
	       has_trans ? skb->transport_header : -1,
	       sh->tx_flags, sh->nr_frags,
	       sh->gso_size, sh->gso_type, sh->gso_segs,
	       skb->csum, skb->ip_summed, skb->csum_complete_sw,
	       skb->csum_valid, skb->csum_level,
	       skb->hash, skb->sw_hash, skb->l4_hash,
	       ntohs(skb->protocol), skb->pkt_type, skb->skb_iif);

	if (dev)
		printk("%sdev name=%s feat=%pNF\n",
		       level, dev->name, &dev->features);
	if (sk)
		printk("%ssk family=%hu type=%u proto=%u\n",
		       level, sk->sk_family, sk->sk_type, sk->sk_protocol);

	if (full_pkt && headroom)
		print_hex_dump(level, "skb headroom: ", DUMP_PREFIX_OFFSET,
			       16, 1, skb->head, headroom, false);

	seg_len = min_t(int, skb_headlen(skb), len);
	if (seg_len)
		print_hex_dump(level, "skb linear:   ", DUMP_PREFIX_OFFSET,
			       16, 1, skb->data, seg_len, false);
	len -= seg_len;

	if (full_pkt && tailroom)
		print_hex_dump(level, "skb tailroom: ", DUMP_PREFIX_OFFSET,
			       16, 1, skb_tail_pointer(skb), tailroom, false);

	for (i = 0; len && i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		u32 p_off, p_len, copied;
		struct page *p;
		u8 *vaddr;

		skb_frag_foreach_page(frag, skb_frag_off(frag),
				      skb_frag_size(frag), p, p_off, p_len,
				      copied) {
			seg_len = min_t(int, p_len, len);
			vaddr = kmap_atomic(p);
			print_hex_dump(level, "skb frag:     ",
				       DUMP_PREFIX_OFFSET,
				       16, 1, vaddr + p_off, seg_len, false);
			kunmap_atomic(vaddr);
			len -= seg_len;
			if (!len)
				break;
		}
	}

	if (full_pkt && skb_has_frag_list(skb)) {
		printk("skb fraglist:\n");
		skb_walk_frags(skb, list_skb)
			skb_dump(level, list_skb, true);
	}
}
EXPORT_SYMBOL(skb_dump);

/**
 *	skb_tx_error - report an sk_buff xmit error
 *	@skb: buffer that triggered an error
 *
 *	Report xmit error if a device callback is tracking this skb.
 *	skb must be freed afterwards.
 */
void skb_tx_error(struct sk_buff *skb)
{
	skb_zcopy_clear(skb, true);
}
EXPORT_SYMBOL(skb_tx_error);

#ifdef CONFIG_TRACEPOINTS
/**
 *	consume_skb - free an skbuff
 *	@skb: buffer to free
 *
 *	Drop a ref to the buffer and free it if the usage count has hit zero
 *	Functions identically to kfree_skb, but kfree_skb assumes that the frame
 *	is being dropped after a failure and notes that
 */
void consume_skb(struct sk_buff *skb)
{
	if (!skb_unref(skb))
		return;

	trace_consume_skb(skb);
	__kfree_skb(skb);
}
EXPORT_SYMBOL(consume_skb);
#endif

/**
 *	consume_stateless_skb - free an skbuff, assuming it is stateless
 *	@skb: buffer to free
 *
 *	Alike consume_skb(), but this variant assumes that this is the last
 *	skb reference and all the head states have been already dropped
 */
void __consume_stateless_skb(struct sk_buff *skb)
{
	trace_consume_skb(skb);
	skb_release_data(skb);
	kfree_skbmem(skb);
}

void __kfree_skb_flush(void)
{
	struct napi_alloc_cache *nc = this_cpu_ptr(&napi_alloc_cache);

	/* flush skb_cache if containing objects */
	if (nc->skb_count) {
		kmem_cache_free_bulk(skbuff_head_cache, nc->skb_count,
				     nc->skb_cache);
		nc->skb_count = 0;
	}
}

static inline void _kfree_skb_defer(struct sk_buff *skb)
{
	struct napi_alloc_cache *nc = this_cpu_ptr(&napi_alloc_cache);

	/* drop skb->head and call any destructors for packet */
	skb_release_all(skb);

	/* record skb to CPU local list */
	nc->skb_cache[nc->skb_count++] = skb;

#ifdef CONFIG_SLUB
	/* SLUB writes into objects when freeing */
	prefetchw(skb);
#endif

	/* flush skb_cache if it is filled */
	if (unlikely(nc->skb_count == NAPI_SKB_CACHE_SIZE)) {
		kmem_cache_free_bulk(skbuff_head_cache, NAPI_SKB_CACHE_SIZE,
				     nc->skb_cache);
		nc->skb_count = 0;
	}
}
void __kfree_skb_defer(struct sk_buff *skb)
{
	_kfree_skb_defer(skb);
}

void napi_consume_skb(struct sk_buff *skb, int budget)
{
	/* Zero budget indicate non-NAPI context called us, like netpoll */
	if (unlikely(!budget)) {
		dev_consume_skb_any(skb);
		return;
	}

	if (!skb_unref(skb))
		return;

	/* if reaching here SKB is ready to free */
	trace_consume_skb(skb);

	/* if SKB is a clone, don't handle this case */
	if (skb->fclone != SKB_FCLONE_UNAVAILABLE) {
		__kfree_skb(skb);
		return;
	}

	_kfree_skb_defer(skb);
}
EXPORT_SYMBOL(napi_consume_skb);

/* Make sure a field is enclosed inside headers_start/headers_end section */
#define CHECK_SKB_FIELD(field) \
	BUILD_BUG_ON(offsetof(struct sk_buff, field) <		\
		     offsetof(struct sk_buff, headers_start));	\
	BUILD_BUG_ON(offsetof(struct sk_buff, field) >		\
		     offsetof(struct sk_buff, headers_end));	\

static void __copy_skb_header(struct sk_buff *new, const struct sk_buff *old)
{
	new->tstamp		= old->tstamp;
	/* We do not copy old->sk */
	new->dev		= old->dev;
	memcpy(new->cb, old->cb, sizeof(old->cb));
	skb_dst_copy(new, old);
	__skb_ext_copy(new, old);
	__nf_copy(new, old, false);

	/* Note : this field could be in headers_start/headers_end section
	 * It is not yet because we do not want to have a 16 bit hole
	 */
	new->queue_mapping = old->queue_mapping;

	memcpy(&new->headers_start, &old->headers_start,
	       offsetof(struct sk_buff, headers_end) -
	       offsetof(struct sk_buff, headers_start));
	CHECK_SKB_FIELD(protocol);
	CHECK_SKB_FIELD(csum);
	CHECK_SKB_FIELD(hash);
	CHECK_SKB_FIELD(priority);
	CHECK_SKB_FIELD(skb_iif);
	CHECK_SKB_FIELD(vlan_proto);
	CHECK_SKB_FIELD(vlan_tci);
	CHECK_SKB_FIELD(transport_header);
	CHECK_SKB_FIELD(network_header);
	CHECK_SKB_FIELD(mac_header);
	CHECK_SKB_FIELD(inner_protocol);
	CHECK_SKB_FIELD(inner_transport_header);
	CHECK_SKB_FIELD(inner_network_header);
	CHECK_SKB_FIELD(inner_mac_header);
	CHECK_SKB_FIELD(mark);
#ifdef CONFIG_NETWORK_SECMARK
	CHECK_SKB_FIELD(secmark);
#endif
#ifdef CONFIG_NET_RX_BUSY_POLL
	CHECK_SKB_FIELD(napi_id);
#endif
#ifdef CONFIG_XPS
	CHECK_SKB_FIELD(sender_cpu);
#endif
#ifdef CONFIG_NET_SCHED
	CHECK_SKB_FIELD(tc_index);
#endif

}

/*
 * You should not add any new code to this function.  Add it to
 * __copy_skb_header above instead.
 */
static struct sk_buff *__skb_clone(struct sk_buff *n, struct sk_buff *skb)
{
#define C(x) n->x = skb->x

	n->next = n->prev = NULL;
	n->sk = NULL;
	__copy_skb_header(n, skb);

	C(len);
	C(data_len);
	C(mac_len);
	n->hdr_len = skb->nohdr ? skb_headroom(skb) : skb->hdr_len;
	n->cloned = 1;
	n->nohdr = 0;
	n->peeked = 0;
	C(pfmemalloc);
	n->destructor = NULL;
	C(tail);
	C(end);
	C(head);
	C(head_frag);
	C(data);
	C(truesize);
	refcount_set(&n->users, 1);

	atomic_inc(&(skb_shinfo(skb)->dataref));
	skb->cloned = 1;

	return n;
#undef C
}

/**
 * alloc_skb_for_msg() - allocate sk_buff to wrap frag list forming a msg
 * @first: first sk_buff of the msg
 */
struct sk_buff *alloc_skb_for_msg(struct sk_buff *first)
{
	struct sk_buff *n;

	n = alloc_skb(0, GFP_ATOMIC);
	if (!n)
		return NULL;

	n->len = first->len;
	n->data_len = first->len;
	n->truesize = first->truesize;

	skb_shinfo(n)->frag_list = first;

	__copy_skb_header(n, first);
	n->destructor = NULL;

	return n;
}
EXPORT_SYMBOL_GPL(alloc_skb_for_msg);

/**
 *	skb_morph	-	morph one skb into another
 *	@dst: the skb to receive the contents
 *	@src: the skb to supply the contents
 *
 *	This is identical to skb_clone except that the target skb is
 *	supplied by the user.
 *
 *	The target skb is returned upon exit.
 */
struct sk_buff *skb_morph(struct sk_buff *dst, struct sk_buff *src)
{
	skb_release_all(dst);
	return __skb_clone(dst, src);
}
EXPORT_SYMBOL_GPL(skb_morph);

int mm_account_pinned_pages(struct mmpin *mmp, size_t size)
{
	unsigned long max_pg, num_pg, new_pg, old_pg;
	struct user_struct *user;

	if (capable(CAP_IPC_LOCK) || !size)
		return 0;

	num_pg = (size >> PAGE_SHIFT) + 2;	/* worst case */
	max_pg = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	user = mmp->user ? : current_user();

	do {
		old_pg = atomic_long_read(&user->locked_vm);
		new_pg = old_pg + num_pg;
		if (new_pg > max_pg)
			return -ENOBUFS;
	} while (atomic_long_cmpxchg(&user->locked_vm, old_pg, new_pg) !=
		 old_pg);

	if (!mmp->user) {
		mmp->user = get_uid(user);
		mmp->num_pg = num_pg;
	} else {
		mmp->num_pg += num_pg;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mm_account_pinned_pages);

void mm_unaccount_pinned_pages(struct mmpin *mmp)
{
	if (mmp->user) {
		atomic_long_sub(mmp->num_pg, &mmp->user->locked_vm);
		free_uid(mmp->user);
	}
}
EXPORT_SYMBOL_GPL(mm_unaccount_pinned_pages);

struct ubuf_info *sock_zerocopy_alloc(struct sock *sk, size_t size)
{
	struct ubuf_info *uarg;
	struct sk_buff *skb;

	WARN_ON_ONCE(!in_task());

	skb = sock_omalloc(sk, 0, GFP_KERNEL);
	if (!skb)
		return NULL;

	BUILD_BUG_ON(sizeof(*uarg) > sizeof(skb->cb));
	uarg = (void *)skb->cb;
	uarg->mmp.user = NULL;

	if (mm_account_pinned_pages(&uarg->mmp, size)) {
		kfree_skb(skb);
		return NULL;
	}

	uarg->callback = sock_zerocopy_callback;
	uarg->id = ((u32)atomic_inc_return(&sk->sk_zckey)) - 1;
	uarg->len = 1;
	uarg->bytelen = size;
	uarg->zerocopy = 1;
	refcount_set(&uarg->refcnt, 1);
	sock_hold(sk);

	return uarg;
}
EXPORT_SYMBOL_GPL(sock_zerocopy_alloc);

static inline struct sk_buff *skb_from_uarg(struct ubuf_info *uarg)
{
	return container_of((void *)uarg, struct sk_buff, cb);
}

struct ubuf_info *sock_zerocopy_realloc(struct sock *sk, size_t size,
					struct ubuf_info *uarg)
{
	if (uarg) {
		const u32 byte_limit = 1 << 19;		/* limit to a few TSO */
		u32 bytelen, next;

		/* realloc only when socket is locked (TCP, UDP cork),
		 * so uarg->len and sk_zckey access is serialized
		 */
		if (!sock_owned_by_user(sk)) {
			WARN_ON_ONCE(1);
			return NULL;
		}

		bytelen = uarg->bytelen + size;
		if (uarg->len == USHRT_MAX - 1 || bytelen > byte_limit) {
			/* TCP can create new skb to attach new uarg */
			if (sk->sk_type == SOCK_STREAM)
				goto new_alloc;
			return NULL;
		}

		next = (u32)atomic_read(&sk->sk_zckey);
		if ((u32)(uarg->id + uarg->len) == next) {
			if (mm_account_pinned_pages(&uarg->mmp, size))
				return NULL;
			uarg->len++;
			uarg->bytelen = bytelen;
			atomic_set(&sk->sk_zckey, ++next);

			/* no extra ref when appending to datagram (MSG_MORE) */
			if (sk->sk_type == SOCK_STREAM)
				sock_zerocopy_get(uarg);

			return uarg;
		}
	}

new_alloc:
	return sock_zerocopy_alloc(sk, size);
}
EXPORT_SYMBOL_GPL(sock_zerocopy_realloc);

static bool skb_zerocopy_notify_extend(struct sk_buff *skb, u32 lo, u16 len)
{
	struct sock_exterr_skb *serr = SKB_EXT_ERR(skb);
	u32 old_lo, old_hi;
	u64 sum_len;

	old_lo = serr->ee.ee_info;
	old_hi = serr->ee.ee_data;
	sum_len = old_hi - old_lo + 1ULL + len;

	if (sum_len >= (1ULL << 32))
		return false;

	if (lo != old_hi + 1)
		return false;

	serr->ee.ee_data += len;
	return true;
}

void sock_zerocopy_callback(struct ubuf_info *uarg, bool success)
{
	struct sk_buff *tail, *skb = skb_from_uarg(uarg);
	struct sock_exterr_skb *serr;
	struct sock *sk = skb->sk;
	struct sk_buff_head *q;
	unsigned long flags;
	u32 lo, hi;
	u16 len;

	mm_unaccount_pinned_pages(&uarg->mmp);

	/* if !len, there was only 1 call, and it was aborted
	 * so do not queue a completion notification
	 */
	if (!uarg->len || sock_flag(sk, SOCK_DEAD))
		goto release;

	len = uarg->len;
	lo = uarg->id;
	hi = uarg->id + len - 1;

	serr = SKB_EXT_ERR(skb);
	memset(serr, 0, sizeof(*serr));
	serr->ee.ee_errno = 0;
	serr->ee.ee_origin = SO_EE_ORIGIN_ZEROCOPY;
	serr->ee.ee_data = hi;
	serr->ee.ee_info = lo;
	if (!success)
		serr->ee.ee_code |= SO_EE_CODE_ZEROCOPY_COPIED;

	q = &sk->sk_error_queue;
	spin_lock_irqsave(&q->lock, flags);
	tail = skb_peek_tail(q);
	if (!tail || SKB_EXT_ERR(tail)->ee.ee_origin != SO_EE_ORIGIN_ZEROCOPY ||
	    !skb_zerocopy_notify_extend(tail, lo, len)) {
		__skb_queue_tail(q, skb);
		skb = NULL;
	}
	spin_unlock_irqrestore(&q->lock, flags);

	sk->sk_error_report(sk);

release:
	consume_skb(skb);
	sock_put(sk);
}
EXPORT_SYMBOL_GPL(sock_zerocopy_callback);

void sock_zerocopy_put(struct ubuf_info *uarg)
{
	if (uarg && refcount_dec_and_test(&uarg->refcnt)) {
		if (uarg->callback)
			uarg->callback(uarg, uarg->zerocopy);
		else
			consume_skb(skb_from_uarg(uarg));
	}
}
EXPORT_SYMBOL_GPL(sock_zerocopy_put);

void sock_zerocopy_put_abort(struct ubuf_info *uarg, bool have_uref)
{
	if (uarg) {
		struct sock *sk = skb_from_uarg(uarg)->sk;

		atomic_dec(&sk->sk_zckey);
		uarg->len--;

		if (have_uref)
			sock_zerocopy_put(uarg);
	}
}
EXPORT_SYMBOL_GPL(sock_zerocopy_put_abort);

int skb_zerocopy_iter_dgram(struct sk_buff *skb, struct msghdr *msg, int len)
{
	return __zerocopy_sg_from_iter(skb->sk, skb, &msg->msg_iter, len);
}
EXPORT_SYMBOL_GPL(skb_zerocopy_iter_dgram);

int skb_zerocopy_iter_stream(struct sock *sk, struct sk_buff *skb,
			     struct msghdr *msg, int len,
			     struct ubuf_info *uarg)
{
	struct ubuf_info *orig_uarg = skb_zcopy(skb);
	struct iov_iter orig_iter = msg->msg_iter;
	int err, orig_len = skb->len;

	/* An skb can only point to one uarg. This edge case happens when
	 * TCP appends to an skb, but zerocopy_realloc triggered a new alloc.
	 */
	if (orig_uarg && uarg != orig_uarg)
		return -EEXIST;

	err = __zerocopy_sg_from_iter(sk, skb, &msg->msg_iter, len);
	if (err == -EFAULT || (err == -EMSGSIZE && skb->len == orig_len)) {
		struct sock *save_sk = skb->sk;

		/* Streams do not free skb on error. Reset to prev state. */
		msg->msg_iter = orig_iter;
		skb->sk = sk;
		___pskb_trim(skb, orig_len);
		skb->sk = save_sk;
		return err;
	}

	skb_zcopy_set(skb, uarg, NULL);
	return skb->len - orig_len;
}
EXPORT_SYMBOL_GPL(skb_zerocopy_iter_stream);

static int skb_zerocopy_clone(struct sk_buff *nskb, struct sk_buff *orig,
			      gfp_t gfp_mask)
{
	if (skb_zcopy(orig)) {
		if (skb_zcopy(nskb)) {
			/* !gfp_mask callers are verified to !skb_zcopy(nskb) */
			if (!gfp_mask) {
				WARN_ON_ONCE(1);
				return -ENOMEM;
			}
			if (skb_uarg(nskb) == skb_uarg(orig))
				return 0;
			if (skb_copy_ubufs(nskb, GFP_ATOMIC))
				return -EIO;
		}
		skb_zcopy_set(nskb, skb_uarg(orig), NULL);
	}
	return 0;
}

/**
 *	skb_copy_ubufs	-	copy userspace skb frags buffers to kernel
 *	@skb: the skb to modify
 *	@gfp_mask: allocation priority
 *
 *	This must be called on SKBTX_DEV_ZEROCOPY skb.
 *	It will copy all frags into kernel and drop the reference
 *	to userspace pages.
 *
 *	If this function is called from an interrupt gfp_mask() must be
 *	%GFP_ATOMIC.
 *
 *	Returns 0 on success or a negative error code on failure
 *	to allocate kernel memory to copy to.
 */
int skb_copy_ubufs(struct sk_buff *skb, gfp_t gfp_mask)
{
	int num_frags = skb_shinfo(skb)->nr_frags;
	struct page *page, *head = NULL;
	int i, new_frags;
	u32 d_off;

	if (skb_shared(skb) || skb_unclone(skb, gfp_mask))
		return -EINVAL;

	if (!num_frags)
		goto release;

	new_frags = (__skb_pagelen(skb) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	for (i = 0; i < new_frags; i++) {
		page = alloc_page(gfp_mask);
		if (!page) {
			while (head) {
				struct page *next = (struct page *)page_private(head);
				put_page(head);
				head = next;
			}
			return -ENOMEM;
		}
		set_page_private(page, (unsigned long)head);
		head = page;
	}

	page = head;
	d_off = 0;
	for (i = 0; i < num_frags; i++) {
		skb_frag_t *f = &skb_shinfo(skb)->frags[i];
		u32 p_off, p_len, copied;
		struct page *p;
		u8 *vaddr;

		skb_frag_foreach_page(f, skb_frag_off(f), skb_frag_size(f),
				      p, p_off, p_len, copied) {
			u32 copy, done = 0;
			vaddr = kmap_atomic(p);

			while (done < p_len) {
				if (d_off == PAGE_SIZE) {
					d_off = 0;
					page = (struct page *)page_private(page);
				}
				copy = min_t(u32, PAGE_SIZE - d_off, p_len - done);
				memcpy(page_address(page) + d_off,
				       vaddr + p_off + done, copy);
				done += copy;
				d_off += copy;
			}
			kunmap_atomic(vaddr);
		}
	}

	/* skb frags release userspace buffers */
	for (i = 0; i < num_frags; i++)
		skb_frag_unref(skb, i);

	/* skb frags point to kernel buffers */
	for (i = 0; i < new_frags - 1; i++) {
		__skb_fill_page_desc(skb, i, head, 0, PAGE_SIZE);
		head = (struct page *)page_private(head);
	}
	__skb_fill_page_desc(skb, new_frags - 1, head, 0, d_off);
	skb_shinfo(skb)->nr_frags = new_frags;

release:
	skb_zcopy_clear(skb, false);
	return 0;
}
EXPORT_SYMBOL_GPL(skb_copy_ubufs);

/**
 *	skb_clone	-	duplicate an sk_buff
 *	@skb: buffer to clone
 *	@gfp_mask: allocation priority
 *
 *	Duplicate an &sk_buff. The new one is not owned by a socket. Both
 *	copies share the same packet data but not structure. The new
 *	buffer has a reference count of 1. If the allocation fails the
 *	function returns %NULL otherwise the new buffer is returned.
 *
 *	If this function is called from an interrupt gfp_mask() must be
 *	%GFP_ATOMIC.
 */

struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t gfp_mask)
{
	struct sk_buff_fclones *fclones = container_of(skb,
						       struct sk_buff_fclones,
						       skb1);
	struct sk_buff *n;

	if (skb_orphan_frags(skb, gfp_mask))
		return NULL;

	if (skb->fclone == SKB_FCLONE_ORIG &&
	    refcount_read(&fclones->fclone_ref) == 1) {
		n = &fclones->skb2;
		refcount_set(&fclones->fclone_ref, 2);
	} else {
		if (skb_pfmemalloc(skb))
			gfp_mask |= __GFP_MEMALLOC;

		n = kmem_cache_alloc(skbuff_head_cache, gfp_mask);
		if (!n)
			return NULL;

		n->fclone = SKB_FCLONE_UNAVAILABLE;
	}

	return __skb_clone(n, skb);
}
EXPORT_SYMBOL(skb_clone);

void skb_headers_offset_update(struct sk_buff *skb, int off)
{
	/* Only adjust this if it actually is csum_start rather than csum */
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		skb->csum_start += off;
	/* {transport,network,mac}_header and tail are relative to skb->head */
	skb->transport_header += off;
	skb->network_header   += off;
	if (skb_mac_header_was_set(skb))
		skb->mac_header += off;
	skb->inner_transport_header += off;
	skb->inner_network_header += off;
	skb->inner_mac_header += off;
}
EXPORT_SYMBOL(skb_headers_offset_update);

void skb_copy_header(struct sk_buff *new, const struct sk_buff *old)
{
	__copy_skb_header(new, old);

	skb_shinfo(new)->gso_size = skb_shinfo(old)->gso_size;
	skb_shinfo(new)->gso_segs = skb_shinfo(old)->gso_segs;
	skb_shinfo(new)->gso_type = skb_shinfo(old)->gso_type;
}
EXPORT_SYMBOL(skb_copy_header);

static inline int skb_alloc_rx_flag(const struct sk_buff *skb)
{
	if (skb_pfmemalloc(skb))
		return SKB_ALLOC_RX;
	return 0;
}

/**
 *	skb_copy	-	create private copy of an sk_buff
 *	@skb: buffer to copy
 *	@gfp_mask: allocation priority
 *
 *	Make a copy of both an &sk_buff and its data. This is used when the
 *	caller wishes to modify the data and needs a private copy of the
 *	data to alter. Returns %NULL on failure or the pointer to the buffer
 *	on success. The returned buffer has a reference count of 1.
 *
 *	As by-product this function converts non-linear &sk_buff to linear
 *	one, so that &sk_buff becomes completely private and caller is allowed
 *	to modify all the data of returned buffer. This means that this
 *	function is not recommended for use in circumstances when only
 *	header is going to be modified. Use pskb_copy() instead.
 */

struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t gfp_mask)
{
	int headerlen = skb_headroom(skb);
	unsigned int size = skb_end_offset(skb) + skb->data_len;
	struct sk_buff *n = __alloc_skb(size, gfp_mask,
					skb_alloc_rx_flag(skb), NUMA_NO_NODE);

	if (!n)
		return NULL;

	/* Set the data pointer */
	skb_reserve(n, headerlen);
	/* Set the tail pointer and length */
	skb_put(n, skb->len);

	BUG_ON(skb_copy_bits(skb, -headerlen, n->head, headerlen + skb->len));

	skb_copy_header(n, skb);
	return n;
}
EXPORT_SYMBOL(skb_copy);

/**
 *	__pskb_copy_fclone	-  create copy of an sk_buff with private head.
 *	@skb: buffer to copy
 *	@headroom: headroom of new skb
 *	@gfp_mask: allocation priority
 *	@fclone: if true allocate the copy of the skb from the fclone
 *	cache instead of the head cache; it is recommended to set this
 *	to true for the cases where the copy will likely be cloned
 *
 *	Make a copy of both an &sk_buff and part of its data, located
 *	in header. Fragmented data remain shared. This is used when
 *	the caller wishes to modify only header of &sk_buff and needs
 *	private copy of the header to alter. Returns %NULL on failure
 *	or the pointer to the buffer on success.
 *	The returned buffer has a reference count of 1.
 */

struct sk_buff *__pskb_copy_fclone(struct sk_buff *skb, int headroom,
				   gfp_t gfp_mask, bool fclone)
{
	unsigned int size = skb_headlen(skb) + headroom;
	int flags = skb_alloc_rx_flag(skb) | (fclone ? SKB_ALLOC_FCLONE : 0);
	struct sk_buff *n = __alloc_skb(size, gfp_mask, flags, NUMA_NO_NODE);

	if (!n)
		goto out;

	/* Set the data pointer */
	skb_reserve(n, headroom);
	/* Set the tail pointer and length */
	skb_put(n, skb_headlen(skb));
	/* Copy the bytes */
	skb_copy_from_linear_data(skb, n->data, n->len);

	n->truesize += skb->data_len;
	n->data_len  = skb->data_len;
	n->len	     = skb->len;

	if (skb_shinfo(skb)->nr_frags) {
		int i;

		if (skb_orphan_frags(skb, gfp_mask) ||
		    skb_zerocopy_clone(n, skb, gfp_mask)) {
			kfree_skb(n);
			n = NULL;
			goto out;
		}
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_shinfo(n)->frags[i] = skb_shinfo(skb)->frags[i];
			skb_frag_ref(skb, i);
		}
		skb_shinfo(n)->nr_frags = i;
	}

	if (skb_has_frag_list(skb)) {
		skb_shinfo(n)->frag_list = skb_shinfo(skb)->frag_list;
		skb_clone_fraglist(n);
	}

	skb_copy_header(n, skb);
out:
	return n;
}
EXPORT_SYMBOL(__pskb_copy_fclone);

/**
 *	pskb_expand_head - reallocate header of &sk_buff
 *	@skb: buffer to reallocate
 *	@nhead: room to add at head
 *	@ntail: room to add at tail
 *	@gfp_mask: allocation priority
 *
 *	Expands (or creates identical copy, if @nhead and @ntail are zero)
 *	header of @skb. &sk_buff itself is not changed. &sk_buff MUST have
 *	reference count of 1. Returns zero in the case of success or error,
 *	if expansion failed. In the last case, &sk_buff is not changed.
 *
 *	All the pointers pointing into skb header may change and must be
 *	reloaded after call to this function.
 */

int pskb_expand_head(struct sk_buff *skb, int nhead, int ntail,
		     gfp_t gfp_mask)
{
	int i, osize = skb_end_offset(skb);
	int size = osize + nhead + ntail;
	long off;
	u8 *data;

	BUG_ON(nhead < 0);

	BUG_ON(skb_shared(skb));

	size = SKB_DATA_ALIGN(size);

	if (skb_pfmemalloc(skb))
		gfp_mask |= __GFP_MEMALLOC;
	data = kmalloc_reserve(size + SKB_DATA_ALIGN(sizeof(struct skb_shared_info)),
			       gfp_mask, NUMA_NO_NODE, NULL);
	if (!data)
		goto nodata;
	size = SKB_WITH_OVERHEAD(ksize(data));

	/* Copy only real data... and, alas, header. This should be
	 * optimized for the cases when header is void.
	 */
	memcpy(data + nhead, skb->head, skb_tail_pointer(skb) - skb->head);

	memcpy((struct skb_shared_info *)(data + size),
	       skb_shinfo(skb),
	       offsetof(struct skb_shared_info, frags[skb_shinfo(skb)->nr_frags]));

	/*
	 * if shinfo is shared we must drop the old head gracefully, but if it
	 * is not we can just drop the old head and let the existing refcount
	 * be since all we did is relocate the values
	 */
	if (skb_cloned(skb)) {
		if (skb_orphan_frags(skb, gfp_mask))
			goto nofrags;
		if (skb_zcopy(skb))
			refcount_inc(&skb_uarg(skb)->refcnt);
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
			skb_frag_ref(skb, i);

		if (skb_has_frag_list(skb))
			skb_clone_fraglist(skb);

		skb_release_data(skb);
	} else {
		skb_free_head(skb);
	}
	off = (data + nhead) - skb->head;

	skb->head     = data;
	skb->head_frag = 0;
	skb->data    += off;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
	skb->end      = size;
	off           = nhead;
#else
	skb->end      = skb->head + size;
#endif
	skb->tail	      += off;
	skb_headers_offset_update(skb, nhead);
	skb->cloned   = 0;
	skb->hdr_len  = 0;
	skb->nohdr    = 0;
	atomic_set(&skb_shinfo(skb)->dataref, 1);

	skb_metadata_clear(skb);

	/* It is not generally safe to change skb->truesize.
	 * For the moment, we really care of rx path, or
	 * when skb is orphaned (not attached to a socket).
	 */
	if (!skb->sk || skb->destructor == sock_edemux)
		skb->truesize += size - osize;

	return 0;

nofrags:
	kfree(data);
nodata:
	return -ENOMEM;
}
EXPORT_SYMBOL(pskb_expand_head);

/* Make private copy of skb with writable head and some headroom */

struct sk_buff *skb_realloc_headroom(struct sk_buff *skb, unsigned int headroom)
{
	struct sk_buff *skb2;
	int delta = headroom - skb_headroom(skb);

	if (delta <= 0)
		skb2 = pskb_copy(skb, GFP_ATOMIC);
	else {
		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2 && pskb_expand_head(skb2, SKB_DATA_ALIGN(delta), 0,
					     GFP_ATOMIC)) {
			kfree_skb(skb2);
			skb2 = NULL;
		}
	}
	return skb2;
}
EXPORT_SYMBOL(skb_realloc_headroom);

/**
 *	skb_copy_expand	-	copy and expand sk_buff
 *	@skb: buffer to copy
 *	@newheadroom: new free bytes at head
 *	@newtailroom: new free bytes at tail
 *	@gfp_mask: allocation priority
 *
 *	Make a copy of both an &sk_buff and its data and while doing so
 *	allocate additional space.
 *
 *	This is used when the caller wishes to modify the data and needs a
 *	private copy of the data to alter as well as more space for new fields.
 *	Returns %NULL on failure or the pointer to the buffer
 *	on success. The returned buffer has a reference count of 1.
 *
 *	You must pass %GFP_ATOMIC as the allocation priority if this function
 *	is called from an interrupt.
 */
struct sk_buff *skb_copy_expand(const struct sk_buff *skb,
				int newheadroom, int newtailroom,
				gfp_t gfp_mask)
{
	/*
	 *	Allocate the copy buffer
	 */
	struct sk_buff *n = __alloc_skb(newheadroom + skb->len + newtailroom,
					gfp_mask, skb_alloc_rx_flag(skb),
					NUMA_NO_NODE);
	int oldheadroom = skb_headroom(skb);
	int head_copy_len, head_copy_off;

	if (!n)
		return NULL;

	skb_reserve(n, newheadroom);

	/* Set the tail pointer and length */
	skb_put(n, skb->len);

	head_copy_len = oldheadroom;
	head_copy_off = 0;
	if (newheadroom <= head_copy_len)
		head_copy_len = newheadroom;
	else
		head_copy_off = newheadroom - head_copy_len;

	/* Copy the linear header and data. */
	BUG_ON(skb_copy_bits(skb, -head_copy_len, n->head + head_copy_off,
			     skb->len + head_copy_len));

	skb_copy_header(n, skb);

	skb_headers_offset_update(n, newheadroom - oldheadroom);

	return n;
}
EXPORT_SYMBOL(skb_copy_expand);

/**
 *	__skb_pad		-	zero pad the tail of an skb
 *	@skb: buffer to pad
 *	@pad: space to pad
 *	@free_on_error: free buffer on error
 *
 *	Ensure that a buffer is followed by a padding area that is zero
 *	filled. Used by network drivers which may DMA or transfer data
 *	beyond the buffer end onto the wire.
 *
 *	May return error in out of memory cases. The skb is freed on error
 *	if @free_on_error is true.
 */

int __skb_pad(struct sk_buff *skb, int pad, bool free_on_error)
{
	int err;
	int ntail;

	/* If the skbuff is non linear tailroom is always zero.. */
	if (!skb_cloned(skb) && skb_tailroom(skb) >= pad) {
		memset(skb->data+skb->len, 0, pad);
		return 0;
	}

	ntail = skb->data_len + pad - (skb->end - skb->tail);
	if (likely(skb_cloned(skb) || ntail > 0)) {
		err = pskb_expand_head(skb, 0, ntail, GFP_ATOMIC);
		if (unlikely(err))
			goto free_skb;
	}

	/* FIXME: The use of this function with non-linear skb's really needs
	 * to be audited.
	 */
	err = skb_linearize(skb);
	if (unlikely(err))
		goto free_skb;

	memset(skb->data + skb->len, 0, pad);
	return 0;

free_skb:
	if (free_on_error)
		kfree_skb(skb);
	return err;
}
EXPORT_SYMBOL(__skb_pad);

/**
 *	pskb_put - add data to the tail of a potentially fragmented buffer
 *	@skb: start of the buffer to use
 *	@tail: tail fragment of the buffer to use
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the potentially
 *	fragmented buffer. @tail must be the last fragment of @skb -- or
 *	@skb itself. If this would exceed the total buffer size the kernel
 *	will panic. A pointer to the first byte of the extra data is
 *	returned.
 */

void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len)
{
	if (tail != skb) {
		skb->data_len += len;
		skb->len += len;
	}
	return skb_put(tail, len);
}
EXPORT_SYMBOL_GPL(pskb_put);

/**
 *	skb_put - add data to a buffer
 *	@skb: buffer to use
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the buffer. If this would
 *	exceed the total buffer size the kernel will panic. A pointer to the
 *	first byte of the extra data is returned.
 */
void *skb_put(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb_tail_pointer(skb);
	SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;
	if (unlikely(skb->tail > skb->end))
		skb_over_panic(skb, len, __builtin_return_address(0));
	return tmp;
}
EXPORT_SYMBOL(skb_put);

/**
 *	skb_push - add data to the start of a buffer
 *	@skb: buffer to use
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the buffer at the buffer
 *	start. If this would exceed the total buffer headroom the kernel will
 *	panic. A pointer to the first byte of the extra data is returned.
 */
void *skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len  += len;
	if (unlikely(skb->data < skb->head))
		skb_under_panic(skb, len, __builtin_return_address(0));
	return skb->data;
}
EXPORT_SYMBOL(skb_push);

/**
 *	skb_pull - remove data from the start of a buffer
 *	@skb: buffer to use
 *	@len: amount of data to remove
 *
 *	This function removes data from the start of a buffer, returning
 *	the memory to the headroom. A pointer to the next data in the buffer
 *	is returned. Once the data has been pulled future pushes will overwrite
 *	the old data.
 */
void *skb_pull(struct sk_buff *skb, unsigned int len)
{
	return skb_pull_inline(skb, len);
}
EXPORT_SYMBOL(skb_pull);

/**
 *	skb_trim - remove end from a buffer
 *	@skb: buffer to alter
 *	@len: new length
 *
 *	Cut the length of a buffer down by removing data from the tail. If
 *	the buffer is already under the length specified it is not modified.
 *	The skb must be linear.
 */
void skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len)
		__skb_trim(skb, len);
}
EXPORT_SYMBOL(skb_trim);

/* Trims skb to length len. It can change skb pointers.
 */

int ___pskb_trim(struct sk_buff *skb, unsigned int len)
{
	struct sk_buff **fragp;
	struct sk_buff *frag;
	int offset = skb_headlen(skb);
	int nfrags = skb_shinfo(skb)->nr_frags;
	int i;
	int err;

	if (skb_cloned(skb) &&
	    unlikely((err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC))))
		return err;

	i = 0;
	if (offset >= len)
		goto drop_pages;

	for (; i < nfrags; i++) {
		int end = offset + skb_frag_size(&skb_shinfo(skb)->frags[i]);

		if (end < len) {
			offset = end;
			continue;
		}

		skb_frag_size_set(&skb_shinfo(skb)->frags[i++], len - offset);

drop_pages:
		skb_shinfo(skb)->nr_frags = i;

		for (; i < nfrags; i++)
			skb_frag_unref(skb, i);

		if (skb_has_frag_list(skb))
			skb_drop_fraglist(skb);
		goto done;
	}

	for (fragp = &skb_shinfo(skb)->frag_list; (frag = *fragp);
	     fragp = &frag->next) {
		int end = offset + frag->len;

		if (skb_shared(frag)) {
			struct sk_buff *nfrag;

			nfrag = skb_clone(frag, GFP_ATOMIC);
			if (unlikely(!nfrag))
				return -ENOMEM;

			nfrag->next = frag->next;
			consume_skb(frag);
			frag = nfrag;
			*fragp = frag;
		}

		if (end < len) {
			offset = end;
			continue;
		}

		if (end > len &&
		    unlikely((err = pskb_trim(frag, len - offset))))
			return err;

		if (frag->next)
			skb_drop_list(&frag->next);
		break;
	}

done:
	if (len > skb_headlen(skb)) {
		skb->data_len -= skb->len - len;
		skb->len       = len;
	} else {
		skb->len       = len;
		skb->data_len  = 0;
		skb_set_tail_pointer(skb, len);
	}

	if (!skb->sk || skb->destructor == sock_edemux)
		skb_condense(skb);
	return 0;
}
EXPORT_SYMBOL(___pskb_trim);

/* Note : use pskb_trim_rcsum() instead of calling this directly
 */
int pskb_trim_rcsum_slow(struct sk_buff *skb, unsigned int len)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		int delta = skb->len - len;

		skb->csum = csum_block_sub(skb->csum,
					   skb_checksum(skb, len, delta, 0),
					   len);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		int hdlen = (len > skb_headlen(skb)) ? skb_headlen(skb) : len;
		int offset = skb_checksum_start_offset(skb) + skb->csum_offset;

		if (offset + sizeof(__sum16) > hdlen)
			return -EINVAL;
	}
	return __pskb_trim(skb, len);
}
EXPORT_SYMBOL(pskb_trim_rcsum_slow);

/**
 *	__pskb_pull_tail - advance tail of skb header
 *	@skb: buffer to reallocate
 *	@delta: number of bytes to advance tail
 *
 *	The function makes a sense only on a fragmented &sk_buff,
 *	it expands header moving its tail forward and copying necessary
 *	data from fragmented part.
 *
 *	&sk_buff MUST have reference count of 1.
 *
 *	Returns %NULL (and &sk_buff does not change) if pull failed
 *	or value of new tail of skb in the case of success.
 *
 *	All the pointers pointing into skb header may change and must be
 *	reloaded after call to this function.
 */

/* Moves tail of skb head forward, copying data from fragmented part,
 * when it is necessary.
 * 1. It may fail due to malloc failure.
 * 2. It may change skb pointers.
 *
 * It is pretty complicated. Luckily, it is called only in exceptional cases.
 */
void *__pskb_pull_tail(struct sk_buff *skb, int delta)
{
	/* If skb has not enough free space at tail, get new one
	 * plus 128 bytes for future expansions. If we have enough
	 * room at tail, reallocate without expansion only if skb is cloned.
	 */
	int i, k, eat = (skb->tail + delta) - skb->end;

	if (eat > 0 || skb_cloned(skb)) {
		if (pskb_expand_head(skb, 0, eat > 0 ? eat + 128 : 0,
				     GFP_ATOMIC))
			return NULL;
	}

	BUG_ON(skb_copy_bits(skb, skb_headlen(skb),
			     skb_tail_pointer(skb), delta));

	/* Optimization: no fragments, no reasons to preestimate
	 * size of pulled pages. Superb.
	 */
	if (!skb_has_frag_list(skb))
		goto pull_pages;

	/* Estimate size of pulled pages. */
	eat = delta;
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int size = skb_frag_size(&skb_shinfo(skb)->frags[i]);

		if (size >= eat)
			goto pull_pages;
		eat -= size;
	}

	/* If we need update frag list, we are in troubles.
	 * Certainly, it is possible to add an offset to skb data,
	 * but taking into account that pulling is expected to
	 * be very rare operation, it is worth to fight against
	 * further bloating skb head and crucify ourselves here instead.
	 * Pure masohism, indeed. 8)8)
	 */
	if (eat) {
		struct sk_buff *list = skb_shinfo(skb)->frag_list;
		struct sk_buff *clone = NULL;
		struct sk_buff *insp = NULL;

		do {
			if (list->len <= eat) {
				/* Eaten as whole. */
				eat -= list->len;
				list = list->next;
				insp = list;
			} else {
				/* Eaten partially. */

				if (skb_shared(list)) {
					/* Sucks! We need to fork list. :-( */
					clone = skb_clone(list, GFP_ATOMIC);
					if (!clone)
						return NULL;
					insp = list->next;
					list = clone;
				} else {
					/* This may be pulled without
					 * problems. */
					insp = list;
				}
				if (!pskb_pull(list, eat)) {
					kfree_skb(clone);
					return NULL;
				}
				break;
			}
		} while (eat);

		/* Free pulled out fragments. */
		while ((list = skb_shinfo(skb)->frag_list) != insp) {
			skb_shinfo(skb)->frag_list = list->next;
			consume_skb(list);
		}
		/* And insert new clone at head. */
		if (clone) {
			clone->next = list;
			skb_shinfo(skb)->frag_list = clone;
		}
	}
	/* Success! Now we may commit changes to skb data. */

pull_pages:
	eat = delta;
	k = 0;
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int size = skb_frag_size(&skb_shinfo(skb)->frags[i]);

		if (size <= eat) {
			skb_frag_unref(skb, i);
			eat -= size;
		} else {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[k];

			*frag = skb_shinfo(skb)->frags[i];
			if (eat) {
				skb_frag_off_add(frag, eat);
				skb_frag_size_sub(frag, eat);
				if (!i)
					goto end;
				eat = 0;
			}
			k++;
		}
	}
	skb_shinfo(skb)->nr_frags = k;

end:
	skb->tail     += delta;
	skb->data_len -= delta;

	if (!skb->data_len)
		skb_zcopy_clear(skb, false);

	return skb_tail_pointer(skb);
}
EXPORT_SYMBOL(__pskb_pull_tail);

/**
 *	skb_copy_bits - copy bits from skb to kernel buffer
 *	@skb: source skb
 *	@offset: offset in source
 *	@to: destination buffer
 *	@len: number of bytes to copy
 *
 *	Copy the specified number of bytes from the source skb to the
 *	destination buffer.
 *
 *	CAUTION ! :
 *		If its prototype is ever changed,
 *		check arch/{*}/net/{*}.S files,
 *		since it is called from BPF assembly code.
 */
int skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len)
{
	int start = skb_headlen(skb);
	struct sk_buff *frag_iter;
	int i, copy;

	if (offset > (int)skb->len - len)
		goto fault;

	/* Copy header. */
	if ((copy = start - offset) > 0) {
		if (copy > len)
			copy = len;
		skb_copy_from_linear_data_offset(skb, offset, to, copy);
		if ((len -= copy) == 0)
			return 0;
		offset += copy;
		to     += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		skb_frag_t *f = &skb_shinfo(skb)->frags[i];

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(f);
		if ((copy = end - offset) > 0) {
			u32 p_off, p_len, copied;
			struct page *p;
			u8 *vaddr;

			if (copy > len)
				copy = len;

			skb_frag_foreach_page(f,
					      skb_frag_off(f) + offset - start,
					      copy, p, p_off, p_len, copied) {
				vaddr = kmap_atomic(p);
				memcpy(to + copied, vaddr + p_off, p_len);
				kunmap_atomic(vaddr);
			}

			if ((len -= copy) == 0)
				return 0;
			offset += copy;
			to     += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			if (skb_copy_bits(frag_iter, offset - start, to, copy))
				goto fault;
			if ((len -= copy) == 0)
				return 0;
			offset += copy;
			to     += copy;
		}
		start = end;
	}

	if (!len)
		return 0;

fault:
	return -EFAULT;
}
EXPORT_SYMBOL(skb_copy_bits);

/*
 * Callback from splice_to_pipe(), if we need to release some pages
 * at the end of the spd in case we error'ed out in filling the pipe.
 */
static void sock_spd_release(struct splice_pipe_desc *spd, unsigned int i)
{
	put_page(spd->pages[i]);
}

static struct page *linear_to_page(struct page *page, unsigned int *len,
				   unsigned int *offset,
				   struct sock *sk)
{
	struct page_frag *pfrag = sk_page_frag(sk);

	if (!sk_page_frag_refill(sk, pfrag))
		return NULL;

	*len = min_t(unsigned int, *len, pfrag->size - pfrag->offset);

	memcpy(page_address(pfrag->page) + pfrag->offset,
	       page_address(page) + *offset, *len);
	*offset = pfrag->offset;
	pfrag->offset += *len;

	return pfrag->page;
}

static bool spd_can_coalesce(const struct splice_pipe_desc *spd,
			     struct page *page,
			     unsigned int offset)
{
	return	spd->nr_pages &&
		spd->pages[spd->nr_pages - 1] == page &&
		(spd->partial[spd->nr_pages - 1].offset +
		 spd->partial[spd->nr_pages - 1].len == offset);
}

/*
 * Fill page/offset/length into spd, if it can hold more pages.
 */
static bool spd_fill_page(struct splice_pipe_desc *spd,
			  struct pipe_inode_info *pipe, struct page *page,
			  unsigned int *len, unsigned int offset,
			  bool linear,
			  struct sock *sk)
{
	if (unlikely(spd->nr_pages == MAX_SKB_FRAGS))
		return true;

	if (linear) {
		page = linear_to_page(page, len, &offset, sk);
		if (!page)
			return true;
	}
	if (spd_can_coalesce(spd, page, offset)) {
		spd->partial[spd->nr_pages - 1].len += *len;
		return false;
	}
	get_page(page);
	spd->pages[spd->nr_pages] = page;
	spd->partial[spd->nr_pages].len = *len;
	spd->partial[spd->nr_pages].offset = offset;
	spd->nr_pages++;

	return false;
}

static bool __splice_segment(struct page *page, unsigned int poff,
			     unsigned int plen, unsigned int *off,
			     unsigned int *len,
			     struct splice_pipe_desc *spd, bool linear,
			     struct sock *sk,
			     struct pipe_inode_info *pipe)
{
	if (!*len)
		return true;

	/* skip this segment if already processed */
	if (*off >= plen) {
		*off -= plen;
		return false;
	}

	/* ignore any bits we already processed */
	poff += *off;
	plen -= *off;
	*off = 0;

	do {
		unsigned int flen = min(*len, plen);

		if (spd_fill_page(spd, pipe, page, &flen, poff,
				  linear, sk))
			return true;
		poff += flen;
		plen -= flen;
		*len -= flen;
	} while (*len && plen);

	return false;
}

/*
 * Map linear and fragment data from the skb to spd. It reports true if the
 * pipe is full or if we already spliced the requested length.
 */
static bool __skb_splice_bits(struct sk_buff *skb, struct pipe_inode_info *pipe,
			      unsigned int *offset, unsigned int *len,
			      struct splice_pipe_desc *spd, struct sock *sk)
{
	int seg;
	struct sk_buff *iter;

	/* map the linear part :
	 * If skb->head_frag is set, this 'linear' part is backed by a
	 * fragment, and if the head is not shared with any clones then
	 * we can avoid a copy since we own the head portion of this page.
	 */
	if (__splice_segment(virt_to_page(skb->data),
			     (unsigned long) skb->data & (PAGE_SIZE - 1),
			     skb_headlen(skb),
			     offset, len, spd,
			     skb_head_is_locked(skb),
			     sk, pipe))
		return true;

	/*
	 * then map the fragments
	 */
	for (seg = 0; seg < skb_shinfo(skb)->nr_frags; seg++) {
		const skb_frag_t *f = &skb_shinfo(skb)->frags[seg];

		if (__splice_segment(skb_frag_page(f),
				     skb_frag_off(f), skb_frag_size(f),
				     offset, len, spd, false, sk, pipe))
			return true;
	}

	skb_walk_frags(skb, iter) {
		if (*offset >= iter->len) {
			*offset -= iter->len;
			continue;
		}
		/* __skb_splice_bits() only fails if the output has no room
		 * left, so no point in going over the frag_list for the error
		 * case.
		 */
		if (__skb_splice_bits(iter, pipe, offset, len, spd, sk))
			return true;
	}

	return false;
}

/*
 * Map data from the skb to a pipe. Should handle both the linear part,
 * the fragments, and the frag list.
 */
int skb_splice_bits(struct sk_buff *skb, struct sock *sk, unsigned int offset,
		    struct pipe_inode_info *pipe, unsigned int tlen,
		    unsigned int flags)
{
	struct partial_page partial[MAX_SKB_FRAGS];
	struct page *pages[MAX_SKB_FRAGS];
	struct splice_pipe_desc spd = {
		.pages = pages,
		.partial = partial,
		.nr_pages_max = MAX_SKB_FRAGS,
		.ops = &nosteal_pipe_buf_ops,
		.spd_release = sock_spd_release,
	};
	int ret = 0;

	__skb_splice_bits(skb, pipe, &offset, &tlen, &spd, sk);

	if (spd.nr_pages)
		ret = splice_to_pipe(pipe, &spd);

	return ret;
}
EXPORT_SYMBOL_GPL(skb_splice_bits);

/* Send skb data on a socket. Socket must be locked. */
int skb_send_sock_locked(struct sock *sk, struct sk_buff *skb, int offset,
			 int len)
{
	unsigned int orig_len = len;
	struct sk_buff *head = skb;
	unsigned short fragidx;
	int slen, ret;

do_frag_list:

	/* Deal with head data */
	while (offset < skb_headlen(skb) && len) {
		struct kvec kv;
		struct msghdr msg;

		slen = min_t(int, len, skb_headlen(skb) - offset);
		kv.iov_base = skb->data + offset;
		kv.iov_len = slen;
		memset(&msg, 0, sizeof(msg));
		msg.msg_flags = MSG_DONTWAIT;

		ret = kernel_sendmsg_locked(sk, &msg, &kv, 1, slen);
		if (ret <= 0)
			goto error;

		offset += ret;
		len -= ret;
	}

	/* All the data was skb head? */
	if (!len)
		goto out;

	/* Make offset relative to start of frags */
	offset -= skb_headlen(skb);

	/* Find where we are in frag list */
	for (fragidx = 0; fragidx < skb_shinfo(skb)->nr_frags; fragidx++) {
		skb_frag_t *frag  = &skb_shinfo(skb)->frags[fragidx];

		if (offset < skb_frag_size(frag))
			break;

		offset -= skb_frag_size(frag);
	}

	for (; len && fragidx < skb_shinfo(skb)->nr_frags; fragidx++) {
		skb_frag_t *frag  = &skb_shinfo(skb)->frags[fragidx];

		slen = min_t(size_t, len, skb_frag_size(frag) - offset);

		while (slen) {
			ret = kernel_sendpage_locked(sk, skb_frag_page(frag),
						     skb_frag_off(frag) + offset,
						     slen, MSG_DONTWAIT);
			if (ret <= 0)
				goto error;

			len -= ret;
			offset += ret;
			slen -= ret;
		}

		offset = 0;
	}

	if (len) {
		/* Process any frag lists */

		if (skb == head) {
			if (skb_has_frag_list(skb)) {
				skb = skb_shinfo(skb)->frag_list;
				goto do_frag_list;
			}
		} else if (skb->next) {
			skb = skb->next;
			goto do_frag_list;
		}
	}

out:
	return orig_len - len;

error:
	return orig_len == len ? ret : orig_len - len;
}
EXPORT_SYMBOL_GPL(skb_send_sock_locked);

/**
 *	skb_store_bits - store bits from kernel buffer to skb
 *	@skb: destination buffer
 *	@offset: offset in destination
 *	@from: source buffer
 *	@len: number of bytes to copy
 *
 *	Copy the specified number of bytes from the source buffer to the
 *	destination skb.  This function handles all the messy bits of
 *	traversing fragment lists and such.
 */

int skb_store_bits(struct sk_buff *skb, int offset, const void *from, int len)
{
	int start = skb_headlen(skb);
	struct sk_buff *frag_iter;
	int i, copy;

	if (offset > (int)skb->len - len)
		goto fault;

	if ((copy = start - offset) > 0) {
		if (copy > len)
			copy = len;
		skb_copy_to_linear_data_offset(skb, offset, from, copy);
		if ((len -= copy) == 0)
			return 0;
		offset += copy;
		from += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int end;

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(frag);
		if ((copy = end - offset) > 0) {
			u32 p_off, p_len, copied;
			struct page *p;
			u8 *vaddr;

			if (copy > len)
				copy = len;

			skb_frag_foreach_page(frag,
					      skb_frag_off(frag) + offset - start,
					      copy, p, p_off, p_len, copied) {
				vaddr = kmap_atomic(p);
				memcpy(vaddr + p_off, from + copied, p_len);
				kunmap_atomic(vaddr);
			}

			if ((len -= copy) == 0)
				return 0;
			offset += copy;
			from += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			if (skb_store_bits(frag_iter, offset - start,
					   from, copy))
				goto fault;
			if ((len -= copy) == 0)
				return 0;
			offset += copy;
			from += copy;
		}
		start = end;
	}
	if (!len)
		return 0;

fault:
	return -EFAULT;
}
EXPORT_SYMBOL(skb_store_bits);

/* Checksum skb data. */
__wsum __skb_checksum(const struct sk_buff *skb, int offset, int len,
		      __wsum csum, const struct skb_checksum_ops *ops)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;
	int pos = 0;

	/* Checksum header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		csum = INDIRECT_CALL_1(ops->update, csum_partial_ext,
				       skb->data + offset, copy, csum);
		if ((len -= copy) == 0)
			return csum;
		offset += copy;
		pos	= copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(frag);
		if ((copy = end - offset) > 0) {
			u32 p_off, p_len, copied;
			struct page *p;
			__wsum csum2;
			u8 *vaddr;

			if (copy > len)
				copy = len;

			skb_frag_foreach_page(frag,
					      skb_frag_off(frag) + offset - start,
					      copy, p, p_off, p_len, copied) {
				vaddr = kmap_atomic(p);
				csum2 = INDIRECT_CALL_1(ops->update,
							csum_partial_ext,
							vaddr + p_off, p_len, 0);
				kunmap_atomic(vaddr);
				csum = INDIRECT_CALL_1(ops->combine,
						       csum_block_add_ext, csum,
						       csum2, pos, p_len);
				pos += p_len;
			}

			if (!(len -= copy))
				return csum;
			offset += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			__wsum csum2;
			if (copy > len)
				copy = len;
			csum2 = __skb_checksum(frag_iter, offset - start,
					       copy, 0, ops);
			csum = INDIRECT_CALL_1(ops->combine, csum_block_add_ext,
					       csum, csum2, pos, copy);
			if ((len -= copy) == 0)
				return csum;
			offset += copy;
			pos    += copy;
		}
		start = end;
	}
	BUG_ON(len);

	return csum;
}
EXPORT_SYMBOL(__skb_checksum);

__wsum skb_checksum(const struct sk_buff *skb, int offset,
		    int len, __wsum csum)
{
	const struct skb_checksum_ops ops = {
		.update  = csum_partial_ext,
		.combine = csum_block_add_ext,
	};

	return __skb_checksum(skb, offset, len, csum, &ops);
}
EXPORT_SYMBOL(skb_checksum);

/* Both of above in one bottle. */

__wsum skb_copy_and_csum_bits(const struct sk_buff *skb, int offset,
				    u8 *to, int len)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;
	int pos = 0;
	__wsum csum = 0;

	/* Copy header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		csum = csum_partial_copy_nocheck(skb->data + offset, to,
						 copy);
		if ((len -= copy) == 0)
			return csum;
		offset += copy;
		to     += copy;
		pos	= copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(&skb_shinfo(skb)->frags[i]);
		if ((copy = end - offset) > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			u32 p_off, p_len, copied;
			struct page *p;
			__wsum csum2;
			u8 *vaddr;

			if (copy > len)
				copy = len;

			skb_frag_foreach_page(frag,
					      skb_frag_off(frag) + offset - start,
					      copy, p, p_off, p_len, copied) {
				vaddr = kmap_atomic(p);
				csum2 = csum_partial_copy_nocheck(vaddr + p_off,
								  to + copied,
								  p_len);
				kunmap_atomic(vaddr);
				csum = csum_block_add(csum, csum2, pos);
				pos += p_len;
			}

			if (!(len -= copy))
				return csum;
			offset += copy;
			to     += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		__wsum csum2;
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			csum2 = skb_copy_and_csum_bits(frag_iter,
						       offset - start,
						       to, copy);
			csum = csum_block_add(csum, csum2, pos);
			if ((len -= copy) == 0)
				return csum;
			offset += copy;
			to     += copy;
			pos    += copy;
		}
		start = end;
	}
	BUG_ON(len);
	return csum;
}
EXPORT_SYMBOL(skb_copy_and_csum_bits);

__sum16 __skb_checksum_complete_head(struct sk_buff *skb, int len)
{
	__sum16 sum;

	sum = csum_fold(skb_checksum(skb, 0, len, skb->csum));
	/* See comments in __skb_checksum_complete(). */
	if (likely(!sum)) {
		if (unlikely(skb->ip_summed == CHECKSUM_COMPLETE) &&
		    !skb->csum_complete_sw)
			netdev_rx_csum_fault(skb->dev, skb);
	}
	if (!skb_shared(skb))
		skb->csum_valid = !sum;
	return sum;
}
EXPORT_SYMBOL(__skb_checksum_complete_head);

/* This function assumes skb->csum already holds pseudo header's checksum,
 * which has been changed from the hardware checksum, for example, by
 * __skb_checksum_validate_complete(). And, the original skb->csum must
 * have been validated unsuccessfully for CHECKSUM_COMPLETE case.
 *
 * It returns non-zero if the recomputed checksum is still invalid, otherwise
 * zero. The new checksum is stored back into skb->csum unless the skb is
 * shared.
 */
__sum16 __skb_checksum_complete(struct sk_buff *skb)
{
	__wsum csum;
	__sum16 sum;

	csum = skb_checksum(skb, 0, skb->len, 0);

	sum = csum_fold(csum_add(skb->csum, csum));
	/* This check is inverted, because we already knew the hardware
	 * checksum is invalid before calling this function. So, if the
	 * re-computed checksum is valid instead, then we have a mismatch
	 * between the original skb->csum and skb_checksum(). This means either
	 * the original hardware checksum is incorrect or we screw up skb->csum
	 * when moving skb->data around.
	 */
	if (likely(!sum)) {
		if (unlikely(skb->ip_summed == CHECKSUM_COMPLETE) &&
		    !skb->csum_complete_sw)
			netdev_rx_csum_fault(skb->dev, skb);
	}

	if (!skb_shared(skb)) {
		/* Save full packet checksum */
		skb->csum = csum;
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum_complete_sw = 1;
		skb->csum_valid = !sum;
	}

	return sum;
}
EXPORT_SYMBOL(__skb_checksum_complete);

static __wsum warn_crc32c_csum_update(const void *buff, int len, __wsum sum)
{
	net_warn_ratelimited(
		"%s: attempt to compute crc32c without libcrc32c.ko\n",
		__func__);
	return 0;
}

static __wsum warn_crc32c_csum_combine(__wsum csum, __wsum csum2,
				       int offset, int len)
{
	net_warn_ratelimited(
		"%s: attempt to compute crc32c without libcrc32c.ko\n",
		__func__);
	return 0;
}

static const struct skb_checksum_ops default_crc32c_ops = {
	.update  = warn_crc32c_csum_update,
	.combine = warn_crc32c_csum_combine,
};

const struct skb_checksum_ops *crc32c_csum_stub __read_mostly =
	&default_crc32c_ops;
EXPORT_SYMBOL(crc32c_csum_stub);

 /**
 *	skb_zerocopy_headlen - Calculate headroom needed for skb_zerocopy()
 *	@from: source buffer
 *
 *	Calculates the amount of linear headroom needed in the 'to' skb passed
 *	into skb_zerocopy().
 */
unsigned int
skb_zerocopy_headlen(const struct sk_buff *from)
{
	unsigned int hlen = 0;

	if (!from->head_frag ||
	    skb_headlen(from) < L1_CACHE_BYTES ||
	    skb_shinfo(from)->nr_frags >= MAX_SKB_FRAGS) {
		hlen = skb_headlen(from);
		if (!hlen)
			hlen = from->len;
	}

	if (skb_has_frag_list(from))
		hlen = from->len;

	return hlen;
}
EXPORT_SYMBOL_GPL(skb_zerocopy_headlen);

/**
 *	skb_zerocopy - Zero copy skb to skb
 *	@to: destination buffer
 *	@from: source buffer
 *	@len: number of bytes to copy from source buffer
 *	@hlen: size of linear headroom in destination buffer
 *
 *	Copies up to `len` bytes from `from` to `to` by creating references
 *	to the frags in the source buffer.
 *
 *	The `hlen` as calculated by skb_zerocopy_headlen() specifies the
 *	headroom in the `to` buffer.
 *
 *	Return value:
 *	0: everything is OK
 *	-ENOMEM: couldn't orphan frags of @from due to lack of memory
 *	-EFAULT: skb_copy_bits() found some problem with skb geometry
 */
int
skb_zerocopy(struct sk_buff *to, struct sk_buff *from, int len, int hlen)
{
	int i, j = 0;
	int plen = 0; /* length of skb->head fragment */
	int ret;
	struct page *page;
	unsigned int offset;

	BUG_ON(!from->head_frag && !hlen);

	/* dont bother with small payloads */
	if (len <= skb_tailroom(to))
		return skb_copy_bits(from, 0, skb_put(to, len), len);

	if (hlen) {
		ret = skb_copy_bits(from, 0, skb_put(to, hlen), hlen);
		if (unlikely(ret))
			return ret;
		len -= hlen;
	} else {
		plen = min_t(int, skb_headlen(from), len);
		if (plen) {
			page = virt_to_head_page(from->head);
			offset = from->data - (unsigned char *)page_address(page);
			__skb_fill_page_desc(to, 0, page, offset, plen);
			get_page(page);
			j = 1;
			len -= plen;
		}
	}

	to->truesize += len + plen;
	to->len += len + plen;
	to->data_len += len + plen;

	if (unlikely(skb_orphan_frags(from, GFP_ATOMIC))) {
		skb_tx_error(from);
		return -ENOMEM;
	}
	skb_zerocopy_clone(to, from, GFP_ATOMIC);

	for (i = 0; i < skb_shinfo(from)->nr_frags; i++) {
		int size;

		if (!len)
			break;
		skb_shinfo(to)->frags[j] = skb_shinfo(from)->frags[i];
		size = min_t(int, skb_frag_size(&skb_shinfo(to)->frags[j]),
					len);
		skb_frag_size_set(&skb_shinfo(to)->frags[j], size);
		len -= size;
		skb_frag_ref(to, j);
		j++;
	}
	skb_shinfo(to)->nr_frags = j;

	return 0;
}
EXPORT_SYMBOL_GPL(skb_zerocopy);

void skb_copy_and_csum_dev(const struct sk_buff *skb, u8 *to)
{
	__wsum csum;
	long csstart;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		csstart = skb_checksum_start_offset(skb);
	else
		csstart = skb_headlen(skb);

	BUG_ON(csstart > skb_headlen(skb));

	skb_copy_from_linear_data(skb, to, csstart);

	csum = 0;
	if (csstart != skb->len)
		csum = skb_copy_and_csum_bits(skb, csstart, to + csstart,
					      skb->len - csstart);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		long csstuff = csstart + skb->csum_offset;

		*((__sum16 *)(to + csstuff)) = csum_fold(csum);
	}
}
EXPORT_SYMBOL(skb_copy_and_csum_dev);

/**
 *	skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. The list lock is taken so the function
 *	may be used safely with other locking list functions. The head item is
 *	returned or %NULL if the list is empty.
 */

struct sk_buff *skb_dequeue(struct sk_buff_head *list)
{
	unsigned long flags;
	struct sk_buff *result;

	spin_lock_irqsave(&list->lock, flags);
	result = __skb_dequeue(list);
	spin_unlock_irqrestore(&list->lock, flags);
	return result;
}
EXPORT_SYMBOL(skb_dequeue);

/**
 *	skb_dequeue_tail - remove from the tail of the queue
 *	@list: list to dequeue from
 *
 *	Remove the tail of the list. The list lock is taken so the function
 *	may be used safely with other locking list functions. The tail item is
 *	returned or %NULL if the list is empty.
 */
struct sk_buff *skb_dequeue_tail(struct sk_buff_head *list)
{
	unsigned long flags;
	struct sk_buff *result;

	spin_lock_irqsave(&list->lock, flags);
	result = __skb_dequeue_tail(list);
	spin_unlock_irqrestore(&list->lock, flags);
	return result;
}
EXPORT_SYMBOL(skb_dequeue_tail);

/**
 *	skb_queue_purge - empty a list
 *	@list: list to empty
 *
 *	Delete all buffers on an &sk_buff list. Each buffer is removed from
 *	the list and one reference dropped. This function takes the list
 *	lock and is atomic with respect to other list locking functions.
 */
void skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(list)) != NULL)
		kfree_skb(skb);
}
EXPORT_SYMBOL(skb_queue_purge);

/**
 *	skb_rbtree_purge - empty a skb rbtree
 *	@root: root of the rbtree to empty
 *	Return value: the sum of truesizes of all purged skbs.
 *
 *	Delete all buffers on an &sk_buff rbtree. Each buffer is removed from
 *	the list and one reference dropped. This function does not take
 *	any lock. Synchronization should be handled by the caller (e.g., TCP
 *	out-of-order queue is protected by the socket lock).
 */
unsigned int skb_rbtree_purge(struct rb_root *root)
{
	struct rb_node *p = rb_first(root);
	unsigned int sum = 0;

	while (p) {
		struct sk_buff *skb = rb_entry(p, struct sk_buff, rbnode);

		p = rb_next(p);
		rb_erase(&skb->rbnode, root);
		sum += skb->truesize;
		kfree_skb(skb);
	}
	return sum;
}

/**
 *	skb_queue_head - queue a buffer at the list head
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the start of the list. This function takes the
 *	list lock and can be used safely with other locking &sk_buff functions
 *	safely.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
void skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_head(list, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}
EXPORT_SYMBOL(skb_queue_head);

/**
 *	skb_queue_tail - queue a buffer at the list tail
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the tail of the list. This function takes the
 *	list lock and can be used safely with other locking &sk_buff functions
 *	safely.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
void skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_tail(list, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}
EXPORT_SYMBOL(skb_queue_tail);

/**
 *	skb_unlink	-	remove a buffer from a list
 *	@skb: buffer to remove
 *	@list: list to use
 *
 *	Remove a packet from a list. The list locks are taken and this
 *	function is atomic with respect to other list locked calls
 *
 *	You must know what list the SKB is on.
 */
void skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_unlink(skb, list);
	spin_unlock_irqrestore(&list->lock, flags);
}
EXPORT_SYMBOL(skb_unlink);

/**
 *	skb_append	-	append a buffer
 *	@old: buffer to insert after
 *	@newsk: buffer to insert
 *	@list: list to use
 *
 *	Place a packet after a given packet in a list. The list locks are taken
 *	and this function is atomic with respect to other list locked calls.
 *	A buffer cannot be placed on two lists at the same time.
 */
void skb_append(struct sk_buff *old, struct sk_buff *newsk, struct sk_buff_head *list)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_after(list, old, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}
EXPORT_SYMBOL(skb_append);

static inline void skb_split_inside_header(struct sk_buff *skb,
					   struct sk_buff* skb1,
					   const u32 len, const int pos)
{
	int i;

	skb_copy_from_linear_data_offset(skb, len, skb_put(skb1, pos - len),
					 pos - len);
	/* And move data appendix as is. */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
		skb_shinfo(skb1)->frags[i] = skb_shinfo(skb)->frags[i];

	skb_shinfo(skb1)->nr_frags = skb_shinfo(skb)->nr_frags;
	skb_shinfo(skb)->nr_frags  = 0;
	skb1->data_len		   = skb->data_len;
	skb1->len		   += skb1->data_len;
	skb->data_len		   = 0;
	skb->len		   = len;
	skb_set_tail_pointer(skb, len);
}

static inline void skb_split_no_header(struct sk_buff *skb,
				       struct sk_buff* skb1,
				       const u32 len, int pos)
{
	int i, k = 0;
	const int nfrags = skb_shinfo(skb)->nr_frags;

	skb_shinfo(skb)->nr_frags = 0;
	skb1->len		  = skb1->data_len = skb->len - len;
	skb->len		  = len;
	skb->data_len		  = len - pos;

	for (i = 0; i < nfrags; i++) {
		int size = skb_frag_size(&skb_shinfo(skb)->frags[i]);

		if (pos + size > len) {
			skb_shinfo(skb1)->frags[k] = skb_shinfo(skb)->frags[i];

			if (pos < len) {
				/* Split frag.
				 * We have two variants in this case:
				 * 1. Move all the frag to the second
				 *    part, if it is possible. F.e.
				 *    this approach is mandatory for TUX,
				 *    where splitting is expensive.
				 * 2. Split is accurately. We make this.
				 */
				skb_frag_ref(skb, i);
				skb_frag_off_add(&skb_shinfo(skb1)->frags[0], len - pos);
				skb_frag_size_sub(&skb_shinfo(skb1)->frags[0], len - pos);
				skb_frag_size_set(&skb_shinfo(skb)->frags[i], len - pos);
				skb_shinfo(skb)->nr_frags++;
			}
			k++;
		} else
			skb_shinfo(skb)->nr_frags++;
		pos += size;
	}
	skb_shinfo(skb1)->nr_frags = k;
}

/**
 * skb_split - Split fragmented skb to two parts at length len.
 * @skb: the buffer to split
 * @skb1: the buffer to receive the second part
 * @len: new length for skb
 */
void skb_split(struct sk_buff *skb, struct sk_buff *skb1, const u32 len)
{
	int pos = skb_headlen(skb);

	skb_shinfo(skb1)->tx_flags |= skb_shinfo(skb)->tx_flags &
				      SKBTX_SHARED_FRAG;
	skb_zerocopy_clone(skb1, skb, 0);
	if (len < pos)	/* Split line is inside header. */
		skb_split_inside_header(skb, skb1, len, pos);
	else		/* Second chunk has no header, nothing to copy. */
		skb_split_no_header(skb, skb1, len, pos);
}
EXPORT_SYMBOL(skb_split);

/* Shifting from/to a cloned skb is a no-go.
 *
 * Caller cannot keep skb_shinfo related pointers past calling here!
 */
static int skb_prepare_for_shift(struct sk_buff *skb)
{
	return skb_unclone_keeptruesize(skb, GFP_ATOMIC);
}

/**
 * skb_shift - Shifts paged data partially from skb to another
 * @tgt: buffer into which tail data gets added
 * @skb: buffer from which the paged data comes from
 * @shiftlen: shift up to this many bytes
 *
 * Attempts to shift up to shiftlen worth of bytes, which may be less than
 * the length of the skb, from skb to tgt. Returns number bytes shifted.
 * It's up to caller to free skb if everything was shifted.
 *
 * If @tgt runs out of frags, the whole operation is aborted.
 *
 * Skb cannot include anything else but paged data while tgt is allowed
 * to have non-paged data as well.
 *
 * TODO: full sized shift could be optimized but that would need
 * specialized skb free'er to handle frags without up-to-date nr_frags.
 */
int skb_shift(struct sk_buff *tgt, struct sk_buff *skb, int shiftlen)
{
	int from, to, merge, todo;
	skb_frag_t *fragfrom, *fragto;

	BUG_ON(shiftlen > skb->len);

	if (skb_headlen(skb))
		return 0;
	if (skb_zcopy(tgt) || skb_zcopy(skb))
		return 0;

	todo = shiftlen;
	from = 0;
	to = skb_shinfo(tgt)->nr_frags;
	fragfrom = &skb_shinfo(skb)->frags[from];

	/* Actual merge is delayed until the point when we know we can
	 * commit all, so that we don't have to undo partial changes
	 */
	if (!to ||
	    !skb_can_coalesce(tgt, to, skb_frag_page(fragfrom),
			      skb_frag_off(fragfrom))) {
		merge = -1;
	} else {
		merge = to - 1;

		todo -= skb_frag_size(fragfrom);
		if (todo < 0) {
			if (skb_prepare_for_shift(skb) ||
			    skb_prepare_for_shift(tgt))
				return 0;

			/* All previous frag pointers might be stale! */
			fragfrom = &skb_shinfo(skb)->frags[from];
			fragto = &skb_shinfo(tgt)->frags[merge];

			skb_frag_size_add(fragto, shiftlen);
			skb_frag_size_sub(fragfrom, shiftlen);
			skb_frag_off_add(fragfrom, shiftlen);

			goto onlymerged;
		}

		from++;
	}

	/* Skip full, not-fitting skb to avoid expensive operations */
	if ((shiftlen == skb->len) &&
	    (skb_shinfo(skb)->nr_frags - from) > (MAX_SKB_FRAGS - to))
		return 0;

	if (skb_prepare_for_shift(skb) || skb_prepare_for_shift(tgt))
		return 0;

	while ((todo > 0) && (from < skb_shinfo(skb)->nr_frags)) {
		if (to == MAX_SKB_FRAGS)
			return 0;

		fragfrom = &skb_shinfo(skb)->frags[from];
		fragto = &skb_shinfo(tgt)->frags[to];

		if (todo >= skb_frag_size(fragfrom)) {
			*fragto = *fragfrom;
			todo -= skb_frag_size(fragfrom);
			from++;
			to++;

		} else {
			__skb_frag_ref(fragfrom);
			skb_frag_page_copy(fragto, fragfrom);
			skb_frag_off_copy(fragto, fragfrom);
			skb_frag_size_set(fragto, todo);

			skb_frag_off_add(fragfrom, todo);
			skb_frag_size_sub(fragfrom, todo);
			todo = 0;

			to++;
			break;
		}
	}

	/* Ready to "commit" this state change to tgt */
	skb_shinfo(tgt)->nr_frags = to;

	if (merge >= 0) {
		fragfrom = &skb_shinfo(skb)->frags[0];
		fragto = &skb_shinfo(tgt)->frags[merge];

		skb_frag_size_add(fragto, skb_frag_size(fragfrom));
		__skb_frag_unref(fragfrom);
	}

	/* Reposition in the original skb */
	to = 0;
	while (from < skb_shinfo(skb)->nr_frags)
		skb_shinfo(skb)->frags[to++] = skb_shinfo(skb)->frags[from++];
	skb_shinfo(skb)->nr_frags = to;

	BUG_ON(todo > 0 && !skb_shinfo(skb)->nr_frags);

onlymerged:
	/* Most likely the tgt won't ever need its checksum anymore, skb on
	 * the other hand might need it if it needs to be resent
	 */
	tgt->ip_summed = CHECKSUM_PARTIAL;
	skb->ip_summed = CHECKSUM_PARTIAL;

	/* Yak, is it really working this way? Some helper please? */
	skb->len -= shiftlen;
	skb->data_len -= shiftlen;
	skb->truesize -= shiftlen;
	tgt->len += shiftlen;
	tgt->data_len += shiftlen;
	tgt->truesize += shiftlen;

	return shiftlen;
}

/**
 * skb_prepare_seq_read - Prepare a sequential read of skb data
 * @skb: the buffer to read
 * @from: lower offset of data to be read
 * @to: upper offset of data to be read
 * @st: state variable
 *
 * Initializes the specified state variable. Must be called before
 * invoking skb_seq_read() for the first time.
 */
void skb_prepare_seq_read(struct sk_buff *skb, unsigned int from,
			  unsigned int to, struct skb_seq_state *st)
{
	st->lower_offset = from;
	st->upper_offset = to;
	st->root_skb = st->cur_skb = skb;
	st->frag_idx = st->stepped_offset = 0;
	st->frag_data = NULL;
}
EXPORT_SYMBOL(skb_prepare_seq_read);

/**
 * skb_seq_read - Sequentially read skb data
 * @consumed: number of bytes consumed by the caller so far
 * @data: destination pointer for data to be returned
 * @st: state variable
 *
 * Reads a block of skb data at @consumed relative to the
 * lower offset specified to skb_prepare_seq_read(). Assigns
 * the head of the data block to @data and returns the length
 * of the block or 0 if the end of the skb data or the upper
 * offset has been reached.
 *
 * The caller is not required to consume all of the data
 * returned, i.e. @consumed is typically set to the number
 * of bytes already consumed and the next call to
 * skb_seq_read() will return the remaining part of the block.
 *
 * Note 1: The size of each block of data returned can be arbitrary,
 *       this limitation is the cost for zerocopy sequential
 *       reads of potentially non linear data.
 *
 * Note 2: Fragment lists within fragments are not implemented
 *       at the moment, state->root_skb could be replaced with
 *       a stack for this purpose.
 */
unsigned int skb_seq_read(unsigned int consumed, const u8 **data,
			  struct skb_seq_state *st)
{
	unsigned int block_limit, abs_offset = consumed + st->lower_offset;
	skb_frag_t *frag;

	if (unlikely(abs_offset >= st->upper_offset)) {
		if (st->frag_data) {
			kunmap_atomic(st->frag_data);
			st->frag_data = NULL;
		}
		return 0;
	}

next_skb:
	block_limit = skb_headlen(st->cur_skb) + st->stepped_offset;

	if (abs_offset < block_limit && !st->frag_data) {
		*data = st->cur_skb->data + (abs_offset - st->stepped_offset);
		return block_limit - abs_offset;
	}

	if (st->frag_idx == 0 && !st->frag_data)
		st->stepped_offset += skb_headlen(st->cur_skb);

	while (st->frag_idx < skb_shinfo(st->cur_skb)->nr_frags) {
		frag = &skb_shinfo(st->cur_skb)->frags[st->frag_idx];
		block_limit = skb_frag_size(frag) + st->stepped_offset;

		if (abs_offset < block_limit) {
			if (!st->frag_data)
				st->frag_data = kmap_atomic(skb_frag_page(frag));

			*data = (u8 *) st->frag_data + skb_frag_off(frag) +
				(abs_offset - st->stepped_offset);

			return block_limit - abs_offset;
		}

		if (st->frag_data) {
			kunmap_atomic(st->frag_data);
			st->frag_data = NULL;
		}

		st->frag_idx++;
		st->stepped_offset += skb_frag_size(frag);
	}

	if (st->frag_data) {
		kunmap_atomic(st->frag_data);
		st->frag_data = NULL;
	}

	if (st->root_skb == st->cur_skb && skb_has_frag_list(st->root_skb)) {
		st->cur_skb = skb_shinfo(st->root_skb)->frag_list;
		st->frag_idx = 0;
		goto next_skb;
	} else if (st->cur_skb->next) {
		st->cur_skb = st->cur_skb->next;
		st->frag_idx = 0;
		goto next_skb;
	}

	return 0;
}
EXPORT_SYMBOL(skb_seq_read);

/**
 * skb_abort_seq_read - Abort a sequential read of skb data
 * @st: state variable
 *
 * Must be called if skb_seq_read() was not called until it
 * returned 0.
 */
void skb_abort_seq_read(struct skb_seq_state *st)
{
	if (st->frag_data)
		kunmap_atomic(st->frag_data);
}
EXPORT_SYMBOL(skb_abort_seq_read);

#define TS_SKB_CB(state)	((struct skb_seq_state *) &((state)->cb))

static unsigned int skb_ts_get_next_block(unsigned int offset, const u8 **text,
					  struct ts_config *conf,
					  struct ts_state *state)
{
	return skb_seq_read(offset, text, TS_SKB_CB(state));
}

static void skb_ts_finish(struct ts_config *conf, struct ts_state *state)
{
	skb_abort_seq_read(TS_SKB_CB(state));
}

/**
 * skb_find_text - Find a text pattern in skb data
 * @skb: the buffer to look in
 * @from: search offset
 * @to: search limit
 * @config: textsearch configuration
 *
 * Finds a pattern in the skb data according to the specified
 * textsearch configuration. Use textsearch_next() to retrieve
 * subsequent occurrences of the pattern. Returns the offset
 * to the first occurrence or UINT_MAX if no match was found.
 */
unsigned int skb_find_text(struct sk_buff *skb, unsigned int from,
			   unsigned int to, struct ts_config *config)
{
	struct ts_state state;
	unsigned int ret;

	config->get_next_block = skb_ts_get_next_block;
	config->finish = skb_ts_finish;

	skb_prepare_seq_read(skb, from, to, TS_SKB_CB(&state));

	ret = textsearch_find(config, &state);
	return (ret <= to - from ? ret : UINT_MAX);
}
EXPORT_SYMBOL(skb_find_text);

int skb_append_pagefrags(struct sk_buff *skb, struct page *page,
			 int offset, size_t size)
{
	int i = skb_shinfo(skb)->nr_frags;

	if (skb_can_coalesce(skb, i, page, offset)) {
		skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], size);
	} else if (i < MAX_SKB_FRAGS) {
		get_page(page);
		skb_fill_page_desc(skb, i, page, offset, size);
	} else {
		return -EMSGSIZE;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(skb_append_pagefrags);

/**
 *	skb_pull_rcsum - pull skb and update receive checksum
 *	@skb: buffer to update
 *	@len: length of data pulled
 *
 *	This function performs an skb_pull on the packet and updates
 *	the CHECKSUM_COMPLETE checksum.  It should be used on
 *	receive path processing instead of skb_pull unless you know
 *	that the checksum difference is zero (e.g., a valid IP header)
 *	or you are setting ip_summed to CHECKSUM_NONE.
 */
void *skb_pull_rcsum(struct sk_buff *skb, unsigned int len)
{
	unsigned char *data = skb->data;

	BUG_ON(len > skb->len);
	__skb_pull(skb, len);
	skb_postpull_rcsum(skb, data, len);
	return skb->data;
}
EXPORT_SYMBOL_GPL(skb_pull_rcsum);

static inline skb_frag_t skb_head_frag_to_page_desc(struct sk_buff *frag_skb)
{
	skb_frag_t head_frag;
	struct page *page;

	page = virt_to_head_page(frag_skb->head);
	__skb_frag_set_page(&head_frag, page);
	skb_frag_off_set(&head_frag, frag_skb->data -
			 (unsigned char *)page_address(page));
	skb_frag_size_set(&head_frag, skb_headlen(frag_skb));
	return head_frag;
}

struct sk_buff *skb_segment_list(struct sk_buff *skb,
				 netdev_features_t features,
				 unsigned int offset)
{
	struct sk_buff *list_skb = skb_shinfo(skb)->frag_list;
	unsigned int tnl_hlen = skb_tnl_header_len(skb);
	unsigned int delta_truesize = 0;
	unsigned int delta_len = 0;
	struct sk_buff *tail = NULL;
	struct sk_buff *nskb, *tmp;
	int len_diff, err;

	skb_push(skb, -skb_network_offset(skb) + offset);

	skb_shinfo(skb)->frag_list = NULL;

	do {
		nskb = list_skb;
		list_skb = list_skb->next;

		err = 0;
		delta_truesize += nskb->truesize;
		if (skb_shared(nskb)) {
			tmp = skb_clone(nskb, GFP_ATOMIC);
			if (tmp) {
				consume_skb(nskb);
				nskb = tmp;
				err = skb_unclone(nskb, GFP_ATOMIC);
			} else {
				err = -ENOMEM;
			}
		}

		if (!tail)
			skb->next = nskb;
		else
			tail->next = nskb;

		if (unlikely(err)) {
			nskb->next = list_skb;
			goto err_linearize;
		}

		tail = nskb;

		delta_len += nskb->len;

		skb_push(nskb, -skb_network_offset(nskb) + offset);

		skb_release_head_state(nskb);
		len_diff = skb_network_header_len(nskb) - skb_network_header_len(skb);
		 __copy_skb_header(nskb, skb);

		skb_headers_offset_update(nskb, skb_headroom(nskb) - skb_headroom(skb));
		nskb->transport_header += len_diff;
		skb_copy_from_linear_data_offset(skb, -tnl_hlen,
						 nskb->data - tnl_hlen,
						 offset + tnl_hlen);

		if (skb_needs_linearize(nskb, features) &&
		    __skb_linearize(nskb))
			goto err_linearize;

	} while (list_skb);

	skb->truesize = skb->truesize - delta_truesize;
	skb->data_len = skb->data_len - delta_len;
	skb->len = skb->len - delta_len;

	skb_gso_reset(skb);

	skb->prev = tail;

	if (skb_needs_linearize(skb, features) &&
	    __skb_linearize(skb))
		goto err_linearize;

	skb_get(skb);

	return skb;

err_linearize:
	kfree_skb_list(skb->next);
	skb->next = NULL;
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(skb_segment_list);

int skb_gro_receive_list(struct sk_buff *p, struct sk_buff *skb)
{
	if (unlikely(p->len + skb->len >= 65536))
		return -E2BIG;

	if (NAPI_GRO_CB(p)->last == p)
		skb_shinfo(p)->frag_list = skb;
	else
		NAPI_GRO_CB(p)->last->next = skb;

	skb_pull(skb, skb_gro_offset(skb));

	NAPI_GRO_CB(p)->last = skb;
	NAPI_GRO_CB(p)->count++;
	p->data_len += skb->len;
	p->truesize += skb->truesize;
	p->len += skb->len;

	NAPI_GRO_CB(skb)->same_flow = 1;

	return 0;
}

/**
 *	skb_segment - Perform protocol segmentation on skb.
 *	@head_skb: buffer to segment
 *	@features: features for the output path (see dev->features)
 *
 *	This function performs segmentation on the given skb.  It returns
 *	a pointer to the first in a list of new skbs for the segments.
 *	In case of error it returns ERR_PTR(err).
 */
struct sk_buff *skb_segment(struct sk_buff *head_skb,
			    netdev_features_t features)
{
	struct sk_buff *segs = NULL;
	struct sk_buff *tail = NULL;
	struct sk_buff *list_skb = skb_shinfo(head_skb)->frag_list;
	skb_frag_t *frag = skb_shinfo(head_skb)->frags;
	unsigned int mss = skb_shinfo(head_skb)->gso_size;
	unsigned int doffset = head_skb->data - skb_mac_header(head_skb);
	struct sk_buff *frag_skb = head_skb;
	unsigned int offset = doffset;
	unsigned int tnl_hlen = skb_tnl_header_len(head_skb);
	unsigned int partial_segs = 0;
	unsigned int headroom;
	unsigned int len = head_skb->len;
	__be16 proto;
	bool csum, sg;
	int nfrags = skb_shinfo(head_skb)->nr_frags;
	int err = -ENOMEM;
	int i = 0;
	int pos;

	if (list_skb && !list_skb->head_frag && skb_headlen(list_skb) &&
	    (skb_shinfo(head_skb)->gso_type & SKB_GSO_DODGY)) {
		/* gso_size is untrusted, and we have a frag_list with a linear
		 * non head_frag head.
		 *
		 * (we assume checking the first list_skb member suffices;
		 * i.e if either of the list_skb members have non head_frag
		 * head, then the first one has too).
		 *
		 * If head_skb's headlen does not fit requested gso_size, it
		 * means that the frag_list members do NOT terminate on exact
		 * gso_size boundaries. Hence we cannot perform skb_frag_t page
		 * sharing. Therefore we must fallback to copying the frag_list
		 * skbs; we do so by disabling SG.
		 */
		if (mss != GSO_BY_FRAGS && mss != skb_headlen(head_skb))
			features &= ~NETIF_F_SG;
	}

	__skb_push(head_skb, doffset);
	proto = skb_network_protocol(head_skb, NULL);
	if (unlikely(!proto))
		return ERR_PTR(-EINVAL);

	sg = !!(features & NETIF_F_SG);
	csum = !!can_checksum_protocol(features, proto);

	if (sg && csum && (mss != GSO_BY_FRAGS))  {
		if (!(features & NETIF_F_GSO_PARTIAL)) {
			struct sk_buff *iter;
			unsigned int frag_len;

			if (!list_skb ||
			    !net_gso_ok(features, skb_shinfo(head_skb)->gso_type))
				goto normal;

			/* If we get here then all the required
			 * GSO features except frag_list are supported.
			 * Try to split the SKB to multiple GSO SKBs
			 * with no frag_list.
			 * Currently we can do that only when the buffers don't
			 * have a linear part and all the buffers except
			 * the last are of the same length.
			 */
			frag_len = list_skb->len;
			skb_walk_frags(head_skb, iter) {
				if (frag_len != iter->len && iter->next)
					goto normal;
				if (skb_headlen(iter) && !iter->head_frag)
					goto normal;

				len -= iter->len;
			}

			if (len != frag_len)
				goto normal;
		}

		/* GSO partial only requires that we trim off any excess that
		 * doesn't fit into an MSS sized block, so take care of that
		 * now.
		 */
		partial_segs = len / mss;
		if (partial_segs > 1)
			mss *= partial_segs;
		else
			partial_segs = 0;
	}

normal:
	headroom = skb_headroom(head_skb);
	pos = skb_headlen(head_skb);

	do {
		struct sk_buff *nskb;
		skb_frag_t *nskb_frag;
		int hsize;
		int size;

		if (unlikely(mss == GSO_BY_FRAGS)) {
			len = list_skb->len;
		} else {
			len = head_skb->len - offset;
			if (len > mss)
				len = mss;
		}

		hsize = skb_headlen(head_skb) - offset;
		if (hsize < 0)
			hsize = 0;
		if (hsize > len || !sg)
			hsize = len;

		if (!hsize && i >= nfrags && skb_headlen(list_skb) &&
		    (skb_headlen(list_skb) == len || sg)) {
			BUG_ON(skb_headlen(list_skb) > len);

			i = 0;
			nfrags = skb_shinfo(list_skb)->nr_frags;
			frag = skb_shinfo(list_skb)->frags;
			frag_skb = list_skb;
			pos += skb_headlen(list_skb);

			while (pos < offset + len) {
				BUG_ON(i >= nfrags);

				size = skb_frag_size(frag);
				if (pos + size > offset + len)
					break;

				i++;
				pos += size;
				frag++;
			}

			nskb = skb_clone(list_skb, GFP_ATOMIC);
			list_skb = list_skb->next;

			if (unlikely(!nskb))
				goto err;

			if (unlikely(pskb_trim(nskb, len))) {
				kfree_skb(nskb);
				goto err;
			}

			hsize = skb_end_offset(nskb);
			if (skb_cow_head(nskb, doffset + headroom)) {
				kfree_skb(nskb);
				goto err;
			}

			nskb->truesize += skb_end_offset(nskb) - hsize;
			skb_release_head_state(nskb);
			__skb_push(nskb, doffset);
		} else {
			nskb = __alloc_skb(hsize + doffset + headroom,
					   GFP_ATOMIC, skb_alloc_rx_flag(head_skb),
					   NUMA_NO_NODE);

			if (unlikely(!nskb))
				goto err;

			skb_reserve(nskb, headroom);
			__skb_put(nskb, doffset);
		}

		if (segs)
			tail->next = nskb;
		else
			segs = nskb;
		tail = nskb;

		__copy_skb_header(nskb, head_skb);

		skb_headers_offset_update(nskb, skb_headroom(nskb) - headroom);
		skb_reset_mac_len(nskb);

		skb_copy_from_linear_data_offset(head_skb, -tnl_hlen,
						 nskb->data - tnl_hlen,
						 doffset + tnl_hlen);

		if (nskb->len == len + doffset)
			goto perform_csum_check;

		if (!sg) {
			if (!csum) {
				if (!nskb->remcsum_offload)
					nskb->ip_summed = CHECKSUM_NONE;
				SKB_GSO_CB(nskb)->csum =
					skb_copy_and_csum_bits(head_skb, offset,
							       skb_put(nskb,
								       len),
							       len);
				SKB_GSO_CB(nskb)->csum_start =
					skb_headroom(nskb) + doffset;
			} else {
				if (skb_copy_bits(head_skb, offset, skb_put(nskb, len), len))
					goto err;
			}
			continue;
		}

		nskb_frag = skb_shinfo(nskb)->frags;

		skb_copy_from_linear_data_offset(head_skb, offset,
						 skb_put(nskb, hsize), hsize);

		skb_shinfo(nskb)->tx_flags |= skb_shinfo(head_skb)->tx_flags &
					      SKBTX_SHARED_FRAG;

		if (skb_orphan_frags(frag_skb, GFP_ATOMIC) ||
		    skb_zerocopy_clone(nskb, frag_skb, GFP_ATOMIC))
			goto err;

		while (pos < offset + len) {
			if (i >= nfrags) {
				i = 0;
				nfrags = skb_shinfo(list_skb)->nr_frags;
				frag = skb_shinfo(list_skb)->frags;
				frag_skb = list_skb;
				if (!skb_headlen(list_skb)) {
					BUG_ON(!nfrags);
				} else {
					BUG_ON(!list_skb->head_frag);

					/* to make room for head_frag. */
					i--;
					frag--;
				}
				if (skb_orphan_frags(frag_skb, GFP_ATOMIC) ||
				    skb_zerocopy_clone(nskb, frag_skb,
						       GFP_ATOMIC))
					goto err;

				list_skb = list_skb->next;
			}

			if (unlikely(skb_shinfo(nskb)->nr_frags >=
				     MAX_SKB_FRAGS)) {
				net_warn_ratelimited(
					"skb_segment: too many frags: %u %u\n",
					pos, mss);
				err = -EINVAL;
				goto err;
			}

			*nskb_frag = (i < 0) ? skb_head_frag_to_page_desc(frag_skb) : *frag;
			__skb_frag_ref(nskb_frag);
			size = skb_frag_size(nskb_frag);

			if (pos < offset) {
				skb_frag_off_add(nskb_frag, offset - pos);
				skb_frag_size_sub(nskb_frag, offset - pos);
			}

			skb_shinfo(nskb)->nr_frags++;

			if (pos + size <= offset + len) {
				i++;
				frag++;
				pos += size;
			} else {
				skb_frag_size_sub(nskb_frag, pos + size - (offset + len));
				goto skip_fraglist;
			}

			nskb_frag++;
		}

skip_fraglist:
		nskb->data_len = len - hsize;
		nskb->len += nskb->data_len;
		nskb->truesize += nskb->data_len;

perform_csum_check:
		if (!csum) {
			if (skb_has_shared_frag(nskb) &&
			    __skb_linearize(nskb))
				goto err;

			if (!nskb->remcsum_offload)
				nskb->ip_summed = CHECKSUM_NONE;
			SKB_GSO_CB(nskb)->csum =
				skb_checksum(nskb, doffset,
					     nskb->len - doffset, 0);
			SKB_GSO_CB(nskb)->csum_start =
				skb_headroom(nskb) + doffset;
		}
	} while ((offset += len) < head_skb->len);

	/* Some callers want to get the end of the list.
	 * Put it in segs->prev to avoid walking the list.
	 * (see validate_xmit_skb_list() for example)
	 */
	segs->prev = tail;

	if (partial_segs) {
		struct sk_buff *iter;
		int type = skb_shinfo(head_skb)->gso_type;
		unsigned short gso_size = skb_shinfo(head_skb)->gso_size;

		/* Update type to add partial and then remove dodgy if set */
		type |= (features & NETIF_F_GSO_PARTIAL) / NETIF_F_GSO_PARTIAL * SKB_GSO_PARTIAL;
		type &= ~SKB_GSO_DODGY;

		/* Update GSO info and prepare to start updating headers on
		 * our way back down the stack of protocols.
		 */
		for (iter = segs; iter; iter = iter->next) {
			skb_shinfo(iter)->gso_size = gso_size;
			skb_shinfo(iter)->gso_segs = partial_segs;
			skb_shinfo(iter)->gso_type = type;
			SKB_GSO_CB(iter)->data_offset = skb_headroom(iter) + doffset;
		}

		if (tail->len - doffset <= gso_size)
			skb_shinfo(tail)->gso_size = 0;
		else if (tail != segs)
			skb_shinfo(tail)->gso_segs = DIV_ROUND_UP(tail->len - doffset, gso_size);
	}

	/* Following permits correct backpressure, for protocols
	 * using skb_set_owner_w().
	 * Idea is to tranfert ownership from head_skb to last segment.
	 */
	if (head_skb->destructor == sock_wfree) {
		swap(tail->truesize, head_skb->truesize);
		swap(tail->destructor, head_skb->destructor);
		swap(tail->sk, head_skb->sk);
	}
	return segs;

err:
	kfree_skb_list(segs);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(skb_segment);

int skb_gro_receive(struct sk_buff *p, struct sk_buff *skb)
{
	struct skb_shared_info *pinfo, *skbinfo = skb_shinfo(skb);
	unsigned int offset = skb_gro_offset(skb);
	unsigned int headlen = skb_headlen(skb);
	unsigned int len = skb_gro_len(skb);
	unsigned int delta_truesize;
	struct sk_buff *lp;

	if (unlikely(p->len + len >= 65536 || NAPI_GRO_CB(skb)->flush))
		return -E2BIG;

	lp = NAPI_GRO_CB(p)->last;
	pinfo = skb_shinfo(lp);

	if (headlen <= offset) {
		skb_frag_t *frag;
		skb_frag_t *frag2;
		int i = skbinfo->nr_frags;
		int nr_frags = pinfo->nr_frags + i;

		if (nr_frags > MAX_SKB_FRAGS)
			goto merge;

		offset -= headlen;
		pinfo->nr_frags = nr_frags;
		skbinfo->nr_frags = 0;

		frag = pinfo->frags + nr_frags;
		frag2 = skbinfo->frags + i;
		do {
			*--frag = *--frag2;
		} while (--i);

		skb_frag_off_add(frag, offset);
		skb_frag_size_sub(frag, offset);

		/* all fragments truesize : remove (head size + sk_buff) */
		delta_truesize = skb->truesize -
				 SKB_TRUESIZE(skb_end_offset(skb));

		skb->truesize -= skb->data_len;
		skb->len -= skb->data_len;
		skb->data_len = 0;

		NAPI_GRO_CB(skb)->free = NAPI_GRO_FREE;
		goto done;
	} else if (skb->head_frag) {
		int nr_frags = pinfo->nr_frags;
		skb_frag_t *frag = pinfo->frags + nr_frags;
		struct page *page = virt_to_head_page(skb->head);
		unsigned int first_size = headlen - offset;
		unsigned int first_offset;

		if (nr_frags + 1 + skbinfo->nr_frags > MAX_SKB_FRAGS)
			goto merge;

		first_offset = skb->data -
			       (unsigned char *)page_address(page) +
			       offset;

		pinfo->nr_frags = nr_frags + 1 + skbinfo->nr_frags;

		__skb_frag_set_page(frag, page);
		skb_frag_off_set(frag, first_offset);
		skb_frag_size_set(frag, first_size);

		memcpy(frag + 1, skbinfo->frags, sizeof(*frag) * skbinfo->nr_frags);
		/* We dont need to clear skbinfo->nr_frags here */

		delta_truesize = skb->truesize - SKB_DATA_ALIGN(sizeof(struct sk_buff));
		NAPI_GRO_CB(skb)->free = NAPI_GRO_FREE_STOLEN_HEAD;
		goto done;
	}

merge:
	delta_truesize = skb->truesize;
	if (offset > headlen) {
		unsigned int eat = offset - headlen;

		skb_frag_off_add(&skbinfo->frags[0], eat);
		skb_frag_size_sub(&skbinfo->frags[0], eat);
		skb->data_len -= eat;
		skb->len -= eat;
		offset = headlen;
	}

	__skb_pull(skb, offset);

	if (NAPI_GRO_CB(p)->last == p)
		skb_shinfo(p)->frag_list = skb;
	else
		NAPI_GRO_CB(p)->last->next = skb;
	NAPI_GRO_CB(p)->last = skb;
	__skb_header_release(skb);
	lp = p;

done:
	NAPI_GRO_CB(p)->count++;
	p->data_len += len;
	p->truesize += delta_truesize;
	p->len += len;
	if (lp != p) {
		lp->data_len += len;
		lp->truesize += delta_truesize;
		lp->len += len;
	}
	NAPI_GRO_CB(skb)->same_flow = 1;
	return 0;
}

#ifdef CONFIG_SKB_EXTENSIONS
#define SKB_EXT_ALIGN_VALUE	8
#define SKB_EXT_CHUNKSIZEOF(x)	(ALIGN((sizeof(x)), SKB_EXT_ALIGN_VALUE) / SKB_EXT_ALIGN_VALUE)

static const u8 skb_ext_type_len[] = {
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	[SKB_EXT_BRIDGE_NF] = SKB_EXT_CHUNKSIZEOF(struct nf_bridge_info),
#endif
#ifdef CONFIG_XFRM
	[SKB_EXT_SEC_PATH] = SKB_EXT_CHUNKSIZEOF(struct sec_path),
#endif
#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	[TC_SKB_EXT] = SKB_EXT_CHUNKSIZEOF(struct tc_skb_ext),
#endif
#if IS_ENABLED(CONFIG_MPTCP)
	[SKB_EXT_MPTCP] = SKB_EXT_CHUNKSIZEOF(struct mptcp_ext),
#endif
#if IS_ENABLED(CONFIG_KCOV)
	[SKB_EXT_KCOV_HANDLE] = SKB_EXT_CHUNKSIZEOF(u64),
#endif
};

static __always_inline unsigned int skb_ext_total_length(void)
{
	return SKB_EXT_CHUNKSIZEOF(struct skb_ext) +
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
		skb_ext_type_len[SKB_EXT_BRIDGE_NF] +
#endif
#ifdef CONFIG_XFRM
		skb_ext_type_len[SKB_EXT_SEC_PATH] +
#endif
#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
		skb_ext_type_len[TC_SKB_EXT] +
#endif
#if IS_ENABLED(CONFIG_MPTCP)
		skb_ext_type_len[SKB_EXT_MPTCP] +
#endif
#if IS_ENABLED(CONFIG_KCOV)
		skb_ext_type_len[SKB_EXT_KCOV_HANDLE] +
#endif
		0;
}

static void skb_extensions_init(void)
{
	BUILD_BUG_ON(SKB_EXT_NUM >= 8);
	BUILD_BUG_ON(skb_ext_total_length() > 255);

	skbuff_ext_cache = kmem_cache_create("skbuff_ext_cache",
					     SKB_EXT_ALIGN_VALUE * skb_ext_total_length(),
					     0,
					     SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					     NULL);
}
#else
static void skb_extensions_init(void) {}
#endif

void __init skb_init(void)
{
	skbuff_head_cache = kmem_cache_create_usercopy("skbuff_head_cache",
					      sizeof(struct sk_buff),
					      0,
					      SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					      offsetof(struct sk_buff, cb),
					      sizeof_field(struct sk_buff, cb),
					      NULL);
	skbuff_fclone_cache = kmem_cache_create("skbuff_fclone_cache",
						sizeof(struct sk_buff_fclones),
						0,
						SLAB_HWCACHE_ALIGN|SLAB_PANIC,
						NULL);
	skb_extensions_init();
}

static int
__skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg, int offset, int len,
	       unsigned int recursion_level)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;
	int elt = 0;

	if (unlikely(recursion_level >= 24))
		return -EMSGSIZE;

	if (copy > 0) {
		if (copy > len)
			copy = len;
		sg_set_buf(sg, skb->data + offset, copy);
		elt++;
		if ((len -= copy) == 0)
			return elt;
		offset += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(&skb_shinfo(skb)->frags[i]);
		if ((copy = end - offset) > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			if (unlikely(elt && sg_is_last(&sg[elt - 1])))
				return -EMSGSIZE;

			if (copy > len)
				copy = len;
			sg_set_page(&sg[elt], skb_frag_page(frag), copy,
				    skb_frag_off(frag) + offset - start);
			elt++;
			if (!(len -= copy))
				return elt;
			offset += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end, ret;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (unlikely(elt && sg_is_last(&sg[elt - 1])))
				return -EMSGSIZE;

			if (copy > len)
				copy = len;
			ret = __skb_to_sgvec(frag_iter, sg+elt, offset - start,
					      copy, recursion_level + 1);
			if (unlikely(ret < 0))
				return ret;
			elt += ret;
			if ((len -= copy) == 0)
				return elt;
			offset += copy;
		}
		start = end;
	}
	BUG_ON(len);
	return elt;
}

/**
 *	skb_to_sgvec - Fill a scatter-gather list from a socket buffer
 *	@skb: Socket buffer containing the buffers to be mapped
 *	@sg: The scatter-gather list to map into
 *	@offset: The offset into the buffer's contents to start mapping
 *	@len: Length of buffer space to be mapped
 *
 *	Fill the specified scatter-gather list with mappings/pointers into a
 *	region of the buffer space attached to a socket buffer. Returns either
 *	the number of scatterlist items used, or -EMSGSIZE if the contents
 *	could not fit.
 */
int skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg, int offset, int len)
{
	int nsg = __skb_to_sgvec(skb, sg, offset, len, 0);

	if (nsg <= 0)
		return nsg;

	sg_mark_end(&sg[nsg - 1]);

	return nsg;
}
EXPORT_SYMBOL_GPL(skb_to_sgvec);

/* As compared with skb_to_sgvec, skb_to_sgvec_nomark only map skb to given
 * sglist without mark the sg which contain last skb data as the end.
 * So the caller can mannipulate sg list as will when padding new data after
 * the first call without calling sg_unmark_end to expend sg list.
 *
 * Scenario to use skb_to_sgvec_nomark:
 * 1. sg_init_table
 * 2. skb_to_sgvec_nomark(payload1)
 * 3. skb_to_sgvec_nomark(payload2)
 *
 * This is equivalent to:
 * 1. sg_init_table
 * 2. skb_to_sgvec(payload1)
 * 3. sg_unmark_end
 * 4. skb_to_sgvec(payload2)
 *
 * When mapping mutilple payload conditionally, skb_to_sgvec_nomark
 * is more preferable.
 */
int skb_to_sgvec_nomark(struct sk_buff *skb, struct scatterlist *sg,
			int offset, int len)
{
	return __skb_to_sgvec(skb, sg, offset, len, 0);
}
EXPORT_SYMBOL_GPL(skb_to_sgvec_nomark);



/**
 *	skb_cow_data - Check that a socket buffer's data buffers are writable
 *	@skb: The socket buffer to check.
 *	@tailbits: Amount of trailing space to be added
 *	@trailer: Returned pointer to the skb where the @tailbits space begins
 *
 *	Make sure that the data buffers attached to a socket buffer are
 *	writable. If they are not, private copies are made of the data buffers
 *	and the socket buffer is set to use these instead.
 *
 *	If @tailbits is given, make sure that there is space to write @tailbits
 *	bytes of data beyond current end of socket buffer.  @trailer will be
 *	set to point to the skb in which this space begins.
 *
 *	The number of scatterlist elements required to completely map the
 *	COW'd and extended socket buffer will be returned.
 */
int skb_cow_data(struct sk_buff *skb, int tailbits, struct sk_buff **trailer)
{
	int copyflag;
	int elt;
	struct sk_buff *skb1, **skb_p;

	/* If skb is cloned or its head is paged, reallocate
	 * head pulling out all the pages (pages are considered not writable
	 * at the moment even if they are anonymous).
	 */
	if ((skb_cloned(skb) || skb_shinfo(skb)->nr_frags) &&
	    !__pskb_pull_tail(skb, __skb_pagelen(skb)))
		return -ENOMEM;

	/* Easy case. Most of packets will go this way. */
	if (!skb_has_frag_list(skb)) {
		/* A little of trouble, not enough of space for trailer.
		 * This should not happen, when stack is tuned to generate
		 * good frames. OK, on miss we reallocate and reserve even more
		 * space, 128 bytes is fair. */

		if (skb_tailroom(skb) < tailbits &&
		    pskb_expand_head(skb, 0, tailbits-skb_tailroom(skb)+128, GFP_ATOMIC))
			return -ENOMEM;

		/* Voila! */
		*trailer = skb;
		return 1;
	}

	/* Misery. We are in troubles, going to mincer fragments... */

	elt = 1;
	skb_p = &skb_shinfo(skb)->frag_list;
	copyflag = 0;

	while ((skb1 = *skb_p) != NULL) {
		int ntail = 0;

		/* The fragment is partially pulled by someone,
		 * this can happen on input. Copy it and everything
		 * after it. */

		if (skb_shared(skb1))
			copyflag = 1;

		/* If the skb is the last, worry about trailer. */

		if (skb1->next == NULL && tailbits) {
			if (skb_shinfo(skb1)->nr_frags ||
			    skb_has_frag_list(skb1) ||
			    skb_tailroom(skb1) < tailbits)
				ntail = tailbits + 128;
		}

		if (copyflag ||
		    skb_cloned(skb1) ||
		    ntail ||
		    skb_shinfo(skb1)->nr_frags ||
		    skb_has_frag_list(skb1)) {
			struct sk_buff *skb2;

			/* Fuck, we are miserable poor guys... */
			if (ntail == 0)
				skb2 = skb_copy(skb1, GFP_ATOMIC);
			else
				skb2 = skb_copy_expand(skb1,
						       skb_headroom(skb1),
						       ntail,
						       GFP_ATOMIC);
			if (unlikely(skb2 == NULL))
				return -ENOMEM;

			if (skb1->sk)
				skb_set_owner_w(skb2, skb1->sk);

			/* Looking around. Are we still alive?
			 * OK, link new skb, drop old one */

			skb2->next = skb1->next;
			*skb_p = skb2;
			kfree_skb(skb1);
			skb1 = skb2;
		}
		elt++;
		*trailer = skb1;
		skb_p = &skb1->next;
	}

	return elt;
}
EXPORT_SYMBOL_GPL(skb_cow_data);

static void sock_rmem_free(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	atomic_sub(skb->truesize, &sk->sk_rmem_alloc);
}

static void skb_set_err_queue(struct sk_buff *skb)
{
	/* pkt_type of skbs received on local sockets is never PACKET_OUTGOING.
	 * So, it is safe to (mis)use it to mark skbs on the error queue.
	 */
	skb->pkt_type = PACKET_OUTGOING;
	BUILD_BUG_ON(PACKET_OUTGOING == 0);
}

/*
 * Note: We dont mem charge error packets (no sk_forward_alloc changes)
 */
int sock_queue_err_skb(struct sock *sk, struct sk_buff *skb)
{
	if (atomic_read(&sk->sk_rmem_alloc) + skb->truesize >=
	    (unsigned int)READ_ONCE(sk->sk_rcvbuf))
		return -ENOMEM;

	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = sock_rmem_free;
	atomic_add(skb->truesize, &sk->sk_rmem_alloc);
	skb_set_err_queue(skb);

	/* before exiting rcu section, make sure dst is refcounted */
	skb_dst_force(skb);

	skb_queue_tail(&sk->sk_error_queue, skb);
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_error_report(sk);
	return 0;
}
EXPORT_SYMBOL(sock_queue_err_skb);

static bool is_icmp_err_skb(const struct sk_buff *skb)
{
	return skb && (SKB_EXT_ERR(skb)->ee.ee_origin == SO_EE_ORIGIN_ICMP ||
		       SKB_EXT_ERR(skb)->ee.ee_origin == SO_EE_ORIGIN_ICMP6);
}

struct sk_buff *sock_dequeue_err_skb(struct sock *sk)
{
	struct sk_buff_head *q = &sk->sk_error_queue;
	struct sk_buff *skb, *skb_next = NULL;
	bool icmp_next = false;
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	skb = __skb_dequeue(q);
	if (skb && (skb_next = skb_peek(q))) {
		icmp_next = is_icmp_err_skb(skb_next);
		if (icmp_next)
			sk->sk_err = SKB_EXT_ERR(skb_next)->ee.ee_errno;
	}
	spin_unlock_irqrestore(&q->lock, flags);

	if (is_icmp_err_skb(skb) && !icmp_next)
		sk->sk_err = 0;

	if (skb_next)
		sk->sk_error_report(sk);

	return skb;
}
EXPORT_SYMBOL(sock_dequeue_err_skb);

/**
 * skb_clone_sk - create clone of skb, and take reference to socket
 * @skb: the skb to clone
 *
 * This function creates a clone of a buffer that holds a reference on
 * sk_refcnt.  Buffers created via this function are meant to be
 * returned using sock_queue_err_skb, or free via kfree_skb.
 *
 * When passing buffers allocated with this function to sock_queue_err_skb
 * it is necessary to wrap the call with sock_hold/sock_put in order to
 * prevent the socket from being released prior to being enqueued on
 * the sk_error_queue.
 */
struct sk_buff *skb_clone_sk(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct sk_buff *clone;

	if (!sk || !refcount_inc_not_zero(&sk->sk_refcnt))
		return NULL;

	clone = skb_clone(skb, GFP_ATOMIC);
	if (!clone) {
		sock_put(sk);
		return NULL;
	}

	clone->sk = sk;
	clone->destructor = sock_efree;

	return clone;
}
EXPORT_SYMBOL(skb_clone_sk);

static void __skb_complete_tx_timestamp(struct sk_buff *skb,
					struct sock *sk,
					int tstype,
					bool opt_stats)
{
	struct sock_exterr_skb *serr;
	int err;

	BUILD_BUG_ON(sizeof(struct sock_exterr_skb) > sizeof(skb->cb));

	serr = SKB_EXT_ERR(skb);
	memset(serr, 0, sizeof(*serr));
	serr->ee.ee_errno = ENOMSG;
	serr->ee.ee_origin = SO_EE_ORIGIN_TIMESTAMPING;
	serr->ee.ee_info = tstype;
	serr->opt_stats = opt_stats;
	serr->header.h4.iif = skb->dev ? skb->dev->ifindex : 0;
	if (sk->sk_tsflags & SOF_TIMESTAMPING_OPT_ID) {
		serr->ee.ee_data = skb_shinfo(skb)->tskey;
		if (sk->sk_protocol == IPPROTO_TCP &&
		    sk->sk_type == SOCK_STREAM)
			serr->ee.ee_data -= sk->sk_tskey;
	}

	err = sock_queue_err_skb(sk, skb);

	if (err)
		kfree_skb(skb);
}

static bool skb_may_tx_timestamp(struct sock *sk, bool tsonly)
{
	bool ret;

	if (likely(READ_ONCE(sysctl_tstamp_allow_data) || tsonly))
		return true;

	read_lock_bh(&sk->sk_callback_lock);
	ret = sk->sk_socket && sk->sk_socket->file &&
	      file_ns_capable(sk->sk_socket->file, &init_user_ns, CAP_NET_RAW);
	read_unlock_bh(&sk->sk_callback_lock);
	return ret;
}

void skb_complete_tx_timestamp(struct sk_buff *skb,
			       struct skb_shared_hwtstamps *hwtstamps)
{
	struct sock *sk = skb->sk;

	if (!skb_may_tx_timestamp(sk, false))
		goto err;

	/* Take a reference to prevent skb_orphan() from freeing the socket,
	 * but only if the socket refcount is not zero.
	 */
	if (likely(refcount_inc_not_zero(&sk->sk_refcnt))) {
		*skb_hwtstamps(skb) = *hwtstamps;
		__skb_complete_tx_timestamp(skb, sk, SCM_TSTAMP_SND, false);
		sock_put(sk);
		return;
	}

err:
	kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(skb_complete_tx_timestamp);

void __skb_tstamp_tx(struct sk_buff *orig_skb,
		     struct skb_shared_hwtstamps *hwtstamps,
		     struct sock *sk, int tstype)
{
	struct sk_buff *skb;
	bool tsonly, opt_stats = false;

	if (!sk)
		return;

	if (!hwtstamps && !(sk->sk_tsflags & SOF_TIMESTAMPING_OPT_TX_SWHW) &&
	    skb_shinfo(orig_skb)->tx_flags & SKBTX_IN_PROGRESS)
		return;

	tsonly = sk->sk_tsflags & SOF_TIMESTAMPING_OPT_TSONLY;
	if (!skb_may_tx_timestamp(sk, tsonly))
		return;

	if (tsonly) {
#ifdef CONFIG_INET
		if ((sk->sk_tsflags & SOF_TIMESTAMPING_OPT_STATS) &&
		    sk->sk_protocol == IPPROTO_TCP &&
		    sk->sk_type == SOCK_STREAM) {
			skb = tcp_get_timestamping_opt_stats(sk, orig_skb);
			opt_stats = true;
		} else
#endif
			skb = alloc_skb(0, GFP_ATOMIC);
	} else {
		skb = skb_clone(orig_skb, GFP_ATOMIC);
	}
	if (!skb)
		return;

	if (tsonly) {
		skb_shinfo(skb)->tx_flags |= skb_shinfo(orig_skb)->tx_flags &
					     SKBTX_ANY_TSTAMP;
		skb_shinfo(skb)->tskey = skb_shinfo(orig_skb)->tskey;
	}

	if (hwtstamps)
		*skb_hwtstamps(skb) = *hwtstamps;
	else
		skb->tstamp = ktime_get_real();

	__skb_complete_tx_timestamp(skb, sk, tstype, opt_stats);
}
EXPORT_SYMBOL_GPL(__skb_tstamp_tx);

void skb_tstamp_tx(struct sk_buff *orig_skb,
		   struct skb_shared_hwtstamps *hwtstamps)
{
	return __skb_tstamp_tx(orig_skb, hwtstamps, orig_skb->sk,
			       SCM_TSTAMP_SND);
}
EXPORT_SYMBOL_GPL(skb_tstamp_tx);

void skb_complete_wifi_ack(struct sk_buff *skb, bool acked)
{
	struct sock *sk = skb->sk;
	struct sock_exterr_skb *serr;
	int err = 1;

	skb->wifi_acked_valid = 1;
	skb->wifi_acked = acked;

	serr = SKB_EXT_ERR(skb);
	memset(serr, 0, sizeof(*serr));
	serr->ee.ee_errno = ENOMSG;
	serr->ee.ee_origin = SO_EE_ORIGIN_TXSTATUS;

	/* Take a reference to prevent skb_orphan() from freeing the socket,
	 * but only if the socket refcount is not zero.
	 */
	if (likely(refcount_inc_not_zero(&sk->sk_refcnt))) {
		err = sock_queue_err_skb(sk, skb);
		sock_put(sk);
	}
	if (err)
		kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(skb_complete_wifi_ack);

/**
 * skb_partial_csum_set - set up and verify partial csum values for packet
 * @skb: the skb to set
 * @start: the number of bytes after skb->data to start checksumming.
 * @off: the offset from start to place the checksum.
 *
 * For untrusted partially-checksummed packets, we need to make sure the values
 * for skb->csum_start and skb->csum_offset are valid so we don't oops.
 *
 * This function checks and sets those values and skb->ip_summed: if this
 * returns false you should drop the packet.
 */
bool skb_partial_csum_set(struct sk_buff *skb, u16 start, u16 off)
{
	u32 csum_end = (u32)start + (u32)off + sizeof(__sum16);
	u32 csum_start = skb_headroom(skb) + (u32)start;

	if (unlikely(csum_start > U16_MAX || csum_end > skb_headlen(skb))) {
		net_warn_ratelimited("bad partial csum: csum=%u/%u headroom=%u headlen=%u\n",
				     start, off, skb_headroom(skb), skb_headlen(skb));
		return false;
	}
	skb->ip_summed = CHECKSUM_PARTIAL;
	skb->csum_start = csum_start;
	skb->csum_offset = off;
	skb_set_transport_header(skb, start);
	return true;
}
EXPORT_SYMBOL_GPL(skb_partial_csum_set);

static int skb_maybe_pull_tail(struct sk_buff *skb, unsigned int len,
			       unsigned int max)
{
	if (skb_headlen(skb) >= len)
		return 0;

	/* If we need to pullup then pullup to the max, so we
	 * won't need to do it again.
	 */
	if (max > skb->len)
		max = skb->len;

	if (__pskb_pull_tail(skb, max - skb_headlen(skb)) == NULL)
		return -ENOMEM;

	if (skb_headlen(skb) < len)
		return -EPROTO;

	return 0;
}

#define MAX_TCP_HDR_LEN (15 * 4)

static __sum16 *skb_checksum_setup_ip(struct sk_buff *skb,
				      typeof(IPPROTO_IP) proto,
				      unsigned int off)
{
	int err;

	switch (proto) {
	case IPPROTO_TCP:
		err = skb_maybe_pull_tail(skb, off + sizeof(struct tcphdr),
					  off + MAX_TCP_HDR_LEN);
		if (!err && !skb_partial_csum_set(skb, off,
						  offsetof(struct tcphdr,
							   check)))
			err = -EPROTO;
		return err ? ERR_PTR(err) : &tcp_hdr(skb)->check;

	case IPPROTO_UDP:
		err = skb_maybe_pull_tail(skb, off + sizeof(struct udphdr),
					  off + sizeof(struct udphdr));
		if (!err && !skb_partial_csum_set(skb, off,
						  offsetof(struct udphdr,
							   check)))
			err = -EPROTO;
		return err ? ERR_PTR(err) : &udp_hdr(skb)->check;
	}

	return ERR_PTR(-EPROTO);
}

/* This value should be large enough to cover a tagged ethernet header plus
 * maximally sized IP and TCP or UDP headers.
 */
#define MAX_IP_HDR_LEN 128

static int skb_checksum_setup_ipv4(struct sk_buff *skb, bool recalculate)
{
	unsigned int off;
	bool fragment;
	__sum16 *csum;
	int err;

	fragment = false;

	err = skb_maybe_pull_tail(skb,
				  sizeof(struct iphdr),
				  MAX_IP_HDR_LEN);
	if (err < 0)
		goto out;

	if (ip_is_fragment(ip_hdr(skb)))
		fragment = true;

	off = ip_hdrlen(skb);

	err = -EPROTO;

	if (fragment)
		goto out;

	csum = skb_checksum_setup_ip(skb, ip_hdr(skb)->protocol, off);
	if (IS_ERR(csum))
		return PTR_ERR(csum);

	if (recalculate)
		*csum = ~csum_tcpudp_magic(ip_hdr(skb)->saddr,
					   ip_hdr(skb)->daddr,
					   skb->len - off,
					   ip_hdr(skb)->protocol, 0);
	err = 0;

out:
	return err;
}

/* This value should be large enough to cover a tagged ethernet header plus
 * an IPv6 header, all options, and a maximal TCP or UDP header.
 */
#define MAX_IPV6_HDR_LEN 256

#define OPT_HDR(type, skb, off) \
	(type *)(skb_network_header(skb) + (off))

static int skb_checksum_setup_ipv6(struct sk_buff *skb, bool recalculate)
{
	int err;
	u8 nexthdr;
	unsigned int off;
	unsigned int len;
	bool fragment;
	bool done;
	__sum16 *csum;

	fragment = false;
	done = false;

	off = sizeof(struct ipv6hdr);

	err = skb_maybe_pull_tail(skb, off, MAX_IPV6_HDR_LEN);
	if (err < 0)
		goto out;

	nexthdr = ipv6_hdr(skb)->nexthdr;

	len = sizeof(struct ipv6hdr) + ntohs(ipv6_hdr(skb)->payload_len);
	while (off <= len && !done) {
		switch (nexthdr) {
		case IPPROTO_DSTOPTS:
		case IPPROTO_HOPOPTS:
		case IPPROTO_ROUTING: {
			struct ipv6_opt_hdr *hp;

			err = skb_maybe_pull_tail(skb,
						  off +
						  sizeof(struct ipv6_opt_hdr),
						  MAX_IPV6_HDR_LEN);
			if (err < 0)
				goto out;

			hp = OPT_HDR(struct ipv6_opt_hdr, skb, off);
			nexthdr = hp->nexthdr;
			off += ipv6_optlen(hp);
			break;
		}
		case IPPROTO_AH: {
			struct ip_auth_hdr *hp;

			err = skb_maybe_pull_tail(skb,
						  off +
						  sizeof(struct ip_auth_hdr),
						  MAX_IPV6_HDR_LEN);
			if (err < 0)
				goto out;

			hp = OPT_HDR(struct ip_auth_hdr, skb, off);
			nexthdr = hp->nexthdr;
			off += ipv6_authlen(hp);
			break;
		}
		case IPPROTO_FRAGMENT: {
			struct frag_hdr *hp;

			err = skb_maybe_pull_tail(skb,
						  off +
						  sizeof(struct frag_hdr),
						  MAX_IPV6_HDR_LEN);
			if (err < 0)
				goto out;

			hp = OPT_HDR(struct frag_hdr, skb, off);

			if (hp->frag_off & htons(IP6_OFFSET | IP6_MF))
				fragment = true;

			nexthdr = hp->nexthdr;
			off += sizeof(struct frag_hdr);
			break;
		}
		default:
			done = true;
			break;
		}
	}

	err = -EPROTO;

	if (!done || fragment)
		goto out;

	csum = skb_checksum_setup_ip(skb, nexthdr, off);
	if (IS_ERR(csum))
		return PTR_ERR(csum);

	if (recalculate)
		*csum = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					 &ipv6_hdr(skb)->daddr,
					 skb->len - off, nexthdr, 0);
	err = 0;

out:
	return err;
}

/**
 * skb_checksum_setup - set up partial checksum offset
 * @skb: the skb to set up
 * @recalculate: if true the pseudo-header checksum will be recalculated
 */
int skb_checksum_setup(struct sk_buff *skb, bool recalculate)
{
	int err;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		err = skb_checksum_setup_ipv4(skb, recalculate);
		break;

	case htons(ETH_P_IPV6):
		err = skb_checksum_setup_ipv6(skb, recalculate);
		break;

	default:
		err = -EPROTO;
		break;
	}

	return err;
}
EXPORT_SYMBOL(skb_checksum_setup);

/**
 * skb_checksum_maybe_trim - maybe trims the given skb
 * @skb: the skb to check
 * @transport_len: the data length beyond the network header
 *
 * Checks whether the given skb has data beyond the given transport length.
 * If so, returns a cloned skb trimmed to this transport length.
 * Otherwise returns the provided skb. Returns NULL in error cases
 * (e.g. transport_len exceeds skb length or out-of-memory).
 *
 * Caller needs to set the skb transport header and free any returned skb if it
 * differs from the provided skb.
 */
static struct sk_buff *skb_checksum_maybe_trim(struct sk_buff *skb,
					       unsigned int transport_len)
{
	struct sk_buff *skb_chk;
	unsigned int len = skb_transport_offset(skb) + transport_len;
	int ret;

	if (skb->len < len)
		return NULL;
	else if (skb->len == len)
		return skb;

	skb_chk = skb_clone(skb, GFP_ATOMIC);
	if (!skb_chk)
		return NULL;

	ret = pskb_trim_rcsum(skb_chk, len);
	if (ret) {
		kfree_skb(skb_chk);
		return NULL;
	}

	return skb_chk;
}

/**
 * skb_checksum_trimmed - validate checksum of an skb
 * @skb: the skb to check
 * @transport_len: the data length beyond the network header
 * @skb_chkf: checksum function to use
 *
 * Applies the given checksum function skb_chkf to the provided skb.
 * Returns a checked and maybe trimmed skb. Returns NULL on error.
 *
 * If the skb has data beyond the given transport length, then a
 * trimmed & cloned skb is checked and returned.
 *
 * Caller needs to set the skb transport header and free any returned skb if it
 * differs from the provided skb.
 */
struct sk_buff *skb_checksum_trimmed(struct sk_buff *skb,
				     unsigned int transport_len,
				     __sum16(*skb_chkf)(struct sk_buff *skb))
{
	struct sk_buff *skb_chk;
	unsigned int offset = skb_transport_offset(skb);
	__sum16 ret;

	skb_chk = skb_checksum_maybe_trim(skb, transport_len);
	if (!skb_chk)
		goto err;

	if (!pskb_may_pull(skb_chk, offset))
		goto err;

	skb_pull_rcsum(skb_chk, offset);
	ret = skb_chkf(skb_chk);
	skb_push_rcsum(skb_chk, offset);

	if (ret)
		goto err;

	return skb_chk;

err:
	if (skb_chk && skb_chk != skb)
		kfree_skb(skb_chk);

	return NULL;

}
EXPORT_SYMBOL(skb_checksum_trimmed);

void __skb_warn_lro_forwarding(const struct sk_buff *skb)
{
	net_warn_ratelimited("%s: received packets cannot be forwarded while LRO is enabled\n",
			     skb->dev->name);
}
EXPORT_SYMBOL(__skb_warn_lro_forwarding);

void kfree_skb_partial(struct sk_buff *skb, bool head_stolen)
{
	if (head_stolen) {
		skb_release_head_state(skb);
		kmem_cache_free(skbuff_head_cache, skb);
	} else {
		__kfree_skb(skb);
	}
}
EXPORT_SYMBOL(kfree_skb_partial);

/**
 * skb_try_coalesce - try to merge skb to prior one
 * @to: prior buffer
 * @from: buffer to add
 * @fragstolen: pointer to boolean
 * @delta_truesize: how much more was allocated than was requested
 */
bool skb_try_coalesce(struct sk_buff *to, struct sk_buff *from,
		      bool *fragstolen, int *delta_truesize)
{
	struct skb_shared_info *to_shinfo, *from_shinfo;
	int i, delta, len = from->len;

	*fragstolen = false;

	if (skb_cloned(to))
		return false;

	if (len <= skb_tailroom(to)) {
		if (len)
			BUG_ON(skb_copy_bits(from, 0, skb_put(to, len), len));
		*delta_truesize = 0;
		return true;
	}

	to_shinfo = skb_shinfo(to);
	from_shinfo = skb_shinfo(from);
	if (to_shinfo->frag_list || from_shinfo->frag_list)
		return false;
	if (skb_zcopy(to) || skb_zcopy(from))
		return false;

	if (skb_headlen(from) != 0) {
		struct page *page;
		unsigned int offset;

		if (to_shinfo->nr_frags +
		    from_shinfo->nr_frags >= MAX_SKB_FRAGS)
			return false;

		if (skb_head_is_locked(from))
			return false;

		delta = from->truesize - SKB_DATA_ALIGN(sizeof(struct sk_buff));

		page = virt_to_head_page(from->head);
		offset = from->data - (unsigned char *)page_address(page);

		skb_fill_page_desc(to, to_shinfo->nr_frags,
				   page, offset, skb_headlen(from));
		*fragstolen = true;
	} else {
		if (to_shinfo->nr_frags +
		    from_shinfo->nr_frags > MAX_SKB_FRAGS)
			return false;

		delta = from->truesize - SKB_TRUESIZE(skb_end_offset(from));
	}

	WARN_ON_ONCE(delta < len);

	memcpy(to_shinfo->frags + to_shinfo->nr_frags,
	       from_shinfo->frags,
	       from_shinfo->nr_frags * sizeof(skb_frag_t));
	to_shinfo->nr_frags += from_shinfo->nr_frags;

	if (!skb_cloned(from))
		from_shinfo->nr_frags = 0;

	/* if the skb is not cloned this does nothing
	 * since we set nr_frags to 0.
	 */
	for (i = 0; i < from_shinfo->nr_frags; i++)
		__skb_frag_ref(&from_shinfo->frags[i]);

	to->truesize += delta;
	to->len += len;
	to->data_len += len;

	*delta_truesize = delta;
	return true;
}
EXPORT_SYMBOL(skb_try_coalesce);

/**
 * skb_scrub_packet - scrub an skb
 *
 * @skb: buffer to clean
 * @xnet: packet is crossing netns
 *
 * skb_scrub_packet can be used after encapsulating or decapsulting a packet
 * into/from a tunnel. Some information have to be cleared during these
 * operations.
 * skb_scrub_packet can also be used to clean a skb before injecting it in
 * another namespace (@xnet == true). We have to clear all information in the
 * skb that could impact namespace isolation.
 */
void skb_scrub_packet(struct sk_buff *skb, bool xnet)
{
	skb->pkt_type = PACKET_HOST;
	skb->skb_iif = 0;
	skb->ignore_df = 0;
	skb_dst_drop(skb);
	skb_ext_reset(skb);
	nf_reset_ct(skb);
	nf_reset_trace(skb);

#ifdef CONFIG_NET_SWITCHDEV
	skb->offload_fwd_mark = 0;
	skb->offload_l3_fwd_mark = 0;
#endif

	if (!xnet)
		return;

	ipvs_reset(skb);
	skb->mark = 0;
	skb->tstamp = 0;
}
EXPORT_SYMBOL_GPL(skb_scrub_packet);

/**
 * skb_gso_transport_seglen - Return length of individual segments of a gso packet
 *
 * @skb: GSO skb
 *
 * skb_gso_transport_seglen is used to determine the real size of the
 * individual segments, including Layer4 headers (TCP/UDP).
 *
 * The MAC/L2 or network (IP, IPv6) headers are not accounted for.
 */
static unsigned int skb_gso_transport_seglen(const struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	unsigned int thlen = 0;

	if (skb->encapsulation) {
		thlen = skb_inner_transport_header(skb) -
			skb_transport_header(skb);

		if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6)))
			thlen += inner_tcp_hdrlen(skb);
	} else if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))) {
		thlen = tcp_hdrlen(skb);
	} else if (unlikely(skb_is_gso_sctp(skb))) {
		thlen = sizeof(struct sctphdr);
	} else if (shinfo->gso_type & SKB_GSO_UDP_L4) {
		thlen = sizeof(struct udphdr);
	}
	/* UFO sets gso_size to the size of the fragmentation
	 * payload, i.e. the size of the L4 (UDP) header is already
	 * accounted for.
	 */
	return thlen + shinfo->gso_size;
}

/**
 * skb_gso_network_seglen - Return length of individual segments of a gso packet
 *
 * @skb: GSO skb
 *
 * skb_gso_network_seglen is used to determine the real size of the
 * individual segments, including Layer3 (IP, IPv6) and L4 headers (TCP/UDP).
 *
 * The MAC/L2 header is not accounted for.
 */
static unsigned int skb_gso_network_seglen(const struct sk_buff *skb)
{
	unsigned int hdr_len = skb_transport_header(skb) -
			       skb_network_header(skb);

	return hdr_len + skb_gso_transport_seglen(skb);
}

/**
 * skb_gso_mac_seglen - Return length of individual segments of a gso packet
 *
 * @skb: GSO skb
 *
 * skb_gso_mac_seglen is used to determine the real size of the
 * individual segments, including MAC/L2, Layer3 (IP, IPv6) and L4
 * headers (TCP/UDP).
 */
static unsigned int skb_gso_mac_seglen(const struct sk_buff *skb)
{
	unsigned int hdr_len = skb_transport_header(skb) - skb_mac_header(skb);

	return hdr_len + skb_gso_transport_seglen(skb);
}

/**
 * skb_gso_size_check - check the skb size, considering GSO_BY_FRAGS
 *
 * There are a couple of instances where we have a GSO skb, and we
 * want to determine what size it would be after it is segmented.
 *
 * We might want to check:
 * -    L3+L4+payload size (e.g. IP forwarding)
 * - L2+L3+L4+payload size (e.g. sanity check before passing to driver)
 *
 * This is a helper to do that correctly considering GSO_BY_FRAGS.
 *
 * @skb: GSO skb
 *
 * @seg_len: The segmented length (from skb_gso_*_seglen). In the
 *           GSO_BY_FRAGS case this will be [header sizes + GSO_BY_FRAGS].
 *
 * @max_len: The maximum permissible length.
 *
 * Returns true if the segmented length <= max length.
 */
static inline bool skb_gso_size_check(const struct sk_buff *skb,
				      unsigned int seg_len,
				      unsigned int max_len) {
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	const struct sk_buff *iter;

	if (shinfo->gso_size != GSO_BY_FRAGS)
		return seg_len <= max_len;

	/* Undo this so we can re-use header sizes */
	seg_len -= GSO_BY_FRAGS;

	skb_walk_frags(skb, iter) {
		if (seg_len + skb_headlen(iter) > max_len)
			return false;
	}

	return true;
}

/**
 * skb_gso_validate_network_len - Will a split GSO skb fit into a given MTU?
 *
 * @skb: GSO skb
 * @mtu: MTU to validate against
 *
 * skb_gso_validate_network_len validates if a given skb will fit a
 * wanted MTU once split. It considers L3 headers, L4 headers, and the
 * payload.
 */
bool skb_gso_validate_network_len(const struct sk_buff *skb, unsigned int mtu)
{
	return skb_gso_size_check(skb, skb_gso_network_seglen(skb), mtu);
}
EXPORT_SYMBOL_GPL(skb_gso_validate_network_len);

/**
 * skb_gso_validate_mac_len - Will a split GSO skb fit in a given length?
 *
 * @skb: GSO skb
 * @len: length to validate against
 *
 * skb_gso_validate_mac_len validates if a given skb will fit a wanted
 * length once split, including L2, L3 and L4 headers and the payload.
 */
bool skb_gso_validate_mac_len(const struct sk_buff *skb, unsigned int len)
{
	return skb_gso_size_check(skb, skb_gso_mac_seglen(skb), len);
}
EXPORT_SYMBOL_GPL(skb_gso_validate_mac_len);

static struct sk_buff *skb_reorder_vlan_header(struct sk_buff *skb)
{
	int mac_len, meta_len;
	void *meta;

	if (skb_cow(skb, skb_headroom(skb)) < 0) {
		kfree_skb(skb);
		return NULL;
	}

	mac_len = skb->data - skb_mac_header(skb);
	if (likely(mac_len > VLAN_HLEN + ETH_TLEN)) {
		memmove(skb_mac_header(skb) + VLAN_HLEN, skb_mac_header(skb),
			mac_len - VLAN_HLEN - ETH_TLEN);
	}

	meta_len = skb_metadata_len(skb);
	if (meta_len) {
		meta = skb_metadata_end(skb) - meta_len;
		memmove(meta + VLAN_HLEN, meta, meta_len);
	}

	skb->mac_header += VLAN_HLEN;
	return skb;
}

struct sk_buff *skb_vlan_untag(struct sk_buff *skb)
{
	struct vlan_hdr *vhdr;
	u16 vlan_tci;

	if (unlikely(skb_vlan_tag_present(skb))) {
		/* vlan_tci is already set-up so leave this for another time */
		return skb;
	}

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		goto err_free;
	/* We may access the two bytes after vlan_hdr in vlan_set_encap_proto(). */
	if (unlikely(!pskb_may_pull(skb, VLAN_HLEN + sizeof(unsigned short))))
		goto err_free;

	vhdr = (struct vlan_hdr *)skb->data;
	vlan_tci = ntohs(vhdr->h_vlan_TCI);
	__vlan_hwaccel_put_tag(skb, skb->protocol, vlan_tci);

	skb_pull_rcsum(skb, VLAN_HLEN);
	vlan_set_encap_proto(skb, vhdr);

	skb = skb_reorder_vlan_header(skb);
	if (unlikely(!skb))
		goto err_free;

	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_reset_mac_len(skb);

	return skb;

err_free:
	kfree_skb(skb);
	return NULL;
}
EXPORT_SYMBOL(skb_vlan_untag);

int skb_ensure_writable(struct sk_buff *skb, int write_len)
{
	if (!pskb_may_pull(skb, write_len))
		return -ENOMEM;

	if (!skb_cloned(skb) || skb_clone_writable(skb, write_len))
		return 0;

	return pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
}
EXPORT_SYMBOL(skb_ensure_writable);

/* remove VLAN header from packet and update csum accordingly.
 * expects a non skb_vlan_tag_present skb with a vlan tag payload
 */
int __skb_vlan_pop(struct sk_buff *skb, u16 *vlan_tci)
{
	struct vlan_hdr *vhdr;
	int offset = skb->data - skb_mac_header(skb);
	int err;

	if (WARN_ONCE(offset,
		      "__skb_vlan_pop got skb with skb->data not at mac header (offset %d)\n",
		      offset)) {
		return -EINVAL;
	}

	err = skb_ensure_writable(skb, VLAN_ETH_HLEN);
	if (unlikely(err))
		return err;

	skb_postpull_rcsum(skb, skb->data + (2 * ETH_ALEN), VLAN_HLEN);

	vhdr = (struct vlan_hdr *)(skb->data + ETH_HLEN);
	*vlan_tci = ntohs(vhdr->h_vlan_TCI);

	memmove(skb->data + VLAN_HLEN, skb->data, 2 * ETH_ALEN);
	__skb_pull(skb, VLAN_HLEN);

	vlan_set_encap_proto(skb, vhdr);
	skb->mac_header += VLAN_HLEN;

	if (skb_network_offset(skb) < ETH_HLEN)
		skb_set_network_header(skb, ETH_HLEN);

	skb_reset_mac_len(skb);

	return err;
}
EXPORT_SYMBOL(__skb_vlan_pop);

/* Pop a vlan tag either from hwaccel or from payload.
 * Expects skb->data at mac header.
 */
int skb_vlan_pop(struct sk_buff *skb)
{
	u16 vlan_tci;
	__be16 vlan_proto;
	int err;

	if (likely(skb_vlan_tag_present(skb))) {
		__vlan_hwaccel_clear_tag(skb);
	} else {
		if (unlikely(!eth_type_vlan(skb->protocol)))
			return 0;

		err = __skb_vlan_pop(skb, &vlan_tci);
		if (err)
			return err;
	}
	/* move next vlan tag to hw accel tag */
	if (likely(!eth_type_vlan(skb->protocol)))
		return 0;

	vlan_proto = skb->protocol;
	err = __skb_vlan_pop(skb, &vlan_tci);
	if (unlikely(err))
		return err;

	__vlan_hwaccel_put_tag(skb, vlan_proto, vlan_tci);
	return 0;
}
EXPORT_SYMBOL(skb_vlan_pop);

/* Push a vlan tag either into hwaccel or into payload (if hwaccel tag present).
 * Expects skb->data at mac header.
 */
int skb_vlan_push(struct sk_buff *skb, __be16 vlan_proto, u16 vlan_tci)
{
	if (skb_vlan_tag_present(skb)) {
		int offset = skb->data - skb_mac_header(skb);
		int err;

		if (WARN_ONCE(offset,
			      "skb_vlan_push got skb with skb->data not at mac header (offset %d)\n",
			      offset)) {
			return -EINVAL;
		}

		err = __vlan_insert_tag(skb, skb->vlan_proto,
					skb_vlan_tag_get(skb));
		if (err)
			return err;

		skb->protocol = skb->vlan_proto;
		skb->mac_len += VLAN_HLEN;

		skb_postpush_rcsum(skb, skb->data + (2 * ETH_ALEN), VLAN_HLEN);
	}
	__vlan_hwaccel_put_tag(skb, vlan_proto, vlan_tci);
	return 0;
}
EXPORT_SYMBOL(skb_vlan_push);

/**
 * skb_eth_pop() - Drop the Ethernet header at the head of a packet
 *
 * @skb: Socket buffer to modify
 *
 * Drop the Ethernet header of @skb.
 *
 * Expects that skb->data points to the mac header and that no VLAN tags are
 * present.
 *
 * Returns 0 on success, -errno otherwise.
 */
int skb_eth_pop(struct sk_buff *skb)
{
	if (!pskb_may_pull(skb, ETH_HLEN) || skb_vlan_tagged(skb) ||
	    skb_network_offset(skb) < ETH_HLEN)
		return -EPROTO;

	skb_pull_rcsum(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	skb_reset_mac_len(skb);

	return 0;
}
EXPORT_SYMBOL(skb_eth_pop);

/**
 * skb_eth_push() - Add a new Ethernet header at the head of a packet
 *
 * @skb: Socket buffer to modify
 * @dst: Destination MAC address of the new header
 * @src: Source MAC address of the new header
 *
 * Prepend @skb with a new Ethernet header.
 *
 * Expects that skb->data points to the mac header, which must be empty.
 *
 * Returns 0 on success, -errno otherwise.
 */
int skb_eth_push(struct sk_buff *skb, const unsigned char *dst,
		 const unsigned char *src)
{
	struct ethhdr *eth;
	int err;

	if (skb_network_offset(skb) || skb_vlan_tag_present(skb))
		return -EPROTO;

	err = skb_cow_head(skb, sizeof(*eth));
	if (err < 0)
		return err;

	skb_push(skb, sizeof(*eth));
	skb_reset_mac_header(skb);
	skb_reset_mac_len(skb);

	eth = eth_hdr(skb);
	ether_addr_copy(eth->h_dest, dst);
	ether_addr_copy(eth->h_source, src);
	eth->h_proto = skb->protocol;

	skb_postpush_rcsum(skb, eth, sizeof(*eth));

	return 0;
}
EXPORT_SYMBOL(skb_eth_push);

/* Update the ethertype of hdr and the skb csum value if required. */
static void skb_mod_eth_type(struct sk_buff *skb, struct ethhdr *hdr,
			     __be16 ethertype)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		__be16 diff[] = { ~hdr->h_proto, ethertype };

		skb->csum = csum_partial((char *)diff, sizeof(diff), skb->csum);
	}

	hdr->h_proto = ethertype;
}

/**
 * skb_mpls_push() - push a new MPLS header after mac_len bytes from start of
 *                   the packet
 *
 * @skb: buffer
 * @mpls_lse: MPLS label stack entry to push
 * @mpls_proto: ethertype of the new MPLS header (expects 0x8847 or 0x8848)
 * @mac_len: length of the MAC header
 * @ethernet: flag to indicate if the resulting packet after skb_mpls_push is
 *            ethernet
 *
 * Expects skb->data at mac header.
 *
 * Returns 0 on success, -errno otherwise.
 */
int skb_mpls_push(struct sk_buff *skb, __be32 mpls_lse, __be16 mpls_proto,
		  int mac_len, bool ethernet)
{
	struct mpls_shim_hdr *lse;
	int err;

	if (unlikely(!eth_p_mpls(mpls_proto)))
		return -EINVAL;

	/* Networking stack does not allow simultaneous Tunnel and MPLS GSO. */
	if (skb->encapsulation)
		return -EINVAL;

	err = skb_cow_head(skb, MPLS_HLEN);
	if (unlikely(err))
		return err;

	if (!skb->inner_protocol) {
		skb_set_inner_network_header(skb, skb_network_offset(skb));
		skb_set_inner_protocol(skb, skb->protocol);
	}

	skb_push(skb, MPLS_HLEN);
	memmove(skb_mac_header(skb) - MPLS_HLEN, skb_mac_header(skb),
		mac_len);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, mac_len);
	skb_reset_mac_len(skb);

	lse = mpls_hdr(skb);
	lse->label_stack_entry = mpls_lse;
	skb_postpush_rcsum(skb, lse, MPLS_HLEN);

	if (ethernet && mac_len >= ETH_HLEN)
		skb_mod_eth_type(skb, eth_hdr(skb), mpls_proto);
	skb->protocol = mpls_proto;

	return 0;
}
EXPORT_SYMBOL_GPL(skb_mpls_push);

/**
 * skb_mpls_pop() - pop the outermost MPLS header
 *
 * @skb: buffer
 * @next_proto: ethertype of header after popped MPLS header
 * @mac_len: length of the MAC header
 * @ethernet: flag to indicate if the packet is ethernet
 *
 * Expects skb->data at mac header.
 *
 * Returns 0 on success, -errno otherwise.
 */
int skb_mpls_pop(struct sk_buff *skb, __be16 next_proto, int mac_len,
		 bool ethernet)
{
	int err;

	if (unlikely(!eth_p_mpls(skb->protocol)))
		return 0;

	err = skb_ensure_writable(skb, mac_len + MPLS_HLEN);
	if (unlikely(err))
		return err;

	skb_postpull_rcsum(skb, mpls_hdr(skb), MPLS_HLEN);
	memmove(skb_mac_header(skb) + MPLS_HLEN, skb_mac_header(skb),
		mac_len);

	__skb_pull(skb, MPLS_HLEN);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, mac_len);

	if (ethernet && mac_len >= ETH_HLEN) {
		struct ethhdr *hdr;

		/* use mpls_hdr() to get ethertype to account for VLANs. */
		hdr = (struct ethhdr *)((void *)mpls_hdr(skb) - ETH_HLEN);
		skb_mod_eth_type(skb, hdr, next_proto);
	}
	skb->protocol = next_proto;

	return 0;
}
EXPORT_SYMBOL_GPL(skb_mpls_pop);

/**
 * skb_mpls_update_lse() - modify outermost MPLS header and update csum
 *
 * @skb: buffer
 * @mpls_lse: new MPLS label stack entry to update to
 *
 * Expects skb->data at mac header.
 *
 * Returns 0 on success, -errno otherwise.
 */
int skb_mpls_update_lse(struct sk_buff *skb, __be32 mpls_lse)
{
	int err;

	if (unlikely(!eth_p_mpls(skb->protocol)))
		return -EINVAL;

	err = skb_ensure_writable(skb, skb->mac_len + MPLS_HLEN);
	if (unlikely(err))
		return err;

	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		__be32 diff[] = { ~mpls_hdr(skb)->label_stack_entry, mpls_lse };

		skb->csum = csum_partial((char *)diff, sizeof(diff), skb->csum);
	}

	mpls_hdr(skb)->label_stack_entry = mpls_lse;

	return 0;
}
EXPORT_SYMBOL_GPL(skb_mpls_update_lse);

/**
 * skb_mpls_dec_ttl() - decrement the TTL of the outermost MPLS header
 *
 * @skb: buffer
 *
 * Expects skb->data at mac header.
 *
 * Returns 0 on success, -errno otherwise.
 */
int skb_mpls_dec_ttl(struct sk_buff *skb)
{
	u32 lse;
	u8 ttl;

	if (unlikely(!eth_p_mpls(skb->protocol)))
		return -EINVAL;

	if (!pskb_may_pull(skb, skb_network_offset(skb) + MPLS_HLEN))
		return -ENOMEM;

	lse = be32_to_cpu(mpls_hdr(skb)->label_stack_entry);
	ttl = (lse & MPLS_LS_TTL_MASK) >> MPLS_LS_TTL_SHIFT;
	if (!--ttl)
		return -EINVAL;

	lse &= ~MPLS_LS_TTL_MASK;
	lse |= ttl << MPLS_LS_TTL_SHIFT;

	return skb_mpls_update_lse(skb, cpu_to_be32(lse));
}
EXPORT_SYMBOL_GPL(skb_mpls_dec_ttl);

/**
 * alloc_skb_with_frags - allocate skb with page frags
 *
 * @header_len: size of linear part
 * @data_len: needed length in frags
 * @max_page_order: max page order desired.
 * @errcode: pointer to error code if any
 * @gfp_mask: allocation mask
 *
 * This can be used to allocate a paged skb, given a maximal order for frags.
 */
struct sk_buff *alloc_skb_with_frags(unsigned long header_len,
				     unsigned long data_len,
				     int max_page_order,
				     int *errcode,
				     gfp_t gfp_mask)
{
	int npages = (data_len + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	unsigned long chunk;
	struct sk_buff *skb;
	struct page *page;
	int i;

	*errcode = -EMSGSIZE;
	/* Note this test could be relaxed, if we succeed to allocate
	 * high order pages...
	 */
	if (npages > MAX_SKB_FRAGS)
		return NULL;

	*errcode = -ENOBUFS;
	skb = alloc_skb(header_len, gfp_mask);
	if (!skb)
		return NULL;

	skb->truesize += npages << PAGE_SHIFT;

	for (i = 0; npages > 0; i++) {
		int order = max_page_order;

		while (order) {
			if (npages >= 1 << order) {
				page = alloc_pages((gfp_mask & ~__GFP_DIRECT_RECLAIM) |
						   __GFP_COMP |
						   __GFP_NOWARN,
						   order);
				if (page)
					goto fill_page;
				/* Do not retry other high order allocations */
				order = 1;
				max_page_order = 0;
			}
			order--;
		}
		page = alloc_page(gfp_mask);
		if (!page)
			goto failure;
fill_page:
		chunk = min_t(unsigned long, data_len,
			      PAGE_SIZE << order);
		skb_fill_page_desc(skb, i, page, 0, chunk);
		data_len -= chunk;
		npages -= 1 << order;
	}
	return skb;

failure:
	kfree_skb(skb);
	return NULL;
}
EXPORT_SYMBOL(alloc_skb_with_frags);

/* carve out the first off bytes from skb when off < headlen */
static int pskb_carve_inside_header(struct sk_buff *skb, const u32 off,
				    const int headlen, gfp_t gfp_mask)
{
	int i;
	int size = skb_end_offset(skb);
	int new_hlen = headlen - off;
	u8 *data;

	size = SKB_DATA_ALIGN(size);

	if (skb_pfmemalloc(skb))
		gfp_mask |= __GFP_MEMALLOC;
	data = kmalloc_reserve(size +
			       SKB_DATA_ALIGN(sizeof(struct skb_shared_info)),
			       gfp_mask, NUMA_NO_NODE, NULL);
	if (!data)
		return -ENOMEM;

	size = SKB_WITH_OVERHEAD(ksize(data));

	/* Copy real data, and all frags */
	skb_copy_from_linear_data_offset(skb, off, data, new_hlen);
	skb->len -= off;

	memcpy((struct skb_shared_info *)(data + size),
	       skb_shinfo(skb),
	       offsetof(struct skb_shared_info,
			frags[skb_shinfo(skb)->nr_frags]));
	if (skb_cloned(skb)) {
		/* drop the old head gracefully */
		if (skb_orphan_frags(skb, gfp_mask)) {
			kfree(data);
			return -ENOMEM;
		}
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
			skb_frag_ref(skb, i);
		if (skb_has_frag_list(skb))
			skb_clone_fraglist(skb);
		skb_release_data(skb);
	} else {
		/* we can reuse existing recount- all we did was
		 * relocate values
		 */
		skb_free_head(skb);
	}

	skb->head = data;
	skb->data = data;
	skb->head_frag = 0;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
	skb->end = size;
#else
	skb->end = skb->head + size;
#endif
	skb_set_tail_pointer(skb, skb_headlen(skb));
	skb_headers_offset_update(skb, 0);
	skb->cloned = 0;
	skb->hdr_len = 0;
	skb->nohdr = 0;
	atomic_set(&skb_shinfo(skb)->dataref, 1);

	return 0;
}

static int pskb_carve(struct sk_buff *skb, const u32 off, gfp_t gfp);

/* carve out the first eat bytes from skb's frag_list. May recurse into
 * pskb_carve()
 */
static int pskb_carve_frag_list(struct sk_buff *skb,
				struct skb_shared_info *shinfo, int eat,
				gfp_t gfp_mask)
{
	struct sk_buff *list = shinfo->frag_list;
	struct sk_buff *clone = NULL;
	struct sk_buff *insp = NULL;

	do {
		if (!list) {
			pr_err("Not enough bytes to eat. Want %d\n", eat);
			return -EFAULT;
		}
		if (list->len <= eat) {
			/* Eaten as whole. */
			eat -= list->len;
			list = list->next;
			insp = list;
		} else {
			/* Eaten partially. */
			if (skb_shared(list)) {
				clone = skb_clone(list, gfp_mask);
				if (!clone)
					return -ENOMEM;
				insp = list->next;
				list = clone;
			} else {
				/* This may be pulled without problems. */
				insp = list;
			}
			if (pskb_carve(list, eat, gfp_mask) < 0) {
				kfree_skb(clone);
				return -ENOMEM;
			}
			break;
		}
	} while (eat);

	/* Free pulled out fragments. */
	while ((list = shinfo->frag_list) != insp) {
		shinfo->frag_list = list->next;
		consume_skb(list);
	}
	/* And insert new clone at head. */
	if (clone) {
		clone->next = list;
		shinfo->frag_list = clone;
	}
	return 0;
}

/* carve off first len bytes from skb. Split line (off) is in the
 * non-linear part of skb
 */
static int pskb_carve_inside_nonlinear(struct sk_buff *skb, const u32 off,
				       int pos, gfp_t gfp_mask)
{
	int i, k = 0;
	int size = skb_end_offset(skb);
	u8 *data;
	const int nfrags = skb_shinfo(skb)->nr_frags;
	struct skb_shared_info *shinfo;

	size = SKB_DATA_ALIGN(size);

	if (skb_pfmemalloc(skb))
		gfp_mask |= __GFP_MEMALLOC;
	data = kmalloc_reserve(size +
			       SKB_DATA_ALIGN(sizeof(struct skb_shared_info)),
			       gfp_mask, NUMA_NO_NODE, NULL);
	if (!data)
		return -ENOMEM;

	size = SKB_WITH_OVERHEAD(ksize(data));

	memcpy((struct skb_shared_info *)(data + size),
	       skb_shinfo(skb), offsetof(struct skb_shared_info, frags[0]));
	if (skb_orphan_frags(skb, gfp_mask)) {
		kfree(data);
		return -ENOMEM;
	}
	shinfo = (struct skb_shared_info *)(data + size);
	for (i = 0; i < nfrags; i++) {
		int fsize = skb_frag_size(&skb_shinfo(skb)->frags[i]);

		if (pos + fsize > off) {
			shinfo->frags[k] = skb_shinfo(skb)->frags[i];

			if (pos < off) {
				/* Split frag.
				 * We have two variants in this case:
				 * 1. Move all the frag to the second
				 *    part, if it is possible. F.e.
				 *    this approach is mandatory for TUX,
				 *    where splitting is expensive.
				 * 2. Split is accurately. We make this.
				 */
				skb_frag_off_add(&shinfo->frags[0], off - pos);
				skb_frag_size_sub(&shinfo->frags[0], off - pos);
			}
			skb_frag_ref(skb, i);
			k++;
		}
		pos += fsize;
	}
	shinfo->nr_frags = k;
	if (skb_has_frag_list(skb))
		skb_clone_fraglist(skb);

	/* split line is in frag list */
	if (k == 0 && pskb_carve_frag_list(skb, shinfo, off - pos, gfp_mask)) {
		/* skb_frag_unref() is not needed here as shinfo->nr_frags = 0. */
		if (skb_has_frag_list(skb))
			kfree_skb_list(skb_shinfo(skb)->frag_list);
		kfree(data);
		return -ENOMEM;
	}
	skb_release_data(skb);

	skb->head = data;
	skb->head_frag = 0;
	skb->data = data;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
	skb->end = size;
#else
	skb->end = skb->head + size;
#endif
	skb_reset_tail_pointer(skb);
	skb_headers_offset_update(skb, 0);
	skb->cloned   = 0;
	skb->hdr_len  = 0;
	skb->nohdr    = 0;
	skb->len -= off;
	skb->data_len = skb->len;
	atomic_set(&skb_shinfo(skb)->dataref, 1);
	return 0;
}

/* remove len bytes from the beginning of the skb */
static int pskb_carve(struct sk_buff *skb, const u32 len, gfp_t gfp)
{
	int headlen = skb_headlen(skb);

	if (len < headlen)
		return pskb_carve_inside_header(skb, len, headlen, gfp);
	else
		return pskb_carve_inside_nonlinear(skb, len, headlen, gfp);
}

/* Extract to_copy bytes starting at off from skb, and return this in
 * a new skb
 */
struct sk_buff *pskb_extract(struct sk_buff *skb, int off,
			     int to_copy, gfp_t gfp)
{
	struct sk_buff  *clone = skb_clone(skb, gfp);

	if (!clone)
		return NULL;

	if (pskb_carve(clone, off, gfp) < 0 ||
	    pskb_trim(clone, to_copy)) {
		kfree_skb(clone);
		return NULL;
	}
	return clone;
}
EXPORT_SYMBOL(pskb_extract);

/**
 * skb_condense - try to get rid of fragments/frag_list if possible
 * @skb: buffer
 *
 * Can be used to save memory before skb is added to a busy queue.
 * If packet has bytes in frags and enough tail room in skb->head,
 * pull all of them, so that we can free the frags right now and adjust
 * truesize.
 * Notes:
 *	We do not reallocate skb->head thus can not fail.
 *	Caller must re-evaluate skb->truesize if needed.
 */
void skb_condense(struct sk_buff *skb)
{
	if (skb->data_len) {
		if (skb->data_len > skb->end - skb->tail ||
		    skb_cloned(skb))
			return;

		/* Nice, we can free page frag(s) right now */
		__pskb_pull_tail(skb, skb->data_len);
	}
	/* At this point, skb->truesize might be over estimated,
	 * because skb had a fragment, and fragments do not tell
	 * their truesize.
	 * When we pulled its content into skb->head, fragment
	 * was freed, but __pskb_pull_tail() could not possibly
	 * adjust skb->truesize, not knowing the frag truesize.
	 */
	skb->truesize = SKB_TRUESIZE(skb_end_offset(skb));
}

#ifdef CONFIG_SKB_EXTENSIONS
static void *skb_ext_get_ptr(struct skb_ext *ext, enum skb_ext_id id)
{
	return (void *)ext + (ext->offset[id] * SKB_EXT_ALIGN_VALUE);
}

/**
 * __skb_ext_alloc - allocate a new skb extensions storage
 *
 * @flags: See kmalloc().
 *
 * Returns the newly allocated pointer. The pointer can later attached to a
 * skb via __skb_ext_set().
 * Note: caller must handle the skb_ext as an opaque data.
 */
struct skb_ext *__skb_ext_alloc(gfp_t flags)
{
	struct skb_ext *new = kmem_cache_alloc(skbuff_ext_cache, flags);

	if (new) {
		memset(new->offset, 0, sizeof(new->offset));
		refcount_set(&new->refcnt, 1);
	}

	return new;
}

static struct skb_ext *skb_ext_maybe_cow(struct skb_ext *old,
					 unsigned int old_active)
{
	struct skb_ext *new;

	if (refcount_read(&old->refcnt) == 1)
		return old;

	new = kmem_cache_alloc(skbuff_ext_cache, GFP_ATOMIC);
	if (!new)
		return NULL;

	memcpy(new, old, old->chunks * SKB_EXT_ALIGN_VALUE);
	refcount_set(&new->refcnt, 1);

#ifdef CONFIG_XFRM
	if (old_active & (1 << SKB_EXT_SEC_PATH)) {
		struct sec_path *sp = skb_ext_get_ptr(old, SKB_EXT_SEC_PATH);
		unsigned int i;

		for (i = 0; i < sp->len; i++)
			xfrm_state_hold(sp->xvec[i]);
	}
#endif
	__skb_ext_put(old);
	return new;
}

/**
 * __skb_ext_set - attach the specified extension storage to this skb
 * @skb: buffer
 * @id: extension id
 * @ext: extension storage previously allocated via __skb_ext_alloc()
 *
 * Existing extensions, if any, are cleared.
 *
 * Returns the pointer to the extension.
 */
void *__skb_ext_set(struct sk_buff *skb, enum skb_ext_id id,
		    struct skb_ext *ext)
{
	unsigned int newlen, newoff = SKB_EXT_CHUNKSIZEOF(*ext);

	skb_ext_put(skb);
	newlen = newoff + skb_ext_type_len[id];
	ext->chunks = newlen;
	ext->offset[id] = newoff;
	skb->extensions = ext;
	skb->active_extensions = 1 << id;
	return skb_ext_get_ptr(ext, id);
}

/**
 * skb_ext_add - allocate space for given extension, COW if needed
 * @skb: buffer
 * @id: extension to allocate space for
 *
 * Allocates enough space for the given extension.
 * If the extension is already present, a pointer to that extension
 * is returned.
 *
 * If the skb was cloned, COW applies and the returned memory can be
 * modified without changing the extension space of clones buffers.
 *
 * Returns pointer to the extension or NULL on allocation failure.
 */
void *skb_ext_add(struct sk_buff *skb, enum skb_ext_id id)
{
	struct skb_ext *new, *old = NULL;
	unsigned int newlen, newoff;

	if (skb->active_extensions) {
		old = skb->extensions;

		new = skb_ext_maybe_cow(old, skb->active_extensions);
		if (!new)
			return NULL;

		if (__skb_ext_exist(new, id))
			goto set_active;

		newoff = new->chunks;
	} else {
		newoff = SKB_EXT_CHUNKSIZEOF(*new);

		new = __skb_ext_alloc(GFP_ATOMIC);
		if (!new)
			return NULL;
	}

	newlen = newoff + skb_ext_type_len[id];
	new->chunks = newlen;
	new->offset[id] = newoff;
set_active:
	skb->extensions = new;
	skb->active_extensions |= 1 << id;
	return skb_ext_get_ptr(new, id);
}
EXPORT_SYMBOL(skb_ext_add);

#ifdef CONFIG_XFRM
static void skb_ext_put_sp(struct sec_path *sp)
{
	unsigned int i;

	for (i = 0; i < sp->len; i++)
		xfrm_state_put(sp->xvec[i]);
}
#endif

void __skb_ext_del(struct sk_buff *skb, enum skb_ext_id id)
{
	struct skb_ext *ext = skb->extensions;

	skb->active_extensions &= ~(1 << id);
	if (skb->active_extensions == 0) {
		skb->extensions = NULL;
		__skb_ext_put(ext);
#ifdef CONFIG_XFRM
	} else if (id == SKB_EXT_SEC_PATH &&
		   refcount_read(&ext->refcnt) == 1) {
		struct sec_path *sp = skb_ext_get_ptr(ext, SKB_EXT_SEC_PATH);

		skb_ext_put_sp(sp);
		sp->len = 0;
#endif
	}
}
EXPORT_SYMBOL(__skb_ext_del);

void __skb_ext_put(struct skb_ext *ext)
{
	/* If this is last clone, nothing can increment
	 * it after check passes.  Avoids one atomic op.
	 */
	if (refcount_read(&ext->refcnt) == 1)
		goto free_now;

	if (!refcount_dec_and_test(&ext->refcnt))
		return;
free_now:
#ifdef CONFIG_XFRM
	if (__skb_ext_exist(ext, SKB_EXT_SEC_PATH))
		skb_ext_put_sp(skb_ext_get_ptr(ext, SKB_EXT_SEC_PATH));
#endif

	kmem_cache_free(skbuff_ext_cache, ext);
}
EXPORT_SYMBOL(__skb_ext_put);
#endif /* CONFIG_SKB_EXTENSIONS */
