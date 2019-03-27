/*-
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/sockopt.h>
#include <sys/syslog.h>
#include <sys/proc.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>

#include <netipsec/ipsec.h>
#include <netipsec/ipsec6.h>
#include <netipsec/ipsec_support.h>
#include <netipsec/key.h>
#include <netipsec/key_debug.h>

MALLOC_DEFINE(M_IPSEC_INPCB, "inpcbpolicy", "inpcb-resident ipsec policy");

static void
ipsec_setsockaddrs_inpcb(struct inpcb *inp, union sockaddr_union *src,
    union sockaddr_union *dst, u_int dir)
{

#ifdef INET6
	if (inp->inp_vflag & INP_IPV6) {
		struct sockaddr_in6 *sin6;

		bzero(&src->sin6, sizeof(src->sin6));
		bzero(&dst->sin6, sizeof(dst->sin6));
		src->sin6.sin6_family = AF_INET6;
		src->sin6.sin6_len = sizeof(struct sockaddr_in6);
		dst->sin6.sin6_family = AF_INET6;
		dst->sin6.sin6_len = sizeof(struct sockaddr_in6);

		if (dir == IPSEC_DIR_OUTBOUND)
			sin6 = &src->sin6;
		else
			sin6 = &dst->sin6;
		sin6->sin6_addr = inp->in6p_laddr;
		sin6->sin6_port = inp->inp_lport;
		if (IN6_IS_SCOPE_LINKLOCAL(&inp->in6p_laddr)) {
			/* XXXAE: use in6p_zoneid */
			sin6->sin6_addr.s6_addr16[1] = 0;
			sin6->sin6_scope_id = ntohs(
			    inp->in6p_laddr.s6_addr16[1]);
		}

		if (dir == IPSEC_DIR_OUTBOUND)
			sin6 = &dst->sin6;
		else
			sin6 = &src->sin6;
		sin6->sin6_addr = inp->in6p_faddr;
		sin6->sin6_port = inp->inp_fport;
		if (IN6_IS_SCOPE_LINKLOCAL(&inp->in6p_faddr)) {
			/* XXXAE: use in6p_zoneid */
			sin6->sin6_addr.s6_addr16[1] = 0;
			sin6->sin6_scope_id = ntohs(
			    inp->in6p_faddr.s6_addr16[1]);
		}
	}
#endif
#ifdef INET
	if (inp->inp_vflag & INP_IPV4) {
		struct sockaddr_in *sin;

		bzero(&src->sin, sizeof(src->sin));
		bzero(&dst->sin, sizeof(dst->sin));
		src->sin.sin_family = AF_INET;
		src->sin.sin_len = sizeof(struct sockaddr_in);
		dst->sin.sin_family = AF_INET;
		dst->sin.sin_len = sizeof(struct sockaddr_in);

		if (dir == IPSEC_DIR_OUTBOUND)
			sin = &src->sin;
		else
			sin = &dst->sin;
		sin->sin_addr = inp->inp_laddr;
		sin->sin_port = inp->inp_lport;

		if (dir == IPSEC_DIR_OUTBOUND)
			sin = &dst->sin;
		else
			sin = &src->sin;
		sin->sin_addr = inp->inp_faddr;
		sin->sin_port = inp->inp_fport;
	}
#endif
}

void
ipsec_setspidx_inpcb(struct inpcb *inp, struct secpolicyindex *spidx,
    u_int dir)
{

	ipsec_setsockaddrs_inpcb(inp, &spidx->src, &spidx->dst, dir);
#ifdef INET6
	if (inp->inp_vflag & INP_IPV6) {
		spidx->prefs = sizeof(struct in6_addr) << 3;
		spidx->prefd = sizeof(struct in6_addr) << 3;
	}
#endif
#ifdef INET
	if (inp->inp_vflag & INP_IPV4) {
		spidx->prefs = sizeof(struct in_addr) << 3;
		spidx->prefd = sizeof(struct in_addr) << 3;
	}
#endif
	spidx->ul_proto = IPPROTO_TCP; /* XXX: currently only TCP uses this */
	spidx->dir = dir;
	KEYDBG(IPSEC_DUMP,
	    printf("%s: ", __func__); kdebug_secpolicyindex(spidx, NULL));
}

