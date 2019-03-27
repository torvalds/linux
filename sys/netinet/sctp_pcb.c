/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2008, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <netinet/sctp_os.h>
#include <sys/proc.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctputil.h>
#include <netinet/sctp.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_bsd_addr.h>
#include <netinet/sctp_dtrace_define.h>
#if defined(INET) || defined(INET6)
#include <netinet/udp.h>
#endif
#ifdef INET6
#include <netinet6/ip6_var.h>
#endif
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/unistd.h>


VNET_DEFINE(struct sctp_base_info, system_base_info);

/* FIX: we don't handle multiple link local scopes */
/* "scopeless" replacement IN6_ARE_ADDR_EQUAL */
#ifdef INET6
int
SCTP6_ARE_ADDR_EQUAL(struct sockaddr_in6 *a, struct sockaddr_in6 *b)
{
	struct sockaddr_in6 tmp_a, tmp_b;

	memcpy(&tmp_a, a, sizeof(struct sockaddr_in6));
	if (sa6_embedscope(&tmp_a, MODULE_GLOBAL(ip6_use_defzone)) != 0) {
		return (0);
	}
	memcpy(&tmp_b, b, sizeof(struct sockaddr_in6));
	if (sa6_embedscope(&tmp_b, MODULE_GLOBAL(ip6_use_defzone)) != 0) {
		return (0);
	}
	return (IN6_ARE_ADDR_EQUAL(&tmp_a.sin6_addr, &tmp_b.sin6_addr));
}
#endif

void
sctp_fill_pcbinfo(struct sctp_pcbinfo *spcb)
{
	/*
	 * We really don't need to lock this, but I will just because it
	 * does not hurt.
	 */
	SCTP_INP_INFO_RLOCK();
	spcb->ep_count = SCTP_BASE_INFO(ipi_count_ep);
	spcb->asoc_count = SCTP_BASE_INFO(ipi_count_asoc);
	spcb->laddr_count = SCTP_BASE_INFO(ipi_count_laddr);
	spcb->raddr_count = SCTP_BASE_INFO(ipi_count_raddr);
	spcb->chk_count = SCTP_BASE_INFO(ipi_count_chunk);
	spcb->readq_count = SCTP_BASE_INFO(ipi_count_readq);
	spcb->stream_oque = SCTP_BASE_INFO(ipi_count_strmoq);
	spcb->free_chunks = SCTP_BASE_INFO(ipi_free_chunks);
	SCTP_INP_INFO_RUNLOCK();
}

/*-
 * Addresses are added to VRF's (Virtual Router's). For BSD we
 * have only the default VRF 0. We maintain a hash list of
 * VRF's. Each VRF has its own list of sctp_ifn's. Each of
 * these has a list of addresses. When we add a new address
 * to a VRF we lookup the ifn/ifn_index, if the ifn does
 * not exist we create it and add it to the list of IFN's
 * within the VRF. Once we have the sctp_ifn, we add the
 * address to the list. So we look something like:
 *
 * hash-vrf-table
 *   vrf-> ifn-> ifn -> ifn
 *   vrf    |
 *    ...   +--ifa-> ifa -> ifa
 *   vrf
 *
 * We keep these separate lists since the SCTP subsystem will
 * point to these from its source address selection nets structure.
 * When an address is deleted it does not happen right away on
 * the SCTP side, it gets scheduled. What we do when a
 * delete happens is immediately remove the address from
 * the master list and decrement the refcount. As our
 * addip iterator works through and frees the src address
 * selection pointing to the sctp_ifa, eventually the refcount
 * will reach 0 and we will delete it. Note that it is assumed
 * that any locking on system level ifn/ifa is done at the
 * caller of these functions and these routines will only
 * lock the SCTP structures as they add or delete things.
 *
 * Other notes on VRF concepts.
 *  - An endpoint can be in multiple VRF's
 *  - An association lives within a VRF and only one VRF.
 *  - Any incoming packet we can deduce the VRF for by
 *    looking at the mbuf/pak inbound (for BSD its VRF=0 :D)
 *  - Any downward send call or connect call must supply the
 *    VRF via ancillary data or via some sort of set default
 *    VRF socket option call (again for BSD no brainer since
 *    the VRF is always 0).
 *  - An endpoint may add multiple VRF's to it.
 *  - Listening sockets can accept associations in any
 *    of the VRF's they are in but the assoc will end up
 *    in only one VRF (gotten from the packet or connect/send).
 *
 */

struct sctp_vrf *
sctp_allocate_vrf(int vrf_id)
{
	struct sctp_vrf *vrf = NULL;
	struct sctp_vrflist *bucket;

	/* First allocate the VRF structure */
	vrf = sctp_find_vrf(vrf_id);
	if (vrf) {
		/* Already allocated */
		return (vrf);
	}
	SCTP_MALLOC(vrf, struct sctp_vrf *, sizeof(struct sctp_vrf),
	    SCTP_M_VRF);
	if (vrf == NULL) {
		/* No memory */
#ifdef INVARIANTS
		panic("No memory for VRF:%d", vrf_id);
#endif
		return (NULL);
	}
	/* setup the VRF */
	memset(vrf, 0, sizeof(struct sctp_vrf));
	vrf->vrf_id = vrf_id;
	LIST_INIT(&vrf->ifnlist);
	vrf->total_ifa_count = 0;
	vrf->refcount = 0;
	/* now also setup table ids */
	SCTP_INIT_VRF_TABLEID(vrf);
	/* Init the HASH of addresses */
	vrf->vrf_addr_hash = SCTP_HASH_INIT(SCTP_VRF_ADDR_HASH_SIZE,
	    &vrf->vrf_addr_hashmark);
	if (vrf->vrf_addr_hash == NULL) {
		/* No memory */
#ifdef INVARIANTS
		panic("No memory for VRF:%d", vrf_id);
#endif
		SCTP_FREE(vrf, SCTP_M_VRF);
		return (NULL);
	}

	/* Add it to the hash table */
	bucket = &SCTP_BASE_INFO(sctp_vrfhash)[(vrf_id & SCTP_BASE_INFO(hashvrfmark))];
	LIST_INSERT_HEAD(bucket, vrf, next_vrf);
	atomic_add_int(&SCTP_BASE_INFO(ipi_count_vrfs), 1);
	return (vrf);
}


struct sctp_ifn *
sctp_find_ifn(void *ifn, uint32_t ifn_index)
{
	struct sctp_ifn *sctp_ifnp;
	struct sctp_ifnlist *hash_ifn_head;

	/*
	 * We assume the lock is held for the addresses if that's wrong
	 * problems could occur :-)
	 */
	hash_ifn_head = &SCTP_BASE_INFO(vrf_ifn_hash)[(ifn_index & SCTP_BASE_INFO(vrf_ifn_hashmark))];
	LIST_FOREACH(sctp_ifnp, hash_ifn_head, next_bucket) {
		if (sctp_ifnp->ifn_index == ifn_index) {
			return (sctp_ifnp);
		}
		if (sctp_ifnp->ifn_p && ifn && (sctp_ifnp->ifn_p == ifn)) {
			return (sctp_ifnp);
		}
	}
	return (NULL);
}


struct sctp_vrf *
sctp_find_vrf(uint32_t vrf_id)
{
	struct sctp_vrflist *bucket;
	struct sctp_vrf *liste;

	bucket = &SCTP_BASE_INFO(sctp_vrfhash)[(vrf_id & SCTP_BASE_INFO(hashvrfmark))];
	LIST_FOREACH(liste, bucket, next_vrf) {
		if (vrf_id == liste->vrf_id) {
			return (liste);
		}
	}
	return (NULL);
}


void
sctp_free_vrf(struct sctp_vrf *vrf)
{
	if (SCTP_DECREMENT_AND_CHECK_REFCOUNT(&vrf->refcount)) {
		if (vrf->vrf_addr_hash) {
			SCTP_HASH_FREE(vrf->vrf_addr_hash, vrf->vrf_addr_hashmark);
			vrf->vrf_addr_hash = NULL;
		}
		/* We zero'd the count */
		LIST_REMOVE(vrf, next_vrf);
		SCTP_FREE(vrf, SCTP_M_VRF);
		atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_vrfs), 1);
	}
}


void
sctp_free_ifn(struct sctp_ifn *sctp_ifnp)
{
	if (SCTP_DECREMENT_AND_CHECK_REFCOUNT(&sctp_ifnp->refcount)) {
		/* We zero'd the count */
		if (sctp_ifnp->vrf) {
			sctp_free_vrf(sctp_ifnp->vrf);
		}
		SCTP_FREE(sctp_ifnp, SCTP_M_IFN);
		atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_ifns), 1);
	}
}


void
sctp_update_ifn_mtu(uint32_t ifn_index, uint32_t mtu)
{
	struct sctp_ifn *sctp_ifnp;

	sctp_ifnp = sctp_find_ifn((void *)NULL, ifn_index);
	if (sctp_ifnp != NULL) {
		sctp_ifnp->ifn_mtu = mtu;
	}
}


void
sctp_free_ifa(struct sctp_ifa *sctp_ifap)
{
	if (SCTP_DECREMENT_AND_CHECK_REFCOUNT(&sctp_ifap->refcount)) {
		/* We zero'd the count */
		if (sctp_ifap->ifn_p) {
			sctp_free_ifn(sctp_ifap->ifn_p);
		}
		SCTP_FREE(sctp_ifap, SCTP_M_IFA);
		atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_ifas), 1);
	}
}


static void
sctp_delete_ifn(struct sctp_ifn *sctp_ifnp, int hold_addr_lock)
{
	struct sctp_ifn *found;

	found = sctp_find_ifn(sctp_ifnp->ifn_p, sctp_ifnp->ifn_index);
	if (found == NULL) {
		/* Not in the list.. sorry */
		return;
	}
	if (hold_addr_lock == 0)
		SCTP_IPI_ADDR_WLOCK();
	LIST_REMOVE(sctp_ifnp, next_bucket);
	LIST_REMOVE(sctp_ifnp, next_ifn);
	SCTP_DEREGISTER_INTERFACE(sctp_ifnp->ifn_index,
	    sctp_ifnp->registered_af);
	if (hold_addr_lock == 0)
		SCTP_IPI_ADDR_WUNLOCK();
	/* Take away the reference, and possibly free it */
	sctp_free_ifn(sctp_ifnp);
}


void
sctp_mark_ifa_addr_down(uint32_t vrf_id, struct sockaddr *addr,
    const char *if_name, uint32_t ifn_index)
{
	struct sctp_vrf *vrf;
	struct sctp_ifa *sctp_ifap;

	SCTP_IPI_ADDR_RLOCK();
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		SCTPDBG(SCTP_DEBUG_PCB4, "Can't find vrf_id 0x%x\n", vrf_id);
		goto out;

	}
	sctp_ifap = sctp_find_ifa_by_addr(addr, vrf->vrf_id, SCTP_ADDR_LOCKED);
	if (sctp_ifap == NULL) {
		SCTPDBG(SCTP_DEBUG_PCB4, "Can't find sctp_ifap for address\n");
		goto out;
	}
	if (sctp_ifap->ifn_p == NULL) {
		SCTPDBG(SCTP_DEBUG_PCB4, "IFA has no IFN - can't mark unusable\n");
		goto out;
	}
	if (if_name) {
		if (strncmp(if_name, sctp_ifap->ifn_p->ifn_name, SCTP_IFNAMSIZ) != 0) {
			SCTPDBG(SCTP_DEBUG_PCB4, "IFN %s of IFA not the same as %s\n",
			    sctp_ifap->ifn_p->ifn_name, if_name);
			goto out;
		}
	} else {
		if (sctp_ifap->ifn_p->ifn_index != ifn_index) {
			SCTPDBG(SCTP_DEBUG_PCB4, "IFA owned by ifn_index:%d down command for ifn_index:%d - ignored\n",
			    sctp_ifap->ifn_p->ifn_index, ifn_index);
			goto out;
		}
	}

	sctp_ifap->localifa_flags &= (~SCTP_ADDR_VALID);
	sctp_ifap->localifa_flags |= SCTP_ADDR_IFA_UNUSEABLE;
out:
	SCTP_IPI_ADDR_RUNLOCK();
}


void
sctp_mark_ifa_addr_up(uint32_t vrf_id, struct sockaddr *addr,
    const char *if_name, uint32_t ifn_index)
{
	struct sctp_vrf *vrf;
	struct sctp_ifa *sctp_ifap;

	SCTP_IPI_ADDR_RLOCK();
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		SCTPDBG(SCTP_DEBUG_PCB4, "Can't find vrf_id 0x%x\n", vrf_id);
		goto out;

	}
	sctp_ifap = sctp_find_ifa_by_addr(addr, vrf->vrf_id, SCTP_ADDR_LOCKED);
	if (sctp_ifap == NULL) {
		SCTPDBG(SCTP_DEBUG_PCB4, "Can't find sctp_ifap for address\n");
		goto out;
	}
	if (sctp_ifap->ifn_p == NULL) {
		SCTPDBG(SCTP_DEBUG_PCB4, "IFA has no IFN - can't mark unusable\n");
		goto out;
	}
	if (if_name) {
		if (strncmp(if_name, sctp_ifap->ifn_p->ifn_name, SCTP_IFNAMSIZ) != 0) {
			SCTPDBG(SCTP_DEBUG_PCB4, "IFN %s of IFA not the same as %s\n",
			    sctp_ifap->ifn_p->ifn_name, if_name);
			goto out;
		}
	} else {
		if (sctp_ifap->ifn_p->ifn_index != ifn_index) {
			SCTPDBG(SCTP_DEBUG_PCB4, "IFA owned by ifn_index:%d down command for ifn_index:%d - ignored\n",
			    sctp_ifap->ifn_p->ifn_index, ifn_index);
			goto out;
		}
	}

	sctp_ifap->localifa_flags &= (~SCTP_ADDR_IFA_UNUSEABLE);
	sctp_ifap->localifa_flags |= SCTP_ADDR_VALID;
out:
	SCTP_IPI_ADDR_RUNLOCK();
}


/*-
 * Add an ifa to an ifn.
 * Register the interface as necessary.
 * NOTE: ADDR write lock MUST be held.
 */
static void
sctp_add_ifa_to_ifn(struct sctp_ifn *sctp_ifnp, struct sctp_ifa *sctp_ifap)
{
	int ifa_af;

	LIST_INSERT_HEAD(&sctp_ifnp->ifalist, sctp_ifap, next_ifa);
	sctp_ifap->ifn_p = sctp_ifnp;
	atomic_add_int(&sctp_ifap->ifn_p->refcount, 1);
	/* update address counts */
	sctp_ifnp->ifa_count++;
	ifa_af = sctp_ifap->address.sa.sa_family;
	switch (ifa_af) {
#ifdef INET
	case AF_INET:
		sctp_ifnp->num_v4++;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		sctp_ifnp->num_v6++;
		break;
#endif
	default:
		break;
	}
	if (sctp_ifnp->ifa_count == 1) {
		/* register the new interface */
		SCTP_REGISTER_INTERFACE(sctp_ifnp->ifn_index, ifa_af);
		sctp_ifnp->registered_af = ifa_af;
	}
}


/*-
 * Remove an ifa from its ifn.
 * If no more addresses exist, remove the ifn too. Otherwise, re-register
 * the interface based on the remaining address families left.
 * NOTE: ADDR write lock MUST be held.
 */
static void
sctp_remove_ifa_from_ifn(struct sctp_ifa *sctp_ifap)
{
	LIST_REMOVE(sctp_ifap, next_ifa);
	if (sctp_ifap->ifn_p) {
		/* update address counts */
		sctp_ifap->ifn_p->ifa_count--;
		switch (sctp_ifap->address.sa.sa_family) {
#ifdef INET
		case AF_INET:
			sctp_ifap->ifn_p->num_v4--;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			sctp_ifap->ifn_p->num_v6--;
			break;
#endif
		default:
			break;
		}

		if (LIST_EMPTY(&sctp_ifap->ifn_p->ifalist)) {
			/* remove the ifn, possibly freeing it */
			sctp_delete_ifn(sctp_ifap->ifn_p, SCTP_ADDR_LOCKED);
		} else {
			/* re-register address family type, if needed */
			if ((sctp_ifap->ifn_p->num_v6 == 0) &&
			    (sctp_ifap->ifn_p->registered_af == AF_INET6)) {
				SCTP_DEREGISTER_INTERFACE(sctp_ifap->ifn_p->ifn_index, AF_INET6);
				SCTP_REGISTER_INTERFACE(sctp_ifap->ifn_p->ifn_index, AF_INET);
				sctp_ifap->ifn_p->registered_af = AF_INET;
			} else if ((sctp_ifap->ifn_p->num_v4 == 0) &&
			    (sctp_ifap->ifn_p->registered_af == AF_INET)) {
				SCTP_DEREGISTER_INTERFACE(sctp_ifap->ifn_p->ifn_index, AF_INET);
				SCTP_REGISTER_INTERFACE(sctp_ifap->ifn_p->ifn_index, AF_INET6);
				sctp_ifap->ifn_p->registered_af = AF_INET6;
			}
			/* free the ifn refcount */
			sctp_free_ifn(sctp_ifap->ifn_p);
		}
		sctp_ifap->ifn_p = NULL;
	}
}


struct sctp_ifa *
sctp_add_addr_to_vrf(uint32_t vrf_id, void *ifn, uint32_t ifn_index,
    uint32_t ifn_type, const char *if_name, void *ifa,
    struct sockaddr *addr, uint32_t ifa_flags,
    int dynamic_add)
{
	struct sctp_vrf *vrf;
	struct sctp_ifn *sctp_ifnp = NULL;
	struct sctp_ifa *sctp_ifap = NULL;
	struct sctp_ifalist *hash_addr_head;
	struct sctp_ifnlist *hash_ifn_head;
	uint32_t hash_of_addr;
	int new_ifn_af = 0;

#ifdef SCTP_DEBUG
	SCTPDBG(SCTP_DEBUG_PCB4, "vrf_id 0x%x: adding address: ", vrf_id);
	SCTPDBG_ADDR(SCTP_DEBUG_PCB4, addr);
#endif
	SCTP_IPI_ADDR_WLOCK();
	sctp_ifnp = sctp_find_ifn(ifn, ifn_index);
	if (sctp_ifnp) {
		vrf = sctp_ifnp->vrf;
	} else {
		vrf = sctp_find_vrf(vrf_id);
		if (vrf == NULL) {
			vrf = sctp_allocate_vrf(vrf_id);
			if (vrf == NULL) {
				SCTP_IPI_ADDR_WUNLOCK();
				return (NULL);
			}
		}
	}
	if (sctp_ifnp == NULL) {
		/*
		 * build one and add it, can't hold lock until after malloc
		 * done though.
		 */
		SCTP_IPI_ADDR_WUNLOCK();
		SCTP_MALLOC(sctp_ifnp, struct sctp_ifn *,
		    sizeof(struct sctp_ifn), SCTP_M_IFN);
		if (sctp_ifnp == NULL) {
#ifdef INVARIANTS
			panic("No memory for IFN");
#endif
			return (NULL);
		}
		memset(sctp_ifnp, 0, sizeof(struct sctp_ifn));
		sctp_ifnp->ifn_index = ifn_index;
		sctp_ifnp->ifn_p = ifn;
		sctp_ifnp->ifn_type = ifn_type;
		sctp_ifnp->refcount = 0;
		sctp_ifnp->vrf = vrf;
		atomic_add_int(&vrf->refcount, 1);
		sctp_ifnp->ifn_mtu = SCTP_GATHER_MTU_FROM_IFN_INFO(ifn, ifn_index, addr->sa_family);
		if (if_name != NULL) {
			snprintf(sctp_ifnp->ifn_name, SCTP_IFNAMSIZ, "%s", if_name);
		} else {
			snprintf(sctp_ifnp->ifn_name, SCTP_IFNAMSIZ, "%s", "unknown");
		}
		hash_ifn_head = &SCTP_BASE_INFO(vrf_ifn_hash)[(ifn_index & SCTP_BASE_INFO(vrf_ifn_hashmark))];
		LIST_INIT(&sctp_ifnp->ifalist);
		SCTP_IPI_ADDR_WLOCK();
		LIST_INSERT_HEAD(hash_ifn_head, sctp_ifnp, next_bucket);
		LIST_INSERT_HEAD(&vrf->ifnlist, sctp_ifnp, next_ifn);
		atomic_add_int(&SCTP_BASE_INFO(ipi_count_ifns), 1);
		new_ifn_af = 1;
	}
	sctp_ifap = sctp_find_ifa_by_addr(addr, vrf->vrf_id, SCTP_ADDR_LOCKED);
	if (sctp_ifap) {
		/* Hmm, it already exists? */
		if ((sctp_ifap->ifn_p) &&
		    (sctp_ifap->ifn_p->ifn_index == ifn_index)) {
			SCTPDBG(SCTP_DEBUG_PCB4, "Using existing ifn %s (0x%x) for ifa %p\n",
			    sctp_ifap->ifn_p->ifn_name, ifn_index,
			    (void *)sctp_ifap);
			if (new_ifn_af) {
				/* Remove the created one that we don't want */
				sctp_delete_ifn(sctp_ifnp, SCTP_ADDR_LOCKED);
			}
			if (sctp_ifap->localifa_flags & SCTP_BEING_DELETED) {
				/* easy to solve, just switch back to active */
				SCTPDBG(SCTP_DEBUG_PCB4, "Clearing deleted ifa flag\n");
				sctp_ifap->localifa_flags = SCTP_ADDR_VALID;
				sctp_ifap->ifn_p = sctp_ifnp;
				atomic_add_int(&sctp_ifap->ifn_p->refcount, 1);
			}
	exit_stage_left:
			SCTP_IPI_ADDR_WUNLOCK();
			return (sctp_ifap);
		} else {
			if (sctp_ifap->ifn_p) {
				/*
				 * The last IFN gets the address, remove the
				 * old one
				 */
				SCTPDBG(SCTP_DEBUG_PCB4, "Moving ifa %p from %s (0x%x) to %s (0x%x)\n",
				    (void *)sctp_ifap, sctp_ifap->ifn_p->ifn_name,
				    sctp_ifap->ifn_p->ifn_index, if_name,
				    ifn_index);
				/* remove the address from the old ifn */
				sctp_remove_ifa_from_ifn(sctp_ifap);
				/* move the address over to the new ifn */
				sctp_add_ifa_to_ifn(sctp_ifnp, sctp_ifap);
				goto exit_stage_left;
			} else {
				/* repair ifnp which was NULL ? */
				sctp_ifap->localifa_flags = SCTP_ADDR_VALID;
				SCTPDBG(SCTP_DEBUG_PCB4, "Repairing ifn %p for ifa %p\n",
				    (void *)sctp_ifnp, (void *)sctp_ifap);
				sctp_add_ifa_to_ifn(sctp_ifnp, sctp_ifap);
			}
			goto exit_stage_left;
		}
	}
	SCTP_IPI_ADDR_WUNLOCK();
	SCTP_MALLOC(sctp_ifap, struct sctp_ifa *, sizeof(struct sctp_ifa), SCTP_M_IFA);
	if (sctp_ifap == NULL) {
#ifdef INVARIANTS
		panic("No memory for IFA");
#endif
		return (NULL);
	}
	memset(sctp_ifap, 0, sizeof(struct sctp_ifa));
	sctp_ifap->ifn_p = sctp_ifnp;
	atomic_add_int(&sctp_ifnp->refcount, 1);
	sctp_ifap->vrf_id = vrf_id;
	sctp_ifap->ifa = ifa;
	memcpy(&sctp_ifap->address, addr, addr->sa_len);
	sctp_ifap->localifa_flags = SCTP_ADDR_VALID | SCTP_ADDR_DEFER_USE;
	sctp_ifap->flags = ifa_flags;
	/* Set scope */
	switch (sctp_ifap->address.sa.sa_family) {
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin;

			sin = &sctp_ifap->address.sin;
			if (SCTP_IFN_IS_IFT_LOOP(sctp_ifap->ifn_p) ||
			    (IN4_ISLOOPBACK_ADDRESS(&sin->sin_addr))) {
				sctp_ifap->src_is_loop = 1;
			}
			if ((IN4_ISPRIVATE_ADDRESS(&sin->sin_addr))) {
				sctp_ifap->src_is_priv = 1;
			}
			sctp_ifnp->num_v4++;
			if (new_ifn_af)
				new_ifn_af = AF_INET;
			break;
		}
#endif
#ifdef INET6
	case AF_INET6:
		{
			/* ok to use deprecated addresses? */
			struct sockaddr_in6 *sin6;

			sin6 = &sctp_ifap->address.sin6;
			if (SCTP_IFN_IS_IFT_LOOP(sctp_ifap->ifn_p) ||
			    (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr))) {
				sctp_ifap->src_is_loop = 1;
			}
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				sctp_ifap->src_is_priv = 1;
			}
			sctp_ifnp->num_v6++;
			if (new_ifn_af)
				new_ifn_af = AF_INET6;
			break;
		}
#endif
	default:
		new_ifn_af = 0;
		break;
	}
	hash_of_addr = sctp_get_ifa_hash_val(&sctp_ifap->address.sa);

	if ((sctp_ifap->src_is_priv == 0) &&
	    (sctp_ifap->src_is_loop == 0)) {
		sctp_ifap->src_is_glob = 1;
	}
	SCTP_IPI_ADDR_WLOCK();
	hash_addr_head = &vrf->vrf_addr_hash[(hash_of_addr & vrf->vrf_addr_hashmark)];
	LIST_INSERT_HEAD(hash_addr_head, sctp_ifap, next_bucket);
	sctp_ifap->refcount = 1;
	LIST_INSERT_HEAD(&sctp_ifnp->ifalist, sctp_ifap, next_ifa);
	sctp_ifnp->ifa_count++;
	vrf->total_ifa_count++;
	atomic_add_int(&SCTP_BASE_INFO(ipi_count_ifas), 1);
	if (new_ifn_af) {
		SCTP_REGISTER_INTERFACE(ifn_index, new_ifn_af);
		sctp_ifnp->registered_af = new_ifn_af;
	}
	SCTP_IPI_ADDR_WUNLOCK();
	if (dynamic_add) {
		/*
		 * Bump up the refcount so that when the timer completes it
		 * will drop back down.
		 */
		struct sctp_laddr *wi;

		atomic_add_int(&sctp_ifap->refcount, 1);
		wi = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_laddr), struct sctp_laddr);
		if (wi == NULL) {
			/*
			 * Gak, what can we do? We have lost an address
			 * change can you say HOSED?
			 */
			SCTPDBG(SCTP_DEBUG_PCB4, "Lost an address change?\n");
			/* Opps, must decrement the count */
			sctp_del_addr_from_vrf(vrf_id, addr, ifn_index,
			    if_name);
			return (NULL);
		}
		SCTP_INCR_LADDR_COUNT();
		memset(wi, 0, sizeof(*wi));
		(void)SCTP_GETTIME_TIMEVAL(&wi->start_time);
		wi->ifa = sctp_ifap;
		wi->action = SCTP_ADD_IP_ADDRESS;

		SCTP_WQ_ADDR_LOCK();
		LIST_INSERT_HEAD(&SCTP_BASE_INFO(addr_wq), wi, sctp_nxt_addr);
		sctp_timer_start(SCTP_TIMER_TYPE_ADDR_WQ,
		    (struct sctp_inpcb *)NULL,
		    (struct sctp_tcb *)NULL,
		    (struct sctp_nets *)NULL);
		SCTP_WQ_ADDR_UNLOCK();
	} else {
		/* it's ready for use */
		sctp_ifap->localifa_flags &= ~SCTP_ADDR_DEFER_USE;
	}
	return (sctp_ifap);
}

void
sctp_del_addr_from_vrf(uint32_t vrf_id, struct sockaddr *addr,
    uint32_t ifn_index, const char *if_name)
{
	struct sctp_vrf *vrf;
	struct sctp_ifa *sctp_ifap = NULL;

	SCTP_IPI_ADDR_WLOCK();
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		SCTPDBG(SCTP_DEBUG_PCB4, "Can't find vrf_id 0x%x\n", vrf_id);
		goto out_now;
	}

#ifdef SCTP_DEBUG
	SCTPDBG(SCTP_DEBUG_PCB4, "vrf_id 0x%x: deleting address:", vrf_id);
	SCTPDBG_ADDR(SCTP_DEBUG_PCB4, addr);
#endif
	sctp_ifap = sctp_find_ifa_by_addr(addr, vrf->vrf_id, SCTP_ADDR_LOCKED);
	if (sctp_ifap) {
		/* Validate the delete */
		if (sctp_ifap->ifn_p) {
			int valid = 0;

			/*-
			 * The name has priority over the ifn_index
			 * if its given. We do this especially for
			 * panda who might recycle indexes fast.
			 */
			if (if_name) {
				if (strncmp(if_name, sctp_ifap->ifn_p->ifn_name, SCTP_IFNAMSIZ) == 0) {
					/* They match its a correct delete */
					valid = 1;
				}
			}
			if (!valid) {
				/* last ditch check ifn_index */
				if (ifn_index == sctp_ifap->ifn_p->ifn_index) {
					valid = 1;
				}
			}
			if (!valid) {
				SCTPDBG(SCTP_DEBUG_PCB4, "ifn:%d ifname:%s does not match addresses\n",
				    ifn_index, ((if_name == NULL) ? "NULL" : if_name));
				SCTPDBG(SCTP_DEBUG_PCB4, "ifn:%d ifname:%s - ignoring delete\n",
				    sctp_ifap->ifn_p->ifn_index, sctp_ifap->ifn_p->ifn_name);
				SCTP_IPI_ADDR_WUNLOCK();
				return;
			}
		}
		SCTPDBG(SCTP_DEBUG_PCB4, "Deleting ifa %p\n", (void *)sctp_ifap);
		sctp_ifap->localifa_flags &= SCTP_ADDR_VALID;
		/*
		 * We don't set the flag. This means that the structure will
		 * hang around in EP's that have bound specific to it until
		 * they close. This gives us TCP like behavior if someone
		 * removes an address (or for that matter adds it right
		 * back).
		 */
		/* sctp_ifap->localifa_flags |= SCTP_BEING_DELETED; */
		vrf->total_ifa_count--;
		LIST_REMOVE(sctp_ifap, next_bucket);
		sctp_remove_ifa_from_ifn(sctp_ifap);
	}
#ifdef SCTP_DEBUG
	else {
		SCTPDBG(SCTP_DEBUG_PCB4, "Del Addr-ifn:%d Could not find address:",
		    ifn_index);
		SCTPDBG_ADDR(SCTP_DEBUG_PCB1, addr);
	}
#endif

out_now:
	SCTP_IPI_ADDR_WUNLOCK();
	if (sctp_ifap) {
		struct sctp_laddr *wi;

		wi = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_laddr), struct sctp_laddr);
		if (wi == NULL) {
			/*
			 * Gak, what can we do? We have lost an address
			 * change can you say HOSED?
			 */
			SCTPDBG(SCTP_DEBUG_PCB4, "Lost an address change?\n");

			/* Oops, must decrement the count */
			sctp_free_ifa(sctp_ifap);
			return;
		}
		SCTP_INCR_LADDR_COUNT();
		memset(wi, 0, sizeof(*wi));
		(void)SCTP_GETTIME_TIMEVAL(&wi->start_time);
		wi->ifa = sctp_ifap;
		wi->action = SCTP_DEL_IP_ADDRESS;
		SCTP_WQ_ADDR_LOCK();
		/*
		 * Should this really be a tailq? As it is we will process
		 * the newest first :-0
		 */
		LIST_INSERT_HEAD(&SCTP_BASE_INFO(addr_wq), wi, sctp_nxt_addr);
		sctp_timer_start(SCTP_TIMER_TYPE_ADDR_WQ,
		    (struct sctp_inpcb *)NULL,
		    (struct sctp_tcb *)NULL,
		    (struct sctp_nets *)NULL);
		SCTP_WQ_ADDR_UNLOCK();
	}
	return;
}


