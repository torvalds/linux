// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <linux/userfaultfd.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../kselftest.h"
#include "vm_util.h"

#define PMD_SIZE_FILE_PATH "/sys/kernel/mm/transparent_hugepage/hpage_pmd_size"
#define SMAP_FILE_PATH "/proc/self/smaps"
#define STATUS_FILE_PATH "/proc/self/status"
#define MAX_LINE_LENGTH 500

unsigned int __page_size;
unsigned int __page_shift;

uint64_t pagemap_get_entry(int fd, char *start)
{
	const unsigned long pfn = (unsigned long)start / getpagesize();
	uint64_t entry;
	int ret;

	ret = pread(fd, &entry, sizeof(entry), pfn * sizeof(entry));
	if (ret != sizeof(entry))
		ksft_exit_fail_msg("reading pagemap failed\n");
	return entry;
}

static uint64_t __pagemap_scan_get_categories(int fd, char *start, struct page_region *r)
{
	struct pm_scan_arg arg;

	arg.start = (uintptr_t)start;
	arg.end = (uintptr_t)(start + psize());
	arg.vec = (uintptr_t)r;
	arg.vec_len = 1;
	arg.flags = 0;
	arg.size = sizeof(struct pm_scan_arg);
	arg.max_pages = 0;
	arg.category_inverted = 0;
	arg.category_mask = 0;
	arg.category_anyof_mask = PAGE_IS_WPALLOWED | PAGE_IS_WRITTEN | PAGE_IS_FILE |
				  PAGE_IS_PRESENT | PAGE_IS_SWAPPED | PAGE_IS_PFNZERO |
				  PAGE_IS_HUGE | PAGE_IS_SOFT_DIRTY;
	arg.return_mask = arg.category_anyof_mask;

	return ioctl(fd, PAGEMAP_SCAN, &arg);
}

static uint64_t pagemap_scan_get_categories(int fd, char *start)
{
	struct page_region r;
	long ret;

	ret = __pagemap_scan_get_categories(fd, start, &r);
	if (ret < 0)
		ksft_exit_fail_msg("PAGEMAP_SCAN failed: %s\n", strerror(errno));
	if (ret == 0)
		return 0;
	return r.categories;
}

/* `start` is any valid address. */
static bool pagemap_scan_supported(int fd, char *start)
{
	static int supported = -1;
	int ret;

	if (supported != -1)
		return supported;

	/* Provide an invalid address in order to trigger EFAULT. */
	ret = __pagemap_scan_get_categories(fd, start, (struct page_region *) ~0UL);
	if (ret == 0)
		ksft_exit_fail_msg("PAGEMAP_SCAN succeeded unexpectedly\n");

	supported = errno == EFAULT;

	return supported;
}

static bool page_entry_is(int fd, char *start, char *desc,
			  uint64_t pagemap_flags, uint64_t pagescan_flags)
{
	bool m = pagemap_get_entry(fd, start) & pagemap_flags;

	if (pagemap_scan_supported(fd, start)) {
		bool s = pagemap_scan_get_categories(fd, start) & pagescan_flags;

		if (m == s)
			return m;

		ksft_exit_fail_msg(
			"read and ioctl return unmatched results for %s: %d %d", desc, m, s);
	}
	return m;
}

bool pagemap_is_softdirty(int fd, char *start)
{
	return page_entry_is(fd, start, "soft-dirty",
				PM_SOFT_DIRTY, PAGE_IS_SOFT_DIRTY);
}

bool pagemap_is_swapped(int fd, char *start)
{
	return page_entry_is(fd, start, "swap", PM_SWAP, PAGE_IS_SWAPPED);
}

bool pagemap_is_populated(int fd, char *start)
{
	return page_entry_is(fd, start, "populated",
				PM_PRESENT | PM_SWAP,
				PAGE_IS_PRESENT | PAGE_IS_SWAPPED);
}

unsigned long pagemap_get_pfn(int fd, char *start)
{
	uint64_t entry = pagemap_get_entry(fd, start);

	/* If present (63th bit), PFN is at bit 0 -- 54. */
	if (entry & PM_PRESENT)
		return entry & 0x007fffffffffffffull;
	return -1ul;
}

