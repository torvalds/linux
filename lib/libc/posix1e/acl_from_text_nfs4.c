/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, 2009 Edward Tomasz Napierała <trasz@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <err.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/acl.h>

#include "acl_support.h"

#define MAX_ENTRY_LENGTH 512

/*
 * Parse the tag field of ACL entry passed as "str".  If qualifier
 * needs to follow, then the variable referenced by "need_qualifier"
 * is set to 1, otherwise it's set to 0.
 */
static int
parse_tag(const char *str, acl_entry_t entry, int *need_qualifier)
{

	assert(need_qualifier != NULL);
	*need_qualifier = 0;

	if (strcmp(str, "owner@") == 0)
		return (acl_set_tag_type(entry, ACL_USER_OBJ));
	if (strcmp(str, "group@") == 0)
		return (acl_set_tag_type(entry, ACL_GROUP_OBJ));
	if (strcmp(str, "everyone@") == 0)
		return (acl_set_tag_type(entry, ACL_EVERYONE));

	*need_qualifier = 1;

	if (strcmp(str, "user") == 0 || strcmp(str, "u") == 0)
		return (acl_set_tag_type(entry, ACL_USER));
	if (strcmp(str, "group") == 0 || strcmp(str, "g") == 0)
		return (acl_set_tag_type(entry, ACL_GROUP));

	warnx("malformed ACL: invalid \"tag\" field");

	return (-1);
}

/*
 * Parse the qualifier field of ACL entry passed as "str".
 * If user or group name cannot be resolved, then the variable
 * referenced by "need_qualifier" is set to 1; it will be checked
 * later to figure out whether the appended_id is required.
 */
static int
parse_qualifier(char *str, acl_entry_t entry, int *need_qualifier)
{
	int qualifier_length, error;
	uid_t id;
	acl_tag_t tag;

	assert(need_qualifier != NULL);
	*need_qualifier = 0;

	qualifier_length = strlen(str);

	if (qualifier_length == 0) {
		warnx("malformed ACL: empty \"qualifier\" field");
		return (-1);
	}

	error = acl_get_tag_type(entry, &tag);
	if (error)
		return (error);

	error = _acl_name_to_id(tag, str, &id);
	if (error) {
		*need_qualifier = 1;
		return (0);
	}

	return (acl_set_qualifier(entry, &id));
}

static int
parse_access_mask(char *str, acl_entry_t entry)
{
	int error;
	acl_perm_t perm;

	error = _nfs4_parse_access_mask(str, &perm);
	if (error)
		return (error);

	error = acl_set_permset(entry, &perm);

	return (error);
}

static int
parse_flags(char *str, acl_entry_t entry)
{
	int error;
	acl_flag_t flags;

	error = _nfs4_parse_flags(str, &flags);
	if (error)
		return (error);

	error = acl_set_flagset_np(entry, &flags);

	return (error);
}

static int
parse_entry_type(const char *str, acl_entry_t entry)
{

	if (strcmp(str, "allow") == 0)
		return (acl_set_entry_type_np(entry, ACL_ENTRY_TYPE_ALLOW));
	if (strcmp(str, "deny") == 0)
		return (acl_set_entry_type_np(entry, ACL_ENTRY_TYPE_DENY));
	if (strcmp(str, "audit") == 0)
		return (acl_set_entry_type_np(entry, ACL_ENTRY_TYPE_AUDIT));
	if (strcmp(str, "alarm") == 0)
		return (acl_set_entry_type_np(entry, ACL_ENTRY_TYPE_ALARM));

	warnx("malformed ACL: invalid \"type\" field");

	return (-1);
}

static int
parse_appended_id(char *str, acl_entry_t entry)
{
	int qualifier_length;
	char *end;
	id_t id;

	qualifier_length = strlen(str);
	if (qualifier_length == 0) {
		warnx("malformed ACL: \"appended id\" field present, "
	           "but empty");
		return (-1);
	}

	id = strtod(str, &end);
	if (end - str != qualifier_length) {
		warnx("malformed ACL: appended id is not a number");
		return (-1);
	}

	return (acl_set_qualifier(entry, &id));
}

static int
number_of_colons(const char *str)
{
	int count = 0;

	while (*str != '\0') {
		if (*str == ':')
			count++;

		str++;
	}

	return (count);
}

int
_nfs4_acl_entry_from_text(acl_t aclp, char *str)
{
	int error, need_qualifier;
	acl_entry_t entry;
	char *field, *qualifier_field;

	error = acl_create_entry(&aclp, &entry);
	if (error)
		return (error);

	assert(_entry_brand(entry) == ACL_BRAND_NFS4);

	if (str == NULL)
		goto truncated_entry;
	field = strsep(&str, ":");

	field = string_skip_whitespace(field);
	if ((*field == '\0') && (!str)) {
		/*
		 * Is an entirely comment line, skip to next
		 * comma.
		 */
		return (0);
	}

	error = parse_tag(field, entry, &need_qualifier);
	if (error)
		goto malformed_field;

	if (need_qualifier) {
		if (str == NULL)
			goto truncated_entry;
		qualifier_field = field = strsep(&str, ":");
		error = parse_qualifier(field, entry, &need_qualifier);
		if (error)
			goto malformed_field;
	}

	if (str == NULL)
		goto truncated_entry;
	field = strsep(&str, ":");
	error = parse_access_mask(field, entry);
	if (error)
		goto malformed_field;

	if (str == NULL)
		goto truncated_entry;
	/* Do we have "flags" field? */
	if (number_of_colons(str) > 0) {
		field = strsep(&str, ":");
		error = parse_flags(field, entry);
		if (error)
			goto malformed_field;
	}

	if (str == NULL)
		goto truncated_entry;
	field = strsep(&str, ":");
	error = parse_entry_type(field, entry);
	if (error)
		goto malformed_field;

	if (need_qualifier) {
		if (str == NULL) {
			warnx("malformed ACL: unknown user or group name "
			    "\"%s\"", qualifier_field);
			goto truncated_entry;
		}

		error = parse_appended_id(str, entry);
		if (error)
			goto malformed_field;
	}

	return (0);

truncated_entry:
malformed_field:
	acl_delete_entry(aclp, entry);
	errno = EINVAL;
	return (-1);
}
