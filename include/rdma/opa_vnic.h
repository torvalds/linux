#ifndef _OPA_VNIC_H
#define _OPA_VNIC_H
/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file contains Intel Omni-Path (OPA) Virtual Network Interface
 * Controller (VNIC) specific declarations.
 */

#include <rdma/ib_verbs.h>

/* VNIC uses 16B header format */
#define OPA_VNIC_L2_TYPE    0x2

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
	char *dev_priv[0];
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
		  IB_DEVICE_RDMA_NETDEV_OPA_VNIC);
}

#endif /* _OPA_VNIC_H */
