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
#ifndef	__XFS_LOG_H__
#define __XFS_LOG_H__

#include "xfs_log_format.h"

struct xfs_log_vec {
	struct xfs_log_vec	*lv_next;	/* next lv in build list */
	int			lv_niovecs;	/* number of iovecs in lv */
	struct xfs_log_iovec	*lv_iovecp;	/* iovec array */
	struct xfs_log_item	*lv_item;	/* owner */
	char			*lv_buf;	/* formatted buffer */
	int			lv_buf_len;	/* size of formatted buffer */
	int			lv_size;	/* size of allocated lv */
};

#define XFS_LOG_VEC_ORDERED	(-1)

/*
 * Structure used to pass callback function and the function's argument
 * to the log manager.
 */
typedef struct xfs_log_callback {
	struct xfs_log_callback	*cb_next;
	void			(*cb_func)(void *, int);
	void			*cb_arg;
} xfs_log_callback_t;

/*
 * By comparing each component, we don't have to worry about extra
 * endian issues in treating two 32 bit numbers as one 64 bit number
 */
static inline xfs_lsn_t	_lsn_cmp(xfs_lsn_t lsn1, xfs_lsn_t lsn2)
{
	if (CYCLE_LSN(lsn1) != CYCLE_LSN(lsn2))
		return (CYCLE_LSN(lsn1)<CYCLE_LSN(lsn2))? -999 : 999;

	if (BLOCK_LSN(lsn1) != BLOCK_LSN(lsn2))
		return (BLOCK_LSN(lsn1)<BLOCK_LSN(lsn2))? -999 : 999;

	return 0;
}

#define	XFS_LSN_CMP(x,y) _lsn_cmp(x,y)

/*
 * Macros, structures, prototypes for interface to the log manager.
 */

/*
 * Flags to xfs_log_done()
 */
#define XFS_LOG_REL_PERM_RESERV	0x1

/*
 * Flags to xfs_log_force()
 *
 *	XFS_LOG_SYNC:	Synchronous force in-core log to disk
 */
#define XFS_LOG_SYNC		0x1

/* Log manager interfaces */
struct xfs_mount;
struct xlog_in_core;
struct xlog_ticket;
struct xfs_log_item;
struct xfs_item_ops;
struct xfs_trans;

void	xfs_log_item_init(struct xfs_mount	*mp,
			struct xfs_log_item	*item,
			int			type,
			const struct xfs_item_ops *ops);

xfs_lsn_t xfs_log_done(struct xfs_mount *mp,
		       struct xlog_ticket *ticket,
		       struct xlog_in_core **iclog,
		       uint		flags);
int	  _xfs_log_force(struct xfs_mount *mp,
			 uint		flags,
			 int		*log_forced);
void	  xfs_log_force(struct xfs_mount	*mp,
			uint			flags);
int	  _xfs_log_force_lsn(struct xfs_mount *mp,
			     xfs_lsn_t		lsn,
			     uint		flags,
			     int		*log_forced);
void	  xfs_log_force_lsn(struct xfs_mount	*mp,
			    xfs_lsn_t		lsn,
			    uint		flags);
int	  xfs_log_mount(struct xfs_mount	*mp,
			struct xfs_buftarg	*log_target,
			xfs_daddr_t		start_block,
			int		 	num_bblocks);
int	  xfs_log_mount_finish(struct xfs_mount *mp);
xfs_lsn_t xlog_assign_tail_lsn(struct xfs_mount *mp);
xfs_lsn_t xlog_assign_tail_lsn_locked(struct xfs_mount *mp);
void	  xfs_log_space_wake(struct xfs_mount *mp);
int	  xfs_log_notify(struct xfs_mount	*mp,
			 struct xlog_in_core	*iclog,
			 xfs_log_callback_t	*callback_entry);
int	  xfs_log_release_iclog(struct xfs_mount *mp,
			 struct xlog_in_core	 *iclog);
int	  xfs_log_reserve(struct xfs_mount *mp,
			  int		   length,
			  int		   count,
			  struct xlog_ticket **ticket,
			  __uint8_t	   clientid,
			  bool		   permanent,
			  uint		   t_type);
int	  xfs_log_regrant(struct xfs_mount *mp, struct xlog_ticket *tic);
int	  xfs_log_unmount_write(struct xfs_mount *mp);
void      xfs_log_unmount(struct xfs_mount *mp);
int	  xfs_log_force_umount(struct xfs_mount *mp, int logerror);
int	  xfs_log_need_covered(struct xfs_mount *mp);

void	  xlog_iodone(struct xfs_buf *);

struct xlog_ticket *xfs_log_ticket_get(struct xlog_ticket *ticket);
void	  xfs_log_ticket_put(struct xlog_ticket *ticket);

int	xfs_log_commit_cil(struct xfs_mount *mp, struct xfs_trans *tp,
				xfs_lsn_t *commit_lsn, int flags);
bool	xfs_log_item_in_current_chkpt(struct xfs_log_item *lip);

void	xfs_log_work_queue(struct xfs_mount *mp);
void	xfs_log_worker(struct work_struct *work);
void	xfs_log_quiesce(struct xfs_mount *mp);

#endif	/* __XFS_LOG_H__ */
