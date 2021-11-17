/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _IIDC_H_
#define _IIDC_H_

#include <linux/auxiliary_bus.h>
#include <linux/dcbnl.h>
#include <linux/device.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

enum iidc_event_type {
	IIDC_EVENT_BEFORE_MTU_CHANGE,
	IIDC_EVENT_AFTER_MTU_CHANGE,
	IIDC_EVENT_BEFORE_TC_CHANGE,
	IIDC_EVENT_AFTER_TC_CHANGE,
	IIDC_EVENT_CRIT_ERR,
	IIDC_EVENT_NBITS		/* must be last */
};

enum iidc_reset_type {
	IIDC_PFR,
	IIDC_CORER,
	IIDC_GLOBR,
};

#define IIDC_MAX_USER_PRIORITY		8

/* Struct to hold per RDMA Qset info */
struct iidc_rdma_qset_params {
	/* Qset TEID returned to the RDMA driver in
	 * ice_add_rdma_qset and used by RDMA driver
	 * for calls to ice_del_rdma_qset
	 */
	u32 teid;	/* Qset TEID */
	u16 qs_handle; /* RDMA driver provides this */
	u16 vport_id; /* VSI index */
	u8 tc; /* TC branch the Qset should belong to */
};

struct iidc_qos_info {
	u64 tc_ctx;
	u8 rel_bw;
	u8 prio_type;
	u8 egress_virt_up;
	u8 ingress_virt_up;
};

/* Struct to pass QoS info */
struct iidc_qos_params {
	struct iidc_qos_info tc_info[IEEE_8021QAZ_MAX_TCS];
	u8 up2tc[IIDC_MAX_USER_PRIORITY];
	u8 vport_relative_bw;
	u8 vport_priority_type;
	u8 num_tc;
};

struct iidc_event {
	DECLARE_BITMAP(type, IIDC_EVENT_NBITS);
	u32 reg;
};

struct ice_pf;

int ice_add_rdma_qset(struct ice_pf *pf, struct iidc_rdma_qset_params *qset);
int ice_del_rdma_qset(struct ice_pf *pf, struct iidc_rdma_qset_params *qset);
int ice_rdma_request_reset(struct ice_pf *pf, enum iidc_reset_type reset_type);
int ice_rdma_update_vsi_filter(struct ice_pf *pf, u16 vsi_id, bool enable);
void ice_get_qos_params(struct ice_pf *pf, struct iidc_qos_params *qos);

#define IIDC_RDMA_ROCE_NAME	"roce"

/* Structure representing auxiliary driver tailored information about the core
 * PCI dev, each auxiliary driver using the IIDC interface will have an
 * instance of this struct dedicated to it.
 */

struct iidc_auxiliary_dev {
	struct auxiliary_device adev;
	struct ice_pf *pf;
};

/* structure representing the auxiliary driver. This struct is to be
 * allocated and populated by the auxiliary driver's owner. The core PCI
 * driver will access these ops by performing a container_of on the
 * auxiliary_device->dev.driver.
 */
struct iidc_auxiliary_drv {
	struct auxiliary_driver adrv;
	/* This event_handler is meant to be a blocking call.  For instance,
	 * when a BEFORE_MTU_CHANGE event comes in, the event_handler will not
	 * return until the auxiliary driver is ready for the MTU change to
	 * happen.
	 */
	void (*event_handler)(struct ice_pf *pf, struct iidc_event *event);
};

#endif /* _IIDC_H_*/
