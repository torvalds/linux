#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>

#include "linux/magic.h"

#include "vm_util.h"

#define BASE_ADDR ((void *)(1UL << 30))
static unsigned long hpage_pmd_size;
static int hpage_pmd_order;
static unsigned long page_size;
static int hpage_pmd_nr;
static int anon_order;
static int collapse_order;
static bool collapse_order_set;
static int collapse_orders[NR_ORDERS];
static int nr_collapse_orders;
static int pagemap_fd = -1;
static int kpageflags_fd = -1;

#define PID_SMAPS "/proc/self/smaps"
#define TEST_FILE "collapse_test_file"

#define MAX_LINE_LENGTH 500

enum vma_type {
	VMA_ANON,
	VMA_FILE,
	VMA_SHMEM,
};

enum file_setup_ops {
	FILE_SETUP_READ_ONLY_FS,
	FILE_SETUP_READ_WRITE_FS_READ_DATA,
	FILE_SETUP_READ_WRITE_FS_WRITE_DATA,
};

struct mem_ops {
	void *(*setup_area)(int nr_hpages);
	void (*cleanup_area)(void *p, unsigned long size);
	void (*fault)(void *p, unsigned long start, unsigned long end);
	bool (*check_huge)(void *addr, size_t len, int nr_hpages, unsigned long hpage_size);
	const char *name;
};

static struct mem_ops *read_only_file_ops;
static struct mem_ops *read_write_file_read_ops;
static struct mem_ops *read_write_file_write_ops;
static struct mem_ops *anon_ops;
static struct mem_ops *shmem_ops;

struct collapse_context {
	void (*collapse)(const char *msg, char *p, int nr_hpages,
			 struct mem_ops *ops, bool expect);
	bool enforce_pte_scan_limits;
	const char *name;
};

static struct collapse_context *khugepaged_context;
static struct collapse_context *mthp_khugepaged_context;
static struct collapse_context *madvise_context;

struct file_info {
	const char *dir;
	char path[PATH_MAX];
	enum vma_type type;
	int fd;
	char dev_queue_read_ahead_path[PATH_MAX];
};

static struct file_info finfo;
static int exit_status;

static void success(const char *msg)
{
	printf(" \e[32m%s\e[0m\n", msg);
	exit_status = KSFT_PASS;
}

static void fail(const char *msg)
{
	printf(" \e[31m%s\e[0m\n", msg);
	exit_status = KSFT_FAIL;
}

static void skip(const char *msg)
{
	printf(" \e[33m%s\e[0m\n", msg);
	exit_status = KSFT_SKIP;
}

static void save_settings(void)
{
	ksft_print_msg("Save THP and khugepaged settings...");
	if ((read_only_file_ops || read_write_file_read_ops ||
	     read_write_file_write_ops) &&
	    finfo.type == VMA_FILE)
		thp_set_read_ahead_path(finfo.dev_queue_read_ahead_path);
	thp_save_settings();

	success("OK");
}

static void get_finfo(const char *dir)
{
	struct stat path_stat;
	struct statfs fs;
	char buf[1 << 10];
	char path[PATH_MAX];
	char *str, *end;
	int ret;

	finfo.dir = dir;
	if (stat(finfo.dir, &path_stat))
		ksft_exit_fail_perror("stat()");
	if (!S_ISDIR(path_stat.st_mode))
		ksft_exit_fail_msg("%s: Not a directory (%s)\n", __func__, finfo.dir);
	if (snprintf(finfo.path, sizeof(finfo.path), "%s/" TEST_FILE,
		     finfo.dir) >= sizeof(finfo.path))
		ksft_exit_fail_msg("%s: Pathname is too long\n", __func__);
	if (statfs(finfo.dir, &fs))
		ksft_exit_fail_perror("statfs()");
	finfo.type = fs.f_type == TMPFS_MAGIC ? VMA_SHMEM : VMA_FILE;
	if (finfo.type == VMA_SHMEM)
		return;

	/* Find owning device's queue/read_ahead_kb control */
	if (snprintf(path, sizeof(path), "/sys/dev/block/%d:%d/uevent",
		     major(path_stat.st_dev), minor(path_stat.st_dev))
	    >= sizeof(path))
		ksft_exit_fail_msg("%s: Pathname is too long\n", __func__);
	ret = read_file(path, buf, sizeof(buf));
	if (ret)
		ksft_exit_fail_msg("read_file(%s): %s\n", path, strerror(-ret));
	if (strstr(buf, "DEVTYPE=disk")) {
		/* Found it */
		if (snprintf(finfo.dev_queue_read_ahead_path,
			     sizeof(finfo.dev_queue_read_ahead_path),
			     "/sys/dev/block/%d:%d/queue/read_ahead_kb",
			     major(path_stat.st_dev), minor(path_stat.st_dev))
		    >= sizeof(finfo.dev_queue_read_ahead_path))
			ksft_exit_fail_msg("%s: Pathname is too long\n", __func__);
		return;
	}
	if (!strstr(buf, "DEVTYPE=partition"))
		ksft_exit_fail_msg("%s: Unknown device type: %s\n", __func__, path);
	/*
	 * Partition of block device - need to find actual device.
	 * Using naming convention that devnameN is partition of
	 * device devname.
	 */
	str = strstr(buf, "DEVNAME=");
	if (!str)
		ksft_exit_fail_msg("%s: Could not read: %s", __func__, path);
	str += 8;
	end = str;
	while (*end) {
		if (isdigit(*end)) {
			*end = '\0';
			if (snprintf(finfo.dev_queue_read_ahead_path,
				     sizeof(finfo.dev_queue_read_ahead_path),
				     "/sys/block/%s/queue/read_ahead_kb",
				     str) >= sizeof(finfo.dev_queue_read_ahead_path))
				ksft_exit_fail_msg("%s: Pathname is too long\n", __func__);
			return;
		}
		++end;
	}
	ksft_exit_fail_msg("%s: Could not read: %s\n", __func__, path);
}

static bool check_swap(void *addr, unsigned long size)
{
	bool swap = false;
	int ret;
	FILE *fp;
	char buffer[MAX_LINE_LENGTH];
	char addr_pattern[MAX_LINE_LENGTH];

	ret = snprintf(addr_pattern, MAX_LINE_LENGTH, "%08lx-",
		       (unsigned long) addr);
	if (ret >= MAX_LINE_LENGTH)
		ksft_exit_fail_msg("%s: Pattern is too long\n", __func__);

	fp = fopen(PID_SMAPS, "r");
	if (!fp)
		ksft_exit_fail_msg("%s: Failed to open file %s\n", __func__, PID_SMAPS);
	if (!check_for_pattern(fp, addr_pattern, buffer, sizeof(buffer)))
		goto err_out;

	ret = snprintf(addr_pattern, MAX_LINE_LENGTH, "Swap:%19ld kB",
		       size >> 10);
	if (ret >= MAX_LINE_LENGTH)
		ksft_exit_fail_msg("%s: Pattern is too long\n", __func__);
	/*
	 * Fetch the Swap: in the same block and check whether it got
	 * the expected number of hugeepages next.
	 */
	if (!check_for_pattern(fp, "Swap:", buffer, sizeof(buffer)))
		goto err_out;

	if (strncmp(buffer, addr_pattern, strlen(addr_pattern)))
		goto err_out;

	swap = true;
err_out:
	fclose(fp);
	return swap;
}

static bool swapout_range(void *p, unsigned long size)
{
	int i;

	/* keep khugepaged from collapsing the range and swapping it back in */
	if (madvise(p, size, MADV_NOHUGEPAGE))
		ksft_exit_fail_perror("madvise(MADV_NOHUGEPAGE)");

	/*
	 * Retry several times because MADV_PAGEOUT is best effort.  Sleep
	 * between the retries to give outstanding writeback a chance to
	 * finish.
	 */
	for (i = 0; i < 40; i++) {
		if (madvise(p, size, MADV_PAGEOUT))
			ksft_exit_fail_perror("madvise(MADV_PAGEOUT)");
		if (check_swap(p, size))
			return true;
		usleep(50 * 1000);
	}
	return false;
}

