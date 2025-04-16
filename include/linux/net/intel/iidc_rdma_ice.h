/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021-2025, Intel Corporation. */

#ifndef _IIDC_RDMA_ICE_H_
#define _IIDC_RDMA_ICE_H_

struct ice_pf;

int ice_add_rdma_qset(struct ice_pf *pf, struct iidc_rdma_qset_params *qset);
int ice_del_rdma_qset(struct ice_pf *pf, struct iidc_rdma_qset_params *qset);
int ice_rdma_request_reset(struct ice_pf *pf,
			   enum iidc_rdma_reset_type reset_type);
int ice_rdma_update_vsi_filter(struct ice_pf *pf, u16 vsi_id, bool enable);
void ice_get_qos_params(struct ice_pf *pf,
			struct iidc_rdma_qos_params *qos);
int ice_alloc_rdma_qvector(struct ice_pf *pf, struct msix_entry *entry);
void ice_free_rdma_qvector(struct ice_pf *pf, struct msix_entry *entry);

#endif /* _IIDC_RDMA_ICE_H_*/
