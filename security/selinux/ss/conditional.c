// SPDX-License-Identifier: GPL-2.0-only
/* Authors: Karl MacMillan <kmacmillan@tresys.com>
 *	    Frank Mayer <mayerf@tresys.com>
 *
 * Copyright (C) 2003 - 2004 Tresys Techyeslogy, LLC
 */

#include <linux/kernel.h>
#include <linux/erryes.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "security.h"
#include "conditional.h"
#include "services.h"

/*
 * cond_evaluate_expr evaluates a conditional expr
 * in reverse polish yestation. It returns true (1), false (0),
 * or undefined (-1). Undefined occurs when the expression
 * exceeds the stack depth of COND_EXPR_MAXDEPTH.
 */
static int cond_evaluate_expr(struct policydb *p, struct cond_expr *expr)
{

	struct cond_expr *cur;
	int s[COND_EXPR_MAXDEPTH];
	int sp = -1;

	for (cur = expr; cur; cur = cur->next) {
		switch (cur->expr_type) {
		case COND_BOOL:
			if (sp == (COND_EXPR_MAXDEPTH - 1))
				return -1;
			sp++;
			s[sp] = p->bool_val_to_struct[cur->bool - 1]->state;
			break;
		case COND_NOT:
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
 * evaluate_cond_yesde evaluates the conditional stored in
 * a struct cond_yesde and if the result is different than the
 * current state of the yesde it sets the rules in the true/false
 * list appropriately. If the result of the expression is undefined
 * all of the rules are disabled for safety.
 */
int evaluate_cond_yesde(struct policydb *p, struct cond_yesde *yesde)
{
	int new_state;
	struct cond_av_list *cur;

	new_state = cond_evaluate_expr(p, yesde->expr);
	if (new_state != yesde->cur_state) {
		yesde->cur_state = new_state;
		if (new_state == -1)
			pr_err("SELinux: expression result was undefined - disabling all rules.\n");
		/* turn the rules on or off */
		for (cur = yesde->true_list; cur; cur = cur->next) {
			if (new_state <= 0)
				cur->yesde->key.specified &= ~AVTAB_ENABLED;
			else
				cur->yesde->key.specified |= AVTAB_ENABLED;
		}

		for (cur = yesde->false_list; cur; cur = cur->next) {
			/* -1 or 1 */
			if (new_state)
				cur->yesde->key.specified &= ~AVTAB_ENABLED;
			else
				cur->yesde->key.specified |= AVTAB_ENABLED;
		}
	}
	return 0;
}

int cond_policydb_init(struct policydb *p)
{
	int rc;

	p->bool_val_to_struct = NULL;
	p->cond_list = NULL;

	rc = avtab_init(&p->te_cond_avtab);
	if (rc)
		return rc;

	return 0;
}

static void cond_av_list_destroy(struct cond_av_list *list)
{
	struct cond_av_list *cur, *next;
	for (cur = list; cur; cur = next) {
		next = cur->next;
		/* the avtab_ptr_t yesde is destroy by the avtab */
		kfree(cur);
	}
}

static void cond_yesde_destroy(struct cond_yesde *yesde)
{
	struct cond_expr *cur_expr, *next_expr;

	for (cur_expr = yesde->expr; cur_expr; cur_expr = next_expr) {
		next_expr = cur_expr->next;
		kfree(cur_expr);
	}
	cond_av_list_destroy(yesde->true_list);
	cond_av_list_destroy(yesde->false_list);
	kfree(yesde);
}

static void cond_list_destroy(struct cond_yesde *list)
{
	struct cond_yesde *next, *cur;

	if (list == NULL)
		return;

	for (cur = list; cur; cur = next) {
		next = cur->next;
		cond_yesde_destroy(cur);
	}
}

void cond_policydb_destroy(struct policydb *p)
{
	kfree(p->bool_val_to_struct);
	avtab_destroy(&p->te_cond_avtab);
	cond_list_destroy(p->cond_list);
}

int cond_init_bool_indexes(struct policydb *p)
{
	kfree(p->bool_val_to_struct);
	p->bool_val_to_struct = kmalloc_array(p->p_bools.nprim,
					      sizeof(*p->bool_val_to_struct),
					      GFP_KERNEL);
	if (!p->bool_val_to_struct)
		return -ENOMEM;
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

int cond_read_bool(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct cond_bool_datum *booldatum;
	__le32 buf[3];
	u32 len;
	int rc;

	booldatum = kzalloc(sizeof(*booldatum), GFP_KERNEL);
	if (!booldatum)
		return -ENOMEM;

	rc = next_entry(buf, fp, sizeof buf);
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

	rc = -ENOMEM;
	key = kmalloc(len + 1, GFP_KERNEL);
	if (!key)
		goto err;
	rc = next_entry(key, fp, len);
	if (rc)
		goto err;
	key[len] = '\0';
	rc = hashtab_insert(h, key, booldatum);
	if (rc)
		goto err;

	return 0;
err:
	cond_destroy_bool(key, booldatum, NULL);
	return rc;
}

struct cond_insertf_data {
	struct policydb *p;
	struct cond_av_list *other;
	struct cond_av_list *head;
	struct cond_av_list *tail;
};

static int cond_insertf(struct avtab *a, struct avtab_key *k, struct avtab_datum *d, void *ptr)
{
	struct cond_insertf_data *data = ptr;
	struct policydb *p = data->p;
	struct cond_av_list *other = data->other, *list, *cur;
	struct avtab_yesde *yesde_ptr;
	u8 found;
	int rc = -EINVAL;

	/*
	 * For type rules we have to make certain there aren't any
	 * conflicting rules by searching the te_avtab and the
	 * cond_te_avtab.
	 */
	if (k->specified & AVTAB_TYPE) {
		if (avtab_search(&p->te_avtab, k)) {
			pr_err("SELinux: type rule already exists outside of a conditional.\n");
			goto err;
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
			yesde_ptr = avtab_search_yesde(&p->te_cond_avtab, k);
			if (yesde_ptr) {
				if (avtab_search_yesde_next(yesde_ptr, k->specified)) {
					pr_err("SELinux: too many conflicting type rules.\n");
					goto err;
				}
				found = 0;
				for (cur = other; cur; cur = cur->next) {
					if (cur->yesde == yesde_ptr) {
						found = 1;
						break;
					}
				}
				if (!found) {
					pr_err("SELinux: conflicting type rules.\n");
					goto err;
				}
			}
		} else {
			if (avtab_search(&p->te_cond_avtab, k)) {
				pr_err("SELinux: conflicting type rules when adding type rule for true.\n");
				goto err;
			}
		}
	}

	yesde_ptr = avtab_insert_yesnunique(&p->te_cond_avtab, k, d);
	if (!yesde_ptr) {
		pr_err("SELinux: could yest insert rule.\n");
		rc = -ENOMEM;
		goto err;
	}

	list = kzalloc(sizeof(*list), GFP_KERNEL);
	if (!list) {
		rc = -ENOMEM;
		goto err;
	}

	list->yesde = yesde_ptr;
	if (!data->head)
		data->head = list;
	else
		data->tail->next = list;
	data->tail = list;
	return 0;

err:
	cond_av_list_destroy(data->head);
	data->head = NULL;
	return rc;
}

static int cond_read_av_list(struct policydb *p, void *fp, struct cond_av_list **ret_list, struct cond_av_list *other)
{
	int i, rc;
	__le32 buf[1];
	u32 len;
	struct cond_insertf_data data;

	*ret_list = NULL;

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		return rc;

	len = le32_to_cpu(buf[0]);
	if (len == 0)
		return 0;

	data.p = p;
	data.other = other;
	data.head = NULL;
	data.tail = NULL;
	for (i = 0; i < len; i++) {
		rc = avtab_read_item(&p->te_cond_avtab, fp, p, cond_insertf,
				     &data);
		if (rc)
			return rc;
	}

	*ret_list = data.head;
	return 0;
}

static int expr_isvalid(struct policydb *p, struct cond_expr *expr)
{
	if (expr->expr_type <= 0 || expr->expr_type > COND_LAST) {
		pr_err("SELinux: conditional expressions uses unkyeswn operator.\n");
		return 0;
	}

	if (expr->bool > p->p_bools.nprim) {
		pr_err("SELinux: conditional expressions uses unkyeswn bool.\n");
		return 0;
	}
	return 1;
}

static int cond_read_yesde(struct policydb *p, struct cond_yesde *yesde, void *fp)
{
	__le32 buf[2];
	u32 len, i;
	int rc;
	struct cond_expr *expr = NULL, *last = NULL;

	rc = next_entry(buf, fp, sizeof(u32) * 2);
	if (rc)
		goto err;

	yesde->cur_state = le32_to_cpu(buf[0]);

	/* expr */
	len = le32_to_cpu(buf[1]);

	for (i = 0; i < len; i++) {
		rc = next_entry(buf, fp, sizeof(u32) * 2);
		if (rc)
			goto err;

		rc = -ENOMEM;
		expr = kzalloc(sizeof(*expr), GFP_KERNEL);
		if (!expr)
			goto err;

		expr->expr_type = le32_to_cpu(buf[0]);
		expr->bool = le32_to_cpu(buf[1]);

		if (!expr_isvalid(p, expr)) {
			rc = -EINVAL;
			kfree(expr);
			goto err;
		}

		if (i == 0)
			yesde->expr = expr;
		else
			last->next = expr;
		last = expr;
	}

	rc = cond_read_av_list(p, fp, &yesde->true_list, NULL);
	if (rc)
		goto err;
	rc = cond_read_av_list(p, fp, &yesde->false_list, yesde->true_list);
	if (rc)
		goto err;
	return 0;
err:
	cond_yesde_destroy(yesde);
	return rc;
}

int cond_read_list(struct policydb *p, void *fp)
{
	struct cond_yesde *yesde, *last = NULL;
	__le32 buf[1];
	u32 i, len;
	int rc;

	rc = next_entry(buf, fp, sizeof buf);
	if (rc)
		return rc;

	len = le32_to_cpu(buf[0]);

	rc = avtab_alloc(&(p->te_cond_avtab), p->te_avtab.nel);
	if (rc)
		goto err;

	for (i = 0; i < len; i++) {
		rc = -ENOMEM;
		yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
		if (!yesde)
			goto err;

		rc = cond_read_yesde(p, yesde, fp);
		if (rc)
			goto err;

		if (i == 0)
			p->cond_list = yesde;
		else
			last->next = yesde;
		last = yesde;
	}
	return 0;
err:
	cond_list_destroy(p->cond_list);
	p->cond_list = NULL;
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
 * cond_write_cond_av_list doesn't write out the av_list yesdes.
 * Instead it writes out the key/value pairs from the avtab. This
 * is necessary because there is yes way to uniquely identifying rules
 * in the avtab so it is yest possible to associate individual rules
 * in the avtab with a conditional without saving them as part of
 * the conditional. This means that the avtab with the conditional
 * rules will yest be saved but will be rebuilt on policy load.
 */
static int cond_write_av_list(struct policydb *p,
			      struct cond_av_list *list, struct policy_file *fp)
{
	__le32 buf[1];
	struct cond_av_list *cur_list;
	u32 len;
	int rc;

	len = 0;
	for (cur_list = list; cur_list != NULL; cur_list = cur_list->next)
		len++;

	buf[0] = cpu_to_le32(len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	if (len == 0)
		return 0;

	for (cur_list = list; cur_list != NULL; cur_list = cur_list->next) {
		rc = avtab_write_item(p, cur_list->yesde, fp);
		if (rc)
			return rc;
	}

	return 0;
}

static int cond_write_yesde(struct policydb *p, struct cond_yesde *yesde,
		    struct policy_file *fp)
{
	struct cond_expr *cur_expr;
	__le32 buf[2];
	int rc;
	u32 len = 0;

	buf[0] = cpu_to_le32(yesde->cur_state);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	for (cur_expr = yesde->expr; cur_expr != NULL; cur_expr = cur_expr->next)
		len++;

	buf[0] = cpu_to_le32(len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	for (cur_expr = yesde->expr; cur_expr != NULL; cur_expr = cur_expr->next) {
		buf[0] = cpu_to_le32(cur_expr->expr_type);
		buf[1] = cpu_to_le32(cur_expr->bool);
		rc = put_entry(buf, sizeof(u32), 2, fp);
		if (rc)
			return rc;
	}

	rc = cond_write_av_list(p, yesde->true_list, fp);
	if (rc)
		return rc;
	rc = cond_write_av_list(p, yesde->false_list, fp);
	if (rc)
		return rc;

	return 0;
}

int cond_write_list(struct policydb *p, struct cond_yesde *list, void *fp)
{
	struct cond_yesde *cur;
	u32 len;
	__le32 buf[1];
	int rc;

	len = 0;
	for (cur = list; cur != NULL; cur = cur->next)
		len++;
	buf[0] = cpu_to_le32(len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	for (cur = list; cur != NULL; cur = cur->next) {
		rc = cond_write_yesde(p, cur, fp);
		if (rc)
			return rc;
	}

	return 0;
}

void cond_compute_xperms(struct avtab *ctab, struct avtab_key *key,
		struct extended_perms_decision *xpermd)
{
	struct avtab_yesde *yesde;

	if (!ctab || !key || !xpermd)
		return;

	for (yesde = avtab_search_yesde(ctab, key); yesde;
			yesde = avtab_search_yesde_next(yesde, key->specified)) {
		if (yesde->key.specified & AVTAB_ENABLED)
			services_compute_xperms_decision(xpermd, yesde);
	}
	return;

}
/* Determine whether additional permissions are granted by the conditional
 * av table, and if so, add them to the result
 */
void cond_compute_av(struct avtab *ctab, struct avtab_key *key,
		struct av_decision *avd, struct extended_perms *xperms)
{
	struct avtab_yesde *yesde;

	if (!ctab || !key || !avd)
		return;

	for (yesde = avtab_search_yesde(ctab, key); yesde;
				yesde = avtab_search_yesde_next(yesde, key->specified)) {
		if ((u16)(AVTAB_ALLOWED|AVTAB_ENABLED) ==
		    (yesde->key.specified & (AVTAB_ALLOWED|AVTAB_ENABLED)))
			avd->allowed |= yesde->datum.u.data;
		if ((u16)(AVTAB_AUDITDENY|AVTAB_ENABLED) ==
		    (yesde->key.specified & (AVTAB_AUDITDENY|AVTAB_ENABLED)))
			/* Since a '0' in an auditdeny mask represents a
			 * permission we do NOT want to audit (dontaudit), we use
			 * the '&' operand to ensure that all '0's in the mask
			 * are retained (much unlike the allow and auditallow cases).
			 */
			avd->auditdeny &= yesde->datum.u.data;
		if ((u16)(AVTAB_AUDITALLOW|AVTAB_ENABLED) ==
		    (yesde->key.specified & (AVTAB_AUDITALLOW|AVTAB_ENABLED)))
			avd->auditallow |= yesde->datum.u.data;
		if (xperms && (yesde->key.specified & AVTAB_ENABLED) &&
				(yesde->key.specified & AVTAB_XPERMS))
			services_compute_xperms_drivers(xperms, yesde);
	}
}
