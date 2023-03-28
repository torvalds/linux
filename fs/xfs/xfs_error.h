// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_ERROR_H__
#define	__XFS_ERROR_H__

struct xfs_mount;

extern void xfs_error_report(const char *tag, int level, struct xfs_mount *mp,
			const char *filename, int linenum,
			xfs_failaddr_t failaddr);
extern void xfs_corruption_error(const char *tag, int level,
			struct xfs_mount *mp, const void *buf, size_t bufsize,
			const char *filename, int linenum,
			xfs_failaddr_t failaddr);
void xfs_buf_corruption_error(struct xfs_buf *bp, xfs_failaddr_t fa);
extern void xfs_buf_verifier_error(struct xfs_buf *bp, int error,
			const char *name, const void *buf, size_t bufsz,
			xfs_failaddr_t failaddr);
extern void xfs_verifier_error(struct xfs_buf *bp, int error,
			xfs_failaddr_t failaddr);
extern void xfs_inode_verifier_error(struct xfs_inode *ip, int error,
			const char *name, const void *buf, size_t bufsz,
			xfs_failaddr_t failaddr);

#define	XFS_ERROR_REPORT(e, lvl, mp)	\
	xfs_error_report(e, lvl, mp, __FILE__, __LINE__, __return_address)
#define	XFS_CORRUPTION_ERROR(e, lvl, mp, buf, bufsize)	\
	xfs_corruption_error(e, lvl, mp, buf, bufsize, \
			     __FILE__, __LINE__, __return_address)

#define XFS_ERRLEVEL_OFF	0
#define XFS_ERRLEVEL_LOW	1
#define XFS_ERRLEVEL_HIGH	5

/* Dump 128 bytes of any corrupt buffer */
#define XFS_CORRUPTION_DUMP_LEN		(128)

#ifdef DEBUG
extern int xfs_errortag_init(struct xfs_mount *mp);
extern void xfs_errortag_del(struct xfs_mount *mp);
extern bool xfs_errortag_test(struct xfs_mount *mp, const char *expression,
		const char *file, int line, unsigned int error_tag);
#define XFS_TEST_ERROR(expr, mp, tag)		\
	((expr) || xfs_errortag_test((mp), #expr, __FILE__, __LINE__, (tag)))
bool xfs_errortag_enabled(struct xfs_mount *mp, unsigned int tag);
#define XFS_ERRORTAG_DELAY(mp, tag)		\
	do { \
		might_sleep(); \
		if (!xfs_errortag_enabled((mp), (tag))) \
			break; \
		xfs_warn_ratelimited((mp), \
"Injecting %ums delay at file %s, line %d, on filesystem \"%s\"", \
				(mp)->m_errortag[(tag)], __FILE__, __LINE__, \
				(mp)->m_super->s_id); \
		mdelay((mp)->m_errortag[(tag)]); \
	} while (0)

extern int xfs_errortag_get(struct xfs_mount *mp, unsigned int error_tag);
extern int xfs_errortag_set(struct xfs_mount *mp, unsigned int error_tag,
		unsigned int tag_value);
extern int xfs_errortag_add(struct xfs_mount *mp, unsigned int error_tag);
extern int xfs_errortag_clearall(struct xfs_mount *mp);
#else
#define xfs_errortag_init(mp)			(0)
#define xfs_errortag_del(mp)
#define XFS_TEST_ERROR(expr, mp, tag)		(expr)
#define XFS_ERRORTAG_DELAY(mp, tag)		((void)0)
#define xfs_errortag_set(mp, tag, val)		(ENOSYS)
#define xfs_errortag_add(mp, tag)		(ENOSYS)
#define xfs_errortag_clearall(mp)		(ENOSYS)
#endif /* DEBUG */

/*
 * XFS panic tags -- allow a call to xfs_alert_tag() be turned into
 *			a panic by setting fs.xfs.panic_mask in a sysctl.
 */
#define		XFS_NO_PTAG			0u
#define		XFS_PTAG_IFLUSH			(1u << 0)
#define		XFS_PTAG_LOGRES			(1u << 1)
#define		XFS_PTAG_AILDELETE		(1u << 2)
#define		XFS_PTAG_ERROR_REPORT		(1u << 3)
#define		XFS_PTAG_SHUTDOWN_CORRUPT	(1u << 4)
#define		XFS_PTAG_SHUTDOWN_IOERROR	(1u << 5)
#define		XFS_PTAG_SHUTDOWN_LOGERROR	(1u << 6)
#define		XFS_PTAG_FSBLOCK_ZERO		(1u << 7)
#define		XFS_PTAG_VERIFIER_ERROR		(1u << 8)

#define		XFS_PTAG_MASK	(XFS_PTAG_IFLUSH | \
				 XFS_PTAG_LOGRES | \
				 XFS_PTAG_AILDELETE | \
				 XFS_PTAG_ERROR_REPORT | \
				 XFS_PTAG_SHUTDOWN_CORRUPT | \
				 XFS_PTAG_SHUTDOWN_IOERROR | \
				 XFS_PTAG_SHUTDOWN_LOGERROR | \
				 XFS_PTAG_FSBLOCK_ZERO | \
				 XFS_PTAG_VERIFIER_ERROR)

#define XFS_PTAG_STRINGS \
	{ XFS_NO_PTAG,			"none" }, \
	{ XFS_PTAG_IFLUSH,		"iflush" }, \
	{ XFS_PTAG_LOGRES,		"logres" }, \
	{ XFS_PTAG_AILDELETE,		"aildelete" }, \
	{ XFS_PTAG_ERROR_REPORT	,	"error_report" }, \
	{ XFS_PTAG_SHUTDOWN_CORRUPT,	"corrupt" }, \
	{ XFS_PTAG_SHUTDOWN_IOERROR,	"ioerror" }, \
	{ XFS_PTAG_SHUTDOWN_LOGERROR,	"logerror" }, \
	{ XFS_PTAG_FSBLOCK_ZERO,	"fsb_zero" }, \
	{ XFS_PTAG_VERIFIER_ERROR,	"verifier" }

#endif	/* __XFS_ERROR_H__ */
