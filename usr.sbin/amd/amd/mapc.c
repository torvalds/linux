/*	$OpenBSD: mapc.c,v 1.24 2021/10/21 10:55:56 deraadt Exp $	*/

/*-
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Mount map cache
 */

#include "am.h"
#include <regex.h>

/*
 * Hash table size
 */
#define	NKVHASH	(1 << 2)		/* Power of two */

/*
 * Wildcard key
 */
static char wildcard[] = "*";

/*
 * Map cache types
 * default, none, incremental, all, regexp
 * MAPC_RE implies MAPC_ALL and must be numerically
 * greater.
 */
#define	MAPC_DFLT	0x000
#define	MAPC_NONE	0x001
#define	MAPC_INC	0x002
#define	MAPC_ROOT	0x004
#define	MAPC_ALL	0x010
#define MAPC_RE		0x020
#define	MAPC_ISRE(m) ((m)->alloc == MAPC_RE)
#define	MAPC_CACHE_MASK	0x0ff
#define	MAPC_SYNC	0x100

static struct opt_tab mapc_opt[] = {
	{ "all", MAPC_ALL },
	{ "default", MAPC_DFLT },
	{ "inc", MAPC_INC },
	{ "mapdefault", MAPC_DFLT },
	{ "none", MAPC_NONE },
	{ "re", MAPC_RE },
	{ "regexp", MAPC_RE },
	{ "sync", MAPC_SYNC },
	{ 0, 0 }
};

/*
 * Lookup recursion
 */
#define	MREC_FULL	2
#define	MREC_PART	1
#define	MREC_NONE	0

/*
 * Cache map operations
 */
typedef void	add_fn(mnt_map *, char *, char *);
typedef int	init_fn(char *, time_t *);
typedef int	search_fn(mnt_map *, char *, char *, char **, time_t *);
typedef int	reload_fn(mnt_map *, char *, add_fn *);
typedef int	mtime_fn(char *, time_t *);

static void	mapc_sync(mnt_map *);

/*
 * Map type
 */
typedef struct map_type map_type;
struct map_type {
	char *name;			/* Name of this map type */
	init_fn *init;			/* Initialisation */
	reload_fn *reload;		/* Reload or fill */
	search_fn *search;		/* Search for new entry */
	mtime_fn *mtime;		/* Find modify time */
	int def_alloc;			/* Default allocation mode */
};

/*
 * Key-value pair
 */
typedef struct kv kv;
struct kv {
	kv *next;
	char *key;
	char *val;
};

struct mnt_map {
	qelem hdr;
	int refc;			/* Reference count */
	short flags;			/* Allocation flags */
	short alloc;			/* Allocation mode */
	time_t modify;			/* Modify time of map */
	char *map_name;			/* Name of this map */
	char *wildcard;			/* Wildcard value */
	reload_fn *reload;		/* Function to be used for reloads */
	search_fn *search;		/* Function to be used for searching */
	mtime_fn *mtime;		/* Modify time function */
	kv *kvhash[NKVHASH];		/* Cached data */
};

/*
 * Map for root node
 */
static mnt_map *root_map;

/*
 * List of known maps
 */
extern qelem map_list_head;
qelem map_list_head = { &map_list_head, &map_list_head };

/*
 * Configuration
 */

/* ROOT MAP */
static int	root_init(char *, time_t *);

/* FILE MAPS */
extern int	file_init(char *, time_t *);
extern int	file_reload(mnt_map *, char *, add_fn *);
extern int	file_search(mnt_map *, char *, char *, char **, time_t *);
extern int	file_mtime(char *, time_t *);

/* Network Information Service (NIS) MAPS */
extern int	nis_init(char *, time_t *);
extern int	nis_reload(mnt_map *, char *, add_fn *);
extern int	nis_search(mnt_map *, char *, char *, char **, time_t *);
#define nis_mtime nis_init