/* Initialize PCB policy. */
int
ipsec_init_pcbpolicy(struct inpcb *inp)
{

	IPSEC_ASSERT(inp != NULL, ("null inp"));
	IPSEC_ASSERT(inp->inp_sp == NULL, ("inp_sp already initialized"));

	inp->inp_sp = malloc(sizeof(struct inpcbpolicy), M_IPSEC_INPCB,
	    M_NOWAIT | M_ZERO);
	if (inp->inp_sp == NULL)
		return (ENOBUFS);
	return (0);
}

/* Delete PCB policy. */
int
ipsec_delete_pcbpolicy(struct inpcb *inp)
{

	if (inp->inp_sp == NULL)
		return (0);

	if (inp->inp_sp->sp_in != NULL)
		key_freesp(&inp->inp_sp->sp_in);

	if (inp->inp_sp->sp_out != NULL)
		key_freesp(&inp->inp_sp->sp_out);

	free(inp->inp_sp, M_IPSEC_INPCB);
	inp->inp_sp = NULL;
	return (0);
}

/* Deep-copy a policy in PCB. */
static struct secpolicy *
ipsec_deepcopy_pcbpolicy(struct secpolicy *src)
{
	struct secpolicy *dst;
	int i;

	if (src == NULL)
		return (NULL);

	IPSEC_ASSERT(src->state == IPSEC_SPSTATE_PCB, ("SP isn't PCB"));

	dst = key_newsp();
	if (dst == NULL)
		return (NULL);

	/* spidx is not copied here */
	dst->policy = src->policy;
	dst->state = src->state;
	dst->priority = src->priority;
	/* Do not touch the refcnt field. */

	/* Copy IPsec request chain. */
	for (i = 0; i < src->tcount; i++) {
		dst->req[i] = ipsec_newisr();
		if (dst->req[i] == NULL) {
			key_freesp(&dst);
			return (NULL);
		}
		bcopy(src->req[i], dst->req[i], sizeof(struct ipsecrequest));
		dst->tcount++;
	}
	KEYDBG(IPSEC_DUMP,
	    printf("%s: copied SP(%p) -> SP(%p)\n", __func__, src, dst);
	    kdebug_secpolicy(dst));
	return (dst);
}

/*
 * Copy IPsec policy from old INPCB into new.
 * It is expected that new INPCB has not configured policies.
 */
int
ipsec_copy_pcbpolicy(struct inpcb *old, struct inpcb *new)
{
	struct secpolicy *sp;

	/*
	 * old->inp_sp can be NULL if PCB was created when an IPsec
	 * support was unavailable. This is not an error, we don't have
	 * policies in this PCB, so nothing to copy.
	 */
	if (old->inp_sp == NULL)
		return (0);

	IPSEC_ASSERT(new->inp_sp != NULL, ("new inp_sp is NULL"));
	IPSEC_ASSERT((new->inp_sp->flags & (
	    INP_INBOUND_POLICY | INP_OUTBOUND_POLICY)) == 0,
	    ("new PCB already has configured policies"));
	INP_WLOCK_ASSERT(new);
	INP_LOCK_ASSERT(old);

	if (old->inp_sp->flags & INP_INBOUND_POLICY) {
		sp = ipsec_deepcopy_pcbpolicy(old->inp_sp->sp_in);
		if (sp == NULL)
			return (ENOBUFS);
		ipsec_setspidx_inpcb(new, &sp->spidx, IPSEC_DIR_INBOUND);
		if (new->inp_sp->sp_in != NULL)
			key_freesp(&new->inp_sp->sp_in);
		new->inp_sp->sp_in = sp;
		new->inp_sp->flags |= INP_INBOUND_POLICY;
	}
	if (old->inp_sp->flags & INP_OUTBOUND_POLICY) {
		sp = ipsec_deepcopy_pcbpolicy(old->inp_sp->sp_out);
		if (sp == NULL)
			return (ENOBUFS);
		ipsec_setspidx_inpcb(new, &sp->spidx, IPSEC_DIR_OUTBOUND);
		if (new->inp_sp->sp_out != NULL)
			key_freesp(&new->inp_sp->sp_out);
		new->inp_sp->sp_out = sp;
		new->inp_sp->flags |= INP_OUTBOUND_POLICY;
	}
	return (0);
}

