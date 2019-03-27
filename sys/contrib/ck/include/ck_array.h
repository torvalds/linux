/*
 * Copyright 2013-2015 Samy Al Bahra
 * Copyright 2013-2014 AppNexus, Inc.
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
 */

#ifndef CK_ARRAY_H
#define CK_ARRAY_H

#include <ck_cc.h>
#include <ck_malloc.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

struct _ck_array {
	unsigned int n_committed;
	unsigned int length;
	void *values[];
};

struct ck_array {
	struct ck_malloc *allocator;
	struct _ck_array *active;
	unsigned int n_entries;
	struct _ck_array *transaction;
};
typedef struct ck_array ck_array_t;

struct ck_array_iterator {
	struct _ck_array *snapshot;
};
typedef struct ck_array_iterator ck_array_iterator_t;

#define CK_ARRAY_MODE_SPMC 0U
#define CK_ARRAY_MODE_MPMC (void) /* Unsupported. */

bool ck_array_init(ck_array_t *, unsigned int, struct ck_malloc *, unsigned int);
bool ck_array_commit(ck_array_t *);
bool ck_array_put(ck_array_t *, void *);
int ck_array_put_unique(ck_array_t *, void *);
bool ck_array_remove(ck_array_t *, void *);
void ck_array_deinit(ck_array_t *, bool);

CK_CC_INLINE static unsigned int
ck_array_length(struct ck_array *array)
{
	struct _ck_array *a = ck_pr_load_ptr(&array->active);

	ck_pr_fence_load();
	return ck_pr_load_uint(&a->n_committed);
}

CK_CC_INLINE static void *
ck_array_buffer(struct ck_array *array, unsigned int *length)
{
	struct _ck_array *a = ck_pr_load_ptr(&array->active);

	ck_pr_fence_load();
	*length = ck_pr_load_uint(&a->n_committed);
	return a->values;
}

CK_CC_INLINE static bool
ck_array_initialized(struct ck_array *array)
{

	return ck_pr_load_ptr(&array->active) != NULL;
}

#define CK_ARRAY_FOREACH(a, i, b)		   	\
	(i)->snapshot = ck_pr_load_ptr(&(a)->active);	\
	ck_pr_fence_load();				\
	for (unsigned int _ck_i = 0;		   	\
	    _ck_i < (a)->active->n_committed &&		\
	    ((*b) = (a)->active->values[_ck_i], 1);	\
	    _ck_i++)

#endif /* CK_ARRAY_H */