static int
sctp_does_stcb_own_this_addr(struct sctp_tcb *stcb, struct sockaddr *to)
{
	int loopback_scope;
#if defined(INET)
	int ipv4_local_scope, ipv4_addr_legal;
#endif
#if defined(INET6)
	int local_scope, site_scope, ipv6_addr_legal;
#endif
	struct sctp_vrf *vrf;
	struct sctp_ifn *sctp_ifn;
	struct sctp_ifa *sctp_ifa;

	loopback_scope = stcb->asoc.scope.loopback_scope;
#if defined(INET)
	ipv4_local_scope = stcb->asoc.scope.ipv4_local_scope;
	ipv4_addr_legal = stcb->asoc.scope.ipv4_addr_legal;
#endif
#if defined(INET6)
	local_scope = stcb->asoc.scope.local_scope;
	site_scope = stcb->asoc.scope.site_scope;
	ipv6_addr_legal = stcb->asoc.scope.ipv6_addr_legal;
#endif

	SCTP_IPI_ADDR_RLOCK();
	vrf = sctp_find_vrf(stcb->asoc.vrf_id);
	if (vrf == NULL) {
		/* no vrf, no addresses */
		SCTP_IPI_ADDR_RUNLOCK();
		return (0);
	}

	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
			if ((loopback_scope == 0) &&
			    SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
				continue;
			}
			LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
				if (sctp_is_addr_restricted(stcb, sctp_ifa) &&
				    (!sctp_is_addr_pending(stcb, sctp_ifa))) {
					/*
					 * We allow pending addresses, where
					 * we have sent an asconf-add to be
					 * considered valid.
					 */
					continue;
				}
				if (sctp_ifa->address.sa.sa_family != to->sa_family) {
					continue;
				}
				switch (sctp_ifa->address.sa.sa_family) {
#ifdef INET
				case AF_INET:
					if (ipv4_addr_legal) {
						struct sockaddr_in *sin,
						           *rsin;

						sin = &sctp_ifa->address.sin;
						rsin = (struct sockaddr_in *)to;
						if ((ipv4_local_scope == 0) &&
						    IN4_ISPRIVATE_ADDRESS(&sin->sin_addr)) {
							continue;
						}
						if (prison_check_ip4(stcb->sctp_ep->ip_inp.inp.inp_cred,
						    &sin->sin_addr) != 0) {
							continue;
						}
						if (sin->sin_addr.s_addr == rsin->sin_addr.s_addr) {
							SCTP_IPI_ADDR_RUNLOCK();
							return (1);
						}
					}
					break;
#endif
#ifdef INET6
				case AF_INET6:
					if (ipv6_addr_legal) {
						struct sockaddr_in6 *sin6,
						            *rsin6;

						sin6 = &sctp_ifa->address.sin6;
						rsin6 = (struct sockaddr_in6 *)to;
						if (prison_check_ip6(stcb->sctp_ep->ip_inp.inp.inp_cred,
						    &sin6->sin6_addr) != 0) {
							continue;
						}
						if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
							if (local_scope == 0)
								continue;
							if (sin6->sin6_scope_id == 0) {
								if (sa6_recoverscope(sin6) != 0)
									continue;
							}
						}
						if ((site_scope == 0) &&
						    (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr))) {
							continue;
						}
						if (SCTP6_ARE_ADDR_EQUAL(sin6, rsin6)) {
							SCTP_IPI_ADDR_RUNLOCK();
							return (1);
						}
					}
					break;
#endif
				default:
					/* TSNH */
					break;
				}
			}
		}
	} else {
		struct sctp_laddr *laddr;

		LIST_FOREACH(laddr, &stcb->sctp_ep->sctp_addr_list, sctp_nxt_addr) {
			if (laddr->ifa->localifa_flags & SCTP_BEING_DELETED) {
				SCTPDBG(SCTP_DEBUG_PCB1, "ifa being deleted\n");
				continue;
			}
			if (sctp_is_addr_restricted(stcb, laddr->ifa) &&
			    (!sctp_is_addr_pending(stcb, laddr->ifa))) {
				/*
				 * We allow pending addresses, where we have
				 * sent an asconf-add to be considered
				 * valid.
				 */
				continue;
			}
			if (laddr->ifa->address.sa.sa_family != to->sa_family) {
				continue;
			}
			switch (to->sa_family) {
#ifdef INET
			case AF_INET:
				{
					struct sockaddr_in *sin, *rsin;

					sin = &laddr->ifa->address.sin;
					rsin = (struct sockaddr_in *)to;
					if (sin->sin_addr.s_addr == rsin->sin_addr.s_addr) {
						SCTP_IPI_ADDR_RUNLOCK();
						return (1);
					}
					break;
				}
#endif
#ifdef INET6
			case AF_INET6:
				{
					struct sockaddr_in6 *sin6, *rsin6;

					sin6 = &laddr->ifa->address.sin6;
					rsin6 = (struct sockaddr_in6 *)to;
					if (SCTP6_ARE_ADDR_EQUAL(sin6, rsin6)) {
						SCTP_IPI_ADDR_RUNLOCK();
						return (1);
					}
					break;
				}

#endif
			default:
				/* TSNH */
				break;
			}

		}
	}
	SCTP_IPI_ADDR_RUNLOCK();
	return (0);
}


static struct sctp_tcb *
sctp_tcb_special_locate(struct sctp_inpcb **inp_p, struct sockaddr *from,
    struct sockaddr *to, struct sctp_nets **netp, uint32_t vrf_id)
{
	/**** ASSUMES THE CALLER holds the INP_INFO_RLOCK */
	/*
	 * If we support the TCP model, then we must now dig through to see
	 * if we can find our endpoint in the list of tcp ep's.
	 */
	uint16_t lport, rport;
	struct sctppcbhead *ephead;
	struct sctp_inpcb *inp;
	struct sctp_laddr *laddr;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;

	if ((to == NULL) || (from == NULL)) {
		return (NULL);
	}

	switch (to->sa_family) {
#ifdef INET
	case AF_INET:
		if (from->sa_family == AF_INET) {
			lport = ((struct sockaddr_in *)to)->sin_port;
			rport = ((struct sockaddr_in *)from)->sin_port;
		} else {
			return (NULL);
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (from->sa_family == AF_INET6) {
			lport = ((struct sockaddr_in6 *)to)->sin6_port;
			rport = ((struct sockaddr_in6 *)from)->sin6_port;
		} else {
			return (NULL);
		}
		break;
#endif
	default:
		return (NULL);
	}
	ephead = &SCTP_BASE_INFO(sctp_tcpephash)[SCTP_PCBHASH_ALLADDR((lport | rport), SCTP_BASE_INFO(hashtcpmark))];
	/*
	 * Ok now for each of the guys in this bucket we must look and see:
	 * - Does the remote port match. - Does there single association's
	 * addresses match this address (to). If so we update p_ep to point
	 * to this ep and return the tcb from it.
	 */
	LIST_FOREACH(inp, ephead, sctp_hash) {
		SCTP_INP_RLOCK(inp);
		if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		if (lport != inp->sctp_lport) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		switch (to->sa_family) {
#ifdef INET
		case AF_INET:
			{
				struct sockaddr_in *sin;

				sin = (struct sockaddr_in *)to;
				if (prison_check_ip4(inp->ip_inp.inp.inp_cred,
				    &sin->sin_addr) != 0) {
					SCTP_INP_RUNLOCK(inp);
					continue;
				}
				break;
			}
#endif
#ifdef INET6
		case AF_INET6:
			{
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)to;
				if (prison_check_ip6(inp->ip_inp.inp.inp_cred,
				    &sin6->sin6_addr) != 0) {
					SCTP_INP_RUNLOCK(inp);
					continue;
				}
				break;
			}
#endif
		default:
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		if (inp->def_vrf_id != vrf_id) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		/* check to see if the ep has one of the addresses */
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) == 0) {
			/* We are NOT bound all, so look further */
			int match = 0;

			LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {

				if (laddr->ifa == NULL) {
					SCTPDBG(SCTP_DEBUG_PCB1, "%s: NULL ifa\n", __func__);
					continue;
				}
				if (laddr->ifa->localifa_flags & SCTP_BEING_DELETED) {
					SCTPDBG(SCTP_DEBUG_PCB1, "ifa being deleted\n");
					continue;
				}
				if (laddr->ifa->address.sa.sa_family ==
				    to->sa_family) {
					/* see if it matches */
#ifdef INET
					if (from->sa_family == AF_INET) {
						struct sockaddr_in *intf_addr,
						           *sin;

						intf_addr = &laddr->ifa->address.sin;
						sin = (struct sockaddr_in *)to;
						if (sin->sin_addr.s_addr ==
						    intf_addr->sin_addr.s_addr) {
							match = 1;
							break;
						}
					}
#endif
#ifdef INET6
					if (from->sa_family == AF_INET6) {
						struct sockaddr_in6 *intf_addr6;
						struct sockaddr_in6 *sin6;

						sin6 = (struct sockaddr_in6 *)
						    to;
						intf_addr6 = &laddr->ifa->address.sin6;

						if (SCTP6_ARE_ADDR_EQUAL(sin6,
						    intf_addr6)) {
							match = 1;
							break;
						}
					}
#endif
				}
			}
			if (match == 0) {
				/* This endpoint does not have this address */
				SCTP_INP_RUNLOCK(inp);
				continue;
			}
		}
		/*
		 * Ok if we hit here the ep has the address, does it hold
		 * the tcb?
		 */
		/* XXX: Why don't we TAILQ_FOREACH through sctp_asoc_list? */
		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		if (stcb == NULL) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		SCTP_TCB_LOCK(stcb);
		if (!sctp_does_stcb_own_this_addr(stcb, to)) {
			SCTP_TCB_UNLOCK(stcb);
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		if (stcb->rport != rport) {
			/* remote port does not match. */
			SCTP_TCB_UNLOCK(stcb);
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
			SCTP_TCB_UNLOCK(stcb);
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		if (!sctp_does_stcb_own_this_addr(stcb, to)) {
			SCTP_TCB_UNLOCK(stcb);
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		/* Does this TCB have a matching address? */
		TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {

			if (net->ro._l_addr.sa.sa_family != from->sa_family) {
				/* not the same family, can't be a match */
				continue;
			}
			switch (from->sa_family) {
#ifdef INET
			case AF_INET:
				{
					struct sockaddr_in *sin, *rsin;

					sin = (struct sockaddr_in *)&net->ro._l_addr;
					rsin = (struct sockaddr_in *)from;
					if (sin->sin_addr.s_addr ==
					    rsin->sin_addr.s_addr) {
						/* found it */
						if (netp != NULL) {
							*netp = net;
						}
						/*
						 * Update the endpoint
						 * pointer
						 */
						*inp_p = inp;
						SCTP_INP_RUNLOCK(inp);
						return (stcb);
					}
					break;
				}
#endif
#ifdef INET6
			case AF_INET6:
				{
					struct sockaddr_in6 *sin6, *rsin6;

					sin6 = (struct sockaddr_in6 *)&net->ro._l_addr;
					rsin6 = (struct sockaddr_in6 *)from;
					if (SCTP6_ARE_ADDR_EQUAL(sin6,
					    rsin6)) {
						/* found it */
						if (netp != NULL) {
							*netp = net;
						}
						/*
						 * Update the endpoint
						 * pointer
						 */
						*inp_p = inp;
						SCTP_INP_RUNLOCK(inp);
						return (stcb);
					}
					break;
				}
#endif
			default:
				/* TSNH */
				break;
			}
		}
		SCTP_TCB_UNLOCK(stcb);
		SCTP_INP_RUNLOCK(inp);
	}
	return (NULL);
}


/*
 * rules for use
 *
 * 1) If I return a NULL you must decrement any INP ref cnt. 2) If I find an
 * stcb, both will be locked (locked_tcb and stcb) but decrement will be done
 * (if locked == NULL). 3) Decrement happens on return ONLY if locked ==
 * NULL.
 */

struct sctp_tcb *
sctp_findassociation_ep_addr(struct sctp_inpcb **inp_p, struct sockaddr *remote,
    struct sctp_nets **netp, struct sockaddr *local, struct sctp_tcb *locked_tcb)
{
	struct sctpasochead *head;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb = NULL;
	struct sctp_nets *net;
	uint16_t rport;

	inp = *inp_p;
	switch (remote->sa_family) {
#ifdef INET
	case AF_INET:
		rport = (((struct sockaddr_in *)remote)->sin_port);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		rport = (((struct sockaddr_in6 *)remote)->sin6_port);
		break;
#endif
	default:
		return (NULL);
	}
	if (locked_tcb) {
		/*
		 * UN-lock so we can do proper locking here this occurs when
		 * called from load_addresses_from_init.
		 */
		atomic_add_int(&locked_tcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(locked_tcb);
	}
	SCTP_INP_INFO_RLOCK();
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
		/*-
		 * Now either this guy is our listener or it's the
		 * connector. If it is the one that issued the connect, then
		 * it's only chance is to be the first TCB in the list. If
		 * it is the acceptor, then do the special_lookup to hash
		 * and find the real inp.
		 */
		if ((inp->sctp_socket) && SCTP_IS_LISTENING(inp)) {
			/* to is peer addr, from is my addr */
			stcb = sctp_tcb_special_locate(inp_p, remote, local,
			    netp, inp->def_vrf_id);
			if ((stcb != NULL) && (locked_tcb == NULL)) {
				/* we have a locked tcb, lower refcount */
				SCTP_INP_DECR_REF(inp);
			}
			if ((locked_tcb != NULL) && (locked_tcb != stcb)) {
				SCTP_INP_RLOCK(locked_tcb->sctp_ep);
				SCTP_TCB_LOCK(locked_tcb);
				atomic_subtract_int(&locked_tcb->asoc.refcnt, 1);
				SCTP_INP_RUNLOCK(locked_tcb->sctp_ep);
			}
			SCTP_INP_INFO_RUNLOCK();
			return (stcb);
		} else {
			SCTP_INP_WLOCK(inp);
			if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
				goto null_return;
			}
			stcb = LIST_FIRST(&inp->sctp_asoc_list);
			if (stcb == NULL) {
				goto null_return;
			}
			SCTP_TCB_LOCK(stcb);

			if (stcb->rport != rport) {
				/* remote port does not match. */
				SCTP_TCB_UNLOCK(stcb);
				goto null_return;
			}
			if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				SCTP_TCB_UNLOCK(stcb);
				goto null_return;
			}
			if (local && !sctp_does_stcb_own_this_addr(stcb, local)) {
				SCTP_TCB_UNLOCK(stcb);
				goto null_return;
			}
			/* now look at the list of remote addresses */
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
#ifdef INVARIANTS
				if (net == (TAILQ_NEXT(net, sctp_next))) {
					panic("Corrupt net list");
				}
#endif
				if (net->ro._l_addr.sa.sa_family !=
				    remote->sa_family) {
					/* not the same family */
					continue;
				}
				switch (remote->sa_family) {
#ifdef INET
				case AF_INET:
					{
						struct sockaddr_in *sin,
						           *rsin;

						sin = (struct sockaddr_in *)
						    &net->ro._l_addr;
						rsin = (struct sockaddr_in *)remote;
						if (sin->sin_addr.s_addr ==
						    rsin->sin_addr.s_addr) {
							/* found it */
							if (netp != NULL) {
								*netp = net;
							}
							if (locked_tcb == NULL) {
								SCTP_INP_DECR_REF(inp);
							} else if (locked_tcb != stcb) {
								SCTP_TCB_LOCK(locked_tcb);
							}
							if (locked_tcb) {
								atomic_subtract_int(&locked_tcb->asoc.refcnt, 1);
							}

							SCTP_INP_WUNLOCK(inp);
							SCTP_INP_INFO_RUNLOCK();
							return (stcb);
						}
						break;
					}
#endif
#ifdef INET6
				case AF_INET6:
					{
						struct sockaddr_in6 *sin6,
						            *rsin6;

						sin6 = (struct sockaddr_in6 *)&net->ro._l_addr;
						rsin6 = (struct sockaddr_in6 *)remote;
						if (SCTP6_ARE_ADDR_EQUAL(sin6,
						    rsin6)) {
							/* found it */
							if (netp != NULL) {
								*netp = net;
							}
							if (locked_tcb == NULL) {
								SCTP_INP_DECR_REF(inp);
							} else if (locked_tcb != stcb) {
								SCTP_TCB_LOCK(locked_tcb);
							}
							if (locked_tcb) {
								atomic_subtract_int(&locked_tcb->asoc.refcnt, 1);
							}
							SCTP_INP_WUNLOCK(inp);
							SCTP_INP_INFO_RUNLOCK();
							return (stcb);
						}
						break;
					}
#endif
				default:
					/* TSNH */
					break;
				}
			}
			SCTP_TCB_UNLOCK(stcb);
		}
	} else {
		SCTP_INP_WLOCK(inp);
		if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
			goto null_return;
		}
		head = &inp->sctp_tcbhash[SCTP_PCBHASH_ALLADDR(rport,
		    inp->sctp_hashmark)];
		LIST_FOREACH(stcb, head, sctp_tcbhash) {
			if (stcb->rport != rport) {
				/* remote port does not match */
				continue;
			}
			SCTP_TCB_LOCK(stcb);
			if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				SCTP_TCB_UNLOCK(stcb);
				continue;
			}
			if (local && !sctp_does_stcb_own_this_addr(stcb, local)) {
				SCTP_TCB_UNLOCK(stcb);
				continue;
			}
			/* now look at the list of remote addresses */
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
#ifdef INVARIANTS
				if (net == (TAILQ_NEXT(net, sctp_next))) {
					panic("Corrupt net list");
				}
#endif
				if (net->ro._l_addr.sa.sa_family !=
				    remote->sa_family) {
					/* not the same family */
					continue;
				}
				switch (remote->sa_family) {
#ifdef INET
				case AF_INET:
					{
						struct sockaddr_in *sin,
						           *rsin;

						sin = (struct sockaddr_in *)
						    &net->ro._l_addr;
						rsin = (struct sockaddr_in *)remote;
						if (sin->sin_addr.s_addr ==
						    rsin->sin_addr.s_addr) {
							/* found it */
							if (netp != NULL) {
								*netp = net;
							}
							if (locked_tcb == NULL) {
								SCTP_INP_DECR_REF(inp);
							} else if (locked_tcb != stcb) {
								SCTP_TCB_LOCK(locked_tcb);
							}
							if (locked_tcb) {
								atomic_subtract_int(&locked_tcb->asoc.refcnt, 1);
							}
							SCTP_INP_WUNLOCK(inp);
							SCTP_INP_INFO_RUNLOCK();
							return (stcb);
						}
						break;
					}
#endif
#ifdef INET6
				case AF_INET6:
					{
						struct sockaddr_in6 *sin6,
						            *rsin6;

						sin6 = (struct sockaddr_in6 *)
						    &net->ro._l_addr;
						rsin6 = (struct sockaddr_in6 *)remote;
						if (SCTP6_ARE_ADDR_EQUAL(sin6,
						    rsin6)) {
							/* found it */
							if (netp != NULL) {
								*netp = net;
							}
							if (locked_tcb == NULL) {
								SCTP_INP_DECR_REF(inp);
							} else if (locked_tcb != stcb) {
								SCTP_TCB_LOCK(locked_tcb);
							}
							if (locked_tcb) {
								atomic_subtract_int(&locked_tcb->asoc.refcnt, 1);
							}
							SCTP_INP_WUNLOCK(inp);
							SCTP_INP_INFO_RUNLOCK();
							return (stcb);
						}
						break;
					}
#endif
				default:
					/* TSNH */
					break;
				}
			}
			SCTP_TCB_UNLOCK(stcb);
		}
	}
null_return:
	/* clean up for returning null */
	if (locked_tcb) {
		SCTP_TCB_LOCK(locked_tcb);
		atomic_subtract_int(&locked_tcb->asoc.refcnt, 1);
	}
	SCTP_INP_WUNLOCK(inp);
	SCTP_INP_INFO_RUNLOCK();
	/* not found */
	return (NULL);
}


/*
 * Find an association for a specific endpoint using the association id given
 * out in the COMM_UP notification
 */
struct sctp_tcb *
sctp_findasoc_ep_asocid_locked(struct sctp_inpcb *inp, sctp_assoc_t asoc_id, int want_lock)
{
	/*
	 * Use my the assoc_id to find a endpoint
	 */
	struct sctpasochead *head;
	struct sctp_tcb *stcb;
	uint32_t id;

	if (inp == NULL) {
		SCTP_PRINTF("TSNH ep_associd\n");
		return (NULL);
	}
	if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
		SCTP_PRINTF("TSNH ep_associd0\n");
		return (NULL);
	}
	id = (uint32_t)asoc_id;
	head = &inp->sctp_asocidhash[SCTP_PCBHASH_ASOC(id, inp->hashasocidmark)];
	if (head == NULL) {
		/* invalid id TSNH */
		SCTP_PRINTF("TSNH ep_associd1\n");
		return (NULL);
	}
	LIST_FOREACH(stcb, head, sctp_tcbasocidhash) {
		if (stcb->asoc.assoc_id == id) {
			if (inp != stcb->sctp_ep) {
				/*
				 * some other guy has the same id active (id
				 * collision ??).
				 */
				SCTP_PRINTF("TSNH ep_associd2\n");
				continue;
			}
			if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				continue;
			}
			if (want_lock) {
				SCTP_TCB_LOCK(stcb);
			}
			return (stcb);
		}
	}
	return (NULL);
}


struct sctp_tcb *
sctp_findassociation_ep_asocid(struct sctp_inpcb *inp, sctp_assoc_t asoc_id, int want_lock)
{
	struct sctp_tcb *stcb;

	SCTP_INP_RLOCK(inp);
	stcb = sctp_findasoc_ep_asocid_locked(inp, asoc_id, want_lock);
	SCTP_INP_RUNLOCK(inp);
	return (stcb);
}


/*
 * Endpoint probe expects that the INP_INFO is locked.
 */
static struct sctp_inpcb *
sctp_endpoint_probe(struct sockaddr *nam, struct sctppcbhead *head,
    uint16_t lport, uint32_t vrf_id)
{
	struct sctp_inpcb *inp;
	struct sctp_laddr *laddr;
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sockaddr_in6 *intf_addr6;
#endif
	int fnd;

#ifdef INET
	sin = NULL;
#endif
#ifdef INET6
	sin6 = NULL;
#endif
	switch (nam->sa_family) {
#ifdef INET
	case AF_INET:
		sin = (struct sockaddr_in *)nam;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)nam;
		break;
#endif
	default:
		/* unsupported family */
		return (NULL);
	}

	if (head == NULL)
		return (NULL);

	LIST_FOREACH(inp, head, sctp_hash) {
		SCTP_INP_RLOCK(inp);
		if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) &&
		    (inp->sctp_lport == lport)) {
			/* got it */
			switch (nam->sa_family) {
#ifdef INET
			case AF_INET:
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
				    SCTP_IPV6_V6ONLY(inp)) {
					/*
					 * IPv4 on a IPv6 socket with ONLY
					 * IPv6 set
					 */
					SCTP_INP_RUNLOCK(inp);
					continue;
				}
				if (prison_check_ip4(inp->ip_inp.inp.inp_cred,
				    &sin->sin_addr) != 0) {
					SCTP_INP_RUNLOCK(inp);
					continue;
				}
				break;
#endif
#ifdef INET6
			case AF_INET6:
				/*
				 * A V6 address and the endpoint is NOT
				 * bound V6
				 */
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) {
					SCTP_INP_RUNLOCK(inp);
					continue;
				}
				if (prison_check_ip6(inp->ip_inp.inp.inp_cred,
				    &sin6->sin6_addr) != 0) {
					SCTP_INP_RUNLOCK(inp);
					continue;
				}
				break;
#endif
			default:
				break;
			}
			/* does a VRF id match? */
			fnd = 0;
			if (inp->def_vrf_id == vrf_id)
				fnd = 1;

			SCTP_INP_RUNLOCK(inp);
			if (!fnd)
				continue;
			return (inp);
		}
		SCTP_INP_RUNLOCK(inp);
	}
	switch (nam->sa_family) {
#ifdef INET
	case AF_INET:
		if (sin->sin_addr.s_addr == INADDR_ANY) {
			/* Can't hunt for one that has no address specified */
			return (NULL);
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			/* Can't hunt for one that has no address specified */
			return (NULL);
		}
		break;
#endif
	default:
		break;
	}
	/*
	 * ok, not bound to all so see if we can find a EP bound to this
	 * address.
	 */
	LIST_FOREACH(inp, head, sctp_hash) {
		SCTP_INP_RLOCK(inp);
		if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL)) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		/*
		 * Ok this could be a likely candidate, look at all of its
		 * addresses
		 */
		if (inp->sctp_lport != lport) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		/* does a VRF id match? */
		fnd = 0;
		if (inp->def_vrf_id == vrf_id)
			fnd = 1;

		if (!fnd) {
			SCTP_INP_RUNLOCK(inp);
			continue;
		}
		LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
			if (laddr->ifa == NULL) {
				SCTPDBG(SCTP_DEBUG_PCB1, "%s: NULL ifa\n",
				    __func__);
				continue;
			}
			SCTPDBG(SCTP_DEBUG_PCB1, "Ok laddr->ifa:%p is possible, ",
			    (void *)laddr->ifa);
			if (laddr->ifa->localifa_flags & SCTP_BEING_DELETED) {
				SCTPDBG(SCTP_DEBUG_PCB1, "Huh IFA being deleted\n");
				continue;
			}
			if (laddr->ifa->address.sa.sa_family == nam->sa_family) {
				/* possible, see if it matches */
				switch (nam->sa_family) {
#ifdef INET
				case AF_INET:
					if (sin->sin_addr.s_addr ==
					    laddr->ifa->address.sin.sin_addr.s_addr) {
						SCTP_INP_RUNLOCK(inp);
						return (inp);
					}
					break;
#endif
#ifdef INET6
				case AF_INET6:
					intf_addr6 = &laddr->ifa->address.sin6;
					if (SCTP6_ARE_ADDR_EQUAL(sin6,
					    intf_addr6)) {
						SCTP_INP_RUNLOCK(inp);
						return (inp);
					}
					break;
#endif
				}
			}
		}
		SCTP_INP_RUNLOCK(inp);
	}
	return (NULL);
}


static struct sctp_inpcb *
sctp_isport_inuse(struct sctp_inpcb *inp, uint16_t lport, uint32_t vrf_id)
{
	struct sctppcbhead *head;
	struct sctp_inpcb *t_inp;
	int fnd;

	head = &SCTP_BASE_INFO(sctp_ephash)[SCTP_PCBHASH_ALLADDR(lport,
	    SCTP_BASE_INFO(hashmark))];
	LIST_FOREACH(t_inp, head, sctp_hash) {
		if (t_inp->sctp_lport != lport) {
			continue;
		}
		/* is it in the VRF in question */
		fnd = 0;
		if (t_inp->def_vrf_id == vrf_id)
			fnd = 1;
		if (!fnd)
			continue;

		/* This one is in use. */
		/* check the v6/v4 binding issue */
		if ((t_inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
		    SCTP_IPV6_V6ONLY(t_inp)) {
			if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
				/* collision in V6 space */
				return (t_inp);
			} else {
				/* inp is BOUND_V4 no conflict */
				continue;
			}
		} else if (t_inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
			/* t_inp is bound v4 and v6, conflict always */
			return (t_inp);
		} else {
			/* t_inp is bound only V4 */
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
			    SCTP_IPV6_V6ONLY(inp)) {
				/* no conflict */
				continue;
			}
			/* else fall through to conflict */
		}
		return (t_inp);
	}
	return (NULL);
}


int
sctp_swap_inpcb_for_listen(struct sctp_inpcb *inp)
{
	/* For 1-2-1 with port reuse */
	struct sctppcbhead *head;
	struct sctp_inpcb *tinp, *ninp;

	if (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_PORTREUSE)) {
		/* only works with port reuse on */
		return (-1);
	}
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) == 0) {
		return (0);
	}
	SCTP_INP_RUNLOCK(inp);
	SCTP_INP_INFO_WLOCK();
	head = &SCTP_BASE_INFO(sctp_ephash)[SCTP_PCBHASH_ALLADDR(inp->sctp_lport,
	    SCTP_BASE_INFO(hashmark))];
	/* Kick out all non-listeners to the TCP hash */
	LIST_FOREACH_SAFE(tinp, head, sctp_hash, ninp) {
		if (tinp->sctp_lport != inp->sctp_lport) {
			continue;
		}
		if (tinp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
			continue;
		}
		if (tinp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
			continue;
		}
		if (SCTP_IS_LISTENING(tinp)) {
			continue;
		}
		SCTP_INP_WLOCK(tinp);
		LIST_REMOVE(tinp, sctp_hash);
		head = &SCTP_BASE_INFO(sctp_tcpephash)[SCTP_PCBHASH_ALLADDR(tinp->sctp_lport, SCTP_BASE_INFO(hashtcpmark))];
		tinp->sctp_flags |= SCTP_PCB_FLAGS_IN_TCPPOOL;
		LIST_INSERT_HEAD(head, tinp, sctp_hash);
		SCTP_INP_WUNLOCK(tinp);
	}
	SCTP_INP_WLOCK(inp);
	/* Pull from where he was */
	LIST_REMOVE(inp, sctp_hash);
	inp->sctp_flags &= ~SCTP_PCB_FLAGS_IN_TCPPOOL;
	head = &SCTP_BASE_INFO(sctp_ephash)[SCTP_PCBHASH_ALLADDR(inp->sctp_lport, SCTP_BASE_INFO(hashmark))];
	LIST_INSERT_HEAD(head, inp, sctp_hash);
	SCTP_INP_WUNLOCK(inp);
	SCTP_INP_RLOCK(inp);
	SCTP_INP_INFO_WUNLOCK();
	return (0);
}


struct sctp_inpcb *
sctp_pcb_findep(struct sockaddr *nam, int find_tcp_pool, int have_lock,
    uint32_t vrf_id)
{
	/*
	 * First we check the hash table to see if someone has this port
	 * bound with just the port.
	 */
	struct sctp_inpcb *inp;
	struct sctppcbhead *head;
	int lport;
	unsigned int i;
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	switch (nam->sa_family) {
#ifdef INET
	case AF_INET:
		sin = (struct sockaddr_in *)nam;
		lport = sin->sin_port;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)nam;
		lport = sin6->sin6_port;
		break;
#endif
	default:
		return (NULL);
	}
	/*
	 * I could cheat here and just cast to one of the types but we will
	 * do it right. It also provides the check against an Unsupported
	 * type too.
	 */
	/* Find the head of the ALLADDR chain */
	if (have_lock == 0) {
		SCTP_INP_INFO_RLOCK();
	}
	head = &SCTP_BASE_INFO(sctp_ephash)[SCTP_PCBHASH_ALLADDR(lport,
	    SCTP_BASE_INFO(hashmark))];
	inp = sctp_endpoint_probe(nam, head, lport, vrf_id);

	/*
	 * If the TCP model exists it could be that the main listening
	 * endpoint is gone but there still exists a connected socket for
	 * this guy. If so we can return the first one that we find. This
	 * may NOT be the correct one so the caller should be wary on the
	 * returned INP. Currently the only caller that sets find_tcp_pool
	 * is in bindx where we are verifying that a user CAN bind the
	 * address. He either has bound it already, or someone else has, or
	 * its open to bind, so this is good enough.
	 */
	if (inp == NULL && find_tcp_pool) {
		for (i = 0; i < SCTP_BASE_INFO(hashtcpmark) + 1; i++) {
			head = &SCTP_BASE_INFO(sctp_tcpephash)[i];
			inp = sctp_endpoint_probe(nam, head, lport, vrf_id);
			if (inp) {
				break;
			}
		}
	}
	if (inp) {
		SCTP_INP_INCR_REF(inp);
	}
	if (have_lock == 0) {
		SCTP_INP_INFO_RUNLOCK();
	}
	return (inp);
}


/*
 * Find an association for an endpoint with the pointer to whom you want to
 * send to and the endpoint pointer. The address can be IPv4 or IPv6. We may
 * need to change the *to to some other struct like a mbuf...
 */
