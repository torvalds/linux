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
#include <net/smc.h>

#include "smc_cdc.h"
#include "smc_ism.h"
#include "smc_loopback.h"

#define SMC_LO_V2_CAPABLE	0x1 /* loopback-ism acts as ISMv2 */
#define SMC_LO_SUPPORT_NOCOPY	0x1
#define SMC_DMA_ADDR_INVALID	(~(dma_addr_t)0)

static const char smc_lo_dev_name[] = "loopback-ism";
static struct smc_lo_dev *lo_dev;

static void smc_lo_generate_ids(struct smc_lo_dev *ldev)
{
	struct smcd_gid *lgid = &ldev->local_gid;
	uuid_t uuid;

	uuid_gen(&uuid);
	memcpy(&lgid->gid, &uuid, sizeof(lgid->gid));
	memcpy(&lgid->gid_ext, (u8 *)&uuid + sizeof(lgid->gid),
	       sizeof(lgid->gid_ext));

	ldev->chid = SMC_LO_RESERVED_CHID;
}

static int smc_lo_query_rgid(struct smcd_dev *smcd, struct smcd_gid *rgid,
			     u32 vid_valid, u32 vid)
{
	struct smc_lo_dev *ldev = smcd->priv;

	/* rgid should be the same as lgid */
	if (!ldev || rgid->gid != ldev->local_gid.gid ||
	    rgid->gid_ext != ldev->local_gid.gid_ext)
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

	if (!sf)
		/* since sndbuf is merged with peer DMB, there is
		 * no need to copy data from sndbuf to peer DMB.
		 */
		return 0;

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

	conn = smcd->conn[rmb_node->sba_idx];
	if (!conn || conn->killed)
		return -EPIPE;
	tasklet_schedule(&conn->rx_tsklet);
	return 0;
}

static int smc_lo_supports_v2(void)
{
	return SMC_LO_V2_CAPABLE;
}

static void smc_lo_get_local_gid(struct smcd_dev *smcd,
				 struct smcd_gid *smcd_gid)
{
	struct smc_lo_dev *ldev = smcd->priv;

	smcd_gid->gid = ldev->local_gid.gid;
	smcd_gid->gid_ext = ldev->local_gid.gid_ext;
}

static u16 smc_lo_get_chid(struct smcd_dev *smcd)
{
	return ((struct smc_lo_dev *)smcd->priv)->chid;
}

static struct device *smc_lo_get_dev(struct smcd_dev *smcd)
{
	return &((struct smc_lo_dev *)smcd->priv)->dev;
}

static const struct smcd_ops lo_ops = {
	.query_remote_gid = smc_lo_query_rgid,
	.register_dmb = smc_lo_register_dmb,
	.unregister_dmb = smc_lo_unregister_dmb,
	.support_dmb_nocopy = smc_lo_support_dmb_nocopy,
	.attach_dmb = smc_lo_attach_dmb,
	.detach_dmb = smc_lo_detach_dmb,
	.add_vlan_id		= NULL,
	.del_vlan_id		= NULL,
	.set_vlan_required	= NULL,
	.reset_vlan_required	= NULL,
	.signal_event		= NULL,
	.move_data = smc_lo_move_data,
	.supports_v2 = smc_lo_supports_v2,
	.get_local_gid = smc_lo_get_local_gid,
	.get_chid = smc_lo_get_chid,
	.get_dev = smc_lo_get_dev,
};

static struct smcd_dev *smcd_lo_alloc_dev(const struct smcd_ops *ops,
					  int max_dmbs)
{
	struct smcd_dev *smcd;

	smcd = kzalloc(sizeof(*smcd), GFP_KERNEL);
	if (!smcd)
		return NULL;

	smcd->conn = kcalloc(max_dmbs, sizeof(struct smc_connection *),
			     GFP_KERNEL);
	if (!smcd->conn)
		goto out_smcd;

	smcd->ops = ops;

	spin_lock_init(&smcd->lock);
	spin_lock_init(&smcd->lgr_lock);
	INIT_LIST_HEAD(&smcd->vlan);
	INIT_LIST_HEAD(&smcd->lgr_list);
	init_waitqueue_head(&smcd->lgrs_deleted);
	return smcd;

out_smcd:
	kfree(smcd);
	return NULL;
}

static int smcd_lo_register_dev(struct smc_lo_dev *ldev)
{
	struct smcd_dev *smcd;

	smcd = smcd_lo_alloc_dev(&lo_ops, SMC_LO_MAX_DMBS);
	if (!smcd)
		return -ENOMEM;
	ldev->smcd = smcd;
	smcd->priv = ldev;
	smc_ism_set_v2_capable();
	mutex_lock(&smcd_dev_list.mutex);
	list_add(&smcd->list, &smcd_dev_list.list);
	mutex_unlock(&smcd_dev_list.mutex);
	pr_warn_ratelimited("smc: adding smcd device %s\n",
			    dev_name(&ldev->dev));
	return 0;
}

static void smcd_lo_unregister_dev(struct smc_lo_dev *ldev)
{
	struct smcd_dev *smcd = ldev->smcd;

	pr_warn_ratelimited("smc: removing smcd device %s\n",
			    dev_name(&ldev->dev));
	smcd->going_away = 1;
	smc_smcd_terminate_all(smcd);
	mutex_lock(&smcd_dev_list.mutex);
	list_del_init(&smcd->list);
	mutex_unlock(&smcd_dev_list.mutex);
	kfree(smcd->conn);
	kfree(smcd);
}

static int smc_lo_dev_init(struct smc_lo_dev *ldev)
{
	smc_lo_generate_ids(ldev);
	rwlock_init(&ldev->dmb_ht_lock);
	hash_init(ldev->dmb_ht);
	atomic_set(&ldev->dmb_cnt, 0);
	init_waitqueue_head(&ldev->ldev_release);

	return smcd_lo_register_dev(ldev);
}

static void smc_lo_dev_exit(struct smc_lo_dev *ldev)
{
	smcd_lo_unregister_dev(ldev);
	if (atomic_read(&ldev->dmb_cnt))
		wait_event(ldev->ldev_release, !atomic_read(&ldev->dmb_cnt));
}

static void smc_lo_dev_release(struct device *dev)
{
	struct smc_lo_dev *ldev =
		container_of(dev, struct smc_lo_dev, dev);

	kfree(ldev);
}

static int smc_lo_dev_probe(void)
{
	struct smc_lo_dev *ldev;
	int ret;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	ldev->dev.parent = NULL;
	ldev->dev.release = smc_lo_dev_release;
	device_initialize(&ldev->dev);
	dev_set_name(&ldev->dev, smc_lo_dev_name);

	ret = smc_lo_dev_init(ldev);
	if (ret)
		goto free_dev;

	lo_dev = ldev; /* global loopback device */
	return 0;

free_dev:
	put_device(&ldev->dev);
	return ret;
}

static void smc_lo_dev_remove(void)
{
	if (!lo_dev)
		return;

	smc_lo_dev_exit(lo_dev);
	put_device(&lo_dev->dev); /* device_initialize in smc_lo_dev_probe */
}

int smc_loopback_init(void)
{
	return smc_lo_dev_probe();
}

void smc_loopback_exit(void)
{
	smc_lo_dev_remove();
}
