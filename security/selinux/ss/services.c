/*
 * Implementation of the security services.
 *
 * Authors : Stephen Smalley, <sds@epoch.ncsc.mil>
 *	     James Morris <jmorris@redhat.com>
 *
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 *	Support for enhanced MLS infrastructure.
 *	Support for context based audit filters.
 *
 * Updated: Frank Mayer <mayerf@tresys.com> and Karl MacMillan <kmacmillan@tresys.com>
 *
 *	Added conditional policy language extensions
 *
 * Updated: Hewlett-Packard <paul.moore@hp.com>
 *
 *      Added support for NetLabel
 *      Added support for the policy capability bitmap
 *
 * Updated: Chad Sellers <csellers@tresys.com>
 *
 *  Added validation of kernel classes and permissions
 *
 * Copyright (C) 2006, 2007 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2004-2006 Trusted Computer Solutions, Inc.
 * Copyright (C) 2003 - 2004, 2006 Tresys Technology, LLC
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/audit.h>
#include <linux/mutex.h>
#include <linux/selinux.h>
#include <net/netlabel.h>

#include "flask.h"
#include "avc.h"
#include "avc_ss.h"
#include "security.h"
#include "context.h"
#include "policydb.h"
#include "sidtab.h"
#include "services.h"
#include "conditional.h"
#include "mls.h"
#include "objsec.h"
#include "netlabel.h"
#include "xfrm.h"
#include "ebitmap.h"
#include "audit.h"

extern void selnl_notify_policyload(u32 seqno);
unsigned int policydb_loaded_version;

int selinux_policycap_netpeer;
int selinux_policycap_openperm;

/*
 * This is declared in avc.c
 */
extern const struct selinux_class_perm selinux_class_perm;

static DEFINE_RWLOCK(policy_rwlock);
#define POLICY_RDLOCK read_lock(&policy_rwlock)
#define POLICY_WRLOCK write_lock_irq(&policy_rwlock)
#define POLICY_RDUNLOCK read_unlock(&policy_rwlock)
#define POLICY_WRUNLOCK write_unlock_irq(&policy_rwlock)

static DEFINE_MUTEX(load_mutex);
#define LOAD_LOCK mutex_lock(&load_mutex)
#define LOAD_UNLOCK mutex_unlock(&load_mutex)

static struct sidtab sidtab;
struct policydb policydb;
int ss_initialized;

/*
 * The largest sequence number that has been used when
 * providing an access decision to the access vector cache.
 * The sequence number only changes when a policy change
 * occurs.
 */
static u32 latest_granting;

/* Forward declaration. */
static int context_struct_to_string(struct context *context, char **scontext,
				    u32 *scontext_len);

/*
 * Return the boolean value of a constraint expression
 * when it is applied to the specified source and target
 * security contexts.
 *
 * xcontext is a special beast...  It is used by the validatetrans rules
 * only.  For these rules, scontext is the context before the transition,
 * tcontext is the context after the transition, and xcontext is the context
 * of the process performing the transition.  All other callers of
 * constraint_expr_eval should pass in NULL for xcontext.
 */
static int constraint_expr_eval(struct context *scontext,
				struct context *tcontext,
				struct context *xcontext,
				struct constraint_expr *cexpr)
{
	u32 val1, val2;
	struct context *c;
	struct role_datum *r1, *r2;
	struct mls_level *l1, *l2;
	struct constraint_expr *e;
	int s[CEXPR_MAXDEPTH];
	int sp = -1;

	for (e = cexpr; e; e = e->next) {
		switch (e->expr_type) {
		case CEXPR_NOT:
			BUG_ON(sp < 0);
			s[sp] = !s[sp];
			break;
		case CEXPR_AND:
			BUG_ON(sp < 1);
			sp--;
			s[sp] &= s[sp+1];
			break;
		case CEXPR_OR:
			BUG_ON(sp < 1);
			sp--;
			s[sp] |= s[sp+1];
			break;
		case CEXPR_ATTR:
			if (sp == (CEXPR_MAXDEPTH-1))
				return 0;
			switch (e->attr) {
			case CEXPR_USER:
				val1 = scontext->user;
				val2 = tcontext->user;
				break;
			case CEXPR_TYPE:
				val1 = scontext->type;
				val2 = tcontext->type;
				break;
			case CEXPR_ROLE:
				val1 = scontext->role;
				val2 = tcontext->role;
				r1 = policydb.role_val_to_struct[val1 - 1];
				r2 = policydb.role_val_to_struct[val2 - 1];
				switch (e->op) {
				case CEXPR_DOM:
					s[++sp] = ebitmap_get_bit(&r1->dominates,
								  val2 - 1);
					continue;
				case CEXPR_DOMBY:
					s[++sp] = ebitmap_get_bit(&r2->dominates,
								  val1 - 1);
					continue;
				case CEXPR_INCOMP:
					s[++sp] = (!ebitmap_get_bit(&r1->dominates,
								    val2 - 1) &&
						   !ebitmap_get_bit(&r2->dominates,
								    val1 - 1));
					continue;
				default:
					break;
				}
				break;
			case CEXPR_L1L2:
				l1 = &(scontext->range.level[0]);
				l2 = &(tcontext->range.level[0]);
				goto mls_ops;
			case CEXPR_L1H2:
				l1 = &(scontext->range.level[0]);
				l2 = &(tcontext->range.level[1]);
				goto mls_ops;
			case CEXPR_H1L2:
				l1 = &(scontext->range.level[1]);
				l2 = &(tcontext->range.level[0]);
				goto mls_ops;
			case CEXPR_H1H2:
				l1 = &(scontext->range.level[1]);
				l2 = &(tcontext->range.level[1]);
				goto mls_ops;
			case CEXPR_L1H1:
				l1 = &(scontext->range.level[0]);
				l2 = &(scontext->range.level[1]);
				goto mls_ops;
			case CEXPR_L2H2:
				l1 = &(tcontext->range.level[0]);
				l2 = &(tcontext->range.level[1]);
				goto mls_ops;
mls_ops:
			switch (e->op) {
			case CEXPR_EQ:
				s[++sp] = mls_level_eq(l1, l2);
				continue;
			case CEXPR_NEQ:
				s[++sp] = !mls_level_eq(l1, l2);
				continue;
			case CEXPR_DOM:
				s[++sp] = mls_level_dom(l1, l2);
				continue;
			case CEXPR_DOMBY:
				s[++sp] = mls_level_dom(l2, l1);
				continue;
			case CEXPR_INCOMP:
				s[++sp] = mls_level_incomp(l2, l1);
				continue;
			default:
				BUG();
				return 0;
			}
			break;
			default:
				BUG();
				return 0;
			}

			switch (e->op) {
			case CEXPR_EQ:
				s[++sp] = (val1 == val2);
				break;
			case CEXPR_NEQ:
				s[++sp] = (val1 != val2);
				break;
			default:
				BUG();
				return 0;
			}
			break;
		case CEXPR_NAMES:
			if (sp == (CEXPR_MAXDEPTH-1))
				return 0;
			c = scontext;
			if (e->attr & CEXPR_TARGET)
				c = tcontext;
			else if (e->attr & CEXPR_XTARGET) {
				c = xcontext;
				if (!c) {
					BUG();
					return 0;
				}
			}
			if (e->attr & CEXPR_USER)
				val1 = c->user;
			else if (e->attr & CEXPR_ROLE)
				val1 = c->role;
			else if (e->attr & CEXPR_TYPE)
				val1 = c->type;
			else {
				BUG();
				return 0;
			}

			switch (e->op) {
			case CEXPR_EQ:
				s[++sp] = ebitmap_get_bit(&e->names, val1 - 1);
				break;
			case CEXPR_NEQ:
				s[++sp] = !ebitmap_get_bit(&e->names, val1 - 1);
				break;
			default:
				BUG();
				return 0;
			}
			break;
		default:
			BUG();
			return 0;
		}
	}

	BUG_ON(sp != 0);
	return s[0];
}

/*
 * Compute access vectors based on a context structure pair for
 * the permissions in a particular class.
 */
static int context_struct_compute_av(struct context *scontext,
				     struct context *tcontext,
				     u16 tclass,
				     u32 requested,
				     struct av_decision *avd)
{
	struct constraint_node *constraint;
	struct role_allow *ra;
	struct avtab_key avkey;
	struct avtab_node *node;
	struct class_datum *tclass_datum;
	struct ebitmap *sattr, *tattr;
	struct ebitmap_node *snode, *tnode;
	const struct selinux_class_perm *kdefs = &selinux_class_perm;
	unsigned int i, j;

	/*
	 * Remap extended Netlink classes for old policy versions.
	 * Do this here rather than socket_type_to_security_class()
	 * in case a newer policy version is loaded, allowing sockets
	 * to remain in the correct class.
	 */
	if (policydb_loaded_version < POLICYDB_VERSION_NLCLASS)
		if (tclass >= SECCLASS_NETLINK_ROUTE_SOCKET &&
		    tclass <= SECCLASS_NETLINK_DNRT_SOCKET)
			tclass = SECCLASS_NETLINK_SOCKET;

	/*
	 * Initialize the access vectors to the default values.
	 */
	avd->allowed = 0;
	avd->decided = 0xffffffff;
	avd->auditallow = 0;
	avd->auditdeny = 0xffffffff;
	avd->seqno = latest_granting;

	/*
	 * Check for all the invalid cases.
	 * - tclass 0
	 * - tclass > policy and > kernel
	 * - tclass > policy but is a userspace class
	 * - tclass > policy but we do not allow unknowns
	 */
	if (unlikely(!tclass))
		goto inval_class;
	if (unlikely(tclass > policydb.p_classes.nprim))
		if (tclass > kdefs->cts_len ||
		    !kdefs->class_to_string[tclass - 1] ||
		    !policydb.allow_unknown)
			goto inval_class;

