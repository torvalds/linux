// SPDX-License-Identifier: GPL-2.0-only
/*
 * Userfaultfd tests util functions
 *
 * Copyright (C) 2015-2023  Red Hat, Inc.
 */

#include "uffd-common.h"

uffd_test_ops_t *uffd_test_ops;
uffd_test_case_ops_t *uffd_test_case_ops;

#define BASE_PMD_ADDR ((void *)(1UL << 30))

/* pthread_mutex_t starts at page offset 0 */
pthread_mutex_t *area_mutex(char *area, unsigned long nr, uffd_global_test_opts_t *gopts)
{
	return (pthread_mutex_t *) (area + nr * gopts->page_size);
}

/*
 * count is placed in the page after pthread_mutex_t naturally aligned
 * to avoid non alignment faults on non-x86 archs.
 */
volatile unsigned long long *area_count(char *area, unsigned long nr,
					uffd_global_test_opts_t *gopts)
{
	return (volatile unsigned long long *)
	       ((unsigned long)(area + nr * gopts->page_size +
	       sizeof(pthread_mutex_t) + sizeof(unsigned long long) - 1) &
	       ~(unsigned long)(sizeof(unsigned long long) - 1));
}

static int uffd_mem_fd_create(off_t mem_size, bool hugetlb)
{
	unsigned int memfd_flags = 0;
	int mem_fd;

	if (hugetlb)
		memfd_flags = MFD_HUGETLB;
	mem_fd = memfd_create("uffd-test", memfd_flags);
	if (mem_fd < 0)
		err("memfd_create");
	if (ftruncate(mem_fd, mem_size))
		err("ftruncate");
	if (fallocate(mem_fd,
		      FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0,
		      mem_size))
		err("fallocate");

	return mem_fd;
}

