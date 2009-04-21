#ifndef _LINUX_NETFILTER_XT_RECENT_H
#define _LINUX_NETFILTER_XT_RECENT_H 1

#include <linux/types.h>

enum {
	XT_RECENT_CHECK    = 1 << 0,
	XT_RECENT_SET      = 1 << 1,
	XT_RECENT_UPDATE   = 1 << 2,
	XT_RECENT_REMOVE   = 1 << 3,
	XT_RECENT_TTL      = 1 << 4,

	XT_RECENT_SOURCE   = 0,
	XT_RECENT_DEST     = 1,

	XT_RECENT_NAME_LEN = 200,
};

struct xt_recent_mtinfo {
	__u32 seconds;
	__u32 hit_count;
	__u8 check_set;
	__u8 invert;
	char name[XT_RECENT_NAME_LEN];
	__u8 side;
};

#endif /* _LINUX_NETFILTER_XT_RECENT_H */
