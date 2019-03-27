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

#include <ck_array.h>
#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_string.h>

static struct _ck_array *
ck_array_create(struct ck_malloc *allocator, unsigned int length)
{
	struct _ck_array *active;

	active = allocator->malloc(sizeof(struct _ck_array) + sizeof(void *) * length);
	if (active == NULL)
		return NULL;

	active->n_committed = 0;
	active->length = length;

	return active;
}

bool
ck_array_init(struct ck_array *array, unsigned int mode, struct ck_malloc *allocator, unsigned int length)
{
	struct _ck_array *active;

	(void)mode;

	if (allocator->realloc == NULL ||
	    allocator->malloc == NULL ||
	    allocator->free == NULL ||
	    length == 0)
		return false;

	active = ck_array_create(allocator, length);
	if (active == NULL)
		return false;

	array->n_entries = 0;
	array->allocator = allocator;
	array->active = active;
	array->transaction = NULL;
	return true;
}

bool
ck_array_put(struct ck_array *array, void *value)
{
	struct _ck_array *target;
	unsigned int size;

	/*
	 * If no transaction copy has been necessary, attempt to do in-place
	 * modification of the array.
	 */
	if (array->transaction == NULL) {
		target = array->active;

		if (array->n_entries == target->length) {
			size = target->length << 1;

			target = array->allocator->realloc(target,
			    sizeof(struct _ck_array) + sizeof(void *) * array->n_entries,
			    sizeof(struct _ck_array) + sizeof(void *) * size,
			    true);

			if (target == NULL)
				return false;

			ck_pr_store_uint(&target->length, size);

			/* Serialize with respect to contents. */
			ck_pr_fence_store();
			ck_pr_store_ptr(&array->active, target);
		}

		target->values[array->n_entries++] = value;
		return true;
	}

	target = array->transaction;
	if (array->n_entries == target->length) {
		size = target->length << 1;

		target = array->allocator->realloc(target,
		    sizeof(struct _ck_array) + sizeof(void *) * array->n_entries,
		    sizeof(struct _ck_array) + sizeof(void *) * size,
		    true);

		if (target == NULL)
			return false;

		target->length = size;
		array->transaction = target;
	}

	target->values[array->n_entries++] = value;
	return false;
}

int
ck_array_put_unique(struct ck_array *array, void *value)
{
	unsigned int i, limit;
	void **v;

	limit = array->n_entries;
	if (array->transaction != NULL) {
		v = array->transaction->values;
	} else {
		v = array->active->values;
	}

	for (i = 0; i < limit; i++) {
		if (v[i] == value)
			return 1;
	}

	return -!ck_array_put(array, value);
}

bool
ck_array_remove(struct ck_array *array, void *value)
{
	struct _ck_array *target;
	unsigned int i;

	if (array->transaction != NULL) {
		target = array->transaction;

		for (i = 0; i < array->n_entries; i++) {
			if (target->values[i] == value) {
				target->values[i] = target->values[--array->n_entries];
				return true;
			}
		}

		return false;
	}

	target = array->active;

	for (i = 0; i < array->n_entries; i++) {
		if (target->values[i] == value)
			break;
	}

	if (i == array->n_entries)
		return false;

	/* If there are pending additions, immediately eliminate the operation. */
	if (target->n_committed != array->n_entries) {
		ck_pr_store_ptr(&target->values[i], target->values[--array->n_entries]);
		return true;
	}

	/*
	 * The assumption is that these allocations are small to begin with.
	 * If there is no immediate opportunity for transaction, allocate a
	 * transactional array which will be applied upon commit time.
	 */
	target = ck_array_create(array->allocator, array->n_entries);
	if (target == NULL)
		return false;

	memcpy(target->values, array->active->values, sizeof(void *) * array->n_entries);
	target->length = array->n_entries;
	target->n_committed = array->n_entries;
	target->values[i] = target->values[--array->n_entries];

	array->transaction = target;
	return true;
}

bool
ck_array_commit(ck_array_t *array)
{
	struct _ck_array *m = array->transaction;

	if (m != NULL) {
		struct _ck_array *p;

		m->n_committed = array->n_entries;
		ck_pr_fence_store();
		p = array->active;
		ck_pr_store_ptr(&array->active, m);
		array->allocator->free(p, sizeof(struct _ck_array) +
		    p->length * sizeof(void *), true);
		array->transaction = NULL;

		return true;
	}

	ck_pr_fence_store();
	ck_pr_store_uint(&array->active->n_committed, array->n_entries);
	return true;
}

void
ck_array_deinit(struct ck_array *array, bool defer)
{

	array->allocator->free(array->active,
	    sizeof(struct _ck_array) + sizeof(void *) * array->active->length, defer);

	if (array->transaction != NULL) {
		array->allocator->free(array->transaction,
		    sizeof(struct _ck_array) + sizeof(void *) * array->transaction->length, defer);
	}

	array->transaction = array->active = NULL;
	return;
}
