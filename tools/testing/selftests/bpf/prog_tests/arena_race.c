// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/syscall.h>
#include <test_progs.h>

#include "arena_race.skel.h"

struct free_thread_ctx {
	struct arena_race *skel;
	int err;
	__u32 retval;
};

struct fault_thread_ctx {
	__u64 *addr;
	int stop;
};

static int run_prog(struct bpf_program *prog, const char *name)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int err;

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);
	return ASSERT_OK(err, name) && ASSERT_OK(opts.retval, name) ? 0 : -1;
}

/* Trigger the sleepable free page path. */
static void *run_free_thread(void *arg)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct free_thread_ctx *ctx = arg;

	ctx->skel->bss->target_tid = sys_gettid();
	ctx->err = bpf_prog_test_run_opts(
		bpf_program__fd(ctx->skel->progs.free_page), &opts);
	ctx->retval = opts.retval;
	return NULL;
}

static __thread sigjmp_buf reader_env;
static __thread bool reader_armed;

/* Only the reader expects SIGBUS; anywhere else it keeps its default action. */
static void reader_sigbus(int sig)
{
	if (!reader_armed) {
		signal(sig, SIG_DFL);
		raise(sig);
		return;
	}
	siglongjmp(reader_env, 1);
}

/* Continuously fault in the address. */
static void *fault_reader_thread(void *arg)
{
	struct fault_thread_ctx *ctx = arg;
	__u64 *addr;

	/* Touching a page while BPF frees it raises SIGBUS; skip that read. */
	sigsetjmp(reader_env, 1);
	reader_armed = true;
	while (!READ_ONCE(ctx->stop)) {
		addr = __atomic_load_n(&ctx->addr, __ATOMIC_ACQUIRE);
		(void)READ_ONCE(*addr);
	}
	reader_armed = false;
	return NULL;
}

static int wait_for(int *p)
{
	__u64 deadline = get_time_ns() + 5ULL * 1000 * 1000 * 1000;

	while (!READ_ONCE(*p)) {
		if (get_time_ns() > deadline)
			return -ETIMEDOUT;
	}
	return 0;
}

/* Skip the subtest, saying why, if @func cannot be traced with fentry. */
static bool require_fentry_target(const char *test, const char *func)
{
	if (libbpf_find_vmlinux_btf_id(func, BPF_TRACE_FENTRY) > 0)
		return true;
	printf("%s:SKIP: %s is not an fentry target\n", test, func);
	test__skip();
	return false;
}

static struct arena_race *setup_arena(__u64 **addr, bool trace_zap)
{
	struct arena_race *skel;
	size_t arena_sz;
	char *base;
	int err;

	skel = arena_race__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return NULL;
	err = bpf_program__set_autoload(skel->progs.trace_zap, trace_zap);
	if (!ASSERT_OK(err, "trace_zap_autoload"))
		goto err_out;

	err = arena_race__load(skel);
	if (!ASSERT_OK(err, "load"))
		goto err_out;
	err = arena_race__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto err_out;

	if (run_prog(skel->progs.alloc_old, "alloc_old"))
		goto err_out;
	if (skel->bss->skip) {
		printf("%s:SKIP: compiler doesn't support arena_cast\n", __func__);
		test__skip();
		goto err_out;
	}

	base = bpf_map__initial_value(skel->maps.arena, &arena_sz);
	if (!ASSERT_OK_PTR(base, "arena_base"))
		goto err_out;
	*addr = (__u64 *)(base + getpagesize());
	if (!ASSERT_EQ((unsigned long)skel->bss->ptr, (unsigned long)*addr,
		       "arena_ptr"))
		goto err_out;
	return skel;

err_out:
	arena_race__destroy(skel);
	return NULL;
}

