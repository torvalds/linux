// SPDX-License-Identifier: GPL-2.0-only
/*
 * Userfaultfd unit tests.
 *
 *  Copyright (C) 2015-2023  Red Hat, Inc.
 */

#include "uffd-common.h"

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
typedef void (*uffd_test_fn)(uffd_test_args_t *);

typedef struct {
	const char *name;
	uffd_test_fn uffd_fn;
	unsigned int mem_targets;
	uint64_t uffd_feature_required;
} uffd_test_case_t;

static void uffd_test_report(void)
{
	printf("Userfaults unit tests: pass=%u, skip=%u, fail=%u (total=%u)\n",
	       ksft_get_pass_cnt(),
	       ksft_get_xskip_cnt(),
	       ksft_get_fail_cnt(),
	       ksft_test_num());
}

static void uffd_test_pass(void)
{
	printf("done\n");
	ksft_inc_pass_cnt();
}

#define  uffd_test_start(...)  do {		\
		printf("Testing ");		\
		printf(__VA_ARGS__);		\
		printf("... ");			\
		fflush(stdout);			\
	} while (0)

#define  uffd_test_fail(...)  do {		\
		printf("failed [reason: ");	\
		printf(__VA_ARGS__);		\
		printf("]\n");			\
		ksft_inc_fail_cnt();		\
	} while (0)

#define  uffd_test_skip(...)  do {		\
		printf("skipped [reason: ");	\
		printf(__VA_ARGS__);		\
		printf("]\n");			\
		ksft_inc_xskip_cnt();		\
	} while (0)

/*
 * Returns 1 if specific userfaultfd supported, 0 otherwise.  Note, we'll
 * return 1 even if some test failed as long as uffd supported, because in
 * that case we still want to proceed with the rest uffd unit tests.
 */
static int test_uffd_api(bool use_dev)
{
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
		return 0;
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

	/* Test double requests of UFFDIO_API with a random feature set */
	uffdio_api.features = BIT_ULL(0);
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == 0) {
		uffd_test_fail("UFFDIO_API should reject initialized uffd");
		goto out;
	}

	uffd_test_pass();
out:
	close(uffd);
	/* We have a valid uffd handle */
	return 1;
}

/*
 * This function initializes the global variables.  TODO: remove global
 * vars and then remove this.
 */
static int
uffd_setup_environment(uffd_test_args_t *args, uffd_test_case_t *test,
		       mem_type_t *mem_type, const char **errmsg)
{
	map_shared = mem_type->shared;
	uffd_test_ops = mem_type->mem_ops;

	if (mem_type->mem_flag & (MEM_HUGETLB_PRIVATE | MEM_HUGETLB))
		page_size = default_huge_page_size();
	else
		page_size = psize();

	nr_pages = UFFD_TEST_MEM_SIZE / page_size;
	/* TODO: remove this global var.. it's so ugly */
	nr_cpus = 1;

	/* Initialize test arguments */
	args->mem_type = mem_type;

	return uffd_test_ctx_init(test->uffd_feature_required, errmsg);
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
	int parent_uffd, child_uffd;
} fork_event_args;

