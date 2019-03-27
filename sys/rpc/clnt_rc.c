/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#include <rpc/krpc.h>

static enum clnt_stat clnt_reconnect_call(CLIENT *, struct rpc_callextra *,
    rpcproc_t, struct mbuf *, struct mbuf **, struct timeval);
static void clnt_reconnect_geterr(CLIENT *, struct rpc_err *);
static bool_t clnt_reconnect_freeres(CLIENT *, xdrproc_t, void *);
static void clnt_reconnect_abort(CLIENT *);
static bool_t clnt_reconnect_control(CLIENT *, u_int, void *);
static void clnt_reconnect_close(CLIENT *);
static void clnt_reconnect_destroy(CLIENT *);

static struct clnt_ops clnt_reconnect_ops = {
	.cl_call =	clnt_reconnect_call,
	.cl_abort =	clnt_reconnect_abort,
	.cl_geterr =	clnt_reconnect_geterr,
	.cl_freeres =	clnt_reconnect_freeres,
	.cl_close =	clnt_reconnect_close,
	.cl_destroy =	clnt_reconnect_destroy,
	.cl_control =	clnt_reconnect_control
};

static int	fake_wchan;

CLIENT *
clnt_reconnect_create(
	struct netconfig *nconf,	/* network type */
	struct sockaddr *svcaddr,	/* servers address */
	rpcprog_t program,		/* program number */
	rpcvers_t version,		/* version number */
	size_t sendsz,			/* buffer recv size */
	size_t recvsz)			/* buffer send size */
{
	CLIENT *cl = NULL;		/* client handle */
	struct rc_data *rc = NULL;	/* private data */

	if (svcaddr == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNADDR;
		return (NULL);
	}

	cl = mem_alloc(sizeof (CLIENT));
	rc = mem_alloc(sizeof (*rc));
	mtx_init(&rc->rc_lock, "rc->rc_lock", NULL, MTX_DEF);
	(void) memcpy(&rc->rc_addr, svcaddr, (size_t)svcaddr->sa_len);
	rc->rc_nconf = nconf;
	rc->rc_prog = program;
	rc->rc_vers = version;
	rc->rc_sendsz = sendsz;
	rc->rc_recvsz = recvsz;
	rc->rc_timeout.tv_sec = -1;
	rc->rc_timeout.tv_usec = -1;
	rc->rc_retry.tv_sec = 3;
	rc->rc_retry.tv_usec = 0;
	rc->rc_retries = INT_MAX;
	rc->rc_privport = FALSE;
	rc->rc_waitchan = "rpcrecv";
	rc->rc_intr = 0;
	rc->rc_connecting = FALSE;
	rc->rc_closed = FALSE;
	rc->rc_ucred = crdup(curthread->td_ucred);
	rc->rc_client = NULL;

	cl->cl_refs = 1;
	cl->cl_ops = &clnt_reconnect_ops;
	cl->cl_private = (caddr_t)(void *)rc;
	cl->cl_auth = authnone_create();
	cl->cl_tp = NULL;
	cl->cl_netid = NULL;
	return (cl);
}

