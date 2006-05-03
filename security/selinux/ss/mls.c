/*
 * Implementation of the multi-level security (MLS) policy.
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "sidtab.h"
#include "mls.h"
#include "policydb.h"
#include "services.h"

/*
 * Return the length in bytes for the MLS fields of the
 * security context string representation of `context'.
 */
int mls_compute_context_len(struct context * context)
{
	int i, l, len, range;
	struct ebitmap_node *node;

	if (!selinux_mls_enabled)
		return 0;

	len = 1; /* for the beginning ":" */
	for (l = 0; l < 2; l++) {
		range = 0;
		len += strlen(policydb.p_sens_val_to_name[context->range.level[l].sens - 1]);

		ebitmap_for_each_bit(&context->range.level[l].cat, node, i) {
			if (ebitmap_node_get_bit(node, i)) {
				if (range) {
					range++;
					continue;
				}

				len += strlen(policydb.p_cat_val_to_name[i]) + 1;
				range++;
			} else {
				if (range > 1)
					len += strlen(policydb.p_cat_val_to_name[i - 1]) + 1;
				range = 0;
			}
		}
		/* Handle case where last category is the end of range */
		if (range > 1)
			len += strlen(policydb.p_cat_val_to_name[i - 1]) + 1;

		if (l == 0) {
			if (mls_level_eq(&context->range.level[0],
			                 &context->range.level[1]))
				break;
			else
				len++;
		}
	}

	return len;
}

/*
 * Write the security context string representation of
 * the MLS fields of `context' into the string `*scontext'.
 * Update `*scontext' to point to the end of the MLS fields.
 */
void mls_sid_to_context(struct context *context,
                        char **scontext)
{
	char *scontextp;
	int i, l, range, wrote_sep;
	struct ebitmap_node *node;

	if (!selinux_mls_enabled)
		return;

	scontextp = *scontext;

	*scontextp = ':';
	scontextp++;

	for (l = 0; l < 2; l++) {
		range = 0;
		wrote_sep = 0;
		strcpy(scontextp,
		       policydb.p_sens_val_to_name[context->range.level[l].sens - 1]);
		scontextp += strlen(policydb.p_sens_val_to_name[context->range.level[l].sens - 1]);

		/* categories */
		ebitmap_for_each_bit(&context->range.level[l].cat, node, i) {
			if (ebitmap_node_get_bit(node, i)) {
				if (range) {
					range++;
					continue;
				}

				if (!wrote_sep) {
					*scontextp++ = ':';
					wrote_sep = 1;
				} else
					*scontextp++ = ',';
				strcpy(scontextp, policydb.p_cat_val_to_name[i]);
				scontextp += strlen(policydb.p_cat_val_to_name[i]);
				range++;
			} else {
				if (range > 1) {
					if (range > 2)
						*scontextp++ = '.';
					else
						*scontextp++ = ',';

					strcpy(scontextp, policydb.p_cat_val_to_name[i - 1]);
					scontextp += strlen(policydb.p_cat_val_to_name[i - 1]);
				}
				range = 0;
			}
		}

		/* Handle case where last category is the end of range */
		if (range > 1) {
			if (range > 2)
				*scontextp++ = '.';
			else
				*scontextp++ = ',';

			strcpy(scontextp, policydb.p_cat_val_to_name[i - 1]);
			scontextp += strlen(policydb.p_cat_val_to_name[i - 1]);
		}

		if (l == 0) {
			if (mls_level_eq(&context->range.level[0],
			                 &context->range.level[1]))
				break;
			else {
				*scontextp = '-';
				scontextp++;
			}
		}
	}

	*scontext = scontextp;
	return;
}

/*
 * Return 1 if the MLS fields in the security context
 * structure `c' are valid.  Return 0 otherwise.
 */
int mls_context_isvalid(struct policydb *p, struct context *c)
{
	struct level_datum *levdatum;
	struct user_datum *usrdatum;
	struct ebitmap_node *node;
	int i, l;

	if (!selinux_mls_enabled)
		return 1;

	/*
	 * MLS range validity checks: high must dominate low, low level must
	 * be valid (category set <-> sensitivity check), and high level must
	 * be valid (category set <-> sensitivity check)
	 */
	if (!mls_level_dom(&c->range.level[1], &c->range.level[0]))
		/* High does not dominate low. */
		return 0;

	for (l = 0; l < 2; l++) {
		if (!c->range.level[l].sens || c->range.level[l].sens > p->p_levels.nprim)
			return 0;
		levdatum = hashtab_search(p->p_levels.table,
			p->p_sens_val_to_name[c->range.level[l].sens - 1]);
		if (!levdatum)
			return 0;

		ebitmap_for_each_bit(&c->range.level[l].cat, node, i) {
			if (ebitmap_node_get_bit(node, i)) {
				if (i > p->p_cats.nprim)
					return 0;
				if (!ebitmap_get_bit(&levdatum->level->cat, i))
					/*
					 * Category may not be associated with
					 * sensitivity in low level.
					 */
					return 0;
			}
		}
	}

	if (c->role == OBJECT_R_VAL)
		return 1;

	/*
	 * User must be authorized for the MLS range.
	 */
	if (!c->user || c->user > p->p_users.nprim)
		return 0;
	usrdatum = p->user_val_to_struct[c->user - 1];
	if (!mls_range_contains(usrdatum->range, c->range))
		return 0; /* user may not be associated with range */

	return 1;
}

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

