/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Andre Oppermann, Internet Business Solutions AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ipfw.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#ifndef INET
#error IPFIREWALL requires INET.
#endif /* INET */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <net/pfil.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#endif

#include <netgraph/ng_ipfw.h>

#include <netpfil/ipfw/ip_fw_private.h>

#include <machine/in_cksum.h>

VNET_DEFINE_STATIC(int, fw_enable) = 1;
#define V_fw_enable	VNET(fw_enable)

#ifdef INET6
VNET_DEFINE_STATIC(int, fw6_enable) = 1;
#define V_fw6_enable	VNET(fw6_enable)
#endif

VNET_DEFINE_STATIC(int, fwlink_enable) = 0;
#define V_fwlink_enable	VNET(fwlink_enable)

int ipfw_chg_hook(SYSCTL_HANDLER_ARGS);

/* Forward declarations. */
static int ipfw_divert(struct mbuf **, struct ip_fw_args *, bool);

#ifdef SYSCTL_NODE

SYSBEGIN(f1)

SYSCTL_DECL(_net_inet_ip_fw);
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, enable,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE3,
    &VNET_NAME(fw_enable), 0, ipfw_chg_hook, "I", "Enable ipfw");
#ifdef INET6
SYSCTL_DECL(_net_inet6_ip6_fw);
SYSCTL_PROC(_net_inet6_ip6_fw, OID_AUTO, enable,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE3,
    &VNET_NAME(fw6_enable), 0, ipfw_chg_hook, "I", "Enable ipfw+6");
#endif /* INET6 */

SYSCTL_DECL(_net_link_ether);
SYSCTL_PROC(_net_link_ether, OID_AUTO, ipfw,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE3,
    &VNET_NAME(fwlink_enable), 0, ipfw_chg_hook, "I",
    "Pass ether pkts through firewall");

SYSEND

#endif /* SYSCTL_NODE */

/*
 * The pfilter hook to pass packets to ipfw_chk and then to
 * dummynet, divert, netgraph or other modules.
 * The packet may be consumed.
 */
