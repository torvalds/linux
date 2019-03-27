/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999, 2000, 2001, 2002 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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
 * acl_get_fd - syscall wrapper for retrieving access ACL by fd
 * acl_get_fd_np - syscall wrapper for retrieving ACL by fd (non-POSIX)
 * acl_get_file - syscall wrapper for retrieving ACL by filename
 * acl_get_link_np - syscall wrapper for retrieving ACL by filename (NOFOLLOW)
 *                   (non-POSIX)
 * acl_get_perm_np() checks if a permission is in the specified
 *                   permset (non-POSIX)
 * acl_get_permset() returns the permission set in the ACL entry
 * acl_get_qualifier() retrieves the qualifier of the tag from the ACL entry
 * acl_get_tag_type() returns the tag type for the ACL entry entry_d
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include "namespace.h"
#include <sys/acl.h>
#include "un-namespace.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "acl_support.h"

acl_t
acl_get_file(const char *path_p, acl_type_t type)
{
	acl_t	aclp;
	int	error;

	aclp = acl_init(ACL_MAX_ENTRIES);
	if (aclp == NULL)
		return (NULL);

	type = _acl_type_unold(type);
	error = __acl_get_file(path_p, type, &aclp->ats_acl);
	if (error) {
		acl_free(aclp);
		return (NULL);
	}

	aclp->ats_acl.acl_maxcnt = ACL_MAX_ENTRIES;
	_acl_brand_from_type(aclp, type);

	return (aclp);
}

acl_t
acl_get_link_np(const char *path_p, acl_type_t type)
{
	acl_t	aclp;
	int	error;

	aclp = acl_init(ACL_MAX_ENTRIES);
	if (aclp == NULL)
		return (NULL);

	type = _acl_type_unold(type);
	error = __acl_get_link(path_p, type, &aclp->ats_acl);
	if (error) {
		acl_free(aclp);
		return (NULL);
	}

	aclp->ats_acl.acl_maxcnt = ACL_MAX_ENTRIES;
	_acl_brand_from_type(aclp, type);

	return (aclp);
}

acl_t
acl_get_fd(int fd)
{
	if (fpathconf(fd, _PC_ACL_NFS4) == 1)
		return (acl_get_fd_np(fd, ACL_TYPE_NFS4));

	return (acl_get_fd_np(fd, ACL_TYPE_ACCESS));
}

acl_t
acl_get_fd_np(int fd, acl_type_t type)
{
	acl_t	aclp;
	int	error;

	aclp = acl_init(ACL_MAX_ENTRIES);
	if (aclp == NULL)
		return (NULL);

	type = _acl_type_unold(type);
	error = ___acl_get_fd(fd, type, &aclp->ats_acl);
	if (error) {
		acl_free(aclp);
		return (NULL);
	}

	aclp->ats_acl.acl_maxcnt = ACL_MAX_ENTRIES;
	_acl_brand_from_type(aclp, type);

	return (aclp);
}

/*
 * acl_get_permset() (23.4.17): return via permset_p a descriptor to
 * the permission set in the ACL entry entry_d.
 */
int
acl_get_permset(acl_entry_t entry_d, acl_permset_t *permset_p)
{

	if (entry_d == NULL || permset_p == NULL) {
		errno = EINVAL;
		return (-1);
	}

	*permset_p = &entry_d->ae_perm;

	return (0);
}

/*
 * acl_get_qualifier() (23.4.18): retrieve the qualifier of the tag
 * for the ACL entry entry_d.
 */
void *
acl_get_qualifier(acl_entry_t entry_d)
{
	uid_t *retval;

	if (entry_d == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	switch(entry_d->ae_tag) {
	case ACL_USER:
	case ACL_GROUP:
		retval = malloc(sizeof(uid_t));
		if (retval == NULL)
			return (NULL);
		*retval = entry_d->ae_id;
		return (retval);
	}

	errno = EINVAL;
	return (NULL);
}

/*
 * acl_get_tag_type() (23.4.19): return the tag type for the ACL
 * entry entry_p.
 */
int
acl_get_tag_type(acl_entry_t entry_d, acl_tag_t *tag_type_p)
{

	if (entry_d == NULL || tag_type_p == NULL) {
		errno = EINVAL;
		return (-1);
	}

	*tag_type_p = entry_d->ae_tag;

	return (0);
}

int
acl_get_entry_type_np(acl_entry_t entry_d, acl_entry_type_t *entry_type_p)
{

	if (entry_d == NULL || entry_type_p == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (!_entry_brand_may_be(entry_d, ACL_BRAND_NFS4)) {
		errno = EINVAL;
		return (-1);
	}

	*entry_type_p = entry_d->ae_entry_type;

	return (0);
}
