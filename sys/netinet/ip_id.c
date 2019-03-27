/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Michael J. Silbersack.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/*
 * IP ID generation is a fascinating topic.
 *
 * In order to avoid ID collisions during packet reassembly, common sense
 * dictates that the period between reuse of IDs be as large as possible.
 * This leads to the classic implementation of a system-wide counter, thereby
 * ensuring that IDs repeat only once every 2^16 packets.
 *
 * Subsequent security researchers have pointed out that using a global
 * counter makes ID values predictable.  This predictability allows traffic
 * analysis, idle scanning, and even packet injection in specific cases.
 * These results suggest that IP IDs should be as random as possible.
 *
 * The "searchable queues" algorithm used in this IP ID implementation was
 * proposed by Amit Klein.  It is a compromise between the above two
 * viewpoints that has provable behavior that can be tuned to the user's
 * requirements.
 *
 * The basic concept is that we supplement a standard random number generator
 * with a queue of the last L IDs that we have handed out to ensure that all
 * IDs have a period of at least L.
 *
 * To efficiently implement this idea, we keep two data structures: a
 * circular array of IDs of size L and a bitstring of 65536 bits.
 *
 * To start, we ask the RNG for a new ID.  A quick index into the bitstring
 * is used to determine if this is a recently used value.  The process is
 * repeated until a value is returned that is not in the bitstring.
 *
 * Having found a usable ID, we remove the ID stored at the current position
 * in the queue from the bitstring and replace it with our new ID.  Our new
 * ID is then added to the bitstring and the queue pointer is incremented.
 *
 * The lower limit of 512 was chosen because there doesn't seem to be much
 * point to having a smaller value.  The upper limit of 32768 was chosen for
 * two reasons.  First, every step above 32768 decreases the entropy.  Taken
 * to an extreme, 65533 would offer 1 bit of entropy.  Second, the number of
 * attempts it takes the algorithm to find an unused ID drastically
 * increases, killing performance.  The default value of 8192 was chosen
 * because it provides a good tradeoff between randomness and non-repetition.
 *
 * With L=8192, the queue will use 16K of memory.  The bitstring always
 * uses 8K of memory.  No memory is allocated until the use of random ids is
 * enabled.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/random.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/bitstring.h>

#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

/*
 * By default we generate IP ID only for non-atomic datagrams, as
 * suggested by RFC6864.  We use per-CPU counter for that, or if
 * user wants to, we can turn on random ID generation.
 */
VNET_DEFINE_STATIC(int, ip_rfc6864) = 1;
VNET_DEFINE_STATIC(int, ip_do_randomid) = 0;
#define	V_ip_rfc6864		VNET(ip_rfc6864)
#define	V_ip_do_randomid	VNET(ip_do_randomid)

/*
 * Random ID state engine.
 */
static MALLOC_DEFINE(M_IPID, "ipid", "randomized ip id state");
VNET_DEFINE_STATIC(uint16_t *, id_array);
VNET_DEFINE_STATIC(bitstr_t *, id_bits);
VNET_DEFINE_STATIC(int, array_ptr);
VNET_DEFINE_STATIC(int, array_size);
VNET_DEFINE_STATIC(int, random_id_collisions);
VNET_DEFINE_STATIC(int, random_id_total);
VNET_DEFINE_STATIC(struct mtx, ip_id_mtx);
#define	V_id_array	VNET(id_array)
#define	V_id_bits	VNET(id_bits)
#define	V_array_ptr	VNET(array_ptr)
#define	V_array_size	VNET(array_size)
#define	V_random_id_collisions	VNET(random_id_collisions)
#define	V_random_id_total	VNET(random_id_total)
#define	V_ip_id_mtx	VNET(ip_id_mtx)

/*
 * Non-random ID state engine is simply a per-cpu counter.
 */
VNET_DEFINE_STATIC(counter_u64_t, ip_id);
#define	V_ip_id		VNET(ip_id)

static int	sysctl_ip_randomid(SYSCTL_HANDLER_ARGS);
static int	sysctl_ip_id_change(SYSCTL_HANDLER_ARGS);
static void	ip_initid(int);
static uint16_t ip_randomid(void);
static void	ipid_sysinit(void);
static void	ipid_sysuninit(void);

SYSCTL_DECL(_net_inet_ip);
SYSCTL_PROC(_net_inet_ip, OID_AUTO, random_id,
    CTLTYPE_INT | CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ip_do_randomid), 0, sysctl_ip_randomid, "IU",
    "Assign random ip_id values");
SYSCTL_INT(_net_inet_ip, OID_AUTO, rfc6864, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ip_rfc6864), 0,
    "Use constant IP ID for atomic datagrams");
SYSCTL_PROC(_net_inet_ip, OID_AUTO, random_id_period,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_VNET,
    &VNET_NAME(array_size), 0, sysctl_ip_id_change, "IU", "IP ID Array size");
