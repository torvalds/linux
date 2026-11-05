// SPDX-License-Identifier: GPL-2.0
/*
 * llc_input.c - Minimal input path for LLC
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */
#include <linux/netdevice.h>
#include <net/llc.h>
#include <net/llc_pdu.h>

#if 0
#define dprintk(args...) printk(KERN_DEBUG args)
#else
#define dprintk(args...)
#endif

/**
 *	llc_fixup_skb - initializes skb pointers
 *	@skb: This argument points to incoming skb
 *
 *	Initializes internal skb pointer to start of network layer by deriving
 *	length of LLC header; finds length of LLC control field in LLC header
 *	by looking at the two lowest-order bits of the first control field
 *	byte; field is either 3 or 4 bytes long.
 */
static inline int llc_fixup_skb(struct sk_buff *skb)
{
	u8 llc_len = 2;
	struct llc_pdu_un *pdu;

	if (unlikely(!pskb_may_pull(skb, sizeof(*pdu))))
		return 0;

	pdu = (struct llc_pdu_un *)skb->data;
	if ((pdu->ctrl_1 & LLC_PDU_TYPE_MASK) == LLC_PDU_TYPE_U)
		llc_len = 1;
	llc_len += 2;

	if (unlikely(!pskb_may_pull(skb, llc_len)))
		return 0;

	skb_pull(skb, llc_len);
	skb_reset_transport_header(skb);
	if (skb->protocol == htons(ETH_P_802_2)) {
		__be16 pdulen;
		s32 data_size;

		if (skb->mac_len < ETH_HLEN)
			return 0;

		pdulen = eth_hdr(skb)->h_proto;
		data_size = ntohs(pdulen) - llc_len;

		if (data_size < 0 ||
		    !pskb_may_pull(skb, data_size))
			return 0;
		if (unlikely(pskb_trim_rcsum(skb, data_size)))
			return 0;
	}
	return 1;
}

/**
 *	llc_rcv - 802.2 entry point from net lower layers
 *	@skb: received pdu
 *	@dev: device that receive pdu
 *	@pt: packet type
 *	@orig_dev: the original receive net device
 *
 *	When the system receives a 802.2 frame this function is called. It
 *	looks up the SAP the pdu is addressed to and passes the frame to its
 *	handler, dropping it if the SAP is unknown or has no handler.
 */
int llc_rcv(struct sk_buff *skb, struct net_device *dev,
	    struct packet_type *pt, struct net_device *orig_dev)
{
	int (*rcv)(struct sk_buff *, struct net_device *,
		   struct packet_type *, struct net_device *);
	struct llc_pdu_un *pdu;
	struct llc_sap *sap;

	/*
	 * When the interface is in promisc. mode, drop all the crap that it
	 * receives, do not try to analyse it.
	 */
	if (unlikely(skb->pkt_type == PACKET_OTHERHOST)) {
		dprintk("%s: PACKET_OTHERHOST\n", __func__);
		goto drop;
	}
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		goto out;
	if (unlikely(!llc_fixup_skb(skb)))
		goto drop;
	pdu = llc_pdu_un_hdr(skb);
	sap = llc_sap_find(pdu->dsap);
	if (unlikely(!sap)) {/* unknown SAP */
		dprintk("%s: llc_sap_find(%02X) failed!\n", __func__,
			pdu->dsap);
		goto drop;
	}
	rcv = rcu_dereference(sap->rcv_func);
	if (likely(rcv))
		rcv(skb, dev, pt, orig_dev);
	else
		kfree_skb(skb);
	llc_sap_put(sap);
out:
	return 0;
drop:
	kfree_skb(skb);
	goto out;
}
