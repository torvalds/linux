/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_AMANDA_H
#define _NF_CONNTRACK_AMANDA_H
/* AMANDA tracking. */

extern unsigned int (*nf_nat_amanda_hook)(struct sk_buff *skb,
					  enum ip_conntrack_info ctinfo,
					  unsigned int protoff,
					  unsigned int matchoff,
					  unsigned int matchlen,
					  struct nf_conntrack_expect *exp);
#endif /* _NF_CONNTRACK_AMANDA_H */
