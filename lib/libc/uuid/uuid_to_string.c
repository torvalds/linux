/*	$OpenBSD: uuid_to_string.c,v 1.2 2015/09/10 18:13:46 guenther Exp $	*/
/*	$NetBSD: uuid_to_string.c,v 1.2 2008/04/23 07:52:32 plunky Exp $	*/

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
 * $FreeBSD: src/lib/libc/uuid/uuid_to_string.c,v 1.2 2003/08/08 19:18:43 marcel Exp $
 */

#include <stdio.h>
#include <string.h>
#include <uuid.h>

/*
 * uuid_to_string() - Convert a binary UUID into a string representation.
 * See also:
 *	http://www.opengroup.org/onlinepubs/009629399/uuid_to_string.htm
 *
 * NOTE: The references given above do not have a status code for when
 *	 the string could not be allocated. The status code has been
 *	 taken from the Hewlett-Packard implementation.
 */
void
uuid_to_string(const uuid_t *u, char **s, uint32_t *status)
{
	int c;
	static const uuid_t nil = { .time_low = 0 };

	if (status != NULL)
		*status = uuid_s_ok;

	/* Why allow a NULL-pointer here? */
	if (s == NULL)
		return;

	if (u == NULL)
		u = &nil;

	c = asprintf(s, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	    u->time_low, u->time_mid, u->time_hi_and_version,
	    u->clock_seq_hi_and_reserved, u->clock_seq_low, u->node[0],
	    u->node[1], u->node[2], u->node[3], u->node[4], u->node[5]);

	if (c == -1 && status != NULL)
		*status = uuid_s_no_memory;
}
