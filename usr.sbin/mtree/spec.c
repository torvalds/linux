/*	$NetBSD: spec.c,v 1.6 1995/03/07 21:12:12 cgd Exp $	*/
/*	$OpenBSD: spec.c,v 1.30 2023/08/11 05:07:28 guenther Exp $	*/

/*-
 * Copyright (c) 1989, 1993
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

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <vis.h>
#include "mtree.h"
#include "extern.h"

int lineno;				/* Current spec line number. */

static void	 set(char *, NODE *);
static void	 unset(char *, NODE *);

NODE *
spec(void)
{
	NODE *centry, *last;
	char *p;
	NODE ginfo, *root;
	int c_cur, c_next;
	char *buf, *tbuf = NULL;
	size_t len;

	last = root = NULL;
	bzero(&ginfo, sizeof(ginfo));
	centry = &ginfo;
	c_cur = c_next = 0;
	for (lineno = 1; (buf = fgetln(stdin, &len));
	    ++lineno, c_cur = c_next, c_next = 0) {
		/* Null-terminate the line. */
		if (buf[len - 1] == '\n') {
			buf[--len] = '\0';
		} else {
			/* EOF with no newline. */
			tbuf = malloc(len + 1);
			memcpy(tbuf, buf, len);
			tbuf[len] = '\0';
			buf = tbuf;
		}

		/* Skip leading whitespace. */
		for (p = buf; isspace((unsigned char)*p); p++)
			;

		/* If nothing but whitespace or comment char, continue. */
		if (*p == '\0' || *p == '#')
			continue;

		/* See if next line is continuation line. */
		if (buf[len - 1] == '\\') {
			c_next = 1;
			if (--len == 0)
				continue;
			buf[len] = '\0';
		}

#ifdef DEBUG
		(void)fprintf(stderr, "line %d: {%s}\n", lineno, p);
#endif
		if (c_cur) {
			set(p, centry);
			continue;
		}

		/* Grab file name, "$", "set", or "unset". */
		if ((p = strtok(p, "\n\t ")) == NULL)
			error("missing field");

		if (p[0] == '/')
			switch(p[1]) {
			case 's':
				if (strcmp(p + 1, "set"))
					break;
				set(NULL, &ginfo);
				continue;
			case 'u':
				if (strcmp(p + 1, "unset"))
					break;
				unset(NULL, &ginfo);
				continue;
			}

		if (strchr(p, '/'))
			error("slash character in file name");

		if (!strcmp(p, "..")) {
			/* Don't go up, if haven't gone down. */
			if (!root)
				goto noparent;
			if (last->type != F_DIR || last->flags & F_DONE) {
				if (last == root)
					goto noparent;
				last = last->parent;
			}
			last->flags |= F_DONE;
			continue;

noparent:		error("no parent node");
		}

		len = strlen(p) + 1;	/* NUL in struct _node */
		if ((centry = calloc(1, sizeof(NODE) + len - 1)) == NULL)
			error("%s", strerror(errno));
		*centry = ginfo;
#define	MAGIC	"?*["
		if (strpbrk(p, MAGIC))
			centry->flags |= F_MAGIC;
		if (strunvis(centry->name, p) == -1) {
			fprintf(stderr,
			    "mtree: filename (%s) encoded incorrectly\n", p);
			strlcpy(centry->name, p, len);
		}
		set(NULL, centry);

		if (!root) {
			last = root = centry;
			root->parent = root;
		} else if (last->type == F_DIR && !(last->flags & F_DONE)) {
			centry->parent = last;
			last = last->child = centry;
		} else {
			centry->parent = last->parent;
			centry->prev = last;
			last = last->next = centry;
		}
	}
	free(tbuf);
	return (root);
}

static void
set(char *t, NODE *ip)
{
	int type;
	char *kw, *val = NULL;
	void *m;
	int value;
	u_int32_t fset, fclr;
	char *ep;
	size_t len;

	for (; (kw = strtok(t, "= \t\n")); t = NULL) {
		ip->flags |= type = parsekey(kw, &value);
		if (value && (val = strtok(NULL, " \t\n")) == NULL)
			error("missing value");
		switch(type) {
		case F_CKSUM:
			ip->cksum = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid checksum %s", val);
			break;
		case F_MD5:
			ip->md5digest = strdup(val);
			if (!ip->md5digest)
				error("%s", strerror(errno));
			break;
		case F_FLAGS:
			if (!strcmp(val, "none")) {
				ip->file_flags = 0;
				break;
			}
			if (strtofflags(&val, &fset, &fclr))
				error("%s", strerror(errno));
			ip->file_flags = fset;
			break;
		case F_GID:
			ip->st_gid = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid gid %s", val);
			break;
		case F_GNAME:
			if (gid_from_group(val, &ip->st_gid) == -1)
			    error("unknown group %s", val);
			break;
		case F_IGN:
			/* just set flag bit */
			break;
		case F_MODE:
			if ((m = setmode(val)) == NULL)
				error("invalid file mode %s", val);
			ip->st_mode = getmode(m, 0);
			free(m);
			break;
		case F_NLINK:
			ip->st_nlink = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid link count %s", val);
			break;
		case F_RMD160:
			ip->rmd160digest = strdup(val);
			if (!ip->rmd160digest)
				error("%s", strerror(errno));
			break;
		case F_SHA1:
			ip->sha1digest = strdup(val);
			if (!ip->sha1digest)
				error("%s", strerror(errno));
			break;
		case F_SHA256:
			ip->sha256digest = strdup(val);
			if (!ip->sha256digest)
				error("%s", strerror(errno));
			break;
		case F_SIZE:
			ip->st_size = strtoll(val, &ep, 10);
			if (*ep)
				error("invalid size %s", val);
			break;
		case F_SLINK:
			len = strlen(val) + 1;
			if ((ip->slink = malloc(len)) == NULL)
				error("%s", strerror(errno));
			if (strunvis(ip->slink, val) == -1) {
				fprintf(stderr,
				    "mtree: filename (%s) encoded incorrectly\n", val);
				strlcpy(ip->slink, val, len);
			}
			break;
		case F_TIME:
			ip->st_mtim.tv_sec = strtoull(val, &ep, 10);
			if (*ep != '.')
				error("invalid time %s", val);
			val = ep + 1;
			ip->st_mtim.tv_nsec = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid time %s", val);
			break;
		case F_TYPE:
			switch(*val) {
			case 'b':
				if (!strcmp(val, "block"))
					ip->type = F_BLOCK;
				break;
			case 'c':
				if (!strcmp(val, "char"))
					ip->type = F_CHAR;
				break;
			case 'd':
				if (!strcmp(val, "dir"))
					ip->type = F_DIR;
				break;
			case 'f':
				if (!strcmp(val, "file"))
					ip->type = F_FILE;
				if (!strcmp(val, "fifo"))
					ip->type = F_FIFO;
				break;
			case 'l':
				if (!strcmp(val, "link"))
					ip->type = F_LINK;
				break;
			case 's':
				if (!strcmp(val, "socket"))
					ip->type = F_SOCK;
				break;
			default:
				error("unknown file type %s", val);
			}
			break;
		case F_UID:
			ip->st_uid = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid uid %s", val);
			break;
		case F_UNAME:
			if (uid_from_user(val, &ip->st_uid) == -1)
			    error("unknown user %s", val);
			break;
		}
	}
}

static void
unset(char *t, NODE *ip)
{
	char *p;

	while ((p = strtok(t, "\n\t ")))
		ip->flags &= ~parsekey(p, NULL);
}
