// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications Direct over loopback-ism device.
 *
 *  Functions for loopback-ism device.
 *
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: Wen Gu <guwen@linux.alibaba.com>
 *          Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#include <linux/device.h>
#include <linux/types.h>
#include <linux/dibs.h>
#include <net/smc.h>

#include "smc_cdc.h"
#include "smc_ism.h"
#include "smc_loopback.h"

#define SMC_LO_SUPPORT_NOCOPY	0x1
#define SMC_DMA_ADDR_INVALID	(~(dma_addr_t)0)

static struct smc_lo_dev *lo_dev;

static int smc_lo_query_rgid(struct smcd_dev *smcd, struct smcd_gid *rgid,
			     u32 vid_valid, u32 vid)
{
	uuid_t temp;

	copy_to_dibsgid(&temp, rgid);
	/* rgid should be the same as lgid */
	if (!uuid_equal(&temp, &smcd->dibs->gid))
		return -ENETUNREACH;
	return 0;
}

static int smc_lo_register_dmb(struct smcd_dev *smcd, struct smcd_dmb *dmb,
			       void *client_priv)
{
	struct smc_lo_dmb_node *dmb_node, *tmp_node;
	struct smc_lo_dev *ldev = smcd->priv;
	int sba_idx, rc;

	/* check space for new dmb */
	for_each_clear_bit(sba_idx, ldev->sba_idx_mask, SMC_LO_MAX_DMBS) {
		if (!test_and_set_bit(sba_idx, ldev->sba_idx_mask))
			break;
	}
	if (sba_idx == SMC_LO_MAX_DMBS)
		return -ENOSPC;

	dmb_node = kzalloc(sizeof(*dmb_node), GFP_KERNEL);
	if (!dmb_node) {
		rc = -ENOMEM;
		goto err_bit;
	}

	dmb_node->sba_idx = sba_idx;
	dmb_node->len = dmb->dmb_len;
	dmb_node->cpu_addr = kzalloc(dmb_node->len, GFP_KERNEL |
				     __GFP_NOWARN | __GFP_NORETRY |
				     __GFP_NOMEMALLOC);
	if (!dmb_node->cpu_addr) {
		rc = -ENOMEM;
		goto err_node;
	}
	dmb_node->dma_addr = SMC_DMA_ADDR_INVALID;
	refcount_set(&dmb_node->refcnt, 1);

again:
	/* add new dmb into hash table */
	get_random_bytes(&dmb_node->token, sizeof(dmb_node->token));
	write_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb_node->token) {
		if (tmp_node->token == dmb_node->token) {
			write_unlock_bh(&ldev->dmb_ht_lock);
			goto again;
		}
	}
	hash_add(ldev->dmb_ht, &dmb_node->list, dmb_node->token);
	write_unlock_bh(&ldev->dmb_ht_lock);
	atomic_inc(&ldev->dmb_cnt);

	dmb->sba_idx = dmb_node->sba_idx;
	dmb->dmb_tok = dmb_node->token;
	dmb->cpu_addr = dmb_node->cpu_addr;
	dmb->dma_addr = dmb_node->dma_addr;
	dmb->dmb_len = dmb_node->len;

	return 0;

err_node:
	kfree(dmb_node);
err_bit:
	clear_bit(sba_idx, ldev->sba_idx_mask);
	return rc;
}

static void __smc_lo_unregister_dmb(struct smc_lo_dev *ldev,
				    struct smc_lo_dmb_node *dmb_node)
{
	/* remove dmb from hash table */
	write_lock_bh(&ldev->dmb_ht_lock);
	hash_del(&dmb_node->list);
	write_unlock_bh(&ldev->dmb_ht_lock);

	clear_bit(dmb_node->sba_idx, ldev->sba_idx_mask);
	kvfree(dmb_node->cpu_addr);
	kfree(dmb_node);

	if (atomic_dec_and_test(&ldev->dmb_cnt))
		wake_up(&ldev->ldev_release);
}

static int smc_lo_unregister_dmb(struct smcd_dev *smcd, struct smcd_dmb *dmb)
{
	struct smc_lo_dmb_node *dmb_node = NULL, *tmp_node;
	struct smc_lo_dev *ldev = smcd->priv;

	/* find dmb from hash table */
	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb->dmb_tok) {
		if (tmp_node->token == dmb->dmb_tok) {
			dmb_node = tmp_node;
			break;
		}
	}
	if (!dmb_node) {
		read_unlock_bh(&ldev->dmb_ht_lock);
		return -EINVAL;
	}
	read_unlock_bh(&ldev->dmb_ht_lock);

	if (refcount_dec_and_test(&dmb_node->refcnt))
		__smc_lo_unregister_dmb(ldev, dmb_node);
	return 0;
}

static int smc_lo_support_dmb_nocopy(struct smcd_dev *smcd)
{
	return SMC_LO_SUPPORT_NOCOPY;
}

