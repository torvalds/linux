// SPDX-License-Identifier: GPL-2.0
/*
 * Tests Memory Protection Keys (see Documentation/core-api/protection-keys.rst)
 *
 * The testcases in this file exercise various flows related to signal handling,
 * using an alternate signal stack, with the default pkey (pkey 0) disabled.
 *
 * Compile with:
 * gcc -mxsave      -o pkey_sighandler_tests -O2 -g -std=gnu99 -pthread -Wall pkey_sighandler_tests.c -I../../../../tools/include -lrt -ldl -lm
 * gcc -mxsave -m32 -o pkey_sighandler_tests -O2 -g -std=gnu99 -pthread -Wall pkey_sighandler_tests.c -I../../../../tools/include -lrt -ldl -lm
 */
#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__
#include <linux/mman.h>
#include <errno.h>
#include <sys/syscall.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>

#include "pkey-helpers.h"

#define STACK_SIZE PTHREAD_STACK_MIN

void expected_pkey_fault(int pkey) {}

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
siginfo_t siginfo = {0};

/*
 * We need to use inline assembly instead of glibc's syscall because glibc's
 * syscall will attempt to access the PLT in order to call a library function
 * which is protected by MPK 0 which we don't have access to.
 */
static inline __always_inline
long syscall_raw(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
	unsigned long ret;
#ifdef __x86_64__
	register long r10 asm("r10") = a4;
	register long r8 asm("r8") = a5;
	register long r9 asm("r9") = a6;
	asm volatile ("syscall"
		      : "=a"(ret)
		      : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
		      : "rcx", "r11", "memory");
#elif defined __i386__
	asm volatile ("int $0x80"
		      : "=a"(ret)
		      : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
		      : "memory");
#elif defined __aarch64__
	register long x0 asm("x0") = a1;
	register long x1 asm("x1") = a2;
	register long x2 asm("x2") = a3;
	register long x3 asm("x3") = a4;
	register long x4 asm("x4") = a5;
	register long x5 asm("x5") = a6;
	register long x8 asm("x8") = n;
	asm volatile ("svc #0"
		      : "=r"(x0)
		      : "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
		      : "memory");
	ret = x0;
#else
# error syscall_raw() not implemented
#endif
	return ret;
}

static inline long clone_raw(unsigned long flags, void *stack,
			     int *parent_tid, int *child_tid)
{
	long a1 = flags;
	long a2 = (long)stack;
	long a3 = (long)parent_tid;
#if defined(__x86_64__) || defined(__i386)
	long a4 = (long)child_tid;
	long a5 = 0;
#elif defined(__aarch64__)
	long a4 = 0;
	long a5 = (long)child_tid;
#else
# error clone_raw() not implemented
#endif

	return syscall_raw(SYS_clone, a1, a2, a3, a4, a5, 0);
}

/*
 * Returns the most restrictive pkey register value that can be used by the
 * tests.
 */
static inline u64 pkey_reg_restrictive_default(void)
{
	/*
	 * Disallow everything except execution on pkey 0, so that each caller
	 * doesn't need to enable it explicitly (the selftest code runs with
	 * its code mapped with pkey 0).
	 */
	return set_pkey_bits(PKEY_REG_ALLOW_NONE, 0, PKEY_DISABLE_ACCESS);
}

static void sigsegv_handler(int signo, siginfo_t *info, void *ucontext)
{
	pthread_mutex_lock(&mutex);

	memcpy(&siginfo, info, sizeof(siginfo_t));

	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);

	syscall_raw(SYS_exit, 0, 0, 0, 0, 0, 0);
}

static void sigusr1_handler(int signo, siginfo_t *info, void *ucontext)
{
	pthread_mutex_lock(&mutex);

	memcpy(&siginfo, info, sizeof(siginfo_t));

	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
}

static void sigusr2_handler(int signo, siginfo_t *info, void *ucontext)
{
	/*
	 * pkru should be the init_pkru value which enabled MPK 0 so
	 * we can use library functions.
	 */
	printf("%s invoked.\n", __func__);
}

