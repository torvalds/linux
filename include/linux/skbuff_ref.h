/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Skb ref helpers.
 *
 */

#ifndef _LINUX_SKBUFF_REF_H
#define _LINUX_SKBUFF_REF_H

#include <linux/skbuff.h>

bool napi_pp_get_page(netmem_ref netmem);

/**
 * __skb_frag_ref - take an addition reference on a paged fragment.
 * @frag: the paged fragment
 *
 * Takes an additional reference on the paged fragment @frag.
 */
static __always_inline void __skb_frag_ref(skb_frag_t *frag)
{
	get_netmem(skb_frag_netmem(frag));
}

/**
 * skb_frag_ref - take an addition reference on a paged fragment of an skb.
 * @skb: the buffer
 * @f: the fragment offset.
 *
 * Takes an additional reference on the @f'th paged fragment of @skb.
 */
static __always_inline void skb_frag_ref(struct sk_buff *skb, int f)
{
	__skb_frag_ref(&skb_shinfo(skb)->frags[f]);
}

bool napi_pp_put_page(netmem_ref netmem);

/**
 * skb_netmem_unref - release a fragment reference on a netmem
 * @netmem: netmem to unreference
 * @recycle: whether page_pool recycling is enabled (e.g. skb->pp_recycle)
 *
 * Drops pp_ref_count via napi_pp_put_page() if @recycle is true and @netmem
 * belongs to a page_pool; otherwise drops a non-pp backing reference via
 * put_netmem(). Counterpart to skb_netmem_ref().
 */
static __always_inline void skb_netmem_unref(netmem_ref netmem, bool recycle)
{
#ifdef CONFIG_PAGE_POOL
	if (recycle && napi_pp_put_page(netmem))
		return;
#endif
	put_netmem(netmem);
}

/**
 * skb_frag_unref - release a reference on a paged fragment of an skb
 * @skb: the buffer
 * @f: the fragment offset
 *
 * Drops a reference on the @f'th fragment of @skb via skb_netmem_unref()
 * (pp_ref_count if skb->pp_recycle and page_pool-backed, else non-pp
 * backing refcount), skipping managed zerocopy fragments. Counterpart to
 * skb_frag_ref().
 */
static __always_inline void skb_frag_unref(struct sk_buff *skb, int f)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);

	if (!skb_zcopy_managed(skb))
		skb_netmem_unref(skb_frag_netmem(&shinfo->frags[f]),
				 skb->pp_recycle);
}

#endif	/* _LINUX_SKBUFF_REF_H */
