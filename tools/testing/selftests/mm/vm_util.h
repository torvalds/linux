/* SPDX-License-Identifier: GPL-2.0 */
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <err.h>
#include <string.h> /* ffsl() */
#include <unistd.h> /* _SC_PAGESIZE */

extern unsigned int __page_size;
extern unsigned int __page_shift;

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

uint64_t pagemap_get_entry(int fd, char *start);
bool pagemap_is_softdirty(int fd, char *start);
bool pagemap_is_swapped(int fd, char *start);
bool pagemap_is_populated(int fd, char *start);
unsigned long pagemap_get_pfn(int fd, char *start);
void clear_softdirty(void);
bool check_for_pattern(FILE *fp, const char *pattern, char *buf, size_t len);
uint64_t read_pmd_pagesize(void);
bool check_huge_anon(void *addr, int nr_hpages, uint64_t hpage_size);
bool check_huge_file(void *addr, int nr_hpages, uint64_t hpage_size);
bool check_huge_shmem(void *addr, int nr_hpages, uint64_t hpage_size);
int64_t allocate_transhuge(void *ptr, int pagemap_fd);
unsigned long default_huge_page_size(void);

/*
 * On ppc64 this will only work with radix 2M hugepage size
 */
#define HPAGE_SHIFT 21
#define HPAGE_SIZE (1 << HPAGE_SHIFT)

#define PAGEMAP_PRESENT(ent)	(((ent) & (1ull << 63)) != 0)
#define PAGEMAP_PFN(ent)	((ent) & ((1ull << 55) - 1))
