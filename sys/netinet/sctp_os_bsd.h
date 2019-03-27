/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
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

#ifndef _NETINET_SCTP_OS_BSD_H_
#define _NETINET_SCTP_OS_BSD_H_
/*
 * includes
 */
#include "opt_inet6.h"
#include "opt_inet.h"
#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/ktr.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/jail.h>
#include <sys/sysctl.h>
#include <sys/resourcevar.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/kthread.h>
#include <sys/priv.h>
#include <sys/random.h>
#include <sys/limits.h>
#include <sys/queue.h>
#include <machine/cpu.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>

#ifdef INET6
#include <sys/domain.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/ip6protosw.h>
#include <netinet6/nd6.h>
#include <netinet6/scope6_var.h>
#endif				/* INET6 */

#include <netinet/ip_options.h>

#include <crypto/sha1.h>
#include <crypto/sha2/sha256.h>

#ifndef in6pcb
#define in6pcb		inpcb
#endif
/* Declare all the malloc names for all the various mallocs */
MALLOC_DECLARE(SCTP_M_MAP);
MALLOC_DECLARE(SCTP_M_STRMI);
MALLOC_DECLARE(SCTP_M_STRMO);
MALLOC_DECLARE(SCTP_M_ASC_ADDR);
MALLOC_DECLARE(SCTP_M_ASC_IT);
MALLOC_DECLARE(SCTP_M_AUTH_CL);
MALLOC_DECLARE(SCTP_M_AUTH_KY);
MALLOC_DECLARE(SCTP_M_AUTH_HL);
MALLOC_DECLARE(SCTP_M_AUTH_IF);
MALLOC_DECLARE(SCTP_M_STRESET);
MALLOC_DECLARE(SCTP_M_CMSG);
MALLOC_DECLARE(SCTP_M_COPYAL);
MALLOC_DECLARE(SCTP_M_VRF);
MALLOC_DECLARE(SCTP_M_IFA);
MALLOC_DECLARE(SCTP_M_IFN);
MALLOC_DECLARE(SCTP_M_TIMW);
MALLOC_DECLARE(SCTP_M_MVRF);
MALLOC_DECLARE(SCTP_M_ITER);
MALLOC_DECLARE(SCTP_M_SOCKOPT);
MALLOC_DECLARE(SCTP_M_MCORE);

#if defined(SCTP_LOCAL_TRACE_BUF)

#define SCTP_GET_CYCLECOUNT get_cyclecount()
#define SCTP_CTR6 sctp_log_trace

#else
#define SCTP_CTR6 CTR6
#endif

/*
 * Macros to expand out globals defined by various modules
 * to either a real global or a virtualized instance of one,
 * depending on whether VIMAGE is defined.
 */
/* then define the macro(s) that hook into the vimage macros */
#define MODULE_GLOBAL(__SYMBOL) V_##__SYMBOL

#define V_system_base_info VNET(system_base_info)
#define SCTP_BASE_INFO(__m) V_system_base_info.sctppcbinfo.__m
#define SCTP_BASE_STATS V_system_base_info.sctpstat
#define SCTP_BASE_STAT(__m) V_system_base_info.sctpstat.__m
#define SCTP_BASE_SYSCTL(__m) V_system_base_info.sctpsysctl.__m
#define SCTP_BASE_VAR(__m) V_system_base_info.__m

#define SCTP_PRINTF(params...)	printf(params)
#if defined(SCTP_DEBUG)
#define SCTPDBG(level, params...)					\
{									\
	do {								\
		if (SCTP_BASE_SYSCTL(sctp_debug_on) & level ) {		\
			SCTP_PRINTF(params);				\
		}							\
	} while (0);							\
}
#define SCTPDBG_ADDR(level, addr)					\
{									\
	do {								\
		if (SCTP_BASE_SYSCTL(sctp_debug_on) & level ) {		\
			sctp_print_address(addr);			\
		}							\
	} while (0);							\
}
#else
#define SCTPDBG(level, params...)
#define SCTPDBG_ADDR(level, addr)
#endif

#ifdef SCTP_LTRACE_CHUNKS
#define SCTP_LTRACE_CHK(a, b, c, d) if(SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LTRACE_CHUNK_ENABLE) SCTP_CTR6(KTR_SUBSYS, "SCTP:%d[%d]:%x-%x-%x-%x", SCTP_LOG_CHUNK_PROC, 0, a, b, c, d)
#else
#define SCTP_LTRACE_CHK(a, b, c, d)
#endif

#ifdef SCTP_LTRACE_ERRORS
#define SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, net, file, err) \
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LTRACE_ERROR_ENABLE) \
		SCTP_PRINTF("mbuf:%p inp:%p stcb:%p net:%p file:%x line:%d error:%d\n", \
		            m, inp, stcb, net, file, __LINE__, err);
