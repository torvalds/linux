/*
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)rpc_fwd.c	8.1 (Berkeley) 6/6/93
 *	$Id: rpc_fwd.c,v 1.11 2019/06/28 13:32:46 deraadt Exp $
 */

/*
 * RPC packet forwarding
 */

#include "am.h"
#include <sys/ioctl.h>

/*
 * Note that the ID field in the external packet is only
 * ever treated as a 32 bit opaque data object, so there
 * is no need to convert to and from network byte ordering.
 */

/*
 * Each pending reply has an rpc_forward structure
 * associated with it.  These have a 15 second lifespan.
 * If a new structure is required, then an expired
 * one will be re-allocated if available, otherwise a fresh
 * one is allocated.  Whenever a reply is received the
 * structure is discarded.
 */
typedef struct rpc_forward rpc_forward;
struct rpc_forward {
	qelem	rf_q;		/* Linked list */
	time_t	rf_ttl;		/* Time to live */
	u_int	rf_xid;		/* Packet id */
	u_int	rf_oldid;	/* Original packet id */
	fwd_fun	rf_fwd;		/* Forwarding function */
	void *	rf_ptr;
	struct sockaddr_in rf_sin;
};

/*
 * Head of list of pending replies
 */
extern qelem rpc_head;
qelem rpc_head = { &rpc_head, &rpc_head };

static u_int xid;
#define	XID_ALLOC()	(xid++)

#define	MAX_PACKET_SIZE	8192	/* Maximum UDP packet size */

int fwd_sock;

/*
 * Allocate a rely structure
 */
static rpc_forward *
fwd_alloc()
{
	time_t now = clocktime();
	rpc_forward *p = 0, *p2;

#ifdef DEBUG
	/*dlog("fwd_alloca: rpc_head = %#x", rpc_head.q_forw);*/
#endif /* DEBUG */
	/*
	 * First search for an existing expired one.
	 */
	ITER(p2, rpc_forward, &rpc_head) {
		if (p2->rf_ttl <= now) {
			p = p2;
			break;
		}
	}

	/*
	 * If one couldn't be found then allocate
	 * a new structure and link it at the
	 * head of the list.
	 */
	if (p) {
		/*
		 * Call forwarding function to say that
		 * this message was junked.
		 */
#ifdef DEBUG
		dlog("Re-using packet forwarding slot - id %#x", p->rf_xid);
#endif /* DEBUG */
		if (p->rf_fwd)
			(*p->rf_fwd)(0, 0, 0, &p->rf_sin, p->rf_ptr, FALSE);
		rem_que(&p->rf_q);
	} else {
		p = ALLOC(rpc_forward);
	}
	ins_que(&p->rf_q, &rpc_head);

	/*
	 * Set the time to live field
	 * Timeout in 43 seconds
	 */
	p->rf_ttl = now + 43;

#ifdef DEBUG
	/*dlog("fwd_alloca: rpc_head = %#x", rpc_head.q_forw);*/
#endif /* DEBUG */
	return p;
}

/*
 * Free an allocated reply structure.
 * First unlink it from the list, then
 * discard it.
 */
static void
fwd_free(rpc_forward *p)
{
#ifdef DEBUG
	/*dlog("fwd_free: rpc_head = %#x", rpc_head.q_forw);*/
#endif /* DEBUG */
	rem_que(&p->rf_q);
#ifdef DEBUG
	/*dlog("fwd_free: rpc_head = %#x", rpc_head.q_forw);*/
#endif /* DEBUG */
	free(p);
}

/*
 * Initialise the RPC forwarder
 */
int fwd_init()
{
	/*
	 * Create ping socket
	 */
	fwd_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (fwd_sock == -1) {
		plog(XLOG_ERROR, "Unable to create RPC forwarding socket: %m");
		return errno;
	}

	/*
	 * Some things we talk to require a priv port - so make one here
	 */
	if (bind_resv_port(fwd_sock, (unsigned short *) 0) == -1)
		plog(XLOG_ERROR, "can't bind privileged port");

	return 0;
}

/*
 * Locate a packet in the forwarding list
 */
static rpc_forward *
fwd_locate(u_int id)
{
	rpc_forward *p;

	ITER(p, rpc_forward, &rpc_head) {
		if (p->rf_xid == id)
			return p;
	}

	return 0;
}

/*
 * This is called to forward a packet to another
 * RPC server.  The message id is changed and noted
 * so that when a reply appears we can tie it up
 * correctly.  Just matching the reply's source address
 * would not work because it might come from a
 * different address.
 */
int
fwd_packet(int type_id, void *pkt, int len, struct sockaddr_in *fwdto,
    struct sockaddr_in *replyto, void *i, fwd_fun cb)
{
	rpc_forward *p;
	u_int *pkt_int;
	int error;

	if ((int)amd_state >= (int)Finishing)
		return ENOENT;

	/*
	 * See if the type_id is fully specified.
	 * If so, then discard any old entries
	 * for this id.
	 * Otherwise make sure the type_id is
	 * fully qualified by allocating an id here.
	 */
#ifdef DEBUG
	switch (type_id & RPC_XID_MASK) {
	case RPC_XID_PORTMAP: dlog("Sending PORTMAP request"); break;
	case RPC_XID_MOUNTD: dlog("Sending MOUNTD request %#x", type_id); break;
	case RPC_XID_NFSPING: dlog("Sending NFS ping"); break;
	default: dlog("UNKNOWN RPC XID"); break;
	}
#endif /* DEBUG */

