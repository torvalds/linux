/*	$NetBSD: auth_unix.c,v 1.18 2000/07/06 03:03:30 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid2 = "@(#)auth_unix.c 1.19 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)auth_unix.c	2.2 88/08/01 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * auth_unix.c, Implements UNIX style authentication parameters.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * The system is very weak.  The client uses no encryption for it's
 * credentials and only sends null verifiers.  The server sends backs
 * null verifiers or optionally a verifier that suggests a new short hand
 * for the credentials.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/hash.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/refcount.h>
#include <sys/sx.h>
#include <sys/ucred.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include <rpc/rpc_com.h>

/* auth_unix.c */
static void authunix_nextverf (AUTH *);
static bool_t authunix_marshal (AUTH *, uint32_t, XDR *, struct mbuf *);
static bool_t authunix_validate (AUTH *, uint32_t, struct opaque_auth *,
    struct mbuf **);
static bool_t authunix_refresh (AUTH *, void *);
static void authunix_destroy (AUTH *);
static void marshal_new_auth (AUTH *);

static struct auth_ops authunix_ops = {
	.ah_nextverf =		authunix_nextverf,
	.ah_marshal =		authunix_marshal,
	.ah_validate =		authunix_validate,
	.ah_refresh =		authunix_refresh,
	.ah_destroy =		authunix_destroy,
};

/*
 * This struct is pointed to by the ah_private field of an auth_handle.
 */
struct audata {
	TAILQ_ENTRY(audata)	au_link;
	TAILQ_ENTRY(audata)	au_alllink;
	volatile u_int		au_refs;
	struct xucred		au_xcred;
	struct opaque_auth	au_origcred;	/* original credentials */
	struct opaque_auth	au_shcred;	/* short hand cred */
	u_long			au_shfaults;	/* short hand cache faults */
	char			au_marshed[MAX_AUTH_BYTES];
	u_int			au_mpos;	/* xdr pos at end of marshed */
	AUTH			*au_auth;	/* link back to AUTH */
};
TAILQ_HEAD(audata_list, audata);
#define	AUTH_PRIVATE(auth)	((struct audata *)auth->ah_private)

#define AUTH_UNIX_HASH_SIZE	16
#define AUTH_UNIX_MAX		256
static struct audata_list auth_unix_cache[AUTH_UNIX_HASH_SIZE];
static struct audata_list auth_unix_all;
static struct sx auth_unix_lock;
static int auth_unix_count;

static void
authunix_init(void *dummy)
{
	int i;

	for (i = 0; i < AUTH_UNIX_HASH_SIZE; i++)
		TAILQ_INIT(&auth_unix_cache[i]);
	TAILQ_INIT(&auth_unix_all);
	sx_init(&auth_unix_lock, "auth_unix_lock");
}
SYSINIT(authunix_init, SI_SUB_KMEM, SI_ORDER_ANY, authunix_init, NULL);

/*
 * Create a unix style authenticator.
 * Returns an auth handle with the given stuff in it.
 */
AUTH *
authunix_create(struct ucred *cred)
{
	uint32_t h, th;
	struct xucred xcr;
	char mymem[MAX_AUTH_BYTES];
	XDR xdrs;
	AUTH *auth;
	struct audata *au, *tau;
	struct timeval now;
	uint32_t time;
	int len;

	if (auth_unix_count > AUTH_UNIX_MAX) {
		while (auth_unix_count > AUTH_UNIX_MAX) {
			sx_xlock(&auth_unix_lock);
			tau = TAILQ_FIRST(&auth_unix_all);
			th = HASHSTEP(HASHINIT, tau->au_xcred.cr_uid)
				% AUTH_UNIX_HASH_SIZE;
			TAILQ_REMOVE(&auth_unix_cache[th], tau, au_link);
			TAILQ_REMOVE(&auth_unix_all, tau, au_alllink);
			auth_unix_count--;
			sx_xunlock(&auth_unix_lock);
			AUTH_DESTROY(tau->au_auth);
		}
	}

	/*
	 * Hash the uid to see if we already have an AUTH with this cred.
	 */
	h = HASHSTEP(HASHINIT, cred->cr_uid) % AUTH_UNIX_HASH_SIZE;
	cru2x(cred, &xcr);
again:
	sx_slock(&auth_unix_lock);
	TAILQ_FOREACH(au, &auth_unix_cache[h], au_link) {
		if (!memcmp(&xcr, &au->au_xcred, sizeof(xcr))) {
			refcount_acquire(&au->au_refs);
			if (sx_try_upgrade(&auth_unix_lock)) {
				/*
				 * Keep auth_unix_all LRU sorted.
				 */
				TAILQ_REMOVE(&auth_unix_all, au, au_alllink);
				TAILQ_INSERT_TAIL(&auth_unix_all, au,
				    au_alllink);
				sx_xunlock(&auth_unix_lock);
			} else {
				sx_sunlock(&auth_unix_lock);
			}
			return (au->au_auth);
		}
	}

	sx_sunlock(&auth_unix_lock);

	/*
	 * Allocate and set up auth handle
	 */
	au = NULL;
	auth = mem_alloc(sizeof(*auth));
	au = mem_alloc(sizeof(*au));
	auth->ah_ops = &authunix_ops;
	auth->ah_private = (caddr_t)au;
	auth->ah_verf = au->au_shcred = _null_auth;
	refcount_init(&au->au_refs, 1);
	au->au_xcred = xcr;
	au->au_shfaults = 0;
	au->au_origcred.oa_base = NULL;
	au->au_auth = auth;

	getmicrotime(&now);
	time = now.tv_sec;

	/*
	 * Serialize the parameters into origcred
	 */
	xdrmem_create(&xdrs, mymem, MAX_AUTH_BYTES, XDR_ENCODE);
	cru2x(cred, &xcr);
	if (! xdr_authunix_parms(&xdrs, &time, &xcr)) 
		panic("authunix_create: failed to encode creds");
	au->au_origcred.oa_length = len = XDR_GETPOS(&xdrs);
	au->au_origcred.oa_flavor = AUTH_UNIX;
	au->au_origcred.oa_base = mem_alloc((u_int) len);
	memcpy(au->au_origcred.oa_base, mymem, (size_t)len);

	/*
	 * set auth handle to reflect new cred.
	 */
	auth->ah_cred = au->au_origcred;
	marshal_new_auth(auth);

	sx_xlock(&auth_unix_lock);
	TAILQ_FOREACH(tau, &auth_unix_cache[h], au_link) {
		if (!memcmp(&xcr, &tau->au_xcred, sizeof(xcr))) {
			/*
			 * We lost a race to create the AUTH that
			 * matches this cred.
			 */
			sx_xunlock(&auth_unix_lock);
			AUTH_DESTROY(auth);
			goto again;
		}
	}

	auth_unix_count++;
	TAILQ_INSERT_TAIL(&auth_unix_cache[h], au, au_link);
	TAILQ_INSERT_TAIL(&auth_unix_all, au, au_alllink);
	refcount_acquire(&au->au_refs);	/* one for the cache, one for user */
	sx_xunlock(&auth_unix_lock);

	return (auth);
}

