// SPDX-License-Identifier: GPL-2.0-only
/*
 * This test is intended to reproduce a crash that happens when
 * kvm_arch_hardware_disable is called and it attempts to unregister the user
 * return notifiers.
 */
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include <test_util.h>

#include "kvm_util.h"

#define VCPU_NUM 4
#define SLEEPING_THREAD_NUM (1 << 4)
#define FORK_NUM (1ULL << 9)
#define DELAY_US_MAX 2000

sem_t *sem;

static void guest_code(void)
{
	for (;;)
		;  /* Some busy work */
	printf("Should not be reached.\n");
}

static void *run_vcpu(void *arg)
{
	struct kvm_vcpu *vcpu = arg;
	struct kvm_run *run = vcpu->run;

	vcpu_run(vcpu);

	TEST_ASSERT(false, "%s: exited with reason %d: %s",
		    __func__, run->exit_reason,
		    exit_reason_str(run->exit_reason));
	pthread_exit(NULL);
}

static void *sleeping_thread(void *arg)
{
	int fd;

	while (true) {
		fd = open("/dev/null", O_RDWR);
		close(fd);
	}
	TEST_ASSERT(false, "%s: exited", __func__);
	pthread_exit(NULL);
}

static inline void check_create_thread(pthread_t *thread, pthread_attr_t *attr,
				       void *(*f)(void *), void *arg)
{
	int r;

	r = pthread_create(thread, attr, f, arg);
	TEST_ASSERT(r == 0, "%s: failed to create thread", __func__);
}

static inline void check_set_affinity(pthread_t thread, cpu_set_t *cpu_set)
{
	int r;

	r = pthread_setaffinity_np(thread, sizeof(cpu_set_t), cpu_set);
	TEST_ASSERT(r == 0, "%s: failed set affinity", __func__);
}

static inline void check_join(pthread_t thread, void **retval)
{
	int r;

	r = pthread_join(thread, retval);
	TEST_ASSERT(r == 0, "%s: failed to join thread", __func__);
}

static void run_test(uint32_t run)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	cpu_set_t cpu_set;
	pthread_t threads[VCPU_NUM];
	pthread_t throw_away;
	void *b;
	uint32_t i, j;

	CPU_ZERO(&cpu_set);
	for (i = 0; i < VCPU_NUM; i++)
		CPU_SET(i, &cpu_set);

	vm = vm_create(VCPU_NUM);

	pr_debug("%s: [%d] start vcpus\n", __func__, run);
	for (i = 0; i < VCPU_NUM; ++i) {
		vcpu = vm_vcpu_add(vm, i, guest_code);

		check_create_thread(&threads[i], NULL, run_vcpu, vcpu);
		check_set_affinity(threads[i], &cpu_set);

		for (j = 0; j < SLEEPING_THREAD_NUM; ++j) {
			check_create_thread(&throw_away, NULL, sleeping_thread,
					    (void *)NULL);
			check_set_affinity(throw_away, &cpu_set);
		}
	}
	pr_debug("%s: [%d] all threads launched\n", __func__, run);
	sem_post(sem);
	for (i = 0; i < VCPU_NUM; ++i)
		check_join(threads[i], &b);
	/* Should not be reached */
	TEST_ASSERT(false, "%s: [%d] child escaped the ninja", __func__, run);
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
	uint32_t i;
	int s, r;
	pid_t pid;

	sem = sem_open("vm_sem", O_CREAT | O_EXCL, 0644, 0);
	sem_unlink("vm_sem");

	for (i = 0; i < FORK_NUM; ++i) {
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
