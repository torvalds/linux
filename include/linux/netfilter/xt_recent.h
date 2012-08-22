#ifndef _LINUX_NETFILTER_XT_RECENT_H
#define _LINUX_NETFILTER_XT_RECENT_H 1

#include <linux/types.h>

enum {
	XT_RECENT_CHECK    = 1 << 0,
	XT_RECENT_SET      = 1 << 1,
	XT_RECENT_UPDATE   = 1 << 2,
	XT_RECENT_REMOVE   = 1 << 3,
	XT_RECENT_TTL      = 1 << 4,
	XT_RECENT_REAP     = 1 << 5,

	XT_RECENT_SOURCE   = 0,
	XT_RECENT_DEST     = 1,

	XT_RECENT_NAME_LEN = 200,
};

/* Only allowed with --rcheck and --update */
#define XT_RECENT_MODIFIERS (XT_RECENT_TTL|XT_RECENT_REAP)

#define XT_RECENT_VALID_FLAGS (XT_RECENT_CHECK|XT_RECENT_SET|XT_RECENT_UPDATE|\
			       XT_RECENT_REMOVE|XT_RECENT_TTL|XT_RECENT_REAP)

struct xt_recent_mtinfo {
	__u32 seconds;
	__u32 hit_count;
	__u8 check_set;
	__u8 invert;
	char name[XT_RECENT_NAME_LEN];
	__u8 side;
};

struct xt_recent_mtinfo_v1 {
	__u32 seconds;
	__u32 hit_count;
	__u8 check_set;
	__u8 invert;
	char name[XT_RECENT_NAME_LEN];
	__u8 side;
	union nf_inet_addr mask;
};

#endif /* _LINUX_NETFILTER_XT_RECENT_H */
