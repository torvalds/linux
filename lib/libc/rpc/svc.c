/*	$OpenBSD: svc.c,v 1.29 2015/10/05 01:23:17 deraadt Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * svc.c, Server-side remote procedure call interface.
 *
 * There are two sets of procedures here.  The xprt routines are
 * for handling transport handles.  The svc routines handle the
 * list of service routines.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

static SVCXPRT **xports;
static int xportssize;

#define	RQCRED_SIZE	400		/* this size is excessive */

#define max(a, b) (a > b ? a : b)

/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * appropriate procedure.
 */
static struct svc_callout {
	struct svc_callout *sc_next;
	u_long		    sc_prog;
	u_long		    sc_vers;
	void		    (*sc_dispatch)();
} *svc_head;

static struct svc_callout *svc_find(u_long, u_long, struct svc_callout **);
static int svc_fd_insert(int);
static int svc_fd_remove(int);

int __svc_fdsetsize = FD_SETSIZE;
fd_set *__svc_fdset = &svc_fdset;
static int svc_pollfd_size;		/* number of slots in svc_pollfd */
static int svc_used_pollfd;		/* number of used slots in svc_pollfd */
static int *svc_pollfd_freelist;	/* svc_pollfd free list */
static int svc_max_free;		/* number of used slots in free list */

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
void
xprt_register(SVCXPRT *xprt)
{
	/* ignore failure conditions */
	(void) __xprt_register(xprt);
}

/*
 * Activate a transport handle.
 */
int
__xprt_register(SVCXPRT *xprt)
{
	int sock = xprt->xp_sock;

	if (xports == NULL || sock + 1 > xportssize) {
		SVCXPRT **xp;
		int size = FD_SETSIZE;

		while (sock + 1 > size)
			size += FD_SETSIZE;
		xp = calloc(size, sizeof(SVCXPRT *));
		if (xp == NULL)
			return (0);
		if (xports) {
			memcpy(xp, xports, xportssize * sizeof(SVCXPRT *));
			free(xports);
		}
		xportssize = size;
		xports = xp;
	}

	if (!svc_fd_insert(sock))
		return (0);
	xports[sock] = xprt;

	return (1);
}

/*
 * Insert a socket into svc_pollfd, svc_fdset and __svc_fdset.
 * If we are out of space, we allocate ~128 more slots than we
 * need now for future expansion.
 * We try to keep svc_pollfd well packed (no holes) as possible
 * so that poll(2) is efficient.
 */
static int
svc_fd_insert(int sock)
{
	int slot;

	/*
	 * Find a slot for sock in svc_pollfd; four possible cases:
	 *  1) need to allocate more space for svc_pollfd
	 *  2) there is an entry on the free list
	 *  3) the free list is empty (svc_used_pollfd is the next slot)
	 */
	if (svc_pollfd == NULL || svc_used_pollfd == svc_pollfd_size) {
		struct pollfd *pfd;
		int new_size, *new_freelist;

		new_size = svc_pollfd ? svc_pollfd_size + 128 : FD_SETSIZE;
		pfd = reallocarray(svc_pollfd, new_size, sizeof(*svc_pollfd));
		if (pfd == NULL)
			return (0);			/* no changes */
		new_freelist = realloc(svc_pollfd_freelist, new_size / 2);
		if (new_freelist == NULL) {
			free(pfd);
			return (0);			/* no changes */
		}
		svc_pollfd = pfd;
		svc_pollfd_size = new_size;
		svc_pollfd_freelist = new_freelist;
		for (slot = svc_used_pollfd; slot < svc_pollfd_size; slot++) {
			svc_pollfd[slot].fd = -1;
			svc_pollfd[slot].events = svc_pollfd[slot].revents = 0;
		}
		slot = svc_used_pollfd;
	} else if (svc_max_free != 0) {
		/* there is an entry on the free list, use it */
		slot = svc_pollfd_freelist[--svc_max_free];
	} else {
		/* nothing on the free list but we have room to grow */
		slot = svc_used_pollfd;
	}
	if (sock + 1 > __svc_fdsetsize) {
		fd_set *fds;
		size_t bytes;

		bytes = howmany(sock + 128, NFDBITS) * sizeof(fd_mask);
		/* realloc() would be nicer but it gets tricky... */
		if ((fds = (fd_set *)mem_alloc(bytes)) != NULL) {
			memset(fds, 0, bytes);
			memcpy(fds, __svc_fdset,
			    howmany(__svc_fdsetsize, NFDBITS) * sizeof(fd_mask));
			if (__svc_fdset != &svc_fdset)
				free(__svc_fdset);
			__svc_fdset = fds;
			__svc_fdsetsize = bytes / sizeof(fd_mask) * NFDBITS;
		}
	}

	svc_pollfd[slot].fd = sock;
	svc_pollfd[slot].events = POLLIN;
	svc_used_pollfd++;
	if (svc_max_pollfd < slot + 1)
		svc_max_pollfd = slot + 1;
	if (sock < FD_SETSIZE)
		FD_SET(sock, &svc_fdset);
	if (sock < __svc_fdsetsize && __svc_fdset != &svc_fdset)
		FD_SET(sock, __svc_fdset);
	svc_maxfd = max(svc_maxfd, sock);

	return (1);
}

