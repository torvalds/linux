// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor /proc/<pid>/attr/ interface functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#include "include/apparmor.h"
#include "include/cred.h"
#include "include/policy.h"
#include "include/policy_ns.h"
#include "include/domain.h"
#include "include/procattr.h"


/**
 * aa_getprocattr - Return the label information for @label
 * @label: the label to print label info about  (NOT NULL)
 * @string: Returns - string containing the label info (NOT NULL)
 * @newline: indicates that a newline should be added
 *
 * Requires: label != NULL && string != NULL
 *
 * Creates a string containing the label information for @label.
 *
 * Returns: size of string placed in @string else error code on failure
 */
int aa_getprocattr(struct aa_label *label, char **string, bool newline)
{
	struct aa_ns *ns = labels_ns(label);
	struct aa_ns *current_ns = aa_get_current_ns();
	int len;

	if (!aa_ns_visible(current_ns, ns, true)) {
		aa_put_ns(current_ns);
		return -EACCES;
	}

	len = aa_label_snxprint(NULL, 0, current_ns, label,
				FLAG_SHOW_MODE | FLAG_VIEW_SUBNS |
				FLAG_HIDDEN_UNCONFINED);
	AA_BUG(len < 0);

	*string = kmalloc(len + 2, GFP_KERNEL);
	if (!*string) {
		aa_put_ns(current_ns);
		return -ENOMEM;
	}

	len = aa_label_snxprint(*string, len + 2, current_ns, label,
				FLAG_SHOW_MODE | FLAG_VIEW_SUBNS |
				FLAG_HIDDEN_UNCONFINED);
	if (len < 0) {
		aa_put_ns(current_ns);
		return len;
	}

	if (newline)
		(*string)[len++] = '\n';
	(*string)[len] = 0;

	aa_put_ns(current_ns);
	return len;
}

/**
 * split_token_from_name - separate a string of form  <token>^<name>
 * @op: operation being checked
 * @args: string to parse  (NOT NULL)
 * @token: stores returned parsed token value  (NOT NULL)
 *
 * Returns: start position of name after token else NULL on failure
 */
static char *split_token_from_name(const char *op, char *args, u64 *token)
{
	char *name;

	*token = simple_strtoull(args, &name, 16);
	if ((name == args) || *name != '^') {
		AA_ERROR("%s: Invalid input '%s'", op, args);
		return ERR_PTR(-EINVAL);
	}

	name++;			/* skip ^ */
	if (!*name)
		name = NULL;
	return name;
}

/**
 * aa_setprocattr_changehat - handle procattr interface to change_hat
 * @args: args received from writing to /proc/<pid>/attr/current (NOT NULL)
 * @size: size of the args
 * @flags: set of flags governing behavior
 *
 * Returns: %0 or error code if change_hat fails
 */
int aa_setprocattr_changehat(char *args, size_t size, int flags)
{
	char *hat;
	u64 token;
	const char *hats[16];		/* current hard limit on # of names */
	int count = 0;

	hat = split_token_from_name(OP_CHANGE_HAT, args, &token);
	if (IS_ERR(hat))
		return PTR_ERR(hat);

	if (!hat && !token) {
		AA_ERROR("change_hat: Invalid input, NULL hat and NULL magic");
		return -EINVAL;
	}

	if (hat) {
		/* set up hat name vector, args guaranteed null terminated
		 * at args[size] by setprocattr.
		 *
		 * If there are multiple hat names in the buffer each is
		 * separated by a \0.  Ie. userspace writes them pre tokenized
		 */
		char *end = args + size;
		for (count = 0; (hat < end) && count < 16; ++count) {
			char *next = hat + strlen(hat) + 1;
			hats[count] = hat;
			AA_DEBUG(DEBUG_DOMAIN,
				 "%s: (pid %d) Magic 0x%llx count %d hat '%s'\n"
				 , __func__, current->pid, token, count, hat);
			hat = next;
		}
	} else
		AA_DEBUG(DEBUG_DOMAIN,
			 "%s: (pid %d) Magic 0x%llx count %d Hat '%s'\n",
			 __func__, current->pid, token, count, "<NULL>");

	return aa_change_hat(hats, count, token, flags);
}