static enum clnt_stat
clnt_reconnect_connect(CLIENT *cl)
{
	struct thread *td = curthread;
	struct rc_data *rc = (struct rc_data *)cl->cl_private;
	struct socket *so;
	enum clnt_stat stat;
	int error;
	int one = 1;
	struct ucred *oldcred;
	CLIENT *newclient = NULL;

	mtx_lock(&rc->rc_lock);
	while (rc->rc_connecting) {
		error = msleep(rc, &rc->rc_lock,
		    rc->rc_intr ? PCATCH : 0, "rpcrecon", 0);
		if (error) {
			mtx_unlock(&rc->rc_lock);
			return (RPC_INTR);
		}
	}
	if (rc->rc_closed) {
		mtx_unlock(&rc->rc_lock);
		return (RPC_CANTSEND);
	}
	if (rc->rc_client) {
		mtx_unlock(&rc->rc_lock);
		return (RPC_SUCCESS);
	}

	/*
	 * My turn to attempt a connect. The rc_connecting variable
	 * serializes the following code sequence, so it is guaranteed
	 * that rc_client will still be NULL after it is re-locked below,
	 * since that is the only place it is set non-NULL.
	 */
	rc->rc_connecting = TRUE;
	mtx_unlock(&rc->rc_lock);

	oldcred = td->td_ucred;
	td->td_ucred = rc->rc_ucred;
	so = __rpc_nconf2socket(rc->rc_nconf);
	if (!so) {
		stat = rpc_createerr.cf_stat = RPC_TLIERROR;
		rpc_createerr.cf_error.re_errno = 0;
		td->td_ucred = oldcred;
		goto out;
	}

	if (rc->rc_privport)
		bindresvport(so, NULL);

	if (rc->rc_nconf->nc_semantics == NC_TPI_CLTS)
		newclient = clnt_dg_create(so,
		    (struct sockaddr *) &rc->rc_addr, rc->rc_prog, rc->rc_vers,
		    rc->rc_sendsz, rc->rc_recvsz);
	else {
		/*
		 * I do not believe a timeout of less than 1sec would make
		 * sense here since short delays can occur when a server is
		 * temporarily overloaded.
		 */
		if (rc->rc_timeout.tv_sec > 0 && rc->rc_timeout.tv_usec >= 0) {
			error = so_setsockopt(so, SOL_SOCKET, SO_SNDTIMEO,
			    &rc->rc_timeout, sizeof(struct timeval));
			if (error != 0) {
				stat = rpc_createerr.cf_stat = RPC_CANTSEND;
				rpc_createerr.cf_error.re_errno = error;
				td->td_ucred = oldcred;
				goto out;
			}
		}
		newclient = clnt_vc_create(so,
		    (struct sockaddr *) &rc->rc_addr, rc->rc_prog, rc->rc_vers,
		    rc->rc_sendsz, rc->rc_recvsz, rc->rc_intr);
	}
	td->td_ucred = oldcred;

	if (!newclient) {
		soclose(so);
		rc->rc_err = rpc_createerr.cf_error;
		stat = rpc_createerr.cf_stat;
		goto out;
	}

	CLNT_CONTROL(newclient, CLSET_FD_CLOSE, 0);
	CLNT_CONTROL(newclient, CLSET_CONNECT, &one);
	CLNT_CONTROL(newclient, CLSET_TIMEOUT, &rc->rc_timeout);
	CLNT_CONTROL(newclient, CLSET_RETRY_TIMEOUT, &rc->rc_retry);
	CLNT_CONTROL(newclient, CLSET_WAITCHAN, rc->rc_waitchan);
	CLNT_CONTROL(newclient, CLSET_INTERRUPTIBLE, &rc->rc_intr);
	if (rc->rc_backchannel != NULL)
		CLNT_CONTROL(newclient, CLSET_BACKCHANNEL, rc->rc_backchannel);
	stat = RPC_SUCCESS;

out:
	mtx_lock(&rc->rc_lock);
	KASSERT(rc->rc_client == NULL, ("rc_client not null"));
	if (!rc->rc_closed) {
		rc->rc_client = newclient;
		newclient = NULL;
	}
	rc->rc_connecting = FALSE;
	wakeup(rc);
	mtx_unlock(&rc->rc_lock);

	if (newclient) {
		/*
		 * It has been closed, so discard the new client.
		 * nb: clnt_[dg|vc]_close()/clnt_[dg|vc]_destroy() cannot
		 * be called with the rc_lock mutex held, since they may
		 * msleep() while holding a different mutex.
		 */
		CLNT_CLOSE(newclient);
		CLNT_RELEASE(newclient);
	}

	return (stat);
}

static enum clnt_stat
clnt_reconnect_call(
	CLIENT		*cl,		/* client handle */
	struct rpc_callextra *ext,	/* call metadata */
	rpcproc_t	proc,		/* procedure number */
	struct mbuf	*args,		/* pointer to args */
	struct mbuf	**resultsp,	/* pointer to results */
	struct timeval	utimeout)
{
	struct rc_data *rc = (struct rc_data *)cl->cl_private;
	CLIENT *client;
	enum clnt_stat stat;
	int tries, error;

