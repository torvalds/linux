/*	$NetBSD: if_bridgevar.h,v 1.4 2003/07/08 07:13:50 itojun Exp $	*/

/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * OpenBSD: if_bridge.h,v 1.14 2001/03/22 03:48:29 jason Exp
 *
 * $FreeBSD$
 */

/*
 * Data structure and control definitions for bridge interfaces.
 */

#include <sys/callout.h>
#include <sys/queue.h>
#include <sys/condvar.h>

/*
 * Commands used in the SIOCSDRVSPEC ioctl.  Note the lookup of the
 * bridge interface itself is keyed off the ifdrv structure.
 */
#define	BRDGADD			0	/* add bridge member (ifbreq) */
#define	BRDGDEL			1	/* delete bridge member (ifbreq) */
#define	BRDGGIFFLGS		2	/* get member if flags (ifbreq) */
#define	BRDGSIFFLGS		3	/* set member if flags (ifbreq) */
#define	BRDGSCACHE		4	/* set cache size (ifbrparam) */
#define	BRDGGCACHE		5	/* get cache size (ifbrparam) */
#define	BRDGGIFS		6	/* get member list (ifbifconf) */
#define	BRDGRTS			7	/* get address list (ifbaconf) */
#define	BRDGSADDR		8	/* set static address (ifbareq) */
#define	BRDGSTO			9	/* set cache timeout (ifbrparam) */
#define	BRDGGTO			10	/* get cache timeout (ifbrparam) */
#define	BRDGDADDR		11	/* delete address (ifbareq) */
#define	BRDGFLUSH		12	/* flush address cache (ifbreq) */

#define	BRDGGPRI		13	/* get priority (ifbrparam) */
#define	BRDGSPRI		14	/* set priority (ifbrparam) */
#define	BRDGGHT			15	/* get hello time (ifbrparam) */
#define	BRDGSHT			16	/* set hello time (ifbrparam) */
#define	BRDGGFD			17	/* get forward delay (ifbrparam) */
#define	BRDGSFD			18	/* set forward delay (ifbrparam) */
#define	BRDGGMA			19	/* get max age (ifbrparam) */
#define	BRDGSMA			20	/* set max age (ifbrparam) */
#define	BRDGSIFPRIO		21	/* set if priority (ifbreq) */
#define	BRDGSIFCOST		22	/* set if path cost (ifbreq) */
#define	BRDGADDS		23	/* add bridge span member (ifbreq) */
#define	BRDGDELS		24	/* delete bridge span member (ifbreq) */
#define	BRDGPARAM		25	/* get bridge STP params (ifbropreq) */
#define	BRDGGRTE		26	/* get cache drops (ifbrparam) */
#define	BRDGGIFSSTP		27	/* get member STP params list
					 * (ifbpstpconf) */
#define	BRDGSPROTO		28	/* set protocol (ifbrparam) */
#define	BRDGSTXHC		29	/* set tx hold count (ifbrparam) */
#define	BRDGSIFAMAX		30	/* set max interface addrs (ifbreq) */

/*
 * Generic bridge control request.
 */
struct ifbreq {
	char		ifbr_ifsname[IFNAMSIZ];	/* member if name */
	uint32_t	ifbr_ifsflags;		/* member if flags */
	uint32_t	ifbr_stpflags;		/* member if STP flags */
	uint32_t	ifbr_path_cost;		/* member if STP cost */
	uint8_t		ifbr_portno;		/* member if port number */
	uint8_t		ifbr_priority;		/* member if STP priority */
	uint8_t		ifbr_proto;		/* member if STP protocol */
	uint8_t		ifbr_role;		/* member if STP role */
	uint8_t		ifbr_state;		/* member if STP state */
	uint32_t	ifbr_addrcnt;		/* member if addr number */
	uint32_t	ifbr_addrmax;		/* member if addr max */
	uint32_t	ifbr_addrexceeded;	/* member if addr violations */
	uint8_t		pad[32];
};