static pfil_return_t
ipfw_check_packet(struct mbuf **m0, struct ifnet *ifp, int flags,
    void *ruleset __unused, struct inpcb *inp)
{
	struct ip_fw_args args;
	struct m_tag *tag;
	pfil_return_t ret;
	int ipfw;

	args.flags = (flags & PFIL_IN) ? IPFW_ARGS_IN : IPFW_ARGS_OUT;
again:
	/*
	 * extract and remove the tag if present. If we are left
	 * with onepass, optimize the outgoing path.
	 */
	tag = m_tag_locate(*m0, MTAG_IPFW_RULE, 0, NULL);
	if (tag != NULL) {
		args.rule = *((struct ipfw_rule_ref *)(tag+1));
		m_tag_delete(*m0, tag);
		if (args.rule.info & IPFW_ONEPASS)
			return (0);
		args.flags |= IPFW_ARGS_REF;
	}

	args.m = *m0;
	args.ifp = ifp;
	args.inp = inp;

	ipfw = ipfw_chk(&args);
	*m0 = args.m;

	KASSERT(*m0 != NULL || ipfw == IP_FW_DENY ||
	    ipfw == IP_FW_NAT64, ("%s: m0 is NULL", __func__));

	ret = PFIL_PASS;
	switch (ipfw) {
	case IP_FW_PASS:
		/* next_hop may be set by ipfw_chk */
		if ((args.flags & (IPFW_ARGS_NH4 | IPFW_ARGS_NH4PTR |
		    IPFW_ARGS_NH6 | IPFW_ARGS_NH6PTR)) == 0)
			break;
#if (!defined(INET6) && !defined(INET))
		ret = PFIL_DROPPED;
#else
	    {
		void *psa;
		size_t len;
#ifdef INET
		if (args.flags & (IPFW_ARGS_NH4 | IPFW_ARGS_NH4PTR)) {
			MPASS((args.flags & (IPFW_ARGS_NH4 |
			    IPFW_ARGS_NH4PTR)) != (IPFW_ARGS_NH4 |
			    IPFW_ARGS_NH4PTR));
			MPASS((args.flags & (IPFW_ARGS_NH6 |
			    IPFW_ARGS_NH6PTR)) == 0);
			len = sizeof(struct sockaddr_in);
			psa = (args.flags & IPFW_ARGS_NH4) ?
			    &args.hopstore : args.next_hop;
			if (in_localip(satosin(psa)->sin_addr))
				(*m0)->m_flags |= M_FASTFWD_OURS;
			(*m0)->m_flags |= M_IP_NEXTHOP;
		}
#endif /* INET */
#ifdef INET6
		if (args.flags & (IPFW_ARGS_NH6 | IPFW_ARGS_NH6PTR)) {
			MPASS((args.flags & (IPFW_ARGS_NH6 |
			    IPFW_ARGS_NH6PTR)) != (IPFW_ARGS_NH6 |
			    IPFW_ARGS_NH6PTR));
			MPASS((args.flags & (IPFW_ARGS_NH4 |
			    IPFW_ARGS_NH4PTR)) == 0);
			len = sizeof(struct sockaddr_in6);
			psa = args.next_hop6;
			(*m0)->m_flags |= M_IP6_NEXTHOP;
		}
#endif /* INET6 */
		/*
		 * Incoming packets should not be tagged so we do not
		 * m_tag_find. Outgoing packets may be tagged, so we
		 * reuse the tag if present.
		 */
		tag = (flags & PFIL_IN) ? NULL :
			m_tag_find(*m0, PACKET_TAG_IPFORWARD, NULL);
		if (tag != NULL) {
			m_tag_unlink(*m0, tag);
		} else {
			tag = m_tag_get(PACKET_TAG_IPFORWARD, len,
			    M_NOWAIT);
			if (tag == NULL) {
				ret = PFIL_DROPPED;
				break;
			}
		}
		if ((args.flags & IPFW_ARGS_NH6) == 0)
			bcopy(psa, tag + 1, len);
		m_tag_prepend(*m0, tag);
		ret = 0;
#ifdef INET6
		/* IPv6 next hop needs additional handling */
		if (args.flags & (IPFW_ARGS_NH6 | IPFW_ARGS_NH6PTR)) {
			struct sockaddr_in6 *sa6;

			sa6 = satosin6(tag + 1);
			if (args.flags & IPFW_ARGS_NH6) {
				sa6->sin6_family = AF_INET6;
				sa6->sin6_len = sizeof(*sa6);
				sa6->sin6_addr = args.hopstore6.sin6_addr;
				sa6->sin6_port = args.hopstore6.sin6_port;
				sa6->sin6_scope_id =
				    args.hopstore6.sin6_scope_id;
			}
			/*
			 * If nh6 address is link-local we should convert
			 * it to kernel internal form before doing any
			 * comparisons.
			 */
			if (sa6_embedscope(sa6, V_ip6_use_defzone) != 0) {
				ret = PFIL_DROPPED;
				break;
			}
			if (in6_localip(&sa6->sin6_addr))
				(*m0)->m_flags |= M_FASTFWD_OURS;
		}
#endif /* INET6 */
	    }
#endif /* INET || INET6 */
		break;

	case IP_FW_DENY:
		ret = PFIL_DROPPED;
		break;

	case IP_FW_DUMMYNET:
		if (ip_dn_io_ptr == NULL) {
			ret = PFIL_DROPPED;
			break;
		}
		MPASS(args.flags & IPFW_ARGS_REF);
		if (args.flags & (IPFW_ARGS_IP4 | IPFW_ARGS_IP6))
			(void )ip_dn_io_ptr(m0, &args);
		else {
			ret = PFIL_DROPPED;
			break;
		}
		/*
		 * XXX should read the return value.
		 * dummynet normally eats the packet and sets *m0=NULL
		 * unless the packet can be sent immediately. In this
		 * case args is updated and we should re-run the
		 * check without clearing args.
		 */
		if (*m0 != NULL)
			goto again;
		ret = PFIL_CONSUMED;
		break;

	case IP_FW_TEE:
	case IP_FW_DIVERT:
		if (ip_divert_ptr == NULL) {
			ret = PFIL_DROPPED;
			break;
		}
		MPASS(args.flags & IPFW_ARGS_REF);
		(void )ipfw_divert(m0, &args, ipfw == IP_FW_TEE);
		/* continue processing for the original packet (tee). */
		if (*m0)
			goto again;
		ret = PFIL_CONSUMED;
		break;

	case IP_FW_NGTEE:
	case IP_FW_NETGRAPH:
		if (ng_ipfw_input_p == NULL) {
			ret = PFIL_DROPPED;
			break;
		}
		MPASS(args.flags & IPFW_ARGS_REF);
		(void )ng_ipfw_input_p(m0, &args, ipfw == IP_FW_NGTEE);
		if (ipfw == IP_FW_NGTEE) /* ignore errors for NGTEE */
			goto again;	/* continue with packet */
		ret = PFIL_CONSUMED;
		break;

	case IP_FW_NAT:
		/* honor one-pass in case of successful nat */
		if (V_fw_one_pass)
			break;
		goto again;

	case IP_FW_REASS:
		goto again;		/* continue with packet */

	case IP_FW_NAT64:
		ret = PFIL_CONSUMED;
		break;

	default:
		KASSERT(0, ("%s: unknown retval", __func__));
	}

	if (ret != PFIL_PASS) {
		if (*m0)
			FREE_PKT(*m0);
		*m0 = NULL;
	}

	return (ret);
}

