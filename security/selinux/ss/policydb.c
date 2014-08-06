/*
 * Implementation of the policy database.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */

/*
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 *	Support for enhanced MLS infrastructure.
 *
 * Updated: Frank Mayer <mayerf@tresys.com> and Karl MacMillan <kmacmillan@tresys.com>
 *
 *	Added conditional policy language extensions
 *
 * Updated: Hewlett-Packard <paul@paul-moore.com>
 *
 *      Added support for the policy capability bitmap
 *
 * Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2004-2005 Trusted Computer Solutions, Inc.
 * Copyright (C) 2003 - 2004 Tresys Technology, LLC
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/audit.h>
#include <linux/flex_array.h>
#include "security.h"

#include "policydb.h"
#include "conditional.h"
#include "mls.h"
#include "services.h"

#define _DEBUG_HASHES

#ifdef DEBUG_HASHES
static const char *symtab_name[SYM_NUM] = {
	"common prefixes",
	"classes",
	"roles",
	"types",
	"users",
	"bools",
	"levels",
	"categories",
};
#endif

static unsigned int symtab_sizes[SYM_NUM] = {
	2,
	32,
	16,
	512,
	128,
	16,
	16,
	16,
};

struct policydb_compat_info {
	int version;
	int sym_num;
	int ocon_num;
};

/* These need to be updated if SYM_NUM or OCON_NUM changes */
static struct policydb_compat_info policydb_compat[] = {
	{
		.version	= POLICYDB_VERSION_BASE,
		.sym_num	= SYM_NUM - 3,
		.ocon_num	= OCON_NUM - 1,
	},
	{
		.version	= POLICYDB_VERSION_BOOL,
		.sym_num	= SYM_NUM - 2,
		.ocon_num	= OCON_NUM - 1,
	},
	{
		.version	= POLICYDB_VERSION_IPV6,
		.sym_num	= SYM_NUM - 2,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_NLCLASS,
		.sym_num	= SYM_NUM - 2,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_MLS,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_AVTAB,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_RANGETRANS,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_POLCAP,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_PERMISSIVE,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_BOUNDARY,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_FILENAME_TRANS,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_ROLETRANS,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_NEW_OBJECT_DEFAULTS,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_DEFAULT_TYPE,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
	{
		.version	= POLICYDB_VERSION_CONSTRAINT_NAMES,
		.sym_num	= SYM_NUM,
		.ocon_num	= OCON_NUM,
	},
};

static struct policydb_compat_info *policydb_lookup_compat(int version)
{
	int i;
	struct policydb_compat_info *info = NULL;

