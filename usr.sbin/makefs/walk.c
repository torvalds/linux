/*	$OpenBSD: walk.c,v 1.12 2023/08/19 04:21:06 guenther Exp $	*/
/*	$NetBSD: walk.c,v 1.29 2015/11/25 00:48:49 christos Exp $	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Luke Mewburn for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "makefs.h"

static	fsnode	*create_fsnode(const char *, const char *, const char *,
			       struct stat *);
static	fsinode	*link_check(fsinode *);


/*
 * walk_dir --
 *	build a tree of fsnodes from `root' and `dir', with a parent
 *	fsnode of `parent' (which may be NULL for the root of the tree).
 *	append the tree to a fsnode of `join' if it is not NULL.
 *	each "level" is a directory, with the "." entry guaranteed to be
 *	at the start of the list, and without ".." entries.
 */
fsnode *
walk_dir(const char *root, const char *dir, fsnode *parent, fsnode *join)
{
	fsnode		*first, *cur, *prev, *last;
	DIR		*dirp;
	struct dirent	*dent;
	char		path[PATH_MAX+1];
	struct stat	stbuf;
	char		*name, *rp;
	int		dot, len;

	assert(root != NULL);
	assert(dir != NULL);

	len = snprintf(path, sizeof(path), "%s/%s", root, dir);
	if (len >= (int)sizeof(path))
		errx(1, "Pathname too long.");
	if ((dirp = opendir(path)) == NULL)
		err(1, "Can't opendir `%s'", path);
	rp = path + strlen(root) + 1;
	if (join != NULL) {
		first = cur = join;
		while (cur->next != NULL)
			cur = cur->next;
		prev = last = cur;
	} else
		last = first = prev = NULL;
	while ((dent = readdir(dirp)) != NULL) {
		name = dent->d_name;
		dot = 0;
		if (name[0] == '.')
			switch (name[1]) {
			case '\0':	/* "." */
				if (join != NULL)
					continue;
				dot = 1;
				break;
			case '.':	/* ".." */
				if (name[2] == '\0')
					continue;
				/* FALLTHROUGH */
			default:
				dot = 0;
			}
		if (snprintf(path + len, sizeof(path) - len, "/%s", name) >=
		    (int)sizeof(path) - len)
			errx(1, "Pathname too long.");
		if (lstat(path, &stbuf) == -1)
			err(1, "Can't lstat `%s'", path);
		if (S_ISSOCK(stbuf.st_mode & S_IFMT))
			continue;

		if (join != NULL) {
			cur = join->next;
			for (;;) {
				if (cur == NULL || strcmp(cur->name, name) == 0)
					break;
				if (cur == last) {
					cur = NULL;
					break;
				}
				cur = cur->next;
			}
			if (cur != NULL) {
				if (S_ISDIR(cur->type) &&
				    S_ISDIR(stbuf.st_mode)) {
					cur->child = walk_dir(root, rp, cur,
					    cur->child);
					continue;
				}
				errx(1, "Can't merge %s `%s' with "
				    "existing %s",
				    inode_type(stbuf.st_mode), path,
				    inode_type(cur->type));
			}
		}

		cur = create_fsnode(root, dir, name, &stbuf);
		cur->parent = parent;
		if (dot) {
				/* ensure "." is at the start of the list */
			cur->next = first;
			first = cur;
			if (! prev)
				prev = cur;
			cur->first = first;
		} else {			/* not "." */
			if (prev)
				prev->next = cur;
			prev = cur;
			if (!first)
				first = cur;
			cur->first = first;
			if (S_ISDIR(cur->type)) {
				cur->child = walk_dir(root, rp, cur, NULL);
				continue;
			}
		}
		if (stbuf.st_nlink > 1) {
			fsinode	*curino;

			curino = link_check(cur->inode);
			if (curino != NULL) {
				free(cur->inode);
				cur->inode = curino;
				cur->inode->nlink++;
			}
		}
		if (S_ISLNK(cur->type)) {
			char	slink[PATH_MAX+1];
			int	llen;

			llen = readlink(path, slink, sizeof(slink) - 1);
			if (llen == -1)
				err(1, "Readlink `%s'", path);
			slink[llen] = '\0';
			cur->symlink = estrdup(slink);
		}
	}
	assert(first != NULL);
	if (join == NULL)
		for (cur = first->next; cur != NULL; cur = cur->next)
			cur->first = first;
	if (closedir(dirp) == -1)
		err(1, "Can't closedir `%s/%s'", root, dir);
	return (first);
}

static fsnode *
create_fsnode(const char *root, const char *path, const char *name,
    struct stat *stbuf)
{
	fsnode *cur;

