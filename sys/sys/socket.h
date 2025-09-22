/*	$OpenBSD: socket.h,v 1.108 2025/08/04 04:59:30 guenther Exp $	*/
/*	$NetBSD: socket.h,v 1.14 1996/02/09 18:25:36 christos Exp $	*/

/*
 * Copyright (c) 1982, 1985, 1986, 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)socket.h	8.4 (Berkeley) 2/21/94
 */

#ifndef _SYS_SOCKET_H_
#define	_SYS_SOCKET_H_

/* get the definitions for struct iovec, size_t, ssize_t, and <sys/cdefs.h> */
#include <sys/uio.h>

#if __BSD_VISIBLE
#include <sys/types.h>			/* for off_t, uid_t, and gid_t */
#endif

#ifndef	_SOCKLEN_T_DEFINED_
#define	_SOCKLEN_T_DEFINED_
typedef	__socklen_t	socklen_t;	/* length type for network syscalls */
#endif

#ifndef	_SA_FAMILY_T_DEFINED_
#define	_SA_FAMILY_T_DEFINED_
typedef	__sa_family_t	sa_family_t;	/* sockaddr address family type */
#endif


/*
 * Definitions related to sockets: types, address families, options.
 */

/*
 * Types
 */
#define	SOCK_STREAM	1		/* stream socket */
#define	SOCK_DGRAM	2		/* datagram socket */
#define	SOCK_RAW	3		/* raw-protocol interface */
#define	SOCK_RDM	4		/* reliably-delivered message */
#define	SOCK_SEQPACKET	5		/* sequenced packet stream */
#ifdef _KERNEL
#define	SOCK_TYPE_MASK	0x000F		/* mask that covers the above */
#endif

/*
 * Socket creation flags
 */
#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
#define	SOCK_CLOEXEC		0x8000	/* set FD_CLOEXEC */
#define	SOCK_NONBLOCK		0x4000	/* set O_NONBLOCK */
#endif
#if __BSD_VISIBLE
#ifdef _KERNEL
#define	SOCK_NONBLOCK_INHERIT	0x2000	/* inherit O_NONBLOCK from listener */
#endif
#define	SOCK_DNS		0x1000	/* set SS_DNS */
#endif /* __BSD_VISIBLE */
#if __POSIX_VISIBLE >= 202405
#define	SOCK_CLOFORK		0x0800	/* set FD_CLOFORK */
#endif

/*
 * Option flags per-socket.
 */
#define	SO_DEBUG	0x0001		/* turn on debugging info recording */
#define	SO_ACCEPTCONN	0x0002		/* socket has had listen() */
#define	SO_REUSEADDR	0x0004		/* allow local address reuse */
#define	SO_KEEPALIVE	0x0008		/* keep connections alive */
#define	SO_DONTROUTE	0x0010		/* just use interface addresses */
#define	SO_BROADCAST	0x0020		/* permit sending of broadcast msgs */
#define	SO_USELOOPBACK	0x0040		/* bypass hardware when possible */
#define	SO_LINGER	0x0080		/* linger on close if data present */
#define	SO_OOBINLINE	0x0100		/* leave received OOB data in line */
#define	SO_REUSEPORT	0x0200		/* allow local address & port reuse */
#define SO_TIMESTAMP	0x0800		/* timestamp received dgram traffic */
#define SO_BINDANY	0x1000		/* allow bind to any address */
#define SO_ZEROIZE	0x2000		/* zero out all mbufs sent over socket */

/*
 * Additional options, not kept in so_options.
 */
#define	SO_SNDBUF	0x1001		/* send buffer size */
#define	SO_RCVBUF	0x1002		/* receive buffer size */
#define	SO_SNDLOWAT	0x1003		/* send low-water mark */
#define	SO_RCVLOWAT	0x1004		/* receive low-water mark */
#define	SO_SNDTIMEO	0x1005		/* send timeout */
#define	SO_RCVTIMEO	0x1006		/* receive timeout */
#define	SO_ERROR	0x1007		/* get error status and clear */
#define	SO_TYPE		0x1008		/* get socket type */
#define	SO_RTABLE	0x1021		/* routing table to be used */
#define	SO_PEERCRED	0x1022		/* get connect-time credentials */
#define	SO_SPLICE	0x1023		/* splice data to other socket */
#define	SO_DOMAIN	0x1024		/* get socket domain */
#define	SO_PROTOCOL	0x1025		/* get socket protocol */

