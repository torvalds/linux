/*-
 * Copyright (c) 2016 Mahdi Mokhtari <mokhi64@gmail.com>
 * Copyright (c) 2016, 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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

#include <sys/queue.h>
#include "namespace.h"
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "un-namespace.h"
#include "libc_private.h"

/*
 * C++11 introduces the thread_local scope (like __thread with some
 * additions).  As a key-feature it should support non-trivial
 * destructors, registered with __cxa_thread_atexit() to be executed
 * at the thread termination.
 *
 * The implemention keeps a _Thread_local list of destructors per each
 * thread, and calls __cxa_thread_call_dtors() on each thread's exit
 * to do cleanup.  For a thread calling exit(3), in particular, for
 * the initial thread returning from main(), we call
 * __cxa_thread_call_dtors() inside exit().
 *
 * It could be possible that a dynamically loaded library, use
 * thread_local variable but is dlclose()'d before thread exit.  The
 * destructor of this variable will then try to access the address,
 * for calling it but it's unloaded, so it'll crash.  We're using
 * __elf_phdr_match_addr() to detect and prevent such cases and so
 * prevent the crash.
 */

#define CXA_DTORS_ITERATIONS 4

struct cxa_thread_dtor {
	void *obj;
	void (*func)(void *);
	void *dso;
	LIST_ENTRY(cxa_thread_dtor) entry;
};
static _Thread_local LIST_HEAD(dtor_list, cxa_thread_dtor) dtors =
    LIST_HEAD_INITIALIZER(dtors);

int
__cxa_thread_atexit_impl(void (*dtor_func)(void *), void *obj,
    void *dso_symbol)
{

	return (__cxa_thread_atexit_hidden(dtor_func, obj, dso_symbol));
}

int
__cxa_thread_atexit_hidden(void (*dtor_func)(void *), void *obj,
    void *dso_symbol)
{
	struct cxa_thread_dtor *new_dtor;

	new_dtor = malloc(sizeof(*new_dtor));
	if (new_dtor == NULL) {
		errno = ENOMEM; /* forcibly override malloc(3) error */
		return (-1);
	}

	new_dtor->obj = obj;
	new_dtor->func = dtor_func;
	new_dtor->dso = dso_symbol;
	LIST_INSERT_HEAD(&dtors, new_dtor, entry);
	return (0);
}

static void
walk_cb_call(struct cxa_thread_dtor *dtor)
{
	struct dl_phdr_info phdr_info;

	if (_rtld_addr_phdr(dtor->dso, &phdr_info) &&
	    __elf_phdr_match_addr(&phdr_info, dtor->func))
		dtor->func(dtor->obj);
	else
		fprintf(stderr, "__cxa_thread_call_dtors: dtr %p from "
		    "unloaded dso, skipping\n", (void *)(dtor->func));
}

static void
walk_cb_nocall(struct cxa_thread_dtor *dtor __unused)
{
}

static void
cxa_thread_walk(void (*cb)(struct cxa_thread_dtor *))
{
	struct cxa_thread_dtor *dtor, *tdtor;

	LIST_FOREACH_SAFE(dtor, &dtors, entry, tdtor) {
		LIST_REMOVE(dtor, entry);
		cb(dtor);
		free(dtor);
	}
}

/*
 * This is the callback function we use to call destructors, once for
 * each thread.  It is called in exit(3) in libc/stdlib/exit.c and
 * before exit_thread() in libthr/thread/thr_exit.c.
 */
void
__cxa_thread_call_dtors(void)
{
	int i;

	for (i = 0; i < CXA_DTORS_ITERATIONS && !LIST_EMPTY(&dtors); i++)
		cxa_thread_walk(walk_cb_call);

	if (!LIST_EMPTY(&dtors)) {
		fprintf(stderr, "Thread %p is exiting with more "
		    "thread-specific dtors created after %d iterations "
		    "of destructor calls\n",
		    _pthread_self(), i);
		cxa_thread_walk(walk_cb_nocall);
	}
}