static void raise_sigusr2(void)
{
	pid_t tid = 0;

	tid = syscall_raw(SYS_gettid, 0, 0, 0, 0, 0, 0);

	syscall_raw(SYS_tkill, tid, SIGUSR2, 0, 0, 0, 0);

	/*
	 * We should return from the signal handler here and be able to
	 * return to the interrupted thread.
	 */
}

static void *thread_segv_with_pkey0_disabled(void *ptr)
{
	/* Disable MPK 0 (and all others too) */
	__write_pkey_reg(pkey_reg_restrictive_default());

	/* Segfault (with SEGV_MAPERR) */
	*(int *) (0x1) = 1;
	return NULL;
}

static void *thread_segv_pkuerr_stack(void *ptr)
{
	/* Disable MPK 0 (and all others too) */
	__write_pkey_reg(pkey_reg_restrictive_default());

	/* After we disable MPK 0, we can't access the stack to return */
	return NULL;
}

static void *thread_segv_maperr_ptr(void *ptr)
{
	stack_t *stack = ptr;
	int *bad = (int *)1;
	u64 pkey_reg;

	/*
	 * Setup alternate signal stack, which should be pkey_mprotect()ed by
	 * MPK 0. The thread's stack cannot be used for signals because it is
	 * not accessible by the default init_pkru value of 0x55555554.
	 */
	syscall_raw(SYS_sigaltstack, (long)stack, 0, 0, 0, 0, 0);

	/* Disable MPK 0.  Only MPK 1 is enabled. */
	pkey_reg = pkey_reg_restrictive_default();
	pkey_reg = set_pkey_bits(pkey_reg, 1, PKEY_UNRESTRICTED);
	__write_pkey_reg(pkey_reg);

	/* Segfault */
	*bad = 1;
	syscall_raw(SYS_exit, 0, 0, 0, 0, 0, 0);
	return NULL;
}

/*
 * Verify that the sigsegv handler is invoked when pkey 0 is disabled.
 * Note that the new thread stack and the alternate signal stack is
 * protected by MPK 0.
 */
static void test_sigsegv_handler_with_pkey0_disabled(void)
{
	struct sigaction sa;
	pthread_attr_t attr;
	pthread_t thr;

	sa.sa_flags = SA_SIGINFO;

	sa.sa_sigaction = sigsegv_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGSEGV, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	memset(&siginfo, 0, sizeof(siginfo));

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_create(&thr, &attr, thread_segv_with_pkey0_disabled, NULL);

	pthread_mutex_lock(&mutex);
	while (siginfo.si_signo == 0)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);

	ksft_test_result(siginfo.si_signo == SIGSEGV &&
			 siginfo.si_code == SEGV_MAPERR &&
			 siginfo.si_addr == (void *)1,
			 "%s\n", __func__);
}

/*
 * Verify that the sigsegv handler is invoked when pkey 0 is disabled.
 * Note that the new thread stack and the alternate signal stack is
 * protected by MPK 0, which renders them inaccessible when MPK 0
 * is disabled. So just the return from the thread should cause a
 * segfault with SEGV_PKUERR.
 */
static void test_sigsegv_handler_cannot_access_stack(void)
{
	struct sigaction sa;
	pthread_attr_t attr;
	pthread_t thr;

	sa.sa_flags = SA_SIGINFO;

	sa.sa_sigaction = sigsegv_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGSEGV, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	memset(&siginfo, 0, sizeof(siginfo));

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_create(&thr, &attr, thread_segv_pkuerr_stack, NULL);

	pthread_mutex_lock(&mutex);
	while (siginfo.si_signo == 0)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);

	ksft_test_result(siginfo.si_signo == SIGSEGV &&
			 siginfo.si_code == SEGV_PKUERR,
			 "%s\n", __func__);
}

/*
 * Verify that the sigsegv handler that uses an alternate signal stack
 * is correctly invoked for a thread which uses a non-zero MPK to protect
 * its own stack, and disables all other MPKs (including 0).
 */
