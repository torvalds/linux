#ifndef _XT_PHYSDEV_H
#define _XT_PHYSDEV_H

#ifdef __KERNEL__
#include <linux/if.h>
#endif

#define XT_PHYSDEV_OP_IN		0x01
#define XT_PHYSDEV_OP_OUT		0x02
#define XT_PHYSDEV_OP_BRIDGED		0x04
#define XT_PHYSDEV_OP_ISIN		0x08
#define XT_PHYSDEV_OP_ISOUT		0x10
#define XT_PHYSDEV_OP_MASK		(0x20 - 1)

struct xt_physdev_info {
	char physindev[IFNAMSIZ];
	char in_mask[IFNAMSIZ];
	char physoutdev[IFNAMSIZ];
	char out_mask[IFNAMSIZ];
	u_int8_t invert;
	u_int8_t bitmask;
};

#endif /*_XT_PHYSDEV_H*/
