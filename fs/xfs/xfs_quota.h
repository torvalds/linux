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
#ifndef __XFS_QUOTA_H__
#define __XFS_QUOTA_H__

struct xfs_trans;

/*
 * The ondisk form of a dquot structure.
 */
#define XFS_DQUOT_MAGIC		0x4451		/* 'DQ' */
#define XFS_DQUOT_VERSION	(u_int8_t)0x01	/* latest version number */

/*
 * uid_t and gid_t are hard-coded to 32 bits in the inode.
 * Hence, an 'id' in a dquot is 32 bits..
 */
typedef __uint32_t	xfs_dqid_t;

/*
 * Even though users may not have quota limits occupying all 64-bits,
 * they may need 64-bit accounting. Hence, 64-bit quota-counters,
 * and quota-limits. This is a waste in the common case, but hey ...
 */
typedef __uint64_t	xfs_qcnt_t;
typedef __uint16_t	xfs_qwarncnt_t;

/*
 * This is the main portion of the on-disk representation of quota
 * information for a user. This is the q_core of the xfs_dquot_t that
 * is kept in kernel memory. We pad this with some more expansion room
 * to construct the on disk structure.
 */
typedef struct	xfs_disk_dquot {
	__be16		d_magic;	/* dquot magic = XFS_DQUOT_MAGIC */
	__u8		d_version;	/* dquot version */
	__u8		d_flags;	/* XFS_DQ_USER/PROJ/GROUP */
	__be32		d_id;		/* user,project,group id */
	__be64		d_blk_hardlimit;/* absolute limit on disk blks */
	__be64		d_blk_softlimit;/* preferred limit on disk blks */
	__be64		d_ino_hardlimit;/* maximum # allocated inodes */
	__be64		d_ino_softlimit;/* preferred inode limit */
	__be64		d_bcount;	/* disk blocks owned by the user */
	__be64		d_icount;	/* inodes owned by the user */
	__be32		d_itimer;	/* zero if within inode limits if not,
					   this is when we refuse service */
	__be32		d_btimer;	/* similar to above; for disk blocks */
	__be16		d_iwarns;	/* warnings issued wrt num inodes */
	__be16		d_bwarns;	/* warnings issued wrt disk blocks */
	__be32		d_pad0;		/* 64 bit align */
	__be64		d_rtb_hardlimit;/* absolute limit on realtime blks */
	__be64		d_rtb_softlimit;/* preferred limit on RT disk blks */
	__be64		d_rtbcount;	/* realtime blocks owned */
	__be32		d_rtbtimer;	/* similar to above; for RT disk blocks */
	__be16		d_rtbwarns;	/* warnings issued wrt RT disk blocks */
	__be16		d_pad;
} xfs_disk_dquot_t;

/*
 * This is what goes on disk. This is separated from the xfs_disk_dquot because
 * carrying the unnecessary padding would be a waste of memory.
 */
typedef struct xfs_dqblk {
	xfs_disk_dquot_t  dd_diskdq;	/* portion that lives incore as well */
	char		  dd_fill[4];	/* filling for posterity */

	/*
	 * These two are only present on filesystems with the CRC bits set.
	 */
	__be32		  dd_crc;	/* checksum */
	__be64		  dd_lsn;	/* last modification in log */
	uuid_t		  dd_uuid;	/* location information */
} xfs_dqblk_t;

/*
 * flags for q_flags field in the dquot.
 */
#define XFS_DQ_USER		0x0001		/* a user quota */
#define XFS_DQ_PROJ		0x0002		/* project quota */
#define XFS_DQ_GROUP		0x0004		/* a group quota */
#define XFS_DQ_DIRTY		0x0008		/* dquot is dirty */
#define XFS_DQ_FREEING		0x0010		/* dquot is beeing torn down */

#define XFS_DQ_ALLTYPES		(XFS_DQ_USER|XFS_DQ_PROJ|XFS_DQ_GROUP)

#define XFS_DQ_FLAGS \
	{ XFS_DQ_USER,		"USER" }, \
	{ XFS_DQ_PROJ,		"PROJ" }, \
	{ XFS_DQ_GROUP,		"GROUP" }, \
	{ XFS_DQ_DIRTY,		"DIRTY" }, \
	{ XFS_DQ_FREEING,	"FREEING" }

/*
 * In the worst case, when both user and group quotas are on,
 * we can have a max of three dquots changing in a single transaction.
 */
