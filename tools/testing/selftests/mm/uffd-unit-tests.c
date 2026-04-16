// SPDX-License-Identifier: GPL-2.0-only
/*
 * Userfaultfd unit tests.
 *
 *  Copyright (C) 2015-2023  Red Hat, Inc.
 */

#include "uffd-common.h"

#include <linux/fs.h>
#include <sys/uio.h>
#include "../../../../mm/gup_test.h"

#ifdef __NR_userfaultfd

/* The unit test doesn't need a large or random size, make it 32MB for now */
#define  UFFD_TEST_MEM_SIZE               (32UL << 20)

#define  MEM_ANON                         BIT_ULL(0)
#define  MEM_SHMEM                        BIT_ULL(1)
#define  MEM_SHMEM_PRIVATE                BIT_ULL(2)
#define  MEM_HUGETLB                      BIT_ULL(3)
#define  MEM_HUGETLB_PRIVATE              BIT_ULL(4)

#define  MEM_ALL  (MEM_ANON | MEM_SHMEM | MEM_SHMEM_PRIVATE | \
		   MEM_HUGETLB | MEM_HUGETLB_PRIVATE)

#define ALIGN_UP(x, align_to) \
	((__typeof__(x))((((unsigned long)(x)) + ((align_to)-1)) & ~((align_to)-1)))

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

struct mem_type {
	const char *name;
	unsigned int mem_flag;
	uffd_test_ops_t *mem_ops;
	bool shared;
};
typedef struct mem_type mem_type_t;

mem_type_t mem_types[] = {
	{
		.name = "anon",
		.mem_flag = MEM_ANON,
		.mem_ops = &anon_uffd_test_ops,
		.shared = false,
	},
	{
		.name = "shmem",
		.mem_flag = MEM_SHMEM,
		.mem_ops = &shmem_uffd_test_ops,
		.shared = true,
	},
	{
		.name = "shmem-private",
		.mem_flag = MEM_SHMEM_PRIVATE,
		.mem_ops = &shmem_uffd_test_ops,
		.shared = false,
	},
	{
		.name = "hugetlb",
		.mem_flag = MEM_HUGETLB,
		.mem_ops = &hugetlb_uffd_test_ops,
		.shared = true,
	},
	{
		.name = "hugetlb-private",
		.mem_flag = MEM_HUGETLB_PRIVATE,
		.mem_ops = &hugetlb_uffd_test_ops,
		.shared = false,
	},
};

/* Arguments to be passed over to each uffd unit test */
struct uffd_test_args {
	mem_type_t *mem_type;
};
typedef struct uffd_test_args uffd_test_args_t;

/* Returns: UFFD_TEST_* */
typedef void (*uffd_test_fn)(uffd_global_test_opts_t *, uffd_test_args_t *);

typedef struct {
	const char *name;
	uffd_test_fn uffd_fn;
	unsigned int mem_targets;
	uint64_t uffd_feature_required;
	uffd_test_case_ops_t *test_case_ops;
} uffd_test_case_t;

static char current_test[256];

static void uffd_test_pass(void)
{
	ksft_test_result_pass("%s\n", current_test);
}

#define  uffd_test_start(...)  do {		\
		snprintf(current_test, sizeof(current_test), __VA_ARGS__); \
	} while (0)

#define  uffd_test_fail(fmt, ...)  do {					\
		ksft_print_msg("failed reason: [" fmt "]\n", ##__VA_ARGS__); \
		ksft_test_result_fail("%s\n", current_test);		\
	} while (0)

static void uffd_test_skip(const char *message)
{
	ksft_test_result_skip("%s (%s)\n", current_test, message);
}

static void test_uffd_api(bool use_dev)
{
	const uint64_t expected_ioctls =
		BIT_ULL(_UFFDIO_REGISTER) |
		BIT_ULL(_UFFDIO_UNREGISTER) |
		BIT_ULL(_UFFDIO_API);
	struct uffdio_api uffdio_api;
	int uffd;

	uffd_test_start("UFFDIO_API (with %s)",
			use_dev ? "/dev/userfaultfd" : "syscall");

	if (use_dev)
		uffd = uffd_open_dev(UFFD_FLAGS);
	else
		uffd = uffd_open_sys(UFFD_FLAGS);
	if (uffd < 0) {
		uffd_test_skip("cannot open userfaultfd handle");
		return;
	}

	/* Test wrong UFFD_API */
	uffdio_api.api = 0xab;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == 0) {
		uffd_test_fail("UFFDIO_API should fail with wrong api but didn't");
		goto out;
	}

	/* Test wrong feature bit */
	uffdio_api.api = UFFD_API;
	uffdio_api.features = BIT_ULL(63);
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == 0) {
		uffd_test_fail("UFFDIO_API should fail with wrong feature but didn't");
		goto out;
	}

	/* Test normal UFFDIO_API */
	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api)) {
		uffd_test_fail("UFFDIO_API should succeed but failed");
		goto out;
	}

	/* Verify returned fd-level ioctls bitmask */
	if ((uffdio_api.ioctls & expected_ioctls) != expected_ioctls) {
		uffd_test_fail("UFFDIO_API missing expected ioctls: "
			       "got=0x%"PRIx64", expected=0x%"PRIx64,
			       (uint64_t)uffdio_api.ioctls,
			       expected_ioctls);
		goto out;
	}

	/* Test double requests of UFFDIO_API with a random feature set */
	uffdio_api.features = BIT_ULL(0);
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == 0) {
		uffd_test_fail("UFFDIO_API should reject initialized uffd");
		goto out;
	}

	uffd_test_pass();
out:
	close(uffd);
}


static bool uffd_feature_supported(uffd_test_case_t *test)
{
	uint64_t features;

	if (uffd_get_features(&features))
		return false;

	return (features & test->uffd_feature_required) ==
	    test->uffd_feature_required;
}

static int pagemap_open(void)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);

	if (fd < 0)
		err("open pagemap");

	return fd;
}

/* This macro let __LINE__ works in err() */
#define  pagemap_check_wp(value, wp) do {				\
		if (!!(value & PM_UFFD_WP) != wp)			\
			err("pagemap uffd-wp bit error: 0x%"PRIx64, value); \
	} while (0)

typedef struct {
	uffd_global_test_opts_t *gopts;
	int child_uffd;
} fork_event_args;

static void *fork_event_consumer(void *data)
{
	fork_event_args *args = data;
	struct uffd_msg msg = { 0 };

	args->gopts->ready_for_fork = true;

	/* Read until a full msg received */
	while (uffd_read_msg(args->gopts, &msg));

	if (msg.event != UFFD_EVENT_FORK)
		err("wrong message: %u\n", msg.event);

	/* Just to be properly freed later */
	args->child_uffd = msg.arg.fork.ufd;
	return NULL;
}

typedef struct {
	int gup_fd;
	bool pinned;
} pin_args;

/*
 * Returns 0 if succeed, <0 for errors.  pin_pages() needs to be paired
 * with unpin_pages().  Currently it needs to be RO longterm pin to satisfy
 * all needs of the test cases (e.g., trigger unshare, trigger fork() early
 * CoW, etc.).
 */
static int pin_pages(pin_args *args, void *buffer, size_t size)
{
	struct pin_longterm_test test = {
		.addr = (uintptr_t)buffer,
		.size = size,
		/* Read-only pins */
		.flags = 0,
	};

	if (args->pinned)
		err("already pinned");

	args->gup_fd = open("/sys/kernel/debug/gup_test", O_RDWR);
	if (args->gup_fd < 0)
		return -errno;

	if (ioctl(args->gup_fd, PIN_LONGTERM_TEST_START, &test)) {
		/* Even if gup_test existed, can be an old gup_test / kernel */
		close(args->gup_fd);
		return -errno;
	}
	args->pinned = true;
	return 0;
}

static void unpin_pages(pin_args *args)
{
	if (!args->pinned)
		err("unpin without pin first");
	if (ioctl(args->gup_fd, PIN_LONGTERM_TEST_STOP))
		err("PIN_LONGTERM_TEST_STOP");
	close(args->gup_fd);
	args->pinned = false;
}

static int pagemap_test_fork(uffd_global_test_opts_t *gopts, bool with_event, bool test_pin)
{
	fork_event_args args = { .gopts = gopts, .child_uffd = -1 };
	pthread_t thread;
	pid_t child;
	uint64_t value;
	int fd, result;

	/* Prepare a thread to resolve EVENT_FORK */
	if (with_event) {
		gopts->ready_for_fork = false;
		if (pthread_create(&thread, NULL, fork_event_consumer, &args))
			err("pthread_create()");
		while (!gopts->ready_for_fork)
			; /* Wait for the poll_thread to start executing before forking */
	}

	child = fork();
	if (!child) {
		/* Open the pagemap fd of the child itself */
		pin_args args = {};

		fd = pagemap_open();

		if (test_pin && pin_pages(&args, gopts->area_dst, gopts->page_size))
			/*
			 * Normally when reach here we have pinned in
			 * previous tests, so shouldn't fail anymore
			 */
			err("pin page failed in child");

		value = pagemap_get_entry(fd, gopts->area_dst);
		/*
		 * After fork(), we should handle uffd-wp bit differently:
		 *
		 * (1) when with EVENT_FORK, it should persist
		 * (2) when without EVENT_FORK, it should be dropped
		 */
		pagemap_check_wp(value, with_event);
		if (test_pin)
			unpin_pages(&args);
		/* Succeed */
		_exit(0);
	}
	waitpid(child, &result, 0);

	if (with_event) {
		if (pthread_join(thread, NULL))
			err("pthread_join()");
		if (args.child_uffd < 0)
			err("Didn't receive child uffd");
		close(args.child_uffd);
	}

	return result;
}

