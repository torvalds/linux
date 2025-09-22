/*	$OpenBSD: min_heap.h,v 1.6 2019/04/29 17:11:52 tobias Exp $	*/

/*
 * Copyright (c) 2006 Maxim Yegorushkin <maxim.yegorushkin@gmail.com>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _MIN_HEAP_H_
#define _MIN_HEAP_H_

#include "event.h"

typedef struct min_heap {
	struct event **p;
	size_t n, a;
} min_heap_t;

static inline void min_heap_ctor(min_heap_t * s);
static inline void min_heap_dtor(min_heap_t * s);
static inline void min_heap_elem_init(struct event * e);
static inline int min_heap_elem_greater(struct event * a, struct event * b);
static inline int min_heap_empty(min_heap_t * s);
static inline size_t min_heap_size(min_heap_t * s);
static inline struct event *min_heap_top(min_heap_t * s);
static inline int min_heap_reserve(min_heap_t * s, size_t n);
static inline int min_heap_push(min_heap_t * s, struct event * e);
static inline struct event *min_heap_pop(min_heap_t * s);
static inline int min_heap_erase(min_heap_t * s, struct event * e);
static inline void min_heap_shift_up_(min_heap_t * s, size_t hole_index, struct event * e);
static inline void min_heap_shift_down_(min_heap_t * s, size_t hole_index, struct event * e);

int 
min_heap_elem_greater(struct event * a, struct event * b)
{
	return timercmp(&a->ev_timeout, &b->ev_timeout, >);
}

void 
min_heap_ctor(min_heap_t * s)
{
	s->p = 0;
	s->n = 0;
	s->a = 0;
}
void min_heap_dtor(min_heap_t * s) {
	if (s->p)
		free(s->p);
}
void 
min_heap_elem_init(struct event * e)
{
	e->min_heap_idx = SIZE_MAX;
}
int 
min_heap_empty(min_heap_t * s)
{
	return 0 == s->n;
}
size_t 
min_heap_size(min_heap_t * s)
{
	return s->n;
}
struct event *
min_heap_top(min_heap_t * s)
{
	return s->n ? *s->p : 0;
}

int 
min_heap_push(min_heap_t * s, struct event * e)
{
	if (min_heap_reserve(s, s->n + 1))
		return -1;
	min_heap_shift_up_(s, s->n++, e);
	return 0;
}

struct event *
min_heap_pop(min_heap_t * s)
{
	if (s->n) {
		struct event *e = *s->p;
		min_heap_shift_down_(s, 0, s->p[--s->n]);
		e->min_heap_idx = SIZE_MAX;
		return e;
	}
	return 0;
}

int 
min_heap_erase(min_heap_t * s, struct event * e)
{
	if (e->min_heap_idx != SIZE_MAX) {
		struct event *last = s->p[--s->n];
		size_t parent = (e->min_heap_idx - 1) / 2;
		/*
		 * we replace e with the last element in the heap.  We might
		 * need to shift it upward if it is less than its parent, or
		 * downward if it is greater than one or both its children.
		 * Since the children are known to be less than the parent,
		 * it can't need to shift both up and down.
		 */
		if (e->min_heap_idx > 0 && min_heap_elem_greater(s->p[parent], last))
			min_heap_shift_up_(s, e->min_heap_idx, last);
		else
			min_heap_shift_down_(s, e->min_heap_idx, last);
		e->min_heap_idx = SIZE_MAX;
		return 0;
	}
	return -1;
}

int 
min_heap_reserve(min_heap_t * s, size_t n)
{
	if (s->a < n) {
		struct event **p;
		size_t a = s->a ? s->a * 2 : 8;
		if (a < n)
			a = n;
		if (!(p = recallocarray(s->p, s->a, a, sizeof *p)))
			return -1;
		s->p = p;
		s->a = a;
	}
	return 0;
}

void 
min_heap_shift_up_(min_heap_t * s, size_t hole_index, struct event * e)
{
	size_t parent = (hole_index - 1) / 2;
	while (hole_index && min_heap_elem_greater(s->p[parent], e)) {
		s->p[hole_index] = s->p[parent];
		s->p[hole_index]->min_heap_idx = hole_index;
		hole_index = parent;
		parent = (hole_index - 1) / 2;
	}
	e->min_heap_idx = hole_index;
	s->p[hole_index] = e;
}

void 
min_heap_shift_down_(min_heap_t * s, size_t hole_index, struct event * e)
{
	size_t min_child = 2 * (hole_index + 1);
	while (min_child <= s->n) {
		if (min_child == s->n ||
		    min_heap_elem_greater(s->p[min_child], s->p[min_child - 1]))
			min_child -= 1;
		if (!(min_heap_elem_greater(e, s->p[min_child])))
			break;
		s->p[hole_index] = s->p[min_child];
		s->p[hole_index]->min_heap_idx = hole_index;
		hole_index = min_child;
		min_child = 2 * (hole_index + 1);
	}
	min_heap_shift_up_(s, hole_index, e);
}
#endif				/* _MIN_HEAP_H_ */
