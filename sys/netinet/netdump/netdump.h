/*-
 * Copyright (c) 2005-2014 Sandvine Incorporated
 * Copyright (c) 2000 Darrell Anderson <anderson@cs.duke.edu>
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
 *
 * $FreeBSD$
 */

#ifndef _NETINET_NETDUMP_H_
#define	_NETINET_NETDUMP_H_

#include <sys/types.h>
#include <sys/disk.h>
#include <sys/ioccom.h>

#include <net/if.h>
#include <netinet/in.h>

#define	NETDUMP_PORT		20023	/* Server UDP port for heralds. */
#define	NETDUMP_ACKPORT		20024	/* Client UDP port for acks. */

#define	NETDUMP_HERALD		1	/* Broadcast before starting a dump. */
#define	NETDUMP_FINISHED	2	/* Send after finishing a dump. */
#define	NETDUMP_VMCORE		3	/* Contains dump data. */
#define	NETDUMP_KDH		4	/* Contains kernel dump header. */
#define	NETDUMP_EKCD_KEY	5	/* Contains kernel dump key. */

#define	NETDUMP_DATASIZE	4096	/* Arbitrary packet size limit. */

struct netdump_msg_hdr {
	uint32_t	mh_type;	/* Netdump message type. */
	uint32_t	mh_seqno;	/* Match acks with msgs. */
	uint64_t	mh_offset;	/* vmcore offset (bytes). */
	uint32_t	mh_len;		/* Attached data (bytes). */
	uint32_t	mh__pad;
} __packed;

struct netdump_ack {
	uint32_t	na_seqno;	/* Match acks with msgs. */
} __packed;

struct netdump_conf {
	struct diocskerneldump_arg ndc_kda;
	char		ndc_iface[IFNAMSIZ];
	struct in_addr	ndc_server;
	struct in_addr	ndc_client;
	struct in_addr	ndc_gateway;
};

#define	_PATH_NETDUMP	"/dev/netdump"

#define	NETDUMPGCONF	_IOR('n', 1, struct netdump_conf)
#define	NETDUMPSCONF	_IOW('n', 2, struct netdump_conf)

#ifdef _KERNEL
#ifdef NETDUMP

#define	NETDUMP_MAX_IN_FLIGHT	64

enum netdump_ev {
	NETDUMP_START,
	NETDUMP_END,
};

struct ifnet;
struct mbuf;

void	netdump_reinit(struct ifnet *);

typedef void netdump_init_t(struct ifnet *, int *nrxr, int *ncl, int *clsize);
typedef void netdump_event_t(struct ifnet *, enum netdump_ev);
typedef int netdump_transmit_t(struct ifnet *, struct mbuf *);
typedef int netdump_poll_t(struct ifnet *, int);

struct netdump_methods {
	netdump_init_t		*nd_init;
	netdump_event_t		*nd_event;
	netdump_transmit_t	*nd_transmit;
	netdump_poll_t		*nd_poll;
};

#define	NETDUMP_DEFINE(driver)					\
	static netdump_init_t driver##_netdump_init;		\
	static netdump_event_t driver##_netdump_event;		\
	static netdump_transmit_t driver##_netdump_transmit;	\
	static netdump_poll_t driver##_netdump_poll;		\
								\
	static struct netdump_methods driver##_netdump_methods = { \
		.nd_init = driver##_netdump_init,		\
		.nd_event = driver##_netdump_event,		\
		.nd_transmit = driver##_netdump_transmit,	\
		.nd_poll = driver##_netdump_poll,		\
	}

#define	NETDUMP_REINIT(ifp)	netdump_reinit(ifp)

#define	NETDUMP_SET(ifp, driver)				\
	(ifp)->if_netdump_methods = &driver##_netdump_methods

#else /* !NETDUMP */

#define	NETDUMP_DEFINE(driver)
#define	NETDUMP_REINIT(ifp)
#define	NETDUMP_SET(ifp, driver)

#endif /* NETDUMP */
#endif /* _KERNEL */

#endif /* _NETINET_NETDUMP_H_ */
