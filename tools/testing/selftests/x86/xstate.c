// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <elf.h>
#include <pthread.h>
#include <stdbool.h>

#include <asm/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include "helpers.h"
#include "xstate.h"

/*
 * The userspace xstate test suite is designed to be generic and operates
 * with randomized xstate data. However, some states require special handling:
 *
 * - PKRU and XTILECFG need specific adjustments, such as modifying
 *   randomization behavior or using fixed values.
 * - But, PKRU already has a dedicated test suite in /tools/selftests/mm.
 * - Legacy states (FP and SSE) are excluded, as they are not considered
 *   part of extended states (xstates) and their usage is already deeply
 *   integrated into user-space libraries.
 */
#define XFEATURE_MASK_TEST_SUPPORTED	\
	((1 << XFEATURE_YMM) |		\
	 (1 << XFEATURE_OPMASK)	|	\
	 (1 << XFEATURE_ZMM_Hi256) |	\
	 (1 << XFEATURE_Hi16_ZMM) |	\
	 (1 << XFEATURE_XTILEDATA) |	\
	 (1 << XFEATURE_APX))

static inline uint64_t xgetbv(uint32_t index)
{
	uint32_t eax, edx;

	asm volatile("xgetbv" : "=a" (eax), "=d" (edx) : "c" (index));
	return eax + ((uint64_t)edx << 32);
}

static inline uint64_t get_xstatebv(struct xsave_buffer *xbuf)
{
	return *(uint64_t *)(&xbuf->header);
}

static struct xstate_info xstate;

struct futex_info {
	unsigned int iterations;
	struct futex_info *next;
	pthread_mutex_t mutex;
	pthread_t thread;
	bool valid;
	int nr;
};

static inline void load_rand_xstate(struct xstate_info *xstate, struct xsave_buffer *xbuf)
{
	clear_xstate_header(xbuf);
	set_xstatebv(xbuf, xstate->mask);
	set_rand_data(xstate, xbuf);
	xrstor(xbuf, xstate->mask);
}

static inline void load_init_xstate(struct xstate_info *xstate, struct xsave_buffer *xbuf)
{
	clear_xstate_header(xbuf);
	xrstor(xbuf, xstate->mask);
}

static inline void copy_xstate(struct xsave_buffer *xbuf_dst, struct xsave_buffer *xbuf_src)
{
	memcpy(&xbuf_dst->bytes[xstate.xbuf_offset],
	       &xbuf_src->bytes[xstate.xbuf_offset],
	       xstate.size);
}

static inline bool validate_xstate_same(struct xsave_buffer *xbuf1, struct xsave_buffer *xbuf2)
{
	int ret;

	ret = memcmp(&xbuf1->bytes[xstate.xbuf_offset],
		     &xbuf2->bytes[xstate.xbuf_offset],
		     xstate.size);
	return ret == 0;
}

static inline bool validate_xregs_same(struct xsave_buffer *xbuf1)
{
	struct xsave_buffer *xbuf2;
	bool ret;

	xbuf2 = alloc_xbuf();
	if (!xbuf2)
		ksft_exit_fail_msg("failed to allocate XSAVE buffer\n");

	xsave(xbuf2, xstate.mask);
	ret = validate_xstate_same(xbuf1, xbuf2);

	free(xbuf2);
	return ret;
}

/* Context switching test */

static void *check_xstate(void *info)
{
	struct futex_info *finfo = (struct futex_info *)info;
	struct xsave_buffer *xbuf;
	int i;

	xbuf = alloc_xbuf();
	if (!xbuf)
		ksft_exit_fail_msg("unable to allocate XSAVE buffer\n");

	/*
	 * Load random data into 'xbuf' and then restore it to the xstate
	 * registers.
	 */
	load_rand_xstate(&xstate, xbuf);
	finfo->valid = true;

	for (i = 0; i < finfo->iterations; i++) {
		pthread_mutex_lock(&finfo->mutex);

		/*
		 * Ensure the register values have not diverged from the
		 * record. Then reload a new random value.  If it failed
		 * ever before, skip it.
		 */
		if (finfo->valid) {
			finfo->valid = validate_xregs_same(xbuf);
			load_rand_xstate(&xstate, xbuf);
		}

		/*
		 * The last thread's last unlock will be for thread 0's
		 * mutex. However, thread 0 will have already exited the
		 * loop and the mutex will already be unlocked.
		 *
		 * Because this is not an ERRORCHECK mutex, that
		 * inconsistency will be silently ignored.
		 */
		pthread_mutex_unlock(&finfo->next->mutex);
	}

	free(xbuf);
	return finfo;
}

