// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sctp_offload - GRO/GSO Offloading for SCTP
 *
 * Copyright (C) 2015, Marcelo Ricardo Leitner <marcelo.leitner@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/socket.h>
#include <linux/sctp.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/kfifo.h>
#include <linux/time.h>
#include <net/net_namespace.h>

#include <linux/skbuff.h>
#include <net/sctp/sctp.h>
#include <net/sctp/checksum.h>
#include <net/protocol.h>
#include <net/gso.h>

static __le32 sctp_gso_make_checksum(struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_NONE;
	skb->csum_not_inet = 0;
	/* csum and csum_start in GSO CB may be needed to do the UDP
	 * checksum when it's a UDP tunneling packet.
	 */
	SKB_GSO_CB(skb)->csum = (__force __wsum)~0;
	SKB_GSO_CB(skb)->csum_start = skb_headroom(skb) + skb->len;
	return sctp_compute_cksum(skb, skb_transport_offset(skb));
}

static struct sk_buff *sctp_gso_segment(struct sk_buff *skb,
					netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	struct sctphdr *sh;

	if (!skb_is_gso_sctp(skb))
		goto out;

	sh = sctp_hdr(skb);
	if (!pskb_may_pull(skb, sizeof(*sh)))
		goto out;

	__skb_pull(skb, sizeof(*sh));

	if (skb_gso_ok(skb, features | NETIF_F_GSO_ROBUST)) {
		/* Packet is from an untrusted source, reset gso_segs. */
		struct skb_shared_info *pinfo = skb_shinfo(skb);
		struct sk_buff *frag_iter;

		pinfo->gso_segs = 0;
		if (skb->len != skb->data_len) {
			/* Means we have chunks in here too */
			pinfo->gso_segs++;
		}

		skb_walk_frags(skb, frag_iter)
			pinfo->gso_segs++;

		segs = NULL;
		goto out;
	}

	segs = skb_segment(skb, (features | NETIF_F_HW_CSUM) & ~NETIF_F_SG);
	if (IS_ERR(segs))
		goto out;

	/* All that is left is update SCTP CRC if necessary */
	if (!(features & NETIF_F_SCTP_CRC)) {
		for (skb = segs; skb; skb = skb->next) {
			if (skb->ip_summed == CHECKSUM_PARTIAL) {
				sh = sctp_hdr(skb);
				sh->checksum = sctp_gso_make_checksum(skb);
			}
		}
	}

out:
	return segs;
}

static const struct net_offload sctp_offload = {
	.callbacks = {
		.gso_segment = sctp_gso_segment,
	},
};

static const struct net_offload sctp6_offload = {
	.callbacks = {
		.gso_segment = sctp_gso_segment,
	},
};

int __init sctp_offload_init(void)
{
	int ret;

	ret = inet_add_offload(&sctp_offload, IPPROTO_SCTP);
	if (ret)
		goto out;

	ret = inet6_add_offload(&sctp6_offload, IPPROTO_SCTP);
	if (ret)
		goto ipv4;

	crc32c_csum_stub = &sctp_csum_ops;
	return ret;

ipv4:
	inet_del_offload(&sctp_offload, IPPROTO_SCTP);
out:
	return ret;
}
