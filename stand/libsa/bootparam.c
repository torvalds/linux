/*	$NetBSD: bootparam.c,v 1.11 1997/06/26 19:11:32 drochner Exp $	*/

/*
 * Copyright (c) 1995 Gordon W. Ross
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Gordon W. Ross
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * RPC/bootparams
 */

#include <sys/param.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <string.h>

#include "rpcv2.h"

#include "stand.h"
#include "net.h"
#include "netif.h"
#include "rpc.h"
#include "bootparam.h"

#ifdef DEBUG_RPC
#define RPC_PRINTF(a)	printf a
#else
#define RPC_PRINTF(a)
#endif

struct in_addr	bp_server_addr;	/* net order */
n_short		bp_server_port;	/* net order */

/*
 * RPC definitions for bootparamd
 */
#define	BOOTPARAM_PROG		100026
#define	BOOTPARAM_VERS		1
#define BOOTPARAM_WHOAMI	1
#define BOOTPARAM_GETFILE	2

/*
 * Inet address in RPC messages
 * (Note, really four ints, NOT chars.  Blech.)
 */
struct xdr_inaddr {
	uint32_t  atype;
	int32_t	addr[4];
};

int xdr_inaddr_encode(char **p, struct in_addr ia);
int xdr_inaddr_decode(char **p, struct in_addr *ia);

int xdr_string_encode(char **p, char *str, int len);
int xdr_string_decode(char **p, char *str, int *len_p);


/*
 * RPC: bootparam/whoami
 * Given client IP address, get:
 *	client name	(hostname)
 *	domain name (domainname)
 *	gateway address
 *
 * The hostname and domainname are set here for convenience.
 *
 * Note - bpsin is initialized to the broadcast address,
 * and will be replaced with the bootparam server address
 * after this call is complete.  Have to use PMAP_PROC_CALL
 * to make sure we get responses only from a servers that
 * know about us (don't want to broadcast a getport call).
 */
