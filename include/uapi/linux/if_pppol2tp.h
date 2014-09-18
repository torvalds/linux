/***************************************************************************
 * Linux PPP over L2TP (PPPoL2TP) Socket Implementation (RFC 2661)
 *
 * This file supplies definitions required by the PPP over L2TP driver
 * (l2tp_ppp.c).  All version information wrt this file is located in l2tp_ppp.c
 *
 * License:
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#ifndef _UAPI__LINUX_IF_PPPOL2TP_H
#define _UAPI__LINUX_IF_PPPOL2TP_H

#include <linux/types.h>


/* Structure used to connect() the socket to a particular tunnel UDP
 * socket over IPv4.
 */
struct pppol2tp_addr {
	__kernel_pid_t	pid;		/* pid that owns the fd.
					 * 0 => current */
	int	fd;			/* FD of UDP socket to use */

	struct sockaddr_in addr;	/* IP address and port to send to */

	__u16 s_tunnel, s_session;	/* For matching incoming packets */
	__u16 d_tunnel, d_session;	/* For sending outgoing packets */
};

/* Structure used to connect() the socket to a particular tunnel UDP
 * socket over IPv6.
 */
struct pppol2tpin6_addr {
	__kernel_pid_t	pid;		/* pid that owns the fd.
					 * 0 => current */
	int	fd;			/* FD of UDP socket to use */

	__u16 s_tunnel, s_session;	/* For matching incoming packets */
	__u16 d_tunnel, d_session;	/* For sending outgoing packets */

	struct sockaddr_in6 addr;	/* IP address and port to send to */
};

/* The L2TPv3 protocol changes tunnel and session ids from 16 to 32
 * bits. So we need a different sockaddr structure.
 */
struct pppol2tpv3_addr {
	__kernel_pid_t	pid;		/* pid that owns the fd.
					 * 0 => current */
	int	fd;			/* FD of UDP or IP socket to use */

	struct sockaddr_in addr;	/* IP address and port to send to */

	__u32 s_tunnel, s_session;	/* For matching incoming packets */
	__u32 d_tunnel, d_session;	/* For sending outgoing packets */
};

struct pppol2tpv3in6_addr {
	__kernel_pid_t	pid;		/* pid that owns the fd.
					 * 0 => current */
	int	fd;			/* FD of UDP or IP socket to use */

	__u32 s_tunnel, s_session;	/* For matching incoming packets */
	__u32 d_tunnel, d_session;	/* For sending outgoing packets */

	struct sockaddr_in6 addr;	/* IP address and port to send to */
};

/* Socket options:
 * DEBUG	- bitmask of debug message categories
 * SENDSEQ	- 0 => don't send packets with sequence numbers
 *		  1 => send packets with sequence numbers
 * RECVSEQ	- 0 => receive packet sequence numbers are optional
 *		  1 => drop receive packets without sequence numbers
 * LNSMODE	- 0 => act as LAC.
 *		  1 => act as LNS.
 * REORDERTO	- reorder timeout (in millisecs). If 0, don't try to reorder.
 */
enum {
	PPPOL2TP_SO_DEBUG	= 1,
	PPPOL2TP_SO_RECVSEQ	= 2,
	PPPOL2TP_SO_SENDSEQ	= 3,
	PPPOL2TP_SO_LNSMODE	= 4,
	PPPOL2TP_SO_REORDERTO	= 5,
};

/* Debug message categories for the DEBUG socket option */
enum {
	PPPOL2TP_MSG_DEBUG	= (1 << 0),	/* verbose debug (if
						 * compiled in) */
	PPPOL2TP_MSG_CONTROL	= (1 << 1),	/* userspace - kernel
						 * interface */
	PPPOL2TP_MSG_SEQ	= (1 << 2),	/* sequence numbers */
	PPPOL2TP_MSG_DATA	= (1 << 3),	/* data packets */
};



#endif /* _UAPI__LINUX_IF_PPPOL2TP_H */
