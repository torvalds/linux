/*
 * Implementation of the symbol table type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "symtab.h"

static unsigned int symhash(struct hashtab *h, void *key)
{
	char *p, *keyp;
	unsigned int size;
	unsigned int val;

	val = 0;
	keyp = key;
	size = strlen(keyp);
	for (p = keyp; (p - keyp) < size; p++)
		val = (val << 4 | (val >> (8*sizeof(unsigned int)-4))) ^ (*p);
	return val & (h->size - 1);
}

static int symcmp(struct hashtab *h, void *key1, void *key2)
{
	char *keyp1, *keyp2;

	keyp1 = key1;
	keyp2 = key2;
	return strcmp(keyp1, keyp2);
}


int symtab_init(struct symtab *s, unsigned int size)
{
	s->table = hashtab_create(symhash, symcmp, size);
	if (!s->table)
		return -1;
	s->nprim = 0;
	return 0;
}