#define XFS_DQUOT_LOGRES(mp)	(sizeof(xfs_disk_dquot_t) * 3)


/*
 * These are the structures used to lay out dquots and quotaoff
 * records on the log. Quite similar to those of inodes.
 */

/*
 * log format struct for dquots.
 * The first two fields must be the type and size fitting into
 * 32 bits : log_recovery code assumes that.
 */
typedef struct xfs_dq_logformat {
	__uint16_t		qlf_type;      /* dquot log item type */
	__uint16_t		qlf_size;      /* size of this item */
	xfs_dqid_t		qlf_id;	       /* usr/grp/proj id : 32 bits */
	__int64_t		qlf_blkno;     /* blkno of dquot buffer */
	__int32_t		qlf_len;       /* len of dquot buffer */
	__uint32_t		qlf_boffset;   /* off of dquot in buffer */
} xfs_dq_logformat_t;

/*
 * log format struct for QUOTAOFF records.
 * The first two fields must be the type and size fitting into
 * 32 bits : log_recovery code assumes that.
 * We write two LI_QUOTAOFF logitems per quotaoff, the last one keeps a pointer
 * to the first and ensures that the first logitem is taken out of the AIL
 * only when the last one is securely committed.
 */
typedef struct xfs_qoff_logformat {
	unsigned short		qf_type;	/* quotaoff log item type */
	unsigned short		qf_size;	/* size of this item */
	unsigned int		qf_flags;	/* USR and/or GRP */
	char			qf_pad[12];	/* padding for future */
} xfs_qoff_logformat_t;


/*
 * Disk quotas status in m_qflags, and also sb_qflags. 16 bits.
 */
#define XFS_UQUOTA_ACCT	0x0001  /* user quota accounting ON */
#define XFS_UQUOTA_ENFD	0x0002  /* user quota limits enforced */
#define XFS_UQUOTA_CHKD	0x0004  /* quotacheck run on usr quotas */
#define XFS_PQUOTA_ACCT	0x0008  /* project quota accounting ON */
#define XFS_OQUOTA_ENFD	0x0010  /* other (grp/prj) quota limits enforced */
#define XFS_OQUOTA_CHKD	0x0020  /* quotacheck run on other (grp/prj) quotas */
#define XFS_GQUOTA_ACCT	0x0040  /* group quota accounting ON */

/*
 * Quota Accounting/Enforcement flags
 */
#define XFS_ALL_QUOTA_ACCT	\
		(XFS_UQUOTA_ACCT | XFS_GQUOTA_ACCT | XFS_PQUOTA_ACCT)
#define XFS_ALL_QUOTA_ENFD	(XFS_UQUOTA_ENFD | XFS_OQUOTA_ENFD)
#define XFS_ALL_QUOTA_CHKD	(XFS_UQUOTA_CHKD | XFS_OQUOTA_CHKD)

#define XFS_IS_QUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_ALL_QUOTA_ACCT)
#define XFS_IS_UQUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_UQUOTA_ACCT)
#define XFS_IS_PQUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_PQUOTA_ACCT)
#define XFS_IS_GQUOTA_RUNNING(mp)	((mp)->m_qflags & XFS_GQUOTA_ACCT)
#define XFS_IS_UQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_UQUOTA_ENFD)
#define XFS_IS_OQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_OQUOTA_ENFD)

/*
 * Incore only flags for quotaoff - these bits get cleared when quota(s)
 * are in the process of getting turned off. These flags are in m_qflags but
 * never in sb_qflags.
 */
#define XFS_UQUOTA_ACTIVE	0x0100  /* uquotas are being turned off */
#define XFS_PQUOTA_ACTIVE	0x0200  /* pquotas are being turned off */
#define XFS_GQUOTA_ACTIVE	0x0400  /* gquotas are being turned off */
#define XFS_ALL_QUOTA_ACTIVE	\
	(XFS_UQUOTA_ACTIVE | XFS_PQUOTA_ACTIVE | XFS_GQUOTA_ACTIVE)

/*
 * Checking XFS_IS_*QUOTA_ON() while holding any inode lock guarantees
 * quota will be not be switched off as long as that inode lock is held.
 */
#define XFS_IS_QUOTA_ON(mp)	((mp)->m_qflags & (XFS_UQUOTA_ACTIVE | \
						   XFS_GQUOTA_ACTIVE | \
						   XFS_PQUOTA_ACTIVE))