/* NDBM MAPS */
#ifdef HAS_NDBM_MAPS
extern int	ndbm_init(char *, time_t *);
extern int	ndbm_search(mnt_map *, char *, charo *, char **, time_t *);
#define ndbm_mtime ndbm_init
#endif /* HAS_NDBM_MAPS */

/* PASSWD MAPS */
extern int	passwd_init(char *, time_t *);
extern int	passwd_search(mnt_map *, char *, char *, char **, time_t *);

/* UNION MAPS */
extern int	union_init(char *, time_t *);
extern int	union_search(mnt_map *, char *, char *, char **, time_t *);
extern int	union_reload(mnt_map *, char *, add_fn *);

/* ERROR MAP */
static int	error_init(char *, time_t *);
static int	error_reload(mnt_map *, char *, add_fn *);
static int	error_search(mnt_map *, char *, char *, char **, time_t *);
static int	error_mtime(char *, time_t *);

static map_type maptypes[] = {
	{ "root", root_init, error_reload, error_search, error_mtime, MAPC_ROOT },

	{ "passwd", passwd_init, error_reload, passwd_search, error_mtime, MAPC_INC },

	{ "union", union_init, union_reload, union_search, error_mtime, MAPC_ALL },

	{ "nis", nis_init, nis_reload, nis_search, nis_mtime, MAPC_INC },

#ifdef HAS_NDBM_MAPS
	{ "ndbm", ndbm_init, error_reload, ndbm_search, ndbm_mtime, MAPC_INC },
#endif

	{ "file", file_init, file_reload, file_search, file_mtime, MAPC_ALL },

	{ "error", error_init, error_reload, error_search, error_mtime, MAPC_NONE },
};

/*
 * Hash function
 */
static unsigned int
kvhash_of(char *key)
{
	unsigned int i, j;

	for (i = 0; (j = *key++); i += j)
		;

	return i % NKVHASH;
}

void
mapc_showtypes(FILE *fp)
{
	map_type *mt;
	char *sep = "";

	for (mt = maptypes; mt < maptypes+sizeof(maptypes)/sizeof(maptypes[0]); mt++) {
		fprintf(fp, "%s%s", sep, mt->name);
		sep = ", ";
	}
}

/*
 * Add key and val to the map m.
 * key and val are assumed to be safe copies
 */
void
mapc_add_kv(mnt_map *m, char *key, char *val)
{
	kv **h;
	kv *n;
	int hash = kvhash_of(key);

#ifdef DEBUG
	dlog("add_kv: %s -> %s", key, val);
#endif

	if (MAPC_ISRE(m)) {
		char keyb[PATH_MAX];
		regex_t *re;
		int err;

		/*
		 * Make sure the string is bound to the start and end
		 */
		snprintf(keyb, sizeof(keyb), "^%s$", key);
		re = malloc(sizeof(*re));
		if (re == NULL) {
			plog(XLOG_USER, "error allocating RE \"%s\"", keyb);
			return;
		}
		err = regcomp(re, keyb, 0);
		if (err) {
			char errbuf[100];

			regerror(err, re, errbuf, sizeof errbuf);
			free(re);
			plog(XLOG_USER, "error compiling RE \"%s\": %s",
			    keyb, errbuf);
			return;
		}

		free(key);
		key = (char *)re;
	}

	h = &m->kvhash[hash];
	n = ALLOC(kv);
	n->key = key;
	n->val = val;
	n->next = *h;
	*h = n;
}

static void
mapc_repl_kv(mnt_map *m, char *key, char *val)
{
	kv *k;

	/*
	 * Compute the hash table offset
	 */
	k = m->kvhash[kvhash_of(key)];

	/*
	 * Scan the linked list for the key
	 */
	while (k && !FSTREQ(k->key, key))
		k = k->next;

	if (k) {
		free(k->val);
		k->val = val;
	} else {
		mapc_add_kv(m, key, val);
	}

}

/*
 * Search a map for a key.
 * Calls map specific search routine.
 * While map is out of date, keep re-syncing.
 */
