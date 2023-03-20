// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <syscall.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <asm/barrier.h>
#include <linux/atomic.h>
#include <linux/rseq.h>
#include <linux/unistd.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

#include "../rseq/rseq.c"

/*
 * Any bug related to task migration is likely to be timing-dependent; perform
 * a large number of migrations to reduce the odds of a false negative.
 */
#define NR_TASK_MIGRATIONS 100000

static pthread_t migration_thread;
static cpu_set_t possible_mask;
static int min_cpu, max_cpu;
static bool done;

static atomic_t seq_cnt;

static void guest_code(void)
{
	for (;;)
		GUEST_SYNC(0);
}

static int next_cpu(int cpu)
{
	/*
	 * Advance to the next CPU, skipping those that weren't in the original
	 * affinity set.  Sadly, there is no CPU_SET_FOR_EACH, and cpu_set_t's
	 * data storage is considered as opaque.  Note, if this task is pinned
	 * to a small set of discontigous CPUs, e.g. 2 and 1023, this loop will
	 * burn a lot cycles and the test will take longer than normal to
	 * complete.
	 */
	do {
		cpu++;
		if (cpu > max_cpu) {
			cpu = min_cpu;
			TEST_ASSERT(CPU_ISSET(cpu, &possible_mask),
				    "Min CPU = %d must always be usable", cpu);
			break;
		}
	} while (!CPU_ISSET(cpu, &possible_mask));

	return cpu;
}

static void *migration_worker(void *__rseq_tid)
{
	pid_t rseq_tid = (pid_t)(unsigned long)__rseq_tid;
	cpu_set_t allowed_mask;
	int r, i, cpu;

	CPU_ZERO(&allowed_mask);

	for (i = 0, cpu = min_cpu; i < NR_TASK_MIGRATIONS; i++, cpu = next_cpu(cpu)) {
		CPU_SET(cpu, &allowed_mask);

		/*
		 * Bump the sequence count twice to allow the reader to detect
		 * that a migration may have occurred in between rseq and sched
		 * CPU ID reads.  An odd sequence count indicates a migration
		 * is in-progress, while a completely different count indicates
		 * a migration occurred since the count was last read.
		 */
		atomic_inc(&seq_cnt);

		/*
		 * Ensure the odd count is visible while getcpu() isn't
		 * stable, i.e. while changing affinity is in-progress.
		 */
		smp_wmb();
		r = sched_setaffinity(rseq_tid, sizeof(allowed_mask), &allowed_mask);
		TEST_ASSERT(!r, "sched_setaffinity failed, errno = %d (%s)",
			    errno, strerror(errno));
		smp_wmb();
		atomic_inc(&seq_cnt);

		CPU_CLR(cpu, &allowed_mask);

		/*
		 * Wait 1-10us before proceeding to the next iteration and more
		 * specifically, before bumping seq_cnt again.  A delay is
		 * needed on three fronts:
		 *
		 *  1. To allow sched_setaffinity() to prompt migration before
		 *     ioctl(KVM_RUN) enters the guest so that TIF_NOTIFY_RESUME
		 *     (or TIF_NEED_RESCHED, which indirectly leads to handling
		 *     NOTIFY_RESUME) is handled in KVM context.
		 *
		 *     If NOTIFY_RESUME/NEED_RESCHED is set after KVM enters
		 *     the guest, the guest will trigger a IO/MMIO exit all the
		 *     way to userspace and the TIF flags will be handled by
		 *     the generic "exit to userspace" logic, not by KVM.  The
		 *     exit to userspace is necessary to give the test a chance
		 *     to check the rseq CPU ID (see #2).
		 *
		 *     Alternatively, guest_code() could include an instruction
		 *     to trigger an exit that is handled by KVM, but any such
		 *     exit requires architecture specific code.
		 *
		 *  2. To let ioctl(KVM_RUN) make its way back to the test
		 *     before the next round of migration.  The test's check on
		 *     the rseq CPU ID must wait for migration to complete in
		 *     order to avoid false positive, thus any kernel rseq bug
		 *     will be missed if the next migration starts before the
		 *     check completes.
		 *
		 *  3. To ensure the read-side makes efficient forward progress,
		 *     e.g. if getcpu() involves a syscall. Stalling the read-side
		 *     means the test will spend more time waiting for getcpu()
		 *     to stabilize and less time trying to hit the timing-dependent
		 *     bug.
		 *
		 * Because any bug in this area is likely to be timing-dependent,
		 * run with a range of delays at 1us intervals from 1us to 10us
		 * as a best effort to avoid tuning the test to the point where
		 * it can hit _only_ the original bug and not detect future
		 * regressions.
		 *
		 * The original bug can reproduce with a delay up to ~500us on
		 * x86-64, but starts to require more iterations to reproduce
		 * as the delay creeps above ~10us, and the average runtime of
		 * each iteration obviously increases as well.  Cap the delay
		 * at 10us to keep test runtime reasonable while minimizing
		 * potential coverage loss.
		 *
		 * The lower bound for reproducing the bug is likely below 1us,
		 * e.g. failures occur on x86-64 with nanosleep(0), but at that
		 * point the overhead of the syscall likely dominates the delay.
		 * Use usleep() for simplicity and to avoid unnecessary kernel
		 * dependencies.
		 */
		usleep((i % 10) + 1);
	}
	done = true;
	return NULL;
}

