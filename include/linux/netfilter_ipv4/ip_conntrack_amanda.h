#ifndef _IP_CONNTRACK_AMANDA_H
#define _IP_CONNTRACK_AMANDA_H
/* AMANDA tracking. */

struct ip_conntrack_expect;
extern unsigned int (*ip_nat_amanda_hook)(struct sk_buff **pskb,
					  enum ip_conntrack_info ctinfo,
					  unsigned int matchoff,
					  unsigned int matchlen,
					  struct ip_conntrack_expect *exp);
#endif /* _IP_CONNTRACK_AMANDA_H */
