/*	$NetBSD: walk.c,v 1.24 2008/12/28 21:51:46 christos Exp $	*/

/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
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


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "makefs.h"
#include "mtree.h"
#include "extern.h"

static	void	 apply_specdir(const char *, NODE *, fsnode *, int);
static	void	 apply_specentry(const char *, NODE *, fsnode *);
static	fsnode	*create_fsnode(const char *, const char *, const char *,
			       struct stat *);


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
	char		path[MAXPATHLEN + 1];
	struct stat	stbuf;
	char		*name, *rp;
	size_t		len;
	int		dot;

	assert(root != NULL);
	assert(dir != NULL);

	len = snprintf(path, sizeof(path), "%s/%s", root, dir);
	if (len >= sizeof(path))
		errx(1, "Pathname too long.");
	if (debug & DEBUG_WALK_DIR)
		printf("walk_dir: %s %p\n", path, parent);
	if ((dirp = opendir(path)) == NULL)
		err(1, "Can't opendir `%s'", path);
	rp = path + strlen(root) + 1;
	if (join != NULL) {
		first = cur = join;
		while (cur->next != NULL)
			cur = cur->next;
		prev = cur;
	} else
		first = prev = NULL;
	last = prev;
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
		if (debug & DEBUG_WALK_DIR_NODE)
			printf("scanning %s/%s/%s\n", root, dir, name);
		if ((size_t)snprintf(path + len, sizeof(path) - len, "/%s",
		    name) >= sizeof(path) - len)
			errx(1, "Pathname too long.");
		if (lstat(path, &stbuf) == -1)
			err(1, "Can't lstat `%s'", path);
#ifdef S_ISSOCK
		if (S_ISSOCK(stbuf.st_mode & S_IFMT)) {
			if (debug & DEBUG_WALK_DIR_NODE)
				printf("  skipping socket %s\n", path);
			continue;
		}
