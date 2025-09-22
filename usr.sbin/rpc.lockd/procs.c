/*	$OpenBSD: procs.c,v 1.16 2023/03/08 04:43:15 guenther Exp $	*/

/*
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

#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpcsvc/sm_inter.h>
#include "nlm_prot.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <limits.h>

#include "lockd.h"
#include "lockd_lock.h"

#define	CLIENT_CACHE_SIZE	64	/* No. of client sockets cached	 */
#define	CLIENT_CACHE_LIFETIME	120	/* In seconds			 */

/* log_from_addr ----------------------------------------------------------- */
/*
 * Purpose:	Log name of function called and source address
 * Returns:	Nothing
 * Notes:	Extracts the source address from the transport handle
 *		passed in as part of the called procedure specification
 */
static void 
log_from_addr(char *fun_name, struct svc_req *req)
{
	struct	sockaddr_in *addr;
	struct	hostent *host;
	char	hostname_buf[HOST_NAME_MAX+1];

	addr = svc_getcaller(req->rq_xprt);
	host = gethostbyaddr((char *) &(addr->sin_addr), addr->sin_len, AF_INET);
	if (host)
		strlcpy(hostname_buf, host->h_name, sizeof(hostname_buf));
	else
		strlcpy(hostname_buf, inet_ntoa(addr->sin_addr),
		    sizeof hostname_buf);
	syslog(LOG_DEBUG, "%s from %s", fun_name, hostname_buf);
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
static struct in_addr clnt_cache_addr[CLIENT_CACHE_SIZE];
static int clnt_cache_next_to_use = 0;

CLIENT *
get_client(struct sockaddr_in *host_addr, u_long vers)
{
	CLIENT *client;
	int     sock_no, i;
	struct timeval retry_time, time_now;

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
		if (client && !memcmp(&clnt_cache_addr[i], &host_addr->sin_addr,
			sizeof(struct in_addr))) {
			/* Found it! */
			if (debug_level > 3)
				syslog(LOG_DEBUG, "Found CLIENT* in cache");
			return client;
		}
	}

	/* Not found in cache.  Free the next entry if it is in use. */
	if (clnt_cache_ptr[clnt_cache_next_to_use]) {
		clnt_destroy(clnt_cache_ptr[clnt_cache_next_to_use]);
		clnt_cache_ptr[clnt_cache_next_to_use] = NULL;
	}

	sock_no = RPC_ANYSOCK;
	retry_time.tv_sec = 5;
	retry_time.tv_usec = 0;
	host_addr->sin_port = 0;
	client = clntudp_create(host_addr, NLM_PROG, vers, retry_time, &sock_no);
	if (!client) {
		syslog(LOG_ERR, "%s", clnt_spcreateerror("clntudp_create"));
		syslog(LOG_ERR, "Unable to return result to %s",
		    inet_ntoa(host_addr->sin_addr));
		return NULL;
	}

	/* Success - update the cache entry */
	clnt_cache_ptr[clnt_cache_next_to_use] = client;
	clnt_cache_addr[clnt_cache_next_to_use] = host_addr->sin_addr;
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
	clnt_control(client, CLSET_TIMEOUT, (char *)(void *)&retry_time);

	if (debug_level > 3)
		syslog(LOG_DEBUG, "Created CLIENT* for %s",
		    inet_ntoa(host_addr->sin_addr));
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
transmit_result(int opcode, nlm_res *result, struct sockaddr_in *addr)
{
	static char dummy;
	CLIENT *cli;
	struct timeval timeo;
	int success;

	if ((cli = get_client(addr, NLM_VERS)) != NULL) {
		/* No timeout - not expecting response */
		timerclear(&timeo);

		success = clnt_call(cli, opcode, xdr_nlm_res,
		    result, xdr_void, &dummy, timeo);

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
transmit4_result(int opcode, nlm4_res *result, struct sockaddr_in *addr)
{
	static char dummy;
	CLIENT *cli;
	struct timeval timeo;
	int success;

	if ((cli = get_client(addr, NLM_VERS4)) != NULL) {
		/* No timeout - not expecting response */
		timerclear(&timeo);

		success = clnt_call(cli, opcode, xdr_nlm4_res,
		    result, xdr_void, &dummy, timeo);

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
	memcpy(arg4, arg, sizeof(nlm_lock));
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
	static nlm_testres result;
	struct nlm4_lock arg4;
	struct nlm4_holder *holder;
	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_test", rqstp);

	holder = testlock(&arg4, 0);
	/*
	 * Copy the cookie from the argument into the result.  Note that this
	 * is slightly hazardous, as the structure contains a pointer to a
	 * malloc()ed buffer that will get freed by the caller.  However, the
	 * main function transmits the result before freeing the argument
	 * so it is in fact safe.
	 */
	result.cookie = arg->cookie;
	if (holder == NULL) {
		result.stat.stat = nlm_granted;
	} else {
		result.stat.stat = nlm_denied;
		memcpy(&result.stat.nlm_testrply_u.holder, holder,
		    sizeof(struct nlm_holder));
		result.stat.nlm_testrply_u.holder.l_offset =
		    (unsigned int)holder->l_offset;
		result.stat.nlm_testrply_u.holder.l_len =
		    (unsigned int)holder->l_len;
	}
	return &result;
}

void *
nlm_test_msg_1_svc(nlm_testargs *arg, struct svc_req *rqstp)
{
	nlm_testres result;
	static char dummy;
	struct sockaddr_in *addr;
	CLIENT *cli;
	int success;
	struct timeval timeo;
	struct nlm4_lock arg4;
	struct nlm4_holder *holder;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_test_msg", rqstp);

	holder = testlock(&arg4, 0);

	result.cookie = arg->cookie;
	if (holder == NULL) {
		result.stat.stat = nlm_granted;
	} else {
		result.stat.stat = nlm_denied;
		memcpy(&result.stat.nlm_testrply_u.holder, holder,
		    sizeof(struct nlm_holder));
		result.stat.nlm_testrply_u.holder.l_offset =
		    (unsigned int)holder->l_offset;
		result.stat.nlm_testrply_u.holder.l_len =
		    (unsigned int)holder->l_len;
	}

	/*
	 * nlm_test has different result type to the other operations, so
	 * can't use transmit_result() in this case
	 */
	addr = svc_getcaller(rqstp->rq_xprt);
	if ((cli = get_client(addr, NLM_VERS)) != NULL) {
		/* No timeout - not expecting response */
		timerclear(&timeo);

		success = clnt_call(cli, NLM_TEST_RES, xdr_nlm_testres,
		    &result, xdr_void, &dummy, timeo);

		if (debug_level > 2)
			syslog(LOG_DEBUG, "clnt_call returns %d", success);
	}
	return NULL;
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
	static nlm_res result;
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
	result.cookie = arg->cookie;

	result.stat.stat = getlock(&arg4, rqstp, LOCK_MON);
	return &result;
}

void *
nlm_lock_msg_1_svc(nlm_lockargs *arg, struct svc_req *rqstp)
{
	static nlm_res result;
	struct nlm4_lockargs arg4;

	nlmtonlm4(&arg->alock, &arg4.alock);
	arg4.cookie = arg->cookie;
	arg4.block = arg->block;
	arg4.exclusive = arg->exclusive;
	arg4.reclaim = arg->reclaim;
	arg4.state = arg->state;

	if (debug_level)
		log_from_addr("nlm_lock_msg", rqstp);

	result.cookie = arg->cookie;
	result.stat.stat = getlock(&arg4, rqstp, LOCK_ASYNC | LOCK_MON);
	transmit_result(NLM_LOCK_RES, &result, svc_getcaller(rqstp->rq_xprt));

	return NULL;
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
	static nlm_res result;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_cancel", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	result.cookie = arg->cookie;

	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	result.stat.stat = unlock(&arg4, LOCK_CANCEL);
	return &result;
}

void *
nlm_cancel_msg_1_svc(nlm_cancargs *arg, struct svc_req *rqstp)
{
	static nlm_res result;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_cancel_msg", rqstp);

	result.cookie = arg->cookie;
	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	result.stat.stat = unlock(&arg4, LOCK_CANCEL);
	transmit_result(NLM_CANCEL_RES, &result, svc_getcaller(rqstp->rq_xprt));
	return NULL;
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
	static nlm_res result;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_unlock", rqstp);

	result.stat.stat = unlock(&arg4, 0);
	result.cookie = arg->cookie;

	return &result;
}

void *
nlm_unlock_msg_1_svc(nlm_unlockargs *arg, struct svc_req *rqstp)
{
	static nlm_res result;
	struct nlm4_lock arg4;

	nlmtonlm4(&arg->alock, &arg4);

	if (debug_level)
		log_from_addr("nlm_unlock_msg", rqstp);

	result.stat.stat = unlock(&arg4, 0);
	result.cookie = arg->cookie;

	transmit_result(NLM_UNLOCK_RES, &result, svc_getcaller(rqstp->rq_xprt));
	return NULL;
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
	static nlm_res result;

	if (debug_level)
		log_from_addr("nlm_granted", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	result.cookie = arg->cookie;

	result.stat.stat = nlm_granted;
	return &result;
}

void *
nlm_granted_msg_1_svc(nlm_testargs *arg, struct svc_req *rqstp)
{
	static nlm_res result;

	if (debug_level)
		log_from_addr("nlm_granted_msg", rqstp);

	result.cookie = arg->cookie;
	result.stat.stat = nlm_granted;
	transmit_result(NLM_GRANTED_RES, &result, svc_getcaller(rqstp->rq_xprt));
	return NULL;
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
	return NULL;
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

	return NULL;
}

/* nlm_cancel_res ---------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_cancel_msg() call
 * Returns:	Nothing
 */
void *
nlm_cancel_res_1_svc(nlm_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm_cancel_res", rqstp);
	return NULL;
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
	return NULL;
}

/* nlm_granted_res --------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_granted_msg() call
 * Returns:	Nothing
 */
void *
nlm_granted_res_1_svc(nlm_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm_granted_res", rqstp);
	return NULL;
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
	static nlm_shareres result;

	if (debug_level)
		log_from_addr("nlm_share", rqstp);

	result.cookie = arg->cookie;
	result.stat = nlm_granted;
	result.sequence = 1234356;	/* X/Open says this field is ignored? */
	return &result;
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
	static nlm_shareres result;

	if (debug_level)
		log_from_addr("nlm_unshare", rqstp);

	result.cookie = arg->cookie;
	result.stat = nlm_granted;
	result.sequence = 1234356;	/* X/Open says this field is ignored? */
	return &result;
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
	static nlm_res result;

	if (debug_level)
		log_from_addr("nlm_nm_lock", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	result.cookie = arg->cookie;
	result.stat.stat = nlm_granted;
	return &result;
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
nlm_free_all_3_svc(nlm_notify *arg, struct svc_req *rqstp)
{
	static char dummy;

	if (debug_level)
		log_from_addr("nlm_free_all", rqstp);
	return &dummy;
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
	static nlm4_testres result;
	struct nlm4_holder *holder;

	if (debug_level)
		log_from_addr("nlm4_test", rqstp);

	holder = testlock(&arg->alock, LOCK_V4);

	/*
	 * Copy the cookie from the argument into the result.  Note that this
	 * is slightly hazardous, as the structure contains a pointer to a
	 * malloc()ed buffer that will get freed by the caller.  However, the
	 * main function transmits the result before freeing the argument
	 * so it is in fact safe.
	 */
	result.cookie = arg->cookie;
	if (holder == NULL) {
		result.stat.stat = nlm4_granted;
	} else {
		result.stat.stat = nlm4_denied;
		memcpy(&result.stat.nlm4_testrply_u.holder, holder,
		    sizeof(struct nlm4_holder));
	}
	return &result;
}

void *
nlm4_test_msg_4_svc(nlm4_testargs *arg, struct svc_req *rqstp)
{
	nlm4_testres result;
	static char dummy;
	struct sockaddr_in *addr;
	CLIENT *cli;
	int success;
	struct timeval timeo;
	struct nlm4_holder *holder;

	if (debug_level)
		log_from_addr("nlm4_test_msg", rqstp);

	holder = testlock(&arg->alock, LOCK_V4);

	result.cookie = arg->cookie;
	if (holder == NULL) {
		result.stat.stat = nlm4_granted;
	} else {
		result.stat.stat = nlm4_denied;
		memcpy(&result.stat.nlm4_testrply_u.holder, holder,
		    sizeof(struct nlm4_holder));
	}

	/*
	 * nlm_test has different result type to the other operations, so
	 * can't use transmit4_result() in this case
	 */
	addr = svc_getcaller(rqstp->rq_xprt);
	if ((cli = get_client(addr, NLM_VERS4)) != NULL) {
		/* No timeout - not expecting response */
		timerclear(&timeo);

		success = clnt_call(cli, NLM4_TEST_RES, xdr_nlm4_testres,
		    &result, xdr_void, &dummy, timeo);

		if (debug_level > 2)
			syslog(LOG_DEBUG, "clnt_call returns %d", success);
	}
	return NULL;
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
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_lock", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_4() */
	result.cookie = arg->cookie;

	result.stat.stat = getlock(arg, rqstp, LOCK_MON | LOCK_V4);
	return &result;
}

void *
nlm4_lock_msg_4_svc(nlm4_lockargs *arg, struct svc_req *rqstp)
{
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_lock_msg", rqstp);

	result.cookie = arg->cookie;
	result.stat.stat = getlock(arg, rqstp, LOCK_MON | LOCK_ASYNC | LOCK_V4);
	transmit4_result(NLM4_LOCK_RES, &result, svc_getcaller(rqstp->rq_xprt));

	return NULL;
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
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_cancel", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	result.cookie = arg->cookie;

	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	result.stat.stat = unlock(&arg->alock, LOCK_CANCEL);
	return &result;
}

void *
nlm4_cancel_msg_4_svc(nlm4_cancargs *arg, struct svc_req *rqstp)
{
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_cancel_msg", rqstp);

	result.cookie = arg->cookie;
	/*
	 * Since at present we never return 'nlm_blocked', there can never be
	 * a lock to cancel, so this call always fails.
	 */
	result.stat.stat = unlock(&arg->alock, LOCK_CANCEL | LOCK_V4);
	transmit4_result(NLM4_CANCEL_RES, &result, svc_getcaller(rqstp->rq_xprt));

	return NULL;
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
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_unlock", rqstp);

	result.stat.stat = unlock(&arg->alock, LOCK_V4);
	result.cookie = arg->cookie;

	return &result;
}

void *
nlm4_unlock_msg_4_svc(nlm4_unlockargs *arg, struct svc_req *rqstp)
{
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_unlock_msg", rqstp);

	result.stat.stat = unlock(&arg->alock, LOCK_V4);
	result.cookie = arg->cookie;

	transmit4_result(NLM4_UNLOCK_RES, &result, svc_getcaller(rqstp->rq_xprt));
	return NULL;
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
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_granted", rqstp);

	/* copy cookie from arg to result.  See comment in nlm_test_1() */
	result.cookie = arg->cookie;

	result.stat.stat = nlm4_granted;
	return &result;
}

void *
nlm4_granted_msg_4_svc(nlm4_testargs *arg, struct svc_req *rqstp)
{
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_granted_msg", rqstp);

	result.cookie = arg->cookie;
	result.stat.stat = nlm4_granted;
	transmit4_result(NLM4_GRANTED_RES, &result, svc_getcaller(rqstp->rq_xprt));
	return NULL;
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
	return NULL;
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

	return NULL;
}

/* nlm_cancel_res ---------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_cancel_msg() call
 * Returns:	Nothing
 */
void *
nlm4_cancel_res_4_svc(nlm4_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_cancel_res", rqstp);
	return NULL;
}

/* nlm_unlock_res ---------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_unlock_msg() call
 * Returns:	Nothing
 */
void *
nlm4_unlock_res_4_svc(nlm4_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_unlock_res", rqstp);
	return NULL;
}

/* nlm_granted_res --------------------------------------------------------- */
/*
 * Purpose:	Accept result from earlier nlm_granted_msg() call
 * Returns:	Nothing
 */
void *
nlm4_granted_res_4_svc(nlm4_res *arg, struct svc_req *rqstp)
{
	if (debug_level)
		log_from_addr("nlm4_granted_res", rqstp);
	return NULL;
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
	static nlm4_shareres result;

	if (debug_level)
		log_from_addr("nlm4_share", rqstp);

	result.cookie = arg->cookie;
	result.stat = nlm4_granted;
	result.sequence = 1234356;	/* X/Open says this field is ignored? */
	return &result;
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
	static nlm4_shareres result;

	if (debug_level)
		log_from_addr("nlm_unshare", rqstp);

	result.cookie = arg->cookie;
	result.stat = nlm4_granted;
	result.sequence = 1234356;	/* X/Open says this field is ignored? */
	return &result;
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
	static nlm4_res result;

	if (debug_level)
		log_from_addr("nlm4_nm_lock", rqstp);

	/* copy cookie from arg to result.  See comment in nlm4_test_1() */
	result.cookie = arg->cookie;
	result.stat.stat = nlm4_granted;
	return &result;
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
nlm4_free_all_4_svc(nlm_notify *arg, struct svc_req *rqstp)
{
	static char dummy;

	if (debug_level)
		log_from_addr("nlm4_free_all", rqstp);
	return &dummy;
}

/* nlm_sm_notify --------------------------------------------------------- */
/*
 * Purpose:	called by rpc.statd when a monitored host state changes.
 * Returns:	Nothing
 */
void *
nlm_sm_notify_0_svc(struct nlm_sm_status *arg, struct svc_req *rqstp)
{
	static char dummy;
	notify(arg->mon_name, arg->state);
	return &dummy;
}
