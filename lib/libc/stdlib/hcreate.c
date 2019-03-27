/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <search.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Thread unsafe interface: use a single process-wide hash table and
 * forward calls to *_r() functions.
 */

static struct hsearch_data global_hashtable;
static bool global_hashtable_initialized = false;

int
hcreate(size_t nel)
{

	return (1);
}

void
hdestroy(void)
{

	/* Destroy global hash table if present. */
	if (global_hashtable_initialized) {
		hdestroy_r(&global_hashtable);
		global_hashtable_initialized = false;
	}
}

ENTRY *
hsearch(ENTRY item, ACTION action)
{
	ENTRY *retval;

	/* Create global hash table if needed. */
	if (!global_hashtable_initialized) {
		if (hcreate_r(0, &global_hashtable) == 0)
			return (NULL);
		global_hashtable_initialized = true;
	}
	if (hsearch_r(item, action, &retval, &global_hashtable) == 0)
		return (NULL);
	return (retval);
}
