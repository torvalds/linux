/*
 * Multi-level security (MLS) policy operations.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
/*
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 *	Support for enhanced MLS infrastructure.
 *
 * Copyright (C) 2004-2006 Trusted Computer Solutions, Inc.
 */
/*
 * Updated: Hewlett-Packard <paul.moore@hp.com>
 *
 *	Added support to import/export the MLS label from NetLabel
 *
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 */

#ifndef _SS_MLS_H_
#define _SS_MLS_H_

#include "context.h"
#include "policydb.h"

int mls_compute_context_len(struct context *context);
void mls_sid_to_context(struct context *context, char **scontext);
int mls_context_isvalid(struct policydb *p, struct context *c);
int mls_range_isvalid(struct policydb *p, struct mls_range *r);
int mls_level_isvalid(struct policydb *p, struct mls_level *l);

int mls_context_to_sid(struct policydb *p,
		       char oldc,
		       char **scontext,
		       struct context *context,
		       struct sidtab *s,
		       u32 def_sid);

int mls_from_string(char *str, struct context *context, gfp_t gfp_mask);

int mls_convert_context(struct policydb *oldp,
			struct policydb *newp,
			struct context *context);

int mls_compute_sid(struct context *scontext,
		    struct context *tcontext,
		    u16 tclass,
		    u32 specified,
		    struct context *newcontext);

int mls_setup_user_range(struct context *fromcon, struct user_datum *user,
			 struct context *usercon);

#ifdef CONFIG_NETLABEL
void mls_export_netlbl_lvl(struct context *context,
			   struct netlbl_lsm_secattr *secattr);
void mls_import_netlbl_lvl(struct context *context,
			   struct netlbl_lsm_secattr *secattr);
int mls_export_netlbl_cat(struct context *context,
			  struct netlbl_lsm_secattr *secattr);
int mls_import_netlbl_cat(struct context *context,
			  struct netlbl_lsm_secattr *secattr);
#else
static inline void mls_export_netlbl_lvl(struct context *context,
					 struct netlbl_lsm_secattr *secattr)
{
	return;
}
static inline void mls_import_netlbl_lvl(struct context *context,
					 struct netlbl_lsm_secattr *secattr)
{
	return;
}
static inline int mls_export_netlbl_cat(struct context *context,
					struct netlbl_lsm_secattr *secattr)
{
	return -ENOMEM;
}
static inline int mls_import_netlbl_cat(struct context *context,
					struct netlbl_lsm_secattr *secattr)
{
	return -ENOMEM;
}
#endif

#endif	/* _SS_MLS_H */

