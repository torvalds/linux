// SPDX-License-Identifier: GPL-2.0-only
/*
 * Userfaultfd tests util functions
 *
 * Copyright (C) 2015-2023  Red Hat, Inc.
 */

#include "uffd-common.h"

#define BASE_PMD_ADDR ((void *)(1UL << 30))

volatile bool test_uffdio_copy_eexist = true;
unsigned long nr_cpus, nr_pages, nr_pages_per_cpu, page_size, hpage_size;
char *area_src, *area_src_alias, *area_dst, *area_dst_alias, *area_remap;
int mem_fd, uffd = -1, uffd_flags, finished, *pipefd, test_type;
bool map_shared, test_collapse, test_dev_userfaultfd;
bool test_uffdio_wp = true, test_uffdio_minor = false;
unsigned long long *count_verify;
uffd_test_ops_t *uffd_test_ops;

static void anon_release_pages(char *rel_area)
{
	if (madvise(rel_area, nr_pages * page_size, MADV_DONTNEED))
		err("madvise(MADV_DONTNEED) failed");
}

static void anon_allocate_area(void **alloc_area, bool is_src)
{
	*alloc_area = mmap(NULL, nr_pages * page_size, PROT_READ | PROT_WRITE,
			   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
}

static void noop_alias_mapping(__u64 *start, size_t len, unsigned long offset)
{
}

static void hugetlb_release_pages(char *rel_area)
{
	if (!map_shared) {
		if (madvise(rel_area, nr_pages * page_size, MADV_DONTNEED))
			err("madvise(MADV_DONTNEED) failed");
	} else {
		if (madvise(rel_area, nr_pages * page_size, MADV_REMOVE))
			err("madvise(MADV_REMOVE) failed");
	}
}

static void hugetlb_allocate_area(void **alloc_area, bool is_src)
{
	off_t size = nr_pages * page_size;
	off_t offset = is_src ? 0 : size;
	void *area_alias = NULL;
	char **alloc_area_alias;

	*alloc_area = mmap(NULL, size, PROT_READ | PROT_WRITE,
			   (map_shared ? MAP_SHARED : MAP_PRIVATE) |
			   (is_src ? 0 : MAP_NORESERVE),
			   mem_fd, offset);
	if (*alloc_area == MAP_FAILED)
		err("mmap of hugetlbfs file failed");

	if (map_shared) {
		area_alias = mmap(NULL, size, PROT_READ | PROT_WRITE,
				  MAP_SHARED, mem_fd, offset);
		if (area_alias == MAP_FAILED)
			err("mmap of hugetlb file alias failed");
	}

	if (is_src) {
		alloc_area_alias = &area_src_alias;
	} else {
		alloc_area_alias = &area_dst_alias;
	}
	if (area_alias)
		*alloc_area_alias = area_alias;
}

static void hugetlb_alias_mapping(__u64 *start, size_t len, unsigned long offset)
{
	if (!map_shared)
		return;

	*start = (unsigned long) area_dst_alias + offset;
}

static void shmem_release_pages(char *rel_area)
{
	if (madvise(rel_area, nr_pages * page_size, MADV_REMOVE))
		err("madvise(MADV_REMOVE) failed");
}

static void shmem_allocate_area(void **alloc_area, bool is_src)
{
	void *area_alias = NULL;
	size_t bytes = nr_pages * page_size;
	unsigned long offset = is_src ? 0 : bytes;
	char *p = NULL, *p_alias = NULL;

	if (test_collapse) {
		p = BASE_PMD_ADDR;
		if (!is_src)
			/* src map + alias + interleaved hpages */
			p += 2 * (bytes + hpage_size);
		p_alias = p;
		p_alias += bytes;
		p_alias += hpage_size;  /* Prevent src/dst VMA merge */
	}

	*alloc_area = mmap(p, bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
			   mem_fd, offset);
	if (*alloc_area == MAP_FAILED)
		err("mmap of memfd failed");
	if (test_collapse && *alloc_area != p)
		err("mmap of memfd failed at %p", p);

	area_alias = mmap(p_alias, bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
			  mem_fd, offset);
	if (area_alias == MAP_FAILED)
		err("mmap of memfd alias failed");
	if (test_collapse && area_alias != p_alias)
		err("mmap of anonymous memory failed at %p", p_alias);

	if (is_src)
		area_src_alias = area_alias;
	else
		area_dst_alias = area_alias;
}

static void shmem_alias_mapping(__u64 *start, size_t len, unsigned long offset)
{
	*start = (unsigned long)area_dst_alias + offset;
}

static void shmem_check_pmd_mapping(void *p, int expect_nr_hpages)
{
	if (!check_huge_shmem(area_dst_alias, expect_nr_hpages, hpage_size))
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

void uffd_stats_report(struct uffd_stats *stats, int n_cpus)
{
	int i;
	unsigned long long miss_total = 0, wp_total = 0, minor_total = 0;

	for (i = 0; i < n_cpus; i++) {
		miss_total += stats[i].missing_faults;
		wp_total += stats[i].wp_faults;
		minor_total += stats[i].minor_faults;
	}

	printf("userfaults: ");
	if (miss_total) {
		printf("%llu missing (", miss_total);
		for (i = 0; i < n_cpus; i++)
			printf("%lu+", stats[i].missing_faults);
		printf("\b) ");
	}
	if (wp_total) {
		printf("%llu wp (", wp_total);
		for (i = 0; i < n_cpus; i++)
			printf("%lu+", stats[i].wp_faults);
		printf("\b) ");
	}
	if (minor_total) {
		printf("%llu minor (", minor_total);
		for (i = 0; i < n_cpus; i++)
			printf("%lu+", stats[i].minor_faults);
		printf("\b)");
	}
	printf("\n");
}

void userfaultfd_open(uint64_t *features)
{
	struct uffdio_api uffdio_api;

	if (test_dev_userfaultfd)
		uffd = uffd_open_dev(UFFD_FLAGS);
	else
		uffd = uffd_open_sys(UFFD_FLAGS);
	if (uffd < 0)
		err("uffd open failed (dev=%d)", test_dev_userfaultfd);
	uffd_flags = fcntl(uffd, F_GETFD, NULL);

	uffdio_api.api = UFFD_API;
	uffdio_api.features = *features;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api))
		err("UFFDIO_API failed.\nPlease make sure to "
		    "run with either root or ptrace capability.");
	if (uffdio_api.api != UFFD_API)
		err("UFFDIO_API error: %" PRIu64, (uint64_t)uffdio_api.api);

	*features = uffdio_api.features;
}

