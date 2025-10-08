/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Support for SATA devices on Serial Attached SCSI (SAS) controllers
 *
 * Copyright (C) 2006 IBM Corporation
 *
 * Written by: Darrick J. Wong <djwong@us.ibm.com>, IBM Corporation
 */

#ifndef _SAS_ATA_H_
#define _SAS_ATA_H_

#include <linux/libata.h>
#include <scsi/libsas.h>

#ifdef CONFIG_SCSI_SAS_ATA

static inline bool dev_is_sata(struct domain_device *dev)
{
	switch (dev->dev_type) {
	case SAS_SATA_DEV:
	case SAS_SATA_PENDING:
	case SAS_SATA_PM:
	case SAS_SATA_PM_PORT:
		return true;
	default:
		return false;
	}
}

void sas_ata_schedule_reset(struct domain_device *dev);
void sas_ata_device_link_abort(struct domain_device *dev, bool force_reset);
int sas_execute_ata_cmd(struct domain_device *device, u8 *fis, int force_phy_id);
int smp_ata_check_ready_type(struct ata_link *link);

extern const struct attribute_group sas_ata_sdev_attr_group;

#else

static inline bool dev_is_sata(struct domain_device *dev)
{
	return false;
}

static inline void sas_ata_schedule_reset(struct domain_device *dev)
{
}

static inline void sas_ata_device_link_abort(struct domain_device *dev,
					     bool force_reset)
{
}

static inline int sas_execute_ata_cmd(struct domain_device *device, u8 *fis,
				      int force_phy_id)
{
	return 0;
}

static inline int smp_ata_check_ready_type(struct ata_link *link)
{
	return 0;
}

#define sas_ata_sdev_attr_group ((struct attribute_group) {})

#endif

#endif /* _SAS_ATA_H_ */
