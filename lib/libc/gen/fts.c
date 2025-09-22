/*	$OpenBSD: fts.c,v 1.61 2021/11/29 03:20:37 deraadt Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/param.h>	/* ALIGN ALIGNBYTES */
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

static FTSENT	*fts_alloc(FTS *, const char *, size_t);
static FTSENT	*fts_build(FTS *, int);
static void	 fts_lfree(FTSENT *);
static void	 fts_load(FTS *, FTSENT *);
static size_t	 fts_maxarglen(char * const *);
static void	 fts_padjust(FTS *, FTSENT *);
static int	 fts_palloc(FTS *, size_t);
static FTSENT	*fts_sort(FTS *, FTSENT *, int);
static u_short	 fts_stat(FTS *, FTSENT *, int, int);
static int	 fts_safe_changedir(FTS *, FTSENT *, int, const char *);

#define	ISDOT(a)	(a[0] == '.' && (!a[1] || (a[1] == '.' && !a[2])))

#define	CLR(opt)	(sp->fts_options &= ~(opt))
#define	ISSET(opt)	(sp->fts_options & (opt))
#define	SET(opt)	(sp->fts_options |= (opt))

#define	FCHDIR(sp, fd)	(!ISSET(FTS_NOCHDIR) && fchdir(fd))

/* fts_build flags */
#define	BCHILD		1		/* fts_children */
#define	BNAMES		2		/* fts_children, names only */
#define	BREAD		3		/* fts_read */