static void *alloc_mapping(int nr)
{
	void *p;

	p = mmap(BASE_ADDR, nr * hpage_pmd_size, PROT_READ | PROT_WRITE,
		 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p != BASE_ADDR)
		ksft_exit_fail_msg("Failed to allocate VMA at %p\n", BASE_ADDR);

	return p;
}

static void fill_memory(int *p, unsigned long start, unsigned long end)
{
	int i;

	for (i = start / page_size; i < end / page_size; i++)
		p[i * page_size / sizeof(*p)] = i + 0xdead0000;
}

/*
 * MADV_COLLAPSE is a best-effort request and may fail if an internal
 * resource is temporarily unavailable, in which case it will set errno to
 * EAGAIN.  In such a case, immediately reattempt the operation one more
 * time.
 */
static int madvise_collapse_retry(void *p, unsigned long size)
{
	bool retry = true;
	int ret;

retry:
	ret = madvise(p, size, MADV_COLLAPSE);
	if (ret && errno == EAGAIN && retry) {
		retry = false;
		goto retry;
	}
	return ret;
}

/*
 * Returns pmd-mapped hugepage in VMA marked VM_HUGEPAGE, filled with
 * validate_memory()'able contents.
 */
static void *alloc_hpage(struct mem_ops *ops)
{
	void *p = ops->setup_area(1);

	ops->fault(p, 0, hpage_pmd_size);

	/*
	 * VMA should be neither VM_HUGEPAGE nor VM_NOHUGEPAGE.
	 * The latter is ineligible for collapse by MADV_COLLAPSE
	 * while the former might cause MADV_COLLAPSE to race with
	 * khugepaged on low-load system (like a test machine), which
	 * would cause MADV_COLLAPSE to fail with EAGAIN.
	 */
	ksft_print_msg("Allocate huge page...");
	if (madvise_collapse_retry(p, hpage_pmd_size))
		ksft_exit_fail_perror("madvise(MADV_COLLAPSE)");
	if (!ops->check_huge(p, hpage_pmd_size, 1, hpage_pmd_size))
		ksft_exit_fail_perror("madvise(MADV_COLLAPSE)");
	if (madvise(p, hpage_pmd_size, MADV_HUGEPAGE))
		ksft_exit_fail_perror("madvise(MADV_HUGEPAGE)");
	success("OK");
	return p;
}

static void validate_memory(int *p, unsigned long start, unsigned long end)
{
	int i;

	for (i = start / page_size; i < end / page_size; i++) {
		if (p[i * page_size / sizeof(*p)] != i + 0xdead0000)
			ksft_exit_fail_msg("Page %d is corrupted: %#x\n",
					   i, p[i * page_size / sizeof(*p)]);
	}
}

static void *anon_setup_area(int nr_hpages)
{
	return alloc_mapping(nr_hpages);
}

static void anon_cleanup_area(void *p, unsigned long size)
{
	munmap(p, size);
}

static void anon_fault(void *p, unsigned long start, unsigned long end)
{
	fill_memory(p, start, end);
}

static bool anon_check_huge(void *addr, size_t len, int nr_hpages,
		unsigned long hpage_size)
{
	return check_huge_anon(addr, len, nr_hpages, hpage_size);
}