static void uffd_wp_unpopulated_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uint64_t value;
	int pagemap_fd;

	if (uffd_register(gopts->uffd, gopts->area_dst, gopts->nr_pages * gopts->page_size,
			  false, true, false))
		err("register failed");

	pagemap_fd = pagemap_open();

	/* Test applying pte marker to anon unpopulated */
	wp_range(gopts->uffd, (uint64_t)gopts->area_dst, gopts->page_size, true);
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, true);

	/* Test unprotect on anon pte marker */
	wp_range(gopts->uffd, (uint64_t)gopts->area_dst, gopts->page_size, false);
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, false);

	/* Test zap on anon marker */
	wp_range(gopts->uffd, (uint64_t)gopts->area_dst, gopts->page_size, true);
	if (madvise(gopts->area_dst, gopts->page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, false);

	/* Test fault in after marker removed */
	*gopts->area_dst = 1;
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, false);
	/* Drop it to make pte none again */
	if (madvise(gopts->area_dst, gopts->page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");

	/* Test read-zero-page upon pte marker */
	wp_range(gopts->uffd, (uint64_t)gopts->area_dst, gopts->page_size, true);
	*(volatile char *)gopts->area_dst;
	/* Drop it to make pte none again */
	if (madvise(gopts->area_dst, gopts->page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");

	uffd_test_pass();
}

static void uffd_wp_fork_test_common(uffd_global_test_opts_t *gopts, uffd_test_args_t *args,
				     bool with_event)
{
	int pagemap_fd;
	uint64_t value;

	if (uffd_register(gopts->uffd, gopts->area_dst, gopts->nr_pages * gopts->page_size,
			  false, true, false))
		err("register failed");

	pagemap_fd = pagemap_open();

	/* Touch the page */
	*gopts->area_dst = 1;
	wp_range(gopts->uffd, (uint64_t)gopts->area_dst, gopts->page_size, true);
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, true);
	if (pagemap_test_fork(gopts, with_event, false)) {
		uffd_test_fail("Detected %s uffd-wp bit in child in present pte",
			       with_event ? "missing" : "stall");
		goto out;
	}

	/*
	 * This is an attempt for zapping the pgtable so as to test the
	 * markers.
	 *
	 * For private mappings, PAGEOUT will only work on exclusive ptes
	 * (PM_MMAP_EXCLUSIVE) which we should satisfy.
	 *
	 * For shared, PAGEOUT may not work.  Use DONTNEED instead which
	 * plays a similar role of zapping (rather than freeing the page)
	 * to expose pte markers.
	 */
	if (args->mem_type->shared) {
		if (madvise(gopts->area_dst, gopts->page_size, MADV_DONTNEED))
			err("MADV_DONTNEED");
	} else {
		/*
		 * NOTE: ignore retval because private-hugetlb doesn't yet
		 * support swapping, so it could fail.
		 */
		madvise(gopts->area_dst, gopts->page_size, MADV_PAGEOUT);
	}

	/* Uffd-wp should persist even swapped out */
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, true);
	if (pagemap_test_fork(gopts, with_event, false)) {
		uffd_test_fail("Detected %s uffd-wp bit in child in zapped pte",
			       with_event ? "missing" : "stall");
		goto out;
	}

	/* Unprotect; this tests swap pte modifications */
	wp_range(gopts->uffd, (uint64_t)gopts->area_dst, gopts->page_size, false);
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, false);

	/* Fault in the page from disk */
	*gopts->area_dst = 2;
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, false);
	uffd_test_pass();
out:
	if (uffd_unregister(gopts->uffd, gopts->area_dst, gopts->nr_pages * gopts->page_size))
		err("unregister failed");
	close(pagemap_fd);
}

static void uffd_wp_fork_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_wp_fork_test_common(gopts, args, false);
}

static void uffd_wp_fork_with_event_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_wp_fork_test_common(gopts, args, true);
}

static void uffd_wp_fork_pin_test_common(uffd_global_test_opts_t *gopts,
					 uffd_test_args_t *args,
					 bool with_event)
{
	int pagemap_fd;
	pin_args pin_args = {};

	if (uffd_register(gopts->uffd, gopts->area_dst, gopts->page_size, false, true, false))
		err("register failed");

	pagemap_fd = pagemap_open();

	/* Touch the page */
	*gopts->area_dst = 1;
	wp_range(gopts->uffd, (uint64_t)gopts->area_dst, gopts->page_size, true);

	/*
	 * 1. First pin, then fork().  This tests fork() special path when
	 * doing early CoW if the page is private.
	 */
	if (pin_pages(&pin_args, gopts->area_dst, gopts->page_size)) {
		uffd_test_skip("Possibly CONFIG_GUP_TEST missing "
			       "or unprivileged");
		close(pagemap_fd);
		uffd_unregister(gopts->uffd, gopts->area_dst, gopts->page_size);
		return;
	}

	if (pagemap_test_fork(gopts, with_event, false)) {
		uffd_test_fail("Detected %s uffd-wp bit in early CoW of fork()",
			       with_event ? "missing" : "stall");
		unpin_pages(&pin_args);
		goto out;
	}

	unpin_pages(&pin_args);

	/*
	 * 2. First fork(), then pin (in the child, where test_pin==true).
	 * This tests COR, aka, page unsharing on private memories.
	 */
	if (pagemap_test_fork(gopts, with_event, true)) {
		uffd_test_fail("Detected %s uffd-wp bit when RO pin",
			       with_event ? "missing" : "stall");
		goto out;
	}
	uffd_test_pass();
out:
	if (uffd_unregister(gopts->uffd, gopts->area_dst, gopts->page_size))
		err("register failed");
	close(pagemap_fd);
}

static void uffd_wp_fork_pin_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_wp_fork_pin_test_common(gopts, args, false);
}

static void uffd_wp_fork_pin_with_event_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_wp_fork_pin_test_common(gopts, args, true);
}

static void check_memory_contents(uffd_global_test_opts_t *gopts, char *p)
{
	unsigned long i, j;
	uint8_t expected_byte;

	for (i = 0; i < gopts->nr_pages; ++i) {
		expected_byte = ~((uint8_t)(i % ((uint8_t)-1)));
		for (j = 0; j < gopts->page_size; j++) {
			uint8_t v = *(uint8_t *)(p + (i * gopts->page_size) + j);
			if (v != expected_byte)
				err("unexpected page contents");
		}
	}
}

static void uffd_minor_test_common(uffd_global_test_opts_t *gopts, bool test_collapse, bool test_wp)
{
	unsigned long p;
	pthread_t uffd_mon;
	char c = '\0';
	struct uffd_args args = { 0 };
	args.gopts = gopts;

	/*
	 * NOTE: MADV_COLLAPSE is not yet compatible with WP, so testing
	 * both do not make much sense.
	 */
	assert(!(test_collapse && test_wp));

	if (uffd_register(gopts->uffd, gopts->area_dst_alias, gopts->nr_pages * gopts->page_size,
			  /* NOTE! MADV_COLLAPSE may not work with uffd-wp */
			  false, test_wp, true))
		err("register failure");

	/*
	 * After registering with UFFD, populate the non-UFFD-registered side of
	 * the shared mapping. This should *not* trigger any UFFD minor faults.
	 */
	for (p = 0; p < gopts->nr_pages; ++p)
		memset(gopts->area_dst + (p * gopts->page_size), p % ((uint8_t)-1),
		       gopts->page_size);

	args.apply_wp = test_wp;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	/*
	 * Read each of the pages back using the UFFD-registered mapping. We
	 * expect that the first time we touch a page, it will result in a minor
	 * fault. uffd_poll_thread will resolve the fault by bit-flipping the
	 * page's contents, and then issuing a CONTINUE ioctl.
	 */
	check_memory_contents(gopts, gopts->area_dst_alias);

	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("join() failed");

	if (test_collapse) {
		if (madvise(gopts->area_dst_alias, gopts->nr_pages * gopts->page_size,
			    MADV_COLLAPSE)) {
			/* It's fine to fail for this one... */
			uffd_test_skip("MADV_COLLAPSE failed");
			return;
		}

		uffd_test_ops->check_pmd_mapping(gopts,
						 gopts->area_dst,
						 gopts->nr_pages * gopts->page_size /
						 read_pmd_pagesize());
		/*
		 * This won't cause uffd-fault - it purely just makes sure there
		 * was no corruption.
		 */
		check_memory_contents(gopts, gopts->area_dst_alias);
	}

	if (args.missing_faults != 0 || args.minor_faults != gopts->nr_pages)
		uffd_test_fail("stats check error");
	else
		uffd_test_pass();
}

void uffd_minor_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_minor_test_common(gopts, false, false);
}

void uffd_minor_wp_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_minor_test_common(gopts, false, true);
}

void uffd_minor_collapse_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_minor_test_common(gopts, true, false);
}

static int uffd_register_rwp(int uffd, void *addr, uint64_t len)
{
	struct uffdio_register reg = {
		.range = { .start = (unsigned long)addr, .len = len },
		.mode = UFFDIO_REGISTER_MODE_RWP,
	};

	if (ioctl(uffd, UFFDIO_REGISTER, &reg) == -1)
		return -errno;
	return 0;
}

static void rwprotect_range(int uffd, __u64 start, __u64 len, bool protect)
{
	struct uffdio_rwprotect rwp = {
		.range = { .start = start, .len = len },
		.mode = protect ? UFFDIO_RWPROTECT_MODE_RWP : 0,
	};

	if (ioctl(uffd, UFFDIO_RWPROTECT, &rwp))
		err("UFFDIO_RWPROTECT failed");
}

static void set_async_mode(int uffd, bool enable)
{
	struct uffdio_set_mode mode = { };

	if (enable)
		mode.enable = UFFD_FEATURE_RWP_ASYNC;
	else
		mode.disable = UFFD_FEATURE_RWP_ASYNC;

	if (ioctl(uffd, UFFDIO_SET_MODE, &mode))
		err("UFFDIO_SET_MODE failed");
}

/*
 * Test async RWP faults on anonymous memory.
 * Populate pages, register MODE_RWP with RWP_ASYNC,
 * RW-protect, re-access, verify content preserved and no faults delivered.
 */
static void uffd_rwp_async_test(uffd_global_test_opts_t *gopts,
				       uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	unsigned long p;

	/* Populate all pages with known content */
	for (p = 0; p < nr_pages; p++)
		memset(gopts->area_dst + p * page_size, p % 255 + 1, page_size);

	/* Register MODE_RWP */
	if (uffd_register_rwp(gopts->uffd, gopts->area_dst,
			  nr_pages * page_size))
		err("register failure");

	/* RW-protect all pages (sets protnone) */
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			 nr_pages * page_size, true);

	/* Access all pages — should auto-resolve, no faults */
	for (p = 0; p < nr_pages; p++) {
		unsigned char *page = (unsigned char *)gopts->area_dst +
				      p * page_size;
		unsigned char expected = p % 255 + 1;

		if (page[0] != expected) {
			uffd_test_fail("page %lu content mismatch: %u != %u",
				       p, page[0], expected);
			return;
		}
	}

	uffd_test_pass();
}

/*
 * Fault handler for RWP — unprotect the page via UFFDIO_RWPROTECT.
 */
static void uffd_handle_rwp_fault(uffd_global_test_opts_t *gopts,
				  struct uffd_msg *msg,
				  struct uffd_args *uargs)
{
	if (!(msg->arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_RWP))
		err("expected RWP fault, got 0x%llx",
		    msg->arg.pagefault.flags);

	rwprotect_range(gopts->uffd, msg->arg.pagefault.address,
			gopts->page_size, false);
	uargs->minor_faults++;
}

/*
 * Test sync RWP faults on anonymous memory.
 * Populate pages, register MODE_RWP (sync), RW-protect,
 * access from worker thread, verify fault delivered, UFFDIO_RWPROTECT resolves.
 */
