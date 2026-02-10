// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_LOG_H__
#define __XFS_LOG_H__

struct xlog_format_buf;
struct xfs_cil_ctx;

/* Region types for iovec's i_type */
#define XLOG_REG_TYPE_BFORMAT		1
#define XLOG_REG_TYPE_BCHUNK		2
#define XLOG_REG_TYPE_EFI_FORMAT	3
#define XLOG_REG_TYPE_EFD_FORMAT	4
#define XLOG_REG_TYPE_IFORMAT		5
#define XLOG_REG_TYPE_ICORE		6
#define XLOG_REG_TYPE_IEXT		7
#define XLOG_REG_TYPE_IBROOT		8
#define XLOG_REG_TYPE_ILOCAL		9
#define XLOG_REG_TYPE_IATTR_EXT		10
#define XLOG_REG_TYPE_IATTR_BROOT	11
#define XLOG_REG_TYPE_IATTR_LOCAL	12
#define XLOG_REG_TYPE_QFORMAT		13
#define XLOG_REG_TYPE_DQUOT		14
#define XLOG_REG_TYPE_QUOTAOFF		15
#define XLOG_REG_TYPE_LRHEADER		16
#define XLOG_REG_TYPE_UNMOUNT		17
#define XLOG_REG_TYPE_COMMIT		18
#define XLOG_REG_TYPE_TRANSHDR		19
#define XLOG_REG_TYPE_ICREATE		20
#define XLOG_REG_TYPE_RUI_FORMAT	21
#define XLOG_REG_TYPE_RUD_FORMAT	22
#define XLOG_REG_TYPE_CUI_FORMAT	23
#define XLOG_REG_TYPE_CUD_FORMAT	24
#define XLOG_REG_TYPE_BUI_FORMAT	25
#define XLOG_REG_TYPE_BUD_FORMAT	26
#define XLOG_REG_TYPE_ATTRI_FORMAT	27
#define XLOG_REG_TYPE_ATTRD_FORMAT	28
#define XLOG_REG_TYPE_ATTR_NAME		29
#define XLOG_REG_TYPE_ATTR_VALUE	30
#define XLOG_REG_TYPE_XMI_FORMAT	31
#define XLOG_REG_TYPE_XMD_FORMAT	32
#define XLOG_REG_TYPE_ATTR_NEWNAME	33
#define XLOG_REG_TYPE_ATTR_NEWVALUE	34
#define XLOG_REG_TYPE_MAX		34

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

void *xlog_format_start(struct xlog_format_buf *lfb, uint16_t type);
void xlog_format_commit(struct xlog_format_buf *lfb, unsigned int data_len);

/*
 * Copy the amount of data requested by the caller into a new log iovec.
 */
static inline void *
xlog_format_copy(
	struct xlog_format_buf	*lfb,
	uint16_t		type,
	void			*data,
	unsigned int		len)
{
	void *buf;

	buf = xlog_format_start(lfb, type);
	memcpy(buf, data, len);
	xlog_format_commit(lfb, len);
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

#endif	/* __XFS_LOG_H__ */
