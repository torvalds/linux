/*
 * linux/include/linux/gss_api.h
 *
 * Somewhat simplified version of the gss api.
 *
 * Dug Song <dugsong@monkey.org>
 * Andy Adamson <andros@umich.edu>
 * Bruce Fields <bfields@umich.edu>
 * Copyright (c) 2000 The Regents of the University of Michigan
 *
 * $Id$
 */

#ifndef _LINUX_SUNRPC_GSS_API_H
#define _LINUX_SUNRPC_GSS_API_H

#ifdef __KERNEL__
#include <linux/sunrpc/xdr.h>
#include <linux/uio.h>

/* The mechanism-independent gss-api context: */
struct gss_ctx {
	struct gss_api_mech	*mech_type;
	void			*internal_ctx_id;
};

#define GSS_C_NO_BUFFER		((struct xdr_netobj) 0)
#define GSS_C_NO_CONTEXT	((struct gss_ctx *) 0)
#define GSS_C_NULL_OID		((struct xdr_netobj) 0)

/*XXX  arbitrary length - is this set somewhere? */
#define GSS_OID_MAX_LEN 32

/* gss-api prototypes; note that these are somewhat simplified versions of
 * the prototypes specified in RFC 2744. */
int gss_import_sec_context(
		const void*		input_token,
		size_t			bufsize,
		struct gss_api_mech	*mech,
		struct gss_ctx		**ctx_id);
u32 gss_get_mic(
		struct gss_ctx		*ctx_id,
		u32			qop,
		struct xdr_buf		*message,
		struct xdr_netobj	*mic_token);
u32 gss_verify_mic(
		struct gss_ctx		*ctx_id,
		struct xdr_buf		*message,
		struct xdr_netobj	*mic_token,
		u32			*qstate);
u32 gss_delete_sec_context(
		struct gss_ctx		**ctx_id);

u32 gss_pseudoflavor_to_service(struct gss_api_mech *, u32 pseudoflavor);
char *gss_service_to_auth_domain_name(struct gss_api_mech *, u32 service);

struct pf_desc {
	u32	pseudoflavor;
	u32	qop;
	u32	service;
	char	*name;
	char	*auth_domain_name;
};

/* Different mechanisms (e.g., krb5 or spkm3) may implement gss-api, and
 * mechanisms may be dynamically registered or unregistered by modules. */

/* Each mechanism is described by the following struct: */
struct gss_api_mech {
	struct list_head	gm_list;
	struct module		*gm_owner;
	struct xdr_netobj	gm_oid;
	char			*gm_name;
	struct gss_api_ops	*gm_ops;
	/* pseudoflavors supported by this mechanism: */
	int			gm_pf_num;
	struct pf_desc *	gm_pfs;
};

/* and must provide the following operations: */
struct gss_api_ops {
	int (*gss_import_sec_context)(
			const void		*input_token,
			size_t			bufsize,
			struct gss_ctx		*ctx_id);
	u32 (*gss_get_mic)(
			struct gss_ctx		*ctx_id,
			u32			qop, 
			struct xdr_buf		*message,
			struct xdr_netobj	*mic_token);
	u32 (*gss_verify_mic)(
			struct gss_ctx		*ctx_id,
			struct xdr_buf		*message,
			struct xdr_netobj	*mic_token,
			u32			*qstate);
	void (*gss_delete_sec_context)(
			void			*internal_ctx_id);
};

int gss_mech_register(struct gss_api_mech *);
void gss_mech_unregister(struct gss_api_mech *);

/* returns a mechanism descriptor given an OID, and increments the mechanism's
 * reference count. */
struct gss_api_mech * gss_mech_get_by_OID(struct xdr_netobj *);

/* Returns a reference to a mechanism, given a name like "krb5" etc. */
struct gss_api_mech *gss_mech_get_by_name(const char *);

/* Similar, but get by pseudoflavor. */
struct gss_api_mech *gss_mech_get_by_pseudoflavor(u32);

/* Just increments the mechanism's reference count and returns its input: */
struct gss_api_mech * gss_mech_get(struct gss_api_mech *);

/* For every succesful gss_mech_get or gss_mech_get_by_* call there must be a
 * corresponding call to gss_mech_put. */
void gss_mech_put(struct gss_api_mech *);

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_GSS_API_H */