static void uffd_rwp_sync_test(uffd_global_test_opts_t *gopts,
				      uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	pthread_t uffd_mon;
	struct uffd_args uargs = { };
	bool failed = false;
	char c = '\0';
	unsigned long p;

	uargs.gopts = gopts;
	uargs.handle_fault = uffd_handle_rwp_fault;

	/* Populate all pages */
	for (p = 0; p < nr_pages; p++)
		memset(gopts->area_dst + p * page_size, p % 255 + 1, page_size);

	/* Register MODE_RWP */
	if (uffd_register_rwp(gopts->uffd, gopts->area_dst,
			  nr_pages * page_size))
		err("register failure");

	/* RW-protect all pages */
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			 nr_pages * page_size, true);

	/* Start fault handler thread */
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &uargs))
		err("uffd_poll_thread create");

	/* Access all pages — triggers sync RWP faults, handler unprotects */
	for (p = 0; p < nr_pages; p++) {
		unsigned char *page = (unsigned char *)gopts->area_dst +
				      p * page_size;

		if (page[0] != (p % 255 + 1)) {
			uffd_test_fail("page %lu content mismatch", p);
			failed = true;
			goto out;
		}
	}

out:
	/*
	 * Stop the handler before reading minor_faults: the last fault
	 * resolution rwprotect_range()s before incrementing the counter,
	 * so the main thread can race ahead of the increment.
	 */
	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("join() failed");

	if (failed)
		return;
	if (uargs.minor_faults == 0)
		uffd_test_fail("expected RWP faults, got 0");
	else
		uffd_test_pass();
}

/*
 * Test PAGEMAP_SCAN working-set discovery via the "hot" (accessed) scan.
 *
 * The working-set primitive is to find pages that were accessed: scan for
 * PAGE_IS_ACCESSED, which is set once an access clears the protnone+uffd
 * marker.  A VMM treats every access fault as "hot" (RWP here; MINOR/MISSING
 * for non-resident pages) and reclaims the rest from the backing file.
 *
 * We deliberately do NOT use an inverted "cold" scan: that only sees
 * VMA-resident ptes, so for a file mapping it misses cached-but-unmapped (and
 * never-faulted, pre-populated) pages, which are pte_none and thus invisible.
 * Hot tracking + file-level reclaim covers them; a cold pte scan cannot.
 */
static void uffd_rwp_pagemap_test(uffd_global_test_opts_t *gopts,
					  uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	unsigned long p;
	struct page_region regions[16];
	struct pm_scan_arg pm_arg;
	int pagemap_fd;
	long ret;

	/* Need at least 4 pages */
	if (nr_pages < 4) {
		uffd_test_skip("need at least 4 pages");
		return;
	}

	/* Populate all pages */
	for (p = 0; p < nr_pages; p++)
		memset(gopts->area_dst + p * page_size, 0xab, page_size);

	/* Register and RW-protect */
	if (uffd_register_rwp(gopts->uffd, gopts->area_dst,
			  nr_pages * page_size))
		err("register failure");

	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			 nr_pages * page_size, true);

	/* Touch first half of pages to re-activate them (async auto-resolve) */
	for (p = 0; p < nr_pages / 2; p++) {
		volatile char *page = gopts->area_dst + p * page_size;
		(void)*page;
	}

	uint64_t start = (uint64_t)gopts->area_dst;
	uint64_t boundary = start + (nr_pages / 2) * page_size;
	uint64_t end = start + nr_pages * page_size;

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		err("open pagemap");

	/*
	 * Hot scan: report the pages that were accessed.  PAGE_IS_ACCESSED is
	 * set once the protnone+uffd marker is cleared (by the access, async
	 * auto-resolve here).  The touched first half must come back as exactly
	 * one hot region [start, boundary); the untouched second half must not
	 * appear.
	 */
	memset(&pm_arg, 0, sizeof(pm_arg));
	pm_arg.size = sizeof(pm_arg);
	pm_arg.start = start;
	pm_arg.end = end;
	pm_arg.vec = (uint64_t)regions;
	pm_arg.vec_len = ARRAY_SIZE(regions);
	pm_arg.category_mask = PAGE_IS_ACCESSED;
	pm_arg.return_mask = PAGE_IS_ACCESSED;

	ret = ioctl(pagemap_fd, PAGEMAP_SCAN, &pm_arg);
	close(pagemap_fd);

	if (ret < 0) {
		uffd_test_fail("PAGEMAP_SCAN failed: %s", strerror(errno));
		return;
	}

	if (ret != 1 || regions[0].start != start ||
	    regions[0].end != boundary) {
		uffd_test_fail("hot set wrong: got %ld regions [0x%lx,0x%lx), expected 1 [0x%lx,0x%lx)",
			       ret, (unsigned long)regions[0].start,
			       (unsigned long)regions[0].end,
			       (unsigned long)start, (unsigned long)boundary);
		return;
	}

	uffd_test_pass();
}

/*
 * Test that RWP protection survives a mprotect(PROT_NONE) ->
 * mprotect(PROT_READ|PROT_WRITE) round-trip. The uffd-wp bit on a
 * VM_UFFD_RWP VMA must continue to carry PROT_NONE semantics after
 * mprotect() changes the base protection; otherwise accesses would
 * silently succeed and the pagemap bit would stick without a fault
 * ever clearing it.
 */
static void uffd_rwp_mprotect_test(uffd_global_test_opts_t *gopts,
				   uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	unsigned long p;
	struct page_region regions[16];
	struct pm_scan_arg pm_arg;
	int pagemap_fd;
	uint64_t value;
	long ret;

	/* Populate all pages */
	for (p = 0; p < nr_pages; p++)
		memset(gopts->area_dst + p * page_size, 0xab, page_size);

	/* Register and RW-protect the whole range */
	if (uffd_register_rwp(gopts->uffd, gopts->area_dst,
			      nr_pages * page_size))
		err("register failure");
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			nr_pages * page_size, true);

	/* Round-trip mprotect(): PROT_NONE -> PROT_READ|PROT_WRITE */
	if (mprotect(gopts->area_dst, nr_pages * page_size, PROT_NONE))
		err("mprotect() PROT_NONE");
	if (mprotect(gopts->area_dst, nr_pages * page_size,
		     PROT_READ | PROT_WRITE))
		err("mprotect() PROT_READ|PROT_WRITE");

	/*
	 * The marker must survive the round-trip; if mprotect() dropped it,
	 * the touches below would not fault and the scan would pass
	 * vacuously.
	 */
	pagemap_fd = pagemap_open();
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	close(pagemap_fd);
	if (!(value & PM_UFFD_WP)) {
		uffd_test_fail("RWP marker lost across mprotect()");
		return;
	}

	/* Touch every page. Async RWP must auto-resolve each fault. */
	for (p = 0; p < nr_pages; p++) {
		volatile char *page = gopts->area_dst + p * page_size;
		(void)*page;
	}

	/*
	 * After touching, no page should remain RW-protected. A stuck
	 * uffd-wp bit would mean mprotect() silently dropped PROT_NONE and
	 * the access never faulted.
	 */
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		err("open pagemap");

	memset(&pm_arg, 0, sizeof(pm_arg));
	pm_arg.size = sizeof(pm_arg);
	pm_arg.start = (uint64_t)gopts->area_dst;
	pm_arg.end = (uint64_t)gopts->area_dst + nr_pages * page_size;
	pm_arg.vec = (uint64_t)regions;
	pm_arg.vec_len = ARRAY_SIZE(regions);
	pm_arg.category_mask = PAGE_IS_ACCESSED;
	pm_arg.category_inverted = PAGE_IS_ACCESSED;
	pm_arg.return_mask = PAGE_IS_ACCESSED;

	ret = ioctl(pagemap_fd, PAGEMAP_SCAN, &pm_arg);
	close(pagemap_fd);

	if (ret < 0) {
		uffd_test_fail("PAGEMAP_SCAN failed: %s", strerror(errno));
		return;
	}
	if (ret != 0) {
		uffd_test_fail("expected no cold pages after mprotect()+touch, got %ld regions",
			       ret);
		return;
	}

	uffd_test_pass();
}

/*
 * Test that GUP resolves through protnone PTEs (async mode).
 * vmsplice() into a pipe pins user pages via get_user_pages_fast() --
 * unlike write(), which goes through copy_from_user() and ordinary
 * hardware page faults -- so it exercises gup_can_follow_protnone() on
 * the RW-protected PTE. In async mode the kernel auto-restores
 * permissions and GUP returns the page.
 */
static void uffd_rwp_gup_test(uffd_global_test_opts_t *gopts,
				     uffd_test_args_t *args)
{
	struct iovec iov;
	char buf;
	int pipefd[2];

	/* Populate first page with known content */
	memset(gopts->area_dst, 0xCD, gopts->page_size);

	if (uffd_register_rwp(gopts->uffd, gopts->area_dst, gopts->page_size))
		err("register failure");

	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			gopts->page_size, true);

	if (pipe(pipefd))
		err("pipe");

	/*
	 * One byte's worth of iov is enough to GUP the containing page and
	 * keeps the pipe transfer well under any pipe-capacity limit even on
	 * hugetlb-backed runs.
	 */
	iov.iov_base = gopts->area_dst;
	iov.iov_len = 1;
	if (vmsplice(pipefd[1], &iov, 1, 0) != 1) {
		uffd_test_fail("vmsplice from RW-protected page failed: %s",
			       strerror(errno));
		goto out;
	}

	if (read(pipefd[0], &buf, 1) != 1) {
		uffd_test_fail("read from pipe failed");
		goto out;
	}

	if (buf != (char)0xCD) {
		uffd_test_fail("content mismatch: got 0x%02x, expected 0xCD",
			       (unsigned char)buf);
		goto out;
	}

	uffd_test_pass();
out:
	close(pipefd[0]);
	close(pipefd[1]);
}

/*
 * Test runtime toggle between async and sync modes.
 * Start in async mode (detection), flip to sync (eviction), verify faults
 * block, resolve them, flip back to async.
 */
