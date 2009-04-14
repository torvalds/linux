#ifndef _XT_NFLOG_TARGET
#define _XT_NFLOG_TARGET

#include <linux/types.h>

#define XT_NFLOG_DEFAULT_GROUP		0x1
#define XT_NFLOG_DEFAULT_THRESHOLD	0

#define XT_NFLOG_MASK			0x0

struct xt_nflog_info {
	__u32	len;
	__u16	group;
	__u16	threshold;
	__u16	flags;
	__u16	pad;
	char		prefix[64];
};

#endif /* _XT_NFLOG_TARGET */
