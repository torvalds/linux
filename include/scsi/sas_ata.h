/*
 * Support for SATA devices on Serial Attached SCSI (SAS) controllers
 *
 * Copyright (C) 2006 IBM Corporation
 *
 * Written by: Darrick J. Wong <djwong@us.ibm.com>, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#ifndef _SAS_ATA_H_
#define _SAS_ATA_H_

#include <linux/libata.h>
#include <scsi/libsas.h>

#ifdef CONFIG_SCSI_SAS_ATA

static inline int dev_is_sata(struct domain_device *dev)
{
	return (dev->rphy->identify.target_port_protocols & SAS_PROTOCOL_SATA);
}

int sas_ata_init_host_and_port(struct domain_device *found_dev,
			       struct scsi_target *starget);

void sas_ata_task_abort(struct sas_task *task);
void sas_ata_strategy_handler(struct Scsi_Host *shost);
int sas_ata_timed_out(struct scsi_cmnd *cmd, struct sas_task *task,
		      enum blk_eh_timer_return *rtn);
int sas_ata_eh(struct Scsi_Host *shost, struct list_head *work_q,
	       struct list_head *done_q);

#else


static inline int dev_is_sata(struct domain_device *dev)
{
	return 0;
}
static inline int sas_ata_init_host_and_port(struct domain_device *found_dev,
			       struct scsi_target *starget)
{
	return 0;
}
static inline void sas_ata_task_abort(struct sas_task *task)
{
}

static inline void sas_ata_strategy_handler(struct Scsi_Host *shost)
{
}

static inline int sas_ata_timed_out(struct scsi_cmnd *cmd,
				    struct sas_task *task,
				    enum blk_eh_timer_return *rtn)
{
	return 0;
}
static inline int sas_ata_eh(struct Scsi_Host *shost, struct list_head *work_q,
			     struct list_head *done_q)
{
	return 0;
}

#endif

#endif /* _SAS_ATA_H_ */