/* BRDGGIFFLAGS, BRDGSIFFLAGS */
#define	IFBIF_LEARNING		0x0001	/* if can learn */
#define	IFBIF_DISCOVER		0x0002	/* if sends packets w/ unknown dest. */
#define	IFBIF_STP		0x0004	/* if participates in spanning tree */
#define	IFBIF_SPAN		0x0008	/* if is a span port */
#define	IFBIF_STICKY		0x0010	/* if learned addresses stick */
#define	IFBIF_BSTP_EDGE		0x0020	/* member stp edge port */
#define	IFBIF_BSTP_AUTOEDGE	0x0040	/* member stp autoedge enabled */
#define	IFBIF_BSTP_PTP		0x0080	/* member stp point to point */
#define	IFBIF_BSTP_AUTOPTP	0x0100	/* member stp autoptp enabled */
#define	IFBIF_BSTP_ADMEDGE	0x0200	/* member stp admin edge enabled */
#define	IFBIF_BSTP_ADMCOST	0x0400	/* member stp admin path cost */
#define	IFBIF_PRIVATE		0x0800	/* if is a private segment */

#define	IFBIFBITS	"\020\001LEARNING\002DISCOVER\003STP\004SPAN" \
			"\005STICKY\014PRIVATE\006EDGE\007AUTOEDGE\010PTP" \
			"\011AUTOPTP"
#define	IFBIFMASK	~(IFBIF_BSTP_EDGE|IFBIF_BSTP_AUTOEDGE|IFBIF_BSTP_PTP| \
			IFBIF_BSTP_AUTOPTP|IFBIF_BSTP_ADMEDGE| \
			IFBIF_BSTP_ADMCOST)	/* not saved */

/* BRDGFLUSH */
#define	IFBF_FLUSHDYN		0x00	/* flush learned addresses only */
#define	IFBF_FLUSHALL		0x01	/* flush all addresses */

/*
 * Interface list structure.
 */
struct ifbifconf {
	uint32_t	ifbic_len;	/* buffer size */
	union {
		caddr_t	ifbicu_buf;
		struct ifbreq *ifbicu_req;
	} ifbic_ifbicu;
#define	ifbic_buf	ifbic_ifbicu.ifbicu_buf
#define	ifbic_req	ifbic_ifbicu.ifbicu_req
};

/*
 * Bridge address request.
 */
struct ifbareq {
	char		ifba_ifsname[IFNAMSIZ];	/* member if name */
	unsigned long	ifba_expire;		/* address expire time */
	uint8_t		ifba_flags;		/* address flags */
	uint8_t		ifba_dst[ETHER_ADDR_LEN];/* destination address */
	uint16_t	ifba_vlan;		/* vlan id */
};

#define	IFBAF_TYPEMASK	0x03	/* address type mask */
#define	IFBAF_DYNAMIC	0x00	/* dynamically learned address */
#define	IFBAF_STATIC	0x01	/* static address */
#define	IFBAF_STICKY	0x02	/* sticky address */

#define	IFBAFBITS	"\020\1STATIC\2STICKY"

/*
 * Address list structure.
 */
struct ifbaconf {
	uint32_t	ifbac_len;	/* buffer size */
	union {
		caddr_t ifbacu_buf;
		struct ifbareq *ifbacu_req;
	} ifbac_ifbacu;
#define	ifbac_buf	ifbac_ifbacu.ifbacu_buf
#define	ifbac_req	ifbac_ifbacu.ifbacu_req
};

/*
 * Bridge parameter structure.
 */
struct ifbrparam {
	union {
		uint32_t ifbrpu_int32;
		uint16_t ifbrpu_int16;
		uint8_t ifbrpu_int8;
	} ifbrp_ifbrpu;
};
#define	ifbrp_csize	ifbrp_ifbrpu.ifbrpu_int32	/* cache size */
#define	ifbrp_ctime	ifbrp_ifbrpu.ifbrpu_int32	/* cache time (sec) */
#define	ifbrp_prio	ifbrp_ifbrpu.ifbrpu_int16	/* bridge priority */
#define	ifbrp_proto	ifbrp_ifbrpu.ifbrpu_int8	/* bridge protocol */
#define	ifbrp_txhc	ifbrp_ifbrpu.ifbrpu_int8	/* bpdu tx holdcount */
#define	ifbrp_hellotime	ifbrp_ifbrpu.ifbrpu_int8	/* hello time (sec) */
#define	ifbrp_fwddelay	ifbrp_ifbrpu.ifbrpu_int8	/* fwd time (sec) */
#define	ifbrp_maxage	ifbrp_ifbrpu.ifbrpu_int8	/* max age (sec) */
#define	ifbrp_cexceeded ifbrp_ifbrpu.ifbrpu_int32	/* # of cache dropped
							 * adresses */
/*
 * Bridge current operational parameters structure.
 */
