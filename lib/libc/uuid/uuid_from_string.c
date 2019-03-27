/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 * $FreeBSD$
 */

#include <stdio.h>
#include <string.h>
#include <uuid.h>

/*
 * uuid_from_string() - convert a string representation of an UUID into
 *			a binary representation.
 * See also:
 *	http://www.opengroup.org/onlinepubs/009629399/uuid_from_string.htm
 *
 * NOTE: The sequence field is in big-endian, while the time fields are in
 *	 native byte order.
 */
void
uuid_from_string(const char *s, uuid_t *u, uint32_t *status)
{
	int n;

	/* Short-circuit 2 special cases: NULL pointer and empty string. */
	if (s == NULL || *s == '\0') {
		uuid_create_nil(u, status);
		return;
	}

	/* Assume the worst. */
	if (status != NULL)
		*status = uuid_s_invalid_string_uuid;

	/* The UUID string representation has a fixed length. */
	if (strlen(s) != 36)
		return;

	/*
	 * We only work with "new" UUIDs. New UUIDs have the form:
	 *	01234567-89ab-cdef-0123-456789abcdef
	 * The so called "old" UUIDs, which we don't support, have the form:
	 *	0123456789ab.cd.ef.01.23.45.67.89.ab
	 */
	if (s[8] != '-')
		return;

	n = sscanf(s,
	    "%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
	    &u->time_low, &u->time_mid, &u->time_hi_and_version,
	    &u->clock_seq_hi_and_reserved, &u->clock_seq_low, &u->node[0],
	    &u->node[1], &u->node[2], &u->node[3], &u->node[4], &u->node[5]);

	/* Make sure we have all conversions. */
	if (n != 11)
		return;

	/* We have a successful scan. Check semantics... */
	n = u->clock_seq_hi_and_reserved;
	if ((n & 0x80) != 0x00 &&			/* variant 0? */
	    (n & 0xc0) != 0x80 &&			/* variant 1? */
	    (n & 0xe0) != 0xc0) {			/* variant 2? */
		if (status != NULL)
			*status = uuid_s_bad_version;
	} else {
		if (status != NULL)
			*status = uuid_s_ok;
	}
}