	for (i = 0; i < ARRAY_SIZE(policydb_compat); i++) {
		if (policydb_compat[i].version == version) {
			info = &policydb_compat[i];
			break;
		}
	}
	return info;
}

/*
 * Initialize the role table.
 */
static int roles_init(struct policydb *p)
{
	char *key = NULL;
	int rc;
	struct role_datum *role;

	rc = -ENOMEM;
	role = kzalloc(sizeof(*role), GFP_KERNEL);
	if (!role)
		goto out;

	rc = -EINVAL;
	role->value = ++p->p_roles.nprim;
	if (role->value != OBJECT_R_VAL)
		goto out;

	rc = -ENOMEM;
	key = kstrdup(OBJECT_R, GFP_KERNEL);
	if (!key)
		goto out;

	rc = hashtab_insert(p->p_roles.table, key, role);
	if (rc)
		goto out;

	return 0;
out:
	kfree(key);
	kfree(role);
	return rc;
}

static u32 filenametr_hash(struct hashtab *h, const void *k)
{
	const struct filename_trans *ft = k;
	unsigned long hash;
	unsigned int byte_num;
	unsigned char focus;

	hash = ft->stype ^ ft->ttype ^ ft->tclass;

	byte_num = 0;
	while ((focus = ft->name[byte_num++]))
		hash = partial_name_hash(focus, hash);
	return hash & (h->size - 1);
}

static int filenametr_cmp(struct hashtab *h, const void *k1, const void *k2)
{
	const struct filename_trans *ft1 = k1;
	const struct filename_trans *ft2 = k2;
	int v;

	v = ft1->stype - ft2->stype;
	if (v)
		return v;

	v = ft1->ttype - ft2->ttype;
	if (v)
		return v;

	v = ft1->tclass - ft2->tclass;
	if (v)
		return v;

	return strcmp(ft1->name, ft2->name);

}

static u32 rangetr_hash(struct hashtab *h, const void *k)
{
	const struct range_trans *key = k;
	return (key->source_type + (key->target_type << 3) +
		(key->target_class << 5)) & (h->size - 1);
}

static int rangetr_cmp(struct hashtab *h, const void *k1, const void *k2)
{
	const struct range_trans *key1 = k1, *key2 = k2;
	int v;

	v = key1->source_type - key2->source_type;
	if (v)
		return v;

	v = key1->target_type - key2->target_type;
	if (v)
		return v;

	v = key1->target_class - key2->target_class;

	return v;
}

/*
 * Initialize a policy database structure.
 */
static int policydb_init(struct policydb *p)
{
	int i, rc;

	memset(p, 0, sizeof(*p));

	for (i = 0; i < SYM_NUM; i++) {
		rc = symtab_init(&p->symtab[i], symtab_sizes[i]);
		if (rc)
			goto out;
	}

	rc = avtab_init(&p->te_avtab);
	if (rc)
		goto out;

	rc = roles_init(p);
	if (rc)
		goto out;

	rc = cond_policydb_init(p);
	if (rc)
		goto out;

	p->filename_trans = hashtab_create(filenametr_hash, filenametr_cmp, (1 << 10));
	if (!p->filename_trans)
		goto out;

	p->range_tr = hashtab_create(rangetr_hash, rangetr_cmp, 256);
	if (!p->range_tr)
		goto out;

	ebitmap_init(&p->filename_trans_ttypes);
	ebitmap_init(&p->policycaps);
	ebitmap_init(&p->permissive_map);

	return 0;
out:
	hashtab_destroy(p->filename_trans);
	hashtab_destroy(p->range_tr);
	for (i = 0; i < SYM_NUM; i++)
		hashtab_destroy(p->symtab[i].table);
	return rc;
}

/*
 * The following *_index functions are used to
 * define the val_to_name and val_to_struct arrays
 * in a policy database structure.  The val_to_name
 * arrays are used when converting security context
 * structures into string representations.  The
 * val_to_struct arrays are used when the attributes
 * of a class, role, or user are needed.
 */

static int common_index(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct common_datum *comdatum;
	struct flex_array *fa;

	comdatum = datum;
	p = datap;
	if (!comdatum->value || comdatum->value > p->p_commons.nprim)
		return -EINVAL;

	fa = p->sym_val_to_name[SYM_COMMONS];
	if (flex_array_put_ptr(fa, comdatum->value - 1, key,
			       GFP_KERNEL | __GFP_ZERO))
		BUG();
	return 0;
}

static int class_index(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct class_datum *cladatum;
	struct flex_array *fa;

	cladatum = datum;
	p = datap;
	if (!cladatum->value || cladatum->value > p->p_classes.nprim)
		return -EINVAL;
	fa = p->sym_val_to_name[SYM_CLASSES];
	if (flex_array_put_ptr(fa, cladatum->value - 1, key,
			       GFP_KERNEL | __GFP_ZERO))
		BUG();
	p->class_val_to_struct[cladatum->value - 1] = cladatum;
	return 0;
}

static int role_index(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct role_datum *role;
	struct flex_array *fa;

	role = datum;
	p = datap;
	if (!role->value
	    || role->value > p->p_roles.nprim
	    || role->bounds > p->p_roles.nprim)
		return -EINVAL;

	fa = p->sym_val_to_name[SYM_ROLES];
	if (flex_array_put_ptr(fa, role->value - 1, key,
			       GFP_KERNEL | __GFP_ZERO))
		BUG();
	p->role_val_to_struct[role->value - 1] = role;
	return 0;
}

static int type_index(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct type_datum *typdatum;
	struct flex_array *fa;

	typdatum = datum;
	p = datap;

	if (typdatum->primary) {
		if (!typdatum->value
		    || typdatum->value > p->p_types.nprim
		    || typdatum->bounds > p->p_types.nprim)
			return -EINVAL;
		fa = p->sym_val_to_name[SYM_TYPES];
		if (flex_array_put_ptr(fa, typdatum->value - 1, key,
				       GFP_KERNEL | __GFP_ZERO))
			BUG();

		fa = p->type_val_to_struct_array;
		if (flex_array_put_ptr(fa, typdatum->value - 1, typdatum,
				       GFP_KERNEL | __GFP_ZERO))
			BUG();
	}

	return 0;
}

static int user_index(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct user_datum *usrdatum;
	struct flex_array *fa;

	usrdatum = datum;
	p = datap;
	if (!usrdatum->value
	    || usrdatum->value > p->p_users.nprim
	    || usrdatum->bounds > p->p_users.nprim)
		return -EINVAL;

	fa = p->sym_val_to_name[SYM_USERS];
	if (flex_array_put_ptr(fa, usrdatum->value - 1, key,
			       GFP_KERNEL | __GFP_ZERO))
		BUG();
	p->user_val_to_struct[usrdatum->value - 1] = usrdatum;
	return 0;
}

static int sens_index(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct level_datum *levdatum;
	struct flex_array *fa;

	levdatum = datum;
	p = datap;

	if (!levdatum->isalias) {
		if (!levdatum->level->sens ||
		    levdatum->level->sens > p->p_levels.nprim)
			return -EINVAL;
		fa = p->sym_val_to_name[SYM_LEVELS];
		if (flex_array_put_ptr(fa, levdatum->level->sens - 1, key,
				       GFP_KERNEL | __GFP_ZERO))
			BUG();
	}

	return 0;
}

static int cat_index(void *key, void *datum, void *datap)
{
	struct policydb *p;
	struct cat_datum *catdatum;
	struct flex_array *fa;

	catdatum = datum;
	p = datap;

	if (!catdatum->isalias) {
		if (!catdatum->value || catdatum->value > p->p_cats.nprim)
			return -EINVAL;
		fa = p->sym_val_to_name[SYM_CATS];
		if (flex_array_put_ptr(fa, catdatum->value - 1, key,
				       GFP_KERNEL | __GFP_ZERO))
			BUG();
	}

	return 0;
}

static int (*index_f[SYM_NUM]) (void *key, void *datum, void *datap) =
{
	common_index,
	class_index,
	role_index,
	type_index,
	user_index,
	cond_index_bool,
	sens_index,
	cat_index,
};

#ifdef DEBUG_HASHES
static void hash_eval(struct hashtab *h, const char *hash_name)
{
	struct hashtab_info info;

	hashtab_stat(h, &info);
	printk(KERN_DEBUG "SELinux: %s:  %d entries and %d/%d buckets used, "
	       "longest chain length %d\n", hash_name, h->nel,
	       info.slots_used, h->size, info.max_chain_len);
}

static void symtab_hash_eval(struct symtab *s)
{
	int i;

	for (i = 0; i < SYM_NUM; i++)
		hash_eval(s[i].table, symtab_name[i]);
}

#else
static inline void hash_eval(struct hashtab *h, char *hash_name)
{
}
#endif

/*
 * Define the other val_to_name and val_to_struct arrays
 * in a policy database structure.
 *
 * Caller must clean up on failure.
 */
static int policydb_index(struct policydb *p)
{
	int i, rc;

	printk(KERN_DEBUG "SELinux:  %d users, %d roles, %d types, %d bools",
	       p->p_users.nprim, p->p_roles.nprim, p->p_types.nprim, p->p_bools.nprim);
	if (p->mls_enabled)
		printk(", %d sens, %d cats", p->p_levels.nprim,
		       p->p_cats.nprim);
	printk("\n");

	printk(KERN_DEBUG "SELinux:  %d classes, %d rules\n",
	       p->p_classes.nprim, p->te_avtab.nel);

#ifdef DEBUG_HASHES
	avtab_hash_eval(&p->te_avtab, "rules");
	symtab_hash_eval(p->symtab);
#endif

	rc = -ENOMEM;
	p->class_val_to_struct =
		kmalloc(p->p_classes.nprim * sizeof(*(p->class_val_to_struct)),
			GFP_KERNEL);
	if (!p->class_val_to_struct)
		goto out;

	rc = -ENOMEM;
	p->role_val_to_struct =
		kmalloc(p->p_roles.nprim * sizeof(*(p->role_val_to_struct)),
			GFP_KERNEL);
	if (!p->role_val_to_struct)
		goto out;

	rc = -ENOMEM;
	p->user_val_to_struct =
		kmalloc(p->p_users.nprim * sizeof(*(p->user_val_to_struct)),
			GFP_KERNEL);
	if (!p->user_val_to_struct)
		goto out;

	/* Yes, I want the sizeof the pointer, not the structure */
	rc = -ENOMEM;
	p->type_val_to_struct_array = flex_array_alloc(sizeof(struct type_datum *),
						       p->p_types.nprim,
						       GFP_KERNEL | __GFP_ZERO);
	if (!p->type_val_to_struct_array)
		goto out;

	rc = flex_array_prealloc(p->type_val_to_struct_array, 0,
				 p->p_types.nprim, GFP_KERNEL | __GFP_ZERO);
	if (rc)
		goto out;

	rc = cond_init_bool_indexes(p);
	if (rc)
		goto out;

	for (i = 0; i < SYM_NUM; i++) {
		rc = -ENOMEM;
		p->sym_val_to_name[i] = flex_array_alloc(sizeof(char *),
							 p->symtab[i].nprim,
							 GFP_KERNEL | __GFP_ZERO);
		if (!p->sym_val_to_name[i])
			goto out;

		rc = flex_array_prealloc(p->sym_val_to_name[i],
					 0, p->symtab[i].nprim,
					 GFP_KERNEL | __GFP_ZERO);
		if (rc)
			goto out;

		rc = hashtab_map(p->symtab[i].table, index_f[i], p);
		if (rc)
			goto out;
	}
	rc = 0;
out:
	return rc;
}

/*
 * The following *_destroy functions are used to
 * free any memory allocated for each kind of
 * symbol data in the policy database.
 */

static int perm_destroy(void *key, void *datum, void *p)
{
	kfree(key);
	kfree(datum);
	return 0;
}

static int common_destroy(void *key, void *datum, void *p)
{
	struct common_datum *comdatum;

	kfree(key);
	if (datum) {
		comdatum = datum;
		hashtab_map(comdatum->permissions.table, perm_destroy, NULL);
		hashtab_destroy(comdatum->permissions.table);
	}
	kfree(datum);
	return 0;
}

static void constraint_expr_destroy(struct constraint_expr *expr)
{
	if (expr) {
		ebitmap_destroy(&expr->names);
		if (expr->type_names) {
			ebitmap_destroy(&expr->type_names->types);
			ebitmap_destroy(&expr->type_names->negset);
			kfree(expr->type_names);
		}
		kfree(expr);
	}
}

static int cls_destroy(void *key, void *datum, void *p)
{
	struct class_datum *cladatum;
	struct constraint_node *constraint, *ctemp;
	struct constraint_expr *e, *etmp;

	kfree(key);
	if (datum) {
		cladatum = datum;
		hashtab_map(cladatum->permissions.table, perm_destroy, NULL);
		hashtab_destroy(cladatum->permissions.table);
		constraint = cladatum->constraints;
		while (constraint) {
			e = constraint->expr;
			while (e) {
				etmp = e;
				e = e->next;
				constraint_expr_destroy(etmp);
			}
			ctemp = constraint;
			constraint = constraint->next;
			kfree(ctemp);
		}

		constraint = cladatum->validatetrans;
		while (constraint) {
			e = constraint->expr;
			while (e) {
				etmp = e;
				e = e->next;
				constraint_expr_destroy(etmp);
			}
			ctemp = constraint;
			constraint = constraint->next;
			kfree(ctemp);
		}
		kfree(cladatum->comkey);
	}
	kfree(datum);
	return 0;
}

static int role_destroy(void *key, void *datum, void *p)
{
	struct role_datum *role;

	kfree(key);
	if (datum) {
		role = datum;
		ebitmap_destroy(&role->dominates);
		ebitmap_destroy(&role->types);
	}
	kfree(datum);
	return 0;
}

static int type_destroy(void *key, void *datum, void *p)
{
	kfree(key);
	kfree(datum);
	return 0;
}

static int user_destroy(void *key, void *datum, void *p)
{
	struct user_datum *usrdatum;

	kfree(key);
	if (datum) {
		usrdatum = datum;
		ebitmap_destroy(&usrdatum->roles);
		ebitmap_destroy(&usrdatum->range.level[0].cat);
		ebitmap_destroy(&usrdatum->range.level[1].cat);
		ebitmap_destroy(&usrdatum->dfltlevel.cat);
	}
	kfree(datum);
	return 0;
}

static int sens_destroy(void *key, void *datum, void *p)
{
	struct level_datum *levdatum;

	kfree(key);
	if (datum) {
		levdatum = datum;
		ebitmap_destroy(&levdatum->level->cat);
		kfree(levdatum->level);
	}
	kfree(datum);
	return 0;
}

static int cat_destroy(void *key, void *datum, void *p)
{
	kfree(key);
	kfree(datum);
	return 0;
}

static int (*destroy_f[SYM_NUM]) (void *key, void *datum, void *datap) =
{
	common_destroy,
	cls_destroy,
	role_destroy,
	type_destroy,
	user_destroy,
	cond_destroy_bool,
	sens_destroy,
	cat_destroy,
};

static int filenametr_destroy(void *key, void *datum, void *p)
{
	struct filename_trans *ft = key;
	kfree(ft->name);
	kfree(key);
	kfree(datum);
	cond_resched();
	return 0;
}

static int range_tr_destroy(void *key, void *datum, void *p)
{
	struct mls_range *rt = datum;
	kfree(key);
	ebitmap_destroy(&rt->level[0].cat);
	ebitmap_destroy(&rt->level[1].cat);
	kfree(datum);
	cond_resched();
	return 0;
}

static void ocontext_destroy(struct ocontext *c, int i)
{
	if (!c)
		return;

	context_destroy(&c->context[0]);
	context_destroy(&c->context[1]);
	if (i == OCON_ISID || i == OCON_FS ||
	    i == OCON_NETIF || i == OCON_FSUSE)
		kfree(c->u.name);
	kfree(c);
}

/*
 * Free any memory allocated by a policy database structure.
 */
void policydb_destroy(struct policydb *p)
{
	struct ocontext *c, *ctmp;
	struct genfs *g, *gtmp;
	int i;
	struct role_allow *ra, *lra = NULL;
	struct role_trans *tr, *ltr = NULL;

	for (i = 0; i < SYM_NUM; i++) {
		cond_resched();
		hashtab_map(p->symtab[i].table, destroy_f[i], NULL);
		hashtab_destroy(p->symtab[i].table);
	}

	for (i = 0; i < SYM_NUM; i++) {
		if (p->sym_val_to_name[i])
			flex_array_free(p->sym_val_to_name[i]);
	}

	kfree(p->class_val_to_struct);
	kfree(p->role_val_to_struct);
	kfree(p->user_val_to_struct);
	if (p->type_val_to_struct_array)
		flex_array_free(p->type_val_to_struct_array);

	avtab_destroy(&p->te_avtab);

	for (i = 0; i < OCON_NUM; i++) {
		cond_resched();
		c = p->ocontexts[i];
		while (c) {
			ctmp = c;
			c = c->next;
			ocontext_destroy(ctmp, i);
		}
		p->ocontexts[i] = NULL;
	}

	g = p->genfs;
	while (g) {
		cond_resched();
		kfree(g->fstype);
		c = g->head;
		while (c) {
			ctmp = c;
			c = c->next;
			ocontext_destroy(ctmp, OCON_FSUSE);
		}
		gtmp = g;
		g = g->next;
		kfree(gtmp);
	}
	p->genfs = NULL;

	cond_policydb_destroy(p);

	for (tr = p->role_tr; tr; tr = tr->next) {
		cond_resched();
		kfree(ltr);
		ltr = tr;
	}
	kfree(ltr);

	for (ra = p->role_allow; ra; ra = ra->next) {
		cond_resched();
		kfree(lra);
		lra = ra;
	}
	kfree(lra);

	hashtab_map(p->filename_trans, filenametr_destroy, NULL);
	hashtab_destroy(p->filename_trans);

	hashtab_map(p->range_tr, range_tr_destroy, NULL);
	hashtab_destroy(p->range_tr);

	if (p->type_attr_map_array) {
		for (i = 0; i < p->p_types.nprim; i++) {
			struct ebitmap *e;

			e = flex_array_get(p->type_attr_map_array, i);
			if (!e)
				continue;
			ebitmap_destroy(e);
		}
		flex_array_free(p->type_attr_map_array);
	}

	ebitmap_destroy(&p->filename_trans_ttypes);
	ebitmap_destroy(&p->policycaps);
	ebitmap_destroy(&p->permissive_map);

	return;
}

/*
 * Load the initial SIDs specified in a policy database
 * structure into a SID table.
 */
int policydb_load_isids(struct policydb *p, struct sidtab *s)
{
	struct ocontext *head, *c;
	int rc;

	rc = sidtab_init(s);
	if (rc) {
		printk(KERN_ERR "SELinux:  out of memory on SID table init\n");
		goto out;
	}

	head = p->ocontexts[OCON_ISID];
	for (c = head; c; c = c->next) {
		rc = -EINVAL;
		if (!c->context[0].user) {
			printk(KERN_ERR "SELinux:  SID %s was never defined.\n",
				c->u.name);
			goto out;
		}

		rc = sidtab_insert(s, c->sid[0], &c->context[0]);
		if (rc) {
			printk(KERN_ERR "SELinux:  unable to load initial SID %s.\n",
				c->u.name);
			goto out;
		}
	}
	rc = 0;
out:
	return rc;
}

int policydb_class_isvalid(struct policydb *p, unsigned int class)
{
	if (!class || class > p->p_classes.nprim)
		return 0;
	return 1;
}

int policydb_role_isvalid(struct policydb *p, unsigned int role)
{
	if (!role || role > p->p_roles.nprim)
		return 0;
	return 1;
}

int policydb_type_isvalid(struct policydb *p, unsigned int type)
{
	if (!type || type > p->p_types.nprim)
		return 0;
	return 1;
}

/*
 * Return 1 if the fields in the security context
 * structure `c' are valid.  Return 0 otherwise.
 */
int policydb_context_isvalid(struct policydb *p, struct context *c)
{
	struct role_datum *role;
	struct user_datum *usrdatum;

	if (!c->role || c->role > p->p_roles.nprim)
		return 0;

	if (!c->user || c->user > p->p_users.nprim)
		return 0;

	if (!c->type || c->type > p->p_types.nprim)
		return 0;

	if (c->role != OBJECT_R_VAL) {
		/*
		 * Role must be authorized for the type.
		 */
		role = p->role_val_to_struct[c->role - 1];
		if (!ebitmap_get_bit(&role->types, c->type - 1))
			/* role may not be associated with type */
			return 0;

		/*
		 * User must be authorized for the role.
		 */
		usrdatum = p->user_val_to_struct[c->user - 1];
		if (!usrdatum)
			return 0;

		if (!ebitmap_get_bit(&usrdatum->roles, c->role - 1))
			/* user may not be associated with role */
			return 0;
	}

	if (!mls_context_isvalid(p, c))
		return 0;

	return 1;
}

/*
 * Read a MLS range structure from a policydb binary
 * representation file.
 */
static int mls_read_range_helper(struct mls_range *r, void *fp)
{
	__le32 buf[2];
	u32 items;
	int rc;

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		goto out;

	rc = -EINVAL;
	items = le32_to_cpu(buf[0]);
	if (items > ARRAY_SIZE(buf)) {
		printk(KERN_ERR "SELinux: mls:  range overflow\n");
		goto out;
	}

	rc = next_entry(buf, fp, sizeof(u32) * items);
	if (rc) {
		printk(KERN_ERR "SELinux: mls:  truncated range\n");
		goto out;
	}

	r->level[0].sens = le32_to_cpu(buf[0]);
	if (items > 1)
		r->level[1].sens = le32_to_cpu(buf[1]);
	else
		r->level[1].sens = r->level[0].sens;

	rc = ebitmap_read(&r->level[0].cat, fp);
	if (rc) {
		printk(KERN_ERR "SELinux: mls:  error reading low categories\n");
		goto out;
	}
	if (items > 1) {
		rc = ebitmap_read(&r->level[1].cat, fp);
		if (rc) {
			printk(KERN_ERR "SELinux: mls:  error reading high categories\n");
			goto bad_high;
		}
	} else {
		rc = ebitmap_cpy(&r->level[1].cat, &r->level[0].cat);
		if (rc) {
			printk(KERN_ERR "SELinux: mls:  out of memory\n");
			goto bad_high;
		}
	}

	return 0;
bad_high:
	ebitmap_destroy(&r->level[0].cat);
out:
	return rc;
}

/*
 * Read and validate a security context structure
 * from a policydb binary representation file.
 */
static int context_read_and_validate(struct context *c,
				     struct policydb *p,
				     void *fp)
{
	__le32 buf[3];
	int rc;

	rc = next_entry(buf, fp, sizeof buf);
	if (rc) {
		printk(KERN_ERR "SELinux: context truncated\n");
		goto out;
	}
	c->user = le32_to_cpu(buf[0]);
	c->role = le32_to_cpu(buf[1]);
	c->type = le32_to_cpu(buf[2]);
	if (p->policyvers >= POLICYDB_VERSION_MLS) {
		rc = mls_read_range_helper(&c->range, fp);
		if (rc) {
			printk(KERN_ERR "SELinux: error reading MLS range of context\n");
			goto out;
		}
	}

	rc = -EINVAL;
	if (!policydb_context_isvalid(p, c)) {
		printk(KERN_ERR "SELinux:  invalid security context\n");
		context_destroy(c);
		goto out;
	}
	rc = 0;
out:
	return rc;
}

/*
 * The following *_read functions are used to
 * read the symbol data from a policy database
 * binary representation file.
 */

static int str_read(char **strp, gfp_t flags, void *fp, u32 len)
{
	int rc;
	char *str;

	str = kmalloc(len + 1, flags);
	if (!str)
		return -ENOMEM;

	/* it's expected the caller should free the str */
	*strp = str;

	rc = next_entry(str, fp, len);
	if (rc)
		return rc;

	str[len] = '\0';
	return 0;
}

static int perm_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct perm_datum *perdatum;
	int rc;
	__le32 buf[2];
	u32 len;

