/*
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
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
 *	from: @(#)am_ops.c	8.1 (Berkeley) 6/6/93
 *	$Id: am_ops.c,v 1.8 2015/12/05 21:15:01 mmcc Exp $
 */

#include "am.h"

static am_ops *vops[] = {
#ifdef HAS_UFS
	&ufs_ops,
#endif
#ifdef HAS_NFS
	&nfs_ops,
#endif
#ifdef HAS_NFSX
	&nfsx_ops,
#endif
#ifdef HAS_HOST
	&host_ops,
#endif
#ifdef HAS_SFS
	&sfs_ops,
#endif
#ifdef HAS_SFSX
	&sfsx_ops,
#endif
#ifdef HAS_LOFS
	&lofs_ops,
#endif
#ifdef HAS_PFS
	&pfs_ops,
#endif
#ifdef HAS_UNION_FS
	&union_ops,
#endif
	&afs_ops,	/* These four should be last ... */
	&dfs_ops,	/* ... */
	&toplvl_ops,	/* ... */
	&efs_ops,	/* ... in the order afs; dfs; toplvl; efs */
	0
};

void
ops_showfstypes(FILE *fp)
{
	struct am_ops **ap;
	int l = 0;

	for (ap = vops; *ap; ap++) {
		fputs((*ap)->fs_type, fp);
		if (ap[1]) fputs(", ", fp);
		l += strlen((*ap)->fs_type) + 2;
		if (l > 60) { l = 0; fputs("\n    ", fp); }
	}
}

#ifdef SUNOS4_COMPAT
#ifdef nomore
/*
 * Crack a SunOS4-style host:fs:sub-link line
 * Construct an amd-style line and call the
 * normal amd matcher.
 */
am_ops *
sunos4_match(am_opts *fo, char *key, char *g_key, char *path,
    char *keym, char *map)
{
	char *host = key;
	char *fs = strchr(host, ':');
	char *sublink = fs ? strchr(fs+1, ':') : 0;
	char keybuf[MAXPATHLEN];

	snprintf(keybuf, sizeof(keybuf),
		"type:=nfs;rhost:=%s;rfs:=%s;sublink:=%s;opts:=%s", host,
		fs ? fs+1 : "",
		sublink ? sublink+1  : "",
		g_key);
	return ops_match(fo, keybuf, "", path, keym, map);
}
#endif
#endif /* SUNOS4_COMPAT */

am_ops *
ops_match(am_opts *fo, char *key, char *g_key, char *path, char *keym,
    char *map)
{
	am_ops **vp;
	am_ops *rop = 0;

	/*
	 * First crack the global opts and the local opts
	 */
	if (!eval_fs_opts(fo, key, g_key, path, keym, map)) {
		rop = &efs_ops;
	} else if (fo->opt_type == 0) {
		plog(XLOG_USER, "No fs type specified (key = \"%s\", map = \"%s\")", keym, map);
		rop = &efs_ops;
	} else {
		/*
		 * Next find the correct filesystem type
		 */
		for (vp = vops; (rop = *vp); vp++)
			if (strcmp(rop->fs_type, fo->opt_type) == 0)
				break;

		if (!rop) {
			plog(XLOG_USER, "fs type \"%s\" not recognised", fo->opt_type);
			rop = &efs_ops;
		}
	}

	/*
	 * Make sure we have a default mount option.
	 * Otherwise skip past any leading '-'.
	 */
	if (fo->opt_opts == 0)
		fo->opt_opts = "rw,defaults";
	else if (*fo->opt_opts == '-')
		fo->opt_opts++;

	/*
	 * Check the filesystem is happy
	 */
	free(fo->fs_mtab);

	if ((fo->fs_mtab = (*rop->fs_match)(fo)))
		return rop;

	/*
	 * Return error file system
	 */
	fo->fs_mtab = (*efs_ops.fs_match)(fo);
	return &efs_ops;
}