static void test_realloc_during_free(bool deferred)
{
	struct free_thread_ctx ctx = {};
	struct arena_race *skel;
	pthread_t thread;
	__u64 *addr;
	bool thread_created = false, zap_seen = false, completed = false;
	int err;

	if (!require_fentry_target(__func__, "zap_vma_range"))
		return;

	skel = setup_arena(&addr, true);
	if (!skel)
		return;

	/* Map the page into user space, so that the free has to zap it. */
	if (!ASSERT_EQ(READ_ONCE(*addr), skel->rodata->old_marker, "old_marker"))
		goto out;

	/* Pause the free after its TLB flush, before it releases the range. */
	skel->bss->pause_on_zap = 1;

	if (deferred) {
		/*
		 * Test the nonsleepable free path that gets
		 * deferred to a worker in the kernel. We do
		 * so by triggering the arena operation from
		 * a nonsleepable tracepoint context.
		 */
		skel->bss->trigger_pid_tgid =
			((__u64)getpid() << 32) | (__u32)sys_gettid();
		skel->bss->trigger_syscall = SYS_getpgid;
		skel->bss->deferred_free = 1;
		if (!ASSERT_GE(syscall(SYS_getpgid, 0), 0, "deferred_free"))
			goto release;
	} else {
		/*
		 * Test the sleepable arena free path through a
		 * syscall test prog.
		 */
		ctx.skel = skel;
		err = pthread_create(&thread, NULL, run_free_thread, &ctx);
		if (!ASSERT_OK(err, "pthread_create")) {
			skel->bss->release = 1;
			goto out;
		}
		thread_created = true;
	}

	/* Wait until the free reaches the user zap. */
	err = wait_for(&skel->bss->zap_entered);
	if (!ASSERT_OK(err, "zap_entered"))
		goto release;

	zap_seen = true;

	/* The range must stay allocated until the free completes. */
	run_prog(skel->progs.try_realloc, "realloc_during_free");
	ASSERT_NULL(skel->bss->realloc_ptr, "realloc_during_free");

release:
	skel->bss->release = 1;
	if (thread_created) {
		ASSERT_OK(pthread_join(thread, NULL), "pthread_join");
		thread_created = false;
		completed = ASSERT_OK(ctx.err, "free_run") &&
			    ASSERT_OK(ctx.retval, "free_retval");
	} else if (skel->bss->target_tid) {
		err = wait_for(&skel->bss->worker_exited);
		completed = ASSERT_OK(err, "worker_exited");
	}
	ASSERT_FALSE(skel->bss->timed_out, "zap_timed_out");

	if (zap_seen && completed &&
	    !run_prog(skel->progs.try_realloc, "realloc_after_free")) {
		ASSERT_EQ((unsigned long)skel->bss->realloc_ptr,
			  (unsigned long)addr, "realloc_after_free");
		ASSERT_EQ(*addr, skel->rodata->new_marker, "new_marker");
	}
out:
	arena_race__destroy(skel);
}

/*
 * Force a race between a faulting thread in userspace and a
 * free operation on the arena.
 */
static void test_fault_free_realloc(void)
{
	struct sigaction sigbus = { .sa_handler = reader_sigbus }, old_sigbus;
	struct fault_thread_ctx fault = {};
	struct arena_race *skel;
	pthread_t fault_thread;
	bool fault_created = false;
	__u64 *addr;
	__u64 expected, value;
	int err, i;

	skel = setup_arena(&addr, false);
	if (!skel)
		return;

	skel->bss->realloc_after_free = 1;

	if (!ASSERT_OK(sigaction(SIGBUS, &sigbus, &old_sigbus), "sigaction_sigbus"))
		goto out_destroy;
	fault.addr = addr;
	err = pthread_create(&fault_thread, NULL, fault_reader_thread, &fault);
	if (!ASSERT_OK(err, "pthread_create_fault"))
		goto out;
	fault_created = true;

	for (i = 0; i < 1000; i++) {
		LIBBPF_OPTS(bpf_test_run_opts, opts);

		/* Keep deferred frees from earlier iterations on separate slots. */
		if (i) {
			skel->bss->page_idx = i;
			if (run_prog(skel->progs.alloc_old, "alloc_next"))
				goto out;
			addr = (__u64 *)(unsigned long)skel->bss->ptr;
			__atomic_store_n(&fault.addr, addr, __ATOMIC_RELEASE);
		}

		err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.free_page),
					     &opts);
		if (err) {
			ASSERT_OK(err, "free_realloc");
			break;
		}
		if (opts.retval) {
			ASSERT_OK(opts.retval, "free_realloc");
			break;
		}
		/* The reader may populate the slot first, or free may defer. */
		if (!skel->bss->realloc_ptr)
			continue;
		value = *addr;
		expected = skel->bss->current_marker;
		if (value != expected) {
			ASSERT_EQ(value, expected, "marker_after_realloc");
			break;
		}
	}

	WRITE_ONCE(fault.stop, 1);
	if (fault_created) {
		ASSERT_OK(pthread_join(fault_thread, NULL), "pthread_join_fault");
		fault_created = false;
	}
out:
	WRITE_ONCE(fault.stop, 1);
	if (fault_created)
		pthread_join(fault_thread, NULL);
	sigaction(SIGBUS, &old_sigbus, NULL);
out_destroy:
	arena_race__destroy(skel);
}

void serial_test_arena_race(void)
{
	cpu_set_t cpuset;
	int err;

	err = sched_getaffinity(0, sizeof(cpuset), &cpuset);
	if (!ASSERT_OK(err, "sched_getaffinity"))
		return;
	if (CPU_COUNT(&cpuset) < 2) {
		printf("%s:SKIP: at least two runnable CPUs are required\n", __func__);
		test__skip();
		return;
	}

	if (test__start_subtest("realloc_during_free"))
		test_realloc_during_free(false);
	if (test__start_subtest("realloc_during_deferred_free"))
		test_realloc_during_free(true);
	if (test__start_subtest("fault_free_realloc"))
		test_fault_free_realloc();
}
