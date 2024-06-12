/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Skb ref helpers.
 *
 */

#ifndef _LINUX_SKBUFF_REF_H
#define _LINUX_SKBUFF_REF_H

#include <linux/skbuff.h>

/**
 * __skb_frag_ref - take an addition reference on a paged fragment.
 * @frag: the paged fragment
 *
 * Takes an additional reference on the paged fragment @frag.
 */
static inline void __skb_frag_ref(skb_frag_t *frag)
{
	get_page(skb_frag_page(frag));
}

/**
 * skb_frag_ref - take an addition reference on a paged fragment of an skb.
 * @skb: the buffer
 * @f: the fragment offset.
 *
 * Takes an additional reference on the @f'th paged fragment of @skb.
 */
static inline void skb_frag_ref(struct sk_buff *skb, int f)
{
	__skb_frag_ref(&skb_shinfo(skb)->frags[f]);
}

bool napi_pp_put_page(struct page *page);

static inline void
skb_page_unref(struct page *page, bool recycle)
{
#ifdef CONFIG_PAGE_POOL
	if (recycle && napi_pp_put_page(page))
		return;
#endif
	put_page(page);
}

/**
 * __skb_frag_unref - release a reference on a paged fragment.
 * @frag: the paged fragment
 * @recycle: recycle the page if allocated via page_pool
 *
 * Releases a reference on the paged fragment @frag
 * or recycles the page via the page_pool API.
 */
static inline void __skb_frag_unref(skb_frag_t *frag, bool recycle)
{
	skb_page_unref(skb_frag_page(frag), recycle);
}

/**
 * skb_frag_unref - release a reference on a paged fragment of an skb.
 * @skb: the buffer
 * @f: the fragment offset
 *
 * Releases a reference on the @f'th paged fragment of @skb.
 */
static inline void skb_frag_unref(struct sk_buff *skb, int f)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);

	if (!skb_zcopy_managed(skb))
		__skb_frag_unref(&shinfo->frags[f], skb->pp_recycle);
}

#endif	/* _LINUX_SKBUFF_REF_H */
