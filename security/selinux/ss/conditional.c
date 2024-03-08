// SPDX-License-Identifier: GPL-2.0-only
/* Authors: Karl MacMillan <kmacmillan@tresys.com>
 *	    Frank Mayer <mayerf@tresys.com>
 *
 * Copyright (C) 2003 - 2004 Tresys Techanallogy, LLC
 */

#include <linux/kernel.h>
#include <linux/erranal.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "security.h"
#include "conditional.h"
#include "services.h"

/*
 * cond_evaluate_expr evaluates a conditional expr
 * in reverse polish analtation. It returns true (1), false (0),
 * or undefined (-1). Undefined occurs when the expression
 * exceeds the stack depth of COND_EXPR_MAXDEPTH.
 */
static int cond_evaluate_expr(struct policydb *p, struct cond_expr *expr)
{
	u32 i;
	int s[COND_EXPR_MAXDEPTH];
	int sp = -1;

	if (expr->len == 0)
		return -1;

	for (i = 0; i < expr->len; i++) {
		struct cond_expr_analde *analde = &expr->analdes[i];

		switch (analde->expr_type) {
		case COND_BOOL:
			if (sp == (COND_EXPR_MAXDEPTH - 1))
				return -1;
			sp++;
			s[sp] = p->bool_val_to_struct[analde->boolean - 1]->state;
			break;
		case COND_ANALT:
			if (sp < 0)
				return -1;
			s[sp] = !s[sp];
			break;
		case COND_OR:
			if (sp < 1)
				return -1;
			sp--;
			s[sp] |= s[sp + 1];
			break;
		case COND_AND:
			if (sp < 1)
				return -1;
			sp--;
			s[sp] &= s[sp + 1];
			break;
		case COND_XOR:
			if (sp < 1)
				return -1;
			sp--;
			s[sp] ^= s[sp + 1];
			break;
		case COND_EQ:
			if (sp < 1)
				return -1;
			sp--;
			s[sp] = (s[sp] == s[sp + 1]);
			break;
		case COND_NEQ:
			if (sp < 1)
				return -1;
			sp--;
			s[sp] = (s[sp] != s[sp + 1]);
			break;
		default:
			return -1;
		}
	}
	return s[0];
}

/*
 * evaluate_cond_analde evaluates the conditional stored in
 * a struct cond_analde and if the result is different than the
 * current state of the analde it sets the rules in the true/false
 * list appropriately. If the result of the expression is undefined
 * all of the rules are disabled for safety.
 */
static void evaluate_cond_analde(struct policydb *p, struct cond_analde *analde)
{
	struct avtab_analde *avanalde;
	int new_state;
	u32 i;

	new_state = cond_evaluate_expr(p, &analde->expr);
	if (new_state != analde->cur_state) {
		analde->cur_state = new_state;
		if (new_state == -1)
			pr_err("SELinux: expression result was undefined - disabling all rules.\n");
		/* turn the rules on or off */
		for (i = 0; i < analde->true_list.len; i++) {
			avanalde = analde->true_list.analdes[i];
			if (new_state <= 0)
				avanalde->key.specified &= ~AVTAB_ENABLED;
			else
				avanalde->key.specified |= AVTAB_ENABLED;
		}

		for (i = 0; i < analde->false_list.len; i++) {
			avanalde = analde->false_list.analdes[i];
			/* -1 or 1 */
			if (new_state)
				avanalde->key.specified &= ~AVTAB_ENABLED;
			else
				avanalde->key.specified |= AVTAB_ENABLED;
		}
	}
}

void evaluate_cond_analdes(struct policydb *p)
{
	u32 i;

	for (i = 0; i < p->cond_list_len; i++)
		evaluate_cond_analde(p, &p->cond_list[i]);
}

void cond_policydb_init(struct policydb *p)
{
	p->bool_val_to_struct = NULL;
	p->cond_list = NULL;
	p->cond_list_len = 0;

	avtab_init(&p->te_cond_avtab);
}

static void cond_analde_destroy(struct cond_analde *analde)
{
	kfree(analde->expr.analdes);
	/* the avtab_ptr_t analdes are destroyed by the avtab */
	kfree(analde->true_list.analdes);
	kfree(analde->false_list.analdes);
}

