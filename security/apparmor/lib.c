// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains basic common functions used in AppArmor
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include "include/audit.h"
#include "include/apparmor.h"
#include "include/lib.h"
#include "include/perms.h"
#include "include/policy.h"

struct aa_perms nullperms;
struct aa_perms allperms = { .allow = ALL_PERMS_MASK,
			     .quiet = ALL_PERMS_MASK,
			     .hide = ALL_PERMS_MASK };

/**
 * aa_free_str_table - free entries str table
 * @str: the string table to free  (MAYBE NULL)
 */
void aa_free_str_table(struct aa_str_table *t)
{
	int i;

	if (t) {
		if (!t->table)
			return;

		for (i = 0; i < t->size; i++)
			kfree_sensitive(t->table[i]);
		kfree_sensitive(t->table);
		t->table = NULL;
	}
}

/**
 * aa_split_fqname - split a fqname into a profile and namespace name
 * @fqname: a full qualified name in namespace profile format (NOT NULL)
 * @ns_name: pointer to portion of the string containing the ns name (NOT NULL)
 *
 * Returns: profile name or NULL if one is not specified
 *
 * Split a namespace name from a profile name (see policy.c for naming
 * description).  If a portion of the name is missing it returns NULL for
 * that portion.
 *
 * NOTE: may modify the @fqname string.  The pointers returned point
 *       into the @fqname string.
 */
char *aa_split_fqname(char *fqname, char **ns_name)
{
	char *name = strim(fqname);

	*ns_name = NULL;
	if (name[0] == ':') {
		char *split = strchr(&name[1], ':');
		*ns_name = skip_spaces(&name[1]);
		if (split) {
			/* overwrite ':' with \0 */
			*split++ = 0;
			if (strncmp(split, "//", 2) == 0)
				split += 2;
			name = skip_spaces(split);
		} else
			/* a ns name without a following profile is allowed */
			name = NULL;
	}
	if (name && *name == 0)
		name = NULL;

	return name;
}

/**
 * skipn_spaces - Removes leading whitespace from @str.
 * @str: The string to be stripped.
 *
 * Returns a pointer to the first non-whitespace character in @str.
 * if all whitespace will return NULL
 */

const char *skipn_spaces(const char *str, size_t n)
{
	for (; n && isspace(*str); --n)
		++str;
	if (n)
		return (char *)str;
	return NULL;
}

const char *aa_splitn_fqname(const char *fqname, size_t n, const char **ns_name,
			     size_t *ns_len)
{
	const char *end = fqname + n;
	const char *name = skipn_spaces(fqname, n);

	*ns_name = NULL;
	*ns_len = 0;

	if (!name)
		return NULL;

	if (name[0] == ':') {
		char *split = strnchr(&name[1], end - &name[1], ':');
		*ns_name = skipn_spaces(&name[1], end - &name[1]);
		if (!*ns_name)
			return NULL;
		if (split) {
			*ns_len = split - *ns_name;
			if (*ns_len == 0)
				*ns_name = NULL;
			split++;
			if (end - split > 1 && strncmp(split, "//", 2) == 0)
				split += 2;
			name = skipn_spaces(split, end - split);
		} else {
			/* a ns name without a following profile is allowed */
			name = NULL;
			*ns_len = end - *ns_name;
		}
	}
	if (name && *name == 0)
		name = NULL;

	return name;
}

/**
 * aa_info_message - log a none profile related status message
 * @str: message to log
 */
void aa_info_message(const char *str)
{
	if (audit_enabled) {
		DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, AA_CLASS_NONE, NULL);

		aad(&sa)->info = str;
		aa_audit_msg(AUDIT_APPARMOR_STATUS, &sa, NULL);
	}
	printk(KERN_INFO "AppArmor: %s\n", str);
}

__counted char *aa_str_alloc(int size, gfp_t gfp)
{
	struct counted_str *str;

	str = kmalloc(struct_size(str, name, size), gfp);
	if (!str)
		return NULL;

	kref_init(&str->count);
	return str->name;
}

void aa_str_kref(struct kref *kref)
{
	kfree(container_of(kref, struct counted_str, count));
}


