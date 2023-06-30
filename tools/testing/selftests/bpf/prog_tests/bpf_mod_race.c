// SPDX-License-Identifier: GPL-2.0
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdatomic.h>
#include <test_progs.h>
#include <sys/syscall.h>
#include <linux/module.h>
#include <linux/userfaultfd.h>

#include "ksym_race.skel.h"
#include "bpf_mod_race.skel.h"
#include "kfunc_call_race.skel.h"
#include "testing_helpers.h"

/* This test crafts a race between btf_try_get_module and do_init_module, and
 * checks whether btf_try_get_module handles the invocation for a well-formed
 * but uninitialized module correctly. Unless the module has completed its
 * initcalls, the verifier should fail the program load and return ENXIO.
 *
 * userfaultfd is used to trigger a fault in an fmod_ret program, and make it
 * sleep, then the BPF program is loaded and the return value from verifier is
 * inspected. After this, the userfaultfd is closed so that the module loading
 * thread makes forward progress, and fmod_ret injects an error so that the
 * module load fails and it is freed.
 *
 * If the verifier succeeded in loading the supplied program, it will end up
 * taking reference to freed module, and trigger a crash when the program fd
 * is closed later. This is true for both kfuncs and ksyms. In both cases,
 * the crash is triggered inside bpf_prog_free_deferred, when module reference
 * is finally released.
 */

struct test_config {
	const char *str_open;
	void *(*bpf_open_and_load)();
	void (*bpf_destroy)(void *);
};

enum bpf_test_state {
	_TS_INVALID,
	TS_MODULE_LOAD,
	TS_MODULE_LOAD_FAIL,
};

static _Atomic enum bpf_test_state state = _TS_INVALID;

static void *load_module_thread(void *p)
{

	if (!ASSERT_NEQ(load_bpf_testmod(false), 0, "load_module_thread must fail"))
		atomic_store(&state, TS_MODULE_LOAD);
	else
		atomic_store(&state, TS_MODULE_LOAD_FAIL);
	return p;
}

static int sys_userfaultfd(int flags)
{
	return syscall(__NR_userfaultfd, flags);
}

static int test_setup_uffd(void *fault_addr)
{
	struct uffdio_register uffd_register = {};
	struct uffdio_api uffd_api = {};
	int uffd;

	uffd = sys_userfaultfd(O_CLOEXEC);
	if (uffd < 0)
		return -errno;

	uffd_api.api = UFFD_API;
	uffd_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffd_api)) {
		close(uffd);
		return -1;
	}

	uffd_register.range.start = (unsigned long)fault_addr;
	uffd_register.range.len = 4096;
	uffd_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffd_register)) {
		close(uffd);
		return -1;
	}
	return uffd;
}

static void test_bpf_mod_race_config(const struct test_config *config)
{
	void *fault_addr, *skel_fail;
	struct bpf_mod_race *skel;
	struct uffd_msg uffd_msg;
	pthread_t load_mod_thrd;
	_Atomic int *blockingp;
	int uffd, ret;

	fault_addr = mmap(0, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!ASSERT_NEQ(fault_addr, MAP_FAILED, "mmap for uffd registration"))
		return;

	if (!ASSERT_OK(unload_bpf_testmod(false), "unload bpf_testmod"))
		goto end_mmap;

	skel = bpf_mod_race__open();
	if (!ASSERT_OK_PTR(skel, "bpf_mod_kfunc_race__open"))
		goto end_module;

	skel->rodata->bpf_mod_race_config.tgid = getpid();
	skel->rodata->bpf_mod_race_config.inject_error = -4242;
	skel->rodata->bpf_mod_race_config.fault_addr = fault_addr;
	if (!ASSERT_OK(bpf_mod_race__load(skel), "bpf_mod___load"))
		goto end_destroy;
	blockingp = (_Atomic int *)&skel->bss->bpf_blocking;

	if (!ASSERT_OK(bpf_mod_race__attach(skel), "bpf_mod_kfunc_race__attach"))
		goto end_destroy;

	uffd = test_setup_uffd(fault_addr);
	if (!ASSERT_GE(uffd, 0, "userfaultfd open + register address"))
		goto end_destroy;

	if (!ASSERT_OK(pthread_create(&load_mod_thrd, NULL, load_module_thread, NULL),
		       "load module thread"))
		goto end_uffd;

	/* Now, we either fail loading module, or block in bpf prog, spin to find out */
	while (!atomic_load(&state) && !atomic_load(blockingp))
		;
	if (!ASSERT_EQ(state, _TS_INVALID, "module load should block"))
		goto end_join;
	if (!ASSERT_EQ(*blockingp, 1, "module load blocked")) {
		pthread_kill(load_mod_thrd, SIGKILL);
		goto end_uffd;
	}

	/* We might have set bpf_blocking to 1, but may have not blocked in
	 * bpf_copy_from_user. Read userfaultfd descriptor to verify that.
	 */
	if (!ASSERT_EQ(read(uffd, &uffd_msg, sizeof(uffd_msg)), sizeof(uffd_msg),
		       "read uffd block event"))
		goto end_join;
	if (!ASSERT_EQ(uffd_msg.event, UFFD_EVENT_PAGEFAULT, "read uffd event is pagefault"))
		goto end_join;

	/* We know that load_mod_thrd is blocked in the fmod_ret program, the
	 * module state is still MODULE_STATE_COMING because mod->init hasn't
	 * returned. This is the time we try to load a program calling kfunc and
	 * check if we get ENXIO from verifier.
	 */
	skel_fail = config->bpf_open_and_load();
	ret = errno;
	if (!ASSERT_EQ(skel_fail, NULL, config->str_open)) {
		/* Close uffd to unblock load_mod_thrd */
		close(uffd);
		uffd = -1;
		while (atomic_load(blockingp) != 2)
			;
		ASSERT_OK(kern_sync_rcu(), "kern_sync_rcu");
		config->bpf_destroy(skel_fail);
		goto end_join;

	}
	ASSERT_EQ(ret, ENXIO, "verifier returns ENXIO");
	ASSERT_EQ(skel->data->res_try_get_module, false, "btf_try_get_module == false");

	close(uffd);
	uffd = -1;
end_join:
	pthread_join(load_mod_thrd, NULL);
	if (uffd < 0)
		ASSERT_EQ(atomic_load(&state), TS_MODULE_LOAD_FAIL, "load_mod_thrd success");
end_uffd:
	if (uffd >= 0)
		close(uffd);
end_destroy:
	bpf_mod_race__destroy(skel);
	ASSERT_OK(kern_sync_rcu(), "kern_sync_rcu");
end_module:
	unload_bpf_testmod(false);
	ASSERT_OK(load_bpf_testmod(false), "restore bpf_testmod");
end_mmap:
	munmap(fault_addr, 4096);
	atomic_store(&state, _TS_INVALID);
}

static const struct test_config ksym_config = {
	.str_open = "ksym_race__open_and_load",
	.bpf_open_and_load = (void *)ksym_race__open_and_load,
	.bpf_destroy = (void *)ksym_race__destroy,
};

static const struct test_config kfunc_config = {
	.str_open = "kfunc_call_race__open_and_load",
	.bpf_open_and_load = (void *)kfunc_call_race__open_and_load,
	.bpf_destroy = (void *)kfunc_call_race__destroy,
};

void serial_test_bpf_mod_race(void)
{
	if (test__start_subtest("ksym (used_btfs UAF)"))
		test_bpf_mod_race_config(&ksym_config);
	if (test__start_subtest("kfunc (kfunc_btf_tab UAF)"))
		test_bpf_mod_race_config(&kfunc_config);
}
