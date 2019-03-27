/*	$NetBSD: lock_proc.c,v 1.7 2000/10/11 20:23:56 is Exp $	*/
/*	$FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	A.R. Gordon (andrew.gordon@net-tel.co.uk).  All rights reserved.
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
 *	This product includes software developed for the FreeBSD project
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANDREW GORDON AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: lock_proc.c,v 1.7 2000/10/11 20:23:56 is Exp $");
#endif

#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <netconfig.h>

#include <rpc/rpc.h>
#include <rpcsvc/sm_inter.h>

#include "lockd.h"
#include <rpcsvc/nlm_prot.h>
#include "lockd_lock.h"


#define	CLIENT_CACHE_SIZE	64	/* No. of client sockets cached */
#define	CLIENT_CACHE_LIFETIME	120	/* In seconds */

#define	getrpcaddr(rqstp)	(struct sockaddr *)(svc_getrpccaller((rqstp)->rq_xprt)->buf)

static void	log_from_addr(const char *, struct svc_req *);
static void	log_netobj(netobj *obj);
static int	addrcmp(struct sockaddr *, struct sockaddr *);

/* log_from_addr ----------------------------------------------------------- */
/*
 * Purpose:	Log name of function called and source address
 * Returns:	Nothing
 * Notes:	Extracts the source address from the transport handle
 *		passed in as part of the called procedure specification
 */
static void
log_from_addr(const char *fun_name, struct svc_req *req)
{
	struct sockaddr *addr;
	char hostname_buf[NI_MAXHOST];

	addr = svc_getrpccaller(req->rq_xprt)->buf;
	if (getnameinfo(addr , addr->sa_len, hostname_buf, sizeof hostname_buf,
	    NULL, 0, 0) != 0)
		return;

	syslog(LOG_DEBUG, "%s from %s", fun_name, hostname_buf);
}

/* log_netobj ----------------------------------------------------------- */
/*
 * Purpose:	Log a netobj
 * Returns:	Nothing
 * Notes:	This function should only really be called as part of
 *  		a debug subsystem.
*/
static void
log_netobj(netobj *obj)
{
	char objvalbuffer[(sizeof(char)*2)*MAX_NETOBJ_SZ+2];
	char objascbuffer[sizeof(char)*MAX_NETOBJ_SZ+1];
	unsigned int i, maxlen;
	char *tmp1, *tmp2;

	/* Notify of potential security attacks */
	if (obj->n_len > MAX_NETOBJ_SZ)	{
		syslog(LOG_DEBUG, "SOMEONE IS TRYING TO DO SOMETHING NASTY!\n");
		syslog(LOG_DEBUG, "netobj too large! Should be %d was %d\n",
		    MAX_NETOBJ_SZ, obj->n_len);
	}
	/* Prevent the security hazard from the buffer overflow */
	maxlen = (obj->n_len < MAX_NETOBJ_SZ ? obj->n_len : MAX_NETOBJ_SZ);
	for (i=0, tmp1 = objvalbuffer, tmp2 = objascbuffer; i < maxlen;
	    i++, tmp1 +=2, tmp2 +=1) {
		sprintf(tmp1,"%02X",*(obj->n_bytes+i));
		sprintf(tmp2,"%c",*(obj->n_bytes+i));
	}
	*tmp1 = '\0';
	*tmp2 = '\0';
	syslog(LOG_DEBUG,"netobjvals: %s\n",objvalbuffer);
	syslog(LOG_DEBUG,"netobjascs: %s\n",objascbuffer);
}
/* get_client -------------------------------------------------------------- */
/*
 * Purpose:	Get a CLIENT* for making RPC calls to lockd on given host
 * Returns:	CLIENT* pointer, from clnt_udp_create, or NULL if error
 * Notes:	Creating a CLIENT* is quite expensive, involving a
 *		conversation with the remote portmapper to get the
 *		port number.  Since a given client is quite likely
 *		to make several locking requests in succession, it is
 *		desirable to cache the created CLIENT*.
 *
 *		Since we are using UDP rather than TCP, there is no cost
 *		to the remote system in keeping these cached indefinitely.
 *		Unfortunately there is a snag: if the remote system
 *		reboots, the cached portmapper results will be invalid,
 *		and we will never detect this since all of the xxx_msg()
 *		calls return no result - we just fire off a udp packet
 *		and hope for the best.
 *
 *		We solve this by discarding cached values after two
 *		minutes, regardless of whether they have been used
 *		in the meanwhile (since a bad one might have been used
 *		plenty of times, as the host keeps retrying the request
 *		and we keep sending the reply back to the wrong port).
 *
 *		Given that the entries will always expire in the order
 *		that they were created, there is no point in a LRU
 *		algorithm for when the cache gets full - entries are
 *		always re-used in sequence.
 */
static CLIENT *clnt_cache_ptr[CLIENT_CACHE_SIZE];
static long clnt_cache_time[CLIENT_CACHE_SIZE];	/* time entry created */
static struct sockaddr_storage clnt_cache_addr[CLIENT_CACHE_SIZE];
static rpcvers_t clnt_cache_vers[CLIENT_CACHE_SIZE];
static int clnt_cache_next_to_use = 0;