static void uffd_rwp_async_toggle_test(uffd_global_test_opts_t *gopts,
					      uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	struct uffd_args uargs = { };
	pthread_t uffd_mon;
	char c = '\0';
	unsigned long p;

	uargs.gopts = gopts;
	uargs.handle_fault = uffd_handle_rwp_fault;

	/* Populate */
	for (p = 0; p < nr_pages; p++)
		memset(gopts->area_dst + p * page_size, p % 255 + 1, page_size);

	if (uffd_register_rwp(gopts->uffd, gopts->area_dst,
			  nr_pages * page_size))
		err("register failure");

	/* Phase 1: async detection — RW-protect, access first half */
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			 nr_pages * page_size, true);

	for (p = 0; p < nr_pages / 2; p++) {
		volatile char *page = gopts->area_dst + p * page_size;
		(void)*page;  /* auto-resolves in async mode */
	}

	/* Phase 2: flip to sync for eviction */
	set_async_mode(gopts->uffd, false);

	/* Start handler — will receive faults for cold pages */
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &uargs))
		err("uffd_poll_thread create");

	/* Access second half (cold pages) — should trigger sync faults */
	for (p = nr_pages / 2; p < nr_pages; p++) {
		unsigned char *page = (unsigned char *)gopts->area_dst +
				      p * page_size;
		if (page[0] != (p % 255 + 1)) {
			uffd_test_fail("page %lu content mismatch", p);
			goto out;
		}
	}

	/*
	 * Stop the handler before reading minor_faults: the last fault
	 * resolution rwprotect_range()s before incrementing the counter,
	 * so the main thread can race ahead of the increment. Stopping
	 * here also makes Phase 3 a clean async-only test -- with the
	 * handler still running it would silently resolve any sync fault
	 * the kernel erroneously delivers, masking a regression.
	 */
	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("join() failed");

	if (uargs.minor_faults == 0) {
		uffd_test_fail("expected sync faults, got 0");
		return;
	}

	/* Phase 3: flip back to async */
	set_async_mode(gopts->uffd, true);

	/* RW-protect and access again — should auto-resolve */
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			 nr_pages * page_size, true);

	for (p = 0; p < nr_pages; p++) {
		volatile char *page = gopts->area_dst + p * page_size;
		(void)*page;
	}

	uffd_test_pass();
	return;
out:
	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("join() failed");
}

/*
 * Test that RW-protected pages become accessible after closing uffd.
 */
static void uffd_rwp_close_test(uffd_global_test_opts_t *gopts,
				       uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	unsigned long p;

	/* Populate */
	for (p = 0; p < nr_pages; p++)
		memset(gopts->area_dst + p * page_size, p % 255 + 1, page_size);

	if (uffd_register_rwp(gopts->uffd, gopts->area_dst,
			  nr_pages * page_size))
		err("register failure");

	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			 nr_pages * page_size, true);

	/* Close uffd — should restore protnone PTEs */
	close(gopts->uffd);
	gopts->uffd = -1;

	/* All pages should be accessible with original content */
	for (p = 0; p < nr_pages; p++) {
		unsigned char *page = (unsigned char *)gopts->area_dst +
				      p * page_size;
		unsigned char expected = p % 255 + 1;

		if (page[0] != expected) {
			uffd_test_fail("page %lu not accessible after close", p);
			return;
		}
	}

	uffd_test_pass();
}

/*
 * Test that RWP protection is preserved across fork() when
 * UFFD_FEATURE_EVENT_FORK is enabled. Without preservation, the child's
 * PTEs would lose the uffd-wp marker and RWP-protected accesses would
 * silently fall through to do_numa_page().
 */
static void uffd_rwp_fork_test(uffd_global_test_opts_t *gopts,
			       uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	int pagemap_fd;
	uint64_t value;

	if (uffd_register_rwp(gopts->uffd, gopts->area_dst,
			      nr_pages * page_size))
		err("register failed");

	/* Populate + RWP-protect */
	*gopts->area_dst = 1;
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst,
			page_size, true);

	/* Parent: verify uffd-wp bit is set before fork */
	pagemap_fd = pagemap_open();
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, true);

	/*
	 * Fork with EVENT_FORK: child inherits VM_UFFD_RWP. Child reads
	 * its own pagemap and must still see the uffd-wp bit set.
	 */
	if (pagemap_test_fork(gopts, true, false)) {
		uffd_test_fail("RWP marker lost in child after fork");
		goto out;
	}

	uffd_test_pass();
out:
	close(pagemap_fd);
}

/*
 * Test that RWP protection on a pinned anon page is preserved across fork().
 * Pinning forces copy_present_page() in the child path, which must restore
 * PAGE_NONE on top of the uffd bit. Using async mode, a read in the child
 * auto-resolves if — and only if — the PTE was actually protnone+uffd; the
 * cleared uffd bit afterward proves the fault path ran.
 */
static void uffd_rwp_fork_pin_test(uffd_global_test_opts_t *gopts,
				   uffd_test_args_t *args)
{
	unsigned long page_size = gopts->page_size;
	fork_event_args fevent_args = { .gopts = gopts, .child_uffd = -1 };
	pin_args pin_args = {};
	int pagemap_fd, status;
	pthread_t fevent_thread;
	uint64_t value;
	pid_t child;

	if (uffd_register_rwp(gopts->uffd, gopts->area_dst, page_size))
		err("register failed");

	/* Populate. */
	*gopts->area_dst = 1;

	/* RO-longterm pin so fork() takes copy_present_page() for this PTE. */
	if (pin_pages(&pin_args, gopts->area_dst, page_size)) {
		uffd_test_skip("Possibly CONFIG_GUP_TEST missing or unprivileged");
		uffd_unregister(gopts->uffd, gopts->area_dst, page_size);
		return;
	}

	/* RWP-protect: PTE is now PAGE_NONE + uffd bit. */
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst, page_size, true);

	pagemap_fd = pagemap_open();
	value = pagemap_get_entry(pagemap_fd, gopts->area_dst);
	pagemap_check_wp(value, true);

	/*
	 * UFFD_FEATURE_EVENT_FORK is required so the child inherits
	 * VM_UFFD_RWP and the marker; without it dup_userfaultfd() resets
	 * the child VMA and the test would pass for the wrong reason.
	 * dup_userfaultfd() blocks until the EVENT_FORK message is consumed,
	 * so spawn a reader before the fork().
	 */
	gopts->ready_for_fork = false;
	if (pthread_create(&fevent_thread, NULL, fork_event_consumer,
			   &fevent_args))
		err("pthread_create() for fork event consumer");
	while (!gopts->ready_for_fork)
		; /* Wait for consumer to start polling. */

	child = fork();
	if (child < 0)
		err("fork");
	if (child == 0) {
		volatile char c;
		int cfd;

		/*
		 * Precondition: the child must have inherited the marker.
		 * If copy_present_page() dropped it together with PAGE_NONE,
		 * the read below would succeed without the fault path and
		 * the after-read check would pass for the wrong reason.
		 */
		cfd = pagemap_open();
		value = pagemap_get_entry(cfd, gopts->area_dst);
		if (!(value & PM_UFFD_WP)) {
			close(cfd);
			_exit(2);
		}

		/*
		 * Read the pinned page. Only reaches the fault path if the
		 * child PTE is protnone + uffd; async mode auto-resolves and
		 * clears the uffd bit. If copy_present_page() dropped
		 * PAGE_NONE, the read would silently succeed and the bit
		 * would still be set.
		 */
		c = *(volatile char *)gopts->area_dst;
		(void)c;

		value = pagemap_get_entry(cfd, gopts->area_dst);
		close(cfd);
		_exit((value & PM_UFFD_WP) ? 1 : 0);
	}
	if (waitpid(child, &status, 0) < 0)
		err("waitpid");
	if (pthread_join(fevent_thread, NULL))
		err("pthread_join() for fork event consumer");
	if (fevent_args.child_uffd >= 0)
		close(fevent_args.child_uffd);

	unpin_pages(&pin_args);
	close(pagemap_fd);
	if (uffd_unregister(gopts->uffd, gopts->area_dst, page_size))
		err("unregister failed");

	if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
		uffd_test_fail("RWP marker not inherited by child");
		return;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		uffd_test_fail("RWP not enforced in child after pinned fork");
		return;
	}

	uffd_test_pass();
}

/*
 * A non-exclusive (forked, COW-shared) anon page that is RWP-protected and
 * then swapped out must keep tracking across swap-in. On the write that
 * swaps it back in, do_swap_page() restores PAGE_NONE and the access retries
 * through the RWP fault path, instead of being COWed straight to a fresh
 * accessible page -- which would silently drop the marker for a non-exclusive
 * folio. Sync mode lets us observe the fault directly: with the bug, the
 * write COWs without delivering any RWP fault.
 *
 * Needs a swap device; skipped if MADV_PAGEOUT cannot evict the page.
 */
static void uffd_rwp_swap_cow_test(uffd_global_test_opts_t *gopts,
				   uffd_test_args_t *args)
{
	unsigned long page_size = gopts->page_size;
	struct uffd_args uargs = { };
	int pagemap_fd, go[2], i;
	pthread_t uffd_mon;
	char c = '\0';
	pid_t child;

	uargs.gopts = gopts;
	uargs.handle_fault = uffd_handle_rwp_fault;

	if (uffd_register_rwp(gopts->uffd, gopts->area_dst, page_size))
		err("register failed");

	/* Populate one page (exclusive at this point). */
	*gopts->area_dst = 0x11;

	/* RWP-protect: PTE becomes PAGE_NONE + uffd bit (still exclusive). */
	rwprotect_range(gopts->uffd, (uint64_t)gopts->area_dst, page_size, true);

	/*
	 * Swap the page out while it is still exclusive: a shared (forked)
	 * folio does not get reclaimed by MADV_PAGEOUT. Retry, since a hot
	 * page may just be rotated on the first reclaim pass.
	 */
	pagemap_fd = pagemap_open();
	for (i = 0; i < 100; i++) {
		if (madvise(gopts->area_dst, page_size, MADV_PAGEOUT))
			err("MADV_PAGEOUT");
		if (pagemap_is_swapped(pagemap_fd, gopts->area_dst))
			break;
		usleep(10000);
	}
	if (!pagemap_is_swapped(pagemap_fd, gopts->area_dst)) {
		uffd_test_skip("MADV_PAGEOUT did not swap the page; is swap enabled?");
		close(pagemap_fd);
		uffd_unregister(gopts->uffd, gopts->area_dst, page_size);
		return;
	}

	/*
	 * fork() now: the child duplicates the swap entry, so the slot becomes
	 * non-exclusive. The child parks (keeping the reference) until the
	 * parent has faulted the page back in.
	 */
	if (pipe(go))
		err("pipe");
	child = fork();
	if (child < 0)
		err("fork");
	if (child == 0) {
		close(go[1]);
		read(go[0], &c, 1);
		_exit(0);
	}
	close(go[0]);

	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &uargs))
		err("uffd_poll_thread create");

	/*
	 * Write the page: swaps it back in (do_swap_page) on a non-exclusive
	 * folio with FAULT_FLAG_WRITE. The marker must survive and deliver an
	 * RWP fault rather than COW silently.
	 */
	*gopts->area_dst = 0x22;

	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("join failed");

	if (uargs.minor_faults == 0)
		uffd_test_fail("no RWP fault on swapped-in non-exclusive page");
	else
		uffd_test_pass();

	close(pagemap_fd);
	if (write(go[1], &c, 1) != 1)
		err("child release");
	close(go[1]);
	waitpid(child, NULL, 0);
}

/*
 * WP and RWP share the uffd-wp PTE bit and cannot coexist in the same VMA.
 * Registration requesting both modes must be rejected.
 */
