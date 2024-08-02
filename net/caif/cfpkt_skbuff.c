// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <net/caif/cfpkt.h>

#define PKT_PREFIX  48
#define PKT_POSTFIX 2
#define PKT_LEN_WHEN_EXTENDING 128
#define PKT_ERROR(pkt, errmsg)		   \
do {					   \
	cfpkt_priv(pkt)->erronous = true;  \
	skb_reset_tail_pointer(&pkt->skb); \
	pr_warn(errmsg);		   \
} while (0)

/*
 * net/caif/ is generic and does not
 * understand SKB, so we do this typecast
 */
struct cfpkt {
	struct sk_buff skb;
};

/* Private data inside SKB */
struct cfpkt_priv_data {
	struct dev_info dev_info;
	bool erronous;
};

static inline struct cfpkt_priv_data *cfpkt_priv(struct cfpkt *pkt)
{
	return (struct cfpkt_priv_data *) pkt->skb.cb;
}

static inline bool is_erronous(struct cfpkt *pkt)
{
	return cfpkt_priv(pkt)->erronous;
}

static inline struct sk_buff *pkt_to_skb(struct cfpkt *pkt)
{
	return &pkt->skb;
}

static inline struct cfpkt *skb_to_pkt(struct sk_buff *skb)
{
	return (struct cfpkt *) skb;
}

struct cfpkt *cfpkt_fromnative(enum caif_direction dir, void *nativepkt)
{
	struct cfpkt *pkt = skb_to_pkt(nativepkt);
	cfpkt_priv(pkt)->erronous = false;
	return pkt;
}
EXPORT_SYMBOL(cfpkt_fromnative);

void *cfpkt_tonative(struct cfpkt *pkt)
{
	return (void *) pkt;
}
EXPORT_SYMBOL(cfpkt_tonative);

static struct cfpkt *cfpkt_create_pfx(u16 len, u16 pfx)
{
	struct sk_buff *skb;

	skb = alloc_skb(len + pfx, GFP_ATOMIC);
	if (unlikely(skb == NULL))
		return NULL;

	skb_reserve(skb, pfx);
	return skb_to_pkt(skb);
}

inline struct cfpkt *cfpkt_create(u16 len)
{
	return cfpkt_create_pfx(len + PKT_POSTFIX, PKT_PREFIX);
}

void cfpkt_destroy(struct cfpkt *pkt)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	kfree_skb(skb);
}

inline bool cfpkt_more(struct cfpkt *pkt)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	return skb->len > 0;
}

int cfpkt_peek_head(struct cfpkt *pkt, void *data, u16 len)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	if (skb_headlen(skb) >= len) {
		memcpy(data, skb->data, len);
		return 0;
	}
	return !cfpkt_extr_head(pkt, data, len) &&
	    !cfpkt_add_head(pkt, data, len);
}

int cfpkt_extr_head(struct cfpkt *pkt, void *data, u16 len)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	u8 *from;
	if (unlikely(is_erronous(pkt)))
		return -EPROTO;

	if (unlikely(len > skb->len)) {
		PKT_ERROR(pkt, "read beyond end of packet\n");
		return -EPROTO;
	}

	if (unlikely(len > skb_headlen(skb))) {
		if (unlikely(skb_linearize(skb) != 0)) {
			PKT_ERROR(pkt, "linearize failed\n");
			return -EPROTO;
		}
	}
	from = skb_pull(skb, len);
	from -= len;
	if (data)
		memcpy(data, from, len);
	return 0;
}
EXPORT_SYMBOL(cfpkt_extr_head);

int cfpkt_extr_trail(struct cfpkt *pkt, void *dta, u16 len)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	u8 *data = dta;
	u8 *from;
	if (unlikely(is_erronous(pkt)))
		return -EPROTO;

	if (unlikely(skb_linearize(skb) != 0)) {
		PKT_ERROR(pkt, "linearize failed\n");
		return -EPROTO;
	}
	if (unlikely(skb->data + len > skb_tail_pointer(skb))) {
		PKT_ERROR(pkt, "read beyond end of packet\n");
		return -EPROTO;
	}
	from = skb_tail_pointer(skb) - len;
	skb_trim(skb, skb->len - len);
	memcpy(data, from, len);
	return 0;
}

int cfpkt_pad_trail(struct cfpkt *pkt, u16 len)
{
	return cfpkt_add_body(pkt, NULL, len);
}

int cfpkt_add_body(struct cfpkt *pkt, const void *data, u16 len)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	struct sk_buff *lastskb;
	u8 *to;
	u16 addlen = 0;


	if (unlikely(is_erronous(pkt)))
		return -EPROTO;

	lastskb = skb;

	/* Check whether we need to add space at the tail */
	if (unlikely(skb_tailroom(skb) < len)) {
		if (likely(len < PKT_LEN_WHEN_EXTENDING))
			addlen = PKT_LEN_WHEN_EXTENDING;
		else
			addlen = len;
	}

	/* Check whether we need to change the SKB before writing to the tail */
	if (unlikely((addlen > 0) || skb_cloned(skb) || skb_shared(skb))) {

		/* Make sure data is writable */
		if (unlikely(skb_cow_data(skb, addlen, &lastskb) < 0)) {
			PKT_ERROR(pkt, "cow failed\n");
			return -EPROTO;
		}
	}

	/* All set to put the last SKB and optionally write data there. */
	to = pskb_put(skb, lastskb, len);
	if (likely(data))
		memcpy(to, data, len);
	return 0;
}

