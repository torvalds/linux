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
 * acl_set_file -- set a file/directory ACL by name
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include "namespace.h"
#include <sys/acl.h>
#include "un-namespace.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "acl_support.h"

/*
 * For POSIX.1e-semantic ACLs, do a presort so the kernel doesn't have to
 * (the POSIX.1e semantic code will reject unsorted ACL submission).  If it's
 * not a semantic that the library knows about, just submit it flat and
 * assume the caller knows what they're up to.
 */
int
acl_set_file(const char *path_p, acl_type_t type, acl_t acl)
{

	if (acl == NULL || path_p == NULL) {
		errno = EINVAL;
		return (-1);
	}
	type = _acl_type_unold(type);
	if (_acl_type_not_valid_for_acl(acl, type)) {
		errno = EINVAL;
		return (-1);
	}
	if (_posix1e_acl(acl, type))
		_posix1e_acl_sort(acl);

	acl->ats_cur_entry = 0;

	return (__acl_set_file(path_p, type, &acl->ats_acl));
}

int
acl_set_link_np(const char *path_p, acl_type_t type, acl_t acl)
{

	if (acl == NULL || path_p == NULL) {
		errno = EINVAL;
		return (-1);
	}
	type = _acl_type_unold(type);
	if (_acl_type_not_valid_for_acl(acl, type)) {
		errno = EINVAL;
		return (-1);
	}
	if (_posix1e_acl(acl, type))
		_posix1e_acl_sort(acl);

	acl->ats_cur_entry = 0;

	return (__acl_set_link(path_p, type, &acl->ats_acl));
}

int
acl_set_fd(int fd, acl_t acl)
{

	if (fpathconf(fd, _PC_ACL_NFS4) == 1)
		return (acl_set_fd_np(fd, acl, ACL_TYPE_NFS4));

	return (acl_set_fd_np(fd, acl, ACL_TYPE_ACCESS));
}

int
acl_set_fd_np(int fd, acl_t acl, acl_type_t type)
{

	if (acl == NULL) {
		errno = EINVAL;
		return (-1);
	}
	type = _acl_type_unold(type);
	if (_acl_type_not_valid_for_acl(acl, type)) {
		errno = EINVAL;
		return (-1);
	}
	if (_posix1e_acl(acl, type))
		_posix1e_acl_sort(acl);

	acl->ats_cur_entry = 0;

	return (___acl_set_fd(fd, type, &acl->ats_acl));
}

/*
 * acl_set_permset() (23.4.23): sets the permissions of ACL entry entry_d
 * with the permissions in permset_d
 */
int
acl_set_permset(acl_entry_t entry_d, acl_permset_t permset_d)
{

	if (!entry_d) {
		errno = EINVAL;
		return (-1);
	}

	if ((*permset_d & ACL_POSIX1E_BITS) != *permset_d) {
		if ((*permset_d & ACL_NFS4_PERM_BITS) != *permset_d) {
			errno = EINVAL;
			return (-1);
		}
		if (!_entry_brand_may_be(entry_d, ACL_BRAND_NFS4)) {
			errno = EINVAL;
			return (-1);
		}
		_entry_brand_as(entry_d, ACL_BRAND_NFS4);
	}

	entry_d->ae_perm = *permset_d;

	return (0);
}

/*
 * acl_set_qualifier() sets the qualifier (ae_id) of the tag for
 * ACL entry entry_d to the value referred to by tag_qualifier_p
 */
int
acl_set_qualifier(acl_entry_t entry_d, const void *tag_qualifier_p)
{

	if (!entry_d || !tag_qualifier_p) {
		errno = EINVAL;
		return (-1);
	}
	switch(entry_d->ae_tag) {
	case ACL_USER:
	case ACL_GROUP:
		entry_d->ae_id = *(uid_t *)tag_qualifier_p;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	return (0);
}

/*
 * acl_set_tag_type() sets the tag type for ACL entry entry_d to the
 * value of tag_type
 */
int
acl_set_tag_type(acl_entry_t entry_d, acl_tag_t tag_type)
{

	if (entry_d == NULL) {
		errno = EINVAL;
		return (-1);
	}

	switch(tag_type) {
	case ACL_OTHER:
	case ACL_MASK:
		if (!_entry_brand_may_be(entry_d, ACL_BRAND_POSIX)) {
			errno = EINVAL;
			return (-1);
		}
		_entry_brand_as(entry_d, ACL_BRAND_POSIX);
		break;
	case ACL_EVERYONE:
		if (!_entry_brand_may_be(entry_d, ACL_BRAND_NFS4)) {
			errno = EINVAL;
			return (-1);
		}
		_entry_brand_as(entry_d, ACL_BRAND_NFS4);
		break;
	}

	switch(tag_type) {
	case ACL_USER_OBJ:
	case ACL_USER:
	case ACL_GROUP_OBJ:
	case ACL_GROUP:
	case ACL_MASK:
	case ACL_OTHER:
	case ACL_EVERYONE:
		entry_d->ae_tag = tag_type;
		return (0);
	}

	errno = EINVAL;
	return (-1);
}

int
acl_set_entry_type_np(acl_entry_t entry_d, acl_entry_type_t entry_type)
{

	if (entry_d == NULL) {
		errno = EINVAL;
		return (-1);
	}
	if (!_entry_brand_may_be(entry_d, ACL_BRAND_NFS4)) {
		errno = EINVAL;
		return (-1);
	}
	_entry_brand_as(entry_d, ACL_BRAND_NFS4);

	switch (entry_type) {
	case ACL_ENTRY_TYPE_ALLOW:
	case ACL_ENTRY_TYPE_DENY:
	case ACL_ENTRY_TYPE_AUDIT:
	case ACL_ENTRY_TYPE_ALARM:
		entry_d->ae_entry_type = entry_type;
		return (0);
	}

	errno = EINVAL;
	return (-1);
}
