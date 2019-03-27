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
/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */
/*
 * auth_des.c, client-side implementation of DES authentication
 */

#include "namespace.h"
#include "reentrant.h"
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/cdefs.h>
#include <rpc/des_crypt.h>
#include <syslog.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/auth_des.h>
#include <rpc/clnt.h>
#include <rpc/xdr.h>
#include <sys/socket.h>
#undef NIS
#include <rpcsvc/nis.h>
#include "un-namespace.h"
#include "mt_misc.h"

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = 	"@(#)auth_des.c	2.2 88/07/29 4.0 RPCSRC; from 1.9 88/02/08 SMI";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define USEC_PER_SEC		1000000
#define RTIME_TIMEOUT		5	/* seconds to wait for sync */

#define AUTH_PRIVATE(auth)	(struct ad_private *) auth->ah_private
#define ALLOC(object_type)	(object_type *) mem_alloc(sizeof(object_type))
#define FREE(ptr, size)		mem_free((char *)(ptr), (int) size)
#define ATTEMPT(xdr_op)		if (!(xdr_op)) return (FALSE)

extern bool_t xdr_authdes_cred( XDR *, struct authdes_cred *);
extern bool_t xdr_authdes_verf( XDR *, struct authdes_verf *);
extern int key_encryptsession_pk(char *, netobj *, des_block *);

extern bool_t __rpc_get_time_offset(struct timeval *, nis_server *, char *,
	char **, char **);

/* 
 * DES authenticator operations vector
 */
static void	authdes_nextverf(AUTH *);
static bool_t	authdes_marshal(AUTH *, XDR *);
static bool_t	authdes_validate(AUTH *, struct opaque_auth *);
static bool_t	authdes_refresh(AUTH *, void *);
static void	authdes_destroy(AUTH *);

static struct auth_ops *authdes_ops(void);

/*
 * This struct is pointed to by the ah_private field of an "AUTH *"
 */
struct ad_private {
	char *ad_fullname; 		/* client's full name */
	u_int ad_fullnamelen;		/* length of name, rounded up */
	char *ad_servername; 		/* server's full name */
	u_int ad_servernamelen;		/* length of name, rounded up */
	u_int ad_window;	  	/* client specified window */
	bool_t ad_dosync;		/* synchronize? */		
	struct netbuf ad_syncaddr;	/* remote host to synch with */
	char *ad_timehost;		/* remote host to synch with */
	struct timeval ad_timediff;	/* server's time - client's time */
	u_int ad_nickname;		/* server's nickname for client */
	struct authdes_cred ad_cred;	/* storage for credential */
	struct authdes_verf ad_verf;	/* storage for verifier */
	struct timeval ad_timestamp;	/* timestamp sent */
	des_block ad_xkey;		/* encrypted conversation key */
	u_char ad_pkey[1024];		/* Server's actual public key */
	char *ad_netid;			/* Timehost netid */
	char *ad_uaddr;			/* Timehost uaddr */
	nis_server *ad_nis_srvr;	/* NIS+ server struct */
};

AUTH *authdes_pk_seccreate(const char *, netobj *, u_int, const char *,
	const des_block *, nis_server *);
	
/*
 * documented version of authdes_seccreate
 */
/*
	servername:	network name of server
	win:		time to live
	timehost:	optional hostname to sync with
	ckey:		optional conversation key to use
*/

AUTH *
authdes_seccreate(const char *servername, const u_int win,
	const char *timehost, const des_block *ckey)
{
	u_char  pkey_data[1024];
	netobj  pkey;
	AUTH    *dummy;

	if (! getpublickey(servername, (char *) pkey_data)) {
		syslog(LOG_ERR,
		    "authdes_seccreate: no public key found for %s",
		    servername);
		return (NULL);
	}

	pkey.n_bytes = (char *) pkey_data;
	pkey.n_len = (u_int)strlen((char *)pkey_data) + 1;
	dummy = authdes_pk_seccreate(servername, &pkey, win, timehost,
	    ckey, NULL);
	return (dummy);
}

/*
 * Slightly modified version of authdessec_create which takes the public key
 * of the server principal as an argument. This spares us a call to
 * getpublickey() which in the nameserver context can cause a deadlock.
 */