	/*
	 * Kernel class and we allow unknown so pad the allow decision
	 * the pad will be all 1 for unknown classes.
	 */
	if (tclass <= kdefs->cts_len && policydb.allow_unknown)
		avd->allowed = policydb.undefined_perms[tclass - 1];

	/*
	 * Not in policy. Since decision is completed (all 1 or all 0) return.
	 */
	if (unlikely(tclass > policydb.p_classes.nprim))
		return 0;

	tclass_datum = policydb.class_val_to_struct[tclass - 1];

	/*
	 * If a specific type enforcement rule was defined for
	 * this permission check, then use it.
	 */
	avkey.target_class = tclass;
	avkey.specified = AVTAB_AV;
	sattr = &policydb.type_attr_map[scontext->type - 1];
	tattr = &policydb.type_attr_map[tcontext->type - 1];
	ebitmap_for_each_positive_bit(sattr, snode, i) {
		ebitmap_for_each_positive_bit(tattr, tnode, j) {
			avkey.source_type = i + 1;
			avkey.target_type = j + 1;
			for (node = avtab_search_node(&policydb.te_avtab, &avkey);
			     node != NULL;
			     node = avtab_search_node_next(node, avkey.specified)) {
				if (node->key.specified == AVTAB_ALLOWED)
					avd->allowed |= node->datum.data;
				else if (node->key.specified == AVTAB_AUDITALLOW)
					avd->auditallow |= node->datum.data;
				else if (node->key.specified == AVTAB_AUDITDENY)
					avd->auditdeny &= node->datum.data;
			}

			/* Check conditional av table for additional permissions */
			cond_compute_av(&policydb.te_cond_avtab, &avkey, avd);

		}
	}

	/*
	 * Remove any permissions prohibited by a constraint (this includes
	 * the MLS policy).
	 */
	constraint = tclass_datum->constraints;
	while (constraint) {
		if ((constraint->permissions & (avd->allowed)) &&
		    !constraint_expr_eval(scontext, tcontext, NULL,
					  constraint->expr)) {
			avd->allowed = (avd->allowed) & ~(constraint->permissions);
		}
		constraint = constraint->next;
	}

	/*
	 * If checking process transition permission and the
	 * role is changing, then check the (current_role, new_role)
	 * pair.
	 */
	if (tclass == SECCLASS_PROCESS &&
	    (avd->allowed & (PROCESS__TRANSITION | PROCESS__DYNTRANSITION)) &&
	    scontext->role != tcontext->role) {
		for (ra = policydb.role_allow; ra; ra = ra->next) {
			if (scontext->role == ra->role &&
			    tcontext->role == ra->new_role)
				break;
		}
		if (!ra)
			avd->allowed = (avd->allowed) & ~(PROCESS__TRANSITION |
							PROCESS__DYNTRANSITION);
	}

	return 0;

inval_class:
	printk(KERN_ERR "SELinux: %s:  unrecognized class %d\n", __func__,
		tclass);
	return -EINVAL;
}

/*
 * Given a sid find if the type has the permissive flag set
 */
int security_permissive_sid(u32 sid)
{
	struct context *context;
	u32 type;
	int rc;

	POLICY_RDLOCK;

	context = sidtab_search(&sidtab, sid);
	BUG_ON(!context);

	type = context->type;
	/*
	 * we are intentionally using type here, not type-1, the 0th bit may
	 * someday indicate that we are globally setting permissive in policy.
	 */
	rc = ebitmap_get_bit(&policydb.permissive_map, type);

	POLICY_RDUNLOCK;
	return rc;
}

static int security_validtrans_handle_fail(struct context *ocontext,
					   struct context *ncontext,
					   struct context *tcontext,
					   u16 tclass)
{
	char *o = NULL, *n = NULL, *t = NULL;
	u32 olen, nlen, tlen;

	if (context_struct_to_string(ocontext, &o, &olen) < 0)
		goto out;
	if (context_struct_to_string(ncontext, &n, &nlen) < 0)
		goto out;
	if (context_struct_to_string(tcontext, &t, &tlen) < 0)
		goto out;
	audit_log(current->audit_context, GFP_ATOMIC, AUDIT_SELINUX_ERR,
		  "security_validate_transition:  denied for"
		  " oldcontext=%s newcontext=%s taskcontext=%s tclass=%s",
		  o, n, t, policydb.p_class_val_to_name[tclass-1]);
out:
	kfree(o);
	kfree(n);
	kfree(t);

	if (!selinux_enforcing)
		return 0;
	return -EPERM;
}

int security_validate_transition(u32 oldsid, u32 newsid, u32 tasksid,
				 u16 tclass)
{
	struct context *ocontext;
	struct context *ncontext;
	struct context *tcontext;
	struct class_datum *tclass_datum;
	struct constraint_node *constraint;
	int rc = 0;

	if (!ss_initialized)
		return 0;

	POLICY_RDLOCK;

	/*
	 * Remap extended Netlink classes for old policy versions.
	 * Do this here rather than socket_type_to_security_class()
	 * in case a newer policy version is loaded, allowing sockets
	 * to remain in the correct class.
	 */
	if (policydb_loaded_version < POLICYDB_VERSION_NLCLASS)
		if (tclass >= SECCLASS_NETLINK_ROUTE_SOCKET &&
		    tclass <= SECCLASS_NETLINK_DNRT_SOCKET)
			tclass = SECCLASS_NETLINK_SOCKET;

	if (!tclass || tclass > policydb.p_classes.nprim) {
		printk(KERN_ERR "SELinux: %s:  unrecognized class %d\n",
			__func__, tclass);
		rc = -EINVAL;
		goto out;
	}
	tclass_datum = policydb.class_val_to_struct[tclass - 1];

	ocontext = sidtab_search(&sidtab, oldsid);
	if (!ocontext) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
			__func__, oldsid);
		rc = -EINVAL;
		goto out;
	}

	ncontext = sidtab_search(&sidtab, newsid);
	if (!ncontext) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
			__func__, newsid);
		rc = -EINVAL;
		goto out;
	}

	tcontext = sidtab_search(&sidtab, tasksid);
	if (!tcontext) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
			__func__, tasksid);
		rc = -EINVAL;
		goto out;
	}

	constraint = tclass_datum->validatetrans;
	while (constraint) {
		if (!constraint_expr_eval(ocontext, ncontext, tcontext,
					  constraint->expr)) {
			rc = security_validtrans_handle_fail(ocontext, ncontext,
							     tcontext, tclass);
			goto out;
		}
		constraint = constraint->next;
	}

out:
	POLICY_RDUNLOCK;
	return rc;
}

/**
 * security_compute_av - Compute access vector decisions.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions
 * @avd: access vector decisions
 *
 * Compute a set of access vector decisions based on the
 * SID pair (@ssid, @tsid) for the permissions in @tclass.
 * Return -%EINVAL if any of the parameters are invalid or %0
 * if the access vector decisions were computed successfully.
 */
int security_compute_av(u32 ssid,
			u32 tsid,
			u16 tclass,
			u32 requested,
			struct av_decision *avd)
{
	struct context *scontext = NULL, *tcontext = NULL;
	int rc = 0;

	if (!ss_initialized) {
		avd->allowed = 0xffffffff;
		avd->decided = 0xffffffff;
		avd->auditallow = 0;
		avd->auditdeny = 0xffffffff;
		avd->seqno = latest_granting;
		return 0;
	}

	POLICY_RDLOCK;

	scontext = sidtab_search(&sidtab, ssid);
	if (!scontext) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
		       __func__, ssid);
		rc = -EINVAL;
		goto out;
	}
	tcontext = sidtab_search(&sidtab, tsid);
	if (!tcontext) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
		       __func__, tsid);
		rc = -EINVAL;
		goto out;
	}

	rc = context_struct_compute_av(scontext, tcontext, tclass,
				       requested, avd);
out:
	POLICY_RDUNLOCK;
	return rc;
}

/*
 * Write the security context string representation of
 * the context structure `context' into a dynamically
 * allocated string of the correct size.  Set `*scontext'
 * to point to this string and set `*scontext_len' to
 * the length of the string.
 */
static int context_struct_to_string(struct context *context, char **scontext, u32 *scontext_len)
{
	char *scontextp;

	*scontext = NULL;
	*scontext_len = 0;

	/* Compute the size of the context. */
	*scontext_len += strlen(policydb.p_user_val_to_name[context->user - 1]) + 1;
	*scontext_len += strlen(policydb.p_role_val_to_name[context->role - 1]) + 1;
	*scontext_len += strlen(policydb.p_type_val_to_name[context->type - 1]) + 1;
	*scontext_len += mls_compute_context_len(context);

	/* Allocate space for the context; caller must free this space. */
	scontextp = kmalloc(*scontext_len, GFP_ATOMIC);
	if (!scontextp)
		return -ENOMEM;
	*scontext = scontextp;

	/*
	 * Copy the user name, role name and type name into the context.
	 */
	sprintf(scontextp, "%s:%s:%s",
		policydb.p_user_val_to_name[context->user - 1],
		policydb.p_role_val_to_name[context->role - 1],
		policydb.p_type_val_to_name[context->type - 1]);
	scontextp += strlen(policydb.p_user_val_to_name[context->user - 1]) +
		     1 + strlen(policydb.p_role_val_to_name[context->role - 1]) +
		     1 + strlen(policydb.p_type_val_to_name[context->type - 1]);

	mls_sid_to_context(context, &scontextp);

	*scontextp = 0;

	return 0;
}

#include "initial_sid_to_string.h"

const char *security_get_initial_sid_context(u32 sid)
{
	if (unlikely(sid > SECINITSID_NUM))
		return NULL;
	return initial_sid_to_string[sid];
}

/**
 * security_sid_to_context - Obtain a context for a given SID.
 * @sid: security identifier, SID
 * @scontext: security context
 * @scontext_len: length in bytes
 *
 * Write the string representation of the context associated with @sid
 * into a dynamically allocated string of the correct size.  Set @scontext
 * to point to this string and set @scontext_len to the length of the string.
 */