void clear_softdirty(void)
{
	int ret;
	const char *ctrl = "4";
	int fd = open("/proc/self/clear_refs", O_WRONLY);

	if (fd < 0)
		ksft_exit_fail_msg("opening clear_refs failed\n");
	ret = write(fd, ctrl, strlen(ctrl));
	close(fd);
	if (ret != (signed int)strlen(ctrl))
		ksft_exit_fail_msg("writing clear_refs failed\n");
}

bool check_for_pattern(FILE *fp, const char *pattern, char *buf, size_t len)
{
	while (fgets(buf, len, fp)) {
		if (!strncmp(buf, pattern, strlen(pattern)))
			return true;
	}
	return false;
}

uint64_t read_pmd_pagesize(void)
{
	int fd;
	char buf[20];
	ssize_t num_read;

	fd = open(PMD_SIZE_FILE_PATH, O_RDONLY);
	if (fd == -1)
		return 0;

	num_read = read(fd, buf, 19);
	if (num_read < 1) {
		close(fd);
		return 0;
	}
	buf[num_read] = '\0';
	close(fd);

	return strtoul(buf, NULL, 10);
}

unsigned long rss_anon(void)
{
	unsigned long rss_anon = 0;
	FILE *fp;
	char buffer[MAX_LINE_LENGTH];

	fp = fopen(STATUS_FILE_PATH, "r");
	if (!fp)
		ksft_exit_fail_msg("%s: Failed to open file %s\n", __func__, STATUS_FILE_PATH);

	if (!check_for_pattern(fp, "RssAnon:", buffer, sizeof(buffer)))
		goto err_out;

	if (sscanf(buffer, "RssAnon:%10lu kB", &rss_anon) != 1)
		ksft_exit_fail_msg("Reading status error\n");

err_out:
	fclose(fp);
	return rss_anon;
}

char *__get_smap_entry(void *addr, const char *pattern, char *buf, size_t len)
{
	int ret;
	FILE *fp;
	char *entry = NULL;
	char addr_pattern[MAX_LINE_LENGTH];

	ret = snprintf(addr_pattern, MAX_LINE_LENGTH, "%08lx-",
		       (unsigned long) addr);
	if (ret >= MAX_LINE_LENGTH)
		ksft_exit_fail_msg("%s: Pattern is too long\n", __func__);

	fp = fopen(SMAP_FILE_PATH, "r");
	if (!fp)
		ksft_exit_fail_msg("%s: Failed to open file %s\n", __func__, SMAP_FILE_PATH);

	if (!check_for_pattern(fp, addr_pattern, buf, len))
		goto err_out;

	/* Fetch the pattern in the same block */
	if (!check_for_pattern(fp, pattern, buf, len))
		goto err_out;

	/* Trim trailing newline */
	entry = strchr(buf, '\n');
	if (entry)
		*entry = '\0';

	entry = buf + strlen(pattern);

err_out:
	fclose(fp);
	return entry;
}

bool __check_huge(void *addr, char *pattern, int nr_hpages,
		  uint64_t hpage_size)
{
	char buffer[MAX_LINE_LENGTH];
	uint64_t thp = -1;
	char *entry;

	entry = __get_smap_entry(addr, pattern, buffer, sizeof(buffer));
	if (!entry)
		goto err_out;

	if (sscanf(entry, "%9" SCNu64 " kB", &thp) != 1)
		ksft_exit_fail_msg("Reading smap error\n");

err_out:
	return thp == (nr_hpages * (hpage_size >> 10));
}

bool check_huge_anon(void *addr, int nr_hpages, uint64_t hpage_size)
{
	return __check_huge(addr, "AnonHugePages: ", nr_hpages, hpage_size);
}

bool check_huge_file(void *addr, int nr_hpages, uint64_t hpage_size)
{
	return __check_huge(addr, "FilePmdMapped:", nr_hpages, hpage_size);
}

bool check_huge_shmem(void *addr, int nr_hpages, uint64_t hpage_size)
{
	return __check_huge(addr, "ShmemPmdMapped:", nr_hpages, hpage_size);
}

