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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <stdio.h>

#include "acl_support.h"

/*
 * acl_calc_mask() (23.4.2): calculate and set the permissions
 * associated with the ACL_MASK ACL entry.  If the ACL already
 * contains an ACL_MASK entry, its permissions shall be
 * overwritten; if not, one shall be added.
 */
int
acl_calc_mask(acl_t *acl_p)
{
	struct acl	*acl_int, *acl_int_new;
	acl_t		acl_new;
	int		i, mask_mode, mask_num;

	/*
	 * (23.4.2.4) requires acl_p to point to a pointer to a valid ACL.
	 * Since one of the primary reasons to use this function would be
	 * to calculate the appropriate mask to obtain a valid ACL, we only
	 * perform sanity checks here and validate the ACL prior to
	 * returning.
	 */
	if (acl_p == NULL || *acl_p == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (!_acl_brand_may_be(*acl_p, ACL_BRAND_POSIX)) {
		errno = EINVAL;
		return (-1);
	}
	_acl_brand_as(*acl_p, ACL_BRAND_POSIX);

	acl_int = &(*acl_p)->ats_acl;
	if ((acl_int->acl_cnt < 3) || (acl_int->acl_cnt > ACL_MAX_ENTRIES)) {
		errno = EINVAL;
		return (-1);
	}

	acl_new = acl_dup(*acl_p);
	if (acl_new == NULL)
		return (-1);
	acl_int_new = &acl_new->ats_acl;

	mask_mode = 0;
	mask_num = -1;

	/* gather permissions and find a mask entry */
	for (i = 0; i < acl_int_new->acl_cnt; i++) {
		switch(acl_int_new->acl_entry[i].ae_tag) {
		case ACL_USER:
		case ACL_GROUP:
		case ACL_GROUP_OBJ:
			mask_mode |=
			    acl_int_new->acl_entry[i].ae_perm & ACL_PERM_BITS;
			break;
		case ACL_MASK:
			mask_num = i;
			break;
		}
	}

	/* if a mask entry already exists, overwrite the perms */
	if (mask_num != -1)
		acl_int_new->acl_entry[mask_num].ae_perm = mask_mode;
	else {
		/* if no mask exists, check acl_cnt... */
		if (acl_int_new->acl_cnt == ACL_MAX_ENTRIES) {
			errno = ENOMEM;
			acl_free(acl_new);
			return (-1);
		}
		/* ...and add the mask entry */
		acl_int_new->acl_entry[acl_int_new->acl_cnt].ae_tag = ACL_MASK;
		acl_int_new->acl_entry[acl_int_new->acl_cnt].ae_id =
		    ACL_UNDEFINED_ID;
		acl_int_new->acl_entry[acl_int_new->acl_cnt].ae_perm =
		    mask_mode;
		acl_int_new->acl_cnt++;
	}

	if (acl_valid(acl_new) == -1) {
		errno = EINVAL;
		acl_free(acl_new);
		return (-1);
	}

	**acl_p = *acl_new;
	acl_free(acl_new);

	return (0);
}