SYSCTL_INT(_net_inet_ip, OID_AUTO, random_id_collisions,
    CTLFLAG_RD | CTLFLAG_VNET,
    &VNET_NAME(random_id_collisions), 0, "Count of IP ID collisions");
SYSCTL_INT(_net_inet_ip, OID_AUTO, random_id_total, CTLFLAG_RD | CTLFLAG_VNET,
    &VNET_NAME(random_id_total), 0, "Count of IP IDs created");

static int
sysctl_ip_randomid(SYSCTL_HANDLER_ARGS)
{
	int error, new;

	new = V_ip_do_randomid;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (new != 0 && new != 1)
		return (EINVAL);
	if (new == V_ip_do_randomid)
		return (0);
	if (new == 1 && V_ip_do_randomid == 0)
		ip_initid(8192);
	/* We don't free memory when turning random ID off, due to race. */
	V_ip_do_randomid = new;
	return (0);
}

static int
sysctl_ip_id_change(SYSCTL_HANDLER_ARGS)
{
	int error, new;

	new = V_array_size;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if (new >= 512 && new <= 32768)
			ip_initid(new);
		else
			error = EINVAL;
	}
	return (error);
}

static void
ip_initid(int new_size)
{
	uint16_t *new_array;
	bitstr_t *new_bits;

	new_array = malloc(new_size * sizeof(uint16_t), M_IPID,
	    M_WAITOK | M_ZERO);
	new_bits = malloc(bitstr_size(65536), M_IPID, M_WAITOK | M_ZERO);

	mtx_lock(&V_ip_id_mtx);
	if (V_id_array != NULL) {
		free(V_id_array, M_IPID);
		free(V_id_bits, M_IPID);
	}
	V_id_array = new_array;
	V_id_bits = new_bits;
	V_array_size = new_size;
	V_array_ptr = 0;
	V_random_id_collisions = 0;
	V_random_id_total = 0;
	mtx_unlock(&V_ip_id_mtx);
}

static uint16_t
ip_randomid(void)
{
	uint16_t new_id;

	mtx_lock(&V_ip_id_mtx);
	/*
	 * To avoid a conflict with the zeros that the array is initially
	 * filled with, we never hand out an id of zero.
	 */
	new_id = 0;
	do {
		if (new_id != 0)
			V_random_id_collisions++;
		arc4rand(&new_id, sizeof(new_id), 0);
	} while (bit_test(V_id_bits, new_id) || new_id == 0);
	bit_clear(V_id_bits, V_id_array[V_array_ptr]);
	bit_set(V_id_bits, new_id);
	V_id_array[V_array_ptr] = new_id;
	V_array_ptr++;
	if (V_array_ptr == V_array_size)
		V_array_ptr = 0;
	V_random_id_total++;
	mtx_unlock(&V_ip_id_mtx);
	return (new_id);
}

void
ip_fillid(struct ip *ip)
{

	/*
	 * Per RFC6864 Section 4
	 *
	 * o  Atomic datagrams: (DF==1) && (MF==0) && (frag_offset==0)
	 * o  Non-atomic datagrams: (DF==0) || (MF==1) || (frag_offset>0)
	 */
	if (V_ip_rfc6864 && (ip->ip_off & htons(IP_DF)) == htons(IP_DF))
		ip->ip_id = 0;
	else if (V_ip_do_randomid)
		ip->ip_id = ip_randomid();
	else {
		counter_u64_add(V_ip_id, 1);
		/*
		 * There are two issues about this trick, to be kept in mind.
		 * 1) We can migrate between counter_u64_add() and next
		 *    line, and grab counter from other CPU, resulting in too
		 *    quick ID reuse. This is tolerable in our particular case,
		 *    since probability of such event is much lower then reuse
		 *    of ID due to legitimate overflow, that at modern Internet
		 *    speeds happens all the time.
		 * 2) We are relying on the fact that counter(9) is based on
		 *    UMA_ZONE_PCPU uma(9) zone. We also take only last
		 *    sixteen bits of a counter, so we don't care about the
		 *    fact that machines with 32-bit word update their counters
		 *    not atomically.
		 */
		ip->ip_id = htons((*(uint64_t *)zpcpu_get(V_ip_id)) & 0xffff);
	}
}

static void
ipid_sysinit(void)
{
	int i;

	mtx_init(&V_ip_id_mtx, "ip_id_mtx", NULL, MTX_DEF);
	V_ip_id = counter_u64_alloc(M_WAITOK);
	
	CPU_FOREACH(i)
		arc4rand(zpcpu_get_cpu(V_ip_id, i), sizeof(uint64_t), 0);
}
VNET_SYSINIT(ip_id, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY, ipid_sysinit, NULL);

static void
ipid_sysuninit(void)
{

	if (V_id_array != NULL) {
		free(V_id_array, M_IPID);
		free(V_id_bits, M_IPID);
	}
	counter_u64_free(V_ip_id);
	mtx_destroy(&V_ip_id_mtx);
}
VNET_SYSUNINIT(ip_id, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD, ipid_sysuninit, NULL);
