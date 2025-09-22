/*	$OpenBSD: uuid_create_nil.c,v 1.3 2015/09/13 11:36:16 guenther Exp $	*/
/*	$NetBSD: uuid_create_nil.c,v 1.2 2005/02/09 21:35:47 kleink Exp $	*/

/*
 * Copyright (c) 2002 Marcel Moolenaar
 * Copyright (c) 2002 Hiten Mahesh Pandya
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
 *
 * $FreeBSD: src/lib/libc/uuid/uuid_create_nil.c,v 1.2 2003/08/08 19:18:43 marcel Exp $
 */

#include <string.h>
#include <uuid.h>

/*
 * uuid_create_nil() - create a nil UUID.
 * See also:
 *	http://www.opengroup.org/onlinepubs/009629399/uuid_create_nil.htm
 */
void
uuid_create_nil(uuid_t *u, uint32_t *status)
{
	memset(u, 0, sizeof(*u));

	if (status)
		*status = uuid_s_ok;
}
DEF_WEAK(uuid_create_nil);
