// SPDX-License-Identifier: GPL-2.0-only
/* Manage a cache of file names' existence */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/list.h>
#include "fncache.h"

struct fncache {
	struct hlist_node nd;
	bool res;
	char name[];
};

#define FNHSIZE 61

static struct hlist_head fncache_hash[FNHSIZE];

unsigned shash(const unsigned char *s)
{
	unsigned h = 0;
	while (*s)
		h = 65599 * h + *s++;
	return h ^ (h >> 16);
}

static bool lookup_fncache(const char *name, bool *res)
{
	int h = shash((const unsigned char *)name) % FNHSIZE;
	struct fncache *n;

	hlist_for_each_entry(n, &fncache_hash[h], nd) {
		if (!strcmp(n->name, name)) {
			*res = n->res;
			return true;
		}
	}
	return false;
}

static void update_fncache(const char *name, bool res)
{
	struct fncache *n = malloc(sizeof(struct fncache) + strlen(name) + 1);
	int h = shash((const unsigned char *)name) % FNHSIZE;

	if (!n)
		return;
	strcpy(n->name, name);
	n->res = res;
	hlist_add_head(&n->nd, &fncache_hash[h]);
}

/* No LRU, only use when bounded in some other way. */
bool file_available(const char *name)
{
	bool res;

	if (lookup_fncache(name, &res))
		return res;
	res = access(name, R_OK) == 0;
	update_fncache(name, res);
	return res;
}