static int
addrcmp(struct sockaddr *sa1, struct sockaddr *sa2)
{
	int len;
	void *p1, *p2;

	if (sa1->sa_family != sa2->sa_family)
		return -1;

	switch (sa1->sa_family) {
	case AF_INET:
		p1 = &((struct sockaddr_in *)sa1)->sin_addr;
		p2 = &((struct sockaddr_in *)sa2)->sin_addr;
		len = 4;
		break;
	case AF_INET6:
		p1 = &((struct sockaddr_in6 *)sa1)->sin6_addr;
		p2 = &((struct sockaddr_in6 *)sa2)->sin6_addr;
		len = 16;
		break;
	default:
		return -1;
	}

	return memcmp(p1, p2, len);
}

CLIENT *
get_client(struct sockaddr *host_addr, rpcvers_t vers)
{
	CLIENT *client;
	struct timeval retry_time, time_now;
	int error, i;
	const char *netid;
	struct netconfig *nconf;
	char host[NI_MAXHOST];
	uid_t old_euid;
	int clnt_fd;

	gettimeofday(&time_now, NULL);

	/*
	 * Search for the given client in the cache, zapping any expired
	 * entries that we happen to notice in passing.
	 */
	for (i = 0; i < CLIENT_CACHE_SIZE; i++) {
		client = clnt_cache_ptr[i];
		if (client && ((clnt_cache_time[i] + CLIENT_CACHE_LIFETIME)
		    < time_now.tv_sec)) {
			/* Cache entry has expired. */
			if (debug_level > 3)
				syslog(LOG_DEBUG, "Expired CLIENT* in cache");
			clnt_cache_time[i] = 0L;
			clnt_destroy(client);
			clnt_cache_ptr[i] = NULL;
			client = NULL;
		}
		if (client && !addrcmp((struct sockaddr *)&clnt_cache_addr[i],
		    host_addr) && clnt_cache_vers[i] == vers) {
			/* Found it! */
			if (debug_level > 3)
				syslog(LOG_DEBUG, "Found CLIENT* in cache");
			return (client);
		}
	}

	if (debug_level > 3)
		syslog(LOG_DEBUG, "CLIENT* not found in cache, creating");

	/* Not found in cache.  Free the next entry if it is in use. */
	if (clnt_cache_ptr[clnt_cache_next_to_use]) {
		clnt_destroy(clnt_cache_ptr[clnt_cache_next_to_use]);
		clnt_cache_ptr[clnt_cache_next_to_use] = NULL;
	}

	/*
	 * Need a host string for clnt_tp_create. Use NI_NUMERICHOST
	 * to avoid DNS lookups.
	 */
	error = getnameinfo(host_addr, host_addr->sa_len, host, sizeof host,
			    NULL, 0, NI_NUMERICHOST);
	if (error != 0) {
		syslog(LOG_ERR, "unable to get name string for caller: %s",
		       gai_strerror(error));
		return NULL;
	}

#if 1
	if (host_addr->sa_family == AF_INET6)
		netid = "udp6";
	else
		netid = "udp";
#else 
	if (host_addr->sa_family == AF_INET6)
		netid = "tcp6";
	else
		netid = "tcp";
#endif
	nconf = getnetconfigent(netid);
	if (nconf == NULL) {
		syslog(LOG_ERR, "could not get netconfig info for '%s': "
				"no /etc/netconfig file?", netid);
		return NULL;
	}

	client = clnt_tp_create(host, NLM_PROG, vers, nconf);
	freenetconfigent(nconf);

	if (!client) {
		syslog(LOG_ERR, "%s", clnt_spcreateerror("clntudp_create"));
		syslog(LOG_ERR, "Unable to return result to %s", host);
		return NULL;
	}

	/* Get the FD of the client, for bindresvport. */ 
	clnt_control(client, CLGET_FD, &clnt_fd);

	/* Regain root privileges, for bindresvport. */
	old_euid = geteuid();
	if (seteuid(0) != 0) {
		syslog(LOG_ERR, "seteuid(0) failed");
		return NULL;
	}

	/*
	 * Bind the client FD to a reserved port.
	 * Some NFS servers reject any NLM request from a non-reserved port. 
	 */ 
	bindresvport(clnt_fd, NULL);

	/* Drop root privileges again. */
	if (seteuid(old_euid) != 0) {
		syslog(LOG_ERR, "seteuid(%d) failed", old_euid);
		return NULL;
	}

	/* Success - update the cache entry */
	clnt_cache_ptr[clnt_cache_next_to_use] = client;
	memcpy(&clnt_cache_addr[clnt_cache_next_to_use], host_addr,
	    host_addr->sa_len);
	clnt_cache_vers[clnt_cache_next_to_use] = vers;
	clnt_cache_time[clnt_cache_next_to_use] = time_now.tv_sec;
	if (++clnt_cache_next_to_use >= CLIENT_CACHE_SIZE)
		clnt_cache_next_to_use = 0;

	/*
	 * Disable the default timeout, so we can specify our own in calls
	 * to clnt_call().  (Note that the timeout is a different concept
	 * from the retry period set in clnt_udp_create() above.)
	 */
	retry_time.tv_sec = -1;
	retry_time.tv_usec = -1;
	clnt_control(client, CLSET_TIMEOUT, (char *)&retry_time);

	if (debug_level > 3)
		syslog(LOG_DEBUG, "Created CLIENT* for %s", host);
	return client;
}