#endif

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
					if (debug & DEBUG_WALK_DIR_NODE)
						printf("merging %s with %p\n",
						    path, cur->child);
					cur->child = walk_dir(root, rp, cur,
					    cur->child);
					continue;
				}
				errx(1, "Can't merge %s `%s' with existing %s",
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
				if (debug & DEBUG_WALK_DIR_LINKCHECK)
					printf("link_check: found [%llu, %llu]\n",
					    (unsigned long long)curino->st.st_dev,
					    (unsigned long long)curino->st.st_ino);
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
	if (stampst.st_ino) {
		cur->inode->st.st_atime = stampst.st_atime;
		cur->inode->st.st_mtime = stampst.st_mtime;
		cur->inode->st.st_ctime = stampst.st_ctime;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
		cur->inode->st.st_atimensec = stampst.st_atimensec;
		cur->inode->st.st_mtimensec = stampst.st_mtimensec;
		cur->inode->st.st_ctimensec = stampst.st_ctimensec;
#endif
#if HAVE_STRUCT_STAT_BIRTHTIME
		cur->inode->st.st_birthtime = stampst.st_birthtime;
		cur->inode->st.st_birthtimensec = stampst.st_birthtimensec;
#endif
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
 * apply_specfile --
 *	read in the mtree(8) specfile, and apply it to the tree
 *	at dir,parent. parameters in parent on equivalent types
 *	will be changed to those found in specfile, and missing
 *	entries will be added.
 */
void
apply_specfile(const char *specfile, const char *dir, fsnode *parent, int speconly)
{
	struct timeval	 start;
	FILE	*fp;
	NODE	*root;

	assert(specfile != NULL);
	assert(parent != NULL);

	if (debug & DEBUG_APPLY_SPECFILE)
		printf("apply_specfile: %s, %s %p\n", specfile, dir, parent);

				/* read in the specfile */
	if ((fp = fopen(specfile, "r")) == NULL)
		err(1, "Can't open `%s'", specfile);
	TIMER_START(start);
	root = spec(fp);
	TIMER_RESULTS(start, "spec");
	if (fclose(fp) == EOF)
		err(1, "Can't close `%s'", specfile);

				/* perform some sanity checks */
	if (root == NULL)
		errx(1, "Specfile `%s' did not contain a tree", specfile);
	assert(strcmp(root->name, ".") == 0);
	assert(root->type == F_DIR);

				/* merge in the changes */
	apply_specdir(dir, root, parent, speconly);

	free_nodes(root);
}

static void
apply_specdir(const char *dir, NODE *specnode, fsnode *dirnode, int speconly)
{
	char	 path[MAXPATHLEN + 1];
	NODE	*curnode;
	fsnode	*curfsnode;

	assert(specnode != NULL);
	assert(dirnode != NULL);

	if (debug & DEBUG_APPLY_SPECFILE)
		printf("apply_specdir: %s %p %p\n", dir, specnode, dirnode);

	if (specnode->type != F_DIR)
		errx(1, "Specfile node `%s/%s' is not a directory",
		    dir, specnode->name);
	if (dirnode->type != S_IFDIR)
		errx(1, "Directory node `%s/%s' is not a directory",
		    dir, dirnode->name);

	apply_specentry(dir, specnode, dirnode);

	/* Remove any filesystem nodes not found in specfile */
	/* XXX inefficient.  This is O^2 in each dir and it would
	 * have been better never to have walked this part of the tree
	 * to begin with
	 */
	if (speconly) {
		fsnode *next;
		assert(dirnode->name[0] == '.' && dirnode->name[1] == '\0');
		for (curfsnode = dirnode->next; curfsnode != NULL; curfsnode = next) {
			next = curfsnode->next;
			for (curnode = specnode->child; curnode != NULL;
			     curnode = curnode->next) {
				if (strcmp(curnode->name, curfsnode->name) == 0)
					break;
			}
			if (curnode == NULL) {
				if (debug & DEBUG_APPLY_SPECONLY) {
					printf("apply_specdir: trimming %s/%s %p\n", dir, curfsnode->name, curfsnode);
				}
				free_fsnodes(curfsnode);
			}
		}
	}

			/* now walk specnode->child matching up with dirnode */
	for (curnode = specnode->child; curnode != NULL;
	    curnode = curnode->next) {
		if (debug & DEBUG_APPLY_SPECENTRY)
			printf("apply_specdir:  spec %s\n",
			    curnode->name);
		for (curfsnode = dirnode->next; curfsnode != NULL;
		    curfsnode = curfsnode->next) {
#if 0	/* too verbose for now */
			if (debug & DEBUG_APPLY_SPECENTRY)
				printf("apply_specdir:  dirent %s\n",
				    curfsnode->name);
#endif
			if (strcmp(curnode->name, curfsnode->name) == 0)
				break;
		}
		if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir,
		    curnode->name) >= sizeof(path))
			errx(1, "Pathname too long.");
		if (curfsnode == NULL) {	/* need new entry */
			struct stat	stbuf;

					    /*
					     * don't add optional spec entries
					     * that lack an existing fs entry
					     */
			if ((curnode->flags & F_OPT) &&
			    lstat(path, &stbuf) == -1)
					continue;

					/* check that enough info is provided */
#define NODETEST(t, m)							\
			if (!(t))					\
				errx(1, "`%s': %s not provided", path, m)
			NODETEST(curnode->flags & F_TYPE, "type");
			NODETEST(curnode->flags & F_MODE, "mode");
				/* XXX: require F_TIME ? */
			NODETEST(curnode->flags & F_GID ||
			    curnode->flags & F_GNAME, "group");
			NODETEST(curnode->flags & F_UID ||
			    curnode->flags & F_UNAME, "user");
/*			if (curnode->type == F_BLOCK || curnode->type == F_CHAR)
				NODETEST(curnode->flags & F_DEV,
				    "device number");*/
#undef NODETEST

			if (debug & DEBUG_APPLY_SPECFILE)
				printf("apply_specdir: adding %s\n",
				    curnode->name);
					/* build minimal fsnode */
			memset(&stbuf, 0, sizeof(stbuf));
			stbuf.st_mode = nodetoino(curnode->type);
			stbuf.st_nlink = 1;
			stbuf.st_mtime = stbuf.st_atime =
			    stbuf.st_ctime = start_time.tv_sec;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
			stbuf.st_mtimensec = stbuf.st_atimensec =
			    stbuf.st_ctimensec = start_time.tv_nsec;
#endif
			curfsnode = create_fsnode(".", ".", curnode->name,
			    &stbuf);
			curfsnode->parent = dirnode->parent;
			curfsnode->first = dirnode;
			curfsnode->next = dirnode->next;
			dirnode->next = curfsnode;
			if (curfsnode->type == S_IFDIR) {
					/* for dirs, make "." entry as well */
				curfsnode->child = create_fsnode(".", ".", ".",
				    &stbuf);
				curfsnode->child->parent = curfsnode;
				curfsnode->child->first = curfsnode->child;
			}
			if (curfsnode->type == S_IFLNK) {
				assert(curnode->slink != NULL);
					/* for symlinks, copy the target */
				curfsnode->symlink = estrdup(curnode->slink);
			}
		}
		apply_specentry(dir, curnode, curfsnode);
		if (curnode->type == F_DIR) {
			if (curfsnode->type != S_IFDIR)
				errx(1, "`%s' is not a directory", path);
			assert (curfsnode->child != NULL);
			apply_specdir(path, curnode, curfsnode->child, speconly);
		}
	}
}

