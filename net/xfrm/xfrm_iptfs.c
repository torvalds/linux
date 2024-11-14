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
#include <net/gro.h>
#include <net/icmp.h>
#include <net/ip6_route.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>

#include <crypto/aead.h>

#include "xfrm_inout.h"

/**
 * struct xfrm_iptfs_config - configuration for the IPTFS tunnel.
 * @pkt_size: size of the outer IP packet. 0 to use interface and MTU discovery,
 *	otherwise the user specified value.
 */
struct xfrm_iptfs_config {
	u32 pkt_size;	    /* outer_packet_size or 0 */
};

/**
 * struct xfrm_iptfs_data - mode specific xfrm state.
 * @cfg: IPTFS tunnel config.
 * @x: owning SA (xfrm_state).
 * @payload_mtu: max payload size.
 */
struct xfrm_iptfs_data {
	struct xfrm_iptfs_config cfg;

	/* Ingress User Input */
	struct xfrm_state *x;	    /* owning state */
	u32 payload_mtu;	    /* max payload size */
};

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

	xc = &xtfs->cfg;

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
	return 0;
}

static unsigned int iptfs_sa_len(const struct xfrm_state *x)
{
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct xfrm_iptfs_config *xc = &xtfs->cfg;
	unsigned int l = 0;

	if (x->dir == XFRM_SA_DIR_OUT)
		l += nla_total_size(sizeof(xc->pkt_size));

	return l;
}

static int iptfs_copy_to_user(struct xfrm_state *x, struct sk_buff *skb)
{
	struct xfrm_iptfs_data *xtfs = x->mode_data;
	struct xfrm_iptfs_config *xc = &xtfs->cfg;
	int ret = 0;

	if (x->dir == XFRM_SA_DIR_OUT)
		ret = nla_put_u32(skb, XFRMA_IPTFS_PKT_SIZE, xc->pkt_size);

	return ret;
}

static void __iptfs_init_state(struct xfrm_state *x,
			       struct xfrm_iptfs_data *xtfs)
{
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

	if (!xtfs)
		return;

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
