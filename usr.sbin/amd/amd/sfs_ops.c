/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
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
 *
 *	from: @(#)sfs_ops.c	8.1 (Berkeley) 6/6/93
 *	$Id: sfs_ops.c,v 1.6 2016/03/16 15:41:11 krw Exp $
 */

#include "am.h"

#if defined(HAS_SFS) || defined(HAS_SFSX)
#define NEED_SFS_MATCH
#define NEED_SFS_UMOUNT
#endif

/*
 * Symbol-link file system
 */

#ifdef HAS_SFSX
#include <sys/stat.h>
#endif

#ifdef NEED_SFS_MATCH
/*
 * SFS needs a link.
 */
static char *
sfs_match(am_opts *fo)
{
	if (!fo->opt_fs) {
		plog(XLOG_USER, "link: no fs specified");
		return 0;
	}

	/*
	 * Bug report (14/12/89) from Jay Plett <jay@princeton.edu>
	 * If an automount point has the same name as an existing
	 * link type mount Amd hits a race condition and either hangs
	 * or causes a symlink loop.
	 *
	 * If fs begins with a '/' change the opt_fs & opt_sublink
	 * fields so that the fs option doesn't end up pointing at
	 * an existing symlink.
	 *
	 * If sublink is nil then set sublink to fs
	 * else set sublink to fs / sublink
	 *
	 * Finally set fs to ".".
	 */
	if (*fo->opt_fs == '/') {
		char *fullpath;
		char *link = fo->opt_sublink;
		if (link) {
			if (*link == '/')
				fullpath = strdup(link);
			else
				fullpath = str3cat(NULL, fo->opt_fs, "/", link);
		} else {
			fullpath = strdup(fo->opt_fs);
		}

		free(fo->opt_sublink);
		fo->opt_sublink = fullpath;
		fo->opt_fs = str3cat(fo->opt_fs, ".", fullpath, "");
	}

	return strdup(fo->opt_fs);
}
#endif

#ifdef HAS_SFSX
static int
sfsx_mount(am_node *mp)
{
	/*
	 * Check for existence of target.
	 */
	struct stat stb;
	char *ln;

	if (mp->am_link)
		ln = mp->am_link;
	else /* should never occur */
		ln = mp->am_mnt->mf_mount;

	/*
	 * Use lstat, not stat, since we don't
	 * want to know if the ultimate target of
	 * a symlink chain exists, just the first.
	 */
	if (lstat(ln, &stb) < 0)
		return errno;

	return 0;
}
#endif

#ifdef HAS_SFS
static int
sfs_fmount(mntfs *mf)
{
	/*
	 * Wow - this is hard to implement!
	 */

	return 0;
}
#endif

#ifdef NEED_SFS_UMOUNT
static int
sfs_fumount(mntfs *mf)
{
	return 0;
}
#endif

/*
 * Ops structures
 */
#ifdef HAS_SFS
am_ops sfs_ops = {
	"link",
	sfs_match,
	0, /* sfs_init */
	auto_fmount,
	sfs_fmount,
	auto_fumount,
	sfs_fumount,
	efs_lookuppn,
	efs_readdir,
	0, /* sfs_readlink */
	0, /* sfs_mounted */
	0, /* sfs_umounted */
	find_afs_srvr,
#ifdef FLUSH_KERNEL_NAME_CACHE
	FS_UBACKGROUND
#else /* FLUSH_KERNEL_NAME_CACHE */
	0
#endif /* FLUSH_KERNEL_NAME_CACHE */
};

#endif /* HAS_SFS */

#ifdef HAS_SFSX
struct am_ops sfsx_ops = {
	"linkx",
	sfs_match,
	0, /* sfsx_init */
	sfsx_mount,
	0,
	auto_fumount,
	sfs_fumount,
	efs_lookuppn,
	efs_readdir,
	0, /* sfsx_readlink */
	0, /* sfsx_mounted */
	0, /* sfsx_umounted */
	find_afs_srvr,
#ifdef FLUSH_KERNEL_NAME_CACHE
	FS_BACKGROUND
#else /* FLUSH_KERNEL_NAME_CACHE */
	FS_MBACKGROUND
#endif /* FLUSH_KERNEL_NAME_CACHE */
};

#endif /* HAS_SFSX */
