/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PSTORE_BLK_H_
#define __PSTORE_BLK_H_

#include <linux/types.h>
#include <linux/pstore.h>
#include <linux/pstore_zone.h>

/**
 * typedef pstore_blk_panic_write_op - panic write operation to block device
 *
 * @buf: the data to write
 * @start_sect: start sector to block device
 * @sects: sectors count on buf
 *
 * Return: On success, zero should be returned. Others excluding -ENOMSG
 * mean error. -ENOMSG means to try next zone.
 *
 * Panic write to block device must be aligned to SECTOR_SIZE.
 */
typedef int (*pstore_blk_panic_write_op)(const char *buf, sector_t start_sect,
		sector_t sects);

/**
 * struct pstore_blk_info - pstore/blk registration details
 *
 * @major:	Which major device number to support with pstore/blk
 * @flags:	The supported PSTORE_FLAGS_* from linux/pstore.h.
 * @panic_write:The write operation only used for the panic case.
 *		This can be NULL, but is recommended to avoid losing
 *		crash data if the kernel's IO path or work queues are
 *		broken during a panic.
 * @devt:	The dev_t that pstore/blk has attached to.
 * @nr_sects:	Number of sectors on @devt.
 * @start_sect:	Starting sector on @devt.
 */
struct pstore_blk_info {
	unsigned int major;
	unsigned int flags;
	pstore_blk_panic_write_op panic_write;

	/* Filled in by pstore/blk after registration. */
	dev_t devt;
	sector_t nr_sects;
	sector_t start_sect;
};

int  register_pstore_blk(struct pstore_blk_info *info);
void unregister_pstore_blk(unsigned int major);

/**
 * struct pstore_device_info - back-end pstore/blk driver structure.
 *
 * @total_size: The total size in bytes pstore/blk can use. It must be greater
 *		than 4096 and be multiple of 4096.
 * @flags:	Refer to macro starting with PSTORE_FLAGS defined in
 *		linux/pstore.h. It means what front-ends this device support.
 *		Zero means all backends for compatible.
 * @read:	The general read operation. Both of the function parameters
 *		@size and @offset are relative value to bock device (not the
 *		whole disk).
 *		On success, the number of bytes should be returned, others
 *		means error.
 * @write:	The same as @read, but the following error number:
 *		-EBUSY means try to write again later.
 *		-ENOMSG means to try next zone.
 * @erase:	The general erase operation for device with special removing
 *		job. Both of the function parameters @size and @offset are
 *		relative value to storage.
 *		Return 0 on success and others on failure.
 * @panic_write:The write operation only used for panic case. It's optional
 *		if you do not care panic log. The parameters are relative
 *		value to storage.
 *		On success, the number of bytes should be returned, others
 *		excluding -ENOMSG mean error. -ENOMSG means to try next zone.
 */
struct pstore_device_info {
	unsigned long total_size;
	unsigned int flags;
	pstore_zone_read_op read;
	pstore_zone_write_op write;
	pstore_zone_erase_op erase;
	pstore_zone_write_op panic_write;
};

int  register_pstore_device(struct pstore_device_info *dev);
void unregister_pstore_device(struct pstore_device_info *dev);

/**
 * struct pstore_blk_config - the pstore_blk backend configuration
 *
 * @device:		Name of the desired block device
 * @max_reason:		Maximum kmsg dump reason to store to block device
 * @kmsg_size:		Total size of for kmsg dumps
 * @pmsg_size:		Total size of the pmsg storage area
 * @console_size:	Total size of the console storage area
 * @ftrace_size:	Total size for ftrace logging data (for all CPUs)
 */
struct pstore_blk_config {
	char device[80];
	enum kmsg_dump_reason max_reason;
	unsigned long kmsg_size;
	unsigned long pmsg_size;
	unsigned long console_size;
	unsigned long ftrace_size;
};

/**
 * pstore_blk_get_config - get a copy of the pstore_blk backend configuration
 *
 * @info:	The sturct pstore_blk_config to be filled in
 *
 * Failure returns negative error code, and success returns 0.
 */
int pstore_blk_get_config(struct pstore_blk_config *info);

#endif
