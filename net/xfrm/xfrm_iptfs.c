// SPDX-License-Identifier: GPL-2.0
/* xfrm_iptfs: IPTFS encapsulation support
 *
 * April 21 2022, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2022, LabN Consulting, L.L.C.
 *
 */

#include <linux/kernel.h>
#include <linux/icmpv6.h>
#include <linux/skbuff_ref.h>
#include <net/gro.h>
#include <net/icmp.h>
#include <net/ip6_route.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>

#include <crypto/aead.h>

#include "xfrm_inout.h"

/* ------------------------------------------------ */
/* IPTFS default SA values (tunnel ingress/dir-out) */
/* ------------------------------------------------ */

/**
 * define IPTFS_DEFAULT_INIT_DELAY_USECS - default initial output delay
 *
 * The initial output delay is the amount of time prior to servicing the output
 * queue after queueing the first packet on said queue. This applies anytime the
 * output queue was previously empty.
 *
 * Default 0.
 */
#define IPTFS_DEFAULT_INIT_DELAY_USECS 0

/**
 * define IPTFS_DEFAULT_MAX_QUEUE_SIZE - default max output queue size.
 *
 * The default IPTFS max output queue size in octets. The output queue is where
 * received packets destined for output over an IPTFS tunnel are stored prior to
 * being output in aggregated/fragmented form over the IPTFS tunnel.
 *
 * Default 1M.
 */
#define IPTFS_DEFAULT_MAX_QUEUE_SIZE (1024 * 10240)

#define NSECS_IN_USEC 1000

#define IPTFS_HRTIMER_MODE HRTIMER_MODE_REL_SOFT

/**
 * struct xfrm_iptfs_config - configuration for the IPTFS tunnel.
 * @pkt_size: size of the outer IP packet. 0 to use interface and MTU discovery,
 *	otherwise the user specified value.
 * @max_queue_size: The maximum number of octets allowed to be queued to be sent
 *	over the IPTFS SA. The queue size is measured as the size of all the
 *	packets enqueued.
 */
struct xfrm_iptfs_config {
	u32 pkt_size;	    /* outer_packet_size or 0 */
	u32 max_queue_size; /* octets */
};

/**
 * struct xfrm_iptfs_data - mode specific xfrm state.
 * @cfg: IPTFS tunnel config.
 * @x: owning SA (xfrm_state).
 * @queue: queued user packets to send.
 * @queue_size: number of octets on queue (sum of packet sizes).
 * @ecn_queue_size: octets above with ECN mark.
 * @init_delay_ns: nanoseconds to wait to send initial IPTFS packet.
 * @iptfs_timer: output timer.
 * @payload_mtu: max payload size.
 */
struct xfrm_iptfs_data {
	struct xfrm_iptfs_config cfg;

	/* Ingress User Input */
	struct xfrm_state *x;	   /* owning state */
	struct sk_buff_head queue; /* output queue */

	u32 queue_size;		    /* octets */
	u32 ecn_queue_size;	    /* octets above which ECN mark */
	u64 init_delay_ns;	    /* nanoseconds */
	struct hrtimer iptfs_timer; /* output timer */
	u32 payload_mtu;	    /* max payload size */
};

static u32 iptfs_get_inner_mtu(struct xfrm_state *x, int outer_mtu);
static enum hrtimer_restart iptfs_delay_timer(struct hrtimer *me);

/* ======================= */
/* IPTFS SK_BUFF Functions */
/* ======================= */

/**
 * iptfs_skb_head_to_frag() - initialize a skb_frag_t based on skb head data
 * @skb: skb with the head data
 * @frag: frag to initialize
 */
static void iptfs_skb_head_to_frag(const struct sk_buff *skb, skb_frag_t *frag)
{
	struct page *page = virt_to_head_page(skb->data);
	unsigned char *addr = (unsigned char *)page_address(page);

	skb_frag_fill_page_desc(frag, page, skb->data - addr, skb_headlen(skb));
}

/* ================================= */
/* IPTFS Sending (ingress) Functions */
/* ================================= */

/* ------------------------- */
/* Enqueue to send functions */
/* ------------------------- */