#define SCTP_LTRACE_ERR_RET(inp, stcb, net, file, err) \
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LTRACE_ERROR_ENABLE) \
		SCTP_PRINTF("inp:%p stcb:%p net:%p file:%x line:%d error:%d\n", \
		            inp, stcb, net, file, __LINE__, err);
#else
#define SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, net, file, err)
#define SCTP_LTRACE_ERR_RET(inp, stcb, net, file, err)
#endif


/*
 * Local address and interface list handling
 */
#define SCTP_MAX_VRF_ID		0
#define SCTP_SIZE_OF_VRF_HASH	3
#define SCTP_IFNAMSIZ		IFNAMSIZ
#define SCTP_DEFAULT_VRFID	0
#define SCTP_VRF_ADDR_HASH_SIZE	16
#define SCTP_VRF_IFN_HASH_SIZE	3
#define	SCTP_INIT_VRF_TABLEID(vrf)

#define SCTP_IFN_IS_IFT_LOOP(ifn) ((ifn)->ifn_type == IFT_LOOP)
#define SCTP_ROUTE_IS_REAL_LOOP(ro) ((ro)->ro_rt && (ro)->ro_rt->rt_ifa && (ro)->ro_rt->rt_ifa->ifa_ifp && (ro)->ro_rt->rt_ifa->ifa_ifp->if_type == IFT_LOOP)

/*
 * Access to IFN's to help with src-addr-selection
 */
/* This could return VOID if the index works but for BSD we provide both. */
#define SCTP_GET_IFN_VOID_FROM_ROUTE(ro) (void *)ro->ro_rt->rt_ifp
#define SCTP_GET_IF_INDEX_FROM_ROUTE(ro) (ro)->ro_rt->rt_ifp->if_index
#define SCTP_ROUTE_HAS_VALID_IFN(ro) ((ro)->ro_rt && (ro)->ro_rt->rt_ifp)

/*
 * general memory allocation
 */
#define SCTP_MALLOC(var, type, size, name) \
	do { \
		var = (type)malloc(size, name, M_NOWAIT); \
	} while (0)

#define SCTP_FREE(var, type)	free(var, type)

#define SCTP_MALLOC_SONAME(var, type, size) \
	do { \
		var = (type)malloc(size, M_SONAME, M_WAITOK | M_ZERO); \
	} while (0)

#define SCTP_FREE_SONAME(var)	free(var, M_SONAME)

#define SCTP_PROCESS_STRUCT struct proc *

/*
 * zone allocation functions
 */
#include <vm/uma.h>

/* SCTP_ZONE_INIT: initialize the zone */
typedef struct uma_zone *sctp_zone_t;
#define SCTP_ZONE_INIT(zone, name, size, number) { \
	zone = uma_zcreate(name, size, NULL, NULL, NULL, NULL, UMA_ALIGN_PTR,\
		0); \
	uma_zone_set_max(zone, number); \
}

#define SCTP_ZONE_DESTROY(zone) uma_zdestroy(zone)

/* SCTP_ZONE_GET: allocate element from the zone */
#define SCTP_ZONE_GET(zone, type) \
	(type *)uma_zalloc(zone, M_NOWAIT);

/* SCTP_ZONE_FREE: free element from the zone */
#define SCTP_ZONE_FREE(zone, element) \
	uma_zfree(zone, element);

#define SCTP_HASH_INIT(size, hashmark) hashinit_flags(size, M_PCB, hashmark, HASH_NOWAIT)
#define SCTP_HASH_FREE(table, hashmark) hashdestroy(table, M_PCB, hashmark)

#define SCTP_M_COPYM	m_copym

/*
 * timers
 */
#include <sys/callout.h>
typedef struct callout sctp_os_timer_t;


#define SCTP_OS_TIMER_INIT(tmr)	callout_init(tmr, 1)
#define SCTP_OS_TIMER_START	callout_reset
#define SCTP_OS_TIMER_STOP	callout_stop
#define SCTP_OS_TIMER_STOP_DRAIN callout_drain
#define SCTP_OS_TIMER_PENDING	callout_pending
#define SCTP_OS_TIMER_ACTIVE	callout_active
#define SCTP_OS_TIMER_DEACTIVATE callout_deactivate

#define sctp_get_tick_count() (ticks)

#define SCTP_UNUSED __attribute__((unused))

/*
 * Functions
 */
