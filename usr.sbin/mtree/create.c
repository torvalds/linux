/*	$NetBSD: create.c,v 1.11 1996/09/05 09:24:19 mycroft Exp $	*/
/*	$OpenBSD: create.c,v 1.36 2023/08/11 05:07:28 guenther Exp $	*/

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
#include <time.h>
#include <fcntl.h>
#include <fts.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <vis.h>
#include <md5.h>
#include <rmd160.h>
#include <sha1.h>
#include <sha2.h>
#include "mtree.h"
#include "extern.h"

#define	INDENTNAMELEN	15
#define	MAXLINELEN	80

extern u_int32_t crc_total;
extern int ftsoptions;
extern int dflag, iflag, nflag, sflag;
extern u_int keys;
extern char fullpath[PATH_MAX];

static gid_t gid;
static uid_t uid;
static mode_t mode;

static void	output(int, int *, const char *, ...)
		    __attribute__((__format__ (printf, 3, 4)));
static int	statd(FTS *, FTSENT *, uid_t *, gid_t *, mode_t *);
static void	statf(int, FTSENT *);

void
cwalk(void)
{
	FTS *t;
	FTSENT *p;
	time_t clock;
	char *argv[2], host[HOST_NAME_MAX+1];
	int indent = 0;

	(void)time(&clock);
	(void)gethostname(host, sizeof(host));
	(void)printf(
	    "#\t   user: %s\n#\tmachine: %s\n#\t   tree: %s\n#\t   date: %s",
	    getlogin(), host, fullpath, ctime(&clock));

	argv[0] = ".";
	argv[1] = NULL;
	if ((t = fts_open(argv, ftsoptions, dsort)) == NULL)
		error("fts_open: %s", strerror(errno));
	while ((p = fts_read(t))) {
		if (iflag)
			indent = p->fts_level * 4;
		switch(p->fts_info) {
		case FTS_D:
			if (!dflag)
				(void)printf("\n");
			if (!nflag)
				(void)printf("# %s\n", p->fts_path);
			statd(t, p, &uid, &gid, &mode);
			statf(indent, p);
			break;
		case FTS_DP:
			if (!nflag && (p->fts_level > 0))
				(void)printf("%*s# %s\n", indent, "", p->fts_path);
			(void)printf("%*s..\n", indent, "");
			if (!dflag)
				(void)printf("\n");
			break;
		case FTS_DNR:
		case FTS_ERR:
		case FTS_NS:
			(void)fprintf(stderr, "mtree: %s: %s\n",
			    p->fts_path, strerror(p->fts_errno));
			break;
		default:
			if (!dflag)
				statf(indent, p);
			break;
		}
	}
	(void)fts_close(t);
	if (sflag && keys & F_CKSUM)
		(void)fprintf(stderr,
		    "mtree: %s checksum: %u\n", fullpath, crc_total);
}