/*
 * ipfw processing for ethernet packets (in and out).
 */
static pfil_return_t
ipfw_check_frame(pfil_packet_t p, struct ifnet *ifp, int flags,
    void *ruleset __unused, struct inpcb *inp)
{
	struct ip_fw_args args;
	pfil_return_t ret;
	bool mem, realloc;
	int ipfw;

	if (flags & PFIL_MEMPTR) {
		mem = true;
		realloc = false;
		args.flags = PFIL_LENGTH(flags) | IPFW_ARGS_ETHER;
		args.mem = p.mem;
	} else {
		mem = realloc = false;
		args.flags = IPFW_ARGS_ETHER;
	}
	args.flags |= (flags & PFIL_IN) ? IPFW_ARGS_IN : IPFW_ARGS_OUT;
	args.ifp = ifp;
	args.inp = inp;

again:
	if (!mem) {
		/*
		 * Fetch start point from rule, if any.
		 * Remove the tag if present.
		 */
		struct m_tag *mtag;

		mtag = m_tag_locate(*p.m, MTAG_IPFW_RULE, 0, NULL);
		if (mtag != NULL) {
			args.rule = *((struct ipfw_rule_ref *)(mtag+1));
			m_tag_delete(*p.m, mtag);
			if (args.rule.info & IPFW_ONEPASS)
				return (PFIL_PASS);
			args.flags |= IPFW_ARGS_REF;
		}
		args.m = *p.m;
	}

	ipfw = ipfw_chk(&args);

	ret = PFIL_PASS;
	switch (ipfw) {
	case IP_FW_PASS:
		break;

	case IP_FW_DENY:
		ret = PFIL_DROPPED;
		break;

	case IP_FW_DUMMYNET:
		if (ip_dn_io_ptr == NULL) {
			ret = PFIL_DROPPED;
			break;
		}
		if (mem) {
			if (pfil_realloc(&p, flags, ifp) != 0) {
				ret = PFIL_DROPPED;
				break;
			}
			mem = false;
			realloc = true;
		}
		MPASS(args.flags & IPFW_ARGS_REF);
		ip_dn_io_ptr(p.m, &args);
		return (PFIL_CONSUMED);

	case IP_FW_NGTEE:
	case IP_FW_NETGRAPH:
		if (ng_ipfw_input_p == NULL) {
			ret = PFIL_DROPPED;
			break;
		}
		if (mem) {
			if (pfil_realloc(&p, flags, ifp) != 0) {
				ret = PFIL_DROPPED;
				break;
			}
			mem = false;
			realloc = true;
		}
		MPASS(args.flags & IPFW_ARGS_REF);
		(void )ng_ipfw_input_p(p.m, &args, ipfw == IP_FW_NGTEE);
		if (ipfw == IP_FW_NGTEE) /* ignore errors for NGTEE */
			goto again;	/* continue with packet */
		ret = PFIL_CONSUMED;
		break;

	default:
		KASSERT(0, ("%s: unknown retval", __func__));
	}

	if (!mem && ret != PFIL_PASS) {
		if (*p.m)
			FREE_PKT(*p.m);
		*p.m = NULL;
	}

	if (realloc && ret == PFIL_PASS)
		ret = PFIL_REALLOCED;

	return (ret);
}