static void anon_release_pages(uffd_global_test_opts_t *gopts, char *rel_area)
{
	if (madvise(rel_area, gopts->nr_pages * gopts->page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");
}

static int anon_allocate_area(uffd_global_test_opts_t *gopts, void **alloc_area, bool is_src)
{
	*alloc_area = mmap(NULL, gopts->nr_pages * gopts->page_size, PROT_READ | PROT_WRITE,
			   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (*alloc_area == MAP_FAILED) {
		*alloc_area = NULL;
		return -errno;
	}
	return 0;
}

static void noop_alias_mapping(uffd_global_test_opts_t *gopts, __u64 *start,
			       size_t len, unsigned long offset)
{
}

static void hugetlb_release_pages(uffd_global_test_opts_t *gopts, char *rel_area)
{
	if (!gopts->map_shared) {
		if (madvise(rel_area, gopts->nr_pages * gopts->page_size, MADV_DONTNEED))
			err("madvise(MADV_DONTNEED) failed");
	} else {
		if (madvise(rel_area, gopts->nr_pages * gopts->page_size, MADV_REMOVE))
			err("madvise(MADV_REMOVE) failed");
	}
}

static int hugetlb_allocate_area(uffd_global_test_opts_t *gopts, void **alloc_area, bool is_src)
{
	off_t size = gopts->nr_pages * gopts->page_size;
	off_t offset = is_src ? 0 : size;
	void *area_alias = NULL;
	char **alloc_area_alias;
	int mem_fd = uffd_mem_fd_create(size * 2, true);

	*alloc_area = mmap(NULL, size, PROT_READ | PROT_WRITE,
			   (gopts->map_shared ? MAP_SHARED : MAP_PRIVATE) |
			   (is_src ? 0 : MAP_NORESERVE),
			   mem_fd, offset);
	if (*alloc_area == MAP_FAILED) {
		*alloc_area = NULL;
		return -errno;
	}

	if (gopts->map_shared) {
		area_alias = mmap(NULL, size, PROT_READ | PROT_WRITE,
				  MAP_SHARED, mem_fd, offset);
		if (area_alias == MAP_FAILED)
			return -errno;
	}

	if (is_src) {
		alloc_area_alias = &gopts->area_src_alias;
	} else {
		alloc_area_alias = &gopts->area_dst_alias;
	}
	if (area_alias)
		*alloc_area_alias = area_alias;

	close(mem_fd);
	return 0;
}

static void hugetlb_alias_mapping(uffd_global_test_opts_t *gopts, __u64 *start,
				  size_t len, unsigned long offset)
{
	if (!gopts->map_shared)
		return;

	*start = (unsigned long) gopts->area_dst_alias + offset;
}

static void shmem_release_pages(uffd_global_test_opts_t *gopts, char *rel_area)
{
	if (madvise(rel_area, gopts->nr_pages * gopts->page_size, MADV_REMOVE))
		err("madvise(MADV_REMOVE) failed");
}

static int shmem_allocate_area(uffd_global_test_opts_t *gopts, void **alloc_area, bool is_src)
{
	void *area_alias = NULL;
	size_t bytes = gopts->nr_pages * gopts->page_size, hpage_size = read_pmd_pagesize();
	unsigned long offset = is_src ? 0 : bytes;
	char *p = NULL, *p_alias = NULL;
	int mem_fd = uffd_mem_fd_create(bytes * 2, false);

	/* TODO: clean this up.  Use a static addr is ugly */
	p = BASE_PMD_ADDR;
	if (!is_src)
		/* src map + alias + interleaved hpages */
		p += 2 * (bytes + hpage_size);
	p_alias = p;
	p_alias += bytes;
	p_alias += hpage_size;  /* Prevent src/dst VMA merge */

	*alloc_area = mmap(p, bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
			   mem_fd, offset);
	if (*alloc_area == MAP_FAILED) {
		*alloc_area = NULL;
		return -errno;
	}
	if (*alloc_area != p)
		err("mmap of memfd failed at %p", p);

	area_alias = mmap(p_alias, bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
			  mem_fd, offset);
	if (area_alias == MAP_FAILED) {
		munmap(*alloc_area, bytes);
		*alloc_area = NULL;
		return -errno;
	}
	if (area_alias != p_alias)
		err("mmap of anonymous memory failed at %p", p_alias);

	if (is_src)
		gopts->area_src_alias = area_alias;
	else
		gopts->area_dst_alias = area_alias;

	close(mem_fd);
	return 0;
}

static void shmem_alias_mapping(uffd_global_test_opts_t *gopts, __u64 *start,
				size_t len, unsigned long offset)
{
	*start = (unsigned long)gopts->area_dst_alias + offset;
}

static void shmem_check_pmd_mapping(uffd_global_test_opts_t *gopts, void *p, int expect_nr_hpages)
{
	if (!check_huge_shmem(gopts->area_dst_alias, expect_nr_hpages,
			      read_pmd_pagesize()))
		err("Did not find expected %d number of hugepages",
		    expect_nr_hpages);
}

struct uffd_test_ops anon_uffd_test_ops = {
	.allocate_area = anon_allocate_area,
	.release_pages = anon_release_pages,
	.alias_mapping = noop_alias_mapping,
	.check_pmd_mapping = NULL,
};

struct uffd_test_ops shmem_uffd_test_ops = {
	.allocate_area = shmem_allocate_area,
	.release_pages = shmem_release_pages,
	.alias_mapping = shmem_alias_mapping,
	.check_pmd_mapping = shmem_check_pmd_mapping,
};

struct uffd_test_ops hugetlb_uffd_test_ops = {
	.allocate_area = hugetlb_allocate_area,
	.release_pages = hugetlb_release_pages,
	.alias_mapping = hugetlb_alias_mapping,
	.check_pmd_mapping = NULL,
};

void uffd_stats_report(struct uffd_args *args, int n_cpus)
{
	int i;
	unsigned long long miss_total = 0, wp_total = 0, minor_total = 0;

	for (i = 0; i < n_cpus; i++) {
		miss_total += args[i].missing_faults;
		wp_total += args[i].wp_faults;
		minor_total += args[i].minor_faults;
	}

	printf("userfaults: ");
	if (miss_total) {
		printf("%llu missing (", miss_total);
		for (i = 0; i < n_cpus; i++)
			printf("%lu+", args[i].missing_faults);
		printf("\b) ");
	}
	if (wp_total) {
		printf("%llu wp (", wp_total);
		for (i = 0; i < n_cpus; i++)
			printf("%lu+", args[i].wp_faults);
		printf("\b) ");
	}
	if (minor_total) {
		printf("%llu minor (", minor_total);
		for (i = 0; i < n_cpus; i++)
			printf("%lu+", args[i].minor_faults);
		printf("\b)");
	}
	printf("\n");
}

int userfaultfd_open(uffd_global_test_opts_t *gopts, uint64_t *features)
{
	struct uffdio_api uffdio_api;

	gopts->uffd = uffd_open(UFFD_FLAGS);
	if (gopts->uffd < 0)
		return -1;
	gopts->uffd_flags = fcntl(gopts->uffd, F_GETFD, NULL);

	uffdio_api.api = UFFD_API;
	uffdio_api.features = *features;
	if (ioctl(gopts->uffd, UFFDIO_API, &uffdio_api))
		/* Probably lack of CAP_PTRACE? */
		return -1;
	if (uffdio_api.api != UFFD_API)
		err("UFFDIO_API error: %" PRIu64, (uint64_t)uffdio_api.api);

	*features = uffdio_api.features;
	return 0;
}

static inline void munmap_area(uffd_global_test_opts_t *gopts, void **area)
{
	if (*area)
		if (munmap(*area, gopts->nr_pages * gopts->page_size))
			err("munmap");

	*area = NULL;
}

void uffd_test_ctx_clear(uffd_global_test_opts_t *gopts)
{
	size_t i;

	if (gopts->pipefd) {
		for (i = 0; i < gopts->nr_parallel * 2; ++i) {
			if (close(gopts->pipefd[i]))
				err("close pipefd");
		}
		free(gopts->pipefd);
		gopts->pipefd = NULL;
	}

	if (gopts->count_verify) {
		free(gopts->count_verify);
		gopts->count_verify = NULL;
	}

	if (gopts->uffd != -1) {
		if (close(gopts->uffd))
			err("close uffd");
		gopts->uffd = -1;
	}

	munmap_area(gopts, (void **)&gopts->area_src);
	munmap_area(gopts, (void **)&gopts->area_src_alias);
	munmap_area(gopts, (void **)&gopts->area_dst);
	munmap_area(gopts, (void **)&gopts->area_dst_alias);
	munmap_area(gopts, (void **)&gopts->area_remap);
}

int uffd_test_ctx_init(uffd_global_test_opts_t *gopts, uint64_t features, const char **errmsg)
{
	unsigned long nr, cpu;
	int ret;

	gopts->area_src_alias = NULL;
	gopts->area_dst_alias = NULL;
	gopts->area_remap = NULL;

	if (uffd_test_case_ops && uffd_test_case_ops->pre_alloc) {
		ret = uffd_test_case_ops->pre_alloc(gopts, errmsg);
		if (ret)
			return ret;
	}

	ret = uffd_test_ops->allocate_area(gopts, (void **) &gopts->area_src, true);
	ret |= uffd_test_ops->allocate_area(gopts, (void **) &gopts->area_dst, false);
	if (ret) {
		if (errmsg)
			*errmsg = "memory allocation failed";
		return ret;
	}

	if (uffd_test_case_ops && uffd_test_case_ops->post_alloc) {
		ret = uffd_test_case_ops->post_alloc(gopts, errmsg);
		if (ret)
			return ret;
	}

	ret = userfaultfd_open(gopts, &features);
	if (ret) {
		if (errmsg)
			*errmsg = "possible lack of privilege";
		return ret;
	}

	gopts->count_verify = malloc(gopts->nr_pages * sizeof(unsigned long long));
	if (!gopts->count_verify)
		err("count_verify");

	for (nr = 0; nr < gopts->nr_pages; nr++) {
		*area_mutex(gopts->area_src, nr, gopts) =
			(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
		gopts->count_verify[nr] = *area_count(gopts->area_src, nr, gopts) = 1;
		/*
		 * In the transition between 255 to 256, powerpc will
		 * read out of order in my_bcmp and see both bytes as
		 * zero, so leave a placeholder below always non-zero
		 * after the count, to avoid my_bcmp to trigger false
		 * positives.
		 */
		*(area_count(gopts->area_src, nr, gopts) + 1) = 1;
	}

	/*
	 * After initialization of area_src, we must explicitly release pages
	 * for area_dst to make sure it's fully empty.  Otherwise we could have
	 * some area_dst pages be erroneously initialized with zero pages,
	 * hence we could hit memory corruption later in the test.
	 *
	 * One example is when THP is globally enabled, above allocate_area()
	 * calls could have the two areas merged into a single VMA (as they
	 * will have the same VMA flags so they're mergeable).  When we
	 * initialize the area_src above, it's possible that some part of
	 * area_dst could have been faulted in via one huge THP that will be
	 * shared between area_src and area_dst.  It could cause some of the
	 * area_dst won't be trapped by missing userfaults.
	 *
	 * This release_pages() will guarantee even if that happened, we'll
	 * proactively split the thp and drop any accidentally initialized
	 * pages within area_dst.
	 */
	uffd_test_ops->release_pages(gopts, gopts->area_dst);

	gopts->pipefd = malloc(sizeof(int) * gopts->nr_parallel * 2);
	if (!gopts->pipefd)
		err("pipefd");
	for (cpu = 0; cpu < gopts->nr_parallel; cpu++)
		if (pipe2(&gopts->pipefd[cpu * 2], O_CLOEXEC | O_NONBLOCK))
			err("pipe");

	return 0;
}

void wp_range(int ufd, __u64 start, __u64 len, bool wp)
{
	struct uffdio_writeprotect prms;

	/* Write protection page faults */
	prms.range.start = start;
	prms.range.len = len;
	/* Undo write-protect, do wakeup after that */
	prms.mode = wp ? UFFDIO_WRITEPROTECT_MODE_WP : 0;

	if (ioctl(ufd, UFFDIO_WRITEPROTECT, &prms))
		err("clear WP failed: address=0x%"PRIx64, (uint64_t)start);
}

static void continue_range(int ufd, __u64 start, __u64 len, bool wp)
{
	struct uffdio_continue req;
	int ret;

	req.range.start = start;
	req.range.len = len;
	req.mode = 0;
	if (wp)
		req.mode |= UFFDIO_CONTINUE_MODE_WP;

	if (ioctl(ufd, UFFDIO_CONTINUE, &req))
		err("UFFDIO_CONTINUE failed for address 0x%" PRIx64,
		    (uint64_t)start);

	/*
	 * Error handling within the kernel for continue is subtly different
	 * from copy or zeropage, so it may be a source of bugs. Trigger an
	 * error (-EEXIST) on purpose, to verify doing so doesn't cause a BUG.
	 */
	req.mapped = 0;
	ret = ioctl(ufd, UFFDIO_CONTINUE, &req);
	if (ret >= 0 || req.mapped != -EEXIST)
		err("failed to exercise UFFDIO_CONTINUE error handling, ret=%d, mapped=%" PRId64,
		    ret, (int64_t) req.mapped);
}

int uffd_read_msg(uffd_global_test_opts_t *gopts, struct uffd_msg *msg)
{
	int ret = read(gopts->uffd, msg, sizeof(*msg));

	if (ret != sizeof(*msg)) {
		if (ret < 0) {
			if (errno == EAGAIN || errno == EINTR)
				return 1;
			err("blocking read error");
		} else {
			err("short read");
		}
	}

	return 0;
}

void uffd_handle_page_fault(uffd_global_test_opts_t *gopts, struct uffd_msg *msg,
			    struct uffd_args *args)
{
	unsigned long offset;

	if (msg->event != UFFD_EVENT_PAGEFAULT)
		err("unexpected msg event %u", msg->event);

	if (msg->arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
		/* Write protect page faults */
		wp_range(gopts->uffd, msg->arg.pagefault.address, gopts->page_size, false);
		args->wp_faults++;
	} else if (msg->arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_MINOR) {
		uint8_t *area;
		int b;

		/*
		 * Minor page faults
		 *
		 * To prove we can modify the original range for testing
		 * purposes, we're going to bit flip this range before
		 * continuing.
		 *
		 * Note that this requires all minor page fault tests operate on
		 * area_dst (non-UFFD-registered) and area_dst_alias
		 * (UFFD-registered).
		 */

		area = (uint8_t *)(gopts->area_dst +
		       ((char *)msg->arg.pagefault.address -
		       gopts->area_dst_alias));
		for (b = 0; b < gopts->page_size; ++b)
			area[b] = ~area[b];
		continue_range(gopts->uffd, msg->arg.pagefault.address, gopts->page_size,
			       args->apply_wp);
		args->minor_faults++;
	} else {
		/*
		 * Missing page faults.
		 *
		 * Here we force a write check for each of the missing mode
		 * faults.  It's guaranteed because the only threads that
		 * will trigger uffd faults are the locking threads, and
		 * their first instruction to touch the missing page will
		 * always be pthread_mutex_lock().
		 *
		 * Note that here we relied on an NPTL glibc impl detail to
		 * always read the lock type at the entry of the lock op
		 * (pthread_mutex_t.__data.__type, offset 0x10) before
		 * doing any locking operations to guarantee that.  It's
		 * actually not good to rely on this impl detail because
		 * logically a pthread-compatible lib can implement the
		 * locks without types and we can fail when linking with
		 * them.  However since we used to find bugs with this
		 * strict check we still keep it around.  Hopefully this
		 * could be a good hint when it fails again.  If one day
		 * it'll break on some other impl of glibc we'll revisit.
		 */
		if (msg->arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE)
			err("unexpected write fault");

		offset = (char *)(unsigned long)msg->arg.pagefault.address - gopts->area_dst;
		offset &= ~(gopts->page_size-1);

		if (copy_page(gopts, offset, args->apply_wp))
			args->missing_faults++;
	}
}

void *uffd_poll_thread(void *arg)
{
	struct uffd_args *args = (struct uffd_args *)arg;
	uffd_global_test_opts_t *gopts = args->gopts;
	unsigned long cpu = args->cpu;
	struct pollfd pollfd[2];
	struct uffd_msg msg;
	struct uffdio_register uffd_reg;
	int ret;
	char tmp_chr;

	if (!args->handle_fault)
		args->handle_fault = uffd_handle_page_fault;

	pollfd[0].fd = gopts->uffd;
	pollfd[0].events = POLLIN;
	pollfd[1].fd = gopts->pipefd[cpu*2];
	pollfd[1].events = POLLIN;

	gopts->ready_for_fork = true;

	for (;;) {
		ret = poll(pollfd, 2, -1);
		if (ret <= 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			err("poll error: %d", ret);
		}
		if (pollfd[1].revents) {
			if (!(pollfd[1].revents & POLLIN))
				err("pollfd[1].revents %d", pollfd[1].revents);
			if (read(pollfd[1].fd, &tmp_chr, 1) != 1)
				err("read pipefd error");
			break;
		}
		if (!(pollfd[0].revents & POLLIN))
			err("pollfd[0].revents %d", pollfd[0].revents);
		if (uffd_read_msg(gopts, &msg))
			continue;
		switch (msg.event) {
		default:
			err("unexpected msg event %u\n", msg.event);
			break;
		case UFFD_EVENT_PAGEFAULT:
			args->handle_fault(gopts, &msg, args);
			break;
		case UFFD_EVENT_FORK:
			close(gopts->uffd);
			gopts->uffd = msg.arg.fork.ufd;
			pollfd[0].fd = gopts->uffd;
			break;
		case UFFD_EVENT_REMOVE:
			uffd_reg.range.start = msg.arg.remove.start;
			uffd_reg.range.len = msg.arg.remove.end -
				msg.arg.remove.start;
			if (ioctl(gopts->uffd, UFFDIO_UNREGISTER, &uffd_reg.range))
				err("remove failure");
			break;
		case UFFD_EVENT_REMAP:
			gopts->area_remap = gopts->area_dst;  /* save for later unmap */
			gopts->area_dst = (char *)(unsigned long)msg.arg.remap.to;
			break;
		}
	}

	return NULL;
}

static void retry_copy_page(uffd_global_test_opts_t *gopts, struct uffdio_copy *uffdio_copy,
			    unsigned long offset)
{
	uffd_test_ops->alias_mapping(gopts,
				     &uffdio_copy->dst,
				     uffdio_copy->len,
				     offset);
	if (ioctl(gopts->uffd, UFFDIO_COPY, uffdio_copy)) {
		/* real retval in ufdio_copy.copy */
		if (uffdio_copy->copy != -EEXIST)
			err("UFFDIO_COPY retry error: %"PRId64,
			(int64_t)uffdio_copy->copy);
	} else {
		err("UFFDIO_COPY retry unexpected: %"PRId64,
		    (int64_t)uffdio_copy->copy);
	}
}

static void wake_range(int ufd, unsigned long addr, unsigned long len)
{
	struct uffdio_range uffdio_wake;

	uffdio_wake.start = addr;
	uffdio_wake.len = len;

	if (ioctl(ufd, UFFDIO_WAKE, &uffdio_wake))
		fprintf(stderr, "error waking %lu\n",
			addr), exit(1);
}

int __copy_page(uffd_global_test_opts_t *gopts, unsigned long offset, bool retry, bool wp)
{
	struct uffdio_copy uffdio_copy;

	if (offset >= gopts->nr_pages * gopts->page_size)
		err("unexpected offset %lu\n", offset);
	uffdio_copy.dst = (unsigned long) gopts->area_dst + offset;
	uffdio_copy.src = (unsigned long) gopts->area_src + offset;
	uffdio_copy.len = gopts->page_size;
	if (wp)
		uffdio_copy.mode = UFFDIO_COPY_MODE_WP;
	else
		uffdio_copy.mode = 0;
	uffdio_copy.copy = 0;
	if (ioctl(gopts->uffd, UFFDIO_COPY, &uffdio_copy)) {
		/* real retval in ufdio_copy.copy */
		if (uffdio_copy.copy != -EEXIST)
			err("UFFDIO_COPY error: %"PRId64,
			    (int64_t)uffdio_copy.copy);
		wake_range(gopts->uffd, uffdio_copy.dst, gopts->page_size);
	} else if (uffdio_copy.copy != gopts->page_size) {
		err("UFFDIO_COPY error: %"PRId64, (int64_t)uffdio_copy.copy);
	} else {
		if (gopts->test_uffdio_copy_eexist && retry) {
			gopts->test_uffdio_copy_eexist = false;
			retry_copy_page(gopts, &uffdio_copy, offset);
		}
		return 1;
	}
	return 0;
}

int copy_page(uffd_global_test_opts_t *gopts, unsigned long offset, bool wp)
{
	return __copy_page(gopts, offset, false, wp);
}

int move_page(uffd_global_test_opts_t *gopts, unsigned long offset, unsigned long len)
{
	struct uffdio_move uffdio_move;

	if (offset + len > gopts->nr_pages * gopts->page_size)
		err("unexpected offset %lu and length %lu\n", offset, len);
	uffdio_move.dst = (unsigned long) gopts->area_dst + offset;
	uffdio_move.src = (unsigned long) gopts->area_src + offset;
	uffdio_move.len = len;
	uffdio_move.mode = UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES;
	uffdio_move.move = 0;
	if (ioctl(gopts->uffd, UFFDIO_MOVE, &uffdio_move)) {
		/* real retval in uffdio_move.move */
		if (uffdio_move.move != -EEXIST)
			err("UFFDIO_MOVE error: %"PRId64,
			    (int64_t)uffdio_move.move);
		wake_range(gopts->uffd, uffdio_move.dst, len);
	} else if (uffdio_move.move != len) {
		err("UFFDIO_MOVE error: %"PRId64, (int64_t)uffdio_move.move);
	} else
		return 1;
	return 0;
}

int uffd_open_dev(unsigned int flags)
{
	int fd, uffd;

	fd = open("/dev/userfaultfd", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	uffd = ioctl(fd, USERFAULTFD_IOC_NEW, flags);
	close(fd);

	return uffd;
}

int uffd_open_sys(unsigned int flags)
{
#ifdef __NR_userfaultfd
	return syscall(__NR_userfaultfd, flags);
#else
	return -1;
#endif
}

int uffd_open(unsigned int flags)
{
	int uffd = uffd_open_sys(flags);

	if (uffd < 0)
		uffd = uffd_open_dev(flags);

	return uffd;
}

int uffd_get_features(uint64_t *features)
{
	struct uffdio_api uffdio_api = { .api = UFFD_API, .features = 0 };
	/*
	 * This should by default work in most kernels; the feature list
	 * will be the same no matter what we pass in here.
	 */
	int fd = uffd_open(UFFD_USER_MODE_ONLY);

	if (fd < 0)
		/* Maybe the kernel is older than user-only mode? */
		fd = uffd_open(0);

	if (fd < 0)
		return fd;

	if (ioctl(fd, UFFDIO_API, &uffdio_api)) {
		close(fd);
		return -errno;
	}

	*features = uffdio_api.features;
	close(fd);

	return 0;
}