static void
statf(int indent, FTSENT *p)
{
	u_int32_t len, val;
	int fd, offset;
	const char *name;
	char *escaped_name;
	size_t esc_len;

	esc_len = p->fts_namelen * 4 + 1;
	escaped_name = malloc(esc_len);
	if (escaped_name == NULL)
		error("statf: %s", strerror(errno));
	strnvis(escaped_name, p->fts_name, esc_len,
	    VIS_WHITE | VIS_OCTAL | VIS_GLOB);

	if (iflag || S_ISDIR(p->fts_statp->st_mode))
		offset = printf("%*s%s", indent, "", escaped_name);
	else
		offset = printf("%*s    %s", indent, "", escaped_name);

	free(escaped_name);

	if (offset > (INDENTNAMELEN + indent))
		offset = MAXLINELEN;
	else
		offset += printf("%*s", (INDENTNAMELEN + indent) - offset, "");

	if (!S_ISREG(p->fts_statp->st_mode) && !dflag)
		output(indent, &offset, "type=%s", inotype(p->fts_statp->st_mode));
	if (p->fts_statp->st_uid != uid) {
		if (keys & F_UNAME) {
			name = user_from_uid(p->fts_statp->st_uid, 1);
			if (name != NULL) {
				output(indent, &offset, "uname=%s", name);
			} else {
				error("could not get uname for uid=%u",
				    p->fts_statp->st_uid);
			}
		}
		if (keys & F_UID)
			output(indent, &offset, "uid=%u", p->fts_statp->st_uid);
	}
	if (p->fts_statp->st_gid != gid) {
		if (keys & F_GNAME) {
			name = group_from_gid(p->fts_statp->st_gid, 1);
			if (name != NULL) {
				output(indent, &offset, "gname=%s", name);
			} else {
				error("could not get gname for gid=%u",
				    p->fts_statp->st_gid);
			}
		}
		if (keys & F_GID)
			output(indent, &offset, "gid=%u", p->fts_statp->st_gid);
	}
	if (keys & F_MODE && (p->fts_statp->st_mode & MBITS) != mode)
		output(indent, &offset, "mode=%#o", p->fts_statp->st_mode & MBITS);
	if (keys & F_NLINK && p->fts_statp->st_nlink != 1)
		output(indent, &offset, "nlink=%u", p->fts_statp->st_nlink);
	if (keys & F_SIZE && S_ISREG(p->fts_statp->st_mode))
		output(indent, &offset, "size=%lld",
		    (long long)p->fts_statp->st_size);
	if (keys & F_TIME)
		output(indent, &offset, "time=%lld.%ld",
		    (long long)p->fts_statp->st_mtim.tv_sec,
		    p->fts_statp->st_mtim.tv_nsec);
	if (keys & F_CKSUM && S_ISREG(p->fts_statp->st_mode)) {
		if ((fd = open(p->fts_accpath, MTREE_O_FLAGS)) == -1 ||
		    crc(fd, &val, &len))
			error("%s: %s", p->fts_accpath, strerror(errno));
		(void)close(fd);
		output(indent, &offset, "cksum=%u", val);
	}
	if (keys & F_MD5 && S_ISREG(p->fts_statp->st_mode)) {
		char *md5digest, buf[MD5_DIGEST_STRING_LENGTH];

		md5digest = MD5File(p->fts_accpath,buf);
		if (!md5digest)
			error("%s: %s", p->fts_accpath, strerror(errno));
		else
			output(indent, &offset, "md5digest=%s", md5digest);
	}
	if (keys & F_RMD160 && S_ISREG(p->fts_statp->st_mode)) {
		char *rmd160digest, buf[RMD160_DIGEST_STRING_LENGTH];

		rmd160digest = RMD160File(p->fts_accpath,buf);
		if (!rmd160digest)
			error("%s: %s", p->fts_accpath, strerror(errno));
		else
			output(indent, &offset, "rmd160digest=%s", rmd160digest);
	}
	if (keys & F_SHA1 && S_ISREG(p->fts_statp->st_mode)) {
		char *sha1digest, buf[SHA1_DIGEST_STRING_LENGTH];

		sha1digest = SHA1File(p->fts_accpath,buf);
		if (!sha1digest)
			error("%s: %s", p->fts_accpath, strerror(errno));
		else
			output(indent, &offset, "sha1digest=%s", sha1digest);
	}
	if (keys & F_SHA256 && S_ISREG(p->fts_statp->st_mode)) {
		char *sha256digest, buf[SHA256_DIGEST_STRING_LENGTH];

		sha256digest = SHA256File(p->fts_accpath,buf);
		if (!sha256digest)
			error("%s: %s", p->fts_accpath, strerror(errno));
		else
			output(indent, &offset, "sha256digest=%s", sha256digest);
	}
	if (keys & F_SLINK &&
	    (p->fts_info == FTS_SL || p->fts_info == FTS_SLNONE)) {
		name = rlink(p->fts_accpath);
		esc_len = strlen(name) * 4 + 1;
		escaped_name = malloc(esc_len);
		if (escaped_name == NULL)
			error("statf: %s", strerror(errno));
		strnvis(escaped_name, name, esc_len, VIS_WHITE | VIS_OCTAL);
		output(indent, &offset, "link=%s", escaped_name);
		free(escaped_name);
	}
	if (keys & F_FLAGS && !S_ISLNK(p->fts_statp->st_mode)) {
		char *file_flags;

		file_flags = fflagstostr(p->fts_statp->st_flags);
		if (file_flags == NULL)
			error("%s", strerror(errno));
		if (*file_flags != '\0')
			output(indent, &offset, "flags=%s", file_flags);
		else
			output(indent, &offset, "flags=none");
		free(file_flags);
	}
	(void)putchar('\n');
}

