/*	$NetBSD: mt_misc.c,v 1.1 2000/06/02 23:11:11 fvdl Exp $	*/

/* #pragma ident	"@(#)mt_misc.c	1.24	93/04/29 SMI" */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include "reentrant.h"
#include <rpc/rpc.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "un-namespace.h"
#include "mt_misc.h"

/* Take these objects out of the application namespace. */
#define	svc_lock		__svc_lock
#define	svc_fd_lock		__svc_fd_lock
#define	rpcbaddr_cache_lock	__rpcbaddr_cache_lock
#define	authdes_ops_lock	__authdes_ops_lock
#define	authnone_lock		__authnone_lock
#define	authsvc_lock		__authsvc_lock
#define	clnt_fd_lock		__clnt_fd_lock
#define	clntraw_lock		__clntraw_lock
#define	dupreq_lock		__dupreq_lock
#define	loopnconf_lock		__loopnconf_lock
#define	ops_lock		__ops_lock
#define	proglst_lock		__proglst_lock
#define	rpcsoc_lock		__rpcsoc_lock
#define	svcraw_lock		__svcraw_lock
#define	xprtlist_lock		__xprtlist_lock

/* protects the services list (svc.c) */
pthread_rwlock_t	svc_lock = PTHREAD_RWLOCK_INITIALIZER;

/* protects svc_fdset and the xports[] array */
pthread_rwlock_t	svc_fd_lock = PTHREAD_RWLOCK_INITIALIZER;

/* protects the RPCBIND address cache */
pthread_rwlock_t	rpcbaddr_cache_lock = PTHREAD_RWLOCK_INITIALIZER;

/* serializes authdes ops initializations */
pthread_mutex_t authdes_ops_lock = PTHREAD_MUTEX_INITIALIZER;

/* protects des stats list */
pthread_mutex_t svcauthdesstats_lock = PTHREAD_MUTEX_INITIALIZER;

/* auth_none.c serialization */
pthread_mutex_t	authnone_lock = PTHREAD_MUTEX_INITIALIZER;

/* protects the Auths list (svc_auth.c) */
pthread_mutex_t	authsvc_lock = PTHREAD_MUTEX_INITIALIZER;

/* protects client-side fd lock array */
pthread_mutex_t	clnt_fd_lock = PTHREAD_MUTEX_INITIALIZER;

/* clnt_raw.c serialization */
pthread_mutex_t	clntraw_lock = PTHREAD_MUTEX_INITIALIZER;

/* dupreq variables (svc_dg.c) */
pthread_mutex_t	dupreq_lock = PTHREAD_MUTEX_INITIALIZER;

/* loopnconf (rpcb_clnt.c) */
pthread_mutex_t	loopnconf_lock = PTHREAD_MUTEX_INITIALIZER;

/* serializes ops initializations */
pthread_mutex_t	ops_lock = PTHREAD_MUTEX_INITIALIZER;

/* protects proglst list (svc_simple.c) */
pthread_mutex_t	proglst_lock = PTHREAD_MUTEX_INITIALIZER;

/* serializes clnt_com_create() (rpc_soc.c) */
pthread_mutex_t	rpcsoc_lock = PTHREAD_MUTEX_INITIALIZER;

/* svc_raw.c serialization */
pthread_mutex_t	svcraw_lock = PTHREAD_MUTEX_INITIALIZER;

/* xprtlist (svc_generic.c) */
pthread_mutex_t	xprtlist_lock = PTHREAD_MUTEX_INITIALIZER;

#undef	rpc_createerr

struct rpc_createerr rpc_createerr;
static thread_key_t rce_key;
static once_t rce_once = ONCE_INITIALIZER;
static int rce_key_error;

static void
rce_key_init(void)
{

	rce_key_error = thr_keycreate(&rce_key, free);
}

struct rpc_createerr *
__rpc_createerr(void)
{
	struct rpc_createerr *rce_addr = NULL;

	if (thr_main())
		return (&rpc_createerr);
	if (thr_once(&rce_once, rce_key_init) != 0 || rce_key_error != 0)
		return (&rpc_createerr);
	rce_addr = (struct rpc_createerr *)thr_getspecific(rce_key);
	if (!rce_addr) {
		rce_addr = (struct rpc_createerr *)
			malloc(sizeof (struct rpc_createerr));
		if (thr_setspecific(rce_key, (void *) rce_addr) != 0) {
			free(rce_addr);
			return (&rpc_createerr);
		}
		memset(rce_addr, 0, sizeof (struct rpc_createerr));
		return (rce_addr);
	}
	return (rce_addr);
}