/*
 * Set the MLS fields in the security context structure
 * `context' based on the string representation in
 * the string `*scontext'.  Update `*scontext' to
 * point to the end of the string representation of
 * the MLS fields.
 *
 * This function modifies the string in place, inserting
 * NULL characters to terminate the MLS fields.
 *
 * If a def_sid is provided and no MLS field is present,
 * copy the MLS field of the associated default context.
 * Used for upgraded to MLS systems where objects may lack
 * MLS fields.
 *
 * Policy read-lock must be held for sidtab lookup.
 *
 */
int mls_context_to_sid(char oldc,
		       char **scontext,
		       struct context *context,
		       struct sidtab *s,
		       u32 def_sid)
{

	char delim;
	char *scontextp, *p, *rngptr;
	struct level_datum *levdatum;
	struct cat_datum *catdatum, *rngdatum;
	int l, rc = -EINVAL;

	if (!selinux_mls_enabled) {
		if (def_sid != SECSID_NULL && oldc)
			*scontext += strlen(*scontext)+1;
		return 0;
	}

	/*
	 * No MLS component to the security context, try and map to
	 * default if provided.
	 */
	if (!oldc) {
		struct context *defcon;

		if (def_sid == SECSID_NULL)
			goto out;

		defcon = sidtab_search(s, def_sid);
		if (!defcon)
			goto out;

		rc = mls_copy_context(context, defcon);
		goto out;
	}

	/* Extract low sensitivity. */
	scontextp = p = *scontext;
	while (*p && *p != ':' && *p != '-')
		p++;

	delim = *p;
	if (delim != 0)
		*p++ = 0;

	for (l = 0; l < 2; l++) {
		levdatum = hashtab_search(policydb.p_levels.table, scontextp);
		if (!levdatum) {
			rc = -EINVAL;
			goto out;
		}

		context->range.level[l].sens = levdatum->level->sens;

		if (delim == ':') {
			/* Extract category set. */
			while (1) {
				scontextp = p;
				while (*p && *p != ',' && *p != '-')
					p++;
				delim = *p;
				if (delim != 0)
					*p++ = 0;

				/* Separate into range if exists */
				if ((rngptr = strchr(scontextp, '.')) != NULL) {
					/* Remove '.' */
					*rngptr++ = 0;
				}

				catdatum = hashtab_search(policydb.p_cats.table,
				                          scontextp);
				if (!catdatum) {
					rc = -EINVAL;
					goto out;
				}

				rc = ebitmap_set_bit(&context->range.level[l].cat,
				                     catdatum->value - 1, 1);
				if (rc)
					goto out;

				/* If range, set all categories in range */
				if (rngptr) {
					int i;

					rngdatum = hashtab_search(policydb.p_cats.table, rngptr);
					if (!rngdatum) {
						rc = -EINVAL;
						goto out;
					}

					if (catdatum->value >= rngdatum->value) {
						rc = -EINVAL;
						goto out;
					}

					for (i = catdatum->value; i < rngdatum->value; i++) {
						rc = ebitmap_set_bit(&context->range.level[l].cat, i, 1);
						if (rc)
							goto out;
					}
				}

				if (delim != ',')
					break;
			}
		}
		if (delim == '-') {
			/* Extract high sensitivity. */
			scontextp = p;
			while (*p && *p != ':')
				p++;

			delim = *p;
			if (delim != 0)
				*p++ = 0;
		} else
			break;
	}

	if (l == 0) {
		context->range.level[1].sens = context->range.level[0].sens;
		rc = ebitmap_cpy(&context->range.level[1].cat,
				 &context->range.level[0].cat);
		if (rc)
			goto out;
	}
	*scontext = ++p;
	rc = 0;
out:
	return rc;
}

/*
 * Set the MLS fields in the security context structure
 * `context' based on the string representation in
 * the string `str'.  This function will allocate temporary memory with the
 * given constraints of gfp_mask.
 */
int mls_from_string(char *str, struct context *context, gfp_t gfp_mask)
{
	char *tmpstr, *freestr;
	int rc;

	if (!selinux_mls_enabled)
		return -EINVAL;

	/* we need freestr because mls_context_to_sid will change
	   the value of tmpstr */
	tmpstr = freestr = kstrdup(str, gfp_mask);
	if (!tmpstr) {
		rc = -ENOMEM;
	} else {
		rc = mls_context_to_sid(':', &tmpstr, context,
		                        NULL, SECSID_NULL);
		kfree(freestr);
	}

	return rc;
}

/*
 * Copies the effective MLS range from `src' into `dst'.
 */
static inline int mls_scopy_context(struct context *dst,
                                    struct context *src)
{
	int l, rc = 0;

