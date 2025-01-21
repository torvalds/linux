/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications Direct over loopback-ism device.
 *
 *  SMC-D loopback-ism device structure definitions.
 *
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: Wen Gu <guwen@linux.alibaba.com>
 *          Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#ifndef _SMC_LOOPBACK_H
#define _SMC_LOOPBACK_H

#include <linux/device.h>
#include <net/smc.h>

#if IS_ENABLED(CONFIG_SMC_LO)
#define SMC_LO_MAX_DMBS		5000
#define SMC_LO_DMBS_HASH_BITS	12
#define SMC_LO_RESERVED_CHID	0xFFFF

struct smc_lo_dmb_node {
	struct hlist_node list;
	u64 token;
	u32 len;
	u32 sba_idx;
	void *cpu_addr;
	dma_addr_t dma_addr;
	refcount_t refcnt;
};

struct smc_lo_dev {
	struct smcd_dev *smcd;
	struct device dev;
	u16 chid;
	struct smcd_gid local_gid;
	atomic_t dmb_cnt;
	rwlock_t dmb_ht_lock;
	DECLARE_BITMAP(sba_idx_mask, SMC_LO_MAX_DMBS);
	DECLARE_HASHTABLE(dmb_ht, SMC_LO_DMBS_HASH_BITS);
	wait_queue_head_t ldev_release;
};

int smc_loopback_init(void);
void smc_loopback_exit(void);
#else
static inline int smc_loopback_init(void)
{
	return 0;
}

static inline void smc_loopback_exit(void)
{
}
#endif

#endif /* _SMC_LOOPBACK_H */