	cur = ecalloc(1, sizeof(*cur));
	cur->path = estrdup(path);
	cur->name = estrdup(name);
	cur->inode = ecalloc(1, sizeof(*cur->inode));
	cur->root = root;
	cur->type = stbuf->st_mode & S_IFMT;
	cur->inode->nlink = 1;
	cur->inode->st = *stbuf;
	if (Tflag) {
		cur->inode->st.st_atim.tv_sec = stampts;
		cur->inode->st.st_atim.tv_nsec = 0;
		cur->inode->st.st_mtim = cur->inode->st.st_ctim =
		    cur->inode->st.st_atim;
	}
	return (cur);
}

/*
 * free_fsnodes --
 *	Removes node from tree and frees it and all of
 *   its descendants.
 */
void
free_fsnodes(fsnode *node)
{
	fsnode	*cur, *next;

	assert(node != NULL);

	/* for ".", start with actual parent node */
	if (node->first == node) {
		assert(node->name[0] == '.' && node->name[1] == '\0');
		if (node->parent) {
			assert(node->parent->child == node);
			node = node->parent;
		}
	}

	/* Find ourselves in our sibling list and unlink */
	if (node->first != node) {
		for (cur = node->first; cur->next; cur = cur->next) {
			if (cur->next == node) {
				cur->next = node->next;
				node->next = NULL;
				break;
			}
		}
	}

	for (cur = node; cur != NULL; cur = next) {
		next = cur->next;
		if (cur->child) {
			cur->child->parent = NULL;
			free_fsnodes(cur->child);
		}
		if (cur->inode->nlink-- == 1)
			free(cur->inode);
		if (cur->symlink)
			free(cur->symlink);
		free(cur->path);
		free(cur->name);
		free(cur);
	}
}


/*
 * inode_type --
 *	for a given inode type `mode', return a descriptive string.
 *	for most cases, uses inotype() from mtree/misc.c
 */
const char *
inode_type(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFBLK:
		return ("block");
	case S_IFCHR:
		return ("char");
	case S_IFDIR:
		return ("dir");
	case S_IFIFO:
		return ("fifo");
	case S_IFREG:
		return ("file");
	case S_IFLNK:
		return ("symlink");
	case S_IFSOCK:
		return ("socket");
	default:
		return ("unknown");
	}
}


/*
 * link_check --
 *	return pointer to fsinode matching `entry's st_ino & st_dev if it exists,
 *	otherwise add `entry' to table and return NULL
 */
/* This was borrowed from du.c and tweaked to keep an fsnode
 * pointer instead. -- dbj@netbsd.org
 */
static fsinode *
link_check(fsinode *entry)
{
	static struct entry {
		fsinode *data;
	} *htable;
	static int htshift;  /* log(allocated size) */
	static int htmask;   /* allocated size - 1 */
	static int htused;   /* 2*number of insertions */
	int h, h2;
	uint64_t tmp;
	/* this constant is (1<<64)/((1+sqrt(5))/2)
	 * aka (word size)/(golden ratio)
	 */
	const uint64_t HTCONST = 11400714819323198485ULL;
	const int HTBITS = 64;

	/* Never store zero in hashtable */
	assert(entry);

	/* Extend hash table if necessary, keep load under 0.5 */
	if (htused<<1 >= htmask) {
		struct entry *ohtable;

		if (!htable)
			htshift = 10;   /* starting hashtable size */
		else
			htshift++;   /* exponential hashtable growth */

		htmask  = (1 << htshift) - 1;
		htused = 0;

		ohtable = htable;
		htable = ecalloc(htmask+1, sizeof(*htable));
		/* populate newly allocated hashtable */
		if (ohtable) {
			int i;
			for (i = 0; i <= htmask>>1; i++)
				if (ohtable[i].data)
					link_check(ohtable[i].data);
			free(ohtable);
		}
	}

	/* multiplicative hashing */
	tmp = entry->st.st_dev;
	tmp <<= HTBITS>>1;
	tmp |=  entry->st.st_ino;
	tmp *= HTCONST;
	h  = tmp >> (HTBITS - htshift);
	h2 = 1 | ( tmp >> (HTBITS - (htshift<<1) - 1)); /* must be odd */

	/* open address hashtable search with double hash probing */
	while (htable[h].data) {
		if ((htable[h].data->st.st_ino == entry->st.st_ino) &&
		    (htable[h].data->st.st_dev == entry->st.st_dev)) {
			return htable[h].data;
		}
		h = (h + h2) & htmask;
	}

	/* Insert the current entry into hashtable */
	htable[h].data = entry;
	htused++;
	return NULL;
}