static void test_sigsegv_handler_with_different_pkey_for_stack(void)
{
	struct sigaction sa;
	static stack_t sigstack;
	void *stack;
	int pkey;
	int parent_pid = 0;
	int child_pid = 0;
	u64 pkey_reg;

	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

	sa.sa_sigaction = sigsegv_handler;

	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGSEGV, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	stack = mmap(0, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	assert(stack != MAP_FAILED);

	/* Allow access to MPK 0 and MPK 1 */
	pkey_reg = pkey_reg_restrictive_default();
	pkey_reg = set_pkey_bits(pkey_reg, 0, PKEY_UNRESTRICTED);
	pkey_reg = set_pkey_bits(pkey_reg, 1, PKEY_UNRESTRICTED);
	__write_pkey_reg(pkey_reg);

	/* Protect the new stack with MPK 1 */
	pkey = pkey_alloc(0, 0);
	pkey_mprotect(stack, STACK_SIZE, PROT_READ | PROT_WRITE, pkey);

	/* Set up alternate signal stack that will use the default MPK */
	sigstack.ss_sp = mmap(0, STACK_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	sigstack.ss_flags = 0;
	sigstack.ss_size = STACK_SIZE;

	memset(&siginfo, 0, sizeof(siginfo));

	/* Use clone to avoid newer glibcs using rseq on new threads */
	long ret = clone_raw(CLONE_VM | CLONE_FS | CLONE_FILES |
			     CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
			     CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID |
			     CLONE_DETACHED,
			     stack + STACK_SIZE,
			     &parent_pid,
			     &child_pid);

	if (ret < 0) {
		errno = -ret;
		perror("clone");
	} else if (ret == 0) {
		thread_segv_maperr_ptr(&sigstack);
		syscall_raw(SYS_exit, 0, 0, 0, 0, 0, 0);
	}

	pthread_mutex_lock(&mutex);
	while (siginfo.si_signo == 0)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);

	ksft_test_result(siginfo.si_signo == SIGSEGV &&
			 siginfo.si_code == SEGV_MAPERR &&
			 siginfo.si_addr == (void *)1,
			 "%s\n", __func__);
}

/*
 * Verify that the PKRU value set by the application is correctly
 * restored upon return from signal handling.
 */
static void test_pkru_preserved_after_sigusr1(void)
{
	struct sigaction sa;
	u64 pkey_reg;

	/* Allow access to MPK 0 and an arbitrary set of keys */
	pkey_reg = pkey_reg_restrictive_default();
	pkey_reg = set_pkey_bits(pkey_reg, 0, PKEY_UNRESTRICTED);
	pkey_reg = set_pkey_bits(pkey_reg, 3, PKEY_UNRESTRICTED);
	pkey_reg = set_pkey_bits(pkey_reg, 7, PKEY_UNRESTRICTED);

	sa.sa_flags = SA_SIGINFO;

	sa.sa_sigaction = sigusr1_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	memset(&siginfo, 0, sizeof(siginfo));

	__write_pkey_reg(pkey_reg);

	raise(SIGUSR1);

	pthread_mutex_lock(&mutex);
	while (siginfo.si_signo == 0)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);

	/* Ensure the pkru value is the same after returning from signal. */
	ksft_test_result(pkey_reg == __read_pkey_reg() &&
			 siginfo.si_signo == SIGUSR1,
			 "%s\n", __func__);
}