const char aa_file_perm_chrs[] = "xwracd         km l     ";
const char *aa_file_perm_names[] = {
	"exec",
	"write",
	"read",
	"append",

	"create",
	"delete",
	"open",
	"rename",

	"setattr",
	"getattr",
	"setcred",
	"getcred",

	"chmod",
	"chown",
	"chgrp",
	"lock",

	"mmap",
	"mprot",
	"link",
	"snapshot",

	"unknown",
	"unknown",
	"unknown",
	"unknown",

	"unknown",
	"unknown",
	"unknown",
	"unknown",

	"stack",
	"change_onexec",
	"change_profile",
	"change_hat",
};

/**
 * aa_perm_mask_to_str - convert a perm mask to its short string
 * @str: character buffer to store string in (at least 10 characters)
 * @str_size: size of the @str buffer
 * @chrs: NUL-terminated character buffer of permission characters
 * @mask: permission mask to convert
 */
void aa_perm_mask_to_str(char *str, size_t str_size, const char *chrs, u32 mask)
{
	unsigned int i, perm = 1;
	size_t num_chrs = strlen(chrs);

	for (i = 0; i < num_chrs; perm <<= 1, i++) {
		if (mask & perm) {
			/* Ensure that one byte is left for NUL-termination */
			if (WARN_ON_ONCE(str_size <= 1))
				break;

			*str++ = chrs[i];
			str_size--;
		}
	}
	*str = '\0';
}

void aa_audit_perm_names(struct audit_buffer *ab, const char * const *names,
			 u32 mask)
{
	const char *fmt = "%s";
	unsigned int i, perm = 1;
	bool prev = false;

	for (i = 0; i < 32; perm <<= 1, i++) {
		if (mask & perm) {
			audit_log_format(ab, fmt, names[i]);
			if (!prev) {
				prev = true;
				fmt = " %s";
			}
		}
	}
}

void aa_audit_perm_mask(struct audit_buffer *ab, u32 mask, const char *chrs,
			u32 chrsmask, const char * const *names, u32 namesmask)
{
	char str[33];

	audit_log_format(ab, "\"");
	if ((mask & chrsmask) && chrs) {
		aa_perm_mask_to_str(str, sizeof(str), chrs, mask & chrsmask);
		mask &= ~chrsmask;
		audit_log_format(ab, "%s", str);
		if (mask & namesmask)
			audit_log_format(ab, " ");
	}
	if ((mask & namesmask) && names)
		aa_audit_perm_names(ab, names, mask & namesmask);
	audit_log_format(ab, "\"");
}

/**
 * aa_audit_perms_cb - generic callback fn for auditing perms
 * @ab: audit buffer (NOT NULL)
 * @va: audit struct to audit values of (NOT NULL)
 */
static void aa_audit_perms_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;

	if (aad(sa)->request) {
		audit_log_format(ab, " requested_mask=");
		aa_audit_perm_mask(ab, aad(sa)->request, aa_file_perm_chrs,
				   PERMS_CHRS_MASK, aa_file_perm_names,
				   PERMS_NAMES_MASK);
	}
	if (aad(sa)->denied) {
		audit_log_format(ab, "denied_mask=");
		aa_audit_perm_mask(ab, aad(sa)->denied, aa_file_perm_chrs,
				   PERMS_CHRS_MASK, aa_file_perm_names,
				   PERMS_NAMES_MASK);
	}
	audit_log_format(ab, " peer=");
	aa_label_xaudit(ab, labels_ns(aad(sa)->label), aad(sa)->peer,
				      FLAGS_NONE, GFP_ATOMIC);
}

/**
 * aa_apply_modes_to_perms - apply namespace and profile flags to perms
 * @profile: that perms where computed from
 * @perms: perms to apply mode modifiers to
 *
 * TODO: split into profile and ns based flags for when accumulating perms
 */
void aa_apply_modes_to_perms(struct aa_profile *profile, struct aa_perms *perms)
{
	switch (AUDIT_MODE(profile)) {
	case AUDIT_ALL:
		perms->audit = ALL_PERMS_MASK;
		fallthrough;
	case AUDIT_NOQUIET:
		perms->quiet = 0;
		break;
	case AUDIT_QUIET:
		perms->audit = 0;
		fallthrough;
	case AUDIT_QUIET_DENIED:
		perms->quiet = ALL_PERMS_MASK;
		break;
	}

	if (KILL_MODE(profile))
		perms->kill = ALL_PERMS_MASK;
	else if (COMPLAIN_MODE(profile))
		perms->complain = ALL_PERMS_MASK;
	else if (USER_MODE(profile))
		perms->prompt = ALL_PERMS_MASK;
}