	rc = -ENOMEM;
	perdatum = kzalloc(sizeof(*perdatum), GFP_KERNEL);
	if (!perdatum)
		goto bad;

	rc = next_entry(buf, fp, sizeof buf);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	perdatum->value = le32_to_cpu(buf[1]);

	rc = str_read(&key, GFP_KERNEL, fp, len);
	if (rc)
		goto bad;

	rc = hashtab_insert(h, key, perdatum);
	if (rc)
		goto bad;

	return 0;
bad:
	perm_destroy(key, perdatum, NULL);
	return rc;
}

static int common_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct common_datum *comdatum;
	__le32 buf[4];
	u32 len, nel;
	int i, rc;

	rc = -ENOMEM;
	comdatum = kzalloc(sizeof(*comdatum), GFP_KERNEL);
	if (!comdatum)
		goto bad;

	rc = next_entry(buf, fp, sizeof buf);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	comdatum->value = le32_to_cpu(buf[1]);

	rc = symtab_init(&comdatum->permissions, PERM_SYMTAB_SIZE);
	if (rc)
		goto bad;
	comdatum->permissions.nprim = le32_to_cpu(buf[2]);
	nel = le32_to_cpu(buf[3]);

	rc = str_read(&key, GFP_KERNEL, fp, len);
	if (rc)
		goto bad;

	for (i = 0; i < nel; i++) {
		rc = perm_read(p, comdatum->permissions.table, fp);
		if (rc)
			goto bad;
	}

	rc = hashtab_insert(h, key, comdatum);
	if (rc)
		goto bad;
	return 0;
bad:
	common_destroy(key, comdatum, NULL);
	return rc;
}

static void type_set_init(struct type_set *t)
{
	ebitmap_init(&t->types);
	ebitmap_init(&t->negset);
}

static int type_set_read(struct type_set *t, void *fp)
{
	__le32 buf[1];
	int rc;

	if (ebitmap_read(&t->types, fp))
		return -EINVAL;
	if (ebitmap_read(&t->negset, fp))
		return -EINVAL;

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc < 0)
		return -EINVAL;
	t->flags = le32_to_cpu(buf[0]);

	return 0;
}


static int read_cons_helper(struct policydb *p,
				struct constraint_node **nodep,
				int ncons, int allowxtarget, void *fp)
{
	struct constraint_node *c, *lc;
	struct constraint_expr *e, *le;
	__le32 buf[3];
	u32 nexpr;
	int rc, i, j, depth;

	lc = NULL;
	for (i = 0; i < ncons; i++) {
		c = kzalloc(sizeof(*c), GFP_KERNEL);
		if (!c)
			return -ENOMEM;

		if (lc)
			lc->next = c;
		else
			*nodep = c;

		rc = next_entry(buf, fp, (sizeof(u32) * 2));
		if (rc)
			return rc;
		c->permissions = le32_to_cpu(buf[0]);
		nexpr = le32_to_cpu(buf[1]);
		le = NULL;
		depth = -1;
		for (j = 0; j < nexpr; j++) {
			e = kzalloc(sizeof(*e), GFP_KERNEL);
			if (!e)
				return -ENOMEM;

			if (le)
				le->next = e;
			else
				c->expr = e;

			rc = next_entry(buf, fp, (sizeof(u32) * 3));
			if (rc)
				return rc;
			e->expr_type = le32_to_cpu(buf[0]);
			e->attr = le32_to_cpu(buf[1]);
			e->op = le32_to_cpu(buf[2]);

			switch (e->expr_type) {
			case CEXPR_NOT:
				if (depth < 0)
					return -EINVAL;
				break;
			case CEXPR_AND:
			case CEXPR_OR:
				if (depth < 1)
					return -EINVAL;
				depth--;
				break;
			case CEXPR_ATTR:
				if (depth == (CEXPR_MAXDEPTH - 1))
					return -EINVAL;
				depth++;
				break;
			case CEXPR_NAMES:
				if (!allowxtarget && (e->attr & CEXPR_XTARGET))
					return -EINVAL;
				if (depth == (CEXPR_MAXDEPTH - 1))
					return -EINVAL;
				depth++;
				rc = ebitmap_read(&e->names, fp);
				if (rc)
					return rc;
				if (p->policyvers >=
					POLICYDB_VERSION_CONSTRAINT_NAMES) {
						e->type_names = kzalloc(sizeof
						(*e->type_names),
						GFP_KERNEL);
					if (!e->type_names)
						return -ENOMEM;
					type_set_init(e->type_names);
					rc = type_set_read(e->type_names, fp);
					if (rc)
						return rc;
				}
				break;
			default:
				return -EINVAL;
			}
			le = e;
		}
		if (depth != 0)
			return -EINVAL;
		lc = c;
	}

	return 0;
}

static int class_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct class_datum *cladatum;
	__le32 buf[6];
	u32 len, len2, ncons, nel;
	int i, rc;

	rc = -ENOMEM;
	cladatum = kzalloc(sizeof(*cladatum), GFP_KERNEL);
	if (!cladatum)
		goto bad;

	rc = next_entry(buf, fp, sizeof(u32)*6);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	len2 = le32_to_cpu(buf[1]);
	cladatum->value = le32_to_cpu(buf[2]);

	rc = symtab_init(&cladatum->permissions, PERM_SYMTAB_SIZE);
	if (rc)
		goto bad;
	cladatum->permissions.nprim = le32_to_cpu(buf[3]);
	nel = le32_to_cpu(buf[4]);

	ncons = le32_to_cpu(buf[5]);

	rc = str_read(&key, GFP_KERNEL, fp, len);
	if (rc)
		goto bad;

	if (len2) {
		rc = str_read(&cladatum->comkey, GFP_KERNEL, fp, len2);
		if (rc)
			goto bad;

		rc = -EINVAL;
		cladatum->comdatum = hashtab_search(p->p_commons.table, cladatum->comkey);
		if (!cladatum->comdatum) {
			printk(KERN_ERR "SELinux:  unknown common %s\n", cladatum->comkey);
			goto bad;
		}
	}
	for (i = 0; i < nel; i++) {
		rc = perm_read(p, cladatum->permissions.table, fp);
		if (rc)
			goto bad;
	}

	rc = read_cons_helper(p, &cladatum->constraints, ncons, 0, fp);
	if (rc)
		goto bad;

	if (p->policyvers >= POLICYDB_VERSION_VALIDATETRANS) {
		/* grab the validatetrans rules */
		rc = next_entry(buf, fp, sizeof(u32));
		if (rc)
			goto bad;
		ncons = le32_to_cpu(buf[0]);
		rc = read_cons_helper(p, &cladatum->validatetrans,
				ncons, 1, fp);
		if (rc)
			goto bad;
	}

	if (p->policyvers >= POLICYDB_VERSION_NEW_OBJECT_DEFAULTS) {
		rc = next_entry(buf, fp, sizeof(u32) * 3);
		if (rc)
			goto bad;

		cladatum->default_user = le32_to_cpu(buf[0]);
		cladatum->default_role = le32_to_cpu(buf[1]);
		cladatum->default_range = le32_to_cpu(buf[2]);
	}

	if (p->policyvers >= POLICYDB_VERSION_DEFAULT_TYPE) {
		rc = next_entry(buf, fp, sizeof(u32) * 1);
		if (rc)
			goto bad;
		cladatum->default_type = le32_to_cpu(buf[0]);
	}

	rc = hashtab_insert(h, key, cladatum);
	if (rc)
		goto bad;

	return 0;