/* transmit_result --------------------------------------------------------- */
/*
 * Purpose:	Transmit result for nlm_xxx_msg pseudo-RPCs
 * Returns:	Nothing - we have no idea if the datagram got there
 * Notes:	clnt_call() will always fail (with timeout) as we are
 *		calling it with timeout 0 as a hack to just issue a datagram
 *		without expecting a result
 */
void
transmit_result(int opcode, nlm_res *result, struct sockaddr *addr)
{
	static char dummy;
	CLIENT *cli;
	struct timeval timeo;
	int success;

	if ((cli = get_client(addr, NLM_VERS)) != NULL) {
		timeo.tv_sec = 0; /* No timeout - not expecting response */
		timeo.tv_usec = 0;

		success = clnt_call(cli, opcode, (xdrproc_t)xdr_nlm_res, result,
		    (xdrproc_t)xdr_void, &dummy, timeo);

		if (debug_level > 2)
			syslog(LOG_DEBUG, "clnt_call returns %d(%s)",
			    success, clnt_sperrno(success));
	}
}
/* transmit4_result --------------------------------------------------------- */
/*
 * Purpose:	Transmit result for nlm4_xxx_msg pseudo-RPCs
 * Returns:	Nothing - we have no idea if the datagram got there
 * Notes:	clnt_call() will always fail (with timeout) as we are
 *		calling it with timeout 0 as a hack to just issue a datagram
 *		without expecting a result
 */
void
transmit4_result(int opcode, nlm4_res *result, struct sockaddr *addr)
{
	static char dummy;
	CLIENT *cli;
	struct timeval timeo;
	int success;

	if ((cli = get_client(addr, NLM_VERS4)) != NULL) {
		timeo.tv_sec = 0; /* No timeout - not expecting response */
		timeo.tv_usec = 0;

		success = clnt_call(cli, opcode,
		    (xdrproc_t)xdr_nlm4_res, result,
		    (xdrproc_t)xdr_void, &dummy, timeo);

		if (debug_level > 2)
			syslog(LOG_DEBUG, "clnt_call returns %d(%s)",
			    success, clnt_sperrno(success));
	}
}

/*
 * converts a struct nlm_lock to struct nlm4_lock
 */
static void
nlmtonlm4(struct nlm_lock *arg, struct nlm4_lock *arg4)
{
	arg4->caller_name = arg->caller_name;
	arg4->fh = arg->fh;
	arg4->oh = arg->oh;
	arg4->svid = arg->svid;
	arg4->l_offset = arg->l_offset;
	arg4->l_len = arg->l_len;
}
/* ------------------------------------------------------------------------- */
/*
 * Functions for Unix<->Unix locking (ie. monitored locking, with rpc.statd
 * involved to ensure reclaim of locks after a crash of the "stateless"
 * server.
 *
 * These all come in two flavours - nlm_xxx() and nlm_xxx_msg().
 * The first are standard RPCs with argument and result.
 * The nlm_xxx_msg() calls implement exactly the same functions, but
 * use two pseudo-RPCs (one in each direction).  These calls are NOT
 * standard use of the RPC protocol in that they do not return a result
 * at all (NB. this is quite different from returning a void result).
 * The effect of this is to make the nlm_xxx_msg() calls simple unacknowledged
 * datagrams, requiring higher-level code to perform retries.
 *
 * Despite the disadvantages of the nlm_xxx_msg() approach (some of which
 * are documented in the comments to get_client() above), this is the
 * interface used by all current commercial NFS implementations
 * [Solaris, SCO, AIX etc.].  This is presumed to be because these allow
 * implementations to continue using the standard RPC libraries, while
 * avoiding the block-until-result nature of the library interface.
 *
 * No client implementations have been identified so far that make use
 * of the true RPC version (early SunOS releases would be a likely candidate
 * for testing).
 */

/* nlm_test ---------------------------------------------------------------- */
/*
 * Purpose:	Test whether a specified lock would be granted if requested
 * Returns:	nlm_granted (or error code)
 * Notes:
 */
