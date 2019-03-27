/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)unpcb.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _SYS_UNPCB_H_
#define _SYS_UNPCB_H_

typedef uint64_t unp_gen_t;

#if defined(_KERNEL) || defined(_WANT_UNPCB)
#include <sys/queue.h>
#include <sys/ucred.h>

/*
 * Protocol control block for an active
 * instance of a UNIX internal protocol.
 *
 * A socket may be associated with a vnode in the
 * filesystem.  If so, the unp_vnode pointer holds
 * a reference count to this vnode, which should be irele'd
 * when the socket goes away.
 *
 * A socket may be connected to another socket, in which
 * case the control block of the socket to which it is connected
 * is given by unp_conn.
 *
 * A socket may be referenced by a number of sockets (e.g. several
 * sockets may be connected to a datagram socket.)  These sockets
 * are in a linked list starting with unp_refs, linked through
 * unp_nextref and null-terminated.  Note that a socket may be referenced
 * by a number of other sockets and may also reference a socket (not
 * necessarily one which is referencing it).  This generates
 * the need for unp_refs and unp_nextref to be separate fields.
 *
 * Stream sockets keep copies of receive sockbuf sb_cc and sb_mbcnt
 * so that changes in the sockbuf may be computed to modify
 * back pressure on the sender accordingly.
 */
LIST_HEAD(unp_head, unpcb);

struct unpcb {
	/* Cache line 1 */
	struct	mtx unp_mtx;		/* mutex */
	struct	unpcb *unp_conn;	/* control block of connected socket */
	volatile u_int	unp_refcount;
	short	unp_flags;		/* flags */
	short	unp_gcflag;		/* Garbage collector flags. */
	struct	sockaddr_un *unp_addr;	/* bound address of socket */
	struct	socket *unp_socket;	/* pointer back to socket */
	/* Cache line 2 */
	struct	vnode *unp_vnode;	/* if associated with file */
	struct	xucred unp_peercred;	/* peer credentials, if applicable */
	LIST_ENTRY(unpcb) unp_reflink;	/* link in unp_refs list */
	LIST_ENTRY(unpcb) unp_link; 	/* glue on list of all PCBs */
	struct	unp_head unp_refs;	/* referencing socket linked list */
	unp_gen_t unp_gencnt;		/* generation count of this instance */
	struct	file *unp_file;		/* back-pointer to file for gc. */
	u_int	unp_msgcount;		/* references from message queue */
	ino_t	unp_ino;		/* fake inode number */
} __aligned(CACHE_LINE_SIZE);

/*
 * Flags in unp_flags.
 *
 * UNP_HAVEPC - indicates that the unp_peercred member is filled in
 * and is really the credentials of the connected peer.  This is used
 * to determine whether the contents should be sent to the user or
 * not.
 */
#define UNP_HAVEPC			0x001
#define	UNP_WANTCRED			0x004	/* credentials wanted */
#define	UNP_CONNWAIT			0x008	/* connect blocks until accepted */

/*
 * These flags are used to handle non-atomicity in connect() and bind()
 * operations on a socket: in particular, to avoid races between multiple
 * threads or processes operating simultaneously on the same socket.
 */
#define	UNP_CONNECTING			0x010	/* Currently connecting. */
#define	UNP_BINDING			0x020	/* Currently binding. */
#define	UNP_NASCENT			0x040	/* Newborn child socket. */

/*
 * Flags in unp_gcflag.
 */
#define	UNPGC_REF			0x1	/* unpcb has external ref. */
#define	UNPGC_DEAD			0x2	/* unpcb might be dead. */
#define	UNPGC_SCANNED			0x4	/* Has been scanned. */
#define	UNPGC_IGNORE_RIGHTS		0x8	/* Attached rights are freed */

#define	sotounpcb(so)	((struct unpcb *)((so)->so_pcb))

#endif	/* _KERNEL || _WANT_UNPCB */

/*
 * UNPCB structure exported to user-land via sysctl(3).
 *
 * Fields prefixed with "xu_" are unique to the export structure, and fields
 * with "unp_" or other prefixes match corresponding fields of 'struct unpcb'.
 *
 * Legend:
 * (s) - used by userland utilities in src
 * (p) - used by utilities in ports
 * (3) - is known to be used by third party software not in ports
 * (n) - no known usage
 *
 * Evil hack: declare only if sys/socketvar.h have been included.
 */
#ifdef	_SYS_SOCKETVAR_H_
struct xunpcb {
	ksize_t		xu_len;			/* length of this structure */
	kvaddr_t	xu_unpp;		/* to help netstat, fstat */
	kvaddr_t	unp_vnode;		/* (s) */
	kvaddr_t	unp_conn;		/* (s) */
	kvaddr_t	xu_firstref;		/* (s) */
	kvaddr_t	xu_nextref;		/* (s) */
	unp_gen_t	unp_gencnt;		/* (s) */
	int64_t		xu_spare64[8];
	int32_t		xu_spare32[8];
	union {
		struct	sockaddr_un xu_addr;	/* our bound address */
		char	xu_dummy1[256];
	};
	union {
		struct	sockaddr_un xu_caddr;	/* their bound address */
		char	xu_dummy2[256];
	};
	struct xsocket	xu_socket;
} __aligned(8);

struct xunpgen {
	ksize_t	xug_len;
	u_int	xug_count;
	unp_gen_t xug_gen;
	so_gen_t xug_sogen;
} __aligned(8);;
#endif /* _SYS_SOCKETVAR_H_ */

#if defined(_KERNEL)
struct thread;

/* In uipc_userreq.c */
void
unp_copy_peercred(struct thread *td, struct unpcb *client_unp,
    struct unpcb *server_unp, struct unpcb *listen_unp);
#endif

#endif /* _SYS_UNPCB_H_ */
