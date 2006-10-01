/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_TRANS_PRIV_H__
#define	__XFS_TRANS_PRIV_H__

struct xfs_log_item;
struct xfs_log_item_desc;
struct xfs_mount;
struct xfs_trans;

/*
 * From xfs_trans_item.c
 */
struct xfs_log_item_desc	*xfs_trans_add_item(struct xfs_trans *,
					    struct xfs_log_item *);
void				xfs_trans_free_item(struct xfs_trans *,
					    struct xfs_log_item_desc *);
struct xfs_log_item_desc	*xfs_trans_find_item(struct xfs_trans *,
					     struct xfs_log_item *);
struct xfs_log_item_desc	*xfs_trans_first_item(struct xfs_trans *);
struct xfs_log_item_desc	*xfs_trans_next_item(struct xfs_trans *,
					     struct xfs_log_item_desc *);
void				xfs_trans_free_items(struct xfs_trans *, int);
void				xfs_trans_unlock_items(struct xfs_trans *,
							xfs_lsn_t);
void				xfs_trans_free_busy(xfs_trans_t *tp);
xfs_log_busy_slot_t		*xfs_trans_add_busy(xfs_trans_t *tp,
						    xfs_agnumber_t ag,
						    xfs_extlen_t idx);

/*
 * From xfs_trans_ail.c
 */
void			xfs_trans_update_ail(struct xfs_mount *mp,
				     struct xfs_log_item *lip, xfs_lsn_t lsn,
				     unsigned long s)
				     __releases(mp->m_ail_lock);
void			xfs_trans_delete_ail(struct xfs_mount *mp,
				     struct xfs_log_item *lip, unsigned long s)
				     __releases(mp->m_ail_lock);
struct xfs_log_item	*xfs_trans_first_ail(struct xfs_mount *, int *);
struct xfs_log_item	*xfs_trans_next_ail(struct xfs_mount *,
				     struct xfs_log_item *, int *, int *);


#endif	/* __XFS_TRANS_PRIV_H__ */
