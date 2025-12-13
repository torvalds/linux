/* SPDX-License-Identifier: GPL-2.0 */
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <err.h>
#include <stdarg.h>
#include <strings.h> /* ffsl() */
#include <unistd.h> /* _SC_PAGESIZE */
#include "../kselftest.h"
#include <linux/fs.h>

#define BIT_ULL(nr)                   (1ULL << (nr))
#define PM_SOFT_DIRTY                 BIT_ULL(55)
#define PM_MMAP_EXCLUSIVE             BIT_ULL(56)
#define PM_UFFD_WP                    BIT_ULL(57)
#define PM_GUARD_REGION               BIT_ULL(58)
#define PM_FILE                       BIT_ULL(61)
#define PM_SWAP                       BIT_ULL(62)
#define PM_PRESENT                    BIT_ULL(63)

#define KPF_COMPOUND_HEAD             BIT_ULL(15)
#define KPF_COMPOUND_TAIL             BIT_ULL(16)
#define KPF_THP                       BIT_ULL(22)
/*
 * Ignore the checkpatch warning, we must read from x but don't want to do
 * anything with it in order to trigger a read page fault. We therefore must use
 * volatile to stop the compiler from optimising this away.
 */
#define FORCE_READ(x) (*(const volatile typeof(x) *)&(x))

extern unsigned int __page_size;
extern unsigned int __page_shift;

/*
 * Represents an open fd and PROCMAP_QUERY state for binary (via ioctl)
 * /proc/$pid/[s]maps lookup.
 */
struct procmap_fd {
	int fd;
	struct procmap_query query;
};

static inline unsigned int psize(void)
{
	if (!__page_size)
		__page_size = sysconf(_SC_PAGESIZE);
	return __page_size;
}

static inline unsigned int pshift(void)
{
	if (!__page_shift)
		__page_shift = (ffsl(psize()) - 1);
	return __page_shift;
}

bool detect_huge_zeropage(void);

/*
 * Plan 9 FS has bugs (at least on QEMU) where certain operations fail with
 * ENOENT on unlinked files. See
 * https://gitlab.com/qemu-project/qemu/-/issues/103 for some info about such
 * bugs. There are rumours of NFS implementations with similar bugs.
 *
 * Ideally, tests should just detect filesystems known to have such issues and
 * bail early. But 9pfs has the additional "feature" that it causes fstatfs to
 * pass through the f_type field from the host filesystem. To avoid having to
 * scrape /proc/mounts or some other hackery, tests can call this function when
 * it seems such a bug might have been encountered.
 */
static inline void skip_test_dodgy_fs(const char *op_name)
{
	ksft_test_result_skip("%s failed with ENOENT. Filesystem might be buggy (9pfs?)\n", op_name);
}

uint64_t pagemap_get_entry(int fd, char *start);
bool pagemap_is_softdirty(int fd, char *start);
bool pagemap_is_swapped(int fd, char *start);
bool pagemap_is_populated(int fd, char *start);
unsigned long pagemap_get_pfn(int fd, char *start);
void clear_softdirty(void);
bool check_for_pattern(FILE *fp, const char *pattern, char *buf, size_t len);
uint64_t read_pmd_pagesize(void);
unsigned long rss_anon(void);
bool check_huge_anon(void *addr, int nr_hpages, uint64_t hpage_size);
bool check_huge_file(void *addr, int nr_hpages, uint64_t hpage_size);
bool check_huge_shmem(void *addr, int nr_hpages, uint64_t hpage_size);
int64_t allocate_transhuge(void *ptr, int pagemap_fd);
unsigned long default_huge_page_size(void);
int detect_hugetlb_page_sizes(size_t sizes[], int max);
int pageflags_get(unsigned long pfn, int kpageflags_fd, uint64_t *flags);

int uffd_register(int uffd, void *addr, uint64_t len,
		  bool miss, bool wp, bool minor);
int uffd_unregister(int uffd, void *addr, uint64_t len);
int uffd_register_with_ioctls(int uffd, void *addr, uint64_t len,
			      bool miss, bool wp, bool minor, uint64_t *ioctls);
unsigned long get_free_hugepages(void);
bool check_vmflag_io(void *addr);
bool check_vmflag_pfnmap(void *addr);
int open_procmap(pid_t pid, struct procmap_fd *procmap_out);
int query_procmap(struct procmap_fd *procmap);
bool find_vma_procmap(struct procmap_fd *procmap, void *address);
int close_procmap(struct procmap_fd *procmap);
int write_sysfs(const char *file_path, unsigned long val);
int read_sysfs(const char *file_path, unsigned long *val);
bool softdirty_supported(void);

static inline int open_self_procmap(struct procmap_fd *procmap_out)
{
	pid_t pid = getpid();

	return open_procmap(pid, procmap_out);
}

/* These helpers need to be inline to match the kselftest.h idiom. */
static char test_name[1024];

static inline void log_test_start(const char *name, ...)
{
	va_list args;
	va_start(args, name);

	vsnprintf(test_name, sizeof(test_name), name, args);
	ksft_print_msg("[RUN] %s\n", test_name);

	va_end(args);
}

static inline void log_test_result(int result)
{
	ksft_test_result_report(result, "%s\n", test_name);
}

static inline int sz2ord(size_t size, size_t pagesize)
{
	return __builtin_ctzll(size / pagesize);
}

void *sys_mremap(void *old_address, unsigned long old_size,
		 unsigned long new_size, int flags, void *new_address);

long ksm_get_self_zero_pages(void);
long ksm_get_self_merging_pages(void);
long ksm_get_full_scans(void);
int ksm_use_zero_pages(void);
int ksm_start(void);
int ksm_stop(void);

/*
 * On ppc64 this will only work with radix 2M hugepage size
 */
#define HPAGE_SHIFT 21
#define HPAGE_SIZE (1 << HPAGE_SHIFT)

#define PAGEMAP_PRESENT(ent)	(((ent) & (1ull << 63)) != 0)
#define PAGEMAP_PFN(ent)	((ent) & ((1ull << 55) - 1))
