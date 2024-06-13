/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_SCTP_H
#define _NF_CONNTRACK_SCTP_H
/* SCTP tracking. */

#include <uapi/linux/netfilter/nf_conntrack_sctp.h>

struct ip_ct_sctp {
	enum sctp_conntrack state;

	__be32 vtag[IP_CT_DIR_MAX];
	u8 last_dir;
	u8 flags;
};

#endif /* _NF_CONNTRACK_SCTP_H */