int
bp_whoami(int sockfd)
{
	/* RPC structures for PMAPPROC_CALLIT */
	struct args {
		uint32_t prog;
		uint32_t vers;
		uint32_t proc;
		uint32_t arglen;
		struct xdr_inaddr xina;
	} *args;
	struct repl {
		uint16_t _pad;
		uint16_t port;
		uint32_t encap_len;
		/* encapsulated data here */
		n_long  capsule[64];
	} *repl;
	struct {
		n_long	h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	char *send_tail, *recv_head;
	struct iodesc *d;
	void *pkt;
	int len, x, rc;

	RPC_PRINTF(("bp_whoami: myip=%s\n", inet_ntoa(myip)));

	rc = -1;
	if (!(d = socktodesc(sockfd))) {
		RPC_PRINTF(("bp_whoami: bad socket. %d\n", sockfd));
		return (rc);
	}
	args = &sdata.d;

	/*
	 * Build request args for PMAPPROC_CALLIT.
	 */
	args->prog = htonl(BOOTPARAM_PROG);
	args->vers = htonl(BOOTPARAM_VERS);
	args->proc = htonl(BOOTPARAM_WHOAMI);
	args->arglen = htonl(sizeof(struct xdr_inaddr));
	send_tail = (char*) &args->xina;

	/*
	 * append encapsulated data (client IP address)
	 */
	if (xdr_inaddr_encode(&send_tail, myip))
		return (rc);

	/* RPC: portmap/callit */
	d->myport = htons(--rpc_port);
	d->destip.s_addr = INADDR_BROADCAST;	/* XXX: subnet bcast? */
	/* rpc_call will set d->destport */

	pkt = NULL;
	len = rpc_call(d, PMAPPROG, PMAPVERS, PMAPPROC_CALLIT,
	    args, send_tail - (char*)args, (void **)&repl, &pkt);
	if (len < 8) {
		printf("bootparamd: 'whoami' call failed\n");
		goto done;
	}

	/* Save bootparam server address (from IP header). */
	rpc_fromaddr(repl, &bp_server_addr, &bp_server_port);

	/*
	 * Note that bp_server_port is now 111 due to the
	 * indirect call (using PMAPPROC_CALLIT), so get the
	 * actual port number from the reply data.
	 */
	bp_server_port = repl->port;

	RPC_PRINTF(("bp_whoami: server at %s:%d\n",
	    inet_ntoa(bp_server_addr), ntohs(bp_server_port)));

	/* We have just done a portmap call, so cache the portnum. */
	rpc_pmap_putcache(bp_server_addr,
			  BOOTPARAM_PROG,
			  BOOTPARAM_VERS,
			  (int)ntohs(bp_server_port));

	/*
	 * Parse the encapsulated results from bootparam/whoami
	 */
	x = ntohl(repl->encap_len);
	if (len < x) {
		printf("bp_whoami: short reply, %d < %d\n", len, x);
		goto done;
	}
	recv_head = (char*) repl->capsule;

	/* client name */
	hostnamelen = MAXHOSTNAMELEN-1;
	if (xdr_string_decode(&recv_head, hostname, &hostnamelen)) {
		RPC_PRINTF(("bp_whoami: bad hostname\n"));
		goto done;
	}

	/* domain name */
	domainnamelen = MAXHOSTNAMELEN-1;
	if (xdr_string_decode(&recv_head, domainname, &domainnamelen)) {
		RPC_PRINTF(("bp_whoami: bad domainname\n"));
		goto done;
	}

	/* gateway address */
	if (xdr_inaddr_decode(&recv_head, &gateip)) {
		RPC_PRINTF(("bp_whoami: bad gateway\n"));
		goto done;
	}

	/* success */
	rc = 0;
done:
	free(pkt);
	return (rc);
}


/*
 * RPC: bootparam/getfile
 * Given client name and file "key", get:
 *	server name
 *	server IP address
 *	server pathname
 */
int
bp_getfile(int sockfd, char *key, struct in_addr *serv_addr, char *pathname)
{
	struct {
		n_long	h[RPC_HEADER_WORDS];
		n_long  d[64];
	} sdata;
	void *pkt;
	char serv_name[FNAME_SIZE];
	char *rdata, *send_tail;
	/* misc... */
	struct iodesc *d;
	int rc = -1, sn_len, path_len, rlen;

	if (!(d = socktodesc(sockfd))) {
		RPC_PRINTF(("bp_getfile: bad socket. %d\n", sockfd));
		return (-1);
	}

	send_tail = (char*) sdata.d;

	/*
	 * Build request message.
	 */

	/* client name (hostname) */
	if (xdr_string_encode(&send_tail, hostname, hostnamelen)) {
		RPC_PRINTF(("bp_getfile: bad client\n"));
		return (-1);
	}

	/* key name (root or swap) */
	if (xdr_string_encode(&send_tail, key, strlen(key))) {
		RPC_PRINTF(("bp_getfile: bad key\n"));
		return (-1);
	}

	/* RPC: bootparam/getfile */
	d->myport = htons(--rpc_port);
	d->destip   = bp_server_addr;
	/* rpc_call will set d->destport */
	pkt = NULL;
	rlen = rpc_call(d,
		BOOTPARAM_PROG, BOOTPARAM_VERS, BOOTPARAM_GETFILE,
		sdata.d, send_tail - (char*)sdata.d,
		(void **)&rdata, &pkt);
	if (rlen < 4) {
		RPC_PRINTF(("bp_getfile: short reply\n"));
		errno = EBADRPC;
		goto done;
	}

	/*
	 * Parse result message.
	 */

	/* server name */
	sn_len = FNAME_SIZE-1;
	if (xdr_string_decode(&rdata, serv_name, &sn_len)) {
		RPC_PRINTF(("bp_getfile: bad server name\n"));
		goto done;
	}

	/* server IP address (mountd/NFS) */
	if (xdr_inaddr_decode(&rdata, serv_addr)) {
		RPC_PRINTF(("bp_getfile: bad server addr\n"));
		goto done;
	}

	/* server pathname */
	path_len = MAXPATHLEN-1;
	if (xdr_string_decode(&rdata, pathname, &path_len)) {
		RPC_PRINTF(("bp_getfile: bad server path\n"));
		goto done;
	}

	/* success */
	rc = 0;
done:
	free(pkt);
	return (rc);
}


/*
 * eXternal Data Representation routines.
 * (but with non-standard args...)
 */


int
xdr_string_encode(char **pkt, char *str, int len)
{
	uint32_t *lenp;
	char *datap;
	int padlen = (len + 3) & ~3;	/* padded length */

	/* The data will be int aligned. */
	lenp = (uint32_t *) *pkt;
	*pkt += sizeof(*lenp);
	*lenp = htonl(len);

	datap = *pkt;
	*pkt += padlen;
	bcopy(str, datap, len);

	return (0);
}

int
xdr_string_decode(char **pkt, char *str, int *len_p)
{
	uint32_t *lenp;
	char *datap;
	int slen;	/* string length */
	int plen;	/* padded length */

	/* The data will be int aligned. */
	lenp = (uint32_t *) *pkt;
	*pkt += sizeof(*lenp);
	slen = ntohl(*lenp);
	plen = (slen + 3) & ~3;

	if (slen > *len_p)
		slen = *len_p;
	datap = *pkt;
	*pkt += plen;
	bcopy(datap, str, slen);

	str[slen] = '\0';
	*len_p = slen;

	return (0);
}


int
xdr_inaddr_encode(char **pkt, struct in_addr ia)
{
	struct xdr_inaddr *xi;
	u_char *cp;
	int32_t *ip;
	union {
		n_long l;	/* network order */
		u_char c[4];
	} uia;

	/* The data will be int aligned. */
	xi = (struct xdr_inaddr *) *pkt;
	*pkt += sizeof(*xi);
	xi->atype = htonl(1);
	uia.l = ia.s_addr;
	cp = uia.c;
	ip = xi->addr;
	/*
	 * Note: the htonl() calls below DO NOT
	 * imply that uia.l is in host order.
	 * In fact this needs it in net order.
	 */
	*ip++ = htonl((unsigned int)*cp++);
	*ip++ = htonl((unsigned int)*cp++);
	*ip++ = htonl((unsigned int)*cp++);
	*ip++ = htonl((unsigned int)*cp++);

	return (0);
}

int
xdr_inaddr_decode(char **pkt, struct in_addr *ia)
{
	struct xdr_inaddr *xi;
	u_char *cp;
	int32_t *ip;
	union {
		n_long l;	/* network order */
		u_char c[4];
	} uia;

	/* The data will be int aligned. */
	xi = (struct xdr_inaddr *) *pkt;
	*pkt += sizeof(*xi);
	if (xi->atype != htonl(1)) {
		RPC_PRINTF(("xdr_inaddr_decode: bad addrtype=%d\n",
		    ntohl(xi->atype)));
		return(-1);
	}

	cp = uia.c;
	ip = xi->addr;
	/*
	 * Note: the ntohl() calls below DO NOT
	 * imply that uia.l is in host order.
	 * In fact this needs it in net order.
	 */
	*cp++ = ntohl(*ip++);
	*cp++ = ntohl(*ip++);
	*cp++ = ntohl(*ip++);
	*cp++ = ntohl(*ip++);
	ia->s_addr = uia.l;

	return (0);
}
