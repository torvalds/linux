/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <ctf_impl.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>

/*
 * This module is used both during the normal operation of the kernel (i.e.
 * after kmem has been initialized) and during boot (before unix`_start has
 * been called).  kobj_alloc is able to tell the difference between the two
 * cases, and as such must be used instead of kmem_alloc.
 */

void *
ctf_data_alloc(size_t size)
{
	void *buf = kobj_alloc(size, KM_NOWAIT|KM_SCRATCH);

	if (buf == NULL)
		return (MAP_FAILED);

	return (buf);
}

void
ctf_data_free(void *buf, size_t size)
{
	kobj_free(buf, size);
}

/*ARGSUSED*/
void
ctf_data_protect(void *buf, size_t size)
{
	/* we don't support this operation in the kernel */
}

void *
ctf_alloc(size_t size)
{
	return (kobj_alloc(size, KM_NOWAIT|KM_TMP));
}

/*ARGSUSED*/
void
ctf_free(void *buf, size_t size)
{
	kobj_free(buf, size);
}

/*ARGSUSED*/
const char *
ctf_strerror(int err)
{
	return (NULL); /* we don't support this operation in the kernel */
}

/*PRINTFLIKE1*/
void
ctf_dprintf(const char *format, ...)
{
	if (_libctf_debug) {
		va_list alist;

		va_start(alist, format);
		(void) printf("ctf DEBUG: ");
		(void) vprintf(format, alist);
		va_end(alist);
	}
}