/* do the divert, return 1 on error 0 on success */
static int
ipfw_divert(struct mbuf **m0, struct ip_fw_args *args, bool tee)
{
	/*
	 * ipfw_chk() has already tagged the packet with the divert tag.
	 * If tee is set, copy packet and return original.
	 * If not tee, consume packet and send it to divert socket.
	 */
	struct mbuf *clone;
	struct ip *ip = mtod(*m0, struct ip *);
	struct m_tag *tag;

	/* Cloning needed for tee? */
	if (tee == false) {
		clone = *m0;	/* use the original mbuf */
		*m0 = NULL;
	} else {
		clone = m_dup(*m0, M_NOWAIT);
		/* If we cannot duplicate the mbuf, we sacrifice the divert
		 * chain and continue with the tee-ed packet.
		 */
		if (clone == NULL)
			return 1;
	}

	/*
	 * Divert listeners can normally handle non-fragmented packets,
	 * but we can only reass in the non-tee case.
	 * This means that listeners on a tee rule may get fragments,
	 * and have to live with that.
	 * Note that we now have the 'reass' ipfw option so if we care
	 * we can do it before a 'tee'.
	 */
	if (tee == false) switch (ip->ip_v) {
	case IPVERSION:
	    if (ntohs(ip->ip_off) & (IP_MF | IP_OFFMASK)) {
		int hlen;
		struct mbuf *reass;

		reass = ip_reass(clone); /* Reassemble packet. */
		if (reass == NULL)
			return 0; /* not an error */
		/* if reass = NULL then it was consumed by ip_reass */
		/*
		 * IP header checksum fixup after reassembly and leave header
		 * in network byte order.
		 */
		ip = mtod(reass, struct ip *);
		hlen = ip->ip_hl << 2;
		ip->ip_sum = 0;
		if (hlen == sizeof(struct ip))
			ip->ip_sum = in_cksum_hdr(ip);
		else
			ip->ip_sum = in_cksum(reass, hlen);
		clone = reass;
	    }
	    break;
#ifdef INET6
	case IPV6_VERSION >> 4:
	    {
	    struct ip6_hdr *const ip6 = mtod(clone, struct ip6_hdr *);

		if (ip6->ip6_nxt == IPPROTO_FRAGMENT) {
			int nxt, off;

			off = sizeof(struct ip6_hdr);
			nxt = frag6_input(&clone, &off, 0);
			if (nxt == IPPROTO_DONE)
				return (0);
		}
		break;
	    }
#endif
	}

	/* attach a tag to the packet with the reinject info */
	tag = m_tag_alloc(MTAG_IPFW_RULE, 0,
		    sizeof(struct ipfw_rule_ref), M_NOWAIT);
	if (tag == NULL) {
		FREE_PKT(clone);
		return 1;
	}
	*((struct ipfw_rule_ref *)(tag+1)) = args->rule;
	m_tag_prepend(clone, tag);

	/* Do the dirty job... */
	ip_divert_ptr(clone, args->flags & IPFW_ARGS_IN);
	return 0;
}

/*
 * attach or detach hooks for a given protocol family
 */
VNET_DEFINE_STATIC(pfil_hook_t, ipfw_inet_hook);
#define	V_ipfw_inet_hook	VNET(ipfw_inet_hook)
#ifdef INET6
VNET_DEFINE_STATIC(pfil_hook_t, ipfw_inet6_hook);
#define	V_ipfw_inet6_hook	VNET(ipfw_inet6_hook)
#endif
VNET_DEFINE_STATIC(pfil_hook_t, ipfw_link_hook);
#define	V_ipfw_link_hook	VNET(ipfw_link_hook)

