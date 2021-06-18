/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * NET		An implementation of the SOCKET network access protocol.
 *		This is the master header file for the Linux NET layer,
 *		or, in plain English: the networking handling part of the
 *		kernel.
 *
 * Version:	@(#)net.h	1.0.3	05/25/93
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI_LINUX_NET_H
#define _UAPI_LINUX_NET_H

#include <linux/socket.h>
#include <asm/socket.h>

#define NPROTO		AF_MAX

#define SYS_SOCKET	1		/* sys_socket(2)		*/
#define SYS_BIND	2		/* sys_bind(2)			*/
#define SYS_CONNECT	3		/* sys_connect(2)		*/
#define SYS_LISTEN	4		/* sys_listen(2)		*/
#define SYS_ACCEPT	5		/* sys_accept(2)		*/
#define SYS_GETSOCKNAME	6		/* sys_getsockname(2)		*/
#define SYS_GETPEERNAME	7		/* sys_getpeername(2)		*/
#define SYS_SOCKETPAIR	8		/* sys_socketpair(2)		*/
#define SYS_SEND	9		/* sys_send(2)			*/
#define SYS_RECV	10		/* sys_recv(2)			*/
#define SYS_SENDTO	11		/* sys_sendto(2)		*/
#define SYS_RECVFROM	12		/* sys_recvfrom(2)		*/
#define SYS_SHUTDOWN	13		/* sys_shutdown(2)		*/
#define SYS_SETSOCKOPT	14		/* sys_setsockopt(2)		*/
#define SYS_GETSOCKOPT	15		/* sys_getsockopt(2)		*/
#define SYS_SENDMSG	16		/* sys_sendmsg(2)		*/
#define SYS_RECVMSG	17		/* sys_recvmsg(2)		*/
#define SYS_ACCEPT4	18		/* sys_accept4(2)		*/
#define SYS_RECVMMSG	19		/* sys_recvmmsg(2)		*/
#define SYS_SENDMMSG	20		/* sys_sendmmsg(2)		*/

typedef enum {
	SS_FREE = 0,			/* not allocated		*/
	SS_UNCONNECTED,			/* unconnected to any socket	*/
	SS_CONNECTING,			/* in process of connecting	*/
	SS_CONNECTED,			/* connected to socket		*/
	SS_DISCONNECTING		/* in process of disconnecting	*/
} socket_state;

#define __SO_ACCEPTCON	(1 << 16)	/* performed a listen		*/

static const char * const pf_family_names[] = {
	[PF_UNSPEC]	= "PF_UNSPEC",
	[PF_UNIX]	= "PF_UNIX/PF_LOCAL",
	[PF_INET]	= "PF_INET",
	[PF_AX25]	= "PF_AX25",
	[PF_IPX]	= "PF_IPX",
	[PF_APPLETALK]	= "PF_APPLETALK",
	[PF_NETROM]	= "PF_NETROM",
	[PF_BRIDGE]	= "PF_BRIDGE",
	[PF_ATMPVC]	= "PF_ATMPVC",
	[PF_X25]	= "PF_X25",
	[PF_INET6]	= "PF_INET6",
	[PF_ROSE]	= "PF_ROSE",
	[PF_DECnet]	= "PF_DECnet",
	[PF_NETBEUI]	= "PF_NETBEUI",
	[PF_SECURITY]	= "PF_SECURITY",
	[PF_KEY]	= "PF_KEY",
	[PF_NETLINK]	= "PF_NETLINK/PF_ROUTE",
	[PF_PACKET]	= "PF_PACKET",
	[PF_ASH]	= "PF_ASH",
	[PF_ECONET]	= "PF_ECONET",
	[PF_ATMSVC]	= "PF_ATMSVC",
	[PF_RDS]	= "PF_RDS",
	[PF_SNA]	= "PF_SNA",
	[PF_IRDA]	= "PF_IRDA",
	[PF_PPPOX]	= "PF_PPPOX",
	[PF_WANPIPE]	= "PF_WANPIPE",
	[PF_LLC]	= "PF_LLC",
	[PF_IB]		= "PF_IB",
	[PF_MPLS]	= "PF_MPLS",
	[PF_CAN]	= "PF_CAN",
	[PF_TIPC]	= "PF_TIPC",
	[PF_BLUETOOTH]	= "PF_BLUETOOTH",
	[PF_IUCV]	= "PF_IUCV",
	[PF_RXRPC]	= "PF_RXRPC",
	[PF_ISDN]	= "PF_ISDN",
	[PF_PHONET]	= "PF_PHONET",
	[PF_IEEE802154]	= "PF_IEEE802154",
	[PF_CAIF]	= "PF_CAIF",
	[PF_ALG]	= "PF_ALG",
	[PF_NFC]	= "PF_NFC",
	[PF_VSOCK]	= "PF_VSOCK",
	[PF_KCM]	= "PF_KCM",
	[PF_QIPCRTR]	= "PF_QIPCRTR",
	[PF_SMC]	= "PF_SMC",
	[PF_XDP]	= "PF_XDP",
};

#endif /* _UAPI_LINUX_NET_H */
