/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for the SMC module (socket related)
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */
#ifndef _SMC_H
#define _SMC_H

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/dibs.h>

struct sock;

#define SMC_MAX_PNETID_LEN	16	/* Max. length of PNET id */

struct smc_hashinfo {
	rwlock_t lock;
	struct hlist_head ht;
};

/* SMCD/ISM device driver interface */
#define ISM_RESERVED_VLANID	0x1FFF

struct smcd_gid {
	u64	gid;
	u64	gid_ext;
};

struct smcd_dev {
	struct dibs_dev *dibs;
	struct list_head list;
	spinlock_t lock;
	struct smc_connection **conn;
	struct list_head vlan;
	struct workqueue_struct *event_wq;
	u8 pnetid[SMC_MAX_PNETID_LEN];
	bool pnetid_by_user;
	struct list_head lgr_list;
	spinlock_t lgr_lock;
	atomic_t lgr_cnt;
	wait_queue_head_t lgrs_deleted;
	u8 going_away : 1;
};

#endif	/* _SMC_H */