/*
 * Remove a socket from svc_pollfd, svc_fdset and __svc_fdset.
 * Freed slots are placed on the free list.  If the free list fills
 * up, we compact svc_pollfd (free list size == svc_pollfd_size /2).
 */
static int
svc_fd_remove(int sock)
{
	int slot;

	if (svc_pollfd == NULL)
		return (0);

	for (slot = 0; slot < svc_max_pollfd; slot++) {
		if (svc_pollfd[slot].fd == sock) {
			svc_pollfd[slot].fd = -1;
			svc_pollfd[slot].events = svc_pollfd[slot].revents = 0;
			svc_used_pollfd--;
			if (sock < FD_SETSIZE)
				FD_CLR(sock, &svc_fdset);
			if (sock < __svc_fdsetsize && __svc_fdset != &svc_fdset)
				FD_CLR(sock, __svc_fdset);
			if (sock == svc_maxfd) {
				for (svc_maxfd--; svc_maxfd >= 0; svc_maxfd--)
					if (xports[svc_maxfd])
						break;
			}
			if (svc_max_free == svc_pollfd_size / 2) {
				int i, j;

				/*
				 * Out of space in the free list; this means
				 * that svc_pollfd is half full.  Pack things
				 * such that svc_max_pollfd == svc_used_pollfd
				 * and svc_pollfd_freelist is empty.
				 */
				for (i = svc_used_pollfd, j = 0;
				    i < svc_max_pollfd && j < svc_max_free; i++) {
					if (svc_pollfd[i].fd == -1)
						continue;
					/* be sure to use a low-numbered slot */
					while (svc_pollfd_freelist[j] >=
					    svc_used_pollfd)
						j++;
					svc_pollfd[svc_pollfd_freelist[j++]] =
					    svc_pollfd[i];
					svc_pollfd[i].fd = -1;
					svc_pollfd[i].events =
					    svc_pollfd[i].revents = 0;
				}
				svc_max_pollfd = svc_used_pollfd;
				svc_max_free = 0;
				/* could realloc if svc_pollfd_size is big */
			} else {
				/* trim svc_max_pollfd from the end */
				while (svc_max_pollfd > 0 &&
				    svc_pollfd[svc_max_pollfd - 1].fd == -1)
					svc_max_pollfd--;
			}
			svc_pollfd_freelist[svc_max_free++] = slot;

			return (1);
		}
	}
	return (0);		/* not found, shouldn't happen */
}

/*
 * De-activate a transport handle. 
 */
void
xprt_unregister(SVCXPRT *xprt)
{ 
	int sock = xprt->xp_sock;

	if (xports[sock] == xprt) {
		xports[sock] = NULL;
		svc_fd_remove(sock);
	}
}
DEF_WEAK(xprt_unregister);


/* ********************** CALLOUT list related stuff ************* */

/*
 * Add a service program to the callout list.
 * The dispatch routine will be called when a rpc request for this
 * program number comes in.
 */
