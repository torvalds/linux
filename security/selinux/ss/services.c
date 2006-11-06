/*
 * Implementation of the security services.
 *
 * Authors : Stephen Smalley, <sds@epoch.ncsc.mil>
 *           James Morris <jmorris@redhat.com>
 *
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 *	Support for enhanced MLS infrastructure.
 *	Support for context based audit filters.
 *
 * Updated: Frank Mayer <mayerf@tresys.com> and Karl MacMillan <kmacmillan@tresys.com>
 *
 * 	Added conditional policy language extensions
 *
 * Updated: Hewlett-Packard <paul.moore@hp.com>
 *
 *      Added support for NetLabel
 *
 * Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2004-2006 Trusted Computer Solutions, Inc.
 * Copyright (C) 2003 - 2004 Tresys Technology, LLC
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *	This program is free software; you can redistribute it and/or modify
 *  	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/audit.h>
#include <linux/mutex.h>
#include <net/sock.h>
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
#include "selinux_netlabel.h"

extern void selnl_notify_policyload(u32 seqno);
unsigned int policydb_loaded_version;

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
int ss_initialized = 0;

/*
 * The largest sequence number that has been used when
 * providing an access decision to the access vector cache.
 * The sequence number only changes when a policy change
 * occurs.
 */
static u32 latest_granting = 0;

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
					s[++sp] = ( !ebitmap_get_bit(&r1->dominates,
								     val2 - 1) &&
						    !ebitmap_get_bit(&r2->dominates,
								     val1 - 1) );
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

	if (!tclass || tclass > policydb.p_classes.nprim) {
		printk(KERN_ERR "security_compute_av:  unrecognized class %d\n",
		       tclass);
		return -EINVAL;
	}
	tclass_datum = policydb.class_val_to_struct[tclass - 1];

	/*
	 * Initialize the access vectors to the default values.
	 */
	avd->allowed = 0;
	avd->decided = 0xffffffff;
	avd->auditallow = 0;
	avd->auditdeny = 0xffffffff;
	avd->seqno = latest_granting;

	/*
	 * If a specific type enforcement rule was defined for
	 * this permission check, then use it.
	 */
	avkey.target_class = tclass;
	avkey.specified = AVTAB_AV;
	sattr = &policydb.type_attr_map[scontext->type - 1];
	tattr = &policydb.type_attr_map[tcontext->type - 1];
	ebitmap_for_each_bit(sattr, snode, i) {
		if (!ebitmap_node_get_bit(snode, i))
			continue;
		ebitmap_for_each_bit(tattr, tnode, j) {
			if (!ebitmap_node_get_bit(tnode, j))
				continue;
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
		printk(KERN_ERR "security_validate_transition:  "
		       "unrecognized class %d\n", tclass);
		rc = -EINVAL;
		goto out;
	}
	tclass_datum = policydb.class_val_to_struct[tclass - 1];

	ocontext = sidtab_search(&sidtab, oldsid);
	if (!ocontext) {
		printk(KERN_ERR "security_validate_transition: "
		       " unrecognized SID %d\n", oldsid);
		rc = -EINVAL;
		goto out;
	}

	ncontext = sidtab_search(&sidtab, newsid);
	if (!ncontext) {
		printk(KERN_ERR "security_validate_transition: "
		       " unrecognized SID %d\n", newsid);
		rc = -EINVAL;
		goto out;
	}

	tcontext = sidtab_search(&sidtab, tasksid);
	if (!tcontext) {
		printk(KERN_ERR "security_validate_transition: "
		       " unrecognized SID %d\n", tasksid);
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
		printk(KERN_ERR "security_compute_av:  unrecognized SID %d\n",
		       ssid);
		rc = -EINVAL;
		goto out;
	}
	tcontext = sidtab_search(&sidtab, tsid);
	if (!tcontext) {
		printk(KERN_ERR "security_compute_av:  unrecognized SID %d\n",
		       tsid);
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
	if (!scontextp) {
		return -ENOMEM;
	}
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

	if (!ss_initialized) {
		if (sid <= SECINITSID_NUM) {
			char *scontextp;

			*scontext_len = strlen(initial_sid_to_string[sid]) + 1;
			scontextp = kmalloc(*scontext_len,GFP_ATOMIC);
			if (!scontextp) {
				rc = -ENOMEM;
				goto out;
			}
			strcpy(scontextp, initial_sid_to_string[sid]);
			*scontext = scontextp;
			goto out;
		}
		printk(KERN_ERR "security_sid_to_context:  called before initial "
		       "load_policy on unknown SID %d\n", sid);
		rc = -EINVAL;
		goto out;
	}
	POLICY_RDLOCK;
	context = sidtab_search(&sidtab, sid);
	if (!context) {
		printk(KERN_ERR "security_sid_to_context:  unrecognized SID "
		       "%d\n", sid);
		rc = -EINVAL;
		goto out_unlock;
	}
	rc = context_struct_to_string(context, scontext, scontext_len);
out_unlock:
	POLICY_RDUNLOCK;
out:
	return rc;

}

static int security_context_to_sid_core(char *scontext, u32 scontext_len, u32 *sid, u32 def_sid)
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
	scontext2 = kmalloc(scontext_len+1,GFP_KERNEL);
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
int security_context_to_sid(char *scontext, u32 scontext_len, u32 *sid)
{
	return security_context_to_sid_core(scontext, scontext_len,
	                                    sid, SECSID_NULL);
}

/**
 * security_context_to_sid_default - Obtain a SID for a given security context,
 * falling back to specified default if needed.
 *
 * @scontext: security context
 * @scontext_len: length in bytes
 * @sid: security identifier, SID
 * @def_sid: default SID to assign on errror
 *
 * Obtains a SID associated with the security context that
 * has the string representation specified by @scontext.
 * The default SID is passed to the MLS layer to be used to allow
 * kernel labeling of the MLS field if the MLS field is not present
 * (for upgrading to MLS without full relabel).
 * Returns -%EINVAL if the context is invalid, -%ENOMEM if insufficient
 * memory is available, or 0 on success.
 */
int security_context_to_sid_default(char *scontext, u32 scontext_len, u32 *sid, u32 def_sid)
{
	return security_context_to_sid_core(scontext, scontext_len,
	                                    sid, def_sid);
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
		printk(KERN_ERR "security_compute_sid:  unrecognized SID %d\n",
		       ssid);
		rc = -EINVAL;
		goto out_unlock;
	}
	tcontext = sidtab_search(&sidtab, tsid);
	if (!tcontext) {
		printk(KERN_ERR "security_compute_sid:  unrecognized SID %d\n",
		       tsid);
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
	if(!avdatum) {
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
		printk(KERN_ERR "security:  context %s is invalid\n", s);
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
	if (!usrdatum) {
		goto bad;
	}
	c->user = usrdatum->value;

	/* Convert the role. */
	role = hashtab_search(args->newp->p_roles.table,
	                      args->oldp->p_role_val_to_name[c->role - 1]);
	if (!role) {
		goto bad;
	}
	c->role = role->value;

	/* Convert the type. */
	typdatum = hashtab_search(args->newp->p_types.table,
	                          args->oldp->p_type_val_to_name[c->type - 1]);
	if (!typdatum) {
		goto bad;
	}
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
	printk(KERN_ERR "security:  invalidating context %s\n", s);
	kfree(s);
	goto out;
}

extern void selinux_complete_init(void);

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
		policydb_loaded_version = policydb.policyvers;
		ss_initialized = 1;
		seqno = ++latest_granting;
		LOAD_UNLOCK;
		selinux_complete_init();
		avc_ss_reset(seqno);
		selnl_notify_policyload(seqno);
		selinux_netlbl_cache_invalidate();
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

	return 0;

err:
	LOAD_UNLOCK;
	sidtab_destroy(&newsidtab);
	policydb_destroy(&newpolicydb);
	return rc;

}

/**
 * security_port_sid - Obtain the SID for a port.
 * @domain: communication domain aka address family
 * @type: socket type
 * @protocol: protocol number
 * @port: port number
 * @out_sid: security identifier
 */
int security_port_sid(u16 domain,
		      u16 type,
		      u8 protocol,
		      u16 port,
		      u32 *out_sid)
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
 * @msg_sid: default SID for received packets
 */
int security_netif_sid(char *name,
		       u32 *if_sid,
		       u32 *msg_sid)
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
		*msg_sid = c->sid[1];
	} else {
		*if_sid = SECINITSID_NETIF;
		*msg_sid = SECINITSID_NETMSG;
	}

