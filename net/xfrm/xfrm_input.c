// SPDX-License-Identifier: GPL-2.0
/*
 * xfrm_input.c
 *
 * Changes:
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific portion
 *
 */

#include <linux/bottom_half.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/percpu.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/ip_tunnels.h>
#include <net/ip6_tunnel.h>

struct xfrm_trans_tasklet {
	struct tasklet_struct tasklet;
	struct sk_buff_head queue;
};

struct xfrm_trans_cb {
	union {
		struct inet_skb_parm	h4;
#if IS_ENABLED(CONFIG_IPV6)
		struct inet6_skb_parm	h6;
#endif
	} header;
	int (*finish)(struct net *net, struct sock *sk, struct sk_buff *skb);
};

#define XFRM_TRANS_SKB_CB(__skb) ((struct xfrm_trans_cb *)&((__skb)->cb[0]))

static struct kmem_cache *secpath_cachep __ro_after_init;

static DEFINE_SPINLOCK(xfrm_input_afinfo_lock);
static struct xfrm_input_afinfo const __rcu *xfrm_input_afinfo[AF_INET6 + 1];

static struct gro_cells gro_cells;
static struct net_device xfrm_napi_dev;

static DEFINE_PER_CPU(struct xfrm_trans_tasklet, xfrm_trans_tasklet);

int xfrm_input_register_afinfo(const struct xfrm_input_afinfo *afinfo)
{
	int err = 0;

	if (WARN_ON(afinfo->family >= ARRAY_SIZE(xfrm_input_afinfo)))
		return -EAFNOSUPPORT;

	spin_lock_bh(&xfrm_input_afinfo_lock);
	if (unlikely(xfrm_input_afinfo[afinfo->family] != NULL))
		err = -EEXIST;
	else
		rcu_assign_pointer(xfrm_input_afinfo[afinfo->family], afinfo);
	spin_unlock_bh(&xfrm_input_afinfo_lock);
	return err;
}
EXPORT_SYMBOL(xfrm_input_register_afinfo);

int xfrm_input_unregister_afinfo(const struct xfrm_input_afinfo *afinfo)
{
	int err = 0;

	spin_lock_bh(&xfrm_input_afinfo_lock);
	if (likely(xfrm_input_afinfo[afinfo->family] != NULL)) {
		if (unlikely(xfrm_input_afinfo[afinfo->family] != afinfo))
			err = -EINVAL;
		else
			RCU_INIT_POINTER(xfrm_input_afinfo[afinfo->family], NULL);
	}
	spin_unlock_bh(&xfrm_input_afinfo_lock);
	synchronize_rcu();
	return err;
}
EXPORT_SYMBOL(xfrm_input_unregister_afinfo);

static const struct xfrm_input_afinfo *xfrm_input_get_afinfo(unsigned int family)
{
	const struct xfrm_input_afinfo *afinfo;

	if (WARN_ON_ONCE(family >= ARRAY_SIZE(xfrm_input_afinfo)))
		return NULL;

	rcu_read_lock();
	afinfo = rcu_dereference(xfrm_input_afinfo[family]);
	if (unlikely(!afinfo))
		rcu_read_unlock();
	return afinfo;
}

static int xfrm_rcv_cb(struct sk_buff *skb, unsigned int family, u8 protocol,
		       int err)
{
	int ret;
	const struct xfrm_input_afinfo *afinfo = xfrm_input_get_afinfo(family);

	if (!afinfo)
		return -EAFNOSUPPORT;

	ret = afinfo->callback(skb, protocol, err);
	rcu_read_unlock();

	return ret;
}

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
	sp->olen = 0;

	memset(sp->ovec, 0, sizeof(sp->ovec[XFRM_MAX_OFFLOAD_DEPTH]));

	if (src) {
		int i;

		memcpy(sp, src, sizeof(*sp));
		for (i = 0; i < sp->len; i++)
			xfrm_state_hold(sp->xvec[i]);
	}
	refcount_set(&sp->refcnt, 1);
	return sp;
}
EXPORT_SYMBOL(secpath_dup);

