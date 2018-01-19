/*
 * AppArmor security module
 *
 * This file contains AppArmor policy attachment and domain transitions
 *
 * Copyright (C) 2002-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/errno.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/syscalls.h>
#include <linux/tracehook.h>
#include <linux/personality.h>

#include "include/audit.h"
#include "include/apparmorfs.h"
#include "include/context.h"
#include "include/domain.h"
#include "include/file.h"
#include "include/ipc.h"
#include "include/match.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/policy_ns.h"

/**
 * aa_free_domain_entries - free entries in a domain table
 * @domain: the domain table to free  (MAYBE NULL)
 */
void aa_free_domain_entries(struct aa_domain *domain)
{
	int i;
	if (domain) {
		if (!domain->table)
			return;

		for (i = 0; i < domain->size; i++)
			kzfree(domain->table[i]);
		kzfree(domain->table);
		domain->table = NULL;
	}
}

/**
 * may_change_ptraced_domain - check if can change profile on ptraced task
 * @to_label: profile to change to  (NOT NULL)
 * @info: message if there is an error
 *
 * Check if current is ptraced and if so if the tracing task is allowed
 * to trace the new domain
 *
 * Returns: %0 or error if change not allowed
 */
static int may_change_ptraced_domain(struct aa_label *to_label,
				     const char **info)
{
	struct task_struct *tracer;
	struct aa_label *tracerl = NULL;
	int error = 0;

	rcu_read_lock();
	tracer = ptrace_parent(current);
	if (tracer)
		/* released below */
		tracerl = aa_get_task_label(tracer);

	/* not ptraced */
	if (!tracer || unconfined(tracerl))
		goto out;

	error = aa_may_ptrace(tracerl, to_label, PTRACE_MODE_ATTACH);

out:
	rcu_read_unlock();
	aa_put_label(tracerl);

	if (error)
		*info = "ptrace prevents transition";
	return error;
}

/**** TODO: dedup to aa_label_match - needs perm and dfa, merging
 * specifically this is an exact copy of aa_label_match except
 * aa_compute_perms is replaced with aa_compute_fperms
 * and policy.dfa with file.dfa
 ****/
/* match a profile and its associated ns component if needed
 * Assumes visibility test has already been done.
 * If a subns profile is not to be matched should be prescreened with
 * visibility test.
 */
static inline unsigned int match_component(struct aa_profile *profile,
					   struct aa_profile *tp,
					   bool stack, unsigned int state)
{
	const char *ns_name;

	if (stack)
		state = aa_dfa_match(profile->file.dfa, state, "&");
	if (profile->ns == tp->ns)
		return aa_dfa_match(profile->file.dfa, state, tp->base.hname);

	/* try matching with namespace name and then profile */
	ns_name = aa_ns_name(profile->ns, tp->ns, true);
	state = aa_dfa_match_len(profile->file.dfa, state, ":", 1);
	state = aa_dfa_match(profile->file.dfa, state, ns_name);
	state = aa_dfa_match_len(profile->file.dfa, state, ":", 1);
	return aa_dfa_match(profile->file.dfa, state, tp->base.hname);
}

/**
 * label_compound_match - find perms for full compound label
 * @profile: profile to find perms for
 * @label: label to check access permissions for
 * @stack: whether this is a stacking request
 * @start: state to start match in
 * @subns: whether to do permission checks on components in a subns
 * @request: permissions to request
 * @perms: perms struct to set
 *
 * Returns: 0 on success else ERROR
 *
 * For the label A//&B//&C this does the perm match for A//&B//&C
 * @perms should be preinitialized with allperms OR a previous permission
 *        check to be stacked.
 */
static int label_compound_match(struct aa_profile *profile,
				struct aa_label *label, bool stack,
				unsigned int state, bool subns, u32 request,
				struct aa_perms *perms)
{
	struct aa_profile *tp;
	struct label_it i;
	struct path_cond cond = { };

	/* find first subcomponent that is visible */
	label_for_each(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = match_component(profile, tp, stack, state);
		if (!state)
			goto fail;
		goto next;
	}

	/* no component visible */
	*perms = allperms;
	return 0;

next:
	label_for_each_cont(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = aa_dfa_match(profile->file.dfa, state, "//&");
		state = match_component(profile, tp, false, state);
		if (!state)
			goto fail;
	}
	*perms = aa_compute_fperms(profile->file.dfa, state, &cond);
	aa_apply_modes_to_perms(profile, perms);
	if ((perms->allow & request) != request)
		return -EACCES;

	return 0;

fail:
	*perms = nullperms;
	return -EACCES;
}

/**
 * label_components_match - find perms for all subcomponents of a label
 * @profile: profile to find perms for
 * @label: label to check access permissions for
 * @stack: whether this is a stacking request
 * @start: state to start match in
 * @subns: whether to do permission checks on components in a subns
 * @request: permissions to request
 * @perms: an initialized perms struct to add accumulation to
 *
 * Returns: 0 on success else ERROR
 *
 * For the label A//&B//&C this does the perm match for each of A and B and C
 * @perms should be preinitialized with allperms OR a previous permission
 *        check to be stacked.
 */
