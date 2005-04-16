#ifndef _IP6T_PHYSDEV_H
#define _IP6T_PHYSDEV_H

#ifdef __KERNEL__
#include <linux/if.h>
#endif

#define IP6T_PHYSDEV_OP_IN		0x01
#define IP6T_PHYSDEV_OP_OUT		0x02
#define IP6T_PHYSDEV_OP_BRIDGED		0x04
#define IP6T_PHYSDEV_OP_ISIN		0x08
#define IP6T_PHYSDEV_OP_ISOUT		0x10
#define IP6T_PHYSDEV_OP_MASK		(0x20 - 1)

struct ip6t_physdev_info {
	char physindev[IFNAMSIZ];
	char in_mask[IFNAMSIZ];
	char physoutdev[IFNAMSIZ];
	char out_mask[IFNAMSIZ];
	u_int8_t invert;
	u_int8_t bitmask;
};

#endif /*_IP6T_PHYSDEV_H*/