static int search_map(mnt_map *m, char *key, char **valp)
{
	int rc;

	do {
		rc = (*m->search)(m, m->map_name, key, valp, &m->modify);
		if (rc < 0) {
			plog(XLOG_MAP, "Re-synchronizing cache for map %s", m->map_name);
			mapc_sync(m);
		}
	} while (rc < 0);

	return rc;
}

/*
 * Do a wildcard lookup in the map and
 * save the result.
 */
static void
mapc_find_wildcard(mnt_map *m)
{
	/*
	 * Attempt to find the wildcard entry
	 */
	int rc = search_map(m, wildcard, &m->wildcard);

	if (rc != 0)
		m->wildcard = 0;
}

/*
 * Make a duplicate reference to an existing map
 */
#define mapc_dup(m) ((m)->refc++, (m))

/*
 * Do a map reload
 */
static int
mapc_reload_map(mnt_map *m)
{
	int error;
#ifdef DEBUG
	dlog("calling map reload on %s", m->map_name);
#endif
	error = (*m->reload)(m, m->map_name, mapc_add_kv);
	if (error)
		return error;
	m->wildcard = 0;
#ifdef DEBUG
	dlog("calling mapc_search for wildcard");
#endif
	error = mapc_search(m, wildcard, &m->wildcard);
	if (error)
		m->wildcard = 0;
	return 0;
}

/*
 * Create a new map
 */
static mnt_map *
mapc_create(char *map, char *opt)
{
	mnt_map *m = ALLOC(mnt_map);
	map_type *mt;
	time_t modify;
	int alloc = 0;

	(void) cmdoption(opt, mapc_opt, &alloc);

	for (mt = maptypes; mt < maptypes+sizeof(maptypes)/sizeof(maptypes[0]); mt++)
		if ((*mt->init)(map, &modify) == 0)
			break;
	/* assert: mt in maptypes */

	m->flags = alloc & ~MAPC_CACHE_MASK;
	alloc &= MAPC_CACHE_MASK;

	if (alloc == MAPC_DFLT)
		alloc = mt->def_alloc;
	switch (alloc) {
	default:
		plog(XLOG_USER, "Ambiguous map cache type \"%s\"; using \"inc\"", opt);
		alloc = MAPC_INC;
		/* fallthrough... */
	case MAPC_NONE:
	case MAPC_INC:
	case MAPC_ROOT:
		break;
	case MAPC_ALL:
		/*
		 * If there is no support for reload and it was requested
		 * then back off to incremental instead.
		 */
		if (mt->reload == error_reload) {
			plog(XLOG_WARNING, "Map type \"%s\" does not support cache type \"all\"; using \"inc\"", mt->name);
			alloc = MAPC_INC;
		}
		break;
	case MAPC_RE:
		if (mt->reload == error_reload) {
			plog(XLOG_WARNING, "Map type \"%s\" does not support cache type \"re\"", mt->name);
			mt = &maptypes[sizeof(maptypes)/sizeof(maptypes[0]) - 1];
			/* assert: mt->name == "error" */
		}
		break;
	}

#ifdef DEBUG
	dlog("Map for %s coming from maptype %s", map, mt->name);
#endif

	m->alloc = alloc;
	m->reload = mt->reload;
	m->modify = modify;
	m->search = alloc >= MAPC_ALL ? error_search : mt->search;
	m->mtime = mt->mtime;
	bzero(m->kvhash, sizeof(m->kvhash));
	m->map_name = strdup(map);
	m->refc = 1;
	m->wildcard = 0;

	/*
	 * synchronize cache with reality
	 */
	mapc_sync(m);

	return m;
}

/*
 * Free the cached data in a map
 */