static int label_components_match(struct aa_profile *profile,
				  struct aa_label *label, bool stack,
				  unsigned int start, bool subns, u32 request,
				  struct aa_perms *perms)
{
	struct aa_profile *tp;
	struct label_it i;
	struct aa_perms tmp;
	struct path_cond cond = { };
	unsigned int state = 0;

	/* find first subcomponent to test */
	label_for_each(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = match_component(profile, tp, stack, start);
		if (!state)
			goto fail;
		goto next;
	}

	/* no subcomponents visible - no change in perms */
	return 0;

next:
	tmp = aa_compute_fperms(profile->file.dfa, state, &cond);
	aa_apply_modes_to_perms(profile, &tmp);
	aa_perms_accum(perms, &tmp);
	label_for_each_cont(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = match_component(profile, tp, stack, start);
		if (!state)
			goto fail;
		tmp = aa_compute_fperms(profile->file.dfa, state, &cond);
		aa_apply_modes_to_perms(profile, &tmp);
		aa_perms_accum(perms, &tmp);
	}

	if ((perms->allow & request) != request)
		return -EACCES;

	return 0;

fail:
	*perms = nullperms;
	return -EACCES;
}

/**
 * label_match - do a multi-component label match
 * @profile: profile to match against (NOT NULL)
 * @label: label to match (NOT NULL)
 * @stack: whether this is a stacking request
 * @state: state to start in
 * @subns: whether to match subns components
 * @request: permission request
 * @perms: Returns computed perms (NOT NULL)
 *
 * Returns: the state the match finished in, may be the none matching state
 */
static int label_match(struct aa_profile *profile, struct aa_label *label,
		       bool stack, unsigned int state, bool subns, u32 request,
		       struct aa_perms *perms)
{
	int error;

	*perms = nullperms;
	error = label_compound_match(profile, label, stack, state, subns,
				     request, perms);
	if (!error)
		return error;

	*perms = allperms;
	return label_components_match(profile, label, stack, state, subns,
				      request, perms);
}

/******* end TODO: dedup *****/

/**
 * change_profile_perms - find permissions for change_profile
 * @profile: the current profile  (NOT NULL)
 * @target: label to transition to (NOT NULL)
 * @stack: whether this is a stacking request
 * @request: requested perms
 * @start: state to start matching in
 *
 *
 * Returns: permission set
 *
 * currently only matches full label A//&B//&C or individual components A, B, C
 * not arbitrary combinations. Eg. A//&B, C
 */
static int change_profile_perms(struct aa_profile *profile,
				struct aa_label *target, bool stack,
				u32 request, unsigned int start,
				struct aa_perms *perms)
{
	if (profile_unconfined(profile)) {
		perms->allow = AA_MAY_CHANGE_PROFILE | AA_MAY_ONEXEC;
		perms->audit = perms->quiet = perms->kill = 0;
		return 0;
	}

	/* TODO: add profile in ns screening */
	return label_match(profile, target, stack, start, true, request, perms);
}

/**
 * __attach_match_ - find an attachment match
 * @name - to match against  (NOT NULL)
 * @head - profile list to walk  (NOT NULL)
 * @info - info message if there was an error (NOT NULL)
 *
 * Do a linear search on the profiles in the list.  There is a matching
 * preference where an exact match is preferred over a name which uses
 * expressions to match, and matching expressions with the greatest
 * xmatch_len are preferred.
 *
 * Requires: @head not be shared or have appropriate locks held
 *
 * Returns: profile or NULL if no match found
 */
static struct aa_profile *__attach_match(const char *name,
					 struct list_head *head,
					 const char **info)
{
	int len = 0;
	bool conflict = false;
	struct aa_profile *profile, *candidate = NULL;

	list_for_each_entry_rcu(profile, head, base.list) {
		if (profile->label.flags & FLAG_NULL &&
		    &profile->label == ns_unconfined(profile->ns))
			continue;

		if (profile->xmatch) {
			if (profile->xmatch_len >= len) {
				unsigned int state;
				u32 perm;

				state = aa_dfa_match(profile->xmatch,
						     DFA_START, name);
				perm = dfa_user_allow(profile->xmatch, state);
				/* any accepting state means a valid match. */
				if (perm & MAY_EXEC) {
					if (profile->xmatch_len == len) {
						conflict = true;
						continue;
					}
					candidate = profile;
					len = profile->xmatch_len;
					conflict = false;
				}
			}
		} else if (!strcmp(profile->base.name, name))
			/* exact non-re match, no more searching required */
			return profile;
	}

	if (conflict) {
		*info = "conflicting profile attachments";
		return NULL;
	}

	return candidate;
}

