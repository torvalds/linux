/*
 * SELinux support for the XFRM LSM hooks
 *
 * Author : Trent Jaeger, <jaegert@us.ibm.com>
 */
#ifndef _SELINUX_XFRM_H_
#define _SELINUX_XFRM_H_

int selinux_xfrm_policy_alloc(struct xfrm_policy *xp, struct xfrm_user_sec_ctx *sec_ctx);
int selinux_xfrm_policy_clone(struct xfrm_policy *old, struct xfrm_policy *new);
void selinux_xfrm_policy_free(struct xfrm_policy *xp);
int selinux_xfrm_policy_delete(struct xfrm_policy *xp);
int selinux_xfrm_state_alloc(struct xfrm_state *x, struct xfrm_user_sec_ctx *sec_ctx);
void selinux_xfrm_state_free(struct xfrm_state *x);
int selinux_xfrm_state_delete(struct xfrm_state *x);
int selinux_xfrm_policy_lookup(struct xfrm_policy *xp, u32 sk_sid, u8 dir);

/*
 * Extract the security blob from the sock (it's actually on the socket)
 */
static inline struct inode_security_struct *get_sock_isec(struct sock *sk)
{
	if (!sk->sk_socket)
		return NULL;

	return SOCK_INODE(sk->sk_socket)->i_security;
}


static inline u32 selinux_no_sk_sid(struct flowi *fl)
{
	/* NOTE: no sock occurs on ICMP reply, forwards, ... */
	/* icmp_reply: authorize as kernel packet */
	if (fl && fl->proto == IPPROTO_ICMP) {
		return SECINITSID_KERNEL;
	}

	return SECINITSID_ANY_SOCKET;
}

#ifdef CONFIG_SECURITY_NETWORK_XFRM
int selinux_xfrm_sock_rcv_skb(u32 sid, struct sk_buff *skb);
int selinux_xfrm_postroute_last(u32 isec_sid, struct sk_buff *skb);
u32 selinux_socket_getpeer_stream(struct sock *sk);
u32 selinux_socket_getpeer_dgram(struct sk_buff *skb);
#else
static inline int selinux_xfrm_sock_rcv_skb(u32 isec_sid, struct sk_buff *skb)
{
	return 0;
}

static inline int selinux_xfrm_postroute_last(u32 isec_sid, struct sk_buff *skb)
{
	return 0;
}

static inline int selinux_socket_getpeer_stream(struct sock *sk)
{
	return SECSID_NULL;
}

static inline int selinux_socket_getpeer_dgram(struct sk_buff *skb)
{
	return SECSID_NULL;
}
#endif

#endif /* _SELINUX_XFRM_H_ */