static void
mapc_clear(mnt_map *m)
{
	int i;

	/*
	 * For each of the hash slots, chain
	 * along free'ing the data.
	 */
	for (i = 0; i < NKVHASH; i++) {
		kv *k = m->kvhash[i];
		while (k) {
			kv *n = k->next;
			free(k->key);
			free(k->val);
			free(k);
			k = n;
		}
	}
	/*
	 * Zero the hash slots
	 */
	bzero(m->kvhash, sizeof(m->kvhash));
	/*
	 * Free the wildcard if it exists
	 */
	if (m->wildcard) {
		free(m->wildcard);
		m->wildcard = 0;
	}
}

/*
 * Find a map, or create one if it does not exist
 */
mnt_map *
mapc_find(char *map, char *opt)
{
	mnt_map *m;

	/*
	 * Search the list of known maps to see if
	 * it has already been loaded.  If it is found
	 * then return a duplicate reference to it.
	 * Otherwise make a new map as required and
	 * add it to the list of maps
	 */
	ITER(m, mnt_map, &map_list_head)
		if (STREQ(m->map_name, map))
			return mapc_dup(m);

	m = mapc_create(map, opt);
	ins_que(&m->hdr, &map_list_head);
	return m;
}

/*
 * Free a map.
 */
void
mapc_free(void *arg)
{
	mnt_map *m = arg;
	/*
	 * Decrement the reference count.
	 * If the reference count hits zero
	 * then throw the map away.
	 */
	if (m && --m->refc == 0) {
		mapc_clear(m);
		free(m->map_name);
		rem_que(&m->hdr);
		free(m);
	}
}

/*
 * Search the map for the key.
 * Put a safe copy in *pval or return
 * an error code
 */
int
mapc_meta_search(mnt_map *m, char *key, char **pval, int recurse)
{
	int error = 0;
	kv *k = 0;

	/*
	 * Firewall
	 */
	if (!m) {
		plog(XLOG_ERROR, "Null map request for %s", key);
		return ENOENT;
	}

	if (m->flags & MAPC_SYNC) {
		/*
		 * Get modify time...
		 */
		time_t t;
		error = (*m->mtime)(m->map_name, &t);
		if (error || t > m->modify) {
			m->modify = t;
			plog(XLOG_INFO, "Map %s is out of date", m->map_name);
			mapc_sync(m);
		}
	}

	if (!MAPC_ISRE(m)) {
		/*
		 * Compute the hash table offset
		 */
		k = m->kvhash[kvhash_of(key)];

		/*
		 * Scan the linked list for the key
		 */
		while (k && !FSTREQ(k->key, key)) k = k->next;

	}
	else if (recurse == MREC_FULL) {
		/*
		 * Try for an RE match against the entire map.
		 * Note that this will be done in a "random"
		 * order.
		 */

		int i;

		for (i = 0; i < NKVHASH; i++) {
			k = m->kvhash[i];
			while (k) {
				if (regexec((regex_t *)k->key, key,
				    0, NULL, 0) == 0)
					break;
				k = k->next;
			}
			if (k)
				break;
		}
	}

	/*
	 * If found then take a copy
	 */
	if (k) {
		if (k->val)
			*pval = strdup(k->val);
		else
			error = ENOENT;
	} else if (m->alloc >= MAPC_ALL) {
		/*
		 * If the entire map is cached then this
		 * key does not exist.
		 */
		error = ENOENT;
	} else {
		/*
		 * Otherwise search the map.  If we are
		 * in incremental mode then add the key
		 * to the cache.
		 */
		error = search_map(m, key, pval);
		if (!error && m->alloc == MAPC_INC)
			mapc_add_kv(m, strdup(key), strdup(*pval));
	}

	/*
	 * If an error, and a wildcard exists,
	 * and the key is not internal then
	 * return a copy of the wildcard.
	 */
	if (error > 0) {
		if (recurse == MREC_FULL && !MAPC_ISRE(m)) {
			char wildname[PATH_MAX];
			char *subp;
			if (*key == '/')
				return error;
			/*
			 * Keep chopping sub-directories from the RHS
			 * and replacing with "/ *" and repeat the lookup.
			 * For example:
			 * "src/gnu/gcc" -> "src / gnu / *" -> "src / *"
			 */
			strlcpy(wildname, key, sizeof wildname);
			while (error && (subp = strrchr(wildname, '/'))) {
				strlcpy(subp, "/*", 3);
#ifdef DEBUG
				dlog("mapc recurses on %s", wildname);
#endif
				error = mapc_meta_search(m, wildname, pval, MREC_PART);
				if (error)
					*subp = 0;
			}
			if (error > 0 && m->wildcard) {
				*pval = strdup(m->wildcard);
				error = 0;
			}
		}
	}

	return error;
}