AUTH *
authdes_pk_seccreate(const char *servername, netobj *pkey, u_int window,
	const char *timehost, const des_block *ckey, nis_server *srvr)
{
	AUTH *auth;
	struct ad_private *ad;
	char namebuf[MAXNETNAMELEN+1];

	/*
	 * Allocate everything now
	 */
	auth = ALLOC(AUTH);
	if (auth == NULL) {
		syslog(LOG_ERR, "authdes_pk_seccreate: out of memory");
		return (NULL);
	}
	ad = ALLOC(struct ad_private);
	if (ad == NULL) {
		syslog(LOG_ERR, "authdes_pk_seccreate: out of memory");
		goto failed;
	}
	ad->ad_fullname = ad->ad_servername = NULL; /* Sanity reasons */
	ad->ad_timehost = NULL;
	ad->ad_netid = NULL;
	ad->ad_uaddr = NULL;
	ad->ad_nis_srvr = NULL;
	ad->ad_timediff.tv_sec = 0;
	ad->ad_timediff.tv_usec = 0;
	memcpy(ad->ad_pkey, pkey->n_bytes, pkey->n_len);
	if (!getnetname(namebuf))
		goto failed;
	ad->ad_fullnamelen = RNDUP((u_int) strlen(namebuf));
	ad->ad_fullname = (char *)mem_alloc(ad->ad_fullnamelen + 1);
	ad->ad_servernamelen = strlen(servername);
	ad->ad_servername = (char *)mem_alloc(ad->ad_servernamelen + 1);

	if (ad->ad_fullname == NULL || ad->ad_servername == NULL) {
		syslog(LOG_ERR, "authdes_seccreate: out of memory");
		goto failed;
	}
	if (timehost != NULL) {
		ad->ad_timehost = (char *)mem_alloc(strlen(timehost) + 1);
		if (ad->ad_timehost == NULL) {
			syslog(LOG_ERR, "authdes_seccreate: out of memory");
			goto failed;
		}
		memcpy(ad->ad_timehost, timehost, strlen(timehost) + 1);
		ad->ad_dosync = TRUE;
	} else if (srvr != NULL) {
		ad->ad_nis_srvr = srvr;	/* transient */
		ad->ad_dosync = TRUE;
	} else {
		ad->ad_dosync = FALSE;
	}
	memcpy(ad->ad_fullname, namebuf, ad->ad_fullnamelen + 1);
	memcpy(ad->ad_servername, servername, ad->ad_servernamelen + 1);
	ad->ad_window = window;
	if (ckey == NULL) {
		if (key_gendes(&auth->ah_key) < 0) {
			syslog(LOG_ERR,
	    "authdes_seccreate: keyserv(1m) is unable to generate session key");
			goto failed;
		}
	} else {
		auth->ah_key = *ckey;
	}

	/*
	 * Set up auth handle
	 */
	auth->ah_cred.oa_flavor = AUTH_DES;
	auth->ah_verf.oa_flavor = AUTH_DES;
	auth->ah_ops = authdes_ops();
	auth->ah_private = (caddr_t)ad;

	if (!authdes_refresh(auth, NULL)) {
		goto failed;
	}
	ad->ad_nis_srvr = NULL; /* not needed any longer */
	return (auth);

failed:
	if (auth)
		FREE(auth, sizeof (AUTH));
	if (ad) {
		if (ad->ad_fullname)
			FREE(ad->ad_fullname, ad->ad_fullnamelen + 1);
		if (ad->ad_servername)
			FREE(ad->ad_servername, ad->ad_servernamelen + 1);
		if (ad->ad_timehost)
			FREE(ad->ad_timehost, strlen(ad->ad_timehost) + 1);
		if (ad->ad_netid)
			FREE(ad->ad_netid, strlen(ad->ad_netid) + 1);
		if (ad->ad_uaddr)
			FREE(ad->ad_uaddr, strlen(ad->ad_uaddr) + 1);
		FREE(ad, sizeof (struct ad_private));
	}
	return (NULL);
}

/*
 * Implement the five authentication operations
 */


/*
 * 1. Next Verifier
 */	
/*ARGSUSED*/
static void
authdes_nextverf(AUTH *auth __unused)
{
	/* what the heck am I supposed to do??? */
}


/*
 * 2. Marshal
 */
