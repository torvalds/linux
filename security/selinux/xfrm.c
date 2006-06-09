/*
 *  NSA Security-Enhanced Linux (SELinux) security module
 *
 *  This file contains the SELinux XFRM hook function implementations.
 *
 *  Authors:  Serge Hallyn <sergeh@us.ibm.com>
 *	      Trent Jaeger <jaegert@us.ibm.com>
 *
 *  Copyright (C) 2005 International Business Machines Corporation
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *	as published by the Free Software Foundation.
 */

/*
 * USAGE:
 * NOTES:
 *   1. Make sure to enable the following options in your kernel config:
 *	CONFIG_SECURITY=y
 *	CONFIG_SECURITY_NETWORK=y
 *	CONFIG_SECURITY_NETWORK_XFRM=y
 *	CONFIG_SECURITY_SELINUX=m/y
 * ISSUES:
 *   1. Caching packets, so they are not dropped during negotiation
 *   2. Emulating a reasonable SO_PEERSEC across machines
 *   3. Testing addition of sk_policy's with security context via setsockopt
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/xfrm.h>
#include <net/xfrm.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <asm/semaphore.h>

#include "avc.h"
#include "objsec.h"
#include "xfrm.h"


/*
 * Returns true if an LSM/SELinux context
 */
static inline int selinux_authorizable_ctx(struct xfrm_sec_ctx *ctx)
{
	return (ctx &&
		(ctx->ctx_doi == XFRM_SC_DOI_LSM) &&
		(ctx->ctx_alg == XFRM_SC_ALG_SELINUX));
}

/*
 * Returns true if the xfrm contains a security blob for SELinux
 */
static inline int selinux_authorizable_xfrm(struct xfrm_state *x)
{
	return selinux_authorizable_ctx(x->security);
}

/*
 * LSM hook implementation that authorizes that a socket can be used
 * with the corresponding xfrm_sec_ctx and direction.
 */
int selinux_xfrm_policy_lookup(struct xfrm_policy *xp, u32 sk_sid, u8 dir)
{
	int rc = 0;
	u32 sel_sid = SECINITSID_UNLABELED;
	struct xfrm_sec_ctx *ctx;

	/* Context sid is either set to label or ANY_ASSOC */
	if ((ctx = xp->security)) {
		if (!selinux_authorizable_ctx(ctx))
			return -EINVAL;

		sel_sid = ctx->ctx_sid;
	}

	rc = avc_has_perm(sk_sid, sel_sid, SECCLASS_ASSOCIATION,
			  ((dir == FLOW_DIR_IN) ? ASSOCIATION__RECVFROM :
			   ((dir == FLOW_DIR_OUT) ?  ASSOCIATION__SENDTO :
			    (ASSOCIATION__SENDTO | ASSOCIATION__RECVFROM))),
			  NULL);

	return rc;
}

/*
 * Security blob allocation for xfrm_policy and xfrm_state
 * CTX does not have a meaningful value on input
 */
static int selinux_xfrm_sec_ctx_alloc(struct xfrm_sec_ctx **ctxp, struct xfrm_user_sec_ctx *uctx)
{
	int rc = 0;
	struct task_security_struct *tsec = current->security;
	struct xfrm_sec_ctx *ctx;

	BUG_ON(!uctx);
	BUG_ON(uctx->ctx_doi != XFRM_SC_ALG_SELINUX);

	if (uctx->ctx_len >= PAGE_SIZE)
		return -ENOMEM;

	*ctxp = ctx = kmalloc(sizeof(*ctx) +
			      uctx->ctx_len,
			      GFP_KERNEL);

	if (!ctx)
		return -ENOMEM;

	ctx->ctx_doi = uctx->ctx_doi;
	ctx->ctx_len = uctx->ctx_len;
	ctx->ctx_alg = uctx->ctx_alg;

	memcpy(ctx->ctx_str,
	       uctx+1,
	       ctx->ctx_len);
	rc = security_context_to_sid(ctx->ctx_str,
				     ctx->ctx_len,
				     &ctx->ctx_sid);

	if (rc)
		goto out;

	/*
	 * Does the subject have permission to set security context?
	 */
	rc = avc_has_perm(tsec->sid, ctx->ctx_sid,
			  SECCLASS_ASSOCIATION,
			  ASSOCIATION__SETCONTEXT, NULL);
	if (rc)
		goto out;

	return rc;

out:
	*ctxp = NULL;
	kfree(ctx);
	return rc;
}