/*
 * Structure used for manipulating linger option.
 */
struct	linger {
	int	l_onoff;		/* option on/off */
	int	l_linger;		/* linger time */
};

#if __BSD_VISIBLE

#ifndef _TIMEVAL_DECLARED
#define _TIMEVAL_DECLARED
struct timeval {
	time_t		tv_sec;		/* seconds */
	suseconds_t	tv_usec;	/* and microseconds */
};
#endif

/*
 * Structure used for manipulating splice option.
 */
struct	splice {
	int	sp_fd;			/* drain socket file descriptor */
	off_t	sp_max;			/* if set, maximum bytes to splice */
	struct	timeval	sp_idle;	/* idle timeout */
};

/*
 * Maximum number of alternate routing tables
 */
#define	RT_TABLEID_MAX		255
#define	RT_TABLEID_BITS		8
#define	RT_TABLEID_MASK		0xff

#endif /* __BSD_VISIBLE */

/*
 * Level number for (get/set)sockopt() to apply to socket itself.
 */
#define	SOL_SOCKET	0xffff		/* options for socket level */

/*
 * Address families.
 */
#define	AF_UNSPEC	0		/* unspecified */
#define	AF_UNIX		1		/* local to host */
#define	AF_LOCAL	AF_UNIX		/* draft POSIX compatibility */
#define	AF_INET		2		/* internetwork: UDP, TCP, etc. */
#define	AF_IMPLINK	3		/* arpanet imp addresses */
#define	AF_PUP		4		/* pup protocols: e.g. BSP */
#define	AF_CHAOS	5		/* mit CHAOS protocols */
#define	AF_NS		6		/* XEROX NS protocols */
#define	AF_ISO		7		/* ISO protocols */
#define	AF_OSI		AF_ISO
#define	AF_ECMA		8		/* european computer manufacturers */
#define	AF_DATAKIT	9		/* datakit protocols */
#define	AF_CCITT	10		/* CCITT protocols, X.25 etc */
#define	AF_SNA		11		/* IBM SNA */
#define AF_DECnet	12		/* DECnet */
#define AF_DLI		13		/* DEC Direct data link interface */
#define AF_LAT		14		/* LAT */
#define	AF_HYLINK	15		/* NSC Hyperchannel */
#define	AF_APPLETALK	16		/* Apple Talk */
#define	AF_ROUTE	17		/* Internal Routing Protocol */
#define	AF_LINK		18		/* Link layer interface */
#define	pseudo_AF_XTP	19		/* eXpress Transfer Protocol (no AF) */
#define	AF_COIP		20		/* connection-oriented IP, aka ST II */
#define	AF_CNT		21		/* Computer Network Technology */
#define pseudo_AF_RTIP	22		/* Help Identify RTIP packets */
#define	AF_IPX		23		/* Novell Internet Protocol */
#define	AF_INET6	24		/* IPv6 */
#define pseudo_AF_PIP	25		/* Help Identify PIP packets */
#define AF_ISDN		26		/* Integrated Services Digital Network*/
#define AF_E164		AF_ISDN		/* CCITT E.164 recommendation */
#define AF_NATM		27		/* native ATM access */
#define	AF_ENCAP	28
#define	AF_SIP		29		/* Simple Internet Protocol */
#define AF_KEY		30
#define pseudo_AF_HDRCMPLT 31		/* Used by BPF to not rewrite headers
					   in interface output routine */
#define	AF_BLUETOOTH	32		/* Bluetooth */
#define AF_MPLS         33              /* MPLS */
#define pseudo_AF_PFLOW 34		/* pflow */
#define pseudo_AF_PIPEX 35		/* PIPEX */
#define AF_FRAME        36		/* frame (Ethernet) sockets */
#define AF_MAX          37

/*
 * Structure used by kernel to store most
 * addresses.
 */
