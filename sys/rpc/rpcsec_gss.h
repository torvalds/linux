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

#include <kgssapi/gssapi.h>

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

#ifdef _KERNEL
/*
 * Set up a structure of entry points for the kgssapi module and inline
 * functions named rpc_gss_XXX_call() to use them, so that the kgssapi
 * module doesn't need to be loaded for the NFS modules to work using
 * AUTH_SYS. The kgssapi modules will be loaded by the gssd(8) daemon
 * when it is started up and the entry points will then be filled in.
 */
typedef AUTH	*rpc_gss_secfind_ftype(CLIENT *clnt, struct ucred *cred,
		    const char *principal, gss_OID mech_oid,
		    rpc_gss_service_t service);
typedef void	rpc_gss_secpurge_ftype(CLIENT *clnt);
typedef AUTH	*rpc_gss_seccreate_ftype(CLIENT *clnt, struct ucred *cred,
		    const char *clnt_principal, const char *principal,
		    const char *mechanism, rpc_gss_service_t service,
		    const char *qop, rpc_gss_options_req_t *options_req,
		    rpc_gss_options_ret_t *options_ret);
typedef bool_t	rpc_gss_set_defaults_ftype(AUTH *auth,
		    rpc_gss_service_t service, const char *qop);
typedef int	rpc_gss_max_data_length_ftype(AUTH *handle,
		    int max_tp_unit_len);
typedef void	rpc_gss_get_error_ftype(rpc_gss_error_t *error);
typedef bool_t	rpc_gss_mech_to_oid_ftype(const char *mech, gss_OID *oid_ret);
typedef bool_t	rpc_gss_oid_to_mech_ftype(gss_OID oid, const char **mech_ret);
typedef bool_t	rpc_gss_qop_to_num_ftype(const char *qop, const char *mech,
		    u_int *num_ret);
typedef const char **rpc_gss_get_mechanisms_ftype(void);
typedef bool_t	rpc_gss_get_versions_ftype(u_int *vers_hi, u_int *vers_lo);
typedef bool_t	rpc_gss_is_installed_ftype(const char *mech);
typedef bool_t	rpc_gss_set_svc_name_ftype(const char *principal,
		    const char *mechanism, u_int req_time, u_int program,
		    u_int version);
typedef void	rpc_gss_clear_svc_name_ftype(u_int program, u_int version);
typedef bool_t	rpc_gss_getcred_ftype(struct svc_req *req,
		    rpc_gss_rawcred_t **rcred,
		    rpc_gss_ucred_t **ucred, void **cookie);
typedef bool_t	rpc_gss_set_callback_ftype(rpc_gss_callback_t *cb);
typedef void	rpc_gss_clear_callback_ftype(rpc_gss_callback_t *cb);
typedef bool_t	rpc_gss_get_principal_name_ftype(rpc_gss_principal_t *principal,
		    const char *mech, const char *name, const char *node,
		    const char *domain);
typedef int	rpc_gss_svc_max_data_length_ftype(struct svc_req *req,
		    int max_tp_unit_len);
typedef void	rpc_gss_refresh_auth_ftype(AUTH *auth);

struct rpc_gss_entries {
	rpc_gss_secfind_ftype		*rpc_gss_secfind;
	rpc_gss_secpurge_ftype		*rpc_gss_secpurge;
	rpc_gss_seccreate_ftype		*rpc_gss_seccreate;
	rpc_gss_set_defaults_ftype	*rpc_gss_set_defaults;
	rpc_gss_max_data_length_ftype	*rpc_gss_max_data_length;
	rpc_gss_get_error_ftype		*rpc_gss_get_error;
	rpc_gss_mech_to_oid_ftype	*rpc_gss_mech_to_oid;
	rpc_gss_oid_to_mech_ftype	*rpc_gss_oid_to_mech;
	rpc_gss_qop_to_num_ftype	*rpc_gss_qop_to_num;
	rpc_gss_get_mechanisms_ftype	*rpc_gss_get_mechanisms;
	rpc_gss_get_versions_ftype	*rpc_gss_get_versions;
	rpc_gss_is_installed_ftype	*rpc_gss_is_installed;
	rpc_gss_set_svc_name_ftype	*rpc_gss_set_svc_name;
	rpc_gss_clear_svc_name_ftype	*rpc_gss_clear_svc_name;
	rpc_gss_getcred_ftype		*rpc_gss_getcred;
	rpc_gss_set_callback_ftype	*rpc_gss_set_callback;
	rpc_gss_clear_callback_ftype	*rpc_gss_clear_callback;
	rpc_gss_get_principal_name_ftype *rpc_gss_get_principal_name;
	rpc_gss_svc_max_data_length_ftype *rpc_gss_svc_max_data_length;
	rpc_gss_refresh_auth_ftype	*rpc_gss_refresh_auth;
};
extern struct rpc_gss_entries	rpc_gss_entries;