struct sctp_tcb *
sctp_findassociation_addr_sa(struct sockaddr *from, struct sockaddr *to,
    struct sctp_inpcb **inp_p, struct sctp_nets **netp, int find_tcp_pool,
    uint32_t vrf_id)
{
	struct sctp_inpcb *inp = NULL;
	struct sctp_tcb *stcb;

	SCTP_INP_INFO_RLOCK();
	if (find_tcp_pool) {
		if (inp_p != NULL) {
			stcb = sctp_tcb_special_locate(inp_p, from, to, netp,
			    vrf_id);
		} else {
			stcb = sctp_tcb_special_locate(&inp, from, to, netp,
			    vrf_id);
		}
		if (stcb != NULL) {
			SCTP_INP_INFO_RUNLOCK();
			return (stcb);
		}
	}
	inp = sctp_pcb_findep(to, 0, 1, vrf_id);
	if (inp_p != NULL) {
		*inp_p = inp;
	}
	SCTP_INP_INFO_RUNLOCK();
	if (inp == NULL) {
		return (NULL);
	}
	/*
	 * ok, we have an endpoint, now lets find the assoc for it (if any)
	 * we now place the source address or from in the to of the find
	 * endpoint call. Since in reality this chain is used from the
	 * inbound packet side.
	 */
	if (inp_p != NULL) {
		stcb = sctp_findassociation_ep_addr(inp_p, from, netp, to,
		    NULL);
	} else {
		stcb = sctp_findassociation_ep_addr(&inp, from, netp, to,
		    NULL);
	}
	return (stcb);
}


/*
 * This routine will grub through the mbuf that is a INIT or INIT-ACK and
 * find all addresses that the sender has specified in any address list. Each
 * address will be used to lookup the TCB and see if one exits.
 */
static struct sctp_tcb *
sctp_findassociation_special_addr(struct mbuf *m, int offset,
    struct sctphdr *sh, struct sctp_inpcb **inp_p, struct sctp_nets **netp,
    struct sockaddr *dst)
{
	struct sctp_paramhdr *phdr, param_buf;
#if defined(INET) || defined(INET6)
	struct sctp_tcb *stcb;
	uint16_t ptype;
#endif
	uint16_t plen;
#ifdef INET
	struct sockaddr_in sin4;
#endif
#ifdef INET6
	struct sockaddr_in6 sin6;
#endif

#ifdef INET
	memset(&sin4, 0, sizeof(sin4));
	sin4.sin_len = sizeof(sin4);
	sin4.sin_family = AF_INET;
	sin4.sin_port = sh->src_port;
#endif
#ifdef INET6
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = sh->src_port;
#endif

	offset += sizeof(struct sctp_init_chunk);

	phdr = sctp_get_next_param(m, offset, &param_buf, sizeof(param_buf));
	while (phdr != NULL) {
		/* now we must see if we want the parameter */
#if defined(INET) || defined(INET6)
		ptype = ntohs(phdr->param_type);
#endif
		plen = ntohs(phdr->param_length);
		if (plen == 0) {
			break;
		}
#ifdef INET
		if (ptype == SCTP_IPV4_ADDRESS &&
		    plen == sizeof(struct sctp_ipv4addr_param)) {
			/* Get the rest of the address */
			struct sctp_ipv4addr_param ip4_param, *p4;

			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)&ip4_param, sizeof(ip4_param));
			if (phdr == NULL) {
				return (NULL);
			}
			p4 = (struct sctp_ipv4addr_param *)phdr;
			memcpy(&sin4.sin_addr, &p4->addr, sizeof(p4->addr));
			/* look it up */
			stcb = sctp_findassociation_ep_addr(inp_p,
			    (struct sockaddr *)&sin4, netp, dst, NULL);
			if (stcb != NULL) {
				return (stcb);
			}
		}
#endif
#ifdef INET6
		if (ptype == SCTP_IPV6_ADDRESS &&
		    plen == sizeof(struct sctp_ipv6addr_param)) {
			/* Get the rest of the address */
			struct sctp_ipv6addr_param ip6_param, *p6;

			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)&ip6_param, sizeof(ip6_param));
			if (phdr == NULL) {
				return (NULL);
			}
			p6 = (struct sctp_ipv6addr_param *)phdr;
			memcpy(&sin6.sin6_addr, &p6->addr, sizeof(p6->addr));
			/* look it up */
			stcb = sctp_findassociation_ep_addr(inp_p,
			    (struct sockaddr *)&sin6, netp, dst, NULL);
			if (stcb != NULL) {
				return (stcb);
			}
		}
#endif
		offset += SCTP_SIZE32(plen);
		phdr = sctp_get_next_param(m, offset, &param_buf,
		    sizeof(param_buf));
	}
	return (NULL);
}

static struct sctp_tcb *
sctp_findassoc_by_vtag(struct sockaddr *from, struct sockaddr *to, uint32_t vtag,
    struct sctp_inpcb **inp_p, struct sctp_nets **netp, uint16_t rport,
    uint16_t lport, int skip_src_check, uint32_t vrf_id, uint32_t remote_tag)
{
	/*
	 * Use my vtag to hash. If we find it we then verify the source addr
	 * is in the assoc. If all goes well we save a bit on rec of a
	 * packet.
	 */
	struct sctpasochead *head;
	struct sctp_nets *net;
	struct sctp_tcb *stcb;

	SCTP_INP_INFO_RLOCK();
	head = &SCTP_BASE_INFO(sctp_asochash)[SCTP_PCBHASH_ASOC(vtag,
	    SCTP_BASE_INFO(hashasocmark))];
	LIST_FOREACH(stcb, head, sctp_asocs) {
		SCTP_INP_RLOCK(stcb->sctp_ep);
		if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
			SCTP_INP_RUNLOCK(stcb->sctp_ep);
			continue;
		}
		if (stcb->sctp_ep->def_vrf_id != vrf_id) {
			SCTP_INP_RUNLOCK(stcb->sctp_ep);
			continue;
		}
		SCTP_TCB_LOCK(stcb);
		SCTP_INP_RUNLOCK(stcb->sctp_ep);
		if (stcb->asoc.my_vtag == vtag) {
			/* candidate */
			if (stcb->rport != rport) {
				SCTP_TCB_UNLOCK(stcb);
				continue;
			}
			if (stcb->sctp_ep->sctp_lport != lport) {
				SCTP_TCB_UNLOCK(stcb);
				continue;
			}
			if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				SCTP_TCB_UNLOCK(stcb);
				continue;
			}
			/* RRS:Need toaddr check here */
			if (sctp_does_stcb_own_this_addr(stcb, to) == 0) {
				/* Endpoint does not own this address */
				SCTP_TCB_UNLOCK(stcb);
				continue;
			}
			if (remote_tag) {
				/*
				 * If we have both vtags that's all we match
				 * on
				 */
				if (stcb->asoc.peer_vtag == remote_tag) {
					/*
					 * If both tags match we consider it
					 * conclusive and check NO
					 * source/destination addresses
					 */
					goto conclusive;
				}
			}
			if (skip_src_check) {
		conclusive:
				if (from) {
					*netp = sctp_findnet(stcb, from);
				} else {
					*netp = NULL;	/* unknown */
				}
				if (inp_p)
					*inp_p = stcb->sctp_ep;
				SCTP_INP_INFO_RUNLOCK();
				return (stcb);
			}
			net = sctp_findnet(stcb, from);
			if (net) {
				/* yep its him. */
				*netp = net;
				SCTP_STAT_INCR(sctps_vtagexpress);
				*inp_p = stcb->sctp_ep;
				SCTP_INP_INFO_RUNLOCK();
				return (stcb);
			} else {
				/*
				 * not him, this should only happen in rare
				 * cases so I peg it.
				 */
				SCTP_STAT_INCR(sctps_vtagbogus);
			}
		}
		SCTP_TCB_UNLOCK(stcb);
	}
	SCTP_INP_INFO_RUNLOCK();
	return (NULL);
}


/*
 * Find an association with the pointer to the inbound IP packet. This can be
 * a IPv4 or IPv6 packet.
 */
struct sctp_tcb *
sctp_findassociation_addr(struct mbuf *m, int offset,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_chunkhdr *ch,
    struct sctp_inpcb **inp_p, struct sctp_nets **netp, uint32_t vrf_id)
{
	struct sctp_tcb *stcb;
	struct sctp_inpcb *inp;

	if (sh->v_tag) {
		/* we only go down this path if vtag is non-zero */
		stcb = sctp_findassoc_by_vtag(src, dst, ntohl(sh->v_tag),
		    inp_p, netp, sh->src_port, sh->dest_port, 0, vrf_id, 0);
		if (stcb) {
			return (stcb);
		}
	}

	if (inp_p) {
		stcb = sctp_findassociation_addr_sa(src, dst, inp_p, netp,
		    1, vrf_id);
		inp = *inp_p;
	} else {
		stcb = sctp_findassociation_addr_sa(src, dst, &inp, netp,
		    1, vrf_id);
	}
	SCTPDBG(SCTP_DEBUG_PCB1, "stcb:%p inp:%p\n", (void *)stcb, (void *)inp);
	if (stcb == NULL && inp) {
		/* Found a EP but not this address */
		if ((ch->chunk_type == SCTP_INITIATION) ||
		    (ch->chunk_type == SCTP_INITIATION_ACK)) {
			/*-
			 * special hook, we do NOT return linp or an
			 * association that is linked to an existing
			 * association that is under the TCP pool (i.e. no
			 * listener exists). The endpoint finding routine
			 * will always find a listener before examining the
			 * TCP pool.
			 */
			if (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) {
				if (inp_p) {
					*inp_p = NULL;
				}
				return (NULL);
			}
			stcb = sctp_findassociation_special_addr(m,
			    offset, sh, &inp, netp, dst);
			if (inp_p != NULL) {
				*inp_p = inp;
			}
		}
	}
	SCTPDBG(SCTP_DEBUG_PCB1, "stcb is %p\n", (void *)stcb);
	return (stcb);
}

/*
 * lookup an association by an ASCONF lookup address.
 * if the lookup address is 0.0.0.0 or ::0, use the vtag to do the lookup
 */
struct sctp_tcb *
sctp_findassociation_ep_asconf(struct mbuf *m, int offset,
    struct sockaddr *dst, struct sctphdr *sh,
    struct sctp_inpcb **inp_p, struct sctp_nets **netp, uint32_t vrf_id)
{
	struct sctp_tcb *stcb;
	union sctp_sockstore remote_store;
	struct sctp_paramhdr param_buf, *phdr;
	int ptype;
	int zero_address = 0;
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	memset(&remote_store, 0, sizeof(remote_store));
	phdr = sctp_get_next_param(m, offset + sizeof(struct sctp_asconf_chunk),
	    &param_buf, sizeof(struct sctp_paramhdr));
	if (phdr == NULL) {
		SCTPDBG(SCTP_DEBUG_INPUT3, "%s: failed to get asconf lookup addr\n",
		    __func__);
		return NULL;
	}
	ptype = (int)((uint32_t)ntohs(phdr->param_type));
	/* get the correlation address */
	switch (ptype) {
#ifdef INET6
	case SCTP_IPV6_ADDRESS:
		{
			/* ipv6 address param */
			struct sctp_ipv6addr_param *p6, p6_buf;

			if (ntohs(phdr->param_length) != sizeof(struct sctp_ipv6addr_param)) {
				return NULL;
			}
			p6 = (struct sctp_ipv6addr_param *)sctp_get_next_param(m,
			    offset + sizeof(struct sctp_asconf_chunk),
			    &p6_buf.ph, sizeof(p6_buf));
			if (p6 == NULL) {
				SCTPDBG(SCTP_DEBUG_INPUT3, "%s: failed to get asconf v6 lookup addr\n",
				    __func__);
				return (NULL);
			}
			sin6 = &remote_store.sin6;
			sin6->sin6_family = AF_INET6;
			sin6->sin6_len = sizeof(*sin6);
			sin6->sin6_port = sh->src_port;
			memcpy(&sin6->sin6_addr, &p6->addr, sizeof(struct in6_addr));
			if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
				zero_address = 1;
			break;
		}
#endif
#ifdef INET
	case SCTP_IPV4_ADDRESS:
		{
			/* ipv4 address param */
			struct sctp_ipv4addr_param *p4, p4_buf;

			if (ntohs(phdr->param_length) != sizeof(struct sctp_ipv4addr_param)) {
				return NULL;
			}
			p4 = (struct sctp_ipv4addr_param *)sctp_get_next_param(m,
			    offset + sizeof(struct sctp_asconf_chunk),
			    &p4_buf.ph, sizeof(p4_buf));
			if (p4 == NULL) {
				SCTPDBG(SCTP_DEBUG_INPUT3, "%s: failed to get asconf v4 lookup addr\n",
				    __func__);
				return (NULL);
			}
			sin = &remote_store.sin;
			sin->sin_family = AF_INET;
			sin->sin_len = sizeof(*sin);
			sin->sin_port = sh->src_port;
			memcpy(&sin->sin_addr, &p4->addr, sizeof(struct in_addr));
			if (sin->sin_addr.s_addr == INADDR_ANY)
				zero_address = 1;
			break;
		}
#endif
	default:
		/* invalid address param type */
		return NULL;
	}

	if (zero_address) {
		stcb = sctp_findassoc_by_vtag(NULL, dst, ntohl(sh->v_tag), inp_p,
		    netp, sh->src_port, sh->dest_port, 1, vrf_id, 0);
		if (stcb != NULL) {
			SCTP_INP_DECR_REF(*inp_p);
		}
	} else {
		stcb = sctp_findassociation_ep_addr(inp_p,
		    &remote_store.sa, netp,
		    dst, NULL);
	}
	return (stcb);
}


/*
 * allocate a sctp_inpcb and setup a temporary binding to a port/all
 * addresses. This way if we don't get a bind we by default pick a ephemeral
 * port with all addresses bound.
 */
int
sctp_inpcb_alloc(struct socket *so, uint32_t vrf_id)
{
	/*
	 * we get called when a new endpoint starts up. We need to allocate
	 * the sctp_inpcb structure from the zone and init it. Mark it as
	 * unbound and find a port that we can use as an ephemeral with
	 * INADDR_ANY. If the user binds later no problem we can then add in
	 * the specific addresses. And setup the default parameters for the
	 * EP.
	 */
	int i, error;
	struct sctp_inpcb *inp;
	struct sctp_pcb *m;
	struct timeval time;
	sctp_sharedkey_t *null_key;

	error = 0;

	SCTP_INP_INFO_WLOCK();
	inp = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_ep), struct sctp_inpcb);
	if (inp == NULL) {
		SCTP_PRINTF("Out of SCTP-INPCB structures - no resources\n");
		SCTP_INP_INFO_WUNLOCK();
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, ENOBUFS);
		return (ENOBUFS);
	}
	/* zap it */
	memset(inp, 0, sizeof(*inp));

	/* bump generations */
	/* setup socket pointers */
	inp->sctp_socket = so;
	inp->ip_inp.inp.inp_socket = so;
	inp->ip_inp.inp.inp_cred = crhold(so->so_cred);
#ifdef INET6
	if (INP_SOCKAF(so) == AF_INET6) {
		if (MODULE_GLOBAL(ip6_auto_flowlabel)) {
			inp->ip_inp.inp.inp_flags |= IN6P_AUTOFLOWLABEL;
		}
		if (MODULE_GLOBAL(ip6_v6only)) {
			inp->ip_inp.inp.inp_flags |= IN6P_IPV6_V6ONLY;
		}
	}
#endif
	inp->sctp_associd_counter = 1;
	inp->partial_delivery_point = SCTP_SB_LIMIT_RCV(so) >> SCTP_PARTIAL_DELIVERY_SHIFT;
	inp->sctp_frag_point = SCTP_DEFAULT_MAXSEGMENT;
	inp->max_cwnd = 0;
	inp->sctp_cmt_on_off = SCTP_BASE_SYSCTL(sctp_cmt_on_off);
	inp->ecn_supported = (uint8_t)SCTP_BASE_SYSCTL(sctp_ecn_enable);
	inp->prsctp_supported = (uint8_t)SCTP_BASE_SYSCTL(sctp_pr_enable);
	inp->auth_supported = (uint8_t)SCTP_BASE_SYSCTL(sctp_auth_enable);
	inp->asconf_supported = (uint8_t)SCTP_BASE_SYSCTL(sctp_asconf_enable);
	inp->reconfig_supported = (uint8_t)SCTP_BASE_SYSCTL(sctp_reconfig_enable);
	inp->nrsack_supported = (uint8_t)SCTP_BASE_SYSCTL(sctp_nrsack_enable);
	inp->pktdrop_supported = (uint8_t)SCTP_BASE_SYSCTL(sctp_pktdrop_enable);
	inp->idata_supported = 0;

	inp->fibnum = so->so_fibnum;
	/* init the small hash table we use to track asocid <-> tcb */
	inp->sctp_asocidhash = SCTP_HASH_INIT(SCTP_STACK_VTAG_HASH_SIZE, &inp->hashasocidmark);
	if (inp->sctp_asocidhash == NULL) {
		crfree(inp->ip_inp.inp.inp_cred);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_ep), inp);
		SCTP_INP_INFO_WUNLOCK();
		return (ENOBUFS);
	}
	SCTP_INCR_EP_COUNT();
	inp->ip_inp.inp.inp_ip_ttl = MODULE_GLOBAL(ip_defttl);
	SCTP_INP_INFO_WUNLOCK();

	so->so_pcb = (caddr_t)inp;

	if (SCTP_SO_TYPE(so) == SOCK_SEQPACKET) {
		/* UDP style socket */
		inp->sctp_flags = (SCTP_PCB_FLAGS_UDPTYPE |
		    SCTP_PCB_FLAGS_UNBOUND);
		/* Be sure it is NON-BLOCKING IO for UDP */
		/* SCTP_SET_SO_NBIO(so); */
	} else if (SCTP_SO_TYPE(so) == SOCK_STREAM) {
		/* TCP style socket */
		inp->sctp_flags = (SCTP_PCB_FLAGS_TCPTYPE |
		    SCTP_PCB_FLAGS_UNBOUND);
		/* Be sure we have blocking IO by default */
		SOCK_LOCK(so);
		SCTP_CLEAR_SO_NBIO(so);
		SOCK_UNLOCK(so);
	} else {
		/*
		 * unsupported socket type (RAW, etc)- in case we missed it
		 * in protosw
		 */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EOPNOTSUPP);
		so->so_pcb = NULL;
		crfree(inp->ip_inp.inp.inp_cred);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_ep), inp);
		return (EOPNOTSUPP);
	}
	if (SCTP_BASE_SYSCTL(sctp_default_frag_interleave) == SCTP_FRAG_LEVEL_1) {
		sctp_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE);
		sctp_feature_off(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS);
	} else if (SCTP_BASE_SYSCTL(sctp_default_frag_interleave) == SCTP_FRAG_LEVEL_2) {
		sctp_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE);
		sctp_feature_on(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS);
	} else if (SCTP_BASE_SYSCTL(sctp_default_frag_interleave) == SCTP_FRAG_LEVEL_0) {
		sctp_feature_off(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE);
		sctp_feature_off(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS);
	}
	inp->sctp_tcbhash = SCTP_HASH_INIT(SCTP_BASE_SYSCTL(sctp_pcbtblsize),
	    &inp->sctp_hashmark);
	if (inp->sctp_tcbhash == NULL) {
		SCTP_PRINTF("Out of SCTP-INPCB->hashinit - no resources\n");
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, ENOBUFS);
		so->so_pcb = NULL;
		crfree(inp->ip_inp.inp.inp_cred);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_ep), inp);
		return (ENOBUFS);
	}
	inp->def_vrf_id = vrf_id;

	SCTP_INP_INFO_WLOCK();
	SCTP_INP_LOCK_INIT(inp);
	INP_LOCK_INIT(&inp->ip_inp.inp, "inp", "sctpinp");
	SCTP_INP_READ_INIT(inp);
	SCTP_ASOC_CREATE_LOCK_INIT(inp);
	/* lock the new ep */
	SCTP_INP_WLOCK(inp);

	/* add it to the info area */
	LIST_INSERT_HEAD(&SCTP_BASE_INFO(listhead), inp, sctp_list);
	SCTP_INP_INFO_WUNLOCK();

	TAILQ_INIT(&inp->read_queue);
	LIST_INIT(&inp->sctp_addr_list);

	LIST_INIT(&inp->sctp_asoc_list);

#ifdef SCTP_TRACK_FREED_ASOCS
	/* TEMP CODE */
	LIST_INIT(&inp->sctp_asoc_free_list);
#endif
	/* Init the timer structure for signature change */
	SCTP_OS_TIMER_INIT(&inp->sctp_ep.signature_change.timer);
	inp->sctp_ep.signature_change.type = SCTP_TIMER_TYPE_NEWCOOKIE;

	/* now init the actual endpoint default data */
	m = &inp->sctp_ep;

	/* setup the base timeout information */
	m->sctp_timeoutticks[SCTP_TIMER_SEND] = SEC_TO_TICKS(SCTP_SEND_SEC);	/* needed ? */
	m->sctp_timeoutticks[SCTP_TIMER_INIT] = SEC_TO_TICKS(SCTP_INIT_SEC);	/* needed ? */
	m->sctp_timeoutticks[SCTP_TIMER_RECV] = MSEC_TO_TICKS(SCTP_BASE_SYSCTL(sctp_delayed_sack_time_default));
	m->sctp_timeoutticks[SCTP_TIMER_HEARTBEAT] = MSEC_TO_TICKS(SCTP_BASE_SYSCTL(sctp_heartbeat_interval_default));
	m->sctp_timeoutticks[SCTP_TIMER_PMTU] = SEC_TO_TICKS(SCTP_BASE_SYSCTL(sctp_pmtu_raise_time_default));
	m->sctp_timeoutticks[SCTP_TIMER_MAXSHUTDOWN] = SEC_TO_TICKS(SCTP_BASE_SYSCTL(sctp_shutdown_guard_time_default));
	m->sctp_timeoutticks[SCTP_TIMER_SIGNATURE] = SEC_TO_TICKS(SCTP_BASE_SYSCTL(sctp_secret_lifetime_default));
	/* all max/min max are in ms */
	m->sctp_maxrto = SCTP_BASE_SYSCTL(sctp_rto_max_default);
	m->sctp_minrto = SCTP_BASE_SYSCTL(sctp_rto_min_default);
	m->initial_rto = SCTP_BASE_SYSCTL(sctp_rto_initial_default);
	m->initial_init_rto_max = SCTP_BASE_SYSCTL(sctp_init_rto_max_default);
	m->sctp_sack_freq = SCTP_BASE_SYSCTL(sctp_sack_freq_default);
	m->max_init_times = SCTP_BASE_SYSCTL(sctp_init_rtx_max_default);
	m->max_send_times = SCTP_BASE_SYSCTL(sctp_assoc_rtx_max_default);
	m->def_net_failure = SCTP_BASE_SYSCTL(sctp_path_rtx_max_default);
	m->def_net_pf_threshold = SCTP_BASE_SYSCTL(sctp_path_pf_threshold);
	m->sctp_sws_sender = SCTP_SWS_SENDER_DEF;
	m->sctp_sws_receiver = SCTP_SWS_RECEIVER_DEF;
	m->max_burst = SCTP_BASE_SYSCTL(sctp_max_burst_default);
	m->fr_max_burst = SCTP_BASE_SYSCTL(sctp_fr_max_burst_default);

	m->sctp_default_cc_module = SCTP_BASE_SYSCTL(sctp_default_cc_module);
	m->sctp_default_ss_module = SCTP_BASE_SYSCTL(sctp_default_ss_module);
	m->max_open_streams_intome = SCTP_BASE_SYSCTL(sctp_nr_incoming_streams_default);
	/* number of streams to pre-open on a association */
	m->pre_open_stream_count = SCTP_BASE_SYSCTL(sctp_nr_outgoing_streams_default);

	m->default_mtu = 0;
	/* Add adaptation cookie */
	m->adaptation_layer_indicator = 0;
	m->adaptation_layer_indicator_provided = 0;

	/* seed random number generator */
	m->random_counter = 1;
	m->store_at = SCTP_SIGNATURE_SIZE;
	SCTP_READ_RANDOM(m->random_numbers, sizeof(m->random_numbers));
	sctp_fill_random_store(m);

	/* Minimum cookie size */
	m->size_of_a_cookie = (sizeof(struct sctp_init_msg) * 2) +
	    sizeof(struct sctp_state_cookie);
	m->size_of_a_cookie += SCTP_SIGNATURE_SIZE;

	/* Setup the initial secret */
	(void)SCTP_GETTIME_TIMEVAL(&time);
	m->time_of_secret_change = time.tv_sec;

	for (i = 0; i < SCTP_NUMBER_OF_SECRETS; i++) {
		m->secret_key[0][i] = sctp_select_initial_TSN(m);
	}
	sctp_timer_start(SCTP_TIMER_TYPE_NEWCOOKIE, inp, NULL, NULL);

	/* How long is a cookie good for ? */
	m->def_cookie_life = MSEC_TO_TICKS(SCTP_BASE_SYSCTL(sctp_valid_cookie_life_default));
	/*
	 * Initialize authentication parameters
	 */
	m->local_hmacs = sctp_default_supported_hmaclist();
	m->local_auth_chunks = sctp_alloc_chunklist();
	if (inp->asconf_supported) {
		sctp_auth_add_chunk(SCTP_ASCONF, m->local_auth_chunks);
		sctp_auth_add_chunk(SCTP_ASCONF_ACK, m->local_auth_chunks);
	}
	m->default_dscp = 0;
#ifdef INET6
	m->default_flowlabel = 0;
#endif
	m->port = 0;		/* encapsulation disabled by default */
	LIST_INIT(&m->shared_keys);
	/* add default NULL key as key id 0 */
	null_key = sctp_alloc_sharedkey();
	sctp_insert_sharedkey(&m->shared_keys, null_key);
	SCTP_INP_WUNLOCK(inp);
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 12);
#endif
	return (error);
}


void
sctp_move_pcb_and_assoc(struct sctp_inpcb *old_inp, struct sctp_inpcb *new_inp,
    struct sctp_tcb *stcb)
{
	struct sctp_nets *net;
	uint16_t lport, rport;
	struct sctppcbhead *head;
	struct sctp_laddr *laddr, *oladdr;

	atomic_add_int(&stcb->asoc.refcnt, 1);
	SCTP_TCB_UNLOCK(stcb);
	SCTP_INP_INFO_WLOCK();
	SCTP_INP_WLOCK(old_inp);
	SCTP_INP_WLOCK(new_inp);
	SCTP_TCB_LOCK(stcb);
	atomic_subtract_int(&stcb->asoc.refcnt, 1);

	new_inp->sctp_ep.time_of_secret_change =
	    old_inp->sctp_ep.time_of_secret_change;
	memcpy(new_inp->sctp_ep.secret_key, old_inp->sctp_ep.secret_key,
	    sizeof(old_inp->sctp_ep.secret_key));
	new_inp->sctp_ep.current_secret_number =
	    old_inp->sctp_ep.current_secret_number;
	new_inp->sctp_ep.last_secret_number =
	    old_inp->sctp_ep.last_secret_number;
	new_inp->sctp_ep.size_of_a_cookie = old_inp->sctp_ep.size_of_a_cookie;

	/* make it so new data pours into the new socket */
	stcb->sctp_socket = new_inp->sctp_socket;
	stcb->sctp_ep = new_inp;

	/* Copy the port across */
	lport = new_inp->sctp_lport = old_inp->sctp_lport;
	rport = stcb->rport;
	/* Pull the tcb from the old association */
	LIST_REMOVE(stcb, sctp_tcbhash);
	LIST_REMOVE(stcb, sctp_tcblist);
	if (stcb->asoc.in_asocid_hash) {
		LIST_REMOVE(stcb, sctp_tcbasocidhash);
	}
	/* Now insert the new_inp into the TCP connected hash */
	head = &SCTP_BASE_INFO(sctp_tcpephash)[SCTP_PCBHASH_ALLADDR((lport | rport), SCTP_BASE_INFO(hashtcpmark))];

	LIST_INSERT_HEAD(head, new_inp, sctp_hash);
	/* Its safe to access */
	new_inp->sctp_flags &= ~SCTP_PCB_FLAGS_UNBOUND;

	/* Now move the tcb into the endpoint list */
	LIST_INSERT_HEAD(&new_inp->sctp_asoc_list, stcb, sctp_tcblist);
	/*
	 * Question, do we even need to worry about the ep-hash since we
	 * only have one connection? Probably not :> so lets get rid of it
	 * and not suck up any kernel memory in that.
	 */
	if (stcb->asoc.in_asocid_hash) {
		struct sctpasochead *lhd;

		lhd = &new_inp->sctp_asocidhash[SCTP_PCBHASH_ASOC(stcb->asoc.assoc_id,
		    new_inp->hashasocidmark)];
		LIST_INSERT_HEAD(lhd, stcb, sctp_tcbasocidhash);
	}
	/* Ok. Let's restart timer. */
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, new_inp,
		    stcb, net);
	}

	SCTP_INP_INFO_WUNLOCK();
	if (new_inp->sctp_tcbhash != NULL) {
		SCTP_HASH_FREE(new_inp->sctp_tcbhash, new_inp->sctp_hashmark);
		new_inp->sctp_tcbhash = NULL;
	}
	if ((new_inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) == 0) {
		/* Subset bound, so copy in the laddr list from the old_inp */
		LIST_FOREACH(oladdr, &old_inp->sctp_addr_list, sctp_nxt_addr) {
			laddr = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_laddr), struct sctp_laddr);
			if (laddr == NULL) {
				/*
				 * Gak, what can we do? This assoc is really
				 * HOSED. We probably should send an abort
				 * here.
				 */
				SCTPDBG(SCTP_DEBUG_PCB1, "Association hosed in TCP model, out of laddr memory\n");
				continue;
			}
			SCTP_INCR_LADDR_COUNT();
			memset(laddr, 0, sizeof(*laddr));
			(void)SCTP_GETTIME_TIMEVAL(&laddr->start_time);
			laddr->ifa = oladdr->ifa;
			atomic_add_int(&laddr->ifa->refcount, 1);
			LIST_INSERT_HEAD(&new_inp->sctp_addr_list, laddr,
			    sctp_nxt_addr);
			new_inp->laddr_count++;
			if (oladdr == stcb->asoc.last_used_address) {
				stcb->asoc.last_used_address = laddr;
			}
		}
	}
	/*
	 * Now any running timers need to be adjusted since we really don't
	 * care if they are running or not just blast in the new_inp into
	 * all of them.
	 */

	stcb->asoc.dack_timer.ep = (void *)new_inp;
	stcb->asoc.asconf_timer.ep = (void *)new_inp;
	stcb->asoc.strreset_timer.ep = (void *)new_inp;
	stcb->asoc.shut_guard_timer.ep = (void *)new_inp;
	stcb->asoc.autoclose_timer.ep = (void *)new_inp;
	stcb->asoc.delayed_event_timer.ep = (void *)new_inp;
	stcb->asoc.delete_prim_timer.ep = (void *)new_inp;
	/* now what about the nets? */
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		net->pmtu_timer.ep = (void *)new_inp;
		net->hb_timer.ep = (void *)new_inp;
		net->rxt_timer.ep = (void *)new_inp;
	}
	SCTP_INP_WUNLOCK(new_inp);
	SCTP_INP_WUNLOCK(old_inp);
}

/*
 * insert an laddr entry with the given ifa for the desired list
 */
static int
sctp_insert_laddr(struct sctpladdr *list, struct sctp_ifa *ifa, uint32_t act)
{
	struct sctp_laddr *laddr;

	laddr = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_laddr), struct sctp_laddr);
	if (laddr == NULL) {
		/* out of memory? */
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
		return (EINVAL);
	}
	SCTP_INCR_LADDR_COUNT();
	memset(laddr, 0, sizeof(*laddr));
	(void)SCTP_GETTIME_TIMEVAL(&laddr->start_time);
	laddr->ifa = ifa;
	laddr->action = act;
	atomic_add_int(&ifa->refcount, 1);
	/* insert it */
	LIST_INSERT_HEAD(list, laddr, sctp_nxt_addr);

	return (0);
}

/*
 * Remove an laddr entry from the local address list (on an assoc)
 */
static void
sctp_remove_laddr(struct sctp_laddr *laddr)
{

	/* remove from the list */
	LIST_REMOVE(laddr, sctp_nxt_addr);
	sctp_free_ifa(laddr->ifa);
	SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_laddr), laddr);
	SCTP_DECR_LADDR_COUNT();
}