FTS *
fts_open(char * const *argv, int options,
    int (*compar)(const FTSENT **, const FTSENT **))
{
	FTS *sp;
	FTSENT *p, *root;
	int nitems;
	FTSENT *parent, *prev;

	/* Options check. */
	if (options & ~FTS_OPTIONMASK) {
		errno = EINVAL;
		return (NULL);
	}

	/* At least one path must be specified. */
	if (*argv == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	/* Allocate/initialize the stream */
	if ((sp = calloc(1, sizeof(FTS))) == NULL)
		return (NULL);
	sp->fts_compar = compar;
	sp->fts_options = options;

	/* Logical walks turn on NOCHDIR; symbolic links are too hard. */
	if (ISSET(FTS_LOGICAL))
		SET(FTS_NOCHDIR);

	/*
	 * Start out with 1K of path space, and enough, in any case,
	 * to hold the user's paths.
	 */
	if (fts_palloc(sp, MAXIMUM(fts_maxarglen(argv), PATH_MAX)))
		goto mem1;

	/* Allocate/initialize root's parent. */
	if ((parent = fts_alloc(sp, "", 0)) == NULL)
		goto mem2;
	parent->fts_level = FTS_ROOTPARENTLEVEL;

	/* Allocate/initialize root(s). */
	for (root = prev = NULL, nitems = 0; *argv; ++argv, ++nitems) {
		if ((p = fts_alloc(sp, *argv, strlen(*argv))) == NULL)
			goto mem3;
		p->fts_level = FTS_ROOTLEVEL;
		p->fts_parent = parent;
		p->fts_accpath = p->fts_name;
		p->fts_info = fts_stat(sp, p, ISSET(FTS_COMFOLLOW), -1);

		/* Command-line "." and ".." are real directories. */
		if (p->fts_info == FTS_DOT)
			p->fts_info = FTS_D;

		/*
		 * If comparison routine supplied, traverse in sorted
		 * order; otherwise traverse in the order specified.
		 */
		if (compar) {
			p->fts_link = root;
			root = p;
		} else {
			p->fts_link = NULL;
			if (root == NULL)
				root = p;
			else
				prev->fts_link = p;
			prev = p;
		}
	}
	if (compar && nitems > 1)
		root = fts_sort(sp, root, nitems);

	/*
	 * Allocate a dummy pointer and make fts_read think that we've just
	 * finished the node before the root(s); set p->fts_info to FTS_INIT
	 * so that everything about the "current" node is ignored.
	 */
	if ((sp->fts_cur = fts_alloc(sp, "", 0)) == NULL)
		goto mem3;
	sp->fts_cur->fts_link = root;
	sp->fts_cur->fts_info = FTS_INIT;

	/*
	 * If using chdir(2), grab a file descriptor pointing to dot to ensure
	 * that we can get back here; this could be avoided for some paths,
	 * but almost certainly not worth the effort.  Slashes, symbolic links,
	 * and ".." are all fairly nasty problems.  Note, if we can't get the
	 * descriptor we run anyway, just more slowly.
	 */
	if (!ISSET(FTS_NOCHDIR) &&
	    (sp->fts_rfd = open(".", O_RDONLY | O_CLOEXEC)) == -1)
		SET(FTS_NOCHDIR);

	if (nitems == 0)
		free(parent);

	return (sp);

mem3:	fts_lfree(root);
	free(parent);
mem2:	free(sp->fts_path);
mem1:	free(sp);
	return (NULL);
}
DEF_WEAK(fts_open);

static void
fts_load(FTS *sp, FTSENT *p)
{
	size_t len;
	char *cp;

	/*
	 * Load the stream structure for the next traversal.  Since we don't
	 * actually enter the directory until after the preorder visit, set
	 * the fts_accpath field specially so the chdir gets done to the right
	 * place and the user can access the first node.  From fts_open it's
	 * known that the path will fit.
	 */
	len = p->fts_pathlen = p->fts_namelen;
	memmove(sp->fts_path, p->fts_name, len + 1);
	if ((cp = strrchr(p->fts_name, '/')) && (cp != p->fts_name || cp[1])) {
		len = strlen(++cp);
		memmove(p->fts_name, cp, len + 1);
		p->fts_namelen = len;
	}
	p->fts_accpath = p->fts_path = sp->fts_path;
	sp->fts_dev = p->fts_dev;
}

int
fts_close(FTS *sp)
{
	FTSENT *freep, *p;
	int rfd, error = 0;

	/*
	 * This still works if we haven't read anything -- the dummy structure
	 * points to the root list, so we step through to the end of the root
	 * list which has a valid parent pointer.
	 */
	if (sp->fts_cur) {
		for (p = sp->fts_cur; p->fts_level >= FTS_ROOTLEVEL;) {
			freep = p;
			p = p->fts_link ? p->fts_link : p->fts_parent;
			free(freep);
		}
		free(p);
	}

	/* Stash the original directory fd if needed. */
	rfd = ISSET(FTS_NOCHDIR) ? -1 : sp->fts_rfd;

	/* Free up child linked list, sort array, path buffer, stream ptr.*/
	if (sp->fts_child)
		fts_lfree(sp->fts_child);
	free(sp->fts_array);
	free(sp->fts_path);
	free(sp);

	/* Return to original directory, checking for error. */
	if (rfd != -1) {
		int saved_errno;
		error = fchdir(rfd);
		saved_errno = errno;
		(void)close(rfd);
		errno = saved_errno;
	}

	return (error);
}
DEF_WEAK(fts_close);

/*
 * Special case of "/" at the end of the path so that slashes aren't
 * appended which would cause paths to be written as "....//foo".
 */
#define	NAPPEND(p)							\
	(p->fts_path[p->fts_pathlen - 1] == '/'				\
	    ? p->fts_pathlen - 1 : p->fts_pathlen)

FTSENT *
fts_read(FTS *sp)
{
	FTSENT *p, *tmp;
	int instr;
	char *t;
	int saved_errno;

	/* If finished or unrecoverable error, return NULL. */
	if (sp->fts_cur == NULL || ISSET(FTS_STOP))
		return (NULL);

	/* Set current node pointer. */
	p = sp->fts_cur;

	/* Save and zero out user instructions. */
	instr = p->fts_instr;
	p->fts_instr = FTS_NOINSTR;

	/* Any type of file may be re-visited; re-stat and re-turn. */
	if (instr == FTS_AGAIN) {
		p->fts_info = fts_stat(sp, p, 0, -1);
		return (p);
	}

	/*
	 * Following a symlink -- SLNONE test allows application to see
	 * SLNONE and recover.  If indirecting through a symlink, have
	 * keep a pointer to current location.  If unable to get that
	 * pointer, follow fails.
	 */
	if (instr == FTS_FOLLOW &&
	    (p->fts_info == FTS_SL || p->fts_info == FTS_SLNONE)) {
		p->fts_info = fts_stat(sp, p, 1, -1);
		if (p->fts_info == FTS_D && !ISSET(FTS_NOCHDIR)) {
			if ((p->fts_symfd =
			    open(".", O_RDONLY | O_CLOEXEC)) == -1) {
				p->fts_errno = errno;
				p->fts_info = FTS_ERR;
			} else
				p->fts_flags |= FTS_SYMFOLLOW;
		}
		return (p);
	}

	/* Directory in pre-order. */
	if (p->fts_info == FTS_D) {
		/* If skipped or crossed mount point, do post-order visit. */
		if (instr == FTS_SKIP ||
		    (ISSET(FTS_XDEV) && p->fts_dev != sp->fts_dev)) {
			if (p->fts_flags & FTS_SYMFOLLOW)
				(void)close(p->fts_symfd);
			if (sp->fts_child) {
				fts_lfree(sp->fts_child);
				sp->fts_child = NULL;
			}
			p->fts_info = FTS_DP;
			return (p);
		}

		/* Rebuild if only read the names and now traversing. */
		if (sp->fts_child && ISSET(FTS_NAMEONLY)) {
			CLR(FTS_NAMEONLY);
			fts_lfree(sp->fts_child);
			sp->fts_child = NULL;
		}

		/*
		 * Cd to the subdirectory.
		 *
		 * If have already read and now fail to chdir, whack the list
		 * to make the names come out right, and set the parent errno
		 * so the application will eventually get an error condition.
		 * Set the FTS_DONTCHDIR flag so that when we logically change
		 * directories back to the parent we don't do a chdir.
		 *
		 * If haven't read do so.  If the read fails, fts_build sets
		 * FTS_STOP or the fts_info field of the node.
		 */
		if (sp->fts_child) {
			if (fts_safe_changedir(sp, p, -1, p->fts_accpath)) {
				p->fts_errno = errno;
				p->fts_flags |= FTS_DONTCHDIR;
				for (p = sp->fts_child; p; p = p->fts_link)
					p->fts_accpath =
					    p->fts_parent->fts_accpath;
			}
		} else if ((sp->fts_child = fts_build(sp, BREAD)) == NULL) {
			if (ISSET(FTS_STOP))
				return (NULL);
			return (p);
		}
		p = sp->fts_child;
		sp->fts_child = NULL;
		goto name;
	}

	/* Move to the next node on this level. */
next:	tmp = p;
	if ((p = p->fts_link)) {
		free(tmp);

		/*
		 * If reached the top, return to the original directory (or
		 * the root of the tree), and load the paths for the next root.
		 */
		if (p->fts_level == FTS_ROOTLEVEL) {
			if (FCHDIR(sp, sp->fts_rfd)) {
				SET(FTS_STOP);
				return (NULL);
			}
			fts_load(sp, p);
			return (sp->fts_cur = p);
		}

		/*
		 * User may have called fts_set on the node.  If skipped,
		 * ignore.  If followed, get a file descriptor so we can
		 * get back if necessary.
		 */
		if (p->fts_instr == FTS_SKIP)
			goto next;
		if (p->fts_instr == FTS_FOLLOW) {
			p->fts_info = fts_stat(sp, p, 1, -1);
			if (p->fts_info == FTS_D && !ISSET(FTS_NOCHDIR)) {
				if ((p->fts_symfd =
				    open(".", O_RDONLY | O_CLOEXEC)) == -1) {
					p->fts_errno = errno;
					p->fts_info = FTS_ERR;
				} else
					p->fts_flags |= FTS_SYMFOLLOW;
			}
			p->fts_instr = FTS_NOINSTR;
		}

name:		t = sp->fts_path + NAPPEND(p->fts_parent);
		*t++ = '/';
		memmove(t, p->fts_name, p->fts_namelen + 1);
		return (sp->fts_cur = p);
	}

	/* Move up to the parent node. */
	p = tmp->fts_parent;
	free(tmp);

	if (p->fts_level == FTS_ROOTPARENTLEVEL) {
		/*
		 * Done; free everything up and set errno to 0 so the user
		 * can distinguish between error and EOF.
		 */
		free(p);
		errno = 0;
		return (sp->fts_cur = NULL);
	}

	/* NUL terminate the pathname. */
	sp->fts_path[p->fts_pathlen] = '\0';

	/*
	 * Return to the parent directory.  If at a root node or came through
	 * a symlink, go back through the file descriptor.  Otherwise, cd up
	 * one directory.
	 */
	if (p->fts_level == FTS_ROOTLEVEL) {
		if (FCHDIR(sp, sp->fts_rfd)) {
			SET(FTS_STOP);
			sp->fts_cur = p;
			return (NULL);
		}
	} else if (p->fts_flags & FTS_SYMFOLLOW) {
		if (FCHDIR(sp, p->fts_symfd)) {
			saved_errno = errno;
			(void)close(p->fts_symfd);
			errno = saved_errno;
			SET(FTS_STOP);
			sp->fts_cur = p;
			return (NULL);
		}
		(void)close(p->fts_symfd);
	} else if (!(p->fts_flags & FTS_DONTCHDIR) &&
	    fts_safe_changedir(sp, p->fts_parent, -1, "..")) {
		SET(FTS_STOP);
		sp->fts_cur = p;
		return (NULL);
	}
	p->fts_info = p->fts_errno ? FTS_ERR : FTS_DP;
	return (sp->fts_cur = p);
}
DEF_WEAK(fts_read);

/*
 * Fts_set takes the stream as an argument although it's not used in this
 * implementation; it would be necessary if anyone wanted to add global
 * semantics to fts using fts_set.  An error return is allowed for similar
 * reasons.
 */
int
fts_set(FTS *sp, FTSENT *p, int instr)
{
	if (instr && instr != FTS_AGAIN && instr != FTS_FOLLOW &&
	    instr != FTS_NOINSTR && instr != FTS_SKIP) {
		errno = EINVAL;
		return (1);
	}
	p->fts_instr = instr;
	return (0);
}
DEF_WEAK(fts_set);

FTSENT *
fts_children(FTS *sp, int instr)
{
	FTSENT *p;
	int fd;

	if (instr && instr != FTS_NAMEONLY) {
		errno = EINVAL;
		return (NULL);
	}

	/* Set current node pointer. */
	p = sp->fts_cur;

	/*
	 * Errno set to 0 so user can distinguish empty directory from
	 * an error.
	 */
	errno = 0;

	/* Fatal errors stop here. */
	if (ISSET(FTS_STOP))
		return (NULL);

	/* Return logical hierarchy of user's arguments. */
	if (p->fts_info == FTS_INIT)
		return (p->fts_link);

	/*
	 * If not a directory being visited in pre-order, stop here.  Could
	 * allow FTS_DNR, assuming the user has fixed the problem, but the
	 * same effect is available with FTS_AGAIN.
	 */
	if (p->fts_info != FTS_D /* && p->fts_info != FTS_DNR */)
		return (NULL);

	/* Free up any previous child list. */
	if (sp->fts_child)
		fts_lfree(sp->fts_child);

	if (instr == FTS_NAMEONLY) {
		SET(FTS_NAMEONLY);
		instr = BNAMES;
	} else
		instr = BCHILD;

	/*
	 * If using chdir on a relative path and called BEFORE fts_read does
	 * its chdir to the root of a traversal, we can lose -- we need to
	 * chdir into the subdirectory, and we don't know where the current
	 * directory is, so we can't get back so that the upcoming chdir by
	 * fts_read will work.
	 */
	if (p->fts_level != FTS_ROOTLEVEL || p->fts_accpath[0] == '/' ||
	    ISSET(FTS_NOCHDIR))
		return (sp->fts_child = fts_build(sp, instr));

	if ((fd = open(".", O_RDONLY | O_CLOEXEC)) == -1)
		return (NULL);
	sp->fts_child = fts_build(sp, instr);
	if (fchdir(fd)) {
		(void)close(fd);
		return (NULL);
	}
	(void)close(fd);
	return (sp->fts_child);
}
DEF_WEAK(fts_children);

/*
 * This is the tricky part -- do not casually change *anything* in here.  The
 * idea is to build the linked list of entries that are used by fts_children
 * and fts_read.  There are lots of special cases.
 *
 * The real slowdown in walking the tree is the stat calls.  If FTS_NOSTAT is
 * set and it's a physical walk (so that symbolic links can't be directories),
 * we can do things quickly.  First, if it's a 4.4BSD file system, the type
 * of the file is in the directory entry.  Otherwise, we assume that the number
 * of subdirectories in a node is equal to the number of links to the parent.
 * The former skips all stat calls.  The latter skips stat calls in any leaf
 * directories and for any files after the subdirectories in the directory have
 * been found, cutting the stat calls by about 2/3.
 */
static FTSENT *
fts_build(FTS *sp, int type)
{
	struct dirent *dp;
	FTSENT *p, *head;
	FTSENT *cur, *tail;
	DIR *dirp;
	void *oldaddr;
	size_t len, maxlen;
	int nitems, cderrno, descend, level, nlinks, nostat, doadjust;
	int saved_errno;
	char *cp;

	/* Set current node pointer. */
	cur = sp->fts_cur;

	/*
	 * Open the directory for reading.  If this fails, we're done.
	 * If being called from fts_read, set the fts_info field.
	 */
	if ((dirp = opendir(cur->fts_accpath)) == NULL) {
		if (type == BREAD) {
			cur->fts_info = FTS_DNR;
			cur->fts_errno = errno;
		}
		return (NULL);
	}

	/*
	 * Nlinks is the number of possible entries of type directory in the
	 * directory if we're cheating on stat calls, 0 if we're not doing
	 * any stat calls at all, -1 if we're doing stats on everything.
	 */
	if (type == BNAMES)
		nlinks = 0;
	else if (ISSET(FTS_NOSTAT) && ISSET(FTS_PHYSICAL)) {
		nlinks = cur->fts_nlink - (ISSET(FTS_SEEDOT) ? 0 : 2);
		nostat = 1;
	} else {
		nlinks = -1;
		nostat = 0;
	}

#ifdef notdef
	(void)printf("nlinks == %d (cur: %u)\n", nlinks, cur->fts_nlink);
	(void)printf("NOSTAT %d PHYSICAL %d SEEDOT %d\n",
	    ISSET(FTS_NOSTAT), ISSET(FTS_PHYSICAL), ISSET(FTS_SEEDOT));
#endif
	/*
	 * If we're going to need to stat anything or we want to descend
	 * and stay in the directory, chdir.  If this fails we keep going,
	 * but set a flag so we don't chdir after the post-order visit.
	 * We won't be able to stat anything, but we can still return the
	 * names themselves.  Note, that since fts_read won't be able to
	 * chdir into the directory, it will have to return different path
	 * names than before, i.e. "a/b" instead of "b".  Since the node
	 * has already been visited in pre-order, have to wait until the
	 * post-order visit to return the error.  There is a special case
	 * here, if there was nothing to stat then it's not an error to
	 * not be able to stat.  This is all fairly nasty.  If a program
	 * needed sorted entries or stat information, they had better be
	 * checking FTS_NS on the returned nodes.
	 */
	cderrno = 0;
	if (nlinks || type == BREAD) {
		if (fts_safe_changedir(sp, cur, dirfd(dirp), NULL)) {
			if (nlinks && type == BREAD)
				cur->fts_errno = errno;
			cur->fts_flags |= FTS_DONTCHDIR;
			descend = 0;
			cderrno = errno;
			(void)closedir(dirp);
			dirp = NULL;
		} else
			descend = 1;
	} else
		descend = 0;

	/*
	 * Figure out the max file name length that can be stored in the
	 * current path -- the inner loop allocates more path as necessary.
	 * We really wouldn't have to do the maxlen calculations here, we
	 * could do them in fts_read before returning the path, but it's a
	 * lot easier here since the length is part of the dirent structure.
	 *
	 * If not changing directories set a pointer so that can just append
	 * each new name into the path.
	 */
	len = NAPPEND(cur);
	if (ISSET(FTS_NOCHDIR)) {
		cp = sp->fts_path + len;
		*cp++ = '/';
	}
	len++;
	maxlen = sp->fts_pathlen - len;

	/*
	 * fts_level is signed so we must prevent it from wrapping
	 * around to FTS_ROOTLEVEL and FTS_ROOTPARENTLEVEL.
	 */
	level = cur->fts_level;
	if (level < FTS_MAXLEVEL)
	    level++;

	/* Read the directory, attaching each entry to the `link' pointer. */
	doadjust = 0;
	for (head = tail = NULL, nitems = 0; dirp && (dp = readdir(dirp));) {
		if (!ISSET(FTS_SEEDOT) && ISDOT(dp->d_name))
			continue;

		if (!(p = fts_alloc(sp, dp->d_name, dp->d_namlen)))
			goto mem1;
		if (dp->d_namlen >= maxlen) {	/* include space for NUL */
			oldaddr = sp->fts_path;
			if (fts_palloc(sp, dp->d_namlen +len + 1)) {
				/*
				 * No more memory for path or structures.  Save
				 * errno, free up the current structure and the
				 * structures already allocated.
				 */
mem1:				saved_errno = errno;
				free(p);
				fts_lfree(head);
				(void)closedir(dirp);
				cur->fts_info = FTS_ERR;
				SET(FTS_STOP);
				errno = saved_errno;
				return (NULL);
			}
			/* Did realloc() change the pointer? */
			if (oldaddr != sp->fts_path) {
				doadjust = 1;
				if (ISSET(FTS_NOCHDIR))
					cp = sp->fts_path + len;
			}
			maxlen = sp->fts_pathlen - len;
		}

		p->fts_level = level;
		p->fts_parent = sp->fts_cur;
		p->fts_pathlen = len + dp->d_namlen;
		if (p->fts_pathlen < len) {
			/*
			 * If we wrap, free up the current structure and
			 * the structures already allocated, then error
			 * out with ENAMETOOLONG.
			 */
			free(p);
			fts_lfree(head);
			(void)closedir(dirp);
			cur->fts_info = FTS_ERR;
			SET(FTS_STOP);
			errno = ENAMETOOLONG;
			return (NULL);
		}

		if (cderrno) {
			if (nlinks) {
				p->fts_info = FTS_NS;
				p->fts_errno = cderrno;
			} else
				p->fts_info = FTS_NSOK;
			p->fts_accpath = cur->fts_accpath;
		} else if (nlinks == 0
#ifdef DT_DIR
		    || (nostat &&
		    dp->d_type != DT_DIR && dp->d_type != DT_UNKNOWN)
#endif
		    ) {
			p->fts_accpath =
			    ISSET(FTS_NOCHDIR) ? p->fts_path : p->fts_name;
			p->fts_info = FTS_NSOK;
		} else {
			/* Build a file name for fts_stat to stat. */
			if (ISSET(FTS_NOCHDIR)) {
				p->fts_accpath = p->fts_path;
				memmove(cp, p->fts_name, p->fts_namelen + 1);
				p->fts_info = fts_stat(sp, p, 0, dirfd(dirp));
			} else {
				p->fts_accpath = p->fts_name;
				p->fts_info = fts_stat(sp, p, 0, -1);
			}

			/* Decrement link count if applicable. */
			if (nlinks > 0 && (p->fts_info == FTS_D ||
			    p->fts_info == FTS_DC || p->fts_info == FTS_DOT))
				--nlinks;
		}

		/* We walk in directory order so "ls -f" doesn't get upset. */
		p->fts_link = NULL;
		if (head == NULL)
			head = tail = p;
		else {
			tail->fts_link = p;
			tail = p;
		}
		++nitems;
	}
	if (dirp)
		(void)closedir(dirp);

	/*
	 * If realloc() changed the address of the path, adjust the
	 * addresses for the rest of the tree and the dir list.
	 */
	if (doadjust)
		fts_padjust(sp, head);

	/*
	 * If not changing directories, reset the path back to original
	 * state.
	 */
	if (ISSET(FTS_NOCHDIR)) {
		if (len == sp->fts_pathlen || nitems == 0)
			--cp;
		*cp = '\0';
	}

	/*
	 * If descended after called from fts_children or after called from
	 * fts_read and nothing found, get back.  At the root level we use
	 * the saved fd; if one of fts_open()'s arguments is a relative path
	 * to an empty directory, we wind up here with no other way back.  If
	 * can't get back, we're done.
	 */
	if (descend && (type == BCHILD || !nitems) &&
	    (cur->fts_level == FTS_ROOTLEVEL ? FCHDIR(sp, sp->fts_rfd) :
	    fts_safe_changedir(sp, cur->fts_parent, -1, ".."))) {
		cur->fts_info = FTS_ERR;
		SET(FTS_STOP);
		return (NULL);
	}

	/* If didn't find anything, return NULL. */
	if (!nitems) {
		if (type == BREAD)
			cur->fts_info = FTS_DP;
		return (NULL);
	}

	/* Sort the entries. */
	if (sp->fts_compar && nitems > 1)
		head = fts_sort(sp, head, nitems);
	return (head);
}

static u_short
fts_stat(FTS *sp, FTSENT *p, int follow, int dfd)
{
	FTSENT *t;
	dev_t dev;
	ino_t ino;
	struct stat *sbp, sb;
	int saved_errno;
	const char *path;

	if (dfd == -1) {
		path = p->fts_accpath;
		dfd = AT_FDCWD;
	} else
		path = p->fts_name;

	/* If user needs stat info, stat buffer already allocated. */
	sbp = ISSET(FTS_NOSTAT) ? &sb : p->fts_statp;

	/*
	 * If doing a logical walk, or application requested FTS_FOLLOW, do
	 * a stat(2).  If that fails, check for a non-existent symlink.  If
	 * fail, set the errno from the stat call.
	 */
	if (ISSET(FTS_LOGICAL) || follow) {
		if (fstatat(dfd, path, sbp, 0)) {
			saved_errno = errno;
			if (!fstatat(dfd, path, sbp, AT_SYMLINK_NOFOLLOW)) {
				errno = 0;
				return (FTS_SLNONE);
			}
			p->fts_errno = saved_errno;
			goto err;
		}
	} else if (fstatat(dfd, path, sbp, AT_SYMLINK_NOFOLLOW)) {
		p->fts_errno = errno;
err:		memset(sbp, 0, sizeof(struct stat));
		return (FTS_NS);
	}

	if (S_ISDIR(sbp->st_mode)) {
		/*
		 * Set the device/inode.  Used to find cycles and check for
		 * crossing mount points.  Also remember the link count, used
		 * in fts_build to limit the number of stat calls.  It is
		 * understood that these fields are only referenced if fts_info
		 * is set to FTS_D.
		 */
		dev = p->fts_dev = sbp->st_dev;
		ino = p->fts_ino = sbp->st_ino;
		p->fts_nlink = sbp->st_nlink;

		if (ISDOT(p->fts_name))
			return (FTS_DOT);

		/*
		 * Cycle detection is done by brute force when the directory
		 * is first encountered.  If the tree gets deep enough or the
		 * number of symbolic links to directories is high enough,
		 * something faster might be worthwhile.
		 */
		for (t = p->fts_parent;
		    t->fts_level >= FTS_ROOTLEVEL; t = t->fts_parent)
			if (ino == t->fts_ino && dev == t->fts_dev) {
				p->fts_cycle = t;
				return (FTS_DC);
			}
		return (FTS_D);
	}
	if (S_ISLNK(sbp->st_mode))
		return (FTS_SL);
	if (S_ISREG(sbp->st_mode))
		return (FTS_F);
	return (FTS_DEFAULT);
}

static FTSENT *
fts_sort(FTS *sp, FTSENT *head, int nitems)
{
	FTSENT **ap, *p;

	/*
	 * Construct an array of pointers to the structures and call qsort(3).
	 * Reassemble the array in the order returned by qsort.  If unable to
	 * sort for memory reasons, return the directory entries in their
	 * current order.  Allocate enough space for the current needs plus
	 * 40 so don't realloc one entry at a time.
	 */
	if (nitems > sp->fts_nitems) {
		struct _ftsent **a;

		if ((a = reallocarray(sp->fts_array,
		    nitems + 40, sizeof(FTSENT *))) == NULL) {
			free(sp->fts_array);
			sp->fts_array = NULL;
			sp->fts_nitems = 0;
			return (head);
		}
		sp->fts_nitems = nitems + 40;
		sp->fts_array = a;
	}
	for (ap = sp->fts_array, p = head; p; p = p->fts_link)
		*ap++ = p;
	qsort(sp->fts_array, nitems, sizeof(FTSENT *), sp->fts_compar);
	for (head = *(ap = sp->fts_array); --nitems; ++ap)
		ap[0]->fts_link = ap[1];
	ap[0]->fts_link = NULL;
	return (head);
}

static FTSENT *
fts_alloc(FTS *sp, const char *name, size_t namelen)
{
	FTSENT *p;
	size_t len;

	/*
	 * The file name is a variable length array and no stat structure is
	 * necessary if the user has set the nostat bit.  Allocate the FTSENT
	 * structure, the file name and the stat structure in one chunk, but
	 * be careful that the stat structure is reasonably aligned.  Since the
	 * fts_name field is declared to be of size 1, the fts_name pointer is
	 * namelen + 2 before the first possible address of the stat structure.
	 */
	len = sizeof(FTSENT) + namelen;
	if (!ISSET(FTS_NOSTAT))
		len += sizeof(struct stat) + ALIGNBYTES;
	if ((p = calloc(1, len)) == NULL)
		return (NULL);

	p->fts_path = sp->fts_path;
	p->fts_namelen = namelen;
	p->fts_instr = FTS_NOINSTR;
	if (!ISSET(FTS_NOSTAT))
		p->fts_statp = (struct stat *)ALIGN(p->fts_name + namelen + 2);
	memcpy(p->fts_name, name, namelen);

	return (p);
}

static void
fts_lfree(FTSENT *head)
{
	FTSENT *p;

	/* Free a linked list of structures. */
	while ((p = head)) {
		head = head->fts_link;
		free(p);
	}
}

/*
 * Allow essentially unlimited paths; find, rm, ls should all work on any tree.
 * Most systems will allow creation of paths much longer than PATH_MAX, even
 * though the kernel won't resolve them.  Add the size (not just what's needed)
 * plus 256 bytes so don't realloc the path 2 bytes at a time.
 */
static int
fts_palloc(FTS *sp, size_t more)
{
	char *p;

	/*
	 * Check for possible wraparound.
	 */
	more += 256;
	if (sp->fts_pathlen + more < sp->fts_pathlen) {
		free(sp->fts_path);
		sp->fts_path = NULL;
		errno = ENAMETOOLONG;
		return (1);
	}
	p = recallocarray(sp->fts_path, sp->fts_pathlen,
	    sp->fts_pathlen + more, 1);
	if (p == NULL) {
		free(sp->fts_path);
		sp->fts_path = NULL;
		return (1);
	}
	sp->fts_pathlen += more;
	sp->fts_path = p;
	return (0);
}

/*
 * When the path is realloc'd, have to fix all of the pointers in structures
 * already returned.
 */
static void
fts_padjust(FTS *sp, FTSENT *head)
{
	FTSENT *p;
	char *addr = sp->fts_path;

#define	ADJUST(p) {							\
	if ((p)->fts_accpath != (p)->fts_name) {			\
		(p)->fts_accpath =					\
		    (char *)addr + ((p)->fts_accpath - (p)->fts_path);	\
	}								\
	(p)->fts_path = addr;						\
}
	/* Adjust the current set of children. */
	for (p = sp->fts_child; p; p = p->fts_link)
		ADJUST(p);

	/* Adjust the rest of the tree, including the current level. */
	for (p = head; p->fts_level >= FTS_ROOTLEVEL;) {
		ADJUST(p);
		p = p->fts_link ? p->fts_link : p->fts_parent;
	}
}

static size_t
fts_maxarglen(char * const *argv)
{
	size_t len, max;

	for (max = 0; *argv; ++argv)
		if ((len = strlen(*argv)) > max)
			max = len;
	return (max + 1);
}

/*
 * Change to dir specified by fd or p->fts_accpath without getting
 * tricked by someone changing the world out from underneath us.
 * Assumes p->fts_dev and p->fts_ino are filled in.
 */
static int
fts_safe_changedir(FTS *sp, FTSENT *p, int fd, const char *path)
{
	int ret, oerrno, newfd;
	struct stat sb;

	newfd = fd;
	if (ISSET(FTS_NOCHDIR))
		return (0);
	if (fd == -1 && (newfd = open(path, O_RDONLY|O_DIRECTORY|O_CLOEXEC)) == -1)
		return (-1);
	if (fstat(newfd, &sb) == -1) {
		ret = -1;
		goto bail;
	}
	if (p->fts_dev != sb.st_dev || p->fts_ino != sb.st_ino) {
		errno = ENOENT;		/* disinformation */
		ret = -1;
		goto bail;
	}
	ret = fchdir(newfd);
bail:
	oerrno = errno;
	if (fd == -1)
		(void)close(newfd);
	errno = oerrno;
	return (ret);
}
