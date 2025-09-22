/*	$OpenBSD: uuid_create.c,v 1.3 2022/11/11 12:05:32 krw Exp $	*/
/*	$NetBSD: uuid_create.c,v 1.1 2004/09/13 21:44:54 thorpej Exp $	*/

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
 * $FreeBSD: src/lib/libc/uuid/uuid_create.c,v 1.2 2003/08/08 19:18:43 marcel Exp $
 */

#include <stdlib.h>
#include <uuid.h>

/*
 * uuid_create() - create an UUID.
 * See also:
 *	http://www.opengroup.org/onlinepubs/009629399/uuid_create.htm and
 *	RFC 4122
 *
 * Create a UUID from random number as defined in section 4.4 of RFC 4122
 */
void
uuid_create(uuid_t *u, uint32_t *status)
{
	arc4random_buf(u, sizeof(uuid_t));

	u->clock_seq_hi_and_reserved &= ~(1 << 6);
	u->clock_seq_hi_and_reserved |= (1 << 7);

	u->time_hi_and_version &= ~(1 << 12);
	u->time_hi_and_version &= ~(1 << 13);
	u->time_hi_and_version |= (1 << 14);
	u->time_hi_and_version &= ~(1 << 15);

	if (status)
		*status = uuid_s_ok;
}