/**
 * iptfs_enqueue() - enqueue packet if ok to send.
 * @xtfs: xtfs state
 * @skb: the packet
 *
 * Return: true if packet enqueued.
 */
static bool iptfs_enqueue(struct xfrm_iptfs_data *xtfs, struct sk_buff *skb)
{
	u64 newsz = xtfs->queue_size + skb->len;
	struct iphdr *iph;

	assert_spin_locked(&xtfs->x->lock);

	if (newsz > xtfs->cfg.max_queue_size)
		return false;

	/* Set ECN CE if we are above our ECN queue threshold */
	if (newsz > xtfs->ecn_queue_size) {
		iph = ip_hdr(skb);
		if (iph->version == 4)
			IP_ECN_set_ce(iph);
		else if (iph->version == 6)
			IP6_ECN_set_ce(skb, ipv6_hdr(skb));
	}

	__skb_queue_tail(&xtfs->queue, skb);
	xtfs->queue_size += skb->len;
	return true;
}

static int iptfs_get_cur_pmtu(struct xfrm_state *x, struct xfrm_iptfs_data *xtfs,
			      struct sk_buff *skb)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)skb_dst(skb);
	u32 payload_mtu = xtfs->payload_mtu;
	u32 pmtu = iptfs_get_inner_mtu(x, xdst->child_mtu_cached);

	if (payload_mtu && payload_mtu < pmtu)
		pmtu = payload_mtu;

	return pmtu;
}

static int iptfs_is_too_big(struct sock *sk, struct sk_buff *skb, u32 pmtu)
{
	if (skb->len <= pmtu)
		return 0;

	/* We only send ICMP too big if the user has configured us as
	 * dont-fragment.
	 */
	if (skb->dev)
		XFRM_INC_STATS(dev_net(skb->dev), LINUX_MIB_XFRMOUTERROR);

	if (sk)
		xfrm_local_error(skb, pmtu);
	else if (ip_hdr(skb)->version == 4)
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(pmtu));
	else
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, pmtu);

	return 1;
}

/* IPv4/IPv6 packet ingress to IPTFS tunnel, arrange to send in IPTFS payload
 * (i.e., aggregating or fragmenting as appropriate).
 * This is set in dst->output for an SA.
 */
static int iptfs_output_collect(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct xfrm_state *x = dst->xfrm;
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct sk_buff *segs, *nskb;
	u32 pmtu = 0;
	bool ok = true;
	bool was_gso;

	/* We have hooked into dst_entry->output which means we have skipped the
	 * protocol specific netfilter (see xfrm4_output, xfrm6_output).
	 * when our timer runs we will end up calling xfrm_output directly on
	 * the encapsulated traffic.
	 *
	 * For both cases this is the NF_INET_POST_ROUTING hook which allows
	 * changing the skb->dst entry which then may not be xfrm based anymore
	 * in which case a REROUTED flag is set. and dst_output is called.
	 *
	 * For IPv6 we are also skipping fragmentation handling for local
	 * sockets, which may or may not be good depending on our tunnel DF
	 * setting. Normally with fragmentation supported we want to skip this
	 * fragmentation.
	 */

	pmtu = iptfs_get_cur_pmtu(x, xtfs, skb);

	/* Break apart GSO skbs. If the queue is nearing full then we want the
	 * accounting and queuing to be based on the individual packets not on the
	 * aggregate GSO buffer.
	 */
	was_gso = skb_is_gso(skb);
	if (!was_gso) {
		segs = skb;
	} else {
		segs = skb_gso_segment(skb, 0);
		if (IS_ERR_OR_NULL(segs)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTERROR);
			kfree_skb(skb);
			if (IS_ERR(segs))
				return PTR_ERR(segs);
			return -EINVAL;
		}
		consume_skb(skb);
		skb = NULL;
	}

	/* We can be running on multiple cores and from the network softirq or
	 * from user context depending on where the packet is coming from.
	 */
	spin_lock_bh(&x->lock);

	skb_list_walk_safe(segs, skb, nskb) {
		skb_mark_not_on_list(skb);

		/* Once we drop due to no queue space we continue to drop the
		 * rest of the packets from that GRO.
		 */
		if (!ok) {
nospace:
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTNOQSPACE);
			kfree_skb_reason(skb, SKB_DROP_REASON_FULL_RING);
			continue;
		}

		/* Fragmenting handled in following commits. */
		if (iptfs_is_too_big(sk, skb, pmtu)) {
			kfree_skb_reason(skb, SKB_DROP_REASON_PKT_TOO_BIG);
			continue;
		}

		/* Enqueue to send in tunnel */
		ok = iptfs_enqueue(xtfs, skb);
		if (!ok)
			goto nospace;
	}

	/* Start a delay timer if we don't have one yet */
	if (!hrtimer_is_queued(&xtfs->iptfs_timer))
		hrtimer_start(&xtfs->iptfs_timer, xtfs->init_delay_ns, IPTFS_HRTIMER_MODE);

	spin_unlock_bh(&x->lock);
	return 0;
}

