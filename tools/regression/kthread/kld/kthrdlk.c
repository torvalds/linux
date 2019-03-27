/*-
 * Copyright (c) 2010 Giovanni Trematerra <giovanni.trematerra@gmail.com>
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

/*
 *	PURPOSE:
 *
 *	This kernel module helped to identify a deadlock in kthread
 *	interface, also pointed out a race in kthread_exit function.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/time.h>

#ifdef TESTPAUSE_DEBUG
#define DPRINTF(x) do {							\
	printf (x);							\
} while (0)
#else
#define DPRINTF(x)
#endif

static struct mtx test_global_lock;
static int global_condvar;
static int test_thrcnt;
volatile int QUIT;

static void 
thr_suspender(void *arg)
{
	struct thread *td = (struct thread *) arg;
	int error;

	for (;;) {
		if (QUIT == 1)
			break;
		error = kthread_suspend(td, 10*hz);
		if (error != 0 && QUIT == 0) {
			if (error == EWOULDBLOCK)
				panic("Ooops: kthread deadlock\n");
			else 
				panic("kthread_suspend error: %d\n", error);
			break;
		}
	}

	mtx_lock(&test_global_lock);
	test_thrcnt--;
	wakeup(&global_condvar);
	mtx_unlock(&test_global_lock);

	kthread_exit();
}

static void 
thr_resumer(void *arg)
{
	struct thread *td = (struct thread *) arg;
	int error;

	for (;;) {
		/* must be the last thread to exit */
		if (QUIT == 1 && test_thrcnt == 1)
			break;
		error = kthread_resume(td);
		if (error != 0)
			panic("%s: error on kthread_resume. error: %d\n",
				   	__func__, error);
	}

	mtx_lock(&test_global_lock);
	test_thrcnt--;
	wakeup(&global_condvar);
	mtx_unlock(&test_global_lock);

	kthread_exit();
}

static void
thr_getsuspended(void *arg)
{
	for (;;) {
		if (QUIT == 1)
			break;
		kthread_suspend_check();
	}

	mtx_lock(&test_global_lock);
	test_thrcnt--;
	wakeup(&global_condvar);
	mtx_unlock(&test_global_lock);

	kthread_exit();
}

static void
kthrdlk_init(void)
{
	struct proc *testproc;
	struct thread *newthr;
	int error;

	QUIT = 0;
	test_thrcnt = 3;
	mtx_init(&test_global_lock, "kthrdlk_lock", NULL, MTX_DEF);
	testproc = NULL;
	error = kproc_kthread_add(thr_getsuspended, NULL, &testproc, &newthr,
	    0, 0, "kthrdlk", "thr_getsuspended");
	if (error != 0)
		panic("cannot start thr_getsuspended error: %d\n", error);

	error = kproc_kthread_add(thr_resumer, newthr, &testproc, NULL, 0, 0, 
	    "kthrdlk", "thr_resumer");
	if (error != 0)
		panic("cannot start thr_resumer error: %d\n", error);

	error = kproc_kthread_add(thr_suspender, newthr, &testproc, NULL, 0, 0, 
	    "kthrdlk", "thr_suspender");
	if (error != 0)
		panic("cannot start thr_suspender error: %d\n", error);
}

static void
kthrdlk_done(void)
{
	int ret;
	DPRINTF(("sending QUIT signal to the thrdlk threads\n"));

	/* wait kernel threads end */
	mtx_lock(&test_global_lock);
	QUIT = 1;
	while (test_thrcnt != 0) {
		ret = mtx_sleep(&global_condvar, &test_global_lock, 0, "waiting thrs end", 30 * hz);
		if (ret == EWOULDBLOCK) {
			panic("some threads not die! remaining: %d", test_thrcnt);
			break;
		}
	}
	if (test_thrcnt == 0)
		DPRINTF(("All test_pause threads die\n"));

	mtx_destroy(&test_global_lock);
}

static int 
kthrdlk_handler(module_t mod, int /*modeventtype_t*/ what,
                            void *arg)
{
	switch (what) {
		case MOD_LOAD:
			kthrdlk_init();
			uprintf("kthrdlk loaded!\n");
			return (0);
		case MOD_UNLOAD:
			kthrdlk_done();
			uprintf("Bye Bye! kthrdlk unloaded!\n");
			return (0);
	}

	return (EOPNOTSUPP);
}

static moduledata_t mod_data= {
             "kthrdlk",
             kthrdlk_handler,
             0
     };

MODULE_VERSION(kthrdlk, 1);

DECLARE_MODULE(kthrdlk, mod_data, SI_SUB_EXEC, SI_ORDER_ANY);