/**
 * find_attach - do attachment search for unconfined processes
 * @ns: the current namespace  (NOT NULL)
 * @list: list to search  (NOT NULL)
 * @name: the executable name to match against  (NOT NULL)
 * @info: info message if there was an error
 *
 * Returns: label or NULL if no match found
 */
static struct aa_label *find_attach(struct aa_ns *ns, struct list_head *list,
				    const char *name, const char **info)
{
	struct aa_profile *profile;

	rcu_read_lock();
	profile = aa_get_profile(__attach_match(name, list, info));
	rcu_read_unlock();

	return profile ? &profile->label : NULL;
}

static const char *next_name(int xtype, const char *name)
{
	return NULL;
}

/**
 * x_table_lookup - lookup an x transition name via transition table
 * @profile: current profile (NOT NULL)
 * @xindex: index into x transition table
 * @name: returns: name tested to find label (NOT NULL)
 *
 * Returns: refcounted label, or NULL on failure (MAYBE NULL)
 */
struct aa_label *x_table_lookup(struct aa_profile *profile, u32 xindex,
				const char **name)
{
	struct aa_label *label = NULL;
	u32 xtype = xindex & AA_X_TYPE_MASK;
	int index = xindex & AA_X_INDEX_MASK;

	AA_BUG(!name);

	/* index is guaranteed to be in range, validated at load time */
	/* TODO: move lookup parsing to unpack time so this is a straight
	 *       index into the resultant label
	 */
	for (*name = profile->file.trans.table[index]; !label && *name;
	     *name = next_name(xtype, *name)) {
		if (xindex & AA_X_CHILD) {
			struct aa_profile *new_profile;
			/* release by caller */
			new_profile = aa_find_child(profile, *name);
			if (new_profile)
				label = &new_profile->label;
			continue;
		}
		label = aa_label_parse(&profile->label, *name, GFP_ATOMIC,
				       true, false);
		if (IS_ERR(label))
			label = NULL;
	}

	/* released by caller */

	return label;
}

/**
 * x_to_label - get target label for a given xindex
 * @profile: current profile  (NOT NULL)
 * @name: name to lookup (NOT NULL)
 * @xindex: index into x transition table
 * @lookupname: returns: name used in lookup if one was specified (NOT NULL)
 *
 * find label for a transition index
 *
 * Returns: refcounted label or NULL if not found available
 */
static struct aa_label *x_to_label(struct aa_profile *profile,
				   const char *name, u32 xindex,
				   const char **lookupname,
				   const char **info)
{
	struct aa_label *new = NULL;
	struct aa_ns *ns = profile->ns;
	u32 xtype = xindex & AA_X_TYPE_MASK;
	const char *stack = NULL;

	switch (xtype) {
	case AA_X_NONE:
		/* fail exec unless ix || ux fallback - handled by caller */
		*lookupname = NULL;
		break;
	case AA_X_TABLE:
		/* TODO: fix when perm mapping done at unload */
		stack = profile->file.trans.table[xindex & AA_X_INDEX_MASK];
		if (*stack != '&') {
			/* released by caller */
			new = x_table_lookup(profile, xindex, lookupname);
			stack = NULL;
			break;
		}
		/* fall through to X_NAME */
	case AA_X_NAME:
		if (xindex & AA_X_CHILD)
			/* released by caller */
			new = find_attach(ns, &profile->base.profiles,
					  name, info);
		else
			/* released by caller */
			new = find_attach(ns, &ns->base.profiles,
					  name, info);
		*lookupname = name;
		break;
	}

	if (!new) {
		if (xindex & AA_X_INHERIT) {
			/* (p|c|n)ix - don't change profile but do
			 * use the newest version
			 */
			*info = "ix fallback";
			/* no profile && no error */
			new = aa_get_newest_label(&profile->label);
		} else if (xindex & AA_X_UNCONFINED) {
			new = aa_get_newest_label(ns_unconfined(profile->ns));
			*info = "ux fallback";
		}
	}

	if (new && stack) {
		/* base the stack on post domain transition */
		struct aa_label *base = new;

		new = aa_label_parse(base, stack, GFP_ATOMIC, true, false);
		if (IS_ERR(new))
			new = NULL;
		aa_put_label(base);
	}

	/* released by caller */
	return new;
}

