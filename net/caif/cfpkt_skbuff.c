/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/hardirq.h>
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

struct cfpktq {
	struct sk_buff_head head;
	atomic_t count;
	/* Lock protects count updates */
	spinlock_t lock;
};

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

inline struct cfpkt_priv_data *cfpkt_priv(struct cfpkt *pkt)
{
	return (struct cfpkt_priv_data *) pkt->skb.cb;
}

inline bool is_erronous(struct cfpkt *pkt)
{
	return cfpkt_priv(pkt)->erronous;
}

inline struct sk_buff *pkt_to_skb(struct cfpkt *pkt)
{
	return &pkt->skb;
}

inline struct cfpkt *skb_to_pkt(struct sk_buff *skb)
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

	if (likely(in_interrupt()))
		skb = alloc_skb(len + pfx, GFP_ATOMIC);
	else
		skb = alloc_skb(len + pfx, GFP_KERNEL);

	if (unlikely(skb == NULL))
		return NULL;

	skb_reserve(skb, pfx);
	return skb_to_pkt(skb);
}

inline struct cfpkt *cfpkt_create(u16 len)
{
	return cfpkt_create_pfx(len + PKT_POSTFIX, PKT_PREFIX);
}
EXPORT_SYMBOL(cfpkt_create);

void cfpkt_destroy(struct cfpkt *pkt)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	kfree_skb(skb);
}
EXPORT_SYMBOL(cfpkt_destroy);

inline bool cfpkt_more(struct cfpkt *pkt)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	return skb->len > 0;
}
EXPORT_SYMBOL(cfpkt_more);

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
EXPORT_SYMBOL(cfpkt_peek_head);

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
EXPORT_SYMBOL(cfpkt_extr_trail);

int cfpkt_pad_trail(struct cfpkt *pkt, u16 len)
{
	return cfpkt_add_body(pkt, NULL, len);
}
EXPORT_SYMBOL(cfpkt_pad_trail);

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
		/*
		 * Is the SKB non-linear after skb_cow_data()? If so, we are
		 * going to add data to the last SKB, so we need to adjust
		 * lengths of the top SKB.
		 */
		if (lastskb != skb) {
			pr_warn("Packet is non-linear\n");
			skb->len += len;
			skb->data_len += len;
		}
	}

	/* All set to put the last SKB and optionally write data there. */
	to = skb_put(lastskb, len);
	if (likely(data))
		memcpy(to, data, len);
	return 0;
}
EXPORT_SYMBOL(cfpkt_add_body);

inline int cfpkt_addbdy(struct cfpkt *pkt, u8 data)
{
	return cfpkt_add_body(pkt, &data, 1);
}
EXPORT_SYMBOL(cfpkt_addbdy);

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
EXPORT_SYMBOL(cfpkt_add_trail);

inline u16 cfpkt_getlen(struct cfpkt *pkt)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	return skb->len;
}
EXPORT_SYMBOL(cfpkt_getlen);

inline u16 cfpkt_iterate(struct cfpkt *pkt,
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
EXPORT_SYMBOL(cfpkt_iterate);

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
EXPORT_SYMBOL(cfpkt_setlen);

struct cfpkt *cfpkt_create_uplink(const unsigned char *data, unsigned int len)
{
	struct cfpkt *pkt = cfpkt_create_pfx(len + PKT_POSTFIX, PKT_PREFIX);
	if (!pkt)
		return NULL;
	if (unlikely(data != NULL))
		cfpkt_add_body(pkt, data, len);
	return pkt;
}
EXPORT_SYMBOL(cfpkt_create_uplink);

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
		skb_set_tail_pointer(tmp, dstlen);
		tmp->len = dstlen;
		memcpy(tmp->data, dst->data, dstlen);
		cfpkt_destroy(dstpkt);
		dst = tmp;
	}
	memcpy(skb_tail_pointer(dst), add->data, skb_headlen(add));
	cfpkt_destroy(addpkt);
	dst->tail += addlen;
	dst->len += addlen;
	return skb_to_pkt(dst);
}
EXPORT_SYMBOL(cfpkt_append);

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

	/* Reduce the length of the original packet */
	skb_set_tail_pointer(skb, pos);
	skb->len = pos;

	memcpy(skb2->data, split, len2nd);
	skb2->tail += len2nd;
	skb2->len += len2nd;
	return skb_to_pkt(skb2);
}
EXPORT_SYMBOL(cfpkt_split);

char *cfpkt_log_pkt(struct cfpkt *pkt, char *buf, int buflen)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	char *p = buf;
	int i;

	/*
	 * Sanity check buffer length, it needs to be at least as large as
	 * the header info: ~=50+ bytes
	 */
	if (buflen < 50)
		return NULL;

	snprintf(buf, buflen, "%s: pkt:%p len:%ld(%ld+%ld) {%ld,%ld} data: [",
		is_erronous(pkt) ? "ERRONOUS-SKB" :
		 (skb->data_len != 0 ? "COMPLEX-SKB" : "SKB"),
		 skb,
		 (long) skb->len,
		 (long) (skb_tail_pointer(skb) - skb->data),
		 (long) skb->data_len,
		 (long) (skb->data - skb->head),
		 (long) (skb_tail_pointer(skb) - skb->head));
	p = buf + strlen(buf);

	for (i = 0; i < skb_tail_pointer(skb) - skb->data && i < 300; i++) {
		if (p > buf + buflen - 10) {
			sprintf(p, "...");
			p = buf + strlen(buf);
			break;
		}
		sprintf(p, "%02x,", skb->data[i]);
		p = buf + strlen(buf);
	}
	sprintf(p, "]\n");
	return buf;
}
EXPORT_SYMBOL(cfpkt_log_pkt);

