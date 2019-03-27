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

#ifndef _NET_NETISR_H_
#define _NET_NETISR_H_

/*
 * The netisr (network interrupt service routine) provides a deferred
 * execution evironment in which (generally inbound) network processing can
 * take place.  Protocols register handlers which will be executed directly,
 * or via deferred dispatch, depending on the circumstances.
 *
 * Historically, this was implemented by the BSD software ISR facility; it is
 * now implemented via a software ithread (SWI).
 */

/*
 * Protocol numbers, which are encoded in monitoring applications and kernel
 * modules.  Internally, these are used in bit shift operations so must have
 * a value 0 < proto < 32; we currently further limit at compile-time to 16
 * for array-sizing purposes.
 */
#define	NETISR_IP	1
#define	NETISR_IGMP	2		/* IGMPv3 output queue */
#define	NETISR_ROUTE	3		/* routing socket */
#define	NETISR_ARP	4		/* same as AF_LINK */
#define	NETISR_ETHER	5		/* ethernet input */
#define	NETISR_IPV6	6
#define	NETISR_EPAIR	8		/* if_epair(4) */
#define	NETISR_IP_DIRECT	9	/* direct-dispatch IPv4 */
#define	NETISR_IPV6_DIRECT	10	/* direct-dispatch IPv6 */

/*
 * Protocol ordering and affinity policy constants.  See the detailed
 * discussion of policies later in the file.
 */
#define	NETISR_POLICY_SOURCE	1	/* Maintain source ordering. */
#define	NETISR_POLICY_FLOW	2	/* Maintain flow ordering. */
#define	NETISR_POLICY_CPU	3	/* Protocol determines CPU placement. */

/*
 * Protocol dispatch policy constants; selects whether and when direct
 * dispatch is permitted.
 */
#define	NETISR_DISPATCH_DEFAULT		0	/* Use global default. */
#define	NETISR_DISPATCH_DEFERRED	1	/* Always defer dispatch. */
#define	NETISR_DISPATCH_HYBRID		2	/* Allow hybrid dispatch. */
#define	NETISR_DISPATCH_DIRECT		3	/* Always direct dispatch. */

/*
 * Monitoring data structures, exported by sysctl(2).
 *
 * Three sysctls are defined.  First, a per-protocol structure exported by
 * net.isr.proto.
 */
#define	NETISR_NAMEMAXLEN	32
struct sysctl_netisr_proto {
	u_int	snp_version;			/* Length of struct. */
	char	snp_name[NETISR_NAMEMAXLEN];	/* nh_name */
	u_int	snp_proto;			/* nh_proto */
	u_int	snp_qlimit;			/* nh_qlimit */
	u_int	snp_policy;			/* nh_policy */
	u_int	snp_flags;			/* Various flags. */
	u_int	snp_dispatch;			/* Dispatch policy. */
	u_int	_snp_ispare[6];
};

/*
 * Flags for sysctl_netisr_proto.snp_flags.
 */
#define	NETISR_SNP_FLAGS_M2FLOW		0x00000001	/* nh_m2flow */
#define	NETISR_SNP_FLAGS_M2CPUID	0x00000002	/* nh_m2cpuid */
#define	NETISR_SNP_FLAGS_DRAINEDCPU	0x00000004	/* nh_drainedcpu */

/*
 * Next, a structure per-workstream, with per-protocol data, exported as
 * net.isr.workstream.
 */
struct sysctl_netisr_workstream {
	u_int	snws_version;			/* Length of struct. */
	u_int	snws_flags;			/* Various flags. */
	u_int	snws_wsid;			/* Workstream ID. */
	u_int	snws_cpu;			/* nws_cpu */
	u_int	_snws_ispare[12];
};

/*
 * Flags for sysctl_netisr_workstream.snws_flags
 */
#define	NETISR_SNWS_FLAGS_INTR		0x00000001	/* nws_intr_event */

/*
 * Finally, a per-workstream-per-protocol structure, exported as
 * net.isr.work.
 */
struct sysctl_netisr_work {
	u_int	snw_version;			/* Length of struct. */
	u_int	snw_wsid;			/* Workstream ID. */
	u_int	snw_proto;			/* Protocol number. */
	u_int	snw_len;			/* nw_len */
	u_int	snw_watermark;			/* nw_watermark */
	u_int	_snw_ispare[3];

	uint64_t	snw_dispatched;		/* nw_dispatched */
	uint64_t	snw_hybrid_dispatched;	/* nw_hybrid_dispatched */
	uint64_t	snw_qdrops;		/* nw_qdrops */
	uint64_t	snw_queued;		/* nw_queued */
	uint64_t	snw_handled;		/* nw_handled */

	uint64_t	_snw_llspare[7];
};

#ifdef _KERNEL

