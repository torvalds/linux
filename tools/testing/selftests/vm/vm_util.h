/* SPDX-License-Identifier: GPL-2.0 */
#include <stdint.h>
#include <stdbool.h>

uint64_t pagemap_get_entry(int fd, char *start);
bool pagemap_is_softdirty(int fd, char *start);
void clear_softdirty(void);
uint64_t read_pmd_pagesize(void);
uint64_t check_huge(void *addr);
