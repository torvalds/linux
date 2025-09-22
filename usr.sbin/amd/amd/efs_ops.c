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
 *	from: @(#)efs_ops.c	8.1 (Berkeley) 6/6/93
 *	$Id: efs_ops.c,v 1.4 2014/10/26 02:43:50 guenther Exp $
 */

#include "am.h"

#ifdef HAS_EFS

/*
 * Error file system.
 * This is used as a last resort catchall if
 * nothing else worked.  EFS just returns lots
 * of error codes, except for unmount which
 * always works of course.
 */

/*
 * EFS file system always matches
 */
static char *
efs_match(am_opts *fo)
{
	return strdup("(error-hook)");
}

static int
efs_fmount(mntfs *mf)
{
	return ENOENT;
}

static int
efs_fumount(mntfs *mf)
{
	/*
	 * Always succeed
	 */

	return 0;
}

/*
 * EFS interface to RPC lookup() routine.
 * Should never get here in the automounter.
 * If we do then just give an error.
 */
am_node *
efs_lookuppn(am_node *mp, char *fname, int *error_return, int op)
{
	*error_return = ESTALE;
	return 0;
}

/*
 * EFS interface to RPC readdir() routine.
 * Should never get here in the automounter.
 * If we do then just give an error.
 */
int
efs_readdir(am_node *mp, nfscookie cookie, dirlist *dp, entry *ep,
    int count)
{
	return ESTALE;
}

/*
 * Ops structure
 */
am_ops efs_ops = {
	"error",
	efs_match,
	0, /* efs_init */
	auto_fmount,
	efs_fmount,
	auto_fumount,
	efs_fumount,
	efs_lookuppn,
	efs_readdir,
	0, /* efs_readlink */
	0, /* efs_mounted */
	0, /* efs_umounted */
	find_afs_srvr,
	FS_DISCARD
};

#endif /* HAS_EFS */
