/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995, 1996
 *	Bill Paul <wpaul@ctr.columbia.edu>. All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Do standard and reverse DNS lookups using the resolver library.
 * Take care of all the dirty work here so the main program only has to
 * pass us a pointer to an array of characters.
 *
 * We have to use direct resolver calls here otherwise the YP server
 * could end up looping by calling itself over and over again until
 * it disappeared up its own belly button.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <resolv.h>
#include <unistd.h>

#include <rpcsvc/yp.h>
#include "yp_extern.h"

static char *
parse(struct hostent *hp)
{
	static char result[MAXHOSTNAMELEN * 2];
	int i;
	size_t len;
	char addr[46];

	if (hp == NULL)
		return(NULL);

	if (inet_ntop(hp->h_addrtype, hp->h_addr, addr, sizeof(addr)) == NULL)
		return(NULL);

	len = strlen(addr) + 1 + strlen(hp->h_name);
	for (i = 0; hp->h_aliases[i]; i++)
		len += strlen(hp->h_aliases[i]) + 1;
	len++;

	if (len > sizeof(result))
		return(NULL);

	bzero(result, sizeof(result));
	snprintf(result, sizeof(result), "%s %s", addr, hp->h_name);
	for (i = 0; hp->h_aliases[i]; i++) {
		strcat(result, " ");
		strcat(result, hp->h_aliases[i]);
	}

	return ((char *)&result);
}

#define MAXPACKET (64*1024)
#define DEF_TTL 50

#define BY_DNS_ID 1
#define BY_RPC_XID 2

extern struct hostent *__dns_getanswer(char *, int, char *, int);

static TAILQ_HEAD(dns_qhead, circleq_dnsentry) qhead;

struct circleq_dnsentry {
	SVCXPRT *xprt;
	unsigned long xid;
	struct sockaddr_in client_addr;
	unsigned long ypvers;
	unsigned long id;
	unsigned long ttl;
	unsigned long type;
	unsigned short prot_type;
	char **domain;
	char *name;
	int addrtype;
	int addrlen;
	uint32_t addr[4];	/* IPv4 or IPv6 */
	TAILQ_ENTRY(circleq_dnsentry) links;
};

static int pending = 0;

