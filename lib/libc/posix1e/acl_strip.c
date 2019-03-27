/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Chris D. Faulhaber
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

#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <sys/acl.h>
#include <sys/stat.h>

#include "acl_support.h"

/*
 * These routines from sys/kern/subr_acl_nfs4.c are used by both kernel
 * and libc.
 */
void	acl_nfs4_sync_mode_from_acl(mode_t *_mode, const struct acl *aclp);
void	acl_nfs4_trivial_from_mode_libc(struct acl *aclp, int file_owner_id,
	    int canonical_six);

static acl_t
_nfs4_acl_strip_np(const acl_t aclp, int canonical_six)
{
	acl_t newacl;
	mode_t mode = 0;

	newacl = acl_init(ACL_MAX_ENTRIES);
	if (newacl == NULL) {
		errno = ENOMEM;
		return (NULL);
	}

	_acl_brand_as(newacl, ACL_BRAND_NFS4);

	acl_nfs4_sync_mode_from_acl(&mode, &(aclp->ats_acl));
	acl_nfs4_trivial_from_mode_libc(&(newacl->ats_acl), mode, canonical_six);

	return (newacl);
}

static acl_t
_posix1e_acl_strip_np(const acl_t aclp, int recalculate_mask)
{
	acl_t acl_new, acl_old;
	acl_entry_t entry, entry_new;
	acl_tag_t tag;
	int entry_id, have_mask_entry;

	assert(_acl_brand(aclp) == ACL_BRAND_POSIX);

	acl_old = acl_dup(aclp);
	if (acl_old == NULL)
		return (NULL);

	assert(_acl_brand(acl_old) == ACL_BRAND_POSIX);

	have_mask_entry = 0;
	acl_new = acl_init(ACL_MAX_ENTRIES);
	if (acl_new == NULL) {
		acl_free(acl_old);
		return (NULL);
	}
	tag = ACL_UNDEFINED_TAG;

	/* only save the default user/group/other entries */
	entry_id = ACL_FIRST_ENTRY;
	while (acl_get_entry(acl_old, entry_id, &entry) == 1) {
		entry_id = ACL_NEXT_ENTRY;

		assert(_entry_brand(entry) == ACL_BRAND_POSIX);

		if (acl_get_tag_type(entry, &tag) == -1)
			goto fail;

		switch(tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_OTHER:
			if (acl_create_entry(&acl_new, &entry_new) == -1)
				goto fail;
			if (acl_copy_entry(entry_new, entry) == -1)
				goto fail;
			assert(_entry_brand(entry_new) == ACL_BRAND_POSIX);
			break;
		case ACL_MASK:
			have_mask_entry = 1;
			break;
		default:
			break;
		}
	}

	assert(_acl_brand(acl_new) == ACL_BRAND_POSIX);

	if (have_mask_entry && recalculate_mask) {
		if (acl_calc_mask(&acl_new) == -1)
			goto fail;
	}

	return (acl_new);

fail:
	acl_free(acl_new);
	acl_free(acl_old);

	return (NULL);
}

acl_t
acl_strip_np(const acl_t aclp, int recalculate_mask)
{
	switch (_acl_brand(aclp)) {
	case ACL_BRAND_NFS4:
		return (_nfs4_acl_strip_np(aclp, 0));

	case ACL_BRAND_POSIX:
		return (_posix1e_acl_strip_np(aclp, recalculate_mask));

	default:
		errno = EINVAL;
		return (NULL);
	}
}

/*
 * Return 1, if ACL is trivial, 0 otherwise.
 *
 * ACL is trivial, iff its meaning could be fully expressed using just file
 * mode.  In other words, ACL is trivial iff it doesn't have "+" to the right
 * of the mode bits in "ls -l" output ;-)
 */
int
acl_is_trivial_np(const acl_t aclp, int *trivialp)
{
	acl_t tmpacl;
	int differs;

	if (aclp == NULL || trivialp == NULL) {
		errno = EINVAL;
		return (-1);
	}

	switch (_acl_brand(aclp)) {
	case ACL_BRAND_POSIX:
		if (aclp->ats_acl.acl_cnt == 3)
			*trivialp = 1;
		else
			*trivialp = 0;

		return (0);

	case ACL_BRAND_NFS4:
		/*
		 * If the ACL has more than canonical six entries,
		 * it's non trivial by definition.
		 */
		if (aclp->ats_acl.acl_cnt > 6) {
			*trivialp = 0;
			return (0);
		}
			
		/*
		 * Calculate trivial ACL - using acl_strip_np(3) - and compare
		 * with the original.
		 */
		tmpacl = _nfs4_acl_strip_np(aclp, 0);
		if (tmpacl == NULL)
			return (-1);

		differs = _acl_differs(aclp, tmpacl);
		acl_free(tmpacl);

		if (differs == 0) {
			*trivialp = 1;
			return (0);
		}

		/*
		 * Try again with an old-style, "canonical six" trivial ACL.
		 */
		tmpacl = _nfs4_acl_strip_np(aclp, 1);
		if (tmpacl == NULL)
			return (-1);

		differs = _acl_differs(aclp, tmpacl);
		acl_free(tmpacl);

		if (differs)
			*trivialp = 0;
		else
			*trivialp = 1;

		return (0);

	default:
		errno = EINVAL;
		return (-1);
	}
}