	tries = 0;
	do {
		mtx_lock(&rc->rc_lock);
		if (rc->rc_closed) {
			mtx_unlock(&rc->rc_lock);
			return (RPC_CANTSEND);
		}

		if (!rc->rc_client) {
			mtx_unlock(&rc->rc_lock);
			stat = clnt_reconnect_connect(cl);
			if (stat == RPC_SYSTEMERROR) {
				error = tsleep(&fake_wchan,
				    rc->rc_intr ? PCATCH : 0, "rpccon", hz);
				if (error == EINTR || error == ERESTART)
					return (RPC_INTR);
				tries++;
				if (tries >= rc->rc_retries)
					return (stat);
				continue;
			}
			if (stat != RPC_SUCCESS)
				return (stat);
			mtx_lock(&rc->rc_lock);
		}

		if (!rc->rc_client) {
			mtx_unlock(&rc->rc_lock);
			stat = RPC_FAILED;
			continue;
		}
		CLNT_ACQUIRE(rc->rc_client);
		client = rc->rc_client;
		mtx_unlock(&rc->rc_lock);
		stat = CLNT_CALL_MBUF(client, ext, proc, args,
		    resultsp, utimeout);

		if (stat != RPC_SUCCESS) {
			if (!ext)
				CLNT_GETERR(client, &rc->rc_err);
		}

		if (stat == RPC_TIMEDOUT) {
			/*
			 * Check for async send misfeature for NLM
			 * protocol.
			 */
			if ((rc->rc_timeout.tv_sec == 0
				&& rc->rc_timeout.tv_usec == 0)
			    || (rc->rc_timeout.tv_sec == -1
				&& utimeout.tv_sec == 0
				&& utimeout.tv_usec == 0)) {
				CLNT_RELEASE(client);
				break;
			}
		}

		if (stat == RPC_TIMEDOUT || stat == RPC_CANTSEND
		    || stat == RPC_CANTRECV) {
			tries++;
			if (tries >= rc->rc_retries) {
				CLNT_RELEASE(client);
				break;
			}

			if (ext && ext->rc_feedback)
				ext->rc_feedback(FEEDBACK_RECONNECT, proc,
				    ext->rc_feedback_arg);

			mtx_lock(&rc->rc_lock);
			/*
			 * Make sure that someone else hasn't already
			 * reconnected by checking if rc_client has changed.
			 * If not, we are done with the client and must
			 * do CLNT_RELEASE(client) twice to dispose of it,
			 * because there is both an initial refcnt and one
			 * acquired by CLNT_ACQUIRE() above.
			 */
			if (rc->rc_client == client) {
				rc->rc_client = NULL;
				mtx_unlock(&rc->rc_lock);
				CLNT_RELEASE(client);
			} else {
				mtx_unlock(&rc->rc_lock);
			}
			CLNT_RELEASE(client);
		} else {
			CLNT_RELEASE(client);
			break;
		}
	} while (stat != RPC_SUCCESS);

	KASSERT(stat != RPC_SUCCESS || *resultsp,
	    ("RPC_SUCCESS without reply"));

	return (stat);
}

static void
clnt_reconnect_geterr(CLIENT *cl, struct rpc_err *errp)
{
	struct rc_data *rc = (struct rc_data *)cl->cl_private;

	*errp = rc->rc_err;
}

/*
 * Since this function requires that rc_client be valid, it can
 * only be called when that is guaranteed to be the case.
 */
static bool_t
clnt_reconnect_freeres(CLIENT *cl, xdrproc_t xdr_res, void *res_ptr)
{
	struct rc_data *rc = (struct rc_data *)cl->cl_private;

	return (CLNT_FREERES(rc->rc_client, xdr_res, res_ptr));
}

/*ARGSUSED*/
static void
clnt_reconnect_abort(CLIENT *h)
{
}