int cfpkt_raw_append(struct cfpkt *pkt, void **buf, unsigned int buflen)
{
	struct sk_buff *skb = pkt_to_skb(pkt);
	struct sk_buff *lastskb;

	caif_assert(buf != NULL);
	if (unlikely(is_erronous(pkt)))
		return -EPROTO;
	/* Make sure SKB is writable */
	if (unlikely(skb_cow_data(skb, 0, &lastskb) < 0)) {
		PKT_ERROR(pkt, "skb_cow_data failed\n");
		return -EPROTO;
	}

	if (unlikely(skb_linearize(skb) != 0)) {
		PKT_ERROR(pkt, "linearize failed\n");
		return -EPROTO;
	}

	if (unlikely(skb_tailroom(skb) < buflen)) {
		PKT_ERROR(pkt, "buffer too short - failed\n");
		return -EPROTO;
	}

	*buf = skb_put(skb, buflen);
	return 1;
}
EXPORT_SYMBOL(cfpkt_raw_append);

int cfpkt_raw_extract(struct cfpkt *pkt, void **buf, unsigned int buflen)
{
	struct sk_buff *skb = pkt_to_skb(pkt);

	caif_assert(buf != NULL);
	if (unlikely(is_erronous(pkt)))
		return -EPROTO;

	if (unlikely(buflen > skb->len)) {
		PKT_ERROR(pkt, "buflen too large - failed\n");
		return -EPROTO;
	}

	if (unlikely(buflen > skb_headlen(skb))) {
		if (unlikely(skb_linearize(skb) != 0)) {
			PKT_ERROR(pkt, "linearize failed\n");
			return -EPROTO;
		}
	}

	*buf = skb->data;
	skb_pull(skb, buflen);

	return 1;
}
EXPORT_SYMBOL(cfpkt_raw_extract);

inline bool cfpkt_erroneous(struct cfpkt *pkt)
{
	return cfpkt_priv(pkt)->erronous;
}
EXPORT_SYMBOL(cfpkt_erroneous);

struct cfpktq *cfpktq_create(void)
{
	struct cfpktq *q = kmalloc(sizeof(struct cfpktq), GFP_ATOMIC);
	if (!q)
		return NULL;
	skb_queue_head_init(&q->head);
	atomic_set(&q->count, 0);
	spin_lock_init(&q->lock);
	return q;
}
EXPORT_SYMBOL(cfpktq_create);

void cfpkt_queue(struct cfpktq *pktq, struct cfpkt *pkt, unsigned short prio)
{
	atomic_inc(&pktq->count);
	spin_lock(&pktq->lock);
	skb_queue_tail(&pktq->head, pkt_to_skb(pkt));
	spin_unlock(&pktq->lock);

}
EXPORT_SYMBOL(cfpkt_queue);

struct cfpkt *cfpkt_qpeek(struct cfpktq *pktq)
{
	struct cfpkt *tmp;
	spin_lock(&pktq->lock);
	tmp = skb_to_pkt(skb_peek(&pktq->head));
	spin_unlock(&pktq->lock);
	return tmp;
}
EXPORT_SYMBOL(cfpkt_qpeek);

struct cfpkt *cfpkt_dequeue(struct cfpktq *pktq)
{
	struct cfpkt *pkt;
	spin_lock(&pktq->lock);
	pkt = skb_to_pkt(skb_dequeue(&pktq->head));
	if (pkt) {
		atomic_dec(&pktq->count);
		caif_assert(atomic_read(&pktq->count) >= 0);
	}
	spin_unlock(&pktq->lock);
	return pkt;
}
EXPORT_SYMBOL(cfpkt_dequeue);

int cfpkt_qcount(struct cfpktq *pktq)
{
	return atomic_read(&pktq->count);
}
EXPORT_SYMBOL(cfpkt_qcount);

struct cfpkt *cfpkt_clone_release(struct cfpkt *pkt)
{
	struct cfpkt *clone;
	clone  = skb_to_pkt(skb_clone(pkt_to_skb(pkt), GFP_ATOMIC));
	/* Free original packet. */
	cfpkt_destroy(pkt);
	if (!clone)
		return NULL;
	return clone;
}
EXPORT_SYMBOL(cfpkt_clone_release);

struct caif_payload_info *cfpkt_info(struct cfpkt *pkt)
{
	return (struct caif_payload_info *)&pkt_to_skb(pkt)->cb;
}
EXPORT_SYMBOL(cfpkt_info);
