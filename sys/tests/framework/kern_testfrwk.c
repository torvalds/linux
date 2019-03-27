/*-
 * Copyright (c) 2015 Netflix, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sdt.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/queue.h>
#include <tests/kern_testfrwk.h>
#ifdef SMP
#include <machine/cpu.h>
#endif

struct kern_test_list {
	TAILQ_ENTRY(kern_test_list) next;
	char name[TEST_NAME_LEN];
	kerntfunc func;
};

TAILQ_HEAD(ktestlist, kern_test_list);

struct kern_test_entry {
	TAILQ_ENTRY(kern_test_entry) next;
	struct kern_test_list *kt_e;
	struct kern_test kt_data;
};

TAILQ_HEAD(ktestqueue, kern_test_entry);

MALLOC_DEFINE(M_KTFRWK, "kern_tfrwk", "Kernel Test Framework");
struct kern_totfrwk {
	struct taskqueue *kfrwk_tq;
	struct task kfrwk_que;
	struct ktestlist kfrwk_testlist;
	struct ktestqueue kfrwk_testq;
	struct mtx kfrwk_mtx;
	int kfrwk_waiting;
};

struct kern_totfrwk kfrwk;
static int ktest_frwk_inited = 0;

#define KTFRWK_MUTEX_INIT() mtx_init(&kfrwk.kfrwk_mtx, "kern_test_frwk", "tfrwk", MTX_DEF)

#define KTFRWK_DESTROY() mtx_destroy(&kfrwk.kfrwk_mtx)

#define KTFRWK_LOCK() mtx_lock(&kfrwk.kfrwk_mtx)

#define KTFRWK_UNLOCK()	mtx_unlock(&kfrwk.kfrwk_mtx)

static void
kfrwk_task(void *context, int pending)
{
	struct kern_totfrwk *tf;
	struct kern_test_entry *wk;
	int free_mem = 0;
	struct kern_test kt_data;
	kerntfunc ktf;

	memset(&kt_data, 0, sizeof(kt_data));
	ktf = NULL;
	tf = (struct kern_totfrwk *)context;
	KTFRWK_LOCK();
	wk = TAILQ_FIRST(&tf->kfrwk_testq);
	if (wk) {
		wk->kt_data.tot_threads_running--;
		tf->kfrwk_waiting--;
		memcpy(&kt_data, &wk->kt_data, sizeof(kt_data));
		if (wk->kt_data.tot_threads_running == 0) {
			TAILQ_REMOVE(&tf->kfrwk_testq, wk, next);
			free_mem = 1;
		} else {
			/* Wake one of my colleages up to help too */
			taskqueue_enqueue(tf->kfrwk_tq, &tf->kfrwk_que);
		}
		if (wk->kt_e) {
			ktf = wk->kt_e->func;
		}
	}
	KTFRWK_UNLOCK();
	if (wk && free_mem) {
		free(wk, M_KTFRWK);
	}
	/* Execute the test */
	if (ktf) {
		(*ktf) (&kt_data);
	}
	/* We are done */
	atomic_add_int(&tf->kfrwk_waiting, 1);
}

static int
kerntest_frwk_init(void)
{
	u_int ncpus = mp_ncpus ? mp_ncpus : MAXCPU;

	KTFRWK_MUTEX_INIT();
	TAILQ_INIT(&kfrwk.kfrwk_testq);
	TAILQ_INIT(&kfrwk.kfrwk_testlist);
	/* Now lets start up a number of tasks to do the work */
	TASK_INIT(&kfrwk.kfrwk_que, 0, kfrwk_task, &kfrwk);
	kfrwk.kfrwk_tq = taskqueue_create_fast("sbtls_task", M_NOWAIT,
	    taskqueue_thread_enqueue, &kfrwk.kfrwk_tq);
	if (kfrwk.kfrwk_tq == NULL) {
		printf("Can't start taskqueue for Kernel Test Framework\n");
		panic("Taskqueue init fails for kfrwk");
	}
	taskqueue_start_threads(&kfrwk.kfrwk_tq, ncpus, PI_NET, "[kt_frwk task]");
	kfrwk.kfrwk_waiting = ncpus;
	ktest_frwk_inited = 1;
	return (0);
}

static int
kerntest_frwk_fini(void)
{
	KTFRWK_LOCK();
	if (!TAILQ_EMPTY(&kfrwk.kfrwk_testlist)) {
		/* Still modules registered */
		KTFRWK_UNLOCK();
		return (EBUSY);
	}
	ktest_frwk_inited = 0;
	KTFRWK_UNLOCK();
	taskqueue_free(kfrwk.kfrwk_tq);
	/* Ok lets destroy the mutex on the way outs */
	KTFRWK_DESTROY();
	return (0);
}


static int kerntest_execute(SYSCTL_HANDLER_ARGS);

SYSCTL_NODE(_kern, OID_AUTO, testfrwk, CTLFLAG_RW, 0, "Kernel Test Framework");
SYSCTL_PROC(_kern_testfrwk, OID_AUTO, runtest, (CTLTYPE_STRUCT | CTLFLAG_RW),
    0, 0, kerntest_execute, "IU", "Execute a kernel test");