int security_sid_to_context(u32 sid, char **scontext, u32 *scontext_len)
{
	struct context *context;
	int rc = 0;

	*scontext = NULL;
	*scontext_len  = 0;

	if (!ss_initialized) {
		if (sid <= SECINITSID_NUM) {
			char *scontextp;

			*scontext_len = strlen(initial_sid_to_string[sid]) + 1;
			scontextp = kmalloc(*scontext_len, GFP_ATOMIC);
			if (!scontextp) {
				rc = -ENOMEM;
				goto out;
			}
			strcpy(scontextp, initial_sid_to_string[sid]);
			*scontext = scontextp;
			goto out;
		}
		printk(KERN_ERR "SELinux: %s:  called before initial "
		       "load_policy on unknown SID %d\n", __func__, sid);
		rc = -EINVAL;
		goto out;
	}
	POLICY_RDLOCK;
	context = sidtab_search(&sidtab, sid);
	if (!context) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
			__func__, sid);
		rc = -EINVAL;
		goto out_unlock;
	}
	rc = context_struct_to_string(context, scontext, scontext_len);
out_unlock:
	POLICY_RDUNLOCK;
out:
	return rc;

}

static int security_context_to_sid_core(const char *scontext, u32 scontext_len,
					u32 *sid, u32 def_sid, gfp_t gfp_flags)
{
	char *scontext2;
	struct context context;
	struct role_datum *role;
	struct type_datum *typdatum;
	struct user_datum *usrdatum;
	char *scontextp, *p, oldc;
	int rc = 0;

	if (!ss_initialized) {
		int i;

		for (i = 1; i < SECINITSID_NUM; i++) {
			if (!strcmp(initial_sid_to_string[i], scontext)) {
				*sid = i;
				goto out;
			}
		}
		*sid = SECINITSID_KERNEL;
		goto out;
	}
	*sid = SECSID_NULL;

	/* Copy the string so that we can modify the copy as we parse it.
	   The string should already by null terminated, but we append a
	   null suffix to the copy to avoid problems with the existing
	   attr package, which doesn't view the null terminator as part
	   of the attribute value. */
	scontext2 = kmalloc(scontext_len+1, gfp_flags);
	if (!scontext2) {
		rc = -ENOMEM;
		goto out;
	}
	memcpy(scontext2, scontext, scontext_len);
	scontext2[scontext_len] = 0;

	context_init(&context);
	*sid = SECSID_NULL;

	POLICY_RDLOCK;

	/* Parse the security context. */

	rc = -EINVAL;
	scontextp = (char *) scontext2;

	/* Extract the user. */
	p = scontextp;
	while (*p && *p != ':')
		p++;

	if (*p == 0)
		goto out_unlock;

	*p++ = 0;

	usrdatum = hashtab_search(policydb.p_users.table, scontextp);
	if (!usrdatum)
		goto out_unlock;

	context.user = usrdatum->value;

	/* Extract role. */
	scontextp = p;
	while (*p && *p != ':')
		p++;

	if (*p == 0)
		goto out_unlock;

	*p++ = 0;

	role = hashtab_search(policydb.p_roles.table, scontextp);
	if (!role)
		goto out_unlock;
	context.role = role->value;

	/* Extract type. */
	scontextp = p;
	while (*p && *p != ':')
		p++;
	oldc = *p;
	*p++ = 0;

	typdatum = hashtab_search(policydb.p_types.table, scontextp);
	if (!typdatum)
		goto out_unlock;

	context.type = typdatum->value;

	rc = mls_context_to_sid(oldc, &p, &context, &sidtab, def_sid);
	if (rc)
		goto out_unlock;

	if ((p - scontext2) < scontext_len) {
		rc = -EINVAL;
		goto out_unlock;
	}

	/* Check the validity of the new context. */
	if (!policydb_context_isvalid(&policydb, &context)) {
		rc = -EINVAL;
		goto out_unlock;
	}
	/* Obtain the new sid. */
	rc = sidtab_context_to_sid(&sidtab, &context, sid);
out_unlock:
	POLICY_RDUNLOCK;
	context_destroy(&context);
	kfree(scontext2);
out:
	return rc;
}

/**
 * security_context_to_sid - Obtain a SID for a given security context.
 * @scontext: security context
 * @scontext_len: length in bytes
 * @sid: security identifier, SID
 *
 * Obtains a SID associated with the security context that
 * has the string representation specified by @scontext.
 * Returns -%EINVAL if the context is invalid, -%ENOMEM if insufficient
 * memory is available, or 0 on success.
 */
int security_context_to_sid(const char *scontext, u32 scontext_len, u32 *sid)
{
	return security_context_to_sid_core(scontext, scontext_len,
					    sid, SECSID_NULL, GFP_KERNEL);
}

/**
 * security_context_to_sid_default - Obtain a SID for a given security context,
 * falling back to specified default if needed.
 *
 * @scontext: security context
 * @scontext_len: length in bytes
 * @sid: security identifier, SID
 * @def_sid: default SID to assign on error
 *
 * Obtains a SID associated with the security context that
 * has the string representation specified by @scontext.
 * The default SID is passed to the MLS layer to be used to allow
 * kernel labeling of the MLS field if the MLS field is not present
 * (for upgrading to MLS without full relabel).
 * Returns -%EINVAL if the context is invalid, -%ENOMEM if insufficient
 * memory is available, or 0 on success.
 */
int security_context_to_sid_default(const char *scontext, u32 scontext_len,
				    u32 *sid, u32 def_sid, gfp_t gfp_flags)
{
	return security_context_to_sid_core(scontext, scontext_len,
					    sid, def_sid, gfp_flags);
}

static int compute_sid_handle_invalid_context(
	struct context *scontext,
	struct context *tcontext,
	u16 tclass,
	struct context *newcontext)
{
	char *s = NULL, *t = NULL, *n = NULL;
	u32 slen, tlen, nlen;

	if (context_struct_to_string(scontext, &s, &slen) < 0)
		goto out;
	if (context_struct_to_string(tcontext, &t, &tlen) < 0)
		goto out;
	if (context_struct_to_string(newcontext, &n, &nlen) < 0)
		goto out;
	audit_log(current->audit_context, GFP_ATOMIC, AUDIT_SELINUX_ERR,
		  "security_compute_sid:  invalid context %s"
		  " for scontext=%s"
		  " tcontext=%s"
		  " tclass=%s",
		  n, s, t, policydb.p_class_val_to_name[tclass-1]);
out:
	kfree(s);
	kfree(t);
	kfree(n);
	if (!selinux_enforcing)
		return 0;
	return -EACCES;
}

static int security_compute_sid(u32 ssid,
				u32 tsid,
				u16 tclass,
				u32 specified,
				u32 *out_sid)
{
	struct context *scontext = NULL, *tcontext = NULL, newcontext;
	struct role_trans *roletr = NULL;
	struct avtab_key avkey;
	struct avtab_datum *avdatum;
	struct avtab_node *node;
	int rc = 0;

	if (!ss_initialized) {
		switch (tclass) {
		case SECCLASS_PROCESS:
			*out_sid = ssid;
			break;
		default:
			*out_sid = tsid;
			break;
		}
		goto out;
	}

	context_init(&newcontext);

	POLICY_RDLOCK;

	scontext = sidtab_search(&sidtab, ssid);
	if (!scontext) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
		       __func__, ssid);
		rc = -EINVAL;
		goto out_unlock;
	}
	tcontext = sidtab_search(&sidtab, tsid);
	if (!tcontext) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
		       __func__, tsid);
		rc = -EINVAL;
		goto out_unlock;
	}

	/* Set the user identity. */
	switch (specified) {
	case AVTAB_TRANSITION:
	case AVTAB_CHANGE:
		/* Use the process user identity. */
		newcontext.user = scontext->user;
		break;
	case AVTAB_MEMBER:
		/* Use the related object owner. */
		newcontext.user = tcontext->user;
		break;
	}

	/* Set the role and type to default values. */
	switch (tclass) {
	case SECCLASS_PROCESS:
		/* Use the current role and type of process. */
		newcontext.role = scontext->role;
		newcontext.type = scontext->type;
		break;
	default:
		/* Use the well-defined object role. */
		newcontext.role = OBJECT_R_VAL;
		/* Use the type of the related object. */
		newcontext.type = tcontext->type;
	}

	/* Look for a type transition/member/change rule. */
	avkey.source_type = scontext->type;
	avkey.target_type = tcontext->type;
	avkey.target_class = tclass;
	avkey.specified = specified;
	avdatum = avtab_search(&policydb.te_avtab, &avkey);

	/* If no permanent rule, also check for enabled conditional rules */
	if (!avdatum) {
		node = avtab_search_node(&policydb.te_cond_avtab, &avkey);
		for (; node != NULL; node = avtab_search_node_next(node, specified)) {
			if (node->key.specified & AVTAB_ENABLED) {
				avdatum = &node->datum;
				break;
			}
		}
	}

	if (avdatum) {
		/* Use the type from the type transition/member/change rule. */
		newcontext.type = avdatum->data;
	}

	/* Check for class-specific changes. */
	switch (tclass) {
	case SECCLASS_PROCESS:
		if (specified & AVTAB_TRANSITION) {
			/* Look for a role transition rule. */
			for (roletr = policydb.role_tr; roletr;
			     roletr = roletr->next) {
				if (roletr->role == scontext->role &&
				    roletr->type == tcontext->type) {
					/* Use the role transition rule. */
					newcontext.role = roletr->new_role;
					break;
				}
			}
		}
		break;
	default:
		break;
	}

	/* Set the MLS attributes.
	   This is done last because it may allocate memory. */
	rc = mls_compute_sid(scontext, tcontext, tclass, specified, &newcontext);
	if (rc)
		goto out_unlock;

	/* Check the validity of the context. */
	if (!policydb_context_isvalid(&policydb, &newcontext)) {
		rc = compute_sid_handle_invalid_context(scontext,
							tcontext,
							tclass,
							&newcontext);
		if (rc)
			goto out_unlock;
	}
	/* Obtain the sid for the context. */
	rc = sidtab_context_to_sid(&sidtab, &newcontext, out_sid);
