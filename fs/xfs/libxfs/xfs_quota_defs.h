/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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

/*
 * flags for q_flags field in the dquot.
 */
#define XFS_DQ_USER		0x0001		/* a user quota */
#define XFS_DQ_PROJ		0x0002		/* project quota */
#define XFS_DQ_GROUP		0x0004		/* a group quota */
#define XFS_DQ_DIRTY		0x0008		/* dquot is dirty */
#define XFS_DQ_FREEING		0x0010		/* dquot is being torn down */

#define XFS_DQ_ALLTYPES		(XFS_DQ_USER|XFS_DQ_PROJ|XFS_DQ_GROUP)

#define XFS_DQ_FLAGS \
	{ XFS_DQ_USER,		"USER" }, \
	{ XFS_DQ_PROJ,		"PROJ" }, \
	{ XFS_DQ_GROUP,		"GROUP" }, \
	{ XFS_DQ_DIRTY,		"DIRTY" }, \
	{ XFS_DQ_FREEING,	"FREEING" }

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

#define XFS_IS_QUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_ALL_QUOTA_ACCT)
#define XFS_IS_UQUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_UQUOTA_ACCT)
#define XFS_IS_PQUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_PQUOTA_ACCT)
#define XFS_IS_GQUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_GQUOTA_ACCT)
#define XFS_IS_UQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_UQUOTA_ENFD)
#define XFS_IS_GQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_GQUOTA_ENFD)
#define XFS_IS_PQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_PQUOTA_ENFD)

/*
 * Incore only flags for quotaoff - these bits get cleared when quota(s)
 * are in the process of getting turned off. These flags are in m_qflags but
 * never in sb_qflags.
 */
#define XFS_UQUOTA_ACTIVE	0x1000  /* uquotas are being turned off */
#define XFS_GQUOTA_ACTIVE	0x2000  /* gquotas are being turned off */
#define XFS_PQUOTA_ACTIVE	0x4000  /* pquotas are being turned off */
#define XFS_ALL_QUOTA_ACTIVE	\
	(XFS_UQUOTA_ACTIVE | XFS_GQUOTA_ACTIVE | XFS_PQUOTA_ACTIVE)

/*
 * Checking XFS_IS_*QUOTA_ON() while holding any inode lock guarantees
 * quota will be not be switched off as long as that inode lock is held.
 */
#define XFS_IS_QUOTA_ON(mp)	((mp)->m_qflags & (XFS_UQUOTA_ACTIVE | \
						   XFS_GQUOTA_ACTIVE | \
						   XFS_PQUOTA_ACTIVE))
#define XFS_IS_UQUOTA_ON(mp)	((mp)->m_qflags & XFS_UQUOTA_ACTIVE)
#define XFS_IS_GQUOTA_ON(mp)	((mp)->m_qflags & XFS_GQUOTA_ACTIVE)
#define XFS_IS_PQUOTA_ON(mp)	((mp)->m_qflags & XFS_PQUOTA_ACTIVE)

/*
 * Flags to tell various functions what to do. Not all of these are meaningful
 * to a single function. None of these XFS_QMOPT_* flags are meant to have
 * persistent values (ie. their values can and will change between versions)
 */
#define XFS_QMOPT_DQALLOC	0x0000002 /* alloc dquot ondisk if needed */
#define XFS_QMOPT_UQUOTA	0x0000004 /* user dquot requested */
#define XFS_QMOPT_PQUOTA	0x0000008 /* project dquot requested */
#define XFS_QMOPT_FORCE_RES	0x0000010 /* ignore quota limits */
#define XFS_QMOPT_SBVERSION	0x0000040 /* change superblock version num */
#define XFS_QMOPT_DOWARN        0x0000400 /* increase warning cnt if needed */
#define XFS_QMOPT_DQREPAIR	0x0001000 /* repair dquot if damaged */
#define XFS_QMOPT_GQUOTA	0x0002000 /* group dquot requested */
#define XFS_QMOPT_ENOSPC	0x0004000 /* enospc instead of edquot (prj) */
#define XFS_QMOPT_DQNEXT	0x0008000 /* return next dquot >= this ID */

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

#define XFS_QMOPT_NOLOCK	0x2000000 /* don't ilock during dqget */

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

extern int xfs_dqcheck(struct xfs_mount *mp, xfs_disk_dquot_t *ddq,
		       xfs_dqid_t id, uint type, uint flags, const char *str);
extern int xfs_calc_dquots_per_chunk(unsigned int nbblks);

#endif	/* __XFS_QUOTA_H__ */
