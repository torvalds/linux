/*
 * xfrm_input.c
 *
 * Changes:
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific portion
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>

static struct kmem_cache *secpath_cachep __read_mostly;

void __secpath_destroy(struct sec_path *sp)
{
	int i;
	for (i = 0; i < sp->len; i++)
		xfrm_state_put(sp->xvec[i]);
	kmem_cache_free(secpath_cachep, sp);
}
EXPORT_SYMBOL(__secpath_destroy);

struct sec_path *secpath_dup(struct sec_path *src)
{
	struct sec_path *sp;

	sp = kmem_cache_alloc(secpath_cachep, GFP_ATOMIC);
	if (!sp)
		return NULL;

	sp->len = 0;
	if (src) {
		int i;

		memcpy(sp, src, sizeof(*sp));
		for (i = 0; i < sp->len; i++)
			xfrm_state_hold(sp->xvec[i]);
	}
	atomic_set(&sp->refcnt, 1);
	return sp;
}
EXPORT_SYMBOL(secpath_dup);

/* Fetch spi and seq from ipsec header */

int xfrm_parse_spi(struct sk_buff *skb, u8 nexthdr, __be32 *spi, __be32 *seq)
{
	int offset, offset_seq;
	int hlen;

	switch (nexthdr) {
	case IPPROTO_AH:
		hlen = sizeof(struct ip_auth_hdr);
		offset = offsetof(struct ip_auth_hdr, spi);
		offset_seq = offsetof(struct ip_auth_hdr, seq_no);
		break;
	case IPPROTO_ESP:
		hlen = sizeof(struct ip_esp_hdr);
		offset = offsetof(struct ip_esp_hdr, spi);
		offset_seq = offsetof(struct ip_esp_hdr, seq_no);
		break;
	case IPPROTO_COMP:
		if (!pskb_may_pull(skb, sizeof(struct ip_comp_hdr)))
			return -EINVAL;
		*spi = htonl(ntohs(*(__be16*)(skb_transport_header(skb) + 2)));
		*seq = 0;
		return 0;
	default:
		return 1;
	}

	if (!pskb_may_pull(skb, hlen))
		return -EINVAL;

	*spi = *(__be32*)(skb_transport_header(skb) + offset);
	*seq = *(__be32*)(skb_transport_header(skb) + offset_seq);
	return 0;
}
EXPORT_SYMBOL(xfrm_parse_spi);

int xfrm_prepare_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;

	err = x->outer_mode->afinfo->extract_input(x, skb);
	if (err)
		return err;

	skb->protocol = x->inner_mode->afinfo->eth_proto;
	return x->inner_mode->input2(x, skb);
}
EXPORT_SYMBOL(xfrm_prepare_input);

int xfrm_input(struct sk_buff *skb, int nexthdr, __be32 spi, int encap_type)
{
	int err;
	__be32 seq;
	struct xfrm_state *x;
	int decaps = 0;
	unsigned int nhoff = XFRM_SPI_SKB_CB(skb)->nhoff;
	unsigned int daddroff = XFRM_SPI_SKB_CB(skb)->daddroff;

	/* Allocate new secpath or COW existing one. */
	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
		struct sec_path *sp;

		sp = secpath_dup(skb->sp);
		if (!sp)
			goto drop;
		if (skb->sp)
			secpath_put(skb->sp);
		skb->sp = sp;
	}

	seq = 0;
	if (!spi && (err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) != 0)
		goto drop;

	do {
		if (skb->sp->len == XFRM_MAX_DEPTH)
			goto drop;

		x = xfrm_state_lookup((xfrm_address_t *)
				      (skb_network_header(skb) + daddroff),
				      spi, nexthdr, AF_INET);
		if (x == NULL)
			goto drop;

		skb->sp->xvec[skb->sp->len++] = x;

		spin_lock(&x->lock);
		if (unlikely(x->km.state != XFRM_STATE_VALID))
			goto drop_unlock;

		if ((x->encap ? x->encap->encap_type : 0) != encap_type)
			goto drop_unlock;

		if (x->props.replay_window && xfrm_replay_check(x, seq))
			goto drop_unlock;

		if (xfrm_state_check_expire(x))
			goto drop_unlock;

		spin_unlock(&x->lock);

		nexthdr = x->type->input(x, skb);

		spin_lock(&x->lock);
		if (nexthdr <= 0) {
			if (nexthdr == -EBADMSG)
				x->stats.integrity_failed++;
			goto drop_unlock;
		}

		skb_network_header(skb)[nhoff] = nexthdr;

		/* only the first xfrm gets the encap type */
		encap_type = 0;

		if (x->props.replay_window)
			xfrm_replay_advance(x, seq);

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock(&x->lock);

		if (x->inner_mode->input(x, skb))
			goto drop;

		if (x->outer_mode->flags & XFRM_MODE_FLAG_TUNNEL) {
			decaps = 1;
			break;
		}

		err = xfrm_parse_spi(skb, nexthdr, &spi, &seq);
		if (err < 0)
			goto drop;
	} while (!err);

	nf_reset(skb);

	if (decaps) {
		dst_release(skb->dst);
		skb->dst = NULL;
		netif_rx(skb);
		return 0;
	} else {
		return x->inner_mode->afinfo->transport_finish(skb, 0);
	}

drop_unlock:
	spin_unlock(&x->lock);
drop:
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL(xfrm_input);

void __init xfrm_input_init(void)
{
	secpath_cachep = kmem_cache_create("secpath_cache",
					   sizeof(struct sec_path),
					   0, SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					   NULL);
}