#define XFS_IS_OQUOTA_ON(mp)	((mp)->m_qflags & (XFS_GQUOTA_ACTIVE | \
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

#ifdef __KERNEL__
/*
 * This check is done typically without holding the inode lock;
 * that may seem racy, but it is harmless in the context that it is used.
 * The inode cannot go inactive as long a reference is kept, and
 * therefore if dquot(s) were attached, they'll stay consistent.
 * If, for example, the ownership of the inode changes while
 * we didn't have the inode locked, the appropriate dquot(s) will be
 * attached atomically.
 */
#define XFS_NOT_DQATTACHED(mp, ip) ((XFS_IS_UQUOTA_ON(mp) &&\
				     (ip)->i_udquot == NULL) || \
				    (XFS_IS_OQUOTA_ON(mp) && \
				     (ip)->i_gdquot == NULL))

#define XFS_QM_NEED_QUOTACHECK(mp) \
	((XFS_IS_UQUOTA_ON(mp) && \
		(mp->m_sb.sb_qflags & XFS_UQUOTA_CHKD) == 0) || \
	 (XFS_IS_GQUOTA_ON(mp) && \
		((mp->m_sb.sb_qflags & XFS_OQUOTA_CHKD) == 0 || \
		 (mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT))) || \
	 (XFS_IS_PQUOTA_ON(mp) && \
		((mp->m_sb.sb_qflags & XFS_OQUOTA_CHKD) == 0 || \
		 (mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT))))

#define XFS_MOUNT_QUOTA_SET1	(XFS_UQUOTA_ACCT|XFS_UQUOTA_ENFD|\
				 XFS_UQUOTA_CHKD|XFS_PQUOTA_ACCT|\
				 XFS_OQUOTA_ENFD|XFS_OQUOTA_CHKD)

#define XFS_MOUNT_QUOTA_SET2	(XFS_UQUOTA_ACCT|XFS_UQUOTA_ENFD|\
				 XFS_UQUOTA_CHKD|XFS_GQUOTA_ACCT|\
				 XFS_OQUOTA_ENFD|XFS_OQUOTA_CHKD)

#define XFS_MOUNT_QUOTA_ALL	(XFS_UQUOTA_ACCT|XFS_UQUOTA_ENFD|\
				 XFS_UQUOTA_CHKD|XFS_PQUOTA_ACCT|\
				 XFS_OQUOTA_ENFD|XFS_OQUOTA_CHKD|\
				 XFS_GQUOTA_ACCT)


/*
 * The structure kept inside the xfs_trans_t keep track of dquot changes
 * within a transaction and apply them later.
 */
typedef struct xfs_dqtrx {
	struct xfs_dquot *qt_dquot;	  /* the dquot this refers to */
	ulong		qt_blk_res;	  /* blks reserved on a dquot */
	ulong		qt_blk_res_used;  /* blks used from the reservation */
	ulong		qt_ino_res;	  /* inode reserved on a dquot */
	ulong		qt_ino_res_used;  /* inodes used from the reservation */
	long		qt_bcount_delta;  /* dquot blk count changes */
	long		qt_delbcnt_delta; /* delayed dquot blk count changes */
	long		qt_icount_delta;  /* dquot inode count changes */
	ulong		qt_rtblk_res;	  /* # blks reserved on a dquot */
	ulong		qt_rtblk_res_used;/* # blks used from reservation */
	long		qt_rtbcount_delta;/* dquot realtime blk changes */
	long		qt_delrtb_delta;  /* delayed RT blk count changes */
} xfs_dqtrx_t;

#ifdef CONFIG_XFS_QUOTA
extern void xfs_trans_dup_dqinfo(struct xfs_trans *, struct xfs_trans *);
extern void xfs_trans_free_dqinfo(struct xfs_trans *);
extern void xfs_trans_mod_dquot_byino(struct xfs_trans *, struct xfs_inode *,
		uint, long);
extern void xfs_trans_apply_dquot_deltas(struct xfs_trans *);
extern void xfs_trans_unreserve_and_mod_dquots(struct xfs_trans *);
extern int xfs_trans_reserve_quota_nblks(struct xfs_trans *,
		struct xfs_inode *, long, long, uint);
