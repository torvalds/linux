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
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <net/rss_config.h>

#include <netinet/in.h>

#include <netinet/in_pcb.h>
#include <netinet/in_rss.h>
#ifdef INET6
#include <netinet6/in6_pcb.h>
#endif /* INET6 */

/*
 * pcbgroups, or "connection groups" are based on Willman, Rixner, and Cox's
 * 2006 USENIX paper, "An Evaluation of Network Stack Parallelization
 * Strategies in Modern Operating Systems".  This implementation differs
 * significantly from that described in the paper, in that it attempts to
 * introduce not just notions of affinity for connections and distribute work
 * so as to reduce lock contention, but also align those notions with
 * hardware work distribution strategies such as RSS.  In this construction,
 * connection groups supplement, rather than replace, existing reservation
 * tables for protocol 4-tuples, offering CPU-affine lookup tables with
 * minimal cache line migration and lock contention during steady state
 * operation.
 *
 * Hardware-offloaded checksums are often inefficient in software -- for
 * example, Toeplitz, specified by RSS, introduced a significant overhead if
 * performed during per-packge processing.  It is therefore desirable to fall
 * back on traditional reservation table lookups without affinity where
 * hardware-offloaded checksums aren't available, such as for traffic over
 * non-RSS interfaces.
 *
 * Internet protocols, such as UDP and TCP, register to use connection groups
 * by providing an ipi_hashfields value other than IPI_HASHFIELDS_NONE; this
 * indicates to the connection group code whether a 2-tuple or 4-tuple is
 * used as an argument to hashes that assign a connection to a particular
 * group.  This must be aligned with any hardware offloaded distribution
 * model, such as RSS or similar approaches taken in embedded network boards.
 * Wildcard sockets require special handling, as in Willman 2006, and are
 * shared between connection groups -- while being protected by group-local
 * locks.  This means that connection establishment and teardown can be
 * signficantly more expensive than without connection groups, but that
 * steady-state processing can be significantly faster.
 *
 * When RSS is used, certain connection group parameters, such as the number
 * of groups, are provided by the RSS implementation, found in in_rss.c.
 * Otherwise, in_pcbgroup.c selects possible sensible parameters
 * corresponding to the degree of parallelism exposed by netisr.
 *
 * Most of the implementation of connection groups is in this file; however,
 * connection group lookup is implemented in in_pcb.c alongside reservation
 * table lookups -- see in_pcblookup_group().
 *
 * TODO:
 *
 * Implement dynamic rebalancing of buckets with connection groups; when
 * load is unevenly distributed, search for more optimal balancing on
 * demand.  This might require scaling up the number of connection groups
 * by <<1.
 *
 * Provide an IP 2-tuple or 4-tuple netisr m2cpu handler based on connection
 * groups for ip_input and ip6_input, allowing non-offloaded work
 * distribution.
 *
 * Expose effective CPU affinity of connections to userspace using socket
 * options.
 *
 * Investigate per-connection affinity overrides based on socket options; an
 * option could be set, certainly resulting in work being distributed
 * differently in software, and possibly propagated to supporting hardware
 * with TCAMs or hardware hash tables.  This might require connections to
 * exist in more than one connection group at a time.
 *
 * Hook netisr thread reconfiguration events, and propagate those to RSS so
 * that rebalancing can occur when the thread pool grows or shrinks.
 *
 * Expose per-pcbgroup statistics to userspace monitoring tools such as
 * netstat, in order to allow better debugging and profiling.
 */