/* sctp_ifap is used to bypass normal local address validation checks */
int
sctp_inpcb_bind(struct socket *so, struct sockaddr *addr,
    struct sctp_ifa *sctp_ifap, struct thread *p)
{
	/* bind a ep to a socket address */
	struct sctppcbhead *head;
	struct sctp_inpcb *inp, *inp_tmp;
	struct inpcb *ip_inp;
	int port_reuse_active = 0;
	int bindall;
	uint16_t lport;
	int error;
	uint32_t vrf_id;

	lport = 0;
	bindall = 1;
	inp = (struct sctp_inpcb *)so->so_pcb;
	ip_inp = (struct inpcb *)so->so_pcb;
#ifdef SCTP_DEBUG
	if (addr) {
		SCTPDBG(SCTP_DEBUG_PCB1, "Bind called port: %d\n",
		    ntohs(((struct sockaddr_in *)addr)->sin_port));
		SCTPDBG(SCTP_DEBUG_PCB1, "Addr: ");
		SCTPDBG_ADDR(SCTP_DEBUG_PCB1, addr);
	}
#endif
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) == 0) {
		/* already did a bind, subsequent binds NOT allowed ! */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
		return (EINVAL);
	}
#ifdef INVARIANTS
	if (p == NULL)
		panic("null proc/thread");
#endif
	if (addr != NULL) {
		switch (addr->sa_family) {
#ifdef INET
		case AF_INET:
			{
				struct sockaddr_in *sin;

				/* IPV6_V6ONLY socket? */
				if (SCTP_IPV6_V6ONLY(ip_inp)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
					return (EINVAL);
				}
				if (addr->sa_len != sizeof(*sin)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
					return (EINVAL);
				}

				sin = (struct sockaddr_in *)addr;
				lport = sin->sin_port;
				/*
				 * For LOOPBACK the prison_local_ip4() call
				 * will transmute the ip address to the
				 * proper value.
				 */
				if (p && (error = prison_local_ip4(p->td_ucred, &sin->sin_addr)) != 0) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, error);
					return (error);
				}
				if (sin->sin_addr.s_addr != INADDR_ANY) {
					bindall = 0;
				}
				break;
			}
#endif
#ifdef INET6
		case AF_INET6:
			{
				/*
				 * Only for pure IPv6 Address. (No IPv4
				 * Mapped!)
				 */
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)addr;

				if (addr->sa_len != sizeof(*sin6)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
					return (EINVAL);
				}
				lport = sin6->sin6_port;
				/*
				 * For LOOPBACK the prison_local_ip6() call
				 * will transmute the ipv6 address to the
				 * proper value.
				 */
				if (p && (error = prison_local_ip6(p->td_ucred, &sin6->sin6_addr,
				    (SCTP_IPV6_V6ONLY(inp) != 0))) != 0) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, error);
					return (error);
				}
				if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					bindall = 0;
					/* KAME hack: embed scopeid */
					if (sa6_embedscope(sin6, MODULE_GLOBAL(ip6_use_defzone)) != 0) {
						SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
						return (EINVAL);
					}
				}
				/* this must be cleared for ifa_ifwithaddr() */
				sin6->sin6_scope_id = 0;
				break;
			}
#endif
		default:
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EAFNOSUPPORT);
			return (EAFNOSUPPORT);
		}
	}
	SCTP_INP_INFO_WLOCK();
	SCTP_INP_WLOCK(inp);
	/* Setup a vrf_id to be the default for the non-bind-all case. */
	vrf_id = inp->def_vrf_id;

	/* increase our count due to the unlock we do */
	SCTP_INP_INCR_REF(inp);
	if (lport) {
		/*
		 * Did the caller specify a port? if so we must see if an ep
		 * already has this one bound.
		 */
		/* got to be root to get at low ports */
		if (ntohs(lport) < IPPORT_RESERVED) {
			if ((p != NULL) && ((error =
			    priv_check(p, PRIV_NETINET_RESERVEDPORT)
			    ) != 0)) {
				SCTP_INP_DECR_REF(inp);
				SCTP_INP_WUNLOCK(inp);
				SCTP_INP_INFO_WUNLOCK();
				return (error);
			}
		}
		SCTP_INP_WUNLOCK(inp);
		if (bindall) {
			vrf_id = inp->def_vrf_id;
			inp_tmp = sctp_pcb_findep(addr, 0, 1, vrf_id);
			if (inp_tmp != NULL) {
				/*
				 * lock guy returned and lower count note
				 * that we are not bound so inp_tmp should
				 * NEVER be inp. And it is this inp
				 * (inp_tmp) that gets the reference bump,
				 * so we must lower it.
				 */
				SCTP_INP_DECR_REF(inp_tmp);
				/* unlock info */
				if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_PORTREUSE)) &&
				    (sctp_is_feature_on(inp_tmp, SCTP_PCB_FLAGS_PORTREUSE))) {
					/*
					 * Ok, must be one-2-one and
					 * allowing port re-use
					 */
					port_reuse_active = 1;
					goto continue_anyway;
				}
				SCTP_INP_DECR_REF(inp);
				SCTP_INP_INFO_WUNLOCK();
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EADDRINUSE);
				return (EADDRINUSE);
			}
		} else {
			inp_tmp = sctp_pcb_findep(addr, 0, 1, vrf_id);
			if (inp_tmp != NULL) {
				/*
				 * lock guy returned and lower count note
				 * that we are not bound so inp_tmp should
				 * NEVER be inp. And it is this inp
				 * (inp_tmp) that gets the reference bump,
				 * so we must lower it.
				 */
				SCTP_INP_DECR_REF(inp_tmp);
				/* unlock info */
				if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_PORTREUSE)) &&
				    (sctp_is_feature_on(inp_tmp, SCTP_PCB_FLAGS_PORTREUSE))) {
					/*
					 * Ok, must be one-2-one and
					 * allowing port re-use
					 */
					port_reuse_active = 1;
					goto continue_anyway;
				}
				SCTP_INP_DECR_REF(inp);
				SCTP_INP_INFO_WUNLOCK();
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EADDRINUSE);
				return (EADDRINUSE);
			}
		}
continue_anyway:
		SCTP_INP_WLOCK(inp);
		if (bindall) {
			/* verify that no lport is not used by a singleton */
			if ((port_reuse_active == 0) &&
			    (inp_tmp = sctp_isport_inuse(inp, lport, vrf_id))) {
				/* Sorry someone already has this one bound */
				if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_PORTREUSE)) &&
				    (sctp_is_feature_on(inp_tmp, SCTP_PCB_FLAGS_PORTREUSE))) {
					port_reuse_active = 1;
				} else {
					SCTP_INP_DECR_REF(inp);
					SCTP_INP_WUNLOCK(inp);
					SCTP_INP_INFO_WUNLOCK();
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EADDRINUSE);
					return (EADDRINUSE);
				}
			}
		}
	} else {
		uint16_t first, last, candidate;
		uint16_t count;
		int done;

		if (ip_inp->inp_flags & INP_HIGHPORT) {
			first = MODULE_GLOBAL(ipport_hifirstauto);
			last = MODULE_GLOBAL(ipport_hilastauto);
		} else if (ip_inp->inp_flags & INP_LOWPORT) {
			if (p && (error =
			    priv_check(p, PRIV_NETINET_RESERVEDPORT)
			    )) {
				SCTP_INP_DECR_REF(inp);
				SCTP_INP_WUNLOCK(inp);
				SCTP_INP_INFO_WUNLOCK();
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, error);
				return (error);
			}
			first = MODULE_GLOBAL(ipport_lowfirstauto);
			last = MODULE_GLOBAL(ipport_lowlastauto);
		} else {
			first = MODULE_GLOBAL(ipport_firstauto);
			last = MODULE_GLOBAL(ipport_lastauto);
		}
		if (first > last) {
			uint16_t temp;

			temp = first;
			first = last;
			last = temp;
		}
		count = last - first + 1;	/* number of candidates */
		candidate = first + sctp_select_initial_TSN(&inp->sctp_ep) % (count);

		done = 0;
		while (!done) {
			if (sctp_isport_inuse(inp, htons(candidate), inp->def_vrf_id) == NULL) {
				done = 1;
			}
			if (!done) {
				if (--count == 0) {
					SCTP_INP_DECR_REF(inp);
					SCTP_INP_WUNLOCK(inp);
					SCTP_INP_INFO_WUNLOCK();
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EADDRINUSE);
					return (EADDRINUSE);
				}
				if (candidate == last)
					candidate = first;
				else
					candidate = candidate + 1;
			}
		}
		lport = htons(candidate);
	}
	SCTP_INP_DECR_REF(inp);
	if (inp->sctp_flags & (SCTP_PCB_FLAGS_SOCKET_GONE |
	    SCTP_PCB_FLAGS_SOCKET_ALLGONE)) {
		/*
		 * this really should not happen. The guy did a non-blocking
		 * bind and then did a close at the same time.
		 */
		SCTP_INP_WUNLOCK(inp);
		SCTP_INP_INFO_WUNLOCK();
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
		return (EINVAL);
	}
	/* ok we look clear to give out this port, so lets setup the binding */
	if (bindall) {
		/* binding to all addresses, so just set in the proper flags */
		inp->sctp_flags |= SCTP_PCB_FLAGS_BOUNDALL;
		/* set the automatic addr changes from kernel flag */
		if (SCTP_BASE_SYSCTL(sctp_auto_asconf) == 0) {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_DO_ASCONF);
			sctp_feature_off(inp, SCTP_PCB_FLAGS_AUTO_ASCONF);
		} else {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_DO_ASCONF);
			sctp_feature_on(inp, SCTP_PCB_FLAGS_AUTO_ASCONF);
		}
		if (SCTP_BASE_SYSCTL(sctp_multiple_asconfs) == 0) {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_MULTIPLE_ASCONFS);
		} else {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_MULTIPLE_ASCONFS);
		}
		/*
		 * set the automatic mobility_base from kernel flag (by
		 * micchie)
		 */
		if (SCTP_BASE_SYSCTL(sctp_mobility_base) == 0) {
			sctp_mobility_feature_off(inp, SCTP_MOBILITY_BASE);
			sctp_mobility_feature_off(inp, SCTP_MOBILITY_PRIM_DELETED);
		} else {
			sctp_mobility_feature_on(inp, SCTP_MOBILITY_BASE);
			sctp_mobility_feature_off(inp, SCTP_MOBILITY_PRIM_DELETED);
		}
		/*
		 * set the automatic mobility_fasthandoff from kernel flag
		 * (by micchie)
		 */
		if (SCTP_BASE_SYSCTL(sctp_mobility_fasthandoff) == 0) {
			sctp_mobility_feature_off(inp, SCTP_MOBILITY_FASTHANDOFF);
			sctp_mobility_feature_off(inp, SCTP_MOBILITY_PRIM_DELETED);
		} else {
			sctp_mobility_feature_on(inp, SCTP_MOBILITY_FASTHANDOFF);
			sctp_mobility_feature_off(inp, SCTP_MOBILITY_PRIM_DELETED);
		}
	} else {
		/*
		 * bind specific, make sure flags is off and add a new
		 * address structure to the sctp_addr_list inside the ep
		 * structure.
		 *
		 * We will need to allocate one and insert it at the head.
		 * The socketopt call can just insert new addresses in there
		 * as well. It will also have to do the embed scope kame
		 * hack too (before adding).
		 */
		struct sctp_ifa *ifa;
		union sctp_sockstore store;

		memset(&store, 0, sizeof(store));
		switch (addr->sa_family) {
#ifdef INET
		case AF_INET:
			memcpy(&store.sin, addr, sizeof(struct sockaddr_in));
			store.sin.sin_port = 0;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			memcpy(&store.sin6, addr, sizeof(struct sockaddr_in6));
			store.sin6.sin6_port = 0;
			break;
#endif
		default:
			break;
		}
		/*
		 * first find the interface with the bound address need to
		 * zero out the port to find the address! yuck! can't do
		 * this earlier since need port for sctp_pcb_findep()
		 */
		if (sctp_ifap != NULL) {
			ifa = sctp_ifap;
		} else {
			/*
			 * Note for BSD we hit here always other O/S's will
			 * pass things in via the sctp_ifap argument
			 * (Panda).
			 */
			ifa = sctp_find_ifa_by_addr(&store.sa,
			    vrf_id, SCTP_ADDR_NOT_LOCKED);
		}
		if (ifa == NULL) {
			/* Can't find an interface with that address */
			SCTP_INP_WUNLOCK(inp);
			SCTP_INP_INFO_WUNLOCK();
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EADDRNOTAVAIL);
			return (EADDRNOTAVAIL);
		}
#ifdef INET6
		if (addr->sa_family == AF_INET6) {
			/* GAK, more FIXME IFA lock? */
			if (ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
				/* Can't bind a non-existent addr. */
				SCTP_INP_WUNLOCK(inp);
				SCTP_INP_INFO_WUNLOCK();
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
				return (EINVAL);
			}
		}
#endif
		/* we're not bound all */
		inp->sctp_flags &= ~SCTP_PCB_FLAGS_BOUNDALL;
		/* allow bindx() to send ASCONF's for binding changes */
		sctp_feature_on(inp, SCTP_PCB_FLAGS_DO_ASCONF);
		/* clear automatic addr changes from kernel flag */
		sctp_feature_off(inp, SCTP_PCB_FLAGS_AUTO_ASCONF);

		/* add this address to the endpoint list */
		error = sctp_insert_laddr(&inp->sctp_addr_list, ifa, 0);
		if (error != 0) {
			SCTP_INP_WUNLOCK(inp);
			SCTP_INP_INFO_WUNLOCK();
			return (error);
		}
		inp->laddr_count++;
	}
	/* find the bucket */
	if (port_reuse_active) {
		/* Put it into tcp 1-2-1 hash */
		head = &SCTP_BASE_INFO(sctp_tcpephash)[SCTP_PCBHASH_ALLADDR(lport, SCTP_BASE_INFO(hashtcpmark))];
		inp->sctp_flags |= SCTP_PCB_FLAGS_IN_TCPPOOL;
	} else {
		head = &SCTP_BASE_INFO(sctp_ephash)[SCTP_PCBHASH_ALLADDR(lport, SCTP_BASE_INFO(hashmark))];
	}
	/* put it in the bucket */
	LIST_INSERT_HEAD(head, inp, sctp_hash);
	SCTPDBG(SCTP_DEBUG_PCB1, "Main hash to bind at head:%p, bound port:%d - in tcp_pool=%d\n",
	    (void *)head, ntohs(lport), port_reuse_active);
	/* set in the port */
	inp->sctp_lport = lport;

	/* turn off just the unbound flag */
	inp->sctp_flags &= ~SCTP_PCB_FLAGS_UNBOUND;
	SCTP_INP_WUNLOCK(inp);
	SCTP_INP_INFO_WUNLOCK();
	return (0);
}


static void
sctp_iterator_inp_being_freed(struct sctp_inpcb *inp)
{
	struct sctp_iterator *it, *nit;

	/*
	 * We enter with the only the ITERATOR_LOCK in place and a write
	 * lock on the inp_info stuff.
	 */
	it = sctp_it_ctl.cur_it;
	if (it && (it->vn != curvnet)) {
		/* Its not looking at our VNET */
		return;
	}
	if (it && (it->inp == inp)) {
		/*
		 * This is tricky and we hold the iterator lock, but when it
		 * returns and gets the lock (when we release it) the
		 * iterator will try to operate on inp. We need to stop that
		 * from happening. But of course the iterator has a
		 * reference on the stcb and inp. We can mark it and it will
		 * stop.
		 *
		 * If its a single iterator situation, we set the end
		 * iterator flag. Otherwise we set the iterator to go to the
		 * next inp.
		 *
		 */
		if (it->iterator_flags & SCTP_ITERATOR_DO_SINGLE_INP) {
			sctp_it_ctl.iterator_flags |= SCTP_ITERATOR_STOP_CUR_IT;
		} else {
			sctp_it_ctl.iterator_flags |= SCTP_ITERATOR_STOP_CUR_INP;
		}
	}
	/*
	 * Now go through and remove any single reference to our inp that
	 * may be still pending on the list
	 */
	SCTP_IPI_ITERATOR_WQ_LOCK();
	TAILQ_FOREACH_SAFE(it, &sctp_it_ctl.iteratorhead, sctp_nxt_itr, nit) {
		if (it->vn != curvnet) {
			continue;
		}
		if (it->inp == inp) {
			/* This one points to me is it inp specific? */
			if (it->iterator_flags & SCTP_ITERATOR_DO_SINGLE_INP) {
				/* Remove and free this one */
				TAILQ_REMOVE(&sctp_it_ctl.iteratorhead,
				    it, sctp_nxt_itr);
				if (it->function_atend != NULL) {
					(*it->function_atend) (it->pointer, it->val);
				}
				SCTP_FREE(it, SCTP_M_ITER);
			} else {
				it->inp = LIST_NEXT(it->inp, sctp_list);
				if (it->inp) {
					SCTP_INP_INCR_REF(it->inp);
				}
			}
			/*
			 * When its put in the refcnt is incremented so decr
			 * it
			 */
			SCTP_INP_DECR_REF(inp);
		}
	}
	SCTP_IPI_ITERATOR_WQ_UNLOCK();
}

/* release sctp_inpcb unbind the port */
void
sctp_inpcb_free(struct sctp_inpcb *inp, int immediate, int from)
{
	/*
	 * Here we free a endpoint. We must find it (if it is in the Hash
	 * table) and remove it from there. Then we must also find it in the
	 * overall list and remove it from there. After all removals are
	 * complete then any timer has to be stopped. Then start the actual
	 * freeing. a) Any local lists. b) Any associations. c) The hash of
	 * all associations. d) finally the ep itself.
	 */
	struct sctp_tcb *asoc, *nasoc;
	struct sctp_laddr *laddr, *nladdr;
	struct inpcb *ip_pcb;
	struct socket *so;
	int being_refed = 0;
	struct sctp_queued_to_read *sq, *nsq;
	int cnt;
	sctp_sharedkey_t *shared_key, *nshared_key;


#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 0);
#endif
	SCTP_ITERATOR_LOCK();
	/* mark any iterators on the list or being processed */
	sctp_iterator_inp_being_freed(inp);
	SCTP_ITERATOR_UNLOCK();
	so = inp->sctp_socket;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
		/* been here before.. eeks.. get out of here */
		SCTP_PRINTF("This conflict in free SHOULD not be happening! from %d, imm %d\n", from, immediate);
#ifdef SCTP_LOG_CLOSING
		sctp_log_closing(inp, NULL, 1);
#endif
		return;
	}
	SCTP_ASOC_CREATE_LOCK(inp);
	SCTP_INP_INFO_WLOCK();

	SCTP_INP_WLOCK(inp);
	if (from == SCTP_CALLED_AFTER_CMPSET_OFCLOSE) {
		inp->sctp_flags &= ~SCTP_PCB_FLAGS_CLOSE_IP;
		/* socket is gone, so no more wakeups allowed */
		inp->sctp_flags |= SCTP_PCB_FLAGS_DONT_WAKE;
		inp->sctp_flags &= ~SCTP_PCB_FLAGS_WAKEINPUT;
		inp->sctp_flags &= ~SCTP_PCB_FLAGS_WAKEOUTPUT;

	}
	/* First time through we have the socket lock, after that no more. */
	sctp_timer_stop(SCTP_TIMER_TYPE_NEWCOOKIE, inp, NULL, NULL,
	    SCTP_FROM_SCTP_PCB + SCTP_LOC_1);

	if (inp->control) {
		sctp_m_freem(inp->control);
		inp->control = NULL;
	}
	if (inp->pkt) {
		sctp_m_freem(inp->pkt);
		inp->pkt = NULL;
	}
	ip_pcb = &inp->ip_inp.inp;	/* we could just cast the main pointer
					 * here but I will be nice :> (i.e.
					 * ip_pcb = ep;) */
	if (immediate == SCTP_FREE_SHOULD_USE_GRACEFUL_CLOSE) {
		int cnt_in_sd;

		cnt_in_sd = 0;
		LIST_FOREACH_SAFE(asoc, &inp->sctp_asoc_list, sctp_tcblist, nasoc) {
			SCTP_TCB_LOCK(asoc);
			if (asoc->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				/* Skip guys being freed */
				cnt_in_sd++;
				if (asoc->asoc.state & SCTP_STATE_IN_ACCEPT_QUEUE) {
					/*
					 * Special case - we did not start a
					 * kill timer on the asoc due to it
					 * was not closed. So go ahead and
					 * start it now.
					 */
					SCTP_CLEAR_SUBSTATE(asoc, SCTP_STATE_IN_ACCEPT_QUEUE);
					sctp_timer_start(SCTP_TIMER_TYPE_ASOCKILL, inp, asoc, NULL);
				}
				SCTP_TCB_UNLOCK(asoc);
				continue;
			}
			if (((SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_WAIT) ||
			    (SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_ECHOED)) &&
			    (asoc->asoc.total_output_queue_size == 0)) {
				/*
				 * If we have data in queue, we don't want
				 * to just free since the app may have done,
				 * send()/close or connect/send/close. And
				 * it wants the data to get across first.
				 */
				/* Just abandon things in the front states */
				if (sctp_free_assoc(inp, asoc, SCTP_PCBFREE_NOFORCE,
				    SCTP_FROM_SCTP_PCB + SCTP_LOC_2) == 0) {
					cnt_in_sd++;
				}
				continue;
			}
			/* Disconnect the socket please */
			asoc->sctp_socket = NULL;
			SCTP_ADD_SUBSTATE(asoc, SCTP_STATE_CLOSED_SOCKET);
			if ((asoc->asoc.size_on_reasm_queue > 0) ||
			    (asoc->asoc.control_pdapi) ||
			    (asoc->asoc.size_on_all_streams > 0) ||
			    (so && (so->so_rcv.sb_cc > 0))) {
				/* Left with Data unread */
				struct mbuf *op_err;

				op_err = sctp_generate_cause(SCTP_CAUSE_USER_INITIATED_ABT, "");
				asoc->sctp_ep->last_abort_code = SCTP_FROM_SCTP_PCB + SCTP_LOC_3;
				sctp_send_abort_tcb(asoc, op_err, SCTP_SO_LOCKED);
				SCTP_STAT_INCR_COUNTER32(sctps_aborted);
				if ((SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) ||
				    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
					SCTP_STAT_DECR_GAUGE32(sctps_currestab);
				}
				if (sctp_free_assoc(inp, asoc,
				    SCTP_PCBFREE_NOFORCE, SCTP_FROM_SCTP_PCB + SCTP_LOC_4) == 0) {
					cnt_in_sd++;
				}
				continue;
			} else if (TAILQ_EMPTY(&asoc->asoc.send_queue) &&
				    TAILQ_EMPTY(&asoc->asoc.sent_queue) &&
			    (asoc->asoc.stream_queue_cnt == 0)) {
				if ((*asoc->asoc.ss_functions.sctp_ss_is_user_msgs_incomplete) (asoc, &asoc->asoc)) {
					goto abort_anyway;
				}
				if ((SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_SENT) &&
				    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_ACK_SENT)) {
					struct sctp_nets *netp;

					/*
					 * there is nothing queued to send,
					 * so I send shutdown
					 */
					if ((SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) ||
					    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
						SCTP_STAT_DECR_GAUGE32(sctps_currestab);
					}
					SCTP_SET_STATE(asoc, SCTP_STATE_SHUTDOWN_SENT);
					sctp_stop_timers_for_shutdown(asoc);
					if (asoc->asoc.alternate) {
						netp = asoc->asoc.alternate;
					} else {
						netp = asoc->asoc.primary_destination;
					}
					sctp_send_shutdown(asoc, netp);
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN, asoc->sctp_ep, asoc,
					    netp);
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, asoc->sctp_ep, asoc,
					    asoc->asoc.primary_destination);
					sctp_chunk_output(inp, asoc, SCTP_OUTPUT_FROM_SHUT_TMR, SCTP_SO_LOCKED);
				}
			} else {
				/* mark into shutdown pending */
				SCTP_ADD_SUBSTATE(asoc, SCTP_STATE_SHUTDOWN_PENDING);
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, asoc->sctp_ep, asoc,
				    asoc->asoc.primary_destination);
				if ((*asoc->asoc.ss_functions.sctp_ss_is_user_msgs_incomplete) (asoc, &asoc->asoc)) {
					SCTP_ADD_SUBSTATE(asoc, SCTP_STATE_PARTIAL_MSG_LEFT);
				}
				if (TAILQ_EMPTY(&asoc->asoc.send_queue) &&
				    TAILQ_EMPTY(&asoc->asoc.sent_queue) &&
				    (asoc->asoc.state & SCTP_STATE_PARTIAL_MSG_LEFT)) {
					struct mbuf *op_err;

			abort_anyway:
					op_err = sctp_generate_cause(SCTP_CAUSE_USER_INITIATED_ABT, "");
					asoc->sctp_ep->last_abort_code = SCTP_FROM_SCTP_PCB + SCTP_LOC_5;
					sctp_send_abort_tcb(asoc, op_err, SCTP_SO_LOCKED);
					SCTP_STAT_INCR_COUNTER32(sctps_aborted);
					if ((SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) ||
					    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
						SCTP_STAT_DECR_GAUGE32(sctps_currestab);
					}
					if (sctp_free_assoc(inp, asoc,
					    SCTP_PCBFREE_NOFORCE,
					    SCTP_FROM_SCTP_PCB + SCTP_LOC_6) == 0) {
						cnt_in_sd++;
					}
					continue;
				} else {
					sctp_chunk_output(inp, asoc, SCTP_OUTPUT_FROM_CLOSING, SCTP_SO_LOCKED);
				}
			}
			cnt_in_sd++;
			SCTP_TCB_UNLOCK(asoc);
		}
		/* now is there some left in our SHUTDOWN state? */
		if (cnt_in_sd) {
#ifdef SCTP_LOG_CLOSING
			sctp_log_closing(inp, NULL, 2);
#endif
			inp->sctp_socket = NULL;
			SCTP_INP_WUNLOCK(inp);
			SCTP_ASOC_CREATE_UNLOCK(inp);
			SCTP_INP_INFO_WUNLOCK();
			return;
		}
	}
	inp->sctp_socket = NULL;
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) !=
	    SCTP_PCB_FLAGS_UNBOUND) {
		/*
		 * ok, this guy has been bound. It's port is somewhere in
		 * the SCTP_BASE_INFO(hash table). Remove it!
		 */
		LIST_REMOVE(inp, sctp_hash);
		inp->sctp_flags |= SCTP_PCB_FLAGS_UNBOUND;
	}

	/*
	 * If there is a timer running to kill us, forget it, since it may
	 * have a contest on the INP lock.. which would cause us to die ...
	 */
	cnt = 0;
	LIST_FOREACH_SAFE(asoc, &inp->sctp_asoc_list, sctp_tcblist, nasoc) {
		SCTP_TCB_LOCK(asoc);
		if (asoc->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
			if (asoc->asoc.state & SCTP_STATE_IN_ACCEPT_QUEUE) {
				SCTP_CLEAR_SUBSTATE(asoc, SCTP_STATE_IN_ACCEPT_QUEUE);
				sctp_timer_start(SCTP_TIMER_TYPE_ASOCKILL, inp, asoc, NULL);
			}
			cnt++;
			SCTP_TCB_UNLOCK(asoc);
			continue;
		}
		/* Free associations that are NOT killing us */
		if ((SCTP_GET_STATE(asoc) != SCTP_STATE_COOKIE_WAIT) &&
		    ((asoc->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) == 0)) {
			struct mbuf *op_err;

			op_err = sctp_generate_cause(SCTP_CAUSE_USER_INITIATED_ABT, "");
			asoc->sctp_ep->last_abort_code = SCTP_FROM_SCTP_PCB + SCTP_LOC_7;
			sctp_send_abort_tcb(asoc, op_err, SCTP_SO_LOCKED);
			SCTP_STAT_INCR_COUNTER32(sctps_aborted);
		} else if (asoc->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
			cnt++;
			SCTP_TCB_UNLOCK(asoc);
			continue;
		}
		if ((SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) ||
		    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
			SCTP_STAT_DECR_GAUGE32(sctps_currestab);
		}
		if (sctp_free_assoc(inp, asoc, SCTP_PCBFREE_FORCE,
		    SCTP_FROM_SCTP_PCB + SCTP_LOC_8) == 0) {
			cnt++;
		}
	}
	if (cnt) {
		/* Ok we have someone out there that will kill us */
		(void)SCTP_OS_TIMER_STOP(&inp->sctp_ep.signature_change.timer);
#ifdef SCTP_LOG_CLOSING
		sctp_log_closing(inp, NULL, 3);
#endif
		SCTP_INP_WUNLOCK(inp);
		SCTP_ASOC_CREATE_UNLOCK(inp);
		SCTP_INP_INFO_WUNLOCK();
		return;
	}
	if (SCTP_INP_LOCK_CONTENDED(inp))
		being_refed++;
	if (SCTP_INP_READ_CONTENDED(inp))
		being_refed++;
	if (SCTP_ASOC_CREATE_LOCK_CONTENDED(inp))
		being_refed++;

	if ((inp->refcount) ||
	    (being_refed) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CLOSE_IP)) {
		(void)SCTP_OS_TIMER_STOP(&inp->sctp_ep.signature_change.timer);
#ifdef SCTP_LOG_CLOSING
		sctp_log_closing(inp, NULL, 4);
#endif
		sctp_timer_start(SCTP_TIMER_TYPE_INPKILL, inp, NULL, NULL);
		SCTP_INP_WUNLOCK(inp);
		SCTP_ASOC_CREATE_UNLOCK(inp);
		SCTP_INP_INFO_WUNLOCK();
		return;
	}
	inp->sctp_ep.signature_change.type = 0;
	inp->sctp_flags |= SCTP_PCB_FLAGS_SOCKET_ALLGONE;
	/*
	 * Remove it from the list .. last thing we need a lock for.
	 */
	LIST_REMOVE(inp, sctp_list);
	SCTP_INP_WUNLOCK(inp);
	SCTP_ASOC_CREATE_UNLOCK(inp);
	SCTP_INP_INFO_WUNLOCK();
	/*
	 * Now we release all locks. Since this INP cannot be found anymore
	 * except possibly by the kill timer that might be running. We call
	 * the drain function here. It should hit the case were it sees the
	 * ACTIVE flag cleared and exit out freeing us to proceed and
	 * destroy everything.
	 */
	if (from != SCTP_CALLED_FROM_INPKILL_TIMER) {
		(void)SCTP_OS_TIMER_STOP_DRAIN(&inp->sctp_ep.signature_change.timer);
	} else {
		/* Probably un-needed */
		(void)SCTP_OS_TIMER_STOP(&inp->sctp_ep.signature_change.timer);
	}

#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 5);
#endif


	if ((inp->sctp_asocidhash) != NULL) {
		SCTP_HASH_FREE(inp->sctp_asocidhash, inp->hashasocidmark);
		inp->sctp_asocidhash = NULL;
	}
	/* sa_ignore FREED_MEMORY */
	TAILQ_FOREACH_SAFE(sq, &inp->read_queue, next, nsq) {
		/* Its only abandoned if it had data left */
		if (sq->length)
			SCTP_STAT_INCR(sctps_left_abandon);

		TAILQ_REMOVE(&inp->read_queue, sq, next);
		sctp_free_remote_addr(sq->whoFrom);
		if (so)
			so->so_rcv.sb_cc -= sq->length;
		if (sq->data) {
			sctp_m_freem(sq->data);
			sq->data = NULL;
		}
		/*
		 * no need to free the net count, since at this point all
		 * assoc's are gone.
		 */
		sctp_free_a_readq(NULL, sq);
	}
	/* Now the sctp_pcb things */
	/*
	 * free each asoc if it is not already closed/free. we can't use the
	 * macro here since le_next will get freed as part of the
	 * sctp_free_assoc() call.
	 */
	if (ip_pcb->inp_options) {
		(void)sctp_m_free(ip_pcb->inp_options);
		ip_pcb->inp_options = 0;
	}


#ifdef INET6
	if (ip_pcb->inp_vflag & INP_IPV6) {
		struct in6pcb *in6p;

		in6p = (struct in6pcb *)inp;
		ip6_freepcbopts(in6p->in6p_outputopts);
	}
