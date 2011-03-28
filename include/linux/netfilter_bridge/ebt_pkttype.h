#ifndef __LINUX_BRIDGE_EBT_PKTTYPE_H
#define __LINUX_BRIDGE_EBT_PKTTYPE_H

#include <linux/types.h>

struct ebt_pkttype_info {
	__u8 pkt_type;
	__u8 invert;
};
#define EBT_PKTTYPE_MATCH "pkttype"

#endif