static struct aa_label *profile_transition(struct aa_profile *profile,
					   const struct linux_binprm *bprm,
					   char *buffer, struct path_cond *cond,
					   bool *secure_exec)
{
	struct aa_label *new = NULL;
	const char *info = NULL, *name = NULL, *target = NULL;
	unsigned int state = profile->file.start;
	struct aa_perms perms = {};
	bool nonewprivs = false;
	int error = 0;

	AA_BUG(!profile);
	AA_BUG(!bprm);
	AA_BUG(!buffer);

	error = aa_path_name(&bprm->file->f_path, profile->path_flags, buffer,
			     &name, &info, profile->disconnected);
	if (error) {
		if (profile_unconfined(profile) ||
		    (profile->label.flags & FLAG_IX_ON_NAME_ERROR)) {
			AA_DEBUG("name lookup ix on error");
			error = 0;
			new = aa_get_newest_label(&profile->label);
		}
		name = bprm->filename;
		goto audit;
	}

	if (profile_unconfined(profile)) {
		new = find_attach(profile->ns, &profile->ns->base.profiles,
				  name, &info);
		if (new) {
			AA_DEBUG("unconfined attached to new label");
			return new;
		}
		AA_DEBUG("unconfined exec no attachment");
		return aa_get_newest_label(&profile->label);
	}

	/* find exec permissions for name */
	state = aa_str_perms(profile->file.dfa, state, name, cond, &perms);
	if (perms.allow & MAY_EXEC) {
		/* exec permission determine how to transition */
		new = x_to_label(profile, name, perms.xindex, &target, &info);
		if (new && new->proxy == profile->label.proxy && info) {
			/* hack ix fallback - improve how this is detected */
			goto audit;
		} else if (!new) {
			error = -EACCES;
			info = "profile transition not found";
			/* remove MAY_EXEC to audit as failure */
			perms.allow &= ~MAY_EXEC;
		}
	} else if (COMPLAIN_MODE(profile)) {
		/* no exec permission - learning mode */
		struct aa_profile *new_profile = NULL;
		char *n = kstrdup(name, GFP_ATOMIC);

		if (n) {
			/* name is ptr into buffer */
			long pos = name - buffer;
			/* break per cpu buffer hold */
			put_buffers(buffer);
			new_profile = aa_new_null_profile(profile, false, n,
							  GFP_KERNEL);
			get_buffers(buffer);
			name = buffer + pos;
			strcpy((char *)name, n);
			kfree(n);
		}
		if (!new_profile) {
			error = -ENOMEM;
			info = "could not create null profile";
		} else {
			error = -EACCES;
			new = &new_profile->label;
		}
		perms.xindex |= AA_X_UNSAFE;
	} else
		/* fail exec */
		error = -EACCES;

	if (!new)
		goto audit;

	/* Policy has specified a domain transitions. if no_new_privs and
	 * confined and not transitioning to the current domain fail.
	 *
	 * NOTE: Domain transitions from unconfined and to stritly stacked
	 * subsets are allowed even when no_new_privs is set because this
	 * aways results in a further reduction of permissions.
	 */
	if ((bprm->unsafe & LSM_UNSAFE_NO_NEW_PRIVS) &&
	    !profile_unconfined(profile) &&
	    !aa_label_is_subset(new, &profile->label)) {
		error = -EPERM;
		info = "no new privs";
		nonewprivs = true;
		perms.allow &= ~MAY_EXEC;
		goto audit;
	}

	if (!(perms.xindex & AA_X_UNSAFE)) {
		if (DEBUG_ON) {
			dbg_printk("apparmor: scrubbing environment variables"
				   " for %s profile=", name);
			aa_label_printk(new, GFP_ATOMIC);
			dbg_printk("\n");
		}
		*secure_exec = true;
	}

audit:
	aa_audit_file(profile, &perms, OP_EXEC, MAY_EXEC, name, target, new,
		      cond->uid, info, error);
	if (!new || nonewprivs) {
		aa_put_label(new);
		return ERR_PTR(error);
	}

	return new;
}

static int profile_onexec(struct aa_profile *profile, struct aa_label *onexec,
			  bool stack, const struct linux_binprm *bprm,
			  char *buffer, struct path_cond *cond,
			  bool *secure_exec)
{
	unsigned int state = profile->file.start;
	struct aa_perms perms = {};
	const char *xname = NULL, *info = "change_profile onexec";
	int error = -EACCES;

	AA_BUG(!profile);
	AA_BUG(!onexec);
	AA_BUG(!bprm);
	AA_BUG(!buffer);

	if (profile_unconfined(profile)) {
		/* change_profile on exec already granted */
		/*
		 * NOTE: Domain transitions from unconfined are allowed
		 * even when no_new_privs is set because this aways results
		 * in a further reduction of permissions.
		 */
		return 0;
	}

	error = aa_path_name(&bprm->file->f_path, profile->path_flags, buffer,
			     &xname, &info, profile->disconnected);
	if (error) {
		if (profile_unconfined(profile) ||
		    (profile->label.flags & FLAG_IX_ON_NAME_ERROR)) {
			AA_DEBUG("name lookup ix on error");
			error = 0;
		}
		xname = bprm->filename;
		goto audit;
	}

	/* find exec permissions for name */
	state = aa_str_perms(profile->file.dfa, state, xname, cond, &perms);
	if (!(perms.allow & AA_MAY_ONEXEC)) {
		info = "no change_onexec valid for executable";
		goto audit;
	}
	/* test if this exec can be paired with change_profile onexec.
	 * onexec permission is linked to exec with a standard pairing
	 * exec\0change_profile
	 */
	state = aa_dfa_null_transition(profile->file.dfa, state);
	error = change_profile_perms(profile, onexec, stack, AA_MAY_ONEXEC,
				     state, &perms);
	if (error) {
		perms.allow &= ~AA_MAY_ONEXEC;
		goto audit;
	}
	/* Policy has specified a domain transitions. if no_new_privs and
	 * confined and not transitioning to the current domain fail.
	 *
	 * NOTE: Domain transitions from unconfined and to stritly stacked
	 * subsets are allowed even when no_new_privs is set because this
	 * aways results in a further reduction of permissions.
	 */
	if ((bprm->unsafe & LSM_UNSAFE_NO_NEW_PRIVS) &&
	    !profile_unconfined(profile) &&
	    !aa_label_is_subset(onexec, &profile->label)) {
		error = -EPERM;
		info = "no new privs";
		perms.allow &= ~AA_MAY_ONEXEC;
		goto audit;
	}

	if (!(perms.xindex & AA_X_UNSAFE)) {
		if (DEBUG_ON) {
			dbg_printk("apparmor: scrubbing environment "
				   "variables for %s label=", xname);
			aa_label_printk(onexec, GFP_ATOMIC);
			dbg_printk("\n");
		}
		*secure_exec = true;
	}

audit:
	return aa_audit_file(profile, &perms, OP_EXEC, AA_MAY_ONEXEC, xname,
			     NULL, onexec, cond->uid, info, error);
}