#endif				/* INET6 */
	ip_pcb->inp_vflag = 0;
	/* free up authentication fields */
	if (inp->sctp_ep.local_auth_chunks != NULL)
		sctp_free_chunklist(inp->sctp_ep.local_auth_chunks);
	if (inp->sctp_ep.local_hmacs != NULL)
		sctp_free_hmaclist(inp->sctp_ep.local_hmacs);

	LIST_FOREACH_SAFE(shared_key, &inp->sctp_ep.shared_keys, next, nshared_key) {
		LIST_REMOVE(shared_key, next);
		sctp_free_sharedkey(shared_key);
		/* sa_ignore FREED_MEMORY */
	}

	/*
	 * if we have an address list the following will free the list of
	 * ifaddr's that are set into this ep. Again macro limitations here,
	 * since the LIST_FOREACH could be a bad idea.
	 */
	LIST_FOREACH_SAFE(laddr, &inp->sctp_addr_list, sctp_nxt_addr, nladdr) {
		sctp_remove_laddr(laddr);
	}

#ifdef SCTP_TRACK_FREED_ASOCS
	/* TEMP CODE */
	LIST_FOREACH_SAFE(asoc, &inp->sctp_asoc_free_list, sctp_tcblist, nasoc) {
		LIST_REMOVE(asoc, sctp_tcblist);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asoc), asoc);
		SCTP_DECR_ASOC_COUNT();
	}
	/* *** END TEMP CODE *** */
#endif
	/* Now lets see about freeing the EP hash table. */
	if (inp->sctp_tcbhash != NULL) {
		SCTP_HASH_FREE(inp->sctp_tcbhash, inp->sctp_hashmark);
		inp->sctp_tcbhash = NULL;
	}
	/* Now we must put the ep memory back into the zone pool */
	crfree(inp->ip_inp.inp.inp_cred);
	INP_LOCK_DESTROY(&inp->ip_inp.inp);
	SCTP_INP_LOCK_DESTROY(inp);
	SCTP_INP_READ_DESTROY(inp);
	SCTP_ASOC_CREATE_LOCK_DESTROY(inp);
	SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_ep), inp);
	SCTP_DECR_EP_COUNT();
}


struct sctp_nets *
sctp_findnet(struct sctp_tcb *stcb, struct sockaddr *addr)
{
	struct sctp_nets *net;

	/* locate the address */
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		if (sctp_cmpaddr(addr, (struct sockaddr *)&net->ro._l_addr))
			return (net);
	}
	return (NULL);
}


int
sctp_is_address_on_local_host(struct sockaddr *addr, uint32_t vrf_id)
{
	struct sctp_ifa *sctp_ifa;

	sctp_ifa = sctp_find_ifa_by_addr(addr, vrf_id, SCTP_ADDR_NOT_LOCKED);
	if (sctp_ifa) {
		return (1);
	} else {
		return (0);
	}
}

/*
 * add's a remote endpoint address, done with the INIT/INIT-ACK as well as
 * when a ASCONF arrives that adds it. It will also initialize all the cwnd
 * stats of stuff.
 */
int
sctp_add_remote_addr(struct sctp_tcb *stcb, struct sockaddr *newaddr,
    struct sctp_nets **netp, uint16_t port, int set_scope, int from)
{
	/*
	 * The following is redundant to the same lines in the
	 * sctp_aloc_assoc() but is needed since others call the add address
	 * function
	 */
	struct sctp_nets *net, *netfirst;
	int addr_inscope;

	SCTPDBG(SCTP_DEBUG_PCB1, "Adding an address (from:%d) to the peer: ",
	    from);
	SCTPDBG_ADDR(SCTP_DEBUG_PCB1, newaddr);

	netfirst = sctp_findnet(stcb, newaddr);
	if (netfirst) {
		/*
		 * Lie and return ok, we don't want to make the association
		 * go away for this behavior. It will happen in the TCP
		 * model in a connected socket. It does not reach the hash
		 * table until after the association is built so it can't be
		 * found. Mark as reachable, since the initial creation will
		 * have been cleared and the NOT_IN_ASSOC flag will have
		 * been added... and we don't want to end up removing it
		 * back out.
		 */
		if (netfirst->dest_state & SCTP_ADDR_UNCONFIRMED) {
			netfirst->dest_state = (SCTP_ADDR_REACHABLE |
			    SCTP_ADDR_UNCONFIRMED);
		} else {
			netfirst->dest_state = SCTP_ADDR_REACHABLE;
		}

		return (0);
	}
	addr_inscope = 1;
	switch (newaddr->sa_family) {
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin;

			sin = (struct sockaddr_in *)newaddr;
			if (sin->sin_addr.s_addr == 0) {
				/* Invalid address */
				return (-1);
			}
			/* zero out the zero area */
			memset(&sin->sin_zero, 0, sizeof(sin->sin_zero));

			/* assure len is set */
			sin->sin_len = sizeof(struct sockaddr_in);
			if (set_scope) {
				if (IN4_ISPRIVATE_ADDRESS(&sin->sin_addr)) {
					stcb->asoc.scope.ipv4_local_scope = 1;
				}
			} else {
				/* Validate the address is in scope */
				if ((IN4_ISPRIVATE_ADDRESS(&sin->sin_addr)) &&
				    (stcb->asoc.scope.ipv4_local_scope == 0)) {
					addr_inscope = 0;
				}
			}
			break;
		}
#endif
#ifdef INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)newaddr;
			if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
				/* Invalid address */
				return (-1);
			}
			/* assure len is set */
			sin6->sin6_len = sizeof(struct sockaddr_in6);
			if (set_scope) {
				if (sctp_is_address_on_local_host(newaddr, stcb->asoc.vrf_id)) {
					stcb->asoc.scope.loopback_scope = 1;
					stcb->asoc.scope.local_scope = 0;
					stcb->asoc.scope.ipv4_local_scope = 1;
					stcb->asoc.scope.site_scope = 1;
				} else if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
					/*
					 * If the new destination is a
					 * LINK_LOCAL we must have common
					 * site scope. Don't set the local
					 * scope since we may not share all
					 * links, only loopback can do this.
					 * Links on the local network would
					 * also be on our private network
					 * for v4 too.
					 */
					stcb->asoc.scope.ipv4_local_scope = 1;
					stcb->asoc.scope.site_scope = 1;
				} else if (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr)) {
					/*
					 * If the new destination is
					 * SITE_LOCAL then we must have site
					 * scope in common.
					 */
					stcb->asoc.scope.site_scope = 1;
				}
			} else {
				/* Validate the address is in scope */
				if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr) &&
				    (stcb->asoc.scope.loopback_scope == 0)) {
					addr_inscope = 0;
				} else if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
				    (stcb->asoc.scope.local_scope == 0)) {
					addr_inscope = 0;
				} else if (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr) &&
				    (stcb->asoc.scope.site_scope == 0)) {
					addr_inscope = 0;
				}
			}
			break;
		}
#endif
	default:
		/* not supported family type */
		return (-1);
	}
	net = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_net), struct sctp_nets);
	if (net == NULL) {
		return (-1);
	}
	SCTP_INCR_RADDR_COUNT();
	memset(net, 0, sizeof(struct sctp_nets));
	(void)SCTP_GETTIME_TIMEVAL(&net->start_time);
	memcpy(&net->ro._l_addr, newaddr, newaddr->sa_len);
	switch (newaddr->sa_family) {
#ifdef INET
	case AF_INET:
		((struct sockaddr_in *)&net->ro._l_addr)->sin_port = stcb->rport;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		((struct sockaddr_in6 *)&net->ro._l_addr)->sin6_port = stcb->rport;
		break;
#endif
	default:
		break;
	}
	net->addr_is_local = sctp_is_address_on_local_host(newaddr, stcb->asoc.vrf_id);
	if (net->addr_is_local && ((set_scope || (from == SCTP_ADDR_IS_CONFIRMED)))) {
		stcb->asoc.scope.loopback_scope = 1;
		stcb->asoc.scope.ipv4_local_scope = 1;
		stcb->asoc.scope.local_scope = 0;
		stcb->asoc.scope.site_scope = 1;
		addr_inscope = 1;
	}
	net->failure_threshold = stcb->asoc.def_net_failure;
	net->pf_threshold = stcb->asoc.def_net_pf_threshold;
	if (addr_inscope == 0) {
		net->dest_state = (SCTP_ADDR_REACHABLE |
		    SCTP_ADDR_OUT_OF_SCOPE);
	} else {
		if (from == SCTP_ADDR_IS_CONFIRMED)
			/* SCTP_ADDR_IS_CONFIRMED is passed by connect_x */
			net->dest_state = SCTP_ADDR_REACHABLE;
		else
			net->dest_state = SCTP_ADDR_REACHABLE |
			    SCTP_ADDR_UNCONFIRMED;
	}
	/*
	 * We set this to 0, the timer code knows that this means its an
	 * initial value
	 */
	net->rto_needed = 1;
	net->RTO = 0;
	net->RTO_measured = 0;
	stcb->asoc.numnets++;
	net->ref_count = 1;
	net->cwr_window_tsn = net->last_cwr_tsn = stcb->asoc.sending_seq - 1;
	net->port = port;
	net->dscp = stcb->asoc.default_dscp;
#ifdef INET6
	net->flowlabel = stcb->asoc.default_flowlabel;
#endif
	if (sctp_stcb_is_feature_on(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_DONOT_HEARTBEAT)) {
		net->dest_state |= SCTP_ADDR_NOHB;
	} else {
		net->dest_state &= ~SCTP_ADDR_NOHB;
	}
	if (sctp_stcb_is_feature_on(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_DO_NOT_PMTUD)) {
		net->dest_state |= SCTP_ADDR_NO_PMTUD;
	} else {
		net->dest_state &= ~SCTP_ADDR_NO_PMTUD;
	}
	net->heart_beat_delay = stcb->asoc.heart_beat_delay;
	/* Init the timer structure */
	SCTP_OS_TIMER_INIT(&net->rxt_timer.timer);
	SCTP_OS_TIMER_INIT(&net->pmtu_timer.timer);
	SCTP_OS_TIMER_INIT(&net->hb_timer.timer);

	/* Now generate a route for this guy */
#ifdef INET6
	/* KAME hack: embed scopeid */
	if (newaddr->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)&net->ro._l_addr;
		(void)sa6_embedscope(sin6, MODULE_GLOBAL(ip6_use_defzone));
		sin6->sin6_scope_id = 0;
	}
#endif
	SCTP_RTALLOC((sctp_route_t *)&net->ro,
	    stcb->asoc.vrf_id,
	    stcb->sctp_ep->fibnum);

	net->src_addr_selected = 0;
	if (SCTP_ROUTE_HAS_VALID_IFN(&net->ro)) {
		/* Get source address */
		net->ro._s_addr = sctp_source_address_selection(stcb->sctp_ep,
		    stcb,
		    (sctp_route_t *)&net->ro,
		    net,
		    0,
		    stcb->asoc.vrf_id);
		if (stcb->asoc.default_mtu > 0) {
			net->mtu = stcb->asoc.default_mtu;
			switch (net->ro._l_addr.sa.sa_family) {
#ifdef INET
			case AF_INET:
				net->mtu += SCTP_MIN_V4_OVERHEAD;
				break;
#endif
#ifdef INET6
			case AF_INET6:
				net->mtu += SCTP_MIN_OVERHEAD;
				break;
#endif
			default:
				break;
			}
#if defined(INET) || defined(INET6)
			if (net->port) {
				net->mtu += (uint32_t)sizeof(struct udphdr);
			}
#endif
		} else if (net->ro._s_addr != NULL) {
			uint32_t imtu, rmtu, hcmtu;

			net->src_addr_selected = 1;
			/* Now get the interface MTU */
			if (net->ro._s_addr->ifn_p != NULL) {
				imtu = SCTP_GATHER_MTU_FROM_INTFC(net->ro._s_addr->ifn_p);
			} else {
				imtu = 0;
			}
			rmtu = SCTP_GATHER_MTU_FROM_ROUTE(net->ro._s_addr, &net->ro._l_addr.sa, net->ro.ro_rt);
			hcmtu = sctp_hc_get_mtu(&net->ro._l_addr, stcb->sctp_ep->fibnum);
			net->mtu = sctp_min_mtu(hcmtu, rmtu, imtu);
			if (rmtu == 0) {
				/*
				 * Start things off to match mtu of
				 * interface please.
				 */
				SCTP_SET_MTU_OF_ROUTE(&net->ro._l_addr.sa,
				    net->ro.ro_rt, net->mtu);
			}
		}
	}
	if (net->mtu == 0) {
		if (stcb->asoc.default_mtu > 0) {
			net->mtu = stcb->asoc.default_mtu;
			switch (net->ro._l_addr.sa.sa_family) {
#ifdef INET
			case AF_INET:
				net->mtu += SCTP_MIN_V4_OVERHEAD;
				break;
#endif
#ifdef INET6
			case AF_INET6:
				net->mtu += SCTP_MIN_OVERHEAD;
				break;
#endif
			default:
				break;
			}
#if defined(INET) || defined(INET6)
			if (net->port) {
				net->mtu += (uint32_t)sizeof(struct udphdr);
			}
#endif
		} else {
			switch (newaddr->sa_family) {
#ifdef INET
			case AF_INET:
				net->mtu = SCTP_DEFAULT_MTU;
				break;
#endif
#ifdef INET6
			case AF_INET6:
				net->mtu = 1280;
				break;
#endif
			default:
				break;
			}
		}
	}
#if defined(INET) || defined(INET6)
	if (net->port) {
		net->mtu -= (uint32_t)sizeof(struct udphdr);
	}
#endif
	if (from == SCTP_ALLOC_ASOC) {
		stcb->asoc.smallest_mtu = net->mtu;
	}
	if (stcb->asoc.smallest_mtu > net->mtu) {
		sctp_pathmtu_adjustment(stcb, net->mtu);
	}
#ifdef INET6
	if (newaddr->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)&net->ro._l_addr;
		(void)sa6_recoverscope(sin6);
	}
#endif

	/* JRS - Use the congestion control given in the CC module */
	if (stcb->asoc.cc_functions.sctp_set_initial_cc_param != NULL)
		(*stcb->asoc.cc_functions.sctp_set_initial_cc_param) (stcb, net);

	/*
	 * CMT: CUC algo - set find_pseudo_cumack to TRUE (1) at beginning
	 * of assoc (2005/06/27, iyengar@cis.udel.edu)
	 */
	net->find_pseudo_cumack = 1;
	net->find_rtx_pseudo_cumack = 1;
	/* Choose an initial flowid. */
	net->flowid = stcb->asoc.my_vtag ^
	    ntohs(stcb->rport) ^
	    ntohs(stcb->sctp_ep->sctp_lport);
	net->flowtype = M_HASHTYPE_OPAQUE_HASH;
	if (netp) {
		*netp = net;
	}
	netfirst = TAILQ_FIRST(&stcb->asoc.nets);
	if (net->ro.ro_rt == NULL) {
		/* Since we have no route put it at the back */
		TAILQ_INSERT_TAIL(&stcb->asoc.nets, net, sctp_next);
	} else if (netfirst == NULL) {
		/* We are the first one in the pool. */
		TAILQ_INSERT_HEAD(&stcb->asoc.nets, net, sctp_next);
	} else if (netfirst->ro.ro_rt == NULL) {
		/*
		 * First one has NO route. Place this one ahead of the first
		 * one.
		 */
		TAILQ_INSERT_HEAD(&stcb->asoc.nets, net, sctp_next);
	} else if (net->ro.ro_rt->rt_ifp != netfirst->ro.ro_rt->rt_ifp) {
		/*
		 * This one has a different interface than the one at the
		 * top of the list. Place it ahead.
		 */
		TAILQ_INSERT_HEAD(&stcb->asoc.nets, net, sctp_next);
	} else {
		/*
		 * Ok we have the same interface as the first one. Move
		 * forward until we find either a) one with a NULL route...
		 * insert ahead of that b) one with a different ifp.. insert
		 * after that. c) end of the list.. insert at the tail.
		 */
		struct sctp_nets *netlook;

		do {
			netlook = TAILQ_NEXT(netfirst, sctp_next);
			if (netlook == NULL) {
				/* End of the list */
				TAILQ_INSERT_TAIL(&stcb->asoc.nets, net, sctp_next);
				break;
			} else if (netlook->ro.ro_rt == NULL) {
				/* next one has NO route */
				TAILQ_INSERT_BEFORE(netfirst, net, sctp_next);
				break;
			} else if (netlook->ro.ro_rt->rt_ifp != net->ro.ro_rt->rt_ifp) {
				TAILQ_INSERT_AFTER(&stcb->asoc.nets, netlook,
				    net, sctp_next);
				break;
			}
			/* Shift forward */
			netfirst = netlook;
		} while (netlook != NULL);
	}

	/* got to have a primary set */
	if (stcb->asoc.primary_destination == 0) {
		stcb->asoc.primary_destination = net;
	} else if ((stcb->asoc.primary_destination->ro.ro_rt == NULL) &&
		    (net->ro.ro_rt) &&
	    ((net->dest_state & SCTP_ADDR_UNCONFIRMED) == 0)) {
		/* No route to current primary adopt new primary */
		stcb->asoc.primary_destination = net;
	}
	/* Validate primary is first */
	net = TAILQ_FIRST(&stcb->asoc.nets);
	if ((net != stcb->asoc.primary_destination) &&
	    (stcb->asoc.primary_destination)) {
		/*
		 * first one on the list is NOT the primary sctp_cmpaddr()
		 * is much more efficient if the primary is the first on the
		 * list, make it so.
		 */
		TAILQ_REMOVE(&stcb->asoc.nets,
		    stcb->asoc.primary_destination, sctp_next);
		TAILQ_INSERT_HEAD(&stcb->asoc.nets,
		    stcb->asoc.primary_destination, sctp_next);
	}
	return (0);
}


static uint32_t
sctp_aloc_a_assoc_id(struct sctp_inpcb *inp, struct sctp_tcb *stcb)
{
	uint32_t id;
	struct sctpasochead *head;
	struct sctp_tcb *lstcb;

try_again:
	if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
		/* TSNH */
		return (0);
	}
	/*
	 * We don't allow assoc id to be one of SCTP_FUTURE_ASSOC,
	 * SCTP_CURRENT_ASSOC and SCTP_ALL_ASSOC.
	 */
	if (inp->sctp_associd_counter <= SCTP_ALL_ASSOC) {
		inp->sctp_associd_counter = SCTP_ALL_ASSOC + 1;
	}
	id = inp->sctp_associd_counter;
	inp->sctp_associd_counter++;
	lstcb = sctp_findasoc_ep_asocid_locked(inp, (sctp_assoc_t)id, 0);
	if (lstcb) {
		goto try_again;
	}
	head = &inp->sctp_asocidhash[SCTP_PCBHASH_ASOC(id, inp->hashasocidmark)];
	LIST_INSERT_HEAD(head, stcb, sctp_tcbasocidhash);
	stcb->asoc.in_asocid_hash = 1;
	return (id);
}

/*
 * allocate an association and add it to the endpoint. The caller must be
 * careful to add all additional addresses once they are know right away or
 * else the assoc will be may experience a blackout scenario.
 */
struct sctp_tcb *
sctp_aloc_assoc(struct sctp_inpcb *inp, struct sockaddr *firstaddr,
    int *error, uint32_t override_tag, uint32_t vrf_id,
    uint16_t o_streams, uint16_t port,
    struct thread *p
)
{
	/* note the p argument is only valid in unbound sockets */

	struct sctp_tcb *stcb;
	struct sctp_association *asoc;
	struct sctpasochead *head;
	uint16_t rport;
	int err;

	/*
	 * Assumption made here: Caller has done a
	 * sctp_findassociation_ep_addr(ep, addr's); to make sure the
	 * address does not exist already.
	 */
	if (SCTP_BASE_INFO(ipi_count_asoc) >= SCTP_MAX_NUM_OF_ASOC) {
		/* Hit max assoc, sorry no more */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, ENOBUFS);
		*error = ENOBUFS;
		return (NULL);
	}
	if (firstaddr == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
		*error = EINVAL;
		return (NULL);
	}
	SCTP_INP_RLOCK(inp);
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) &&
	    ((sctp_is_feature_off(inp, SCTP_PCB_FLAGS_PORTREUSE)) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED))) {
		/*
		 * If its in the TCP pool, its NOT allowed to create an
		 * association. The parent listener needs to call
		 * sctp_aloc_assoc.. or the one-2-many socket. If a peeled
		 * off, or connected one does this.. its an error.
		 */
		SCTP_INP_RUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
		*error = EINVAL;
		return (NULL);
	}
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE)) {
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_WAS_CONNECTED) ||
		    (inp->sctp_flags & SCTP_PCB_FLAGS_WAS_ABORTED)) {
			SCTP_INP_RUNLOCK(inp);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
			*error = EINVAL;
			return (NULL);
		}
	}
	SCTPDBG(SCTP_DEBUG_PCB3, "Allocate an association for peer:");
#ifdef SCTP_DEBUG
	if (firstaddr) {
		SCTPDBG_ADDR(SCTP_DEBUG_PCB3, firstaddr);
		switch (firstaddr->sa_family) {
#ifdef INET
		case AF_INET:
			SCTPDBG(SCTP_DEBUG_PCB3, "Port:%d\n",
			    ntohs(((struct sockaddr_in *)firstaddr)->sin_port));
			break;
#endif
#ifdef INET6
		case AF_INET6:
			SCTPDBG(SCTP_DEBUG_PCB3, "Port:%d\n",
			    ntohs(((struct sockaddr_in6 *)firstaddr)->sin6_port));
			break;
#endif
		default:
			break;
		}
	} else {
		SCTPDBG(SCTP_DEBUG_PCB3, "None\n");
	}
#endif				/* SCTP_DEBUG */
	switch (firstaddr->sa_family) {
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin;

			sin = (struct sockaddr_in *)firstaddr;
			if ((ntohs(sin->sin_port) == 0) ||
			    (sin->sin_addr.s_addr == INADDR_ANY) ||
			    (sin->sin_addr.s_addr == INADDR_BROADCAST) ||
			    IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
				/* Invalid address */
				SCTP_INP_RUNLOCK(inp);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
				*error = EINVAL;
				return (NULL);
			}
			rport = sin->sin_port;
			break;
		}
#endif
#ifdef INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)firstaddr;
			if ((ntohs(sin6->sin6_port) == 0) ||
			    IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ||
			    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
				/* Invalid address */
				SCTP_INP_RUNLOCK(inp);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
				*error = EINVAL;
				return (NULL);
			}
			rport = sin6->sin6_port;
			break;
		}
#endif
	default:
		/* not supported family type */
		SCTP_INP_RUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
		*error = EINVAL;
		return (NULL);
	}
	SCTP_INP_RUNLOCK(inp);
	if (inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) {
		/*
		 * If you have not performed a bind, then we need to do the
		 * ephemeral bind for you.
		 */
		if ((err = sctp_inpcb_bind(inp->sctp_socket,
		    (struct sockaddr *)NULL,
		    (struct sctp_ifa *)NULL,
		    p
		    ))) {
			/* bind error, probably perm */
			*error = err;
			return (NULL);
		}
	}
	stcb = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_asoc), struct sctp_tcb);
	if (stcb == NULL) {
		/* out of memory? */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, ENOMEM);
		*error = ENOMEM;
		return (NULL);
	}
	SCTP_INCR_ASOC_COUNT();

	memset(stcb, 0, sizeof(*stcb));
	asoc = &stcb->asoc;

	SCTP_TCB_LOCK_INIT(stcb);
	SCTP_TCB_SEND_LOCK_INIT(stcb);
	stcb->rport = rport;
	/* setup back pointer's */
	stcb->sctp_ep = inp;
	stcb->sctp_socket = inp->sctp_socket;
	if ((err = sctp_init_asoc(inp, stcb, override_tag, vrf_id, o_streams))) {
		/* failed */
		SCTP_TCB_LOCK_DESTROY(stcb);
		SCTP_TCB_SEND_LOCK_DESTROY(stcb);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asoc), stcb);
		SCTP_DECR_ASOC_COUNT();
		*error = err;
		return (NULL);
	}
	/* and the port */
	SCTP_INP_INFO_WLOCK();
	SCTP_INP_WLOCK(inp);
	if (inp->sctp_flags & (SCTP_PCB_FLAGS_SOCKET_GONE | SCTP_PCB_FLAGS_SOCKET_ALLGONE)) {
		/* inpcb freed while alloc going on */
		SCTP_TCB_LOCK_DESTROY(stcb);
		SCTP_TCB_SEND_LOCK_DESTROY(stcb);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asoc), stcb);
		SCTP_INP_WUNLOCK(inp);
		SCTP_INP_INFO_WUNLOCK();
		SCTP_DECR_ASOC_COUNT();
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, EINVAL);
		*error = EINVAL;
		return (NULL);
	}
	SCTP_TCB_LOCK(stcb);

	asoc->assoc_id = sctp_aloc_a_assoc_id(inp, stcb);
	/* now that my_vtag is set, add it to the hash */
	head = &SCTP_BASE_INFO(sctp_asochash)[SCTP_PCBHASH_ASOC(stcb->asoc.my_vtag, SCTP_BASE_INFO(hashasocmark))];
	/* put it in the bucket in the vtag hash of assoc's for the system */
	LIST_INSERT_HEAD(head, stcb, sctp_asocs);
	SCTP_INP_INFO_WUNLOCK();

	if ((err = sctp_add_remote_addr(stcb, firstaddr, NULL, port, SCTP_DO_SETSCOPE, SCTP_ALLOC_ASOC))) {
		/* failure.. memory error? */
		if (asoc->strmout) {
			SCTP_FREE(asoc->strmout, SCTP_M_STRMO);
			asoc->strmout = NULL;
		}
		if (asoc->mapping_array) {
			SCTP_FREE(asoc->mapping_array, SCTP_M_MAP);
			asoc->mapping_array = NULL;
		}
		if (asoc->nr_mapping_array) {
			SCTP_FREE(asoc->nr_mapping_array, SCTP_M_MAP);
			asoc->nr_mapping_array = NULL;
		}
		SCTP_DECR_ASOC_COUNT();
		SCTP_TCB_UNLOCK(stcb);
		SCTP_TCB_LOCK_DESTROY(stcb);
		SCTP_TCB_SEND_LOCK_DESTROY(stcb);
		LIST_REMOVE(stcb, sctp_tcbasocidhash);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asoc), stcb);
		SCTP_INP_WUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_PCB, ENOBUFS);
		*error = ENOBUFS;
		return (NULL);
	}
	/* Init all the timers */
	SCTP_OS_TIMER_INIT(&asoc->dack_timer.timer);
	SCTP_OS_TIMER_INIT(&asoc->strreset_timer.timer);
	SCTP_OS_TIMER_INIT(&asoc->asconf_timer.timer);
	SCTP_OS_TIMER_INIT(&asoc->shut_guard_timer.timer);
	SCTP_OS_TIMER_INIT(&asoc->autoclose_timer.timer);
	SCTP_OS_TIMER_INIT(&asoc->delayed_event_timer.timer);
	SCTP_OS_TIMER_INIT(&asoc->delete_prim_timer.timer);

	LIST_INSERT_HEAD(&inp->sctp_asoc_list, stcb, sctp_tcblist);
	/* now file the port under the hash as well */
	if (inp->sctp_tcbhash != NULL) {
		head = &inp->sctp_tcbhash[SCTP_PCBHASH_ALLADDR(stcb->rport,
		    inp->sctp_hashmark)];
		LIST_INSERT_HEAD(head, stcb, sctp_tcbhash);
	}
	SCTP_INP_WUNLOCK(inp);
	SCTPDBG(SCTP_DEBUG_PCB1, "Association %p now allocated\n", (void *)stcb);
	return (stcb);
}


void
sctp_remove_net(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	struct sctp_association *asoc;

	asoc = &stcb->asoc;
	asoc->numnets--;
	TAILQ_REMOVE(&asoc->nets, net, sctp_next);
	if (net == asoc->primary_destination) {
		/* Reset primary */
		struct sctp_nets *lnet;

		lnet = TAILQ_FIRST(&asoc->nets);
		/*
		 * Mobility adaptation Ideally, if deleted destination is
		 * the primary, it becomes a fast retransmission trigger by
		 * the subsequent SET PRIMARY. (by micchie)
		 */
		if (sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_BASE) ||
		    sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_FASTHANDOFF)) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "remove_net: primary dst is deleting\n");
			if (asoc->deleted_primary != NULL) {
				SCTPDBG(SCTP_DEBUG_ASCONF1, "remove_net: deleted primary may be already stored\n");
				goto out;
			}
			asoc->deleted_primary = net;
			atomic_add_int(&net->ref_count, 1);
			memset(&net->lastsa, 0, sizeof(net->lastsa));
			memset(&net->lastsv, 0, sizeof(net->lastsv));
			sctp_mobility_feature_on(stcb->sctp_ep,
			    SCTP_MOBILITY_PRIM_DELETED);
			sctp_timer_start(SCTP_TIMER_TYPE_PRIM_DELETED,
			    stcb->sctp_ep, stcb, NULL);
		}
out:
		/* Try to find a confirmed primary */
		asoc->primary_destination = sctp_find_alternate_net(stcb, lnet, 0);
	}
	if (net == asoc->last_data_chunk_from) {
		/* Reset primary */
		asoc->last_data_chunk_from = TAILQ_FIRST(&asoc->nets);
	}
	if (net == asoc->last_control_chunk_from) {
		/* Clear net */
		asoc->last_control_chunk_from = NULL;
	}
	if (net == stcb->asoc.alternate) {
		sctp_free_remote_addr(stcb->asoc.alternate);
		stcb->asoc.alternate = NULL;
	}
	sctp_free_remote_addr(net);
}

/*
 * remove a remote endpoint address from an association, it will fail if the
 * address does not exist.
 */
int
sctp_del_remote_addr(struct sctp_tcb *stcb, struct sockaddr *remaddr)
{
	/*
	 * Here we need to remove a remote address. This is quite simple, we
	 * first find it in the list of address for the association
	 * (tasoc->asoc.nets) and then if it is there, we do a LIST_REMOVE
	 * on that item. Note we do not allow it to be removed if there are
	 * no other addresses.
	 */
	struct sctp_association *asoc;
	struct sctp_nets *net, *nnet;

	asoc = &stcb->asoc;

	/* locate the address */
	TAILQ_FOREACH_SAFE(net, &asoc->nets, sctp_next, nnet) {
		if (net->ro._l_addr.sa.sa_family != remaddr->sa_family) {
			continue;
		}
		if (sctp_cmpaddr((struct sockaddr *)&net->ro._l_addr,
		    remaddr)) {
			/* we found the guy */
			if (asoc->numnets < 2) {
				/* Must have at LEAST two remote addresses */
				return (-1);
			} else {
				sctp_remove_net(stcb, net);
				return (0);
			}
		}
	}
	/* not found. */
	return (-2);
}

void
sctp_delete_from_timewait(uint32_t tag, uint16_t lport, uint16_t rport)
{
	struct sctpvtaghead *chain;
	struct sctp_tagblock *twait_block;
	int found = 0;
	int i;

	chain = &SCTP_BASE_INFO(vtag_timewait)[(tag % SCTP_STACK_VTAG_HASH_SIZE)];
	LIST_FOREACH(twait_block, chain, sctp_nxt_tagblock) {
		for (i = 0; i < SCTP_NUMBER_IN_VTAG_BLOCK; i++) {
			if ((twait_block->vtag_block[i].v_tag == tag) &&
			    (twait_block->vtag_block[i].lport == lport) &&
			    (twait_block->vtag_block[i].rport == rport)) {
				twait_block->vtag_block[i].tv_sec_at_expire = 0;
				twait_block->vtag_block[i].v_tag = 0;
				twait_block->vtag_block[i].lport = 0;
				twait_block->vtag_block[i].rport = 0;
				found = 1;
				break;
			}
		}
		if (found)
			break;
	}
}

