/*	$OpenBSD: rthread_tls.c,v 1.6 2025/08/06 04:11:22 dlg Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * thread specific storage
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "rthread.h"


struct rthread_key {
	int used;
	void (*destructor)(void *);
};

static struct rthread_key rkeys[PTHREAD_KEYS_MAX];
static struct __cmtx rkeyslock = __CMTX_INITIALIZER();

int
pthread_key_create(pthread_key_t *key, void (*destructor)(void*))
{
	static int hint;
	int i;

	__cmtx_enter(&rkeyslock);
	if (rkeys[hint].used) {
		for (i = 0; i < PTHREAD_KEYS_MAX; i++) {
			if (!rkeys[i].used)
				break;
		}
		if (i == PTHREAD_KEYS_MAX) {
			__cmtx_leave(&rkeyslock);
			return (EAGAIN);
		}
		hint = i;
	}
	rkeys[hint].used = 1;
	rkeys[hint].destructor = destructor;

	*key = hint++;
	if (hint >= PTHREAD_KEYS_MAX)
		hint = 0;
	__cmtx_leave(&rkeyslock);

	return (0);
}
DEF_STRONG(pthread_key_create);

int
pthread_key_delete(pthread_key_t key)
{
	struct rthread_storage *rs;
	int rv = 0;

	if (key < 0 || key >= PTHREAD_KEYS_MAX)
		return (EINVAL);

	__cmtx_enter(&rkeyslock);
	if (!rkeys[key].used) {
		rv = EINVAL;
		goto out;
	}

	rkeys[key].used = 0;
	rkeys[key].destructor = NULL;
	if (_thread_cb.tc_thread_key_zero != NULL)
		_thread_cb.tc_thread_key_zero(key);
	else {
		for (rs = _initial_thread.local_storage; rs; rs = rs->next) {
			if (rs->keyid == key)
				rs->data = NULL;
		}
	}

out:
	__cmtx_leave(&rkeyslock);
	return (rv);
}

static struct rthread_storage *
_rthread_findstorage(pthread_key_t key)
{
	struct rthread_storage *rs;
	pthread_t self;

	if (!rkeys[key].used) {
		rs = NULL;
		goto out;
	}

	self = pthread_self();

	for (rs = self->local_storage; rs; rs = rs->next) {
		if (rs->keyid == key)
			break;
	}
	if (!rs) {
		rs = calloc(1, sizeof(*rs));
		if (!rs)
			goto out;
		rs->keyid = key;
		rs->data = NULL;
		rs->next = self->local_storage;
		self->local_storage = rs;
	}

out:
	return (rs);
}

void *
pthread_getspecific(pthread_key_t key)
{
	struct rthread_storage *rs;

	if (key < 0 || key >= PTHREAD_KEYS_MAX)
		return (NULL);

	rs = _rthread_findstorage(key);
	if (!rs)
		return (NULL);

	return (rs->data);
}
DEF_STRONG(pthread_getspecific);

int
pthread_setspecific(pthread_key_t key, const void *data)
{
	struct rthread_storage *rs;

	if (key < 0 || key >= PTHREAD_KEYS_MAX)
		return (EINVAL);

	rs = _rthread_findstorage(key);
	if (!rs)
		return (ENOMEM);
	rs->data = (void *)data;

	return (0);
}
DEF_STRONG(pthread_setspecific);

void
_rthread_tls_destructors(pthread_t thread)
{
	struct rthread_storage *rs;
	int i;

	__cmtx_enter(&rkeyslock);
	for (i = 0; i < PTHREAD_DESTRUCTOR_ITERATIONS; i++) {
		for (rs = thread->local_storage; rs; rs = rs->next) {
			if (!rs->data)
				continue;
			if (rkeys[rs->keyid].destructor) {
				void (*destructor)(void *) =
				    rkeys[rs->keyid].destructor;
				void *data = rs->data;
				rs->data = NULL;
				__cmtx_leave(&rkeyslock);
				destructor(data);
				__cmtx_enter(&rkeyslock);
			}
		}
	}
	for (rs = thread->local_storage; rs; rs = thread->local_storage) {
		thread->local_storage = rs->next;
		free(rs);
	}
	__cmtx_leave(&rkeyslock);
}
