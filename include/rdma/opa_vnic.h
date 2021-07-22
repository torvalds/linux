/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2017 - 2020 Intel Corporation.
 */

#ifndef _OPA_VNIC_H
#define _OPA_VNIC_H

/*
 * This file contains Intel Omni-Path (OPA) Virtual Network Interface
 * Controller (VNIC) specific declarations.
 */

#include <rdma/ib_verbs.h>

/* 16 header bytes + 2 reserved bytes */
#define OPA_VNIC_L2_HDR_LEN   (16 + 2)

#define OPA_VNIC_L4_HDR_LEN   2

#define OPA_VNIC_HDR_LEN      (OPA_VNIC_L2_HDR_LEN + \
			       OPA_VNIC_L4_HDR_LEN)

#define OPA_VNIC_L4_ETHR  0x78

#define OPA_VNIC_ICRC_LEN   4
#define OPA_VNIC_TAIL_LEN   1
#define OPA_VNIC_ICRC_TAIL_LEN  (OPA_VNIC_ICRC_LEN + OPA_VNIC_TAIL_LEN)

#define OPA_VNIC_SKB_MDATA_LEN         4
#define OPA_VNIC_SKB_MDATA_ENCAP_ERR   0x1

/* opa vnic rdma netdev's private data structure */
struct opa_vnic_rdma_netdev {
	struct rdma_netdev rn;  /* keep this first */
	/* followed by device private data */
	char *dev_priv[];
};

static inline void *opa_vnic_priv(const struct net_device *dev)
{
	struct rdma_netdev *rn = netdev_priv(dev);

	return rn->clnt_priv;
}

static inline void *opa_vnic_dev_priv(const struct net_device *dev)
{
	struct opa_vnic_rdma_netdev *oparn = netdev_priv(dev);

	return oparn->dev_priv;
}

/* opa_vnic skb meta data structrue */
struct opa_vnic_skb_mdata {
	u8 vl;
	u8 entropy;
	u8 flags;
	u8 rsvd;
} __packed;

/* OPA VNIC group statistics */
struct opa_vnic_grp_stats {
	u64 unicast;
	u64 mcastbcast;
	u64 untagged;
	u64 vlan;
	u64 s_64;
	u64 s_65_127;
	u64 s_128_255;
	u64 s_256_511;
	u64 s_512_1023;
	u64 s_1024_1518;
	u64 s_1519_max;
};

struct opa_vnic_stats {
	/* standard netdev statistics */
	struct rtnl_link_stats64 netstats;

	/* OPA VNIC statistics */
	struct opa_vnic_grp_stats tx_grp;
	struct opa_vnic_grp_stats rx_grp;
	u64 tx_dlid_zero;
	u64 tx_drop_state;
	u64 rx_drop_state;
	u64 rx_runt;
	u64 rx_oversize;
};

static inline bool rdma_cap_opa_vnic(struct ib_device *device)
{
	return !!(device->attrs.device_cap_flags &
		  IB_DEVICE_RDMA_NETDEV_OPA);
}

#endif /* _OPA_VNIC_H */