static int
ipsec_set_pcbpolicy(struct inpcb *inp, struct ucred *cred,
    void *request, size_t len)
{
	struct sadb_x_policy *xpl;
	struct secpolicy **spp, *newsp;
	int error, flags;

	xpl = (struct sadb_x_policy *)request;
	/* Select direction. */
	switch (xpl->sadb_x_policy_dir) {
	case IPSEC_DIR_INBOUND:
	case IPSEC_DIR_OUTBOUND:
		break;
	default:
		ipseclog((LOG_ERR, "%s: invalid direction=%u\n", __func__,
			xpl->sadb_x_policy_dir));
		return (EINVAL);
	}
	/*
	 * Privileged sockets are allowed to set own security policy
	 * and configure IPsec bypass. Unprivileged sockets only can
	 * have ENTRUST policy.
	 */
	switch (xpl->sadb_x_policy_type) {
	case IPSEC_POLICY_IPSEC:
	case IPSEC_POLICY_BYPASS:
		if (cred != NULL &&
		    priv_check_cred(cred, PRIV_NETINET_IPSEC) != 0)
			return (EACCES);
		/* Allocate new SP entry. */
		newsp = key_msg2sp(xpl, len, &error);
		if (newsp == NULL)
			return (error);
		newsp->state = IPSEC_SPSTATE_PCB;
		newsp->spidx.ul_proto = IPSEC_ULPROTO_ANY;
#ifdef INET
		if (inp->inp_vflag & INP_IPV4) {
			newsp->spidx.src.sin.sin_family =
			    newsp->spidx.dst.sin.sin_family = AF_INET;
			newsp->spidx.src.sin.sin_len =
			    newsp->spidx.dst.sin.sin_len =
			    sizeof(struct sockaddr_in);
		}
#endif
#ifdef INET6
		if (inp->inp_vflag & INP_IPV6) {
			newsp->spidx.src.sin6.sin6_family =
			    newsp->spidx.dst.sin6.sin6_family = AF_INET6;
			newsp->spidx.src.sin6.sin6_len =
			    newsp->spidx.dst.sin6.sin6_len =
			    sizeof(struct sockaddr_in6);
		}
#endif
		break;
	case IPSEC_POLICY_ENTRUST:
		/* We just use NULL pointer for ENTRUST policy */
		newsp = NULL;
		break;
	default:
		/* Other security policy types aren't allowed for PCB */
		return (EINVAL);
	}

	INP_WLOCK(inp);
	if (xpl->sadb_x_policy_dir == IPSEC_DIR_INBOUND) {
		spp = &inp->inp_sp->sp_in;
		flags = INP_INBOUND_POLICY;
	} else {
		spp = &inp->inp_sp->sp_out;
		flags = INP_OUTBOUND_POLICY;
	}
	/* Clear old SP and set new SP. */
	if (*spp != NULL)
		key_freesp(spp);
	*spp = newsp;
	KEYDBG(IPSEC_DUMP,
	    printf("%s: new SP(%p)\n", __func__, newsp));
	if (newsp == NULL)
		inp->inp_sp->flags &= ~flags;
	else {
		inp->inp_sp->flags |= flags;
		KEYDBG(IPSEC_DUMP, kdebug_secpolicy(newsp));
	}
	INP_WUNLOCK(inp);
	return (0);
}