/* ensure none ns domain transitions are correctly applied with onexec */

static struct aa_label *handle_onexec(struct aa_label *label,
				      struct aa_label *onexec, bool stack,
				      const struct linux_binprm *bprm,
				      char *buffer, struct path_cond *cond,
				      bool *unsafe)
{
	struct aa_profile *profile;
	struct aa_label *new;
	int error;

	AA_BUG(!label);
	AA_BUG(!onexec);
	AA_BUG(!bprm);
	AA_BUG(!buffer);

	if (!stack) {
		error = fn_for_each_in_ns(label, profile,
				profile_onexec(profile, onexec, stack,
					       bprm, buffer, cond, unsafe));
		if (error)
			return ERR_PTR(error);
		new = fn_label_build_in_ns(label, profile, GFP_ATOMIC,
				aa_get_newest_label(onexec),
				profile_transition(profile, bprm, buffer,
						   cond, unsafe));

	} else {
		/* TODO: determine how much we want to losen this */
		error = fn_for_each_in_ns(label, profile,
				profile_onexec(profile, onexec, stack, bprm,
					       buffer, cond, unsafe));
		if (error)
			return ERR_PTR(error);
		new = fn_label_build_in_ns(label, profile, GFP_ATOMIC,
				aa_label_merge(&profile->label, onexec,
					       GFP_ATOMIC),
				profile_transition(profile, bprm, buffer,
						   cond, unsafe));
	}

	if (new)
		return new;

	/* TODO: get rid of GLOBAL_ROOT_UID */
	error = fn_for_each_in_ns(label, profile,
			aa_audit_file(profile, &nullperms, OP_CHANGE_ONEXEC,
				      AA_MAY_ONEXEC, bprm->filename, NULL,
				      onexec, GLOBAL_ROOT_UID,
				      "failed to build target label", -ENOMEM));
	return ERR_PTR(error);
}

/**
 * apparmor_bprm_set_creds - set the new creds on the bprm struct
 * @bprm: binprm for the exec  (NOT NULL)
 *
 * Returns: %0 or error on failure
 *
 * TODO: once the other paths are done see if we can't refactor into a fn
 */