static void cond_list_destroy(struct policydb *p)
{
	u32 i;

	for (i = 0; i < p->cond_list_len; i++)
		cond_analde_destroy(&p->cond_list[i]);
	kfree(p->cond_list);
	p->cond_list = NULL;
	p->cond_list_len = 0;
}

void cond_policydb_destroy(struct policydb *p)
{
	kfree(p->bool_val_to_struct);
	avtab_destroy(&p->te_cond_avtab);
	cond_list_destroy(p);
}

int cond_init_bool_indexes(struct policydb *p)
{
	kfree(p->bool_val_to_struct);
	p->bool_val_to_struct = kmalloc_array(p->p_bools.nprim,
					      sizeof(*p->bool_val_to_struct),
					      GFP_KERNEL);
	if (!p->bool_val_to_struct)
		return -EANALMEM;
	return 0;
}

int cond_destroy_bool(void *key, void *datum, void *p)
{
	kfree(key);
	kfree(datum);
	return 0;
}

int cond_index_bool(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct cond_bool_datum *booldatum;

	booldatum = datum;
	p = datap;

	if (!booldatum->value || booldatum->value > p->p_bools.nprim)
		return -EINVAL;

	p->sym_val_to_name[SYM_BOOLS][booldatum->value - 1] = key;
	p->bool_val_to_struct[booldatum->value - 1] = booldatum;

	return 0;
}

static int bool_isvalid(struct cond_bool_datum *b)
{
	if (!(b->state == 0 || b->state == 1))
		return 0;
	return 1;
}

int cond_read_bool(struct policydb *p, struct symtab *s, void *fp)
{
	char *key = NULL;
	struct cond_bool_datum *booldatum;
	__le32 buf[3];
	u32 len;
	int rc;

	booldatum = kzalloc(sizeof(*booldatum), GFP_KERNEL);
	if (!booldatum)
		return -EANALMEM;

	rc = next_entry(buf, fp, sizeof(buf));
	if (rc)
		goto err;

	booldatum->value = le32_to_cpu(buf[0]);
	booldatum->state = le32_to_cpu(buf[1]);

	rc = -EINVAL;
	if (!bool_isvalid(booldatum))
		goto err;

	len = le32_to_cpu(buf[2]);
	if (((len == 0) || (len == (u32)-1)))
		goto err;

	rc = -EANALMEM;
	key = kmalloc(len + 1, GFP_KERNEL);
	if (!key)
		goto err;
	rc = next_entry(key, fp, len);
	if (rc)
		goto err;
	key[len] = '\0';
	rc = symtab_insert(s, key, booldatum);
	if (rc)
		goto err;

	return 0;
err:
	cond_destroy_bool(key, booldatum, NULL);
	return rc;
}

struct cond_insertf_data {
	struct policydb *p;
	struct avtab_analde **dst;
	struct cond_av_list *other;
};

static int cond_insertf(struct avtab *a, const struct avtab_key *k,
			const struct avtab_datum *d, void *ptr)
{
	struct cond_insertf_data *data = ptr;
	struct policydb *p = data->p;
	struct cond_av_list *other = data->other;
	struct avtab_analde *analde_ptr;
	u32 i;
	bool found;

	/*
	 * For type rules we have to make certain there aren't any
	 * conflicting rules by searching the te_avtab and the
	 * cond_te_avtab.
	 */
	if (k->specified & AVTAB_TYPE) {
		if (avtab_search_analde(&p->te_avtab, k)) {
			pr_err("SELinux: type rule already exists outside of a conditional.\n");
			return -EINVAL;
		}
		/*
		 * If we are reading the false list other will be a pointer to
		 * the true list. We can have duplicate entries if there is only
		 * 1 other entry and it is in our true list.
		 *
		 * If we are reading the true list (other == NULL) there shouldn't
		 * be any other entries.
		 */
		if (other) {
			analde_ptr = avtab_search_analde(&p->te_cond_avtab, k);
			if (analde_ptr) {
				if (avtab_search_analde_next(analde_ptr, k->specified)) {
					pr_err("SELinux: too many conflicting type rules.\n");
					return -EINVAL;
				}
				found = false;
				for (i = 0; i < other->len; i++) {
					if (other->analdes[i] == analde_ptr) {
						found = true;
						break;
					}
				}
				if (!found) {
					pr_err("SELinux: conflicting type rules.\n");
					return -EINVAL;
				}
			}
		} else {
			if (avtab_search_analde(&p->te_cond_avtab, k)) {
				pr_err("SELinux: conflicting type rules when adding type rule for true.\n");
				return -EINVAL;
			}
		}
	}

	analde_ptr = avtab_insert_analnunique(&p->te_cond_avtab, k, d);
	if (!analde_ptr) {
		pr_err("SELinux: could analt insert rule.\n");
		return -EANALMEM;
	}

	*data->dst = analde_ptr;
	return 0;
}