static void create_threads(uint32_t num_threads, uint32_t iterations, struct futex_info *finfo)
{
	int i;

	for (i = 0; i < num_threads; i++) {
		int next_nr;

		finfo[i].nr = i;
		finfo[i].iterations = iterations;

		/*
		 * Thread 'i' will wait on this mutex to be unlocked.
		 * Lock it immediately after initialization:
		 */
		pthread_mutex_init(&finfo[i].mutex, NULL);
		pthread_mutex_lock(&finfo[i].mutex);

		next_nr = (i + 1) % num_threads;
		finfo[i].next = &finfo[next_nr];

		if (pthread_create(&finfo[i].thread, NULL, check_xstate, &finfo[i]))
			ksft_exit_fail_msg("pthread_create() failed\n");
	}
}

static bool checkout_threads(uint32_t num_threads, struct futex_info *finfo)
{
	void *thread_retval;
	bool valid = true;
	int err, i;

	for (i = 0; i < num_threads; i++) {
		err = pthread_join(finfo[i].thread, &thread_retval);
		if (err)
			ksft_exit_fail_msg("pthread_join() failed for thread %d err: %d\n", i, err);

		if (thread_retval != &finfo[i]) {
			ksft_exit_fail_msg("unexpected thread retval for thread %d: %p\n",
					   i, thread_retval);
		}

		valid &= finfo[i].valid;
	}

	return valid;
}

static void affinitize_cpu0(void)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);

	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		ksft_exit_fail_msg("sched_setaffinity to CPU 0 failed\n");
}

static void test_context_switch(uint32_t num_threads, uint32_t iterations)
{
	struct futex_info *finfo;

	/* Affinitize to one CPU to force context switches */
	affinitize_cpu0();

	printf("[RUN]\t%s: check context switches, %d iterations, %d threads.\n",
	       xstate.name, iterations, num_threads);

	finfo = malloc(sizeof(*finfo) * num_threads);
	if (!finfo)
		ksft_exit_fail_msg("unable allocate memory\n");

	create_threads(num_threads, iterations, finfo);

	/*
	 * This thread wakes up thread 0
	 * Thread 0 will wake up 1
	 * Thread 1 will wake up 2
	 * ...
	 * The last thread will wake up 0
	 *
	 * This will repeat for the configured
	 * number of iterations.
	 */
	pthread_mutex_unlock(&finfo[0].mutex);

	/* Wait for all the threads to finish: */
	if (checkout_threads(num_threads, finfo))
		printf("[OK]\tNo incorrect case was found.\n");
	else
		printf("[FAIL]\tFailed with context switching test.\n");

	free(finfo);
}

/*
 * Ptrace test for the ABI format as described in arch/x86/include/asm/user.h
 */

/*
 * Make sure the ptracee has the expanded kernel buffer on the first use.
 * Then, initialize the state before performing the state injection from
 * the ptracer. For non-dynamic states, this is benign.
 */
static inline void ptracee_touch_xstate(void)
{
	struct xsave_buffer *xbuf;

	xbuf = alloc_xbuf();

	load_rand_xstate(&xstate, xbuf);
	load_init_xstate(&xstate, xbuf);

	free(xbuf);
}

/*
 * Ptracer injects the randomized xstate data. It also reads before and
 * after that, which will execute the kernel's state copy functions.
 */
static void ptracer_inject_xstate(pid_t target)
{
	uint32_t xbuf_size = get_xbuf_size();
	struct xsave_buffer *xbuf1, *xbuf2;
	struct iovec iov;

	/*
	 * Allocate buffers to keep data while ptracer can write the
	 * other buffer
	 */
	xbuf1 = alloc_xbuf();
	xbuf2 = alloc_xbuf();
	if (!xbuf1 || !xbuf2)
		ksft_exit_fail_msg("unable to allocate XSAVE buffer\n");

	iov.iov_base = xbuf1;
	iov.iov_len  = xbuf_size;

	if (ptrace(PTRACE_GETREGSET, target, (uint32_t)NT_X86_XSTATE, &iov))
		ksft_exit_fail_msg("PTRACE_GETREGSET failed\n");

	printf("[RUN]\t%s: inject xstate via ptrace().\n", xstate.name);

	load_rand_xstate(&xstate, xbuf1);
	copy_xstate(xbuf2, xbuf1);

	if (ptrace(PTRACE_SETREGSET, target, (uint32_t)NT_X86_XSTATE, &iov))
		ksft_exit_fail_msg("PTRACE_SETREGSET failed\n");

	if (ptrace(PTRACE_GETREGSET, target, (uint32_t)NT_X86_XSTATE, &iov))
		ksft_exit_fail_msg("PTRACE_GETREGSET failed\n");

	if (*(uint64_t *)get_fpx_sw_bytes(xbuf1) == xgetbv(0))
		printf("[OK]\t'xfeatures' in SW reserved area was correctly written\n");
	else
		printf("[FAIL]\t'xfeatures' in SW reserved area was not correctly written\n");

	if (validate_xstate_same(xbuf2, xbuf1))
		printf("[OK]\txstate was correctly updated.\n");
	else
		printf("[FAIL]\txstate was not correctly updated.\n");

	free(xbuf1);
	free(xbuf2);
}