int apparmor_bprm_set_creds(struct linux_binprm *bprm)
{
	struct aa_task_ctx *ctx;
	struct aa_label *label, *new = NULL;
	struct aa_profile *profile;
	char *buffer = NULL;
	const char *info = NULL;
	int error = 0;
	bool unsafe = false;
	struct path_cond cond = {
		file_inode(bprm->file)->i_uid,
		file_inode(bprm->file)->i_mode
	};

	if (bprm->called_set_creds)
		return 0;

	ctx = cred_ctx(bprm->cred);
	AA_BUG(!ctx);

	label = aa_get_newest_label(ctx->label);

	/* buffer freed below, name is pointer into buffer */
	get_buffers(buffer);
	/* Test for onexec first as onexec override other x transitions. */
	if (ctx->onexec)
		new = handle_onexec(label, ctx->onexec, ctx->token,
				    bprm, buffer, &cond, &unsafe);
	else
		new = fn_label_build(label, profile, GFP_ATOMIC,
				profile_transition(profile, bprm, buffer,
						   &cond, &unsafe));

	AA_BUG(!new);
	if (IS_ERR(new)) {
		error = PTR_ERR(new);
		goto done;
	} else if (!new) {
		error = -ENOMEM;
		goto done;
	}

	/* TODO: Add ns level no_new_privs subset test */

	if (bprm->unsafe & LSM_UNSAFE_SHARE) {
		/* FIXME: currently don't mediate shared state */
		;
	}

	if (bprm->unsafe & (LSM_UNSAFE_PTRACE)) {
		/* TODO: test needs to be profile of label to new */
		error = may_change_ptraced_domain(new, &info);
		if (error)
			goto audit;
	}

	if (unsafe) {
		if (DEBUG_ON) {
			dbg_printk("scrubbing environment variables for %s "
				   "label=", bprm->filename);
			aa_label_printk(new, GFP_ATOMIC);
			dbg_printk("\n");
		}
		bprm->secureexec = 1;
	}

	if (label->proxy != new->proxy) {
		/* when transitioning clear unsafe personality bits */
		if (DEBUG_ON) {
			dbg_printk("apparmor: clearing unsafe personality "
				   "bits. %s label=", bprm->filename);
			aa_label_printk(new, GFP_ATOMIC);
			dbg_printk("\n");
		}
		bprm->per_clear |= PER_CLEAR_ON_SETID;
	}
	aa_put_label(ctx->label);
	/* transfer reference, released when ctx is freed */
	ctx->label = new;

done:
	/* clear out temporary/transitional state from the context */
	aa_clear_task_ctx_trans(ctx);

	aa_put_label(label);
	put_buffers(buffer);

	return error;

audit:
	error = fn_for_each(label, profile,
			aa_audit_file(profile, &nullperms, OP_EXEC, MAY_EXEC,
				      bprm->filename, NULL, new,
				      file_inode(bprm->file)->i_uid, info,
				      error));
	aa_put_label(new);
	goto done;
}

/*
 * Functions for self directed profile change
 */


/* helper fn for change_hat
 *
 * Returns: label for hat transition OR ERR_PTR.  Does NOT return NULL
 */
static struct aa_label *build_change_hat(struct aa_profile *profile,
					 const char *name, bool sibling)
{
	struct aa_profile *root, *hat = NULL;
	const char *info = NULL;
	int error = 0;

	if (sibling && PROFILE_IS_HAT(profile)) {
		root = aa_get_profile_rcu(&profile->parent);
	} else if (!sibling && !PROFILE_IS_HAT(profile)) {
		root = aa_get_profile(profile);
	} else {
		info = "conflicting target types";
		error = -EPERM;
		goto audit;
	}

	hat = aa_find_child(root, name);
	if (!hat) {
		error = -ENOENT;
		if (COMPLAIN_MODE(profile)) {
			hat = aa_new_null_profile(profile, true, name,
						  GFP_KERNEL);
			if (!hat) {
				info = "failed null profile create";
				error = -ENOMEM;
			}
		}
	}
	aa_put_profile(root);

audit:
	aa_audit_file(profile, &nullperms, OP_CHANGE_HAT, AA_MAY_CHANGEHAT,
		      name, hat ? hat->base.hname : NULL,
		      hat ? &hat->label : NULL, GLOBAL_ROOT_UID, NULL,
		      error);
	if (!hat || (error && error != -ENOENT))
		return ERR_PTR(error);
	/* if hat && error - complain mode, already audited and we adjust for
	 * complain mode allow by returning hat->label
	 */
	return &hat->label;
}

/* helper fn for changing into a hat
 *
 * Returns: label for hat transition or ERR_PTR. Does not return NULL
 */
static struct aa_label *change_hat(struct aa_label *label, const char *hats[],
				   int count, int flags)
{
	struct aa_profile *profile, *root, *hat = NULL;
	struct aa_label *new;
	struct label_it it;
	bool sibling = false;
	const char *name, *info = NULL;
	int i, error;

	AA_BUG(!label);
	AA_BUG(!hats);
	AA_BUG(count < 1);

	if (PROFILE_IS_HAT(labels_profile(label)))
		sibling = true;

	/*find first matching hat */
	for (i = 0; i < count && !hat; i++) {
		name = hats[i];
		label_for_each_in_ns(it, labels_ns(label), label, profile) {
			if (sibling && PROFILE_IS_HAT(profile)) {
				root = aa_get_profile_rcu(&profile->parent);
			} else if (!sibling && !PROFILE_IS_HAT(profile)) {
				root = aa_get_profile(profile);
			} else {	/* conflicting change type */
				info = "conflicting targets types";
				error = -EPERM;
				goto fail;
			}
			hat = aa_find_child(root, name);
			aa_put_profile(root);
			if (!hat) {
				if (!COMPLAIN_MODE(profile))
					goto outer_continue;
				/* complain mode succeed as if hat */
			} else if (!PROFILE_IS_HAT(hat)) {
				info = "target not hat";
				error = -EPERM;
				aa_put_profile(hat);
				goto fail;
			}
			aa_put_profile(hat);
		}
		/* found a hat for all profiles in ns */
		goto build;
outer_continue:
	;
	}
	/* no hats that match, find appropriate error
	 *
	 * In complain mode audit of the failure is based off of the first
	 * hat supplied.  This is done due how userspace interacts with
	 * change_hat.
	 */
	name = NULL;
	label_for_each_in_ns(it, labels_ns(label), label, profile) {
		if (!list_empty(&profile->base.profiles)) {
			info = "hat not found";
			error = -ENOENT;
			goto fail;
		}
	}
	info = "no hats defined";
	error = -ECHILD;

fail:
	label_for_each_in_ns(it, labels_ns(label), label, profile) {
		/*
		 * no target as it has failed to be found or built
		 *
		 * change_hat uses probing and should not log failures
		 * related to missing hats
		 */
		/* TODO: get rid of GLOBAL_ROOT_UID */
		if (count > 1 || COMPLAIN_MODE(profile)) {
			aa_audit_file(profile, &nullperms, OP_CHANGE_HAT,
				      AA_MAY_CHANGEHAT, name, NULL, NULL,
				      GLOBAL_ROOT_UID, info, error);
		}
	}
	return ERR_PTR(error);

build:
	new = fn_label_build_in_ns(label, profile, GFP_KERNEL,
				   build_change_hat(profile, name, sibling),
				   aa_get_label(&profile->label));
	if (!new) {
		info = "label build failed";
		error = -ENOMEM;
		goto fail;
	} /* else if (IS_ERR) build_change_hat has logged error so return new */

	return new;
}