static int smc_lo_attach_dmb(struct smcd_dev *smcd, struct smcd_dmb *dmb)
{
	struct smc_lo_dmb_node *dmb_node = NULL, *tmp_node;
	struct smc_lo_dev *ldev = smcd->priv;

	/* find dmb_node according to dmb->dmb_tok */
	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb->dmb_tok) {
		if (tmp_node->token == dmb->dmb_tok) {
			dmb_node = tmp_node;
			break;
		}
	}
	if (!dmb_node) {
		read_unlock_bh(&ldev->dmb_ht_lock);
		return -EINVAL;
	}
	read_unlock_bh(&ldev->dmb_ht_lock);

	if (!refcount_inc_not_zero(&dmb_node->refcnt))
		/* the dmb is being unregistered, but has
		 * not been removed from the hash table.
		 */
		return -EINVAL;

	/* provide dmb information */
	dmb->sba_idx = dmb_node->sba_idx;
	dmb->dmb_tok = dmb_node->token;
	dmb->cpu_addr = dmb_node->cpu_addr;
	dmb->dma_addr = dmb_node->dma_addr;
	dmb->dmb_len = dmb_node->len;
	return 0;
}

static int smc_lo_detach_dmb(struct smcd_dev *smcd, u64 token)
{
	struct smc_lo_dmb_node *dmb_node = NULL, *tmp_node;
	struct smc_lo_dev *ldev = smcd->priv;

	/* find dmb_node according to dmb->dmb_tok */
	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, token) {
		if (tmp_node->token == token) {
			dmb_node = tmp_node;
			break;
		}
	}
	if (!dmb_node) {
		read_unlock_bh(&ldev->dmb_ht_lock);
		return -EINVAL;
	}
	read_unlock_bh(&ldev->dmb_ht_lock);

	if (refcount_dec_and_test(&dmb_node->refcnt))
		__smc_lo_unregister_dmb(ldev, dmb_node);
	return 0;
}

static int smc_lo_move_data(struct smcd_dev *smcd, u64 dmb_tok,
			    unsigned int idx, bool sf, unsigned int offset,
			    void *data, unsigned int size)
{
	struct smc_lo_dmb_node *rmb_node = NULL, *tmp_node;
	struct smc_lo_dev *ldev = smcd->priv;
	struct smc_connection *conn;

	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb_tok) {
		if (tmp_node->token == dmb_tok) {
			rmb_node = tmp_node;
			break;
		}
	}
	if (!rmb_node) {
		read_unlock_bh(&ldev->dmb_ht_lock);
		return -EINVAL;
	}
	memcpy((char *)rmb_node->cpu_addr + offset, data, size);
	read_unlock_bh(&ldev->dmb_ht_lock);

	if (!sf)
		return 0;

	conn = smcd->conn[rmb_node->sba_idx];
	if (!conn || conn->killed)
		return -EPIPE;
	tasklet_schedule(&conn->rx_tsklet);
	return 0;
}

static const struct smcd_ops lo_ops = {
	.query_remote_gid = smc_lo_query_rgid,
	.register_dmb = smc_lo_register_dmb,
	.unregister_dmb = smc_lo_unregister_dmb,
	.support_dmb_nocopy = smc_lo_support_dmb_nocopy,
	.attach_dmb = smc_lo_attach_dmb,
	.detach_dmb = smc_lo_detach_dmb,
	.signal_event		= NULL,
	.move_data = smc_lo_move_data,
};

const struct smcd_ops *smc_lo_get_smcd_ops(void)
{
	return &lo_ops;
}

static void smc_lo_dev_init(struct smc_lo_dev *ldev)
{
	rwlock_init(&ldev->dmb_ht_lock);
	hash_init(ldev->dmb_ht);
	atomic_set(&ldev->dmb_cnt, 0);
	init_waitqueue_head(&ldev->ldev_release);

	return;
}

static void smc_lo_dev_exit(struct smc_lo_dev *ldev)
{
	if (atomic_read(&ldev->dmb_cnt))
		wait_event(ldev->ldev_release, !atomic_read(&ldev->dmb_cnt));
}

static int smc_lo_dev_probe(void)
{
	struct smc_lo_dev *ldev;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	smc_lo_dev_init(ldev);

	lo_dev = ldev; /* global loopback device */

	return 0;
}

static void smc_lo_dev_remove(void)
{
	if (!lo_dev)
		return;

	smc_lo_dev_exit(lo_dev);
	kfree(lo_dev);
	lo_dev = NULL;
}

int smc_loopback_init(struct smc_lo_dev **smc_lb)
{
	int ret;

	ret = smc_lo_dev_probe();
	if (!ret)
		*smc_lb = lo_dev;
	return ret;
}

void smc_loopback_exit(void)
{
	smc_lo_dev_remove();
}
