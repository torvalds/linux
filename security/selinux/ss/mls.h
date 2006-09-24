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
 *      Added support to import/export the MLS label
 *
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 */

#ifndef _SS_MLS_H_
#define _SS_MLS_H_

#include "context.h"
#include "policydb.h"

/*
 * Copies the MLS range from `src' into `dst'.
 */
static inline int mls_copy_context(struct context *dst,
				   struct context *src)
{
	int l, rc = 0;

	/* Copy the MLS range from the source context */
	for (l = 0; l < 2; l++) {
		dst->range.level[l].sens = src->range.level[l].sens;
		rc = ebitmap_cpy(&dst->range.level[l].cat,
				 &src->range.level[l].cat);
		if (rc)
			break;
	}

	return rc;
}

int mls_compute_context_len(struct context *context);
void mls_sid_to_context(struct context *context, char **scontext);
int mls_context_isvalid(struct policydb *p, struct context *c);

int mls_context_to_sid(char oldc,
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

void mls_export_lvl(const struct context *context, u32 *low, u32 *high);
void mls_import_lvl(struct context *context, u32 low, u32 high);

int mls_export_cat(const struct context *context,
		   unsigned char **low,
		   size_t *low_len,
		   unsigned char **high,
		   size_t *high_len);
int mls_import_cat(struct context *context,
		   const unsigned char *low,
		   size_t low_len,
		   const unsigned char *high,
		   size_t high_len);

#endif	/* _SS_MLS_H */

