/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Sean C. Farley <scf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <libutil.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int lockfd = -1;
static char group_dir[PATH_MAX];
static char group_file[PATH_MAX];
static char tempname[PATH_MAX];
static int initialized;
static size_t grmemlen(const struct group *, const char *, int *);
static struct group *grcopy(const struct group *gr, char *mem, const char *, int ndx);

/*
 * Initialize statics
 */
int
gr_init(const char *dir, const char *group)
{

	if (dir == NULL) {
		strcpy(group_dir, _PATH_ETC);
	} else {
		if (strlen(dir) >= sizeof(group_dir)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(group_dir, dir);
	}

	if (group == NULL) {
		if (dir == NULL) {
			strcpy(group_file, _PATH_GROUP);
		} else if (snprintf(group_file, sizeof(group_file), "%s/group",
			group_dir) > (int)sizeof(group_file)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
	} else {
		if (strlen(group) >= sizeof(group_file)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(group_file, group);
	}

	initialized = 1;
	return (0);
}

/*
 * Lock the group file
 */
int
gr_lock(void)
{
	if (*group_file == '\0')
		return (-1);

	for (;;) {
		struct stat st;

		lockfd = flopen(group_file, O_RDONLY|O_NONBLOCK|O_CLOEXEC, 0);
		if (lockfd == -1) {
			if (errno == EWOULDBLOCK) {
				errx(1, "the group file is busy");
			} else {
				err(1, "could not lock the group file: ");
			}
		}
		if (fstat(lockfd, &st) == -1)
			err(1, "fstat() failed: ");
		if (st.st_nlink != 0)
			break;
		close(lockfd);
		lockfd = -1;
	}
	return (lockfd);
}

/*
 * Create and open a presmuably safe temp file for editing group data
 */
int
gr_tmp(int mfd)
{
	char buf[8192];
	ssize_t nr;
	const char *p;
	int tfd;

	if (*group_file == '\0')
		return (-1);
	if ((p = strrchr(group_file, '/')))
		++p;
	else
		p = group_file;
	if (snprintf(tempname, sizeof(tempname), "%.*sgroup.XXXXXX",
		(int)(p - group_file), group_file) >= (int)sizeof(tempname)) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	if ((tfd = mkostemp(tempname, 0)) == -1)
		return (-1);
	if (mfd != -1) {
		while ((nr = read(mfd, buf, sizeof(buf))) > 0)
			if (write(tfd, buf, (size_t)nr) != nr)
				break;
		if (nr != 0) {
			unlink(tempname);
			*tempname = '\0';
			close(tfd);
			return (-1);
		}
	}
	return (tfd);
}

/*
 * Copy the group file from one descriptor to another, replacing, deleting
 * or adding a single record on the way.
 */
int
gr_copy(int ffd, int tfd, const struct group *gr, struct group *old_gr)
{
	char *buf, *end, *line, *p, *q, *r, *tmp;
	struct group *fgr;
	const struct group *sgr;
	size_t len, size;
	int eof, readlen;
	char t;

	if (old_gr == NULL && gr == NULL)
		return(-1);

	sgr = old_gr;
	/* deleting a group */
	if (gr == NULL) {
		line = NULL;
	} else {
		if ((line = gr_make(gr)) == NULL)
			return (-1);
	}

	/* adding a group */
	if (sgr == NULL)
		sgr = gr;

	/* initialize the buffer */
	if ((buf = malloc(size = 1024)) == NULL)
		goto err;

	eof = 0;
	len = 0;
	p = q = end = buf;
	for (;;) {
		/* find the end of the current line */
		for (p = q; q < end && *q != '\0'; ++q)
			if (*q == '\n')
				break;

		/* if we don't have a complete line, fill up the buffer */
		if (q >= end) {
			if (eof)
				break;
			while ((size_t)(q - p) >= size) {
				if ((tmp = reallocarray(buf, 2, size)) == NULL) {
					warnx("group line too long");
					goto err;
				}
				p = tmp + (p - buf);
				q = tmp + (q - buf);
				end = tmp + (end - buf);
				buf = tmp;
				size = size * 2;
			}
			if (p < end) {
				q = memmove(buf, p, end -p);
				end -= p - buf;
			} else {
				p = q = end = buf;
			}
			readlen = read(ffd, end, size - (end - buf));
			if (readlen == -1)
				goto err;
			else
				len = (size_t)readlen;
			if (len == 0 && p == buf)
				break;
			end += len;
			len = end - buf;
			if (len < size) {
				eof = 1;
				if (len > 0 && buf[len -1] != '\n')
					++len, *end++ = '\n';
			}
			continue;
		}

		/* is it a blank line or a comment? */
		for (r = p; r < q && isspace(*r); ++r)
			/* nothing */;
		if (r == q || *r == '#') {
			/* yep */
			if (write(tfd, p, q -p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}

		/* is it the one we're looking for? */

		t = *q;
		*q = '\0';

		fgr = gr_scan(r);

		/* fgr is either a struct group for the current line,
		 * or NULL if the line is malformed.
		 */

		*q = t;
		if (fgr == NULL || fgr->gr_gid != sgr->gr_gid) {
			/* nope */
			if (fgr != NULL)
				free(fgr);
			if (write(tfd, p, q - p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}
		if (old_gr && !gr_equal(fgr, old_gr)) {
			warnx("entry inconsistent");
			free(fgr);
			errno = EINVAL; /* hack */
			goto err;
		}
		free(fgr);

		/* it is, replace or remove it */
		if (line != NULL) {
			len = strlen(line);
			if (write(tfd, line, len) != (int) len)
				goto err;
		} else {
			/* when removed, avoid the \n */
			q++;
		}
		/* we're done, just copy the rest over */
		for (;;) {
			if (write(tfd, q, end - q) != end - q)
				goto err;
			q = buf;
			readlen = read(ffd, buf, size);
			if (readlen == 0)
				break;
			else
				len = (size_t)readlen;
			if (readlen == -1)
				goto err;
			end = buf + len;
		}
		goto done;
	}

	/* if we got here, we didn't find the old entry */
	if (line == NULL) {
		errno = ENOENT;
		goto err;
	}
	len = strlen(line);
	if ((size_t)write(tfd, line, len) != len ||
	   write(tfd, "\n", 1) != 1)
		goto err;
 done:
	free(line);
	free(buf);
	return (0);
 err:
	free(line);
	free(buf);
	return (-1);
}

/*
 * Regenerate the group file
 */
int
gr_mkdb(void)
{
	int fd;

	if (chmod(tempname, 0644) != 0)
		return (-1);

	if (rename(tempname, group_file) != 0)
		return (-1);

	/*
	 * Make sure new group file is safe on disk. To improve performance we
	 * will call fsync() to the directory where file lies
	 */
	if ((fd = open(group_dir, O_RDONLY|O_DIRECTORY)) == -1)
		return (-1);

	if (fsync(fd) != 0) {
		close(fd);
		return (-1);
	}

	close(fd);
	return(0);
}

/*
 * Clean up. Preserves errno for the caller's convenience.
 */
void
gr_fini(void)
{
	int serrno;

	if (!initialized)
		return;
	initialized = 0;
	serrno = errno;
	if (*tempname != '\0') {
		unlink(tempname);
		*tempname = '\0';
	}
	if (lockfd != -1)
		close(lockfd);
	errno = serrno;
}

/*
 * Compares two struct group's.
 */
int
gr_equal(const struct group *gr1, const struct group *gr2)
{

	/* Check that the non-member information is the same. */
	if (gr1->gr_name == NULL || gr2->gr_name == NULL) {
		if (gr1->gr_name != gr2->gr_name)
			return (false);
	} else if (strcmp(gr1->gr_name, gr2->gr_name) != 0)
		return (false);
	if (gr1->gr_passwd == NULL || gr2->gr_passwd == NULL) {
		if (gr1->gr_passwd != gr2->gr_passwd)
			return (false);
	} else if (strcmp(gr1->gr_passwd, gr2->gr_passwd) != 0)
		return (false);
	if (gr1->gr_gid != gr2->gr_gid)
		return (false);

	/*
	 * Check all members in both groups.
	 * getgrnam can return gr_mem with a pointer to NULL.
	 * gr_dup and gr_add strip out this superfluous NULL, setting
	 * gr_mem to NULL for no members.
	*/
	if (gr1->gr_mem != NULL && gr2->gr_mem != NULL) {
		int i;

		for (i = 0;
		    gr1->gr_mem[i] != NULL && gr2->gr_mem[i] != NULL; i++) {
			if (strcmp(gr1->gr_mem[i], gr2->gr_mem[i]) != 0)
				return (false);
		}
		if (gr1->gr_mem[i] != NULL || gr2->gr_mem[i] != NULL)
			return (false);
	} else if (gr1->gr_mem != NULL && gr1->gr_mem[0] != NULL) {
		return (false);
	} else if (gr2->gr_mem != NULL && gr2->gr_mem[0] != NULL) {
		return (false);
	}

	return (true);
}

/*
 * Make a group line out of a struct group.
 */
char *
gr_make(const struct group *gr)
{
	const char *group_line_format = "%s:%s:%ju:";
	const char *sep;
	char *line;
	char *p;
	size_t line_size;
	int ndx;

	/* Calculate the length of the group line. */
	line_size = snprintf(NULL, 0, group_line_format, gr->gr_name,
	    gr->gr_passwd, (uintmax_t)gr->gr_gid) + 1;
	if (gr->gr_mem != NULL) {
		for (ndx = 0; gr->gr_mem[ndx] != NULL; ndx++)
			line_size += strlen(gr->gr_mem[ndx]) + 1;
		if (ndx > 0)
			line_size--;
	}

	/* Create the group line and fill it. */
	if ((line = p = malloc(line_size)) == NULL)
		return (NULL);
	p += sprintf(p, group_line_format, gr->gr_name, gr->gr_passwd,
	    (uintmax_t)gr->gr_gid);
	if (gr->gr_mem != NULL) {
		sep = "";
		for (ndx = 0; gr->gr_mem[ndx] != NULL; ndx++) {
			p = stpcpy(p, sep);
			p = stpcpy(p, gr->gr_mem[ndx]);
			sep = ",";
		}
	}

	return (line);
}

/*
 * Duplicate a struct group.
 */
struct group *
gr_dup(const struct group *gr)
{
	return (gr_add(gr, NULL));
}
/*
 * Add a new member name to a struct group.
 */
struct group *
gr_add(const struct group *gr, const char *newmember)
{
	char *mem;
	size_t len;
	int num_mem;

	num_mem = 0;
	len = grmemlen(gr, newmember, &num_mem);
	/* Create new group and copy old group into it. */
	if ((mem = malloc(len)) == NULL)
		return (NULL);
	return (grcopy(gr, mem, newmember, num_mem));
}

/* It is safer to walk the pointers given at gr_mem since there is no
 * guarantee the gr_mem + strings are contiguous in the given struct group
 * but compactify the new group into the following form.
 *
 * The new struct is laid out like this in memory. The example given is
 * for a group with two members only.
 *
 * {
 * (char *name)
 * (char *passwd)
 * (int gid)
 * (gr_mem * newgrp + sizeof(struct group) + sizeof(**)) points to gr_mem area
 * gr_mem area
 * (member1 *) 
 * (member2 *)
 * (NULL)
 * (name string)
 * (passwd string)
 * (member1 string)
 * (member2 string)
 * }
 */
/*
 * Copy the contents of a group plus given name to a preallocated group struct
 */
static struct group *
grcopy(const struct group *gr, char *dst, const char *name, int ndx)
{
	int i;
	struct group *newgr;

	newgr = (struct group *)(void *)dst;	/* avoid alignment warning */
	dst += sizeof(*newgr);
	if (ndx != 0) {
		newgr->gr_mem = (char **)(void *)(dst);	/* avoid alignment warning */
		dst += (ndx + 1) * sizeof(*newgr->gr_mem);
	} else
		newgr->gr_mem = NULL;
	if (gr->gr_name != NULL) {
		newgr->gr_name = dst;
		dst = stpcpy(dst, gr->gr_name) + 1;
	} else
		newgr->gr_name = NULL;
	if (gr->gr_passwd != NULL) {
		newgr->gr_passwd = dst;
		dst = stpcpy(dst, gr->gr_passwd) + 1;
	} else
		newgr->gr_passwd = NULL;
	newgr->gr_gid = gr->gr_gid;
	i = 0;
	/* Original group struct might have a NULL gr_mem */
	if (gr->gr_mem != NULL) {
		for (; gr->gr_mem[i] != NULL; i++) {
			newgr->gr_mem[i] = dst;
			dst = stpcpy(dst, gr->gr_mem[i]) + 1;
		}
	}
	/* If name is not NULL, newgr->gr_mem is known to be not NULL */
	if (name != NULL) {
		newgr->gr_mem[i++] = dst;
		dst = stpcpy(dst, name) + 1;
	}
	/* if newgr->gr_mem is not NULL add NULL marker */
	if (newgr->gr_mem != NULL)
		newgr->gr_mem[i] = NULL;

	return (newgr);
}

/*
 *  Calculate length of a struct group + given name
 */
static size_t
grmemlen(const struct group *gr, const char *name, int *num_mem)
{
	size_t len;
	int i;

	if (gr == NULL)
		return (0);
	/* Calculate size of the group. */
	len = sizeof(*gr);
	if (gr->gr_name != NULL)
		len += strlen(gr->gr_name) + 1;
	if (gr->gr_passwd != NULL)
		len += strlen(gr->gr_passwd) + 1;
	i = 0;
	if (gr->gr_mem != NULL) {
		for (; gr->gr_mem[i] != NULL; i++) {
			len += strlen(gr->gr_mem[i]) + 1;
			len += sizeof(*gr->gr_mem);
		}
	}
	if (name != NULL) {
		i++;
		len += strlen(name) + 1;
		len += sizeof(*gr->gr_mem);
	}
	/* Allow for NULL pointer */
	if (i != 0)
		len += sizeof(*gr->gr_mem);
	*num_mem = i;
	return(len);
}

/*
 * Scan a line and place it into a group structure.
 */
static bool
__gr_scan(char *line, struct group *gr)
{
	char *loc;
	int ndx;

	/* Assign non-member information to structure. */
	gr->gr_name = line;
	if ((loc = strchr(line, ':')) == NULL)
		return (false);
	*loc = '\0';
	gr->gr_passwd = loc + 1;
	if (*gr->gr_passwd == ':')
		*gr->gr_passwd = '\0';
	else {
		if ((loc = strchr(loc + 1, ':')) == NULL)
			return (false);
		*loc = '\0';
	}
	if (sscanf(loc + 1, "%u", &gr->gr_gid) != 1)
		return (false);

	/* Assign member information to structure. */
	if ((loc = strchr(loc + 1, ':')) == NULL)
		return (false);
	line = loc + 1;
	gr->gr_mem = NULL;
	ndx = 0;
	do {
		gr->gr_mem = reallocf(gr->gr_mem, sizeof(*gr->gr_mem) *
		    (ndx + 1));
		if (gr->gr_mem == NULL)
			return (false);

		/* Skip locations without members (i.e., empty string). */
		do {
			gr->gr_mem[ndx] = strsep(&line, ",");
		} while (gr->gr_mem[ndx] != NULL && *gr->gr_mem[ndx] == '\0');
	} while (gr->gr_mem[ndx++] != NULL);

	return (true);
}

/*
 * Create a struct group from a line.
 */
struct group *
gr_scan(const char *line)
{
	struct group gr;
	char *line_copy;
	struct group *new_gr;

	if ((line_copy = strdup(line)) == NULL)
		return (NULL);
	if (!__gr_scan(line_copy, &gr)) {
		free(line_copy);
		return (NULL);
	}
	new_gr = gr_dup(&gr);
	free(line_copy);
	if (gr.gr_mem != NULL)
		free(gr.gr_mem);

	return (new_gr);
}