static inline void munmap_area(void **area)
{
	if (*area)
		if (munmap(*area, nr_pages * page_size))
			err("munmap");

	*area = NULL;
}

static void uffd_test_ctx_clear(void)
{
	size_t i;

	if (pipefd) {
		for (i = 0; i < nr_cpus * 2; ++i) {
			if (close(pipefd[i]))
				err("close pipefd");
		}
		free(pipefd);
		pipefd = NULL;
	}

	if (count_verify) {
		free(count_verify);
		count_verify = NULL;
	}

	if (uffd != -1) {
		if (close(uffd))
			err("close uffd");
		uffd = -1;
	}

	munmap_area((void **)&area_src);
	munmap_area((void **)&area_src_alias);
	munmap_area((void **)&area_dst);
	munmap_area((void **)&area_dst_alias);
	munmap_area((void **)&area_remap);
}

void uffd_test_ctx_init(uint64_t features)
{
	unsigned long nr, cpu;

	uffd_test_ctx_clear();

	uffd_test_ops->allocate_area((void **)&area_src, true);
	uffd_test_ops->allocate_area((void **)&area_dst, false);

	userfaultfd_open(&features);

	count_verify = malloc(nr_pages * sizeof(unsigned long long));
	if (!count_verify)
		err("count_verify");

	for (nr = 0; nr < nr_pages; nr++) {
		*area_mutex(area_src, nr) =
			(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
		count_verify[nr] = *area_count(area_src, nr) = 1;
		/*
		 * In the transition between 255 to 256, powerpc will
		 * read out of order in my_bcmp and see both bytes as
		 * zero, so leave a placeholder below always non-zero
		 * after the count, to avoid my_bcmp to trigger false
		 * positives.
		 */
		*(area_count(area_src, nr) + 1) = 1;
	}

	/*
	 * After initialization of area_src, we must explicitly release pages
	 * for area_dst to make sure it's fully empty.  Otherwise we could have
	 * some area_dst pages be errornously initialized with zero pages,
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
	uffd_test_ops->release_pages(area_dst);

	pipefd = malloc(sizeof(int) * nr_cpus * 2);
	if (!pipefd)
		err("pipefd");
	for (cpu = 0; cpu < nr_cpus; cpu++)
		if (pipe2(&pipefd[cpu * 2], O_CLOEXEC | O_NONBLOCK))
			err("pipe");
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

static void continue_range(int ufd, __u64 start, __u64 len)
{
	struct uffdio_continue req;
	int ret;

	req.range.start = start;
	req.range.len = len;
	req.mode = 0;
	if (test_uffdio_wp)
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

int uffd_read_msg(int ufd, struct uffd_msg *msg)
{
	int ret = read(uffd, msg, sizeof(*msg));

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

void uffd_handle_page_fault(struct uffd_msg *msg, struct uffd_stats *stats)
{
	unsigned long offset;

	if (msg->event != UFFD_EVENT_PAGEFAULT)
		err("unexpected msg event %u", msg->event);

	if (msg->arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
		/* Write protect page faults */
		wp_range(uffd, msg->arg.pagefault.address, page_size, false);
		stats->wp_faults++;
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

		area = (uint8_t *)(area_dst +
				   ((char *)msg->arg.pagefault.address -
				    area_dst_alias));
		for (b = 0; b < page_size; ++b)
			area[b] = ~area[b];
		continue_range(uffd, msg->arg.pagefault.address, page_size);
		stats->minor_faults++;
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

		offset = (char *)(unsigned long)msg->arg.pagefault.address - area_dst;
		offset &= ~(page_size-1);

		if (copy_page(uffd, offset))
			stats->missing_faults++;
	}
}

