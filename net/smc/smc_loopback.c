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

#include "smc_ism.h"
#include "smc_loopback.h"

#define SMC_LO_V2_CAPABLE	0x1 /* loopback-ism acts as ISMv2 */

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
	.register_dmb		= NULL,
	.unregister_dmb		= NULL,
	.add_vlan_id		= NULL,
	.del_vlan_id		= NULL,
	.set_vlan_required	= NULL,
	.reset_vlan_required	= NULL,
	.signal_event		= NULL,
	.move_data		= NULL,
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

	/* TODO:
	 * register loopback-ism to smcd_dev list.
	 */
	return 0;
}

static void smcd_lo_unregister_dev(struct smc_lo_dev *ldev)
{
	struct smcd_dev *smcd = ldev->smcd;

	/* TODO:
	 * unregister loopback-ism from smcd_dev list.
	 */
	kfree(smcd->conn);
	kfree(smcd);
}

static int smc_lo_dev_init(struct smc_lo_dev *ldev)
{
	smc_lo_generate_ids(ldev);
	return smcd_lo_register_dev(ldev);
}

static void smc_lo_dev_exit(struct smc_lo_dev *ldev)
{
	smcd_lo_unregister_dev(ldev);
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
