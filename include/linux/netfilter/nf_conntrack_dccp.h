/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_DCCP_H
#define _NF_CONNTRACK_DCCP_H

/* Exposed to userspace over nfnetlink */
enum ct_dccp_states {
	CT_DCCP_NONE,
	CT_DCCP_REQUEST,
	CT_DCCP_RESPOND,
	CT_DCCP_PARTOPEN,
	CT_DCCP_OPEN,
	CT_DCCP_CLOSEREQ,
	CT_DCCP_CLOSING,
	CT_DCCP_TIMEWAIT,
	CT_DCCP_IGNORE,
	CT_DCCP_INVALID,
	__CT_DCCP_MAX
};
#define CT_DCCP_MAX		(__CT_DCCP_MAX - 1)

enum ct_dccp_roles {
	CT_DCCP_ROLE_CLIENT,
	CT_DCCP_ROLE_SERVER,
	__CT_DCCP_ROLE_MAX
};
#define CT_DCCP_ROLE_MAX	(__CT_DCCP_ROLE_MAX - 1)

#ifdef __KERNEL__
#include <linux/netfilter/nf_conntrack_tuple_common.h>

struct nf_ct_dccp {
	u_int8_t	role[IP_CT_DIR_MAX];
	u_int8_t	state;
	u_int8_t	last_pkt;
	u_int8_t	last_dir;
	u_int64_t	handshake_seq;
};

#endif /* __KERNEL__ */

#endif /* _NF_CONNTRACK_DCCP_H */