	/* Copy the MLS range from the source context */
	for (l = 0; l < 2; l++) {
		dst->range.level[l].sens = src->range.level[0].sens;
		rc = ebitmap_cpy(&dst->range.level[l].cat,
				 &src->range.level[0].cat);
		if (rc)
			break;
	}

	return rc;
}

/*
 * Copies the MLS range `range' into `context'.
 */
static inline int mls_range_set(struct context *context,
                                struct mls_range *range)
{
	int l, rc = 0;

	/* Copy the MLS range into the  context */
	for (l = 0; l < 2; l++) {
		context->range.level[l].sens = range->level[l].sens;
		rc = ebitmap_cpy(&context->range.level[l].cat,
				 &range->level[l].cat);
		if (rc)
			break;
	}

	return rc;
}

int mls_setup_user_range(struct context *fromcon, struct user_datum *user,
                         struct context *usercon)
{
	if (selinux_mls_enabled) {
		struct mls_level *fromcon_sen = &(fromcon->range.level[0]);
		struct mls_level *fromcon_clr = &(fromcon->range.level[1]);
		struct mls_level *user_low = &(user->range.level[0]);
		struct mls_level *user_clr = &(user->range.level[1]);
		struct mls_level *user_def = &(user->dfltlevel);
		struct mls_level *usercon_sen = &(usercon->range.level[0]);
		struct mls_level *usercon_clr = &(usercon->range.level[1]);

		/* Honor the user's default level if we can */
		if (mls_level_between(user_def, fromcon_sen, fromcon_clr)) {
			*usercon_sen = *user_def;
		} else if (mls_level_between(fromcon_sen, user_def, user_clr)) {
			*usercon_sen = *fromcon_sen;
		} else if (mls_level_between(fromcon_clr, user_low, user_def)) {
			*usercon_sen = *user_low;
		} else
			return -EINVAL;

		/* Lower the clearance of available contexts
		   if the clearance of "fromcon" is lower than
		   that of the user's default clearance (but
		   only if the "fromcon" clearance dominates
		   the user's computed sensitivity level) */
		if (mls_level_dom(user_clr, fromcon_clr)) {
			*usercon_clr = *fromcon_clr;
		} else if (mls_level_dom(fromcon_clr, user_clr)) {
			*usercon_clr = *user_clr;
		} else
			return -EINVAL;
	}

	return 0;
}

/*
 * Convert the MLS fields in the security context
 * structure `c' from the values specified in the
 * policy `oldp' to the values specified in the policy `newp'.
 */
int mls_convert_context(struct policydb *oldp,
			struct policydb *newp,
			struct context *c)
{
	struct level_datum *levdatum;
	struct cat_datum *catdatum;
	struct ebitmap bitmap;
	struct ebitmap_node *node;
	int l, i;

	if (!selinux_mls_enabled)
		return 0;

	for (l = 0; l < 2; l++) {
		levdatum = hashtab_search(newp->p_levels.table,
			oldp->p_sens_val_to_name[c->range.level[l].sens - 1]);

		if (!levdatum)
			return -EINVAL;
		c->range.level[l].sens = levdatum->level->sens;

		ebitmap_init(&bitmap);
		ebitmap_for_each_bit(&c->range.level[l].cat, node, i) {
			if (ebitmap_node_get_bit(node, i)) {
				int rc;

				catdatum = hashtab_search(newp->p_cats.table,
				         	oldp->p_cat_val_to_name[i]);
				if (!catdatum)
					return -EINVAL;
				rc = ebitmap_set_bit(&bitmap, catdatum->value - 1, 1);
				if (rc)
					return rc;
			}
		}
		ebitmap_destroy(&c->range.level[l].cat);
		c->range.level[l].cat = bitmap;
	}

	return 0;
}

int mls_compute_sid(struct context *scontext,
		    struct context *tcontext,
		    u16 tclass,
		    u32 specified,
		    struct context *newcontext)
{
	if (!selinux_mls_enabled)
		return 0;

	switch (specified) {
	case AVTAB_TRANSITION:
		if (tclass == SECCLASS_PROCESS) {
			struct range_trans *rangetr;
			/* Look for a range transition rule. */
			for (rangetr = policydb.range_tr; rangetr;
			     rangetr = rangetr->next) {
				if (rangetr->dom == scontext->type &&
				    rangetr->type == tcontext->type) {
					/* Set the range from the rule */
					return mls_range_set(newcontext,
					                     &rangetr->range);
				}
			}
		}
		/* Fallthrough */
	case AVTAB_CHANGE:
		if (tclass == SECCLASS_PROCESS)
			/* Use the process MLS attributes. */
			return mls_copy_context(newcontext, scontext);
		else
			/* Use the process effective MLS attributes. */
			return mls_scopy_context(newcontext, scontext);
	case AVTAB_MEMBER:
		/* Only polyinstantiate the MLS attributes if
		   the type is being polyinstantiated */
		if (newcontext->type != tcontext->type) {
			/* Use the process effective MLS attributes. */
			return mls_scopy_context(newcontext, scontext);
		} else {
			/* Use the related object MLS attributes. */
			return mls_copy_context(newcontext, tcontext);
		}
	default:
		return -EINVAL;
	}
	return -EINVAL;
}