/*
 * CLNT_CONTROL() on the client returned by clnt_reconnect_create() must
 * always be called before CLNT_CALL_MBUF() by a single thread only.
 */
static bool_t
clnt_reconnect_control(CLIENT *cl, u_int request, void *info)
{
	struct rc_data *rc = (struct rc_data *)cl->cl_private;
	SVCXPRT *xprt;

	if (info == NULL) {
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		rc->rc_timeout = *(struct timeval *)info;
		if (rc->rc_client)
			CLNT_CONTROL(rc->rc_client, request, info);
		break;

	case CLGET_TIMEOUT:
		*(struct timeval *)info = rc->rc_timeout;
		break;

	case CLSET_RETRY_TIMEOUT:
		rc->rc_retry = *(struct timeval *)info;
		if (rc->rc_client)
			CLNT_CONTROL(rc->rc_client, request, info);
		break;

	case CLGET_RETRY_TIMEOUT:
		*(struct timeval *)info = rc->rc_retry;
		break;

	case CLGET_VERS:
		*(uint32_t *)info = rc->rc_vers;
		break;

	case CLSET_VERS:
		rc->rc_vers = *(uint32_t *) info;
		if (rc->rc_client)
			CLNT_CONTROL(rc->rc_client, CLSET_VERS, info);
		break;

	case CLGET_PROG:
		*(uint32_t *)info = rc->rc_prog;
		break;

	case CLSET_PROG:
		rc->rc_prog = *(uint32_t *) info;
		if (rc->rc_client)
			CLNT_CONTROL(rc->rc_client, request, info);
		break;

	case CLSET_WAITCHAN:
		rc->rc_waitchan = (char *)info;
		if (rc->rc_client)
			CLNT_CONTROL(rc->rc_client, request, info);
		break;

	case CLGET_WAITCHAN:
		*(const char **) info = rc->rc_waitchan;
		break;

	case CLSET_INTERRUPTIBLE:
		rc->rc_intr = *(int *) info;
		if (rc->rc_client)
			CLNT_CONTROL(rc->rc_client, request, info);
		break;

	case CLGET_INTERRUPTIBLE:
		*(int *) info = rc->rc_intr;
		break;

	case CLSET_RETRIES:
		rc->rc_retries = *(int *) info;
		break;

	case CLGET_RETRIES:
		*(int *) info = rc->rc_retries;
		break;

	case CLSET_PRIVPORT:
		rc->rc_privport = *(int *) info;
		break;

	case CLGET_PRIVPORT:
		*(int *) info = rc->rc_privport;
		break;

	case CLSET_BACKCHANNEL:
		xprt = (SVCXPRT *)info;
		xprt_register(xprt);
		rc->rc_backchannel = info;
		break;

	default:
		return (FALSE);
	}

	return (TRUE);
}

static void
clnt_reconnect_close(CLIENT *cl)
{
	struct rc_data *rc = (struct rc_data *)cl->cl_private;
	CLIENT *client;

	mtx_lock(&rc->rc_lock);

	if (rc->rc_closed) {
		mtx_unlock(&rc->rc_lock);
		return;
	}

	rc->rc_closed = TRUE;
	client = rc->rc_client;
	rc->rc_client = NULL;

	mtx_unlock(&rc->rc_lock);

	if (client) {
		CLNT_CLOSE(client);
		CLNT_RELEASE(client);
	}
}

static void
clnt_reconnect_destroy(CLIENT *cl)
{
	struct rc_data *rc = (struct rc_data *)cl->cl_private;
	SVCXPRT *xprt;

	if (rc->rc_client)
		CLNT_DESTROY(rc->rc_client);
	if (rc->rc_backchannel) {
		xprt = (SVCXPRT *)rc->rc_backchannel;
		xprt_unregister(xprt);
		SVC_RELEASE(xprt);
	}
	crfree(rc->rc_ucred);
	mtx_destroy(&rc->rc_lock);
	mem_free(rc, sizeof(*rc));
	mem_free(cl, sizeof (CLIENT));
}