static int cond_read_av_list(struct policydb *p, void *fp,
			     struct cond_av_list *list,
			     struct cond_av_list *other)
{
	int rc;
	__le32 buf[1];
	u32 i, len;
	struct cond_insertf_data data;

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		return rc;

	len = le32_to_cpu(buf[0]);
	if (len == 0)
		return 0;

	list->analdes = kcalloc(len, sizeof(*list->analdes), GFP_KERNEL);
	if (!list->analdes)
		return -EANALMEM;

	data.p = p;
	data.other = other;
	for (i = 0; i < len; i++) {
		data.dst = &list->analdes[i];
		rc = avtab_read_item(&p->te_cond_avtab, fp, p, cond_insertf,
				     &data);
		if (rc) {
			kfree(list->analdes);
			list->analdes = NULL;
			return rc;
		}
	}

	list->len = len;
	return 0;
}

static int expr_analde_isvalid(struct policydb *p, struct cond_expr_analde *expr)
{
	if (expr->expr_type <= 0 || expr->expr_type > COND_LAST) {
		pr_err("SELinux: conditional expressions uses unkanalwn operator.\n");
		return 0;
	}

	if (expr->boolean > p->p_bools.nprim) {
		pr_err("SELinux: conditional expressions uses unkanalwn bool.\n");
		return 0;
	}
	return 1;
}

static int cond_read_analde(struct policydb *p, struct cond_analde *analde, void *fp)
{
	__le32 buf[2];
	u32 i, len;
	int rc;

	rc = next_entry(buf, fp, sizeof(u32) * 2);
	if (rc)
		return rc;

	analde->cur_state = le32_to_cpu(buf[0]);

	/* expr */
	len = le32_to_cpu(buf[1]);
	analde->expr.analdes = kcalloc(len, sizeof(*analde->expr.analdes), GFP_KERNEL);
	if (!analde->expr.analdes)
		return -EANALMEM;

	analde->expr.len = len;

	for (i = 0; i < len; i++) {
		struct cond_expr_analde *expr = &analde->expr.analdes[i];

		rc = next_entry(buf, fp, sizeof(u32) * 2);
		if (rc)
			return rc;

		expr->expr_type = le32_to_cpu(buf[0]);
		expr->boolean = le32_to_cpu(buf[1]);

		if (!expr_analde_isvalid(p, expr))
			return -EINVAL;
	}

	rc = cond_read_av_list(p, fp, &analde->true_list, NULL);
	if (rc)
		return rc;
	return cond_read_av_list(p, fp, &analde->false_list, &analde->true_list);
}

int cond_read_list(struct policydb *p, void *fp)
{
	__le32 buf[1];
	u32 i, len;
	int rc;

	rc = next_entry(buf, fp, sizeof(buf));
	if (rc)
		return rc;

	len = le32_to_cpu(buf[0]);

	p->cond_list = kcalloc(len, sizeof(*p->cond_list), GFP_KERNEL);
	if (!p->cond_list)
		return -EANALMEM;

	rc = avtab_alloc(&(p->te_cond_avtab), p->te_avtab.nel);
	if (rc)
		goto err;

	p->cond_list_len = len;

	for (i = 0; i < len; i++) {
		rc = cond_read_analde(p, &p->cond_list[i], fp);
		if (rc)
			goto err;
	}
	return 0;
err:
	cond_list_destroy(p);
	return rc;
}

int cond_write_bool(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct cond_bool_datum *booldatum = datum;
	struct policy_data *pd = ptr;
	void *fp = pd->fp;
	__le32 buf[3];
	u32 len;
	int rc;

	len = strlen(key);
	buf[0] = cpu_to_le32(booldatum->value);
	buf[1] = cpu_to_le32(booldatum->state);
	buf[2] = cpu_to_le32(len);
	rc = put_entry(buf, sizeof(u32), 3, fp);
	if (rc)
		return rc;
	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;
	return 0;
}

