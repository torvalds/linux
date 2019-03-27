/*	$NetBSD: krpc_subr.c,v 1.12.4.1 1996/06/07 00:52:26 cgd Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Gordon Ross, Adam Glass
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * partially based on:
 *      libnetboot/rpc.c
 *               @(#) Header: rpc.c,v 1.12 93/09/28 08:31:56 leres Exp  (LBL)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/vnet.h>

#include <netinet/in.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/rpc_msg.h>
#include <nfs/krpc.h>
#include <nfs/xdr_subs.h>

/*
 * Kernel support for Sun RPC
 *
 * Used currently for bootstrapping in nfs diskless configurations.
 */

/*
 * Generic RPC headers
 */

struct auth_info {
	u_int32_t 	authtype;	/* auth type */
	u_int32_t	authlen;	/* auth length */
};

struct auth_unix {
	int32_t   ua_time;
	int32_t   ua_hostname;	/* null */
	int32_t   ua_uid;
	int32_t   ua_gid;
	int32_t   ua_gidlist;	/* null */
};

struct krpc_call {
	u_int32_t	rp_xid;		/* request transaction id */
	int32_t 	rp_direction;	/* call direction (0) */
	u_int32_t	rp_rpcvers;	/* rpc version (2) */
	u_int32_t	rp_prog;	/* program */
	u_int32_t	rp_vers;	/* version */
	u_int32_t	rp_proc;	/* procedure */
	struct	auth_info rpc_auth;
	struct	auth_unix rpc_unix;
	struct	auth_info rpc_verf;
};

struct krpc_reply {
	u_int32_t rp_xid;		/* request transaction id */
	int32_t  rp_direction;		/* call direction (1) */
	int32_t  rp_astatus;		/* accept status (0: accepted) */
	union {
		u_int32_t rpu_errno;
		struct {
			struct auth_info rok_auth;
			u_int32_t	rok_status;
		} rpu_rok;
	} rp_u;
};
#define rp_errno  rp_u.rpu_errno
#define rp_auth   rp_u.rpu_rok.rok_auth
#define rp_status rp_u.rpu_rok.rok_status

#define MIN_REPLY_HDR 16	/* xid, dir, astat, errno */

/*
 * What is the longest we will wait before re-sending a request?
 * Note this is also the frequency of "RPC timeout" messages.
 * The re-send loop count sup linearly to this maximum, so the
 * first complaint will happen after (1+2+3+4+5)=15 seconds.
 */
#define	MAX_RESEND_DELAY 5	/* seconds */

/*
 * Call portmap to lookup a port number for a particular rpc program
 * Returns non-zero error on failure.
 */
int
krpc_portmap(struct sockaddr_in *sin, u_int prog, u_int vers, u_int16_t *portp,
    struct thread *td)
{
	struct sdata {
		u_int32_t prog;		/* call program */
		u_int32_t vers;		/* call version */
		u_int32_t proto;	/* call protocol */
		u_int32_t port;		/* call port (unused) */
	} *sdata;
	struct rdata {
		u_int16_t pad;
		u_int16_t port;
	} *rdata;
	struct mbuf *m;
	int error;

	/* The portmapper port is fixed. */
	if (prog == PMAPPROG) {
		*portp = htons(PMAPPORT);
		return 0;
	}

	m = m_get(M_WAITOK, MT_DATA);
	sdata = mtod(m, struct sdata *);
	m->m_len = sizeof(*sdata);

	/* Do the RPC to get it. */
	sdata->prog = txdr_unsigned(prog);
	sdata->vers = txdr_unsigned(vers);
	sdata->proto = txdr_unsigned(IPPROTO_UDP);
	sdata->port = 0;

	sin->sin_port = htons(PMAPPORT);
	error = krpc_call(sin, PMAPPROG, PMAPVERS,
					  PMAPPROC_GETPORT, &m, NULL, td);
	if (error)
		return error;

	if (m->m_len < sizeof(*rdata)) {
		m = m_pullup(m, sizeof(*rdata));
		if (m == NULL)
			return ENOBUFS;
	}
	rdata = mtod(m, struct rdata *);
	*portp = rdata->port;