bad:
	cls_destroy(key, cladatum, NULL);
	return rc;
}

static int role_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct role_datum *role;
	int rc, to_read = 2;
	__le32 buf[3];
	u32 len;

	rc = -ENOMEM;
	role = kzalloc(sizeof(*role), GFP_KERNEL);
	if (!role)
		goto bad;

	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY)
		to_read = 3;

	rc = next_entry(buf, fp, sizeof(buf[0]) * to_read);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	role->value = le32_to_cpu(buf[1]);
	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY)
		role->bounds = le32_to_cpu(buf[2]);

	rc = str_read(&key, GFP_KERNEL, fp, len);
	if (rc)
		goto bad;

	rc = ebitmap_read(&role->dominates, fp);
	if (rc)
		goto bad;

	rc = ebitmap_read(&role->types, fp);
	if (rc)
		goto bad;

	if (strcmp(key, OBJECT_R) == 0) {
		rc = -EINVAL;
		if (role->value != OBJECT_R_VAL) {
			printk(KERN_ERR "SELinux: Role %s has wrong value %d\n",
			       OBJECT_R, role->value);
			goto bad;
		}
		rc = 0;
		goto bad;
	}

	rc = hashtab_insert(h, key, role);
	if (rc)
		goto bad;
	return 0;
bad:
	role_destroy(key, role, NULL);
	return rc;
}

static int type_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct type_datum *typdatum;
	int rc, to_read = 3;
	__le32 buf[4];
	u32 len;

	rc = -ENOMEM;
	typdatum = kzalloc(sizeof(*typdatum), GFP_KERNEL);
	if (!typdatum)
		goto bad;

	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY)
		to_read = 4;

	rc = next_entry(buf, fp, sizeof(buf[0]) * to_read);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	typdatum->value = le32_to_cpu(buf[1]);
	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY) {
		u32 prop = le32_to_cpu(buf[2]);

		if (prop & TYPEDATUM_PROPERTY_PRIMARY)
			typdatum->primary = 1;
		if (prop & TYPEDATUM_PROPERTY_ATTRIBUTE)
			typdatum->attribute = 1;

		typdatum->bounds = le32_to_cpu(buf[3]);
	} else {
		typdatum->primary = le32_to_cpu(buf[2]);
	}

	rc = str_read(&key, GFP_KERNEL, fp, len);
	if (rc)
		goto bad;

	rc = hashtab_insert(h, key, typdatum);
	if (rc)
		goto bad;
	return 0;
bad:
	type_destroy(key, typdatum, NULL);
	return rc;
}


/*
 * Read a MLS level structure from a policydb binary
 * representation file.
 */
static int mls_read_level(struct mls_level *lp, void *fp)
{
	__le32 buf[1];
	int rc;

	memset(lp, 0, sizeof(*lp));

	rc = next_entry(buf, fp, sizeof buf);
	if (rc) {
		printk(KERN_ERR "SELinux: mls: truncated level\n");
		return rc;
	}
	lp->sens = le32_to_cpu(buf[0]);

	rc = ebitmap_read(&lp->cat, fp);
	if (rc) {
		printk(KERN_ERR "SELinux: mls:  error reading level categories\n");
		return rc;
	}
	return 0;
}

static int user_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct user_datum *usrdatum;
	int rc, to_read = 2;
	__le32 buf[3];
	u32 len;

	rc = -ENOMEM;
	usrdatum = kzalloc(sizeof(*usrdatum), GFP_KERNEL);
	if (!usrdatum)
		goto bad;

	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY)
		to_read = 3;

	rc = next_entry(buf, fp, sizeof(buf[0]) * to_read);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	usrdatum->value = le32_to_cpu(buf[1]);
	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY)
		usrdatum->bounds = le32_to_cpu(buf[2]);

	rc = str_read(&key, GFP_KERNEL, fp, len);
	if (rc)
		goto bad;

	rc = ebitmap_read(&usrdatum->roles, fp);
	if (rc)
		goto bad;

	if (p->policyvers >= POLICYDB_VERSION_MLS) {
		rc = mls_read_range_helper(&usrdatum->range, fp);
		if (rc)
			goto bad;
		rc = mls_read_level(&usrdatum->dfltlevel, fp);
		if (rc)
			goto bad;
	}

	rc = hashtab_insert(h, key, usrdatum);
	if (rc)
		goto bad;
	return 0;
bad:
	user_destroy(key, usrdatum, NULL);
	return rc;
}

static int sens_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct level_datum *levdatum;
	int rc;
	__le32 buf[2];
	u32 len;

	rc = -ENOMEM;
	levdatum = kzalloc(sizeof(*levdatum), GFP_ATOMIC);
	if (!levdatum)
		goto bad;

	rc = next_entry(buf, fp, sizeof buf);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	levdatum->isalias = le32_to_cpu(buf[1]);

	rc = str_read(&key, GFP_ATOMIC, fp, len);
	if (rc)
		goto bad;

	rc = -ENOMEM;
	levdatum->level = kmalloc(sizeof(struct mls_level), GFP_ATOMIC);
	if (!levdatum->level)
		goto bad;

	rc = mls_read_level(levdatum->level, fp);
	if (rc)
		goto bad;

	rc = hashtab_insert(h, key, levdatum);
	if (rc)
		goto bad;
	return 0;
bad:
	sens_destroy(key, levdatum, NULL);
	return rc;
}

static int cat_read(struct policydb *p, struct hashtab *h, void *fp)
{
	char *key = NULL;
	struct cat_datum *catdatum;
	int rc;
	__le32 buf[3];
	u32 len;

	rc = -ENOMEM;
	catdatum = kzalloc(sizeof(*catdatum), GFP_ATOMIC);
	if (!catdatum)
		goto bad;

	rc = next_entry(buf, fp, sizeof buf);
	if (rc)
		goto bad;

	len = le32_to_cpu(buf[0]);
	catdatum->value = le32_to_cpu(buf[1]);
	catdatum->isalias = le32_to_cpu(buf[2]);

	rc = str_read(&key, GFP_ATOMIC, fp, len);
	if (rc)
		goto bad;

	rc = hashtab_insert(h, key, catdatum);
	if (rc)
		goto bad;
	return 0;
bad:
	cat_destroy(key, catdatum, NULL);
	return rc;
}

static int (*read_f[SYM_NUM]) (struct policydb *p, struct hashtab *h, void *fp) =
{
	common_read,
	class_read,
	role_read,
	type_read,
	user_read,
	cond_read_bool,
	sens_read,
	cat_read,
};

static int user_bounds_sanity_check(void *key, void *datum, void *datap)
{
	struct user_datum *upper, *user;
	struct policydb *p = datap;
	int depth = 0;

	upper = user = datum;
	while (upper->bounds) {
		struct ebitmap_node *node;
		unsigned long bit;

		if (++depth == POLICYDB_BOUNDS_MAXDEPTH) {
			printk(KERN_ERR "SELinux: user %s: "
			       "too deep or looped boundary",
			       (char *) key);
			return -EINVAL;
		}

		upper = p->user_val_to_struct[upper->bounds - 1];
		ebitmap_for_each_positive_bit(&user->roles, node, bit) {
			if (ebitmap_get_bit(&upper->roles, bit))
				continue;

			printk(KERN_ERR
			       "SELinux: boundary violated policy: "
			       "user=%s role=%s bounds=%s\n",
			       sym_name(p, SYM_USERS, user->value - 1),
			       sym_name(p, SYM_ROLES, bit),
			       sym_name(p, SYM_USERS, upper->value - 1));

			return -EINVAL;
		}
	}

	return 0;
}

static int role_bounds_sanity_check(void *key, void *datum, void *datap)
{
	struct role_datum *upper, *role;
	struct policydb *p = datap;
	int depth = 0;

	upper = role = datum;
	while (upper->bounds) {
		struct ebitmap_node *node;
		unsigned long bit;

		if (++depth == POLICYDB_BOUNDS_MAXDEPTH) {
			printk(KERN_ERR "SELinux: role %s: "
			       "too deep or looped bounds\n",
			       (char *) key);
			return -EINVAL;
		}

		upper = p->role_val_to_struct[upper->bounds - 1];
		ebitmap_for_each_positive_bit(&role->types, node, bit) {
			if (ebitmap_get_bit(&upper->types, bit))
				continue;

			printk(KERN_ERR
			       "SELinux: boundary violated policy: "
			       "role=%s type=%s bounds=%s\n",
			       sym_name(p, SYM_ROLES, role->value - 1),
			       sym_name(p, SYM_TYPES, bit),
			       sym_name(p, SYM_ROLES, upper->value - 1));

			return -EINVAL;
		}
	}

	return 0;
}

static int type_bounds_sanity_check(void *key, void *datum, void *datap)
{
	struct type_datum *upper;
	struct policydb *p = datap;
	int depth = 0;

	upper = datum;
	while (upper->bounds) {
		if (++depth == POLICYDB_BOUNDS_MAXDEPTH) {
			printk(KERN_ERR "SELinux: type %s: "
			       "too deep or looped boundary\n",
			       (char *) key);
			return -EINVAL;
		}

		upper = flex_array_get_ptr(p->type_val_to_struct_array,
					   upper->bounds - 1);
		BUG_ON(!upper);

		if (upper->attribute) {
			printk(KERN_ERR "SELinux: type %s: "
			       "bounded by attribute %s",
			       (char *) key,
			       sym_name(p, SYM_TYPES, upper->value - 1));
			return -EINVAL;
		}
	}

	return 0;
}

static int policydb_bounds_sanity_check(struct policydb *p)
{
	int rc;

	if (p->policyvers < POLICYDB_VERSION_BOUNDARY)
		return 0;

	rc = hashtab_map(p->p_users.table,
			 user_bounds_sanity_check, p);
	if (rc)
		return rc;

	rc = hashtab_map(p->p_roles.table,
			 role_bounds_sanity_check, p);
	if (rc)
		return rc;

	rc = hashtab_map(p->p_types.table,
			 type_bounds_sanity_check, p);
	if (rc)
		return rc;

	return 0;
}

u16 string_to_security_class(struct policydb *p, const char *name)
{
	struct class_datum *cladatum;

	cladatum = hashtab_search(p->p_classes.table, name);
	if (!cladatum)
		return 0;

	return cladatum->value;
}

