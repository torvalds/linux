#ifndef _XT_MULTIPORT_H
#define _XT_MULTIPORT_H

enum xt_multiport_flags
{
	XT_MULTIPORT_SOURCE,
	XT_MULTIPORT_DESTINATION,
	XT_MULTIPORT_EITHER
};

#define XT_MULTI_PORTS	15

/* Must fit inside union xt_matchinfo: 16 bytes */
struct xt_multiport
{
	u_int8_t flags;				/* Type of comparison */
	u_int8_t count;				/* Number of ports */
	u_int16_t ports[XT_MULTI_PORTS];	/* Ports */
};

struct xt_multiport_v1
{
	u_int8_t flags;				/* Type of comparison */
	u_int8_t count;				/* Number of ports */
	u_int16_t ports[XT_MULTI_PORTS];	/* Ports */
	u_int8_t pflags[XT_MULTI_PORTS];	/* Port flags */
	u_int8_t invert;			/* Invert flag */
};

#endif /*_XT_MULTIPORT_H*/