out_unlock:
	POLICY_RDUNLOCK;
	context_destroy(&newcontext);
out:
	return rc;
}

/**
 * security_transition_sid - Compute the SID for a new subject/object.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @out_sid: security identifier for new subject/object
 *
 * Compute a SID to use for labeling a new subject or object in the
 * class @tclass based on a SID pair (@ssid, @tsid).
 * Return -%EINVAL if any of the parameters are invalid, -%ENOMEM
 * if insufficient memory is available, or %0 if the new SID was
 * computed successfully.
 */
int security_transition_sid(u32 ssid,
			    u32 tsid,
			    u16 tclass,
			    u32 *out_sid)
{
	return security_compute_sid(ssid, tsid, tclass, AVTAB_TRANSITION, out_sid);
}

/**
 * security_member_sid - Compute the SID for member selection.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @out_sid: security identifier for selected member
 *
 * Compute a SID to use when selecting a member of a polyinstantiated
 * object of class @tclass based on a SID pair (@ssid, @tsid).
 * Return -%EINVAL if any of the parameters are invalid, -%ENOMEM
 * if insufficient memory is available, or %0 if the SID was
 * computed successfully.
 */
int security_member_sid(u32 ssid,
			u32 tsid,
			u16 tclass,
			u32 *out_sid)
{
	return security_compute_sid(ssid, tsid, tclass, AVTAB_MEMBER, out_sid);
}

/**
 * security_change_sid - Compute the SID for object relabeling.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @out_sid: security identifier for selected member
 *
 * Compute a SID to use for relabeling an object of class @tclass
 * based on a SID pair (@ssid, @tsid).
 * Return -%EINVAL if any of the parameters are invalid, -%ENOMEM
 * if insufficient memory is available, or %0 if the SID was
 * computed successfully.
 */
int security_change_sid(u32 ssid,
			u32 tsid,
			u16 tclass,
			u32 *out_sid)
{
	return security_compute_sid(ssid, tsid, tclass, AVTAB_CHANGE, out_sid);
}

/*
 * Verify that each kernel class that is defined in the
 * policy is correct
 */
static int validate_classes(struct policydb *p)
{
	int i, j;
	struct class_datum *cladatum;
	struct perm_datum *perdatum;
	u32 nprim, tmp, common_pts_len, perm_val, pol_val;
	u16 class_val;
	const struct selinux_class_perm *kdefs = &selinux_class_perm;
	const char *def_class, *def_perm, *pol_class;
	struct symtab *perms;

	if (p->allow_unknown) {
		u32 num_classes = kdefs->cts_len;
		p->undefined_perms = kcalloc(num_classes, sizeof(u32), GFP_KERNEL);
		if (!p->undefined_perms)
			return -ENOMEM;
	}

	for (i = 1; i < kdefs->cts_len; i++) {
		def_class = kdefs->class_to_string[i];
		if (!def_class)
			continue;
		if (i > p->p_classes.nprim) {
			printk(KERN_INFO
			       "SELinux:  class %s not defined in policy\n",
			       def_class);
			if (p->reject_unknown)
				return -EINVAL;
			if (p->allow_unknown)
				p->undefined_perms[i-1] = ~0U;
			continue;
		}
		pol_class = p->p_class_val_to_name[i-1];
		if (strcmp(pol_class, def_class)) {
			printk(KERN_ERR
			       "SELinux:  class %d is incorrect, found %s but should be %s\n",
			       i, pol_class, def_class);
			return -EINVAL;
		}
	}
	for (i = 0; i < kdefs->av_pts_len; i++) {
		class_val = kdefs->av_perm_to_string[i].tclass;
		perm_val = kdefs->av_perm_to_string[i].value;
		def_perm = kdefs->av_perm_to_string[i].name;
		if (class_val > p->p_classes.nprim)
			continue;
		pol_class = p->p_class_val_to_name[class_val-1];
		cladatum = hashtab_search(p->p_classes.table, pol_class);
		BUG_ON(!cladatum);
		perms = &cladatum->permissions;
		nprim = 1 << (perms->nprim - 1);
		if (perm_val > nprim) {
			printk(KERN_INFO
			       "SELinux:  permission %s in class %s not defined in policy\n",
			       def_perm, pol_class);
			if (p->reject_unknown)
				return -EINVAL;
			if (p->allow_unknown)
				p->undefined_perms[class_val-1] |= perm_val;
			continue;
		}
		perdatum = hashtab_search(perms->table, def_perm);
		if (perdatum == NULL) {
			printk(KERN_ERR
			       "SELinux:  permission %s in class %s not found in policy, bad policy\n",
			       def_perm, pol_class);
			return -EINVAL;
		}
		pol_val = 1 << (perdatum->value - 1);
		if (pol_val != perm_val) {
			printk(KERN_ERR
			       "SELinux:  permission %s in class %s has incorrect value\n",
			       def_perm, pol_class);
			return -EINVAL;
		}
	}
	for (i = 0; i < kdefs->av_inherit_len; i++) {
		class_val = kdefs->av_inherit[i].tclass;
		if (class_val > p->p_classes.nprim)
			continue;
		pol_class = p->p_class_val_to_name[class_val-1];
		cladatum = hashtab_search(p->p_classes.table, pol_class);
		BUG_ON(!cladatum);
		if (!cladatum->comdatum) {
			printk(KERN_ERR
			       "SELinux:  class %s should have an inherits clause but does not\n",
			       pol_class);
			return -EINVAL;
		}
		tmp = kdefs->av_inherit[i].common_base;
		common_pts_len = 0;
		while (!(tmp & 0x01)) {
			common_pts_len++;
			tmp >>= 1;
		}
		perms = &cladatum->comdatum->permissions;
		for (j = 0; j < common_pts_len; j++) {
			def_perm = kdefs->av_inherit[i].common_pts[j];
			if (j >= perms->nprim) {
				printk(KERN_INFO
				       "SELinux:  permission %s in class %s not defined in policy\n",
				       def_perm, pol_class);
				if (p->reject_unknown)
					return -EINVAL;
				if (p->allow_unknown)
					p->undefined_perms[class_val-1] |= (1 << j);
				continue;
			}
			perdatum = hashtab_search(perms->table, def_perm);
			if (perdatum == NULL) {
				printk(KERN_ERR
				       "SELinux:  permission %s in class %s not found in policy, bad policy\n",
				       def_perm, pol_class);
				return -EINVAL;
			}
			if (perdatum->value != j + 1) {
				printk(KERN_ERR
				       "SELinux:  permission %s in class %s has incorrect value\n",
				       def_perm, pol_class);
				return -EINVAL;
			}
		}
	}
	return 0;
}

/* Clone the SID into the new SID table. */
static int clone_sid(u32 sid,
		     struct context *context,
		     void *arg)
{
	struct sidtab *s = arg;

	return sidtab_insert(s, sid, context);
}

static inline int convert_context_handle_invalid_context(struct context *context)
{
	int rc = 0;

	if (selinux_enforcing) {
		rc = -EINVAL;
	} else {
		char *s;
		u32 len;

		context_struct_to_string(context, &s, &len);
		printk(KERN_ERR "SELinux:  context %s is invalid\n", s);
		kfree(s);
	}
	return rc;
}

struct convert_context_args {
	struct policydb *oldp;
	struct policydb *newp;
};

/*
 * Convert the values in the security context
 * structure `c' from the values specified
 * in the policy `p->oldp' to the values specified
 * in the policy `p->newp'.  Verify that the
 * context is valid under the new policy.
 */
static int convert_context(u32 key,
			   struct context *c,
			   void *p)
{
	struct convert_context_args *args;
	struct context oldc;
	struct role_datum *role;
	struct type_datum *typdatum;
	struct user_datum *usrdatum;
	char *s;
	u32 len;
	int rc;

	args = p;

	rc = context_cpy(&oldc, c);
	if (rc)
		goto out;

	rc = -EINVAL;

	/* Convert the user. */
	usrdatum = hashtab_search(args->newp->p_users.table,
				  args->oldp->p_user_val_to_name[c->user - 1]);
	if (!usrdatum)
		goto bad;
	c->user = usrdatum->value;

	/* Convert the role. */
	role = hashtab_search(args->newp->p_roles.table,
			      args->oldp->p_role_val_to_name[c->role - 1]);
	if (!role)
		goto bad;
	c->role = role->value;

	/* Convert the type. */
	typdatum = hashtab_search(args->newp->p_types.table,
				  args->oldp->p_type_val_to_name[c->type - 1]);
	if (!typdatum)
		goto bad;
	c->type = typdatum->value;

	rc = mls_convert_context(args->oldp, args->newp, c);
	if (rc)
		goto bad;

	/* Check the validity of the new context. */
	if (!policydb_context_isvalid(args->newp, c)) {
		rc = convert_context_handle_invalid_context(&oldc);
		if (rc)
			goto bad;
	}

	context_destroy(&oldc);
out:
	return rc;
bad:
	context_struct_to_string(&oldc, &s, &len);
	context_destroy(&oldc);
	printk(KERN_ERR "SELinux:  invalidating context %s\n", s);
	kfree(s);
	goto out;
}

static void security_load_policycaps(void)
{
	selinux_policycap_netpeer = ebitmap_get_bit(&policydb.policycaps,
						  POLICYDB_CAPABILITY_NETPEER);
	selinux_policycap_openperm = ebitmap_get_bit(&policydb.policycaps,
						  POLICYDB_CAPABILITY_OPENPERM);
}

extern void selinux_complete_init(void);
static int security_preserve_bools(struct policydb *p);