out:
	POLICY_RDUNLOCK;
	return rc;
}

static int match_ipv6_addrmask(u32 *input, u32 *addr, u32 *mask)
{
	int i, fail = 0;

	for(i = 0; i < 4; i++)
		if(addr[i] != (input[i] & mask[i])) {
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
	u32 *mysids, *mysids2, sid;
	u32 mynel = 0, maxnel = SIDS_NEL;
	struct user_datum *user;
	struct role_datum *role;
	struct av_decision avd;
	struct ebitmap_node *rnode, *tnode;
	int rc = 0, i, j;

	if (!ss_initialized) {
		*sids = NULL;
		*nel = 0;
		goto out;
	}

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

	ebitmap_for_each_bit(&user->roles, rnode, i) {
		if (!ebitmap_node_get_bit(rnode, i))
			continue;
		role = policydb.role_val_to_struct[i];
		usercon.role = i+1;
		ebitmap_for_each_bit(&role->types, tnode, j) {
			if (!ebitmap_node_get_bit(tnode, j))
				continue;
			usercon.type = j+1;

			if (mls_setup_user_range(fromcon, user, &usercon))
				continue;

			rc = context_struct_compute_av(fromcon, &usercon,
						       SECCLASS_PROCESS,
						       PROCESS__TRANSITION,
						       &avd);
			if (rc ||  !(avd.allowed & PROCESS__TRANSITION))
				continue;
			rc = sidtab_context_to_sid(&sidtab, &usercon, &sid);
			if (rc) {
				kfree(mysids);
				goto out_unlock;
			}
			if (mynel < maxnel) {
				mysids[mynel++] = sid;
			} else {
				maxnel += SIDS_NEL;
				mysids2 = kcalloc(maxnel, sizeof(*mysids2), GFP_ATOMIC);
				if (!mysids2) {
					rc = -ENOMEM;
					kfree(mysids);
					goto out_unlock;
				}
				memcpy(mysids2, mysids, mynel * sizeof(*mysids2));
				kfree(mysids);
				mysids = mysids2;
				mysids[mynel++] = sid;
			}
		}
	}

	*sids = mysids;
	*nel = mynel;

out_unlock:
	POLICY_RDUNLOCK;
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

       *names = kcalloc(*len, sizeof(char*), GFP_ATOMIC);
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
				"bool=%s val=%d old_val=%d auid=%u",
				policydb.p_bool_val_to_name[i],
				!!values[i],
				policydb.bool_val_to_struct[i]->state,
				audit_get_loginuid(current->audit_context));
		}
		if (values[i]) {
			policydb.bool_val_to_struct[i]->state = 1;
		} else {
			policydb.bool_val_to_struct[i]->state = 0;
		}
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
		printk(KERN_ERR "security_sid_mls_copy:  unrecognized SID "
		       "%d\n", sid);
		rc = -EINVAL;
		goto out_unlock;
	}

	context2 = sidtab_search(&sidtab, mls_sid);
	if (!context2) {
		printk(KERN_ERR "security_sid_mls_copy:  unrecognized SID "
		       "%d\n", mls_sid);
		rc = -EINVAL;
		goto out_unlock;
	}

	newcon.user = context1->user;
	newcon.role = context1->role;
	newcon.type = context1->type;
	rc = mls_copy_context(&newcon, context2);
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