/*
 * authunix operations
 */

/* ARGSUSED */
static void
authunix_nextverf(AUTH *auth)
{
	/* no action necessary */
}

static bool_t
authunix_marshal(AUTH *auth, uint32_t xid, XDR *xdrs, struct mbuf *args)
{
	struct audata *au;

	au = AUTH_PRIVATE(auth);
	if (!XDR_PUTBYTES(xdrs, au->au_marshed, au->au_mpos))
		return (FALSE);

	xdrmbuf_append(xdrs, args);

	return (TRUE);
}

static bool_t
authunix_validate(AUTH *auth, uint32_t xid, struct opaque_auth *verf,
    struct mbuf **mrepp)
{
	struct audata *au;
	XDR txdrs;

	if (!verf)
		return (TRUE);

	if (verf->oa_flavor == AUTH_SHORT) {
		au = AUTH_PRIVATE(auth);
		xdrmem_create(&txdrs, verf->oa_base, verf->oa_length,
		    XDR_DECODE);

		if (au->au_shcred.oa_base != NULL) {
			mem_free(au->au_shcred.oa_base,
			    au->au_shcred.oa_length);
			au->au_shcred.oa_base = NULL;
		}
		if (xdr_opaque_auth(&txdrs, &au->au_shcred)) {
			auth->ah_cred = au->au_shcred;
		} else {
			txdrs.x_op = XDR_FREE;
			(void)xdr_opaque_auth(&txdrs, &au->au_shcred);
			au->au_shcred.oa_base = NULL;
			auth->ah_cred = au->au_origcred;
		}
		marshal_new_auth(auth);
	}

	return (TRUE);
}

static bool_t
authunix_refresh(AUTH *auth, void *dummy)
{
	struct audata *au = AUTH_PRIVATE(auth);
	struct xucred xcr;
	uint32_t time;
	struct timeval now;
	XDR xdrs;
	int stat;

	if (auth->ah_cred.oa_base == au->au_origcred.oa_base) {
		/* there is no hope.  Punt */
		return (FALSE);
	}
	au->au_shfaults ++;

	/* first deserialize the creds back into a struct ucred */
	xdrmem_create(&xdrs, au->au_origcred.oa_base,
	    au->au_origcred.oa_length, XDR_DECODE);
	stat = xdr_authunix_parms(&xdrs, &time, &xcr);
	if (! stat)
		goto done;

	/* update the time and serialize in place */
	getmicrotime(&now);
	time = now.tv_sec;
	xdrs.x_op = XDR_ENCODE;
	XDR_SETPOS(&xdrs, 0);

	stat = xdr_authunix_parms(&xdrs, &time, &xcr);
	if (! stat)
		goto done;
	auth->ah_cred = au->au_origcred;
	marshal_new_auth(auth);
done:
	XDR_DESTROY(&xdrs);
	return (stat);
}

static void
authunix_destroy(AUTH *auth)
{
	struct audata *au;

	au = AUTH_PRIVATE(auth);

	if (!refcount_release(&au->au_refs))
		return;

	mem_free(au->au_origcred.oa_base, au->au_origcred.oa_length);

	if (au->au_shcred.oa_base != NULL)
		mem_free(au->au_shcred.oa_base, au->au_shcred.oa_length);

	mem_free(auth->ah_private, sizeof(struct audata));

	if (auth->ah_verf.oa_base != NULL)
		mem_free(auth->ah_verf.oa_base, auth->ah_verf.oa_length);

	mem_free(auth, sizeof(*auth));
}

/*
 * Marshals (pre-serializes) an auth struct.
 * sets private data, au_marshed and au_mpos
 */
static void
marshal_new_auth(AUTH *auth)
{
	XDR	xdr_stream;
	XDR	*xdrs = &xdr_stream;
	struct audata *au;

	au = AUTH_PRIVATE(auth);
	xdrmem_create(xdrs, au->au_marshed, MAX_AUTH_BYTES, XDR_ENCODE);
	if ((! xdr_opaque_auth(xdrs, &(auth->ah_cred))) ||
	    (! xdr_opaque_auth(xdrs, &(auth->ah_verf))))
		printf("auth_none.c - Fatal marshalling problem");
	else
		au->au_mpos = XDR_GETPOS(xdrs);
	XDR_DESTROY(xdrs);
}
