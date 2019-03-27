/*	$NetBSD: auth.h,v 1.15 2000/06/02 22:57:55 fvdl Exp $	*/

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
 *
 *	from: @(#)auth.h 1.17 88/02/08 SMI
 *	from: @(#)auth.h	2.3 88/08/07 4.0 RPCSRC
 *	from: @(#)auth.h	1.43 	98/02/02 SMI
 * $FreeBSD$
 */

/*
 * auth.h, Authentication interface.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * The data structures are completely opaque to the client.  The client
 * is required to pass an AUTH * to routines that create rpc
 * "sessions".
 */

#ifndef _RPC_AUTH_H
#define _RPC_AUTH_H
#include <rpc/xdr.h>
#include <rpc/clnt_stat.h>
#include <sys/cdefs.h>
#include <sys/socket.h>

#define MAX_AUTH_BYTES	400
#define MAXNETNAMELEN	255	/* maximum length of network user's name */

/*
 *  Client side authentication/security data
 */

typedef struct sec_data {
	u_int	secmod;		/* security mode number e.g. in nfssec.conf */
	u_int	rpcflavor;	/* rpc flavors:AUTH_UNIX,AUTH_DES,RPCSEC_GSS */
	int	flags;		/* AUTH_F_xxx flags */
	caddr_t data;		/* opaque data per flavor */
} sec_data_t;

#ifdef _SYSCALL32_IMPL
struct sec_data32 {
	uint32_t secmod;	/* security mode number e.g. in nfssec.conf */
	uint32_t rpcflavor;	/* rpc flavors:AUTH_UNIX,AUTH_DES,RPCSEC_GSS */
	int32_t flags;		/* AUTH_F_xxx flags */
	caddr32_t data;		/* opaque data per flavor */
};
#endif /* _SYSCALL32_IMPL */

/*
 * AUTH_DES flavor specific data from sec_data opaque data field.
 * AUTH_KERB has the same structure.
 */
typedef struct des_clnt_data {
	struct netbuf	syncaddr;	/* time sync addr */
	struct knetconfig *knconf;	/* knetconfig info that associated */
					/* with the syncaddr. */
	char		*netname;	/* server's netname */
	int		netnamelen;	/* server's netname len */
} dh_k4_clntdata_t;

#ifdef _SYSCALL32_IMPL
struct des_clnt_data32 {
	struct netbuf32 syncaddr;	/* time sync addr */
	caddr32_t knconf;		/* knetconfig info that associated */
					/* with the syncaddr. */
	caddr32_t netname;		/* server's netname */
	int32_t netnamelen;		/* server's netname len */
};
#endif /* _SYSCALL32_IMPL */

#ifdef KERBEROS
/*
 * flavor specific data to hold the data for AUTH_DES/AUTH_KERB(v4)
 * in sec_data->data opaque field.
 */
typedef struct krb4_svc_data {
	int		window;		/* window option value */
} krb4_svcdata_t;
 
typedef struct krb4_svc_data	des_svcdata_t;
#endif /* KERBEROS */

/*
 * authentication/security specific flags
 */
#define AUTH_F_RPCTIMESYNC	0x001	/* use RPC to do time sync */
#define AUTH_F_TRYNONE		0x002	/* allow fall back to AUTH_NONE */


/*
 * Status returned from authentication check
 */
enum auth_stat {
	AUTH_OK=0,
	/*
	 * failed at remote end
	 */
	AUTH_BADCRED=1,			/* bogus credentials (seal broken) */
	AUTH_REJECTEDCRED=2,		/* client should begin new session */
	AUTH_BADVERF=3,			/* bogus verifier (seal broken) */
	AUTH_REJECTEDVERF=4,		/* verifier expired or was replayed */
	AUTH_TOOWEAK=5,			/* rejected due to security reasons */
	/*
	 * failed locally
	*/
	AUTH_INVALIDRESP=6,		/* bogus response verifier */
	AUTH_FAILED=7,			/* some unknown reason */
#ifdef KERBEROS
	/*
	 * kerberos errors
	 */
	,
	AUTH_KERB_GENERIC = 8,		/* kerberos generic error */
	AUTH_TIMEEXPIRE = 9,		/* time of credential expired */
	AUTH_TKT_FILE = 10,		/* something wrong with ticket file */
	AUTH_DECODE = 11,			/* can't decode authenticator */
	AUTH_NET_ADDR = 12,		/* wrong net address in ticket */
#endif /* KERBEROS */
	/*
	 * RPCSEC_GSS errors
	 */
	RPCSEC_GSS_CREDPROBLEM = 13,
	RPCSEC_GSS_CTXPROBLEM = 14,
	RPCSEC_GSS_NODISPATCH = 0x8000000
};

union des_block {
	struct {
		uint32_t high;
		uint32_t low;
	} key;
	char c[8];
};
typedef union des_block des_block;
__BEGIN_DECLS
extern bool_t xdr_des_block(XDR *, des_block *);
__END_DECLS

/*
 * Authentication info.  Opaque to client.
 */
struct opaque_auth {
	enum_t	oa_flavor;		/* flavor of auth */
	caddr_t	oa_base;		/* address of more auth stuff */
	u_int	oa_length;		/* not to exceed MAX_AUTH_BYTES */
};


/*
 * Auth handle, interface to client side authenticators.
 */
typedef struct __auth {
	struct	opaque_auth	ah_cred;
	struct	opaque_auth	ah_verf;
	union	des_block	ah_key;
	struct auth_ops {
		void	(*ah_nextverf) (struct __auth *);
		/* nextverf & serialize */
		int	(*ah_marshal) (struct __auth *, XDR *);
		/* validate verifier */
		int	(*ah_validate) (struct __auth *,
			    struct opaque_auth *);
		/* refresh credentials */
		int	(*ah_refresh) (struct __auth *, void *);
		/* destroy this structure */
		void	(*ah_destroy) (struct __auth *);
	} *ah_ops;
	void *ah_private;
} AUTH;


/*
 * Authentication ops.
 * The ops and the auth handle provide the interface to the authenticators.
 *
 * AUTH	*auth;
 * XDR	*xdrs;
 * struct opaque_auth verf;
 */
#define AUTH_NEXTVERF(auth)		\
		((*((auth)->ah_ops->ah_nextverf))(auth))
#define auth_nextverf(auth)		\
		((*((auth)->ah_ops->ah_nextverf))(auth))

#define AUTH_MARSHALL(auth, xdrs)	\
		((*((auth)->ah_ops->ah_marshal))(auth, xdrs))
#define auth_marshall(auth, xdrs)	\
		((*((auth)->ah_ops->ah_marshal))(auth, xdrs))

#define AUTH_VALIDATE(auth, verfp)	\
		((*((auth)->ah_ops->ah_validate))((auth), verfp))
#define auth_validate(auth, verfp)	\
		((*((auth)->ah_ops->ah_validate))((auth), verfp))

#define AUTH_REFRESH(auth, msg)		\
		((*((auth)->ah_ops->ah_refresh))(auth, msg))
#define auth_refresh(auth, msg)		\
		((*((auth)->ah_ops->ah_refresh))(auth, msg))

#define AUTH_DESTROY(auth)		\
		((*((auth)->ah_ops->ah_destroy))(auth))
#define auth_destroy(auth)		\
		((*((auth)->ah_ops->ah_destroy))(auth))


__BEGIN_DECLS
extern struct opaque_auth _null_auth;
__END_DECLS

/*
 * These are the various implementations of client side authenticators.
 */

/*
 * System style authentication
 * AUTH *authunix_create(machname, uid, gid, len, aup_gids)
 *	char *machname;
 *	u_int uid;
 *	u_int gid;
 *	int len;
 *	u_int *aup_gids;
 */
__BEGIN_DECLS
extern AUTH *authunix_create(char *, u_int, u_int, int, u_int *);
extern AUTH *authunix_create_default(void);	/* takes no parameters */
extern AUTH *authnone_create(void);		/* takes no parameters */
__END_DECLS
/*
 * DES style authentication
 * AUTH *authsecdes_create(servername, window, timehost, ckey)
 * 	char *servername;		- network name of server
 *	u_int window;			- time to live
 * 	const char *timehost;			- optional hostname to sync with
 * 	des_block *ckey;		- optional conversation key to use
 */
__BEGIN_DECLS
extern AUTH *authdes_create (char *, u_int, struct sockaddr *, des_block *);
extern AUTH *authdes_seccreate (const char *, const u_int, const  char *,
    const  des_block *);
__END_DECLS

__BEGIN_DECLS
extern bool_t xdr_opaque_auth		(XDR *, struct opaque_auth *);
__END_DECLS

#define authsys_create(c,i1,i2,i3,ip) authunix_create((c),(i1),(i2),(i3),(ip))
#define authsys_create_default() authunix_create_default()

/*
 * Netname manipulation routines.
 */
__BEGIN_DECLS
extern int getnetname(char *);
extern int host2netname(char *, const char *, const char *);
extern int user2netname(char *, const uid_t, const char *);
extern int netname2user(char *, uid_t *, gid_t *, int *, gid_t *);
extern int netname2host(char *, char *, const int);
extern void passwd2des ( char *, char * );
__END_DECLS

/*
 *
 * These routines interface to the keyserv daemon
 *
 */
__BEGIN_DECLS
extern int key_decryptsession(const char *, des_block *);
extern int key_encryptsession(const char *, des_block *);
extern int key_gendes(des_block *);
extern int key_setsecret(const char *);
extern int key_secretkey_is_set(void);
__END_DECLS

/*
 * Publickey routines.
 */
__BEGIN_DECLS
extern int getpublickey (const char *, char *);
extern int getpublicandprivatekey (const char *, char *);
extern int getsecretkey (char *, char *, char *);
__END_DECLS

#ifdef KERBEROS
/*
 * Kerberos style authentication
 * AUTH *authkerb_seccreate(service, srv_inst, realm, window, timehost, status)
 *	const char *service;			- service name
 *	const char *srv_inst;			- server instance
 *	const char *realm;			- server realm
 *	const u_int window;			- time to live
 *	const char *timehost;			- optional hostname to sync with
 *	int *status;				- kerberos status returned
 */
__BEGIN_DECLS
extern AUTH	*authkerb_seccreate(const char *, const char *, const  char *,
		    const u_int, const char *, int *);
__END_DECLS

/*
 * Map a kerberos credential into a unix cred.
 *
 *	authkerb_getucred(rqst, uid, gid, grouplen, groups)
 *	const struct svc_req *rqst;		- request pointer
 *	uid_t *uid;
 *	gid_t *gid;
 *	short *grouplen;
 *	int *groups;
 *
 */
__BEGIN_DECLS
extern int	authkerb_getucred(/* struct svc_req *, uid_t *, gid_t *,
		    short *, int * */);
__END_DECLS
#endif /* KERBEROS */

__BEGIN_DECLS
struct svc_req;
struct rpc_msg;
enum auth_stat _svcauth_null (struct svc_req *, struct rpc_msg *);
enum auth_stat _svcauth_short (struct svc_req *, struct rpc_msg *);
enum auth_stat _svcauth_unix (struct svc_req *, struct rpc_msg *);
__END_DECLS

#define AUTH_NONE	0		/* no authentication */
#define	AUTH_NULL	0		/* backward compatibility */
#define	AUTH_SYS	1		/* unix style (uid, gids) */
#define AUTH_UNIX	AUTH_SYS
#define	AUTH_SHORT	2		/* short hand unix style */
#define AUTH_DH		3		/* for Diffie-Hellman mechanism */
#define AUTH_DES	AUTH_DH		/* for backward compatibility */
#define AUTH_KERB	4		/* kerberos style */
#define RPCSEC_GSS	6		/* RPCSEC_GSS */

/*
 * Pseudo auth flavors for RPCSEC_GSS.
 */
#define	RPCSEC_GSS_KRB5		390003
#define	RPCSEC_GSS_KRB5I	390004
#define	RPCSEC_GSS_KRB5P	390005

#endif /* !_RPC_AUTH_H */
