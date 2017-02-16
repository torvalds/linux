/*
 * AppArmor security module
 *
 * This file contains AppArmor /proc/<pid>/attr/ interface functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include "include/apparmor.h"
#include "include/context.h"
#include "include/policy.h"
#include "include/policy_ns.h"
#include "include/domain.h"
#include "include/procattr.h"


/**
 * aa_getprocattr - Return the profile information for @profile
 * @profile: the profile to print profile info about  (NOT NULL)
 * @string: Returns - string containing the profile info (NOT NULL)
 *
 * Returns: length of @string on success else error on failure
 *
 * Requires: profile != NULL
 *
 * Creates a string containing the namespace_name://profile_name for
 * @profile.
 *
 * Returns: size of string placed in @string else error code on failure
 */
int aa_getprocattr(struct aa_profile *profile, char **string)
{
	char *str;
	int len = 0, mode_len = 0, ns_len = 0, name_len;
	const char *mode_str = aa_profile_mode_names[profile->mode];
	const char *ns_name = NULL;
	struct aa_ns *ns = profile->ns;
	struct aa_ns *current_ns = __aa_current_profile()->ns;
	char *s;

	if (!aa_ns_visible(current_ns, ns, true))
		return -EACCES;

	ns_name = aa_ns_name(current_ns, ns, true);
	ns_len = strlen(ns_name);

	/* if the visible ns_name is > 0 increase size for : :// seperator */
	if (ns_len)
		ns_len += 4;

	/* unconfined profiles don't have a mode string appended */
	if (!unconfined(profile))
		mode_len = strlen(mode_str) + 3;	/* + 3 for _() */

	name_len = strlen(profile->base.hname);
	len = mode_len + ns_len + name_len + 1;	    /* + 1 for \n */
	s = str = kmalloc(len + 1, GFP_KERNEL);	    /* + 1 \0 */
	if (!str)
		return -ENOMEM;

	if (ns_len) {
		/* skip over prefix current_ns->base.hname and separating // */
		sprintf(s, ":%s://", ns_name);
		s += ns_len;
	}
	if (unconfined(profile))
		/* mode string not being appended */
		sprintf(s, "%s\n", profile->base.hname);
	else
		sprintf(s, "%s (%s)\n", profile->base.hname, mode_str);
	*string = str;

	/* NOTE: len does not include \0 of string, not saved as part of file */
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
 * aa_setprocattr_chagnehat - handle procattr interface to change_hat
 * @args: args received from writing to /proc/<pid>/attr/current (NOT NULL)
 * @size: size of the args
 * @test: true if this is a test of change_hat permissions
 *
 * Returns: %0 or error code if change_hat fails
 */
int aa_setprocattr_changehat(char *args, size_t size, int test)
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
			AA_DEBUG("%s: (pid %d) Magic 0x%llx count %d hat '%s'\n"
				 , __func__, current->pid, token, count, hat);
			hat = next;
		}
	} else
		AA_DEBUG("%s: (pid %d) Magic 0x%llx count %d Hat '%s'\n",
			 __func__, current->pid, token, count, "<NULL>");

	return aa_change_hat(hats, count, token, test);
}