	if (type_id & ~RPC_XID_MASK) {
#ifdef DEBUG
		/*dlog("Fully qualified rpc type provided");*/
#endif /* DEBUG */
		p = fwd_locate(type_id);
		if (p) {
#ifdef DEBUG
			dlog("Discarding earlier rpc fwd handle");
#endif /* DEBUG */
			fwd_free(p);
		}
	} else {
#ifdef DEBUG
		dlog("Allocating a new xid...");
#endif /* DEBUG */
		type_id = MK_RPC_XID(type_id, XID_ALLOC());
	}

	p = fwd_alloc();
	if (!p)
		return ENOBUFS;

	error = 0;

	pkt_int = (u_int *) pkt;

	/*
	 * Get the original packet id
	 */
	p->rf_oldid = *pkt_int;

	/*
	 * Replace with newly allocated id
	 */
	p->rf_xid = *pkt_int = type_id;

	/*
	 * The sendto may fail if, for example, the route
	 * to a remote host is lost because an intermediate
	 * gateway has gone down.  Important to fill in the
	 * rest of "p" otherwise nasty things happen later...
	 */
#ifdef DEBUG
	{ char dq[20];

	dlog("Sending packet id %#x to %s.%d", p->rf_xid,
	    inet_dquad(dq, sizeof(dq), fwdto->sin_addr.s_addr),
	    ntohs(fwdto->sin_port));
	}
#endif /* DEBUG */
	if (sendto(fwd_sock, (char *) pkt, len, 0,
	    (struct sockaddr *) fwdto, sizeof(*fwdto)) == -1)
		error = errno;

	/*
	 * Save callback function and return address
	 */
	p->rf_fwd = cb;
	if (replyto)
		p->rf_sin = *replyto;
	else
		bzero(&p->rf_sin, sizeof(p->rf_sin));
	p->rf_ptr = i;

	return error;
}

/*
 * Called when some data arrives on the forwarding socket
 */
void
fwd_reply()
{
	int len;
#ifdef DYNAMIC_BUFFERS
	void *pkt;
#else
	u_int pkt[MAX_PACKET_SIZE/sizeof(u_int)+1];
#endif /* DYNAMIC_BUFFERS */
	u_int *pkt_int;
	int rc;
	rpc_forward *p;
	struct sockaddr_in src_addr;
	socklen_t src_addr_len;

	/*
	 * Determine the length of the packet
	 */
#ifdef DYNAMIC_BUFFERS
	if (ioctl(fwd_sock, FIONREAD, &len) == -1 || len < 0) {
		plog(XLOG_ERROR, "Error reading packet size: %m");
		return;
	}

	/*
	 * Allocate a buffer
	 */
	pkt = malloc(len);
	if (!pkt) {
		plog(XLOG_ERROR, "Out of buffers in fwd_reply");
		return;
	}
#else
	len = MAX_PACKET_SIZE;
#endif /* DYNAMIC_BUFFERS */

	/*
	 * Read the packet and check for validity
	 */
again:
	src_addr_len = sizeof(src_addr);
	rc = recvfrom(fwd_sock, (char *) pkt, len, 0,
	    (struct sockaddr *) &src_addr, &src_addr_len);
	if (rc == -1 || src_addr_len != sizeof(src_addr) ||
			src_addr.sin_family != AF_INET) {
		if (rc == -1 && errno == EINTR)
			goto again;
		plog(XLOG_ERROR, "Error reading RPC reply: %m");
		goto out;
	}

#ifdef DYNAMIC_BUFFERS
	if (rc != len) {
		plog(XLOG_ERROR, "Short read in fwd_reply");
		goto out;
	}
#endif /* DYNAMIC_BUFFERS */

	/*
	 * Do no more work if finishing soon
	 */
	if ((int)amd_state >= (int)Finishing)
		goto out;

	/*
	 * Find packet reference
	 */
	pkt_int = (u_int *) pkt;

#ifdef DEBUG
	switch (*pkt_int & RPC_XID_MASK) {
	case RPC_XID_PORTMAP: dlog("Receiving PORTMAP reply"); break;
	case RPC_XID_MOUNTD: dlog("Receiving MOUNTD reply %#x", *pkt_int); break;
	case RPC_XID_NFSPING: dlog("Receiving NFS ping %#x", *pkt_int); break;
	default: dlog("UNKNOWN RPC XID"); break;
	}
#endif /* DEBUG */

	p = fwd_locate(*pkt_int);
	if (!p) {
#ifdef DEBUG
		dlog("Can't forward reply id %#x", *pkt_int);
#endif /* DEBUG */
		goto out;
	}

	if (p->rf_fwd) {
		/*
		 * Put the original message id back
		 * into the packet.
		 */
		*pkt_int = p->rf_oldid;

		/*
		 * Call forwarding function
		 */
		(*p->rf_fwd)(pkt, rc, &src_addr, &p->rf_sin, p->rf_ptr, TRUE);
	}

	/*
	 * Free forwarding info
	 */
	fwd_free(p);

out:;
#ifdef DYNAMIC_BUFFERS
	/*
	 * Free the packet
	 */
	free(pkt);
#endif /* DYNAMIC_BUFFERS */
}