nlm_testres *
nlm_test_1_svc(nlm_testargs *arg, struct svc_req *rqstp)
{
	static nlm_testres res;
	struct nlm4_lock arg4;
	struct nlm4_holder *holder;
	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_test", rqstp);

	holder = testlock(&arg4, arg->exclusive, 0);
	/*
	 * Copy the cookie from the argument into the result.  Note that this
	 * is slightly hazardous, as the structure contains a pointer to a
	 * malloc()ed buffer that will get freed by the caller.  However, the
	 * main function transmits the result before freeing the argument
	 * so it is in fact safe.
	 */
	res.cookie = arg->cookie;
	if (holder == NULL) {
		res.stat.stat = nlm_granted;
	} else {
		res.stat.stat = nlm_denied;
		memcpy(&res.stat.nlm_testrply_u.holder, holder,
		    sizeof(struct nlm_holder));
		res.stat.nlm_testrply_u.holder.l_offset = holder->l_offset;
		res.stat.nlm_testrply_u.holder.l_len = holder->l_len;
	}
	return (&res);
}

void *
nlm_test_msg_1_svc(nlm_testargs *arg, struct svc_req *rqstp)
{
	nlm_testres res;
	static char dummy;
	struct sockaddr *addr;
	CLIENT *cli;
	int success;
	struct timeval timeo;
	struct nlm4_lock arg4;
	struct nlm4_holder *holder;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_test_msg", rqstp);

	holder = testlock(&arg4, arg->exclusive, 0);

	res.cookie = arg->cookie;
	if (holder == NULL) {
		res.stat.stat = nlm_granted;
	} else {
		res.stat.stat = nlm_denied;
		memcpy(&res.stat.nlm_testrply_u.holder, holder,
		    sizeof(struct nlm_holder));
		res.stat.nlm_testrply_u.holder.l_offset = holder->l_offset;
		res.stat.nlm_testrply_u.holder.l_len = holder->l_len;
	}

	/*
	 * nlm_test has different result type to the other operations, so
	 * can't use transmit_result() in this case
	 */
	addr = svc_getrpccaller(rqstp->rq_xprt)->buf;
	if ((cli = get_client(addr, NLM_VERS)) != NULL) {
		timeo.tv_sec = 0; /* No timeout - not expecting response */
		timeo.tv_usec = 0;

		success = clnt_call(cli, NLM_TEST_RES,
		    (xdrproc_t)xdr_nlm_testres, &res,
		    (xdrproc_t)xdr_void, &dummy, timeo);

		if (debug_level > 2)
			syslog(LOG_DEBUG, "clnt_call returns %d", success);
	}
	return (NULL);
}

/* nlm_lock ---------------------------------------------------------------- */
/*
 * Purposes:	Establish a lock
 * Returns:	granted, denied or blocked
 * Notes:	*** grace period support missing
 */