struct sockaddr {
	__uint8_t    sa_len;		/* total length */
	sa_family_t sa_family;		/* address family */
	char	    sa_data[14];	/* actually longer; address value */
};

/*
 * Sockaddr type which can hold any sockaddr type available
 * in the system.
 *
 * Note: __ss_{len,family} is defined in RFC2553.  During RFC2553 discussion
 * the field name went back and forth between ss_len and __ss_len,
 * and RFC2553 specifies it to be __ss_len.  openbsd picked ss_len.
 * For maximum portability, userland programmer would need to
 * (1) make the code never touch ss_len portion (cast it into sockaddr and
 * touch sa_len), or (2) add "-Dss_len=__ss_len" into CFLAGS to unify all
 * occurrences (including header file) to __ss_len.
 */
struct sockaddr_storage {
	__uint8_t	ss_len;		/* total length */
	sa_family_t	ss_family;	/* address family */
	unsigned char	__ss_pad1[6];	/* align to quad */
	__uint64_t	__ss_pad2;	/* force alignment for stupid compilers */
	unsigned char	__ss_pad3[240];	/* pad to a total of 256 bytes */
};

#ifdef _KERNEL
/*
 * Structure used by kernel to pass protocol
 * information in raw sockets.
 */
struct sockproto {
	unsigned short	sp_family;	/* address family */
	unsigned short	sp_protocol;	/* protocol */
};
#endif /* _KERNEL */

/*
 * Protocol families, same as address families for now.
 */
#define	PF_UNSPEC	AF_UNSPEC
#define	PF_LOCAL	AF_LOCAL
#define	PF_UNIX		AF_UNIX
#define	PF_INET		AF_INET
#define	PF_IMPLINK	AF_IMPLINK
#define	PF_PUP		AF_PUP
#define	PF_CHAOS	AF_CHAOS
#define	PF_NS		AF_NS
#define	PF_ISO		AF_ISO
#define	PF_OSI		AF_ISO
#define	PF_ECMA		AF_ECMA
#define	PF_DATAKIT	AF_DATAKIT
#define	PF_CCITT	AF_CCITT
#define	PF_SNA		AF_SNA
#define PF_DECnet	AF_DECnet
#define PF_DLI		AF_DLI
#define PF_LAT		AF_LAT
#define	PF_HYLINK	AF_HYLINK
#define	PF_APPLETALK	AF_APPLETALK
#define	PF_ROUTE	AF_ROUTE
#define	PF_LINK		AF_LINK
#define	PF_XTP		pseudo_AF_XTP	/* really just proto family, no AF */
#define	PF_COIP		AF_COIP
#define	PF_CNT		AF_CNT
#define	PF_IPX		AF_IPX		/* same format as AF_NS */
#define PF_INET6	AF_INET6
#define PF_RTIP		pseudo_AF_RTIP	/* same format as AF_INET */
#define PF_PIP		pseudo_AF_PIP
#define PF_ISDN		AF_ISDN
#define PF_NATM		AF_NATM
#define PF_ENCAP	AF_ENCAP
#define	PF_SIP		AF_SIP
#define PF_KEY		AF_KEY
#define PF_BPF		pseudo_AF_HDRCMPLT
#define	PF_BLUETOOTH	AF_BLUETOOTH
#define PF_MPLS		AF_MPLS
#define PF_PFLOW	pseudo_AF_PFLOW
#define PF_PIPEX	pseudo_AF_PIPEX
#define PF_FRAME	AF_FRAME
#define	PF_MAX		AF_MAX

/*
 * These are the valid values for the "how" field used by shutdown(2).
 */
#define	SHUT_RD		0
#define	SHUT_WR		1
#define	SHUT_RDWR	2

#if __BSD_VISIBLE
#define SA_LEN(x) ((x)->sa_len)

/* Read using getsockopt() with SOL_SOCKET, SO_PEERCRED */
struct sockpeercred {
	uid_t		uid;		/* effective user id */
	gid_t		gid;		/* effective group id */
	pid_t		pid;
};

/*
 * Definitions for network related sysctl, CTL_NET.
 *
 * Second level is protocol family.
 * Third level is protocol number.
 *
 * Further levels are defined by the individual families below.
 */