void
in_pcbgroup_init(struct inpcbinfo *pcbinfo, u_int hashfields,
    int hash_nelements)
{
	struct inpcbgroup *pcbgroup;
	u_int numpcbgroups, pgn;

	/*
	 * Only enable connection groups for a protocol if it has been
	 * specifically requested.
	 */
	if (hashfields == IPI_HASHFIELDS_NONE)
		return;

	/*
	 * Connection groups are about multi-processor load distribution,
	 * lock contention, and connection CPU affinity.  As such, no point
	 * in turning them on for a uniprocessor machine, it only wastes
	 * memory.
	 */
	if (mp_ncpus == 1)
		return;

#ifdef RSS
	/*
	 * If we're using RSS, then RSS determines the number of connection
	 * groups to use: one connection group per RSS bucket.  If for some
	 * reason RSS isn't able to provide a number of buckets, disable
	 * connection groups entirely.
	 *
	 * XXXRW: Can this ever happen?
	 */
	numpcbgroups = rss_getnumbuckets();
	if (numpcbgroups == 0)
		return;
#else
	/*
	 * Otherwise, we'll just use one per CPU for now.  If we decide to
	 * do dynamic rebalancing a la RSS, we'll need similar logic here.
	 */
	numpcbgroups = mp_ncpus;
#endif

	pcbinfo->ipi_hashfields = hashfields;
	pcbinfo->ipi_pcbgroups = malloc(numpcbgroups *
	    sizeof(*pcbinfo->ipi_pcbgroups), M_PCB, M_WAITOK | M_ZERO);
	pcbinfo->ipi_npcbgroups = numpcbgroups;
	pcbinfo->ipi_wildbase = hashinit(hash_nelements, M_PCB,
	    &pcbinfo->ipi_wildmask);
	for (pgn = 0; pgn < pcbinfo->ipi_npcbgroups; pgn++) {
		pcbgroup = &pcbinfo->ipi_pcbgroups[pgn];
		pcbgroup->ipg_hashbase = hashinit(hash_nelements, M_PCB,
		    &pcbgroup->ipg_hashmask);
		INP_GROUP_LOCK_INIT(pcbgroup, "pcbgroup");

		/*
		 * Initialise notional affinity of the pcbgroup -- for RSS,
		 * we want the same notion of affinity as NICs to be used.  In
		 * the non-RSS case, just round robin for the time being.
		 *
		 * XXXRW: The notion of a bucket to CPU mapping is common at
		 * both pcbgroup and RSS layers -- does that mean that we
		 * should migrate it all from RSS to here, and just leave RSS
		 * responsible only for providing hashing and mapping funtions?
		 */
#ifdef RSS
		pcbgroup->ipg_cpu = rss_getcpu(pgn);
#else
		pcbgroup->ipg_cpu = (pgn % mp_ncpus);
#endif
	}
}

void
in_pcbgroup_destroy(struct inpcbinfo *pcbinfo)
{
	struct inpcbgroup *pcbgroup;
	u_int pgn;

	if (pcbinfo->ipi_npcbgroups == 0)
		return;

	for (pgn = 0; pgn < pcbinfo->ipi_npcbgroups; pgn++) {
		pcbgroup = &pcbinfo->ipi_pcbgroups[pgn];
		KASSERT(CK_LIST_EMPTY(pcbinfo->ipi_listhead),
		    ("in_pcbinfo_destroy: listhead not empty"));
		INP_GROUP_LOCK_DESTROY(pcbgroup);
		hashdestroy(pcbgroup->ipg_hashbase, M_PCB,
		    pcbgroup->ipg_hashmask);
	}
	hashdestroy(pcbinfo->ipi_wildbase, M_PCB, pcbinfo->ipi_wildmask);
	free(pcbinfo->ipi_pcbgroups, M_PCB);
	pcbinfo->ipi_pcbgroups = NULL;
	pcbinfo->ipi_npcbgroups = 0;
	pcbinfo->ipi_hashfields = 0;
}

/*
 * Given a hash of whatever the covered tuple might be, return a pcbgroup
 * index.  Where RSS is supported, try to align bucket selection with RSS CPU
 * affinity strategy.
 */
static __inline u_int
in_pcbgroup_getbucket(struct inpcbinfo *pcbinfo, uint32_t hash)
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
 * a TCP packet turns up with a 2-tuple hash, or if an RSS hash is present but
 * RSS is not compiled into the kernel.
 */
