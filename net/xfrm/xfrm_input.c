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
		*spi = htonl(ntohs(*(__be16 *)(skb_transport_header(skb) + 2)));
		*seq = 0;
		return 0;
	default:
		return 1;
	}

	if (!pskb_may_pull(skb, hlen))
		return -EINVAL;

	*spi = *(__be32 *)(skb_transport_header(skb) + offset);
	*seq = *(__be32 *)(skb_transport_header(skb) + offset_seq);
	return 0;
}

int xfrm_prepare_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct xfrm_mode *inner_mode = x->inner_mode;
	int err;

	err = x->outer_mode->afinfo->extract_input(x, skb);
	if (err)
		return err;

	if (x->sel.family == AF_UNSPEC) {
		inner_mode = xfrm_ip2inner_mode(x, XFRM_MODE_SKB_CB(skb)->protocol);
		if (inner_mode == NULL)
			return -EAFNOSUPPORT;
	}

	skb->protocol = inner_mode->afinfo->eth_proto;
	return inner_mode->input2(x, skb);
}
EXPORT_SYMBOL(xfrm_prepare_input);

int xfrm_input(struct sk_buff *skb, int nexthdr, __be32 spi, int encap_type)
{
	struct net *net = dev_net(skb->dev);
	int err;
	__be32 seq;
	__be32 seq_hi;
	struct xfrm_state *x;
	xfrm_address_t *daddr;
	struct xfrm_mode *inner_mode;
	unsigned int family;
	int decaps = 0;
	int async = 0;

	/* A negative encap_type indicates async resumption. */
	if (encap_type < 0) {
		async = 1;
		x = xfrm_input_state(skb);
		seq = XFRM_SKB_CB(skb)->seq.input.low;
		goto resume;
	}

	/* Allocate new secpath or COW existing one. */
	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
		struct sec_path *sp;

		sp = secpath_dup(skb->sp);
		if (!sp) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINERROR);
			goto drop;
		}
		if (skb->sp)
			secpath_put(skb->sp);
		skb->sp = sp;
	}

	daddr = (xfrm_address_t *)(skb_network_header(skb) +
				   XFRM_SPI_SKB_CB(skb)->daddroff);
	family = XFRM_SPI_SKB_CB(skb)->family;

	seq = 0;
	if (!spi && (err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) != 0) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINHDRERROR);
		goto drop;
	}

	do {
		if (skb->sp->len == XFRM_MAX_DEPTH) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINBUFFERERROR);
			goto drop;
		}

		x = xfrm_state_lookup(net, skb->mark, daddr, spi, nexthdr, family);
		if (x == NULL) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINNOSTATES);
			xfrm_audit_state_notfound(skb, family, spi, seq);
			goto drop;
		}

		skb->sp->xvec[skb->sp->len++] = x;

		spin_lock(&x->lock);
		if (unlikely(x->km.state == XFRM_STATE_ACQ)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMACQUIREERROR);
			goto drop_unlock;
		}

		if (unlikely(x->km.state != XFRM_STATE_VALID)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEINVALID);
			goto drop_unlock;
		}

		if ((x->encap ? x->encap->encap_type : 0) != encap_type) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEMISMATCH);
			goto drop_unlock;
		}

		if (x->repl->check(x, skb, seq)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATESEQERROR);
			goto drop_unlock;
		}

		if (xfrm_state_check_expire(x)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEEXPIRED);
			goto drop_unlock;
		}

		spin_unlock(&x->lock);

		seq_hi = htonl(xfrm_replay_seqhi(x, seq));

		XFRM_SKB_CB(skb)->seq.input.low = seq;
		XFRM_SKB_CB(skb)->seq.input.hi = seq_hi;

		skb_dst_force(skb);

		nexthdr = x->type->input(x, skb);

		if (nexthdr == -EINPROGRESS)
			return 0;

resume:
		spin_lock(&x->lock);
		if (nexthdr <= 0) {
			if (nexthdr == -EBADMSG) {
				xfrm_audit_state_icvfail(x, skb,
							 x->type->proto);
				x->stats.integrity_failed++;
			}
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEPROTOERROR);
			goto drop_unlock;
		}

		/* only the first xfrm gets the encap type */
		encap_type = 0;

		if (async && x->repl->recheck(x, skb, seq)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATESEQERROR);
			goto drop_unlock;
		}

		x->repl->advance(x, seq);

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock(&x->lock);

		XFRM_MODE_SKB_CB(skb)->protocol = nexthdr;

		inner_mode = x->inner_mode;

		if (x->sel.family == AF_UNSPEC) {
			inner_mode = xfrm_ip2inner_mode(x, XFRM_MODE_SKB_CB(skb)->protocol);
			if (inner_mode == NULL)
				goto drop;
		}

		if (inner_mode->input(x, skb)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEMODEERROR);
			goto drop;
		}

		if (x->outer_mode->flags & XFRM_MODE_FLAG_TUNNEL) {
			decaps = 1;
			break;
		}

		/*
		 * We need the inner address.  However, we only get here for
		 * transport mode so the outer address is identical.
		 */
		daddr = &x->id.daddr;
		family = x->outer_mode->afinfo->family;

		err = xfrm_parse_spi(skb, nexthdr, &spi, &seq);
		if (err < 0) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINHDRERROR);
			goto drop;
		}
	} while (!err);

	nf_reset(skb);

	if (decaps) {
		skb_dst_drop(skb);
		netif_rx(skb);
		return 0;
	} else {
		return x->inner_mode->afinfo->transport_finish(skb, async);
	}

drop_unlock:
	spin_unlock(&x->lock);
drop:
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL(xfrm_input);

int xfrm_input_resume(struct sk_buff *skb, int nexthdr)
{
	return xfrm_input(skb, nexthdr, 0, -1);
}
EXPORT_SYMBOL(xfrm_input_resume);

void __init xfrm_input_init(void)
{
	secpath_cachep = kmem_cache_create("secpath_cache",
					   sizeof(struct sec_path),
					   0, SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					   NULL);
}
