// SPDX-License-Identifier: GPL-2.0-only
/*
 * This test is intended to reproduce a crash that happens when
 * kvm_arch_hardware_disable is called and it attempts to unregister the user
 * return notifiers.
 */
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include <test_util.h>

#include "kvm_syscalls.h"
#include "kvm_util.h"
#include "ucall_common.h"

#define NR_VCPUS		4
#define NR_SLEEPERS_PER_VCPU	16
#define NR_ITERATIONS		512
#define DELAY_US_MAX		2000

static cpu_set_t threads_cpu_set;
static sem_t *sem;

static void guest_code(void)
{
	for (;;)
		;  /* Some busy work */
	GUEST_ASSERT(0);
}

static void *run_vcpu(void *arg)
{
	struct kvm_vcpu *vcpu = arg;
	struct kvm_run *run = vcpu->run;

#ifndef _GNU_SOURCE
	kvm_sched_setaffinity(0, sizeof(cpu_set_t), &threads_cpu_set);
#endif

	vcpu_run(vcpu);

	TEST_FAIL("vCPU%d exited with reason %d: %s",
		  vcpu->id, run->exit_reason, exit_reason_str(run->exit_reason));
}

static void *sleeping_thread(void *arg)
{
	int fd;

#ifndef _GNU_SOURCE
	kvm_sched_setaffinity(0, sizeof(cpu_set_t), &threads_cpu_set);
#endif

	while (1) {
		fd = open("/dev/null", O_RDWR);
		close(fd);
	}
	TEST_FAIL("%s: exited", __func__);
}

static void run_test(u32 run)
{
	struct kvm_vcpu *vcpu;
	pthread_attr_t attr;
	struct kvm_vm *vm;
	pthread_t thread;
	u32 i, j;

	TEST_ASSERT_EQ(pthread_attr_init(&attr), 0);
#ifdef _GNU_SOURCE
	TEST_ASSERT_EQ(pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &threads_cpu_set), 0);
#endif

	vm = vm_create(NR_VCPUS);

	pr_debug("%s: [%d] start vcpus\n", __func__, run);
	for (i = 0; i < NR_VCPUS; ++i) {
		vcpu = vm_vcpu_add(vm, i, guest_code);

		kvm_pthread_create(&thread, &attr, run_vcpu, vcpu);

		for (j = 0; j < NR_SLEEPERS_PER_VCPU; ++j)
			kvm_pthread_create(&thread, &attr, sleeping_thread, (void *)NULL);
	}
	pr_debug("%s: [%d] all threads launched\n", __func__, run);
	sem_post(sem);

	/* Wait for the parent to SIGKILL this child. */
	while (1)
		pause();
}

void wait_for_child_setup(pid_t pid)
{
	/*
	 * Wait for the child to post to the semaphore, but wake up periodically
	 * to check if the child exited prematurely.
	 */
	for (;;) {
		const struct timespec wait_period = { .tv_sec = 1 };
		int status;

		if (!sem_timedwait(sem, &wait_period))
			return;

		/* Child is still running, keep waiting. */
		if (pid != waitpid(pid, &status, WNOHANG))
			continue;

		/*
		 * Child is no longer running, which is not expected.
		 *
		 * If it exited with a non-zero status, we explicitly forward
		 * the child's status in case it exited with KSFT_SKIP.
		 */
		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));
		else
			TEST_ASSERT(false, "Child exited unexpectedly");
	}
}

int main(int argc, char **argv)
{
	cpu_set_t allowed_cpu_set;
	int s, r, cpu, i;
	pid_t pid;

	kvm_sched_getaffinity(0, sizeof(cpu_set_t), &allowed_cpu_set);

	for (i = 0; i < NR_VCPUS && CPU_COUNT(&allowed_cpu_set); i++) {
		cpu = kvm_pick_random_cpu(&allowed_cpu_set);
		CPU_CLR(cpu, &allowed_cpu_set);
		CPU_SET(cpu, &threads_cpu_set);
	}

	sem = sem_open("vm_sem", O_CREAT | O_EXCL, 0644, 0);
	sem_unlink("vm_sem");

	for (i = 0; i < NR_ITERATIONS; ++i) {
		pid = fork();
		TEST_ASSERT(pid >= 0, "%s: unable to fork", __func__);
		if (pid == 0)
			run_test(i); /* This function always exits */

		pr_debug("%s: [%d] waiting semaphore\n", __func__, i);
		wait_for_child_setup(pid);
		r = (rand() % DELAY_US_MAX) + 1;
		pr_debug("%s: [%d] waiting %dus\n", __func__, i, r);
		usleep(r);
		r = waitpid(pid, &s, WNOHANG);
		TEST_ASSERT(r != pid,
			    "%s: [%d] child exited unexpectedly status: [%d]",
			    __func__, i, s);
		pr_debug("%s: [%d] killing child\n", __func__, i);
		kill(pid, SIGKILL);
	}

	sem_destroy(sem);
	exit(0);
}