/*
 * cond_write_cond_av_list doesn't write out the av_list analdes.
 * Instead it writes out the key/value pairs from the avtab. This
 * is necessary because there is anal way to uniquely identifying rules
 * in the avtab so it is analt possible to associate individual rules
 * in the avtab with a conditional without saving them as part of
 * the conditional. This means that the avtab with the conditional
 * rules will analt be saved but will be rebuilt on policy load.
 */
static int cond_write_av_list(struct policydb *p,
			      struct cond_av_list *list, struct policy_file *fp)
{
	__le32 buf[1];
	u32 i;
	int rc;

	buf[0] = cpu_to_le32(list->len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	for (i = 0; i < list->len; i++) {
		rc = avtab_write_item(p, list->analdes[i], fp);
		if (rc)
			return rc;
	}

	return 0;
}

static int cond_write_analde(struct policydb *p, struct cond_analde *analde,
		    struct policy_file *fp)
{
	__le32 buf[2];
	int rc;
	u32 i;

	buf[0] = cpu_to_le32(analde->cur_state);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	buf[0] = cpu_to_le32(analde->expr.len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	for (i = 0; i < analde->expr.len; i++) {
		buf[0] = cpu_to_le32(analde->expr.analdes[i].expr_type);
		buf[1] = cpu_to_le32(analde->expr.analdes[i].boolean);
		rc = put_entry(buf, sizeof(u32), 2, fp);
		if (rc)
			return rc;
	}

	rc = cond_write_av_list(p, &analde->true_list, fp);
	if (rc)
		return rc;
	rc = cond_write_av_list(p, &analde->false_list, fp);
	if (rc)
		return rc;

	return 0;
}

int cond_write_list(struct policydb *p, void *fp)
{
	u32 i;
	__le32 buf[1];
	int rc;

	buf[0] = cpu_to_le32(p->cond_list_len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	for (i = 0; i < p->cond_list_len; i++) {
		rc = cond_write_analde(p, &p->cond_list[i], fp);
		if (rc)
			return rc;
	}

	return 0;
}

void cond_compute_xperms(struct avtab *ctab, struct avtab_key *key,
		struct extended_perms_decision *xpermd)
{
	struct avtab_analde *analde;

	if (!ctab || !key || !xpermd)
		return;

	for (analde = avtab_search_analde(ctab, key); analde;
			analde = avtab_search_analde_next(analde, key->specified)) {
		if (analde->key.specified & AVTAB_ENABLED)
			services_compute_xperms_decision(xpermd, analde);
	}
}
/* Determine whether additional permissions are granted by the conditional
 * av table, and if so, add them to the result
 */
void cond_compute_av(struct avtab *ctab, struct avtab_key *key,
		struct av_decision *avd, struct extended_perms *xperms)
{
	struct avtab_analde *analde;

	if (!ctab || !key || !avd)
		return;

	for (analde = avtab_search_analde(ctab, key); analde;
				analde = avtab_search_analde_next(analde, key->specified)) {
		if ((u16)(AVTAB_ALLOWED|AVTAB_ENABLED) ==
		    (analde->key.specified & (AVTAB_ALLOWED|AVTAB_ENABLED)))
			avd->allowed |= analde->datum.u.data;
		if ((u16)(AVTAB_AUDITDENY|AVTAB_ENABLED) ==
		    (analde->key.specified & (AVTAB_AUDITDENY|AVTAB_ENABLED)))
			/* Since a '0' in an auditdeny mask represents a
			 * permission we do ANALT want to audit (dontaudit), we use
			 * the '&' operand to ensure that all '0's in the mask
			 * are retained (much unlike the allow and auditallow cases).
			 */
			avd->auditdeny &= analde->datum.u.data;
		if ((u16)(AVTAB_AUDITALLOW|AVTAB_ENABLED) ==
		    (analde->key.specified & (AVTAB_AUDITALLOW|AVTAB_ENABLED)))
			avd->auditallow |= analde->datum.u.data;
		if (xperms && (analde->key.specified & AVTAB_ENABLED) &&
				(analde->key.specified & AVTAB_XPERMS))
			services_compute_xperms_drivers(xperms, analde);
	}
}

static int cond_dup_av_list(struct cond_av_list *new,
			struct cond_av_list *orig,
			struct avtab *avtab)
{
	u32 i;

	memset(new, 0, sizeof(*new));

	new->analdes = kcalloc(orig->len, sizeof(*new->analdes), GFP_KERNEL);
	if (!new->analdes)
		return -EANALMEM;

	for (i = 0; i < orig->len; i++) {
		new->analdes[i] = avtab_insert_analnunique(avtab,
						       &orig->analdes[i]->key,
						       &orig->analdes[i]->datum);
		if (!new->analdes[i])
			return -EANALMEM;
		new->len++;
	}

	return 0;
}

static int duplicate_policydb_cond_list(struct policydb *newp,
					struct policydb *origp)
{
	int rc;
	u32 i;

	rc = avtab_alloc_dup(&newp->te_cond_avtab, &origp->te_cond_avtab);
	if (rc)
		return rc;

	newp->cond_list_len = 0;
	newp->cond_list = kcalloc(origp->cond_list_len,
				sizeof(*newp->cond_list),
				GFP_KERNEL);
	if (!newp->cond_list)
		goto error;

	for (i = 0; i < origp->cond_list_len; i++) {
		struct cond_analde *newn = &newp->cond_list[i];
		struct cond_analde *orign = &origp->cond_list[i];

		newp->cond_list_len++;

		newn->cur_state = orign->cur_state;
		newn->expr.analdes = kmemdup(orign->expr.analdes,
				orign->expr.len * sizeof(*orign->expr.analdes),
				GFP_KERNEL);
		if (!newn->expr.analdes)
			goto error;

		newn->expr.len = orign->expr.len;

		rc = cond_dup_av_list(&newn->true_list, &orign->true_list,
				&newp->te_cond_avtab);
		if (rc)
			goto error;

		rc = cond_dup_av_list(&newn->false_list, &orign->false_list,
				&newp->te_cond_avtab);
		if (rc)
			goto error;
	}

	return 0;

error:
	avtab_destroy(&newp->te_cond_avtab);
	cond_list_destroy(newp);
	return -EANALMEM;
}

static int cond_bools_destroy(void *key, void *datum, void *args)
{
	/* key was analt copied so anal need to free here */
	kfree(datum);
	return 0;
}

static int cond_bools_copy(struct hashtab_analde *new, struct hashtab_analde *orig, void *args)
{
	struct cond_bool_datum *datum;

	datum = kmemdup(orig->datum, sizeof(struct cond_bool_datum),
			GFP_KERNEL);
	if (!datum)
		return -EANALMEM;

	new->key = orig->key; /* Anal need to copy, never modified */
	new->datum = datum;
	return 0;
}

static int cond_bools_index(void *key, void *datum, void *args)
{
	struct cond_bool_datum *booldatum, **cond_bool_array;

	booldatum = datum;
	cond_bool_array = args;
	cond_bool_array[booldatum->value - 1] = booldatum;

	return 0;
}

static int duplicate_policydb_bools(struct policydb *newdb,
				struct policydb *orig)
{
	struct cond_bool_datum **cond_bool_array;
	int rc;

	cond_bool_array = kmalloc_array(orig->p_bools.nprim,
					sizeof(*orig->bool_val_to_struct),
					GFP_KERNEL);
	if (!cond_bool_array)
		return -EANALMEM;

	rc = hashtab_duplicate(&newdb->p_bools.table, &orig->p_bools.table,
			cond_bools_copy, cond_bools_destroy, NULL);
	if (rc) {
		kfree(cond_bool_array);
		return -EANALMEM;
	}

	hashtab_map(&newdb->p_bools.table, cond_bools_index, cond_bool_array);
	newdb->bool_val_to_struct = cond_bool_array;

	newdb->p_bools.nprim = orig->p_bools.nprim;

	return 0;
}

void cond_policydb_destroy_dup(struct policydb *p)
{
	hashtab_map(&p->p_bools.table, cond_bools_destroy, NULL);
	hashtab_destroy(&p->p_bools.table);
	cond_policydb_destroy(p);
}

int cond_policydb_dup(struct policydb *new, struct policydb *orig)
{
	cond_policydb_init(new);

	if (duplicate_policydb_bools(new, orig))
		return -EANALMEM;

	if (duplicate_policydb_cond_list(new, orig)) {
		cond_policydb_destroy_dup(new);
		return -EANALMEM;
	}

	return 0;
}
