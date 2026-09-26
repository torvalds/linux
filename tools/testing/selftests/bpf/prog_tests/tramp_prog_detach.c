// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <pthread.h>
#include <poll.h>
#include <sys/mman.h>
#include <linux/userfaultfd.h>
#include "tramp_prog_detach.skel.h"
#include "testing_helpers.h"

/*
 * Detach and free progs while a task sleeps in the prog that runs before them
 * in the same trampoline image, then let that task continue through the
 * image. It must not call into the freed progs.
 *
 * The task is held in a sleepable prog with userfaultfd, like bpf_mod_race
 * does.
 */

static struct bpf_program *pick_prog(struct tramp_prog_detach *skel,
				     bool fexit, bool sleepable)
{
	if (fexit)
		return sleepable ? skel->progs.fexit_sleepable :
				   skel->progs.fexit_victim;
	return sleepable ? skel->progs.fentry_sleepable :
			   skel->progs.fentry_victim;
}

static struct bpf_program *sleepable_prog;

static void *run_sleepable(void *arg)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	/* calls bpf_fentry_test1() */
	return (void *)(long)bpf_prog_test_run_opts(bpf_program__fd(sleepable_prog),
						    &topts);
}

static struct tramp_prog_detach *load_one(bool fexit, bool sleepable)
{
	struct tramp_prog_detach *skel;
	int err;

	skel = tramp_prog_detach__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return NULL;

	bpf_program__set_autoload(pick_prog(skel, fexit, sleepable), true);
	err = tramp_prog_detach__load(skel);
	if (!ASSERT_OK(err, "load"))
		goto err;
	skel->bss->pid = getpid();
	err = tramp_prog_detach__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto err;
	return skel;
err:
	tramp_prog_detach__destroy(skel);
	return NULL;
}

/* The .bss map of a destroyed skeleton goes away when its prog is freed */
static bool wait_for_map_free(__u32 map_id)
{
	int i, fd;

	for (i = 0; i < 100; i++) {
		fd = bpf_map_get_fd_by_id(map_id);
		if (fd < 0)
			return true;
		close(fd);
		usleep(100 * 1000);
	}
	return false;
}

static void test_detach(bool sleepable_fexit, bool victim_fexit)
{
	struct tramp_prog_detach *sleepable = NULL, *victims[2] = {};
	struct pollfd pfd = { .events = POLLIN };
	struct uffdio_copy uffd_copy = {};
	struct bpf_map_info map_info = {};
	__u32 map_info_len = sizeof(map_info);
	struct uffd_msg uffd_msg;
	void *fault_page, *src_page = MAP_FAILED;
	long page_size = getpagesize();
	bool started = false;
	void *thread_ret;
	pthread_t thread;
	int i, uffd = -1;

	fault_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!ASSERT_NEQ(fault_page, MAP_FAILED, "mmap fault_page"))
		return;
	src_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!ASSERT_NEQ(src_page, MAP_FAILED, "mmap src_page"))
		goto out;

	/* The most recently attached prog runs first */
	for (i = 0; i < ARRAY_SIZE(victims); i++) {
		victims[i] = load_one(victim_fexit, false);
		if (!victims[i])
			goto out;
	}
	sleepable = load_one(sleepable_fexit, true);
	if (!sleepable)
		goto out;
	sleepable_prog = pick_prog(sleepable, sleepable_fexit, true);

	/* Not armed yet so this doesn't block, make sure sleepable runs first */
	if (!ASSERT_OK((long)run_sleepable(NULL), "dry run"))
		goto out;
	for (i = 0; i < ARRAY_SIZE(victims); i++)
		if (!ASSERT_LT(sleepable->bss->ts, victims[i]->bss->ts, "prog order"))
			goto out;

	uffd = uffd_block_page(fault_page);
	if (!ASSERT_GE(uffd, 0, "userfaultfd open + register address"))
		goto out;
	sleepable->bss->fault_addr = fault_page;

	if (!ASSERT_OK(pthread_create(&thread, NULL, run_sleepable, NULL),
		       "pthread_create"))
		goto out;
	started = true;

	/* Wait for the thread to sleep in bpf_copy_from_user() */
	pfd.fd = uffd;
	if (!ASSERT_EQ(poll(&pfd, 1, 10000), 1, "poll uffd"))
		goto out;
	if (!ASSERT_EQ(read(uffd, &uffd_msg, sizeof(uffd_msg)), sizeof(uffd_msg),
		       "read uffd"))
		goto out;
	if (!ASSERT_EQ(uffd_msg.event, UFFD_EVENT_PAGEFAULT, "uffd pagefault"))
		goto out;

	/*
	 * Detach and unload the victim progs and wait for them to be freed.
	 * After the first one, the task is in an image that isn't the
	 * trampoline's current one anymore.
	 */
	for (i = 0; i < ARRAY_SIZE(victims); i++) {
		if (!ASSERT_OK(bpf_map_get_info_by_fd(bpf_map__fd(victims[i]->maps.bss),
						      &map_info, &map_info_len),
			       "victim bss info"))
			goto out;
		tramp_prog_detach__destroy(victims[i]);
		victims[i] = NULL;
		if (!ASSERT_TRUE(wait_for_map_free(map_info.id), "victim freed"))
			goto out;
	}

out:
	/* Let the thread proceed with the rest of the trampoline */
	if (uffd >= 0) {
		uffd_copy.dst = (unsigned long)fault_page;
		uffd_copy.src = (unsigned long)src_page;
		uffd_copy.len = page_size;
		ASSERT_OK(ioctl(uffd, UFFDIO_COPY, &uffd_copy), "uffd copy");
		close(uffd);
	}
	if (started &&
	    ASSERT_OK(pthread_join(thread, &thread_ret), "pthread_join"))
		ASSERT_NULL(thread_ret, "blocking run");

	for (i = 0; i < ARRAY_SIZE(victims); i++)
		tramp_prog_detach__destroy(victims[i]);
	tramp_prog_detach__destroy(sleepable);
	if (src_page != MAP_FAILED)
		munmap(src_page, page_size);
	munmap(fault_page, page_size);
}

void serial_test_tramp_prog_detach(void)
{
	/* a task sleeping before the original function is called */
	if (test__start_subtest("fentry"))
		test_detach(false, false);
	/* a task sleeping after the original function returned */
	if (test__start_subtest("fexit"))
		test_detach(true, true);
	/* the original function runs in between and must still be called */
	if (test__start_subtest("fentry_fexit"))
		test_detach(false, true);
}
