/* 
 *  Transport specific attributes.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef SCSI_TRANSPORT_H
#define SCSI_TRANSPORT_H

#include <linux/transport_class.h>
#include <linux/blkdev.h>
#include <linux/bug.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>

struct scsi_transport_template {
	/* the attribute containers */
	struct transport_container host_attrs;
	struct transport_container target_attrs;
	struct transport_container device_attrs;

	/*
	 * If set, called from sysfs and legacy procfs rescanning code.
	 */
	int (*user_scan)(struct Scsi_Host *, uint, uint, u64);

	/* The size of the specific transport attribute structure (a
	 * space of this size will be left at the end of the
	 * scsi_* structure */
	int	device_size;
	int	device_private_offset;
	int	target_size;
	int	target_private_offset;
	int	host_size;
	/* no private offset for the host; there's an alternative mechanism */

	/*
	 * True if the transport wants to use a host-based work-queue
	 */
	unsigned int create_work_queue : 1;

	/*
	 * Allows a transport to override the default error handler.
	 */
	void (* eh_strategy_handler)(struct Scsi_Host *);
};

#define transport_class_to_shost(tc) \
	dev_to_shost((tc)->parent)


/* Private area maintenance. The driver requested allocations come
 * directly after the transport class allocations (if any).  The idea
 * is that you *must* call these only once.  The code assumes that the
 * initial values are the ones the transport specific code requires */
static inline void
scsi_transport_reserve_target(struct scsi_transport_template * t, int space)
{
	BUG_ON(t->target_private_offset != 0);
	t->target_private_offset = ALIGN(t->target_size, sizeof(void *));
	t->target_size = t->target_private_offset + space;
}
static inline void
scsi_transport_reserve_device(struct scsi_transport_template * t, int space)
{
	BUG_ON(t->device_private_offset != 0);
	t->device_private_offset = ALIGN(t->device_size, sizeof(void *));
	t->device_size = t->device_private_offset + space;
}
static inline void *
scsi_transport_target_data(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	return (u8 *)starget->starget_data
		+ shost->transportt->target_private_offset;

}
static inline void *
scsi_transport_device_data(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	return (u8 *)sdev->sdev_data
		+ shost->transportt->device_private_offset;
}

void __scsi_init_queue(struct Scsi_Host *shost, struct request_queue *q);

#endif /* SCSI_TRANSPORT_H */
