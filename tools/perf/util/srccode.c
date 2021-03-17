// SPDX-License-Identifier: GPL-2.0-only
/*
 * Manage printing of source lines
 * Copyright (c) 2017, Intel Corporation.
 * Author: Andi Kleen
 */
#include <linux/list.h>
#include <linux/zalloc.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include "srccode.h"
#include "debug.h"
#include <internal/lib.h> // page_size
#include "fncache.h"

#define MAXSRCCACHE (32*1024*1024)
#define MAXSRCFILES     64
#define SRC_HTAB_SZ	64

struct srcfile {
	struct hlist_node hash_nd;
	struct list_head nd;
	char *fn;
	char **lines;
	char *map;
	unsigned numlines;
	size_t maplen;
};

static struct hlist_head srcfile_htab[SRC_HTAB_SZ];
static LIST_HEAD(srcfile_list);
static long map_total_sz;
static int num_srcfiles;

static int countlines(char *map, int maplen)
{
	int numl;
	char *end = map + maplen;
	char *p = map;

	if (maplen == 0)
		return 0;
	numl = 0;
	while (p < end && (p = memchr(p, '\n', end - p)) != NULL) {
		numl++;
		p++;
	}
	if (p < end)
		numl++;
	return numl;
}

static void fill_lines(char **lines, int maxline, char *map, int maplen)
{
	int l;
	char *end = map + maplen;
	char *p = map;

	if (maplen == 0 || maxline == 0)
		return;
	l = 0;
	lines[l++] = map;
	while (p < end && (p = memchr(p, '\n', end - p)) != NULL) {
		if (l >= maxline)
			return;
		lines[l++] = ++p;
	}
	if (p < end)
		lines[l] = p;
}

static void free_srcfile(struct srcfile *sf)
{
	list_del_init(&sf->nd);
	hlist_del(&sf->hash_nd);
	map_total_sz -= sf->maplen;
	munmap(sf->map, sf->maplen);
	zfree(&sf->lines);
	zfree(&sf->fn);
	free(sf);
	num_srcfiles--;
}

static struct srcfile *find_srcfile(char *fn)
{
	struct stat st;
	struct srcfile *h;
	int fd;
	unsigned long sz;
	unsigned hval = shash((unsigned char *)fn) % SRC_HTAB_SZ;

	hlist_for_each_entry (h, &srcfile_htab[hval], hash_nd) {
		if (!strcmp(fn, h->fn)) {
			/* Move to front */
			list_del(&h->nd);
			list_add(&h->nd, &srcfile_list);
			return h;
		}
	}

	/* Only prune if there is more than one entry */
	while ((num_srcfiles > MAXSRCFILES || map_total_sz > MAXSRCCACHE) &&
	       srcfile_list.next != &srcfile_list) {
		assert(!list_empty(&srcfile_list));
		h = list_entry(srcfile_list.prev, struct srcfile, nd);
		free_srcfile(h);
	}

	fd = open(fn, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) < 0) {
		pr_debug("cannot open source file %s\n", fn);
		return NULL;
	}

	h = malloc(sizeof(struct srcfile));
	if (!h)
		return NULL;

	h->fn = strdup(fn);
	if (!h->fn)
		goto out_h;

	h->maplen = st.st_size;
	sz = (h->maplen + page_size - 1) & ~(page_size - 1);
	h->map = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (h->map == (char *)-1) {
		pr_debug("cannot mmap source file %s\n", fn);
		goto out_fn;
	}
	h->numlines = countlines(h->map, h->maplen);
	h->lines = calloc(h->numlines, sizeof(char *));
	if (!h->lines)
		goto out_map;
	fill_lines(h->lines, h->numlines, h->map, h->maplen);
	list_add(&h->nd, &srcfile_list);
	hlist_add_head(&h->hash_nd, &srcfile_htab[hval]);
	map_total_sz += h->maplen;
	num_srcfiles++;
	return h;

out_map:
	munmap(h->map, sz);
out_fn:
	zfree(&h->fn);
out_h:
	free(h);
	return NULL;
}

/* Result is not 0 terminated */
char *find_sourceline(char *fn, unsigned line, int *lenp)
{
	char *l, *p;
	struct srcfile *sf = find_srcfile(fn);
	if (!sf)
		return NULL;
	line--;
	if (line >= sf->numlines)
		return NULL;
	l = sf->lines[line];
	if (!l)
		return NULL;
	p = memchr(l, '\n', sf->map + sf->maplen - l);
	*lenp = p - l;
	return l;
}
