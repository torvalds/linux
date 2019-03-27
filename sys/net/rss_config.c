/*-
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
#include "opt_pcbgroup.h"

#ifndef PCBGROUP
#error "options RSS depends on options PCBGROUP"
#endif

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/priv.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/rss_config.h>
#include <net/toeplitz.h>

/*-
 * Operating system parts of receiver-side scaling (RSS), which allows
 * network cards to direct flows to particular receive queues based on hashes
 * of header tuples.  This implementation aligns RSS buckets with connection
 * groups at the TCP/IP layer, so each bucket is associated with exactly one
 * group.  As a result, the group lookup structures (and lock) should have an
 * effective affinity with exactly one CPU.
 *
 * Network device drivers needing to configure RSS will query this framework
 * for parameters, such as the current RSS key, hashing policies, number of
 * bits, and indirection table mapping hashes to buckets and CPUs.  They may
 * provide their own supplementary information, such as queue<->CPU bindings.
 * It is the responsibility of the network device driver to inject packets
 * into the stack on as close to the right CPU as possible, if playing by RSS
 * rules.
 *
 * TODO:
 *
 * - Synchronization for rss_key and other future-configurable parameters.
 * - Event handler drivers can register to pick up RSS configuration changes.
 * - Should we allow rss_basecpu to be configured?
 * - Randomize key on boot.
 * - IPv6 support.
 * - Statistics on how often there's a misalignment between hardware
 *   placement and pcbgroup expectations.
 */

SYSCTL_DECL(_net_inet);
SYSCTL_NODE(_net_inet, OID_AUTO, rss, CTLFLAG_RW, 0, "Receive-side steering");

/*
 * Toeplitz is the only required hash function in the RSS spec, so use it by
 * default.
 */
static u_int	rss_hashalgo = RSS_HASH_TOEPLITZ;
SYSCTL_INT(_net_inet_rss, OID_AUTO, hashalgo, CTLFLAG_RDTUN, &rss_hashalgo, 0,
    "RSS hash algorithm");

/*
 * Size of the indirection table; at most 128 entries per the RSS spec.  We
 * size it to at least 2 times the number of CPUs by default to allow useful
 * rebalancing.  If not set explicitly with a loader tunable, we tune based
 * on the number of CPUs present.
 *
 * XXXRW: buckets might be better to use for the tunable than bits.
 */
static u_int	rss_bits;
SYSCTL_INT(_net_inet_rss, OID_AUTO, bits, CTLFLAG_RDTUN, &rss_bits, 0,
    "RSS bits");

static u_int	rss_mask;
SYSCTL_INT(_net_inet_rss, OID_AUTO, mask, CTLFLAG_RD, &rss_mask, 0,
    "RSS mask");

static const u_int	rss_maxbits = RSS_MAXBITS;
SYSCTL_INT(_net_inet_rss, OID_AUTO, maxbits, CTLFLAG_RD,
    __DECONST(int *, &rss_maxbits), 0, "RSS maximum bits");

/*
 * RSS's own count of the number of CPUs it could be using for processing.
 * Bounded to 64 by RSS constants.
 */
static u_int	rss_ncpus;
SYSCTL_INT(_net_inet_rss, OID_AUTO, ncpus, CTLFLAG_RD, &rss_ncpus, 0,
    "Number of CPUs available to RSS");

#define	RSS_MAXCPUS	(1 << (RSS_MAXBITS - 1))
static const u_int	rss_maxcpus = RSS_MAXCPUS;
SYSCTL_INT(_net_inet_rss, OID_AUTO, maxcpus, CTLFLAG_RD,
    __DECONST(int *, &rss_maxcpus), 0, "RSS maximum CPUs that can be used");

/*
 * Variable exists just for reporting rss_bits in a user-friendly way.
 */
static u_int	rss_buckets;
SYSCTL_INT(_net_inet_rss, OID_AUTO, buckets, CTLFLAG_RD, &rss_buckets, 0,
    "RSS buckets");

/*
 * Base CPU number; devices will add this to all CPU numbers returned by the
 * RSS indirection table.  Currently unmodifable in FreeBSD.
 */
