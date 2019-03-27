/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2001, 2008 Robert N. M. Watson
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
 * Support functionality for the POSIX.1e ACL interface
 * These calls are intended only to be called within the library.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include "namespace.h"
#include <sys/acl.h>
#include "un-namespace.h"
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "acl_support.h"

/*
 * Given a uid/gid, return a username/groupname for the text form of an ACL.
 * Note that we truncate user and group names, rather than error out, as
 * this is consistent with other tools manipulating user and group names.
 * XXX NOT THREAD SAFE, RELIES ON GETPWUID, GETGRGID
 * XXX USES *PW* AND *GR* WHICH ARE STATEFUL AND THEREFORE THIS ROUTINE
 * MAY HAVE SIDE-EFFECTS
 */
int
_posix1e_acl_id_to_name(acl_tag_t tag, uid_t id, ssize_t buf_len, char *buf,
    int flags)
{
	struct group	*g;
	struct passwd	*p;
	int	i;

	switch(tag) {
	case ACL_USER:
		if (flags & ACL_TEXT_NUMERIC_IDS)
			p = NULL;
		else
			p = getpwuid(id);
		if (!p)
			i = snprintf(buf, buf_len, "%d", id);
		else
			i = snprintf(buf, buf_len, "%s", p->pw_name);

		if (i < 0) {
			errno = ENOMEM;
			return (-1);
		}
		return (0);

	case ACL_GROUP:
		if (flags & ACL_TEXT_NUMERIC_IDS)
			g = NULL;
		else
			g = getgrgid(id);
		if (g == NULL)
			i = snprintf(buf, buf_len, "%d", id);
		else
			i = snprintf(buf, buf_len, "%s", g->gr_name);

		if (i < 0) {
			errno = ENOMEM;
			return (-1);
		}
		return (0);

	default:
		return (EINVAL);
	}
}