int64_t allocate_transhuge(void *ptr, int pagemap_fd)
{
	uint64_t ent[2];

	/* drop pmd */
	if (mmap(ptr, HPAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_FIXED | MAP_ANONYMOUS |
		 MAP_NORESERVE | MAP_PRIVATE, -1, 0) != ptr)
		ksft_exit_fail_msg("mmap transhuge\n");

	if (madvise(ptr, HPAGE_SIZE, MADV_HUGEPAGE))
		ksft_exit_fail_msg("MADV_HUGEPAGE\n");

	/* allocate transparent huge page */
	*(volatile void **)ptr = ptr;

	if (pread(pagemap_fd, ent, sizeof(ent),
		  (uintptr_t)ptr >> (pshift() - 3)) != sizeof(ent))
		ksft_exit_fail_msg("read pagemap\n");

	if (PAGEMAP_PRESENT(ent[0]) && PAGEMAP_PRESENT(ent[1]) &&
	    PAGEMAP_PFN(ent[0]) + 1 == PAGEMAP_PFN(ent[1]) &&
	    !(PAGEMAP_PFN(ent[0]) & ((1 << (HPAGE_SHIFT - pshift())) - 1)))
		return PAGEMAP_PFN(ent[0]);

	return -1;
}

unsigned long default_huge_page_size(void)
{
	unsigned long hps = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return 0;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "Hugepagesize:       %lu kB", &hps) == 1) {
			hps <<= 10;
			break;
		}
	}

	free(line);
	fclose(f);
	return hps;
}

int detect_hugetlb_page_sizes(size_t sizes[], int max)
{
	DIR *dir = opendir("/sys/kernel/mm/hugepages/");
	int count = 0;

	if (!dir)
		return 0;

	while (count < max) {
		struct dirent *entry = readdir(dir);
		size_t kb;

		if (!entry)
			break;
		if (entry->d_type != DT_DIR)
			continue;
		if (sscanf(entry->d_name, "hugepages-%zukB", &kb) != 1)
			continue;
		sizes[count++] = kb * 1024;
		ksft_print_msg("[INFO] detected hugetlb page size: %zu KiB\n",
			       kb);
	}
	closedir(dir);
	return count;
}

int pageflags_get(unsigned long pfn, int kpageflags_fd, uint64_t *flags)
{
	size_t count;

	count = pread(kpageflags_fd, flags, sizeof(*flags),
		      pfn * sizeof(*flags));

	if (count != sizeof(*flags))
		return -1;

	return 0;
}

/* If `ioctls' non-NULL, the allowed ioctls will be returned into the var */
int uffd_register_with_ioctls(int uffd, void *addr, uint64_t len,
			      bool miss, bool wp, bool minor, uint64_t *ioctls)
{
	struct uffdio_register uffdio_register = { 0 };
	uint64_t mode = 0;
	int ret = 0;

	if (miss)
		mode |= UFFDIO_REGISTER_MODE_MISSING;
	if (wp)
		mode |= UFFDIO_REGISTER_MODE_WP;
	if (minor)
		mode |= UFFDIO_REGISTER_MODE_MINOR;

	uffdio_register.range.start = (unsigned long)addr;
	uffdio_register.range.len = len;
	uffdio_register.mode = mode;

	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
		ret = -errno;
	else if (ioctls)
		*ioctls = uffdio_register.ioctls;

	return ret;
}

int uffd_register(int uffd, void *addr, uint64_t len,
		  bool miss, bool wp, bool minor)
{
	return uffd_register_with_ioctls(uffd, addr, len,
					 miss, wp, minor, NULL);
}

int uffd_unregister(int uffd, void *addr, uint64_t len)
{
	struct uffdio_range range = { .start = (uintptr_t)addr, .len = len };
	int ret = 0;

	if (ioctl(uffd, UFFDIO_UNREGISTER, &range) == -1)
		ret = -errno;

	return ret;
}

unsigned long get_free_hugepages(void)
{
	unsigned long fhp = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return fhp;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "HugePages_Free:      %lu", &fhp) == 1)
			break;
	}

	free(line);
	fclose(f);
	return fhp;
}