static void uffd_rwp_wp_exclusive_test(uffd_global_test_opts_t *gopts,
				       uffd_test_args_t *args)
{
	unsigned long nr_pages = gopts->nr_pages;
	unsigned long page_size = gopts->page_size;
	struct uffdio_register reg = { };

	reg.range.start = (unsigned long)gopts->area_dst;
	reg.range.len = nr_pages * page_size;
	reg.mode = UFFDIO_REGISTER_MODE_WP | UFFDIO_REGISTER_MODE_RWP;

	if (ioctl(gopts->uffd, UFFDIO_REGISTER, &reg) == 0) {
		uffd_test_fail("register with WP|RWP unexpectedly succeeded");
		return;
	}
	if (errno != EINVAL) {
		uffd_test_fail("register with WP|RWP: expected EINVAL, got %d",
			       errno);
		return;
	}
	uffd_test_pass();
}

static sigjmp_buf jbuf, *sigbuf;

static void sighndl(int sig, siginfo_t *siginfo, void *ptr)
{
	if (sig == SIGBUS) {
		if (sigbuf)
			siglongjmp(*sigbuf, 1);
		abort();
	}
}

/*
 * For non-cooperative userfaultfd test we fork() a process that will
 * generate pagefaults, will mremap the area monitored by the
 * userfaultfd and at last this process will release the monitored
 * area.
 * For the anonymous and shared memory the area is divided into two
 * parts, the first part is accessed before mremap, and the second
 * part is accessed after mremap. Since hugetlbfs does not support
 * mremap, the entire monitored area is accessed in a single pass for
 * HUGETLB_TEST.
 * The release of the pages currently generates event for shmem and
 * anonymous memory (UFFD_EVENT_REMOVE), hence it is not checked
 * for hugetlb.
 * For signal test(UFFD_FEATURE_SIGBUS), signal_test = 1, we register
 * monitored area, generate pagefaults and test that signal is delivered.
 * Use UFFDIO_COPY to allocate missing page and retry. For signal_test = 2
 * test robustness use case - we release monitored area, fork a process
 * that will generate pagefaults and verify signal is generated.
 * This also tests UFFD_FEATURE_EVENT_FORK event along with the signal
 * feature. Using monitor thread, verify no userfault events are generated.
 */
static int faulting_process(uffd_global_test_opts_t *gopts, int signal_test, bool wp)
{
	unsigned long nr, i;
	unsigned long long count;
	unsigned long split_nr_pages;
	unsigned long lastnr;
	struct sigaction act;
	volatile unsigned long signalled = 0;

	split_nr_pages = (gopts->nr_pages + 1) / 2;

	if (signal_test) {
		sigbuf = &jbuf;
		memset(&act, 0, sizeof(act));
		act.sa_sigaction = sighndl;
		act.sa_flags = SA_SIGINFO;
		if (sigaction(SIGBUS, &act, 0))
			err("sigaction");
		lastnr = (unsigned long)-1;
	}

	for (nr = 0; nr < split_nr_pages; nr++) {
		volatile int steps = 1;
		unsigned long offset = nr * gopts->page_size;

		if (signal_test) {
			if (sigsetjmp(*sigbuf, 1) != 0) {
				if (steps == 1 && nr == lastnr)
					err("Signal repeated");

				lastnr = nr;
				if (signal_test == 1) {
					if (steps == 1) {
						/* This is a MISSING request */
						steps++;
						if (copy_page(gopts, offset, wp))
							signalled++;
					} else {
						/* This is a WP request */
						assert(steps == 2);
						wp_range(gopts->uffd,
							 (__u64)gopts->area_dst +
							 offset,
							 gopts->page_size, false);
					}
				} else {
					signalled++;
					continue;
				}
			}
		}

		count = *area_count(gopts->area_dst, nr, gopts);
		if (count != gopts->count_verify[nr])
			err("nr %lu memory corruption %llu %llu\n",
			    nr, count, gopts->count_verify[nr]);
		/*
		 * Trigger write protection if there is by writing
		 * the same value back.
		 */
		*area_count(gopts->area_dst, nr, gopts) = count;
	}

	if (signal_test)
		return signalled != split_nr_pages;

	gopts->area_dst = mremap(gopts->area_dst, gopts->nr_pages * gopts->page_size,
				 gopts->nr_pages * gopts->page_size,
				 MREMAP_MAYMOVE | MREMAP_FIXED,
				 gopts->area_src);
	if (gopts->area_dst == MAP_FAILED)
		err("mremap");
	/* Reset area_src since we just clobbered it */
	gopts->area_src = NULL;

	for (; nr < gopts->nr_pages; nr++) {
		count = *area_count(gopts->area_dst, nr, gopts);
		if (count != gopts->count_verify[nr]) {
			err("nr %lu memory corruption %llu %llu\n",
			    nr, count, gopts->count_verify[nr]);
		}
		/*
		 * Trigger write protection if there is by writing
		 * the same value back.
		 */
		*area_count(gopts->area_dst, nr, gopts) = count;
	}

	uffd_test_ops->release_pages(gopts, gopts->area_dst);

	for (nr = 0; nr < gopts->nr_pages; nr++)
		for (i = 0; i < gopts->page_size; i++)
			if (*(gopts->area_dst + nr * gopts->page_size + i) != 0)
				err("page %lu offset %lu is not zero", nr, i);

	return 0;
}

static void uffd_sigbus_test_common(uffd_global_test_opts_t *gopts, bool wp)
{
	unsigned long userfaults;
	pthread_t uffd_mon;
	pid_t pid;
	int err;
	char c = '\0';
	struct uffd_args args = { 0 };
	args.gopts = gopts;

	gopts->ready_for_fork = false;

	fcntl(gopts->uffd, F_SETFL, gopts->uffd_flags | O_NONBLOCK);

	if (uffd_register(gopts->uffd, gopts->area_dst, gopts->nr_pages * gopts->page_size,
			  true, wp, false))
		err("register failure");

	if (faulting_process(gopts, 1, wp))
		err("faulting process failed");

	uffd_test_ops->release_pages(gopts, gopts->area_dst);

	args.apply_wp = wp;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	while (!gopts->ready_for_fork)
		; /* Wait for the poll_thread to start executing before forking */

	pid = fork();
	if (pid < 0)
		err("fork");

	if (!pid)
		_exit(faulting_process(gopts, 2, wp));

	waitpid(pid, &err, 0);
	if (err)
		err("faulting process failed");
	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, (void **)&userfaults))
		err("pthread_join()");

	if (userfaults)
		uffd_test_fail("Signal test failed, userfaults: %ld", userfaults);
	else
		uffd_test_pass();
}

static void uffd_sigbus_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_sigbus_test_common(gopts, false);
}

static void uffd_sigbus_wp_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_sigbus_test_common(gopts, true);
}

static void uffd_events_test_common(uffd_global_test_opts_t *gopts, bool wp)
{
	pthread_t uffd_mon;
	pid_t pid;
	int err;
	char c = '\0';
	struct uffd_args args = { 0 };
	args.gopts = gopts;

	gopts->ready_for_fork = false;

	fcntl(gopts->uffd, F_SETFL, gopts->uffd_flags | O_NONBLOCK);
	if (uffd_register(gopts->uffd, gopts->area_dst, gopts->nr_pages * gopts->page_size,
			  true, wp, false))
		err("register failure");

	args.apply_wp = wp;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	while (!gopts->ready_for_fork)
		; /* Wait for the poll_thread to start executing before forking */

	pid = fork();
	if (pid < 0)
		err("fork");

	if (!pid)
		_exit(faulting_process(gopts, 0, wp));

	waitpid(pid, &err, 0);
	if (err)
		err("faulting process failed");
	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("pthread_join()");

	if (args.missing_faults != gopts->nr_pages)
		uffd_test_fail("Fault counts wrong");
	else
		uffd_test_pass();
}

static void uffd_events_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_events_test_common(gopts, false);
}

static void uffd_events_wp_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	uffd_events_test_common(gopts, true);
}

static void retry_uffdio_zeropage(uffd_global_test_opts_t *gopts,
				  struct uffdio_zeropage *uffdio_zeropage)
{
	uffd_test_ops->alias_mapping(gopts, &uffdio_zeropage->range.start,
				     uffdio_zeropage->range.len,
				     0);
	if (ioctl(gopts->uffd, UFFDIO_ZEROPAGE, uffdio_zeropage)) {
		if (uffdio_zeropage->zeropage != -EEXIST)
			err("UFFDIO_ZEROPAGE error: %"PRId64,
			    (int64_t)uffdio_zeropage->zeropage);
	} else {
		err("UFFDIO_ZEROPAGE error: %"PRId64,
		    (int64_t)uffdio_zeropage->zeropage);
	}
}

static bool do_uffdio_zeropage(uffd_global_test_opts_t *gopts, bool has_zeropage)
{
	struct uffdio_zeropage uffdio_zeropage = { 0 };
	int ret;
	__s64 res;

	uffdio_zeropage.range.start = (unsigned long) gopts->area_dst;
	uffdio_zeropage.range.len = gopts->page_size;
	uffdio_zeropage.mode = 0;
	ret = ioctl(gopts->uffd, UFFDIO_ZEROPAGE, &uffdio_zeropage);
	res = uffdio_zeropage.zeropage;
	if (ret) {
		/* real retval in ufdio_zeropage.zeropage */
		if (has_zeropage)
			err("UFFDIO_ZEROPAGE error: %"PRId64, (int64_t)res);
		else if (res != -EINVAL)
			err("UFFDIO_ZEROPAGE not -EINVAL");
	} else if (has_zeropage) {
		if (res != gopts->page_size)
			err("UFFDIO_ZEROPAGE unexpected size");
		else
			retry_uffdio_zeropage(gopts, &uffdio_zeropage);
		return true;
	} else
		err("UFFDIO_ZEROPAGE succeeded");

	return false;
}

/*
 * Registers a range with MISSING mode only for zeropage test.  Return true
 * if UFFDIO_ZEROPAGE supported, false otherwise. Can't use uffd_register()
 * because we want to detect .ioctls along the way.
 */
static bool
uffd_register_detect_zeropage(int uffd, void *addr, uint64_t len)
{
	uint64_t ioctls = 0;

	if (uffd_register_with_ioctls(uffd, addr, len, true,
				      false, false, &ioctls))
		err("zeropage register fail");

	return ioctls & (1 << _UFFDIO_ZEROPAGE);
}

/* exercise UFFDIO_ZEROPAGE */
static void uffd_zeropage_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	bool has_zeropage;
	int i;

	has_zeropage = uffd_register_detect_zeropage(gopts->uffd,
						     gopts->area_dst,
						     gopts->page_size);
	if (gopts->area_dst_alias)
		/* Ignore the retval; we already have it */
		uffd_register_detect_zeropage(gopts->uffd, gopts->area_dst_alias, gopts->page_size);

	if (do_uffdio_zeropage(gopts, has_zeropage))
		for (i = 0; i < gopts->page_size; i++)
			if (gopts->area_dst[i] != 0)
				err("data non-zero at offset %d\n", i);

	if (uffd_unregister(gopts->uffd, gopts->area_dst, gopts->page_size))
		err("unregister");

	if (gopts->area_dst_alias && uffd_unregister(gopts->uffd,
						     gopts->area_dst_alias,
						     gopts->page_size))
		err("unregister");

	uffd_test_pass();
}