struct inpcbgroup *
in_pcbgroup_byhash(struct inpcbinfo *pcbinfo, u_int hashtype, uint32_t hash)
{

#ifdef RSS
	if ((pcbinfo->ipi_hashfields == IPI_HASHFIELDS_4TUPLE &&
	    hashtype == M_HASHTYPE_RSS_TCP_IPV4) ||
	    (pcbinfo->ipi_hashfields == IPI_HASHFIELDS_4TUPLE &&
	    hashtype == M_HASHTYPE_RSS_UDP_IPV4) ||
	    (pcbinfo->ipi_hashfields == IPI_HASHFIELDS_2TUPLE &&
	    hashtype == M_HASHTYPE_RSS_IPV4))
		return (&pcbinfo->ipi_pcbgroups[
		    in_pcbgroup_getbucket(pcbinfo, hash)]);
#endif
	return (NULL);
}

static struct inpcbgroup *
in_pcbgroup_bymbuf(struct inpcbinfo *pcbinfo, struct mbuf *m)
{

	return (in_pcbgroup_byhash(pcbinfo, M_HASHTYPE_GET(m),
	    m->m_pkthdr.flowid));
}

struct inpcbgroup *
in_pcbgroup_bytuple(struct inpcbinfo *pcbinfo, struct in_addr laddr,
    u_short lport, struct in_addr faddr, u_short fport)
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
		hash = rss_hash_ip4_4tuple(faddr, fport, laddr, lport);
#else
		hash = faddr.s_addr ^ fport;
#endif
		break;

	case IPI_HASHFIELDS_2TUPLE:
#ifdef RSS
		hash = rss_hash_ip4_2tuple(faddr, laddr);
#else
		hash = faddr.s_addr ^ laddr.s_addr;
#endif
		break;

	default:
		hash = 0;
	}
	return (&pcbinfo->ipi_pcbgroups[in_pcbgroup_getbucket(pcbinfo,
	    hash)]);
}

struct inpcbgroup *
in_pcbgroup_byinpcb(struct inpcb *inp)
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

	return (in_pcbgroup_bytuple(inp->inp_pcbinfo, inp->inp_laddr,
	    inp->inp_lport, inp->inp_faddr, inp->inp_fport));
}

static void
in_pcbwild_add(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo;
	struct inpcbhead *head;
	u_int pgn;

	INP_WLOCK_ASSERT(inp);
	KASSERT(!(inp->inp_flags2 & INP_PCBGROUPWILD),
	    ("%s: is wild",__func__));

	pcbinfo = inp->inp_pcbinfo;
	for (pgn = 0; pgn < pcbinfo->ipi_npcbgroups; pgn++)
		INP_GROUP_LOCK(&pcbinfo->ipi_pcbgroups[pgn]);
	head = &pcbinfo->ipi_wildbase[INP_PCBHASH(INADDR_ANY, inp->inp_lport,
	    0, pcbinfo->ipi_wildmask)];
	CK_LIST_INSERT_HEAD(head, inp, inp_pcbgroup_wild);
	inp->inp_flags2 |= INP_PCBGROUPWILD;
	for (pgn = 0; pgn < pcbinfo->ipi_npcbgroups; pgn++)
		INP_GROUP_UNLOCK(&pcbinfo->ipi_pcbgroups[pgn]);
}

static void
in_pcbwild_remove(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo;
	u_int pgn;

	INP_WLOCK_ASSERT(inp);
	KASSERT((inp->inp_flags2 & INP_PCBGROUPWILD),
	    ("%s: not wild", __func__));

	pcbinfo = inp->inp_pcbinfo;
	for (pgn = 0; pgn < pcbinfo->ipi_npcbgroups; pgn++)
		INP_GROUP_LOCK(&pcbinfo->ipi_pcbgroups[pgn]);
	CK_LIST_REMOVE(inp, inp_pcbgroup_wild);
	for (pgn = 0; pgn < pcbinfo->ipi_npcbgroups; pgn++)
		INP_GROUP_UNLOCK(&pcbinfo->ipi_pcbgroups[pgn]);
	inp->inp_flags2 &= ~INP_PCBGROUPWILD;
}