/*
 * LSM hook implementation that allocs and transfers uctx spec to
 * xfrm_policy.
 */
int selinux_xfrm_policy_alloc(struct xfrm_policy *xp, struct xfrm_user_sec_ctx *uctx)
{
	int err;

	BUG_ON(!xp);

	err = selinux_xfrm_sec_ctx_alloc(&xp->security, uctx);
	return err;
}


/*
 * LSM hook implementation that copies security data structure from old to
 * new for policy cloning.
 */
int selinux_xfrm_policy_clone(struct xfrm_policy *old, struct xfrm_policy *new)
{
	struct xfrm_sec_ctx *old_ctx, *new_ctx;

	old_ctx = old->security;

	if (old_ctx) {
		new_ctx = new->security = kmalloc(sizeof(*new_ctx) +
						  old_ctx->ctx_len,
						  GFP_KERNEL);

		if (!new_ctx)
			return -ENOMEM;

		memcpy(new_ctx, old_ctx, sizeof(*new_ctx));
		memcpy(new_ctx->ctx_str, old_ctx->ctx_str, new_ctx->ctx_len);
	}
	return 0;
}

/*
 * LSM hook implementation that frees xfrm_policy security information.
 */
void selinux_xfrm_policy_free(struct xfrm_policy *xp)
{
	struct xfrm_sec_ctx *ctx = xp->security;
	if (ctx)
		kfree(ctx);
}

/*
 * LSM hook implementation that authorizes deletion of labeled policies.
 */
int selinux_xfrm_policy_delete(struct xfrm_policy *xp)
{
	struct task_security_struct *tsec = current->security;
	struct xfrm_sec_ctx *ctx = xp->security;
	int rc = 0;

	if (ctx)
		rc = avc_has_perm(tsec->sid, ctx->ctx_sid,
				  SECCLASS_ASSOCIATION,
				  ASSOCIATION__SETCONTEXT, NULL);

	return rc;
}

/*
 * LSM hook implementation that allocs and transfers sec_ctx spec to
 * xfrm_state.
 */
int selinux_xfrm_state_alloc(struct xfrm_state *x, struct xfrm_user_sec_ctx *uctx)
{
	int err;

	BUG_ON(!x);

	err = selinux_xfrm_sec_ctx_alloc(&x->security, uctx);
	return err;
}

/*
 * LSM hook implementation that frees xfrm_state security information.
 */
void selinux_xfrm_state_free(struct xfrm_state *x)
{
	struct xfrm_sec_ctx *ctx = x->security;
	if (ctx)
		kfree(ctx);
}

/*
 * SELinux internal function to retrieve the context of a connected
 * (sk->sk_state == TCP_ESTABLISHED) TCP socket based on its security
 * association used to connect to the remote socket.
 *
 * Retrieve via getsockopt SO_PEERSEC.
 */
u32 selinux_socket_getpeer_stream(struct sock *sk)
{
	struct dst_entry *dst, *dst_test;
	u32 peer_sid = SECSID_NULL;

	if (sk->sk_state != TCP_ESTABLISHED)
		goto out;

	dst = sk_dst_get(sk);
	if (!dst)
		goto out;

 	for (dst_test = dst; dst_test != 0;
      	     dst_test = dst_test->child) {
		struct xfrm_state *x = dst_test->xfrm;

 		if (x && selinux_authorizable_xfrm(x)) {
	 	 	struct xfrm_sec_ctx *ctx = x->security;
			peer_sid = ctx->ctx_sid;
			break;
		}
	}
	dst_release(dst);

out:
	return peer_sid;
}