int
sctp_is_in_timewait(uint32_t tag, uint16_t lport, uint16_t rport)
{
	struct sctpvtaghead *chain;
	struct sctp_tagblock *twait_block;
	int found = 0;
	int i;

	SCTP_INP_INFO_WLOCK();
	chain = &SCTP_BASE_INFO(vtag_timewait)[(tag % SCTP_STACK_VTAG_HASH_SIZE)];
	LIST_FOREACH(twait_block, chain, sctp_nxt_tagblock) {
		for (i = 0; i < SCTP_NUMBER_IN_VTAG_BLOCK; i++) {
			if ((twait_block->vtag_block[i].v_tag == tag) &&
			    (twait_block->vtag_block[i].lport == lport) &&
			    (twait_block->vtag_block[i].rport == rport)) {
				found = 1;
				break;
			}
		}
		if (found)
			break;
	}
	SCTP_INP_INFO_WUNLOCK();
	return (found);
}


void
sctp_add_vtag_to_timewait(uint32_t tag, uint32_t time, uint16_t lport, uint16_t rport)
{
	struct sctpvtaghead *chain;
	struct sctp_tagblock *twait_block;
	struct timeval now;
	int set, i;

	if (time == 0) {
		/* Its disabled */
		return;
	}
	(void)SCTP_GETTIME_TIMEVAL(&now);
	chain = &SCTP_BASE_INFO(vtag_timewait)[(tag % SCTP_STACK_VTAG_HASH_SIZE)];
	set = 0;
	LIST_FOREACH(twait_block, chain, sctp_nxt_tagblock) {
		/* Block(s) present, lets find space, and expire on the fly */
		for (i = 0; i < SCTP_NUMBER_IN_VTAG_BLOCK; i++) {
			if ((twait_block->vtag_block[i].v_tag == 0) &&
			    !set) {
				twait_block->vtag_block[i].tv_sec_at_expire =
				    now.tv_sec + time;
				twait_block->vtag_block[i].v_tag = tag;
				twait_block->vtag_block[i].lport = lport;
				twait_block->vtag_block[i].rport = rport;
				set = 1;
			} else if ((twait_block->vtag_block[i].v_tag) &&
			    ((long)twait_block->vtag_block[i].tv_sec_at_expire < now.tv_sec)) {
				/* Audit expires this guy */
				twait_block->vtag_block[i].tv_sec_at_expire = 0;
				twait_block->vtag_block[i].v_tag = 0;
				twait_block->vtag_block[i].lport = 0;
				twait_block->vtag_block[i].rport = 0;
				if (set == 0) {
					/* Reuse it for my new tag */
					twait_block->vtag_block[i].tv_sec_at_expire = now.tv_sec + time;
					twait_block->vtag_block[i].v_tag = tag;
					twait_block->vtag_block[i].lport = lport;
					twait_block->vtag_block[i].rport = rport;
					set = 1;
				}
			}
		}
		if (set) {
			/*
			 * We only do up to the block where we can place our
			 * tag for audits
			 */
			break;
		}
	}
	/* Need to add a new block to chain */
	if (!set) {
		SCTP_MALLOC(twait_block, struct sctp_tagblock *,
		    sizeof(struct sctp_tagblock), SCTP_M_TIMW);
		if (twait_block == NULL) {
#ifdef INVARIANTS
			panic("Can not alloc tagblock");
#endif
			return;
		}
		memset(twait_block, 0, sizeof(struct sctp_tagblock));
		LIST_INSERT_HEAD(chain, twait_block, sctp_nxt_tagblock);
		twait_block->vtag_block[0].tv_sec_at_expire = now.tv_sec + time;
		twait_block->vtag_block[0].v_tag = tag;
		twait_block->vtag_block[0].lport = lport;
		twait_block->vtag_block[0].rport = rport;
	}
}

void
sctp_clean_up_stream(struct sctp_tcb *stcb, struct sctp_readhead *rh)
{
	struct sctp_tmit_chunk *chk, *nchk;
	struct sctp_queued_to_read *control, *ncontrol;

	TAILQ_FOREACH_SAFE(control, rh, next_instrm, ncontrol) {
		TAILQ_REMOVE(rh, control, next_instrm);
		control->on_strm_q = 0;
		if (control->on_read_q == 0) {
			sctp_free_remote_addr(control->whoFrom);
			if (control->data) {
				sctp_m_freem(control->data);
				control->data = NULL;
			}
		}
		/* Reassembly free? */
		TAILQ_FOREACH_SAFE(chk, &control->reasm, sctp_next, nchk) {
			TAILQ_REMOVE(&control->reasm, chk, sctp_next);
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
			if (chk->holds_key_ref)
				sctp_auth_key_release(stcb, chk->auth_keyid, SCTP_SO_LOCKED);
			sctp_free_remote_addr(chk->whoTo);
			SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_chunk), chk);
			SCTP_DECR_CHK_COUNT();
			/* sa_ignore FREED_MEMORY */
		}
		/*
		 * We don't free the address here since all the net's were
		 * freed above.
		 */
		if (control->on_read_q == 0) {
			sctp_free_a_readq(stcb, control);
		}
	}
}


/*-
 * Free the association after un-hashing the remote port. This
 * function ALWAYS returns holding NO LOCK on the stcb. It DOES
 * expect that the input to this function IS a locked TCB.
 * It will return 0, if it did NOT destroy the association (instead
 * it unlocks it. It will return NON-zero if it either destroyed the
 * association OR the association is already destroyed.
 */
int
sctp_free_assoc(struct sctp_inpcb *inp, struct sctp_tcb *stcb, int from_inpcbfree, int from_location)
{
	int i;
	struct sctp_association *asoc;
	struct sctp_nets *net, *nnet;
	struct sctp_laddr *laddr, *naddr;
	struct sctp_tmit_chunk *chk, *nchk;
	struct sctp_asconf_addr *aparam, *naparam;
	struct sctp_asconf_ack *aack, *naack;
	struct sctp_stream_reset_list *strrst, *nstrrst;
	struct sctp_queued_to_read *sq, *nsq;
	struct sctp_stream_queue_pending *sp, *nsp;
	sctp_sharedkey_t *shared_key, *nshared_key;
	struct socket *so;

	/* first, lets purge the entry from the hash table. */

#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, stcb, 6);
#endif
	if (stcb->asoc.state == 0) {
#ifdef SCTP_LOG_CLOSING
		sctp_log_closing(inp, NULL, 7);
#endif
		/* there is no asoc, really TSNH :-0 */
		return (1);
	}
	if (stcb->asoc.alternate) {
		sctp_free_remote_addr(stcb->asoc.alternate);
		stcb->asoc.alternate = NULL;
	}
	/* TEMP CODE */
	if (stcb->freed_from_where == 0) {
		/* Only record the first place free happened from */
		stcb->freed_from_where = from_location;
	}
	/* TEMP CODE */

	asoc = &stcb->asoc;
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE))
		/* nothing around */
		so = NULL;
	else
		so = inp->sctp_socket;

	/*
	 * We used timer based freeing if a reader or writer is in the way.
	 * So we first check if we are actually being called from a timer,
	 * if so we abort early if a reader or writer is still in the way.
	 */
	if ((stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) &&
	    (from_inpcbfree == SCTP_NORMAL_PROC)) {
		/*
		 * is it the timer driving us? if so are the reader/writers
		 * gone?
		 */
		if (stcb->asoc.refcnt) {
			/* nope, reader or writer in the way */
			sctp_timer_start(SCTP_TIMER_TYPE_ASOCKILL, inp, stcb, NULL);
			/* no asoc destroyed */
			SCTP_TCB_UNLOCK(stcb);
#ifdef SCTP_LOG_CLOSING
			sctp_log_closing(inp, stcb, 8);
#endif
			return (0);
		}
	}
	/* now clean up any other timers */
	(void)SCTP_OS_TIMER_STOP(&asoc->dack_timer.timer);
	asoc->dack_timer.self = NULL;
	(void)SCTP_OS_TIMER_STOP(&asoc->strreset_timer.timer);
	/*-
	 * For stream reset we don't blast this unless
	 * it is a str-reset timer, it might be the
	 * free-asoc timer which we DON'T want to
	 * disturb.
	 */
	if (asoc->strreset_timer.type == SCTP_TIMER_TYPE_STRRESET)
		asoc->strreset_timer.self = NULL;
	(void)SCTP_OS_TIMER_STOP(&asoc->asconf_timer.timer);
	asoc->asconf_timer.self = NULL;
	(void)SCTP_OS_TIMER_STOP(&asoc->autoclose_timer.timer);
	asoc->autoclose_timer.self = NULL;
	(void)SCTP_OS_TIMER_STOP(&asoc->shut_guard_timer.timer);
	asoc->shut_guard_timer.self = NULL;
	(void)SCTP_OS_TIMER_STOP(&asoc->delayed_event_timer.timer);
	asoc->delayed_event_timer.self = NULL;
	/* Mobility adaptation */
	(void)SCTP_OS_TIMER_STOP(&asoc->delete_prim_timer.timer);
	asoc->delete_prim_timer.self = NULL;
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		(void)SCTP_OS_TIMER_STOP(&net->rxt_timer.timer);
		net->rxt_timer.self = NULL;
		(void)SCTP_OS_TIMER_STOP(&net->pmtu_timer.timer);
		net->pmtu_timer.self = NULL;
		(void)SCTP_OS_TIMER_STOP(&net->hb_timer.timer);
		net->hb_timer.self = NULL;
	}
	/* Now the read queue needs to be cleaned up (only once) */
	if ((stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) == 0) {
		SCTP_ADD_SUBSTATE(stcb, SCTP_STATE_ABOUT_TO_BE_FREED);
		SCTP_INP_READ_LOCK(inp);
		TAILQ_FOREACH(sq, &inp->read_queue, next) {
			if (sq->stcb == stcb) {
				sq->do_not_ref_stcb = 1;
				sq->sinfo_cumtsn = stcb->asoc.cumulative_tsn;
				/*
				 * If there is no end, there never will be
				 * now.
				 */
				if (sq->end_added == 0) {
					/* Held for PD-API clear that. */
					sq->pdapi_aborted = 1;
					sq->held_length = 0;
					if (sctp_stcb_is_feature_on(inp, stcb, SCTP_PCB_FLAGS_PDAPIEVNT) && (so != NULL)) {
						/*
						 * Need to add a PD-API
						 * aborted indication.
						 * Setting the control_pdapi
						 * assures that it will be
						 * added right after this
						 * msg.
						 */
						uint32_t strseq;

						stcb->asoc.control_pdapi = sq;
						strseq = (sq->sinfo_stream << 16) | (sq->mid & 0x0000ffff);
						sctp_ulp_notify(SCTP_NOTIFY_PARTIAL_DELVIERY_INDICATION,
						    stcb,
						    SCTP_PARTIAL_DELIVERY_ABORTED,
						    (void *)&strseq,
						    SCTP_SO_LOCKED);
						stcb->asoc.control_pdapi = NULL;
					}
				}
				/* Add an end to wake them */
				sq->end_added = 1;
			}
		}
		SCTP_INP_READ_UNLOCK(inp);
		if (stcb->block_entry) {
			SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_PCB, ECONNRESET);
			stcb->block_entry->error = ECONNRESET;
			stcb->block_entry = NULL;
		}
	}
	if ((stcb->asoc.refcnt) || (stcb->asoc.state & SCTP_STATE_IN_ACCEPT_QUEUE)) {
		/*
		 * Someone holds a reference OR the socket is unaccepted
		 * yet.
		 */
		if ((stcb->asoc.refcnt) ||
		    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
		    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE)) {
			SCTP_CLEAR_SUBSTATE(stcb, SCTP_STATE_IN_ACCEPT_QUEUE);
			sctp_timer_start(SCTP_TIMER_TYPE_ASOCKILL, inp, stcb, NULL);
		}
		SCTP_TCB_UNLOCK(stcb);
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
		    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE))
			/* nothing around */
			so = NULL;
		if (so) {
			/* Wake any reader/writers */
			sctp_sorwakeup(inp, so);
			sctp_sowwakeup(inp, so);
		}

#ifdef SCTP_LOG_CLOSING
		sctp_log_closing(inp, stcb, 9);
#endif
		/* no asoc destroyed */
		return (0);
	}
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, stcb, 10);
#endif
	/*
	 * When I reach here, no others want to kill the assoc yet.. and I
	 * own the lock. Now its possible an abort comes in when I do the
	 * lock exchange below to grab all the locks to do the final take
	 * out. to prevent this we increment the count, which will start a
	 * timer and blow out above thus assuring us that we hold exclusive
	 * killing of the asoc. Note that after getting back the TCB lock we
	 * will go ahead and increment the counter back up and stop any
	 * timer a passing stranger may have started :-S
	 */
	if (from_inpcbfree == SCTP_NORMAL_PROC) {
		atomic_add_int(&stcb->asoc.refcnt, 1);

		SCTP_TCB_UNLOCK(stcb);
		SCTP_INP_INFO_WLOCK();
		SCTP_INP_WLOCK(inp);
		SCTP_TCB_LOCK(stcb);
	}
	/* Double check the GONE flag */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE))
		/* nothing around */
		so = NULL;

	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
		/*
		 * For TCP type we need special handling when we are
		 * connected. We also include the peel'ed off ones to.
		 */
		if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
			inp->sctp_flags &= ~SCTP_PCB_FLAGS_CONNECTED;
			inp->sctp_flags |= SCTP_PCB_FLAGS_WAS_CONNECTED;
			if (so) {
				SOCKBUF_LOCK(&so->so_rcv);
				if (so->so_rcv.sb_cc == 0) {
					so->so_state &= ~(SS_ISCONNECTING |
					    SS_ISDISCONNECTING |
					    SS_ISCONFIRMING |
					    SS_ISCONNECTED);
				}
				socantrcvmore_locked(so);
				socantsendmore(so);
				sctp_sowwakeup(inp, so);
				sctp_sorwakeup(inp, so);
				SCTP_SOWAKEUP(so);
			}
		}
	}

	/*
	 * Make it invalid too, that way if its about to run it will abort
	 * and return.
	 */
	/* re-increment the lock */
	if (from_inpcbfree == SCTP_NORMAL_PROC) {
		atomic_add_int(&stcb->asoc.refcnt, -1);
	}
	if (stcb->asoc.refcnt) {
		SCTP_CLEAR_SUBSTATE(stcb, SCTP_STATE_IN_ACCEPT_QUEUE);
		sctp_timer_start(SCTP_TIMER_TYPE_ASOCKILL, inp, stcb, NULL);
		if (from_inpcbfree == SCTP_NORMAL_PROC) {
			SCTP_INP_INFO_WUNLOCK();
			SCTP_INP_WUNLOCK(inp);
		}
		SCTP_TCB_UNLOCK(stcb);
		return (0);
	}
	asoc->state = 0;
	if (inp->sctp_tcbhash) {
		LIST_REMOVE(stcb, sctp_tcbhash);
	}
	if (stcb->asoc.in_asocid_hash) {
		LIST_REMOVE(stcb, sctp_tcbasocidhash);
	}
	/* Now lets remove it from the list of ALL associations in the EP */
	LIST_REMOVE(stcb, sctp_tcblist);
	if (from_inpcbfree == SCTP_NORMAL_PROC) {
		SCTP_INP_INCR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
	}
	/* pull from vtag hash */
	LIST_REMOVE(stcb, sctp_asocs);
	sctp_add_vtag_to_timewait(asoc->my_vtag, SCTP_BASE_SYSCTL(sctp_vtag_time_wait),
	    inp->sctp_lport, stcb->rport);

	/*
	 * Now restop the timers to be sure this is paranoia at is finest!
	 */
	(void)SCTP_OS_TIMER_STOP(&asoc->strreset_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->dack_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->strreset_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->asconf_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->shut_guard_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->autoclose_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->delayed_event_timer.timer);
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		(void)SCTP_OS_TIMER_STOP(&net->rxt_timer.timer);
		(void)SCTP_OS_TIMER_STOP(&net->pmtu_timer.timer);
		(void)SCTP_OS_TIMER_STOP(&net->hb_timer.timer);
	}

	asoc->strreset_timer.type = SCTP_TIMER_TYPE_NONE;
	/*
	 * The chunk lists and such SHOULD be empty but we check them just
	 * in case.
	 */
	/* anything on the wheel needs to be removed */
	SCTP_TCB_SEND_LOCK(stcb);
	for (i = 0; i < asoc->streamoutcnt; i++) {
		struct sctp_stream_out *outs;

		outs = &asoc->strmout[i];
		/* now clean up any chunks here */
		TAILQ_FOREACH_SAFE(sp, &outs->outqueue, next, nsp) {
			atomic_subtract_int(&asoc->stream_queue_cnt, 1);
			TAILQ_REMOVE(&outs->outqueue, sp, next);
			stcb->asoc.ss_functions.sctp_ss_remove_from_stream(stcb, asoc, outs, sp, 1);
			sctp_free_spbufspace(stcb, asoc, sp);
			if (sp->data) {
				if (so) {
					/* Still an open socket - report */
					sctp_ulp_notify(SCTP_NOTIFY_SPECIAL_SP_FAIL, stcb,
					    0, (void *)sp, SCTP_SO_LOCKED);
				}
				if (sp->data) {
					sctp_m_freem(sp->data);
					sp->data = NULL;
					sp->tail_mbuf = NULL;
					sp->length = 0;
				}
			}
			if (sp->net) {
				sctp_free_remote_addr(sp->net);
				sp->net = NULL;
			}
			sctp_free_a_strmoq(stcb, sp, SCTP_SO_LOCKED);
		}
	}
	SCTP_TCB_SEND_UNLOCK(stcb);
	/* sa_ignore FREED_MEMORY */
	TAILQ_FOREACH_SAFE(strrst, &asoc->resetHead, next_resp, nstrrst) {
		TAILQ_REMOVE(&asoc->resetHead, strrst, next_resp);
		SCTP_FREE(strrst, SCTP_M_STRESET);
	}
	TAILQ_FOREACH_SAFE(sq, &asoc->pending_reply_queue, next, nsq) {
		TAILQ_REMOVE(&asoc->pending_reply_queue, sq, next);
		if (sq->data) {
			sctp_m_freem(sq->data);
			sq->data = NULL;
		}
		sctp_free_remote_addr(sq->whoFrom);
		sq->whoFrom = NULL;
		sq->stcb = NULL;
		/* Free the ctl entry */
		sctp_free_a_readq(stcb, sq);
		/* sa_ignore FREED_MEMORY */
	}
	TAILQ_FOREACH_SAFE(chk, &asoc->free_chunks, sctp_next, nchk) {
		TAILQ_REMOVE(&asoc->free_chunks, chk, sctp_next);
		if (chk->data) {
			sctp_m_freem(chk->data);
			chk->data = NULL;
		}
		if (chk->holds_key_ref)
			sctp_auth_key_release(stcb, chk->auth_keyid, SCTP_SO_LOCKED);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_chunk), chk);
		SCTP_DECR_CHK_COUNT();
		atomic_subtract_int(&SCTP_BASE_INFO(ipi_free_chunks), 1);
		asoc->free_chunk_cnt--;
		/* sa_ignore FREED_MEMORY */
	}
	/* pending send queue SHOULD be empty */
	TAILQ_FOREACH_SAFE(chk, &asoc->send_queue, sctp_next, nchk) {
		if (asoc->strmout[chk->rec.data.sid].chunks_on_queues > 0) {
			asoc->strmout[chk->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
		} else {
			panic("No chunks on the queues for sid %u.", chk->rec.data.sid);
#endif
		}
		TAILQ_REMOVE(&asoc->send_queue, chk, sctp_next);
		if (chk->data) {
			if (so) {
				/* Still a socket? */
				sctp_ulp_notify(SCTP_NOTIFY_UNSENT_DG_FAIL, stcb,
				    0, chk, SCTP_SO_LOCKED);
			}
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
		}
		if (chk->holds_key_ref)
			sctp_auth_key_release(stcb, chk->auth_keyid, SCTP_SO_LOCKED);
		if (chk->whoTo) {
			sctp_free_remote_addr(chk->whoTo);
			chk->whoTo = NULL;
		}
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_chunk), chk);
		SCTP_DECR_CHK_COUNT();
		/* sa_ignore FREED_MEMORY */
	}
	/* sent queue SHOULD be empty */
	TAILQ_FOREACH_SAFE(chk, &asoc->sent_queue, sctp_next, nchk) {
		if (chk->sent != SCTP_DATAGRAM_NR_ACKED) {
			if (asoc->strmout[chk->rec.data.sid].chunks_on_queues > 0) {
				asoc->strmout[chk->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
			} else {
				panic("No chunks on the queues for sid %u.", chk->rec.data.sid);
#endif
			}
		}
		TAILQ_REMOVE(&asoc->sent_queue, chk, sctp_next);
		if (chk->data) {
			if (so) {
				/* Still a socket? */
				sctp_ulp_notify(SCTP_NOTIFY_SENT_DG_FAIL, stcb,
				    0, chk, SCTP_SO_LOCKED);
			}
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
		}
		if (chk->holds_key_ref)
			sctp_auth_key_release(stcb, chk->auth_keyid, SCTP_SO_LOCKED);
		sctp_free_remote_addr(chk->whoTo);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_chunk), chk);
		SCTP_DECR_CHK_COUNT();
		/* sa_ignore FREED_MEMORY */
	}
#ifdef INVARIANTS
	for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
		if (stcb->asoc.strmout[i].chunks_on_queues > 0) {
			panic("%u chunks left for stream %u.", stcb->asoc.strmout[i].chunks_on_queues, i);
		}
	}
#endif
	/* control queue MAY not be empty */
	TAILQ_FOREACH_SAFE(chk, &asoc->control_send_queue, sctp_next, nchk) {
		TAILQ_REMOVE(&asoc->control_send_queue, chk, sctp_next);
		if (chk->data) {
			sctp_m_freem(chk->data);
			chk->data = NULL;
		}
		if (chk->holds_key_ref)
			sctp_auth_key_release(stcb, chk->auth_keyid, SCTP_SO_LOCKED);
		sctp_free_remote_addr(chk->whoTo);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_chunk), chk);
		SCTP_DECR_CHK_COUNT();
		/* sa_ignore FREED_MEMORY */
	}
	/* ASCONF queue MAY not be empty */
	TAILQ_FOREACH_SAFE(chk, &asoc->asconf_send_queue, sctp_next, nchk) {
		TAILQ_REMOVE(&asoc->asconf_send_queue, chk, sctp_next);
		if (chk->data) {
			sctp_m_freem(chk->data);
			chk->data = NULL;
		}
		if (chk->holds_key_ref)
			sctp_auth_key_release(stcb, chk->auth_keyid, SCTP_SO_LOCKED);
		sctp_free_remote_addr(chk->whoTo);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_chunk), chk);
		SCTP_DECR_CHK_COUNT();
		/* sa_ignore FREED_MEMORY */
	}
	if (asoc->mapping_array) {
		SCTP_FREE(asoc->mapping_array, SCTP_M_MAP);
		asoc->mapping_array = NULL;
	}
	if (asoc->nr_mapping_array) {
		SCTP_FREE(asoc->nr_mapping_array, SCTP_M_MAP);
		asoc->nr_mapping_array = NULL;
	}
	/* the stream outs */
	if (asoc->strmout) {
		SCTP_FREE(asoc->strmout, SCTP_M_STRMO);
		asoc->strmout = NULL;
	}
	asoc->strm_realoutsize = asoc->streamoutcnt = 0;
	if (asoc->strmin) {
		for (i = 0; i < asoc->streamincnt; i++) {
			sctp_clean_up_stream(stcb, &asoc->strmin[i].inqueue);
			sctp_clean_up_stream(stcb, &asoc->strmin[i].uno_inqueue);
		}
		SCTP_FREE(asoc->strmin, SCTP_M_STRMI);
		asoc->strmin = NULL;
	}
	asoc->streamincnt = 0;
	TAILQ_FOREACH_SAFE(net, &asoc->nets, sctp_next, nnet) {
#ifdef INVARIANTS
		if (SCTP_BASE_INFO(ipi_count_raddr) == 0) {
			panic("no net's left alloc'ed, or list points to itself");
		}
#endif
		TAILQ_REMOVE(&asoc->nets, net, sctp_next);
		sctp_free_remote_addr(net);
	}
	LIST_FOREACH_SAFE(laddr, &asoc->sctp_restricted_addrs, sctp_nxt_addr, naddr) {
		/* sa_ignore FREED_MEMORY */
		sctp_remove_laddr(laddr);
	}

	/* pending asconf (address) parameters */
	TAILQ_FOREACH_SAFE(aparam, &asoc->asconf_queue, next, naparam) {
		/* sa_ignore FREED_MEMORY */
		TAILQ_REMOVE(&asoc->asconf_queue, aparam, next);
		SCTP_FREE(aparam, SCTP_M_ASC_ADDR);
	}
	TAILQ_FOREACH_SAFE(aack, &asoc->asconf_ack_sent, next, naack) {
		/* sa_ignore FREED_MEMORY */
		TAILQ_REMOVE(&asoc->asconf_ack_sent, aack, next);
		if (aack->data != NULL) {
			sctp_m_freem(aack->data);
		}
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asconf_ack), aack);
	}
	/* clean up auth stuff */
	if (asoc->local_hmacs)
		sctp_free_hmaclist(asoc->local_hmacs);
	if (asoc->peer_hmacs)
		sctp_free_hmaclist(asoc->peer_hmacs);

	if (asoc->local_auth_chunks)
		sctp_free_chunklist(asoc->local_auth_chunks);
	if (asoc->peer_auth_chunks)
		sctp_free_chunklist(asoc->peer_auth_chunks);

	sctp_free_authinfo(&asoc->authinfo);

	LIST_FOREACH_SAFE(shared_key, &asoc->shared_keys, next, nshared_key) {
		LIST_REMOVE(shared_key, next);
		sctp_free_sharedkey(shared_key);
		/* sa_ignore FREED_MEMORY */
	}

	/* Insert new items here :> */

	/* Get rid of LOCK */
	SCTP_TCB_UNLOCK(stcb);
	SCTP_TCB_LOCK_DESTROY(stcb);
	SCTP_TCB_SEND_LOCK_DESTROY(stcb);
	if (from_inpcbfree == SCTP_NORMAL_PROC) {
		SCTP_INP_INFO_WUNLOCK();
		SCTP_INP_RLOCK(inp);
	}
#ifdef SCTP_TRACK_FREED_ASOCS
	if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
		/* now clean up the tasoc itself */
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asoc), stcb);
		SCTP_DECR_ASOC_COUNT();
	} else {
		LIST_INSERT_HEAD(&inp->sctp_asoc_free_list, stcb, sctp_tcblist);
	}
#else
	SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asoc), stcb);
	SCTP_DECR_ASOC_COUNT();
#endif
	if (from_inpcbfree == SCTP_NORMAL_PROC) {
		if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
			/*
			 * If its NOT the inp_free calling us AND sctp_close
			 * as been called, we call back...
			 */
			SCTP_INP_RUNLOCK(inp);
			/*
			 * This will start the kill timer (if we are the
			 * last one) since we hold an increment yet. But
			 * this is the only safe way to do this since
			 * otherwise if the socket closes at the same time
			 * we are here we might collide in the cleanup.
			 */
			sctp_inpcb_free(inp,
			    SCTP_FREE_SHOULD_USE_GRACEFUL_CLOSE,
			    SCTP_CALLED_DIRECTLY_NOCMPSET);
			SCTP_INP_DECR_REF(inp);
			goto out_of;
		} else {
			/* The socket is still open. */
			SCTP_INP_DECR_REF(inp);
		}
	}
	if (from_inpcbfree == SCTP_NORMAL_PROC) {
		SCTP_INP_RUNLOCK(inp);
	}
out_of:
	/* destroyed the asoc */
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 11);
#endif
	return (1);
}



/*
 * determine if a destination is "reachable" based upon the addresses bound
 * to the current endpoint (e.g. only v4 or v6 currently bound)
 */
/*
 * FIX: if we allow assoc-level bindx(), then this needs to be fixed to use
 * assoc level v4/v6 flags, as the assoc *may* not have the same address
 * types bound as its endpoint
 */
int
sctp_destination_is_reachable(struct sctp_tcb *stcb, struct sockaddr *destaddr)
{
	struct sctp_inpcb *inp;
	int answer;

	/*
	 * No locks here, the TCB, in all cases is already locked and an
	 * assoc is up. There is either a INP lock by the caller applied (in
	 * asconf case when deleting an address) or NOT in the HB case,
	 * however if HB then the INP increment is up and the INP will not
	 * be removed (on top of the fact that we have a TCB lock). So we
	 * only want to read the sctp_flags, which is either bound-all or
	 * not.. no protection needed since once an assoc is up you can't be
	 * changing your binding.
	 */
	inp = stcb->sctp_ep;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/* if bound all, destination is not restricted */
		/*
		 * RRS: Question during lock work: Is this correct? If you
		 * are bound-all you still might need to obey the V4--V6
		 * flags??? IMO this bound-all stuff needs to be removed!
		 */
		return (1);
	}
	/* NOTE: all "scope" checks are done when local addresses are added */
	switch (destaddr->sa_family) {
#ifdef INET6
	case AF_INET6:
		answer = inp->ip_inp.inp.inp_vflag & INP_IPV6;
		break;
#endif
#ifdef INET
	case AF_INET:
		answer = inp->ip_inp.inp.inp_vflag & INP_IPV4;
		break;
#endif
	default:
		/* invalid family, so it's unreachable */
		answer = 0;
		break;
	}
	return (answer);
}

/*
 * update the inp_vflags on an endpoint
 */
static void
sctp_update_ep_vflag(struct sctp_inpcb *inp)
{
	struct sctp_laddr *laddr;

	/* first clear the flag */
	inp->ip_inp.inp.inp_vflag = 0;
	/* set the flag based on addresses on the ep list */
	LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
		if (laddr->ifa == NULL) {
			SCTPDBG(SCTP_DEBUG_PCB1, "%s: NULL ifa\n",
			    __func__);
			continue;
		}

		if (laddr->ifa->localifa_flags & SCTP_BEING_DELETED) {
			continue;
		}
		switch (laddr->ifa->address.sa.sa_family) {
#ifdef INET6
		case AF_INET6:
			inp->ip_inp.inp.inp_vflag |= INP_IPV6;
			break;
#endif
#ifdef INET
		case AF_INET:
			inp->ip_inp.inp.inp_vflag |= INP_IPV4;
			break;
#endif
		default:
			break;
		}
	}
}

/*
 * Add the address to the endpoint local address list There is nothing to be
 * done if we are bound to all addresses
 */
void
sctp_add_local_addr_ep(struct sctp_inpcb *inp, struct sctp_ifa *ifa, uint32_t action)
{
	struct sctp_laddr *laddr;
	struct sctp_tcb *stcb;
	int fnd, error = 0;

	fnd = 0;

	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/* You are already bound to all. You have it already */
		return;
	}
#ifdef INET6
	if (ifa->address.sa.sa_family == AF_INET6) {
		if (ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
			/* Can't bind a non-useable addr. */
			return;
		}
	}
#endif
	/* first, is it already present? */
	LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
		if (laddr->ifa == ifa) {
			fnd = 1;
			break;
		}
	}

	if (fnd == 0) {
		/* Not in the ep list */
		error = sctp_insert_laddr(&inp->sctp_addr_list, ifa, action);
		if (error != 0)
			return;
		inp->laddr_count++;
		/* update inp_vflag flags */
		switch (ifa->address.sa.sa_family) {
#ifdef INET6
		case AF_INET6:
			inp->ip_inp.inp.inp_vflag |= INP_IPV6;
			break;
#endif
#ifdef INET
		case AF_INET:
			inp->ip_inp.inp.inp_vflag |= INP_IPV4;
			break;
#endif
		default:
			break;
		}
		LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
			sctp_add_local_addr_restricted(stcb, ifa);
		}
	}
	return;
}


/*
 * select a new (hopefully reachable) destination net (should only be used
 * when we deleted an ep addr that is the only usable source address to reach
 * the destination net)
 */
static void
sctp_select_primary_destination(struct sctp_tcb *stcb)
{
	struct sctp_nets *net;

	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		/* for now, we'll just pick the first reachable one we find */
		if (net->dest_state & SCTP_ADDR_UNCONFIRMED)
			continue;
		if (sctp_destination_is_reachable(stcb,
		    (struct sockaddr *)&net->ro._l_addr)) {
			/* found a reachable destination */
			stcb->asoc.primary_destination = net;
		}
	}
	/* I can't there from here! ...we're gonna die shortly... */
}


/*
 * Delete the address from the endpoint local address list. There is nothing
 * to be done if we are bound to all addresses
 */