int
kerntest_execute(SYSCTL_HANDLER_ARGS)
{
	struct kern_test kt;
	struct kern_test_list *li, *te = NULL;
	struct kern_test_entry *kte = NULL;
	int error = 0;

	if (ktest_frwk_inited == 0) {
		return (ENOENT);
	}
	/* Find the entry if possible */
	error = SYSCTL_IN(req, &kt, sizeof(struct kern_test));
	if (error) {
		return (error);
	}
	if (kt.num_threads <= 0) {
		return (EINVAL);
	}
	/* Grab some memory */
	kte = malloc(sizeof(struct kern_test_entry), M_KTFRWK, M_WAITOK);
	if (kte == NULL) {
		error = ENOMEM;
		goto out;
	}
	KTFRWK_LOCK();
	TAILQ_FOREACH(li, &kfrwk.kfrwk_testlist, next) {
		if (strcmp(li->name, kt.name) == 0) {
			te = li;
			break;
		}
	}
	if (te == NULL) {
		printf("Can't find the test %s\n", kt.name);
		error = ENOENT;
		free(kte, M_KTFRWK);
		goto out;
	}
	/* Ok we have a test item to run, can we? */
	if (!TAILQ_EMPTY(&kfrwk.kfrwk_testq)) {
		/* We don't know if there is enough threads */
		error = EAGAIN;
		free(kte, M_KTFRWK);
		goto out;
	}
	if (kfrwk.kfrwk_waiting < kt.num_threads) {
		error = E2BIG;
		free(kte, M_KTFRWK);
		goto out;
	}
	kt.tot_threads_running = kt.num_threads;
	/* Ok it looks like we can do it, lets get an entry */
	kte->kt_e = li;
	memcpy(&kte->kt_data, &kt, sizeof(kt));
	TAILQ_INSERT_TAIL(&kfrwk.kfrwk_testq, kte, next);
	taskqueue_enqueue(kfrwk.kfrwk_tq, &kfrwk.kfrwk_que);
out:
	KTFRWK_UNLOCK();
	return (error);
}

int
kern_testframework_register(const char *name, kerntfunc func)
{
	int error = 0;
	struct kern_test_list *li, *te = NULL;
	int len;

	len = strlen(name);
	if (len >= TEST_NAME_LEN) {
		return (E2BIG);
	}
	te = malloc(sizeof(struct kern_test_list), M_KTFRWK, M_WAITOK);
	if (te == NULL) {
		error = ENOMEM;
		goto out;
	}
	KTFRWK_LOCK();
	/* First does it already exist? */
	TAILQ_FOREACH(li, &kfrwk.kfrwk_testlist, next) {
		if (strcmp(li->name, name) == 0) {
			error = EALREADY;
			free(te, M_KTFRWK);
			goto out;
		}
	}
	/* Ok we can do it, lets add it to the list */
	te->func = func;
	strcpy(te->name, name);
	TAILQ_INSERT_TAIL(&kfrwk.kfrwk_testlist, te, next);
out:
	KTFRWK_UNLOCK();
	return (error);
}

int
kern_testframework_deregister(const char *name)
{
	struct kern_test_list *li, *te = NULL;
	u_int ncpus = mp_ncpus ? mp_ncpus : MAXCPU;
	int error = 0;

	KTFRWK_LOCK();
	/* First does it already exist? */
	TAILQ_FOREACH(li, &kfrwk.kfrwk_testlist, next) {
		if (strcmp(li->name, name) == 0) {
			te = li;
			break;
		}
	}
	if (te == NULL) {
		/* It is not registered so no problem */
		goto out;
	}
	if (ncpus != kfrwk.kfrwk_waiting) {
		/* We are busy executing something -- can't unload */
		error = EBUSY;
		goto out;
	}
	if (!TAILQ_EMPTY(&kfrwk.kfrwk_testq)) {
		/* Something still to execute */
		error = EBUSY;
		goto out;
	}
	/* Ok we can remove the dude safely */
	TAILQ_REMOVE(&kfrwk.kfrwk_testlist, te, next);
	memset(te, 0, sizeof(struct kern_test_list));
	free(te, M_KTFRWK);
out:
	KTFRWK_UNLOCK();
	return (error);
}

static int
kerntest_mod_init(module_t mod, int type, void *data)
{
	int err;

	switch (type) {
	case MOD_LOAD:
		err = kerntest_frwk_init();
		break;
	case MOD_QUIESCE:
		KTFRWK_LOCK();
		if (TAILQ_EMPTY(&kfrwk.kfrwk_testlist)) {
			err = 0;
		} else {
			err = EBUSY;
		}
		KTFRWK_UNLOCK();
		break;
	case MOD_UNLOAD:
		err = kerntest_frwk_fini();
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (err);
}

static moduledata_t kern_test_framework = {
	.name = "kernel_testfrwk",
	.evhand = kerntest_mod_init,
	.priv = 0
};

MODULE_VERSION(kern_testframework, 1);
DECLARE_MODULE(kern_testframework, kern_test_framework, SI_SUB_PSEUDO, SI_ORDER_ANY);