u32 string_to_av_perm(struct policydb *p, u16 tclass, const char *name)
{
	struct class_datum *cladatum;
	struct perm_datum *perdatum = NULL;
	struct common_datum *comdatum;

	if (!tclass || tclass > p->p_classes.nprim)
		return 0;

	cladatum = p->class_val_to_struct[tclass-1];
	comdatum = cladatum->comdatum;
	if (comdatum)
		perdatum = hashtab_search(comdatum->permissions.table,
					  name);
	if (!perdatum)
		perdatum = hashtab_search(cladatum->permissions.table,
					  name);
	if (!perdatum)
		return 0;

	return 1U << (perdatum->value-1);
}

static int range_read(struct policydb *p, void *fp)
{
	struct range_trans *rt = NULL;
	struct mls_range *r = NULL;
	int i, rc;
	__le32 buf[2];
	u32 nel;

	if (p->policyvers < POLICYDB_VERSION_MLS)
		return 0;

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		goto out;

	nel = le32_to_cpu(buf[0]);
	for (i = 0; i < nel; i++) {
		rc = -ENOMEM;
		rt = kzalloc(sizeof(*rt), GFP_KERNEL);
		if (!rt)
			goto out;

		rc = next_entry(buf, fp, (sizeof(u32) * 2));
		if (rc)
			goto out;

		rt->source_type = le32_to_cpu(buf[0]);
		rt->target_type = le32_to_cpu(buf[1]);
		if (p->policyvers >= POLICYDB_VERSION_RANGETRANS) {
			rc = next_entry(buf, fp, sizeof(u32));
			if (rc)
				goto out;
			rt->target_class = le32_to_cpu(buf[0]);
		} else
			rt->target_class = p->process_class;

		rc = -EINVAL;
		if (!policydb_type_isvalid(p, rt->source_type) ||
		    !policydb_type_isvalid(p, rt->target_type) ||
		    !policydb_class_isvalid(p, rt->target_class))
			goto out;

		rc = -ENOMEM;
		r = kzalloc(sizeof(*r), GFP_KERNEL);
		if (!r)
			goto out;

		rc = mls_read_range_helper(r, fp);
		if (rc)
			goto out;

		rc = -EINVAL;
		if (!mls_range_isvalid(p, r)) {
			printk(KERN_WARNING "SELinux:  rangetrans:  invalid range\n");
			goto out;
		}

		rc = hashtab_insert(p->range_tr, rt, r);
		if (rc)
			goto out;

		rt = NULL;
		r = NULL;
	}
	hash_eval(p->range_tr, "rangetr");
	rc = 0;
out:
	kfree(rt);
	kfree(r);
	return rc;
}

static int filename_trans_read(struct policydb *p, void *fp)
{
	struct filename_trans *ft;
	struct filename_trans_datum *otype;
	char *name;
	u32 nel, len;
	__le32 buf[4];
	int rc, i;

	if (p->policyvers < POLICYDB_VERSION_FILENAME_TRANS)
		return 0;

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		return rc;
	nel = le32_to_cpu(buf[0]);

	for (i = 0; i < nel; i++) {
		ft = NULL;
		otype = NULL;
		name = NULL;

		rc = -ENOMEM;
		ft = kzalloc(sizeof(*ft), GFP_KERNEL);
		if (!ft)
			goto out;

		rc = -ENOMEM;
		otype = kmalloc(sizeof(*otype), GFP_KERNEL);
		if (!otype)
			goto out;

		/* length of the path component string */
		rc = next_entry(buf, fp, sizeof(u32));
		if (rc)
			goto out;
		len = le32_to_cpu(buf[0]);

		/* path component string */
		rc = str_read(&name, GFP_KERNEL, fp, len);
		if (rc)
			goto out;

		ft->name = name;

		rc = next_entry(buf, fp, sizeof(u32) * 4);
		if (rc)
			goto out;

		ft->stype = le32_to_cpu(buf[0]);
		ft->ttype = le32_to_cpu(buf[1]);
		ft->tclass = le32_to_cpu(buf[2]);

		otype->otype = le32_to_cpu(buf[3]);

		rc = ebitmap_set_bit(&p->filename_trans_ttypes, ft->ttype, 1);
		if (rc)
			goto out;

		rc = hashtab_insert(p->filename_trans, ft, otype);
		if (rc) {
			/*
			 * Do not return -EEXIST to the caller, or the system
			 * will not boot.
			 */
			if (rc != -EEXIST)
				goto out;
			/* But free memory to avoid memory leak. */
			kfree(ft);
			kfree(name);
			kfree(otype);
		}
	}
	hash_eval(p->filename_trans, "filenametr");
	return 0;
out:
	kfree(ft);
	kfree(name);
	kfree(otype);

	return rc;
}

static int genfs_read(struct policydb *p, void *fp)
{
	int i, j, rc;
	u32 nel, nel2, len, len2;
	__le32 buf[1];
	struct ocontext *l, *c;
	struct ocontext *newc = NULL;
	struct genfs *genfs_p, *genfs;
	struct genfs *newgenfs = NULL;

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		goto out;
	nel = le32_to_cpu(buf[0]);

	for (i = 0; i < nel; i++) {
		rc = next_entry(buf, fp, sizeof(u32));
		if (rc)
			goto out;
		len = le32_to_cpu(buf[0]);

		rc = -ENOMEM;
		newgenfs = kzalloc(sizeof(*newgenfs), GFP_KERNEL);
		if (!newgenfs)
			goto out;

		rc = str_read(&newgenfs->fstype, GFP_KERNEL, fp, len);
		if (rc)
			goto out;

		for (genfs_p = NULL, genfs = p->genfs; genfs;
		     genfs_p = genfs, genfs = genfs->next) {
			rc = -EINVAL;
			if (strcmp(newgenfs->fstype, genfs->fstype) == 0) {
				printk(KERN_ERR "SELinux:  dup genfs fstype %s\n",
				       newgenfs->fstype);
				goto out;
			}
			if (strcmp(newgenfs->fstype, genfs->fstype) < 0)
				break;
		}
		newgenfs->next = genfs;
		if (genfs_p)
			genfs_p->next = newgenfs;
		else
			p->genfs = newgenfs;
		genfs = newgenfs;
		newgenfs = NULL;

		rc = next_entry(buf, fp, sizeof(u32));
		if (rc)
			goto out;

		nel2 = le32_to_cpu(buf[0]);
		for (j = 0; j < nel2; j++) {
			rc = next_entry(buf, fp, sizeof(u32));
			if (rc)
				goto out;
			len = le32_to_cpu(buf[0]);

			rc = -ENOMEM;
			newc = kzalloc(sizeof(*newc), GFP_KERNEL);
			if (!newc)
				goto out;

			rc = str_read(&newc->u.name, GFP_KERNEL, fp, len);
			if (rc)
				goto out;

			rc = next_entry(buf, fp, sizeof(u32));
			if (rc)
				goto out;

			newc->v.sclass = le32_to_cpu(buf[0]);
			rc = context_read_and_validate(&newc->context[0], p, fp);
			if (rc)
				goto out;

			for (l = NULL, c = genfs->head; c;
			     l = c, c = c->next) {
				rc = -EINVAL;
				if (!strcmp(newc->u.name, c->u.name) &&
				    (!c->v.sclass || !newc->v.sclass ||
				     newc->v.sclass == c->v.sclass)) {
					printk(KERN_ERR "SELinux:  dup genfs entry (%s,%s)\n",
					       genfs->fstype, c->u.name);
					goto out;
				}
				len = strlen(newc->u.name);
				len2 = strlen(c->u.name);
				if (len > len2)
					break;
			}

			newc->next = c;
			if (l)
				l->next = newc;
			else
				genfs->head = newc;
			newc = NULL;
		}
	}
	rc = 0;
out:
	if (newgenfs)
		kfree(newgenfs->fstype);
	kfree(newgenfs);
	ocontext_destroy(newc, OCON_FSUSE);

	return rc;
}

static int ocontext_read(struct policydb *p, struct policydb_compat_info *info,
			 void *fp)
{
	int i, j, rc;
	u32 nel, len;
	__le32 buf[3];
	struct ocontext *l, *c;
	u32 nodebuf[8];

	for (i = 0; i < info->ocon_num; i++) {
		rc = next_entry(buf, fp, sizeof(u32));
		if (rc)
			goto out;
		nel = le32_to_cpu(buf[0]);

		l = NULL;
		for (j = 0; j < nel; j++) {
			rc = -ENOMEM;
			c = kzalloc(sizeof(*c), GFP_KERNEL);
			if (!c)
				goto out;
			if (l)
				l->next = c;
			else
				p->ocontexts[i] = c;
			l = c;

			switch (i) {
			case OCON_ISID:
				rc = next_entry(buf, fp, sizeof(u32));
				if (rc)
					goto out;

				c->sid[0] = le32_to_cpu(buf[0]);
				rc = context_read_and_validate(&c->context[0], p, fp);
				if (rc)
					goto out;
				break;
			case OCON_FS:
			case OCON_NETIF:
				rc = next_entry(buf, fp, sizeof(u32));
				if (rc)
					goto out;
				len = le32_to_cpu(buf[0]);

				rc = str_read(&c->u.name, GFP_KERNEL, fp, len);
				if (rc)
					goto out;

				rc = context_read_and_validate(&c->context[0], p, fp);
				if (rc)
					goto out;
				rc = context_read_and_validate(&c->context[1], p, fp);
				if (rc)
					goto out;
				break;
			case OCON_PORT:
				rc = next_entry(buf, fp, sizeof(u32)*3);
				if (rc)
					goto out;
				c->u.port.protocol = le32_to_cpu(buf[0]);
				c->u.port.low_port = le32_to_cpu(buf[1]);
				c->u.port.high_port = le32_to_cpu(buf[2]);
				rc = context_read_and_validate(&c->context[0], p, fp);
				if (rc)
					goto out;
				break;
			case OCON_NODE:
				rc = next_entry(nodebuf, fp, sizeof(u32) * 2);
				if (rc)
					goto out;
				c->u.node.addr = nodebuf[0]; /* network order */
				c->u.node.mask = nodebuf[1]; /* network order */
				rc = context_read_and_validate(&c->context[0], p, fp);
				if (rc)
					goto out;
				break;
			case OCON_FSUSE:
				rc = next_entry(buf, fp, sizeof(u32)*2);
				if (rc)
					goto out;

				rc = -EINVAL;
				c->v.behavior = le32_to_cpu(buf[0]);
				/* Determined at runtime, not in policy DB. */
				if (c->v.behavior == SECURITY_FS_USE_MNTPOINT)
					goto out;
				if (c->v.behavior > SECURITY_FS_USE_MAX)
					goto out;

				len = le32_to_cpu(buf[1]);
				rc = str_read(&c->u.name, GFP_KERNEL, fp, len);
				if (rc)
					goto out;

				rc = context_read_and_validate(&c->context[0], p, fp);
				if (rc)
					goto out;
				break;
			case OCON_NODE6: {
				int k;

				rc = next_entry(nodebuf, fp, sizeof(u32) * 8);
				if (rc)
					goto out;
				for (k = 0; k < 4; k++)
					c->u.node6.addr[k] = nodebuf[k];
				for (k = 0; k < 4; k++)
					c->u.node6.mask[k] = nodebuf[k+4];
				rc = context_read_and_validate(&c->context[0], p, fp);
				if (rc)
					goto out;
				break;
			}
			}
		}
	}
	rc = 0;
out:
	return rc;
}

