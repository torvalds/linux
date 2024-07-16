/* SPDX-License-Identifier: GPL-2.0 */
#include <stdint.h>
#include <stdbool.h>

uint64_t pagemap_get_entry(int fd, char *start);
bool pagemap_is_softdirty(int fd, char *start);
void clear_softdirty(void);
bool check_for_pattern(FILE *fp, const char *pattern, char *buf, size_t len);
uint64_t read_pmd_pagesize(void);
bool check_huge_anon(void *addr, int nr_hpages, uint64_t hpage_size);
bool check_huge_file(void *addr, int nr_hpages, uint64_t hpage_size);
bool check_huge_shmem(void *addr, int nr_hpages, uint64_t hpage_size);
