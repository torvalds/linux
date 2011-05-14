#ifndef _IPT_ADDRTYPE_H
#define _IPT_ADDRTYPE_H

#include <linux/types.h>

enum {
	IPT_ADDRTYPE_INVERT_SOURCE	= 0x0001,
	IPT_ADDRTYPE_INVERT_DEST	= 0x0002,
	IPT_ADDRTYPE_LIMIT_IFACE_IN	= 0x0004,
	IPT_ADDRTYPE_LIMIT_IFACE_OUT	= 0x0008,
};

struct ipt_addrtype_info_v1 {
	__u16	source;		/* source-type mask */
	__u16	dest;		/* dest-type mask */
	__u32	flags;
};

/* revision 0 */
struct ipt_addrtype_info {
	__u16	source;		/* source-type mask */
	__u16	dest;		/* dest-type mask */
	__u32	invert_source;
	__u32	invert_dest;
};

#endif