/*
 * Read the configuration data from a policy database binary
 * representation file into a policy database structure.
 */
int policydb_read(struct policydb *p, void *fp)
{
	struct role_allow *ra, *lra;
	struct role_trans *tr, *ltr;
	int i, j, rc;
	__le32 buf[4];
	u32 len, nprim, nel;

	char *policydb_str;
	struct policydb_compat_info *info;

	rc = policydb_init(p);
	if (rc)
		return rc;

	/* Read the magic number and string length. */
	rc = next_entry(buf, fp, sizeof(u32) * 2);
	if (rc)
		goto bad;

	rc = -EINVAL;
	if (le32_to_cpu(buf[0]) != POLICYDB_MAGIC) {
		printk(KERN_ERR "SELinux:  policydb magic number 0x%x does "
		       "not match expected magic number 0x%x\n",
		       le32_to_cpu(buf[0]), POLICYDB_MAGIC);
		goto bad;
	}

	rc = -EINVAL;
	len = le32_to_cpu(buf[1]);
	if (len != strlen(POLICYDB_STRING)) {
		printk(KERN_ERR "SELinux:  policydb string length %d does not "
		       "match expected length %Zu\n",
		       len, strlen(POLICYDB_STRING));
		goto bad;
	}

	rc = -ENOMEM;
	policydb_str = kmalloc(len + 1, GFP_KERNEL);
	if (!policydb_str) {
		printk(KERN_ERR "SELinux:  unable to allocate memory for policydb "
		       "string of length %d\n", len);
		goto bad;
	}

	rc = next_entry(policydb_str, fp, len);
	if (rc) {
		printk(KERN_ERR "SELinux:  truncated policydb string identifier\n");
		kfree(policydb_str);
		goto bad;
	}

	rc = -EINVAL;
	policydb_str[len] = '\0';
	if (strcmp(policydb_str, POLICYDB_STRING)) {
		printk(KERN_ERR "SELinux:  policydb string %s does not match "
		       "my string %s\n", policydb_str, POLICYDB_STRING);
		kfree(policydb_str);
		goto bad;
	}
	/* Done with policydb_str. */
	kfree(policydb_str);
	policydb_str = NULL;

	/* Read the version and table sizes. */
	rc = next_entry(buf, fp, sizeof(u32)*4);
	if (rc)
		goto bad;

	rc = -EINVAL;
	p->policyvers = le32_to_cpu(buf[0]);
	if (p->policyvers < POLICYDB_VERSION_MIN ||
	    p->policyvers > POLICYDB_VERSION_MAX) {
		printk(KERN_ERR "SELinux:  policydb version %d does not match "
		       "my version range %d-%d\n",
		       le32_to_cpu(buf[0]), POLICYDB_VERSION_MIN, POLICYDB_VERSION_MAX);
		goto bad;
	}

	if ((le32_to_cpu(buf[1]) & POLICYDB_CONFIG_MLS)) {
		p->mls_enabled = 1;

		rc = -EINVAL;
		if (p->policyvers < POLICYDB_VERSION_MLS) {
			printk(KERN_ERR "SELinux: security policydb version %d "
				"(MLS) not backwards compatible\n",
				p->policyvers);
			goto bad;
		}
	}
	p->reject_unknown = !!(le32_to_cpu(buf[1]) & REJECT_UNKNOWN);
	p->allow_unknown = !!(le32_to_cpu(buf[1]) & ALLOW_UNKNOWN);

	if (p->policyvers >= POLICYDB_VERSION_POLCAP) {
		rc = ebitmap_read(&p->policycaps, fp);
		if (rc)
			goto bad;
	}

	if (p->policyvers >= POLICYDB_VERSION_PERMISSIVE) {
		rc = ebitmap_read(&p->permissive_map, fp);
		if (rc)
			goto bad;
	}

	rc = -EINVAL;
	info = policydb_lookup_compat(p->policyvers);
	if (!info) {
		printk(KERN_ERR "SELinux:  unable to find policy compat info "
		       "for version %d\n", p->policyvers);
		goto bad;
	}

	rc = -EINVAL;
	if (le32_to_cpu(buf[2]) != info->sym_num ||
		le32_to_cpu(buf[3]) != info->ocon_num) {
		printk(KERN_ERR "SELinux:  policydb table sizes (%d,%d) do "
		       "not match mine (%d,%d)\n", le32_to_cpu(buf[2]),
			le32_to_cpu(buf[3]),
		       info->sym_num, info->ocon_num);
		goto bad;
	}

	for (i = 0; i < info->sym_num; i++) {
		rc = next_entry(buf, fp, sizeof(u32)*2);
		if (rc)
			goto bad;
		nprim = le32_to_cpu(buf[0]);
		nel = le32_to_cpu(buf[1]);
		for (j = 0; j < nel; j++) {
			rc = read_f[i](p, p->symtab[i].table, fp);
			if (rc)
				goto bad;
		}

		p->symtab[i].nprim = nprim;
	}

	rc = -EINVAL;
	p->process_class = string_to_security_class(p, "process");
	if (!p->process_class)
		goto bad;

	rc = avtab_read(&p->te_avtab, fp, p);
	if (rc)
		goto bad;

	if (p->policyvers >= POLICYDB_VERSION_BOOL) {
		rc = cond_read_list(p, fp);
		if (rc)
			goto bad;
	}

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		goto bad;
	nel = le32_to_cpu(buf[0]);
	ltr = NULL;
	for (i = 0; i < nel; i++) {
		rc = -ENOMEM;
		tr = kzalloc(sizeof(*tr), GFP_KERNEL);
		if (!tr)
			goto bad;
		if (ltr)
			ltr->next = tr;
		else
			p->role_tr = tr;
		rc = next_entry(buf, fp, sizeof(u32)*3);
		if (rc)
			goto bad;

		rc = -EINVAL;
		tr->role = le32_to_cpu(buf[0]);
		tr->type = le32_to_cpu(buf[1]);
		tr->new_role = le32_to_cpu(buf[2]);
		if (p->policyvers >= POLICYDB_VERSION_ROLETRANS) {
			rc = next_entry(buf, fp, sizeof(u32));
			if (rc)
				goto bad;
			tr->tclass = le32_to_cpu(buf[0]);
		} else
			tr->tclass = p->process_class;

		if (!policydb_role_isvalid(p, tr->role) ||
		    !policydb_type_isvalid(p, tr->type) ||
		    !policydb_class_isvalid(p, tr->tclass) ||
		    !policydb_role_isvalid(p, tr->new_role))
			goto bad;
		ltr = tr;
	}

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc)
		goto bad;
	nel = le32_to_cpu(buf[0]);
	lra = NULL;
	for (i = 0; i < nel; i++) {
		rc = -ENOMEM;
		ra = kzalloc(sizeof(*ra), GFP_KERNEL);
		if (!ra)
			goto bad;
		if (lra)
			lra->next = ra;
		else
			p->role_allow = ra;
		rc = next_entry(buf, fp, sizeof(u32)*2);
		if (rc)
			goto bad;

		rc = -EINVAL;
		ra->role = le32_to_cpu(buf[0]);
		ra->new_role = le32_to_cpu(buf[1]);
		if (!policydb_role_isvalid(p, ra->role) ||
		    !policydb_role_isvalid(p, ra->new_role))
			goto bad;
		lra = ra;
	}

	rc = filename_trans_read(p, fp);
	if (rc)
		goto bad;

	rc = policydb_index(p);
	if (rc)
		goto bad;

	rc = -EINVAL;
	p->process_trans_perms = string_to_av_perm(p, p->process_class, "transition");
	p->process_trans_perms |= string_to_av_perm(p, p->process_class, "dyntransition");
	if (!p->process_trans_perms)
		goto bad;

	rc = ocontext_read(p, info, fp);
	if (rc)
		goto bad;

	rc = genfs_read(p, fp);
	if (rc)
		goto bad;

	rc = range_read(p, fp);
	if (rc)
		goto bad;

	rc = -ENOMEM;
	p->type_attr_map_array = flex_array_alloc(sizeof(struct ebitmap),
						  p->p_types.nprim,
						  GFP_KERNEL | __GFP_ZERO);
	if (!p->type_attr_map_array)
		goto bad;

	/* preallocate so we don't have to worry about the put ever failing */
	rc = flex_array_prealloc(p->type_attr_map_array, 0, p->p_types.nprim,
				 GFP_KERNEL | __GFP_ZERO);
	if (rc)
		goto bad;

	for (i = 0; i < p->p_types.nprim; i++) {
		struct ebitmap *e = flex_array_get(p->type_attr_map_array, i);

		BUG_ON(!e);
		ebitmap_init(e);
		if (p->policyvers >= POLICYDB_VERSION_AVTAB) {
			rc = ebitmap_read(e, fp);
			if (rc)
				goto bad;
		}
		/* add the type itself as the degenerate case */
		rc = ebitmap_set_bit(e, i, 1);
		if (rc)
			goto bad;
	}

	rc = policydb_bounds_sanity_check(p);
	if (rc)
		goto bad;

	rc = 0;
out:
	return rc;
bad:
	policydb_destroy(p);
	goto out;
}

/*
 * Write a MLS level structure to a policydb binary
 * representation file.
 */
static int mls_write_level(struct mls_level *l, void *fp)
{
	__le32 buf[1];
	int rc;

	buf[0] = cpu_to_le32(l->sens);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	rc = ebitmap_write(&l->cat, fp);
	if (rc)
		return rc;

	return 0;
}

/*
 * Write a MLS range structure to a policydb binary
 * representation file.
 */
static int mls_write_range_helper(struct mls_range *r, void *fp)
{
	__le32 buf[3];
	size_t items;
	int rc, eq;

	eq = mls_level_eq(&r->level[1], &r->level[0]);

	if (eq)
		items = 2;
	else
		items = 3;
	buf[0] = cpu_to_le32(items-1);
	buf[1] = cpu_to_le32(r->level[0].sens);
	if (!eq)
		buf[2] = cpu_to_le32(r->level[1].sens);

	BUG_ON(items > ARRAY_SIZE(buf));

	rc = put_entry(buf, sizeof(u32), items, fp);
	if (rc)
		return rc;

	rc = ebitmap_write(&r->level[0].cat, fp);
	if (rc)
		return rc;
	if (!eq) {
		rc = ebitmap_write(&r->level[1].cat, fp);
		if (rc)
			return rc;
	}

	return 0;
}