static const u_int	rss_basecpu;
SYSCTL_INT(_net_inet_rss, OID_AUTO, basecpu, CTLFLAG_RD,
    __DECONST(int *, &rss_basecpu), 0, "RSS base CPU");

/*
 * Print verbose debugging messages.
 * 0 - disable
 * non-zero - enable
 */
int	rss_debug = 0;
SYSCTL_INT(_net_inet_rss, OID_AUTO, debug, CTLFLAG_RWTUN, &rss_debug, 0,
    "RSS debug level");

/*
 * RSS secret key, intended to prevent attacks on load-balancing.  Its
 * effectiveness may be limited by algorithm choice and available entropy
 * during the boot.
 *
 * XXXRW: And that we don't randomize it yet!
 *
 * This is the default Microsoft RSS specification key which is also
 * the Chelsio T5 firmware default key.
 */
static uint8_t rss_key[RSS_KEYSIZE] = {
	0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
	0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
	0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
	0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
	0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
};

/*
 * RSS hash->CPU table, which maps hashed packet headers to particular CPUs.
 * Drivers may supplement this table with a separate CPU<->queue table when
 * programming devices.
 */
struct rss_table_entry {
	uint8_t		rte_cpu;	/* CPU affinity of bucket. */
};
static struct rss_table_entry	rss_table[RSS_TABLE_MAXLEN];

static void
rss_init(__unused void *arg)
{
	u_int i;
	u_int cpuid;

	/*
	 * Validate tunables, coerce to sensible values.
	 */
	switch (rss_hashalgo) {
	case RSS_HASH_TOEPLITZ:
	case RSS_HASH_NAIVE:
		break;

	default:
		RSS_DEBUG("invalid RSS hashalgo %u, coercing to %u\n",
		    rss_hashalgo, RSS_HASH_TOEPLITZ);
		rss_hashalgo = RSS_HASH_TOEPLITZ;
	}

	/*
	 * Count available CPUs.
	 *
	 * XXXRW: Note incorrect assumptions regarding contiguity of this set
	 * elsewhere.
	 */
	rss_ncpus = 0;
	for (i = 0; i <= mp_maxid; i++) {
		if (CPU_ABSENT(i))
			continue;
		rss_ncpus++;
	}
	if (rss_ncpus > RSS_MAXCPUS)
		rss_ncpus = RSS_MAXCPUS;

	/*
	 * Tune RSS table entries to be no less than 2x the number of CPUs
	 * -- unless we're running uniprocessor, in which case there's not
	 * much point in having buckets to rearrange for load-balancing!
	 */
	if (rss_ncpus > 1) {
		if (rss_bits == 0)
			rss_bits = fls(rss_ncpus - 1) + 1;

		/*
		 * Microsoft limits RSS table entries to 128, so apply that
		 * limit to both auto-detected CPU counts and user-configured
		 * ones.
		 */
		if (rss_bits == 0 || rss_bits > RSS_MAXBITS) {
			RSS_DEBUG("RSS bits %u not valid, coercing to %u\n",
			    rss_bits, RSS_MAXBITS);
			rss_bits = RSS_MAXBITS;
		}

		/*
		 * Figure out how many buckets to use; warn if less than the
		 * number of configured CPUs, although this is not a fatal
		 * problem.
		 */
		rss_buckets = (1 << rss_bits);
		if (rss_buckets < rss_ncpus)
			RSS_DEBUG("WARNING: rss_buckets (%u) less than "
			    "rss_ncpus (%u)\n", rss_buckets, rss_ncpus);
		rss_mask = rss_buckets - 1;
	} else {
		rss_bits = 0;
		rss_buckets = 1;
		rss_mask = 0;
	}

	/*
	 * Set up initial CPU assignments: round-robin by default.
	 */
	cpuid = CPU_FIRST();
	for (i = 0; i < rss_buckets; i++) {
		rss_table[i].rte_cpu = cpuid;
		cpuid = CPU_NEXT(cpuid);
	}

	/*
	 * Randomize rrs_key.
	 *
	 * XXXRW: Not yet.  If nothing else, will require an rss_isbadkey()
	 * loop to check for "bad" RSS keys.
	 */
}
SYSINIT(rss_init, SI_SUB_SOFTINTR, SI_ORDER_SECOND, rss_init, NULL);