/* Functions to access the entry points. */
static __inline AUTH *
rpc_gss_secfind_call(CLIENT *clnt, struct ucred *cred, const char *principal,
    gss_OID mech_oid, rpc_gss_service_t service)
{
	AUTH *ret = NULL;

	if (rpc_gss_entries.rpc_gss_secfind != NULL)
		ret = (*rpc_gss_entries.rpc_gss_secfind)(clnt, cred, principal,
		    mech_oid, service);
	return (ret);
}

static __inline void
rpc_gss_secpurge_call(CLIENT *clnt)
{

	if (rpc_gss_entries.rpc_gss_secpurge != NULL)
		(*rpc_gss_entries.rpc_gss_secpurge)(clnt);
}

static __inline AUTH *
rpc_gss_seccreate_call(CLIENT *clnt, struct ucred *cred,
    const char *clnt_principal, const char *principal, const char *mechanism,
    rpc_gss_service_t service, const char *qop,
    rpc_gss_options_req_t *options_req, rpc_gss_options_ret_t *options_ret)
{
	AUTH *ret = NULL;

	if (rpc_gss_entries.rpc_gss_seccreate != NULL)
		ret = (*rpc_gss_entries.rpc_gss_seccreate)(clnt, cred,
		    clnt_principal, principal, mechanism, service, qop,
		    options_req, options_ret);
	return (ret);
}

static __inline bool_t
rpc_gss_set_defaults_call(AUTH *auth, rpc_gss_service_t service,
    const char *qop)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_set_defaults != NULL)
		ret = (*rpc_gss_entries.rpc_gss_set_defaults)(auth, service,
		    qop);
	return (ret);
}

static __inline int
rpc_gss_max_data_length_call(AUTH *handle, int max_tp_unit_len)
{
	int ret = 0;

	if (rpc_gss_entries.rpc_gss_max_data_length != NULL)
		ret = (*rpc_gss_entries.rpc_gss_max_data_length)(handle,
		    max_tp_unit_len);
	return (ret);
}

static __inline void
rpc_gss_get_error_call(rpc_gss_error_t *error)
{

	if (rpc_gss_entries.rpc_gss_get_error != NULL)
		(*rpc_gss_entries.rpc_gss_get_error)(error);
}

static __inline bool_t
rpc_gss_mech_to_oid_call(const char *mech, gss_OID *oid_ret)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_mech_to_oid != NULL)
		ret = (*rpc_gss_entries.rpc_gss_mech_to_oid)(mech, oid_ret);
	return (ret);
}

static __inline bool_t
rpc_gss_oid_to_mech_call(gss_OID oid, const char **mech_ret)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_oid_to_mech != NULL)
		ret = (*rpc_gss_entries.rpc_gss_oid_to_mech)(oid, mech_ret);
	return (ret);
}

static __inline bool_t
rpc_gss_qop_to_num_call(const char *qop, const char *mech, u_int *num_ret)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_qop_to_num != NULL)
		ret = (*rpc_gss_entries.rpc_gss_qop_to_num)(qop, mech, num_ret);
	return (ret);
}

static __inline const char **
rpc_gss_get_mechanisms_call(void)
{
	const char **ret = NULL;

	if (rpc_gss_entries.rpc_gss_get_mechanisms != NULL)
		ret = (*rpc_gss_entries.rpc_gss_get_mechanisms)();
	return (ret);
}

static __inline bool_t
rpc_gss_get_versions_call(u_int *vers_hi, u_int *vers_lo)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_get_versions != NULL)
		ret = (*rpc_gss_entries.rpc_gss_get_versions)(vers_hi, vers_lo);
	return (ret);
}

static __inline bool_t
rpc_gss_is_installed_call(const char *mech)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_is_installed != NULL)
		ret = (*rpc_gss_entries.rpc_gss_is_installed)(mech);
	return (ret);
}

static __inline bool_t
rpc_gss_set_svc_name_call(const char *principal, const char *mechanism,
    u_int req_time, u_int program, u_int version)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_set_svc_name != NULL)
		ret = (*rpc_gss_entries.rpc_gss_set_svc_name)(principal,
		    mechanism, req_time, program, version);
	return (ret);
}