/* Mbuf manipulation and access macros  */
#define SCTP_BUF_LEN(m) (m->m_len)
#define SCTP_BUF_NEXT(m) (m->m_next)
#define SCTP_BUF_NEXT_PKT(m) (m->m_nextpkt)
#define SCTP_BUF_RESV_UF(m, size) m->m_data += size
#define SCTP_BUF_AT(m, size) m->m_data + size
#define SCTP_BUF_IS_EXTENDED(m) (m->m_flags & M_EXT)
#define SCTP_BUF_SIZE M_SIZE
#define SCTP_BUF_TYPE(m) (m->m_type)
#define SCTP_BUF_RECVIF(m) (m->m_pkthdr.rcvif)
#define SCTP_BUF_PREPEND	M_PREPEND

#define SCTP_ALIGN_TO_END(m, len) M_ALIGN(m, len)

/* We make it so if you have up to 4 threads
 * writing based on the default size of
 * the packet log 65 k, that would be
 * 4 16k packets before we would hit
 * a problem.
 */
#define SCTP_PKTLOG_WRITERS_NEED_LOCK 3

/*************************/
/*      MTU              */
/*************************/
#define SCTP_GATHER_MTU_FROM_IFN_INFO(ifn, ifn_index, af) ((struct ifnet *)ifn)->if_mtu
#define SCTP_GATHER_MTU_FROM_ROUTE(sctp_ifa, sa, rt) ((uint32_t)((rt != NULL) ? rt->rt_mtu : 0))
#define SCTP_GATHER_MTU_FROM_INTFC(sctp_ifn) ((sctp_ifn->ifn_p != NULL) ? ((struct ifnet *)(sctp_ifn->ifn_p))->if_mtu : 0)
#define SCTP_SET_MTU_OF_ROUTE(sa, rt, mtu) do { \
                                              if (rt != NULL) \
                                                 rt->rt_mtu = mtu; \
                                           } while(0)

/* (de-)register interface event notifications */
#define SCTP_REGISTER_INTERFACE(ifhandle, af)
#define SCTP_DEREGISTER_INTERFACE(ifhandle, af)


/*************************/
/* These are for logging */
/*************************/
/* return the base ext data pointer */
#define SCTP_BUF_EXTEND_BASE(m) (m->m_ext.ext_buf)
 /* return the refcnt of the data pointer */
#define SCTP_BUF_EXTEND_REFCNT(m) (*m->m_ext.ext_cnt)
/* return any buffer related flags, this is
 * used beyond logging for apple only.
 */
#define SCTP_BUF_GET_FLAGS(m) (m->m_flags)

/* For BSD this just accesses the M_PKTHDR length
 * so it operates on an mbuf with hdr flag. Other
 * O/S's may have separate packet header and mbuf
 * chain pointers.. thus the macro.
 */
#define SCTP_HEADER_TO_CHAIN(m) (m)
#define SCTP_DETACH_HEADER_FROM_CHAIN(m)
#define SCTP_HEADER_LEN(m) ((m)->m_pkthdr.len)
#define SCTP_GET_HEADER_FOR_OUTPUT(o_pak) 0
#define SCTP_RELEASE_HEADER(m)
#define SCTP_RELEASE_PKT(m)	sctp_m_freem(m)
#define SCTP_ENABLE_UDP_CSUM(m) do { \
					m->m_pkthdr.csum_flags = CSUM_UDP; \
					m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum); \
				} while (0)

#define SCTP_GET_PKT_VRFID(m, vrf_id)  ((vrf_id = SCTP_DEFAULT_VRFID) != SCTP_DEFAULT_VRFID)



/* Attach the chain of data into the sendable packet. */
#define SCTP_ATTACH_CHAIN(pak, m, packet_length) do { \
                                                 pak = m; \
                                                 pak->m_pkthdr.len = packet_length; \
                         } while(0)

/* Other m_pkthdr type things */
#define SCTP_IS_IT_BROADCAST(dst, m) ((m->m_flags & M_PKTHDR) ? in_broadcast(dst, m->m_pkthdr.rcvif) : 0)
#define SCTP_IS_IT_LOOPBACK(m) ((m->m_flags & M_PKTHDR) && ((m->m_pkthdr.rcvif == NULL) || (m->m_pkthdr.rcvif->if_type == IFT_LOOP)))


/* This converts any input packet header
 * into the chain of data holders, for BSD
 * its a NOP.
 */

/* get the v6 hop limit */
#define SCTP_GET_HLIM(inp, ro)	in6_selecthlim((struct in6pcb *)&inp->ip_inp.inp, (ro ? (ro->ro_rt ? (ro->ro_rt->rt_ifp) : (NULL)) : (NULL)));

