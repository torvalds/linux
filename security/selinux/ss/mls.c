// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the multi-level security (MLS) policy.
 *
 * Author : Stephen Smalley, <sds@tycho.nsa.gov>
 */
/*
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 *	Support for enhanced MLS infrastructure.
 *
 * Copyright (C) 2004-2006 Trusted Computer Solutions, Inc.
 */
/*
 * Updated: Hewlett-Packard <paul@paul-moore.com>
 *
 *      Added support to import/export the MLS label from NetLabel
 *
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <net/netlabel.h>
#include "sidtab.h"
#include "mls.h"
#include "policydb.h"
#include "services.h"

/*
 * Return the length in bytes for the MLS fields of the
 * security context string representation of `context'.
 */
int mls_compute_context_len(struct policydb *p, struct context *context)
{
	int i, l, len, head, prev;
	char *nm;
	struct ebitmap *e;
	struct ebitmap_node *node;

	if (!p->mls_enabled)
		return 0;

	len = 1; /* for the beginning ":" */
	for (l = 0; l < 2; l++) {
		int index_sens = context->range.level[l].sens;
		len += strlen(sym_name(p, SYM_LEVELS, index_sens - 1));

		/* categories */
		head = -2;
		prev = -2;
		e = &context->range.level[l].cat;
		ebitmap_for_each_positive_bit(e, node, i) {
			if (i - prev > 1) {
				/* one or more negative bits are skipped */
				if (head != prev) {
					nm = sym_name(p, SYM_CATS, prev);
					len += strlen(nm) + 1;
				}
				nm = sym_name(p, SYM_CATS, i);
				len += strlen(nm) + 1;
				head = i;
			}
			prev = i;
		}
		if (prev != head) {
			nm = sym_name(p, SYM_CATS, prev);
			len += strlen(nm) + 1;
		}
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
void mls_sid_to_context(struct policydb *p,
			struct context *context,
			char **scontext)
{
	char *scontextp, *nm;
	int i, l, head, prev;
	struct ebitmap *e;
	struct ebitmap_node *node;

	if (!p->mls_enabled)
		return;

	scontextp = *scontext;

	*scontextp = ':';
	scontextp++;

	for (l = 0; l < 2; l++) {
		strcpy(scontextp, sym_name(p, SYM_LEVELS,
					   context->range.level[l].sens - 1));
		scontextp += strlen(scontextp);

		/* categories */
		head = -2;
		prev = -2;
		e = &context->range.level[l].cat;
		ebitmap_for_each_positive_bit(e, node, i) {
			if (i - prev > 1) {
				/* one or more negative bits are skipped */
				if (prev != head) {
					if (prev - head > 1)
						*scontextp++ = '.';
					else
						*scontextp++ = ',';
					nm = sym_name(p, SYM_CATS, prev);
					strcpy(scontextp, nm);
					scontextp += strlen(nm);
				}
				if (prev < 0)
					*scontextp++ = ':';
				else
					*scontextp++ = ',';
				nm = sym_name(p, SYM_CATS, i);
				strcpy(scontextp, nm);
				scontextp += strlen(nm);
				head = i;
			}
			prev = i;
		}

		if (prev != head) {
			if (prev - head > 1)
				*scontextp++ = '.';
			else
				*scontextp++ = ',';
			nm = sym_name(p, SYM_CATS, prev);
			strcpy(scontextp, nm);
			scontextp += strlen(nm);
		}

		if (l == 0) {
			if (mls_level_eq(&context->range.level[0],
					 &context->range.level[1]))
				break;
			else
				*scontextp++ = '-';
		}
	}

	*scontext = scontextp;
	return;
}

int mls_level_isvalid(struct policydb *p, struct mls_level *l)
{
	struct level_datum *levdatum;

	if (!l->sens || l->sens > p->p_levels.nprim)
		return 0;
	levdatum = hashtab_search(p->p_levels.table,
				  sym_name(p, SYM_LEVELS, l->sens - 1));
	if (!levdatum)
		return 0;

	/*
	 * Return 1 iff all the bits set in l->cat are also be set in
	 * levdatum->level->cat and no bit in l->cat is larger than
	 * p->p_cats.nprim.
	 */
	return ebitmap_contains(&levdatum->level->cat, &l->cat,
				p->p_cats.nprim);
}

int mls_range_isvalid(struct policydb *p, struct mls_range *r)
{
	return (mls_level_isvalid(p, &r->level[0]) &&
		mls_level_isvalid(p, &r->level[1]) &&
		mls_level_dom(&r->level[1], &r->level[0]));
}

/*
 * Return 1 if the MLS fields in the security context
 * structure `c' are valid.  Return 0 otherwise.
 */
int mls_context_isvalid(struct policydb *p, struct context *c)
{
	struct user_datum *usrdatum;

	if (!p->mls_enabled)
		return 1;

	if (!mls_range_isvalid(p, &c->range))
		return 0;

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
 * Set the MLS fields in the security context structure
 * `context' based on the string representation in
 * the string `scontext'.
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
int mls_context_to_sid(struct policydb *pol,
		       char oldc,
		       char *scontext,
		       struct context *context,
		       struct sidtab *s,
		       u32 def_sid)
{
	char *sensitivity, *cur_cat, *next_cat, *rngptr;
	struct level_datum *levdatum;
	struct cat_datum *catdatum, *rngdatum;
	int l, rc, i;
	char *rangep[2];

	if (!pol->mls_enabled) {
		/*
		 * With no MLS, only return -EINVAL if there is a MLS field
		 * and it did not come from an xattr.
		 */
		if (oldc && def_sid == SECSID_NULL)
			return -EINVAL;
		return 0;
	}

	/*
	 * No MLS component to the security context, try and map to
	 * default if provided.
	 */
	if (!oldc) {
		struct context *defcon;

		if (def_sid == SECSID_NULL)
			return -EINVAL;

		defcon = sidtab_search(s, def_sid);
		if (!defcon)
			return -EINVAL;

		return mls_context_cpy(context, defcon);
	}

	/*
	 * If we're dealing with a range, figure out where the two parts
	 * of the range begin.
	 */
	rangep[0] = scontext;
	rangep[1] = strchr(scontext, '-');
	if (rangep[1]) {
		rangep[1][0] = '\0';
		rangep[1]++;
	}

	/* For each part of the range: */
	for (l = 0; l < 2; l++) {
		/* Split sensitivity and category set. */
		sensitivity = rangep[l];
		if (sensitivity == NULL)
			break;
		next_cat = strchr(sensitivity, ':');
		if (next_cat)
			*(next_cat++) = '\0';

		/* Parse sensitivity. */
		levdatum = hashtab_search(pol->p_levels.table, sensitivity);
		if (!levdatum)
			return -EINVAL;
		context->range.level[l].sens = levdatum->level->sens;

		/* Extract category set. */
		while (next_cat != NULL) {
			cur_cat = next_cat;
			next_cat = strchr(next_cat, ',');
			if (next_cat != NULL)
				*(next_cat++) = '\0';

			/* Separate into range if exists */
			rngptr = strchr(cur_cat, '.');
			if (rngptr != NULL) {
				/* Remove '.' */
				*rngptr++ = '\0';
			}

			catdatum = hashtab_search(pol->p_cats.table, cur_cat);
			if (!catdatum)
				return -EINVAL;

			rc = ebitmap_set_bit(&context->range.level[l].cat,
					     catdatum->value - 1, 1);
			if (rc)
				return rc;

			/* If range, set all categories in range */
			if (rngptr == NULL)
				continue;

			rngdatum = hashtab_search(pol->p_cats.table, rngptr);
			if (!rngdatum)
				return -EINVAL;

			if (catdatum->value >= rngdatum->value)
				return -EINVAL;

			for (i = catdatum->value; i < rngdatum->value; i++) {
				rc = ebitmap_set_bit(&context->range.level[l].cat, i, 1);
				if (rc)
					return rc;
			}
		}
	}

	/* If we didn't see a '-', the range start is also the range end. */
	if (rangep[1] == NULL) {
		context->range.level[1].sens = context->range.level[0].sens;
		rc = ebitmap_cpy(&context->range.level[1].cat,
				 &context->range.level[0].cat);
		if (rc)
			return rc;
	}

	return 0;
}

/*
 * Set the MLS fields in the security context structure
 * `context' based on the string representation in
 * the string `str'.  This function will allocate temporary memory with the
 * given constraints of gfp_mask.
 */
int mls_from_string(struct policydb *p, char *str, struct context *context,
		    gfp_t gfp_mask)
{
	char *tmpstr;
	int rc;

	if (!p->mls_enabled)
		return -EINVAL;

	tmpstr = kstrdup(str, gfp_mask);
	if (!tmpstr) {
		rc = -ENOMEM;
	} else {
		rc = mls_context_to_sid(p, ':', tmpstr, context,
					NULL, SECSID_NULL);
		kfree(tmpstr);
	}

	return rc;
}

/*
 * Copies the MLS range `range' into `context'.
 */
int mls_range_set(struct context *context,
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

int mls_setup_user_range(struct policydb *p,
			 struct context *fromcon, struct user_datum *user,
			 struct context *usercon)
{
	if (p->mls_enabled) {
		struct mls_level *fromcon_sen = &(fromcon->range.level[0]);
		struct mls_level *fromcon_clr = &(fromcon->range.level[1]);
		struct mls_level *user_low = &(user->range.level[0]);
		struct mls_level *user_clr = &(user->range.level[1]);
		struct mls_level *user_def = &(user->dfltlevel);
		struct mls_level *usercon_sen = &(usercon->range.level[0]);
		struct mls_level *usercon_clr = &(usercon->range.level[1]);

		/* Honor the user's default level if we can */
		if (mls_level_between(user_def, fromcon_sen, fromcon_clr))
			*usercon_sen = *user_def;
		else if (mls_level_between(fromcon_sen, user_def, user_clr))
			*usercon_sen = *fromcon_sen;
		else if (mls_level_between(fromcon_clr, user_low, user_def))
			*usercon_sen = *user_low;
		else
			return -EINVAL;

		/* Lower the clearance of available contexts
		   if the clearance of "fromcon" is lower than
		   that of the user's default clearance (but
		   only if the "fromcon" clearance dominates
		   the user's computed sensitivity level) */
		if (mls_level_dom(user_clr, fromcon_clr))
			*usercon_clr = *fromcon_clr;
		else if (mls_level_dom(fromcon_clr, user_clr))
			*usercon_clr = *user_clr;
		else
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

	if (!oldp->mls_enabled || !newp->mls_enabled)
		return 0;

	for (l = 0; l < 2; l++) {
		levdatum = hashtab_search(newp->p_levels.table,
					  sym_name(oldp, SYM_LEVELS,
						   c->range.level[l].sens - 1));

		if (!levdatum)
			return -EINVAL;
		c->range.level[l].sens = levdatum->level->sens;

		ebitmap_init(&bitmap);
		ebitmap_for_each_positive_bit(&c->range.level[l].cat, node, i) {
			int rc;

			catdatum = hashtab_search(newp->p_cats.table,
						  sym_name(oldp, SYM_CATS, i));
			if (!catdatum)
				return -EINVAL;
			rc = ebitmap_set_bit(&bitmap, catdatum->value - 1, 1);
			if (rc)
				return rc;

			cond_resched();
		}
		ebitmap_destroy(&c->range.level[l].cat);
		c->range.level[l].cat = bitmap;
	}

	return 0;
}

int mls_compute_sid(struct policydb *p,
		    struct context *scontext,
		    struct context *tcontext,
		    u16 tclass,
		    u32 specified,
		    struct context *newcontext,
		    bool sock)
{
	struct range_trans rtr;
	struct mls_range *r;
	struct class_datum *cladatum;
	int default_range = 0;

	if (!p->mls_enabled)
		return 0;

	switch (specified) {
	case AVTAB_TRANSITION:
		/* Look for a range transition rule. */
		rtr.source_type = scontext->type;
		rtr.target_type = tcontext->type;
		rtr.target_class = tclass;
		r = hashtab_search(p->range_tr, &rtr);
		if (r)
			return mls_range_set(newcontext, r);

		if (tclass && tclass <= p->p_classes.nprim) {
			cladatum = p->class_val_to_struct[tclass - 1];
			if (cladatum)
				default_range = cladatum->default_range;
		}

		switch (default_range) {
		case DEFAULT_SOURCE_LOW:
			return mls_context_cpy_low(newcontext, scontext);
		case DEFAULT_SOURCE_HIGH:
			return mls_context_cpy_high(newcontext, scontext);
		case DEFAULT_SOURCE_LOW_HIGH:
			return mls_context_cpy(newcontext, scontext);
		case DEFAULT_TARGET_LOW:
			return mls_context_cpy_low(newcontext, tcontext);
		case DEFAULT_TARGET_HIGH:
			return mls_context_cpy_high(newcontext, tcontext);
		case DEFAULT_TARGET_LOW_HIGH:
			return mls_context_cpy(newcontext, tcontext);
		}

		/* Fallthrough */
	case AVTAB_CHANGE:
		if ((tclass == p->process_class) || (sock == true))
			/* Use the process MLS attributes. */
			return mls_context_cpy(newcontext, scontext);
		else
			/* Use the process effective MLS attributes. */
			return mls_context_cpy_low(newcontext, scontext);
	case AVTAB_MEMBER:
		/* Use the process effective MLS attributes. */
		return mls_context_cpy_low(newcontext, scontext);

	/* fall through */
	}
	return -EINVAL;
}

#ifdef CONFIG_NETLABEL
/**
 * mls_export_netlbl_lvl - Export the MLS sensitivity levels to NetLabel
 * @context: the security context
 * @secattr: the NetLabel security attributes
 *
 * Description:
 * Given the security context copy the low MLS sensitivity level into the
 * NetLabel MLS sensitivity level field.
 *
 */
void mls_export_netlbl_lvl(struct policydb *p,
			   struct context *context,
			   struct netlbl_lsm_secattr *secattr)
{
	if (!p->mls_enabled)
		return;

	secattr->attr.mls.lvl = context->range.level[0].sens - 1;
	secattr->flags |= NETLBL_SECATTR_MLS_LVL;
}

/**
 * mls_import_netlbl_lvl - Import the NetLabel MLS sensitivity levels
 * @context: the security context
 * @secattr: the NetLabel security attributes
 *
 * Description:
 * Given the security context and the NetLabel security attributes, copy the
 * NetLabel MLS sensitivity level into the context.
 *
 */
void mls_import_netlbl_lvl(struct policydb *p,
			   struct context *context,
			   struct netlbl_lsm_secattr *secattr)
{
	if (!p->mls_enabled)
		return;

	context->range.level[0].sens = secattr->attr.mls.lvl + 1;
	context->range.level[1].sens = context->range.level[0].sens;
}

/**
 * mls_export_netlbl_cat - Export the MLS categories to NetLabel
 * @context: the security context
 * @secattr: the NetLabel security attributes
 *
 * Description:
 * Given the security context copy the low MLS categories into the NetLabel
 * MLS category field.  Returns zero on success, negative values on failure.
 *
 */
int mls_export_netlbl_cat(struct policydb *p,
			  struct context *context,
			  struct netlbl_lsm_secattr *secattr)
{
	int rc;

	if (!p->mls_enabled)
		return 0;

	rc = ebitmap_netlbl_export(&context->range.level[0].cat,
				   &secattr->attr.mls.cat);
	if (rc == 0 && secattr->attr.mls.cat != NULL)
		secattr->flags |= NETLBL_SECATTR_MLS_CAT;

	return rc;
}

/**
 * mls_import_netlbl_cat - Import the MLS categories from NetLabel
 * @context: the security context
 * @secattr: the NetLabel security attributes
 *
 * Description:
 * Copy the NetLabel security attributes into the SELinux context; since the
 * NetLabel security attribute only contains a single MLS category use it for
 * both the low and high categories of the context.  Returns zero on success,
 * negative values on failure.
 *
 */
int mls_import_netlbl_cat(struct policydb *p,
			  struct context *context,
			  struct netlbl_lsm_secattr *secattr)
{
	int rc;

	if (!p->mls_enabled)
		return 0;

	rc = ebitmap_netlbl_import(&context->range.level[0].cat,
				   secattr->attr.mls.cat);
	if (rc)
		goto import_netlbl_cat_failure;
	memcpy(&context->range.level[1].cat, &context->range.level[0].cat,
	       sizeof(context->range.level[0].cat));

	return 0;

import_netlbl_cat_failure:
	ebitmap_destroy(&context->range.level[0].cat);
	return rc;
}
#endif /* CONFIG_NETLABEL */