int secpath_set(struct sk_buff *skb)
{
	struct sec_path *sp;

	/* Allocate new secpath or COW existing one. */
	if (!skb->sp || refcount_read(&skb->sp->refcnt) != 1) {
		sp = secpath_dup(skb->sp);
		if (!sp)
			return -ENOMEM;

		if (skb->sp)
			secpath_put(skb->sp);
		skb->sp = sp;
	}
	return 0;
}
EXPORT_SYMBOL(secpath_set);

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
EXPORT_SYMBOL(xfrm_parse_spi);

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
	struct xfrm_state *x = NULL;
	xfrm_address_t *daddr;
	struct xfrm_mode *inner_mode;
	u32 mark = skb->mark;
	unsigned int family = AF_UNSPEC;
	int decaps = 0;
	int async = 0;
	bool xfrm_gro = false;
	bool crypto_done = false;
	struct xfrm_offload *xo = xfrm_offload(skb);

	if (encap_type < 0) {
		x = xfrm_input_state(skb);

		if (unlikely(x->km.state != XFRM_STATE_VALID)) {
			if (x->km.state == XFRM_STATE_ACQ)
				XFRM_INC_STATS(net, LINUX_MIB_XFRMACQUIREERROR);
			else
				XFRM_INC_STATS(net,
					       LINUX_MIB_XFRMINSTATEINVALID);
			goto drop;
		}

		family = x->outer_mode->afinfo->family;

		/* An encap_type of -1 indicates async resumption. */
		if (encap_type == -1) {
			async = 1;
			seq = XFRM_SKB_CB(skb)->seq.input.low;
			goto resume;
		}

		/* encap_type < -1 indicates a GRO call. */
		encap_type = 0;
		seq = XFRM_SPI_SKB_CB(skb)->seq;

		if (xo && (xo->flags & CRYPTO_DONE)) {
			crypto_done = true;
			family = XFRM_SPI_SKB_CB(skb)->family;

			if (!(xo->status & CRYPTO_SUCCESS)) {
				if (xo->status &
				    (CRYPTO_TRANSPORT_AH_AUTH_FAILED |
				     CRYPTO_TRANSPORT_ESP_AUTH_FAILED |
				     CRYPTO_TUNNEL_AH_AUTH_FAILED |
				     CRYPTO_TUNNEL_ESP_AUTH_FAILED)) {

					xfrm_audit_state_icvfail(x, skb,
								 x->type->proto);
					x->stats.integrity_failed++;
					XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEPROTOERROR);
					goto drop;
				}

				if (xo->status & CRYPTO_INVALID_PROTOCOL) {
					XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEPROTOERROR);
					goto drop;
				}

				XFRM_INC_STATS(net, LINUX_MIB_XFRMINBUFFERERROR);
				goto drop;
			}

			if ((err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) != 0) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINHDRERROR);
				goto drop;
			}
		}

		goto lock;
	}

	family = XFRM_SPI_SKB_CB(skb)->family;

	/* if tunnel is present override skb->mark value with tunnel i_key */
	switch (family) {
	case AF_INET:
		if (XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4)
			mark = be32_to_cpu(XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4->parms.i_key);
		break;
	case AF_INET6:
		if (XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6)
			mark = be32_to_cpu(XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6->parms.i_key);
		break;
	}

	err = secpath_set(skb);
	if (err) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINERROR);
		goto drop;
	}

	seq = 0;
	if (!spi && (err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) != 0) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINHDRERROR);
		goto drop;
	}

	daddr = (xfrm_address_t *)(skb_network_header(skb) +
				   XFRM_SPI_SKB_CB(skb)->daddroff);
	do {
		if (skb->sp->len == XFRM_MAX_DEPTH) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINBUFFERERROR);
			goto drop;
		}

		x = xfrm_state_lookup(net, mark, daddr, spi, nexthdr, family);
		if (x == NULL) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINNOSTATES);
			xfrm_audit_state_notfound(skb, family, spi, seq);
			goto drop;
		}

		skb->sp->xvec[skb->sp->len++] = x;