inline int cfpkt_addbdy(struct cfpkt *pkt, u8 data)
{
	return cfpkt_add_body(pkt, &data, 1);
}

int cfpkt_add_head(struct cfpkt *pkt, const void *data2, u16 len)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	struct sk_buff *lastskb;
	u8 *to;
	const u8 *data = data2;
	int ret;
	if (unlikely(is_erronous(pkt)))
		return -EPROTO;
	if (unlikely(skb_headroom(skb) < len)) {
		PKT_ERROR(pkt, "no headroom\n");
		return -EPROTO;
	}

	/* Make sure data is writable */
	ret = skb_cow_data(skb, 0, &lastskb);
	if (unlikely(ret < 0)) {
		PKT_ERROR(pkt, "cow failed\n");
		return ret;
	}

	to = skb_push(skb, len);
	memcpy(to, data, len);
	return 0;
}
EXPORT_SYMBOL(cfpkt_add_head);

inline int cfpkt_add_trail(struct cfpkt *pkt, const void *data, u16 len)
{
	return cfpkt_add_body(pkt, data, len);
}

inline u16 cfpkt_getlen(struct cfpkt *pkt)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	return skb->len;
}

int cfpkt_iterate(struct cfpkt *pkt,
		  u16 (*iter_func)(u16, void *, u16),
		  u16 data)
{
	/*
	 * Don't care about the performance hit of linearizing,
	 * Checksum should not be used on high-speed interfaces anyway.
	 */
	if (unlikely(is_erronous(pkt)))
		return -EPROTO;
	if (unlikely(skb_linearize(&pkt->skb) != 0)) {
		PKT_ERROR(pkt, "linearize failed\n");
		return -EPROTO;
	}
	return iter_func(data, pkt->skb.data, cfpkt_getlen(pkt));
}

int cfpkt_setlen(struct cfpkt *pkt, u16 len)
{
	struct sk_buff *skb = pkt_to_skb(pkt);


	if (unlikely(is_erronous(pkt)))
		return -EPROTO;

	if (likely(len <= skb->len)) {
		if (unlikely(skb->data_len))
			___pskb_trim(skb, len);
		else
			skb_trim(skb, len);

		return cfpkt_getlen(pkt);
	}

	/* Need to expand SKB */
	if (unlikely(!cfpkt_pad_trail(pkt, len - skb->len)))
		PKT_ERROR(pkt, "skb_pad_trail failed\n");

	return cfpkt_getlen(pkt);
}

struct cfpkt *cfpkt_append(struct cfpkt *dstpkt,
			   struct cfpkt *addpkt,
			   u16 expectlen)
{
	struct sk_buff *dst = pkt_to_skb(dstpkt);
	struct sk_buff *add = pkt_to_skb(addpkt);
	u16 addlen = skb_headlen(add);
	u16 neededtailspace;
	struct sk_buff *tmp;
	u16 dstlen;
	u16 createlen;
	if (unlikely(is_erronous(dstpkt) || is_erronous(addpkt))) {
		return dstpkt;
	}
	if (expectlen > addlen)
		neededtailspace = expectlen;
	else
		neededtailspace = addlen;

	if (dst->tail + neededtailspace > dst->end) {
		/* Create a dumplicate of 'dst' with more tail space */
		struct cfpkt *tmppkt;
		dstlen = skb_headlen(dst);
		createlen = dstlen + neededtailspace;
		tmppkt = cfpkt_create(createlen + PKT_PREFIX + PKT_POSTFIX);
		if (tmppkt == NULL)
			return NULL;
		tmp = pkt_to_skb(tmppkt);
		skb_put_data(tmp, dst->data, dstlen);
		cfpkt_destroy(dstpkt);
		dst = tmp;
	}
	skb_put_data(dst, add->data, skb_headlen(add));
	cfpkt_destroy(addpkt);
	return skb_to_pkt(dst);
}

struct cfpkt *cfpkt_split(struct cfpkt *pkt, u16 pos)
{
	struct sk_buff *skb2;
	struct sk_buff *skb = pkt_to_skb(pkt);
	struct cfpkt *tmppkt;
	u8 *split = skb->data + pos;
	u16 len2nd = skb_tail_pointer(skb) - split;

	if (unlikely(is_erronous(pkt)))
		return NULL;

	if (skb->data + pos > skb_tail_pointer(skb)) {
		PKT_ERROR(pkt, "trying to split beyond end of packet\n");
		return NULL;
	}

	/* Create a new packet for the second part of the data */
	tmppkt = cfpkt_create_pfx(len2nd + PKT_PREFIX + PKT_POSTFIX,
				  PKT_PREFIX);
	if (tmppkt == NULL)
		return NULL;
	skb2 = pkt_to_skb(tmppkt);


	if (skb2 == NULL)
		return NULL;

	skb_put_data(skb2, split, len2nd);

	/* Reduce the length of the original packet */
	skb_trim(skb, pos);

	skb2->priority = skb->priority;
	return skb_to_pkt(skb2);
}

bool cfpkt_erroneous(struct cfpkt *pkt)
{
	return cfpkt_priv(pkt)->erronous;
}

struct caif_payload_info *cfpkt_info(struct cfpkt *pkt)
{
	return (struct caif_payload_info *)&pkt_to_skb(pkt)->cb;
}
EXPORT_SYMBOL(cfpkt_info);

void cfpkt_set_prio(struct cfpkt *pkt, int prio)
{
	pkt_to_skb(pkt)->priority = prio;
}
EXPORT_SYMBOL(cfpkt_set_prio);
