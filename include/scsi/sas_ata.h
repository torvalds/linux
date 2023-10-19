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

static inline int dev_is_sata(struct domain_device *dev)
{
	return dev->dev_type == SAS_SATA_DEV || dev->dev_type == SAS_SATA_PM ||
	       dev->dev_type == SAS_SATA_PM_PORT || dev->dev_type == SAS_SATA_PENDING;
}

int sas_get_ata_info(struct domain_device *dev, struct ex_phy *phy);
int sas_ata_init(struct domain_device *dev);
void sas_ata_task_abort(struct sas_task *task);
void sas_ata_strategy_handler(struct Scsi_Host *shost);
void sas_ata_eh(struct Scsi_Host *shost, struct list_head *work_q);
void sas_ata_schedule_reset(struct domain_device *dev);
void sas_ata_wait_eh(struct domain_device *dev);
void sas_probe_sata(struct asd_sas_port *port);
void sas_suspend_sata(struct asd_sas_port *port);
void sas_resume_sata(struct asd_sas_port *port);
void sas_ata_end_eh(struct ata_port *ap);
int sas_execute_ata_cmd(struct domain_device *device, u8 *fis,
			int force_phy_id);
int sas_ata_wait_after_reset(struct domain_device *dev, unsigned long deadline);
int smp_ata_check_ready_type(struct ata_link *link);
#else


static inline int dev_is_sata(struct domain_device *dev)
{
	return 0;
}
static inline int sas_ata_init(struct domain_device *dev)
{
	return 0;
}
static inline void sas_ata_task_abort(struct sas_task *task)
{
}

static inline void sas_ata_strategy_handler(struct Scsi_Host *shost)
{
}

static inline void sas_ata_eh(struct Scsi_Host *shost, struct list_head *work_q)
{
}

static inline void sas_ata_schedule_reset(struct domain_device *dev)
{
}

static inline void sas_ata_wait_eh(struct domain_device *dev)
{
}

static inline void sas_probe_sata(struct asd_sas_port *port)
{
}

static inline void sas_suspend_sata(struct asd_sas_port *port)
{
}

static inline void sas_resume_sata(struct asd_sas_port *port)
{
}

static inline int sas_get_ata_info(struct domain_device *dev, struct ex_phy *phy)
{
	return 0;
}

static inline void sas_ata_end_eh(struct ata_port *ap)
{
}

static inline int sas_execute_ata_cmd(struct domain_device *device, u8 *fis,
				      int force_phy_id)
{
	return 0;
}

static inline int sas_ata_wait_after_reset(struct domain_device *dev,
					   unsigned long deadline)
{
	return -ETIMEDOUT;
}

static inline int smp_ata_check_ready_type(struct ata_link *link)
{
	return 0;
}
#endif

#endif /* _SAS_ATA_H_ */
