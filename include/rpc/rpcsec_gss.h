/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Doug Rabson
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
 *
 *	$FreeBSD$
 */

#ifndef _RPCSEC_GSS_H
#define _RPCSEC_GSS_H

#include <gssapi/gssapi.h>

#ifndef MAX_GSS_MECH
#define MAX_GSS_MECH	64
#endif

/*
 * Define the types of security service required for rpc_gss_seccreate().
 */
typedef enum {
	rpc_gss_svc_default	= 0,
	rpc_gss_svc_none	= 1,
	rpc_gss_svc_integrity	= 2,
	rpc_gss_svc_privacy	= 3
} rpc_gss_service_t;

/*
 * Structure containing options for rpc_gss_seccreate().
 */
typedef struct {
	int		req_flags;	/* GSS request bits */
	int		time_req;	/* requested credential lifetime */
	gss_cred_id_t	my_cred;	/* GSS credential */
	gss_channel_bindings_t input_channel_bindings;
} rpc_gss_options_req_t;

/*
 * Structure containing options returned by rpc_gss_seccreate().
 */
typedef struct {
	int		major_status;
	int		minor_status;
	u_int		rpcsec_version;
	int		ret_flags;
	int		time_req;
	gss_ctx_id_t	gss_context;
	char		actual_mechanism[MAX_GSS_MECH];
} rpc_gss_options_ret_t;

/*
 * Client principal type. Used as an argument to
 * rpc_gss_get_principal_name(). Also referenced by the
 * rpc_gss_rawcred_t structure.
 */
typedef struct {
	int		len;
	char		name[1];
} *rpc_gss_principal_t;

/*
 * Structure for raw credentials used by rpc_gss_getcred() and
 * rpc_gss_set_callback().
 */
typedef struct {
	u_int		version;	/* RPC version number */
	const char	*mechanism;	/* security mechanism */
	const char	*qop;		/* quality of protection */
	rpc_gss_principal_t client_principal; /* client name */
	const char	*svc_principal;	/* server name */
	rpc_gss_service_t service;	/* service type */
} rpc_gss_rawcred_t;

/*
 * Unix credentials derived from raw credentials. Returned by
 * rpc_gss_getcred().
 */
typedef struct {
	uid_t		uid;		/* user ID */
	gid_t		gid;		/* group ID */
	short		gidlen;
	gid_t		*gidlist;	/* list of groups */
} rpc_gss_ucred_t;

/*
 * Structure used to enforce a particular QOP and service.
 */
typedef struct {
	bool_t		locked;
	rpc_gss_rawcred_t *raw_cred;
} rpc_gss_lock_t;

/*
 * Callback structure used by rpc_gss_set_callback().
 */
typedef struct {
	u_int		program;	/* RPC program number */
	u_int		version;	/* RPC version number */
					/* user defined callback */
	bool_t		(*callback)(struct svc_req *req,
				    gss_cred_id_t deleg,
				    gss_ctx_id_t gss_context,
				    rpc_gss_lock_t *lock,
				    void **cookie);
} rpc_gss_callback_t;

/*
 * Structure used to return error information by rpc_gss_get_error()
 */
typedef struct {
	int		rpc_gss_error;
	int		system_error;	/* same as errno */
} rpc_gss_error_t;

/*
 * Values for rpc_gss_error
 */
#define RPC_GSS_ER_SUCCESS	0	/* no error */
#define RPC_GSS_ER_SYSTEMERROR	1	/* system error */

__BEGIN_DECLS

AUTH	*rpc_gss_seccreate(CLIENT *clnt, const char *principal,
    const char *mechanism, rpc_gss_service_t service, const char *qop,
    rpc_gss_options_req_t *options_req, rpc_gss_options_ret_t *options_ret);
bool_t	rpc_gss_set_defaults(AUTH *auth, rpc_gss_service_t service,
    const char *qop);
int	rpc_gss_max_data_length(AUTH *handle, int max_tp_unit_len);
void	rpc_gss_get_error(rpc_gss_error_t *error);

bool_t	rpc_gss_mech_to_oid(const char *mech, gss_OID *oid_ret);
bool_t	rpc_gss_oid_to_mech(gss_OID oid, const char **mech_ret);
bool_t	rpc_gss_qop_to_num(const char *qop, const char *mech, u_int *num_ret);
const char **rpc_gss_get_mechanisms(void);
const char **rpc_gss_get_mech_info(const char *mech, rpc_gss_service_t *service);
bool_t	rpc_gss_get_versions(u_int *vers_hi, u_int *vers_lo);
bool_t	rpc_gss_is_installed(const char *mech);

bool_t	rpc_gss_set_svc_name(const char *principal, const char *mechanism,
    u_int req_time, u_int program, u_int version);
bool_t	rpc_gss_getcred(struct svc_req *req, rpc_gss_rawcred_t **rcred,
    rpc_gss_ucred_t **ucred, void **cookie);
bool_t	rpc_gss_set_callback(rpc_gss_callback_t *cb);
bool_t	rpc_gss_get_principal_name(rpc_gss_principal_t *principal,
    const char *mech, const char *name, const char *node, const char *domain);
int	rpc_gss_svc_max_data_length(struct svc_req *req, int max_tp_unit_len);

/*
 * Internal interface from the RPC implementation.
 */
bool_t	__rpc_gss_wrap(AUTH *auth, void *header, size_t headerlen,
    XDR* xdrs, xdrproc_t xdr_args, void *args_ptr);
bool_t	__rpc_gss_unwrap(AUTH *auth, XDR* xdrs, xdrproc_t xdr_args,
    void *args_ptr);
bool_t __rpc_gss_set_error(int rpc_gss_error, int system_error);

__END_DECLS

#endif /* !_RPCSEC_GSS_H */