	m_freem(m);
	return 0;
}

/*
 * Do a remote procedure call (RPC) and wait for its reply.
 * If from_p is non-null, then we are doing broadcast, and
 * the address from whence the response came is saved there.
 */
int
krpc_call(struct sockaddr_in *sa, u_int prog, u_int vers, u_int func,
    struct mbuf **data, struct sockaddr **from_p, struct thread *td)
{
	struct socket *so;
	struct sockaddr_in *sin, ssin;
	struct sockaddr *from;
	struct mbuf *m, *nam, *mhead;
	struct krpc_call *call;
	struct krpc_reply *reply;
	struct sockopt sopt;
	struct timeval tv;
	struct uio auio;
	int error, rcvflg, timo, secs, len;
	static u_int32_t xid = ~0xFF;
	u_int16_t tport;
	u_int32_t saddr;

	/*
	 * Validate address family.
	 * Sorry, this is INET specific...
	 */
	if (sa->sin_family != AF_INET)
		return (EAFNOSUPPORT);

	/* Free at end if not null. */
	nam = mhead = NULL;
	from = NULL;

	/*
	 * Create socket and set its receive timeout.
	 */
	if ((error = socreate(AF_INET, &so, SOCK_DGRAM, 0, td->td_ucred, td)))
		return error;

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	bzero(&sopt, sizeof sopt);
	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_level = SOL_SOCKET;
	sopt.sopt_name = SO_RCVTIMEO;
	sopt.sopt_val = &tv;
	sopt.sopt_valsize = sizeof tv;

	if ((error = sosetopt(so, &sopt)) != 0)
		goto out;

	/*
	 * Enable broadcast if necessary.
	 */
	if (from_p) {
		int on = 1;
		sopt.sopt_name = SO_BROADCAST;
		sopt.sopt_val = &on;
		sopt.sopt_valsize = sizeof on;
		if ((error = sosetopt(so, &sopt)) != 0)
			goto out;
	}

	/*
	 * Bind the local endpoint to a reserved port,
	 * because some NFS servers refuse requests from
	 * non-reserved (non-privileged) ports.
	 */
	sin = &ssin;
	bzero(sin, sizeof *sin);
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = INADDR_ANY;
	tport = IPPORT_RESERVED;
	do {
		tport--;
		sin->sin_port = htons(tport);
		error = sobind(so, (struct sockaddr *)sin, td);
	} while (error == EADDRINUSE &&
			 tport > IPPORT_RESERVED / 2);
	if (error) {
		printf("bind failed\n");
		goto out;
	}

	/*
	 * Setup socket address for the server.
	 */

	/*
	 * Prepend RPC message header.
	 */
	mhead = m_gethdr(M_WAITOK, MT_DATA);
	mhead->m_next = *data;
	call = mtod(mhead, struct krpc_call *);
	mhead->m_len = sizeof(*call);
	bzero((caddr_t)call, sizeof(*call));
	/* rpc_call part */
	xid++;
	call->rp_xid = txdr_unsigned(xid);
	/* call->rp_direction = 0; */
	call->rp_rpcvers = txdr_unsigned(2);
	call->rp_prog = txdr_unsigned(prog);
	call->rp_vers = txdr_unsigned(vers);
	call->rp_proc = txdr_unsigned(func);
	/* rpc_auth part (auth_unix as root) */
	call->rpc_auth.authtype = txdr_unsigned(AUTH_UNIX);
	call->rpc_auth.authlen  = txdr_unsigned(sizeof(struct auth_unix));
	/* rpc_verf part (auth_null) */
	call->rpc_verf.authtype = 0;
	call->rpc_verf.authlen  = 0;

	/*
	 * Setup packet header
	 */
	m_fixhdr(mhead);
	mhead->m_pkthdr.rcvif = NULL;

