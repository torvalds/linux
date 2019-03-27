/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2002 Robert N. M. Watson
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
 * acl_to_text - return a text string with a text representation of the acl
 * in it.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include "namespace.h"
#include <sys/acl.h>
#include "un-namespace.h"
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acl_support.h"

/*
 * acl_to_text - generate a text form of an acl
 * spec says nothing about output ordering, so leave in acl order
 *
 * This function will not produce nice results if it is called with
 * a non-POSIX.1e semantics ACL.
 */

char *_nfs4_acl_to_text_np(const acl_t acl, ssize_t *len_p, int flags);

static char *
_posix1e_acl_to_text(acl_t acl, ssize_t *len_p, int flags)
{
	struct acl	*acl_int;
	char		*buf, *tmpbuf;
	char		 name_buf[MAXLOGNAME];
	char		 perm_buf[_POSIX1E_ACL_STRING_PERM_MAXSIZE+1],
			 effective_perm_buf[_POSIX1E_ACL_STRING_PERM_MAXSIZE+1];
	int		 i, error, len;
	uid_t		 ae_id;
	acl_tag_t	 ae_tag;
	acl_perm_t	 ae_perm, effective_perm, mask_perm;

	buf = strdup("");
	if (buf == NULL)
		return(NULL);

	acl_int = &acl->ats_acl;

	mask_perm = ACL_PERM_BITS;	/* effective is regular if no mask */
	for (i = 0; i < acl_int->acl_cnt; i++)
		if (acl_int->acl_entry[i].ae_tag == ACL_MASK) 
			mask_perm = acl_int->acl_entry[i].ae_perm;

	for (i = 0; i < acl_int->acl_cnt; i++) {
		ae_tag = acl_int->acl_entry[i].ae_tag;
		ae_id = acl_int->acl_entry[i].ae_id;
		ae_perm = acl_int->acl_entry[i].ae_perm;

		switch(ae_tag) {
		case ACL_USER_OBJ:
			error = _posix1e_acl_perm_to_string(ae_perm,
			    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1, perm_buf);
			if (error)
				goto error_label;
			len = asprintf(&tmpbuf, "%suser::%s\n", buf,
			    perm_buf);
			if (len == -1)
				goto error_label;
			free(buf);
			buf = tmpbuf;
			break;

		case ACL_USER:
			error = _posix1e_acl_perm_to_string(ae_perm,
			    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1, perm_buf);
			if (error)
				goto error_label;

			error = _posix1e_acl_id_to_name(ae_tag, ae_id,
			    MAXLOGNAME, name_buf, flags);
			if (error)
				goto error_label;

			effective_perm = ae_perm & mask_perm;
			if (effective_perm != ae_perm) {
				error = _posix1e_acl_perm_to_string(
				    effective_perm,
				    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1,
				    effective_perm_buf);
				if (error)
					goto error_label;
				len = asprintf(&tmpbuf, "%suser:%s:%s\t\t# "
				    "effective: %s\n",
				    buf, name_buf, perm_buf,
				    effective_perm_buf);
			} else {
				len = asprintf(&tmpbuf, "%suser:%s:%s\n", buf,
				    name_buf, perm_buf);
			}
			if (len == -1)
				goto error_label;
			free(buf);
			buf = tmpbuf;
			break;

		case ACL_GROUP_OBJ:
			error = _posix1e_acl_perm_to_string(ae_perm,
			    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1, perm_buf);
			if (error)
				goto error_label;

			effective_perm = ae_perm & mask_perm;
			if (effective_perm != ae_perm) {
				error = _posix1e_acl_perm_to_string(
				    effective_perm,
				    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1,
				    effective_perm_buf);
				if (error)
					goto error_label;
				len = asprintf(&tmpbuf, "%sgroup::%s\t\t# "
				    "effective: %s\n",
				    buf, perm_buf, effective_perm_buf);
			} else {
				len = asprintf(&tmpbuf, "%sgroup::%s\n", buf,
				    perm_buf);
			}
			if (len == -1)
				goto error_label;
			free(buf);
			buf = tmpbuf;
			break;

		case ACL_GROUP:
			error = _posix1e_acl_perm_to_string(ae_perm,
			    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1, perm_buf);
			if (error)
				goto error_label;

			error = _posix1e_acl_id_to_name(ae_tag, ae_id,
			    MAXLOGNAME, name_buf, flags);
			if (error)
				goto error_label;

			effective_perm = ae_perm & mask_perm;
			if (effective_perm != ae_perm) {
				error = _posix1e_acl_perm_to_string(
				    effective_perm,
				    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1,
				    effective_perm_buf);
				if (error)
					goto error_label;
				len = asprintf(&tmpbuf, "%sgroup:%s:%s\t\t# "
				    "effective: %s\n",
				    buf, name_buf, perm_buf,
				    effective_perm_buf);
			} else {
				len = asprintf(&tmpbuf, "%sgroup:%s:%s\n", buf,
				    name_buf, perm_buf);
			}
			if (len == -1)
				goto error_label;
			free(buf);
			buf = tmpbuf;
			break;

		case ACL_MASK:
			error = _posix1e_acl_perm_to_string(ae_perm,
			    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1, perm_buf);
			if (error)
				goto error_label;

			len = asprintf(&tmpbuf, "%smask::%s\n", buf,
			    perm_buf);
			if (len == -1)
				goto error_label;
			free(buf);
			buf = tmpbuf;
			break;

		case ACL_OTHER:
			error = _posix1e_acl_perm_to_string(ae_perm,
			    _POSIX1E_ACL_STRING_PERM_MAXSIZE+1, perm_buf);
			if (error)
				goto error_label;

			len = asprintf(&tmpbuf, "%sother::%s\n", buf,
			    perm_buf);
			if (len == -1)
				goto error_label;
			free(buf);
			buf = tmpbuf;
			break;

		default:
			errno = EINVAL;
			goto error_label;
		}
	}

	if (len_p) {
		*len_p = strlen(buf);
	}
	return (buf);

error_label:
	/* jump to here sets errno already, we just clean up */
	if (buf) free(buf);
	return (NULL);
}

char *
acl_to_text_np(acl_t acl, ssize_t *len_p, int flags)
{

	if (acl == NULL) {
		errno = EINVAL;
		return(NULL);
	}

	switch (_acl_brand(acl)) {
	case ACL_BRAND_POSIX:
		return (_posix1e_acl_to_text(acl, len_p, flags));
	case ACL_BRAND_NFS4:
		return (_nfs4_acl_to_text_np(acl, len_p, flags));
	default:
		errno = EINVAL;
		return (NULL);
	}
}

char *
acl_to_text(acl_t acl, ssize_t *len_p)
{

	return (acl_to_text_np(acl, len_p, 0));
}