static uint32_t
rss_naive_hash(u_int keylen, const uint8_t *key, u_int datalen,
    const uint8_t *data)
{
	uint32_t v;
	u_int i;

	v = 0;
	for (i = 0; i < keylen; i++)
		v += key[i];
	for (i = 0; i < datalen; i++)
		v += data[i];
	return (v);
}

uint32_t
rss_hash(u_int datalen, const uint8_t *data)
{
 
	switch (rss_hashalgo) {
	case RSS_HASH_TOEPLITZ:
		return (toeplitz_hash(sizeof(rss_key), rss_key, datalen,
		    data));

	case RSS_HASH_NAIVE:
		return (rss_naive_hash(sizeof(rss_key), rss_key, datalen,
		    data));

	default:
		panic("%s: unsupported/unknown hashalgo %d", __func__,
		    rss_hashalgo);
	}
}

/*
 * Query the number of RSS bits in use.
 */
u_int
rss_getbits(void)
{

	return (rss_bits);
}

/*
 * Query the RSS bucket associated with an RSS hash.
 */
u_int
rss_getbucket(u_int hash)
{

	return (hash & rss_mask);
}

/*
 * Query the RSS layer bucket associated with the given
 * entry in the RSS hash space.
 *
 * The RSS indirection table is 0 .. rss_buckets-1,
 * covering the low 'rss_bits' of the total 128 slot
 * RSS indirection table.  So just mask off rss_bits and
 * return that.
 *
 * NIC drivers can then iterate over the 128 slot RSS
 * indirection table and fetch which RSS bucket to
 * map it to.  This will typically be a CPU queue
 */
u_int
rss_get_indirection_to_bucket(u_int index)
{

	return (index & rss_mask);
}

/*
 * Query the RSS CPU associated with an RSS bucket.
 */
u_int
rss_getcpu(u_int bucket)
{

	return (rss_table[bucket].rte_cpu);
}

/*
 * netisr CPU affinity lookup given just the hash and hashtype.
 */
u_int
rss_hash2cpuid(uint32_t hash_val, uint32_t hash_type)
{

	switch (hash_type) {
	case M_HASHTYPE_RSS_IPV4:
	case M_HASHTYPE_RSS_TCP_IPV4:
	case M_HASHTYPE_RSS_UDP_IPV4:
	case M_HASHTYPE_RSS_IPV6:
	case M_HASHTYPE_RSS_TCP_IPV6:
	case M_HASHTYPE_RSS_UDP_IPV6:
		return (rss_getcpu(rss_getbucket(hash_val)));
	default:
		return (NETISR_CPUID_NONE);
	}
}

/*
 * Query the RSS bucket associated with the given hash value and
 * type.
 */
int
rss_hash2bucket(uint32_t hash_val, uint32_t hash_type, uint32_t *bucket_id)
{

	switch (hash_type) {
	case M_HASHTYPE_RSS_IPV4:
	case M_HASHTYPE_RSS_TCP_IPV4:
	case M_HASHTYPE_RSS_UDP_IPV4:
	case M_HASHTYPE_RSS_IPV6:
	case M_HASHTYPE_RSS_TCP_IPV6:
	case M_HASHTYPE_RSS_UDP_IPV6:
		*bucket_id = rss_getbucket(hash_val);
		return (0);
	default:
		return (-1);
	}
}

/*
 * netisr CPU affinity lookup routine for use by protocols.
 */
struct mbuf *
rss_m2cpuid(struct mbuf *m, uintptr_t source, u_int *cpuid)
{

	M_ASSERTPKTHDR(m);
	*cpuid = rss_hash2cpuid(m->m_pkthdr.flowid, M_HASHTYPE_GET(m));
	return (m);
}

