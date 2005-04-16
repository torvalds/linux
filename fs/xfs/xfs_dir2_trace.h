/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef __XFS_DIR2_TRACE_H__
#define __XFS_DIR2_TRACE_H__

/*
 * Tracing for xfs v2 directories.
 */

#if defined(XFS_DIR2_TRACE)

struct ktrace;
struct xfs_dabuf;
struct xfs_da_args;

#define	XFS_DIR2_GTRACE_SIZE		4096	/* global buffer */
#define	XFS_DIR2_KTRACE_SIZE		32	/* per-inode buffer */
extern struct ktrace *xfs_dir2_trace_buf;

#define	XFS_DIR2_KTRACE_ARGS		1	/* args only */
#define	XFS_DIR2_KTRACE_ARGS_B		2	/* args + buffer */
#define	XFS_DIR2_KTRACE_ARGS_BB		3	/* args + 2 buffers */
#define	XFS_DIR2_KTRACE_ARGS_DB		4	/* args, db, buffer */
#define	XFS_DIR2_KTRACE_ARGS_I		5	/* args, inum */
#define	XFS_DIR2_KTRACE_ARGS_S		6	/* args, int */
#define	XFS_DIR2_KTRACE_ARGS_SB		7	/* args, int, buffer */
#define	XFS_DIR2_KTRACE_ARGS_BIBII	8	/* args, buf/int/buf/int/int */

void xfs_dir2_trace_args(char *where, struct xfs_da_args *args);
void xfs_dir2_trace_args_b(char *where, struct xfs_da_args *args,
			   struct xfs_dabuf *bp);
void xfs_dir2_trace_args_bb(char *where, struct xfs_da_args *args,
			    struct xfs_dabuf *lbp, struct xfs_dabuf *dbp);
void xfs_dir2_trace_args_bibii(char *where, struct xfs_da_args *args,
			       struct xfs_dabuf *bs, int ss,
			       struct xfs_dabuf *bd, int sd, int c);
void xfs_dir2_trace_args_db(char *where, struct xfs_da_args *args,
			    xfs_dir2_db_t db, struct xfs_dabuf *bp);
void xfs_dir2_trace_args_i(char *where, struct xfs_da_args *args, xfs_ino_t i);
void xfs_dir2_trace_args_s(char *where, struct xfs_da_args *args, int s);
void xfs_dir2_trace_args_sb(char *where, struct xfs_da_args *args, int s,
			    struct xfs_dabuf *bp);

#else	/* XFS_DIR2_TRACE */

#define	xfs_dir2_trace_args(where, args)
#define	xfs_dir2_trace_args_b(where, args, bp)
#define	xfs_dir2_trace_args_bb(where, args, lbp, dbp)
#define	xfs_dir2_trace_args_bibii(where, args, bs, ss, bd, sd, c)
#define	xfs_dir2_trace_args_db(where, args, db, bp)
#define	xfs_dir2_trace_args_i(where, args, i)
#define	xfs_dir2_trace_args_s(where, args, s)
#define	xfs_dir2_trace_args_sb(where, args, s, bp)

#endif	/* XFS_DIR2_TRACE */

#endif	/* __XFS_DIR2_TRACE_H__ */
