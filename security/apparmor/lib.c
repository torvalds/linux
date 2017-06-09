/*
 * AppArmor security module
 *
 * This file contains basic common functions used in AppArmor
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
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

	if (!name)
		return NULL;
	*ns_name = NULL;
	*ns_len = 0;
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
		DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, NULL);

		aad(&sa)->info = str;
		aa_audit_msg(AUDIT_APPARMOR_STATUS, &sa, NULL);
	}
	printk(KERN_INFO "AppArmor: %s\n", str);
}

__counted char *aa_str_alloc(int size, gfp_t gfp)
{
	struct counted_str *str;

	str = kmalloc(sizeof(struct counted_str) + size, gfp);
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
 * @mask: permission mask to convert
 */
void aa_perm_mask_to_str(char *str, const char *chrs, u32 mask)
{
	unsigned int i, perm = 1;

	for (i = 0; i < 32; perm <<= 1, i++) {
		if (mask & perm)
			*str++ = chrs[i];
	}
	*str = '\0';
}

void aa_audit_perm_names(struct audit_buffer *ab, const char **names, u32 mask)
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
			u32 chrsmask, const char **names, u32 namesmask)
{
	char str[33];

	audit_log_format(ab, "\"");
	if ((mask & chrsmask) && chrs) {
		aa_perm_mask_to_str(str, chrs, mask & chrsmask);
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
		/* fall through */
	case AUDIT_NOQUIET:
		perms->quiet = 0;
		break;
	case AUDIT_QUIET:
		perms->audit = 0;
		/* fall through */
	case AUDIT_QUIET_DENIED:
		perms->quiet = ALL_PERMS_MASK;
		break;
	}

	if (KILL_MODE(profile))
		perms->kill = ALL_PERMS_MASK;
	else if (COMPLAIN_MODE(profile))
		perms->complain = ALL_PERMS_MASK;
/*
 *  TODO:
 *	else if (PROMPT_MODE(profile))
 *		perms->prompt = ALL_PERMS_MASK;
 */
}

static u32 map_other(u32 x)
{
	return ((x & 0x3) << 8) |	/* SETATTR/GETATTR */
		((x & 0x1c) << 18) |	/* ACCEPT/BIND/LISTEN */
		((x & 0x60) << 19);	/* SETOPT/GETOPT */
}

void aa_compute_perms(struct aa_dfa *dfa, unsigned int state,
		      struct aa_perms *perms)
{
	perms->deny = 0;
	perms->kill = perms->stop = 0;
	perms->complain = perms->cond = 0;
	perms->hide = 0;
	perms->prompt = 0;
	perms->allow = dfa_user_allow(dfa, state);
	perms->audit = dfa_user_audit(dfa, state);
	perms->quiet = dfa_user_quiet(dfa, state);

	/* for v5 perm mapping in the policydb, the other set is used
	 * to extend the general perm set
	 */
	perms->allow |= map_other(dfa_other_allow(dfa, state));
	perms->audit |= map_other(dfa_other_audit(dfa, state));
	perms->quiet |= map_other(dfa_other_quiet(dfa, state));
//	perms->xindex = dfa_user_xindex(dfa, state);
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
