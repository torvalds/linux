/*	$NetBSD: auth_none.c,v 1.13 2000/01/22 22:19:17 mycroft Exp $	*/

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
static char *sccsid2 = "@(#)auth_none.c 1.19 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)auth_none.c	2.1 88/07/29 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * auth_none.c
 * Creates a client authentication handle for passing "null"
 * credentials and verifiers to remote systems.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#define MAX_MARSHAL_SIZE 20

/*
 * Authenticator operations routines
 */

static bool_t authnone_marshal (AUTH *, uint32_t, XDR *, struct mbuf *);
static void authnone_verf (AUTH *);
static bool_t authnone_validate (AUTH *, uint32_t, struct opaque_auth *,
    struct mbuf **);
static bool_t authnone_refresh (AUTH *, void *);
static void authnone_destroy (AUTH *);

static struct auth_ops authnone_ops = {
	.ah_nextverf =		authnone_verf,
	.ah_marshal =		authnone_marshal,
	.ah_validate =		authnone_validate,
	.ah_refresh =		authnone_refresh,
	.ah_destroy =		authnone_destroy,
};

struct authnone_private {
	AUTH	no_client;
	char	mclient[MAX_MARSHAL_SIZE];
	u_int	mcnt;
};

static struct authnone_private authnone_private;

static void
authnone_init(void *dummy)
{
	struct authnone_private *ap = &authnone_private;
	XDR xdrs;

	ap->no_client.ah_cred = ap->no_client.ah_verf = _null_auth;
	ap->no_client.ah_ops = &authnone_ops;
	xdrmem_create(&xdrs, ap->mclient, MAX_MARSHAL_SIZE, XDR_ENCODE);
	xdr_opaque_auth(&xdrs, &ap->no_client.ah_cred);
	xdr_opaque_auth(&xdrs, &ap->no_client.ah_verf);
	ap->mcnt = XDR_GETPOS(&xdrs);
	XDR_DESTROY(&xdrs);
}
SYSINIT(authnone_init, SI_SUB_KMEM, SI_ORDER_ANY, authnone_init, NULL);

AUTH *
authnone_create()
{
	struct authnone_private *ap = &authnone_private;

	return (&ap->no_client);
}

/*ARGSUSED*/
static bool_t
authnone_marshal(AUTH *client, uint32_t xid, XDR *xdrs, struct mbuf *args)
{
	struct authnone_private *ap = &authnone_private;

	KASSERT(xdrs != NULL, ("authnone_marshal: xdrs is null"));

	if (!XDR_PUTBYTES(xdrs, ap->mclient, ap->mcnt))
		return (FALSE);

	xdrmbuf_append(xdrs, args);

	return (TRUE);
}

/* All these unused parameters are required to keep ANSI-C from grumbling */
/*ARGSUSED*/
static void
authnone_verf(AUTH *client)
{
}

/*ARGSUSED*/
static bool_t
authnone_validate(AUTH *client, uint32_t xid, struct opaque_auth *opaque,
    struct mbuf **mrepp)
{

	return (TRUE);
}

/*ARGSUSED*/
static bool_t
authnone_refresh(AUTH *client, void *dummy)
{

	return (FALSE);
}

/*ARGSUSED*/
static void
authnone_destroy(AUTH *client)
{
}