void aa_profile_match_label(struct aa_profile *profile,
			    struct aa_ruleset *rules,
			    struct aa_label *label,
			    int type, u32 request, struct aa_perms *perms)
{
	/* TODO: doesn't yet handle extended types */
	aa_state_t state;

	state = aa_dfa_next(rules->policy.dfa,
			    rules->policy.start[AA_CLASS_LABEL],
			    type);
	aa_label_match(profile, rules, label, state, false, request, perms);
}


/* currently unused */
int aa_profile_label_perm(struct aa_profile *profile, struct aa_profile *target,
			  u32 request, int type, u32 *deny,
			  struct common_audit_data *sa)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	struct aa_perms perms;

	aad(sa)->label = &profile->label;
	aad(sa)->peer = &target->label;
	aad(sa)->request = request;

	aa_profile_match_label(profile, rules, &target->label, type, request,
			       &perms);
	aa_apply_modes_to_perms(profile, &perms);
	*deny |= request & perms.deny;
	return aa_check_perms(profile, &perms, request, sa, aa_audit_perms_cb);
}

/**
 * aa_check_perms - do audit mode selection based on perms set
 * @profile: profile being checked
 * @perms: perms computed for the request
 * @request: requested perms
 * @deny: Returns: explicit deny set
 * @sa: initialized audit structure (MAY BE NULL if not auditing)
 * @cb: callback fn for type specific fields (MAY BE NULL)
 *
 * Returns: 0 if permission else error code
 *
 * Note: profile audit modes need to be set before calling by setting the
 *       perm masks appropriately.
 *
 *       If not auditing then complain mode is not enabled and the
 *       error code will indicate whether there was an explicit deny
 *	 with a positive value.
 */
int aa_check_perms(struct aa_profile *profile, struct aa_perms *perms,
		   u32 request, struct common_audit_data *sa,
		   void (*cb)(struct audit_buffer *, void *))
{
	int type, error;
	u32 denied = request & (~perms->allow | perms->deny);

	if (likely(!denied)) {
		/* mask off perms that are not being force audited */
		request &= perms->audit;
		if (!request || !sa)
			return 0;

		type = AUDIT_APPARMOR_AUDIT;
		error = 0;
	} else {
		error = -EACCES;

		if (denied & perms->kill)
			type = AUDIT_APPARMOR_KILL;
		else if (denied == (denied & perms->complain))
			type = AUDIT_APPARMOR_ALLOWED;
		else
			type = AUDIT_APPARMOR_DENIED;

		if (denied == (denied & perms->hide))
			error = -ENOENT;

		denied &= ~perms->quiet;
		if (!sa || !denied)
			return error;
	}

	if (sa) {
		aad(sa)->label = &profile->label;
		aad(sa)->request = request;
		aad(sa)->denied = denied;
		aad(sa)->error = error;
		aa_audit_msg(type, sa, cb);
	}

	if (type == AUDIT_APPARMOR_ALLOWED)
		error = 0;

	return error;
}


/**
 * aa_policy_init - initialize a policy structure
 * @policy: policy to initialize  (NOT NULL)
 * @prefix: prefix name if any is required.  (MAYBE NULL)
 * @name: name of the policy, init will make a copy of it  (NOT NULL)
 * @gfp: allocation mode
 *
 * Note: this fn creates a copy of strings passed in
 *
 * Returns: true if policy init successful
 */
bool aa_policy_init(struct aa_policy *policy, const char *prefix,
		    const char *name, gfp_t gfp)
{
	char *hname;

	/* freed by policy_free */
	if (prefix) {
		hname = aa_str_alloc(strlen(prefix) + strlen(name) + 3, gfp);
		if (hname)
			sprintf(hname, "%s//%s", prefix, name);
	} else {
		hname = aa_str_alloc(strlen(name) + 1, gfp);
		if (hname)
			strcpy(hname, name);
	}
	if (!hname)
		return false;
	policy->hname = hname;
	/* base.name is a substring of fqname */
	policy->name = basename(policy->hname);
	INIT_LIST_HEAD(&policy->list);
	INIT_LIST_HEAD(&policy->profiles);

	return true;
}

/**
 * aa_policy_destroy - free the elements referenced by @policy
 * @policy: policy that is to have its elements freed  (NOT NULL)
 */
void aa_policy_destroy(struct aa_policy *policy)
{
	AA_BUG(on_list_rcu(&policy->profiles));
	AA_BUG(on_list_rcu(&policy->list));

	/* don't free name as its a subset of hname */
	aa_put_str(policy->hname);
}