static bool_t
authdes_marshal(AUTH *auth, XDR *xdrs)
{
/* LINTED pointer alignment */
	struct ad_private *ad = AUTH_PRIVATE(auth);
	struct authdes_cred *cred = &ad->ad_cred;
	struct authdes_verf *verf = &ad->ad_verf;
	des_block cryptbuf[2];	
	des_block ivec;
	int status;
	int len;
	rpc_inline_t *ixdr;

	/*
	 * Figure out the "time", accounting for any time difference
	 * with the server if necessary.
	 */
	(void)gettimeofday(&ad->ad_timestamp, NULL);
	ad->ad_timestamp.tv_sec += ad->ad_timediff.tv_sec;
	ad->ad_timestamp.tv_usec += ad->ad_timediff.tv_usec;
	while (ad->ad_timestamp.tv_usec >= USEC_PER_SEC) {
		ad->ad_timestamp.tv_usec -= USEC_PER_SEC;
		ad->ad_timestamp.tv_sec++;
	}

	/*
	 * XDR the timestamp and possibly some other things, then
	 * encrypt them.
	 */
	ixdr = (rpc_inline_t *)cryptbuf;
	IXDR_PUT_INT32(ixdr, ad->ad_timestamp.tv_sec);
	IXDR_PUT_INT32(ixdr, ad->ad_timestamp.tv_usec);
	if (ad->ad_cred.adc_namekind == ADN_FULLNAME) {
		IXDR_PUT_U_INT32(ixdr, ad->ad_window);
		IXDR_PUT_U_INT32(ixdr, ad->ad_window - 1);
		ivec.key.high = ivec.key.low = 0;	
		status = cbc_crypt((char *)&auth->ah_key, (char *)cryptbuf, 
			(u_int) 2 * sizeof (des_block),
			DES_ENCRYPT | DES_HW, (char *)&ivec);
	} else {
		status = ecb_crypt((char *)&auth->ah_key, (char *)cryptbuf, 
			(u_int) sizeof (des_block),
			DES_ENCRYPT | DES_HW);
	}
	if (DES_FAILED(status)) {
		syslog(LOG_ERR, "authdes_marshal: DES encryption failure");
		return (FALSE);
	}
	ad->ad_verf.adv_xtimestamp = cryptbuf[0];
	if (ad->ad_cred.adc_namekind == ADN_FULLNAME) {
		ad->ad_cred.adc_fullname.window = cryptbuf[1].key.high;
		ad->ad_verf.adv_winverf = cryptbuf[1].key.low;
	} else {
		ad->ad_cred.adc_nickname = ad->ad_nickname;
		ad->ad_verf.adv_winverf = 0;
	}

	/*
	 * Serialize the credential and verifier into opaque
	 * authentication data.
	 */
	if (ad->ad_cred.adc_namekind == ADN_FULLNAME) {
		len = ((1 + 1 + 2 + 1)*BYTES_PER_XDR_UNIT + ad->ad_fullnamelen);
	} else {
		len = (1 + 1)*BYTES_PER_XDR_UNIT;
	}

	if ((ixdr = xdr_inline(xdrs, 2*BYTES_PER_XDR_UNIT))) {
		IXDR_PUT_INT32(ixdr, AUTH_DES);
		IXDR_PUT_INT32(ixdr, len);
	} else {
		ATTEMPT(xdr_putint32(xdrs, (int *)&auth->ah_cred.oa_flavor));
		ATTEMPT(xdr_putint32(xdrs, &len));
	}
	ATTEMPT(xdr_authdes_cred(xdrs, cred));

	len = (2 + 1)*BYTES_PER_XDR_UNIT; 
	if ((ixdr = xdr_inline(xdrs, 2*BYTES_PER_XDR_UNIT))) {
		IXDR_PUT_INT32(ixdr, AUTH_DES);
		IXDR_PUT_INT32(ixdr, len);
	} else {
		ATTEMPT(xdr_putint32(xdrs, (int *)&auth->ah_verf.oa_flavor));
		ATTEMPT(xdr_putint32(xdrs, &len));
	}
	ATTEMPT(xdr_authdes_verf(xdrs, verf));
	return (TRUE);
}


/*
 * 3. Validate
 */
