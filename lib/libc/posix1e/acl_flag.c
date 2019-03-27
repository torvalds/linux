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
#include <errno.h>
#include <sys/acl.h>

#include "acl_support.h"

static int
_flag_is_invalid(acl_flag_t flag)
{

	if ((flag & ACL_FLAGS_BITS) == flag)
		return (0);

	errno = EINVAL;

	return (1);
}

int
acl_add_flag_np(acl_flagset_t flagset_d, acl_flag_t flag)
{

	if (flagset_d == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (_flag_is_invalid(flag))
		return (-1);

	*flagset_d |= flag;

	return (0);
}

int
acl_clear_flags_np(acl_flagset_t flagset_d)
{

	if (flagset_d == NULL) {
		errno = EINVAL;
		return (-1);
	}

	*flagset_d = 0;

	return (0);
}

int
acl_delete_flag_np(acl_flagset_t flagset_d, acl_flag_t flag)
{

	if (flagset_d == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (_flag_is_invalid(flag))
		return (-1);

	*flagset_d &= ~flag;

	return (0);
}

int
acl_get_flag_np(acl_flagset_t flagset_d, acl_flag_t flag)
{

	if (flagset_d == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (_flag_is_invalid(flag))
		return (-1);

	if (*flagset_d & flag)
		return (1);

	return (0);
}

int
acl_get_flagset_np(acl_entry_t entry_d, acl_flagset_t *flagset_p)
{

	if (entry_d == NULL || flagset_p == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (!_entry_brand_may_be(entry_d, ACL_BRAND_NFS4)) {
		errno = EINVAL;
		return (-1);
	}

	*flagset_p = &entry_d->ae_flags;

	return (0);
}

int
acl_set_flagset_np(acl_entry_t entry_d, acl_flagset_t flagset_d)
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

	if (_flag_is_invalid(*flagset_d))
		return (-1);

	entry_d->ae_flags = *flagset_d;

	return (0);
}