int
mapc_search(mnt_map *m, char *key, char **pval)
{
	return mapc_meta_search(m, key, pval, MREC_FULL);
}

/*
 * Get map cache in sync with physical representation
 */
static void
mapc_sync(mnt_map *m)
{
	if (m->alloc != MAPC_ROOT) {
		mapc_clear(m);

		if (m->alloc >= MAPC_ALL)
			if (mapc_reload_map(m))
				m->alloc = MAPC_INC;
		/*
		 * Attempt to find the wildcard entry
		 */
		if (m->alloc < MAPC_ALL)
			mapc_find_wildcard(m);
	}
}

/*
 * Reload all the maps
 * Called when Amd gets hit by a SIGHUP.
 */
void mapc_reload(void)
{
	mnt_map *m;

	/*
	 * For all the maps,
	 * Throw away the existing information.
	 * Do a reload
	 * Find the wildcard
	 */
	ITER(m, mnt_map, &map_list_head)
		mapc_sync(m);
}

/*
 * Root map.
 * The root map is used to bootstrap amd.
 * All the require top-level mounts are added
 * into the root map and then the map is iterated
 * and a lookup is done on all the mount points.
 * This causes the top level mounts to be automounted.
 */

static int
root_init(char *map, time_t *tp)
{
	*tp = clocktime();
	return strcmp(map, ROOT_MAP) == 0 ? 0 : ENOENT;
}

/*
 * Add a new entry to the root map
 *
 * dir - directory (key)
 * opts - mount options
 * map - map name
 */
void
root_newmap(char *dir, char *opts, char *map)
{
	char str[PATH_MAX];

	/*
	 * First make sure we have a root map to talk about...
	 */
	if (!root_map)
		root_map = mapc_find(ROOT_MAP, "mapdefault");

	/*
	 * Then add the entry...
	 */
	dir = strdup(dir);
	if (map)
		snprintf(str, sizeof(str), "cache:=mapdefault;type:=toplvl;fs:=\"%s\";%s",
			map, opts ? opts : "");
	else
		strlcpy(str, opts, sizeof str);
	mapc_repl_kv(root_map, dir, strdup(str));
}

int
mapc_keyiter(mnt_map *m, void (*fn)(char *,void *), void *arg)
{
	int i;
	int c = 0;

	for (i = 0; i < NKVHASH; i++) {
		kv *k = m->kvhash[i];
		while (k) {
			(*fn)(k->key, arg);
			k = k->next;
			c++;
		}
	}

	return c;
}

/*
 * Iterate over the root map
 * and call (*fn)() on the key
 * of all the nodes.
 * Finally throw away the root map.
 */
int
root_keyiter(void (*fn)(char *,void *), void *arg)
{
	if (root_map) {
		int c = mapc_keyiter(root_map, fn, arg);
#ifdef notdef
		mapc_free(root_map);
		root_map = 0;
#endif
		return c;
	}
	return 0;
}

/*
 * Error map
 */
static int
error_init(char *map, time_t *tp)
{
	plog(XLOG_USER, "No source data for map %s", map);
	*tp = 0;
	return 0;
}

static int
error_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp)
{
	return ENOENT;
}

static int
error_reload(mnt_map *m, char *map, add_fn *fn)
{
	return ENOENT;
}

static int
error_mtime(char *map, time_t *tp)
{
	*tp = 0;
	return 0;
}
