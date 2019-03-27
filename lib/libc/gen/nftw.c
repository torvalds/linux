/*	$OpenBSD: nftw.c,v 1.7 2006/03/31 19:41:44 millert Exp $	*/

/*
 * Copyright (c) 2003, 2004 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fts.h>
#include <ftw.h>

int
nftw(const char *path, int (*fn)(const char *, const struct stat *, int,
     struct FTW *), int nfds, int ftwflags)
{
	char * const paths[2] = { (char *)path, NULL };
	struct FTW ftw;
	FTSENT *cur;
	FTS *ftsp;
	int error = 0, ftsflags, fnflag, postorder, sverrno;

	/* XXX - nfds is currently unused */
	if (nfds < 1) {
		errno = EINVAL;
		return (-1);
	}

	ftsflags = FTS_COMFOLLOW;
	if (!(ftwflags & FTW_CHDIR))
		ftsflags |= FTS_NOCHDIR;
	if (ftwflags & FTW_MOUNT)
		ftsflags |= FTS_XDEV;
	if (ftwflags & FTW_PHYS)
		ftsflags |= FTS_PHYSICAL;
	else
		ftsflags |= FTS_LOGICAL;
	postorder = (ftwflags & FTW_DEPTH) != 0;
	ftsp = fts_open(paths, ftsflags, NULL);
	if (ftsp == NULL)
		return (-1);
	while ((cur = fts_read(ftsp)) != NULL) {
		switch (cur->fts_info) {
		case FTS_D:
			if (postorder)
				continue;
			fnflag = FTW_D;
			break;
		case FTS_DC:
			continue;
		case FTS_DNR:
			fnflag = FTW_DNR;
			break;
		case FTS_DP:
			if (!postorder)
				continue;
			fnflag = FTW_DP;
			break;
		case FTS_F:
		case FTS_DEFAULT:
			fnflag = FTW_F;
			break;
		case FTS_NS:
		case FTS_NSOK:
			fnflag = FTW_NS;
			break;
		case FTS_SL:
			fnflag = FTW_SL;
			break;
		case FTS_SLNONE:
			fnflag = FTW_SLN;
			break;
		default:
			error = -1;
			goto done;
		}
		ftw.base = cur->fts_pathlen - cur->fts_namelen;
		ftw.level = cur->fts_level;
		error = fn(cur->fts_path, cur->fts_statp, fnflag, &ftw);
		if (error != 0)
			break;
	}
done:
	sverrno = errno;
	if (fts_close(ftsp) != 0 && error == 0)
		error = -1;
	else
		errno = sverrno;
	return (error);
}