#define NET_MAXID	AF_MAX

#define CTL_NET_NAMES { \
	{ 0, 0 }, \
	{ "unix", CTLTYPE_NODE }, \
	{ "inet", CTLTYPE_NODE }, \
	{ "implink", CTLTYPE_NODE }, \
	{ "pup", CTLTYPE_NODE }, \
	{ "chaos", CTLTYPE_NODE }, \
	{ "xerox_ns", CTLTYPE_NODE }, \
	{ "iso", CTLTYPE_NODE }, \
	{ "ecma", CTLTYPE_NODE }, \
	{ "datakit", CTLTYPE_NODE }, \
	{ "ccitt", CTLTYPE_NODE }, \
	{ "ibm_sna", CTLTYPE_NODE }, \
	{ "decnet", CTLTYPE_NODE }, \
	{ "dec_dli", CTLTYPE_NODE }, \
	{ "lat", CTLTYPE_NODE }, \
	{ "hylink", CTLTYPE_NODE }, \
	{ "appletalk", CTLTYPE_NODE }, \
	{ "route", CTLTYPE_NODE }, \
	{ "link", CTLTYPE_NODE }, \
	{ "xtp", CTLTYPE_NODE }, \
	{ "coip", CTLTYPE_NODE }, \
	{ "cnt", CTLTYPE_NODE }, \
	{ "rtip", CTLTYPE_NODE }, \
	{ "ipx", CTLTYPE_NODE }, \
	{ "inet6", CTLTYPE_NODE }, \
	{ "pip", CTLTYPE_NODE }, \
	{ "isdn", CTLTYPE_NODE }, \
	{ "natm", CTLTYPE_NODE }, \
	{ "encap", CTLTYPE_NODE }, \
	{ "sip", CTLTYPE_NODE }, \
	{ "key", CTLTYPE_NODE }, \
	{ "bpf", CTLTYPE_NODE }, \
	{ "bluetooth", CTLTYPE_NODE }, \
	{ "mpls", CTLTYPE_NODE }, \
	{ "pflow", CTLTYPE_NODE }, \
	{ "pipex", CTLTYPE_NODE }, \
}

/*
 * PF_ROUTE - Routing table
 *
 * Four additional levels are defined:
 *	Fourth: address family, 0 is wildcard
 *	Fifth: type of info, defined below
 *	Sixth: flag(s) to mask with for NET_RT_FLAGS
 *	Seventh: routing table to use (facultative, defaults to 0)
 *		 NET_RT_TABLE has the table id as sixth element.
 */
#define NET_RT_DUMP	1		/* dump; may limit to a.f. */
#define NET_RT_FLAGS	2		/* by flags, e.g. RESOLVING */
#define NET_RT_IFLIST	3		/* survey interface list */
#define	NET_RT_STATS	4		/* routing table statistics */
#define	NET_RT_TABLE	5
#define	NET_RT_IFNAMES	6
#define	NET_RT_SOURCE	7
#define	NET_RT_MAXID	8

#define CTL_NET_RT_NAMES { \
	{ 0, 0 }, \
	{ "dump", CTLTYPE_STRUCT }, \
	{ "flags", CTLTYPE_STRUCT }, \
	{ "iflist", CTLTYPE_STRUCT }, \
	{ "stats", CTLTYPE_STRUCT }, \
	{ "table", CTLTYPE_STRUCT }, \
	{ "ifnames", CTLTYPE_STRUCT }, \
	{ "source", CTLTYPE_STRUCT }, \
}

/*
 * PF_UNIX - unix socket tunables
 */
#define NET_UNIX_INFLIGHT	6
#define NET_UNIX_DEFERRED	7
#define NET_UNIX_MAXID		8

#define CTL_NET_UNIX_NAMES { \
	{ 0, 0 }, \
	{ "stream", CTLTYPE_NODE }, \
	{ "dgram", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "seqpacket", CTLTYPE_NODE }, \
	{ "inflight", CTLTYPE_INT }, \
	{ "deferred", CTLTYPE_INT }, \
}

#define UNPCTL_RECVSPACE	1
#define UNPCTL_SENDSPACE	2
#define NET_UNIX_PROTO_MAXID	3