nlm_res *
nlm_lock_1_svc(nlm_lockargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;
	struct nlm4_lockargs arg4;
	nlmtonlm4(&arg->alock, &arg4.alock);
	arg4.cookie = arg->cookie;
	arg4.block = arg->block;
	arg4.exclusive = arg->exclusive;
	arg4.reclaim = arg->reclaim;
	arg4.state = arg->state;

	if (debug_level)
		log_from_addr("nlm_lock", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	res.cookie = arg->cookie;

	res.stat.stat = getlock(&arg4, rqstp, LOCK_MON);
	return (&res);
}

void *
nlm_lock_msg_1_svc(nlm_lockargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;
	struct nlm4_lockargs arg4;

	nlmtonlm4(&arg->alock, &arg4.alock);
	arg4.cookie = arg->cookie;
	arg4.block = arg->block;
	arg4.exclusive = arg->exclusive;
	arg4.reclaim = arg->reclaim;
	arg4.state = arg->state;

	if (debug_level)
		log_from_addr("nlm_lock_msg", rqstp);

	res.cookie = arg->cookie;
	res.stat.stat = getlock(&arg4, rqstp, LOCK_ASYNC | LOCK_MON);
	transmit_result(NLM_LOCK_RES, &res, getrpcaddr(rqstp));

	return (NULL);
}

/* nlm_cancel -------------------------------------------------------------- */
/*
 * Purpose:	Cancel a blocked lock request
 * Returns:	granted or denied
 * Notes:
 */
nlm_res *
nlm_cancel_1_svc(nlm_cancargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_cancel", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	res.cookie = arg->cookie;

	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	res.stat.stat = unlock(&arg4, LOCK_CANCEL);
	return (&res);
}

void *
nlm_cancel_msg_1_svc(nlm_cancargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_cancel_msg", rqstp);

	res.cookie = arg->cookie;
	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	res.stat.stat = unlock(&arg4, LOCK_CANCEL);
	transmit_result(NLM_CANCEL_RES, &res, getrpcaddr(rqstp));
	return (NULL);
}

/* nlm_unlock -------------------------------------------------------------- */
/*
 * Purpose:	Release an existing lock
 * Returns:	Always granted, unless during grace period
 * Notes:	"no such lock" error condition is ignored, as the
 *		protocol uses unreliable UDP datagrams, and may well
 *		re-try an unlock that has already succeeded.
 */
nlm_res *
nlm_unlock_1_svc(nlm_unlockargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_unlock", rqstp);

	res.stat.stat = unlock(&arg4, 0);
	res.cookie = arg->cookie;

	return (&res);
}

void *
nlm_unlock_msg_1_svc(nlm_unlockargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_unlock_msg", rqstp);

	res.stat.stat = unlock(&arg4, 0);
	res.cookie = arg->cookie;

	transmit_result(NLM_UNLOCK_RES, &res, getrpcaddr(rqstp));
	return (NULL);
}

/* ------------------------------------------------------------------------- */
/*
 * Client-side pseudo-RPCs for results.  Note that for the client there
 * are only nlm_xxx_msg() versions of each call, since the 'real RPC'
 * version returns the results in the RPC result, and so the client
 * does not normally receive incoming RPCs.
 *
 * The exception to this is nlm_granted(), which is genuinely an RPC
 * call from the server to the client - a 'call-back' in normal procedure
 * call terms.
 */

/* nlm_granted ------------------------------------------------------------- */
/*
 * Purpose:	Receive notification that formerly blocked lock now granted
 * Returns:	always success ('granted')
 * Notes:
 */
nlm_res *
nlm_granted_1_svc(nlm_testargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;

	if (debug_level)
		log_from_addr("nlm_granted", rqstp);

	res.stat.stat = lock_answer(arg->alock.svid, &arg->cookie,
		nlm_granted, NULL, NLM_VERS) == 0 ?
		nlm_granted : nlm_denied;

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	res.cookie = arg->cookie;

	return (&res);
}

void *
nlm_granted_msg_1_svc(nlm_testargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;

	if (debug_level)
		log_from_addr("nlm_granted_msg", rqstp);

	res.cookie = arg->cookie;
	res.stat.stat = lock_answer(arg->alock.svid, &arg->cookie,
		nlm_granted, NULL, NLM_VERS) == 0 ?
		nlm_granted : nlm_denied;

	transmit_result(NLM_GRANTED_RES, &res, getrpcaddr(rqstp));
	return (NULL);
}

/* nlm_test_res ------------------------------------------------------------ */
/*
 * Purpose:	Accept result from earlier nlm_test_msg() call
 * Returns:	Nothing
 */
void *
nlm_test_res_1_svc(nlm_testres *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm_test_res", rqstp);
	(void)lock_answer(-1, &arg->cookie, arg->stat.stat, 
		&arg->stat.nlm_testrply_u.holder.svid, NLM_VERS);
	return (NULL);
}

/* nlm_lock_res ------------------------------------------------------------ */
/*
 * Purpose:	Accept result from earlier nlm_lock_msg() call
 * Returns:	Nothing
 */
void *
nlm_lock_res_1_svc(nlm_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm_lock_res", rqstp);

	(void)lock_answer(-1, &arg->cookie, arg->stat.stat, NULL, NLM_VERS);

	return (NULL);
}

/* nlm_cancel_res ---------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_cancel_msg() call
 * Returns:	Nothing
 */
void *
nlm_cancel_res_1_svc(nlm_res *arg __unused, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm_cancel_res", rqstp);
	return (NULL);
}

/* nlm_unlock_res ---------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_unlock_msg() call
 * Returns:	Nothing
 */
void *
nlm_unlock_res_1_svc(nlm_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm_unlock_res", rqstp);

	lock_answer(-1, &arg->cookie, arg->stat.stat, NULL, NLM_VERS);

	return (NULL);
}

/* nlm_granted_res --------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_granted_msg() call
 * Returns:	Nothing
 */
void *
nlm_granted_res_1_svc(nlm_res *arg __unused, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm_granted_res", rqstp);
	return (NULL);
}

/* ------------------------------------------------------------------------- */
/*
 * Calls for PCNFS locking (aka non-monitored locking, no involvement
 * of rpc.statd).
 *
 * These are all genuine RPCs - no nlm_xxx_msg() nonsense here.
 */

/* nlm_share --------------------------------------------------------------- */
/*
 * Purpose:	Establish a DOS-style lock
 * Returns:	success or failure
 * Notes:	Blocking locks are not supported - client is expected
 *		to retry if required.
 */
nlm_shareres *
nlm_share_3_svc(nlm_shareargs *arg, struct svc_req *rqstp)
{
	static nlm_shareres res;

	if (debug_level)
		log_from_addr("nlm_share", rqstp);

	res.cookie = arg->cookie;
	res.stat = nlm_granted;
	res.sequence = 1234356;	/* X/Open says this field is ignored? */
	return (&res);
}

/* nlm_unshare ------------------------------------------------------------ */
/*
 * Purpose:	Release a DOS-style lock
 * Returns:	nlm_granted, unless in grace period
 * Notes:
 */
nlm_shareres *
nlm_unshare_3_svc(nlm_shareargs *arg, struct svc_req *rqstp)
{
	static nlm_shareres res;

	if (debug_level)
		log_from_addr("nlm_unshare", rqstp);

	res.cookie = arg->cookie;
	res.stat = nlm_granted;
	res.sequence = 1234356;	/* X/Open says this field is ignored? */
	return (&res);
}

/* nlm_nm_lock ------------------------------------------------------------ */
/*
 * Purpose:	non-monitored version of nlm_lock()
 * Returns:	as for nlm_lock()
 * Notes:	These locks are in the same style as the standard nlm_lock,
 *		but the rpc.statd should not be called to establish a
 *		monitor for the client machine, since that machine is
 *		declared not to be running a rpc.statd, and so would not
 *		respond to the statd protocol.
 */
nlm_res *
nlm_nm_lock_3_svc(nlm_lockargs *arg, struct svc_req *rqstp)
{
	static nlm_res res;

	if (debug_level)
		log_from_addr("nlm_nm_lock", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	res.cookie = arg->cookie;
	res.stat.stat = nlm_granted;
	return (&res);
}

/* nlm_free_all ------------------------------------------------------------ */
/*
 * Purpose:	Release all locks held by a named client
 * Returns:	Nothing
 * Notes:	Potential denial of service security problem here - the
 *		locks to be released are specified by a host name, independent
 *		of the address from which the request has arrived.
 *		Should probably be rejected if the named host has been
 *		using monitored locks.
 */
void *
nlm_free_all_3_svc(nlm_notify *arg __unused, struct svc_req *rqstp)
{
	static char dummy;

	if (debug_level)
		log_from_addr("nlm_free_all", rqstp);
	return (&dummy);
}

/* calls for nlm version 4 (NFSv3) */
/* nlm_test ---------------------------------------------------------------- */
/*
 * Purpose:	Test whether a specified lock would be granted if requested
 * Returns:	nlm_granted (or error code)
 * Notes:
 */
nlm4_testres *
nlm4_test_4_svc(nlm4_testargs *arg, struct svc_req *rqstp)
{
	static nlm4_testres res;
	struct nlm4_holder *holder;

	if (debug_level)
		log_from_addr("nlm4_test", rqstp);
	if (debug_level > 5) {
		syslog(LOG_DEBUG, "Locking arguments:\n");
		log_netobj(&(arg->cookie));
		syslog(LOG_DEBUG, "Alock arguments:\n");
		syslog(LOG_DEBUG, "Caller Name: %s\n",arg->alock.caller_name);
		syslog(LOG_DEBUG, "File Handle:\n");
		log_netobj(&(arg->alock.fh));
		syslog(LOG_DEBUG, "Owner Handle:\n");
		log_netobj(&(arg->alock.oh));
		syslog(LOG_DEBUG, "SVID:        %d\n", arg->alock.svid);
		syslog(LOG_DEBUG, "Lock Offset: %llu\n",
		    (unsigned long long)arg->alock.l_offset);
		syslog(LOG_DEBUG, "Lock Length: %llu\n",
		    (unsigned long long)arg->alock.l_len);
		syslog(LOG_DEBUG, "Exclusive:   %s\n",
		    (arg->exclusive ? "true" : "false"));
	}

	holder = testlock(&arg->alock, arg->exclusive, LOCK_V4);

	/*
	 * Copy the cookie from the argument into the result.  Note that this
	 * is slightly hazardous, as the structure contains a pointer to a
	 * malloc()ed buffer that will get freed by the caller.  However, the
	 * main function transmits the result before freeing the argument
	 * so it is in fact safe.
	 */
	res.cookie = arg->cookie;
	if (holder == NULL) {
		res.stat.stat = nlm4_granted;
	} else {
		res.stat.stat = nlm4_denied;
		memcpy(&res.stat.nlm4_testrply_u.holder, holder,
		    sizeof(struct nlm4_holder));
	}
	return (&res);
}

void *
nlm4_test_msg_4_svc(nlm4_testargs *arg, struct svc_req *rqstp)
{
	nlm4_testres res;
	static char dummy;
	struct sockaddr *addr;
	CLIENT *cli;
	int success;
	struct timeval timeo;
	struct nlm4_holder *holder;

	if (debug_level)
		log_from_addr("nlm4_test_msg", rqstp);

	holder = testlock(&arg->alock, arg->exclusive, LOCK_V4);

	res.cookie = arg->cookie;
	if (holder == NULL) {
		res.stat.stat = nlm4_granted;
	} else {
		res.stat.stat = nlm4_denied;
		memcpy(&res.stat.nlm4_testrply_u.holder, holder,
		    sizeof(struct nlm4_holder));
	}

	/*
	 * nlm_test has different result type to the other operations, so
	 * can't use transmit4_result() in this case
	 */
	addr = svc_getrpccaller(rqstp->rq_xprt)->buf;
	if ((cli = get_client(addr, NLM_VERS4)) != NULL) {
		timeo.tv_sec = 0; /* No timeout - not expecting response */
		timeo.tv_usec = 0;

		success = clnt_call(cli, NLM4_TEST_RES,
		    (xdrproc_t)xdr_nlm4_testres, &res,
		    (xdrproc_t)xdr_void, &dummy, timeo);

		if (debug_level > 2)
			syslog(LOG_DEBUG, "clnt_call returns %d", success);
	}
	return (NULL);
}

/* nlm_lock ---------------------------------------------------------------- */
/*
 * Purposes:	Establish a lock
 * Returns:	granted, denied or blocked
 * Notes:	*** grace period support missing
 */
nlm4_res *
nlm4_lock_4_svc(nlm4_lockargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_lock", rqstp);
	if (debug_level > 5) {
		syslog(LOG_DEBUG, "Locking arguments:\n");
		log_netobj(&(arg->cookie));
		syslog(LOG_DEBUG, "Alock arguments:\n");
		syslog(LOG_DEBUG, "Caller Name: %s\n",arg->alock.caller_name);
		syslog(LOG_DEBUG, "File Handle:\n");
		log_netobj(&(arg->alock.fh));
		syslog(LOG_DEBUG, "Owner Handle:\n");
		log_netobj(&(arg->alock.oh));
		syslog(LOG_DEBUG, "SVID:        %d\n", arg->alock.svid);
		syslog(LOG_DEBUG, "Lock Offset: %llu\n",
		    (unsigned long long)arg->alock.l_offset);
		syslog(LOG_DEBUG, "Lock Length: %llu\n", 
		    (unsigned long long)arg->alock.l_len);
		syslog(LOG_DEBUG, "Block:       %s\n", (arg->block ? "true" : "false"));
		syslog(LOG_DEBUG, "Exclusive:   %s\n", (arg->exclusive ? "true" : "false"));
		syslog(LOG_DEBUG, "Reclaim:     %s\n", (arg->reclaim ? "true" : "false"));
		syslog(LOG_DEBUG, "State num:   %d\n", arg->state);
	}

	/* copy cookie from arg to result.  See comment in nlm_test_4() */
	res.cookie = arg->cookie;

	res.stat.stat = getlock(arg, rqstp, LOCK_MON | LOCK_V4);
	return (&res);
}

void *
nlm4_lock_msg_4_svc(nlm4_lockargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_lock_msg", rqstp);

	res.cookie = arg->cookie;
	res.stat.stat = getlock(arg, rqstp, LOCK_MON | LOCK_ASYNC | LOCK_V4);
	transmit4_result(NLM4_LOCK_RES, &res, getrpcaddr(rqstp));

	return (NULL);
}

/* nlm_cancel -------------------------------------------------------------- */
/*
 * Purpose:	Cancel a blocked lock request
 * Returns:	granted or denied
 * Notes:
 */
nlm4_res *
nlm4_cancel_4_svc(nlm4_cancargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_cancel", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	res.cookie = arg->cookie;

	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	res.stat.stat = unlock(&arg->alock, LOCK_CANCEL);
	return (&res);
}

void *
nlm4_cancel_msg_4_svc(nlm4_cancargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_cancel_msg", rqstp);

	res.cookie = arg->cookie;
	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	res.stat.stat = unlock(&arg->alock, LOCK_CANCEL | LOCK_V4);
	transmit4_result(NLM4_CANCEL_RES, &res, getrpcaddr(rqstp));
	return (NULL);
}

/* nlm_unlock -------------------------------------------------------------- */
/*
 * Purpose:	Release an existing lock
 * Returns:	Always granted, unless during grace period
 * Notes:	"no such lock" error condition is ignored, as the
 *		protocol uses unreliable UDP datagrams, and may well
 *		re-try an unlock that has already succeeded.
 */
nlm4_res *
nlm4_unlock_4_svc(nlm4_unlockargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_unlock", rqstp);

	res.stat.stat = unlock(&arg->alock, LOCK_V4);
	res.cookie = arg->cookie;

	return (&res);
}

void *
nlm4_unlock_msg_4_svc(nlm4_unlockargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_unlock_msg", rqstp);

	res.stat.stat = unlock(&arg->alock, LOCK_V4);
	res.cookie = arg->cookie;

	transmit4_result(NLM4_UNLOCK_RES, &res, getrpcaddr(rqstp));
	return (NULL);
}

/* ------------------------------------------------------------------------- */
/*
 * Client-side pseudo-RPCs for results.  Note that for the client there
 * are only nlm_xxx_msg() versions of each call, since the 'real RPC'
 * version returns the results in the RPC result, and so the client
 * does not normally receive incoming RPCs.
 *
 * The exception to this is nlm_granted(), which is genuinely an RPC
 * call from the server to the client - a 'call-back' in normal procedure
 * call terms.
 */

/* nlm_granted ------------------------------------------------------------- */
/*
 * Purpose:	Receive notification that formerly blocked lock now granted
 * Returns:	always success ('granted')
 * Notes:
 */
nlm4_res *
nlm4_granted_4_svc(nlm4_testargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_granted", rqstp);

	res.stat.stat = lock_answer(arg->alock.svid, &arg->cookie,
		nlm4_granted, NULL, NLM_VERS4) == 0 ?
		nlm4_granted : nlm4_denied;

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	res.cookie = arg->cookie;

	return (&res);
}

void *
nlm4_granted_msg_4_svc(nlm4_testargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_granted_msg", rqstp);

	res.cookie = arg->cookie;
	res.stat.stat = lock_answer(arg->alock.svid, &arg->cookie,
		nlm4_granted, NULL, NLM_VERS4) == 0 ?
		nlm4_granted : nlm4_denied;
	transmit4_result(NLM4_GRANTED_RES, &res, getrpcaddr(rqstp));
	return (NULL);
}

/* nlm_test_res ------------------------------------------------------------ */
/*
 * Purpose:	Accept result from earlier nlm_test_msg() call
 * Returns:	Nothing
 */
void *
nlm4_test_res_4_svc(nlm4_testres *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_test_res", rqstp);

	(void)lock_answer(-1, &arg->cookie, arg->stat.stat,
		(int *)&arg->stat.nlm4_testrply_u.holder.svid,
		NLM_VERS4);
	return (NULL);
}

/* nlm_lock_res ------------------------------------------------------------ */
/*
 * Purpose:	Accept result from earlier nlm_lock_msg() call
 * Returns:	Nothing
 */
void *
nlm4_lock_res_4_svc(nlm4_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_lock_res", rqstp);

	(void)lock_answer(-1, &arg->cookie, arg->stat.stat, NULL, NLM_VERS4);

	return (NULL);
}

/* nlm_cancel_res ---------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_cancel_msg() call
 * Returns:	Nothing
 */
void *
nlm4_cancel_res_4_svc(nlm4_res *arg __unused, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_cancel_res", rqstp);
	return (NULL);
}

/* nlm_unlock_res ---------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_unlock_msg() call
 * Returns:	Nothing
 */
void *
nlm4_unlock_res_4_svc(nlm4_res *arg __unused, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_unlock_res", rqstp);
	return (NULL);
}

/* nlm_granted_res --------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_granted_msg() call
 * Returns:	Nothing
 */
void *
nlm4_granted_res_4_svc(nlm4_res *arg __unused, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_granted_res", rqstp);
	return (NULL);
}

/* ------------------------------------------------------------------------- */
/*
 * Calls for PCNFS locking (aka non-monitored locking, no involvement
 * of rpc.statd).
 *
 * These are all genuine RPCs - no nlm_xxx_msg() nonsense here.
 */

/* nlm_share --------------------------------------------------------------- */
/*
 * Purpose:	Establish a DOS-style lock
 * Returns:	success or failure
 * Notes:	Blocking locks are not supported - client is expected
 *		to retry if required.
 */
nlm4_shareres *
nlm4_share_4_svc(nlm4_shareargs *arg, struct svc_req *rqstp)
{
	static nlm4_shareres res;

	if (debug_level)
		log_from_addr("nlm4_share", rqstp);

	res.cookie = arg->cookie;
	res.stat = nlm4_granted;
	res.sequence = 1234356;	/* X/Open says this field is ignored? */
	return (&res);
}

/* nlm4_unshare ------------------------------------------------------------ */
/*
 * Purpose:	Release a DOS-style lock
 * Returns:	nlm_granted, unless in grace period
 * Notes:
 */
nlm4_shareres *
nlm4_unshare_4_svc(nlm4_shareargs *arg, struct svc_req *rqstp)
{
	static nlm4_shareres res;

	if (debug_level)
		log_from_addr("nlm_unshare", rqstp);

	res.cookie = arg->cookie;
	res.stat = nlm4_granted;
	res.sequence = 1234356;	/* X/Open says this field is ignored? */
	return (&res);
}

/* nlm4_nm_lock ------------------------------------------------------------ */
/*
 * Purpose:	non-monitored version of nlm4_lock()
 * Returns:	as for nlm4_lock()
 * Notes:	These locks are in the same style as the standard nlm4_lock,
 *		but the rpc.statd should not be called to establish a
 *		monitor for the client machine, since that machine is
 *		declared not to be running a rpc.statd, and so would not
 *		respond to the statd protocol.
 */
nlm4_res *
nlm4_nm_lock_4_svc(nlm4_lockargs *arg, struct svc_req *rqstp)
{
	static nlm4_res res;

	if (debug_level)
		log_from_addr("nlm4_nm_lock", rqstp);

	/* copy cookie from arg to result.  See comment in nlm4_test_1() */
	res.cookie = arg->cookie;
	res.stat.stat = nlm4_granted;
	return (&res);
}

/* nlm4_free_all ------------------------------------------------------------ */
/*
 * Purpose:	Release all locks held by a named client
 * Returns:	Nothing
 * Notes:	Potential denial of service security problem here - the
 *		locks to be released are specified by a host name, independent
 *		of the address from which the request has arrived.
 *		Should probably be rejected if the named host has been
 *		using monitored locks.
 */
void *
nlm4_free_all_4_svc(struct nlm4_notify *arg __unused, struct svc_req *rqstp)
{
	static char dummy;

	if (debug_level)
		log_from_addr("nlm4_free_all", rqstp);
	return (&dummy);
}

/* nlm_sm_notify --------------------------------------------------------- */
/*
 * Purpose:	called by rpc.statd when a monitored host state changes.
 * Returns:	Nothing
 */
void *
nlm_sm_notify_0_svc(struct nlm_sm_status *arg, struct svc_req *rqstp __unused)
{
	static char dummy;
	notify(arg->mon_name, arg->state);
	return (&dummy);
}