static void uffd_register_poison(int uffd, void *addr, uint64_t len)
{
	uint64_t ioctls = 0;
	uint64_t expected = (1 << _UFFDIO_COPY) | (1 << _UFFDIO_POISON);

	if (uffd_register_with_ioctls(uffd, addr, len, true,
				      false, false, &ioctls))
		err("poison register fail");

	if ((ioctls & expected) != expected)
		err("registered area doesn't support COPY and POISON ioctls");
}

static void do_uffdio_poison(uffd_global_test_opts_t *gopts, unsigned long offset)
{
	struct uffdio_poison uffdio_poison = { 0 };
	int ret;
	__s64 res;

	uffdio_poison.range.start = (unsigned long) gopts->area_dst + offset;
	uffdio_poison.range.len = gopts->page_size;
	uffdio_poison.mode = 0;
	ret = ioctl(gopts->uffd, UFFDIO_POISON, &uffdio_poison);
	res = uffdio_poison.updated;

	if (ret)
		err("UFFDIO_POISON error: %"PRId64, (int64_t)res);
	else if (res != gopts->page_size)
		err("UFFDIO_POISON unexpected size: %"PRId64, (int64_t)res);
}

static void uffd_poison_handle_fault(uffd_global_test_opts_t *gopts,
				     struct uffd_msg *msg,
				     struct uffd_args *args)
{
	unsigned long offset;

	if (msg->event != UFFD_EVENT_PAGEFAULT)
		err("unexpected msg event %u", msg->event);

	if (msg->arg.pagefault.flags &
	    (UFFD_PAGEFAULT_FLAG_WP | UFFD_PAGEFAULT_FLAG_MINOR))
		err("unexpected fault type %llu", msg->arg.pagefault.flags);

	offset = (char *)(unsigned long)msg->arg.pagefault.address - gopts->area_dst;
	offset &= ~(gopts->page_size-1);

	/* Odd pages -> copy zeroed page; even pages -> poison. */
	if (offset & gopts->page_size)
		copy_page(gopts, offset, false);
	else
		do_uffdio_poison(gopts, offset);
}

/* Make sure to cover odd/even, and minimum duplications */
#define  UFFD_POISON_TEST_NPAGES  4

static void uffd_poison_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *targs)
{
	pthread_t uffd_mon;
	char c;
	struct uffd_args args = { 0 };
	struct sigaction act = { 0 };
	unsigned long nr_sigbus = 0;
	unsigned long nr, poison_pages = UFFD_POISON_TEST_NPAGES;

	if (gopts->nr_pages < poison_pages) {
		uffd_test_skip("Too less pages for POISON test");
		return;
	}

	args.gopts = gopts;

	fcntl(gopts->uffd, F_SETFL, gopts->uffd_flags | O_NONBLOCK);

	uffd_register_poison(gopts->uffd, gopts->area_dst, poison_pages * gopts->page_size);
	memset(gopts->area_src, 0, poison_pages * gopts->page_size);

	args.handle_fault = uffd_poison_handle_fault;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	sigbuf = &jbuf;
	act.sa_sigaction = sighndl;
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGBUS, &act, 0))
		err("sigaction");

	for (nr = 0; nr < poison_pages; ++nr) {
		unsigned long offset = nr * gopts->page_size;
		const char *bytes = (const char *) gopts->area_dst + offset;
		const char *i;

		if (sigsetjmp(*sigbuf, 1)) {
			/*
			 * Access below triggered a SIGBUS, which was caught by
			 * sighndl, which then jumped here. Count this SIGBUS,
			 * and move on to next page.
			 */
			++nr_sigbus;
			continue;
		}

		for (i = bytes; i < bytes + gopts->page_size; ++i) {
			if (*i)
				err("nonzero byte in area_dst (%p) at %p: %u",
				    gopts->area_dst, i, *i);
		}
	}

	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("pthread_join()");

	if (nr_sigbus != poison_pages / 2)
		err("expected to receive %lu SIGBUS, actually received %lu",
		    poison_pages / 2, nr_sigbus);

	uffd_test_pass();
}

static void
uffd_move_handle_fault_common(uffd_global_test_opts_t *gopts,
			      struct uffd_msg *msg,
			      struct uffd_args *args,
			      unsigned long len)
{
	unsigned long offset;

	if (msg->event != UFFD_EVENT_PAGEFAULT)
		err("unexpected msg event %u", msg->event);

	if (msg->arg.pagefault.flags &
	    (UFFD_PAGEFAULT_FLAG_WP | UFFD_PAGEFAULT_FLAG_MINOR | UFFD_PAGEFAULT_FLAG_WRITE))
		err("unexpected fault type %llu", msg->arg.pagefault.flags);

	offset = (char *)(unsigned long)msg->arg.pagefault.address - gopts->area_dst;
	offset &= ~(len-1);

	if (move_page(gopts, offset, len))
		args->missing_faults++;
}

static void uffd_move_handle_fault(uffd_global_test_opts_t *gopts, struct uffd_msg *msg,
				   struct uffd_args *args)
{
	uffd_move_handle_fault_common(gopts, msg, args, gopts->page_size);
}

static void uffd_move_pmd_handle_fault(uffd_global_test_opts_t *gopts, struct uffd_msg *msg,
				       struct uffd_args *args)
{
	uffd_move_handle_fault_common(gopts, msg, args, read_pmd_pagesize());
}

static void
uffd_move_test_common(uffd_global_test_opts_t *gopts,
		      uffd_test_args_t *targs,
		      unsigned long chunk_size,
		      void (*handle_fault)(struct uffd_global_test_opts *gopts,
		      struct uffd_msg *msg, struct uffd_args *args)
)
{
	unsigned long nr;
	pthread_t uffd_mon;
	char c = '\0';
	unsigned long long count;
	struct uffd_args args = { 0 };
	char *orig_area_src = NULL, *orig_area_dst = NULL;
	unsigned long step_size, step_count;
	unsigned long src_offs = 0;
	unsigned long dst_offs = 0;

	args.gopts = gopts;

	/* Prevent source pages from being mapped more than once */
	if (madvise(gopts->area_src, gopts->nr_pages * gopts->page_size, MADV_DONTFORK))
		err("madvise(MADV_DONTFORK) failure");

	if (uffd_register(gopts->uffd, gopts->area_dst, gopts->nr_pages * gopts->page_size,
			  true, false, false))
		err("register failure");

	args.handle_fault = handle_fault;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	step_size = chunk_size / gopts->page_size;
	step_count = gopts->nr_pages / step_size;

	if (chunk_size > gopts->page_size) {
		char *aligned_src = ALIGN_UP(gopts->area_src, chunk_size);
		char *aligned_dst = ALIGN_UP(gopts->area_dst, chunk_size);

		if (aligned_src != gopts->area_src || aligned_dst != gopts->area_dst) {
			src_offs = (aligned_src - gopts->area_src) / gopts->page_size;
			dst_offs = (aligned_dst - gopts->area_dst) / gopts->page_size;
			step_count--;
		}
		orig_area_src = gopts->area_src;
		orig_area_dst = gopts->area_dst;
		gopts->area_src = aligned_src;
		gopts->area_dst = aligned_dst;
	}

	/*
	 * Read each of the pages back using the UFFD-registered mapping. We
	 * expect that the first time we touch a page, it will result in a missing
	 * fault. uffd_poll_thread will resolve the fault by moving source
	 * page to destination.
	 */
	for (nr = 0; nr < step_count * step_size; nr += step_size) {
		unsigned long i;

		/* Check area_src content */
		for (i = 0; i < step_size; i++) {
			count = *area_count(gopts->area_src, nr + i, gopts);
			if (count != gopts->count_verify[src_offs + nr + i])
				err("nr %lu source memory invalid %llu %llu\n",
				    nr + i, count, gopts->count_verify[src_offs + nr + i]);
		}

		/* Faulting into area_dst should move the page or the huge page */
		for (i = 0; i < step_size; i++) {
			count = *area_count(gopts->area_dst, nr + i, gopts);
			if (count != gopts->count_verify[dst_offs + nr + i])
				err("nr %lu memory corruption %llu %llu\n",
				    nr, count, gopts->count_verify[dst_offs + nr + i]);
		}

		/* Re-check area_src content which should be empty */
		for (i = 0; i < step_size; i++) {
			count = *area_count(gopts->area_src, nr + i, gopts);
			if (count != 0)
				err("nr %lu move failed %llu %llu\n",
				    nr, count, gopts->count_verify[src_offs + nr + i]);
		}
	}
	if (chunk_size > gopts->page_size) {
		gopts->area_src = orig_area_src;
		gopts->area_dst = orig_area_dst;
	}

	if (write(gopts->pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("join() failed");

	if (args.missing_faults != step_count || args.minor_faults != 0)
		uffd_test_fail("stats check error");
	else
		uffd_test_pass();
}

static void uffd_move_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *targs)
{
	uffd_move_test_common(gopts, targs, gopts->page_size, uffd_move_handle_fault);
}

static void uffd_move_pmd_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *targs)
{
	if (madvise(gopts->area_dst, gopts->nr_pages * gopts->page_size, MADV_HUGEPAGE))
		err("madvise(MADV_HUGEPAGE) failure");
	uffd_move_test_common(gopts, targs, read_pmd_pagesize(),
			      uffd_move_pmd_handle_fault);
}

static void uffd_move_pmd_split_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *targs)
{
	if (madvise(gopts->area_dst, gopts->nr_pages * gopts->page_size, MADV_NOHUGEPAGE))
		err("madvise(MADV_NOHUGEPAGE) failure");
	uffd_move_test_common(gopts, targs, read_pmd_pagesize(),
			      uffd_move_pmd_handle_fault);
}

static bool
uffdio_verify_results(const char *name, int ret, int error, long result)
{
	/*
	 * Should always return -1 with errno=EAGAIN, with corresponding
	 * result field updated in ioctl() args to be -EAGAIN too
	 * (e.g. copy.copy field for UFFDIO_COPY).
	 */
	if (ret != -1) {
		uffd_test_fail("%s should have returned -1", name);
		return false;
	}

	if (error != EAGAIN) {
		uffd_test_fail("%s should have errno==EAGAIN", name);
		return false;
	}

	if (result != -EAGAIN) {
		uffd_test_fail("%s should have been updated for -EAGAIN",
			       name);
		return false;
	}

	return true;
}

/*
 * This defines a function to test one ioctl.  Note that here "field" can
 * be 1 or anything not -EAGAIN.  With that initial value set, we can
 * verify later that it should be updated by kernel (when -EAGAIN
 * returned), by checking whether it is also updated to -EAGAIN.
 */
