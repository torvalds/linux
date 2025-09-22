/*	$OpenBSD: pppoe_local.h,v 1.5 2012/09/18 13:14:08 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */
#ifndef PPPOE_LOCAL_H
#define PPPOE_LOCAL_H 1

#define	BPF_CAPTURE_SIZ			32768
#define PPPOE_SESSION_HASH_SIZ		557
#define PPPOE_SESSION_BUFSIZ		2048
#define	PPPOED_SESSION_SHUFFLE_MARK	0x10000000
#define PPPOED_SHUTDOWN_TIMEOUT		5

#ifndef PPPOE_NSESSION
/** PPPoE maximum number of sessions */
#define	PPPOE_NSESSION			10000
#endif
#define	PPPOE_NLISTENER			512

#define pppoe_session_listen_ifname(session)				\
	((pppoed_listener *)slist_get(&(session)->pppoed->listener,	\
	    (session)->listener_index))->listen_ifname
#define pppoe_session_sock_ether_addr(session)				\
	((pppoed_listener *)slist_get(&(session)->pppoed->listener,	\
	    (session)->listener_index))->ether_addr
#define pppoe_session_sock_bpf(session)					\
	((pppoed_listener *)slist_get(&(session)->pppoed->listener,	\
	    (session)->listener_index))->bpf

/** macro is to get the physical layer label by {@link pppoe_session} */
#define PPPOE_SESSION_LISTENER_TUN_NAME(session) 			\
	((pppoed_listener *)slist_get(&(session)->pppoed->listener,	\
	(session)->listener_index))->tun_name
/** macro is to get the interface name by {@link pppoe_session} */
#define PPPOE_SESSION_LISTENER_IFNAME(session) 				\
	((pppoed_listener *)slist_get(&(session)->pppoed->listener,	\
	(session)->listener_index))->listen_ifname

#ifndef GETSHORT
#define GETSHORT(s, cp) { \
	(s) = *(cp)++ << 8; \
	(s) |= *(cp)++; \
}
#endif
#ifndef countof
#define	countof(x)	(sizeof((x)) / sizeof((x)[0]))
#endif

#define	IFTYPE_IS_LAN(iftype)						\
	((iftype) == IFT_ETHER || (iftype) == IFT_L2VLAN ||		\
	(iftype) == IFT_L3IPVLAN || (iftype) == IFT_L3IPXVLAN)


#endif