static void *file_setup_area_common(int nr_hpages, enum file_setup_ops setup)
{
	const int open_opt = setup == FILE_SETUP_READ_ONLY_FS ? O_RDONLY : O_RDWR;
	const int mmap_prot = setup == FILE_SETUP_READ_ONLY_FS ? PROT_READ : (PROT_READ | PROT_WRITE);
	int fd, ret;
	void *p;
	unsigned long size;

	unlink(finfo.path);  /* Cleanup from previous failed tests */
	ksft_print_msg("Creating %s for collapse%s...", finfo.path,
		       finfo.type == VMA_SHMEM ? " (tmpfs)" : "");
	fd = open(finfo.path, O_CREAT | O_RDWR | O_TRUNC | O_EXCL,
		  777);
	if (fd < 0)
		ksft_exit_fail_perror("open()");

	size = nr_hpages * hpage_pmd_size;
	if (ftruncate(fd, size))
		ksft_exit_fail_perror("ftruncate()");
	p = mmap(BASE_ADDR, size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (p != BASE_ADDR)
		ksft_exit_fail_perror("mmap()");
	fill_memory(p, 0, size);
	if (msync(p, size, MS_SYNC))
		ksft_exit_fail_perror("msync()");
	close(fd);
	munmap(p, size);
	success("OK");
	ksft_print_msg("Opening %s %s for collapse...", finfo.path,
	       setup == FILE_SETUP_READ_ONLY_FS ? "read-only" :
	       setup == FILE_SETUP_READ_WRITE_FS_READ_DATA ?
						  "read-write (read)" :
						  "read-write (write)");
	finfo.fd = open(finfo.path, open_opt, 777);
	if (finfo.fd < 0)
		ksft_exit_fail_perror("open()");
	p = mmap(BASE_ADDR, size, mmap_prot, MAP_SHARED, finfo.fd, 0);
	if (p == MAP_FAILED || p != BASE_ADDR)
		ksft_exit_fail_perror("mmap()");

	/* Drop page cache */
	ret = write_file("/proc/sys/vm/drop_caches", "3", 2);
	if (ret)
		ksft_exit_fail_msg("write_file(drop_caches): %s\n",
				   strerror(-ret));

	success("OK");
	return p;
}

static void *file_setup_read_only_area(int nr_hpages)
{
	return file_setup_area_common(nr_hpages, FILE_SETUP_READ_ONLY_FS);
}

static void *file_setup_read_write_fs_read_area(int nr_hpages)
{
	return file_setup_area_common(nr_hpages, FILE_SETUP_READ_WRITE_FS_READ_DATA);
}

static void *file_setup_read_write_fs_write_area(int nr_hpages)
{
	return file_setup_area_common(nr_hpages, FILE_SETUP_READ_WRITE_FS_WRITE_DATA);
}

static void file_cleanup_area(void *p, unsigned long size)
{
	munmap(p, size);
	close(finfo.fd);
	unlink(finfo.path);
}

static void file_fault_read(void *p, unsigned long start, unsigned long end)
{
	if (madvise(((char *)p) + start, end - start, MADV_POPULATE_READ))
		ksft_exit_fail_perror("madvise(MADV_POPULATE_READ)");
}

static void file_fault_read_and_flush(void *p, unsigned long start, unsigned long end)
{
	file_fault_read(p, start, end);
	/*
	 * make folio clean, since dirty folios from read&write file are
	 * rejected and not flushed
	 */
	msync((char *)p + start, end - start, MS_SYNC);
}

static void file_fault_write(void *p, unsigned long start, unsigned long end)
{
	if (madvise(((char *)p) + start, end - start, MADV_POPULATE_WRITE))
		ksft_exit_fail_perror("madvise(MADV_POPULATE_WRITE)");
}

static bool file_check_huge(void *addr, size_t len, int nr_hpages,
		unsigned long hpage_size)
{
	switch (finfo.type) {
	case VMA_FILE:
		return check_huge_file(addr, len, nr_hpages, hpage_size);
	case VMA_SHMEM:
		return check_huge_shmem(addr, len, nr_hpages, hpage_size);
	default:
		ksft_exit_fail_msg("Unknown VMA type\n");
		return false;
	}
}

static void *shmem_setup_area(int nr_hpages)
{
	void *p;
	unsigned long size = nr_hpages * hpage_pmd_size;

	finfo.fd = memfd_create("khugepaged-selftest-collapse-shmem", 0);
	if (finfo.fd < 0)
		ksft_exit_fail_perror("memfd_create()");
	if (ftruncate(finfo.fd, size))
		ksft_exit_fail_perror("ftruncate()");
	p = mmap(BASE_ADDR, size, PROT_READ | PROT_WRITE, MAP_SHARED, finfo.fd,
		 0);
	if (p != BASE_ADDR)
		ksft_exit_fail_perror("mmap()");
	return p;
}

static void shmem_cleanup_area(void *p, unsigned long size)
{
	munmap(p, size);
	close(finfo.fd);
}

static bool shmem_check_huge(void *addr, size_t len, int nr_hpages,
		unsigned long hpage_size)
{
	return check_huge_shmem(addr, len, nr_hpages, hpage_size);
}

static struct mem_ops __anon_ops = {
	.setup_area = &anon_setup_area,
	.cleanup_area = &anon_cleanup_area,
	.fault = &anon_fault,
	.check_huge = &anon_check_huge,
	.name = "anon",
};

static struct mem_ops __read_only_file_ops = {
	.setup_area = &file_setup_read_only_area,
	.cleanup_area = &file_cleanup_area,
	.fault = &file_fault_read,
	.check_huge = &file_check_huge,
	.name = "file",
};

static struct mem_ops __read_write_file_read_ops = {
	.setup_area = &file_setup_read_write_fs_read_area,
	.cleanup_area = &file_cleanup_area,
	.fault = &file_fault_read_and_flush,
	.check_huge = &file_check_huge,
	.name = "file",
};

static struct mem_ops __read_write_file_write_ops = {
	.setup_area = &file_setup_read_write_fs_write_area,
	.cleanup_area = &file_cleanup_area,
	.fault = &file_fault_write,
	.check_huge = &file_check_huge,
	.name = "file",
};

static struct mem_ops __shmem_ops = {
	.setup_area = &shmem_setup_area,
	.cleanup_area = &shmem_cleanup_area,
	.fault = &anon_fault,
	.check_huge = &shmem_check_huge,
	.name = "shmem",
};

static bool is_tmpfs(struct mem_ops *ops)
{
	return (ops == &__read_only_file_ops ||
		ops == &__read_write_file_read_ops ||
		ops == &__read_write_file_write_ops) &&
	       finfo.type == VMA_SHMEM;
}

static bool is_anon(struct mem_ops *ops)
{
	return ops == &__anon_ops;
}

static void __madvise_collapse(const char *msg, char *p, int nr_hpages,
			       struct mem_ops *ops, bool expect)
{
	struct thp_settings settings = *thp_current_settings();
	int ret, i;

	ksft_print_msg("%s...", msg);

	/*
	 * read&write file collapse succeeds for MADV_COLLAPSE because dirty
	 * folios are written back after collapse fails for dirty folios and
	 * another collapse is attempted.
	 */

	/*
	 * Prevent khugepaged interference and tests that MADV_COLLAPSE
	 * ignores /sys/kernel/mm/transparent_hugepage/enabled
	 */
	settings.thp_enabled = THP_NEVER;
	settings.shmem_enabled = SHMEM_NEVER;
	for (i = 0; i < NR_ORDERS; i++) {
		settings.hugepages[i].enabled = THP_NEVER;
		settings.shmem_hugepages[i].enabled = SHMEM_NEVER;
	}
	thp_push_settings(&settings);

	/* Clear VM_NOHUGEPAGE */
	madvise(p, nr_hpages * hpage_pmd_size, MADV_HUGEPAGE);
	ret = madvise_collapse_retry(p, nr_hpages * hpage_pmd_size);
	if (((bool)ret) == expect)
		fail("Fail: Bad return value");
	else if (!ops->check_huge(p, nr_hpages * hpage_pmd_size, expect ? nr_hpages : 0, hpage_pmd_size))
		fail("Fail: check_huge()");
	else
		success("OK");

	thp_pop_settings();
}

static void madvise_collapse(const char *msg, char *p, int nr_hpages,
			     struct mem_ops *ops, bool expect)
{
	/* Sanity check */
	if (!ops->check_huge(p, nr_hpages * hpage_pmd_size, 0, hpage_pmd_size))
		ksft_exit_fail_msg("Unexpected huge page\n");
	__madvise_collapse(msg, p, nr_hpages, ops, expect);
}

#define TICK 500000
static bool wait_for_scan(const char *msg, char *p, size_t len,
		int nr_hpages, int collap_order, struct mem_ops *ops)
{
	unsigned long hpage_size = page_size << collap_order;
	unsigned long bytes = (unsigned long)nr_hpages * hpage_size;
	int timeout, full_scans;

	/* Half-second ticks: three seconds floor, plus a second per 128M */
	timeout = 6 + 2 * (bytes / (128UL << 20));

	/* Sanity check */
	if (!ops->check_huge(p, len, 0, hpage_size))
		ksft_exit_fail_msg("Unexpected huge page\n");

	madvise(p, len, MADV_HUGEPAGE);

	/* Wait until the second full_scan completed */
	full_scans = thp_read_num("khugepaged/full_scans") + 2;

	ksft_print_msg("%s...", msg);
	while (timeout--) {
		if (ops->check_huge(p, len, nr_hpages, hpage_size))
			break;
		if (thp_read_num("khugepaged/full_scans") >= full_scans)
			break;
		printf(".");
		usleep(TICK);
	}

	return timeout == -1;
}

static void khugepaged_collapse(const char *msg, char *p, int nr_hpages,
				struct mem_ops *ops, bool expect)
{
	size_t len = nr_hpages * hpage_pmd_size;

	/*
	 * read&write file collapse fails since khugepaged does not flush
	 * the target dirty folios
	 */
	if (!is_tmpfs(ops) && ops == &__read_write_file_write_ops)
		expect = false;

	if (wait_for_scan(msg, p, len, nr_hpages, hpage_pmd_order, ops)) {
		if (expect)
			fail("Timeout");
		else
			success("OK");
		return;
	}

	/*
	 * For file and shmem memory, khugepaged only retracts pte entries after
	 * putting the new hugepage in the page cache. The hugepage must be
	 * subsequently refaulted to install the pmd mapping for the mm.
	 */
	if (ops != &__anon_ops)
		ops->fault(p, 0, nr_hpages * hpage_pmd_size);

	if (ops->check_huge(p, len, expect ? nr_hpages : 0, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");
}

static void mthp_khugepaged_collapse(const char *msg, char *p, int nr_hpages,
				struct mem_ops *ops, bool expect)
{
	unsigned long hpage_size = page_size << collapse_order;
	struct thp_settings settings = *thp_current_settings();
	/* mTHP collpase only allocates PMD sized memory */
	size_t len = hpage_pmd_size;

	/* Set mTHP setting for mTHP collapse */
	if (ops == &__anon_ops) {
		settings.thp_enabled = THP_NEVER;
		settings.hugepages[collapse_order].enabled = THP_MADVISE;
	}

	thp_push_settings(&settings);

	if (wait_for_scan(msg, p, len, nr_hpages, collapse_order, ops)) {
		if (expect)
			fail("Timeout");
		else
			success("OK");

		/* Restore THP settings for mTHP collapse. */
		thp_pop_settings();
		return;
	}

	/*
	 * For file and shmem memory, khugepaged only retracts pte entries after
	 * putting the new hugepage in the page cache. The hugepage must be
	 * subsequently refaulted to install the pmd mapping for the mm.
	 */
	if (ops != &__anon_ops)
		ops->fault(p, 0, nr_hpages * hpage_size);

	if (ops->check_huge(p, len, expect ? nr_hpages : 0, hpage_size))
		success("OK");
	else
		fail("Fail");

	/* Restore THP settings for mTHP collapse. */
	thp_pop_settings();
}

static struct collapse_context __khugepaged_context = {
	.collapse = &khugepaged_collapse,
	.enforce_pte_scan_limits = true,
	.name = "khugepaged",
};

static struct collapse_context __mthp_khugepaged_context = {
	.collapse = &mthp_khugepaged_collapse,
	.enforce_pte_scan_limits = true,
	.name = "mthp_khugepaged",
};

static struct collapse_context __madvise_context = {
	.collapse = &madvise_collapse,
	.enforce_pte_scan_limits = false,
	.name = "madvise",
};

static void alloc_at_fault(void)
{
	struct thp_settings settings = *thp_current_settings();
	char *p;

	settings.thp_enabled = THP_ALWAYS;
	thp_push_settings(&settings);

	p = alloc_mapping(1);
	*p = 1;
	ksft_print_msg("Allocate huge page on fault...");
	if (check_huge_anon(p, hpage_pmd_size, 1, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");

	thp_pop_settings();

	madvise(p, page_size, MADV_DONTNEED);
	ksft_print_msg("Split huge PMD on MADV_DONTNEED...");
	if (check_huge_anon(p, hpage_pmd_size, 0, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");
	munmap(p, hpage_pmd_size);

	ksft_test_result_report(exit_status, "allocate on fault and split\n");
}

static void collapse_full(struct collapse_context *c, struct mem_ops *ops)
{
	void *p;
	int nr_pmds = 4, nr_hpages = 4;
	unsigned long size = nr_hpages * hpage_pmd_size;

	/* Only try 1 PMD sized range for mTHP collapse. */
	if (c == &__mthp_khugepaged_context) {
		nr_pmds = 1;
		nr_hpages = 1 << (hpage_pmd_order - collapse_order);
		size = hpage_pmd_size;
	}

	p = ops->setup_area(nr_pmds);
	ops->fault(p, 0, size);
	c->collapse("Collapse multiple fully populated PTE table", p, nr_hpages,
		    ops, true);
	validate_memory(p, 0, size);
	ops->cleanup_area(p, size);

	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_empty(struct collapse_context *c, struct mem_ops *ops)
{
	int nr_hpages = 1;
	void *p;

	if (c == &__mthp_khugepaged_context)
		nr_hpages = 1 << (hpage_pmd_order - collapse_order);

	p = ops->setup_area(1);
	c->collapse("Do not collapse empty PTE table", p, nr_hpages, ops, false);
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_single_mthp(struct collapse_context *c, struct mem_ops *ops)
{
	unsigned long hpage_size = page_size << collapse_order;
	void *p;

	p = ops->setup_area(1);
	/*
	 * Only fault collapse_order sized ranges, and only check 1
	 * collapse_order sized huge page.
	 */
	ops->fault(p, 0, hpage_size);
	c->collapse("Collapse PTE table with half PTE entries present",
		p, 1, ops, true);
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_single_pte_entry(struct collapse_context *c, struct mem_ops *ops)
{
	void *p;

	p = ops->setup_area(1);
	ops->fault(p, 0, page_size);
	c->collapse("Collapse PTE table with single PTE entry present", p,
		    1, ops, true);
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_max_ptes_none(struct collapse_context *c, struct mem_ops *ops)
{
	int max_ptes_none = hpage_pmd_nr / 2;
	struct thp_settings settings = *thp_current_settings();
	void *p;
	int fault_nr_pages = is_anon(ops) ? 1 << anon_order : 1;

	settings.khugepaged.max_ptes_none = max_ptes_none;
	thp_push_settings(&settings);

	p = ops->setup_area(1);

	if (is_tmpfs(ops)) {
		/* shmem pages always in the page cache */
		printf("tmpfs...");
		skip("Skip");
		goto skip;
	}

	ops->fault(p, 0, (hpage_pmd_nr - max_ptes_none - fault_nr_pages) * page_size);
	c->collapse("Maybe collapse with max_ptes_none exceeded", p, 1,
		    ops, !c->enforce_pte_scan_limits);
	validate_memory(p, 0, (hpage_pmd_nr - max_ptes_none - fault_nr_pages) * page_size);

	if (c->enforce_pte_scan_limits) {
		ops->cleanup_area(p, hpage_pmd_size);
		p = ops->setup_area(1);

		ops->fault(p, 0, (hpage_pmd_nr - max_ptes_none) * page_size);
		c->collapse("Collapse with max_ptes_none PTEs empty", p, 1, ops,
			    true);
		validate_memory(p, 0,
				(hpage_pmd_nr - max_ptes_none) * page_size);
	}
skip:
	ops->cleanup_area(p, hpage_pmd_size);
	thp_pop_settings();
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_swapin_single_pte(struct collapse_context *c, struct mem_ops *ops)
{
	void *p;

	p = ops->setup_area(1);
	ops->fault(p, 0, hpage_pmd_size);

	ksft_print_msg("Swapout one page...");
	if (swapout_range(p, page_size)) {
		success("OK");
	} else {
		skip("Could not swap out");
		goto out;
	}

	c->collapse("Collapse with swapping in single PTE entry", p, 1, ops,
		    true);
	validate_memory(p, 0, hpage_pmd_size);
out:
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_max_ptes_swap(struct collapse_context *c, struct mem_ops *ops)
{
	int max_ptes_swap = thp_read_num("khugepaged/max_ptes_swap");
	void *p;

	p = ops->setup_area(1);
	ops->fault(p, 0, hpage_pmd_size);

	ksft_print_msg("Swapout %d of %d pages...", max_ptes_swap + 1, hpage_pmd_nr);
	if (swapout_range(p, (max_ptes_swap + 1) * page_size)) {
		success("OK");
	} else {
		skip("Could not swap out");
		goto out;
	}

	c->collapse("Maybe collapse with max_ptes_swap exceeded", p, 1, ops,
		    !c->enforce_pte_scan_limits);
	validate_memory(p, 0, hpage_pmd_size);

	if (c->enforce_pte_scan_limits) {
		ops->fault(p, 0, hpage_pmd_size);
		ksft_print_msg("Swapout %d of %d pages...", max_ptes_swap,
		       hpage_pmd_nr);
		if (swapout_range(p, max_ptes_swap * page_size)) {
			success("OK");
		} else {
			skip("Could not swap out");
			goto out;
		}

		c->collapse("Collapse with max_ptes_swap pages swapped out", p,
			    1, ops, true);
		validate_memory(p, 0, hpage_pmd_size);
	}
out:
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_single_pte_entry_compound(struct collapse_context *c, struct mem_ops *ops)
{
	void *p;

	p = alloc_hpage(ops);

	if (is_tmpfs(ops)) {
		/* MADV_DONTNEED won't evict tmpfs pages */
		printf("tmpfs...");
		skip("Skip");
		goto skip;
	}

	madvise(p, hpage_pmd_size, MADV_NOHUGEPAGE);
	ksft_print_msg("Split huge page leaving single PTE mapping compound page...");
	madvise(p + page_size, hpage_pmd_size - page_size, MADV_DONTNEED);
	if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");

	c->collapse("Collapse PTE table with single PTE mapping compound page",
		    p, 1, ops, true);
	validate_memory(p, 0, page_size);
skip:
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_full_of_compound(struct collapse_context *c, struct mem_ops *ops)
{
	void *p;

	p = alloc_hpage(ops);
	ksft_print_msg("Split huge page leaving single PTE page table full of compound pages...");
	madvise(p, page_size, MADV_NOHUGEPAGE);
	madvise(p, hpage_pmd_size, MADV_NOHUGEPAGE);
	if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");

	c->collapse("Collapse PTE table full of compound pages", p, 1, ops,
		    true);
	validate_memory(p, 0, hpage_pmd_size);
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_compound_extreme(struct collapse_context *c, struct mem_ops *ops)
{
	void *p;
	int i;

	/*
	 * This needs hpage_pmd_nr PMD-order allocations in a row, which the
	 * allocator will not supply if the PMD is very large.
	 */
	if (hpage_pmd_size > (32UL << 20)) {
		ksft_test_result_skip("%s: PMD too large for fault-time THP construction\n",
				      __func__);
		return;
	}

	p = ops->setup_area(1);
	ksft_print_msg("Construct PTE page table full of different PTE-mapped compound pages\n");
	for (i = 0; i < hpage_pmd_nr; i++) {
		madvise(BASE_ADDR, hpage_pmd_size, MADV_HUGEPAGE);
		ops->fault(BASE_ADDR, 0, hpage_pmd_size);
		if (!ops->check_huge(BASE_ADDR, hpage_pmd_size, 1, hpage_pmd_size))
			ksft_exit_fail_msg("Failed to allocate huge page\n");
		madvise(BASE_ADDR, hpage_pmd_size, MADV_NOHUGEPAGE);

		p = mremap(BASE_ADDR - i * page_size,
				i * page_size + hpage_pmd_size,
				(i + 1) * page_size,
				MREMAP_MAYMOVE | MREMAP_FIXED,
				BASE_ADDR + 2 * hpage_pmd_size);
		if (p == MAP_FAILED)
			ksft_exit_fail_perror("mremap+unmap");

		p = mremap(BASE_ADDR + 2 * hpage_pmd_size,
				(i + 1) * page_size,
				(i + 1) * page_size + hpage_pmd_size,
				MREMAP_MAYMOVE | MREMAP_FIXED,
				BASE_ADDR - (i + 1) * page_size);
		if (p == MAP_FAILED)
			ksft_exit_fail_perror("mremap+alloc");
	}

	ops->cleanup_area(BASE_ADDR, hpage_pmd_size);
	ops->fault(p, 0, hpage_pmd_size);
	if (!ops->check_huge(p, hpage_pmd_size, 1, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");

	c->collapse("Collapse PTE table full of different compound pages", p, 1,
		    ops, true);

	validate_memory(p, 0, hpage_pmd_size);
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_fork(struct collapse_context *c, struct mem_ops *ops)
{
	int wstatus;
	void *p;

	p = ops->setup_area(1);

	ksft_print_msg("Allocate small page...");
	ops->fault(p, 0, page_size);
	if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");

	ksft_print_msg("Share small page over fork()...");
	if (!fork()) {
		/* Do not touch settings on child exit */
		if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
			success("OK");
		else
			fail("Fail");

		ops->fault(p, page_size, 2 * page_size);
		c->collapse("Collapse PTE table with single page shared with parent process",
			    p, 1, ops, true);

		validate_memory(p, 0, page_size);
		ops->cleanup_area(p, hpage_pmd_size);
		_exit(exit_status);
	}

	wait(&wstatus);
	exit_status = WEXITSTATUS(wstatus);
	if (exit_status == KSFT_FAIL)
		goto out;

	ksft_print_msg("Check if parent still has small page...");
	if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");
	validate_memory(p, 0, page_size);
out:
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_fork_compound(struct collapse_context *c, struct mem_ops *ops)
{
	int wstatus;
	void *p;

	p = alloc_hpage(ops);
	ksft_print_msg("Share huge page over fork()...");
	if (!fork()) {
		/* Do not touch settings on child exit */
		if (ops->check_huge(p, hpage_pmd_size, 1, hpage_pmd_size))
			success("OK");
		else
			fail("Fail");

		ksft_print_msg("Split huge page PMD in child process...");
		madvise(p, page_size, MADV_NOHUGEPAGE);
		madvise(p, hpage_pmd_size, MADV_NOHUGEPAGE);
		if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
			success("OK");
		else
			fail("Fail");
		ops->fault(p, 0, page_size);

		thp_write_num("khugepaged/max_ptes_shared", hpage_pmd_nr - 1);
		c->collapse("Collapse PTE table full of compound pages in child",
			    p, 1, ops, true);
		thp_write_num("khugepaged/max_ptes_shared",
			  thp_current_settings()->khugepaged.max_ptes_shared);

		validate_memory(p, 0, hpage_pmd_size);
		ops->cleanup_area(p, hpage_pmd_size);
		_exit(exit_status);
	}

	wait(&wstatus);
	exit_status = WEXITSTATUS(wstatus);
	if (exit_status == KSFT_FAIL)
		goto out;

	ksft_print_msg("Check if parent still has huge page...");
	if (ops->check_huge(p, hpage_pmd_size, 1, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");
	validate_memory(p, 0, hpage_pmd_size);
out:
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_max_ptes_shared(struct collapse_context *c, struct mem_ops *ops)
{
	int max_ptes_shared = thp_read_num("khugepaged/max_ptes_shared");
	int wstatus;
	void *p;

	p = alloc_hpage(ops);
	ksft_print_msg("Share huge page over fork()...");
	if (!fork()) {
		/* Do not touch settings on child exit */
		if (ops->check_huge(p, hpage_pmd_size, 1, hpage_pmd_size))
			success("OK");
		else
			fail("Fail");

		ksft_print_msg("Trigger CoW on page %d of %d...",
				hpage_pmd_nr - max_ptes_shared - 1, hpage_pmd_nr);
		ops->fault(p, 0, (hpage_pmd_nr - max_ptes_shared - 1) * page_size);
		if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
			success("OK");
		else
			fail("Fail");

		c->collapse("Maybe collapse with max_ptes_shared exceeded", p,
			    1, ops, !c->enforce_pte_scan_limits);

		if (c->enforce_pte_scan_limits) {
			ksft_print_msg("Trigger CoW on page %d of %d...",
			       hpage_pmd_nr - max_ptes_shared, hpage_pmd_nr);
			ops->fault(p, 0, (hpage_pmd_nr - max_ptes_shared) *
				    page_size);
			if (ops->check_huge(p, hpage_pmd_size, 0, hpage_pmd_size))
				success("OK");
			else
				fail("Fail");

			c->collapse("Collapse with max_ptes_shared PTEs shared",
				    p, 1, ops, true);
		}

		validate_memory(p, 0, hpage_pmd_size);
		ops->cleanup_area(p, hpage_pmd_size);
		_exit(exit_status);
	}

	wait(&wstatus);
	exit_status = WEXITSTATUS(wstatus);
	if (exit_status == KSFT_FAIL)
		goto out;

	ksft_print_msg("Check if parent still has huge page...");
	if (ops->check_huge(p, hpage_pmd_size, 1, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");
	validate_memory(p, 0, hpage_pmd_size);
out:
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

/*
 * The parent writes to the fork-shared range throughout the child's
 * collapse.  CoW must keep the two apart: the child sees the pre-fork
 * content, the parent only its own writes.
 */
static void collapse_fork_cow_race(struct collapse_context *c, struct mem_ops *ops)
{
	const int stride = page_size / sizeof(int);
	int wstatus, child_status, i, n;
	unsigned long shared;
	volatile int *ip;
	pid_t child;
	int sync[2];
	char go = 1;
	void *p;

	/* At a page per 10 ms, 64 pages spread the writes across the collapse */
	n = 64;
	shared = n * page_size;

	p = ops->setup_area(1);
	/* Shared prefix, with the pre-fork pattern */
	ops->fault(p, 0, shared);
	if (pipe(sync))
		ksft_exit_fail_perror("pipe()");

	/* A volatile pointer so the stores are not merged or dropped */
	ip = p;

	ksft_print_msg("Fork, collapse in the child while the parent rewrites...");
	child = fork();
	if (!child) {
		int collapse_status;

		close(sync[0]);
		/* Private remainder */
		ops->fault(p, shared, hpage_pmd_size);
		/* Start the parent unsharing, and give it a head start */
		if (write(sync[1], &go, 1) != 1)
			_exit(KSFT_FAIL);
		usleep(5000);
		c->collapse("Collapse a range the parent is writing to",
			    p, 1, ops, true);
		collapse_status = exit_status;
		for (i = 0; i < n; i++)
			if (ip[i * stride] != i + 0xdead0000)
				break;
		if (i == n)
			success("OK");
		else
			fail("Fail: child content");
		/* The content check must not bury a failed collapse */
		if (exit_status != KSFT_FAIL)
			exit_status = collapse_status;
		ops->cleanup_area(p, hpage_pmd_size);
		_exit(exit_status);
	}

	close(sync[1]);
	if (read(sync[0], &go, 1) != 1)
		ksft_exit_fail_msg("child never reached the collapse\n");
	close(sync[0]);

	/*
	 * Unshare one page at a time: a burst would break CoW on the whole
	 * range before the collapse starts, leaving nothing shared to collapse.
	 */
	i = 0;
	for (;;) {
		pid_t ret;

		if (i < n)
			ip[i * stride] = i + 0xbeef0000;
		i++;
		usleep(10 * 1000);
		ret = waitpid(child, &wstatus, WNOHANG);
		if (ret == child)
			break;
		if (ret < 0)
			ksft_exit_fail_perror("waitpid()");
	}

	/* Finish whatever the paced sweep did not reach */
	for (; i < n; i++)
		ip[i * stride] = i + 0xbeef0000;
	/* A child that died reading the racing pages is a failure, not a zero */
	child_status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : KSFT_FAIL;

	ksft_print_msg("Check the parent sees only its own writes...");
	for (i = 0; i < n; i++)
		if (ip[i * stride] != i + 0xbeef0000)
			break;
	if (i == n)
		success("OK");
	else
		fail("Fail: parent content");
	ops->cleanup_area(p, hpage_pmd_size);
	/* The parent's check must not bury the child's verdict */
	if (exit_status != KSFT_FAIL)
		exit_status = child_status;
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void madvise_collapse_existing_thps(struct collapse_context *c,
					   struct mem_ops *ops)
{
	void *p;

	p = ops->setup_area(1);
	ops->fault(p, 0, hpage_pmd_size);
	c->collapse("Collapse fully populated PTE table...", p, 1, ops, true);
	validate_memory(p, 0, hpage_pmd_size);

	/* c->collapse() will find a hugepage and complain - call directly. */
	__madvise_collapse("Re-collapse PMD-mapped hugepage", p, 1, ops, true);
	validate_memory(p, 0, hpage_pmd_size);
	ops->cleanup_area(p, hpage_pmd_size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

/*
 * Test race with khugepaged where page tables have been retracted and
 * pmd cleared.
 */
static void madvise_retracted_page_tables(struct collapse_context *c,
					  struct mem_ops *ops)
{
	void *p;
	int nr_hpages = 1;
	unsigned long size = nr_hpages * hpage_pmd_size;

	p = ops->setup_area(nr_hpages);
	ops->fault(p, 0, size);

	/* Let khugepaged collapse and leave pmd cleared */
	if (wait_for_scan("Collapse and leave PMD cleared", p, size, nr_hpages,
			  hpage_pmd_order, ops)) {
		fail("Timeout");
		return;
	}
	success("OK");
	c->collapse("Install huge PMD from page cache", p, nr_hpages, ops,
		    true);
	validate_memory(p, 0, size);
	ops->cleanup_area(p, size);
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

/* Smallest order khugepaged will consider for mTHP collapse */
#define MIN_MTHP_ORDER 2

/* Time budget for one khugepaged pass in the collapse_order_* cases */
#define MTHP_PASS_TIMEOUT_S 30

static size_t mthp_window_size(void)
{
	return page_size << collapse_order;
}

static void mthp_push_target_order(void)
{
	struct thp_settings settings = *thp_current_settings();
	int i;

	/*
	 * Only the target order, and only for madvise: the cases fault their
	 * region first, so the sources stay order 0 whatever -s asked for.
	 */
	settings.thp_enabled = THP_NEVER;
	for (i = 0; i < NR_ORDERS; i++)
		settings.hugepages[i].enabled = THP_NEVER;
	settings.hugepages[collapse_order].enabled = THP_MADVISE;
	thp_push_settings(&settings);
}

static bool all_windows_at_order(void *p, size_t len)
{
	return is_range_backed_by_order(p, len, collapse_order,
					pagemap_fd, kpageflags_fd);
}

static bool any_window_at_order(void *p, size_t len)
{
	size_t window = mthp_window_size();
	char *addr = p;

	for (; len >= window; addr += window, len -= window) {
		if (all_windows_at_order(addr, window))
			return true;
	}
	return false;
}

static void collapse_order_single_window(struct collapse_context *c,
					 struct mem_ops *ops)
{
	size_t window = mthp_window_size();
	void *p;

	mthp_push_target_order();

	p = ops->setup_area(1);
	ops->fault(p, window, 2 * window);
	if (any_window_at_order(p, hpage_pmd_size))
		ksft_exit_fail_msg("Unexpected large folio after fault\n");

	if (madvise(p, hpage_pmd_size, MADV_HUGEPAGE))
		ksft_exit_fail_perror("madvise(MADV_HUGEPAGE)");
	ksft_print_msg("Collapse one fully populated window...");
	if (!khugepaged_full_pass(MTHP_PASS_TIMEOUT_S))
		fail("Timeout");
	else if (all_windows_at_order(p + window, window) &&
		 !any_window_at_order(p, window) &&
		 !any_window_at_order(p + 2 * window,
				      hpage_pmd_size - 2 * window))
		success("OK");
	else
		fail("Fail");

	validate_memory(p, window, 2 * window);
	ops->cleanup_area(p, hpage_pmd_size);
	thp_pop_settings();
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_order_partial_window(struct collapse_context *c,
					  struct mem_ops *ops)
{
	void *p;

	mthp_push_target_order();

	p = ops->setup_area(1);
	ops->fault(p, 0, page_size);
	if (any_window_at_order(p, hpage_pmd_size))
		ksft_exit_fail_msg("Unexpected large folio after fault\n");

	if (madvise(p, hpage_pmd_size, MADV_HUGEPAGE))
		ksft_exit_fail_perror("madvise(MADV_HUGEPAGE)");
	ksft_print_msg("Collapse window with single PTE entry present...");
	if (!khugepaged_full_pass(MTHP_PASS_TIMEOUT_S))
		fail("Timeout");
	else if (all_windows_at_order(p, mthp_window_size()))
		success("OK");
	else
		fail("Fail");

	validate_memory(p, 0, page_size);
	ops->cleanup_area(p, hpage_pmd_size);
	thp_pop_settings();
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_order_max_ptes_none(struct collapse_context *c,
					 struct mem_ops *ops)
{
	struct thp_settings settings;
	size_t window = mthp_window_size();
	void *p;

	mthp_push_target_order();
	settings = *thp_current_settings();
	settings.khugepaged.max_ptes_none = 0;
	thp_push_settings(&settings);

	p = ops->setup_area(1);
	ops->fault(p, 0, 2 * window - page_size);
	if (any_window_at_order(p, hpage_pmd_size))
		ksft_exit_fail_msg("Unexpected large folio after fault\n");

	if (madvise(p, hpage_pmd_size, MADV_HUGEPAGE))
		ksft_exit_fail_perror("madvise(MADV_HUGEPAGE)");
	ksft_print_msg("Collapse full window, not the one missing a page...");
	if (!khugepaged_full_pass(MTHP_PASS_TIMEOUT_S))
		fail("Timeout");
	else if (all_windows_at_order(p, window) &&
		 !any_window_at_order(p + window, window))
		success("OK");
	else
		fail("Fail");

	validate_memory(p, 0, 2 * window - page_size);
	ops->cleanup_area(p, hpage_pmd_size);
	thp_pop_settings();
	thp_pop_settings();
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void collapse_order_mixed_sources(struct collapse_context *c,
					 struct mem_ops *ops)
{
	int source_order = anon_order ? anon_order : MIN_MTHP_ORDER;
	struct thp_settings settings;
	void *p;

	if (source_order >= collapse_order ||
	    !(thp_supported_orders() & (1UL << source_order))) {
		ksft_test_result_skip("%s: no supported source order below target\n",
				      __func__);
		return;
	}

	mthp_push_target_order();

	settings = *thp_current_settings();
	settings.hugepages[source_order].enabled = THP_ALWAYS;
	thp_push_settings(&settings);
	p = ops->setup_area(1);
	ops->fault(p, 0, hpage_pmd_size);
	thp_pop_settings();

	/*
	 * The allocator can fall back to smaller folios under fragmentation;
	 * having nothing to collapse from is not a failure.
	 */
	if (!is_range_backed_by_order(p, hpage_pmd_size, source_order,
				      pagemap_fd, kpageflags_fd)) {
		ksft_print_msg("No order-%d sources to collapse...", source_order);
		skip("Skip");
		ops->cleanup_area(p, hpage_pmd_size);
		thp_pop_settings();
		ksft_test_result_report(exit_status, "%s\n", __func__);
		return;
	}

	if (madvise(p, hpage_pmd_size, MADV_HUGEPAGE))
		ksft_exit_fail_perror("madvise(MADV_HUGEPAGE)");
	ksft_print_msg("Collapse region backed by order-%d sources...",
		       source_order);
	if (!khugepaged_full_pass(MTHP_PASS_TIMEOUT_S))
		fail("Timeout");
	else if (all_windows_at_order(p, hpage_pmd_size))
		success("OK");
	else
		fail("Fail");

	validate_memory(p, 0, hpage_pmd_size);
	ops->cleanup_area(p, hpage_pmd_size);
	thp_pop_settings();
	ksft_test_result_report(exit_status, "%s\n", __func__);
}

static void usage(void)
{
	fprintf(stderr, "\nUsage: ./khugepaged [OPTIONS] <test type> [dir]\n\n");
	fprintf(stderr, "\t<test type>\t: <context>:<mem_type>\n");
	fprintf(stderr, "\t<context>\t: [all|khugepaged|mthp_khugepaged|madvise]\n");
	fprintf(stderr, "\t<mem_type>\t: [all|anon|file|shmem]\n");
	fprintf(stderr, "\n\t\"file,all\" mem_type requires [dir] argument\n");
	fprintf(stderr, "\n\t\"file,all\" mem_type requires a file system\n");
	fprintf(stderr,	"\twith PMD-sized large folio support\n");
	fprintf(stderr, "\n\tif [dir] is a (sub)directory of a tmpfs mount, tmpfs must be\n");
	fprintf(stderr,	"\tmounted with huge=advise option for khugepaged tests to work\n");
	fprintf(stderr, "\n\tmthp_khugepaged only supports anon mem_type now.\n");
	fprintf(stderr,	"\n\tSupported Options:\n");
	fprintf(stderr,	"\t\t-h: This help message.\n");
	fprintf(stderr,	"\t\t-s: mTHP size, expressed as page order.\n");
	fprintf(stderr,	"\t\t    Defaults to 0. Use this size for anon or shmem allocations.\n");
	fprintf(stderr,	"\t\t-c: collapse order for mTHP collapse, expressed as page order.\n");
	fprintf(stderr,	"\t\t    Defaults to every supported order below the PMD.\n");
	fprintf(stderr,	"\t\t    -s, if set, is the source order for the mixed-source case.\n");
	exit(1);
}

static void parse_test_type(int argc, char **argv)
{
	bool mthp_context_implied = false;
	int opt;
	char *buf;
	const char *token;

	while ((opt = getopt(argc, argv, "s:c:h")) != -1) {
		switch (opt) {
		case 's':
			anon_order = atoi(optarg);
			break;
		case 'c':
			collapse_order = atoi(optarg);
			collapse_order_set = true;
			break;
		case 'h':
		default:
			usage();
		}
	}

	/*
	 * Both orders end up as array indices and shift counts, so neither
	 * can be negative, and a zero collapse order asks for base pages.
	 */
	if (anon_order < 0 || anon_order > hpage_pmd_order)
		ksft_exit_fail_msg("-s takes an order in 0..%d, not %d\n",
				   hpage_pmd_order, anon_order);
	if (collapse_order_set &&
	    (collapse_order <= 0 || collapse_order >= hpage_pmd_order))
		ksft_exit_fail_msg("-c takes an order in 1..%d, not %d\n",
				   hpage_pmd_order - 1, collapse_order);

	argv += optind;
	argc -= optind;

	if (argc == 0) {
		/* No arguments: anon under every context */
		khugepaged_context =  &__khugepaged_context;
		mthp_khugepaged_context =  &__mthp_khugepaged_context;
		madvise_context =  &__madvise_context;
		anon_ops = &__anon_ops;
		return;
	}

	buf = argv[0];
	token = strsep(&buf, ":");

	if (!strcmp(token, "all")) {
		khugepaged_context =  &__khugepaged_context;
		mthp_khugepaged_context =  &__mthp_khugepaged_context;
		madvise_context =  &__madvise_context;
		/* The mTHP context has only anon cases; let other mem_types drop it */
		mthp_context_implied = true;
	} else if (!strcmp(token, "khugepaged")) {
		khugepaged_context =  &__khugepaged_context;
	} else if (!strcmp(token, "mthp_khugepaged")) {
		mthp_khugepaged_context =  &__mthp_khugepaged_context;
	} else if (!strcmp(token, "madvise")) {
		madvise_context =  &__madvise_context;
	} else {
		usage();
	}

	if (!buf)
		usage();

	if (!strcmp(buf, "all")) {
		read_only_file_ops =  &__read_only_file_ops;
		read_write_file_read_ops =  &__read_write_file_read_ops;
		read_write_file_write_ops =  &__read_write_file_write_ops;
		anon_ops = &__anon_ops;
		shmem_ops = &__shmem_ops;
	} else if (!strcmp(buf, "anon")) {
		anon_ops = &__anon_ops;
	} else if (!strcmp(buf, "file")) {
		read_only_file_ops =  &__read_only_file_ops;
		read_write_file_read_ops =  &__read_write_file_read_ops;
		read_write_file_write_ops =  &__read_write_file_write_ops;
		if (mthp_khugepaged_context && !mthp_context_implied)
			usage();
		mthp_khugepaged_context = NULL;
	} else if (!strcmp(buf, "shmem")) {
		shmem_ops = &__shmem_ops;
		if (mthp_khugepaged_context && !mthp_context_implied)
			usage();
		mthp_khugepaged_context = NULL;
	} else {
		usage();
	}

	if (!read_only_file_ops && !read_write_file_read_ops &&
	    !read_write_file_write_ops)
		return;

	if (argc != 2)
		usage();

	get_finfo(argv[1]);
}

typedef void (*test_fn)(struct collapse_context *c, struct mem_ops *ops);

struct test_case {
	struct collapse_context *ctx;
	struct mem_ops *ops;
	const char *desc;
	test_fn fn;
	int order;		/* mTHP contexts: the collapse order */
};

#define MAX_TEST_CASES 256
static struct test_case test_cases[MAX_TEST_CASES];
static int nr_test_cases;

#define TEST(t, c, o) do {						\
	if (c && o) {							\
		if (nr_test_cases >= MAX_TEST_CASES)			\
			ksft_exit_fail_msg("MAX_TEST_CASES is too small\n"); \
		test_cases[nr_test_cases++] = (struct test_case){	\
			.ctx	= c,					\
			.ops	= o,					\
			.desc	= #t,					\
			.fn	= t,					\
			.order	= collapse_order,			\
		};							\
	}								\
	} while (0)

int main(int argc, char **argv)
{
	struct thp_settings default_settings = {
		.thp_enabled = THP_MADVISE,
		.thp_defrag = THP_DEFRAG_ALWAYS,
		.shmem_enabled = SHMEM_ADVISE,
		.use_zero_page = 0,
		.khugepaged = {
			.defrag = 1,
			.alloc_sleep_millisecs = 10,
			.scan_sleep_millisecs = 10,
		},
		/*
		 * When testing file-backed memory, the collapse path
		 * looks at how many pages are found in the page cache, not
		 * what pages are mapped. Disable read ahead optimization so
		 * pages don't find their way into the page cache unless
		 * we mem_ops->fault() them in.
		 */
		.read_ahead_kb = 0,
	};

	ksft_print_header();

	if (!thp_is_enabled())
		ksft_exit_skip("Transparent Hugepages not available\n");

	page_size = getpagesize();
	hpage_pmd_size = read_pmd_pagesize();
	if (!hpage_pmd_size)
		ksft_exit_fail_msg("Reading PMD pagesize failed\n");
	hpage_pmd_nr = hpage_pmd_size / page_size;
	hpage_pmd_order = __builtin_ctz(hpage_pmd_nr);

	parse_test_type(argc, argv);

	if (mthp_khugepaged_context) {
		unsigned long orders = thp_supported_orders();

		if (collapse_order_set) {
			if (!(orders & (1UL << collapse_order)))
				ksft_exit_skip("Order %d is not a supported anon THP order\n",
					       collapse_order);
			if (collapse_order <= anon_order)
				ksft_exit_skip("-c %d needs a source order below it, -s says %d\n",
					       collapse_order, anon_order);
			collapse_orders[nr_collapse_orders++] = collapse_order;
		} else {
			/*
			 * Every supported order above the source: -s makes the
			 * fault path hand out folios of that order, so a target
			 * at or below it has nothing to collapse.
			 */
			int first = anon_order + 1;

			if (first < MIN_MTHP_ORDER)
				first = MIN_MTHP_ORDER;
			for (int i = first; i < hpage_pmd_order; i++) {
				if (orders & (1UL << i))
					collapse_orders[nr_collapse_orders++] = i;
			}
			if (!nr_collapse_orders)
				ksft_print_msg("mTHP cases skipped: no order above the source\n");
		}
	}

	if (mthp_khugepaged_context) {
		pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
		if (pagemap_fd < 0)
			ksft_exit_fail_perror("open(/proc/self/pagemap)");
		kpageflags_fd = open("/proc/kpageflags", O_RDONLY);
		if (kpageflags_fd < 0)
			ksft_exit_fail_perror("open(/proc/kpageflags)");
	}

	setbuf(stdout, NULL);

	/*
	 * Without a PMD-order page cache folio the kernel refuses these
	 * collapses, so there is nothing to test.
	 */
	if (!(thp_file_supported_orders() & (1UL << hpage_pmd_order))) {
		if (shmem_ops) {
			ksft_print_msg("no PMD-order page cache folio: skipping shmem\n");
			shmem_ops = NULL;
		}
		if (read_only_file_ops) {
			ksft_print_msg("no PMD-order page cache folio: skipping file\n");
			read_only_file_ops = NULL;
			read_write_file_read_ops = NULL;
			read_write_file_write_ops = NULL;
		}
		if (!anon_ops && !shmem_ops && !read_only_file_ops)
			ksft_exit_skip("No mem_type left to run\n");
	}

	default_settings.khugepaged.max_ptes_none = hpage_pmd_nr - 1;
	default_settings.khugepaged.max_ptes_swap = hpage_pmd_nr / 8;
	default_settings.khugepaged.max_ptes_shared = hpage_pmd_nr / 2;
	default_settings.khugepaged.pages_to_scan = hpage_pmd_nr * 8;
	default_settings.hugepages[hpage_pmd_order].enabled = THP_INHERIT;
	default_settings.hugepages[anon_order].enabled = THP_ALWAYS;
	default_settings.shmem_hugepages[hpage_pmd_order].enabled = SHMEM_INHERIT;
	default_settings.shmem_hugepages[anon_order].enabled = SHMEM_ALWAYS;

	save_settings();
	thp_push_settings(&default_settings);

	TEST(collapse_full, khugepaged_context, anon_ops);
	TEST(collapse_full, khugepaged_context, read_only_file_ops);
	TEST(collapse_full, khugepaged_context, read_write_file_read_ops);
	TEST(collapse_full, khugepaged_context, read_write_file_write_ops);
	TEST(collapse_full, khugepaged_context, shmem_ops);
	for (int i = 0; i < nr_collapse_orders; i++) {
		collapse_order = collapse_orders[i];
		TEST(collapse_full, mthp_khugepaged_context, anon_ops);
		TEST(collapse_empty, mthp_khugepaged_context, anon_ops);
		TEST(collapse_single_mthp, mthp_khugepaged_context, anon_ops);
		TEST(collapse_order_single_window, mthp_khugepaged_context, anon_ops);
		TEST(collapse_order_partial_window, mthp_khugepaged_context, anon_ops);
		TEST(collapse_order_max_ptes_none, mthp_khugepaged_context, anon_ops);
		TEST(collapse_order_mixed_sources, mthp_khugepaged_context, anon_ops);
	}

	TEST(collapse_full, madvise_context, anon_ops);
	TEST(collapse_full, madvise_context, read_only_file_ops);
	TEST(collapse_full, madvise_context, read_write_file_read_ops);
	TEST(collapse_full, madvise_context, read_write_file_write_ops);
	TEST(collapse_full, madvise_context, shmem_ops);

	TEST(collapse_empty, khugepaged_context, anon_ops);
	TEST(collapse_empty, madvise_context, anon_ops);

	TEST(collapse_single_pte_entry, khugepaged_context, anon_ops);
	TEST(collapse_single_pte_entry, khugepaged_context, read_only_file_ops);
	TEST(collapse_single_pte_entry, khugepaged_context, read_write_file_read_ops);
	TEST(collapse_single_pte_entry, khugepaged_context, read_write_file_write_ops);
	TEST(collapse_single_pte_entry, khugepaged_context, shmem_ops);
	TEST(collapse_single_pte_entry, madvise_context, anon_ops);
	TEST(collapse_single_pte_entry, madvise_context, read_only_file_ops);
	TEST(collapse_single_pte_entry, madvise_context, read_write_file_read_ops);
	TEST(collapse_single_pte_entry, madvise_context, read_write_file_write_ops);
	TEST(collapse_single_pte_entry, madvise_context, shmem_ops);

	TEST(collapse_max_ptes_none, khugepaged_context, anon_ops);
	TEST(collapse_max_ptes_none, khugepaged_context, read_only_file_ops);
	TEST(collapse_max_ptes_none, khugepaged_context, read_write_file_read_ops);
	TEST(collapse_max_ptes_none, khugepaged_context, read_write_file_write_ops);
	TEST(collapse_max_ptes_none, madvise_context, anon_ops);
	TEST(collapse_max_ptes_none, madvise_context, read_only_file_ops);
	TEST(collapse_max_ptes_none, madvise_context, read_write_file_read_ops);
	TEST(collapse_max_ptes_none, madvise_context, read_write_file_write_ops);

	TEST(collapse_single_pte_entry_compound, khugepaged_context, anon_ops);
	TEST(collapse_single_pte_entry_compound, khugepaged_context, read_only_file_ops);
	TEST(collapse_single_pte_entry_compound, khugepaged_context, read_write_file_read_ops);
	TEST(collapse_single_pte_entry_compound, madvise_context, anon_ops);
	TEST(collapse_single_pte_entry_compound, madvise_context, read_only_file_ops);
	TEST(collapse_single_pte_entry_compound, madvise_context, read_write_file_read_ops);

	TEST(collapse_full_of_compound, khugepaged_context, anon_ops);
	TEST(collapse_full_of_compound, khugepaged_context, read_only_file_ops);
	TEST(collapse_full_of_compound, khugepaged_context, read_write_file_read_ops);
	TEST(collapse_full_of_compound, khugepaged_context, shmem_ops);
	TEST(collapse_full_of_compound, madvise_context, anon_ops);
	TEST(collapse_full_of_compound, madvise_context, read_only_file_ops);
	TEST(collapse_full_of_compound, madvise_context, read_write_file_read_ops);
	TEST(collapse_full_of_compound, madvise_context, shmem_ops);

	TEST(collapse_compound_extreme, khugepaged_context, anon_ops);
	TEST(collapse_compound_extreme, madvise_context, anon_ops);

	TEST(collapse_swapin_single_pte, khugepaged_context, anon_ops);
	TEST(collapse_swapin_single_pte, madvise_context, anon_ops);

	TEST(collapse_max_ptes_swap, khugepaged_context, anon_ops);
	TEST(collapse_max_ptes_swap, madvise_context, anon_ops);

	TEST(collapse_fork, khugepaged_context, anon_ops);
	TEST(collapse_fork, madvise_context, anon_ops);

	TEST(collapse_fork_compound, khugepaged_context, anon_ops);
	TEST(collapse_fork_compound, madvise_context, anon_ops);

	TEST(collapse_max_ptes_shared, khugepaged_context, anon_ops);
	TEST(collapse_max_ptes_shared, madvise_context, anon_ops);

	TEST(collapse_fork_cow_race, khugepaged_context, anon_ops);
	TEST(collapse_fork_cow_race, madvise_context, anon_ops);

	TEST(madvise_collapse_existing_thps, madvise_context, anon_ops);
	TEST(madvise_collapse_existing_thps, madvise_context, read_only_file_ops);
	TEST(madvise_collapse_existing_thps, madvise_context, read_write_file_read_ops);
	TEST(madvise_collapse_existing_thps, madvise_context, shmem_ops);

	TEST(madvise_retracted_page_tables, madvise_context, read_only_file_ops);
	TEST(madvise_retracted_page_tables, madvise_context, read_write_file_read_ops);
	TEST(madvise_retracted_page_tables, madvise_context, shmem_ops);

	ksft_set_plan(nr_test_cases + 1);

	alloc_at_fault();
	for (int i = 0; i < nr_test_cases; i++) {
		struct test_case *t = &test_cases[i];

		if (t->ctx == &__mthp_khugepaged_context) {
			collapse_order = t->order;
			ksft_print_msg("\n# Run test: %s (%s:%s, order %d)\n",
				       t->desc, t->ctx->name, t->ops->name,
				       t->order);
		} else {
			ksft_print_msg("\n# Run test: %s (%s:%s)\n", t->desc,
				       t->ctx->name, t->ops->name);
		}
		t->fn(t->ctx, t->ops);
	}

	ksft_finished();
}
