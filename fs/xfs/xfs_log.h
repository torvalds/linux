// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_LOG_H__
#define __XFS_LOG_H__

struct xfs_cil_ctx;

struct xfs_log_vec {
	struct xfs_log_vec	*lv_next;	/* next lv in build list */
	int			lv_niovecs;	/* number of iovecs in lv */
	struct xfs_log_iovec	*lv_iovecp;	/* iovec array */
	struct xfs_log_item	*lv_item;	/* owner */
	char			*lv_buf;	/* formatted buffer */
	int			lv_bytes;	/* accounted space in buffer */
	int			lv_buf_len;	/* aligned size of buffer */
	int			lv_size;	/* size of allocated lv */
};

#define XFS_LOG_VEC_ORDERED	(-1)

static inline void *
xlog_prepare_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec **vecp,
		uint type)
{
	struct xfs_log_iovec *vec = *vecp;

	if (vec) {
		ASSERT(vec - lv->lv_iovecp < lv->lv_niovecs);
		vec++;
	} else {
		vec = &lv->lv_iovecp[0];
	}

	vec->i_type = type;
	vec->i_addr = lv->lv_buf + lv->lv_buf_len;

	ASSERT(IS_ALIGNED((unsigned long)vec->i_addr, sizeof(uint64_t)));

	*vecp = vec;
	return vec->i_addr;
}

/*
 * We need to make sure the next buffer is naturally aligned for the biggest
 * basic data type we put into it.  We already accounted for this padding when
 * sizing the buffer.
 *
 * However, this padding does not get written into the log, and hence we have to
 * track the space used by the log vectors separately to prevent log space hangs
 * due to inaccurate accounting (i.e. a leak) of the used log space through the
 * CIL context ticket.
 */
static inline void
xlog_finish_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec *vec, int len)
{
	lv->lv_buf_len += round_up(len, sizeof(uint64_t));
	lv->lv_bytes += len;
	vec->i_len = len;
}

static inline void *
xlog_copy_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec **vecp,
		uint type, void *data, int len)
{
	void *buf;

	buf = xlog_prepare_iovec(lv, vecp, type);
	memcpy(buf, data, len);
	xlog_finish_iovec(lv, *vecp, len);
	return buf;
}

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

xfs_lsn_t xfs_log_done(struct xfs_mount *mp,
		       struct xlog_ticket *ticket,
		       struct xlog_in_core **iclog,
		       bool regrant);
int	  xfs_log_force(struct xfs_mount *mp, uint flags);
int	  xfs_log_force_lsn(struct xfs_mount *mp, xfs_lsn_t lsn, uint flags,
		int *log_forced);
int	  xfs_log_mount(struct xfs_mount	*mp,
			struct xfs_buftarg	*log_target,
			xfs_daddr_t		start_block,
			int		 	num_bblocks);
int	  xfs_log_mount_finish(struct xfs_mount *mp);
int	xfs_log_mount_cancel(struct xfs_mount *);
xfs_lsn_t xlog_assign_tail_lsn(struct xfs_mount *mp);
xfs_lsn_t xlog_assign_tail_lsn_locked(struct xfs_mount *mp);
void	  xfs_log_space_wake(struct xfs_mount *mp);
int	  xfs_log_release_iclog(struct xfs_mount *mp,
			 struct xlog_in_core	 *iclog);
int	  xfs_log_reserve(struct xfs_mount *mp,
			  int		   length,
			  int		   count,
			  struct xlog_ticket **ticket,
			  uint8_t		   clientid,
			  bool		   permanent);
int	  xfs_log_regrant(struct xfs_mount *mp, struct xlog_ticket *tic);
void      xfs_log_unmount(struct xfs_mount *mp);
int	  xfs_log_force_umount(struct xfs_mount *mp, int logerror);

struct xlog_ticket *xfs_log_ticket_get(struct xlog_ticket *ticket);
void	  xfs_log_ticket_put(struct xlog_ticket *ticket);

void	xfs_log_commit_cil(struct xfs_mount *mp, struct xfs_trans *tp,
				xfs_lsn_t *commit_lsn, bool regrant);
void	xlog_cil_process_committed(struct list_head *list, bool aborted);
bool	xfs_log_item_in_current_chkpt(struct xfs_log_item *lip);

void	xfs_log_work_queue(struct xfs_mount *mp);
void	xfs_log_quiesce(struct xfs_mount *mp);
bool	xfs_log_check_lsn(struct xfs_mount *, xfs_lsn_t);
bool	xfs_log_in_recovery(struct xfs_mount *);

#endif	/* __XFS_LOG_H__ */