static void
apply_specentry(const char *dir, NODE *specnode, fsnode *dirnode)
{

	assert(specnode != NULL);
	assert(dirnode != NULL);

	if (nodetoino(specnode->type) != dirnode->type)
		errx(1, "`%s/%s' type mismatch: specfile %s, tree %s",
		    dir, specnode->name, inode_type(nodetoino(specnode->type)),
		    inode_type(dirnode->type));

	if (debug & DEBUG_APPLY_SPECENTRY)
		printf("apply_specentry: %s/%s\n", dir, dirnode->name);

#define ASEPRINT(t, b, o, n) \
		if (debug & DEBUG_APPLY_SPECENTRY) \
			printf("\t\t\tchanging %s from " b " to " b "\n", \
			    t, o, n)

	if (specnode->flags & (F_GID | F_GNAME)) {
		ASEPRINT("gid", "%d",
		    dirnode->inode->st.st_gid, specnode->st_gid);
		dirnode->inode->st.st_gid = specnode->st_gid;
	}
	if (specnode->flags & F_MODE) {
		ASEPRINT("mode", "%#o",
		    dirnode->inode->st.st_mode & ALLPERMS, specnode->st_mode);
		dirnode->inode->st.st_mode &= ~ALLPERMS;
		dirnode->inode->st.st_mode |= (specnode->st_mode & ALLPERMS);
	}
		/* XXX: ignoring F_NLINK for now */
	if (specnode->flags & F_SIZE) {
		ASEPRINT("size", "%lld",
		    (long long)dirnode->inode->st.st_size,
		    (long long)specnode->st_size);
		dirnode->inode->st.st_size = specnode->st_size;
	}
	if (specnode->flags & F_SLINK) {
		assert(dirnode->symlink != NULL);
		assert(specnode->slink != NULL);
		ASEPRINT("symlink", "%s", dirnode->symlink, specnode->slink);
		free(dirnode->symlink);
		dirnode->symlink = estrdup(specnode->slink);
	}
	if (specnode->flags & F_TIME) {
		ASEPRINT("time", "%ld",
		    (long)dirnode->inode->st.st_mtime,
		    (long)specnode->st_mtimespec.tv_sec);
		dirnode->inode->st.st_mtime =		specnode->st_mtimespec.tv_sec;
		dirnode->inode->st.st_atime =		specnode->st_mtimespec.tv_sec;
		dirnode->inode->st.st_ctime =		start_time.tv_sec;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
		dirnode->inode->st.st_mtimensec =	specnode->st_mtimespec.tv_nsec;
		dirnode->inode->st.st_atimensec =	specnode->st_mtimespec.tv_nsec;
		dirnode->inode->st.st_ctimensec =	start_time.tv_nsec;
#endif
	}
	if (specnode->flags & (F_UID | F_UNAME)) {
		ASEPRINT("uid", "%d",
		    dirnode->inode->st.st_uid, specnode->st_uid);
		dirnode->inode->st.st_uid = specnode->st_uid;
	}
#if HAVE_STRUCT_STAT_ST_FLAGS
	if (specnode->flags & F_FLAGS) {
		ASEPRINT("flags", "%#lX",
		    (unsigned long)dirnode->inode->st.st_flags,
		    (unsigned long)specnode->st_flags);
		dirnode->inode->st.st_flags = specnode->st_flags;
	}
#endif
/*	if (specnode->flags & F_DEV) {
		ASEPRINT("rdev", "%#llx",
		    (unsigned long long)dirnode->inode->st.st_rdev,
		    (unsigned long long)specnode->st_rdev);
		dirnode->inode->st.st_rdev = specnode->st_rdev;
	}*/
#undef ASEPRINT

	dirnode->flags |= FSNODE_F_HASSPEC;
}


