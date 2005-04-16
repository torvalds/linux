#ifndef _IP_CONNTRACK_ICMP_H
#define _IP_CONNTRACK_ICMP_H
/* ICMP tracking. */
#include <asm/atomic.h>

struct ip_ct_icmp
{
	/* Optimization: when number in == number out, forget immediately. */
	atomic_t count;
};
#endif /* _IP_CONNTRACK_ICMP_H */