void
sctp_del_local_addr_ep(struct sctp_inpcb *inp, struct sctp_ifa *ifa)
{
	struct sctp_laddr *laddr;
	int fnd;

	fnd = 0;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/* You are already bound to all. You have it already */
		return;
	}
	LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
		if (laddr->ifa == ifa) {
			fnd = 1;
			break;
		}
	}
	if (fnd && (inp->laddr_count < 2)) {
		/* can't delete unless there are at LEAST 2 addresses */
		return;
	}
	if (fnd) {
		/*
		 * clean up any use of this address go through our
		 * associations and clear any last_used_address that match
		 * this one for each assoc, see if a new primary_destination
		 * is needed
		 */
		struct sctp_tcb *stcb;

		/* clean up "next_addr_touse" */
		if (inp->next_addr_touse == laddr)
			/* delete this address */
			inp->next_addr_touse = NULL;

		/* clean up "last_used_address" */
		LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
			struct sctp_nets *net;

			SCTP_TCB_LOCK(stcb);
			if (stcb->asoc.last_used_address == laddr)
				/* delete this address */
				stcb->asoc.last_used_address = NULL;
			/*
			 * Now spin through all the nets and purge any ref
			 * to laddr
			 */
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				if (net->ro._s_addr == laddr->ifa) {
					/* Yep, purge src address selected */
					sctp_rtentry_t *rt;

					/* delete this address if cached */
					rt = net->ro.ro_rt;
					if (rt != NULL) {
						RTFREE(rt);
						net->ro.ro_rt = NULL;
					}
					sctp_free_ifa(net->ro._s_addr);
					net->ro._s_addr = NULL;
					net->src_addr_selected = 0;
				}
			}
			SCTP_TCB_UNLOCK(stcb);
		}		/* for each tcb */
		/* remove it from the ep list */
		sctp_remove_laddr(laddr);
		inp->laddr_count--;
		/* update inp_vflag flags */
		sctp_update_ep_vflag(inp);
	}
	return;
}

/*
 * Add the address to the TCB local address restricted list.
 * This is a "pending" address list (eg. addresses waiting for an
 * ASCONF-ACK response) and cannot be used as a valid source address.
 */
void
sctp_add_local_addr_restricted(struct sctp_tcb *stcb, struct sctp_ifa *ifa)
{
	struct sctp_laddr *laddr;
	struct sctpladdr *list;

	/*
	 * Assumes TCB is locked.. and possibly the INP. May need to
	 * confirm/fix that if we need it and is not the case.
	 */
	list = &stcb->asoc.sctp_restricted_addrs;

#ifdef INET6
	if (ifa->address.sa.sa_family == AF_INET6) {
		if (ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
			/* Can't bind a non-existent addr. */
			return;
		}
	}
#endif
	/* does the address already exist? */
	LIST_FOREACH(laddr, list, sctp_nxt_addr) {
		if (laddr->ifa == ifa) {
			return;
		}
	}

	/* add to the list */
	(void)sctp_insert_laddr(list, ifa, 0);
	return;
}

/*
 * Remove a local address from the TCB local address restricted list
 */
void
sctp_del_local_addr_restricted(struct sctp_tcb *stcb, struct sctp_ifa *ifa)
{
	struct sctp_inpcb *inp;
	struct sctp_laddr *laddr;

	/*
	 * This is called by asconf work. It is assumed that a) The TCB is
	 * locked and b) The INP is locked. This is true in as much as I can
	 * trace through the entry asconf code where I did these locks.
	 * Again, the ASCONF code is a bit different in that it does lock
	 * the INP during its work often times. This must be since we don't
	 * want other proc's looking up things while what they are looking
	 * up is changing :-D
	 */

	inp = stcb->sctp_ep;
	/* if subset bound and don't allow ASCONF's, can't delete last */
	if (((inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) == 0) &&
	    sctp_is_feature_off(inp, SCTP_PCB_FLAGS_DO_ASCONF)) {
		if (stcb->sctp_ep->laddr_count < 2) {
			/* can't delete last address */
			return;
		}
	}
	LIST_FOREACH(laddr, &stcb->asoc.sctp_restricted_addrs, sctp_nxt_addr) {
		/* remove the address if it exists */
		if (laddr->ifa == NULL)
			continue;
		if (laddr->ifa == ifa) {
			sctp_remove_laddr(laddr);
			return;
		}
	}

	/* address not found! */
	return;
}

/*
 * Temporarily remove for __APPLE__ until we use the Tiger equivalents
 */
/* sysctl */
static int sctp_max_number_of_assoc = SCTP_MAX_NUM_OF_ASOC;
static int sctp_scale_up_for_address = SCTP_SCALE_FOR_ADDR;



#if defined(__FreeBSD__) && defined(SCTP_MCORE_INPUT) && defined(SMP)
struct sctp_mcore_ctrl *sctp_mcore_workers = NULL;
int *sctp_cpuarry = NULL;
void
sctp_queue_to_mcore(struct mbuf *m, int off, int cpu_to_use)
{
	/* Queue a packet to a processor for the specified core */
	struct sctp_mcore_queue *qent;
	struct sctp_mcore_ctrl *wkq;
	int need_wake = 0;

	if (sctp_mcore_workers == NULL) {
		/* Something went way bad during setup */
		sctp_input_with_port(m, off, 0);
		return;
	}
	SCTP_MALLOC(qent, struct sctp_mcore_queue *,
	    (sizeof(struct sctp_mcore_queue)),
	    SCTP_M_MCORE);
	if (qent == NULL) {
		/* This is trouble  */
		sctp_input_with_port(m, off, 0);
		return;
	}
	qent->vn = curvnet;
	qent->m = m;
	qent->off = off;
	qent->v6 = 0;
	wkq = &sctp_mcore_workers[cpu_to_use];
	SCTP_MCORE_QLOCK(wkq);

	TAILQ_INSERT_TAIL(&wkq->que, qent, next);
	if (wkq->running == 0) {
		need_wake = 1;
	}
	SCTP_MCORE_QUNLOCK(wkq);
	if (need_wake) {
		wakeup(&wkq->running);
	}
}

static void
sctp_mcore_thread(void *arg)
{

	struct sctp_mcore_ctrl *wkq;
	struct sctp_mcore_queue *qent;

	wkq = (struct sctp_mcore_ctrl *)arg;
	struct mbuf *m;
	int off, v6;

	/* Wait for first tickle */
	SCTP_MCORE_LOCK(wkq);
	wkq->running = 0;
	msleep(&wkq->running,
	    &wkq->core_mtx,
	    0, "wait for pkt", 0);
	SCTP_MCORE_UNLOCK(wkq);

	/* Bind to our cpu */
	thread_lock(curthread);
	sched_bind(curthread, wkq->cpuid);
	thread_unlock(curthread);

	/* Now lets start working */
	SCTP_MCORE_LOCK(wkq);
	/* Now grab lock and go */
	for (;;) {
		SCTP_MCORE_QLOCK(wkq);
skip_sleep:
		wkq->running = 1;
		qent = TAILQ_FIRST(&wkq->que);
		if (qent) {
			TAILQ_REMOVE(&wkq->que, qent, next);
			SCTP_MCORE_QUNLOCK(wkq);
			CURVNET_SET(qent->vn);
			m = qent->m;
			off = qent->off;
			v6 = qent->v6;
			SCTP_FREE(qent, SCTP_M_MCORE);
			if (v6 == 0) {
				sctp_input_with_port(m, off, 0);
			} else {
				SCTP_PRINTF("V6 not yet supported\n");
				sctp_m_freem(m);
			}
			CURVNET_RESTORE();
			SCTP_MCORE_QLOCK(wkq);
		}
		wkq->running = 0;
		if (!TAILQ_EMPTY(&wkq->que)) {
			goto skip_sleep;
		}
		SCTP_MCORE_QUNLOCK(wkq);
		msleep(&wkq->running,
		    &wkq->core_mtx,
		    0, "wait for pkt", 0);
	}
}

static void
sctp_startup_mcore_threads(void)
{
	int i, cpu;

	if (mp_ncpus == 1)
		return;

	if (sctp_mcore_workers != NULL) {
		/*
		 * Already been here in some previous vnet?
		 */
		return;
	}
	SCTP_MALLOC(sctp_mcore_workers, struct sctp_mcore_ctrl *,
	    ((mp_maxid + 1) * sizeof(struct sctp_mcore_ctrl)),
	    SCTP_M_MCORE);
	if (sctp_mcore_workers == NULL) {
		/* TSNH I hope */
		return;
	}
	memset(sctp_mcore_workers, 0, ((mp_maxid + 1) *
	    sizeof(struct sctp_mcore_ctrl)));
	/* Init the structures */
	for (i = 0; i <= mp_maxid; i++) {
		TAILQ_INIT(&sctp_mcore_workers[i].que);
		SCTP_MCORE_LOCK_INIT(&sctp_mcore_workers[i]);
		SCTP_MCORE_QLOCK_INIT(&sctp_mcore_workers[i]);
		sctp_mcore_workers[i].cpuid = i;
	}
	if (sctp_cpuarry == NULL) {
		SCTP_MALLOC(sctp_cpuarry, int *,
		    (mp_ncpus * sizeof(int)),
		    SCTP_M_MCORE);
		i = 0;
		CPU_FOREACH(cpu) {
			sctp_cpuarry[i] = cpu;
			i++;
		}
	}

	/* Now start them all */
	CPU_FOREACH(cpu) {
		(void)kproc_create(sctp_mcore_thread,
		    (void *)&sctp_mcore_workers[cpu],
		    &sctp_mcore_workers[cpu].thread_proc,
		    RFPROC,
		    SCTP_KTHREAD_PAGES,
		    SCTP_MCORE_NAME);

	}
}
#endif

void
sctp_pcb_init()
{
	/*
	 * SCTP initialization for the PCB structures should be called by
	 * the sctp_init() function.
	 */
	int i;
	struct timeval tv;

	if (SCTP_BASE_VAR(sctp_pcb_initialized) != 0) {
		/* error I was called twice */
		return;
	}
	SCTP_BASE_VAR(sctp_pcb_initialized) = 1;

#if defined(SCTP_LOCAL_TRACE_BUF)
	memset(&SCTP_BASE_SYSCTL(sctp_log), 0, sizeof(struct sctp_log));
#endif
#if defined(__FreeBSD__) && defined(SMP) && defined(SCTP_USE_PERCPU_STAT)
	SCTP_MALLOC(SCTP_BASE_STATS, struct sctpstat *,
	    ((mp_maxid + 1) * sizeof(struct sctpstat)),
	    SCTP_M_MCORE);
#endif
	(void)SCTP_GETTIME_TIMEVAL(&tv);
#if defined(__FreeBSD__) && defined(SMP) && defined(SCTP_USE_PERCPU_STAT)
	memset(SCTP_BASE_STATS, 0, sizeof(struct sctpstat) * (mp_maxid + 1));
	SCTP_BASE_STATS[PCPU_GET(cpuid)].sctps_discontinuitytime.tv_sec = (uint32_t)tv.tv_sec;
	SCTP_BASE_STATS[PCPU_GET(cpuid)].sctps_discontinuitytime.tv_usec = (uint32_t)tv.tv_usec;
#else
	memset(&SCTP_BASE_STATS, 0, sizeof(struct sctpstat));
	SCTP_BASE_STAT(sctps_discontinuitytime).tv_sec = (uint32_t)tv.tv_sec;
	SCTP_BASE_STAT(sctps_discontinuitytime).tv_usec = (uint32_t)tv.tv_usec;
#endif
	/* init the empty list of (All) Endpoints */
	LIST_INIT(&SCTP_BASE_INFO(listhead));


	/* init the hash table of endpoints */
	TUNABLE_INT_FETCH("net.inet.sctp.tcbhashsize", &SCTP_BASE_SYSCTL(sctp_hashtblsize));
	TUNABLE_INT_FETCH("net.inet.sctp.pcbhashsize", &SCTP_BASE_SYSCTL(sctp_pcbtblsize));
	TUNABLE_INT_FETCH("net.inet.sctp.chunkscale", &SCTP_BASE_SYSCTL(sctp_chunkscale));
	SCTP_BASE_INFO(sctp_asochash) = SCTP_HASH_INIT((SCTP_BASE_SYSCTL(sctp_hashtblsize) * 31),
	    &SCTP_BASE_INFO(hashasocmark));
	SCTP_BASE_INFO(sctp_ephash) = SCTP_HASH_INIT(SCTP_BASE_SYSCTL(sctp_hashtblsize),
	    &SCTP_BASE_INFO(hashmark));
	SCTP_BASE_INFO(sctp_tcpephash) = SCTP_HASH_INIT(SCTP_BASE_SYSCTL(sctp_hashtblsize),
	    &SCTP_BASE_INFO(hashtcpmark));
	SCTP_BASE_INFO(hashtblsize) = SCTP_BASE_SYSCTL(sctp_hashtblsize);


	SCTP_BASE_INFO(sctp_vrfhash) = SCTP_HASH_INIT(SCTP_SIZE_OF_VRF_HASH,
	    &SCTP_BASE_INFO(hashvrfmark));

	SCTP_BASE_INFO(vrf_ifn_hash) = SCTP_HASH_INIT(SCTP_VRF_IFN_HASH_SIZE,
	    &SCTP_BASE_INFO(vrf_ifn_hashmark));
	/* init the zones */
	/*
	 * FIX ME: Should check for NULL returns, but if it does fail we are
	 * doomed to panic anyways... add later maybe.
	 */
	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_ep), "sctp_ep",
	    sizeof(struct sctp_inpcb), maxsockets);

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_asoc), "sctp_asoc",
	    sizeof(struct sctp_tcb), sctp_max_number_of_assoc);

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_laddr), "sctp_laddr",
	    sizeof(struct sctp_laddr),
	    (sctp_max_number_of_assoc * sctp_scale_up_for_address));

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_net), "sctp_raddr",
	    sizeof(struct sctp_nets),
	    (sctp_max_number_of_assoc * sctp_scale_up_for_address));

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_chunk), "sctp_chunk",
	    sizeof(struct sctp_tmit_chunk),
	    (sctp_max_number_of_assoc * SCTP_BASE_SYSCTL(sctp_chunkscale)));

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_readq), "sctp_readq",
	    sizeof(struct sctp_queued_to_read),
	    (sctp_max_number_of_assoc * SCTP_BASE_SYSCTL(sctp_chunkscale)));

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_strmoq), "sctp_stream_msg_out",
	    sizeof(struct sctp_stream_queue_pending),
	    (sctp_max_number_of_assoc * SCTP_BASE_SYSCTL(sctp_chunkscale)));

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_asconf), "sctp_asconf",
	    sizeof(struct sctp_asconf),
	    (sctp_max_number_of_assoc * SCTP_BASE_SYSCTL(sctp_chunkscale)));

	SCTP_ZONE_INIT(SCTP_BASE_INFO(ipi_zone_asconf_ack), "sctp_asconf_ack",
	    sizeof(struct sctp_asconf_ack),
	    (sctp_max_number_of_assoc * SCTP_BASE_SYSCTL(sctp_chunkscale)));


	/* Master Lock INIT for info structure */
	SCTP_INP_INFO_LOCK_INIT();
	SCTP_STATLOG_INIT_LOCK();

	SCTP_IPI_COUNT_INIT();
	SCTP_IPI_ADDR_INIT();
#ifdef SCTP_PACKET_LOGGING
	SCTP_IP_PKTLOG_INIT();
#endif
	LIST_INIT(&SCTP_BASE_INFO(addr_wq));

	SCTP_WQ_ADDR_INIT();
	/* not sure if we need all the counts */
	SCTP_BASE_INFO(ipi_count_ep) = 0;
	/* assoc/tcb zone info */
	SCTP_BASE_INFO(ipi_count_asoc) = 0;
	/* local addrlist zone info */
	SCTP_BASE_INFO(ipi_count_laddr) = 0;
	/* remote addrlist zone info */
	SCTP_BASE_INFO(ipi_count_raddr) = 0;
	/* chunk info */
	SCTP_BASE_INFO(ipi_count_chunk) = 0;

	/* socket queue zone info */
	SCTP_BASE_INFO(ipi_count_readq) = 0;

	/* stream out queue cont */
	SCTP_BASE_INFO(ipi_count_strmoq) = 0;

	SCTP_BASE_INFO(ipi_free_strmoq) = 0;
	SCTP_BASE_INFO(ipi_free_chunks) = 0;

	SCTP_OS_TIMER_INIT(&SCTP_BASE_INFO(addr_wq_timer.timer));

	/* Init the TIMEWAIT list */
	for (i = 0; i < SCTP_STACK_VTAG_HASH_SIZE; i++) {
		LIST_INIT(&SCTP_BASE_INFO(vtag_timewait)[i]);
	}
	sctp_startup_iterator();

#if defined(__FreeBSD__) && defined(SCTP_MCORE_INPUT) && defined(SMP)
	sctp_startup_mcore_threads();
#endif

	/*
	 * INIT the default VRF which for BSD is the only one, other O/S's
	 * may have more. But initially they must start with one and then
	 * add the VRF's as addresses are added.
	 */
	sctp_init_vrf_list(SCTP_DEFAULT_VRF);
}

/*
 * Assumes that the SCTP_BASE_INFO() lock is NOT held.
 */
void
sctp_pcb_finish(void)
{
	struct sctp_vrflist *vrf_bucket;
	struct sctp_vrf *vrf, *nvrf;
	struct sctp_ifn *ifn, *nifn;
	struct sctp_ifa *ifa, *nifa;
	struct sctpvtaghead *chain;
	struct sctp_tagblock *twait_block, *prev_twait_block;
	struct sctp_laddr *wi, *nwi;
	int i;
	struct sctp_iterator *it, *nit;

	if (SCTP_BASE_VAR(sctp_pcb_initialized) == 0) {
		SCTP_PRINTF("%s: race condition on teardown.\n", __func__);
		return;
	}
	SCTP_BASE_VAR(sctp_pcb_initialized) = 0;
	/*
	 * In FreeBSD the iterator thread never exits but we do clean up.
	 * The only way FreeBSD reaches here is if we have VRF's but we
	 * still add the ifdef to make it compile on old versions.
	 */
retry:
	SCTP_IPI_ITERATOR_WQ_LOCK();
	/*
	 * sctp_iterator_worker() might be working on an it entry without
	 * holding the lock.  We won't find it on the list either and
	 * continue and free/destroy it.  While holding the lock, spin, to
	 * avoid the race condition as sctp_iterator_worker() will have to
	 * wait to re-aquire the lock.
	 */
	if (sctp_it_ctl.iterator_running != 0 || sctp_it_ctl.cur_it != NULL) {
		SCTP_IPI_ITERATOR_WQ_UNLOCK();
		SCTP_PRINTF("%s: Iterator running while we held the lock. Retry. "
		    "cur_it=%p\n", __func__, sctp_it_ctl.cur_it);
		DELAY(10);
		goto retry;
	}
	TAILQ_FOREACH_SAFE(it, &sctp_it_ctl.iteratorhead, sctp_nxt_itr, nit) {
		if (it->vn != curvnet) {
			continue;
		}
		TAILQ_REMOVE(&sctp_it_ctl.iteratorhead, it, sctp_nxt_itr);
		if (it->function_atend != NULL) {
			(*it->function_atend) (it->pointer, it->val);
		}
		SCTP_FREE(it, SCTP_M_ITER);
	}
	SCTP_IPI_ITERATOR_WQ_UNLOCK();
	SCTP_ITERATOR_LOCK();
	if ((sctp_it_ctl.cur_it) &&
	    (sctp_it_ctl.cur_it->vn == curvnet)) {
		sctp_it_ctl.iterator_flags |= SCTP_ITERATOR_STOP_CUR_IT;
	}
	SCTP_ITERATOR_UNLOCK();
	SCTP_OS_TIMER_STOP_DRAIN(&SCTP_BASE_INFO(addr_wq_timer.timer));
	SCTP_WQ_ADDR_LOCK();
	LIST_FOREACH_SAFE(wi, &SCTP_BASE_INFO(addr_wq), sctp_nxt_addr, nwi) {
		LIST_REMOVE(wi, sctp_nxt_addr);
		SCTP_DECR_LADDR_COUNT();
		if (wi->action == SCTP_DEL_IP_ADDRESS) {
			SCTP_FREE(wi->ifa, SCTP_M_IFA);
		}
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_laddr), wi);
	}
	SCTP_WQ_ADDR_UNLOCK();

	/*
	 * free the vrf/ifn/ifa lists and hashes (be sure address monitor is
	 * destroyed first).
	 */
	vrf_bucket = &SCTP_BASE_INFO(sctp_vrfhash)[(SCTP_DEFAULT_VRFID & SCTP_BASE_INFO(hashvrfmark))];
	LIST_FOREACH_SAFE(vrf, vrf_bucket, next_vrf, nvrf) {
		LIST_FOREACH_SAFE(ifn, &vrf->ifnlist, next_ifn, nifn) {
			LIST_FOREACH_SAFE(ifa, &ifn->ifalist, next_ifa, nifa) {
				/* free the ifa */
				LIST_REMOVE(ifa, next_bucket);
				LIST_REMOVE(ifa, next_ifa);
				SCTP_FREE(ifa, SCTP_M_IFA);
			}
			/* free the ifn */
			LIST_REMOVE(ifn, next_bucket);
			LIST_REMOVE(ifn, next_ifn);
			SCTP_FREE(ifn, SCTP_M_IFN);
		}
		SCTP_HASH_FREE(vrf->vrf_addr_hash, vrf->vrf_addr_hashmark);
		/* free the vrf */
		LIST_REMOVE(vrf, next_vrf);
		SCTP_FREE(vrf, SCTP_M_VRF);
	}
	/* free the vrf hashes */
	SCTP_HASH_FREE(SCTP_BASE_INFO(sctp_vrfhash), SCTP_BASE_INFO(hashvrfmark));
	SCTP_HASH_FREE(SCTP_BASE_INFO(vrf_ifn_hash), SCTP_BASE_INFO(vrf_ifn_hashmark));

	/*
	 * free the TIMEWAIT list elements malloc'd in the function
	 * sctp_add_vtag_to_timewait()...
	 */
	for (i = 0; i < SCTP_STACK_VTAG_HASH_SIZE; i++) {
		chain = &SCTP_BASE_INFO(vtag_timewait)[i];
		if (!LIST_EMPTY(chain)) {
			prev_twait_block = NULL;
			LIST_FOREACH(twait_block, chain, sctp_nxt_tagblock) {
				if (prev_twait_block) {
					SCTP_FREE(prev_twait_block, SCTP_M_TIMW);
				}
				prev_twait_block = twait_block;
			}
			SCTP_FREE(prev_twait_block, SCTP_M_TIMW);
		}
	}

	/* free the locks and mutexes */
#ifdef SCTP_PACKET_LOGGING
	SCTP_IP_PKTLOG_DESTROY();
#endif
	SCTP_IPI_ADDR_DESTROY();
	SCTP_STATLOG_DESTROY();
	SCTP_INP_INFO_LOCK_DESTROY();

	SCTP_WQ_ADDR_DESTROY();

	/* Get rid of other stuff too. */
	if (SCTP_BASE_INFO(sctp_asochash) != NULL)
		SCTP_HASH_FREE(SCTP_BASE_INFO(sctp_asochash), SCTP_BASE_INFO(hashasocmark));
	if (SCTP_BASE_INFO(sctp_ephash) != NULL)
		SCTP_HASH_FREE(SCTP_BASE_INFO(sctp_ephash), SCTP_BASE_INFO(hashmark));
	if (SCTP_BASE_INFO(sctp_tcpephash) != NULL)
		SCTP_HASH_FREE(SCTP_BASE_INFO(sctp_tcpephash), SCTP_BASE_INFO(hashtcpmark));

	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_ep));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_asoc));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_laddr));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_net));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_chunk));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_readq));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_strmoq));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_asconf));
	SCTP_ZONE_DESTROY(SCTP_BASE_INFO(ipi_zone_asconf_ack));
#if defined(__FreeBSD__) && defined(SMP) && defined(SCTP_USE_PERCPU_STAT)
	SCTP_FREE(SCTP_BASE_STATS, SCTP_M_MCORE);
#endif
}