lock:
		spin_lock(&x->lock);

		if (unlikely(x->km.state != XFRM_STATE_VALID)) {
			if (x->km.state == XFRM_STATE_ACQ)
				XFRM_INC_STATS(net, LINUX_MIB_XFRMACQUIREERROR);
			else
				XFRM_INC_STATS(net,
					       LINUX_MIB_XFRMINSTATEINVALID);
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

		if (xfrm_tunnel_check(skb, x, family)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEMODEERROR);
			goto drop;
		}

		seq_hi = htonl(xfrm_replay_seqhi(x, seq));

		XFRM_SKB_CB(skb)->seq.input.low = seq;
		XFRM_SKB_CB(skb)->seq.input.hi = seq_hi;

		skb_dst_force(skb);
		dev_hold(skb->dev);

		if (crypto_done)
			nexthdr = x->type_offload->input_tail(x, skb);
		else
			nexthdr = x->type->input(x, skb);

		if (nexthdr == -EINPROGRESS)
			return 0;
resume:
		dev_put(skb->dev);

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
			if (inner_mode == NULL) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEMODEERROR);
				goto drop;
			}
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

	err = xfrm_rcv_cb(skb, family, x->type->proto, 0);
	if (err)
		goto drop;

	nf_reset(skb);

	if (decaps) {
		if (skb->sp)
			skb->sp->olen = 0;
		skb_dst_drop(skb);
		gro_cells_receive(&gro_cells, skb);
		return 0;
	} else {
		xo = xfrm_offload(skb);
		if (xo)
			xfrm_gro = xo->flags & XFRM_GRO;

		err = x->inner_mode->afinfo->transport_finish(skb, xfrm_gro || async);
		if (xfrm_gro) {
			if (skb->sp)
				skb->sp->olen = 0;
			skb_dst_drop(skb);
			gro_cells_receive(&gro_cells, skb);
			return err;
		}

		return err;
	}

drop_unlock:
	spin_unlock(&x->lock);
drop:
	xfrm_rcv_cb(skb, family, x && x->type ? x->type->proto : nexthdr, -1);
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL(xfrm_input);

int xfrm_input_resume(struct sk_buff *skb, int nexthdr)
{
	return xfrm_input(skb, nexthdr, 0, -1);
}
EXPORT_SYMBOL(xfrm_input_resume);

static void xfrm_trans_reinject(unsigned long data)
{
	struct xfrm_trans_tasklet *trans = (void *)data;
	struct sk_buff_head queue;
	struct sk_buff *skb;

	__skb_queue_head_init(&queue);
	skb_queue_splice_init(&trans->queue, &queue);

	while ((skb = __skb_dequeue(&queue)))
		XFRM_TRANS_SKB_CB(skb)->finish(dev_net(skb->dev), NULL, skb);
}

int xfrm_trans_queue(struct sk_buff *skb,
		     int (*finish)(struct net *, struct sock *,
				   struct sk_buff *))
{
	struct xfrm_trans_tasklet *trans;

	trans = this_cpu_ptr(&xfrm_trans_tasklet);

	if (skb_queue_len(&trans->queue) >= netdev_max_backlog)
		return -ENOBUFS;

	XFRM_TRANS_SKB_CB(skb)->finish = finish;
	__skb_queue_tail(&trans->queue, skb);
	tasklet_schedule(&trans->tasklet);
	return 0;
}
EXPORT_SYMBOL(xfrm_trans_queue);

void __init xfrm_input_init(void)
{
	int err;
	int i;

	init_dummy_netdev(&xfrm_napi_dev);
	err = gro_cells_init(&gro_cells, &xfrm_napi_dev);
	if (err)
		gro_cells.cells = NULL;

	secpath_cachep = kmem_cache_create("secpath_cache",
					   sizeof(struct sec_path),
					   0, SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					   NULL);

	for_each_possible_cpu(i) {
		struct xfrm_trans_tasklet *trans;

		trans = &per_cpu(xfrm_trans_tasklet, i);
		__skb_queue_head_init(&trans->queue);
		tasklet_init(&trans->tasklet, xfrm_trans_reinject,
			     (unsigned long)trans);
	}
}