	/*
	 * Send it, repeatedly, until a reply is received,
	 * but delay each re-send by an increasing amount.
	 * If the delay hits the maximum, start complaining.
	 */
	timo = 0;
	for (;;) {
		/* Send RPC request (or re-send). */
		m = m_copym(mhead, 0, M_COPYALL, M_WAITOK);
		error = sosend(so, (struct sockaddr *)sa, NULL, m,
			       NULL, 0, td);
		if (error) {
			printf("krpc_call: sosend: %d\n", error);
			goto out;
		}
		m = NULL;

		/* Determine new timeout. */
		if (timo < MAX_RESEND_DELAY)
			timo++;
		else {
			saddr = ntohl(sa->sin_addr.s_addr);
			printf("RPC timeout for server %d.%d.%d.%d\n",
			       (saddr >> 24) & 255,
			       (saddr >> 16) & 255,
			       (saddr >> 8) & 255,
			       saddr & 255);
		}

		/*
		 * Wait for up to timo seconds for a reply.
		 * The socket receive timeout was set to 1 second.
		 */
		secs = timo;
		while (secs > 0) {
			if (from) {
				free(from, M_SONAME);
				from = NULL;
			}
			if (m) {
				m_freem(m);
				m = NULL;
			}
			bzero(&auio, sizeof(auio));
			auio.uio_resid = len = 1<<16;
			rcvflg = 0;
			error = soreceive(so, &from, &auio, &m, NULL, &rcvflg);
			if (error == EWOULDBLOCK) {
				secs--;
				continue;
			}
			if (error)
				goto out;
			len -= auio.uio_resid;

			/* Does the reply contain at least a header? */
			if (len < MIN_REPLY_HDR)
				continue;
			if (m->m_len < MIN_REPLY_HDR)
				continue;
			reply = mtod(m, struct krpc_reply *);

			/* Is it the right reply? */
			if (reply->rp_direction != txdr_unsigned(REPLY))
				continue;

			if (reply->rp_xid != txdr_unsigned(xid))
				continue;

			/* Was RPC accepted? (authorization OK) */
			if (reply->rp_astatus != 0) {
				error = fxdr_unsigned(u_int32_t, reply->rp_errno);
				printf("rpc denied, error=%d\n", error);
				continue;
			}

			/* Did the call succeed? */
			if (reply->rp_status != 0) {
				error = fxdr_unsigned(u_int32_t, reply->rp_status);
				if (error == PROG_MISMATCH) {
				  error = EBADRPC;
				  goto out;
				}
				printf("rpc denied, status=%d\n", error);
				continue;
			}

			goto gotreply;	/* break two levels */

		} /* while secs */
	} /* forever send/receive */

	error = ETIMEDOUT;
	goto out;

 gotreply:

	/*
	 * Get RPC reply header into first mbuf,
	 * get its length, then strip it off.
	 */
	len = sizeof(*reply);
	if (m->m_len < len) {
		m = m_pullup(m, len);
		if (m == NULL) {
			error = ENOBUFS;
			goto out;
		}
	}
	reply = mtod(m, struct krpc_reply *);
	if (reply->rp_auth.authtype != 0) {
		len += fxdr_unsigned(u_int32_t, reply->rp_auth.authlen);
		len = (len + 3) & ~3; /* XXX? */
	}
	m_adj(m, len);

	/* result */
	*data = m;
	if (from_p) {
		*from_p = from;
		from = NULL;
	}

 out:
	if (mhead) m_freem(mhead);
	if (from) free(from, M_SONAME);
	soclose(so);
	return error;
}

/*
 * eXternal Data Representation routines.
 * (but with non-standard args...)
 */

/*
 * String representation for RPC.
 */
struct xdr_string {
	u_int32_t len;		/* length without null or padding */
	char data[4];	/* data (longer, of course) */
    /* data is padded to a long-word boundary */
};

struct mbuf *
xdr_string_encode(char *str, int len)
{
	struct mbuf *m;
	struct xdr_string *xs;
	int dlen;	/* padded string length */
	int mlen;	/* message length */

	dlen = (len + 3) & ~3;
	mlen = dlen + 4;

	if (mlen > MCLBYTES)		/* If too big, we just can't do it. */
		return (NULL);

	m = m_get2(mlen, M_WAITOK, MT_DATA, 0);
	xs = mtod(m, struct xdr_string *);
	m->m_len = mlen;
	xs->len = txdr_unsigned(len);
	bcopy(str, xs->data, len);
	return (m);
}