#define CTL_NET_UNIX_PROTO_NAMES { \
	{ 0, 0 }, \
	{ "recvspace", CTLTYPE_INT }, \
	{ "sendspace", CTLTYPE_INT }, \
}

/*
 * PF_LINK - link layer or device tunables
 */
#define NET_LINK_IFRXQ		1	/* net.link.ifrxq */
#define NET_LINK_MAXID		2

#define CTL_NET_LINK_NAMES { \
	{ 0, 0 }, \
	{ "ifrxq", CTLTYPE_NODE }, \
}

#define NET_LINK_IFRXQ_PRESSURE_RETURN \
				1	/* net.link.ifrxq.pressure_return */
#define NET_LINK_IFRXQ_PRESSURE_DROP \
				2	/* net.link.ifrxq.pressure_drop */
#define NET_LINK_IFRXQ_MAXID	3

#define CTL_NET_LINK_IFRXQ_NAMES { \
	{ 0, 0 }, \
	{ "pressure_return", CTLTYPE_INT }, \
	{ "pressure_drop", CTLTYPE_INT }, \
}

/*
 * PF_KEY - Key Management
 */
#define NET_KEY_SADB_DUMP	1	/* return SADB */
#define NET_KEY_SPD_DUMP	2	/* return SPD */
#define NET_KEY_MAXID		3

#define CTL_NET_KEY_NAMES { \
	{ 0, 0 }, \
	{ "sadb_dump", CTLTYPE_STRUCT }, \
	{ "spd_dump", CTLTYPE_STRUCT }, \
}

/*
 * PF_BPF  not really a family, but connected under CTL_NET
 */
#define NET_BPF_BUFSIZE		1		/* default buffer size */
#define NET_BPF_MAXBUFSIZE	2		/* maximum buffer size */
#define NET_BPF_MAXID		3

#define CTL_NET_BPF_NAMES { \
	{ 0, 0 }, \
	{ "bufsize", CTLTYPE_INT }, \
	{ "maxbufsize", CTLTYPE_INT }, \
}

/*
 * PF_PFLOW not really a family, but connected under CTL_NET
 */
#define NET_PFLOW_STATS		1		/* statistics */
#define NET_PFLOW_MAXID		2

#define CTL_NET_PFLOW_NAMES { \
	{ 0, 0 }, \
	{ "stats", CTLTYPE_STRUCT }, \
}
#endif /* __BSD_VISIBLE */

/*
 * Maximum queue length specifiable by listen(2).
 */
#define	SOMAXCONN	128

/*
 * Message header for recvmsg and sendmsg calls.
 * Used value-result for recvmsg, value only for sendmsg.
 */
struct msghdr {
	void		*msg_name;	/* optional address */
	socklen_t	msg_namelen;	/* size of address */
	struct		iovec *msg_iov;	/* scatter/gather array */
	unsigned int	msg_iovlen;	/* # elements in msg_iov */
	void		*msg_control;	/* ancillary data, see below */
	socklen_t	msg_controllen;	/* ancillary data buffer len */
	int		msg_flags;	/* flags on received message */
};

struct mmsghdr {
	struct msghdr msg_hdr;
	unsigned int msg_len;
};

struct timespec;

#define	MSG_OOB			0x1	/* process out-of-band data */
#define	MSG_PEEK		0x2	/* peek at incoming message */
#define	MSG_DONTROUTE		0x4	/* send without using routing tables */
#define	MSG_EOR			0x8	/* data completes record */
#define	MSG_TRUNC		0x10	/* data discarded before delivery */
#define	MSG_CTRUNC		0x20	/* control data lost before delivery */
#define	MSG_WAITALL		0x40	/* wait for full request or error */
#define	MSG_DONTWAIT		0x80	/* this message should be nonblocking */
#define	MSG_BCAST		0x100	/* this message rec'd as broadcast */
#define	MSG_MCAST		0x200	/* this message rec'd as multicast */
#define	MSG_NOSIGNAL		0x400	/* do not send SIGPIPE */
#define	MSG_CMSG_CLOEXEC	0x800	/* set FD_CLOEXEC on received fds */
#define	MSG_WAITFORONE		0x1000	/* nonblocking but wait for one msg */
#define	MSG_CMSG_CLOFORK	0x2000	/* set FD_CLOFORK on received fds */