static void calc_min_max_cpu(void)
{
	int i, cnt, nproc;

	TEST_REQUIRE(CPU_COUNT(&possible_mask) >= 2);

	/*
	 * CPU_SET doesn't provide a FOR_EACH helper, get the min/max CPU that
	 * this task is affined to in order to reduce the time spent querying
	 * unusable CPUs, e.g. if this task is pinned to a small percentage of
	 * total CPUs.
	 */
	nproc = get_nprocs_conf();
	min_cpu = -1;
	max_cpu = -1;
	cnt = 0;

	for (i = 0; i < nproc; i++) {
		if (!CPU_ISSET(i, &possible_mask))
			continue;
		if (min_cpu == -1)
			min_cpu = i;
		max_cpu = i;
		cnt++;
	}

	__TEST_REQUIRE(cnt >= 2,
		       "Only one usable CPU, task migration not possible");
}

int main(int argc, char *argv[])
{
	int r, i, snapshot;
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	u32 cpu, rseq_cpu;

	r = sched_getaffinity(0, sizeof(possible_mask), &possible_mask);
	TEST_ASSERT(!r, "sched_getaffinity failed, errno = %d (%s)", errno,
		    strerror(errno));

	calc_min_max_cpu();

	r = rseq_register_current_thread();
	TEST_ASSERT(!r, "rseq_register_current_thread failed, errno = %d (%s)",
		    errno, strerror(errno));

	/*
	 * Create and run a dummy VM that immediately exits to userspace via
	 * GUEST_SYNC, while concurrently migrating the process by setting its
	 * CPU affinity.
	 */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	pthread_create(&migration_thread, NULL, migration_worker,
		       (void *)(unsigned long)syscall(SYS_gettid));

	for (i = 0; !done; i++) {
		vcpu_run(vcpu);
		TEST_ASSERT(get_ucall(vcpu, NULL) == UCALL_SYNC,
			    "Guest failed?");

		/*
		 * Verify rseq's CPU matches sched's CPU.  Ensure migration
		 * doesn't occur between getcpu() and reading the rseq cpu_id
		 * by rereading both if the sequence count changes, or if the
		 * count is odd (migration in-progress).
		 */
		do {
			/*
			 * Drop bit 0 to force a mismatch if the count is odd,
			 * i.e. if a migration is in-progress.
			 */
			snapshot = atomic_read(&seq_cnt) & ~1;

			/*
			 * Ensure calling getcpu() and reading rseq.cpu_id complete
			 * in a single "no migration" window, i.e. are not reordered
			 * across the seq_cnt reads.
			 */
			smp_rmb();
			r = sys_getcpu(&cpu, NULL);
			TEST_ASSERT(!r, "getcpu failed, errno = %d (%s)",
				    errno, strerror(errno));
			rseq_cpu = rseq_current_cpu_raw();
			smp_rmb();
		} while (snapshot != atomic_read(&seq_cnt));

		TEST_ASSERT(rseq_cpu == cpu,
			    "rseq CPU = %d, sched CPU = %d\n", rseq_cpu, cpu);
	}

	/*
	 * Sanity check that the test was able to enter the guest a reasonable
	 * number of times, e.g. didn't get stalled too often/long waiting for
	 * getcpu() to stabilize.  A 2:1 migration:KVM_RUN ratio is a fairly
	 * conservative ratio on x86-64, which can do _more_ KVM_RUNs than
	 * migrations given the 1us+ delay in the migration task.
	 */
	TEST_ASSERT(i > (NR_TASK_MIGRATIONS / 2),
		    "Only performed %d KVM_RUNs, task stalled too much?\n", i);

	pthread_join(migration_thread, NULL);

	kvm_vm_free(vm);

	rseq_unregister_current_thread();

	return 0;
}
