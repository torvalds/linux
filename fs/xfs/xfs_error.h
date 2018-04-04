/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#ifndef	__XFS_ERROR_H__
#define	__XFS_ERROR_H__

struct xfs_mount;

extern void xfs_error_report(const char *tag, int level, struct xfs_mount *mp,
			const char *filename, int linenum,
			xfs_failaddr_t failaddr);
extern void xfs_corruption_error(const char *tag, int level,
			struct xfs_mount *mp, void *p, const char *filename,
			int linenum, xfs_failaddr_t failaddr);
extern void xfs_verifier_error(struct xfs_buf *bp, int error,
			xfs_failaddr_t failaddr);
extern void xfs_inode_verifier_error(struct xfs_inode *ip, int error,
			const char *name, void *buf, size_t bufsz,
			xfs_failaddr_t failaddr);

#define	XFS_ERROR_REPORT(e, lvl, mp)	\
	xfs_error_report(e, lvl, mp, __FILE__, __LINE__, __return_address)
#define	XFS_CORRUPTION_ERROR(e, lvl, mp, mem)	\
	xfs_corruption_error(e, lvl, mp, mem, \
			     __FILE__, __LINE__, __return_address)

#define XFS_ERRLEVEL_OFF	0
#define XFS_ERRLEVEL_LOW	1
#define XFS_ERRLEVEL_HIGH	5

/* Dump 128 bytes of any corrupt buffer */
#define XFS_CORRUPTION_DUMP_LEN		(128)

/*
 * Macros to set EFSCORRUPTED & return/branch.
 */
#define	XFS_WANT_CORRUPTED_GOTO(mp, x, l)	\
	{ \
		int fs_is_ok = (x); \
		ASSERT(fs_is_ok); \
		if (unlikely(!fs_is_ok)) { \
			XFS_ERROR_REPORT("XFS_WANT_CORRUPTED_GOTO", \
					 XFS_ERRLEVEL_LOW, mp); \
			error = -EFSCORRUPTED; \
			goto l; \
		} \
	}

#define	XFS_WANT_CORRUPTED_RETURN(mp, x)	\
	{ \
		int fs_is_ok = (x); \
		ASSERT(fs_is_ok); \
		if (unlikely(!fs_is_ok)) { \
			XFS_ERROR_REPORT("XFS_WANT_CORRUPTED_RETURN", \
					 XFS_ERRLEVEL_LOW, mp); \
			return -EFSCORRUPTED; \
		} \
	}

#ifdef DEBUG
extern int xfs_errortag_init(struct xfs_mount *mp);
extern void xfs_errortag_del(struct xfs_mount *mp);
extern bool xfs_errortag_test(struct xfs_mount *mp, const char *expression,
		const char *file, int line, unsigned int error_tag);
#define XFS_TEST_ERROR(expr, mp, tag)		\
	((expr) || xfs_errortag_test((mp), #expr, __FILE__, __LINE__, (tag)))

extern int xfs_errortag_get(struct xfs_mount *mp, unsigned int error_tag);
extern int xfs_errortag_set(struct xfs_mount *mp, unsigned int error_tag,
		unsigned int tag_value);
extern int xfs_errortag_add(struct xfs_mount *mp, unsigned int error_tag);
extern int xfs_errortag_clearall(struct xfs_mount *mp);
#else
#define xfs_errortag_init(mp)			(0)
#define xfs_errortag_del(mp)
#define XFS_TEST_ERROR(expr, mp, tag)		(expr)
#define xfs_errortag_set(mp, tag, val)		(ENOSYS)
#define xfs_errortag_add(mp, tag)		(ENOSYS)
#define xfs_errortag_clearall(mp)		(ENOSYS)
#endif /* DEBUG */

/*
 * XFS panic tags -- allow a call to xfs_alert_tag() be turned into
 *			a panic by setting xfs_panic_mask in a sysctl.
 */
#define		XFS_NO_PTAG			0
#define		XFS_PTAG_IFLUSH			0x00000001
#define		XFS_PTAG_LOGRES			0x00000002
#define		XFS_PTAG_AILDELETE		0x00000004
#define		XFS_PTAG_ERROR_REPORT		0x00000008
#define		XFS_PTAG_SHUTDOWN_CORRUPT	0x00000010
#define		XFS_PTAG_SHUTDOWN_IOERROR	0x00000020
#define		XFS_PTAG_SHUTDOWN_LOGERROR	0x00000040
#define		XFS_PTAG_FSBLOCK_ZERO		0x00000080

#endif	/* __XFS_ERROR_H__ */
