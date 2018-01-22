// SPDX-License-Identifier: GPL-2.0
/*
 * Simple pointer stack
 *
 * (c) 2010 Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "util.h"
#include "pstack.h"
#include "debug.h"
#include <linux/kernel.h>
#include <stdlib.h>

struct pstack {
	unsigned short	top;
	unsigned short	max_nr_entries;
	void		*entries[0];
};

struct pstack *pstack__new(unsigned short max_nr_entries)
{
	struct pstack *pstack = zalloc((sizeof(*pstack) +
				       max_nr_entries * sizeof(void *)));
	if (pstack != NULL)
		pstack->max_nr_entries = max_nr_entries;
	return pstack;
}

void pstack__delete(struct pstack *pstack)
{
	free(pstack);
}

bool pstack__empty(const struct pstack *pstack)
{
	return pstack->top == 0;
}

void pstack__remove(struct pstack *pstack, void *key)
{
	unsigned short i = pstack->top, last_index = pstack->top - 1;

	while (i-- != 0) {
		if (pstack->entries[i] == key) {
			if (i < last_index)
				memmove(pstack->entries + i,
					pstack->entries + i + 1,
					(last_index - i) * sizeof(void *));
			--pstack->top;
			return;
		}
	}
	pr_err("%s: %p not on the pstack!\n", __func__, key);
}

void pstack__push(struct pstack *pstack, void *key)
{
	if (pstack->top == pstack->max_nr_entries) {
		pr_err("%s: top=%d, overflow!\n", __func__, pstack->top);
		return;
	}
	pstack->entries[pstack->top++] = key;
}

void *pstack__pop(struct pstack *pstack)
{
	void *ret;

	if (pstack->top == 0) {
		pr_err("%s: underflow!\n", __func__);
		return NULL;
	}

	ret = pstack->entries[--pstack->top];
	pstack->entries[pstack->top] = NULL;
	return ret;
}

void *pstack__peek(struct pstack *pstack)
{
	if (pstack->top == 0)
		return NULL;
	return pstack->entries[pstack->top - 1];
}
