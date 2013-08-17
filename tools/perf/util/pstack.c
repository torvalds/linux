/*
 * Simple pointer stack
 *
 * (c) 2010 Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "util.h"
#include "pstack.h"
#include <linux/kernel.h>
#include <stdlib.h>

struct pstack {
	unsigned short	top;
	unsigned short	max_nr_entries;
	void		*entries[0];
};

struct pstack *pstack__new(unsigned short max_nr_entries)
{
	struct pstack *self = zalloc((sizeof(*self) +
				     max_nr_entries * sizeof(void *)));
	if (self != NULL)
		self->max_nr_entries = max_nr_entries;
	return self;
}

void pstack__delete(struct pstack *self)
{
	free(self);
}

bool pstack__empty(const struct pstack *self)
{
	return self->top == 0;
}

void pstack__remove(struct pstack *self, void *key)
{
	unsigned short i = self->top, last_index = self->top - 1;

	while (i-- != 0) {
		if (self->entries[i] == key) {
			if (i < last_index)
				memmove(self->entries + i,
					self->entries + i + 1,
					(last_index - i) * sizeof(void *));
			--self->top;
			return;
		}
	}
	pr_err("%s: %p not on the pstack!\n", __func__, key);
}

void pstack__push(struct pstack *self, void *key)
{
	if (self->top == self->max_nr_entries) {
		pr_err("%s: top=%d, overflow!\n", __func__, self->top);
		return;
	}
	self->entries[self->top++] = key;
}

void *pstack__pop(struct pstack *self)
{
	void *ret;

	if (self->top == 0) {
		pr_err("%s: underflow!\n", __func__);
		return NULL;
	}

	ret = self->entries[--self->top];
	self->entries[self->top] = NULL;
	return ret;
}
