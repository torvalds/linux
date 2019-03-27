/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Chris D. Faulhaber
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

#include <sys/types.h>
#include "namespace.h"
#include <sys/acl.h>
#include "un-namespace.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "acl_support.h"

static int
_entry_matches(const acl_entry_t a, const acl_entry_t b)
{
	/*
	 * There is a semantical difference here between NFSv4 and POSIX
	 * draft ACLs.  In POSIX, there may be only one entry for the particular
	 * user or group.  In NFSv4 ACL, there may be any number of them.  We're
	 * trying to be more specific here in that case.
	 */
	switch (_entry_brand(a)) {
	case ACL_BRAND_NFS4:
		if (a->ae_tag != b->ae_tag || a->ae_entry_type != b->ae_entry_type)
			return (0);

		/* If ae_ids matter, compare them as well. */
		if (a->ae_tag == ACL_USER || a->ae_tag == ACL_GROUP) {
			if (a->ae_id != b->ae_id)
				return (0);
		}

		return (1);

	default:
		if ((a->ae_tag == b->ae_tag) && (a->ae_id == b->ae_id))
			return (1);
	}

	return (0);
}

/*
 * acl_delete_entry() (23.4.9): remove the ACL entry indicated by entry_d
 * from acl.
 */
int
acl_delete_entry(acl_t acl, acl_entry_t entry_d)
{
	struct acl *acl_int;
	struct acl_entry entry_int;
	int i, j, found = 0;

	if (acl == NULL || entry_d == NULL) {
		errno = EINVAL;
		return (-1);
	}

	acl_int = &acl->ats_acl;

	if (_entry_brand(entry_d) != _acl_brand(acl)) {
		errno = EINVAL;
		return (-1);
	}

	if ((acl->ats_acl.acl_cnt < 1) ||
	    (acl->ats_acl.acl_cnt > ACL_MAX_ENTRIES)) {
		errno = EINVAL;
		return (-1);
	}

	/* Use a local copy to prevent deletion of more than this entry */
	entry_int = *entry_d;

	for (i = 0; i < acl->ats_acl.acl_cnt;) {
		if (_entry_matches(&(acl->ats_acl.acl_entry[i]), &entry_int)) {
			/* ...shift the remaining entries... */
			for (j = i; j < acl->ats_acl.acl_cnt - 1; ++j)
				acl->ats_acl.acl_entry[j] =
				    acl->ats_acl.acl_entry[j+1];
			/* ...drop the count and zero the unused entry... */
			acl->ats_acl.acl_cnt--;
			bzero(&acl->ats_acl.acl_entry[j],
			    sizeof(struct acl_entry));
			acl->ats_cur_entry = 0;
			
			/* Continue with the loop to remove all matching entries. */
			found = 1;
		} else
			i++;
	}

	if (found)
		return (0);

	errno = EINVAL;
	return (-1);
}

int
acl_delete_entry_np(acl_t acl, int offset)
{
	struct acl *acl_int;
	int i;

	if (acl == NULL) {
		errno = EINVAL;
		return (-1);
	}

	acl_int = &acl->ats_acl;

	if (offset < 0 || offset >= acl_int->acl_cnt) {
		errno = EINVAL;
		return (-1);
	}

	if ((acl->ats_acl.acl_cnt < 1) ||
	    (acl->ats_acl.acl_cnt > ACL_MAX_ENTRIES)) {
		errno = EINVAL;
		return (-1);
	}

	/* ...shift the remaining entries... */
	for (i = offset; i < acl->ats_acl.acl_cnt - 1; ++i)
		acl->ats_acl.acl_entry[i] =
		    acl->ats_acl.acl_entry[i+1];
	/* ...drop the count and zero the unused entry... */
	acl->ats_acl.acl_cnt--;
	bzero(&acl->ats_acl.acl_entry[i],
	    sizeof(struct acl_entry));
	acl->ats_cur_entry = 0;

	return (0);
}
