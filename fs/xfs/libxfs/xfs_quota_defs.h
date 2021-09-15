// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_QUOTA_DEFS_H__
#define __XFS_QUOTA_DEFS_H__

/*
 * Quota definitions shared between user and kernel source trees.
 */

/*
 * Even though users may not have quota limits occupying all 64-bits,
 * they may need 64-bit accounting. Hence, 64-bit quota-counters,
 * and quota-limits. This is a waste in the common case, but hey ...
 */
typedef uint64_t	xfs_qcnt_t;
typedef uint16_t	xfs_qwarncnt_t;

typedef uint8_t		xfs_dqtype_t;

#define XFS_DQTYPE_STRINGS \
	{ XFS_DQTYPE_USER,	"USER" }, \
	{ XFS_DQTYPE_PROJ,	"PROJ" }, \
	{ XFS_DQTYPE_GROUP,	"GROUP" }, \
	{ XFS_DQTYPE_BIGTIME,	"BIGTIME" }

/*
 * flags for q_flags field in the dquot.
 */
#define XFS_DQFLAG_DIRTY	(1 << 0)	/* dquot is dirty */
#define XFS_DQFLAG_FREEING	(1 << 1)	/* dquot is being torn down */

#define XFS_DQFLAG_STRINGS \
	{ XFS_DQFLAG_DIRTY,	"DIRTY" }, \
	{ XFS_DQFLAG_FREEING,	"FREEING" }

/*
 * We have the possibility of all three quota types being active at once, and
 * hence free space modification requires modification of all three current
 * dquots in a single transaction. For this case we need to have a reservation
 * of at least 3 dquots.
 *
 * However, a chmod operation can change both UID and GID in a single
 * transaction, resulting in requiring {old, new} x {uid, gid} dquots to be
 * modified. Hence for this case we need to reserve space for at least 4 dquots.
 *
 * And in the worst case, there's a rename operation that can be modifying up to
 * 4 inodes with dquots attached to them. In reality, the only inodes that can
 * have their dquots modified are the source and destination directory inodes
 * due to directory name creation and removal. That can require space allocation
 * and/or freeing on both directory inodes, and hence all three dquots on each
 * inode can be modified. And if the directories are world writeable, all the
 * dquots can be unique and so 6 dquots can be modified....
 *
 * And, of course, we also need to take into account the dquot log format item
 * used to describe each dquot.
 */
#define XFS_DQUOT_LOGRES(mp)	\
	((sizeof(struct xfs_dq_logformat) + sizeof(struct xfs_disk_dquot)) * 6)

#define XFS_IS_QUOTA_ON(mp)		((mp)->m_qflags & XFS_ALL_QUOTA_ACCT)
#define XFS_IS_UQUOTA_ON(mp)		((mp)->m_qflags & XFS_UQUOTA_ACCT)
#define XFS_IS_PQUOTA_ON(mp)		((mp)->m_qflags & XFS_PQUOTA_ACCT)
#define XFS_IS_GQUOTA_ON(mp)		((mp)->m_qflags & XFS_GQUOTA_ACCT)
#define XFS_IS_UQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_UQUOTA_ENFD)
#define XFS_IS_GQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_GQUOTA_ENFD)
#define XFS_IS_PQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_PQUOTA_ENFD)

/*
 * Flags to tell various functions what to do. Not all of these are meaningful
 * to a single function. None of these XFS_QMOPT_* flags are meant to have
 * persistent values (ie. their values can and will change between versions)
 */
#define XFS_QMOPT_UQUOTA	0x0000004 /* user dquot requested */
#define XFS_QMOPT_PQUOTA	0x0000008 /* project dquot requested */
#define XFS_QMOPT_FORCE_RES	0x0000010 /* ignore quota limits */
#define XFS_QMOPT_SBVERSION	0x0000040 /* change superblock version num */
#define XFS_QMOPT_GQUOTA	0x0002000 /* group dquot requested */

/*
 * flags to xfs_trans_mod_dquot to indicate which field needs to be
 * modified.
 */
#define XFS_QMOPT_RES_REGBLKS	0x0010000
#define XFS_QMOPT_RES_RTBLKS	0x0020000
#define XFS_QMOPT_BCOUNT	0x0040000
#define XFS_QMOPT_ICOUNT	0x0080000
#define XFS_QMOPT_RTBCOUNT	0x0100000
#define XFS_QMOPT_DELBCOUNT	0x0200000
#define XFS_QMOPT_DELRTBCOUNT	0x0400000
#define XFS_QMOPT_RES_INOS	0x0800000

/*
 * flags for dqalloc.
 */
#define XFS_QMOPT_INHERIT	0x1000000

/*
 * flags to xfs_trans_mod_dquot.
 */
#define XFS_TRANS_DQ_RES_BLKS	XFS_QMOPT_RES_REGBLKS
#define XFS_TRANS_DQ_RES_RTBLKS	XFS_QMOPT_RES_RTBLKS
#define XFS_TRANS_DQ_RES_INOS	XFS_QMOPT_RES_INOS
#define XFS_TRANS_DQ_BCOUNT	XFS_QMOPT_BCOUNT
#define XFS_TRANS_DQ_DELBCOUNT	XFS_QMOPT_DELBCOUNT
#define XFS_TRANS_DQ_ICOUNT	XFS_QMOPT_ICOUNT
#define XFS_TRANS_DQ_RTBCOUNT	XFS_QMOPT_RTBCOUNT
#define XFS_TRANS_DQ_DELRTBCOUNT XFS_QMOPT_DELRTBCOUNT


#define XFS_QMOPT_QUOTALL	\
		(XFS_QMOPT_UQUOTA | XFS_QMOPT_PQUOTA | XFS_QMOPT_GQUOTA)
#define XFS_QMOPT_RESBLK_MASK	(XFS_QMOPT_RES_REGBLKS | XFS_QMOPT_RES_RTBLKS)

extern xfs_failaddr_t xfs_dquot_verify(struct xfs_mount *mp,
		struct xfs_disk_dquot *ddq, xfs_dqid_t id);
extern xfs_failaddr_t xfs_dqblk_verify(struct xfs_mount *mp,
		struct xfs_dqblk *dqb, xfs_dqid_t id);
extern int xfs_calc_dquots_per_chunk(unsigned int nbblks);
extern void xfs_dqblk_repair(struct xfs_mount *mp, struct xfs_dqblk *dqb,
		xfs_dqid_t id, xfs_dqtype_t type);

struct xfs_dquot;
time64_t xfs_dquot_from_disk_ts(struct xfs_disk_dquot *ddq,
		__be32 dtimer);
__be32 xfs_dquot_to_disk_ts(struct xfs_dquot *ddq, time64_t timer);

#endif	/* __XFS_QUOTA_H__ */