struct selinux_audit_rule {
	u32 au_seqno;
	struct context au_ctxt;
};

void selinux_audit_rule_free(struct selinux_audit_rule *rule)
{
	if (rule) {
		context_destroy(&rule->au_ctxt);
		kfree(rule);
	}
}

int selinux_audit_rule_init(u32 field, u32 op, char *rulestr,
                            struct selinux_audit_rule **rule)
{
	struct selinux_audit_rule *tmprule;
	struct role_datum *roledatum;
	struct type_datum *typedatum;
	struct user_datum *userdatum;
	int rc = 0;

	*rule = NULL;

	if (!ss_initialized)
		return -ENOTSUPP;

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

int selinux_audit_rule_match(u32 sid, u32 field, u32 op,
                             struct selinux_audit_rule *rule,
                             struct audit_context *actx)
{
	struct context *ctxt;
	struct mls_level *level;
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

static int (*aurule_callback)(void) = NULL;

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

void selinux_audit_set_callback(int (*callback)(void))
{
	aurule_callback = callback;
}

#ifdef CONFIG_NETLABEL
/*
 * This is the structure we store inside the NetLabel cache block.
 */
#define NETLBL_CACHE(x)           ((struct netlbl_cache *)(x))
#define NETLBL_CACHE_T_NONE       0
#define NETLBL_CACHE_T_SID        1
#define NETLBL_CACHE_T_MLS        2
struct netlbl_cache {
	u32 type;
	union {
		u32 sid;
		struct mls_range mls_label;
	} data;
};

/**
 * selinux_netlbl_cache_free - Free the NetLabel cached data
 * @data: the data to free
 *
 * Description:
 * This function is intended to be used as the free() callback inside the
 * netlbl_lsm_cache structure.
 *
 */
static void selinux_netlbl_cache_free(const void *data)
{
	struct netlbl_cache *cache;

	if (data == NULL)
		return;

	cache = NETLBL_CACHE(data);
	switch (cache->type) {
	case NETLBL_CACHE_T_MLS:
		ebitmap_destroy(&cache->data.mls_label.level[0].cat);
		break;
	}
	kfree(data);
}

/**
 * selinux_netlbl_cache_add - Add an entry to the NetLabel cache
 * @skb: the packet
 * @ctx: the SELinux context
 *
 * Description:
 * Attempt to cache the context in @ctx, which was derived from the packet in
 * @skb, in the NetLabel subsystem cache.
 *
 */
static void selinux_netlbl_cache_add(struct sk_buff *skb, struct context *ctx)
{
	struct netlbl_cache *cache = NULL;
	struct netlbl_lsm_secattr secattr;

	netlbl_secattr_init(&secattr);
	secattr.cache = netlbl_secattr_cache_alloc(GFP_ATOMIC);
	if (secattr.cache == NULL)
		goto netlbl_cache_add_return;

	cache = kzalloc(sizeof(*cache),	GFP_ATOMIC);
	if (cache == NULL)
		goto netlbl_cache_add_return;
	secattr.cache->free = selinux_netlbl_cache_free;
	secattr.cache->data = (void *)cache;

	cache->type = NETLBL_CACHE_T_MLS;
	if (ebitmap_cpy(&cache->data.mls_label.level[0].cat,
			&ctx->range.level[0].cat) != 0)
		goto netlbl_cache_add_return;
	cache->data.mls_label.level[1].cat.highbit =
		cache->data.mls_label.level[0].cat.highbit;
	cache->data.mls_label.level[1].cat.node =
		cache->data.mls_label.level[0].cat.node;
	cache->data.mls_label.level[0].sens = ctx->range.level[0].sens;
	cache->data.mls_label.level[1].sens = ctx->range.level[0].sens;

	netlbl_cache_add(skb, &secattr);

netlbl_cache_add_return:
	netlbl_secattr_destroy(&secattr);
}

/**
 * selinux_netlbl_cache_invalidate - Invalidate the NetLabel cache
 *
 * Description:
 * Invalidate the NetLabel security attribute mapping cache.
 *
 */
void selinux_netlbl_cache_invalidate(void)
{
	netlbl_cache_invalidate();
}

/**
 * selinux_netlbl_secattr_to_sid - Convert a NetLabel secattr to a SELinux SID
 * @skb: the network packet
 * @secattr: the NetLabel packet security attributes
 * @base_sid: the SELinux SID to use as a context for MLS only attributes
 * @sid: the SELinux SID
 *
 * Description:
 * Convert the given NetLabel packet security attributes in @secattr into a
 * SELinux SID.  If the @secattr field does not contain a full SELinux
 * SID/context then use the context in @base_sid as the foundation.  If @skb
 * is not NULL attempt to cache as much data as possibile.  Returns zero on
 * success, negative values on failure.
 *
 */
static int selinux_netlbl_secattr_to_sid(struct sk_buff *skb,
					 struct netlbl_lsm_secattr *secattr,
					 u32 base_sid,
					 u32 *sid)
{
	int rc = -EIDRM;
	struct context *ctx;
	struct context ctx_new;
	struct netlbl_cache *cache;

	POLICY_RDLOCK;

	if (secattr->cache) {
		cache = NETLBL_CACHE(secattr->cache->data);
		switch (cache->type) {
		case NETLBL_CACHE_T_SID:
			*sid = cache->data.sid;
			rc = 0;
			break;
		case NETLBL_CACHE_T_MLS:
			ctx = sidtab_search(&sidtab, base_sid);
			if (ctx == NULL)
				goto netlbl_secattr_to_sid_return;

			ctx_new.user = ctx->user;
			ctx_new.role = ctx->role;
			ctx_new.type = ctx->type;
			ctx_new.range.level[0].sens =
				cache->data.mls_label.level[0].sens;
			ctx_new.range.level[0].cat.highbit =
				cache->data.mls_label.level[0].cat.highbit;
			ctx_new.range.level[0].cat.node =
				cache->data.mls_label.level[0].cat.node;
			ctx_new.range.level[1].sens =
				cache->data.mls_label.level[1].sens;
			ctx_new.range.level[1].cat.highbit =
				cache->data.mls_label.level[1].cat.highbit;
			ctx_new.range.level[1].cat.node =
				cache->data.mls_label.level[1].cat.node;

			rc = sidtab_context_to_sid(&sidtab, &ctx_new, sid);
			break;
		default:
			goto netlbl_secattr_to_sid_return;
		}
	} else if (secattr->mls_lvl_vld) {
		ctx = sidtab_search(&sidtab, base_sid);
		if (ctx == NULL)
			goto netlbl_secattr_to_sid_return;

		ctx_new.user = ctx->user;
		ctx_new.role = ctx->role;
		ctx_new.type = ctx->type;
		mls_import_lvl(&ctx_new, secattr->mls_lvl, secattr->mls_lvl);
		if (secattr->mls_cat) {
			if (mls_import_cat(&ctx_new,
					   secattr->mls_cat,
					   secattr->mls_cat_len,
					   NULL,
					   0) != 0)
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

		if (skb != NULL)
			selinux_netlbl_cache_add(skb, &ctx_new);
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
 * selinux_netlbl_skbuff_getsid - Get the sid of a packet using NetLabel
 * @skb: the packet
 * @base_sid: the SELinux SID to use as a context for MLS only attributes
 * @sid: the SID
 *
 * Description:
 * Call the NetLabel mechanism to get the security attributes of the given
 * packet and use those attributes to determine the correct context/SID to
 * assign to the packet.  Returns zero on success, negative values on failure.
 *
 */
static int selinux_netlbl_skbuff_getsid(struct sk_buff *skb,
					u32 base_sid,
					u32 *sid)
{
	int rc;
	struct netlbl_lsm_secattr secattr;

	netlbl_secattr_init(&secattr);
	rc = netlbl_skbuff_getattr(skb, &secattr);
	if (rc == 0)
		rc = selinux_netlbl_secattr_to_sid(skb,
						   &secattr,
						   base_sid,
						   sid);
	netlbl_secattr_destroy(&secattr);

	return rc;
}

/**
 * selinux_netlbl_socket_setsid - Label a socket using the NetLabel mechanism
 * @sock: the socket to label
 * @sid: the SID to use
 *
 * Description:
 * Attempt to label a socket using the NetLabel mechanism using the given
 * SID.  Returns zero values on success, negative values on failure.
 *
 */
static int selinux_netlbl_socket_setsid(struct socket *sock, u32 sid)
{
	int rc = -ENOENT;
	struct sk_security_struct *sksec = sock->sk->sk_security;
	struct netlbl_lsm_secattr secattr;
	struct context *ctx;

	if (!ss_initialized)
		return 0;

	netlbl_secattr_init(&secattr);

	POLICY_RDLOCK;

	ctx = sidtab_search(&sidtab, sid);
	if (ctx == NULL)
		goto netlbl_socket_setsid_return;

	secattr.domain = kstrdup(policydb.p_type_val_to_name[ctx->type - 1],
				 GFP_ATOMIC);
	mls_export_lvl(ctx, &secattr.mls_lvl, NULL);
	secattr.mls_lvl_vld = 1;
	rc = mls_export_cat(ctx,
			    &secattr.mls_cat,
			    &secattr.mls_cat_len,
			    NULL,
			    NULL);
	if (rc != 0)
		goto netlbl_socket_setsid_return;

	rc = netlbl_socket_setattr(sock, &secattr);
	if (rc == 0)
		sksec->nlbl_state = NLBL_LABELED;

netlbl_socket_setsid_return:
	POLICY_RDUNLOCK;
	netlbl_secattr_destroy(&secattr);
	return rc;
}

/**
 * selinux_netlbl_sk_security_init - Setup the NetLabel fields
 * @ssec: the sk_security_struct
 * @family: the socket family
 *
 * Description:
 * Called when a new sk_security_struct is allocated to initialize the NetLabel
 * fields.
 *
 */
void selinux_netlbl_sk_security_init(struct sk_security_struct *ssec,
				     int family)
{
        if (family == PF_INET)
		ssec->nlbl_state = NLBL_REQUIRE;
	else
		ssec->nlbl_state = NLBL_UNSET;
}

/**
 * selinux_netlbl_sk_clone_security - Copy the NetLabel fields
 * @ssec: the original sk_security_struct
 * @newssec: the cloned sk_security_struct
 *
 * Description:
 * Clone the NetLabel specific sk_security_struct fields from @ssec to
 * @newssec.
 *
 */
void selinux_netlbl_sk_clone_security(struct sk_security_struct *ssec,
				      struct sk_security_struct *newssec)
{
	newssec->sclass = ssec->sclass;
	if (ssec->nlbl_state != NLBL_UNSET)
		newssec->nlbl_state = NLBL_REQUIRE;
	else
		newssec->nlbl_state = NLBL_UNSET;
}

/**
 * selinux_netlbl_socket_post_create - Label a socket using NetLabel
 * @sock: the socket to label
 * @sock_family: the socket family
 * @sid: the SID to use
 *
 * Description:
 * Attempt to label a socket using the NetLabel mechanism using the given
 * SID.  Returns zero values on success, negative values on failure.
 *
 */
int selinux_netlbl_socket_post_create(struct socket *sock,
				      int sock_family,
				      u32 sid)
{
	struct inode_security_struct *isec = SOCK_INODE(sock)->i_security;
	struct sk_security_struct *sksec = sock->sk->sk_security;

	sksec->sclass = isec->sclass;

	if (sock_family != PF_INET)
		return 0;

	sksec->nlbl_state = NLBL_REQUIRE;
	return selinux_netlbl_socket_setsid(sock, sid);
}

/**
 * selinux_netlbl_sock_graft - Netlabel the new socket
 * @sk: the new connection
 * @sock: the new socket
 *
 * Description:
 * The connection represented by @sk is being grafted onto @sock so set the
 * socket's NetLabel to match the SID of @sk.
 *
 */
void selinux_netlbl_sock_graft(struct sock *sk, struct socket *sock)
{
	struct inode_security_struct *isec = SOCK_INODE(sock)->i_security;
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr secattr;
	u32 nlbl_peer_sid;

	sksec->sclass = isec->sclass;

	if (sk->sk_family != PF_INET)
		return;

	netlbl_secattr_init(&secattr);
	if (netlbl_sock_getattr(sk, &secattr) == 0 &&
	    selinux_netlbl_secattr_to_sid(NULL,
					  &secattr,
					  SECINITSID_UNLABELED,
					  &nlbl_peer_sid) == 0)
		sksec->peer_sid = nlbl_peer_sid;
	netlbl_secattr_destroy(&secattr);

	sksec->nlbl_state = NLBL_REQUIRE;

	/* Try to set the NetLabel on the socket to save time later, if we fail
	 * here we will pick up the pieces in later calls to
	 * selinux_netlbl_inode_permission(). */
	selinux_netlbl_socket_setsid(sock, sksec->sid);
}

/**
 * selinux_netlbl_inet_conn_request - Handle a new connection request
 * @skb: the packet
 * @sock_sid: the SID of the parent socket
 *
 * Description:
 * If present, use the security attributes of the packet in @skb and the
 * parent sock's SID to arrive at a SID for the new child sock.  Returns the
 * SID of the connection or SECSID_NULL on failure.
 *
 */
u32 selinux_netlbl_inet_conn_request(struct sk_buff *skb, u32 sock_sid)
{
	int rc;
	u32 peer_sid;

	rc = selinux_netlbl_skbuff_getsid(skb, sock_sid, &peer_sid);
	if (rc != 0)
		return SECSID_NULL;

	return peer_sid;
}

/**
 * selinux_netlbl_inode_permission - Verify the socket is NetLabel labeled
 * @inode: the file descriptor's inode
 * @mask: the permission mask
 *
 * Description:
 * Looks at a file's inode and if it is marked as a socket protected by
 * NetLabel then verify that the socket has been labeled, if not try to label
 * the socket now with the inode's SID.  Returns zero on success, negative
 * values on failure.
 *
 */
int selinux_netlbl_inode_permission(struct inode *inode, int mask)
{
	int rc;
	struct inode_security_struct *isec;
	struct sk_security_struct *sksec;
	struct socket *sock;

	if (!S_ISSOCK(inode->i_mode))
		return 0;

	sock = SOCKET_I(inode);
	isec = inode->i_security;
	sksec = sock->sk->sk_security;
	mutex_lock(&isec->lock);
	if (unlikely(sksec->nlbl_state == NLBL_REQUIRE &&
		     (mask & (MAY_WRITE | MAY_APPEND)))) {
		lock_sock(sock->sk);
		rc = selinux_netlbl_socket_setsid(sock, sksec->sid);
		release_sock(sock->sk);
	} else
		rc = 0;
	mutex_unlock(&isec->lock);

	return rc;
}

/**
 * selinux_netlbl_sock_rcv_skb - Do an inbound access check using NetLabel
 * @sksec: the sock's sk_security_struct
 * @skb: the packet
 * @ad: the audit data
 *
 * Description:
 * Fetch the NetLabel security attributes from @skb and perform an access check
 * against the receiving socket.  Returns zero on success, negative values on
 * error.
 *
 */
int selinux_netlbl_sock_rcv_skb(struct sk_security_struct *sksec,
				struct sk_buff *skb,
				struct avc_audit_data *ad)
{
	int rc;
	u32 netlbl_sid;
	u32 recv_perm;

	rc = selinux_netlbl_skbuff_getsid(skb,
					  SECINITSID_UNLABELED,
					  &netlbl_sid);
	if (rc != 0)
		return rc;

	if (netlbl_sid == SECSID_NULL)
		return 0;

	switch (sksec->sclass) {
	case SECCLASS_UDP_SOCKET:
		recv_perm = UDP_SOCKET__RECVFROM;
		break;
	case SECCLASS_TCP_SOCKET:
		recv_perm = TCP_SOCKET__RECVFROM;
		break;
	default:
		recv_perm = RAWIP_SOCKET__RECVFROM;
	}

	rc = avc_has_perm(sksec->sid,
			  netlbl_sid,
			  sksec->sclass,
			  recv_perm,
			  ad);
	if (rc == 0)
		return 0;

	netlbl_skbuff_err(skb, rc);
	return rc;
}

/**
 * selinux_netlbl_socket_getpeersec_stream - Return the connected peer's SID
 * @sock: the socket
 *
 * Description:
 * Examine @sock to find the connected peer's SID.  Returns the SID on success
 * or SECSID_NULL on error.
 *
 */
u32 selinux_netlbl_socket_getpeersec_stream(struct socket *sock)
{
	struct sk_security_struct *sksec = sock->sk->sk_security;
	return sksec->peer_sid;
}

/**
 * selinux_netlbl_socket_getpeersec_dgram - Return the SID of a NetLabel packet
 * @skb: the packet
 *
 * Description:
 * Examine @skb to find the SID assigned to it by NetLabel.  Returns the SID on
 * success, SECSID_NULL on error.
 *
 */
u32 selinux_netlbl_socket_getpeersec_dgram(struct sk_buff *skb)
{
	int peer_sid;

	if (selinux_netlbl_skbuff_getsid(skb,
					 SECINITSID_UNLABELED,
					 &peer_sid) != 0)
		return SECSID_NULL;

	return peer_sid;
}

/**
 * selinux_netlbl_socket_setsockopt - Do not allow users to remove a NetLabel
 * @sock: the socket
 * @level: the socket level or protocol
 * @optname: the socket option name
 *
 * Description:
 * Check the setsockopt() call and if the user is trying to replace the IP
 * options on a socket and a NetLabel is in place for the socket deny the
 * access; otherwise allow the access.  Returns zero when the access is
 * allowed, -EACCES when denied, and other negative values on error.
 *
 */
int selinux_netlbl_socket_setsockopt(struct socket *sock,
				     int level,
				     int optname)
{
	int rc = 0;
	struct inode *inode = SOCK_INODE(sock);
	struct sk_security_struct *sksec = sock->sk->sk_security;
	struct inode_security_struct *isec = inode->i_security;
	struct netlbl_lsm_secattr secattr;

	mutex_lock(&isec->lock);
	if (level == IPPROTO_IP && optname == IP_OPTIONS &&
	    sksec->nlbl_state == NLBL_LABELED) {
		netlbl_secattr_init(&secattr);
		rc = netlbl_socket_getattr(sock, &secattr);
		if (rc == 0 && (secattr.cache || secattr.mls_lvl_vld))
			rc = -EACCES;
		netlbl_secattr_destroy(&secattr);
	}
	mutex_unlock(&isec->lock);

	return rc;
}
#endif /* CONFIG_NETLABEL */