struct ifbropreq {
	uint8_t		ifbop_holdcount;
	uint8_t		ifbop_maxage;
	uint8_t		ifbop_hellotime;
	uint8_t		ifbop_fwddelay;
	uint8_t		ifbop_protocol;
	uint16_t	ifbop_priority;
	uint16_t	ifbop_root_port;
	uint32_t	ifbop_root_path_cost;
	uint64_t	ifbop_bridgeid;
	uint64_t	ifbop_designated_root;
	uint64_t	ifbop_designated_bridge;
	struct timeval	ifbop_last_tc_time;
};

/*
 * Bridge member operational STP params structure.
 */
struct ifbpstpreq {
	uint8_t		ifbp_portno;		/* bp STP port number */
	uint32_t	ifbp_fwd_trans;		/* bp STP fwd transitions */
	uint32_t	ifbp_design_cost;	/* bp STP designated cost */
	uint32_t	ifbp_design_port;	/* bp STP designated port */
	uint64_t	ifbp_design_bridge;	/* bp STP designated bridge */
	uint64_t	ifbp_design_root;	/* bp STP designated root */
};

/*
 * Bridge STP ports list structure.
 */
struct ifbpstpconf {
	uint32_t	ifbpstp_len;	/* buffer size */
	union {
		caddr_t	ifbpstpu_buf;
		struct ifbpstpreq *ifbpstpu_req;
	} ifbpstp_ifbpstpu;
#define	ifbpstp_buf	ifbpstp_ifbpstpu.ifbpstpu_buf
#define	ifbpstp_req	ifbpstp_ifbpstpu.ifbpstpu_req
};

#ifdef _KERNEL

#define BRIDGE_LOCK_INIT(_sc)		do {			\
	mtx_init(&(_sc)->sc_mtx, "if_bridge", NULL, MTX_DEF);	\
	cv_init(&(_sc)->sc_cv, "if_bridge_cv");			\
} while (0)
#define BRIDGE_LOCK_DESTROY(_sc)	do {	\
	mtx_destroy(&(_sc)->sc_mtx);		\
	cv_destroy(&(_sc)->sc_cv);		\
} while (0)
#define BRIDGE_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define BRIDGE_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define BRIDGE_LOCK_ASSERT(_sc)		mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define BRIDGE_UNLOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_NOTOWNED)
#define	BRIDGE_LOCK2REF(_sc, _err)	do {	\
	mtx_assert(&(_sc)->sc_mtx, MA_OWNED);	\
	if ((_sc)->sc_iflist_xcnt > 0)		\
		(_err) = EBUSY;			\
	else					\
		(_sc)->sc_iflist_ref++;		\
	mtx_unlock(&(_sc)->sc_mtx);		\
} while (0)
#define	BRIDGE_UNREF(_sc)		do {				\
	mtx_lock(&(_sc)->sc_mtx);					\
	(_sc)->sc_iflist_ref--;						\
	if (((_sc)->sc_iflist_xcnt > 0) && ((_sc)->sc_iflist_ref == 0))	\
		cv_broadcast(&(_sc)->sc_cv);				\
	mtx_unlock(&(_sc)->sc_mtx);					\
} while (0)
#define	BRIDGE_XLOCK(_sc)		do {		\
	mtx_assert(&(_sc)->sc_mtx, MA_OWNED);		\
	(_sc)->sc_iflist_xcnt++;			\
	while ((_sc)->sc_iflist_ref > 0)		\
		cv_wait(&(_sc)->sc_cv, &(_sc)->sc_mtx);	\
} while (0)
#define	BRIDGE_XDROP(_sc)		do {	\
	mtx_assert(&(_sc)->sc_mtx, MA_OWNED);	\
	(_sc)->sc_iflist_xcnt--;		\
} while (0)

#define BRIDGE_INPUT(_ifp, _m)		do {			\
		KASSERT((_ifp)->if_bridge_input != NULL,		\
	    ("%s: if_bridge not loaded!", __func__));	\
	_m = (*(_ifp)->if_bridge_input)(_ifp, _m);			\
	if (_m != NULL)					\
		_ifp = _m->m_pkthdr.rcvif;		\
} while (0)

#define BRIDGE_OUTPUT(_ifp, _m, _err)	do {    		\
	KASSERT((_ifp)->if_bridge_output != NULL,		\
	    ("%s: if_bridge not loaded!", __func__));		\
	_err = (*(_ifp)->if_bridge_output)(_ifp, _m, NULL, NULL);	\
} while (0)

extern	void (*bridge_dn_p)(struct mbuf *, struct ifnet *);

#endif /* _KERNEL */
