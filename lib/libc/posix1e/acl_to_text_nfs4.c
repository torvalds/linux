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
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/acl.h>

#include "acl_support.h"

#define MAX_ENTRY_LENGTH 512

static int
format_who(char *str, size_t size, const acl_entry_t entry, int numeric)
{
	int error;
	acl_tag_t tag;
	struct passwd *pwd;
	struct group *grp;
	uid_t *id;

	error = acl_get_tag_type(entry, &tag);
	if (error)
		return (error);

	switch (tag) {
	case ACL_USER_OBJ:
		snprintf(str, size, "owner@");
		break;

	case ACL_USER:
		id = (uid_t *)acl_get_qualifier(entry);
		if (id == NULL)
			return (-1);
		/* XXX: Thread-unsafe. */
		if (!numeric)
			pwd = getpwuid(*id);
		else
			pwd = NULL;
		if (pwd == NULL)
			snprintf(str, size, "user:%d", (unsigned int)*id);
		else
			snprintf(str, size, "user:%s", pwd->pw_name);
		break;

	case ACL_GROUP_OBJ:
		snprintf(str, size, "group@");
		break;

	case ACL_GROUP:
		id = (uid_t *)acl_get_qualifier(entry);
		if (id == NULL)
			return (-1);
		/* XXX: Thread-unsafe. */
		if (!numeric)
			grp = getgrgid(*id);
		else
			grp = NULL;
		if (grp == NULL)
			snprintf(str, size, "group:%d", (unsigned int)*id);
		else
			snprintf(str, size, "group:%s", grp->gr_name);
		break;

	case ACL_EVERYONE:
		snprintf(str, size, "everyone@");
		break;

	default:
		return (-1);
	}

	return (0);
}

static int
format_entry_type(char *str, size_t size, const acl_entry_t entry)
{
	int error;
	acl_entry_type_t entry_type;

	error = acl_get_entry_type_np(entry, &entry_type);
	if (error)
		return (error);

	switch (entry_type) {
	case ACL_ENTRY_TYPE_ALLOW:
		snprintf(str, size, "allow");
		break;
	case ACL_ENTRY_TYPE_DENY:
		snprintf(str, size, "deny");
		break;
	case ACL_ENTRY_TYPE_AUDIT:
		snprintf(str, size, "audit");
		break;
	case ACL_ENTRY_TYPE_ALARM:
		snprintf(str, size, "alarm");
		break;
	default:
		return (-1);
	}

	return (0);
}

static int
format_additional_id(char *str, size_t size, const acl_entry_t entry)
{
	int error;
	acl_tag_t tag;
	uid_t *id;

	error = acl_get_tag_type(entry, &tag);
	if (error)
		return (error);

	switch (tag) {
	case ACL_USER_OBJ:
	case ACL_GROUP_OBJ:
	case ACL_EVERYONE:
		str[0] = '\0';
		break;

	default:
		id = (uid_t *)acl_get_qualifier(entry);
		if (id == NULL)
			return (-1);
		snprintf(str, size, ":%d", (unsigned int)*id);
	}

	return (0);
}

static int
format_entry(char *str, size_t size, const acl_entry_t entry, int flags)
{
	size_t off = 0, min_who_field_length = 18;
	acl_permset_t permset;
	acl_flagset_t flagset;
	int error, len;
	char buf[MAX_ENTRY_LENGTH + 1];

	assert(_entry_brand(entry) == ACL_BRAND_NFS4);

	error = acl_get_flagset_np(entry, &flagset);
	if (error)
		return (error);

	error = acl_get_permset(entry, &permset);
	if (error)
		return (error);

	error = format_who(buf, sizeof(buf), entry,
	    flags & ACL_TEXT_NUMERIC_IDS);
	if (error)
		return (error);
	len = strlen(buf);
	if (len < min_who_field_length)
		len = min_who_field_length;
	off += snprintf(str + off, size - off, "%*s:", len, buf);

	error = _nfs4_format_access_mask(buf, sizeof(buf), *permset,
	    flags & ACL_TEXT_VERBOSE);
	if (error)
		return (error);
	off += snprintf(str + off, size - off, "%s:", buf);

	error = _nfs4_format_flags(buf, sizeof(buf), *flagset,
	    flags & ACL_TEXT_VERBOSE);
	if (error)
		return (error);
	off += snprintf(str + off, size - off, "%s:", buf);

	error = format_entry_type(buf, sizeof(buf), entry);
	if (error)
		return (error);
	off += snprintf(str + off, size - off, "%s", buf);

	if (flags & ACL_TEXT_APPEND_ID) {
		error = format_additional_id(buf, sizeof(buf), entry);
		if (error)
			return (error);
		off += snprintf(str + off, size - off, "%s", buf);
	}

	off += snprintf(str + off, size - off, "\n");

	/* Make sure we didn't truncate anything. */
	assert (off < size);

	return (0);
}

char *
_nfs4_acl_to_text_np(const acl_t aclp, ssize_t *len_p, int flags)
{
	int error, off = 0, size, entry_id = ACL_FIRST_ENTRY;
	char *str;
	acl_entry_t entry;

	if (aclp->ats_acl.acl_cnt == 0)
		return strdup("");

	size = aclp->ats_acl.acl_cnt * MAX_ENTRY_LENGTH;
	str = malloc(size);
	if (str == NULL)
		return (NULL);

	while (acl_get_entry(aclp, entry_id, &entry) == 1) {
		entry_id = ACL_NEXT_ENTRY;

		assert(off < size);

		error = format_entry(str + off, size - off, entry, flags);
		if (error) {
			free(str);
			errno = EINVAL;
			return (NULL);
		}

		off = strlen(str);
	}

	assert(off < size);
	str[off] = '\0';

	if (len_p != NULL)
		*len_p = off;

	return (str);
}
