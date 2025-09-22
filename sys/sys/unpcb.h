/*	$OpenBSD: unpcb.h,v 1.45 2022/11/26 17:51:18 mvs Exp $	*/
/*	$NetBSD: unpcb.h,v 1.6 1994/06/29 06:46:08 cgd Exp $	*/

/*
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
 */

#include <sys/refcnt.h>

/*
 * Protocol control block for an active
 * instance of a UNIX internal protocol.
 *
 * A socket may be associated with an vnode in the
 * file system.  If so, the unp_vnode pointer holds
 * a reference count to this vnode, which should be vrele'd
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
 *
 * Locks used to protect struct members:
 *      I       immutable after creation
 *      G       unp_gc_lock
 *      s       socket lock
 */


struct	unpcb {
	struct  refcnt unp_refcnt;      /* references to this pcb */
	struct	socket *unp_socket;	/* [I] pointer back to socket */
	struct	vnode *unp_vnode;	/* [s] if associated with file */
	struct	file *unp_file;		/* [G] backpointer for unp_gc() */
	struct	unpcb *unp_conn;	/* [s] control block of connected
						socket */
	ino_t	unp_ino;		/* [s] fake inode number */
	SLIST_HEAD(,unpcb) unp_refs;	/* [s] referencing socket linked list */
	SLIST_ENTRY(unpcb) unp_nextref;	/* [s] link in unp_refs list */
	struct	mbuf *unp_addr;		/* [s] bound address of socket */
	long	unp_msgcount;		/* [G] references from socket rcv buf */
	long	unp_gcrefs;		/* [G] references from gc */
	int	unp_flags;		/* [s] this unpcb contains peer eids */
	int	unp_gcflags;		/* [G] garbage collector flags */
	struct	sockpeercred unp_connid;/* [s] id of peer process */
	struct	timespec unp_ctime;	/* [I] holds creation time */
	LIST_ENTRY(unpcb) unp_link;	/* [G] link in per-AF list of sockets */
};

/*
 * flag bits in unp_flags
 */
#define UNP_FEIDS	0x01		/* unp_connid contains information */
#define UNP_FEIDSBIND	0x02		/* unp_connid was set by a bind */
#define UNP_BINDING	0x04		/* unp is binding now */
#define UNP_CONNECTING	0x08		/* unp is connecting now */

/*
 * flag bits in unp_gcflags
 */
#define UNP_GCDEAD	0x01		/* unp could be dead */

#define	sotounpcb(so)	((struct unpcb *)((so)->so_pcb))

#ifdef _KERNEL

struct stat;

struct fdpass {
	struct file	*fp;
	int		 flags;
};

extern const struct pr_usrreqs uipc_usrreqs;
extern const struct pr_usrreqs uipc_dgram_usrreqs;

int	uipc_attach(struct socket *, int, int);
int	uipc_detach(struct socket *);
int	uipc_bind(struct socket *, struct mbuf *, struct proc *);
int	uipc_listen(struct socket *);
int	uipc_connect(struct socket *, struct mbuf *);
int	uipc_accept(struct socket *, struct mbuf *);
int	uipc_disconnect(struct socket *);
int	uipc_shutdown(struct socket *);
int	uipc_dgram_shutdown(struct socket *);
void	uipc_rcvd(struct socket *);
int	uipc_send(struct socket *, struct mbuf *, struct mbuf *,
	    struct mbuf *);
int	uipc_dgram_send(struct socket *, struct mbuf *, struct mbuf *,
	    struct mbuf *);
void	uipc_abort(struct socket *);
int	uipc_sense(struct socket *, struct stat *);
int	uipc_sockaddr(struct socket *, struct mbuf *);
int	uipc_peeraddr(struct socket *, struct mbuf *);
int	uipc_connect2(struct socket *, struct socket *);

void	unp_init(void);
int	unp_connect(struct socket *, struct mbuf *, struct proc *);
int	unp_connect2(struct socket *, struct socket *);
void	unp_detach(struct unpcb *);
void	unp_disconnect(struct unpcb *);
void	unp_gc(void *);
int 	unp_externalize(struct mbuf *, socklen_t, int);
int	unp_internalize(struct mbuf *, struct proc *);
void 	unp_dispose(struct mbuf *);
#endif /* _KERNEL */