/* is the endpoint v6only? */
#define SCTP_IPV6_V6ONLY(inp)	(((struct inpcb *)inp)->inp_flags & IN6P_IPV6_V6ONLY)
/* is the socket non-blocking? */
#define SCTP_SO_IS_NBIO(so)	((so)->so_state & SS_NBIO)
#define SCTP_SET_SO_NBIO(so)	((so)->so_state |= SS_NBIO)
#define SCTP_CLEAR_SO_NBIO(so)	((so)->so_state &= ~SS_NBIO)
/* get the socket type */
#define SCTP_SO_TYPE(so)	((so)->so_type)
/* Use a macro for renaming sb_cc to sb_acc.
 * Initially sb_ccc was used, but this broke select() when used
 * with SCTP sockets.
 */
#define sb_cc sb_acc
/* reserve sb space for a socket */
#define SCTP_SORESERVE(so, send, recv)	soreserve(so, send, recv)
/* wakeup a socket */
#define SCTP_SOWAKEUP(so)	wakeup(&(so)->so_timeo)
/* clear the socket buffer state */
#define SCTP_SB_CLEAR(sb)	\
	(sb).sb_cc = 0;		\
	(sb).sb_mb = NULL;	\
	(sb).sb_mbcnt = 0;

#define SCTP_SB_LIMIT_RCV(so) (SOLISTENING(so) ? so->sol_sbrcv_hiwat : so->so_rcv.sb_hiwat)
#define SCTP_SB_LIMIT_SND(so) (SOLISTENING(so) ? so->sol_sbsnd_hiwat : so->so_snd.sb_hiwat)

/*
 * routes, output, etc.
 */
typedef struct route sctp_route_t;
typedef struct rtentry sctp_rtentry_t;

#define SCTP_RTALLOC(ro, vrf_id, fibnum) \
	rtalloc_ign_fib((struct route *)ro, 0UL, fibnum)

/*
 * SCTP protocol specific mbuf flags.
 */
#define	M_NOTIFICATION		M_PROTO1	/* SCTP notification */

/*
 * IP output routines
 */
#define SCTP_IP_OUTPUT(result, o_pak, ro, stcb, vrf_id) \
{ \
	int o_flgs = IP_RAWOUTPUT; \
	struct sctp_tcb *local_stcb = stcb; \
	if (local_stcb && \
	    local_stcb->sctp_ep && \
	    local_stcb->sctp_ep->sctp_socket) \
		o_flgs |= local_stcb->sctp_ep->sctp_socket->so_options & SO_DONTROUTE; \
	m_clrprotoflags(o_pak); \
	result = ip_output(o_pak, NULL, ro, o_flgs, 0, NULL); \
}

#define SCTP_IP6_OUTPUT(result, o_pak, ro, ifp, stcb, vrf_id) \
{ \
	struct sctp_tcb *local_stcb = stcb; \
	m_clrprotoflags(o_pak); \
	if (local_stcb && local_stcb->sctp_ep) \
		result = ip6_output(o_pak, \
				    ((struct in6pcb *)(local_stcb->sctp_ep))->in6p_outputopts, \
				    (ro), 0, 0, ifp, NULL); \
	else \
		result = ip6_output(o_pak, NULL, (ro), 0, 0, ifp, NULL); \
}

struct mbuf *
sctp_get_mbuf_for_msg(unsigned int space_needed,
    int want_header, int how, int allonebuf, int type);


/*
 * SCTP AUTH
 */
#define SCTP_READ_RANDOM(buf, len)	arc4rand(buf, len, 0)

/* map standard crypto API names */
#define SCTP_SHA1_CTX		SHA1_CTX
#define SCTP_SHA1_INIT		SHA1Init
#define SCTP_SHA1_UPDATE	SHA1Update
#define SCTP_SHA1_FINAL(x,y)	SHA1Final((caddr_t)x, y)

#define SCTP_SHA256_CTX		SHA256_CTX
#define SCTP_SHA256_INIT	SHA256_Init
#define SCTP_SHA256_UPDATE	SHA256_Update
#define SCTP_SHA256_FINAL(x,y)	SHA256_Final((caddr_t)x, y)

#define SCTP_DECREMENT_AND_CHECK_REFCOUNT(addr) (atomic_fetchadd_int(addr, -1) == 1)
#if defined(INVARIANTS)
#define SCTP_SAVE_ATOMIC_DECREMENT(addr, val) \
{ \
	int32_t oldval; \
	oldval = atomic_fetchadd_int(addr, -val); \
	if (oldval < val) { \
		panic("Counter goes negative"); \
	} \
}
#else
#define SCTP_SAVE_ATOMIC_DECREMENT(addr, val) \
{ \
	int32_t oldval; \
	oldval = atomic_fetchadd_int(addr, -val); \
	if (oldval < val) { \
		*addr = 0; \
	} \
}
#endif

#define SCTP_IS_LISTENING(inp) ((inp->sctp_flags & SCTP_PCB_FLAGS_ACCEPTING) != 0)

#endif