#define DEFINE_MMAP_CHANGING_TEST(name, ioctl_name, field)		\
	static bool uffdio_mmap_changing_test_##name(int fd)		\
	{								\
		int ret;						\
		struct uffdio_##name args = {				\
			.field = 1,					\
		};							\
		ret = ioctl(fd, ioctl_name, &args);			\
		return uffdio_verify_results(#ioctl_name, ret, errno, args.field); \
	}

DEFINE_MMAP_CHANGING_TEST(zeropage, UFFDIO_ZEROPAGE, zeropage)
DEFINE_MMAP_CHANGING_TEST(copy, UFFDIO_COPY, copy)
DEFINE_MMAP_CHANGING_TEST(move, UFFDIO_MOVE, move)
DEFINE_MMAP_CHANGING_TEST(poison, UFFDIO_POISON, updated)
DEFINE_MMAP_CHANGING_TEST(continue, UFFDIO_CONTINUE, mapped)

typedef enum {
	/* We actually do not care about any state except UNINTERRUPTIBLE.. */
	THR_STATE_UNKNOWN = 0,
	THR_STATE_UNINTERRUPTIBLE,
} thread_state;

typedef struct {
	uffd_global_test_opts_t *gopts;
	volatile pid_t *pid;
} mmap_changing_thread_args;

static void sleep_short(void)
{
	usleep(1000);
}

static thread_state thread_state_get(pid_t tid)
{
	const char *header = "State:\t";
	char tmp[256], *p, c;
	FILE *fp;

	snprintf(tmp, sizeof(tmp), "/proc/%d/status", tid);
	fp = fopen(tmp, "r");

	if (!fp)
		return THR_STATE_UNKNOWN;

	while (fgets(tmp, sizeof(tmp), fp)) {
		p = strstr(tmp, header);
		if (p) {
			/* For example, "State:\tD (disk sleep)" */
			c = *(p + strlen(header));
			return c == 'D' ?
			    THR_STATE_UNINTERRUPTIBLE : THR_STATE_UNKNOWN;
		}
	}

	return THR_STATE_UNKNOWN;
}

static void thread_state_until(pid_t tid, thread_state state)
{
	thread_state s;

	do {
		s = thread_state_get(tid);
		sleep_short();
	} while (s != state);
}

static void *uffd_mmap_changing_thread(void *opaque)
{
	mmap_changing_thread_args *args = opaque;
	uffd_global_test_opts_t *gopts = args->gopts;
	volatile pid_t *pid = args->pid;
	int ret;

	/* Unfortunately, it's only fetch-able from the thread itself.. */
	assert(*pid == 0);
	*pid = syscall(SYS_gettid);

	/* Inject an event, this will hang solid until the event read */
	ret = madvise(gopts->area_dst, gopts->page_size, MADV_REMOVE);
	if (ret)
		err("madvise(MADV_REMOVE) failed");

	return NULL;
}

static void uffd_consume_message(uffd_global_test_opts_t *gopts)
{
	struct uffd_msg msg = { 0 };

	while (uffd_read_msg(gopts, &msg));
}

static void uffd_mmap_changing_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *targs)
{
	/*
	 * This stores the real PID (which can be different from how tid is
	 * defined..) for the child thread, 0 means not initialized.
	 */
	pid_t pid = 0;
	pthread_t tid;
	int ret;
	mmap_changing_thread_args args = { gopts, &pid };

	if (uffd_register(gopts->uffd, gopts->area_dst, gopts->nr_pages * gopts->page_size,
			  true, false, false))
		err("uffd_register() failed");

	/* Create a thread to generate the racy event */
	ret = pthread_create(&tid, NULL, uffd_mmap_changing_thread, &args);
	if (ret)
		err("pthread_create() failed");

	/*
	 * Wait until the thread setup the pid.  Use volatile to make sure
	 * it reads from RAM not regs.
	 */
	while (!(volatile pid_t)pid)
		sleep_short();

	/* Wait until the thread hangs at REMOVE event */
	thread_state_until(pid, THR_STATE_UNINTERRUPTIBLE);

	if (!uffdio_mmap_changing_test_copy(gopts->uffd))
		return;

	if (!uffdio_mmap_changing_test_zeropage(gopts->uffd))
		return;

	if (!uffdio_mmap_changing_test_move(gopts->uffd))
		return;

	if (!uffdio_mmap_changing_test_poison(gopts->uffd))
		return;

	if (!uffdio_mmap_changing_test_continue(gopts->uffd))
		return;

	/*
	 * All succeeded above!  Recycle everything.  Start by reading the
	 * event so as to kick the thread roll again..
	 */
	uffd_consume_message(gopts);

	ret = pthread_join(tid, NULL);
	assert(ret == 0);

	uffd_test_pass();
}

static int prevent_hugepages(uffd_global_test_opts_t *gopts, const char **errmsg)
{
	/* This should be done before source area is populated */
	if (madvise(gopts->area_src, gopts->nr_pages * gopts->page_size, MADV_NOHUGEPAGE)) {
		/* Ignore only if CONFIG_TRANSPARENT_HUGEPAGE=n */
		if (errno != EINVAL) {
			if (errmsg)
				*errmsg = "madvise(MADV_NOHUGEPAGE) failed";
			return -errno;
		}
	}
	return 0;
}

static int request_hugepages(uffd_global_test_opts_t *gopts, const char **errmsg)
{
	/* This should be done before source area is populated */
	if (madvise(gopts->area_src, gopts->nr_pages * gopts->page_size, MADV_HUGEPAGE)) {
		if (errmsg) {
			*errmsg = (errno == EINVAL) ?
				"CONFIG_TRANSPARENT_HUGEPAGE is not set" :
				"madvise(MADV_HUGEPAGE) failed";
		}
		return -errno;
	}
	return 0;
}

struct uffd_test_case_ops uffd_move_test_case_ops = {
	.post_alloc = prevent_hugepages,
};

struct uffd_test_case_ops uffd_move_test_pmd_case_ops = {
	.post_alloc = request_hugepages,
};

/*
 * Test the returned uffdio_register.ioctls with different register modes.
 * Note that _UFFDIO_ZEROPAGE is tested separately in the zeropage test.
 */
static void
do_register_ioctls_test(uffd_global_test_opts_t *gopts,
			uffd_test_args_t *args,
			bool miss,
			bool wp,
			bool minor)
{
	uint64_t ioctls = 0, expected = BIT_ULL(_UFFDIO_WAKE);
	mem_type_t *mem_type = args->mem_type;
	int ret;

	ret = uffd_register_with_ioctls(gopts->uffd, gopts->area_dst, gopts->page_size,
					miss, wp, minor, &ioctls);

	/*
	 * Handle special cases of UFFDIO_REGISTER here where it should
	 * just fail with -EINVAL first..
	 *
	 * Case 1: register MINOR on anon
	 * Case 2: register with no mode selected
	 */
	if ((minor && (mem_type->mem_flag == MEM_ANON)) ||
	    (!miss && !wp && !minor)) {
		if (ret != -EINVAL)
			err("register (miss=%d, wp=%d, minor=%d) failed "
			    "with wrong errno=%d", miss, wp, minor, ret);
		return;
	}

	/* UFFDIO_REGISTER should succeed, then check ioctls returned */
	if (miss)
		expected |= BIT_ULL(_UFFDIO_COPY);
	if (wp)
		expected |= BIT_ULL(_UFFDIO_WRITEPROTECT);
	if (minor)
		expected |= BIT_ULL(_UFFDIO_CONTINUE);

	if ((ioctls & expected) != expected)
		err("unexpected uffdio_register.ioctls "
		    "(miss=%d, wp=%d, minor=%d): expected=0x%"PRIx64", "
		    "returned=0x%"PRIx64, miss, wp, minor, expected, ioctls);

	if (uffd_unregister(gopts->uffd, gopts->area_dst, gopts->page_size))
		err("unregister");
}

static void uffd_register_ioctls_test(uffd_global_test_opts_t *gopts, uffd_test_args_t *args)
{
	int miss, wp, minor;

	for (miss = 0; miss <= 1; miss++)
		for (wp = 0; wp <= 1; wp++)
			for (minor = 0; minor <= 1; minor++)
				do_register_ioctls_test(gopts, args, miss, wp, minor);

	uffd_test_pass();
}

