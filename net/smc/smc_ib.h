/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for IB environment
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <Ursula Braun@linux.vnet.ibm.com>
 */

#ifndef _SMC_IB_H
#define _SMC_IB_H

#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <rdma/ib_verbs.h>
#include <net/smc.h>

#define SMC_MAX_PORTS			2	/* Max # of ports */
#define SMC_GID_SIZE			sizeof(union ib_gid)

#define SMC_IB_MAX_SEND_SGE		2

struct smc_ib_devices {			/* list of smc ib devices definition */
	struct list_head	list;
	spinlock_t		lock;	/* protects list of smc ib devices */
};

extern struct smc_ib_devices	smc_ib_devices; /* list of smc ib devices */

struct smc_ib_device {				/* ib-device infos for smc */
	struct list_head	list;
	struct ib_device	*ibdev;
	struct ib_port_attr	pattr[SMC_MAX_PORTS];	/* ib dev. port attrs */
	struct ib_event_handler	event_handler;	/* global ib_event handler */
	struct ib_cq		*roce_cq_send;	/* send completion queue */
	struct ib_cq		*roce_cq_recv;	/* recv completion queue */
	struct tasklet_struct	send_tasklet;	/* called by send cq handler */
	struct tasklet_struct	recv_tasklet;	/* called by recv cq handler */
	char			mac[SMC_MAX_PORTS][ETH_ALEN];
						/* mac address per port*/
	u8			pnetid[SMC_MAX_PORTS][SMC_MAX_PNETID_LEN];
						/* pnetid per port */
	bool			pnetid_by_user[SMC_MAX_PORTS];
						/* pnetid defined by user? */
	u8			initialized : 1; /* ib dev CQ, evthdl done */
	struct work_struct	port_event_work;
	unsigned long		port_event_mask;
	DECLARE_BITMAP(ports_going_away, SMC_MAX_PORTS);
};

struct smc_buf_desc;
struct smc_link;

int smc_ib_register_client(void) __init;
void smc_ib_unregister_client(void);
bool smc_ib_port_active(struct smc_ib_device *smcibdev, u8 ibport);
int smc_ib_buf_map_sg(struct smc_ib_device *smcibdev,
		      struct smc_buf_desc *buf_slot,
		      enum dma_data_direction data_direction);
void smc_ib_buf_unmap_sg(struct smc_ib_device *smcibdev,
			 struct smc_buf_desc *buf_slot,
			 enum dma_data_direction data_direction);
void smc_ib_dealloc_protection_domain(struct smc_link *lnk);
int smc_ib_create_protection_domain(struct smc_link *lnk);
void smc_ib_destroy_queue_pair(struct smc_link *lnk);
int smc_ib_create_queue_pair(struct smc_link *lnk);
int smc_ib_ready_link(struct smc_link *lnk);
int smc_ib_modify_qp_rts(struct smc_link *lnk);
int smc_ib_modify_qp_reset(struct smc_link *lnk);
long smc_ib_setup_per_ibdev(struct smc_ib_device *smcibdev);
int smc_ib_get_memory_region(struct ib_pd *pd, int access_flags,
			     struct smc_buf_desc *buf_slot);
void smc_ib_put_memory_region(struct ib_mr *mr);
void smc_ib_sync_sg_for_cpu(struct smc_ib_device *smcibdev,
			    struct smc_buf_desc *buf_slot,
			    enum dma_data_direction data_direction);
void smc_ib_sync_sg_for_device(struct smc_ib_device *smcibdev,
			       struct smc_buf_desc *buf_slot,
			       enum dma_data_direction data_direction);
int smc_ib_determine_gid(struct smc_ib_device *smcibdev, u8 ibport,
			 unsigned short vlan_id, u8 gid[], u8 *sgid_index);
#endif
