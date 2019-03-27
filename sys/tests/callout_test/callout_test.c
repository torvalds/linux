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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cpuctl.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/pmckern.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <tests/kern_testfrwk.h>
#include <tests/callout_test.h>
#include <machine/cpu.h>

MALLOC_DEFINE(M_CALLTMP, "Temp callout Memory", "CalloutTest");

struct callout_run {
	struct mtx lock;
	struct callout *co_array;
	int co_test;
	int co_number_callouts;
	int co_return_npa;
	int co_completed;
	int callout_waiting;
	int drain_calls;
	int cnt_zero;
	int cnt_one;
	int index;
};

static struct callout_run *comaster[MAXCPU];

uint64_t callout_total = 0;

static void execute_the_co_test(struct callout_run *rn);

static void
co_saydone(void *arg)
{
	struct callout_run *rn;

	rn = (struct callout_run *)arg;
	printf("The callout test is now complete for thread %d\n",
	    rn->index);
	printf("number_callouts:%d\n",
	    rn->co_number_callouts);
	printf("Callouts that bailed (Not PENDING or ACTIVE cleared):%d\n",
	    rn->co_return_npa);
	printf("Callouts that completed:%d\n", rn->co_completed);
	printf("Drain calls:%d\n", rn->drain_calls);
	printf("Zero returns:%d non-zero:%d\n",
	    rn->cnt_zero,
	    rn->cnt_one);

}

static void
drainit(void *arg)
{
	struct callout_run *rn;

	rn = (struct callout_run *)arg;
	mtx_lock(&rn->lock);
	rn->drain_calls++;
	mtx_unlock(&rn->lock);
}

static void
test_callout(void *arg)
{
	struct callout_run *rn;
	int cpu;

	critical_enter();
	cpu = curcpu;
	critical_exit();
	rn = (struct callout_run *)arg;
	atomic_add_int(&rn->callout_waiting, 1);
	mtx_lock(&rn->lock);
	if (callout_pending(&rn->co_array[cpu]) ||
	    !callout_active(&rn->co_array[cpu])) {
		rn->co_return_npa++;
		atomic_subtract_int(&rn->callout_waiting, 1);
		mtx_unlock(&rn->lock);
		return;
	}
	callout_deactivate(&rn->co_array[cpu]);
	rn->co_completed++;
	mtx_unlock(&rn->lock);
	atomic_subtract_int(&rn->callout_waiting, 1);
}

void
execute_the_co_test(struct callout_run *rn)
{
	int i, ret, cpu;
	uint32_t tk_s, tk_e, tk_d;

	mtx_lock(&rn->lock);
	rn->callout_waiting = 0;
	for (i = 0; i < rn->co_number_callouts; i++) {
		if (rn->co_test == 1) {
			/* start all on spread out cpu's */
			cpu = i % mp_ncpus;
			callout_reset_sbt_on(&rn->co_array[i], 3, 0, test_callout, rn,
			    cpu, 0);
		} else {
			/* Start all on the same CPU */
			callout_reset_sbt_on(&rn->co_array[i], 3, 0, test_callout, rn,
			    rn->index, 0);
		}
	}
	tk_s = ticks;
	while (rn->callout_waiting != rn->co_number_callouts) {
		cpu_spinwait();
		tk_e = ticks;
		tk_d = tk_e - tk_s;
		if (tk_d > 100) {
			break;
		}
	}
	/* OK everyone is waiting and we have the lock */
	for (i = 0; i < rn->co_number_callouts; i++) {
		ret = callout_async_drain(&rn->co_array[i], drainit);
		if (ret) {
			rn->cnt_one++;
		} else {
			rn->cnt_zero++;
		}
	}
	rn->callout_waiting -= rn->cnt_one;
	mtx_unlock(&rn->lock);
	/* Now wait until all are done */
	tk_s = ticks;
	while (rn->callout_waiting > 0) {
		cpu_spinwait();
		tk_e = ticks;
		tk_d = tk_e - tk_s;
		if (tk_d > 100) {
			break;
		}
	}
	co_saydone((void *)rn);
}


static void
run_callout_test(struct kern_test *test)
{
	struct callout_test *u;
	size_t sz;
	int i;
	struct callout_run *rn;
	int index = test->tot_threads_running;

	u = (struct callout_test *)test->test_options;
	if (comaster[index] == NULL) {
		rn = comaster[index] = malloc(sizeof(struct callout_run), M_CALLTMP, M_WAITOK);
		memset(comaster[index], 0, sizeof(struct callout_run));
		mtx_init(&rn->lock, "callouttest", NULL, MTX_DUPOK);
		rn->index = index;
	} else {
		rn = comaster[index];
		rn->co_number_callouts = rn->co_return_npa = 0;
		rn->co_completed = rn->callout_waiting = 0;
		rn->drain_calls = rn->cnt_zero = rn->cnt_one = 0;
		if (rn->co_array) {
			free(rn->co_array, M_CALLTMP);
			rn->co_array = NULL;
		}
	}
	rn->co_number_callouts = u->number_of_callouts;
	rn->co_test = u->test_number;
	sz = sizeof(struct callout) * rn->co_number_callouts;
	rn->co_array = malloc(sz, M_CALLTMP, M_WAITOK);
	for (i = 0; i < rn->co_number_callouts; i++) {
		callout_init(&rn->co_array[i], CALLOUT_MPSAFE);
	}
	execute_the_co_test(rn);
}

int callout_test_is_loaded = 0;

static void
cocleanup(void)
{
	int i;

	for (i = 0; i < MAXCPU; i++) {
		if (comaster[i]) {
			if (comaster[i]->co_array) {
				free(comaster[i]->co_array, M_CALLTMP);
				comaster[i]->co_array = NULL;
			}
			free(comaster[i], M_CALLTMP);
			comaster[i] = NULL;
		}
	}
}

static int
callout_test_modevent(module_t mod, int type, void *data)
{
	int err = 0;

	switch (type) {
	case MOD_LOAD:
		err = kern_testframework_register("callout_test",
		    run_callout_test);
		if (err) {
			printf("Can't load callout_test err:%d returned\n",
			    err);
		} else {
			memset(comaster, 0, sizeof(comaster));
			callout_test_is_loaded = 1;
		}
		break;
	case MOD_QUIESCE:
		err = kern_testframework_deregister("callout_test");
		if (err == 0) {
			callout_test_is_loaded = 0;
			cocleanup();
		}
		break;
	case MOD_UNLOAD:
		if (callout_test_is_loaded) {
			err = kern_testframework_deregister("callout_test");
			if (err == 0) {
				cocleanup();
				callout_test_is_loaded = 0;
			}
		}
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (err);
}

static moduledata_t callout_test_mod = {
	.name = "callout_test",
	.evhand = callout_test_modevent,
	.priv = 0
};

MODULE_DEPEND(callout_test, kern_testframework, 1, 1, 1);
DECLARE_MODULE(callout_test, callout_test_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