/**
 * security_load_policy - Load a security policy configuration.
 * @data: binary policy data
 * @len: length of data in bytes
 *
 * Load a new set of security policy configuration data,
 * validate it and convert the SID table as necessary.
 * This function will flush the access vector cache after
 * loading the new policy.
 */
int security_load_policy(void *data, size_t len)
{
	struct policydb oldpolicydb, newpolicydb;
	struct sidtab oldsidtab, newsidtab;
	struct convert_context_args args;
	u32 seqno;
	int rc = 0;
	struct policy_file file = { data, len }, *fp = &file;

	LOAD_LOCK;

	if (!ss_initialized) {
		avtab_cache_init();
		if (policydb_read(&policydb, fp)) {
			LOAD_UNLOCK;
			avtab_cache_destroy();
			return -EINVAL;
		}
		if (policydb_load_isids(&policydb, &sidtab)) {
			LOAD_UNLOCK;
			policydb_destroy(&policydb);
			avtab_cache_destroy();
			return -EINVAL;
		}
		/* Verify that the kernel defined classes are correct. */
		if (validate_classes(&policydb)) {
			printk(KERN_ERR
			       "SELinux:  the definition of a class is incorrect\n");
			LOAD_UNLOCK;
			sidtab_destroy(&sidtab);
			policydb_destroy(&policydb);
			avtab_cache_destroy();
			return -EINVAL;
		}
		security_load_policycaps();
		policydb_loaded_version = policydb.policyvers;
		ss_initialized = 1;
		seqno = ++latest_granting;
		LOAD_UNLOCK;
		selinux_complete_init();
		avc_ss_reset(seqno);
		selnl_notify_policyload(seqno);
		selinux_netlbl_cache_invalidate();
		selinux_xfrm_notify_policyload();
		return 0;
	}

#if 0
	sidtab_hash_eval(&sidtab, "sids");
#endif

	if (policydb_read(&newpolicydb, fp)) {
		LOAD_UNLOCK;
		return -EINVAL;
	}

	sidtab_init(&newsidtab);

	/* Verify that the kernel defined classes are correct. */
	if (validate_classes(&newpolicydb)) {
		printk(KERN_ERR
		       "SELinux:  the definition of a class is incorrect\n");
		rc = -EINVAL;
		goto err;
	}

	rc = security_preserve_bools(&newpolicydb);
	if (rc) {
		printk(KERN_ERR "SELinux:  unable to preserve booleans\n");
		goto err;
	}

	/* Clone the SID table. */
	sidtab_shutdown(&sidtab);
	if (sidtab_map(&sidtab, clone_sid, &newsidtab)) {
		rc = -ENOMEM;
		goto err;
	}

	/* Convert the internal representations of contexts
	   in the new SID table and remove invalid SIDs. */
	args.oldp = &policydb;
	args.newp = &newpolicydb;
	sidtab_map_remove_on_error(&newsidtab, convert_context, &args);

	/* Save the old policydb and SID table to free later. */
	memcpy(&oldpolicydb, &policydb, sizeof policydb);
	sidtab_set(&oldsidtab, &sidtab);

	/* Install the new policydb and SID table. */
	POLICY_WRLOCK;
	memcpy(&policydb, &newpolicydb, sizeof policydb);
	sidtab_set(&sidtab, &newsidtab);
	security_load_policycaps();
	seqno = ++latest_granting;
	policydb_loaded_version = policydb.policyvers;
	POLICY_WRUNLOCK;
	LOAD_UNLOCK;

	/* Free the old policydb and SID table. */
	policydb_destroy(&oldpolicydb);
	sidtab_destroy(&oldsidtab);

	avc_ss_reset(seqno);
	selnl_notify_policyload(seqno);
	selinux_netlbl_cache_invalidate();
	selinux_xfrm_notify_policyload();

	return 0;

err:
	LOAD_UNLOCK;
	sidtab_destroy(&newsidtab);
	policydb_destroy(&newpolicydb);
	return rc;

}

/**
 * security_port_sid - Obtain the SID for a port.
 * @protocol: protocol number
 * @port: port number
 * @out_sid: security identifier
 */
int security_port_sid(u8 protocol, u16 port, u32 *out_sid)
{
	struct ocontext *c;
	int rc = 0;

	POLICY_RDLOCK;

	c = policydb.ocontexts[OCON_PORT];
	while (c) {
		if (c->u.port.protocol == protocol &&
		    c->u.port.low_port <= port &&
		    c->u.port.high_port >= port)
			break;
		c = c->next;
	}

	if (c) {
		if (!c->sid[0]) {
			rc = sidtab_context_to_sid(&sidtab,
						   &c->context[0],
						   &c->sid[0]);
			if (rc)
				goto out;
		}
		*out_sid = c->sid[0];
	} else {
		*out_sid = SECINITSID_PORT;
	}

out:
	POLICY_RDUNLOCK;
	return rc;
}

/**
 * security_netif_sid - Obtain the SID for a network interface.
 * @name: interface name
 * @if_sid: interface SID
 */
int security_netif_sid(char *name, u32 *if_sid)
{
	int rc = 0;
	struct ocontext *c;

	POLICY_RDLOCK;

	c = policydb.ocontexts[OCON_NETIF];
	while (c) {
		if (strcmp(name, c->u.name) == 0)
			break;
		c = c->next;
	}

	if (c) {
		if (!c->sid[0] || !c->sid[1]) {
			rc = sidtab_context_to_sid(&sidtab,
						  &c->context[0],
						  &c->sid[0]);
			if (rc)
				goto out;
			rc = sidtab_context_to_sid(&sidtab,
						   &c->context[1],
						   &c->sid[1]);
			if (rc)
				goto out;
		}
		*if_sid = c->sid[0];
	} else
		*if_sid = SECINITSID_NETIF;

out:
	POLICY_RDUNLOCK;
	return rc;
}

static int match_ipv6_addrmask(u32 *input, u32 *addr, u32 *mask)
{
	int i, fail = 0;

	for (i = 0; i < 4; i++)
		if (addr[i] != (input[i] & mask[i])) {
			fail = 1;
			break;
		}

	return !fail;
}

/**
 * security_node_sid - Obtain the SID for a node (host).
 * @domain: communication domain aka address family
 * @addrp: address
 * @addrlen: address length in bytes
 * @out_sid: security identifier
 */
int security_node_sid(u16 domain,
		      void *addrp,
		      u32 addrlen,
		      u32 *out_sid)
{
	int rc = 0;
	struct ocontext *c;

	POLICY_RDLOCK;

	switch (domain) {
	case AF_INET: {
		u32 addr;

		if (addrlen != sizeof(u32)) {
			rc = -EINVAL;
			goto out;
		}

		addr = *((u32 *)addrp);

		c = policydb.ocontexts[OCON_NODE];
		while (c) {
			if (c->u.node.addr == (addr & c->u.node.mask))
				break;
			c = c->next;
		}
		break;
	}

	case AF_INET6:
		if (addrlen != sizeof(u64) * 2) {
			rc = -EINVAL;
			goto out;
		}
		c = policydb.ocontexts[OCON_NODE6];
		while (c) {
			if (match_ipv6_addrmask(addrp, c->u.node6.addr,
						c->u.node6.mask))
				break;
			c = c->next;
		}
		break;

	default:
		*out_sid = SECINITSID_NODE;
		goto out;
	}

	if (c) {
		if (!c->sid[0]) {
			rc = sidtab_context_to_sid(&sidtab,
						   &c->context[0],
						   &c->sid[0]);
			if (rc)
				goto out;
		}
		*out_sid = c->sid[0];
	} else {
		*out_sid = SECINITSID_NODE;
	}

out:
	POLICY_RDUNLOCK;
	return rc;
}

#define SIDS_NEL 25

/**
 * security_get_user_sids - Obtain reachable SIDs for a user.
 * @fromsid: starting SID
 * @username: username
 * @sids: array of reachable SIDs for user
 * @nel: number of elements in @sids
 *
 * Generate the set of SIDs for legal security contexts
 * for a given user that can be reached by @fromsid.
 * Set *@sids to point to a dynamically allocated
 * array containing the set of SIDs.  Set *@nel to the
 * number of elements in the array.
 */

int security_get_user_sids(u32 fromsid,
			   char *username,
			   u32 **sids,
			   u32 *nel)
{
	struct context *fromcon, usercon;
	u32 *mysids = NULL, *mysids2, sid;
	u32 mynel = 0, maxnel = SIDS_NEL;
	struct user_datum *user;
	struct role_datum *role;
	struct ebitmap_node *rnode, *tnode;
	int rc = 0, i, j;

	*sids = NULL;
	*nel = 0;

	if (!ss_initialized)
		goto out;

	POLICY_RDLOCK;

	fromcon = sidtab_search(&sidtab, fromsid);
	if (!fromcon) {
		rc = -EINVAL;
		goto out_unlock;
	}

	user = hashtab_search(policydb.p_users.table, username);
	if (!user) {
		rc = -EINVAL;
		goto out_unlock;
	}
	usercon.user = user->value;

	mysids = kcalloc(maxnel, sizeof(*mysids), GFP_ATOMIC);
	if (!mysids) {
		rc = -ENOMEM;
		goto out_unlock;
	}

	ebitmap_for_each_positive_bit(&user->roles, rnode, i) {
		role = policydb.role_val_to_struct[i];
		usercon.role = i+1;
		ebitmap_for_each_positive_bit(&role->types, tnode, j) {
			usercon.type = j+1;

			if (mls_setup_user_range(fromcon, user, &usercon))
				continue;

			rc = sidtab_context_to_sid(&sidtab, &usercon, &sid);
			if (rc)
				goto out_unlock;
			if (mynel < maxnel) {
				mysids[mynel++] = sid;
			} else {
				maxnel += SIDS_NEL;
				mysids2 = kcalloc(maxnel, sizeof(*mysids2), GFP_ATOMIC);
				if (!mysids2) {
					rc = -ENOMEM;
					goto out_unlock;
				}
				memcpy(mysids2, mysids, mynel * sizeof(*mysids2));
				kfree(mysids);
				mysids = mysids2;
				mysids[mynel++] = sid;
			}
		}
	}

out_unlock:
	POLICY_RDUNLOCK;
	if (rc || !mynel) {
		kfree(mysids);
		goto out;
	}

	mysids2 = kcalloc(mynel, sizeof(*mysids2), GFP_KERNEL);
	if (!mysids2) {
		rc = -ENOMEM;
		kfree(mysids);
		goto out;
	}
	for (i = 0, j = 0; i < mynel; i++) {
		rc = avc_has_perm_noaudit(fromsid, mysids[i],
					  SECCLASS_PROCESS,
					  PROCESS__TRANSITION, AVC_STRICT,
					  NULL);
		if (!rc)
			mysids2[j++] = mysids[i];
		cond_resched();
	}
	rc = 0;
	kfree(mysids);
	*sids = mysids2;
	*nel = j;
out:
	return rc;
}