int
sctp_load_addresses_from_init(struct sctp_tcb *stcb, struct mbuf *m,
    int offset, int limit,
    struct sockaddr *src, struct sockaddr *dst,
    struct sockaddr *altsa, uint16_t port)
{
	/*
	 * grub through the INIT pulling addresses and loading them to the
	 * nets structure in the asoc. The from address in the mbuf should
	 * also be loaded (if it is not already). This routine can be called
	 * with either INIT or INIT-ACK's as long as the m points to the IP
	 * packet and the offset points to the beginning of the parameters.
	 */
	struct sctp_inpcb *inp;
	struct sctp_nets *net, *nnet, *net_tmp;
	struct sctp_paramhdr *phdr, param_buf;
	struct sctp_tcb *stcb_tmp;
	uint16_t ptype, plen;
	struct sockaddr *sa;
	uint8_t random_store[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_auth_random *p_random = NULL;
	uint16_t random_len = 0;
	uint8_t hmacs_store[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_auth_hmac_algo *hmacs = NULL;
	uint16_t hmacs_len = 0;
	uint8_t saw_asconf = 0;
	uint8_t saw_asconf_ack = 0;
	uint8_t chunks_store[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_auth_chunk_list *chunks = NULL;
	uint16_t num_chunks = 0;
	sctp_key_t *new_key;
	uint32_t keylen;
	int got_random = 0, got_hmacs = 0, got_chklist = 0;
	uint8_t peer_supports_ecn;
	uint8_t peer_supports_prsctp;
	uint8_t peer_supports_auth;
	uint8_t peer_supports_asconf;
	uint8_t peer_supports_asconf_ack;
	uint8_t peer_supports_reconfig;
	uint8_t peer_supports_nrsack;
	uint8_t peer_supports_pktdrop;
	uint8_t peer_supports_idata;
#ifdef INET
	struct sockaddr_in sin;
#endif
#ifdef INET6
	struct sockaddr_in6 sin6;
#endif

	/* First get the destination address setup too. */
#ifdef INET
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_port = stcb->rport;
#endif
#ifdef INET6
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_port = stcb->rport;
#endif
	if (altsa) {
		sa = altsa;
	} else {
		sa = src;
	}
	peer_supports_idata = 0;
	peer_supports_ecn = 0;
	peer_supports_prsctp = 0;
	peer_supports_auth = 0;
	peer_supports_asconf = 0;
	peer_supports_reconfig = 0;
	peer_supports_nrsack = 0;
	peer_supports_pktdrop = 0;
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		/* mark all addresses that we have currently on the list */
		net->dest_state |= SCTP_ADDR_NOT_IN_ASSOC;
	}
	/* does the source address already exist? if so skip it */
	inp = stcb->sctp_ep;
	atomic_add_int(&stcb->asoc.refcnt, 1);
	stcb_tmp = sctp_findassociation_ep_addr(&inp, sa, &net_tmp, dst, stcb);
	atomic_add_int(&stcb->asoc.refcnt, -1);

	if ((stcb_tmp == NULL && inp == stcb->sctp_ep) || inp == NULL) {
		/* we must add the source address */
		/* no scope set here since we have a tcb already. */
		switch (sa->sa_family) {
#ifdef INET
		case AF_INET:
			if (stcb->asoc.scope.ipv4_addr_legal) {
				if (sctp_add_remote_addr(stcb, sa, NULL, port, SCTP_DONOT_SETSCOPE, SCTP_LOAD_ADDR_2)) {
					return (-1);
				}
			}
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (stcb->asoc.scope.ipv6_addr_legal) {
				if (sctp_add_remote_addr(stcb, sa, NULL, port, SCTP_DONOT_SETSCOPE, SCTP_LOAD_ADDR_3)) {
					return (-2);
				}
			}
			break;
#endif
		default:
			break;
		}
	} else {
		if (net_tmp != NULL && stcb_tmp == stcb) {
			net_tmp->dest_state &= ~SCTP_ADDR_NOT_IN_ASSOC;
		} else if (stcb_tmp != stcb) {
			/* It belongs to another association? */
			if (stcb_tmp)
				SCTP_TCB_UNLOCK(stcb_tmp);
			return (-3);
		}
	}
	if (stcb->asoc.state == 0) {
		/* the assoc was freed? */
		return (-4);
	}
	/* now we must go through each of the params. */
	phdr = sctp_get_next_param(m, offset, &param_buf, sizeof(param_buf));
	while (phdr) {
		ptype = ntohs(phdr->param_type);
		plen = ntohs(phdr->param_length);
		/*
		 * SCTP_PRINTF("ptype => %0x, plen => %d\n",
		 * (uint32_t)ptype, (int)plen);
		 */
		if (offset + plen > limit) {
			break;
		}
		if (plen == 0) {
			break;
		}
#ifdef INET
		if (ptype == SCTP_IPV4_ADDRESS) {
			if (stcb->asoc.scope.ipv4_addr_legal) {
				struct sctp_ipv4addr_param *p4, p4_buf;

				/* ok get the v4 address and check/add */
				phdr = sctp_get_next_param(m, offset,
				    (struct sctp_paramhdr *)&p4_buf,
				    sizeof(p4_buf));
				if (plen != sizeof(struct sctp_ipv4addr_param) ||
				    phdr == NULL) {
					return (-5);
				}
				p4 = (struct sctp_ipv4addr_param *)phdr;
				sin.sin_addr.s_addr = p4->addr;
				if (IN_MULTICAST(ntohl(sin.sin_addr.s_addr))) {
					/* Skip multi-cast addresses */
					goto next_param;
				}
				if ((sin.sin_addr.s_addr == INADDR_BROADCAST) ||
				    (sin.sin_addr.s_addr == INADDR_ANY)) {
					goto next_param;
				}
				sa = (struct sockaddr *)&sin;
				inp = stcb->sctp_ep;
				atomic_add_int(&stcb->asoc.refcnt, 1);
				stcb_tmp = sctp_findassociation_ep_addr(&inp, sa, &net,
				    dst, stcb);
				atomic_add_int(&stcb->asoc.refcnt, -1);

				if ((stcb_tmp == NULL && inp == stcb->sctp_ep) ||
				    inp == NULL) {
					/* we must add the source address */
					/*
					 * no scope set since we have a tcb
					 * already
					 */

					/*
					 * we must validate the state again
					 * here
					 */
			add_it_now:
					if (stcb->asoc.state == 0) {
						/* the assoc was freed? */
						return (-7);
					}
					if (sctp_add_remote_addr(stcb, sa, NULL, port, SCTP_DONOT_SETSCOPE, SCTP_LOAD_ADDR_4)) {
						return (-8);
					}
				} else if (stcb_tmp == stcb) {
					if (stcb->asoc.state == 0) {
						/* the assoc was freed? */
						return (-10);
					}
					if (net != NULL) {
						/* clear flag */
						net->dest_state &=
						    ~SCTP_ADDR_NOT_IN_ASSOC;
					}
				} else {
					/*
					 * strange, address is in another
					 * assoc? straighten out locks.
					 */
					if (stcb_tmp) {
						if (SCTP_GET_STATE(stcb_tmp) == SCTP_STATE_COOKIE_WAIT) {
							struct mbuf *op_err;
							char msg[SCTP_DIAG_INFO_LEN];

							/*
							 * in setup state we
							 * abort this guy
							 */
							snprintf(msg, sizeof(msg),
							    "%s:%d at %s", __FILE__, __LINE__, __func__);
							op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
							    msg);
							sctp_abort_an_association(stcb_tmp->sctp_ep,
							    stcb_tmp, op_err,
							    SCTP_SO_NOT_LOCKED);
							goto add_it_now;
						}
						SCTP_TCB_UNLOCK(stcb_tmp);
					}

					if (stcb->asoc.state == 0) {
						/* the assoc was freed? */
						return (-12);
					}
					return (-13);
				}
			}
		} else
#endif
#ifdef INET6
		if (ptype == SCTP_IPV6_ADDRESS) {
			if (stcb->asoc.scope.ipv6_addr_legal) {
				/* ok get the v6 address and check/add */
				struct sctp_ipv6addr_param *p6, p6_buf;

				phdr = sctp_get_next_param(m, offset,
				    (struct sctp_paramhdr *)&p6_buf,
				    sizeof(p6_buf));
				if (plen != sizeof(struct sctp_ipv6addr_param) ||
				    phdr == NULL) {
					return (-14);
				}
				p6 = (struct sctp_ipv6addr_param *)phdr;
				memcpy((caddr_t)&sin6.sin6_addr, p6->addr,
				    sizeof(p6->addr));
				if (IN6_IS_ADDR_MULTICAST(&sin6.sin6_addr)) {
					/* Skip multi-cast addresses */
					goto next_param;
				}
				if (IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr)) {
					/*
					 * Link local make no sense without
					 * scope
					 */
					goto next_param;
				}
				sa = (struct sockaddr *)&sin6;
				inp = stcb->sctp_ep;
				atomic_add_int(&stcb->asoc.refcnt, 1);
				stcb_tmp = sctp_findassociation_ep_addr(&inp, sa, &net,
				    dst, stcb);
				atomic_add_int(&stcb->asoc.refcnt, -1);
				if (stcb_tmp == NULL &&
				    (inp == stcb->sctp_ep || inp == NULL)) {
					/*
					 * we must validate the state again
					 * here
					 */
			add_it_now6:
					if (stcb->asoc.state == 0) {
						/* the assoc was freed? */
						return (-16);
					}
					/*
					 * we must add the address, no scope
					 * set
					 */
					if (sctp_add_remote_addr(stcb, sa, NULL, port, SCTP_DONOT_SETSCOPE, SCTP_LOAD_ADDR_5)) {
						return (-17);
					}
				} else if (stcb_tmp == stcb) {
					/*
					 * we must validate the state again
					 * here
					 */
					if (stcb->asoc.state == 0) {
						/* the assoc was freed? */
						return (-19);
					}
					if (net != NULL) {
						/* clear flag */
						net->dest_state &=
						    ~SCTP_ADDR_NOT_IN_ASSOC;
					}
				} else {
					/*
					 * strange, address is in another
					 * assoc? straighten out locks.
					 */
					if (stcb_tmp) {
						if (SCTP_GET_STATE(stcb_tmp) == SCTP_STATE_COOKIE_WAIT) {
							struct mbuf *op_err;
							char msg[SCTP_DIAG_INFO_LEN];

							/*
							 * in setup state we
							 * abort this guy
							 */
							snprintf(msg, sizeof(msg),
							    "%s:%d at %s", __FILE__, __LINE__, __func__);
							op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
							    msg);
							sctp_abort_an_association(stcb_tmp->sctp_ep,
							    stcb_tmp, op_err,
							    SCTP_SO_NOT_LOCKED);
							goto add_it_now6;
						}
						SCTP_TCB_UNLOCK(stcb_tmp);
					}
					if (stcb->asoc.state == 0) {
						/* the assoc was freed? */
						return (-21);
					}
					return (-22);
				}
			}
		} else
#endif
		if (ptype == SCTP_ECN_CAPABLE) {
			peer_supports_ecn = 1;
		} else if (ptype == SCTP_ULP_ADAPTATION) {
			if (stcb->asoc.state != SCTP_STATE_OPEN) {
				struct sctp_adaptation_layer_indication ai,
				                                *aip;

				phdr = sctp_get_next_param(m, offset,
				    (struct sctp_paramhdr *)&ai, sizeof(ai));
				aip = (struct sctp_adaptation_layer_indication *)phdr;
				if (aip) {
					stcb->asoc.peers_adaptation = ntohl(aip->indication);
					stcb->asoc.adaptation_needed = 1;
				}
			}
		} else if (ptype == SCTP_SET_PRIM_ADDR) {
			struct sctp_asconf_addr_param lstore, *fee;
			int lptype;
			struct sockaddr *lsa = NULL;
#ifdef INET
			struct sctp_asconf_addrv4_param *fii;
#endif

			if (stcb->asoc.asconf_supported == 0) {
				return (-100);
			}
			if (plen > sizeof(lstore)) {
				return (-23);
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)&lstore,
			    plen);
			if (phdr == NULL) {
				return (-24);
			}
			fee = (struct sctp_asconf_addr_param *)phdr;
			lptype = ntohs(fee->addrp.ph.param_type);
			switch (lptype) {
#ifdef INET
			case SCTP_IPV4_ADDRESS:
				if (plen !=
				    sizeof(struct sctp_asconf_addrv4_param)) {
					SCTP_PRINTF("Sizeof setprim in init/init ack not %d but %d - ignored\n",
					    (int)sizeof(struct sctp_asconf_addrv4_param),
					    plen);
				} else {
					fii = (struct sctp_asconf_addrv4_param *)fee;
					sin.sin_addr.s_addr = fii->addrp.addr;
					lsa = (struct sockaddr *)&sin;
				}
				break;
#endif
#ifdef INET6
			case SCTP_IPV6_ADDRESS:
				if (plen !=
				    sizeof(struct sctp_asconf_addr_param)) {
					SCTP_PRINTF("Sizeof setprim (v6) in init/init ack not %d but %d - ignored\n",
					    (int)sizeof(struct sctp_asconf_addr_param),
					    plen);
				} else {
					memcpy(sin6.sin6_addr.s6_addr,
					    fee->addrp.addr,
					    sizeof(fee->addrp.addr));
					lsa = (struct sockaddr *)&sin6;
				}
				break;
#endif
			default:
				break;
			}
			if (lsa) {
				(void)sctp_set_primary_addr(stcb, sa, NULL);
			}
		} else if (ptype == SCTP_HAS_NAT_SUPPORT) {
			stcb->asoc.peer_supports_nat = 1;
		} else if (ptype == SCTP_PRSCTP_SUPPORTED) {
			/* Peer supports pr-sctp */
			peer_supports_prsctp = 1;
		} else if (ptype == SCTP_SUPPORTED_CHUNK_EXT) {
			/* A supported extension chunk */
			struct sctp_supported_chunk_types_param *pr_supported;
			uint8_t local_store[SCTP_PARAM_BUFFER_SIZE];
			int num_ent, i;

			if (plen > sizeof(local_store)) {
				return (-35);
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)&local_store, plen);
			if (phdr == NULL) {
				return (-25);
			}
			pr_supported = (struct sctp_supported_chunk_types_param *)phdr;
			num_ent = plen - sizeof(struct sctp_paramhdr);
			for (i = 0; i < num_ent; i++) {
				switch (pr_supported->chunk_types[i]) {
				case SCTP_ASCONF:
					peer_supports_asconf = 1;
					break;
				case SCTP_ASCONF_ACK:
					peer_supports_asconf_ack = 1;
					break;
				case SCTP_FORWARD_CUM_TSN:
					peer_supports_prsctp = 1;
					break;
				case SCTP_PACKET_DROPPED:
					peer_supports_pktdrop = 1;
					break;
				case SCTP_NR_SELECTIVE_ACK:
					peer_supports_nrsack = 1;
					break;
				case SCTP_STREAM_RESET:
					peer_supports_reconfig = 1;
					break;
				case SCTP_AUTHENTICATION:
					peer_supports_auth = 1;
					break;
				case SCTP_IDATA:
					peer_supports_idata = 1;
					break;
				default:
					/* one I have not learned yet */
					break;

				}
			}
		} else if (ptype == SCTP_RANDOM) {
			if (plen > sizeof(random_store))
				break;
			if (got_random) {
				/* already processed a RANDOM */
				goto next_param;
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)random_store,
			    plen);
			if (phdr == NULL)
				return (-26);
			p_random = (struct sctp_auth_random *)phdr;
			random_len = plen - sizeof(*p_random);
			/* enforce the random length */
			if (random_len != SCTP_AUTH_RANDOM_SIZE_REQUIRED) {
				SCTPDBG(SCTP_DEBUG_AUTH1, "SCTP: invalid RANDOM len\n");
				return (-27);
			}
			got_random = 1;
		} else if (ptype == SCTP_HMAC_LIST) {
			uint16_t num_hmacs;
			uint16_t i;

			if (plen > sizeof(hmacs_store))
				break;
			if (got_hmacs) {
				/* already processed a HMAC list */
				goto next_param;
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)hmacs_store,
			    plen);
			if (phdr == NULL)
				return (-28);
			hmacs = (struct sctp_auth_hmac_algo *)phdr;
			hmacs_len = plen - sizeof(*hmacs);
			num_hmacs = hmacs_len / sizeof(hmacs->hmac_ids[0]);
			/* validate the hmac list */
			if (sctp_verify_hmac_param(hmacs, num_hmacs)) {
				return (-29);
			}
			if (stcb->asoc.peer_hmacs != NULL)
				sctp_free_hmaclist(stcb->asoc.peer_hmacs);
			stcb->asoc.peer_hmacs = sctp_alloc_hmaclist(num_hmacs);
			if (stcb->asoc.peer_hmacs != NULL) {
				for (i = 0; i < num_hmacs; i++) {
					(void)sctp_auth_add_hmacid(stcb->asoc.peer_hmacs,
					    ntohs(hmacs->hmac_ids[i]));
				}
			}
			got_hmacs = 1;
		} else if (ptype == SCTP_CHUNK_LIST) {
			int i;

			if (plen > sizeof(chunks_store))
				break;
			if (got_chklist) {
				/* already processed a Chunks list */
				goto next_param;
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)chunks_store,
			    plen);
			if (phdr == NULL)
				return (-30);
			chunks = (struct sctp_auth_chunk_list *)phdr;
			num_chunks = plen - sizeof(*chunks);
			if (stcb->asoc.peer_auth_chunks != NULL)
				sctp_clear_chunklist(stcb->asoc.peer_auth_chunks);
			else
				stcb->asoc.peer_auth_chunks = sctp_alloc_chunklist();
			for (i = 0; i < num_chunks; i++) {
				(void)sctp_auth_add_chunk(chunks->chunk_types[i],
				    stcb->asoc.peer_auth_chunks);
				/* record asconf/asconf-ack if listed */
				if (chunks->chunk_types[i] == SCTP_ASCONF)
					saw_asconf = 1;
				if (chunks->chunk_types[i] == SCTP_ASCONF_ACK)
					saw_asconf_ack = 1;

			}
			got_chklist = 1;
		} else if ((ptype == SCTP_HEARTBEAT_INFO) ||
			    (ptype == SCTP_STATE_COOKIE) ||
			    (ptype == SCTP_UNRECOG_PARAM) ||
			    (ptype == SCTP_COOKIE_PRESERVE) ||
			    (ptype == SCTP_SUPPORTED_ADDRTYPE) ||
			    (ptype == SCTP_ADD_IP_ADDRESS) ||
			    (ptype == SCTP_DEL_IP_ADDRESS) ||
			    (ptype == SCTP_ERROR_CAUSE_IND) ||
		    (ptype == SCTP_SUCCESS_REPORT)) {
			 /* don't care */ ;
		} else {
			if ((ptype & 0x8000) == 0x0000) {
				/*
				 * must stop processing the rest of the
				 * param's. Any report bits were handled
				 * with the call to
				 * sctp_arethere_unrecognized_parameters()
				 * when the INIT or INIT-ACK was first seen.
				 */
				break;
			}
		}

next_param:
		offset += SCTP_SIZE32(plen);
		if (offset >= limit) {
			break;
		}
		phdr = sctp_get_next_param(m, offset, &param_buf,
		    sizeof(param_buf));
	}
	/* Now check to see if we need to purge any addresses */
	TAILQ_FOREACH_SAFE(net, &stcb->asoc.nets, sctp_next, nnet) {
		if ((net->dest_state & SCTP_ADDR_NOT_IN_ASSOC) ==
		    SCTP_ADDR_NOT_IN_ASSOC) {
			/* This address has been removed from the asoc */
			/* remove and free it */
			stcb->asoc.numnets--;
			TAILQ_REMOVE(&stcb->asoc.nets, net, sctp_next);
			sctp_free_remote_addr(net);
			if (net == stcb->asoc.primary_destination) {
				stcb->asoc.primary_destination = NULL;
				sctp_select_primary_destination(stcb);
			}
		}
	}
	if ((stcb->asoc.ecn_supported == 1) &&
	    (peer_supports_ecn == 0)) {
		stcb->asoc.ecn_supported = 0;
	}
	if ((stcb->asoc.prsctp_supported == 1) &&
	    (peer_supports_prsctp == 0)) {
		stcb->asoc.prsctp_supported = 0;
	}
	if ((stcb->asoc.auth_supported == 1) &&
	    ((peer_supports_auth == 0) ||
	    (got_random == 0) || (got_hmacs == 0))) {
		stcb->asoc.auth_supported = 0;
	}
	if ((stcb->asoc.asconf_supported == 1) &&
	    ((peer_supports_asconf == 0) || (peer_supports_asconf_ack == 0) ||
	    (stcb->asoc.auth_supported == 0) ||
	    (saw_asconf == 0) || (saw_asconf_ack == 0))) {
		stcb->asoc.asconf_supported = 0;
	}
	if ((stcb->asoc.reconfig_supported == 1) &&
	    (peer_supports_reconfig == 0)) {
		stcb->asoc.reconfig_supported = 0;
	}
	if ((stcb->asoc.idata_supported == 1) &&
	    (peer_supports_idata == 0)) {
		stcb->asoc.idata_supported = 0;
	}
	if ((stcb->asoc.nrsack_supported == 1) &&
	    (peer_supports_nrsack == 0)) {
		stcb->asoc.nrsack_supported = 0;
	}
	if ((stcb->asoc.pktdrop_supported == 1) &&
	    (peer_supports_pktdrop == 0)) {
		stcb->asoc.pktdrop_supported = 0;
	}
	/* validate authentication required parameters */
	if ((peer_supports_auth == 0) && (got_chklist == 1)) {
		/* peer does not support auth but sent a chunks list? */
		return (-31);
	}
	if ((peer_supports_asconf == 1) && (peer_supports_auth == 0)) {
		/* peer supports asconf but not auth? */
		return (-32);
	} else if ((peer_supports_asconf == 1) &&
		    (peer_supports_auth == 1) &&
	    ((saw_asconf == 0) || (saw_asconf_ack == 0))) {
		return (-33);
	}
	/* concatenate the full random key */
	keylen = sizeof(*p_random) + random_len + sizeof(*hmacs) + hmacs_len;
	if (chunks != NULL) {
		keylen += sizeof(*chunks) + num_chunks;
	}
	new_key = sctp_alloc_key(keylen);
	if (new_key != NULL) {
		/* copy in the RANDOM */
		if (p_random != NULL) {
			keylen = sizeof(*p_random) + random_len;
			memcpy(new_key->key, p_random, keylen);
		} else {
			keylen = 0;
		}
		/* append in the AUTH chunks */
		if (chunks != NULL) {
			memcpy(new_key->key + keylen, chunks,
			    sizeof(*chunks) + num_chunks);
			keylen += sizeof(*chunks) + num_chunks;
		}
		/* append in the HMACs */
		if (hmacs != NULL) {
			memcpy(new_key->key + keylen, hmacs,
			    sizeof(*hmacs) + hmacs_len);
		}
	} else {
		/* failed to get memory for the key */
		return (-34);
	}
	if (stcb->asoc.authinfo.peer_random != NULL)
		sctp_free_key(stcb->asoc.authinfo.peer_random);
	stcb->asoc.authinfo.peer_random = new_key;
	sctp_clear_cachedkeys(stcb, stcb->asoc.authinfo.assoc_keyid);
	sctp_clear_cachedkeys(stcb, stcb->asoc.authinfo.recv_keyid);

	return (0);
}

int
sctp_set_primary_addr(struct sctp_tcb *stcb, struct sockaddr *sa,
    struct sctp_nets *net)
{
	/* make sure the requested primary address exists in the assoc */
	if (net == NULL && sa)
		net = sctp_findnet(stcb, sa);

	if (net == NULL) {
		/* didn't find the requested primary address! */
		return (-1);
	} else {
		/* set the primary address */
		if (net->dest_state & SCTP_ADDR_UNCONFIRMED) {
			/* Must be confirmed, so queue to set */
			net->dest_state |= SCTP_ADDR_REQ_PRIMARY;
			return (0);
		}
		stcb->asoc.primary_destination = net;
		if (!(net->dest_state & SCTP_ADDR_PF) && (stcb->asoc.alternate)) {
			sctp_free_remote_addr(stcb->asoc.alternate);
			stcb->asoc.alternate = NULL;
		}
		net = TAILQ_FIRST(&stcb->asoc.nets);
		if (net != stcb->asoc.primary_destination) {
			/*
			 * first one on the list is NOT the primary
			 * sctp_cmpaddr() is much more efficient if the
			 * primary is the first on the list, make it so.
			 */
			TAILQ_REMOVE(&stcb->asoc.nets, stcb->asoc.primary_destination, sctp_next);
			TAILQ_INSERT_HEAD(&stcb->asoc.nets, stcb->asoc.primary_destination, sctp_next);
		}
		return (0);
	}
}

int
sctp_is_vtag_good(uint32_t tag, uint16_t lport, uint16_t rport, struct timeval *now)
{
	/*
	 * This function serves two purposes. It will see if a TAG can be
	 * re-used and return 1 for yes it is ok and 0 for don't use that
	 * tag. A secondary function it will do is purge out old tags that
	 * can be removed.
	 */
	struct sctpvtaghead *chain;
	struct sctp_tagblock *twait_block;
	struct sctpasochead *head;
	struct sctp_tcb *stcb;
	int i;

	SCTP_INP_INFO_RLOCK();
	head = &SCTP_BASE_INFO(sctp_asochash)[SCTP_PCBHASH_ASOC(tag,
	    SCTP_BASE_INFO(hashasocmark))];
	LIST_FOREACH(stcb, head, sctp_asocs) {
		/*
		 * We choose not to lock anything here. TCB's can't be
		 * removed since we have the read lock, so they can't be
		 * freed on us, same thing for the INP. I may be wrong with
		 * this assumption, but we will go with it for now :-)
		 */
		if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) {
			continue;
		}
		if (stcb->asoc.my_vtag == tag) {
			/* candidate */
			if (stcb->rport != rport) {
				continue;
			}
			if (stcb->sctp_ep->sctp_lport != lport) {
				continue;
			}
			/* Its a used tag set */
			SCTP_INP_INFO_RUNLOCK();
			return (0);
		}
	}
	chain = &SCTP_BASE_INFO(vtag_timewait)[(tag % SCTP_STACK_VTAG_HASH_SIZE)];
	/* Now what about timed wait ? */
	LIST_FOREACH(twait_block, chain, sctp_nxt_tagblock) {
		/*
		 * Block(s) are present, lets see if we have this tag in the
		 * list
		 */
		for (i = 0; i < SCTP_NUMBER_IN_VTAG_BLOCK; i++) {
			if (twait_block->vtag_block[i].v_tag == 0) {
				/* not used */
				continue;
			} else if ((long)twait_block->vtag_block[i].tv_sec_at_expire <
			    now->tv_sec) {
				/* Audit expires this guy */
				twait_block->vtag_block[i].tv_sec_at_expire = 0;
				twait_block->vtag_block[i].v_tag = 0;
				twait_block->vtag_block[i].lport = 0;
				twait_block->vtag_block[i].rport = 0;
			} else if ((twait_block->vtag_block[i].v_tag == tag) &&
				    (twait_block->vtag_block[i].lport == lport) &&
			    (twait_block->vtag_block[i].rport == rport)) {
				/* Bad tag, sorry :< */
				SCTP_INP_INFO_RUNLOCK();
				return (0);
			}
		}
	}
	SCTP_INP_INFO_RUNLOCK();
	return (1);
}

static void
sctp_drain_mbufs(struct sctp_tcb *stcb)
{
	/*
	 * We must hunt this association for MBUF's past the cumack (i.e.
	 * out of order data that we can renege on).
	 */
	struct sctp_association *asoc;
	struct sctp_tmit_chunk *chk, *nchk;
	uint32_t cumulative_tsn_p1;
	struct sctp_queued_to_read *control, *ncontrol;
	int cnt, strmat;
	uint32_t gap, i;
	int fnd = 0;

	/* We look for anything larger than the cum-ack + 1 */

	asoc = &stcb->asoc;
	if (asoc->cumulative_tsn == asoc->highest_tsn_inside_map) {
		/* none we can reneg on. */
		return;
	}
	SCTP_STAT_INCR(sctps_protocol_drains_done);
	cumulative_tsn_p1 = asoc->cumulative_tsn + 1;
	cnt = 0;
	/* Ok that was fun, now we will drain all the inbound streams? */
	for (strmat = 0; strmat < asoc->streamincnt; strmat++) {
		TAILQ_FOREACH_SAFE(control, &asoc->strmin[strmat].inqueue, next_instrm, ncontrol) {
#ifdef INVARIANTS
			if (control->on_strm_q != SCTP_ON_ORDERED) {
				panic("Huh control: %p on_q: %d -- not ordered?",
				    control, control->on_strm_q);
			}
#endif
			if (SCTP_TSN_GT(control->sinfo_tsn, cumulative_tsn_p1)) {
				/* Yep it is above cum-ack */
				cnt++;
				SCTP_CALC_TSN_TO_GAP(gap, control->sinfo_tsn, asoc->mapping_array_base_tsn);
				KASSERT(control->length > 0, ("control has zero length"));
				if (asoc->size_on_all_streams >= control->length) {
					asoc->size_on_all_streams -= control->length;
				} else {
#ifdef INVARIANTS
					panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
					asoc->size_on_all_streams = 0;
#endif
				}
				sctp_ucount_decr(asoc->cnt_on_all_streams);
				SCTP_UNSET_TSN_PRESENT(asoc->mapping_array, gap);
				if (control->on_read_q) {
					TAILQ_REMOVE(&stcb->sctp_ep->read_queue, control, next);
					control->on_read_q = 0;
				}
				TAILQ_REMOVE(&asoc->strmin[strmat].inqueue, control, next_instrm);
				control->on_strm_q = 0;
				if (control->data) {
					sctp_m_freem(control->data);
					control->data = NULL;
				}
				sctp_free_remote_addr(control->whoFrom);
				/* Now its reasm? */
				TAILQ_FOREACH_SAFE(chk, &control->reasm, sctp_next, nchk) {
					cnt++;
					SCTP_CALC_TSN_TO_GAP(gap, chk->rec.data.tsn, asoc->mapping_array_base_tsn);
					KASSERT(chk->send_size > 0, ("chunk has zero length"));
					if (asoc->size_on_reasm_queue >= chk->send_size) {
						asoc->size_on_reasm_queue -= chk->send_size;
					} else {
#ifdef INVARIANTS
						panic("size_on_reasm_queue = %u smaller than chunk length %u", asoc->size_on_reasm_queue, chk->send_size);
#else
						asoc->size_on_reasm_queue = 0;
#endif
					}
					sctp_ucount_decr(asoc->cnt_on_reasm_queue);
					SCTP_UNSET_TSN_PRESENT(asoc->mapping_array, gap);
					TAILQ_REMOVE(&control->reasm, chk, sctp_next);
					if (chk->data) {
						sctp_m_freem(chk->data);
						chk->data = NULL;
					}
					sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
				}
				sctp_free_a_readq(stcb, control);
			}
		}
		TAILQ_FOREACH_SAFE(control, &asoc->strmin[strmat].uno_inqueue, next_instrm, ncontrol) {
#ifdef INVARIANTS
			if (control->on_strm_q != SCTP_ON_UNORDERED) {
				panic("Huh control: %p on_q: %d -- not unordered?",
				    control, control->on_strm_q);
			}
#endif
			if (SCTP_TSN_GT(control->sinfo_tsn, cumulative_tsn_p1)) {
				/* Yep it is above cum-ack */
				cnt++;
				SCTP_CALC_TSN_TO_GAP(gap, control->sinfo_tsn, asoc->mapping_array_base_tsn);
				KASSERT(control->length > 0, ("control has zero length"));
				if (asoc->size_on_all_streams >= control->length) {
					asoc->size_on_all_streams -= control->length;
				} else {
#ifdef INVARIANTS
					panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
					asoc->size_on_all_streams = 0;
#endif
				}
				sctp_ucount_decr(asoc->cnt_on_all_streams);
				SCTP_UNSET_TSN_PRESENT(asoc->mapping_array, gap);
				if (control->on_read_q) {
					TAILQ_REMOVE(&stcb->sctp_ep->read_queue, control, next);
					control->on_read_q = 0;
				}
				TAILQ_REMOVE(&asoc->strmin[strmat].uno_inqueue, control, next_instrm);
				control->on_strm_q = 0;
				if (control->data) {
					sctp_m_freem(control->data);
					control->data = NULL;
				}
				sctp_free_remote_addr(control->whoFrom);
				/* Now its reasm? */
				TAILQ_FOREACH_SAFE(chk, &control->reasm, sctp_next, nchk) {
					cnt++;
					SCTP_CALC_TSN_TO_GAP(gap, chk->rec.data.tsn, asoc->mapping_array_base_tsn);
					KASSERT(chk->send_size > 0, ("chunk has zero length"));
					if (asoc->size_on_reasm_queue >= chk->send_size) {
						asoc->size_on_reasm_queue -= chk->send_size;
					} else {
#ifdef INVARIANTS
						panic("size_on_reasm_queue = %u smaller than chunk length %u", asoc->size_on_reasm_queue, chk->send_size);
#else
						asoc->size_on_reasm_queue = 0;
#endif
					}
					sctp_ucount_decr(asoc->cnt_on_reasm_queue);
					SCTP_UNSET_TSN_PRESENT(asoc->mapping_array, gap);
					TAILQ_REMOVE(&control->reasm, chk, sctp_next);
					if (chk->data) {
						sctp_m_freem(chk->data);
						chk->data = NULL;
					}
					sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
				}
				sctp_free_a_readq(stcb, control);
			}
		}
	}
	if (cnt) {
		/* We must back down to see what the new highest is */
		for (i = asoc->highest_tsn_inside_map; SCTP_TSN_GE(i, asoc->mapping_array_base_tsn); i--) {
			SCTP_CALC_TSN_TO_GAP(gap, i, asoc->mapping_array_base_tsn);
			if (SCTP_IS_TSN_PRESENT(asoc->mapping_array, gap)) {
				asoc->highest_tsn_inside_map = i;
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
			asoc->highest_tsn_inside_map = asoc->mapping_array_base_tsn - 1;
		}

		/*
		 * Question, should we go through the delivery queue? The
		 * only reason things are on here is the app not reading OR
		 * a p-d-api up. An attacker COULD send enough in to
		 * initiate the PD-API and then send a bunch of stuff to
		 * other streams... these would wind up on the delivery
		 * queue.. and then we would not get to them. But in order
		 * to do this I then have to back-track and un-deliver
		 * sequence numbers in streams.. el-yucko. I think for now
		 * we will NOT look at the delivery queue and leave it to be
		 * something to consider later. An alternative would be to
		 * abort the P-D-API with a notification and then deliver
		 * the data.... Or another method might be to keep track of
		 * how many times the situation occurs and if we see a
		 * possible attack underway just abort the association.
		 */
#ifdef SCTP_DEBUG
		SCTPDBG(SCTP_DEBUG_PCB1, "Freed %d chunks from reneg harvest\n", cnt);
#endif
		/*
		 * Now do we need to find a new
		 * asoc->highest_tsn_inside_map?
		 */
		asoc->last_revoke_count = cnt;
		(void)SCTP_OS_TIMER_STOP(&stcb->asoc.dack_timer.timer);
		/* sa_ignore NO_NULL_CHK */
		sctp_send_sack(stcb, SCTP_SO_NOT_LOCKED);
		sctp_chunk_output(stcb->sctp_ep, stcb, SCTP_OUTPUT_FROM_DRAIN, SCTP_SO_NOT_LOCKED);
	}
	/*
	 * Another issue, in un-setting the TSN's in the mapping array we
	 * DID NOT adjust the highest_tsn marker.  This will cause one of
	 * two things to occur. It may cause us to do extra work in checking
	 * for our mapping array movement. More importantly it may cause us
	 * to SACK every datagram. This may not be a bad thing though since
	 * we will recover once we get our cum-ack above and all this stuff
	 * we dumped recovered.
	 */
}

void
sctp_drain()
{
	/*
	 * We must walk the PCB lists for ALL associations here. The system
	 * is LOW on MBUF's and needs help. This is where reneging will
	 * occur. We really hope this does NOT happen!
	 */
	VNET_ITERATOR_DECL(vnet_iter);
	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		struct sctp_inpcb *inp;
		struct sctp_tcb *stcb;

		SCTP_STAT_INCR(sctps_protocol_drain_calls);
		if (SCTP_BASE_SYSCTL(sctp_do_drain) == 0) {
#ifdef VIMAGE
			continue;
#else
			return;
#endif
		}
		SCTP_INP_INFO_RLOCK();
		LIST_FOREACH(inp, &SCTP_BASE_INFO(listhead), sctp_list) {
			/* For each endpoint */
			SCTP_INP_RLOCK(inp);
			LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
				/* For each association */
				SCTP_TCB_LOCK(stcb);
				sctp_drain_mbufs(stcb);
				SCTP_TCB_UNLOCK(stcb);
			}
			SCTP_INP_RUNLOCK(inp);
		}
		SCTP_INP_INFO_RUNLOCK();
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

/*
 * start a new iterator
 * iterates through all endpoints and associations based on the pcb_state
 * flags and asoc_state.  "af" (mandatory) is executed for all matching
 * assocs and "ef" (optional) is executed when the iterator completes.
 * "inpf" (optional) is executed for each new endpoint as it is being
 * iterated through. inpe (optional) is called when the inp completes
 * its way through all the stcbs.
 */
int
sctp_initiate_iterator(inp_func inpf,
    asoc_func af,
    inp_func inpe,
    uint32_t pcb_state,
    uint32_t pcb_features,
    uint32_t asoc_state,
    void *argp,
    uint32_t argi,
    end_func ef,
    struct sctp_inpcb *s_inp,
    uint8_t chunk_output_off)
{
	struct sctp_iterator *it = NULL;

	if (af == NULL) {
		return (-1);
	}
	if (SCTP_BASE_VAR(sctp_pcb_initialized) == 0) {
		SCTP_PRINTF("%s: abort on initialize being %d\n", __func__,
		    SCTP_BASE_VAR(sctp_pcb_initialized));
		return (-1);
	}
	SCTP_MALLOC(it, struct sctp_iterator *, sizeof(struct sctp_iterator),
	    SCTP_M_ITER);
	if (it == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_PCB, ENOMEM);
		return (ENOMEM);
	}
	memset(it, 0, sizeof(*it));
	it->function_assoc = af;
	it->function_inp = inpf;
	if (inpf)
		it->done_current_ep = 0;
	else
		it->done_current_ep = 1;
	it->function_atend = ef;
	it->pointer = argp;
	it->val = argi;
	it->pcb_flags = pcb_state;
	it->pcb_features = pcb_features;
	it->asoc_state = asoc_state;
	it->function_inp_end = inpe;
	it->no_chunk_output = chunk_output_off;
	it->vn = curvnet;
	if (s_inp) {
		/* Assume lock is held here */
		it->inp = s_inp;
		SCTP_INP_INCR_REF(it->inp);
		it->iterator_flags = SCTP_ITERATOR_DO_SINGLE_INP;
	} else {
		SCTP_INP_INFO_RLOCK();
		it->inp = LIST_FIRST(&SCTP_BASE_INFO(listhead));
		if (it->inp) {
			SCTP_INP_INCR_REF(it->inp);
		}
		SCTP_INP_INFO_RUNLOCK();
		it->iterator_flags = SCTP_ITERATOR_DO_ALL_INP;

	}
	SCTP_IPI_ITERATOR_WQ_LOCK();
	if (SCTP_BASE_VAR(sctp_pcb_initialized) == 0) {
		SCTP_IPI_ITERATOR_WQ_UNLOCK();
		SCTP_PRINTF("%s: rollback on initialize being %d it=%p\n", __func__,
		    SCTP_BASE_VAR(sctp_pcb_initialized), it);
		SCTP_FREE(it, SCTP_M_ITER);
		return (-1);
	}
	TAILQ_INSERT_TAIL(&sctp_it_ctl.iteratorhead, it, sctp_nxt_itr);
	if (sctp_it_ctl.iterator_running == 0) {
		sctp_wakeup_iterator();
	}
	SCTP_IPI_ITERATOR_WQ_UNLOCK();
	/* sa_ignore MEMLEAK {memory is put on the tailq for the iterator} */
	return (0);
}
