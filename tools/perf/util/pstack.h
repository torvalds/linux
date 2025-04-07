/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_PSTACK_
#define _PERF_PSTACK_

#include <stdbool.h>

struct pstack;
struct pstack *pstack__new(unsigned short max_nr_entries);
void pstack__delete(struct pstack *pstack);
bool pstack__empty(const struct pstack *pstack);
void pstack__remove(struct pstack *pstack, void *key);
void pstack__push(struct pstack *pstack, void *key);
void *pstack__peek(struct pstack *pstack);

#endif /* _PERF_PSTACK_ */