/**
 * security_genfs_sid - Obtain a SID for a file in a filesystem
 * @fstype: filesystem type
 * @path: path from root of mount
 * @sclass: file security class
 * @sid: SID for path
 *
 * Obtain a SID to use for a file in a filesystem that
 * cannot support xattr or use a fixed labeling behavior like
 * transition SIDs or task SIDs.
 */
int security_genfs_sid(const char *fstype,
		       char *path,
		       u16 sclass,
		       u32 *sid)
{
	int len;
	struct genfs *genfs;
	struct ocontext *c;
	int rc = 0, cmp = 0;

	while (path[0] == '/' && path[1] == '/')
		path++;

	POLICY_RDLOCK;

	for (genfs = policydb.genfs; genfs; genfs = genfs->next) {
		cmp = strcmp(fstype, genfs->fstype);
		if (cmp <= 0)
			break;
	}

	if (!genfs || cmp) {
		*sid = SECINITSID_UNLABELED;
		rc = -ENOENT;
		goto out;
	}

	for (c = genfs->head; c; c = c->next) {
		len = strlen(c->u.name);
		if ((!c->v.sclass || sclass == c->v.sclass) &&
		    (strncmp(c->u.name, path, len) == 0))
			break;
	}

	if (!c) {
		*sid = SECINITSID_UNLABELED;
		rc = -ENOENT;
		goto out;
	}

	if (!c->sid[0]) {
		rc = sidtab_context_to_sid(&sidtab,
					   &c->context[0],
					   &c->sid[0]);
		if (rc)
			goto out;
	}

	*sid = c->sid[0];
out:
	POLICY_RDUNLOCK;
	return rc;
}

/**
 * security_fs_use - Determine how to handle labeling for a filesystem.
 * @fstype: filesystem type
 * @behavior: labeling behavior
 * @sid: SID for filesystem (superblock)
 */
int security_fs_use(
	const char *fstype,
	unsigned int *behavior,
	u32 *sid)
{
	int rc = 0;
	struct ocontext *c;

	POLICY_RDLOCK;

	c = policydb.ocontexts[OCON_FSUSE];
	while (c) {
		if (strcmp(fstype, c->u.name) == 0)
			break;
		c = c->next;
	}

	if (c) {
		*behavior = c->v.behavior;
		if (!c->sid[0]) {
			rc = sidtab_context_to_sid(&sidtab,
						   &c->context[0],
						   &c->sid[0]);
			if (rc)
				goto out;
		}
		*sid = c->sid[0];
	} else {
		rc = security_genfs_sid(fstype, "/", SECCLASS_DIR, sid);
		if (rc) {
			*behavior = SECURITY_FS_USE_NONE;
			rc = 0;
		} else {
			*behavior = SECURITY_FS_USE_GENFS;
		}
	}

out:
	POLICY_RDUNLOCK;
	return rc;
}

int security_get_bools(int *len, char ***names, int **values)
{
	int i, rc = -ENOMEM;

	POLICY_RDLOCK;
	*names = NULL;
	*values = NULL;

	*len = policydb.p_bools.nprim;
	if (!*len) {
		rc = 0;
		goto out;
	}

       *names = kcalloc(*len, sizeof(char *), GFP_ATOMIC);
	if (!*names)
		goto err;

       *values = kcalloc(*len, sizeof(int), GFP_ATOMIC);
	if (!*values)
		goto err;

	for (i = 0; i < *len; i++) {
		size_t name_len;
		(*values)[i] = policydb.bool_val_to_struct[i]->state;
		name_len = strlen(policydb.p_bool_val_to_name[i]) + 1;
	       (*names)[i] = kmalloc(sizeof(char) * name_len, GFP_ATOMIC);
		if (!(*names)[i])
			goto err;
		strncpy((*names)[i], policydb.p_bool_val_to_name[i], name_len);
		(*names)[i][name_len - 1] = 0;
	}
	rc = 0;
out:
	POLICY_RDUNLOCK;
	return rc;
err:
	if (*names) {
		for (i = 0; i < *len; i++)
			kfree((*names)[i]);
	}
	kfree(*values);
	goto out;
}


int security_set_bools(int len, int *values)
{
	int i, rc = 0;
	int lenp, seqno = 0;
	struct cond_node *cur;

	POLICY_WRLOCK;

	lenp = policydb.p_bools.nprim;
	if (len != lenp) {
		rc = -EFAULT;
		goto out;
	}

	for (i = 0; i < len; i++) {
		if (!!values[i] != policydb.bool_val_to_struct[i]->state) {
			audit_log(current->audit_context, GFP_ATOMIC,
				AUDIT_MAC_CONFIG_CHANGE,
				"bool=%s val=%d old_val=%d auid=%u ses=%u",
				policydb.p_bool_val_to_name[i],
				!!values[i],
				policydb.bool_val_to_struct[i]->state,
				audit_get_loginuid(current),
				audit_get_sessionid(current));
		}
		if (values[i])
			policydb.bool_val_to_struct[i]->state = 1;
		else
			policydb.bool_val_to_struct[i]->state = 0;
	}

	for (cur = policydb.cond_list; cur != NULL; cur = cur->next) {
		rc = evaluate_cond_node(&policydb, cur);
		if (rc)
			goto out;
	}

	seqno = ++latest_granting;

out:
	POLICY_WRUNLOCK;
	if (!rc) {
		avc_ss_reset(seqno);
		selnl_notify_policyload(seqno);
		selinux_xfrm_notify_policyload();
	}
	return rc;
}

int security_get_bool_value(int bool)
{
	int rc = 0;
	int len;

	POLICY_RDLOCK;

	len = policydb.p_bools.nprim;
	if (bool >= len) {
		rc = -EFAULT;
		goto out;
	}

	rc = policydb.bool_val_to_struct[bool]->state;
out:
	POLICY_RDUNLOCK;
	return rc;
}

static int security_preserve_bools(struct policydb *p)
{
	int rc, nbools = 0, *bvalues = NULL, i;
	char **bnames = NULL;
	struct cond_bool_datum *booldatum;
	struct cond_node *cur;

	rc = security_get_bools(&nbools, &bnames, &bvalues);
	if (rc)
		goto out;
	for (i = 0; i < nbools; i++) {
		booldatum = hashtab_search(p->p_bools.table, bnames[i]);
		if (booldatum)
			booldatum->state = bvalues[i];
	}
	for (cur = p->cond_list; cur != NULL; cur = cur->next) {
		rc = evaluate_cond_node(p, cur);
		if (rc)
			goto out;
	}

out:
	if (bnames) {
		for (i = 0; i < nbools; i++)
			kfree(bnames[i]);
	}
	kfree(bnames);
	kfree(bvalues);
	return rc;
}

/*
 * security_sid_mls_copy() - computes a new sid based on the given
 * sid and the mls portion of mls_sid.
 */
int security_sid_mls_copy(u32 sid, u32 mls_sid, u32 *new_sid)
{
	struct context *context1;
	struct context *context2;
	struct context newcon;
	char *s;
	u32 len;
	int rc = 0;

	if (!ss_initialized || !selinux_mls_enabled) {
		*new_sid = sid;
		goto out;
	}

	context_init(&newcon);

	POLICY_RDLOCK;
	context1 = sidtab_search(&sidtab, sid);
	if (!context1) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
			__func__, sid);
		rc = -EINVAL;
		goto out_unlock;
	}

	context2 = sidtab_search(&sidtab, mls_sid);
	if (!context2) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
			__func__, mls_sid);
		rc = -EINVAL;
		goto out_unlock;
	}

	newcon.user = context1->user;
	newcon.role = context1->role;
	newcon.type = context1->type;
	rc = mls_context_cpy(&newcon, context2);
	if (rc)
		goto out_unlock;

	/* Check the validity of the new context. */
	if (!policydb_context_isvalid(&policydb, &newcon)) {
		rc = convert_context_handle_invalid_context(&newcon);
		if (rc)
			goto bad;
	}

	rc = sidtab_context_to_sid(&sidtab, &newcon, new_sid);
	goto out_unlock;

bad:
	if (!context_struct_to_string(&newcon, &s, &len)) {
		audit_log(current->audit_context, GFP_ATOMIC, AUDIT_SELINUX_ERR,
			  "security_sid_mls_copy: invalid context %s", s);
		kfree(s);
	}

out_unlock:
	POLICY_RDUNLOCK;
	context_destroy(&newcon);
out:
	return rc;
}

