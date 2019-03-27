/*-
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
#include <stdlib.h>

#include "hsearch.h"

int
hcreate_r(size_t nel, struct hsearch_data *htab)
{
	struct __hsearch *hsearch;

	/*
	 * Allocate a hash table object. Ignore the provided hint and start
	 * off with a table of sixteen entries. In most cases this hint is
	 * just a wild guess. Resizing the table dynamically if the use
	 * increases a threshold does not affect the worst-case running time.
	 */
	hsearch = malloc(sizeof(*hsearch));
	if (hsearch == NULL)
		return 0;
	hsearch->entries = calloc(16, sizeof(ENTRY));
	if (hsearch->entries == NULL) {
		free(hsearch);
		return 0;
	}

	/*
	 * Pick a random initialization for the FNV-1a hashing. This makes it
	 * hard to come up with a fixed set of keys to force hash collisions.
	 */
	arc4random_buf(&hsearch->offset_basis, sizeof(hsearch->offset_basis));
	hsearch->index_mask = 0xf;
	hsearch->entries_used = 0;
	htab->__hsearch = hsearch;
	return 1;
}