/**
 * aa_change_hat - change hat to/from subprofile
 * @hats: vector of hat names to try changing into (MAYBE NULL if @count == 0)
 * @count: number of hat names in @hats
 * @token: magic value to validate the hat change
 * @flags: flags affecting behavior of the change
 *
 * Returns %0 on success, error otherwise.
 *
 * Change to the first profile specified in @hats that exists, and store
 * the @hat_magic in the current task context.  If the count == 0 and the
 * @token matches that stored in the current task context, return to the
 * top level profile.
 *
 * change_hat only applies to profiles in the current ns, and each profile
 * in the ns must make the same transition otherwise change_hat will fail.
 */
int aa_change_hat(const char *hats[], int count, u64 token, int flags)
{
	const struct cred *cred;
	struct aa_task_ctx *ctx;
	struct aa_label *label, *previous, *new = NULL, *target = NULL;
	struct aa_profile *profile;
	struct aa_perms perms = {};
	const char *info = NULL;
	int error = 0;

	/*
	 * Fail explicitly requested domain transitions if no_new_privs.
	 * There is no exception for unconfined as change_hat is not
	 * available.
	 */
	if (task_no_new_privs(current)) {
		/* not an apparmor denial per se, so don't log it */
		AA_DEBUG("no_new_privs - change_hat denied");
		return -EPERM;
	}

	/* released below */
	cred = get_current_cred();
	ctx = cred_ctx(cred);
	label = aa_get_newest_cred_label(cred);
	previous = aa_get_newest_label(ctx->previous);

	if (unconfined(label)) {
		info = "unconfined can not change_hat";
		error = -EPERM;
		goto fail;
	}

	if (count) {
		new = change_hat(label, hats, count, flags);
		AA_BUG(!new);
		if (IS_ERR(new)) {
			error = PTR_ERR(new);
			new = NULL;
			/* already audited */
			goto out;
		}

		error = may_change_ptraced_domain(new, &info);
		if (error)
			goto fail;

		if (flags & AA_CHANGE_TEST)
			goto out;

		target = new;
		error = aa_set_current_hat(new, token);
		if (error == -EACCES)
			/* kill task in case of brute force attacks */
			goto kill;
	} else if (previous && !(flags & AA_CHANGE_TEST)) {
		/* Return to saved label.  Kill task if restore fails
		 * to avoid brute force attacks
		 */
		target = previous;
		error = aa_restore_previous_label(token);
		if (error) {
			if (error == -EACCES)
				goto kill;
			goto fail;
		}
	} /* else ignore @flags && restores when there is no saved profile */

out:
	aa_put_label(new);
	aa_put_label(previous);
	aa_put_label(label);
	put_cred(cred);

	return error;

kill:
	info = "failed token match";
	perms.kill = AA_MAY_CHANGEHAT;

fail:
	fn_for_each_in_ns(label, profile,
		aa_audit_file(profile, &perms, OP_CHANGE_HAT,
			      AA_MAY_CHANGEHAT, NULL, NULL, target,
			      GLOBAL_ROOT_UID, info, error));

	goto out;
}


