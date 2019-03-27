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
 * Note: some comments taken from lib/libc/uuid/uuid_to_string.c
 * Copyright (c) 2002,2005 Marcel Moolenaar
 * Copyright (c) 2002 Hiten Mahesh Pandya
 */

#include <stand.h>
#include <uuid.h>

/*
 * Dump len characters into *buf from val as hex and update *buf
 */
static void
tohex(char **buf, int len, uint32_t val)
{
	static const char *hexstr = "0123456789abcdef";
	char *walker = *buf;
	int i;

	for (i = len - 1; i >= 0; i--) {
		walker[i] = hexstr[val & 0xf];
		val >>= 4;
	}
	*buf = walker + len;
}

/*
 * uuid_to_string() - Convert a binary UUID into a string representation.
 * See also:
 *	http://www.opengroup.org/onlinepubs/009629399/uuid_to_string.htm
 *
 * NOTE: The references given above do not have a status code for when
 *	 the string could not be allocated. The status code has been
 *	 taken from the Hewlett-Packard implementation.
 *
 * NOTE: we don't support u == NULL for a nil UUID, sorry.
 *
 * NOTE: The sequence field is in big-endian, while the time fields are in
 *	 native byte order.
 *
 *	 hhhhhhhh-hhhh-hhhh-bbbb-bbbbbbbbbbbb
 *	 01234567-89ab-cdef-0123-456789abcdef
 */
void
uuid_to_string(const uuid_t *u, char **s, uint32_t *status)
{
	uuid_t nil;
	char *w;

	if (status != NULL)
		*status = uuid_s_ok;
	if (s == NULL)	/* Regular version does this odd-ball behavior too */
		return;
	w = *s = malloc(37);
	if (*s == NULL) {
		if (status != NULL)
			*status = uuid_s_no_memory;
		return;
	}
	if (u == NULL) {
		u = &nil;
		uuid_create_nil(&nil, NULL);
	}
	/* native */
	tohex(&w, 8, u->time_low);
	*w++ = '-';
	tohex(&w, 4, u->time_mid);
	*w++ = '-';
	tohex(&w, 4, u->time_hi_and_version);
	*w++ = '-';
	/* Big endian, so do a byte at a time */
	tohex(&w, 2, u->clock_seq_hi_and_reserved);
	tohex(&w, 2, u->clock_seq_low);
	*w++ = '-';
	tohex(&w, 2, u->node[0]);
	tohex(&w, 2, u->node[1]);
	tohex(&w, 2, u->node[2]);
	tohex(&w, 2, u->node[3]);
	tohex(&w, 2, u->node[4]);
	tohex(&w, 2, u->node[5]);
	*w++ = '\0';
}