static void test_ptrace(void)
{
	pid_t child;
	int status;

	child = fork();
	if (child < 0) {
		ksft_exit_fail_msg("fork() failed\n");
	} else if (!child) {
		if (ptrace(PTRACE_TRACEME, 0, NULL, NULL))
			ksft_exit_fail_msg("PTRACE_TRACEME failed\n");

		ptracee_touch_xstate();

		raise(SIGTRAP);
		_exit(0);
	}

	do {
		wait(&status);
	} while (WSTOPSIG(status) != SIGTRAP);

	ptracer_inject_xstate(child);

	ptrace(PTRACE_DETACH, child, NULL, NULL);
	wait(&status);
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		ksft_exit_fail_msg("ptracee exit error\n");
}

/*
 * Test signal delivery for the ABI compatibility.
 * See the ABI format: arch/x86/include/uapi/asm/sigcontext.h
 */

/*
 * Avoid using printf() in signal handlers as it is not
 * async-signal-safe.
 */
#define SIGNAL_BUF_LEN 1000
static char signal_message_buffer[SIGNAL_BUF_LEN];
static void sig_print(char *msg)
{
	int left = SIGNAL_BUF_LEN - strlen(signal_message_buffer) - 1;

	strncat(signal_message_buffer, msg, left);
}

static struct xsave_buffer *stashed_xbuf;

static void validate_sigfpstate(int sig, siginfo_t *si, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t *)ctx_void;
	void *xbuf = ctx->uc_mcontext.fpregs;
	struct _fpx_sw_bytes *sw_bytes;
	uint32_t magic2;

	/* Reset the signal message buffer: */
	signal_message_buffer[0] = '\0';

	sw_bytes = get_fpx_sw_bytes(xbuf);
	if (sw_bytes->magic1 == FP_XSTATE_MAGIC1)
		sig_print("[OK]\t'magic1' is valid\n");
	else
		sig_print("[FAIL]\t'magic1' is not valid\n");

	if (get_fpx_sw_bytes_features(xbuf) & xstate.mask)
		sig_print("[OK]\t'xfeatures' in SW reserved area is valid\n");
	else
		sig_print("[FAIL]\t'xfeatures' in SW reserved area is not valid\n");

	if (get_xstatebv(xbuf) & xstate.mask)
		sig_print("[OK]\t'xfeatures' in XSAVE header is valid\n");
	else
		sig_print("[FAIL]\t'xfeatures' in XSAVE header is not valid\n");

	if (validate_xstate_same(stashed_xbuf, xbuf))
		sig_print("[OK]\txstate delivery was successful\n");
	else
		sig_print("[FAIL]\txstate delivery was not successful\n");

	magic2 = *(uint32_t *)(xbuf + sw_bytes->xstate_size);
	if (magic2 == FP_XSTATE_MAGIC2)
		sig_print("[OK]\t'magic2' is valid\n");
	else
		sig_print("[FAIL]\t'magic2' is not valid\n");

	set_rand_data(&xstate, xbuf);
	copy_xstate(stashed_xbuf, xbuf);
}

static void test_signal(void)
{
	bool valid_xstate;

	/*
	 * The signal handler will access this to verify xstate context
	 * preservation.
	 */
	stashed_xbuf = alloc_xbuf();
	if (!stashed_xbuf)
		ksft_exit_fail_msg("unable to allocate XSAVE buffer\n");

	printf("[RUN]\t%s: load xstate and raise SIGUSR1\n", xstate.name);

	sethandler(SIGUSR1, validate_sigfpstate, 0);

	load_rand_xstate(&xstate, stashed_xbuf);

	raise(SIGUSR1);

	/*
	 * Immediately record the test result, deferring printf() to
	 * prevent unintended state contamination by that.
	 */
	valid_xstate = validate_xregs_same(stashed_xbuf);
	printf("%s", signal_message_buffer);

	printf("[RUN]\t%s: load new xstate from sighandler and check it after sigreturn\n",
	       xstate.name);

	if (valid_xstate)
		printf("[OK]\txstate was restored correctly\n");
	else
		printf("[FAIL]\txstate restoration failed\n");

	clearhandler(SIGUSR1);
	free(stashed_xbuf);
}

void test_xstate(uint32_t feature_num)
{
	const unsigned int ctxtsw_num_threads = 5, ctxtsw_iterations = 10;
	unsigned long features;
	long rc;

	if (!(XFEATURE_MASK_TEST_SUPPORTED & (1 << feature_num))) {
		ksft_print_msg("The xstate test does not fully support the component %u, yet.\n",
			       feature_num);
		return;
	}

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_SUPP, &features);
	if (rc || !(features & (1 << feature_num))) {
		ksft_print_msg("The kernel does not support feature number: %u\n", feature_num);
		return;
	}

	xstate = get_xstate_info(feature_num);
	if (!xstate.size || !xstate.xbuf_offset) {
		ksft_exit_fail_msg("invalid state size/offset (%d/%d)\n",
				   xstate.size, xstate.xbuf_offset);
	}

	test_context_switch(ctxtsw_num_threads, ctxtsw_iterations);
	test_ptrace();
	test_signal();
}