static int sens_write(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct level_datum *levdatum = datum;
	struct policy_data *pd = ptr;
	void *fp = pd->fp;
	__le32 buf[2];
	size_t len;
	int rc;

	len = strlen(key);
	buf[0] = cpu_to_le32(len);
	buf[1] = cpu_to_le32(levdatum->isalias);
	rc = put_entry(buf, sizeof(u32), 2, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	rc = mls_write_level(levdatum->level, fp);
	if (rc)
		return rc;

	return 0;
}

static int cat_write(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct cat_datum *catdatum = datum;
	struct policy_data *pd = ptr;
	void *fp = pd->fp;
	__le32 buf[3];
	size_t len;
	int rc;

	len = strlen(key);
	buf[0] = cpu_to_le32(len);
	buf[1] = cpu_to_le32(catdatum->value);
	buf[2] = cpu_to_le32(catdatum->isalias);
	rc = put_entry(buf, sizeof(u32), 3, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	return 0;
}

static int role_trans_write(struct policydb *p, void *fp)
{
	struct role_trans *r = p->role_tr;
	struct role_trans *tr;
	u32 buf[3];
	size_t nel;
	int rc;

	nel = 0;
	for (tr = r; tr; tr = tr->next)
		nel++;
	buf[0] = cpu_to_le32(nel);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;
	for (tr = r; tr; tr = tr->next) {
		buf[0] = cpu_to_le32(tr->role);
		buf[1] = cpu_to_le32(tr->type);
		buf[2] = cpu_to_le32(tr->new_role);
		rc = put_entry(buf, sizeof(u32), 3, fp);
		if (rc)
			return rc;
		if (p->policyvers >= POLICYDB_VERSION_ROLETRANS) {
			buf[0] = cpu_to_le32(tr->tclass);
			rc = put_entry(buf, sizeof(u32), 1, fp);
			if (rc)
				return rc;
		}
	}

	return 0;
}

static int role_allow_write(struct role_allow *r, void *fp)
{
	struct role_allow *ra;
	u32 buf[2];
	size_t nel;
	int rc;

	nel = 0;
	for (ra = r; ra; ra = ra->next)
		nel++;
	buf[0] = cpu_to_le32(nel);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;
	for (ra = r; ra; ra = ra->next) {
		buf[0] = cpu_to_le32(ra->role);
		buf[1] = cpu_to_le32(ra->new_role);
		rc = put_entry(buf, sizeof(u32), 2, fp);
		if (rc)
			return rc;
	}
	return 0;
}

/*
 * Write a security context structure
 * to a policydb binary representation file.
 */
static int context_write(struct policydb *p, struct context *c,
			 void *fp)
{
	int rc;
	__le32 buf[3];

	buf[0] = cpu_to_le32(c->user);
	buf[1] = cpu_to_le32(c->role);
	buf[2] = cpu_to_le32(c->type);

	rc = put_entry(buf, sizeof(u32), 3, fp);
	if (rc)
		return rc;

	rc = mls_write_range_helper(&c->range, fp);
	if (rc)
		return rc;

	return 0;
}

/*
 * The following *_write functions are used to
 * write the symbol data to a policy database
 * binary representation file.
 */

static int perm_write(void *vkey, void *datum, void *fp)
{
	char *key = vkey;
	struct perm_datum *perdatum = datum;
	__le32 buf[2];
	size_t len;
	int rc;

	len = strlen(key);
	buf[0] = cpu_to_le32(len);
	buf[1] = cpu_to_le32(perdatum->value);
	rc = put_entry(buf, sizeof(u32), 2, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	return 0;
}

static int common_write(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct common_datum *comdatum = datum;
	struct policy_data *pd = ptr;
	void *fp = pd->fp;
	__le32 buf[4];
	size_t len;
	int rc;

	len = strlen(key);
	buf[0] = cpu_to_le32(len);
	buf[1] = cpu_to_le32(comdatum->value);
	buf[2] = cpu_to_le32(comdatum->permissions.nprim);
	buf[3] = cpu_to_le32(comdatum->permissions.table->nel);
	rc = put_entry(buf, sizeof(u32), 4, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	rc = hashtab_map(comdatum->permissions.table, perm_write, fp);
	if (rc)
		return rc;

	return 0;
}

static int type_set_write(struct type_set *t, void *fp)
{
	int rc;
	__le32 buf[1];

	if (ebitmap_write(&t->types, fp))
		return -EINVAL;
	if (ebitmap_write(&t->negset, fp))
		return -EINVAL;

	buf[0] = cpu_to_le32(t->flags);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return -EINVAL;

	return 0;
}

static int write_cons_helper(struct policydb *p, struct constraint_node *node,
			     void *fp)
{
	struct constraint_node *c;
	struct constraint_expr *e;
	__le32 buf[3];
	u32 nel;
	int rc;

	for (c = node; c; c = c->next) {
		nel = 0;
		for (e = c->expr; e; e = e->next)
			nel++;
		buf[0] = cpu_to_le32(c->permissions);
		buf[1] = cpu_to_le32(nel);
		rc = put_entry(buf, sizeof(u32), 2, fp);
		if (rc)
			return rc;
		for (e = c->expr; e; e = e->next) {
			buf[0] = cpu_to_le32(e->expr_type);
			buf[1] = cpu_to_le32(e->attr);
			buf[2] = cpu_to_le32(e->op);
			rc = put_entry(buf, sizeof(u32), 3, fp);
			if (rc)
				return rc;

			switch (e->expr_type) {
			case CEXPR_NAMES:
				rc = ebitmap_write(&e->names, fp);
				if (rc)
					return rc;
				if (p->policyvers >=
					POLICYDB_VERSION_CONSTRAINT_NAMES) {
					rc = type_set_write(e->type_names, fp);
					if (rc)
						return rc;
				}
				break;
			default:
				break;
			}
		}
	}

	return 0;
}

static int class_write(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct class_datum *cladatum = datum;
	struct policy_data *pd = ptr;
	void *fp = pd->fp;
	struct policydb *p = pd->p;
	struct constraint_node *c;
	__le32 buf[6];
	u32 ncons;
	size_t len, len2;
	int rc;

	len = strlen(key);
	if (cladatum->comkey)
		len2 = strlen(cladatum->comkey);
	else
		len2 = 0;

	ncons = 0;
	for (c = cladatum->constraints; c; c = c->next)
		ncons++;

	buf[0] = cpu_to_le32(len);
	buf[1] = cpu_to_le32(len2);
	buf[2] = cpu_to_le32(cladatum->value);
	buf[3] = cpu_to_le32(cladatum->permissions.nprim);
	if (cladatum->permissions.table)
		buf[4] = cpu_to_le32(cladatum->permissions.table->nel);
	else
		buf[4] = 0;
	buf[5] = cpu_to_le32(ncons);
	rc = put_entry(buf, sizeof(u32), 6, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	if (cladatum->comkey) {
		rc = put_entry(cladatum->comkey, 1, len2, fp);
		if (rc)
			return rc;
	}

	rc = hashtab_map(cladatum->permissions.table, perm_write, fp);
	if (rc)
		return rc;

	rc = write_cons_helper(p, cladatum->constraints, fp);
	if (rc)
		return rc;

	/* write out the validatetrans rule */
	ncons = 0;
	for (c = cladatum->validatetrans; c; c = c->next)
		ncons++;

	buf[0] = cpu_to_le32(ncons);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	rc = write_cons_helper(p, cladatum->validatetrans, fp);
	if (rc)
		return rc;

	if (p->policyvers >= POLICYDB_VERSION_NEW_OBJECT_DEFAULTS) {
		buf[0] = cpu_to_le32(cladatum->default_user);
		buf[1] = cpu_to_le32(cladatum->default_role);
		buf[2] = cpu_to_le32(cladatum->default_range);

		rc = put_entry(buf, sizeof(uint32_t), 3, fp);
		if (rc)
			return rc;
	}

	if (p->policyvers >= POLICYDB_VERSION_DEFAULT_TYPE) {
		buf[0] = cpu_to_le32(cladatum->default_type);
		rc = put_entry(buf, sizeof(uint32_t), 1, fp);
		if (rc)
			return rc;
	}

	return 0;
}

static int role_write(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct role_datum *role = datum;
	struct policy_data *pd = ptr;
	void *fp = pd->fp;
	struct policydb *p = pd->p;
	__le32 buf[3];
	size_t items, len;
	int rc;

	len = strlen(key);
	items = 0;
	buf[items++] = cpu_to_le32(len);
	buf[items++] = cpu_to_le32(role->value);
	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY)
		buf[items++] = cpu_to_le32(role->bounds);

	BUG_ON(items > ARRAY_SIZE(buf));

	rc = put_entry(buf, sizeof(u32), items, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	rc = ebitmap_write(&role->dominates, fp);
	if (rc)
		return rc;

	rc = ebitmap_write(&role->types, fp);
	if (rc)
		return rc;

	return 0;
}

static int type_write(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct type_datum *typdatum = datum;
	struct policy_data *pd = ptr;
	struct policydb *p = pd->p;
	void *fp = pd->fp;
	__le32 buf[4];
	int rc;
	size_t items, len;

	len = strlen(key);
	items = 0;
	buf[items++] = cpu_to_le32(len);
	buf[items++] = cpu_to_le32(typdatum->value);
	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY) {
		u32 properties = 0;

		if (typdatum->primary)
			properties |= TYPEDATUM_PROPERTY_PRIMARY;

		if (typdatum->attribute)
			properties |= TYPEDATUM_PROPERTY_ATTRIBUTE;

		buf[items++] = cpu_to_le32(properties);
		buf[items++] = cpu_to_le32(typdatum->bounds);
	} else {
		buf[items++] = cpu_to_le32(typdatum->primary);
	}
	BUG_ON(items > ARRAY_SIZE(buf));
	rc = put_entry(buf, sizeof(u32), items, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	return 0;
}

static int user_write(void *vkey, void *datum, void *ptr)
{
	char *key = vkey;
	struct user_datum *usrdatum = datum;
	struct policy_data *pd = ptr;
	struct policydb *p = pd->p;
	void *fp = pd->fp;
	__le32 buf[3];
	size_t items, len;
	int rc;

	len = strlen(key);
	items = 0;
	buf[items++] = cpu_to_le32(len);
	buf[items++] = cpu_to_le32(usrdatum->value);
	if (p->policyvers >= POLICYDB_VERSION_BOUNDARY)
		buf[items++] = cpu_to_le32(usrdatum->bounds);
	BUG_ON(items > ARRAY_SIZE(buf));
	rc = put_entry(buf, sizeof(u32), items, fp);
	if (rc)
		return rc;

	rc = put_entry(key, 1, len, fp);
	if (rc)
		return rc;

	rc = ebitmap_write(&usrdatum->roles, fp);
	if (rc)
		return rc;

	rc = mls_write_range_helper(&usrdatum->range, fp);
	if (rc)
		return rc;

	rc = mls_write_level(&usrdatum->dfltlevel, fp);
	if (rc)
		return rc;

	return 0;
}

static int (*write_f[SYM_NUM]) (void *key, void *datum,
				void *datap) =
{
	common_write,
	class_write,
	role_write,
	type_write,
	user_write,
	cond_write_bool,
	sens_write,
	cat_write,
};

static int ocontext_write(struct policydb *p, struct policydb_compat_info *info,
			  void *fp)
{
	unsigned int i, j, rc;
	size_t nel, len;
	__le32 buf[3];
	u32 nodebuf[8];
	struct ocontext *c;
	for (i = 0; i < info->ocon_num; i++) {
		nel = 0;
		for (c = p->ocontexts[i]; c; c = c->next)
			nel++;
		buf[0] = cpu_to_le32(nel);
		rc = put_entry(buf, sizeof(u32), 1, fp);
		if (rc)
			return rc;
		for (c = p->ocontexts[i]; c; c = c->next) {
			switch (i) {
			case OCON_ISID:
				buf[0] = cpu_to_le32(c->sid[0]);
				rc = put_entry(buf, sizeof(u32), 1, fp);
				if (rc)
					return rc;
				rc = context_write(p, &c->context[0], fp);
				if (rc)
					return rc;
				break;
			case OCON_FS:
			case OCON_NETIF:
				len = strlen(c->u.name);
				buf[0] = cpu_to_le32(len);
				rc = put_entry(buf, sizeof(u32), 1, fp);
				if (rc)
					return rc;
				rc = put_entry(c->u.name, 1, len, fp);
				if (rc)
					return rc;
				rc = context_write(p, &c->context[0], fp);
				if (rc)
					return rc;
				rc = context_write(p, &c->context[1], fp);
				if (rc)
					return rc;
				break;
			case OCON_PORT:
				buf[0] = cpu_to_le32(c->u.port.protocol);
				buf[1] = cpu_to_le32(c->u.port.low_port);
				buf[2] = cpu_to_le32(c->u.port.high_port);
				rc = put_entry(buf, sizeof(u32), 3, fp);
				if (rc)
					return rc;
				rc = context_write(p, &c->context[0], fp);
				if (rc)
					return rc;
				break;
			case OCON_NODE:
				nodebuf[0] = c->u.node.addr; /* network order */
				nodebuf[1] = c->u.node.mask; /* network order */
				rc = put_entry(nodebuf, sizeof(u32), 2, fp);
				if (rc)
					return rc;
				rc = context_write(p, &c->context[0], fp);
				if (rc)
					return rc;
				break;
			case OCON_FSUSE:
				buf[0] = cpu_to_le32(c->v.behavior);
				len = strlen(c->u.name);
				buf[1] = cpu_to_le32(len);
				rc = put_entry(buf, sizeof(u32), 2, fp);
				if (rc)
					return rc;
				rc = put_entry(c->u.name, 1, len, fp);
				if (rc)
					return rc;
				rc = context_write(p, &c->context[0], fp);
				if (rc)
					return rc;
				break;
			case OCON_NODE6:
				for (j = 0; j < 4; j++)
					nodebuf[j] = c->u.node6.addr[j]; /* network order */
				for (j = 0; j < 4; j++)
					nodebuf[j + 4] = c->u.node6.mask[j]; /* network order */
				rc = put_entry(nodebuf, sizeof(u32), 8, fp);
				if (rc)
					return rc;
				rc = context_write(p, &c->context[0], fp);
				if (rc)
					return rc;
				break;
			}
		}
	}
	return 0;
}

static int genfs_write(struct policydb *p, void *fp)
{
	struct genfs *genfs;
	struct ocontext *c;
	size_t len;
	__le32 buf[1];
	int rc;

	len = 0;
	for (genfs = p->genfs; genfs; genfs = genfs->next)
		len++;
	buf[0] = cpu_to_le32(len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;
	for (genfs = p->genfs; genfs; genfs = genfs->next) {
		len = strlen(genfs->fstype);
		buf[0] = cpu_to_le32(len);
		rc = put_entry(buf, sizeof(u32), 1, fp);
		if (rc)
			return rc;
		rc = put_entry(genfs->fstype, 1, len, fp);
		if (rc)
			return rc;
		len = 0;
		for (c = genfs->head; c; c = c->next)
			len++;
		buf[0] = cpu_to_le32(len);
		rc = put_entry(buf, sizeof(u32), 1, fp);
		if (rc)
			return rc;
		for (c = genfs->head; c; c = c->next) {
			len = strlen(c->u.name);
			buf[0] = cpu_to_le32(len);
			rc = put_entry(buf, sizeof(u32), 1, fp);
			if (rc)
				return rc;
			rc = put_entry(c->u.name, 1, len, fp);
			if (rc)
				return rc;
			buf[0] = cpu_to_le32(c->v.sclass);
			rc = put_entry(buf, sizeof(u32), 1, fp);
			if (rc)
				return rc;
			rc = context_write(p, &c->context[0], fp);
			if (rc)
				return rc;
		}
	}
	return 0;
}

static int hashtab_cnt(void *key, void *data, void *ptr)
{
	int *cnt = ptr;
	*cnt = *cnt + 1;

	return 0;
}

static int range_write_helper(void *key, void *data, void *ptr)
{
	__le32 buf[2];
	struct range_trans *rt = key;
	struct mls_range *r = data;
	struct policy_data *pd = ptr;
	void *fp = pd->fp;
	struct policydb *p = pd->p;
	int rc;

	buf[0] = cpu_to_le32(rt->source_type);
	buf[1] = cpu_to_le32(rt->target_type);
	rc = put_entry(buf, sizeof(u32), 2, fp);
	if (rc)
		return rc;
	if (p->policyvers >= POLICYDB_VERSION_RANGETRANS) {
		buf[0] = cpu_to_le32(rt->target_class);
		rc = put_entry(buf, sizeof(u32), 1, fp);
		if (rc)
			return rc;
	}
	rc = mls_write_range_helper(r, fp);
	if (rc)
		return rc;

	return 0;
}

static int range_write(struct policydb *p, void *fp)
{
	__le32 buf[1];
	int rc, nel;
	struct policy_data pd;

	pd.p = p;
	pd.fp = fp;

	/* count the number of entries in the hashtab */
	nel = 0;
	rc = hashtab_map(p->range_tr, hashtab_cnt, &nel);
	if (rc)
		return rc;

	buf[0] = cpu_to_le32(nel);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	/* actually write all of the entries */
	rc = hashtab_map(p->range_tr, range_write_helper, &pd);
	if (rc)
		return rc;

	return 0;
}

static int filename_write_helper(void *key, void *data, void *ptr)
{
	__le32 buf[4];
	struct filename_trans *ft = key;
	struct filename_trans_datum *otype = data;
	void *fp = ptr;
	int rc;
	u32 len;

	len = strlen(ft->name);
	buf[0] = cpu_to_le32(len);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	rc = put_entry(ft->name, sizeof(char), len, fp);
	if (rc)
		return rc;

	buf[0] = cpu_to_le32(ft->stype);
	buf[1] = cpu_to_le32(ft->ttype);
	buf[2] = cpu_to_le32(ft->tclass);
	buf[3] = cpu_to_le32(otype->otype);

	rc = put_entry(buf, sizeof(u32), 4, fp);
	if (rc)
		return rc;

	return 0;
}

static int filename_trans_write(struct policydb *p, void *fp)
{
	u32 nel;
	__le32 buf[1];
	int rc;

	if (p->policyvers < POLICYDB_VERSION_FILENAME_TRANS)
		return 0;

	nel = 0;
	rc = hashtab_map(p->filename_trans, hashtab_cnt, &nel);
	if (rc)
		return rc;

	buf[0] = cpu_to_le32(nel);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	rc = hashtab_map(p->filename_trans, filename_write_helper, fp);
	if (rc)
		return rc;

	return 0;
}

/*
 * Write the configuration data in a policy database
 * structure to a policy database binary representation
 * file.
 */
int policydb_write(struct policydb *p, void *fp)
{
	unsigned int i, num_syms;
	int rc;
	__le32 buf[4];
	u32 config;
	size_t len;
	struct policydb_compat_info *info;

	/*
	 * refuse to write policy older than compressed avtab
	 * to simplify the writer.  There are other tests dropped
	 * since we assume this throughout the writer code.  Be
	 * careful if you ever try to remove this restriction
	 */
	if (p->policyvers < POLICYDB_VERSION_AVTAB) {
		printk(KERN_ERR "SELinux: refusing to write policy version %d."
		       "  Because it is less than version %d\n", p->policyvers,
		       POLICYDB_VERSION_AVTAB);
		return -EINVAL;
	}

	config = 0;
	if (p->mls_enabled)
		config |= POLICYDB_CONFIG_MLS;

	if (p->reject_unknown)
		config |= REJECT_UNKNOWN;
	if (p->allow_unknown)
		config |= ALLOW_UNKNOWN;

	/* Write the magic number and string identifiers. */
	buf[0] = cpu_to_le32(POLICYDB_MAGIC);
	len = strlen(POLICYDB_STRING);
	buf[1] = cpu_to_le32(len);
	rc = put_entry(buf, sizeof(u32), 2, fp);
	if (rc)
		return rc;
	rc = put_entry(POLICYDB_STRING, 1, len, fp);
	if (rc)
		return rc;

	/* Write the version, config, and table sizes. */
	info = policydb_lookup_compat(p->policyvers);
	if (!info) {
		printk(KERN_ERR "SELinux: compatibility lookup failed for policy "
		    "version %d", p->policyvers);
		return -EINVAL;
	}

	buf[0] = cpu_to_le32(p->policyvers);
	buf[1] = cpu_to_le32(config);
	buf[2] = cpu_to_le32(info->sym_num);
	buf[3] = cpu_to_le32(info->ocon_num);

	rc = put_entry(buf, sizeof(u32), 4, fp);
	if (rc)
		return rc;

	if (p->policyvers >= POLICYDB_VERSION_POLCAP) {
		rc = ebitmap_write(&p->policycaps, fp);
		if (rc)
			return rc;
	}

	if (p->policyvers >= POLICYDB_VERSION_PERMISSIVE) {
		rc = ebitmap_write(&p->permissive_map, fp);
		if (rc)
			return rc;
	}

	num_syms = info->sym_num;
	for (i = 0; i < num_syms; i++) {
		struct policy_data pd;

		pd.fp = fp;
		pd.p = p;

		buf[0] = cpu_to_le32(p->symtab[i].nprim);
		buf[1] = cpu_to_le32(p->symtab[i].table->nel);

		rc = put_entry(buf, sizeof(u32), 2, fp);
		if (rc)
			return rc;
		rc = hashtab_map(p->symtab[i].table, write_f[i], &pd);
		if (rc)
			return rc;
	}

	rc = avtab_write(p, &p->te_avtab, fp);
	if (rc)
		return rc;

	rc = cond_write_list(p, p->cond_list, fp);
	if (rc)
		return rc;

	rc = role_trans_write(p, fp);
	if (rc)
		return rc;

	rc = role_allow_write(p->role_allow, fp);
	if (rc)
		return rc;

	rc = filename_trans_write(p, fp);
	if (rc)
		return rc;

	rc = ocontext_write(p, info, fp);
	if (rc)
		return rc;

	rc = genfs_write(p, fp);
	if (rc)
		return rc;

	rc = range_write(p, fp);
	if (rc)
		return rc;

	for (i = 0; i < p->p_types.nprim; i++) {
		struct ebitmap *e = flex_array_get(p->type_attr_map_array, i);

		BUG_ON(!e);
		rc = ebitmap_write(e, fp);
		if (rc)
			return rc;
	}

	return 0;
}
