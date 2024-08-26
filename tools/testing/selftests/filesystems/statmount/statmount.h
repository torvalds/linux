/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __STATMOUNT_H
#define __STATMOUNT_H

#include <stdint.h>
#include <linux/mount.h>
#include <asm/unistd.h>

static inline int statmount(uint64_t mnt_id, uint64_t mnt_ns_id, uint64_t mask,
			    struct statmount *buf, size_t bufsize,
			    unsigned int flags)
{
	struct mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER0,
		.mnt_id = mnt_id,
		.param = mask,
	};

	if (mnt_ns_id) {
		req.size = MNT_ID_REQ_SIZE_VER1;
		req.mnt_ns_id = mnt_ns_id;
	}

	return syscall(__NR_statmount, &req, buf, bufsize, flags);
}

static ssize_t listmount(uint64_t mnt_id, uint64_t mnt_ns_id,
			 uint64_t last_mnt_id, uint64_t list[], size_t num,
			 unsigned int flags)
{
	struct mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER0,
		.mnt_id = mnt_id,
		.param = last_mnt_id,
	};

	if (mnt_ns_id) {
		req.size = MNT_ID_REQ_SIZE_VER1;
		req.mnt_ns_id = mnt_ns_id;
	}

	return syscall(__NR_listmount, &req, list, num, flags);
}

#endif /* __STATMOUNT_H */