void *uffd_poll_thread(void *arg)
{
	struct uffd_stats *stats = (struct uffd_stats *)arg;
	unsigned long cpu = stats->cpu;
	struct pollfd pollfd[2];
	struct uffd_msg msg;
	struct uffdio_register uffd_reg;
	int ret;
	char tmp_chr;

	pollfd[0].fd = uffd;
	pollfd[0].events = POLLIN;
	pollfd[1].fd = pipefd[cpu*2];
	pollfd[1].events = POLLIN;

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
		if (uffd_read_msg(uffd, &msg))
			continue;
		switch (msg.event) {
		default:
			err("unexpected msg event %u\n", msg.event);
			break;
		case UFFD_EVENT_PAGEFAULT:
			uffd_handle_page_fault(&msg, stats);
			break;
		case UFFD_EVENT_FORK:
			close(uffd);
			uffd = msg.arg.fork.ufd;
			pollfd[0].fd = uffd;
			break;
		case UFFD_EVENT_REMOVE:
			uffd_reg.range.start = msg.arg.remove.start;
			uffd_reg.range.len = msg.arg.remove.end -
				msg.arg.remove.start;
			if (ioctl(uffd, UFFDIO_UNREGISTER, &uffd_reg.range))
				err("remove failure");
			break;
		case UFFD_EVENT_REMAP:
			area_remap = area_dst;  /* save for later unmap */
			area_dst = (char *)(unsigned long)msg.arg.remap.to;
			break;
		}
	}

	return NULL;
}

static void retry_copy_page(int ufd, struct uffdio_copy *uffdio_copy,
			    unsigned long offset)
{
	uffd_test_ops->alias_mapping(&uffdio_copy->dst,
				     uffdio_copy->len,
				     offset);
	if (ioctl(ufd, UFFDIO_COPY, uffdio_copy)) {
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

int __copy_page(int ufd, unsigned long offset, bool retry)
{
	struct uffdio_copy uffdio_copy;

	if (offset >= nr_pages * page_size)
		err("unexpected offset %lu\n", offset);
	uffdio_copy.dst = (unsigned long) area_dst + offset;
	uffdio_copy.src = (unsigned long) area_src + offset;
	uffdio_copy.len = page_size;
	if (test_uffdio_wp)
		uffdio_copy.mode = UFFDIO_COPY_MODE_WP;
	else
		uffdio_copy.mode = 0;
	uffdio_copy.copy = 0;
	if (ioctl(ufd, UFFDIO_COPY, &uffdio_copy)) {
		/* real retval in ufdio_copy.copy */
		if (uffdio_copy.copy != -EEXIST)
			err("UFFDIO_COPY error: %"PRId64,
			    (int64_t)uffdio_copy.copy);
		wake_range(ufd, uffdio_copy.dst, page_size);
	} else if (uffdio_copy.copy != page_size) {
		err("UFFDIO_COPY error: %"PRId64, (int64_t)uffdio_copy.copy);
	} else {
		if (test_uffdio_copy_eexist && retry) {
			test_uffdio_copy_eexist = false;
			retry_copy_page(ufd, &uffdio_copy, offset);
		}
		return 1;
	}
	return 0;
}

int copy_page(int ufd, unsigned long offset)
{
	return __copy_page(ufd, offset, false);
}
