/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021-2025, Intel Corporation. */

#ifndef _IIDC_RDMA_ICE_H_
#define _IIDC_RDMA_ICE_H_

#include <linux/dcbnl.h>

#define IIDC_MAX_USER_PRIORITY         8
#define IIDC_DSCP_PFC_MODE             0x1

/**
 * struct iidc_rdma_qset_params - Struct to hold per RDMA Qset info
 * @teid: TEID of the Qset node
 * @qs_handle: SW index of the Qset, RDMA provides this
 * @vport_id: VSI index
 * @tc: Traffic Class branch the QSet should belong to
 */
struct iidc_rdma_qset_params {
	/* Qset TEID returned to the RDMA driver in
	 * ice_add_rdma_qset and used by RDMA driver
	 * for calls to ice_del_rdma_qset
	 */
	u32 teid;
	u16 qs_handle;
	u16 vport_id;
	u8 tc;
};

struct iidc_rdma_qos_info {
	u64 tc_ctx;
	u8 rel_bw;
	u8 prio_type;
	u8 egress_virt_up;
	u8 ingress_virt_up;
};

/* Struct to pass QoS info */
struct iidc_rdma_qos_params {
	struct iidc_rdma_qos_info tc_info[IEEE_8021QAZ_MAX_TCS];
	u8 up2tc[IIDC_MAX_USER_PRIORITY];
	u8 vport_relative_bw;
	u8 vport_priority_type;
	u8 num_tc;
	u8 pfc_mode;
	u8 dscp_map[DSCP_MAX];
};

struct iidc_rdma_priv_dev_info {
	u8 pf_id;
	u16 vport_id;
	struct net_device *netdev;
	struct iidc_rdma_qos_params qos_info;
	u8 __iomem *hw_addr;
};

int ice_add_rdma_qset(struct iidc_rdma_core_dev_info *cdev,
		      struct iidc_rdma_qset_params *qset);
int ice_del_rdma_qset(struct iidc_rdma_core_dev_info *cdev,
		      struct iidc_rdma_qset_params *qset);
int ice_rdma_request_reset(struct iidc_rdma_core_dev_info *cdev,
			   enum iidc_rdma_reset_type reset_type);
int ice_rdma_update_vsi_filter(struct iidc_rdma_core_dev_info *cdev, u16 vsi_id,
			       bool enable);
int ice_alloc_rdma_qvector(struct iidc_rdma_core_dev_info *cdev,
			   struct msix_entry *entry);
void ice_free_rdma_qvector(struct iidc_rdma_core_dev_info *cdev,
			   struct msix_entry *entry);

#endif /* _IIDC_RDMA_ICE_H_*/