static __inline int
in_pcbwild_needed(struct inpcb *inp)
{
#ifdef	RSS
	/*
	 * If it's a listen socket and INP_RSS_BUCKET_SET is set,
	 * it's a wildcard socket _but_ it's in a specific pcbgroup.
	 * Thus we don't treat it as a pcbwild inp.
	 */
	if (inp->inp_flags2 & INP_RSS_BUCKET_SET)
		return (0);
#endif

#ifdef INET6
	if (inp->inp_vflag & INP_IPV6)
		return (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr));
	else
#endif
		return (inp->inp_faddr.s_addr == htonl(INADDR_ANY));
}

static void
in_pcbwild_update_internal(struct inpcb *inp)
{
	int wildcard_needed;

	wildcard_needed = in_pcbwild_needed(inp);
	if (wildcard_needed && !(inp->inp_flags2 & INP_PCBGROUPWILD))
		in_pcbwild_add(inp);
	else if (!wildcard_needed && (inp->inp_flags2 & INP_PCBGROUPWILD))
		in_pcbwild_remove(inp);
}

/*
 * Update the pcbgroup of an inpcb, which might include removing an old
 * pcbgroup reference and/or adding a new one.  Wildcard processing is not
 * performed here, although ideally we'll never install a pcbgroup for a
 * wildcard inpcb (asserted below).
 */
static void
in_pcbgroup_update_internal(struct inpcbinfo *pcbinfo,
    struct inpcbgroup *newpcbgroup, struct inpcb *inp)
{
	struct inpcbgroup *oldpcbgroup;
	struct inpcbhead *pcbhash;
	uint32_t hashkey_faddr;

	INP_WLOCK_ASSERT(inp);

	oldpcbgroup = inp->inp_pcbgroup;
	if (oldpcbgroup != NULL && oldpcbgroup != newpcbgroup) {
		INP_GROUP_LOCK(oldpcbgroup);
		CK_LIST_REMOVE(inp, inp_pcbgrouphash);
		inp->inp_pcbgroup = NULL;
		INP_GROUP_UNLOCK(oldpcbgroup);
	}
	if (newpcbgroup != NULL && oldpcbgroup != newpcbgroup) {
#ifdef INET6
		if (inp->inp_vflag & INP_IPV6)
			hashkey_faddr = INP6_PCBHASHKEY(&inp->in6p_faddr);
		else
#endif
			hashkey_faddr = inp->inp_faddr.s_addr;
		INP_GROUP_LOCK(newpcbgroup);
		/*
		 * If the inp is an RSS bucket wildcard entry, ensure
		 * that the PCB hash is calculated correctly.
		 *
		 * The wildcard hash calculation differs from the
		 * non-wildcard definition.  The source address is
		 * INADDR_ANY and the far port is 0.
		 */
		if (inp->inp_flags2 & INP_RSS_BUCKET_SET) {
			pcbhash = &newpcbgroup->ipg_hashbase[
			    INP_PCBHASH(INADDR_ANY, inp->inp_lport, 0,
			    newpcbgroup->ipg_hashmask)];
		} else {
			pcbhash = &newpcbgroup->ipg_hashbase[
			    INP_PCBHASH(hashkey_faddr, inp->inp_lport,
			    inp->inp_fport,
			    newpcbgroup->ipg_hashmask)];
		}
		CK_LIST_INSERT_HEAD(pcbhash, inp, inp_pcbgrouphash);
		inp->inp_pcbgroup = newpcbgroup;
		INP_GROUP_UNLOCK(newpcbgroup);
	}

	KASSERT(!(newpcbgroup != NULL && in_pcbwild_needed(inp)),
	    ("%s: pcbgroup and wildcard!", __func__));
}

