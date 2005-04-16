#ifndef _IPT_MULTIPORT_H
#define _IPT_MULTIPORT_H
#include <linux/netfilter_ipv4/ip_tables.h>

enum ipt_multiport_flags
{
	IPT_MULTIPORT_SOURCE,
	IPT_MULTIPORT_DESTINATION,
	IPT_MULTIPORT_EITHER
};

#define IPT_MULTI_PORTS	15

/* Must fit inside union ipt_matchinfo: 16 bytes */
struct ipt_multiport
{
	u_int8_t flags;				/* Type of comparison */
	u_int8_t count;				/* Number of ports */
	u_int16_t ports[IPT_MULTI_PORTS];	/* Ports */
};

struct ipt_multiport_v1
{
	u_int8_t flags;				/* Type of comparison */
	u_int8_t count;				/* Number of ports */
	u_int16_t ports[IPT_MULTI_PORTS];	/* Ports */
	u_int8_t pflags[IPT_MULTI_PORTS];	/* Port flags */
	u_int8_t invert;			/* Invert flag */
};
#endif /*_IPT_MULTIPORT_H*/
