#ifndef _XT_ADDRTYPE_H
#define _XT_ADDRTYPE_H

#include <linux/types.h>

enum {
	XT_ADDRTYPE_INVERT_SOURCE	= 0x0001,
	XT_ADDRTYPE_INVERT_DEST		= 0x0002,
	XT_ADDRTYPE_LIMIT_IFACE_IN	= 0x0004,
	XT_ADDRTYPE_LIMIT_IFACE_OUT	= 0x0008,
};

struct xt_addrtype_info_v1 {
	__u16	source;		/* source-type mask */
	__u16	dest;		/* dest-type mask */
	__u32	flags;
};

/* revision 0 */
struct xt_addrtype_info {
	__u16	source;		/* source-type mask */
	__u16	dest;		/* dest-type mask */
	__u32	invert_source;
	__u32	invert_dest;
};

#endif