extern int xfs_trans_reserve_quota_bydquots(struct xfs_trans *,
		struct xfs_mount *, struct xfs_dquot *,
		struct xfs_dquot *, long, long, uint);

extern int xfs_qm_vop_dqalloc(struct xfs_inode *, uid_t, gid_t, prid_t, uint,
		struct xfs_dquot **, struct xfs_dquot **);
extern void xfs_qm_vop_create_dqattach(struct xfs_trans *, struct xfs_inode *,
		struct xfs_dquot *, struct xfs_dquot *);
extern int xfs_qm_vop_rename_dqattach(struct xfs_inode **);
extern struct xfs_dquot *xfs_qm_vop_chown(struct xfs_trans *,
		struct xfs_inode *, struct xfs_dquot **, struct xfs_dquot *);
extern int xfs_qm_vop_chown_reserve(struct xfs_trans *, struct xfs_inode *,
		struct xfs_dquot *, struct xfs_dquot *, uint);
extern int xfs_qm_dqattach(struct xfs_inode *, uint);
extern int xfs_qm_dqattach_locked(struct xfs_inode *, uint);
extern void xfs_qm_dqdetach(struct xfs_inode *);
extern void xfs_qm_dqrele(struct xfs_dquot *);
extern void xfs_qm_statvfs(struct xfs_inode *, struct kstatfs *);
extern int xfs_qm_newmount(struct xfs_mount *, uint *, uint *);
extern void xfs_qm_mount_quotas(struct xfs_mount *);
extern void xfs_qm_unmount(struct xfs_mount *);
extern void xfs_qm_unmount_quotas(struct xfs_mount *);

#else
static inline int
xfs_qm_vop_dqalloc(struct xfs_inode *ip, uid_t uid, gid_t gid, prid_t prid,
		uint flags, struct xfs_dquot **udqp, struct xfs_dquot **gdqp)
{
	*udqp = NULL;
	*gdqp = NULL;
	return 0;
}
#define xfs_trans_dup_dqinfo(tp, tp2)
#define xfs_trans_free_dqinfo(tp)
#define xfs_trans_mod_dquot_byino(tp, ip, fields, delta)
#define xfs_trans_apply_dquot_deltas(tp)
#define xfs_trans_unreserve_and_mod_dquots(tp)
static inline int xfs_trans_reserve_quota_nblks(struct xfs_trans *tp,
		struct xfs_inode *ip, long nblks, long ninos, uint flags)
{
	return 0;
}
static inline int xfs_trans_reserve_quota_bydquots(struct xfs_trans *tp,
		struct xfs_mount *mp, struct xfs_dquot *udqp,
		struct xfs_dquot *gdqp, long nblks, long nions, uint flags)
{
	return 0;
}
#define xfs_qm_vop_create_dqattach(tp, ip, u, g)
#define xfs_qm_vop_rename_dqattach(it)					(0)
#define xfs_qm_vop_chown(tp, ip, old, new)				(NULL)
#define xfs_qm_vop_chown_reserve(tp, ip, u, g, fl)			(0)
#define xfs_qm_dqattach(ip, fl)						(0)
#define xfs_qm_dqattach_locked(ip, fl)					(0)
#define xfs_qm_dqdetach(ip)
#define xfs_qm_dqrele(d)
#define xfs_qm_statvfs(ip, s)
#define xfs_qm_newmount(mp, a, b)					(0)
#define xfs_qm_mount_quotas(mp)
#define xfs_qm_unmount(mp)
#define xfs_qm_unmount_quotas(mp)
#endif /* CONFIG_XFS_QUOTA */

#define xfs_trans_unreserve_quota_nblks(tp, ip, nblks, ninos, flags) \
	xfs_trans_reserve_quota_nblks(tp, ip, -(nblks), -(ninos), flags)
#define xfs_trans_reserve_quota(tp, mp, ud, gd, nb, ni, f) \
	xfs_trans_reserve_quota_bydquots(tp, mp, ud, gd, nb, ni, \
				f | XFS_QMOPT_RES_REGBLKS)

extern int xfs_qm_dqcheck(struct xfs_mount *, xfs_disk_dquot_t *,
				xfs_dqid_t, uint, uint, char *);
extern int xfs_mount_reset_sbqflags(struct xfs_mount *);

extern const struct xfs_buf_ops xfs_dquot_buf_ops;

#endif	/* __KERNEL__ */
#endif	/* __XFS_QUOTA_H__ */
