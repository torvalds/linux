// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the symbol table type.
 *
 * Author : Stephen Smalley, <stephen.smalley.work@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "symtab.h"

static unsigned int symhash(const void *key)
{
	/*
	 * djb2a
	 * Public domain from cdb v0.75
	 */
	unsigned int hash = 5381;
	unsigned char c;

	while ((c = *(const unsigned char *)key++))
		hash = ((hash << 5) + hash) ^ c;

	return hash;
}

static int symcmp(const void *key1, const void *key2)
{
	const char *keyp1, *keyp2;

	keyp1 = key1;
	keyp2 = key2;
	return strcmp(keyp1, keyp2);
}

static const struct hashtab_key_params symtab_key_params = {
	.hash = symhash,
	.cmp = symcmp,
};

int symtab_init(struct symtab *s, u32 size)
{
	s->nprim = 0;
	return hashtab_init(&s->table, size);
}

int symtab_insert(struct symtab *s, char *name, void *datum)
{
	return hashtab_insert(&s->table, name, datum, symtab_key_params);
}

void *symtab_search(struct symtab *s, const char *name)
{
	return hashtab_search(&s->table, name, symtab_key_params);
}
