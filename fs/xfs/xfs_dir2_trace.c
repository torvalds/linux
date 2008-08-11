/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_dir2.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
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
		(void *)(unsigned long)(args->op_flags & XFS_DA_OP_JUSTCHECK),
		NULL, NULL);
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
		(void *)(unsigned long)(args->op_flags & XFS_DA_OP_JUSTCHECK),
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
		(void *)(unsigned long)(args->op_flags & XFS_DA_OP_JUSTCHECK),
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
		(void *)(unsigned long)(args->op_flags & XFS_DA_OP_JUSTCHECK),
		(void *)(long)db, (void *)dbp);
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
		(void *)(unsigned long)(args->op_flags & XFS_DA_OP_JUSTCHECK),
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
		(void *)(unsigned long)(args->op_flags & XFS_DA_OP_JUSTCHECK),
		(void *)(long)s, NULL);
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
		(void *)(unsigned long)(args->op_flags & XFS_DA_OP_JUSTCHECK),
		(void *)(long)s, (void *)dbp);
}
#endif	/* XFS_DIR2_TRACE */
