// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_LOG_H__
#define __XFS_LOG_H__

struct xfs_cil_ctx;

struct xfs_log_vec {
	struct list_head	lv_list;	/* CIL lv chain ptrs */
	uint32_t		lv_order_id;	/* chain ordering info */
	int			lv_niovecs;	/* number of iovecs in lv */
	struct xfs_log_iovec	*lv_iovecp;	/* iovec array */
	struct xfs_log_item	*lv_item;	/* owner */
	char			*lv_buf;	/* formatted buffer */
	int			lv_bytes;	/* accounted space in buffer */
	int			lv_buf_len;	/* aligned size of buffer */
	int			lv_size;	/* size of allocated lv */
};

#define XFS_LOG_VEC_ORDERED	(-1)

/*
 * Calculate the log iovec length for a given user buffer length. Intended to be
 * used by ->iop_size implementations when sizing buffers of arbitrary
 * alignments.
 */
static inline int
xlog_calc_iovec_len(int len)
{
	return roundup(len, sizeof(uint32_t));
}

void *xlog_prepare_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec **vecp,
		uint type);

static inline void
xlog_finish_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec *vec,
		int data_len)
{
	struct xlog_op_header	*oph = vec->i_addr;
	int			len;

	/*
	 * Always round up the length to the correct alignment so callers don't
	 * need to know anything about this log vec layout requirement. This
	 * means we have to zero the area the data to be written does not cover.
	 * This is complicated by fact the payload region is offset into the
	 * logvec region by the opheader that tracks the payload.
	 */
	len = xlog_calc_iovec_len(data_len);
	if (len - data_len != 0) {
		char	*buf = vec->i_addr + sizeof(struct xlog_op_header);

		memset(buf + data_len, 0, len - data_len);
	}

	/*
	 * The opheader tracks aligned payload length, whilst the logvec tracks
	 * the overall region length.
	 */
	oph->oh_len = cpu_to_be32(len);

	len += sizeof(struct xlog_op_header);
	lv->lv_buf_len += len;
	lv->lv_bytes += len;
	vec->i_len = len;

	/* Catch buffer overruns */
	ASSERT((void *)lv->lv_buf + lv->lv_bytes <= (void *)lv + lv->lv_size);
}

/*
 * Copy the amount of data requested by the caller into a new log iovec.
 */
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

static inline void *
xlog_copy_from_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec **vecp,
		const struct xfs_log_iovec *src)
{
	return xlog_copy_iovec(lv, vecp, src->i_type, src->i_addr, src->i_len);
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
struct xlog;

int	  xfs_log_force(struct xfs_mount *mp, uint flags);
int	  xfs_log_force_seq(struct xfs_mount *mp, xfs_csn_t seq, uint flags,
		int *log_forced);
int	  xfs_log_mount(struct xfs_mount	*mp,
			struct xfs_buftarg	*log_target,
			xfs_daddr_t		start_block,
			int		 	num_bblocks);
int	  xfs_log_mount_finish(struct xfs_mount *mp);
void	xfs_log_mount_cancel(struct xfs_mount *);
xfs_lsn_t xlog_assign_tail_lsn(struct xfs_mount *mp);
xfs_lsn_t xlog_assign_tail_lsn_locked(struct xfs_mount *mp);
void	xfs_log_space_wake(struct xfs_mount *mp);
int	xfs_log_reserve(struct xfs_mount *mp, int length, int count,
			struct xlog_ticket **ticket, bool permanent);
int	xfs_log_regrant(struct xfs_mount *mp, struct xlog_ticket *tic);
void	xfs_log_unmount(struct xfs_mount *mp);
bool	xfs_log_writable(struct xfs_mount *mp);

struct xlog_ticket *xfs_log_ticket_get(struct xlog_ticket *ticket);
void	  xfs_log_ticket_put(struct xlog_ticket *ticket);

void	xlog_cil_process_committed(struct list_head *list);
bool	xfs_log_item_in_current_chkpt(struct xfs_log_item *lip);

void	xfs_log_work_queue(struct xfs_mount *mp);
int	xfs_log_quiesce(struct xfs_mount *mp);
void	xfs_log_clean(struct xfs_mount *mp);
bool	xfs_log_check_lsn(struct xfs_mount *, xfs_lsn_t);

bool	  xlog_force_shutdown(struct xlog *log, uint32_t shutdown_flags);

int xfs_attr_use_log_assist(struct xfs_mount *mp);

#endif	/* __XFS_LOG_H__ */