/* -------------------------- */
/* Dequeue and send functions */
/* -------------------------- */

static void iptfs_output_prepare_skb(struct sk_buff *skb, u32 blkoff)
{
	struct ip_iptfs_hdr *h;
	size_t hsz = sizeof(*h);

	/* now reset values to be pointing at the rest of the packets */
	h = skb_push(skb, hsz);
	memset(h, 0, hsz);
	if (blkoff)
		h->block_offset = htons(blkoff);

	/* network_header current points at the inner IP packet
	 * move it to the iptfs header
	 */
	skb->transport_header = skb->network_header;
	skb->network_header -= hsz;

	IPCB(skb)->flags |= IPSKB_XFRM_TUNNEL_SIZE;
}

static struct sk_buff **iptfs_rehome_fraglist(struct sk_buff **nextp, struct sk_buff *child)
{
	u32 fllen = 0;

	/* It might be possible to account for a frag list in addition to page
	 * fragment if it's a valid state to be in. The page fragments size
	 * should be kept as data_len so only the frag_list size is removed,
	 * this must be done above as well.
	 */
	*nextp = skb_shinfo(child)->frag_list;
	while (*nextp) {
		fllen += (*nextp)->len;
		nextp = &(*nextp)->next;
	}
	skb_frag_list_init(child);
	child->len -= fllen;
	child->data_len -= fllen;

	return nextp;
}

static void iptfs_consume_frags(struct sk_buff *to, struct sk_buff *from)
{
	struct skb_shared_info *fromi = skb_shinfo(from);
	struct skb_shared_info *toi = skb_shinfo(to);
	unsigned int new_truesize;

	/* If we have data in a head page, grab it */
	if (!skb_headlen(from)) {
		new_truesize = SKB_TRUESIZE(skb_end_offset(from));
	} else {
		iptfs_skb_head_to_frag(from, &toi->frags[toi->nr_frags]);
		skb_frag_ref(to, toi->nr_frags++);
		new_truesize = SKB_DATA_ALIGN(sizeof(struct sk_buff));
	}

	/* Move any other page fragments rather than copy */
	memcpy(&toi->frags[toi->nr_frags], fromi->frags,
	       sizeof(fromi->frags[0]) * fromi->nr_frags);
	toi->nr_frags += fromi->nr_frags;
	fromi->nr_frags = 0;
	from->data_len = 0;
	from->len = 0;
	to->truesize += from->truesize - new_truesize;
	from->truesize = new_truesize;

	/* We are done with this SKB */
	consume_skb(from);
}

