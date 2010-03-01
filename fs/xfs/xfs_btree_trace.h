/*
 * Copyright (c) 2008 Silicon Graphics, Inc.
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
#ifndef __XFS_BTREE_TRACE_H__
#define	__XFS_BTREE_TRACE_H__

struct xfs_btree_cur;
struct xfs_buf;


/*
 * Trace hooks.
 * i,j = integer (32 bit)
 * b = btree block buffer (xfs_buf_t)
 * p = btree ptr
 * r = btree record
 * k = btree key
 */

#ifdef XFS_BTREE_TRACE

/*
 * Trace buffer entry types.
 */
#define XFS_BTREE_KTRACE_ARGBI   1
#define XFS_BTREE_KTRACE_ARGBII  2
#define XFS_BTREE_KTRACE_ARGFFFI 3
#define XFS_BTREE_KTRACE_ARGI    4
#define XFS_BTREE_KTRACE_ARGIPK  5
#define XFS_BTREE_KTRACE_ARGIPR  6
#define XFS_BTREE_KTRACE_ARGIK   7
#define XFS_BTREE_KTRACE_ARGR	 8
#define XFS_BTREE_KTRACE_CUR     9

/*
 * Sub-types for cursor traces.
 */
#define XBT_ARGS	0
#define XBT_ENTRY	1
#define XBT_ERROR	2
#define XBT_EXIT	3

void xfs_btree_trace_argbi(const char *, struct xfs_btree_cur *,
		struct xfs_buf *, int, int);
void xfs_btree_trace_argbii(const char *, struct xfs_btree_cur *,
		struct xfs_buf *, int, int, int);
void xfs_btree_trace_argi(const char *, struct xfs_btree_cur *, int, int);
void xfs_btree_trace_argipk(const char *, struct xfs_btree_cur *, int,
		union xfs_btree_ptr, union xfs_btree_key *, int);
void xfs_btree_trace_argipr(const char *, struct xfs_btree_cur *, int,
		union xfs_btree_ptr, union xfs_btree_rec *, int);
void xfs_btree_trace_argik(const char *, struct xfs_btree_cur *, int,
		union xfs_btree_key *, int);
void xfs_btree_trace_argr(const char *, struct xfs_btree_cur *,
		union xfs_btree_rec *, int);
void xfs_btree_trace_cursor(const char *, struct xfs_btree_cur *, int, int);

#define	XFS_BTREE_TRACE_ARGBI(c, b, i)	\
	xfs_btree_trace_argbi(__func__, c, b, i, __LINE__)
#define	XFS_BTREE_TRACE_ARGBII(c, b, i, j)	\
	xfs_btree_trace_argbii(__func__, c, b, i, j, __LINE__)
#define	XFS_BTREE_TRACE_ARGI(c, i)	\
	xfs_btree_trace_argi(__func__, c, i, __LINE__)
#define	XFS_BTREE_TRACE_ARGIPK(c, i, p, k)	\
	xfs_btree_trace_argipk(__func__, c, i, p, k, __LINE__)
#define	XFS_BTREE_TRACE_ARGIPR(c, i, p, r)	\
	xfs_btree_trace_argipr(__func__, c, i, p, r, __LINE__)
#define	XFS_BTREE_TRACE_ARGIK(c, i, k)	\
	xfs_btree_trace_argik(__func__, c, i, k, __LINE__)
#define XFS_BTREE_TRACE_ARGR(c, r)	\
	xfs_btree_trace_argr(__func__, c, r, __LINE__)
#define	XFS_BTREE_TRACE_CURSOR(c, t)	\
	xfs_btree_trace_cursor(__func__, c, t, __LINE__)
#else
#define	XFS_BTREE_TRACE_ARGBI(c, b, i)
#define	XFS_BTREE_TRACE_ARGBII(c, b, i, j)
#define	XFS_BTREE_TRACE_ARGI(c, i)
#define	XFS_BTREE_TRACE_ARGIPK(c, i, p, s)
#define	XFS_BTREE_TRACE_ARGIPR(c, i, p, r)
#define	XFS_BTREE_TRACE_ARGIK(c, i, k)
#define XFS_BTREE_TRACE_ARGR(c, r)
#define	XFS_BTREE_TRACE_CURSOR(c, t)
#endif	/* XFS_BTREE_TRACE */

#endif /* __XFS_BTREE_TRACE_H__ */
