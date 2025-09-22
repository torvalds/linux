/*	$OpenBSD: route6d.h,v 1.1 2000/04/26 21:35:43 jakob Exp $	*/

#define	RIP6_VERSION	1

#define	RIP6_REQUEST	1
#define	RIP6_RESPONSE	2

struct netinfo6 {
	struct	in6_addr	rip6_dest;
	u_short	rip6_tag;
	u_char	rip6_plen;
	u_char	rip6_metric;
};

struct	rip6 {
	u_char	rip6_cmd;
	u_char	rip6_vers;
	u_char	rip6_res1[2];
	union {
		struct	netinfo6	ru6_nets[1];
		char	ru6_tracefile[1];
	} rip6un;
#define	rip6_nets	rip6un.ru6_nets
#define	rip6_tracefile	rip6un.ru6_tracefile
};

#define	HOPCNT_INFINITY6	16
#define	MAXRTE			24
#define	NEXTHOP_METRIC		0xff

#ifndef	DEBUG
#define	SUPPLY_INTERVAL6	30
#define	RIP_LIFETIME		180
#define	RIP_HOLDDOWN		120
#define	RIP_TRIG_INTERVAL6	5
#define	RIP_TRIG_INTERVAL6_MIN	1
#else
/* only for debugging; can not wait for 30sec to appear a bug */
#define	SUPPLY_INTERVAL6	10
#define	RIP_LIFETIME		60
#define	RIP_HOLDDOWN		40
#define	RIP_TRIG_INTERVAL6	5
#define	RIP_TRIG_INTERVAL6_MIN	1
#endif

#define	RIP6_PORT		521
#define	RIP6_DEST		"ff02::9"
