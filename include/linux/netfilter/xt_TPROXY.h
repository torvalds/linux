#ifndef _XT_TPROXY_H_target
#define _XT_TPROXY_H_target

/* TPROXY target is capable of marking the packet to perform
 * redirection. We can get rid of that whenever we get support for
 * mutliple targets in the same rule. */
struct xt_tproxy_target_info {
	u_int32_t mark_mask;
	u_int32_t mark_value;
	__be32 laddr;
	__be16 lport;
};

#endif /* _XT_TPROXY_H_target */
