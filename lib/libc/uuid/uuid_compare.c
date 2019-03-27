/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002,2005 Marcel Moolenaar
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

#include <string.h>
#include <uuid.h>

/* A macro used to improve the readability of uuid_compare(). */
#define DIFF_RETURN(a, b, field)	do {			\
	if ((a)->field != (b)->field)				\
		return (((a)->field < (b)->field) ? -1 : 1);	\
} while (0)

/*
 * uuid_compare() - compare two UUIDs.
 * See also:
 *	http://www.opengroup.org/onlinepubs/009629399/uuid_compare.htm
 *
 * NOTE: Either UUID can be NULL, meaning a nil UUID. nil UUIDs are smaller
 *	 than any non-nil UUID.
 */
int32_t
uuid_compare(const uuid_t *a, const uuid_t *b, uint32_t *status)
{
	int	res;

	if (status != NULL)
		*status = uuid_s_ok;

	/* Deal with NULL or equal pointers. */
	if (a == b)
		return (0);
	if (a == NULL)
		return ((uuid_is_nil(b, NULL)) ? 0 : -1);
	if (b == NULL)
		return ((uuid_is_nil(a, NULL)) ? 0 : 1);

	/* We have to compare the hard way. */
	DIFF_RETURN(a, b, time_low);
	DIFF_RETURN(a, b, time_mid);
	DIFF_RETURN(a, b, time_hi_and_version);
	DIFF_RETURN(a, b, clock_seq_hi_and_reserved);
	DIFF_RETURN(a, b, clock_seq_low);

	res = memcmp(a->node, b->node, sizeof(a->node));
	if (res)
		return ((res < 0) ? -1 : 1);
	return (0);
}

#undef DIFF_RETURN