bool_t
svc_register(SVCXPRT *xprt, u_long prog, u_long vers, void (*dispatch)(),
    int protocol)
{
	struct svc_callout *prev;
	struct svc_callout *s;

	if ((s = svc_find(prog, vers, &prev)) != NULL) {
		if (s->sc_dispatch == dispatch)
			goto pmap_it;  /* he is registering another xptr */
		return (FALSE);
	}
	s = (struct svc_callout *)mem_alloc(sizeof(struct svc_callout));
	if (s == NULL) {
		return (FALSE);
	}
	s->sc_prog = prog;
	s->sc_vers = vers;
	s->sc_dispatch = dispatch;
	s->sc_next = svc_head;
	svc_head = s;
pmap_it:
	/* now register the information with the local binder service */
	if (protocol) {
		return (pmap_set(prog, vers, protocol, xprt->xp_port));
	}
	return (TRUE);
}
DEF_WEAK(svc_register);

/*
 * Remove a service program from the callout list.
 */
void
svc_unregister(u_long prog, u_long vers)
{
	struct svc_callout *prev;
	struct svc_callout *s;

	if ((s = svc_find(prog, vers, &prev)) == NULL)
		return;
	if (prev == NULL) {
		svc_head = s->sc_next;
	} else {
		prev->sc_next = s->sc_next;
	}
	s->sc_next = NULL;
	mem_free((char *) s, (u_int) sizeof(struct svc_callout));
	/* now unregister the information with the local binder service */
	(void)pmap_unset(prog, vers);
}

/*
 * Search the callout list for a program number, return the callout
 * struct.
 */
static struct svc_callout *
svc_find(u_long prog, u_long vers, struct svc_callout **prev)
{
	struct svc_callout *s, *p;

	p = NULL;
	for (s = svc_head; s != NULL; s = s->sc_next) {
		if ((s->sc_prog == prog) && (s->sc_vers == vers))
			goto done;
		p = s;
	}
done:
	*prev = p;
	return (s);
}

/* ******************* REPLY GENERATION ROUTINES  ************ */

/*
 * Send a reply to an rpc request
 */
bool_t
svc_sendreply(SVCXPRT *xprt, xdrproc_t xdr_results, caddr_t xdr_location)
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf; 
	rply.acpted_rply.ar_stat = SUCCESS;
	rply.acpted_rply.ar_results.where = xdr_location;
	rply.acpted_rply.ar_results.proc = xdr_results;
	return (SVC_REPLY(xprt, &rply)); 
}
DEF_WEAK(svc_sendreply);

/*
 * No procedure error reply
 */
void
svcerr_noproc(SVCXPRT *xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROC_UNAVAIL;
	SVC_REPLY(xprt, &rply);
}

/*
 * Can't decode args error reply
 */
void
svcerr_decode(SVCXPRT *xprt)
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY; 
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = GARBAGE_ARGS;
	SVC_REPLY(xprt, &rply); 
}
DEF_WEAK(svcerr_decode);

/*
 * Some system error
 */
void
svcerr_systemerr(SVCXPRT *xprt)
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY; 
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = SYSTEM_ERR;
	SVC_REPLY(xprt, &rply); 
}

/*
 * Authentication error reply
 */
void
svcerr_auth(SVCXPRT *xprt, enum auth_stat why)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_DENIED;
	rply.rjcted_rply.rj_stat = AUTH_ERROR;
	rply.rjcted_rply.rj_why = why;
	SVC_REPLY(xprt, &rply);
}
DEF_WEAK(svcerr_auth);

/*
 * Auth too weak error reply
 */
void
svcerr_weakauth(SVCXPRT *xprt)
{

	svcerr_auth(xprt, AUTH_TOOWEAK);
}

/*
 * Program unavailable error reply
 */
void 
svcerr_noprog(SVCXPRT *xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_UNAVAIL;
	SVC_REPLY(xprt, &rply);
}
DEF_WEAK(svcerr_noprog);

/*
 * Program version mismatch error reply
 */
void
svcerr_progvers(SVCXPRT *xprt, u_long low_vers, u_long high_vers)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_MISMATCH;
	rply.acpted_rply.ar_vers.low = low_vers;
	rply.acpted_rply.ar_vers.high = high_vers;
	SVC_REPLY(xprt, &rply);
}
DEF_WEAK(svcerr_progvers);

/* ******************* SERVER INPUT STUFF ******************* */

