/*-
 * Copyright (c) 2015 M. Warner Losh
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

/*
 * Note: some comments taken from lib/libc/uuid/uuid_from_string.c
 * Copyright (c) 2002 Marcel Moolenaar
 * Copyright (c) 2002 Hiten Mahesh Pandya
 */


#include <stand.h>
#include <uuid.h>

static int
hex2int(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return 10 + ch - 'a';
	if (ch >= 'A' && ch <= 'F')
		return 10 + ch - 'A';
	return 16;
}

static uint32_t
fromhex(const char *s, int len, int *ok)
{
	uint32_t v;
	int i, h;

	if (!*ok)
		return 0;
	v = 0;
	for (i = 0; i < len; i++) {
		h = hex2int(s[i]);
		if (h == 16) {
			*ok = 0;
			return v;
		}
		v = (v << 4) | h;
	}
	return v;
}

/*
 * uuid_from_string() - convert a string representation of an UUID into
 *			a binary representation.
 * See also:
 *	http://www.opengroup.org/onlinepubs/009629399/uuid_from_string.htm
 *
 * NOTE: The sequence field is in big-endian, while the time fields are in
 *	 native byte order.
 *
 * 01234567-89ab-cdef-0123-456789abcdef
 * 000000000011111111112222222222333333
 * 012345678901234567890123456789012345
 *         -    -    -    -
 * hhhhhhhh-hhhh-hhhh-bbbb-bbbbbbbbbbbb
 *
 */
void
uuid_from_string(const char *s, uuid_t *u, uint32_t *status)
{
	int ok = 1;
	int n;

	if (s == NULL || *s == '\0') {
		uuid_create_nil(u, status);
		return;
	}

	if (status != NULL)
		*status = uuid_s_invalid_string_uuid;
	if (strlen(s) != 36)
		return;
	/* Only support new format, check for all the right dashes */
	if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
		return;
	/* native byte order */
	u->time_low                  = fromhex(s     , 8, &ok);
	u->time_mid                  = fromhex(s +  9, 4, &ok);
	u->time_hi_and_version       = fromhex(s + 14, 4, &ok);
	/* Big endian, but presented as a whole number so decode as such */
	u->clock_seq_hi_and_reserved = fromhex(s + 19, 2, &ok);
	u->clock_seq_low             = fromhex(s + 21, 2, &ok);
	u->node[0]                   = fromhex(s + 24, 2, &ok);
	u->node[1]                   = fromhex(s + 26, 2, &ok);
	u->node[2]                   = fromhex(s + 28, 2, &ok);
	u->node[3]                   = fromhex(s + 30, 2, &ok);
	u->node[4]                   = fromhex(s + 32, 2, &ok);
	u->node[5]                   = fromhex(s + 34, 2, &ok);
	if (!ok)
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
