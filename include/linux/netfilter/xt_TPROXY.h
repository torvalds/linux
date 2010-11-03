#ifndef _XT_TPROXY_H
#define _XT_TPROXY_H

/* TPROXY target is capable of marking the packet to perform
 * redirection. We can get rid of that whenever we get support for
 * mutliple targets in the same rule. */
struct xt_tproxy_target_info {
	u_int32_t mark_mask;
	u_int32_t mark_value;
	__be32 laddr;
	__be16 lport;
};

struct xt_tproxy_target_info_v1 {
	u_int32_t mark_mask;
	u_int32_t mark_value;
	union nf_inet_addr laddr;
	__be16 lport;
};

#endif /* _XT_TPROXY_H */
