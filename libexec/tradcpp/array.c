/*-
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>

#define ARRAYINLINE
#include "array.h"

struct array *
array_create(void)
{
	struct array *a;

	a = domalloc(sizeof(*a));
	array_init(a);
	return a;
}

void
array_destroy(struct array *a)
{
	array_cleanup(a);
	dofree(a, sizeof(*a));
}

void
array_init(struct array *a)
{
	a->num = a->max = 0;
	a->v = NULL;
}

void
array_cleanup(struct array *a)
{
	arrayassert(a->num == 0);
	dofree(a->v, a->max * sizeof(a->v[0]));
#ifdef ARRAYS_CHECKED
	a->v = NULL;
#endif
}

void
array_setsize(struct array *a, unsigned num)
{
	unsigned newmax;
	void **newptr;

	if (num > a->max) {
		newmax = a->max;
		while (num > newmax) {
			newmax = newmax ? newmax*2 : 4;
		}
		newptr = dorealloc(a->v, a->max * sizeof(a->v[0]),
				   newmax * sizeof(a->v[0]));
		a->v = newptr;
		a->max = newmax;
	}
	a->num = num;
}

void
array_insert(struct array *a, unsigned index_)
{
	unsigned movers;

	arrayassert(a->num <= a->max);
	arrayassert(index_ < a->num);

	movers = a->num - index_;

	array_setsize(a, a->num + 1);

	memmove(a->v + index_+1, a->v + index_, movers*sizeof(*a->v));
}

void
array_remove(struct array *a, unsigned index_)
{
	unsigned movers;

	arrayassert(a->num <= a->max);
	arrayassert(index_ < a->num);

	movers = a->num - (index_ + 1);
	memmove(a->v + index_, a->v + index_+1, movers*sizeof(*a->v));
	a->num--;
}
