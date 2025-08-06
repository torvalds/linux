// SPDX-License-Identifier: GPL-2.0-only
/* Manage a cache of file names' existence */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/compiler.h>
#include "fncache.h"
#include "hashmap.h"

static struct hashmap *fncache;

static size_t fncache__hash(long key, void *ctx __maybe_unused)
{
	return str_hash((const char *)key);
}

static bool fncache__equal(long key1, long key2, void *ctx __maybe_unused)
{
	return strcmp((const char *)key1, (const char *)key2) == 0;
}

static void fncache__init(void)
{
	fncache = hashmap__new(fncache__hash, fncache__equal, /*ctx=*/NULL);
}

static struct hashmap *fncache__get(void)
{
	static pthread_once_t fncache_once = PTHREAD_ONCE_INIT;

	pthread_once(&fncache_once, fncache__init);

	return fncache;
}

static bool lookup_fncache(const char *name, bool *res)
{
	long val;

	if (!hashmap__find(fncache__get(), name, &val))
		return false;

	*res = (val != 0);
	return true;
}

static void update_fncache(const char *name, bool res)
{
	char *old_key = NULL, *key = strdup(name);

	if (key) {
		hashmap__set(fncache__get(), key, res, &old_key, /*old_value*/NULL);
		free(old_key);
	}
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
