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

#endif /* EMPTY_MNTNS_H */