/**
 * security_net_peersid_resolve - Compare and resolve two network peer SIDs
 * @nlbl_sid: NetLabel SID
 * @nlbl_type: NetLabel labeling protocol type
 * @xfrm_sid: XFRM SID
 *
 * Description:
 * Compare the @nlbl_sid and @xfrm_sid values and if the two SIDs can be
 * resolved into a single SID it is returned via @peer_sid and the function
 * returns zero.  Otherwise @peer_sid is set to SECSID_NULL and the function
 * returns a negative value.  A table summarizing the behavior is below:
 *
 *                                 | function return |      @sid
 *   ------------------------------+-----------------+-----------------
 *   no peer labels                |        0        |    SECSID_NULL
 *   single peer label             |        0        |    <peer_label>
 *   multiple, consistent labels   |        0        |    <peer_label>
 *   multiple, inconsistent labels |    -<errno>     |    SECSID_NULL
 *
 */
int security_net_peersid_resolve(u32 nlbl_sid, u32 nlbl_type,
				 u32 xfrm_sid,
				 u32 *peer_sid)
{
	int rc;
	struct context *nlbl_ctx;
	struct context *xfrm_ctx;

	/* handle the common (which also happens to be the set of easy) cases
	 * right away, these two if statements catch everything involving a
	 * single or absent peer SID/label */
	if (xfrm_sid == SECSID_NULL) {
		*peer_sid = nlbl_sid;
		return 0;
	}
	/* NOTE: an nlbl_type == NETLBL_NLTYPE_UNLABELED is a "fallback" label
	 * and is treated as if nlbl_sid == SECSID_NULL when a XFRM SID/label
	 * is present */
	if (nlbl_sid == SECSID_NULL || nlbl_type == NETLBL_NLTYPE_UNLABELED) {
		*peer_sid = xfrm_sid;
		return 0;
	}

	/* we don't need to check ss_initialized here since the only way both
	 * nlbl_sid and xfrm_sid are not equal to SECSID_NULL would be if the
	 * security server was initialized and ss_initialized was true */
	if (!selinux_mls_enabled) {
		*peer_sid = SECSID_NULL;
		return 0;
	}

	POLICY_RDLOCK;

	nlbl_ctx = sidtab_search(&sidtab, nlbl_sid);
	if (!nlbl_ctx) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
		       __func__, nlbl_sid);
		rc = -EINVAL;
		goto out_slowpath;
	}
	xfrm_ctx = sidtab_search(&sidtab, xfrm_sid);
	if (!xfrm_ctx) {
		printk(KERN_ERR "SELinux: %s:  unrecognized SID %d\n",
		       __func__, xfrm_sid);
		rc = -EINVAL;
		goto out_slowpath;
	}
	rc = (mls_context_cmp(nlbl_ctx, xfrm_ctx) ? 0 : -EACCES);

out_slowpath:
	POLICY_RDUNLOCK;
	if (rc == 0)
		/* at present NetLabel SIDs/labels really only carry MLS
		 * information so if the MLS portion of the NetLabel SID
		 * matches the MLS portion of the labeled XFRM SID/label
		 * then pass along the XFRM SID as it is the most
		 * expressive */
		*peer_sid = xfrm_sid;
	else
		*peer_sid = SECSID_NULL;
	return rc;
}

static int get_classes_callback(void *k, void *d, void *args)
{
	struct class_datum *datum = d;
	char *name = k, **classes = args;
	int value = datum->value - 1;

	classes[value] = kstrdup(name, GFP_ATOMIC);
	if (!classes[value])
		return -ENOMEM;

	return 0;
}

int security_get_classes(char ***classes, int *nclasses)
{
	int rc = -ENOMEM;

	POLICY_RDLOCK;

	*nclasses = policydb.p_classes.nprim;
	*classes = kcalloc(*nclasses, sizeof(*classes), GFP_ATOMIC);
	if (!*classes)
		goto out;

	rc = hashtab_map(policydb.p_classes.table, get_classes_callback,
			*classes);
	if (rc < 0) {
		int i;
		for (i = 0; i < *nclasses; i++)
			kfree((*classes)[i]);
		kfree(*classes);
	}

out:
	POLICY_RDUNLOCK;
	return rc;
}

static int get_permissions_callback(void *k, void *d, void *args)
{
	struct perm_datum *datum = d;
	char *name = k, **perms = args;
	int value = datum->value - 1;

	perms[value] = kstrdup(name, GFP_ATOMIC);
	if (!perms[value])
		return -ENOMEM;

	return 0;
}

int security_get_permissions(char *class, char ***perms, int *nperms)
{
	int rc = -ENOMEM, i;
	struct class_datum *match;

	POLICY_RDLOCK;

	match = hashtab_search(policydb.p_classes.table, class);
	if (!match) {
		printk(KERN_ERR "SELinux: %s:  unrecognized class %s\n",
			__func__, class);
		rc = -EINVAL;
		goto out;
	}

	*nperms = match->permissions.nprim;
	*perms = kcalloc(*nperms, sizeof(*perms), GFP_ATOMIC);
	if (!*perms)
		goto out;

	if (match->comdatum) {
		rc = hashtab_map(match->comdatum->permissions.table,
				get_permissions_callback, *perms);
		if (rc < 0)
			goto err;
	}

	rc = hashtab_map(match->permissions.table, get_permissions_callback,
			*perms);
	if (rc < 0)
		goto err;

out:
	POLICY_RDUNLOCK;
	return rc;

err:
	POLICY_RDUNLOCK;
	for (i = 0; i < *nperms; i++)
		kfree((*perms)[i]);
	kfree(*perms);
	return rc;
}

int security_get_reject_unknown(void)
{
	return policydb.reject_unknown;
}

int security_get_allow_unknown(void)
{
	return policydb.allow_unknown;
}

/**
 * security_policycap_supported - Check for a specific policy capability
 * @req_cap: capability
 *
 * Description:
 * This function queries the currently loaded policy to see if it supports the
 * capability specified by @req_cap.  Returns true (1) if the capability is
 * supported, false (0) if it isn't supported.
 *
 */
int security_policycap_supported(unsigned int req_cap)
{
	int rc;

	POLICY_RDLOCK;
	rc = ebitmap_get_bit(&policydb.policycaps, req_cap);
	POLICY_RDUNLOCK;

	return rc;
}

struct selinux_audit_rule {
	u32 au_seqno;
	struct context au_ctxt;
};

void selinux_audit_rule_free(void *vrule)
{
	struct selinux_audit_rule *rule = vrule;

	if (rule) {
		context_destroy(&rule->au_ctxt);
		kfree(rule);
	}
}

int selinux_audit_rule_init(u32 field, u32 op, char *rulestr, void **vrule)
{
	struct selinux_audit_rule *tmprule;
	struct role_datum *roledatum;
	struct type_datum *typedatum;
	struct user_datum *userdatum;
	struct selinux_audit_rule **rule = (struct selinux_audit_rule **)vrule;
	int rc = 0;

	*rule = NULL;

	if (!ss_initialized)
		return -EOPNOTSUPP;

	switch (field) {
	case AUDIT_SUBJ_USER:
	case AUDIT_SUBJ_ROLE:
	case AUDIT_SUBJ_TYPE:
	case AUDIT_OBJ_USER:
	case AUDIT_OBJ_ROLE:
	case AUDIT_OBJ_TYPE:
		/* only 'equals' and 'not equals' fit user, role, and type */
		if (op != AUDIT_EQUAL && op != AUDIT_NOT_EQUAL)
			return -EINVAL;
		break;
	case AUDIT_SUBJ_SEN:
	case AUDIT_SUBJ_CLR:
	case AUDIT_OBJ_LEV_LOW:
	case AUDIT_OBJ_LEV_HIGH:
		/* we do not allow a range, indicated by the presense of '-' */
		if (strchr(rulestr, '-'))
			return -EINVAL;
		break;
	default:
		/* only the above fields are valid */
		return -EINVAL;
	}

	tmprule = kzalloc(sizeof(struct selinux_audit_rule), GFP_KERNEL);
	if (!tmprule)
		return -ENOMEM;

	context_init(&tmprule->au_ctxt);

	POLICY_RDLOCK;

	tmprule->au_seqno = latest_granting;

	switch (field) {
	case AUDIT_SUBJ_USER:
	case AUDIT_OBJ_USER:
		userdatum = hashtab_search(policydb.p_users.table, rulestr);
		if (!userdatum)
			rc = -EINVAL;
		else
			tmprule->au_ctxt.user = userdatum->value;
		break;
	case AUDIT_SUBJ_ROLE:
	case AUDIT_OBJ_ROLE:
		roledatum = hashtab_search(policydb.p_roles.table, rulestr);
		if (!roledatum)
			rc = -EINVAL;
		else
			tmprule->au_ctxt.role = roledatum->value;
		break;
	case AUDIT_SUBJ_TYPE:
	case AUDIT_OBJ_TYPE:
		typedatum = hashtab_search(policydb.p_types.table, rulestr);
		if (!typedatum)
			rc = -EINVAL;
		else
			tmprule->au_ctxt.type = typedatum->value;
		break;
	case AUDIT_SUBJ_SEN:
	case AUDIT_SUBJ_CLR:
	case AUDIT_OBJ_LEV_LOW:
	case AUDIT_OBJ_LEV_HIGH:
		rc = mls_from_string(rulestr, &tmprule->au_ctxt, GFP_ATOMIC);
		break;
	}

	POLICY_RDUNLOCK;

	if (rc) {
		selinux_audit_rule_free(tmprule);
		tmprule = NULL;
	}

	*rule = tmprule;

	return rc;
}

/* Check to see if the rule contains any selinux fields */
int selinux_audit_rule_known(struct audit_krule *rule)
{
	int i;

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &rule->fields[i];
		switch (f->type) {
		case AUDIT_SUBJ_USER:
		case AUDIT_SUBJ_ROLE:
		case AUDIT_SUBJ_TYPE:
		case AUDIT_SUBJ_SEN:
		case AUDIT_SUBJ_CLR:
		case AUDIT_OBJ_USER:
		case AUDIT_OBJ_ROLE:
		case AUDIT_OBJ_TYPE:
		case AUDIT_OBJ_LEV_LOW:
		case AUDIT_OBJ_LEV_HIGH:
			return 1;
		}
	}

	return 0;
}

