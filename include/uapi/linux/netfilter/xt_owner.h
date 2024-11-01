/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_OWNER_MATCH_H
#define _XT_OWNER_MATCH_H

#include <linux/types.h>

enum {
	XT_OWNER_UID          = 1 << 0,
	XT_OWNER_GID          = 1 << 1,
	XT_OWNER_SOCKET       = 1 << 2,
	XT_OWNER_SUPPL_GROUPS = 1 << 3,
};

#define XT_OWNER_MASK	(XT_OWNER_UID | 	\
			 XT_OWNER_GID | 	\
			 XT_OWNER_SOCKET |	\
			 XT_OWNER_SUPPL_GROUPS)

struct xt_owner_match_info {
	__u32 uid_min, uid_max;
	__u32 gid_min, gid_max;
	__u8 match, invert;
};

#endif /* _XT_OWNER_MATCH_H */
