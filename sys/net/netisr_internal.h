/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Robert N. M. Watson
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
 *
 * $FreeBSD$
 */

#ifndef _NET_NETISR_INTERNAL_H_
#define	_NET_NETISR_INTERNAL_H_

#ifndef _WANT_NETISR_INTERNAL
#error "no user-serviceable parts inside"
#endif

/*
 * These definitions are private to the netisr implementation, but provided
 * here for use by post-mortem crashdump analysis tools.  They should not be
 * used in any other context as they can and will change.  Public definitions
 * may be found in netisr.h.
 */

#ifndef _KERNEL
typedef void *netisr_handler_t;
typedef void *netisr_m2flow_t;
typedef void *netisr_m2cpuid_t;
typedef void *netisr_drainedcpu_t;
#endif

/*
 * Each protocol is described by a struct netisr_proto, which holds all
 * global per-protocol information.  This data structure is set up by
 * netisr_register(), and derived from the public struct netisr_handler.
 */
struct netisr_proto {
	const char	*np_name;	/* Character string protocol name. */
	netisr_handler_t *np_handler;	/* Protocol handler. */
	netisr_m2flow_t	*np_m2flow;	/* Query flow for untagged packet. */
	netisr_m2cpuid_t *np_m2cpuid;	/* Query CPU to process packet on. */
	netisr_drainedcpu_t *np_drainedcpu; /* Callback when drained a queue. */
	u_int		 np_qlimit;	/* Maximum per-CPU queue depth. */
	u_int		 np_policy;	/* Work placement policy. */
	u_int		 np_dispatch;	/* Work dispatch policy. */
};

#define	NETISR_MAXPROT	16		/* Compile-time limit. */

/*
 * Protocol-specific work for each workstream is described by struct
 * netisr_work.  Each work descriptor consists of an mbuf queue and
 * statistics.
 */
struct netisr_work {
	/*
	 * Packet queue, linked by m_nextpkt.
	 */
	struct mbuf	*nw_head;
	struct mbuf	*nw_tail;
	u_int		 nw_len;
	u_int		 nw_qlimit;
	u_int		 nw_watermark;

	/*
	 * Statistics -- written unlocked, but mostly from curcpu.
	 */
	u_int64_t	 nw_dispatched; /* Number of direct dispatches. */
	u_int64_t	 nw_hybrid_dispatched; /* "" hybrid dispatches. */
	u_int64_t	 nw_qdrops;	/* "" drops. */
	u_int64_t	 nw_queued;	/* "" enqueues. */
	u_int64_t	 nw_handled;	/* "" handled in worker. */
};

/*
 * Workstreams hold a queue of ordered work across each protocol, and are
 * described by netisr_workstream.  Each workstream is associated with a
 * worker thread, which in turn is pinned to a CPU.  Work associated with a
 * workstream can be processd in other threads during direct dispatch;
 * concurrent processing is prevented by the NWS_RUNNING flag, which
 * indicates that a thread is already processing the work queue.  It is
 * important to prevent a directly dispatched packet from "skipping ahead" of
 * work already in the workstream queue.
 */
struct netisr_workstream {
	struct intr_event *nws_intr_event;	/* Handler for stream. */
	void		*nws_swi_cookie;	/* swi(9) cookie for stream. */
	struct mtx	 nws_mtx;		/* Synchronize work. */
	u_int		 nws_cpu;		/* CPU pinning. */
	u_int		 nws_flags;		/* Wakeup flags. */
	u_int		 nws_pendingbits;	/* Scheduled protocols. */

	/*
	 * Each protocol has per-workstream data.
	 */
	struct netisr_work	nws_work[NETISR_MAXPROT];
} __aligned(CACHE_LINE_SIZE);

/*
 * Per-workstream flags.
 */
#define	NWS_RUNNING	0x00000001	/* Currently running in a thread. */
#define	NWS_DISPATCHING	0x00000002	/* Currently being direct-dispatched. */
#define	NWS_SCHEDULED	0x00000004	/* Signal issued. */

#endif /* !_NET_NETISR_INTERNAL_H_ */
