/*	$NetBSD: compare.c,v 1.11 1996/09/05 09:56:48 mycroft Exp $	*/
/*	$OpenBSD: compare.c,v 1.31 2024/04/23 13:34:51 jsg Exp $	*/

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

#include <sys/stat.h>
#include <fcntl.h>
#include <fts.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <md5.h>
#include <rmd160.h>
#include <sha1.h>
#include <sha2.h>
#include "mtree.h"
#include "extern.h"

extern int lflag, tflag, uflag;

static char *ftype(u_int);

#define	INDENTNAMELEN	8
#define	LABEL \
	if (!label++) { \
		len = printf("%s: ", RP(p)); \
		if (len > INDENTNAMELEN) { \
			tab = "\t"; \
			(void)printf("\n"); \
		} else { \
			tab = ""; \
			(void)printf("%*s", INDENTNAMELEN - (int)len, ""); \
		} \
	}

#define REPLACE_COMMA(x)						\
	do {								\
		char *l;						\
		for (l = x; *l; l++) {					\
			if (*l == ',')					\
				*l = ' ';				\
		}							\
	} while (0)							\

int
compare(char *name, NODE *s, FTSENT *p)
{
	u_int32_t len, val;
	int fd, label;
	char *cp, *tab = "";

	label = 0;
	switch(s->type) {
	case F_BLOCK:
		if (!S_ISBLK(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_CHAR:
		if (!S_ISCHR(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_DIR:
		if (!S_ISDIR(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_FIFO:
		if (!S_ISFIFO(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_FILE:
		if (!S_ISREG(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_LINK:
		if (!S_ISLNK(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_SOCK:
		if (!S_ISSOCK(p->fts_statp->st_mode)) {
typeerr:		LABEL;
			(void)printf("\ttype (%s, %s)\n",
			    ftype(s->type), inotype(p->fts_statp->st_mode));
		}
		break;
	}
	/* Set the uid/gid first, then set the mode. */
	if (s->flags & (F_UID | F_UNAME) && s->st_uid != p->fts_statp->st_uid) {
		LABEL;
		(void)printf("%suser (%u, %u",
		    tab, s->st_uid, p->fts_statp->st_uid);
		if (uflag)
			if (chown(p->fts_accpath, s->st_uid, -1))
				(void)printf(", not modified: %s)\n",
				    strerror(errno));
			else
				(void)printf(", modified)\n");
		else
			(void)printf(")\n");
		tab = "\t";
	}
	if (s->flags & (F_GID | F_GNAME) && s->st_gid != p->fts_statp->st_gid) {
		LABEL;
		(void)printf("%sgid (%u, %u",
		    tab, s->st_gid, p->fts_statp->st_gid);
		if (uflag)
			if (chown(p->fts_accpath, -1, s->st_gid))
				(void)printf(", not modified: %s)\n",
				    strerror(errno));
			else
				(void)printf(", modified)\n");
		else
			(void)printf(")\n");
		tab = "\t";
	}
	if (s->flags & F_MODE &&
	    s->st_mode != (p->fts_statp->st_mode & MBITS)) {
		if (lflag) {
			mode_t tmode, mode;

			tmode = s->st_mode;
			mode = p->fts_statp->st_mode & MBITS;
			/*
			 * if none of the suid/sgid/etc bits are set,
			 * then if the mode is a subset of the target,
			 * skip.
			 */
			if (!((tmode & ~(S_IRWXU|S_IRWXG|S_IRWXO)) ||
			    (mode & ~(S_IRWXU|S_IRWXG|S_IRWXO))))
				if ((mode | tmode) == tmode)
					goto skip;
		}
		LABEL;
		(void)printf("%spermissions (%#o, %#o",
		    tab, s->st_mode, p->fts_statp->st_mode & MBITS);
		if (uflag)
			if (chmod(p->fts_accpath, s->st_mode))
				(void)printf(", not modified: %s)\n",
				    strerror(errno));
			else
				(void)printf(", modified)\n");
		else
			(void)printf(")\n");
		tab = "\t";
	skip:
		;
	}
	if (s->flags & F_NLINK && s->type != F_DIR &&
	    s->st_nlink != p->fts_statp->st_nlink) {
		LABEL;
		(void)printf("%slink count (%u, %u)\n",
		    tab, s->st_nlink, p->fts_statp->st_nlink);
		tab = "\t";
	}
	if (s->flags & F_SIZE && s->st_size != p->fts_statp->st_size) {
		LABEL;
		(void)printf("%ssize (%lld, %lld)\n",
		    tab, (long long)s->st_size,
		    (long long)p->fts_statp->st_size);
		tab = "\t";
	}
	/*
	 * XXX
	 * Since utimes(2) only takes a timeval, there's no point in
	 * comparing the low bits of the timespec nanosecond field.  This
	 * will only result in mismatches that we can never fix.
	 *
	 * Doesn't display microsecond differences.
	 */
	if (s->flags & F_TIME) {
		struct timespec ts[2];

		ts[0] = s->st_mtim;
		ts[1] = p->fts_statp->st_mtim;
		if (ts[0].tv_sec != ts[1].tv_sec ||
		    ts[0].tv_nsec != ts[1].tv_nsec) {
			LABEL;
			(void)printf("%smodification time (%.24s, ",
			    tab, ctime(&s->st_mtime));
			(void)printf("%.24s", ctime(&p->fts_statp->st_mtime));
			if (tflag) {
				ts[1] = ts[0];
				if (utimensat(AT_FDCWD, p->fts_accpath, ts, 0))
					(void)printf(", not modified: %s)\n",
					    strerror(errno));
				else
					(void)printf(", modified)\n");
			} else
				(void)printf(")\n");
			tab = "\t";
		}
	}
	if (s->flags & F_CKSUM) {
		if ((fd = open(p->fts_accpath, MTREE_O_FLAGS)) == -1) {
			LABEL;
			(void)printf("%scksum: %s: %s\n",
			    tab, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else if (crc(fd, &val, &len)) {
			(void)close(fd);
			LABEL;
			(void)printf("%scksum: %s: %s\n",
			    tab, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			(void)close(fd);
			if (s->cksum != val) {
				LABEL;
				(void)printf("%scksum (%u, %u)\n",
				    tab, s->cksum, val);
			}
			tab = "\t";
		}
	}
	if (s->flags & F_MD5) {
		char *new_digest, buf[MD5_DIGEST_STRING_LENGTH];

		new_digest = MD5File(p->fts_accpath, buf);
		if (!new_digest) {
			LABEL;
			printf("%sMD5File: %s: %s\n", tab, p->fts_accpath,
			       strerror(errno));
			tab = "\t";
		} else if (strcmp(new_digest, s->md5digest)) {
			LABEL;
			printf("%sMD5 (%s, %s)\n", tab, s->md5digest,
			       new_digest);
			tab = "\t";
		}
	}
	if (s->flags & F_RMD160) {
		char *new_digest, buf[RMD160_DIGEST_STRING_LENGTH];

		new_digest = RMD160File(p->fts_accpath, buf);
		if (!new_digest) {
			LABEL;
			printf("%sRMD160File: %s: %s\n", tab, p->fts_accpath,
			       strerror(errno));
			tab = "\t";
		} else if (strcmp(new_digest, s->rmd160digest)) {
			LABEL;
			printf("%sRMD160 (%s, %s)\n", tab, s->rmd160digest,
			       new_digest);
			tab = "\t";
		}
	}
	if (s->flags & F_SHA1) {
		char *new_digest, buf[SHA1_DIGEST_STRING_LENGTH];

		new_digest = SHA1File(p->fts_accpath, buf);
		if (!new_digest) {
			LABEL;
			printf("%sSHA1File: %s: %s\n", tab, p->fts_accpath,
			       strerror(errno));
			tab = "\t";
		} else if (strcmp(new_digest, s->sha1digest)) {
			LABEL;
			printf("%sSHA1 (%s, %s)\n", tab, s->sha1digest,
			       new_digest);
			tab = "\t";
		}
	}
	if (s->flags & F_SHA256) {
		char *new_digest, buf[SHA256_DIGEST_STRING_LENGTH];

		new_digest = SHA256File(p->fts_accpath, buf);
		if (!new_digest) {
			LABEL;
			printf("%sSHA256File: %s: %s\n", tab, p->fts_accpath,
			       strerror(errno));
			tab = "\t";
		} else if (strcmp(new_digest, s->sha256digest)) {
			LABEL;
			printf("%sSHA256 (%s, %s)\n", tab, s->sha256digest,
			       new_digest);
			tab = "\t";
		}
	}
	if (s->flags & F_SLINK && strcmp(cp = rlink(name), s->slink)) {
		LABEL;
		(void)printf("%slink ref (%s, %s)\n", tab, cp, s->slink);
	}
	if (s->flags & F_FLAGS && s->file_flags != p->fts_statp->st_flags) {
		char *db_flags = NULL;
		char *cur_flags = NULL;

		if ((db_flags = fflagstostr(s->file_flags)) == NULL ||
		    (cur_flags = fflagstostr(p->fts_statp->st_flags)) == NULL) {
			LABEL;
			(void)printf("%sflags: %s %s\n", tab, p->fts_accpath,
				     strerror(errno));
			tab = "\t";
			free(db_flags);
			free(cur_flags);
		} else {
			LABEL;
			REPLACE_COMMA(db_flags);
			REPLACE_COMMA(cur_flags);
			printf("%sflags (%s, %s", tab, (*db_flags == '\0') ?
						  "-" : db_flags,
						  (*cur_flags == '\0') ?
						  "-" : cur_flags);
			tab = "\t";
			if (uflag)
				if (chflags(p->fts_accpath, s->file_flags))
					(void)printf(", not modified: %s)\n",
						strerror(errno));
				else
					(void)printf(", modified)\n");
			else
				(void)printf(")\n");
			tab = "\t";

			free(db_flags);
			free(cur_flags);
		}
	}
	return (label);
}

char *
inotype(u_int type)
{
	switch(type & S_IFMT) {
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
		return ("link");
	case S_IFSOCK:
		return ("socket");
	default:
		return ("unknown");
	}
	/* NOTREACHED */
}

static char *
ftype(u_int type)
{
	switch(type) {
	case F_BLOCK:
		return ("block");
	case F_CHAR:
		return ("char");
	case F_DIR:
		return ("dir");
	case F_FIFO:
		return ("fifo");
	case F_FILE:
		return ("file");
	case F_LINK:
		return ("link");
	case F_SOCK:
		return ("socket");
	default:
		return ("unknown");
	}
	/* NOTREACHED */
}

char *
rlink(char *name)
{
	static char lbuf[PATH_MAX];
	int len;

	if ((len = readlink(name, lbuf, sizeof(lbuf)-1)) == -1)
		error("%s: %s", name, strerror(errno));
	lbuf[len] = '\0';
	return (lbuf);
}