#define	MAXGID	5000
#define	MAXUID	5000
#define	MAXMODE	MBITS + 1

static int
statd(FTS *t, FTSENT *parent, uid_t *puid, gid_t *pgid, mode_t *pmode)
{
	FTSENT *p;
	gid_t sgid;
	uid_t suid;
	mode_t smode;
	gid_t savegid = *pgid;
	uid_t saveuid = *puid;
	mode_t savemode = *pmode;
	int maxgid;
	int maxuid;
	u_short maxmode;
	gid_t g[MAXGID];
	uid_t u[MAXUID];
	mode_t m[MAXMODE];
	const char *name;
	static int first = 1;

	if ((p = fts_children(t, 0)) == NULL) {
		if (errno)
			error("%s: %s", RP(parent), strerror(errno));
		return (1);
	}

	bzero(g, sizeof(g));
	bzero(u, sizeof(u));
	bzero(m, sizeof(m));

	maxuid = maxgid = maxmode = 0;
	for (; p; p = p->fts_link) {
		if (!dflag || (dflag && S_ISDIR(p->fts_statp->st_mode))) {
			smode = p->fts_statp->st_mode & MBITS;
			if (smode < MAXMODE && ++m[smode] > maxmode) {
				savemode = smode;
				maxmode = m[smode];
			}
			sgid = p->fts_statp->st_gid;
			if (sgid < MAXGID && ++g[sgid] > maxgid) {
				savegid = sgid;
				maxgid = g[sgid];
			}
			suid = p->fts_statp->st_uid;
			if (suid < MAXUID && ++u[suid] > maxuid) {
				saveuid = suid;
				maxuid = u[suid];
			}
		}
	}
	/*
	 * If the /set record is the same as the last one we do not need to output
	 * a new one.  So first we check to see if anything changed.  Note that we
	 * always output a /set record for the first directory.
	 */
	if ((((keys & F_UNAME) | (keys & F_UID)) && (*puid != saveuid)) ||
	    (((keys & F_GNAME) | (keys & F_GID)) && (*pgid != savegid)) ||
	    ((keys & F_MODE) && (*pmode != savemode)) || (first)) {
		first = 0;
		if (dflag)
			(void)printf("/set type=dir");
		else
			(void)printf("/set type=file");
		if (keys & F_UNAME) {
			if ((name = user_from_uid(saveuid, 1)) != NULL)
				(void)printf(" uname=%s", name);
			else
				error("could not get uname for uid=%u", saveuid);
		}
		if (keys & F_UID)
			(void)printf(" uid=%u", saveuid);
		if (keys & F_GNAME) {
			if ((name = group_from_gid(savegid, 1)) != NULL)
				(void)printf(" gname=%s", name);
			else
				error("could not get gname for gid=%u", savegid);
		}
		if (keys & F_GID)
			(void)printf(" gid=%u", savegid);
		if (keys & F_MODE)
			(void)printf(" mode=%#o", savemode);
		if (keys & F_NLINK)
			(void)printf(" nlink=1");
		(void)printf("\n");
		*puid = saveuid;
		*pgid = savegid;
		*pmode = savemode;
	}
	return (0);
}

int
dsort(const FTSENT **a, const FTSENT **b)
{
	if (S_ISDIR((*a)->fts_statp->st_mode)) {
		if (!S_ISDIR((*b)->fts_statp->st_mode))
			return (1);
	} else if (S_ISDIR((*b)->fts_statp->st_mode))
		return (-1);
	return (strcmp((*a)->fts_name, (*b)->fts_name));
}

void
output(int indent, int *offset, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (*offset + strlen(buf) > MAXLINELEN - 3) {
		(void)printf(" \\\n%*s", INDENTNAMELEN + indent, "");
		*offset = INDENTNAMELEN + indent;
	}
	*offset += printf(" %s", buf) + 1;
}
