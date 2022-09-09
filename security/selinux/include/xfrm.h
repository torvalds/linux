/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SELinux support for the XFRM LSM hooks
 *
 * Author : Trent Jaeger, <jaegert@us.ibm.com>
 * Updated : Venkat Yekkirala, <vyekkirala@TrustedCS.com>
 */
#ifndef _SELINUX_XFRM_H_
#define _SELINUX_XFRM_H_

#include <linux/lsm_audit.h>
#include <net/flow.h>
#include <net/xfrm.h>

int selinux_xfrm_policy_alloc(struct xfrm_sec_ctx **ctxp,
			      struct xfrm_user_sec_ctx *uctx,
			      gfp_t gfp);
int selinux_xfrm_policy_clone(struct xfrm_sec_ctx *old_ctx,
			      struct xfrm_sec_ctx **new_ctxp);
void selinux_xfrm_policy_free(struct xfrm_sec_ctx *ctx);
int selinux_xfrm_policy_delete(struct xfrm_sec_ctx *ctx);
int selinux_xfrm_state_alloc(struct xfrm_state *x,
			     struct xfrm_user_sec_ctx *uctx);
int selinux_xfrm_state_alloc_acquire(struct xfrm_state *x,
				     struct xfrm_sec_ctx *polsec, u32 secid);
void selinux_xfrm_state_free(struct xfrm_state *x);
int selinux_xfrm_state_delete(struct xfrm_state *x);
int selinux_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx, u32 fl_secid);
int selinux_xfrm_state_pol_flow_match(struct xfrm_state *x,
				      struct xfrm_policy *xp,
				      const struct flowi_common *flic);

#ifdef CONFIG_SECURITY_NETWORK_XFRM
extern atomic_t selinux_xfrm_refcount;

static inline int selinux_xfrm_enabled(void)
{
	return (atomic_read(&selinux_xfrm_refcount) > 0);
}

int selinux_xfrm_sock_rcv_skb(u32 sk_sid, struct sk_buff *skb,
			      struct common_audit_data *ad);
int selinux_xfrm_postroute_last(u32 sk_sid, struct sk_buff *skb,
				struct common_audit_data *ad, u8 proto);
int selinux_xfrm_decode_session(struct sk_buff *skb, u32 *sid, int ckall);
int selinux_xfrm_skb_sid(struct sk_buff *skb, u32 *sid);

static inline void selinux_xfrm_notify_policyload(void)
{
	struct net *net;

	down_read(&net_rwsem);
	for_each_net(net)
		rt_genid_bump_all(net);
	up_read(&net_rwsem);
}
#else
static inline int selinux_xfrm_enabled(void)
{
	return 0;
}

static inline int selinux_xfrm_sock_rcv_skb(u32 sk_sid, struct sk_buff *skb,
					    struct common_audit_data *ad)
{
	return 0;
}

static inline int selinux_xfrm_postroute_last(u32 sk_sid, struct sk_buff *skb,
					      struct common_audit_data *ad,
					      u8 proto)
{
	return 0;
}

static inline int selinux_xfrm_decode_session(struct sk_buff *skb, u32 *sid,
					      int ckall)
{
	*sid = SECSID_NULL;
	return 0;
}

static inline void selinux_xfrm_notify_policyload(void)
{
}

static inline int selinux_xfrm_skb_sid(struct sk_buff *skb, u32 *sid)
{
	*sid = SECSID_NULL;
	return 0;
}
#endif

#endif /* _SELINUX_XFRM_H_ */
