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
#include "include/policy.h"

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

static const char *skipn_spaces(const char *str, size_t n)
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

/**
 * aa_policy_init - initialize a policy structure
 * @policy: policy to initialize  (NOT NULL)
 * @prefix: prefix name if any is required.  (MAYBE NULL)
 * @name: name of the policy, init will make a copy of it  (NOT NULL)
 *
 * Note: this fn creates a copy of strings passed in
 *
 * Returns: true if policy init successful
 */
bool aa_policy_init(struct aa_policy *policy, const char *prefix,
		    const char *name, gfp_t gfp)
{
	/* freed by policy_free */
	if (prefix) {
		policy->hname = kmalloc(strlen(prefix) + strlen(name) + 3,
					gfp);
		if (policy->hname)
			sprintf((char *)policy->hname, "%s//%s", prefix, name);
	} else
		policy->hname = kstrdup(name, gfp);
	if (!policy->hname)
		return false;
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
	kzfree(policy->hname);
}