/*
 * Get server side input from some transport.
 *
 * Statement of authentication parameters management:
 * This function owns and manages all authentication parameters, specifically
 * the "raw" parameters (msg.rm_call.cb_cred and msg.rm_call.cb_verf) and
 * the "cooked" credentials (rqst->rq_clntcred).
 * However, this function does not know the structure of the cooked
 * credentials, so it make the following assumptions: 
 *   a) the structure is contiguous (no pointers), and
 *   b) the cred structure size does not exceed RQCRED_SIZE bytes. 
 * In all events, all three parameters are freed upon exit from this routine.
 * The storage is trivially management on the call stack in userland, but
 * is mallocated in kernel land.
 */

void
svc_getreq(int rdfds)
{
	int bit;

	for (; (bit = ffs(rdfds)); rdfds ^= (1 << (bit - 1)))
		svc_getreq_common(bit - 1);
}
DEF_WEAK(svc_getreq);

void
svc_getreqset(fd_set *readfds)
{
	svc_getreqset2(readfds, FD_SETSIZE);
}

void
svc_getreqset2(fd_set *readfds, int width)
{
	fd_mask mask, *maskp;
	int bit, sock;

	maskp = readfds->fds_bits;
	for (sock = 0; sock < width; sock += NFDBITS) {
		for (mask = *maskp++; (bit = ffs(mask));
		    mask ^= (1 << (bit - 1)))
			svc_getreq_common(sock + bit - 1);
	}
}
DEF_WEAK(svc_getreqset2);

void
svc_getreq_poll(struct pollfd *pfd, const int nready)
{
	int i, n;

	for (n = nready, i = 0; n > 0; i++) {
		if (pfd[i].fd == -1)
			continue;
		if (pfd[i].revents != 0)
			n--;
		if ((pfd[i].revents & (POLLIN | POLLHUP)) == 0)
			continue;
		svc_getreq_common(pfd[i].fd);
	}
}
DEF_WEAK(svc_getreq_poll);

void
svc_getreq_common(int fd)
{
	enum xprt_stat stat;
	struct rpc_msg msg;
	int prog_found;
	u_long low_vers;
	u_long high_vers;
	struct svc_req r;
	SVCXPRT *xprt;
	char cred_area[2*MAX_AUTH_BYTES + RQCRED_SIZE];

	msg.rm_call.cb_cred.oa_base = cred_area;
	msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
	r.rq_clntcred = &(cred_area[2*MAX_AUTH_BYTES]);

	/* sock has input waiting */
	xprt = xports[fd];
	if (xprt == NULL)
		/* But do we control the fd? */
		return;
	/* now receive msgs from xprtprt (support batch calls) */
	do {
		if (SVC_RECV(xprt, &msg)) {
			/* find the exported program and call it */
			struct svc_callout *s;
			enum auth_stat why;

			r.rq_xprt = xprt;
			r.rq_prog = msg.rm_call.cb_prog;
			r.rq_vers = msg.rm_call.cb_vers;
			r.rq_proc = msg.rm_call.cb_proc;
			r.rq_cred = msg.rm_call.cb_cred;
			/* first authenticate the message */
			if ((why= _authenticate(&r, &msg)) != AUTH_OK) {
				svcerr_auth(xprt, why);
				goto call_done;
			}
			/* now match message with a registered service*/
			prog_found = FALSE;
			low_vers = (u_long) -1;
			high_vers = 0;
			for (s = svc_head; s != NULL; s = s->sc_next) {
				if (s->sc_prog == r.rq_prog) {
					if (s->sc_vers == r.rq_vers) {
						(*s->sc_dispatch)(&r, xprt);
						goto call_done;
					}  /* found correct version */
					prog_found = TRUE;
					if (s->sc_vers < low_vers)
						low_vers = s->sc_vers;
					if (s->sc_vers > high_vers)
						high_vers = s->sc_vers;
				}   /* found correct program */
			}
			/*
			 * if we got here, the program or version
			 * is not served ...
			 */
			if (prog_found)
				svcerr_progvers(xprt, low_vers, high_vers);
			else
				 svcerr_noprog(xprt);
			/* Fall through to ... */
		}
	call_done:
		if ((stat = SVC_STAT(xprt)) == XPRT_DIED){
			SVC_DESTROY(xprt);
			break;
		}
	} while (stat == XPRT_MOREREQS);
}
DEF_WEAK(svc_getreq_common);