uffd_test_case_t uffd_tests[] = {
	{
		/* Test returned uffdio_register.ioctls. */
		.name = "register-ioctls",
		.uffd_fn = uffd_register_ioctls_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_MISSING_HUGETLBFS |
		UFFD_FEATURE_MISSING_SHMEM |
		UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM |
		UFFD_FEATURE_MINOR_HUGETLBFS |
		UFFD_FEATURE_MINOR_SHMEM,
	},
	{
		.name = "zeropage",
		.uffd_fn = uffd_zeropage_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = 0,
	},
	{
		.name = "move",
		.uffd_fn = uffd_move_test,
		.mem_targets = MEM_ANON,
		.uffd_feature_required = UFFD_FEATURE_MOVE,
		.test_case_ops = &uffd_move_test_case_ops,
	},
	{
		.name = "move-pmd",
		.uffd_fn = uffd_move_pmd_test,
		.mem_targets = MEM_ANON,
		.uffd_feature_required = UFFD_FEATURE_MOVE,
		.test_case_ops = &uffd_move_test_pmd_case_ops,
	},
	{
		.name = "move-pmd-split",
		.uffd_fn = uffd_move_pmd_split_test,
		.mem_targets = MEM_ANON,
		.uffd_feature_required = UFFD_FEATURE_MOVE,
		.test_case_ops = &uffd_move_test_pmd_case_ops,
	},
	{
		.name = "wp-fork",
		.uffd_fn = uffd_wp_fork_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM,
	},
	{
		.name = "wp-fork-with-event",
		.uffd_fn = uffd_wp_fork_with_event_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM |
		/* when set, child process should inherit uffd-wp bits */
		UFFD_FEATURE_EVENT_FORK,
	},
	{
		.name = "wp-fork-pin",
		.uffd_fn = uffd_wp_fork_pin_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM,
	},
	{
		.name = "wp-fork-pin-with-event",
		.uffd_fn = uffd_wp_fork_pin_with_event_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM |
		/* when set, child process should inherit uffd-wp bits */
		UFFD_FEATURE_EVENT_FORK,
	},
	{
		.name = "wp-unpopulated",
		.uffd_fn = uffd_wp_unpopulated_test,
		.mem_targets = MEM_ANON,
		.uffd_feature_required =
		UFFD_FEATURE_PAGEFAULT_FLAG_WP | UFFD_FEATURE_WP_UNPOPULATED,
	},
	{
		.name = "minor",
		.uffd_fn = uffd_minor_test,
		.mem_targets = MEM_SHMEM | MEM_HUGETLB,
		.uffd_feature_required =
		UFFD_FEATURE_MINOR_HUGETLBFS | UFFD_FEATURE_MINOR_SHMEM,
	},
	{
		.name = "minor-wp",
		.uffd_fn = uffd_minor_wp_test,
		.mem_targets = MEM_SHMEM | MEM_HUGETLB,
		.uffd_feature_required =
		UFFD_FEATURE_MINOR_HUGETLBFS | UFFD_FEATURE_MINOR_SHMEM |
		UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		/*
		 * HACK: here we leveraged WP_UNPOPULATED to detect whether
		 * minor mode supports wr-protect.  There's no feature flag
		 * for it so this is the best we can test against.
		 */
		UFFD_FEATURE_WP_UNPOPULATED,
	},
	{
		.name = "minor-collapse",
		.uffd_fn = uffd_minor_collapse_test,
		/* MADV_COLLAPSE only works with shmem */
		.mem_targets = MEM_SHMEM,
		/* We can't test MADV_COLLAPSE, so try our luck */
		.uffd_feature_required = UFFD_FEATURE_MINOR_SHMEM,
	},
	{
		.name = "rwp-async",
		.uffd_fn = uffd_rwp_async_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required =
		UFFD_FEATURE_RWP | UFFD_FEATURE_RWP_ASYNC,
	},
	{
		.name = "rwp-sync",
		.uffd_fn = uffd_rwp_sync_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_RWP,
	},
	{
		.name = "rwp-pagemap",
		.uffd_fn = uffd_rwp_pagemap_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required =
		UFFD_FEATURE_RWP | UFFD_FEATURE_RWP_ASYNC,
	},
	{
		.name = "rwp-mprotect",
		.uffd_fn = uffd_rwp_mprotect_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required =
		UFFD_FEATURE_RWP | UFFD_FEATURE_RWP_ASYNC,
	},
	{
		.name = "rwp-gup",
		.uffd_fn = uffd_rwp_gup_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required =
		UFFD_FEATURE_RWP | UFFD_FEATURE_RWP_ASYNC,
	},
	{
		.name = "rwp-async-toggle",
		.uffd_fn = uffd_rwp_async_toggle_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required =
		UFFD_FEATURE_RWP | UFFD_FEATURE_RWP_ASYNC,
	},
	{
		.name = "rwp-close",
		.uffd_fn = uffd_rwp_close_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_RWP,
	},
	{
		.name = "rwp-fork",
		.uffd_fn = uffd_rwp_fork_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required =
		UFFD_FEATURE_RWP | UFFD_FEATURE_EVENT_FORK,
	},
	{
		.name = "rwp-fork-pin",
		.uffd_fn = uffd_rwp_fork_pin_test,
		.mem_targets = MEM_ANON,
		.uffd_feature_required =
		UFFD_FEATURE_RWP | UFFD_FEATURE_RWP_ASYNC |
		UFFD_FEATURE_EVENT_FORK,
	},
	{
		.name = "rwp-swap-cow",
		.uffd_fn = uffd_rwp_swap_cow_test,
		.mem_targets = MEM_ANON,
		.uffd_feature_required = UFFD_FEATURE_RWP,
	},
	{
		.name = "rwp-wp-exclusive",
		.uffd_fn = uffd_rwp_wp_exclusive_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required =
		UFFD_FEATURE_RWP |
		UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM,
	},
	{
		.name = "sigbus",
		.uffd_fn = uffd_sigbus_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_SIGBUS |
		UFFD_FEATURE_EVENT_FORK,
	},
	{
		.name = "sigbus-wp",
		.uffd_fn = uffd_sigbus_wp_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_SIGBUS |
		UFFD_FEATURE_EVENT_FORK | UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM,
	},
	{
		.name = "events",
		.uffd_fn = uffd_events_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_EVENT_FORK |
		UFFD_FEATURE_EVENT_REMAP | UFFD_FEATURE_EVENT_REMOVE,
	},
	{
		.name = "events-wp",
		.uffd_fn = uffd_events_wp_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_EVENT_FORK |
		UFFD_FEATURE_EVENT_REMAP | UFFD_FEATURE_EVENT_REMOVE |
		UFFD_FEATURE_PAGEFAULT_FLAG_WP |
		UFFD_FEATURE_WP_HUGETLBFS_SHMEM,
	},
	{
		.name = "poison",
		.uffd_fn = uffd_poison_test,
		.mem_targets = MEM_ALL,
		.uffd_feature_required = UFFD_FEATURE_POISON,
	},
	{
		.name = "mmap-changing",
		.uffd_fn = uffd_mmap_changing_test,
		/*
		 * There's no point running this test over all mem types as
		 * they share the same code paths.
		 *
		 * Choose shmem for simplicity, because (1) shmem supports
		 * MINOR mode to cover UFFDIO_CONTINUE, and (2) shmem is
		 * almost always available (unlike hugetlb).  Here we
		 * abused SHMEM for UFFDIO_MOVE, but the test we want to
		 * cover doesn't yet need the correct memory type..
		 */
		.mem_targets = MEM_SHMEM,
		/*
		 * Any UFFD_FEATURE_EVENT_* should work to trigger the
		 * race logically, but choose the simplest (REMOVE).
		 *
		 * Meanwhile, since we'll cover quite a few new ioctl()s
		 * (CONTINUE, POISON, MOVE), skip this test for old kernels
		 * by choosing all of them.
		 */
		.uffd_feature_required = UFFD_FEATURE_EVENT_REMOVE |
		UFFD_FEATURE_MOVE | UFFD_FEATURE_POISON |
		UFFD_FEATURE_MINOR_SHMEM,
	},
};

static void usage(const char *prog)
{
	printf("usage: %s [-f TESTNAME]\n", prog);
	puts("");
	puts(" -f: test name to filter (e.g., event)");
	puts(" -h: show the help msg");
	puts(" -l: list tests only");
	puts("");
	exit(KSFT_FAIL);
}

static int uffd_count_tests(int n_tests, int n_mems, const char *test_filter)
{
	uffd_test_case_t *test;
	int i, j, count = 0;

	if (!test_filter)
		count += 2;	/* test_uffd_api(false) + test_uffd_api(true) */

	for (i = 0; i < n_tests; i++) {
		test = &uffd_tests[i];
		if (test_filter && !strstr(test->name, test_filter))
			continue;
		for (j = 0; j < n_mems; j++)
			if (test->mem_targets & mem_types[j].mem_flag)
				count++;
	}

	return count;
}

static unsigned long uffd_setup_hugetlb(void)
{
	unsigned long nr_hugepages, hp_size;

	hugetlb_save_settings();
	hp_size = default_huge_page_size();

	if (!hp_size)
		return 0;

	/* need twice UFFD_TEST_MEM_SIZE, one for src area and one for dst */
	nr_hugepages = 2 * MAX(UFFD_TEST_MEM_SIZE, hp_size * 2) / hp_size;
	hugetlb_set_nr_default_pages(nr_hugepages);

	if (hugetlb_free_default_pages() < nr_hugepages)
		return 0;

	return hp_size;
}

int main(int argc, char *argv[])
{
	int n_tests = sizeof(uffd_tests) / sizeof(uffd_test_case_t);
	int n_mems = sizeof(mem_types) / sizeof(mem_type_t);
	const char *test_filter = NULL;
	unsigned long hugepage_size;
	bool list_only = false;
	uffd_test_case_t *test;
	mem_type_t *mem_type;
	uffd_test_args_t args;
	const char *errmsg;
	int i, j, opt;

	while ((opt = getopt(argc, argv, "f:hl")) != -1) {
		switch (opt) {
		case 'f':
			test_filter = optarg;
			break;
		case 'l':
			list_only = true;
			break;
		case 'h':
		default:
			/* Unknown */
			usage(argv[0]);
			break;
		}
	}

	if (list_only) {
		for (i = 0; i < n_tests; i++) {
			test = &uffd_tests[i];
			if (test_filter && !strstr(test->name, test_filter))
				continue;
			printf("%s\n", test->name);
		}
		return KSFT_PASS;
	}

	hugepage_size = uffd_setup_hugetlb();

	ksft_print_header();
	ksft_set_plan(uffd_count_tests(n_tests, n_mems, test_filter));

	if (!test_filter) {
		test_uffd_api(false);
		test_uffd_api(true);
	}

	for (i = 0; i < n_tests; i++) {
		test = &uffd_tests[i];
		if (test_filter && !strstr(test->name, test_filter))
			continue;
		for (j = 0; j < n_mems; j++) {
			mem_type = &mem_types[j];

			/* Initialize global test options */
			uffd_global_test_opts_t gopts = { 0 };

			gopts.map_shared = mem_type->shared;
			uffd_test_ops = mem_type->mem_ops;
			uffd_test_case_ops = test->test_case_ops;

			if (!(test->mem_targets & mem_type->mem_flag))
				continue;

			uffd_test_start("%s on %s", test->name, mem_type->name);
			if (mem_type->mem_flag & (MEM_HUGETLB_PRIVATE | MEM_HUGETLB)) {
				gopts.page_size = hugepage_size;
				if (gopts.page_size == 0) {
					uffd_test_skip("not enough HugeTLB pages");
					continue;
				}
			} else {
				gopts.page_size = psize();
			}

			/* Ensure we have at least 2 pages */
			gopts.nr_pages = MAX(UFFD_TEST_MEM_SIZE, gopts.page_size * 2)
				/ gopts.page_size;

			gopts.nr_parallel = 1;

			/* Initialize test arguments */
			args.mem_type = mem_type;

			if (!uffd_feature_supported(test)) {
				uffd_test_skip("feature missing");
				continue;
			}
			if (uffd_test_ctx_init(&gopts, test->uffd_feature_required, &errmsg)) {
				uffd_test_skip(errmsg);
				continue;
			}
			/*
			 * RWP tracks protection on ptes; a THP-backed shmem/anon
			 * range (e.g. shmem_enabled=always) would split on
			 * rwprotect and change behaviour under the test. Keep
			 * such ranges off THP. hugetlb is huge by definition and
			 * rejects MADV_NOHUGEPAGE, so skip it.
			 */
			if ((test->uffd_feature_required & UFFD_FEATURE_RWP) &&
			    !(mem_type->mem_flag & (MEM_HUGETLB | MEM_HUGETLB_PRIVATE))) {
				unsigned long len = gopts.nr_pages * gopts.page_size;

				/*
				 * EINVAL means CONFIG_TRANSPARENT_HUGEPAGE=n:
				 * nothing to opt out of.
				 */
				if (madvise(gopts.area_dst, len, MADV_NOHUGEPAGE) &&
				    errno != EINVAL)
					err("madvise(MADV_NOHUGEPAGE)");
			}
			test->uffd_fn(&gopts, &args);
			uffd_test_ctx_clear(&gopts);
		}
	}

	ksft_finished();
}

#else /* __NR_userfaultfd */

#warning "missing __NR_userfaultfd definition"

int main(void)
{
	ksft_print_header();
	ksft_exit_skip("missing __NR_userfaultfd definition\n");
}

#endif /* __NR_userfaultfd */
