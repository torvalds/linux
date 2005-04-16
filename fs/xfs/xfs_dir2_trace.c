/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 * xfs_dir2_trace.c
 * Tracing for xfs v2 directories.
 */
#include "xfs.h"

#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_bmap_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_dir2_trace.h"

#ifdef XFS_DIR2_TRACE
ktrace_t	*xfs_dir2_trace_buf;

/*
 * Enter something in the trace buffers.
 */
static void
xfs_dir2_trace_enter(
	xfs_inode_t	*dp,
	int		type,
	char		*where,
	char		*name,
	int		namelen,
	void		*a0,
	void		*a1,
	void		*a2,
	void		*a3,
	void		*a4,
	void		*a5,
	void		*a6,
	void		*a7)
{
	void		*n[5];

	ASSERT(xfs_dir2_trace_buf);
	ASSERT(dp->i_dir_trace);
	if (name)
		memcpy(n, name, min((int)sizeof(n), namelen));
	else
		memset((char *)n, 0, sizeof(n));
	ktrace_enter(xfs_dir2_trace_buf,
		(void *)(long)type, (void *)where,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)(long)namelen,
		(void *)n[0], (void *)n[1], (void *)n[2],
		(void *)n[3], (void *)n[4]);
	ktrace_enter(dp->i_dir_trace,
		(void *)(long)type, (void *)where,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)(long)namelen,
		(void *)n[0], (void *)n[1], (void *)n[2],
		(void *)n[3], (void *)n[4]);
}

void
xfs_dir2_trace_args(
	char		*where,
	xfs_da_args_t	*args)
{
	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS, where,
		(char *)args->name, (int)args->namelen,
		(void *)(unsigned long)args->hashval,
		(void *)((unsigned long)(args->inumber >> 32)),
		(void *)((unsigned long)(args->inumber & 0xFFFFFFFF)),
		(void *)args->dp, (void *)args->trans,
		(void *)(unsigned long)args->justcheck, NULL, NULL);
}

void
xfs_dir2_trace_args_b(
	char		*where,
	xfs_da_args_t	*args,
	xfs_dabuf_t	*bp)
{
	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS_B, where,
		(char *)args->name, (int)args->namelen,
		(void *)(unsigned long)args->hashval,
		(void *)((unsigned long)(args->inumber >> 32)),
		(void *)((unsigned long)(args->inumber & 0xFFFFFFFF)),
		(void *)args->dp, (void *)args->trans,
		(void *)(unsigned long)args->justcheck,
		(void *)(bp ? bp->bps[0] : NULL), NULL);
}

void
xfs_dir2_trace_args_bb(
	char		*where,
	xfs_da_args_t	*args,
	xfs_dabuf_t	*lbp,
	xfs_dabuf_t	*dbp)
{
	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS_BB, where,
		(char *)args->name, (int)args->namelen,
		(void *)(unsigned long)args->hashval,
		(void *)((unsigned long)(args->inumber >> 32)),
		(void *)((unsigned long)(args->inumber & 0xFFFFFFFF)),
		(void *)args->dp, (void *)args->trans,
		(void *)(unsigned long)args->justcheck,
		(void *)(lbp ? lbp->bps[0] : NULL),
		(void *)(dbp ? dbp->bps[0] : NULL));
}

void
xfs_dir2_trace_args_bibii(
	char		*where,
	xfs_da_args_t	*args,
	xfs_dabuf_t	*bs,
	int		ss,
	xfs_dabuf_t	*bd,
	int		sd,
	int		c)
{
	xfs_buf_t	*bpbs = bs ? bs->bps[0] : NULL;
	xfs_buf_t	*bpbd = bd ? bd->bps[0] : NULL;

	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS_BIBII, where,
		(char *)args->name, (int)args->namelen,
		(void *)args->dp, (void *)args->trans,
		(void *)bpbs, (void *)(long)ss, (void *)bpbd, (void *)(long)sd,
		(void *)(long)c, NULL);
}

void
xfs_dir2_trace_args_db(
	char		*where,
	xfs_da_args_t	*args,
	xfs_dir2_db_t	db,
	xfs_dabuf_t	*bp)
{
	xfs_buf_t	*dbp = bp ? bp->bps[0] : NULL;

	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS_DB, where,
		(char *)args->name, (int)args->namelen,
		(void *)(unsigned long)args->hashval,
		(void *)((unsigned long)(args->inumber >> 32)),
		(void *)((unsigned long)(args->inumber & 0xFFFFFFFF)),
		(void *)args->dp, (void *)args->trans,
		(void *)(unsigned long)args->justcheck, (void *)(long)db,
		(void *)dbp);
}

void
xfs_dir2_trace_args_i(
	char		*where,
	xfs_da_args_t	*args,
	xfs_ino_t	i)
{
	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS_I, where,
		(char *)args->name, (int)args->namelen,
		(void *)(unsigned long)args->hashval,
		(void *)((unsigned long)(args->inumber >> 32)),
		(void *)((unsigned long)(args->inumber & 0xFFFFFFFF)),
		(void *)args->dp, (void *)args->trans,
		(void *)(unsigned long)args->justcheck,
		(void *)((unsigned long)(i >> 32)),
		(void *)((unsigned long)(i & 0xFFFFFFFF)));
}

void
xfs_dir2_trace_args_s(
	char		*where,
	xfs_da_args_t	*args,
	int		s)
{
	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS_S, where,
		(char *)args->name, (int)args->namelen,
		(void *)(unsigned long)args->hashval,
		(void *)((unsigned long)(args->inumber >> 32)),
		(void *)((unsigned long)(args->inumber & 0xFFFFFFFF)),
		(void *)args->dp, (void *)args->trans,
		(void *)(unsigned long)args->justcheck, (void *)(long)s, NULL);
}

void
xfs_dir2_trace_args_sb(
	char		*where,
	xfs_da_args_t	*args,
	int		s,
	xfs_dabuf_t	*bp)
{
	xfs_buf_t	*dbp = bp ? bp->bps[0] : NULL;

	xfs_dir2_trace_enter(args->dp, XFS_DIR2_KTRACE_ARGS_SB, where,
		(char *)args->name, (int)args->namelen,
		(void *)(unsigned long)args->hashval,
		(void *)((unsigned long)(args->inumber >> 32)),
		(void *)((unsigned long)(args->inumber & 0xFFFFFFFF)),
		(void *)args->dp, (void *)args->trans,
		(void *)(unsigned long)args->justcheck, (void *)(long)s,
		(void *)dbp);
}
#endif	/* XFS_DIR2_TRACE */
