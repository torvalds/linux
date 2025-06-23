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

struct val_table_ent {
	const char *str;
	int value;
};

static struct val_table_ent debug_values_table[] = {
	{ "N", DEBUG_NONE },
	{ "none", DEBUG_NONE },
	{ "n", DEBUG_NONE },
	{ "0", DEBUG_NONE },
	{ "all", DEBUG_ALL },
	{ "Y", DEBUG_ALL },
	{ "y", DEBUG_ALL },
	{ "1", DEBUG_ALL },
	{ "abs_root", DEBUG_LABEL_ABS_ROOT },
	{ "label", DEBUG_LABEL },
	{ "domain", DEBUG_DOMAIN },
	{ "policy", DEBUG_POLICY },
	{ "interface", DEBUG_INTERFACE },
	{ NULL, 0 }
};

static struct val_table_ent *val_table_find_ent(struct val_table_ent *table,
						const char *name, size_t len)
{
	struct val_table_ent *entry;

	for (entry = table; entry->str != NULL; entry++) {
		if (strncmp(entry->str, name, len) == 0 &&
		    strlen(entry->str) == len)
			return entry;
	}
	return NULL;
}

int aa_parse_debug_params(const char *str)
{
	struct val_table_ent *ent;
	const char *next;
	int val = 0;

	do {
		size_t n = strcspn(str, "\r\n,");

		next = str + n;
		ent = val_table_find_ent(debug_values_table, str, next - str);
		if (ent)
			val |= ent->value;
		else
			AA_DEBUG(DEBUG_INTERFACE, "unknown debug type '%.*s'",
				 (int)(next - str), str);
		str = next + 1;
	} while (*next != 0);
	return val;
}

/**
 * val_mask_to_str - convert a perm mask to its short string
 * @str: character buffer to store string in (at least 10 characters)
 * @size: size of the @str buffer
 * @table: NUL-terminated character buffer of permission characters (NOT NULL)
 * @mask: permission mask to convert
 */
static int val_mask_to_str(char *str, size_t size,
			   const struct val_table_ent *table, u32 mask)
{
	const struct val_table_ent *ent;
	int total = 0;

	for (ent = table; ent->str; ent++) {
		if (ent->value && (ent->value & mask) == ent->value) {
			int len = scnprintf(str, size, "%s%s", total ? "," : "",
					    ent->str);
			size -= len;
			str += len;
			total += len;
			mask &= ~ent->value;
		}
	}

	return total;
}

int aa_print_debug_params(char *buffer)
{
	if (!aa_g_debug)
		return sprintf(buffer, "N");
	return val_mask_to_str(buffer, PAGE_SIZE, debug_values_table,
			       aa_g_debug);
}

bool aa_resize_str_table(struct aa_str_table *t, int newsize, gfp_t gfp)
{
	char **n;
	int i;

	if (t->size == newsize)
		return true;
	n = kcalloc(newsize, sizeof(*n), gfp);
	if (!n)
		return false;
	for (i = 0; i < min(t->size, newsize); i++)
		n[i] = t->table[i];
	for (; i < t->size; i++)
		kfree_sensitive(t->table[i]);
	if (newsize > t->size)
		memset(&n[t->size], 0, (newsize-t->size)*sizeof(*n));
	kfree_sensitive(t->table);
	t->table = n;
	t->size = newsize;

	return true;
}

/**
 * aa_free_str_table - free entries str table
 * @t: the string table to free  (MAYBE NULL)
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
		t->size = 0;
	}
}

/**
 * skipn_spaces - Removes leading whitespace from @str.
 * @str: The string to be stripped.
 * @n: length of str to parse, will stop at \0 if encountered before n
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
		DEFINE_AUDIT_DATA(ad, LSM_AUDIT_DATA_NONE, AA_CLASS_NONE, NULL);

		ad.info = str;
		aa_audit_msg(AUDIT_APPARMOR_STATUS, &ad, NULL);
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

	state = aa_dfa_next(rules->policy->dfa,
			    rules->policy->start[AA_CLASS_LABEL],
			    type);
	aa_label_match(profile, rules, label, state, false, request, perms);
}


/**
 * aa_check_perms - do audit mode selection based on perms set
 * @profile: profile being checked
 * @perms: perms computed for the request
 * @request: requested perms
 * @ad: initialized audit structure (MAY BE NULL if not auditing)
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
		   u32 request, struct apparmor_audit_data *ad,
		   void (*cb)(struct audit_buffer *, void *))
{
	int type, error;
	u32 denied = request & (~perms->allow | perms->deny);

	if (likely(!denied)) {
		/* mask off perms that are not being force audited */
		request &= perms->audit;
		if (!request || !ad)
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
		if (!ad || !denied)
			return error;
	}

	if (ad) {
		ad->subj_label = &profile->label;
		ad->request = request;
		ad->denied = denied;
		ad->error = error;
		aa_audit_msg(type, ad, cb);
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