static bool_t
authdes_validate(AUTH *auth, struct opaque_auth *rverf)
{
/* LINTED pointer alignment */
	struct ad_private *ad = AUTH_PRIVATE(auth);
	struct authdes_verf verf;
	int status;
	uint32_t *ixdr;
	des_block buf;

	if (rverf->oa_length != (2 + 1) * BYTES_PER_XDR_UNIT) {
		return (FALSE);
	}
/* LINTED pointer alignment */
	ixdr = (uint32_t *)rverf->oa_base;
	buf.key.high = (uint32_t)*ixdr++;
	buf.key.low = (uint32_t)*ixdr++;
	verf.adv_int_u = (uint32_t)*ixdr++;

	/*
	 * Decrypt the timestamp
	 */
	status = ecb_crypt((char *)&auth->ah_key, (char *)&buf,
		(u_int)sizeof (des_block), DES_DECRYPT | DES_HW);

	if (DES_FAILED(status)) {
		syslog(LOG_ERR, "authdes_validate: DES decryption failure");
		return (FALSE);
	}

	/*
	 * xdr the decrypted timestamp
	 */
/* LINTED pointer alignment */
	ixdr = (uint32_t *)buf.c;
	verf.adv_timestamp.tv_sec = IXDR_GET_INT32(ixdr) + 1;
	verf.adv_timestamp.tv_usec = IXDR_GET_INT32(ixdr);

	/*
	 * validate
	 */
	if (bcmp((char *)&ad->ad_timestamp, (char *)&verf.adv_timestamp,
		 sizeof(struct timeval)) != 0) {
		syslog(LOG_DEBUG, "authdes_validate: verifier mismatch");
		return (FALSE);
	}

	/*
	 * We have a nickname now, let's use it
	 */
	ad->ad_nickname = verf.adv_nickname;
	ad->ad_cred.adc_namekind = ADN_NICKNAME;
	return (TRUE);
}

/*
 * 4. Refresh
 */
/*ARGSUSED*/
static bool_t
authdes_refresh(AUTH *auth, void *dummy __unused)
{
/* LINTED pointer alignment */
	struct ad_private *ad = AUTH_PRIVATE(auth);
	struct authdes_cred *cred = &ad->ad_cred;
	int		ok;
	netobj		pkey;

	if (ad->ad_dosync) {
                ok = __rpc_get_time_offset(&ad->ad_timediff, ad->ad_nis_srvr,
		    ad->ad_timehost, &(ad->ad_uaddr),
		    &(ad->ad_netid));
		if (! ok) {
			/*
			 * Hope the clocks are synced!
			 */
			ad->ad_dosync = 0;
			syslog(LOG_DEBUG,
			    "authdes_refresh: unable to synchronize clock");
		 }
	}
	ad->ad_xkey = auth->ah_key;
	pkey.n_bytes = (char *)(ad->ad_pkey);
	pkey.n_len = (u_int)strlen((char *)ad->ad_pkey) + 1;
	if (key_encryptsession_pk(ad->ad_servername, &pkey, &ad->ad_xkey) < 0) {
		syslog(LOG_INFO,
		    "authdes_refresh: keyserv(1m) is unable to encrypt session key");
		return (FALSE);
	}
	cred->adc_fullname.key = ad->ad_xkey;
	cred->adc_namekind = ADN_FULLNAME;
	cred->adc_fullname.name = ad->ad_fullname;
	return (TRUE);
}


/*
 * 5. Destroy
 */
static void
authdes_destroy(AUTH *auth)
{
/* LINTED pointer alignment */
	struct ad_private *ad = AUTH_PRIVATE(auth);

	FREE(ad->ad_fullname, ad->ad_fullnamelen + 1);
	FREE(ad->ad_servername, ad->ad_servernamelen + 1);
	if (ad->ad_timehost)
		FREE(ad->ad_timehost, strlen(ad->ad_timehost) + 1);
	if (ad->ad_netid)
		FREE(ad->ad_netid, strlen(ad->ad_netid) + 1);
	if (ad->ad_uaddr)
		FREE(ad->ad_uaddr, strlen(ad->ad_uaddr) + 1);
	FREE(ad, sizeof (struct ad_private));
	FREE(auth, sizeof(AUTH));
}

static struct auth_ops *
authdes_ops(void)
{
	static struct auth_ops ops;

	/* VARIABLES PROTECTED BY ops_lock: ops */
 
	mutex_lock(&authdes_ops_lock);
	if (ops.ah_nextverf == NULL) {
		ops.ah_nextverf = authdes_nextverf;
		ops.ah_marshal = authdes_marshal;
		ops.ah_validate = authdes_validate;
		ops.ah_refresh = authdes_refresh;
		ops.ah_destroy = authdes_destroy;
        }
	mutex_unlock(&authdes_ops_lock);
	return (&ops);
}