static int
ipsec_get_pcbpolicy(struct inpcb *inp, void *request, size_t *len)
{
	struct sadb_x_policy *xpl;
	struct secpolicy *sp;
	int error, flags;

	xpl = (struct sadb_x_policy *)request;

	INP_RLOCK(inp);
	flags = inp->inp_sp->flags;
	/* Select direction. */
	switch (xpl->sadb_x_policy_dir) {
	case IPSEC_DIR_INBOUND:
		sp = inp->inp_sp->sp_in;
		flags &= INP_INBOUND_POLICY;
		break;
	case IPSEC_DIR_OUTBOUND:
		sp = inp->inp_sp->sp_out;
		flags &= INP_OUTBOUND_POLICY;
		break;
	default:
		INP_RUNLOCK(inp);
		ipseclog((LOG_ERR, "%s: invalid direction=%u\n", __func__,
			xpl->sadb_x_policy_dir));
		return (EINVAL);
	}

	if (flags == 0) {
		/* Return ENTRUST policy */
		INP_RUNLOCK(inp);
		xpl->sadb_x_policy_exttype = SADB_X_EXT_POLICY;
		xpl->sadb_x_policy_type = IPSEC_POLICY_ENTRUST;
		xpl->sadb_x_policy_id = 0;
		xpl->sadb_x_policy_priority = 0;
		xpl->sadb_x_policy_len = PFKEY_UNIT64(sizeof(*xpl));
		*len = sizeof(*xpl);
		return (0);
	}

	IPSEC_ASSERT(sp != NULL,
	    ("sp is NULL, but flags is 0x%04x", inp->inp_sp->flags));

	key_addref(sp);
	INP_RUNLOCK(inp);
	error = key_sp2msg(sp, request, len);
	key_freesp(&sp);
	if (error == EINVAL)
		return (error);
	/*
	 * We return "success", but user should check *len.
	 * *len will be set to size of valid data and
	 * sadb_x_policy_len will contain needed size.
	 */
	return (0);
}

/* Handle socket option control request for PCB */
static int
ipsec_control_pcbpolicy(struct inpcb *inp, struct sockopt *sopt)
{
	void *optdata;
	size_t optlen;
	int error;

	if (inp->inp_sp == NULL)
		return (ENOPROTOOPT);

	/* Limit maximum request size to PAGE_SIZE */
	optlen = sopt->sopt_valsize;
	if (optlen < sizeof(struct sadb_x_policy) || optlen > PAGE_SIZE)
		return (EINVAL);

	optdata = malloc(optlen, M_TEMP, sopt->sopt_td ? M_WAITOK: M_NOWAIT);
	if (optdata == NULL)
		return (ENOBUFS);
	/*
	 * We need a hint from the user, what policy is requested - input
	 * or output? User should specify it in the buffer, even for
	 * setsockopt().
	 */
	error = sooptcopyin(sopt, optdata, optlen, optlen);
	if (error == 0) {
		if (sopt->sopt_dir == SOPT_SET)
			error = ipsec_set_pcbpolicy(inp,
			    sopt->sopt_td ? sopt->sopt_td->td_ucred: NULL,
			    optdata, optlen);
		else {
			error = ipsec_get_pcbpolicy(inp, optdata, &optlen);
			if (error == 0)
				error = sooptcopyout(sopt, optdata, optlen);
		}
	}
	free(optdata, M_TEMP);
	return (error);
}

#ifdef INET
/*
 * IPSEC_PCBCTL() method implementation for IPv4.
 */
int
ipsec4_pcbctl(struct inpcb *inp, struct sockopt *sopt)
{

	if (sopt->sopt_name != IP_IPSEC_POLICY)
		return (ENOPROTOOPT);
	return (ipsec_control_pcbpolicy(inp, sopt));
}
#endif

#ifdef INET6
/*
 * IPSEC_PCBCTL() method implementation for IPv6.
 */
int
ipsec6_pcbctl(struct inpcb *inp, struct sockopt *sopt)
{

	if (sopt->sopt_name != IPV6_IPSEC_POLICY)
		return (ENOPROTOOPT);
	return (ipsec_control_pcbpolicy(inp, sopt));
}
#endif