static void iptfs_output_queued(struct xfrm_state *x, struct sk_buff_head *list)
{
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct sk_buff *skb, *skb2, **nextp;
	struct skb_shared_info *shi, *shi2;

	while ((skb = __skb_dequeue(list))) {
		u32 mtu = iptfs_get_cur_pmtu(x, xtfs, skb);
		bool share_ok = true;
		int remaining;

		/* protocol comes to us cleared sometimes */
		skb->protocol = x->outer_mode.family == AF_INET ? htons(ETH_P_IP) :
								  htons(ETH_P_IPV6);

		if (skb->len > mtu) {
			/* We handle this case before enqueueing so we are only
			 * here b/c MTU changed after we enqueued before we
			 * dequeued, just drop these.
			 */
			XFRM_INC_STATS(xs_net(x), LINUX_MIB_XFRMOUTERROR);

			kfree_skb_reason(skb, SKB_DROP_REASON_PKT_TOO_BIG);
			continue;
		}

		/* If we don't have a cksum in the packet we need to add one
		 * before encapsulation.
		 */
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			if (skb_checksum_help(skb)) {
				XFRM_INC_STATS(dev_net(skb_dst(skb)->dev), LINUX_MIB_XFRMOUTERROR);
				kfree_skb(skb);
				continue;
			}
		}

		/* Consider the buffer Tx'd and no longer owned */
		skb_orphan(skb);

		/* Convert first inner packet into an outer IPTFS packet */
		iptfs_output_prepare_skb(skb, 0);

		/* The space remaining to send more inner packet data is `mtu` -
		 * (skb->len - sizeof iptfs header). This is b/c the `mtu` value
		 * has the basic IPTFS header len accounted for, and we added
		 * that header to the skb so it is a part of skb->len, thus we
		 * subtract it from the skb length.
		 */
		remaining = mtu - (skb->len - sizeof(struct ip_iptfs_hdr));

		/* Re-home (un-nest) nested fragment lists. We need to do this
		 * b/c we will simply be appending any following aggregated
		 * inner packets using the frag list.
		 */
		shi = skb_shinfo(skb);
		nextp = &shi->frag_list;
		while (*nextp) {
			if (skb_has_frag_list(*nextp))
				nextp = iptfs_rehome_fraglist(&(*nextp)->next, *nextp);
			else
				nextp = &(*nextp)->next;
		}

		if (shi->frag_list || skb_cloned(skb) || skb_shared(skb))
			share_ok = false;

		/* See if we have enough space to simply append.
		 *
		 * NOTE: Maybe do not append if we will be mis-aligned,
		 * SW-based endpoints will probably have to copy in this
		 * case.
		 */
		while ((skb2 = skb_peek(list))) {
			if (skb2->len > remaining)
				break;

			__skb_unlink(skb2, list);

			/* Consider the buffer Tx'd and no longer owned */
			skb_orphan(skb);

			/* If we don't have a cksum in the packet we need to add
			 * one before encapsulation.
			 */
			if (skb2->ip_summed == CHECKSUM_PARTIAL) {
				if (skb_checksum_help(skb2)) {
					XFRM_INC_STATS(xs_net(x), LINUX_MIB_XFRMOUTERROR);
					kfree_skb(skb2);
					continue;
				}
			}

			/* skb->pp_recycle is passed to __skb_flag_unref for all
			 * frag pages so we can only share pages with skb's who
			 * match ourselves.
			 */
			shi2 = skb_shinfo(skb2);
			if (share_ok &&
			    (shi2->frag_list ||
			     (!skb2->head_frag && skb_headlen(skb)) ||
			     skb->pp_recycle != skb2->pp_recycle ||
			     skb_zcopy(skb2) ||
			     (shi->nr_frags + shi2->nr_frags + 1 > MAX_SKB_FRAGS)))
				share_ok = false;

			/* Do accounting */
			skb->data_len += skb2->len;
			skb->len += skb2->len;
			remaining -= skb2->len;

			if (share_ok) {
				iptfs_consume_frags(skb, skb2);
			} else {
				/* Append to the frag_list */
				*nextp = skb2;
				nextp = &skb2->next;
				if (skb_has_frag_list(skb2))
					nextp = iptfs_rehome_fraglist(nextp,
								      skb2);
				skb->truesize += skb2->truesize;
			}
		}

		xfrm_output(NULL, skb);
	}
}

static enum hrtimer_restart iptfs_delay_timer(struct hrtimer *me)
{
	struct sk_buff_head list;
	struct xfrm_iptfs_data *xtfs;
	struct xfrm_state *x;

	xtfs = container_of(me, typeof(*xtfs), iptfs_timer);
	x = xtfs->x;

