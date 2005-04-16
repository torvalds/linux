#ifndef __LINUX_BRIDGE_EBT_PKTTYPE_H
#define __LINUX_BRIDGE_EBT_PKTTYPE_H

struct ebt_pkttype_info
{
	uint8_t pkt_type;
	uint8_t invert;
};
#define EBT_PKTTYPE_MATCH "pkttype"

#endif