static void
ipfw_hook(int pf)
{
	struct pfil_hook_args pha;
	pfil_hook_t *h;

	pha.pa_version = PFIL_VERSION;
	pha.pa_flags = PFIL_IN | PFIL_OUT;
	pha.pa_modname = "ipfw";
	pha.pa_ruleset = NULL;

	switch (pf) {
	case AF_INET:
		pha.pa_func = ipfw_check_packet;
		pha.pa_type = PFIL_TYPE_IP4;
		pha.pa_rulname = "default";
		h = &V_ipfw_inet_hook;
		break;
#ifdef INET6
	case AF_INET6:
		pha.pa_func = ipfw_check_packet;
		pha.pa_type = PFIL_TYPE_IP6;
		pha.pa_rulname = "default6";
		h = &V_ipfw_inet6_hook;
		break;
#endif
	case AF_LINK:
		pha.pa_func = ipfw_check_frame;
		pha.pa_type = PFIL_TYPE_ETHERNET;
		pha.pa_rulname = "default-link";
		pha.pa_flags |= PFIL_MEMPTR;
		h = &V_ipfw_link_hook;
		break;
	}

	*h = pfil_add_hook(&pha);
}

static void
ipfw_unhook(int pf)
{

	switch (pf) {
	case AF_INET:
		pfil_remove_hook(V_ipfw_inet_hook);
		break;
#ifdef INET6
	case AF_INET6:
		pfil_remove_hook(V_ipfw_inet6_hook);
		break;
#endif
	case AF_LINK:
		pfil_remove_hook(V_ipfw_link_hook);
		break;
	}
}

static int
ipfw_link(int pf, bool unlink)
{
	struct pfil_link_args pla;

	pla.pa_version = PFIL_VERSION;
	pla.pa_flags = PFIL_IN | PFIL_OUT | PFIL_HEADPTR | PFIL_HOOKPTR;
	if (unlink)
		pla.pa_flags |= PFIL_UNLINK;

	switch (pf) {
	case AF_INET:
		pla.pa_head = V_inet_pfil_head;
		pla.pa_hook = V_ipfw_inet_hook;
		break;
#ifdef INET6
	case AF_INET6:
		pla.pa_head = V_inet6_pfil_head;
		pla.pa_hook = V_ipfw_inet6_hook;
		break;
#endif
	case AF_LINK:
		pla.pa_head = V_link_pfil_head;
		pla.pa_hook = V_ipfw_link_hook;
		break;
	}

	return (pfil_link(&pla));
}

int
ipfw_attach_hooks(void)
{
	int error = 0;

	ipfw_hook(AF_INET);
	TUNABLE_INT_FETCH("net.inet.ip.fw.enable", &V_fw_enable);
	if (V_fw_enable && (error = ipfw_link(AF_INET, false)) != 0)
                printf("ipfw_hook() error\n");
#ifdef INET6
	ipfw_hook(AF_INET6);
	TUNABLE_INT_FETCH("net.inet6.ip6.fw.enable", &V_fw6_enable);
	if (V_fw6_enable && (error = ipfw_link(AF_INET6, false)) != 0)
                printf("ipfw6_hook() error\n");
#endif
	ipfw_hook(AF_LINK);
	TUNABLE_INT_FETCH("net.link.ether.ipfw", &V_fwlink_enable);
	if (V_fwlink_enable && (error = ipfw_link(AF_LINK, false)) != 0)
                printf("ipfw_link_hook() error\n");

	return (error);
}

void
ipfw_detach_hooks(void)
{

	ipfw_unhook(AF_INET);
#ifdef INET6
	ipfw_unhook(AF_INET6);
#endif
	ipfw_unhook(AF_LINK);
}

int
ipfw_chg_hook(SYSCTL_HANDLER_ARGS)
{
	int newval;
	int error;
	int af;

	if (arg1 == &V_fw_enable)
		af = AF_INET;
#ifdef INET6
	else if (arg1 == &V_fw6_enable)
		af = AF_INET6;
#endif
	else if (arg1 == &V_fwlink_enable)
		af = AF_LINK;
	else 
		return (EINVAL);

	newval = *(int *)arg1;
	/* Handle sysctl change */
	error = sysctl_handle_int(oidp, &newval, 0, req);

	if (error)
		return (error);

	/* Formalize new value */
	newval = (newval) ? 1 : 0;

	if (*(int *)arg1 == newval)
		return (0);

	error = ipfw_link(af, newval == 0 ? true : false);
	if (error)
		return (error);
	*(int *)arg1 = newval;

	return (0);
}
/* end of file */
