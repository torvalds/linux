/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999, 2000, 2001 Robert N. M. Watson
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
/*
 * acl_from_text: Convert a text-form ACL from a string to an acl_t.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include "namespace.h"
#include <sys/acl.h>
#include "un-namespace.h"
#include <sys/errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "acl_support.h"

static acl_tag_t acl_string_to_tag(char *tag, char *qualifier);

int _nfs4_acl_entry_from_text(acl_t aclp, char *entry);
int _text_could_be_nfs4_acl(const char *entry);

static acl_tag_t
acl_string_to_tag(char *tag, char *qualifier)
{

	if (*qualifier == '\0') {
		if ((!strcmp(tag, "user")) || (!strcmp(tag, "u"))) {
			return (ACL_USER_OBJ);
		} else
		if ((!strcmp(tag, "group")) || (!strcmp(tag, "g"))) {
			return (ACL_GROUP_OBJ);
		} else
		if ((!strcmp(tag, "mask")) || (!strcmp(tag, "m"))) {
			return (ACL_MASK);
		} else
		if ((!strcmp(tag, "other")) || (!strcmp(tag, "o"))) {
			return (ACL_OTHER);
		} else
			return(-1);
	} else {
		if ((!strcmp(tag, "user")) || (!strcmp(tag, "u"))) {
			return(ACL_USER);
		} else
		if ((!strcmp(tag, "group")) || (!strcmp(tag, "g"))) {
			return(ACL_GROUP);
		} else
			return(-1);
	}
}

static int
_posix1e_acl_entry_from_text(acl_t aclp, char *entry)
{
	acl_tag_t	 t;
	acl_perm_t	 p;
	char		*tag, *qualifier, *permission;
	uid_t		 id;
	int		 error;

	assert(_acl_brand(aclp) == ACL_BRAND_POSIX);

	/* Split into three ':' delimited fields. */
	tag = strsep(&entry, ":");
	if (tag == NULL) {
		errno = EINVAL;
		return (-1);
	}
	tag = string_skip_whitespace(tag);
	if ((*tag == '\0') && (!entry)) {
		/*
		 * Is an entirely comment line, skip to next
		 * comma.
		 */
		return (0);
	}
	string_trim_trailing_whitespace(tag);

	qualifier = strsep(&entry, ":");
	if (qualifier == NULL) {
		errno = EINVAL;
		return (-1);
	}
	qualifier = string_skip_whitespace(qualifier);
	string_trim_trailing_whitespace(qualifier);

	permission = strsep(&entry, ":");
	if (permission == NULL || entry) {
		errno = EINVAL;
		return (-1);
	}
	permission = string_skip_whitespace(permission);
	string_trim_trailing_whitespace(permission);

	t = acl_string_to_tag(tag, qualifier);
	if (t == -1) {
		errno = EINVAL;
		return (-1);
	}

	error = _posix1e_acl_string_to_perm(permission, &p);
	if (error == -1) {
		errno = EINVAL;
		return (-1);
	}		

	switch(t) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			if (*qualifier != '\0') {
				errno = EINVAL;
				return (-1);
			}
			id = 0;
			break;

		case ACL_USER:
		case ACL_GROUP:
			error = _acl_name_to_id(t, qualifier, &id);
			if (error == -1)
				return (-1);
			break;

		default:
			errno = EINVAL;
			return (-1);
	}

	error = _posix1e_acl_add_entry(aclp, t, id, p);
	if (error == -1)
		return (-1);

	return (0);
}

static int
_text_is_nfs4_entry(const char *entry)
{
	int count = 0;

	assert(strlen(entry) > 0);

	while (*entry != '\0') {
		if (*entry == ':' || *entry == '@')
			count++;
		entry++;
	}

	if (count <= 2)
		return (0);

	return (1);
}

/*
 * acl_from_text -- Convert a string into an ACL.
 * Postpone most validity checking until the end and call acl_valid() to do
 * that.
 */
acl_t
acl_from_text(const char *buf_p)
{
	acl_t		 acl;
	char		*mybuf_p, *line, *cur, *notcomment, *comment, *entry;
	int		 error;

	/* Local copy we can mess up. */
	mybuf_p = strdup(buf_p);
	if (mybuf_p == NULL)
		return(NULL);

	acl = acl_init(3); /* XXX: WTF, 3? */
	if (acl == NULL) {
		free(mybuf_p);
		return(NULL);
	}

	/* Outer loop: delimit at \n boundaries. */
	cur = mybuf_p;
	while ((line = strsep(&cur, "\n"))) {
		/* Now split the line on the first # to strip out comments. */
		comment = line;
		notcomment = strsep(&comment, "#");

		/* Inner loop: delimit at ',' boundaries. */
		while ((entry = strsep(&notcomment, ","))) {

			/* Skip empty lines. */
			if (strlen(string_skip_whitespace(entry)) == 0)
				continue;

			if (_acl_brand(acl) == ACL_BRAND_UNKNOWN) {
				if (_text_is_nfs4_entry(entry))
					_acl_brand_as(acl, ACL_BRAND_NFS4);
				else
					_acl_brand_as(acl, ACL_BRAND_POSIX);
			}

			switch (_acl_brand(acl)) {
			case ACL_BRAND_NFS4:
				error = _nfs4_acl_entry_from_text(acl, entry);
				break;

			case ACL_BRAND_POSIX:
				error = _posix1e_acl_entry_from_text(acl, entry);
				break;

			default:
				error = EINVAL;
				break;
			}

			if (error)
				goto error_label;
		}
	}

#if 0
	/* XXX Should we only return ACLs valid according to acl_valid? */
	/* Verify validity of the ACL we read in. */
	if (acl_valid(acl) == -1) {
		errno = EINVAL;
		goto error_label;
	}
#endif

	free(mybuf_p);
	return(acl);

error_label:
	acl_free(acl);
	free(mybuf_p);
	return(NULL);
}

/*
 * Given a username/groupname from a text form of an ACL, return the uid/gid
 * XXX NOT THREAD SAFE, RELIES ON GETPWNAM, GETGRNAM
 * XXX USES *PW* AND *GR* WHICH ARE STATEFUL AND THEREFORE THIS ROUTINE
 * MAY HAVE SIDE-EFFECTS
 */
int
_acl_name_to_id(acl_tag_t tag, char *name, uid_t *id)
{
	struct group	*g;
	struct passwd	*p;
	unsigned long	l;
	char 		*endp;

	switch(tag) {
	case ACL_USER:
		p = getpwnam(name);
		if (p == NULL) {
			l = strtoul(name, &endp, 0);
			if (*endp != '\0' || l != (unsigned long)(uid_t)l) {
				errno = EINVAL;
				return (-1);
			}
			*id = (uid_t)l;
			return (0);
		}
		*id = p->pw_uid;
		return (0);

	case ACL_GROUP:
		g = getgrnam(name);
		if (g == NULL) {
			l = strtoul(name, &endp, 0);
			if (*endp != '\0' || l != (unsigned long)(gid_t)l) {
				errno = EINVAL;
				return (-1);
			}
			*id = (gid_t)l;
			return (0);
		}
		*id = g->gr_gid;
		return (0);

	default:
		return (EINVAL);
	}
}