int selinux_audit_rule_match(u32 sid, u32 field, u32 op, void *vrule,
                             struct audit_context *actx)
{
	struct context *ctxt;
	struct mls_level *level;
	struct selinux_audit_rule *rule = vrule;
	int match = 0;

	if (!rule) {
		audit_log(actx, GFP_ATOMIC, AUDIT_SELINUX_ERR,
			  "selinux_audit_rule_match: missing rule\n");
		return -ENOENT;
	}

	POLICY_RDLOCK;

	if (rule->au_seqno < latest_granting) {
		audit_log(actx, GFP_ATOMIC, AUDIT_SELINUX_ERR,
			  "selinux_audit_rule_match: stale rule\n");
		match = -ESTALE;
		goto out;
	}

	ctxt = sidtab_search(&sidtab, sid);
	if (!ctxt) {
		audit_log(actx, GFP_ATOMIC, AUDIT_SELINUX_ERR,
			  "selinux_audit_rule_match: unrecognized SID %d\n",
			  sid);
		match = -ENOENT;
		goto out;
	}

	/* a field/op pair that is not caught here will simply fall through
	   without a match */
	switch (field) {
	case AUDIT_SUBJ_USER:
	case AUDIT_OBJ_USER:
		switch (op) {
		case AUDIT_EQUAL:
			match = (ctxt->user == rule->au_ctxt.user);
			break;
		case AUDIT_NOT_EQUAL:
			match = (ctxt->user != rule->au_ctxt.user);
			break;
		}
		break;
	case AUDIT_SUBJ_ROLE:
	case AUDIT_OBJ_ROLE:
		switch (op) {
		case AUDIT_EQUAL:
			match = (ctxt->role == rule->au_ctxt.role);
			break;
		case AUDIT_NOT_EQUAL:
			match = (ctxt->role != rule->au_ctxt.role);
			break;
		}
		break;
	case AUDIT_SUBJ_TYPE:
	case AUDIT_OBJ_TYPE:
		switch (op) {
		case AUDIT_EQUAL:
			match = (ctxt->type == rule->au_ctxt.type);
			break;
		case AUDIT_NOT_EQUAL:
			match = (ctxt->type != rule->au_ctxt.type);
			break;
		}
		break;
	case AUDIT_SUBJ_SEN:
	case AUDIT_SUBJ_CLR:
	case AUDIT_OBJ_LEV_LOW:
	case AUDIT_OBJ_LEV_HIGH:
		level = ((field == AUDIT_SUBJ_SEN ||
			  field == AUDIT_OBJ_LEV_LOW) ?
			 &ctxt->range.level[0] : &ctxt->range.level[1]);
		switch (op) {
		case AUDIT_EQUAL:
			match = mls_level_eq(&rule->au_ctxt.range.level[0],
					     level);
			break;
		case AUDIT_NOT_EQUAL:
			match = !mls_level_eq(&rule->au_ctxt.range.level[0],
					      level);
			break;
		case AUDIT_LESS_THAN:
			match = (mls_level_dom(&rule->au_ctxt.range.level[0],
					       level) &&
				 !mls_level_eq(&rule->au_ctxt.range.level[0],
					       level));
			break;
		case AUDIT_LESS_THAN_OR_EQUAL:
			match = mls_level_dom(&rule->au_ctxt.range.level[0],
					      level);
			break;
		case AUDIT_GREATER_THAN:
			match = (mls_level_dom(level,
					      &rule->au_ctxt.range.level[0]) &&
				 !mls_level_eq(level,
					       &rule->au_ctxt.range.level[0]));
			break;
		case AUDIT_GREATER_THAN_OR_EQUAL:
			match = mls_level_dom(level,
					      &rule->au_ctxt.range.level[0]);
			break;
		}
	}

out:
	POLICY_RDUNLOCK;
	return match;
}

static int (*aurule_callback)(void) = audit_update_lsm_rules;

static int aurule_avc_callback(u32 event, u32 ssid, u32 tsid,
                               u16 class, u32 perms, u32 *retained)
{
	int err = 0;

	if (event == AVC_CALLBACK_RESET && aurule_callback)
		err = aurule_callback();
	return err;
}

static int __init aurule_init(void)
{
	int err;

	err = avc_add_callback(aurule_avc_callback, AVC_CALLBACK_RESET,
			       SECSID_NULL, SECSID_NULL, SECCLASS_NULL, 0);
	if (err)
		panic("avc_add_callback() failed, error %d\n", err);

	return err;
}
__initcall(aurule_init);

#ifdef CONFIG_NETLABEL
/**
 * security_netlbl_cache_add - Add an entry to the NetLabel cache
 * @secattr: the NetLabel packet security attributes
 * @sid: the SELinux SID
 *
 * Description:
 * Attempt to cache the context in @ctx, which was derived from the packet in
 * @skb, in the NetLabel subsystem cache.  This function assumes @secattr has
 * already been initialized.
 *
 */
static void security_netlbl_cache_add(struct netlbl_lsm_secattr *secattr,
				      u32 sid)
{
	u32 *sid_cache;

	sid_cache = kmalloc(sizeof(*sid_cache), GFP_ATOMIC);
	if (sid_cache == NULL)
		return;
	secattr->cache = netlbl_secattr_cache_alloc(GFP_ATOMIC);
	if (secattr->cache == NULL) {
		kfree(sid_cache);
		return;
	}

	*sid_cache = sid;
	secattr->cache->free = kfree;
	secattr->cache->data = sid_cache;
	secattr->flags |= NETLBL_SECATTR_CACHE;
}

/**
 * security_netlbl_secattr_to_sid - Convert a NetLabel secattr to a SELinux SID
 * @secattr: the NetLabel packet security attributes
 * @sid: the SELinux SID
 *
 * Description:
 * Convert the given NetLabel security attributes in @secattr into a
 * SELinux SID.  If the @secattr field does not contain a full SELinux
 * SID/context then use SECINITSID_NETMSG as the foundation.  If possibile the
 * 'cache' field of @secattr is set and the CACHE flag is set; this is to
 * allow the @secattr to be used by NetLabel to cache the secattr to SID
 * conversion for future lookups.  Returns zero on success, negative values on
 * failure.
 *
 */
int security_netlbl_secattr_to_sid(struct netlbl_lsm_secattr *secattr,
				   u32 *sid)
{
	int rc = -EIDRM;
	struct context *ctx;
	struct context ctx_new;

	if (!ss_initialized) {
		*sid = SECSID_NULL;
		return 0;
	}

	POLICY_RDLOCK;

	if (secattr->flags & NETLBL_SECATTR_CACHE) {
		*sid = *(u32 *)secattr->cache->data;
		rc = 0;
	} else if (secattr->flags & NETLBL_SECATTR_SECID) {
		*sid = secattr->attr.secid;
		rc = 0;
	} else if (secattr->flags & NETLBL_SECATTR_MLS_LVL) {
		ctx = sidtab_search(&sidtab, SECINITSID_NETMSG);
		if (ctx == NULL)
			goto netlbl_secattr_to_sid_return;

		ctx_new.user = ctx->user;
		ctx_new.role = ctx->role;
		ctx_new.type = ctx->type;
		mls_import_netlbl_lvl(&ctx_new, secattr);
		if (secattr->flags & NETLBL_SECATTR_MLS_CAT) {
			if (ebitmap_netlbl_import(&ctx_new.range.level[0].cat,
						  secattr->attr.mls.cat) != 0)
				goto netlbl_secattr_to_sid_return;
			ctx_new.range.level[1].cat.highbit =
				ctx_new.range.level[0].cat.highbit;
			ctx_new.range.level[1].cat.node =
				ctx_new.range.level[0].cat.node;
		} else {
			ebitmap_init(&ctx_new.range.level[0].cat);
			ebitmap_init(&ctx_new.range.level[1].cat);
		}
		if (mls_context_isvalid(&policydb, &ctx_new) != 1)
			goto netlbl_secattr_to_sid_return_cleanup;

		rc = sidtab_context_to_sid(&sidtab, &ctx_new, sid);
		if (rc != 0)
			goto netlbl_secattr_to_sid_return_cleanup;

		security_netlbl_cache_add(secattr, *sid);

		ebitmap_destroy(&ctx_new.range.level[0].cat);
	} else {
		*sid = SECSID_NULL;
		rc = 0;
	}

netlbl_secattr_to_sid_return:
	POLICY_RDUNLOCK;
	return rc;
netlbl_secattr_to_sid_return_cleanup:
	ebitmap_destroy(&ctx_new.range.level[0].cat);
	goto netlbl_secattr_to_sid_return;
}

/**
 * security_netlbl_sid_to_secattr - Convert a SELinux SID to a NetLabel secattr
 * @sid: the SELinux SID
 * @secattr: the NetLabel packet security attributes
 *
 * Description:
 * Convert the given SELinux SID in @sid into a NetLabel security attribute.
 * Returns zero on success, negative values on failure.
 *
 */
int security_netlbl_sid_to_secattr(u32 sid, struct netlbl_lsm_secattr *secattr)
{
	int rc = -ENOENT;
	struct context *ctx;

	if (!ss_initialized)
		return 0;

	POLICY_RDLOCK;
	ctx = sidtab_search(&sidtab, sid);
	if (ctx == NULL)
		goto netlbl_sid_to_secattr_failure;
	secattr->domain = kstrdup(policydb.p_type_val_to_name[ctx->type - 1],
				  GFP_ATOMIC);
	secattr->flags |= NETLBL_SECATTR_DOMAIN_CPY;
	mls_export_netlbl_lvl(ctx, secattr);
	rc = mls_export_netlbl_cat(ctx, secattr);
	if (rc != 0)
		goto netlbl_sid_to_secattr_failure;
	POLICY_RDUNLOCK;

	return 0;

netlbl_sid_to_secattr_failure:
	POLICY_RDUNLOCK;
	return rc;
}
#endif /* CONFIG_NETLABEL */