/*-
 * Protocols express ordering constraints and affinity preferences by
 * implementing one or neither of nh_m2flow and nh_m2cpuid, which are used by
 * netisr to determine which per-CPU workstream to assign mbufs to.
 *
 * The following policies may be used by protocols:
 *
 * NETISR_POLICY_SOURCE - netisr should maintain source ordering without
 *                        advice from the protocol.  netisr will ignore any
 *                        flow IDs present on the mbuf for the purposes of
 *                        work placement.
 *
 * NETISR_POLICY_FLOW - netisr should maintain flow ordering as defined by
 *                      the mbuf header flow ID field.  If the protocol
 *                      implements nh_m2flow, then netisr will query the
 *                      protocol in the event that the mbuf doesn't have a
 *                      flow ID, falling back on source ordering.
 *
 * NETISR_POLICY_CPU - netisr will delegate all work placement decisions to
 *                     the protocol, querying nh_m2cpuid for each packet.
 *
 * Protocols might make decisions about work placement based on an existing
 * calculated flow ID on the mbuf, such as one provided in hardware, the
 * receive interface pointed to by the mbuf (if any), the optional source
 * identifier passed at some dispatch points, or even parse packet headers to
 * calculate a flow.  Both protocol handlers may return a new mbuf pointer
 * for the chain, or NULL if the packet proves invalid or m_pullup() fails.
 *
 * XXXRW: If we eventually support dynamic reconfiguration, there should be
 * protocol handlers to notify them of CPU configuration changes so that they
 * can rebalance work.
 */
struct mbuf;
typedef void		 netisr_handler_t(struct mbuf *m);
typedef struct mbuf	*netisr_m2cpuid_t(struct mbuf *m, uintptr_t source,
			 u_int *cpuid);
typedef	struct mbuf	*netisr_m2flow_t(struct mbuf *m, uintptr_t source);
typedef void		 netisr_drainedcpu_t(u_int cpuid);

#define	NETISR_CPUID_NONE	((u_int)-1)	/* No affinity returned. */

/*
 * Data structure describing a protocol handler.
 */
struct netisr_handler {
	const char	*nh_name;	/* Character string protocol name. */
	netisr_handler_t *nh_handler;	/* Protocol handler. */
	netisr_m2flow_t	*nh_m2flow;	/* Query flow for untagged packet. */
	netisr_m2cpuid_t *nh_m2cpuid;	/* Query CPU to process mbuf on. */
	netisr_drainedcpu_t *nh_drainedcpu; /* Callback when drained a queue. */
	u_int		 nh_proto;	/* Integer protocol ID. */
	u_int		 nh_qlimit;	/* Maximum per-CPU queue depth. */
	u_int		 nh_policy;	/* Work placement policy. */
	u_int		 nh_dispatch;	/* Dispatch policy. */
	u_int		 nh_ispare[4];	/* For future use. */
	void		*nh_pspare[4];	/* For future use. */
};

/*
 * Register, unregister, and other netisr handler management functions.
 */
void	netisr_clearqdrops(const struct netisr_handler *nhp);
void	netisr_getqdrops(const struct netisr_handler *nhp,
	    u_int64_t *qdropsp);
void	netisr_getqlimit(const struct netisr_handler *nhp, u_int *qlimitp);
void	netisr_register(const struct netisr_handler *nhp);
int	netisr_setqlimit(const struct netisr_handler *nhp, u_int qlimit);
void	netisr_unregister(const struct netisr_handler *nhp);
#ifdef VIMAGE
void	netisr_register_vnet(const struct netisr_handler *nhp);
void	netisr_unregister_vnet(const struct netisr_handler *nhp);
#endif

/*
 * Process a packet destined for a protocol, and attempt direct dispatch.
 * Supplemental source ordering information can be passed using the _src
 * variant.
 */
int	netisr_dispatch(u_int proto, struct mbuf *m);
int	netisr_dispatch_src(u_int proto, uintptr_t source, struct mbuf *m);
int	netisr_queue(u_int proto, struct mbuf *m);
int	netisr_queue_src(u_int proto, uintptr_t source, struct mbuf *m);

/*
 * Provide a default implementation of "map an ID to a CPU ID".
 */
u_int	netisr_default_flow2cpu(u_int flowid);

/*
 * Utility routines to return the number of CPUs participting in netisr, and
 * to return a mapping from a number to a CPU ID that can be used with the
 * scheduler.
 */
u_int	netisr_get_cpucount(void);
u_int	netisr_get_cpuid(u_int cpunumber);

/*
 * Interfaces between DEVICE_POLLING and netisr.
 */
void	netisr_sched_poll(void);
void	netisr_poll(void);
void	netisr_pollmore(void);

#endif /* !_KERNEL */
#endif /* !_NET_NETISR_H_ */
