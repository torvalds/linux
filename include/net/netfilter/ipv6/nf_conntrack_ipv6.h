/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_IPV6_H
#define _NF_CONNTRACK_IPV6_H

extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_icmpv6;

#include <linux/sysctl.h>
extern struct ctl_table nf_ct_ipv6_sysctl_table[];

#endif /* _NF_CONNTRACK_IPV6_H*/