int
rss_m2bucket(struct mbuf *m, uint32_t *bucket_id)
{

	M_ASSERTPKTHDR(m);

	return(rss_hash2bucket(m->m_pkthdr.flowid, M_HASHTYPE_GET(m),
	    bucket_id));
}

/*
 * Query the RSS hash algorithm.
 */
u_int
rss_gethashalgo(void)
{

	return (rss_hashalgo);
}

/*
 * Query the current RSS key; likely to be used by device drivers when
 * configuring hardware RSS.  Caller must pass an array of size RSS_KEYSIZE.
 *
 * XXXRW: Perhaps we should do the accept-a-length-and-truncate thing?
 */
void
rss_getkey(uint8_t *key)
{

	bcopy(rss_key, key, sizeof(rss_key));
}

/*
 * Query the number of buckets; this may be used by both network device
 * drivers, which will need to populate hardware shadows of the software
 * indirection table, and the network stack itself (such as when deciding how
 * many connection groups to allocate).
 */
u_int
rss_getnumbuckets(void)
{

	return (rss_buckets);
}

/*
 * Query the number of CPUs in use by RSS; may be useful to device drivers
 * trying to figure out how to map a larger number of CPUs into a smaller
 * number of receive queues.
 */
u_int
rss_getnumcpus(void)
{

	return (rss_ncpus);
}

/*
 * Return the supported RSS hash configuration.
 *
 * NICs should query this to determine what to configure in their redirection
 * matching table.
 */
inline u_int
rss_gethashconfig(void)
{

	/* Return 4-tuple for TCP; 2-tuple for others */
	/*
	 * UDP may fragment more often than TCP and thus we'll end up with
	 * NICs returning 2-tuple fragments.
	 * udp_init() and udplite_init() both currently initialise things
	 * as 2-tuple.
	 * So for now disable UDP 4-tuple hashing until all of the other
	 * pieces are in place.
	 */
	return (
	    RSS_HASHTYPE_RSS_IPV4
	|    RSS_HASHTYPE_RSS_TCP_IPV4
	|    RSS_HASHTYPE_RSS_IPV6
	|    RSS_HASHTYPE_RSS_TCP_IPV6
	|    RSS_HASHTYPE_RSS_IPV6_EX
	|    RSS_HASHTYPE_RSS_TCP_IPV6_EX
#if 0
	|    RSS_HASHTYPE_RSS_UDP_IPV4
	|    RSS_HASHTYPE_RSS_UDP_IPV6
	|    RSS_HASHTYPE_RSS_UDP_IPV6_EX
#endif
	);
}

/*
 * XXXRW: Confirm that sysctl -a won't dump this keying material, don't want
 * it appearing in debugging output unnecessarily.
 */
static int
sysctl_rss_key(SYSCTL_HANDLER_ARGS)
{
	uint8_t temp_rss_key[RSS_KEYSIZE];
	int error;

	error = priv_check(req->td, PRIV_NETINET_HASHKEY);
	if (error)
		return (error);

	bcopy(rss_key, temp_rss_key, sizeof(temp_rss_key));
	error = sysctl_handle_opaque(oidp, temp_rss_key,
	    sizeof(temp_rss_key), req);
	if (error)
		return (error);
	if (req->newptr != NULL) {
		/* XXXRW: Not yet. */
		return (EINVAL);
	}
	return (0);
}
SYSCTL_PROC(_net_inet_rss, OID_AUTO, key,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0, sysctl_rss_key,
    "", "RSS keying material");

static int
sysctl_rss_bucket_mapping(SYSCTL_HANDLER_ARGS)
{
	struct sbuf *sb;
	int error;
	int i;

	error = 0;
	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);
	for (i = 0; i < rss_buckets; i++) {
		sbuf_printf(sb, "%s%d:%d", i == 0 ? "" : " ",
		    i,
		    rss_getcpu(i));
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);

	return (error);
}
SYSCTL_PROC(_net_inet_rss, OID_AUTO, bucket_mapping,
    CTLTYPE_STRING | CTLFLAG_RD, NULL, 0,
    sysctl_rss_bucket_mapping, "", "RSS bucket -> CPU mapping");