/*
 * SELinux internal function to retrieve the context of a UDP packet
 * based on its security association used to connect to the remote socket.
 *
 * Retrieve via setsockopt IP_PASSSEC and recvmsg with control message
 * type SCM_SECURITY.
 */
u32 selinux_socket_getpeer_dgram(struct sk_buff *skb)
{
	struct sec_path *sp;

	if (skb == NULL)
		return SECSID_NULL;

	if (skb->sk->sk_protocol != IPPROTO_UDP)
		return SECSID_NULL;

	sp = skb->sp;
	if (sp) {
		int i;

		for (i = sp->len-1; i >= 0; i--) {
			struct xfrm_state *x = sp->xvec[i];
			if (selinux_authorizable_xfrm(x)) {
				struct xfrm_sec_ctx *ctx = x->security;
				return ctx->ctx_sid;
			}
		}
	}

	return SECSID_NULL;
}

 /*
  * LSM hook implementation that authorizes deletion of labeled SAs.
  */
int selinux_xfrm_state_delete(struct xfrm_state *x)
{
	struct task_security_struct *tsec = current->security;
	struct xfrm_sec_ctx *ctx = x->security;
	int rc = 0;

	if (ctx)
		rc = avc_has_perm(tsec->sid, ctx->ctx_sid,
				  SECCLASS_ASSOCIATION,
				  ASSOCIATION__SETCONTEXT, NULL);

	return rc;
}

/*
 * LSM hook that controls access to unlabelled packets.  If
 * a xfrm_state is authorizable (defined by macro) then it was
 * already authorized by the IPSec process.  If not, then
 * we need to check for unlabelled access since this may not have
 * gone thru the IPSec process.
 */
int selinux_xfrm_sock_rcv_skb(u32 isec_sid, struct sk_buff *skb)
{
	int i, rc = 0;
	struct sec_path *sp;

	sp = skb->sp;

	if (sp) {
		/*
		 * __xfrm_policy_check does not approve unless xfrm_policy_ok
		 * says that spi's match for policy and the socket.
		 *
		 *  Only need to verify the existence of an authorizable sp.
		 */
		for (i = 0; i < sp->len; i++) {
			struct xfrm_state *x = sp->xvec[i];

			if (x && selinux_authorizable_xfrm(x))
				goto accept;
		}
	}

	/* check SELinux sock for unlabelled access */
	rc = avc_has_perm(isec_sid, SECINITSID_UNLABELED, SECCLASS_ASSOCIATION,
			  ASSOCIATION__RECVFROM, NULL);
	if (rc)
		goto drop;

accept:
	return 0;

drop:
	return rc;
}

/*
 * POSTROUTE_LAST hook's XFRM processing:
 * If we have no security association, then we need to determine
 * whether the socket is allowed to send to an unlabelled destination.
 * If we do have a authorizable security association, then it has already been
 * checked in xfrm_policy_lookup hook.
 */
int selinux_xfrm_postroute_last(u32 isec_sid, struct sk_buff *skb)
{
	struct dst_entry *dst;
	int rc = 0;

	dst = skb->dst;

	if (dst) {
		struct dst_entry *dst_test;

		for (dst_test = dst; dst_test != 0;
		     dst_test = dst_test->child) {
			struct xfrm_state *x = dst_test->xfrm;

			if (x && selinux_authorizable_xfrm(x))
				goto out;
		}
	}

	rc = avc_has_perm(isec_sid, SECINITSID_UNLABELED, SECCLASS_ASSOCIATION,
			  ASSOCIATION__SENDTO, NULL);
out:
	return rc;
}
