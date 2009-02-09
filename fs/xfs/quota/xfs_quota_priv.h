/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
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
#ifndef __XFS_QUOTA_PRIV_H__
#define __XFS_QUOTA_PRIV_H__

/*
 * Number of bmaps that we ask from bmapi when doing a quotacheck.
 * We make this restriction to keep the memory usage to a minimum.
 */
#define XFS_DQITER_MAP_SIZE	10

/* Number of dquots that fit in to a dquot block */
#define XFS_QM_DQPERBLK(mp)	((mp)->m_quotainfo->qi_dqperchunk)

#define XFS_DQ_IS_ADDEDTO_TRX(t, d)	((d)->q_transp == (t))

#define XFS_QI_MPLRECLAIMS(mp)	((mp)->m_quotainfo->qi_dqreclaims)
#define XFS_QI_UQIP(mp)		((mp)->m_quotainfo->qi_uquotaip)
#define XFS_QI_GQIP(mp)		((mp)->m_quotainfo->qi_gquotaip)
#define XFS_QI_DQCHUNKLEN(mp)	((mp)->m_quotainfo->qi_dqchunklen)
#define XFS_QI_BTIMELIMIT(mp)	((mp)->m_quotainfo->qi_btimelimit)
#define XFS_QI_RTBTIMELIMIT(mp) ((mp)->m_quotainfo->qi_rtbtimelimit)
#define XFS_QI_ITIMELIMIT(mp)	((mp)->m_quotainfo->qi_itimelimit)
#define XFS_QI_BWARNLIMIT(mp)	((mp)->m_quotainfo->qi_bwarnlimit)
#define XFS_QI_RTBWARNLIMIT(mp)	((mp)->m_quotainfo->qi_rtbwarnlimit)
#define XFS_QI_IWARNLIMIT(mp)	((mp)->m_quotainfo->qi_iwarnlimit)
#define XFS_QI_QOFFLOCK(mp)	((mp)->m_quotainfo->qi_quotaofflock)

#define XFS_QI_MPL_LIST(mp)	((mp)->m_quotainfo->qi_dqlist)
#define XFS_QI_MPLNEXT(mp)	((mp)->m_quotainfo->qi_dqlist.qh_next)
#define XFS_QI_MPLNDQUOTS(mp)	((mp)->m_quotainfo->qi_dqlist.qh_nelems)

#define xfs_qm_mplist_lock(mp) \
	mutex_lock(&(XFS_QI_MPL_LIST(mp).qh_lock))
#define xfs_qm_mplist_nowait(mp) \
	mutex_trylock(&(XFS_QI_MPL_LIST(mp).qh_lock))
#define xfs_qm_mplist_unlock(mp) \
	mutex_unlock(&(XFS_QI_MPL_LIST(mp).qh_lock))
#define XFS_QM_IS_MPLIST_LOCKED(mp) \
	mutex_is_locked(&(XFS_QI_MPL_LIST(mp).qh_lock))

#define xfs_qm_freelist_lock(qm) \
	mutex_lock(&((qm)->qm_dqfreelist.qh_lock))
#define xfs_qm_freelist_lock_nowait(qm) \
	mutex_trylock(&((qm)->qm_dqfreelist.qh_lock))
#define xfs_qm_freelist_unlock(qm) \
	mutex_unlock(&((qm)->qm_dqfreelist.qh_lock))

/*
 * Hash into a bucket in the dquot hash table, based on <mp, id>.
 */
#define XFS_DQ_HASHVAL(mp, id) (((__psunsigned_t)(mp) + \
				 (__psunsigned_t)(id)) & \
				(xfs_Gqm->qm_dqhashmask - 1))
#define XFS_DQ_HASH(mp, id, type)   (type == XFS_DQ_USER ? \
				     (xfs_Gqm->qm_usr_dqhtable + \
				      XFS_DQ_HASHVAL(mp, id)) : \
				     (xfs_Gqm->qm_grp_dqhtable + \
				      XFS_DQ_HASHVAL(mp, id)))
#define XFS_IS_DQTYPE_ON(mp, type)   (type == XFS_DQ_USER ? \
					XFS_IS_UQUOTA_ON(mp) : \
					XFS_IS_OQUOTA_ON(mp))
