#ifndef _IP_NAT_CORE_H
#define _IP_NAT_CORE_H
#include <linux/list.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>

/* This header used to share core functionality between the standalone
   NAT module, and the compatibility layer's use of NAT for masquerading. */

extern unsigned int ip_nat_packet(struct ip_conntrack *ct,
			       enum ip_conntrack_info conntrackinfo,
			       unsigned int hooknum,
			       struct sk_buff **pskb);

extern int ip_nat_icmp_reply_translation(struct sk_buff **pskb,
					 struct ip_conntrack *ct,
					 enum ip_nat_manip_type manip,
					 enum ip_conntrack_dir dir);
#endif /* _IP_NAT_CORE_H */
