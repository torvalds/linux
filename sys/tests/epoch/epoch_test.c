/*-
 * Copyright (c) 2018, Matthew Macy <mmacy@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Neither the name of Matthew Macy nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/counter.h>
#include <sys/epoch.h>
#include <sys/gtaskqueue.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>


struct epoch_test_instance {
	int threadid;
};

static int inited;
static int iterations;
#define ET_EXITING 0x1
static volatile int state_flags;
static struct mtx state_mtx __aligned(CACHE_LINE_SIZE*2);
MTX_SYSINIT(state_mtx, &state_mtx, "epoch state mutex", MTX_DEF);
static struct mtx mutexA __aligned(CACHE_LINE_SIZE*2);
MTX_SYSINIT(mutexA, &mutexA, "epoch mutexA", MTX_DEF);
static struct mtx mutexB __aligned(CACHE_LINE_SIZE*2);
MTX_SYSINIT(mutexB, &mutexB, "epoch mutexB", MTX_DEF);
epoch_t test_epoch;

static void
epoch_testcase1(struct epoch_test_instance *eti)
{
	int i, startticks;
	struct mtx *mtxp;
	struct epoch_tracker et;

	startticks = ticks;
	i = 0;
	if (eti->threadid & 0x1)
		mtxp = &mutexA;
	else
		mtxp = &mutexB;

	while (i < iterations) {
		epoch_enter_preempt(test_epoch, &et);
		mtx_lock(mtxp);
		i++;
		mtx_unlock(mtxp);
		epoch_exit_preempt(test_epoch, &et);
		epoch_wait_preempt(test_epoch);
	}
	printf("test1: thread: %d took %d ticks to complete %d iterations\n",
		   eti->threadid, ticks - startticks, iterations);
}

static void
epoch_testcase2(struct epoch_test_instance *eti)
{
	int i, startticks;
	struct mtx *mtxp;
	struct epoch_tracker et;

	startticks = ticks;
	i = 0;
	mtxp = &mutexA;

	while (i < iterations) {
		epoch_enter_preempt(test_epoch, &et);
		mtx_lock(mtxp);
		DELAY(1);
		i++;
		mtx_unlock(mtxp);
		epoch_exit_preempt(test_epoch, &et);
		epoch_wait_preempt(test_epoch);
	}
	printf("test2: thread: %d took %d ticks to complete %d iterations\n",
		   eti->threadid, ticks - startticks, iterations);
}

static void
testloop(void *arg) {

	mtx_lock(&state_mtx);
	while ((state_flags & ET_EXITING) == 0) {
		msleep(&state_mtx, &state_mtx, 0, "epoch start wait", 0);
		if (state_flags & ET_EXITING)
			goto out;
		mtx_unlock(&state_mtx);
		epoch_testcase2(arg);
		pause("W", 500);
		epoch_testcase1(arg);
		mtx_lock(&state_mtx);
	}
 out:
	mtx_unlock(&state_mtx);
	kthread_exit();
}

static struct thread *testthreads[MAXCPU];
static struct epoch_test_instance etilist[MAXCPU];

static int
test_modinit(void)
{
	struct thread *td;
	int i, error, pri_range, pri_off;

	pri_range = PRI_MIN_TIMESHARE - PRI_MIN_REALTIME;
	test_epoch = epoch_alloc(EPOCH_PREEMPT);
	for (i = 0; i < mp_ncpus*2; i++) {
		etilist[i].threadid = i;
		error = kthread_add(testloop, &etilist[i], NULL, &testthreads[i],
							0, 0, "epoch_test_%d", i);
		if (error) {
			printf("%s: kthread_add(epoch_test): error %d", __func__,
				   error);
		} else {
			pri_off = (i*4)%pri_range;
			td = testthreads[i];
			thread_lock(td);
			sched_prio(td, PRI_MIN_REALTIME + pri_off);
			thread_unlock(td);
		}
	}
	inited = 1;
	return (0);
}

static int
epochtest_execute(SYSCTL_HANDLER_ARGS)
{
	int error, v;

	if (inited == 0)
		return (ENOENT);

	v = 0;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error)
		return (error);
	if (req->newptr == NULL)
		return (error);
	if (v == 0)
		return (0);
	mtx_lock(&state_mtx);
	iterations = v;
	wakeup(&state_mtx);
	mtx_unlock(&state_mtx);

	return (0);
}

SYSCTL_NODE(_kern, OID_AUTO, epochtest, CTLFLAG_RW, 0, "Epoch Test Framework");
SYSCTL_PROC(_kern_epochtest, OID_AUTO, runtest, (CTLTYPE_INT | CTLFLAG_RW),
			0, 0, epochtest_execute, "I", "Execute an epoch test");

static int
epoch_test_module_event_handler(module_t mod, int what, void *arg __unused)
{
	int err;

	switch (what) {
	case MOD_LOAD:
		if ((err = test_modinit()) != 0)
			return (err);
		break;
	case MOD_UNLOAD:
		mtx_lock(&state_mtx);
		state_flags = ET_EXITING;
		wakeup(&state_mtx);
		mtx_unlock(&state_mtx);
		/* yes --- gross */
		pause("epoch unload", 3*hz);
		break;
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t epoch_test_moduledata = {
	"epoch_test",
	epoch_test_module_event_handler,
	NULL
};

MODULE_VERSION(epoch_test, 1);
DECLARE_MODULE(epoch_test, epoch_test_moduledata, SI_SUB_PSEUDO, SI_ORDER_ANY);