static __inline void
rpc_gss_clear_svc_name_call(u_int program, u_int version)
{

	if (rpc_gss_entries.rpc_gss_clear_svc_name != NULL)
		(*rpc_gss_entries.rpc_gss_clear_svc_name)(program, version);
}

static __inline bool_t
rpc_gss_getcred_call(struct svc_req *req, rpc_gss_rawcred_t **rcred,
    rpc_gss_ucred_t **ucred, void **cookie)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_getcred != NULL)
		ret = (*rpc_gss_entries.rpc_gss_getcred)(req, rcred, ucred,
		    cookie);
	return (ret);
}

static __inline bool_t
rpc_gss_set_callback_call(rpc_gss_callback_t *cb)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_set_callback != NULL)
		ret = (*rpc_gss_entries.rpc_gss_set_callback)(cb);
	return (ret);
}

static __inline void
rpc_gss_clear_callback_call(rpc_gss_callback_t *cb)
{

	if (rpc_gss_entries.rpc_gss_clear_callback != NULL)
		(*rpc_gss_entries.rpc_gss_clear_callback)(cb);
}

static __inline bool_t
rpc_gss_get_principal_name_call(rpc_gss_principal_t *principal,
    const char *mech, const char *name, const char *node, const char *domain)
{
	bool_t ret = 1;

	if (rpc_gss_entries.rpc_gss_get_principal_name != NULL)
		ret = (*rpc_gss_entries.rpc_gss_get_principal_name)(principal,
		    mech, name, node, domain);
	return (ret);
}

static __inline int
rpc_gss_svc_max_data_length_call(struct svc_req *req, int max_tp_unit_len)
{
	int ret = 0;

	if (rpc_gss_entries.rpc_gss_svc_max_data_length != NULL)
		ret = (*rpc_gss_entries.rpc_gss_svc_max_data_length)(req,
		    max_tp_unit_len);
	return (ret);
}

static __inline void
rpc_gss_refresh_auth_call(AUTH *auth)
{

	if (rpc_gss_entries.rpc_gss_refresh_auth != NULL)
		(*rpc_gss_entries.rpc_gss_refresh_auth)(auth);
}

AUTH	*rpc_gss_secfind(CLIENT *clnt, struct ucred *cred,
    const char *principal, gss_OID mech_oid, rpc_gss_service_t service);
void	rpc_gss_secpurge(CLIENT *clnt);
void	rpc_gss_refresh_auth(AUTH *auth);
AUTH	*rpc_gss_seccreate(CLIENT *clnt, struct ucred *cred,
    const char *clnt_principal, const char *principal,
    const char *mechanism, rpc_gss_service_t service,
    const char *qop, rpc_gss_options_req_t *options_req,
    rpc_gss_options_ret_t *options_ret);
#else	/* !_KERNEL */
AUTH	*rpc_gss_seccreate(CLIENT *clnt, struct ucred *cred,
    const char *principal, const char *mechanism, rpc_gss_service_t service,
    const char *qop, rpc_gss_options_req_t *options_req,
    rpc_gss_options_ret_t *options_ret);
#endif	/* _KERNEL */
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
void rpc_gss_clear_svc_name(u_int program, u_int version);
bool_t	rpc_gss_getcred(struct svc_req *req, rpc_gss_rawcred_t **rcred,
    rpc_gss_ucred_t **ucred, void **cookie);
bool_t	rpc_gss_set_callback(rpc_gss_callback_t *cb);
void rpc_gss_clear_callback(rpc_gss_callback_t *cb);
bool_t	rpc_gss_get_principal_name(rpc_gss_principal_t *principal,
    const char *mech, const char *name, const char *node, const char *domain);
int	rpc_gss_svc_max_data_length(struct svc_req *req, int max_tp_unit_len);

/*
 * Internal interface from the RPC implementation.
 */
#ifndef _KERNEL
bool_t	__rpc_gss_wrap(AUTH *auth, void *header, size_t headerlen,
    XDR* xdrs, xdrproc_t xdr_args, void *args_ptr);
bool_t	__rpc_gss_unwrap(AUTH *auth, XDR* xdrs, xdrproc_t xdr_args,
    void *args_ptr);
#endif
bool_t __rpc_gss_set_error(int rpc_gss_error, int system_error);

__END_DECLS

#endif /* !_RPCSEC_GSS_H */