	/* Process all the queued packets
	 *
	 * softirq execution order: timer > tasklet > hrtimer
	 *
	 * Network rx will have run before us giving one last chance to queue
	 * ingress packets for us to process and transmit.
	 */

	spin_lock(&x->lock);
	__skb_queue_head_init(&list);
	skb_queue_splice_init(&xtfs->queue, &list);
	xtfs->queue_size = 0;
	spin_unlock(&x->lock);

	/* After the above unlock, packets can begin queuing again, and the
	 * timer can be set again, from another CPU either in softirq or user
	 * context (not from this one since we are running at softirq level
	 * already).
	 */

	iptfs_output_queued(x, &list);

	return HRTIMER_NORESTART;
}

/**
 * iptfs_encap_add_ipv4() - add outer encaps
 * @x: xfrm state
 * @skb: the packet
 *
 * This was originally taken from xfrm4_tunnel_encap_add. The reason for the
 * copy is that IP-TFS/AGGFRAG can have different functionality for how to set
 * the TOS/DSCP bits. Sets the protocol to a different value and doesn't do
 * anything with inner headers as they aren't pointing into a normal IP
 * singleton inner packet.
 *
 * Return: 0 on success or a negative error code on failure
 */
static int iptfs_encap_add_ipv4(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct iphdr *top_iph;

	skb_reset_inner_network_header(skb);
	skb_reset_inner_transport_header(skb);

	skb_set_network_header(skb, -(x->props.header_len - x->props.enc_hdr_len));
	skb->mac_header = skb->network_header + offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + sizeof(*top_iph);

	top_iph = ip_hdr(skb);
	top_iph->ihl = 5;
	top_iph->version = 4;
	top_iph->protocol = IPPROTO_AGGFRAG;

	/* As we have 0, fractional, 1 or N inner packets there's no obviously
	 * correct DSCP mapping to inherit. ECN should be cleared per RFC9347
	 * 3.1.
	 */
	top_iph->tos = 0;

	top_iph->frag_off = htons(IP_DF);
	top_iph->ttl = ip4_dst_hoplimit(xfrm_dst_child(dst));
	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;
	ip_select_ident(dev_net(dst->dev), skb, NULL);

	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
/**
 * iptfs_encap_add_ipv6() - add outer encaps
 * @x: xfrm state
 * @skb: the packet
 *
 * This was originally taken from xfrm6_tunnel_encap_add. The reason for the
 * copy is that IP-TFS/AGGFRAG can have different functionality for how to set
 * the flow label and TOS/DSCP bits. It also sets the protocol to a different
 * value and doesn't do anything with inner headers as they aren't pointing into
 * a normal IP singleton inner packet.
 *
 * Return: 0 on success or a negative error code on failure
 */
static int iptfs_encap_add_ipv6(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct ipv6hdr *top_iph;
	int dsfield;

	skb_reset_inner_network_header(skb);
	skb_reset_inner_transport_header(skb);

	skb_set_network_header(skb, -x->props.header_len + x->props.enc_hdr_len);
	skb->mac_header = skb->network_header + offsetof(struct ipv6hdr, nexthdr);
	skb->transport_header = skb->network_header + sizeof(*top_iph);

	top_iph = ipv6_hdr(skb);
	top_iph->version = 6;
	top_iph->priority = 0;
	memset(top_iph->flow_lbl, 0, sizeof(top_iph->flow_lbl));
	top_iph->nexthdr = IPPROTO_AGGFRAG;

	/* As we have 0, fractional, 1 or N inner packets there's no obviously
	 * correct DSCP mapping to inherit. ECN should be cleared per RFC9347
	 * 3.1.
	 */
	dsfield = 0;
	ipv6_change_dsfield(top_iph, 0, dsfield);

	top_iph->hop_limit = ip6_dst_hoplimit(xfrm_dst_child(dst));
	top_iph->saddr = *(struct in6_addr *)&x->props.saddr;
	top_iph->daddr = *(struct in6_addr *)&x->id.daddr;

	return 0;
}
#endif

/**
 * iptfs_prepare_output() -  prepare the skb for output
 * @x: xfrm state
 * @skb: the packet
 *
 * Return: Error value, if 0 then skb values should be as follows:
 *    - transport_header should point at ESP header
 *    - network_header should point at Outer IP header
 *    - mac_header should point at protocol/nexthdr of the outer IP
 */
static int iptfs_prepare_output(struct xfrm_state *x, struct sk_buff *skb)
{
	if (x->outer_mode.family == AF_INET)
		return iptfs_encap_add_ipv4(x, skb);
	if (x->outer_mode.family == AF_INET6) {
#if IS_ENABLED(CONFIG_IPV6)
		return iptfs_encap_add_ipv6(x, skb);
#else
		return -EAFNOSUPPORT;
#endif
	}
	return -EOPNOTSUPP;
}

/* ========================== */
/* State Management Functions */
/* ========================== */

/**
 * iptfs_get_inner_mtu() - return inner MTU with no fragmentation.
 * @x: xfrm state.
 * @outer_mtu: the outer mtu
 */
static u32 iptfs_get_inner_mtu(struct xfrm_state *x, int outer_mtu)
{
	struct crypto_aead *aead;
	u32 blksize;

	aead = x->data;
	blksize = ALIGN(crypto_aead_blocksize(aead), 4);
	return ((outer_mtu - x->props.header_len - crypto_aead_authsize(aead)) &
		~(blksize - 1)) - 2;
}

/**
 * iptfs_user_init() - initialize the SA with IPTFS options from netlink.
 * @net: the net data
 * @x: xfrm state
 * @attrs: netlink attributes
 * @extack: extack return data
 *
 * Return: 0 on success or a negative error code on failure
 */
static int iptfs_user_init(struct net *net, struct xfrm_state *x,
			   struct nlattr **attrs,
			   struct netlink_ext_ack *extack)
{
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct xfrm_iptfs_config *xc;
	u64 q;

	xc = &xtfs->cfg;
	xc->max_queue_size = IPTFS_DEFAULT_MAX_QUEUE_SIZE;
	xtfs->init_delay_ns = IPTFS_DEFAULT_INIT_DELAY_USECS * NSECS_IN_USEC;

	if (attrs[XFRMA_IPTFS_PKT_SIZE]) {
		xc->pkt_size = nla_get_u32(attrs[XFRMA_IPTFS_PKT_SIZE]);
		if (!xc->pkt_size) {
			xtfs->payload_mtu = 0;
		} else if (xc->pkt_size > x->props.header_len) {
			xtfs->payload_mtu = xc->pkt_size - x->props.header_len;
		} else {
			NL_SET_ERR_MSG(extack,
				       "Packet size must be 0 or greater than IPTFS/ESP header length");
			return -EINVAL;
		}
	}
	if (attrs[XFRMA_IPTFS_MAX_QSIZE])
		xc->max_queue_size = nla_get_u32(attrs[XFRMA_IPTFS_MAX_QSIZE]);
	if (attrs[XFRMA_IPTFS_INIT_DELAY])
		xtfs->init_delay_ns =
			(u64)nla_get_u32(attrs[XFRMA_IPTFS_INIT_DELAY]) * NSECS_IN_USEC;

	q = (u64)xc->max_queue_size * 95;
	do_div(q, 100);
	xtfs->ecn_queue_size = (u32)q;

	return 0;
}

static unsigned int iptfs_sa_len(const struct xfrm_state *x)
{
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct xfrm_iptfs_config *xc = &xtfs->cfg;
	unsigned int l = 0;

	if (x->dir == XFRM_SA_DIR_OUT) {
		l += nla_total_size(sizeof(u32)); /* init delay usec */
		l += nla_total_size(sizeof(xc->max_queue_size));
		l += nla_total_size(sizeof(xc->pkt_size));
	}

	return l;
}

static int iptfs_copy_to_user(struct xfrm_state *x, struct sk_buff *skb)
{
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct xfrm_iptfs_config *xc = &xtfs->cfg;
	int ret = 0;
	u64 q;

	if (x->dir == XFRM_SA_DIR_OUT) {
		q = xtfs->init_delay_ns;
		do_div(q, NSECS_IN_USEC);
		ret = nla_put_u32(skb, XFRMA_IPTFS_INIT_DELAY, q);
		if (ret)
			return ret;

		ret = nla_put_u32(skb, XFRMA_IPTFS_MAX_QSIZE, xc->max_queue_size);
		if (ret)
			return ret;

		ret = nla_put_u32(skb, XFRMA_IPTFS_PKT_SIZE, xc->pkt_size);
	}

	return ret;
}

static void __iptfs_init_state(struct xfrm_state *x,
			       struct xfrm_iptfs_data *xtfs)
{
	__skb_queue_head_init(&xtfs->queue);
	hrtimer_init(&xtfs->iptfs_timer, CLOCK_MONOTONIC, IPTFS_HRTIMER_MODE);
	xtfs->iptfs_timer.function = iptfs_delay_timer;

	/* Modify type (esp) adjustment values */

	if (x->props.family == AF_INET)
		x->props.header_len += sizeof(struct iphdr) + sizeof(struct ip_iptfs_hdr);
	else if (x->props.family == AF_INET6)
		x->props.header_len += sizeof(struct ipv6hdr) + sizeof(struct ip_iptfs_hdr);
	x->props.enc_hdr_len = sizeof(struct ip_iptfs_hdr);

	/* Always keep a module reference when x->mode_data is set */
	__module_get(x->mode_cbs->owner);

	x->mode_data = xtfs;
	xtfs->x = x;
}

static int iptfs_clone_state(struct xfrm_state *x, struct xfrm_state *orig)
{
	struct xfrm_iptfs_data *xtfs;

	xtfs = kmemdup(orig->mode_data, sizeof(*xtfs), GFP_KERNEL);
	if (!xtfs)
		return -ENOMEM;

	x->mode_data = xtfs;
	xtfs->x = x;

	return 0;
}

static int iptfs_init_state(struct xfrm_state *x)
{
	struct xfrm_iptfs_data *xtfs;

	if (x->mode_data) {
		/* We have arrived here from xfrm_state_clone() */
		xtfs = x->mode_data;
	} else {
		xtfs = kzalloc(sizeof(*xtfs), GFP_KERNEL);
		if (!xtfs)
			return -ENOMEM;
	}

	__iptfs_init_state(x, xtfs);

	return 0;
}

static void iptfs_destroy_state(struct xfrm_state *x)
{
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct sk_buff_head list;
	struct sk_buff *skb;

	if (!xtfs)
		return;

	spin_lock_bh(&xtfs->x->lock);
	hrtimer_cancel(&xtfs->iptfs_timer);
	__skb_queue_head_init(&list);
	skb_queue_splice_init(&xtfs->queue, &list);
	spin_unlock_bh(&xtfs->x->lock);

	while ((skb = __skb_dequeue(&list)))
		kfree_skb(skb);

	kfree_sensitive(xtfs);

	module_put(x->mode_cbs->owner);
}

static const struct xfrm_mode_cbs iptfs_mode_cbs = {
	.owner = THIS_MODULE,
	.init_state = iptfs_init_state,
	.clone_state = iptfs_clone_state,
	.destroy_state = iptfs_destroy_state,
	.user_init = iptfs_user_init,
	.copy_to_user = iptfs_copy_to_user,
	.sa_len = iptfs_sa_len,
	.get_inner_mtu = iptfs_get_inner_mtu,
	.output = iptfs_output_collect,
	.prepare_output = iptfs_prepare_output,
};

static int __init xfrm_iptfs_init(void)
{
	int err;

	pr_info("xfrm_iptfs: IPsec IP-TFS tunnel mode module\n");

	err = xfrm_register_mode_cbs(XFRM_MODE_IPTFS, &iptfs_mode_cbs);
	if (err < 0)
		pr_info("%s: can't register IP-TFS\n", __func__);

	return err;
}

static void __exit xfrm_iptfs_fini(void)
{
	xfrm_unregister_mode_cbs(XFRM_MODE_IPTFS);
}

module_init(xfrm_iptfs_init);
module_exit(xfrm_iptfs_fini);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IP-TFS support for xfrm ipsec tunnels");