static bool check_vmflag(void *addr, const char *flag)
{
	char buffer[MAX_LINE_LENGTH];
	const char *flags;
	size_t flaglen;

	flags = __get_smap_entry(addr, "VmFlags:", buffer, sizeof(buffer));
	if (!flags)
		ksft_exit_fail_msg("%s: No VmFlags for %p\n", __func__, addr);

	while (true) {
		flags += strspn(flags, " ");

		flaglen = strcspn(flags, " ");
		if (!flaglen)
			return false;

		if (flaglen == strlen(flag) && !memcmp(flags, flag, flaglen))
			return true;

		flags += flaglen;
	}
}

bool check_vmflag_io(void *addr)
{
	return check_vmflag(addr, "io");
}

bool check_vmflag_pfnmap(void *addr)
{
	return check_vmflag(addr, "pf");
}

bool softdirty_supported(void)
{
	char *addr;
	bool supported = false;
	const size_t pagesize = getpagesize();

	/* New mappings are expected to be marked with VM_SOFTDIRTY (sd). */
	addr = mmap(0, pagesize, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (!addr)
		ksft_exit_fail_msg("mmap failed\n");

	supported = check_vmflag(addr, "sd");
	munmap(addr, pagesize);
	return supported;
}

/*
 * Open an fd at /proc/$pid/maps and configure procmap_out ready for
 * PROCMAP_QUERY query. Returns 0 on success, or an error code otherwise.
 */
int open_procmap(pid_t pid, struct procmap_fd *procmap_out)
{
	char path[256];
	int ret = 0;

	memset(procmap_out, '\0', sizeof(*procmap_out));
	sprintf(path, "/proc/%d/maps", pid);
	procmap_out->query.size = sizeof(procmap_out->query);
	procmap_out->fd = open(path, O_RDONLY);
	if (procmap_out->fd < 0)
		ret = -errno;

	return ret;
}

/* Perform PROCMAP_QUERY. Returns 0 on success, or an error code otherwise. */
int query_procmap(struct procmap_fd *procmap)
{
	int ret = 0;

	if (ioctl(procmap->fd, PROCMAP_QUERY, &procmap->query) == -1)
		ret = -errno;

	return ret;
}

/*
 * Try to find the VMA at specified address, returns true if found, false if not
 * found, and the test is failed if any other error occurs.
 *
 * On success, procmap->query is populated with the results.
 */
bool find_vma_procmap(struct procmap_fd *procmap, void *address)
{
	int err;

	procmap->query.query_flags = 0;
	procmap->query.query_addr = (unsigned long)address;
	err = query_procmap(procmap);
	if (!err)
		return true;

	if (err != -ENOENT)
		ksft_exit_fail_msg("%s: Error %d on ioctl(PROCMAP_QUERY)\n",
				   __func__, err);
	return false;
}

/*
 * Close fd used by PROCMAP_QUERY mechanism. Returns 0 on success, or an error
 * code otherwise.
 */
int close_procmap(struct procmap_fd *procmap)
{
	return close(procmap->fd);
}

int write_sysfs(const char *file_path, unsigned long val)
{
	FILE *f = fopen(file_path, "w");

	if (!f) {
		fprintf(stderr, "f %s\n", file_path);
		perror("fopen");
		return 1;
	}
	if (fprintf(f, "%lu", val) < 0) {
		perror("fprintf");
		fclose(f);
		return 1;
	}
	fclose(f);

	return 0;
}

int read_sysfs(const char *file_path, unsigned long *val)
{
	FILE *f = fopen(file_path, "r");

	if (!f) {
		fprintf(stderr, "f %s\n", file_path);
		perror("fopen");
		return 1;
	}
	if (fscanf(f, "%lu", val) != 1) {
		perror("fscanf");
		fclose(f);
		return 1;
	}
	fclose(f);

	return 0;
}

void *sys_mremap(void *old_address, unsigned long old_size,
		 unsigned long new_size, int flags, void *new_address)
{
	return (void *)syscall(__NR_mremap, (unsigned long)old_address,
			       old_size, new_size, flags,
			       (unsigned long)new_address);
}

bool detect_huge_zeropage(void)
{
	int fd = open("/sys/kernel/mm/transparent_hugepage/use_zero_page",
		      O_RDONLY);
	bool enabled = 0;
	char buf[15];
	int ret;

	if (fd < 0)
		return 0;

	ret = pread(fd, buf, sizeof(buf), 0);
	if (ret > 0 && ret < sizeof(buf)) {
		buf[ret] = 0;

		if (strtoul(buf, NULL, 10) == 1)
			enabled = 1;
	}

	close(fd);
	return enabled;
}

long ksm_get_self_zero_pages(void)
{
	int proc_self_ksm_stat_fd;
	char buf[200];
	char *substr_ksm_zero;
	size_t value_pos;
	ssize_t read_size;

	proc_self_ksm_stat_fd = open("/proc/self/ksm_stat", O_RDONLY);
	if (proc_self_ksm_stat_fd < 0)
		return -errno;

	read_size = pread(proc_self_ksm_stat_fd, buf, sizeof(buf) - 1, 0);
	close(proc_self_ksm_stat_fd);
	if (read_size < 0)
		return -errno;

	buf[read_size] = 0;

	substr_ksm_zero = strstr(buf, "ksm_zero_pages");
	if (!substr_ksm_zero)
		return 0;

	value_pos = strcspn(substr_ksm_zero, "0123456789");
	return strtol(substr_ksm_zero + value_pos, NULL, 10);
}

long ksm_get_self_merging_pages(void)
{
	int proc_self_ksm_merging_pages_fd;
	char buf[10];
	ssize_t ret;

	proc_self_ksm_merging_pages_fd = open("/proc/self/ksm_merging_pages",
						O_RDONLY);
	if (proc_self_ksm_merging_pages_fd < 0)
		return -errno;

	ret = pread(proc_self_ksm_merging_pages_fd, buf, sizeof(buf) - 1, 0);
	close(proc_self_ksm_merging_pages_fd);
	if (ret <= 0)
		return -errno;
	buf[ret] = 0;

	return strtol(buf, NULL, 10);
}

long ksm_get_full_scans(void)
{
	int ksm_full_scans_fd;
	char buf[10];
	ssize_t ret;

	ksm_full_scans_fd = open("/sys/kernel/mm/ksm/full_scans", O_RDONLY);
	if (ksm_full_scans_fd < 0)
		return -errno;

	ret = pread(ksm_full_scans_fd, buf, sizeof(buf) - 1, 0);
	close(ksm_full_scans_fd);
	if (ret <= 0)
		return -errno;
	buf[ret] = 0;

	return strtol(buf, NULL, 10);
}

int ksm_use_zero_pages(void)
{
	int ksm_use_zero_pages_fd;
	ssize_t ret;

	ksm_use_zero_pages_fd = open("/sys/kernel/mm/ksm/use_zero_pages", O_RDWR);
	if (ksm_use_zero_pages_fd < 0)
		return -errno;

	ret = write(ksm_use_zero_pages_fd, "1", 1);
	close(ksm_use_zero_pages_fd);
	return ret == 1 ? 0 : -errno;
}

int ksm_start(void)
{
	int ksm_fd;
	ssize_t ret;
	long start_scans, end_scans;

	ksm_fd = open("/sys/kernel/mm/ksm/run", O_RDWR);
	if (ksm_fd < 0)
		return -errno;

	/* Wait for two full scans such that any possible merging happened. */
	start_scans = ksm_get_full_scans();
	if (start_scans < 0) {
		close(ksm_fd);
		return start_scans;
	}
	ret = write(ksm_fd, "1", 1);
	close(ksm_fd);
	if (ret != 1)
		return -errno;
	do {
		end_scans = ksm_get_full_scans();
		if (end_scans < 0)
			return end_scans;
	} while (end_scans < start_scans + 2);

	return 0;
}

int ksm_stop(void)
{
	int ksm_fd;
	ssize_t ret;

	ksm_fd = open("/sys/kernel/mm/ksm/run", O_RDWR);
	if (ksm_fd < 0)
		return -errno;

	ret = write(ksm_fd, "2", 1);
	close(ksm_fd);
	return ret == 1 ? 0 : -errno;
}