int
yp_init_resolver(void)
{
	TAILQ_INIT(&qhead);
	if (!(_res.options & RES_INIT) && res_init() == -1) {
		yp_error("res_init failed");
		return(1);
	}
	if ((resfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		yp_error("couldn't create socket");
		return(1);
	}
	if (fcntl(resfd, F_SETFL, O_NONBLOCK) == -1) {
		yp_error("couldn't make resolver socket non-blocking");
		return(1);
	}
	return(0);
}

static struct
circleq_dnsentry *yp_malloc_dnsent(void)
{
	register struct circleq_dnsentry *q;

	q = malloc(sizeof(struct circleq_dnsentry));

	if (q == NULL) {
		yp_error("failed to malloc() circleq dns entry");
		return(NULL);
	}

	return(q);
}

/*
 * Transmit a query.
 */
static unsigned long
yp_send_dns_query(char *name, int type)
{
	char buf[MAXPACKET];
	int n;
	HEADER *hptr;
	int ns;
	int rval;
	unsigned long id;

	bzero(buf, sizeof(buf));

	n = res_mkquery(QUERY,name,C_IN,type,NULL,0,NULL,buf,sizeof(buf));

	if (n <= 0) {
		yp_error("res_mkquery failed for %s type %d", name, type);
		return(0);
	}

	hptr = (HEADER *)&buf;
	id = ntohs(hptr->id);

	for (ns = 0; ns < _res.nscount; ns++) {
		rval = sendto(resfd, buf, n, 0,
			(struct sockaddr *)&_res.nsaddr_list[ns],
				sizeof(struct sockaddr));
		if (rval == -1) {
			yp_error("sendto failed");
			return(0);
		}
	}

	return(id);
}

static struct circleq_dnsentry *
yp_find_dnsqent(unsigned long id, int type)
{
	register struct circleq_dnsentry *q;

	TAILQ_FOREACH(q, &qhead, links) {
		switch (type) {
		case BY_RPC_XID:
			if (id == q->xid)
				return(q);
			break;
		case BY_DNS_ID:
		default:
			if (id == q->id)
				return(q);
			break;
		}
	}
	return (NULL);
}

static void
yp_send_dns_reply(struct circleq_dnsentry *q, char *buf)
{
	ypresponse result_v1;
	ypresp_val result_v2;
	unsigned long xid;
	struct sockaddr_in client_addr;
	xdrproc_t xdrfunc;
	char *result;

	/*
	 * Set up correct reply struct and
	 * XDR filter depending on ypvers.
	 */
	switch (q->ypvers) {
	case YPVERS:
		bzero((char *)&result_v2, sizeof(result_v2));

		if (buf == NULL)
			result_v2.stat = YP_NOKEY;
		else {
			result_v2.val.valdat_len = strlen(buf);
			result_v2.val.valdat_val = buf;
			result_v2.stat = YP_TRUE;
		}
		result = (char *)&result_v2;
		xdrfunc = (xdrproc_t)xdr_ypresp_val;
		break;
	case YPOLDVERS:
		/*
		 * The odds are we will _never_ execute this
		 * particular code, but we include it anyway
		 * for the sake of completeness.
		 */
		bzero((char *)&result_v1, sizeof(result_v1));
		result_v1.yp_resptype = YPRESP_VAL;

#define YPVAL ypresponse_u.yp_resp_valtype
		if (buf == NULL)
			result_v1.YPVAL.stat = YP_NOKEY;
		else {
			result_v1.YPVAL.val.valdat_len = strlen(buf);
			result_v1.YPVAL.val.valdat_val = buf;
			result_v1.YPVAL.stat = YP_TRUE;
		}
		result = (char *)&result_v1;
		xdrfunc = (xdrproc_t)xdr_ypresponse;
		break;
	default:
		yp_error("bad YP program version (%lu)!", q->ypvers);
			return;
		break;
	}

	if (debug)
		yp_error("sending dns reply to %s (%lu)",
			inet_ntoa(q->client_addr.sin_addr), q->id);
	/*
	 * XXX This is disgusting. There's basically one transport
	 * handle for UDP, but we're holding off on replying to a
	 * client until we're ready, by which time we may have received
	 * several other queries from other clients with different
	 * transaction IDs. So to make the delayed response thing work,
	 * we have to save the transaction ID and client address of
	 * each request, then jam them into the transport handle when
	 * we're ready to send a reply. Then after we've send the reply,
	 * we put the old transaction ID and remote address back the
	 * way we found 'em. This is _INCREDIBLY_ non-portable; it's
	 * not even supported by the RPC library.
	 */
	/*
	 * XXX Don't frob the transaction ID for TCP handles.
	 */
	if (q->prot_type == SOCK_DGRAM)
		xid = svcudp_set_xid(q->xprt, q->xid);
	client_addr = q->xprt->xp_raddr;
	q->xprt->xp_raddr = q->client_addr;

	if (!svc_sendreply(q->xprt, xdrfunc, result))
		yp_error("svc_sendreply failed");

	/*
	 * Now that we sent the reply,
	 * put the handle back the way it was.
	 */
	if (q->prot_type == SOCK_DGRAM)
		svcudp_set_xid(q->xprt, xid);
	q->xprt->xp_raddr = client_addr;

	return;
}

/*
 * Decrement TTL on all queue entries, possibly nuking
 * any that have been around too long without being serviced.
 */
void
yp_prune_dnsq(void)
{
	register struct circleq_dnsentry *q, *n;

	q = TAILQ_FIRST(&qhead);
	while (q != NULL) {
		q->ttl--;
		n = TAILQ_NEXT(q, links);
		if (!q->ttl) {
			TAILQ_REMOVE(&qhead, q, links);
			free(q->name);
			free(q);
			pending--;
		}
		q = n;
	}

	if (pending < 0)
		pending = 0;

	return;
}

/*
 * Data is pending on the DNS socket; check for valid replies
 * to our queries and dispatch them to waiting clients.
 */
void
yp_run_dnsq(void)
{
	register struct circleq_dnsentry *q;
	char buf[sizeof(HEADER) + MAXPACKET];
	struct sockaddr_in sin;
	socklen_t len;
	int rval;
	HEADER *hptr;
	struct hostent *hent;

	if (debug)
		yp_error("running dns queue");

	bzero(buf, sizeof(buf));

	len = sizeof(struct sockaddr_in);
	rval = recvfrom(resfd, buf, sizeof(buf), 0,
			(struct sockaddr *)&sin, &len);

	if (rval == -1) {
		yp_error("recvfrom failed: %s", strerror(errno));
		return;
	}

	/*
	 * We may have data left in the socket that represents
	 * replies to earlier queries that we don't care about
	 * anymore. If there are no lookups pending or the packet
	 * ID doesn't match any of the queue IDs, just drop it
	 * on the floor.
	 */
	hptr = (HEADER *)&buf;
	if (!pending ||
		(q = yp_find_dnsqent(ntohs(hptr->id), BY_DNS_ID)) == NULL) {
		/* ignore */
		return;
	}

	if (debug)
		yp_error("got dns reply from %s", inet_ntoa(sin.sin_addr));

	hent = __dns_getanswer(buf, rval, q->name, q->type);

	if (hent != NULL) {
		if (q->type == T_PTR) {
			hent->h_addr = (char *)q->addr;
			hent->h_addrtype = q->addrtype;
			hent->h_length = q->addrlen;
		}
	}

	/* Got an answer ready for a client -- send it off. */
	yp_send_dns_reply(q, parse(hent));
	pending--;
	TAILQ_REMOVE(&qhead, q, links);
	free(q->name);
	free(q);

	/* Decrement TTLs on other entries while we're here. */
	yp_prune_dnsq();

	return;
}

/*
 * Queue and transmit an asynchronous DNS hostname lookup.
 */
ypstat
yp_async_lookup_name(struct svc_req *rqstp, char *name, int af)
{
	register struct circleq_dnsentry *q;
	socklen_t len;
	int type;

	/* Check for SOCK_DGRAM or SOCK_STREAM -- we need to know later */
	type = -1;
	len = sizeof(type);
	if (getsockopt(rqstp->rq_xprt->xp_fd, SOL_SOCKET,
					SO_TYPE, &type, &len) == -1) {
		yp_error("getsockopt failed: %s", strerror(errno));
		return(YP_YPERR);
	}

	/* Avoid transmitting dupe requests. */
	if (type == SOCK_DGRAM &&
	    yp_find_dnsqent(svcudp_get_xid(rqstp->rq_xprt),BY_RPC_XID) != NULL)
		return(YP_TRUE);

	if ((q = yp_malloc_dnsent()) == NULL)
		return(YP_YPERR);

	q->type = (af == AF_INET) ? T_A : T_AAAA;
	q->ttl = DEF_TTL;
	q->xprt = rqstp->rq_xprt;
	q->ypvers = rqstp->rq_vers;
	q->prot_type = type;
	if (q->prot_type == SOCK_DGRAM)
		q->xid = svcudp_get_xid(q->xprt);
	q->client_addr = q->xprt->xp_raddr;
	q->domain = _res.dnsrch;
	q->id = yp_send_dns_query(name, q->type);

	if (q->id == 0) {
		yp_error("DNS query failed");
		free(q);
		return(YP_YPERR);
	}

	q->name = strdup(name);
	TAILQ_INSERT_HEAD(&qhead, q, links);
	pending++;

	if (debug)
		yp_error("queueing async DNS name lookup (%lu)", q->id);

	yp_prune_dnsq();
	return(YP_TRUE);
}

/*
 * Queue and transmit an asynchronous DNS IP address lookup.
 */
ypstat
yp_async_lookup_addr(struct svc_req *rqstp, char *addr, int af)
{
	register struct circleq_dnsentry *q;
	char buf[MAXHOSTNAMELEN], *qp;
	uint32_t abuf[4];	/* IPv4 or IPv6 */
	u_char *uaddr = (u_char *)abuf;
	socklen_t len;
	int type, n;

	/* Check for SOCK_DGRAM or SOCK_STREAM -- we need to know later */
	type = -1;
	len = sizeof(type);
	if (getsockopt(rqstp->rq_xprt->xp_fd, SOL_SOCKET,
					SO_TYPE, &type, &len) == -1) {
		yp_error("getsockopt failed: %s", strerror(errno));
		return(YP_YPERR);
	}

	/* Avoid transmitting dupe requests. */
	if (type == SOCK_DGRAM &&
	    yp_find_dnsqent(svcudp_get_xid(rqstp->rq_xprt),BY_RPC_XID) != NULL)
		return(YP_TRUE);

	switch (af) {
	case AF_INET:
		if (inet_aton(addr, (struct in_addr *)uaddr) != 1)
			return(YP_NOKEY);
		snprintf(buf, sizeof(buf), "%u.%u.%u.%u.in-addr.arpa",
		    (uaddr[3] & 0xff), (uaddr[2] & 0xff),
		    (uaddr[1] & 0xff), (uaddr[0] & 0xff));
		len = INADDRSZ;
		break;
	case AF_INET6:
		if (inet_pton(af, addr, uaddr) != 1)
			return(YP_NOKEY);
		qp = buf;
		for (n = IN6ADDRSZ - 1; n >= 0; n--) {
			qp += (size_t)sprintf(qp, "%x.%x.", uaddr[n] & 0xf,
			    (uaddr[n] >> 4) & 0xf);
		}
		strlcat(buf, "ip6.arpa", sizeof(buf));
		len = IN6ADDRSZ;
		break;
	default:
		return(YP_YPERR);
	}

	if ((q = yp_malloc_dnsent()) == NULL)
		return(YP_YPERR);

	if (debug)
		yp_error("DNS address is: %s", buf);

	q->type = T_PTR;
	q->ttl = DEF_TTL;
	q->xprt = rqstp->rq_xprt;
	q->ypvers = rqstp->rq_vers;
	q->domain = NULL;
	q->prot_type = type;
	if (q->prot_type == SOCK_DGRAM)
		q->xid = svcudp_get_xid(q->xprt);
	q->client_addr = q->xprt->xp_raddr;
	q->id = yp_send_dns_query(buf, q->type);

	if (q->id == 0) {
		yp_error("DNS query failed");
		free(q);
		return(YP_YPERR);
	}

	memcpy(q->addr, uaddr, len);
	q->addrlen = len;
	q->addrtype = af;
	q->name = strdup(buf);
	TAILQ_INSERT_HEAD(&qhead, q, links);
	pending++;

	if (debug)
		yp_error("queueing async DNS address lookup (%lu)", q->id);

	yp_prune_dnsq();
	return(YP_TRUE);
}
