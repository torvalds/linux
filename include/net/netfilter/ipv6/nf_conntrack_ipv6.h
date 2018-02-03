/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_IPV6_H
#define _NF_CONNTRACK_IPV6_H

extern const struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv6;

extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp6;
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_udp6;
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_icmpv6;
#ifdef CONFIG_NF_CT_PROTO_DCCP
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_dccp6;
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_sctp6;
#endif
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_udplite6;
#endif

#include <linux/sysctl.h>
extern struct ctl_table nf_ct_ipv6_sysctl_table[];

#endif /* _NF_CONNTRACK_IPV6_H*/
