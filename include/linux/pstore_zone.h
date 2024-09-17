/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PSTORE_ZONE_H_
#define __PSTORE_ZONE_H_

#include <linux/types.h>

typedef ssize_t (*pstore_zone_read_op)(char *, size_t, loff_t);
typedef ssize_t (*pstore_zone_write_op)(const char *, size_t, loff_t);
typedef ssize_t (*pstore_zone_erase_op)(size_t, loff_t);
/**
 * struct pstore_zone_info - pstore/zone back-end driver structure
 *
 * @owner:	Module which is responsible for this back-end driver.
 * @name:	Name of the back-end driver.
 * @total_size: The total size in bytes pstore/zone can use. It must be greater
 *		than 4096 and be multiple of 4096.
 * @kmsg_size:	The size of oops/panic zone. Zero means disabled, otherwise,
 *		it must be multiple of SECTOR_SIZE(512 Bytes).
 * @max_reason: Maximum kmsg dump reason to store.
 * @pmsg_size:	The size of pmsg zone which is the same as @kmsg_size.
 * @console_size:The size of console zone which is the same as @kmsg_size.
 * @ftrace_size:The size of ftrace zone which is the same as @kmsg_size.
 * @read:	The general read operation. Both of the function parameters
 *		@size and @offset are relative value to storage.
 *		On success, the number of bytes should be returned, others
 *		mean error.
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
struct pstore_zone_info {
	struct module *owner;
	const char *name;

	unsigned long total_size;
	unsigned long kmsg_size;
	int max_reason;
	unsigned long pmsg_size;
	unsigned long console_size;
	unsigned long ftrace_size;
	pstore_zone_read_op read;
	pstore_zone_write_op write;
	pstore_zone_erase_op erase;
	pstore_zone_write_op panic_write;
};

extern int register_pstore_zone(struct pstore_zone_info *info);
extern void unregister_pstore_zone(struct pstore_zone_info *info);

#endif