/*
 * Two update paths: one in which the 4-tuple on an inpcb has been updated
 * and therefore connection groups may need to change (or a wildcard entry
 * may needed to be installed), and another in which the 4-tuple has been
 * set as a result of a packet received, in which case we may be able to use
 * the hash on the mbuf to avoid doing a software hash calculation for RSS.
 *
 * In each case: first, let the wildcard code have a go at placing it as a
 * wildcard socket.  If it was a wildcard, or if the connection has been
 * dropped, then no pcbgroup is required (so potentially clear it);
 * otherwise, calculate and update the pcbgroup for the inpcb.
 */
void
in_pcbgroup_update(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo;
	struct inpcbgroup *newpcbgroup;

	INP_WLOCK_ASSERT(inp);

	pcbinfo = inp->inp_pcbinfo;
	if (!in_pcbgroup_enabled(pcbinfo))
		return;

	in_pcbwild_update_internal(inp);
	if (!(inp->inp_flags2 & INP_PCBGROUPWILD) &&
	    !(inp->inp_flags & INP_DROPPED)) {
#ifdef INET6
		if (inp->inp_vflag & INP_IPV6)
			newpcbgroup = in6_pcbgroup_byinpcb(inp);
		else
#endif
			newpcbgroup = in_pcbgroup_byinpcb(inp);
	} else
		newpcbgroup = NULL;
	in_pcbgroup_update_internal(pcbinfo, newpcbgroup, inp);
}

void
in_pcbgroup_update_mbuf(struct inpcb *inp, struct mbuf *m)
{
	struct inpcbinfo *pcbinfo;
	struct inpcbgroup *newpcbgroup;

	INP_WLOCK_ASSERT(inp);

	pcbinfo = inp->inp_pcbinfo;
	if (!in_pcbgroup_enabled(pcbinfo))
		return;

	/*
	 * Possibly should assert !INP_PCBGROUPWILD rather than testing for
	 * it; presumably this function should never be called for anything
	 * other than non-wildcard socket?
	 */
	in_pcbwild_update_internal(inp);
	if (!(inp->inp_flags2 & INP_PCBGROUPWILD) &&
	    !(inp->inp_flags & INP_DROPPED)) {
		newpcbgroup = in_pcbgroup_bymbuf(pcbinfo, m);
#ifdef INET6
		if (inp->inp_vflag & INP_IPV6) {
			if (newpcbgroup == NULL)
				newpcbgroup = in6_pcbgroup_byinpcb(inp);
		} else {
#endif
			if (newpcbgroup == NULL)
				newpcbgroup = in_pcbgroup_byinpcb(inp);
#ifdef INET6
		}
#endif
	} else
		newpcbgroup = NULL;
	in_pcbgroup_update_internal(pcbinfo, newpcbgroup, inp);
}

/*
 * Remove pcbgroup entry and optional pcbgroup wildcard entry for this inpcb.
 */
void
in_pcbgroup_remove(struct inpcb *inp)
{
	struct inpcbgroup *pcbgroup;

	INP_WLOCK_ASSERT(inp);

	if (!in_pcbgroup_enabled(inp->inp_pcbinfo))
		return;

	if (inp->inp_flags2 & INP_PCBGROUPWILD)
		in_pcbwild_remove(inp);

	pcbgroup = inp->inp_pcbgroup;
	if (pcbgroup != NULL) {
		INP_GROUP_LOCK(pcbgroup);
		CK_LIST_REMOVE(inp, inp_pcbgrouphash);
		inp->inp_pcbgroup = NULL;
		INP_GROUP_UNLOCK(pcbgroup);
	}
}

/*
 * Query whether or not it is appropriate to use pcbgroups to look up inpcbs
 * for a protocol.
 */
int
in_pcbgroup_enabled(struct inpcbinfo *pcbinfo)
{

	return (pcbinfo->ipi_npcbgroups > 0);
}