static int change_profile_perms_wrapper(const char *op, const char *name,
					struct aa_profile *profile,
					struct aa_label *target, bool stack,
					u32 request, struct aa_perms *perms)
{
	const char *info = NULL;
	int error = 0;

	/*
	 * Fail explicitly requested domain transitions when no_new_privs
	 * and not unconfined OR the transition results in a stack on
	 * the current label.
	 * Stacking domain transitions and transitions from unconfined are
	 * allowed even when no_new_privs is set because this aways results
	 * in a reduction of permissions.
	 */
	if (task_no_new_privs(current) && !stack &&
	    !profile_unconfined(profile) &&
	    !aa_label_is_subset(target, &profile->label)) {
		info = "no new privs";
		error = -EPERM;
	}

	if (!error)
		error = change_profile_perms(profile, target, stack, request,
					     profile->file.start, perms);
	if (error)
		error = aa_audit_file(profile, perms, op, request, name,
				      NULL, target, GLOBAL_ROOT_UID, info,
				      error);

	return error;
}

/**
 * aa_change_profile - perform a one-way profile transition
 * @fqname: name of profile may include namespace (NOT NULL)
 * @onexec: whether this transition is to take place immediately or at exec
 * @flags: flags affecting change behavior
 *
 * Change to new profile @name.  Unlike with hats, there is no way
 * to change back.  If @name isn't specified the current profile name is
 * used.
 * If @onexec then the transition is delayed until
 * the next exec.
 *
 * Returns %0 on success, error otherwise.
 */
int aa_change_profile(const char *fqname, int flags)
{
	struct aa_label *label, *new = NULL, *target = NULL;
	struct aa_profile *profile;
	struct aa_perms perms = {};
	const char *info = NULL;
	const char *auditname = fqname;		/* retain leading & if stack */
	bool stack = flags & AA_CHANGE_STACK;
	int error = 0;
	char *op;
	u32 request;

	if (!fqname || !*fqname) {
		AA_DEBUG("no profile name");
		return -EINVAL;
	}

	if (flags & AA_CHANGE_ONEXEC) {
		request = AA_MAY_ONEXEC;
		if (stack)
			op = OP_STACK_ONEXEC;
		else
			op = OP_CHANGE_ONEXEC;
	} else {
		request = AA_MAY_CHANGE_PROFILE;
		if (stack)
			op = OP_STACK;
		else
			op = OP_CHANGE_PROFILE;
	}

	label = aa_get_current_label();

	if (*fqname == '&') {
		stack = true;
		/* don't have label_parse() do stacking */
		fqname++;
	}
	target = aa_label_parse(label, fqname, GFP_KERNEL, true, false);
	if (IS_ERR(target)) {
		struct aa_profile *tprofile;

		info = "label not found";
		error = PTR_ERR(target);
		target = NULL;
		/*
		 * TODO: fixme using labels_profile is not right - do profile
		 * per complain profile
		 */
		if ((flags & AA_CHANGE_TEST) ||
		    !COMPLAIN_MODE(labels_profile(label)))
			goto audit;
		/* released below */
		tprofile = aa_new_null_profile(labels_profile(label), false,
					       fqname, GFP_KERNEL);
		if (!tprofile) {
			info = "failed null profile create";
			error = -ENOMEM;
			goto audit;
		}
		target = &tprofile->label;
		goto check;
	}

	/*
	 * self directed transitions only apply to current policy ns
	 * TODO: currently requiring perms for stacking and straight change
	 *       stacking doesn't strictly need this. Determine how much
	 *       we want to loosen this restriction for stacking
	 *
	 * if (!stack) {
	 */
	error = fn_for_each_in_ns(label, profile,
			change_profile_perms_wrapper(op, auditname,
						     profile, target, stack,
						     request, &perms));
	if (error)
		/* auditing done in change_profile_perms_wrapper */
		goto out;

	/* } */

check:
	/* check if tracing task is allowed to trace target domain */
	error = may_change_ptraced_domain(target, &info);
	if (error && !fn_for_each_in_ns(label, profile,
					COMPLAIN_MODE(profile)))
		goto audit;

	/* TODO: add permission check to allow this
	 * if ((flags & AA_CHANGE_ONEXEC) && !current_is_single_threaded()) {
	 *      info = "not a single threaded task";
	 *      error = -EACCES;
	 *      goto audit;
	 * }
	 */
	if (flags & AA_CHANGE_TEST)
		goto out;

	if (!(flags & AA_CHANGE_ONEXEC)) {
		/* only transition profiles in the current ns */
		if (stack)
			new = aa_label_merge(label, target, GFP_KERNEL);
		else
			new = fn_label_build_in_ns(label, profile, GFP_KERNEL,
					aa_get_label(target),
					aa_get_label(&profile->label));
		if (IS_ERR_OR_NULL(new)) {
			info = "failed to build target label";
			error = PTR_ERR(new);
			new = NULL;
			perms.allow = 0;
			goto audit;
		}
		error = aa_replace_current_label(new);
	} else
		/* full transition will be built in exec path */
		error = aa_set_current_onexec(target, stack);

audit:
	error = fn_for_each_in_ns(label, profile,
			aa_audit_file(profile, &perms, op, request, auditname,
				      NULL, new ? new : target,
				      GLOBAL_ROOT_UID, info, error));

out:
	aa_put_label(new);
	aa_put_label(target);
	aa_put_label(label);

	return error;
}