/*
 * Header for ancillary data objects in msg_control buffer.
 * Used for additional information with/about a datagram
 * not expressible by flags.  The format is a sequence
 * of message elements headed by cmsghdr structures.
 */
struct cmsghdr {
	socklen_t	cmsg_len;	/* data byte count, including hdr */
	int		cmsg_level;	/* originating protocol */
	int		cmsg_type;	/* protocol-specific type */
/* followed by	u_char  cmsg_data[]; */
};

/* given pointer to struct cmsghdr, return pointer to data */
#define	CMSG_DATA(cmsg) \
	((unsigned char *)(cmsg) + _ALIGN(sizeof(struct cmsghdr)))

/* given pointer to struct cmsghdr, return pointer to next cmsghdr */
#define	CMSG_NXTHDR(mhdr, cmsg)	\
	(((char *)(cmsg) + _ALIGN((cmsg)->cmsg_len) + \
			    _ALIGN(sizeof(struct cmsghdr)) > \
	    ((char *)(mhdr)->msg_control) + (mhdr)->msg_controllen) ? \
	    (struct cmsghdr *)NULL : \
	    (struct cmsghdr *)((char *)(cmsg) + _ALIGN((cmsg)->cmsg_len)))

/*
 * RFC 2292 requires to check msg_controllen, in case that the kernel returns
 * an empty list for some reasons.
 */
#define	CMSG_FIRSTHDR(mhdr) \
	((mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? \
	 (struct cmsghdr *)(mhdr)->msg_control : \
	 (struct cmsghdr *)NULL)

/* Round len up to next alignment boundary */
#ifdef _KERNEL
#define CMSG_ALIGN(n)		_ALIGN(n)
#endif

/* Length of the contents of a control message of length len */
#define	CMSG_LEN(len)	(_ALIGN(sizeof(struct cmsghdr)) + (len))

/* Length of the space taken up by a padded control message of length len */
#define	CMSG_SPACE(len)	(_ALIGN(sizeof(struct cmsghdr)) + _ALIGN(len))

/* "Socket"-level control message types: */
#define	SCM_RIGHTS	0x01		/* access rights (array of int) */
#define	SCM_TIMESTAMP	0x04		/* timestamp (struct timeval) */

#ifndef _KERNEL

__BEGIN_DECLS
int	accept(int, struct sockaddr *, socklen_t *);
int	bind(int, const struct sockaddr *, socklen_t);
int	connect(int, const struct sockaddr *, socklen_t);
int	getpeername(int, struct sockaddr *, socklen_t *);
int	getsockname(int, struct sockaddr *, socklen_t *);
int	getsockopt(int, int, int, void *, socklen_t *);
int	listen(int, int);
ssize_t	recv(int, void *, size_t, int);
ssize_t	recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
ssize_t	recvmsg(int, struct msghdr *, int);
int	recvmmsg(int, struct mmsghdr *, unsigned int, int, struct timespec *);
ssize_t	send(int, const void *, size_t, int);
ssize_t	sendto(int, const void *,
	    size_t, int, const struct sockaddr *, socklen_t);
ssize_t	sendmsg(int, const struct msghdr *, int);
int	sendmmsg(int, struct mmsghdr *, unsigned int, int);
int	setsockopt(int, int, int, const void *, socklen_t);
int	shutdown(int, int);
int	sockatmark(int);
int	socket(int, int, int);
int	socketpair(int, int, int, int *);

#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
int	accept4(int, struct sockaddr *__restrict, socklen_t *__restrict, int);
#endif

#if __BSD_VISIBLE
int	getpeereid(int, uid_t *, gid_t *);
int	getrtable(void);
int	setrtable(int);
#endif /* __BSD_VISIBLE */

__END_DECLS

#else

static inline struct sockaddr *
sstosa(struct sockaddr_storage *ss)
{
	return ((struct sockaddr *)(ss));
}

#endif /* !_KERNEL */

#endif /* !_SYS_SOCKET_H_ */
