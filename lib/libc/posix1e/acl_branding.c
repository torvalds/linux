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

#include <assert.h>
#include <errno.h>
#include <sys/acl.h>

#include "acl_support.h"

/*
 * An ugly detail of the implementation - fortunately not visible
 * to the API users - is the "branding": libc needs to keep track
 * of what "brand" ACL is: NFSv4, POSIX.1e or unknown.  It happens
 * automatically - for example, during acl_get_file(3) ACL gets
 * branded according to the "type" argument; during acl_set_permset
 * ACL, if its brand is unknown it gets branded as NFSv4 if any of the
 * NFSv4 permissions that are not valid for POSIX.1e ACL are set etc.
 * Branding information is used for printing out the ACL (acl_to_text(3)),
 * veryfying acl_set_whatever arguments (checking against setting
 * bits that are valid only for NFSv4 in ACL branded as POSIX.1e) etc.
 */

static acl_t
entry2acl(acl_entry_t entry)
{
	acl_t aclp;

	aclp = (acl_t)(((long)entry >> _ACL_T_ALIGNMENT_BITS) << _ACL_T_ALIGNMENT_BITS);

	return (aclp);
}

/*
 * Return brand of an ACL.
 */
int
_acl_brand(const acl_t acl)
{

	return (acl->ats_brand);
}

int
_entry_brand(const acl_entry_t entry)
{

	return (_acl_brand(entry2acl(entry)));
}

/*
 * Return 1, iff branding ACL as "brand" is ok.
 */
int
_acl_brand_may_be(const acl_t acl, int brand)
{

	if (_acl_brand(acl) == ACL_BRAND_UNKNOWN)
		return (1);

	if (_acl_brand(acl) == brand)
		return (1);

	return (0);
}

int
_entry_brand_may_be(const acl_entry_t entry, int brand)
{

	return (_acl_brand_may_be(entry2acl(entry), brand));
}

/*
 * Brand ACL as "brand".
 */
void
_acl_brand_as(acl_t acl, int brand)
{

	assert(_acl_brand_may_be(acl, brand));

	acl->ats_brand = brand;
}

void
_entry_brand_as(const acl_entry_t entry, int brand)
{

	_acl_brand_as(entry2acl(entry), brand);
}

int
_acl_type_not_valid_for_acl(const acl_t acl, acl_type_t type)
{

	switch (_acl_brand(acl)) {
	case ACL_BRAND_NFS4:
		if (type == ACL_TYPE_NFS4)
			return (0);
		break;

	case ACL_BRAND_POSIX:
		if (type == ACL_TYPE_ACCESS || type == ACL_TYPE_DEFAULT)
			return (0);
		break;

	case ACL_BRAND_UNKNOWN:
		return (0);
	}

	return (-1);
}

void
_acl_brand_from_type(acl_t acl, acl_type_t type)
{

	switch (type) {
	case ACL_TYPE_NFS4:
		_acl_brand_as(acl, ACL_BRAND_NFS4);
		break;
	case ACL_TYPE_ACCESS:
	case ACL_TYPE_DEFAULT:
		_acl_brand_as(acl, ACL_BRAND_POSIX);
		break;
	default:
		/* XXX: What to do here? */
		break;
	}
}

int
acl_get_brand_np(acl_t acl, int *brand_p)
{

	if (acl == NULL || brand_p == NULL) {
		errno = EINVAL;
		return (-1);
	}
	*brand_p = _acl_brand(acl);

	return (0);
}
