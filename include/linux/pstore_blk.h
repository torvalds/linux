/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PSTORE_BLK_H_
#define __PSTORE_BLK_H_

#include <linux/types.h>
#include <linux/pstore.h>
#include <linux/pstore_zone.h>

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