static void *fork_event_consumer(void *data)
{
	fork_event_args *args = data;
	struct uffd_msg msg = { 0 };

	/* Read until a full msg received */
	while (uffd_read_msg(args->parent_uffd, &msg));

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

static int pagemap_test_fork(int uffd, bool with_event, bool test_pin)
{
	fork_event_args args = { .parent_uffd = uffd, .child_uffd = -1 };
	pthread_t thread;
	pid_t child;
	uint64_t value;
	int fd, result;

	/* Prepare a thread to resolve EVENT_FORK */
	if (with_event) {
		if (pthread_create(&thread, NULL, fork_event_consumer, &args))
			err("pthread_create()");
	}

	child = fork();
	if (!child) {
		/* Open the pagemap fd of the child itself */
		pin_args args = {};

		fd = pagemap_open();

		if (test_pin && pin_pages(&args, area_dst, page_size))
			/*
			 * Normally when reach here we have pinned in
			 * previous tests, so shouldn't fail anymore
			 */
			err("pin page failed in child");

		value = pagemap_get_entry(fd, area_dst);
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
		exit(0);
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

static void uffd_wp_unpopulated_test(uffd_test_args_t *args)
{
	uint64_t value;
	int pagemap_fd;

	if (uffd_register(uffd, area_dst, nr_pages * page_size,
			  false, true, false))
		err("register failed");

	pagemap_fd = pagemap_open();

	/* Test applying pte marker to anon unpopulated */
	wp_range(uffd, (uint64_t)area_dst, page_size, true);
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, true);

	/* Test unprotect on anon pte marker */
	wp_range(uffd, (uint64_t)area_dst, page_size, false);
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, false);

	/* Test zap on anon marker */
	wp_range(uffd, (uint64_t)area_dst, page_size, true);
	if (madvise(area_dst, page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, false);

	/* Test fault in after marker removed */
	*area_dst = 1;
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, false);
	/* Drop it to make pte none again */
	if (madvise(area_dst, page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");

	/* Test read-zero-page upon pte marker */
	wp_range(uffd, (uint64_t)area_dst, page_size, true);
	*(volatile char *)area_dst;
	/* Drop it to make pte none again */
	if (madvise(area_dst, page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");

	uffd_test_pass();
}

static void uffd_wp_fork_test_common(uffd_test_args_t *args,
				     bool with_event)
{
	int pagemap_fd;
	uint64_t value;

	if (uffd_register(uffd, area_dst, nr_pages * page_size,
			  false, true, false))
		err("register failed");

	pagemap_fd = pagemap_open();

	/* Touch the page */
	*area_dst = 1;
	wp_range(uffd, (uint64_t)area_dst, page_size, true);
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, true);
	if (pagemap_test_fork(uffd, with_event, false)) {
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
		if (madvise(area_dst, page_size, MADV_DONTNEED))
			err("MADV_DONTNEED");
	} else {
		/*
		 * NOTE: ignore retval because private-hugetlb doesn't yet
		 * support swapping, so it could fail.
		 */
		madvise(area_dst, page_size, MADV_PAGEOUT);
	}

	/* Uffd-wp should persist even swapped out */
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, true);
	if (pagemap_test_fork(uffd, with_event, false)) {
		uffd_test_fail("Detected %s uffd-wp bit in child in zapped pte",
			       with_event ? "missing" : "stall");
		goto out;
	}

	/* Unprotect; this tests swap pte modifications */
	wp_range(uffd, (uint64_t)area_dst, page_size, false);
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, false);

	/* Fault in the page from disk */
	*area_dst = 2;
	value = pagemap_get_entry(pagemap_fd, area_dst);
	pagemap_check_wp(value, false);
	uffd_test_pass();
out:
	if (uffd_unregister(uffd, area_dst, nr_pages * page_size))
		err("unregister failed");
	close(pagemap_fd);
}

static void uffd_wp_fork_test(uffd_test_args_t *args)
{
	uffd_wp_fork_test_common(args, false);
}

static void uffd_wp_fork_with_event_test(uffd_test_args_t *args)
{
	uffd_wp_fork_test_common(args, true);
}

static void uffd_wp_fork_pin_test_common(uffd_test_args_t *args,
					 bool with_event)
{
	int pagemap_fd;
	pin_args pin_args = {};

	if (uffd_register(uffd, area_dst, page_size, false, true, false))
		err("register failed");

	pagemap_fd = pagemap_open();

	/* Touch the page */
	*area_dst = 1;
	wp_range(uffd, (uint64_t)area_dst, page_size, true);

	/*
	 * 1. First pin, then fork().  This tests fork() special path when
	 * doing early CoW if the page is private.
	 */
	if (pin_pages(&pin_args, area_dst, page_size)) {
		uffd_test_skip("Possibly CONFIG_GUP_TEST missing "
			       "or unprivileged");
		close(pagemap_fd);
		uffd_unregister(uffd, area_dst, page_size);
		return;
	}

	if (pagemap_test_fork(uffd, with_event, false)) {
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
	if (pagemap_test_fork(uffd, with_event, true)) {
		uffd_test_fail("Detected %s uffd-wp bit when RO pin",
			       with_event ? "missing" : "stall");
		goto out;
	}
	uffd_test_pass();
out:
	if (uffd_unregister(uffd, area_dst, page_size))
		err("register failed");
	close(pagemap_fd);
}

static void uffd_wp_fork_pin_test(uffd_test_args_t *args)
{
	uffd_wp_fork_pin_test_common(args, false);
}

static void uffd_wp_fork_pin_with_event_test(uffd_test_args_t *args)
{
	uffd_wp_fork_pin_test_common(args, true);
}

static void check_memory_contents(char *p)
{
	unsigned long i, j;
	uint8_t expected_byte;

	for (i = 0; i < nr_pages; ++i) {
		expected_byte = ~((uint8_t)(i % ((uint8_t)-1)));
		for (j = 0; j < page_size; j++) {
			uint8_t v = *(uint8_t *)(p + (i * page_size) + j);
			if (v != expected_byte)
				err("unexpected page contents");
		}
	}
}

static void uffd_minor_test_common(bool test_collapse, bool test_wp)
{
	unsigned long p;
	pthread_t uffd_mon;
	char c;
	struct uffd_args args = { 0 };

	/*
	 * NOTE: MADV_COLLAPSE is not yet compatible with WP, so testing
	 * both do not make much sense.
	 */
	assert(!(test_collapse && test_wp));

	if (uffd_register(uffd, area_dst_alias, nr_pages * page_size,
			  /* NOTE! MADV_COLLAPSE may not work with uffd-wp */
			  false, test_wp, true))
		err("register failure");

	/*
	 * After registering with UFFD, populate the non-UFFD-registered side of
	 * the shared mapping. This should *not* trigger any UFFD minor faults.
	 */
	for (p = 0; p < nr_pages; ++p)
		memset(area_dst + (p * page_size), p % ((uint8_t)-1),
		       page_size);

	args.apply_wp = test_wp;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	/*
	 * Read each of the pages back using the UFFD-registered mapping. We
	 * expect that the first time we touch a page, it will result in a minor
	 * fault. uffd_poll_thread will resolve the fault by bit-flipping the
	 * page's contents, and then issuing a CONTINUE ioctl.
	 */
	check_memory_contents(area_dst_alias);

	if (write(pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("join() failed");

	if (test_collapse) {
		if (madvise(area_dst_alias, nr_pages * page_size,
			    MADV_COLLAPSE)) {
			/* It's fine to fail for this one... */
			uffd_test_skip("MADV_COLLAPSE failed");
			return;
		}

		uffd_test_ops->check_pmd_mapping(area_dst,
						 nr_pages * page_size /
						 read_pmd_pagesize());
		/*
		 * This won't cause uffd-fault - it purely just makes sure there
		 * was no corruption.
		 */
		check_memory_contents(area_dst_alias);
	}

	if (args.missing_faults != 0 || args.minor_faults != nr_pages)
		uffd_test_fail("stats check error");
	else
		uffd_test_pass();
}

void uffd_minor_test(uffd_test_args_t *args)
{
	uffd_minor_test_common(false, false);
}

void uffd_minor_wp_test(uffd_test_args_t *args)
{
	uffd_minor_test_common(false, true);
}

void uffd_minor_collapse_test(uffd_test_args_t *args)
{
	uffd_minor_test_common(true, false);
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
static int faulting_process(int signal_test, bool wp)
{
	unsigned long nr, i;
	unsigned long long count;
	unsigned long split_nr_pages;
	unsigned long lastnr;
	struct sigaction act;
	volatile unsigned long signalled = 0;

	split_nr_pages = (nr_pages + 1) / 2;

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
		unsigned long offset = nr * page_size;

		if (signal_test) {
			if (sigsetjmp(*sigbuf, 1) != 0) {
				if (steps == 1 && nr == lastnr)
					err("Signal repeated");

				lastnr = nr;
				if (signal_test == 1) {
					if (steps == 1) {
						/* This is a MISSING request */
						steps++;
						if (copy_page(uffd, offset, wp))
							signalled++;
					} else {
						/* This is a WP request */
						assert(steps == 2);
						wp_range(uffd,
							 (__u64)area_dst +
							 offset,
							 page_size, false);
					}
				} else {
					signalled++;
					continue;
				}
			}
		}

		count = *area_count(area_dst, nr);
		if (count != count_verify[nr])
			err("nr %lu memory corruption %llu %llu\n",
			    nr, count, count_verify[nr]);
		/*
		 * Trigger write protection if there is by writing
		 * the same value back.
		 */
		*area_count(area_dst, nr) = count;
	}

	if (signal_test)
		return signalled != split_nr_pages;

	area_dst = mremap(area_dst, nr_pages * page_size,  nr_pages * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, area_src);
	if (area_dst == MAP_FAILED)
		err("mremap");
	/* Reset area_src since we just clobbered it */
	area_src = NULL;

	for (; nr < nr_pages; nr++) {
		count = *area_count(area_dst, nr);
		if (count != count_verify[nr]) {
			err("nr %lu memory corruption %llu %llu\n",
			    nr, count, count_verify[nr]);
		}
		/*
		 * Trigger write protection if there is by writing
		 * the same value back.
		 */
		*area_count(area_dst, nr) = count;
	}

	uffd_test_ops->release_pages(area_dst);

	for (nr = 0; nr < nr_pages; nr++)
		for (i = 0; i < page_size; i++)
			if (*(area_dst + nr * page_size + i) != 0)
				err("page %lu offset %lu is not zero", nr, i);

	return 0;
}

static void uffd_sigbus_test_common(bool wp)
{
	unsigned long userfaults;
	pthread_t uffd_mon;
	pid_t pid;
	int err;
	char c;
	struct uffd_args args = { 0 };

	fcntl(uffd, F_SETFL, uffd_flags | O_NONBLOCK);

	if (uffd_register(uffd, area_dst, nr_pages * page_size,
			  true, wp, false))
		err("register failure");

	if (faulting_process(1, wp))
		err("faulting process failed");

	uffd_test_ops->release_pages(area_dst);

	args.apply_wp = wp;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	pid = fork();
	if (pid < 0)
		err("fork");

	if (!pid)
		exit(faulting_process(2, wp));

	waitpid(pid, &err, 0);
	if (err)
		err("faulting process failed");
	if (write(pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, (void **)&userfaults))
		err("pthread_join()");

	if (userfaults)
		uffd_test_fail("Signal test failed, userfaults: %ld", userfaults);
	else
		uffd_test_pass();
}

static void uffd_sigbus_test(uffd_test_args_t *args)
{
	uffd_sigbus_test_common(false);
}

static void uffd_sigbus_wp_test(uffd_test_args_t *args)
{
	uffd_sigbus_test_common(true);
}

static void uffd_events_test_common(bool wp)
{
	pthread_t uffd_mon;
	pid_t pid;
	int err;
	char c;
	struct uffd_args args = { 0 };

	fcntl(uffd, F_SETFL, uffd_flags | O_NONBLOCK);
	if (uffd_register(uffd, area_dst, nr_pages * page_size,
			  true, wp, false))
		err("register failure");

	args.apply_wp = wp;
	if (pthread_create(&uffd_mon, NULL, uffd_poll_thread, &args))
		err("uffd_poll_thread create");

	pid = fork();
	if (pid < 0)
		err("fork");

	if (!pid)
		exit(faulting_process(0, wp));

	waitpid(pid, &err, 0);
	if (err)
		err("faulting process failed");
	if (write(pipefd[1], &c, sizeof(c)) != sizeof(c))
		err("pipe write");
	if (pthread_join(uffd_mon, NULL))
		err("pthread_join()");

	if (args.missing_faults != nr_pages)
		uffd_test_fail("Fault counts wrong");
	else
		uffd_test_pass();
}

static void uffd_events_test(uffd_test_args_t *args)
{
	uffd_events_test_common(false);
}

static void uffd_events_wp_test(uffd_test_args_t *args)
{
	uffd_events_test_common(true);
}

static void retry_uffdio_zeropage(int ufd,
				  struct uffdio_zeropage *uffdio_zeropage)
{
	uffd_test_ops->alias_mapping(&uffdio_zeropage->range.start,
				     uffdio_zeropage->range.len,
				     0);
	if (ioctl(ufd, UFFDIO_ZEROPAGE, uffdio_zeropage)) {
		if (uffdio_zeropage->zeropage != -EEXIST)
			err("UFFDIO_ZEROPAGE error: %"PRId64,
			    (int64_t)uffdio_zeropage->zeropage);
	} else {
		err("UFFDIO_ZEROPAGE error: %"PRId64,
		    (int64_t)uffdio_zeropage->zeropage);
	}
}

static bool do_uffdio_zeropage(int ufd, bool has_zeropage)
{
	struct uffdio_zeropage uffdio_zeropage = { 0 };
	int ret;
	__s64 res;

	uffdio_zeropage.range.start = (unsigned long) area_dst;
	uffdio_zeropage.range.len = page_size;
	uffdio_zeropage.mode = 0;
	ret = ioctl(ufd, UFFDIO_ZEROPAGE, &uffdio_zeropage);
	res = uffdio_zeropage.zeropage;
	if (ret) {
		/* real retval in ufdio_zeropage.zeropage */
		if (has_zeropage)
			err("UFFDIO_ZEROPAGE error: %"PRId64, (int64_t)res);
		else if (res != -EINVAL)
			err("UFFDIO_ZEROPAGE not -EINVAL");
	} else if (has_zeropage) {
		if (res != page_size)
			err("UFFDIO_ZEROPAGE unexpected size");
		else
			retry_uffdio_zeropage(ufd, &uffdio_zeropage);
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
static void uffd_zeropage_test(uffd_test_args_t *args)
{
	bool has_zeropage;
	int i;

	has_zeropage = uffd_register_detect_zeropage(uffd, area_dst, page_size);
	if (area_dst_alias)
		/* Ignore the retval; we already have it */
		uffd_register_detect_zeropage(uffd, area_dst_alias, page_size);

	if (do_uffdio_zeropage(uffd, has_zeropage))
		for (i = 0; i < page_size; i++)
			if (area_dst[i] != 0)
				err("data non-zero at offset %d\n", i);

	if (uffd_unregister(uffd, area_dst, page_size))
		err("unregister");

	if (area_dst_alias && uffd_unregister(uffd, area_dst_alias, page_size))
		err("unregister");

	uffd_test_pass();
}

/*
 * Test the returned uffdio_register.ioctls with different register modes.
 * Note that _UFFDIO_ZEROPAGE is tested separately in the zeropage test.
 */
static void
do_register_ioctls_test(uffd_test_args_t *args, bool miss, bool wp, bool minor)
{
	uint64_t ioctls = 0, expected = BIT_ULL(_UFFDIO_WAKE);
	mem_type_t *mem_type = args->mem_type;
	int ret;

	ret = uffd_register_with_ioctls(uffd, area_dst, page_size,
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

	if (uffd_unregister(uffd, area_dst, page_size))
		err("unregister");
}

static void uffd_register_ioctls_test(uffd_test_args_t *args)
{
	int miss, wp, minor;

	for (miss = 0; miss <= 1; miss++)
		for (wp = 0; wp <= 1; wp++)
			for (minor = 0; minor <= 1; minor++)
				do_register_ioctls_test(args, miss, wp, minor);

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
		UFFD_FEATURE_EVENT_FORK | UFFD_FEATURE_PAGEFAULT_FLAG_WP,
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

int main(int argc, char *argv[])
{
	int n_tests = sizeof(uffd_tests) / sizeof(uffd_test_case_t);
	int n_mems = sizeof(mem_types) / sizeof(mem_type_t);
	const char *test_filter = NULL;
	bool list_only = false;
	uffd_test_case_t *test;
	mem_type_t *mem_type;
	uffd_test_args_t args;
	char test_name[128];
	const char *errmsg;
	int has_uffd, opt;
	int i, j;

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

	if (!test_filter && !list_only) {
		has_uffd = test_uffd_api(false);
		has_uffd |= test_uffd_api(true);

		if (!has_uffd) {
			printf("Userfaultfd not supported or unprivileged, skip all tests\n");
			exit(KSFT_SKIP);
		}
	}

	for (i = 0; i < n_tests; i++) {
		test = &uffd_tests[i];
		if (test_filter && !strstr(test->name, test_filter))
			continue;
		if (list_only) {
			printf("%s\n", test->name);
			continue;
		}
		for (j = 0; j < n_mems; j++) {
			mem_type = &mem_types[j];
			if (!(test->mem_targets & mem_type->mem_flag))
				continue;
			snprintf(test_name, sizeof(test_name),
				 "%s on %s", test->name, mem_type->name);

			uffd_test_start(test_name);
			if (!uffd_feature_supported(test)) {
				uffd_test_skip("feature missing");
				continue;
			}
			if (uffd_setup_environment(&args, test, mem_type,
						   &errmsg)) {
				uffd_test_skip(errmsg);
				continue;
			}
			test->uffd_fn(&args);
		}
	}

	if (!list_only)
		uffd_test_report();

	return ksft_get_fail_cnt() ? KSFT_FAIL : KSFT_PASS;
}

#else /* __NR_userfaultfd */

#warning "missing __NR_userfaultfd definition"

int main(void)
{
	printf("Skipping %s (missing __NR_userfaultfd)\n", __file__);
	return KSFT_SKIP;
}

#endif /* __NR_userfaultfd */
