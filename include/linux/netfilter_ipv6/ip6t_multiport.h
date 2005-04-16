#ifndef _IP6T_MULTIPORT_H
#define _IP6T_MULTIPORT_H
#include <linux/netfilter_ipv6/ip6_tables.h>

enum ip6t_multiport_flags
{
	IP6T_MULTIPORT_SOURCE,
	IP6T_MULTIPORT_DESTINATION,
	IP6T_MULTIPORT_EITHER
};

#define IP6T_MULTI_PORTS	15

/* Must fit inside union ip6t_matchinfo: 16 bytes */
struct ip6t_multiport
{
	u_int8_t flags;				/* Type of comparison */
	u_int8_t count;				/* Number of ports */
	u_int16_t ports[IP6T_MULTI_PORTS];	/* Ports */
};
#endif /*_IPT_MULTIPORT_H*/
