/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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

#include "namespace.h"
#include <sys/mman.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "un-namespace.h"
#include "libc_private.h"

#include "thr_private.h"

/* Used in symbol lookup of libthread_db */
struct pthread_key _thread_keytable[PTHREAD_KEYS_MAX];

__weak_reference(_pthread_key_create, pthread_key_create);
__weak_reference(_pthread_key_delete, pthread_key_delete);
__weak_reference(_pthread_getspecific, pthread_getspecific);
__weak_reference(_pthread_setspecific, pthread_setspecific);


int
_pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
	struct pthread *curthread;
	int i;

	_thr_check_init();

	curthread = _get_curthread();

	THR_LOCK_ACQUIRE(curthread, &_keytable_lock);
	for (i = 0; i < PTHREAD_KEYS_MAX; i++) {

		if (_thread_keytable[i].allocated == 0) {
			_thread_keytable[i].allocated = 1;
			_thread_keytable[i].destructor = destructor;
			_thread_keytable[i].seqno++;

			THR_LOCK_RELEASE(curthread, &_keytable_lock);
			*key = i + 1;
			return (0);
		}

	}
	THR_LOCK_RELEASE(curthread, &_keytable_lock);
	return (EAGAIN);
}

int
_pthread_key_delete(pthread_key_t userkey)
{
	struct pthread *curthread;
	int key, ret;

	key = userkey - 1;
	if ((unsigned int)key >= PTHREAD_KEYS_MAX)
		return (EINVAL);
	curthread = _get_curthread();
	THR_LOCK_ACQUIRE(curthread, &_keytable_lock);
	if (_thread_keytable[key].allocated) {
		_thread_keytable[key].allocated = 0;
		ret = 0;
	} else {
		ret = EINVAL;
	}
	THR_LOCK_RELEASE(curthread, &_keytable_lock);
	return (ret);
}

void 
_thread_cleanupspecific(void)
{
	struct pthread *curthread;
	void (*destructor)(void *);
	const void *data;
	int i, key;

	curthread = _get_curthread();
	if (curthread->specific == NULL)
		return;
	THR_LOCK_ACQUIRE(curthread, &_keytable_lock);
	for (i = 0; i < PTHREAD_DESTRUCTOR_ITERATIONS &&
	    curthread->specific_data_count > 0; i++) {
		for (key = 0; key < PTHREAD_KEYS_MAX &&
		    curthread->specific_data_count > 0; key++) {
			destructor = NULL;

			if (_thread_keytable[key].allocated &&
			    (curthread->specific[key].data != NULL)) {
				if (curthread->specific[key].seqno ==
				    _thread_keytable[key].seqno) {
					data = curthread->specific[key].data;
					destructor = _thread_keytable[key].
					    destructor;
				}
				curthread->specific[key].data = NULL;
				curthread->specific_data_count--;
			} else if (curthread->specific[key].data != NULL) {
				/* 
				 * This can happen if the key is
				 * deleted via pthread_key_delete
				 * without first setting the value to
				 * NULL in all threads.  POSIX says
				 * that the destructor is not invoked
				 * in this case.
				 */
				curthread->specific[key].data = NULL;
				curthread->specific_data_count--;
			}

			/*
			 * If there is a destructor, call it with the
			 * key table entry unlocked.
			 */
			if (destructor != NULL) {
				THR_LOCK_RELEASE(curthread, &_keytable_lock);
				destructor(__DECONST(void *, data));
				THR_LOCK_ACQUIRE(curthread, &_keytable_lock);
			}
		}
	}
	THR_LOCK_RELEASE(curthread, &_keytable_lock);
	__thr_free(curthread->specific);
	curthread->specific = NULL;
	if (curthread->specific_data_count > 0) {
		stderr_debug("Thread %p has exited with leftover "
		    "thread-specific data after %d destructor iterations\n",
		    curthread, PTHREAD_DESTRUCTOR_ITERATIONS);
	}
}

int 
_pthread_setspecific(pthread_key_t userkey, const void *value)
{
	struct pthread *pthread;
	void *tmp;
	pthread_key_t key;

	key = userkey - 1;
	if ((unsigned int)key >= PTHREAD_KEYS_MAX ||
	    !_thread_keytable[key].allocated)
		return (EINVAL);

	pthread = _get_curthread();
	if (pthread->specific == NULL) {
		tmp = __thr_calloc(PTHREAD_KEYS_MAX,
		    sizeof(struct pthread_specific_elem));
		if (tmp == NULL)
			return (ENOMEM);
		pthread->specific = tmp;
	}
	if (pthread->specific[key].data == NULL) {
		if (value != NULL)
			pthread->specific_data_count++;
	} else if (value == NULL)
		pthread->specific_data_count--;
	pthread->specific[key].data = value;
	pthread->specific[key].seqno = _thread_keytable[key].seqno;
	return (0);
}

void *
_pthread_getspecific(pthread_key_t userkey)
{
	struct pthread *pthread;
	const void *data;
	pthread_key_t key;

	/* Check if there is specific data. */
	key = userkey - 1;
	if ((unsigned int)key >= PTHREAD_KEYS_MAX)
		return (NULL);

	pthread = _get_curthread();
	/* Check if this key has been used before. */
	if (_thread_keytable[key].allocated && pthread->specific != NULL &&
	    pthread->specific[key].seqno == _thread_keytable[key].seqno) {
		/* Return the value: */
		data = pthread->specific[key].data;
	} else {
		/*
		 * This key has not been used before, so return NULL
		 * instead.
		 */
		data = NULL;
	}
	return (__DECONST(void *, data));
}

void
_thr_tsd_unload(struct dl_phdr_info *phdr_info)
{
	struct pthread *curthread;
	void (*destructor)(void *);
	int key;

	curthread = _get_curthread();
	THR_LOCK_ACQUIRE(curthread, &_keytable_lock);
	for (key = 0; key < PTHREAD_KEYS_MAX; key++) {
		if (!_thread_keytable[key].allocated)
			continue;
		destructor = _thread_keytable[key].destructor;
		if (destructor == NULL)
			continue;
		if (__elf_phdr_match_addr(phdr_info, destructor))
			_thread_keytable[key].destructor = NULL;
	}
	THR_LOCK_RELEASE(curthread, &_keytable_lock);
}