/*
 * dump_fsnodes --
 *	dump the fsnodes from `cur'
 */
void
dump_fsnodes(fsnode *root)
{
	fsnode	*cur;
	char	path[MAXPATHLEN + 1];

	printf("dump_fsnodes: %s %p\n", root->path, root);
	for (cur = root; cur != NULL; cur = cur->next) {
		if (snprintf(path, sizeof(path), "%s/%s", cur->path,
		    cur->name) >= (int)sizeof(path))
			errx(1, "Pathname too long.");

		if (debug & DEBUG_DUMP_FSNODES_VERBOSE)
			printf("cur=%8p parent=%8p first=%8p ",
			    cur, cur->parent, cur->first);
		printf("%7s: %s", inode_type(cur->type), path);
		if (S_ISLNK(cur->type)) {
			assert(cur->symlink != NULL);
			printf(" -> %s", cur->symlink);
		} else {
			assert (cur->symlink == NULL);
		}
		if (cur->inode->nlink > 1)
			printf(", nlinks=%d", cur->inode->nlink);
		putchar('\n');

		if (cur->child) {
			assert (cur->type == S_IFDIR);
			dump_fsnodes(cur->child);
		}
	}
	printf("dump_fsnodes: finished %s/%s\n", root->path, root->name);
}


/*
 * inode_type --
 *	for a given inode type `mode', return a descriptive string.
 *	for most cases, uses inotype() from mtree/misc.c
 */
const char *
inode_type(mode_t mode)
{
	if (S_ISREG(mode))
		return ("file");
	if (S_ISLNK(mode))
		return ("symlink");
	if (S_ISDIR(mode))
		return ("dir");
	if (S_ISLNK(mode))
		return ("link");
	if (S_ISFIFO(mode))
		return ("fifo");
	if (S_ISSOCK(mode))
		return ("socket");
	/* XXX should not happen but handle them */
	if (S_ISCHR(mode))
		return ("char");
	if (S_ISBLK(mode))
		return ("block");
	return ("unknown");
}


/*
 * link_check --
 *	return pointer to fsinode matching `entry's st_ino & st_dev if it exists,
 *	otherwise add `entry' to table and return NULL
 */
/* This was borrowed from du.c and tweaked to keep an fsnode 
 * pointer instead. -- dbj@netbsd.org
 */
fsinode *
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