static noinline void *thread_sigusr2_self(void *ptr)
{
	/*
	 * A const char array like "Resuming after SIGUSR2" won't be stored on
	 * the stack and the code could access it via an offset from the program
	 * counter. This makes sure it's on the function's stack frame.
	 */
	char str[] = {'R', 'e', 's', 'u', 'm', 'i', 'n', 'g', ' ',
		'a', 'f', 't', 'e', 'r', ' ',
		'S', 'I', 'G', 'U', 'S', 'R', '2',
		'.', '.', '.', '\n', '\0'};
	stack_t *stack = ptr;
	u64 pkey_reg;

	/*
	 * Setup alternate signal stack, which should be pkey_mprotect()ed by
	 * MPK 0. The thread's stack cannot be used for signals because it is
	 * not accessible by the default init_pkru value of 0x55555554.
	 */
	syscall(SYS_sigaltstack, (long)stack, 0, 0, 0, 0, 0);

	/* Disable MPK 0.  Only MPK 2 is enabled. */
	pkey_reg = pkey_reg_restrictive_default();
	pkey_reg = set_pkey_bits(pkey_reg, 2, PKEY_UNRESTRICTED);
	__write_pkey_reg(pkey_reg);

	raise_sigusr2();

	/* Do something, to show the thread resumed execution after the signal */
	syscall_raw(SYS_write, 1, (long) str, sizeof(str) - 1, 0, 0, 0);

	/*
	 * We can't return to test_pkru_sigreturn because it
	 * will attempt to use a %rbp value which is on the stack
	 * of the main thread.
	 */
	syscall_raw(SYS_exit, 0, 0, 0, 0, 0, 0);
	return NULL;
}

/*
 * Verify that sigreturn is able to restore altstack even if the thread had
 * disabled pkey 0.
 */
static void test_pkru_sigreturn(void)
{
	struct sigaction sa = {0};
	static stack_t sigstack;
	void *stack;
	int pkey;
	int parent_pid = 0;
	int child_pid = 0;
	u64 pkey_reg;

	sa.sa_handler = SIG_DFL;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);

	/*
	 * For this testcase, we do not want to handle SIGSEGV. Reset handler
	 * to default so that the application can crash if it receives SIGSEGV.
	 */
	if (sigaction(SIGSEGV, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sa.sa_sigaction = sigusr2_handler;
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGUSR2, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	stack = mmap(0, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	assert(stack != MAP_FAILED);

	/*
	 * Allow access to MPK 0 and MPK 2. The child thread (to be created
	 * later in this flow) will have its stack protected by MPK 2, whereas
	 * the current thread's stack is protected by the default MPK 0. Hence
	 * both need to be enabled.
	 */
	pkey_reg = pkey_reg_restrictive_default();
	pkey_reg = set_pkey_bits(pkey_reg, 0, PKEY_UNRESTRICTED);
	pkey_reg = set_pkey_bits(pkey_reg, 2, PKEY_UNRESTRICTED);
	__write_pkey_reg(pkey_reg);

	/* Protect the stack with MPK 2 */
	pkey = pkey_alloc(0, 0);
	pkey_mprotect(stack, STACK_SIZE, PROT_READ | PROT_WRITE, pkey);

	/* Set up alternate signal stack that will use the default MPK */
	sigstack.ss_sp = mmap(0, STACK_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	sigstack.ss_flags = 0;
	sigstack.ss_size = STACK_SIZE;

	/* Use clone to avoid newer glibcs using rseq on new threads */
	long ret = clone_raw(CLONE_VM | CLONE_FS | CLONE_FILES |
			     CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
			     CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID |
			     CLONE_DETACHED,
			     stack + STACK_SIZE,
			     &parent_pid,
			     &child_pid);

	if (ret < 0) {
		errno = -ret;
		perror("clone");
	}  else if (ret == 0) {
		thread_sigusr2_self(&sigstack);
		syscall_raw(SYS_exit, 0, 0, 0, 0, 0, 0);
	}

	child_pid =  ret;
	/* Check that thread exited */
	do {
		sched_yield();
		ret = syscall_raw(SYS_tkill, child_pid, 0, 0, 0, 0, 0);
	} while (ret != -ESRCH && ret != -EINVAL);

	ksft_test_result_pass("%s\n", __func__);
}

static void (*pkey_tests[])(void) = {
	test_sigsegv_handler_with_pkey0_disabled,
	test_sigsegv_handler_cannot_access_stack,
	test_sigsegv_handler_with_different_pkey_for_stack,
	test_pkru_preserved_after_sigusr1,
	test_pkru_sigreturn
};

int main(int argc, char *argv[])
{
	int i;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(pkey_tests));

	for (i = 0; i < ARRAY_SIZE(pkey_tests); i++)
		(*pkey_tests[i])();

	ksft_finished();
	return 0;
}
