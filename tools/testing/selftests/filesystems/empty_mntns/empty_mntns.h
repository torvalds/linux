/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef EMPTY_MNTNS_H
#define EMPTY_MNTNS_H

#include <errno.h>
#include <stdlib.h>

#include "../statmount/statmount.h"

#ifndef UNSHARE_EMPTY_MNTNS
#define UNSHARE_EMPTY_MNTNS	0x00100000
#endif

#ifndef CLONE_EMPTY_MNTNS
#define CLONE_EMPTY_MNTNS	(1ULL << 37)
#endif

static inline ssize_t count_mounts(void)
{
	uint64_t list[4096];

	return listmount(LSMT_ROOT, 0, 0, list, sizeof(list) / sizeof(list[0]), 0);
}

static inline struct statmount *statmount_alloc(uint64_t mnt_id,
						uint64_t mnt_ns_id,
						uint64_t mask)
{
	size_t bufsize = 1 << 15;
	struct statmount *buf;
	int ret;

	for (;;) {
		buf = malloc(bufsize);
		if (!buf)
			return NULL;

		ret = statmount(mnt_id, mnt_ns_id, 0, mask, buf, bufsize, 0);
		if (ret == 0)
			return buf;

		free(buf);
		if (errno != EOVERFLOW)
			return NULL;

		bufsize <<= 1;
	}
}

#endif /* EMPTY_MNTNS_H */