#define XFS_IS_DQUOT_UNINITIALIZED(dqp) ( \
	!dqp->q_core.d_blk_hardlimit && \
	!dqp->q_core.d_blk_softlimit && \
	!dqp->q_core.d_rtb_hardlimit && \
	!dqp->q_core.d_rtb_softlimit && \
	!dqp->q_core.d_ino_hardlimit && \
	!dqp->q_core.d_ino_softlimit && \
	!dqp->q_core.d_bcount && \
	!dqp->q_core.d_rtbcount && \
	!dqp->q_core.d_icount)

#define HL_PREVP	dq_hashlist.ql_prevp
#define HL_NEXT		dq_hashlist.ql_next
#define MPL_PREVP	dq_mplist.ql_prevp
#define MPL_NEXT	dq_mplist.ql_next


#define _LIST_REMOVE(h, dqp, PVP, NXT)				\
	{							\
		 xfs_dquot_t *d;				\
		 if (((d) = (dqp)->NXT))				\
			 (d)->PVP = (dqp)->PVP;			\
		 *((dqp)->PVP) = d;				\
		 (dqp)->NXT = NULL;				\
		 (dqp)->PVP = NULL;				\
		 (h)->qh_version++;				\
		 (h)->qh_nelems--;				\
	}

#define _LIST_INSERT(h, dqp, PVP, NXT)				\
	{							\
		 xfs_dquot_t *d;				\
		 if (((d) = (h)->qh_next))			\
			 (d)->PVP = &((dqp)->NXT);		\
		 (dqp)->NXT = d;				\
		 (dqp)->PVP = &((h)->qh_next);			\
		 (h)->qh_next = dqp;				\
		 (h)->qh_version++;				\
		 (h)->qh_nelems++;				\
	 }

#define FOREACH_DQUOT_IN_MP(dqp, mp) \
	for ((dqp) = XFS_QI_MPLNEXT(mp); (dqp) != NULL; (dqp) = (dqp)->MPL_NEXT)

#define FOREACH_DQUOT_IN_FREELIST(dqp, qlist)	\
for ((dqp) = (qlist)->qh_next; (dqp) != (xfs_dquot_t *)(qlist); \
     (dqp) = (dqp)->dq_flnext)

#define XQM_HASHLIST_INSERT(h, dqp)	\
	 _LIST_INSERT(h, dqp, HL_PREVP, HL_NEXT)

#define XQM_FREELIST_INSERT(h, dqp)	\
	 xfs_qm_freelist_append(h, dqp)

#define XQM_MPLIST_INSERT(h, dqp)	\
	 _LIST_INSERT(h, dqp, MPL_PREVP, MPL_NEXT)

#define XQM_HASHLIST_REMOVE(h, dqp)	\
	 _LIST_REMOVE(h, dqp, HL_PREVP, HL_NEXT)
#define XQM_FREELIST_REMOVE(dqp)	\
	 xfs_qm_freelist_unlink(dqp)
#define XQM_MPLIST_REMOVE(h, dqp)	\
	{ _LIST_REMOVE(h, dqp, MPL_PREVP, MPL_NEXT); \
	  XFS_QI_MPLRECLAIMS((dqp)->q_mount)++; }

#define XFS_DQ_IS_LOGITEM_INITD(dqp)	((dqp)->q_logitem.qli_dquot == (dqp))

#define XFS_QM_DQP_TO_DQACCT(tp, dqp)	(XFS_QM_ISUDQ(dqp) ? \
					 (tp)->t_dqinfo->dqa_usrdquots : \
					 (tp)->t_dqinfo->dqa_grpdquots)
#define XFS_IS_SUSER_DQUOT(dqp)		\
	(!((dqp)->q_core.d_id))

#define DQFLAGTO_TYPESTR(d)	(((d)->dq_flags & XFS_DQ_USER) ? "USR" : \
				 (((d)->dq_flags & XFS_DQ_GROUP) ? "GRP" : \
				 (((d)->dq_flags & XFS_DQ_PROJ) ? "PRJ":"???")))

#endif	/* __XFS_QUOTA_PRIV_H__ */
