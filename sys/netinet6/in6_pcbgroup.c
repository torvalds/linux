/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
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

#include "opt_inet6.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/rss_config.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#ifdef INET6
#include <netinet6/in6_pcb.h>
#include <netinet6/in6_rss.h>
#endif /* INET6 */

/*
 * Given a hash of whatever the covered tuple might be, return a pcbgroup
 * index.  Where RSS is supported, try to align bucket selection with RSS CPU
 * affinity strategy.
 */
static __inline u_int
in6_pcbgroup_getbucket(struct inpcbinfo *pcbinfo, uint32_t hash)
{

#ifdef RSS
	return (rss_getbucket(hash));
#else
	return (hash % pcbinfo->ipi_npcbgroups);
#endif
}

/*
 * Map a (hashtype, hash) tuple into a connection group, or NULL if the hash 
 * information is insufficient to identify the pcbgroup.  This might occur if
 * a TCP packet turnsup with a 2-tuple hash, or if an RSS hash is present but
 * RSS is not compiled into the kernel.
 */
struct inpcbgroup *
in6_pcbgroup_byhash(struct inpcbinfo *pcbinfo, u_int hashtype, uint32_t hash)
{

#ifdef RSS
	if ((pcbinfo->ipi_hashfields == IPI_HASHFIELDS_4TUPLE &&
	    hashtype == M_HASHTYPE_RSS_TCP_IPV6) ||
	    (pcbinfo->ipi_hashfields == IPI_HASHFIELDS_4TUPLE &&
	    hashtype == M_HASHTYPE_RSS_UDP_IPV6) ||
	    (pcbinfo->ipi_hashfields == IPI_HASHFIELDS_2TUPLE &&
	    hashtype == M_HASHTYPE_RSS_IPV6))
		return (&pcbinfo->ipi_pcbgroups[
		    in6_pcbgroup_getbucket(pcbinfo, hash)]);
#endif
	return (NULL);
}

struct inpcbgroup *
in6_pcbgroup_bymbuf(struct inpcbinfo *pcbinfo, struct mbuf *m)
{

	return (in6_pcbgroup_byhash(pcbinfo, M_HASHTYPE_GET(m),
	    m->m_pkthdr.flowid));
}

struct inpcbgroup *
in6_pcbgroup_bytuple(struct inpcbinfo *pcbinfo, const struct in6_addr *laddrp,
    u_short lport, const struct in6_addr *faddrp, u_short fport)
{
	uint32_t hash;

	/*
	 * RSS note: we pass foreign addr/port as source, and local addr/port
	 * as destination, as we want to align with what the hardware is
	 * doing.
	 */
	switch (pcbinfo->ipi_hashfields) {
	case IPI_HASHFIELDS_4TUPLE:
#ifdef RSS
		hash = rss_hash_ip6_4tuple(faddrp, fport, laddrp, lport);
#else
		hash = faddrp->s6_addr32[3] ^ fport;
#endif
		break;

	case IPI_HASHFIELDS_2TUPLE:
#ifdef RSS
		hash = rss_hash_ip6_2tuple(faddrp, laddrp);
#else
		hash = faddrp->s6_addr32[3] ^ laddrp->s6_addr32[3];
#endif
		break;

	default:
		hash = 0;
	}
	return (&pcbinfo->ipi_pcbgroups[in6_pcbgroup_getbucket(pcbinfo,
	    hash)]);
}

struct inpcbgroup *
in6_pcbgroup_byinpcb(struct inpcb *inp)
{

#ifdef	RSS
	/*
	 * Listen sockets with INP_RSS_BUCKET_SET set have a pre-determined
	 * RSS bucket and thus we should use this pcbgroup, rather than
	 * using a tuple or hash.
	 *
	 * XXX should verify that there's actually pcbgroups and inp_rss_listen_bucket
	 * fits in that!
	 */
	if (inp->inp_flags2 & INP_RSS_BUCKET_SET)
		return (&inp->inp_pcbinfo->ipi_pcbgroups[inp->inp_rss_listen_bucket]);
#endif

	return (in6_pcbgroup_bytuple(inp->inp_pcbinfo, &inp->in6p_laddr,
	    inp->inp_lport, &inp->in6p_faddr, inp->inp_fport));
}
