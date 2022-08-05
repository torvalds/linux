/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef USER_BLK_DRV_CMD_INC_H
#define USER_BLK_DRV_CMD_INC_H

#include <linux/types.h>

/* ublk server command definition */

/*
 * Admin commands, issued by ublk server, and handled by ublk driver.
 */
#define	UBLK_CMD_GET_QUEUE_AFFINITY	0x01
#define	UBLK_CMD_GET_DEV_INFO	0x02
#define	UBLK_CMD_ADD_DEV		0x04
#define	UBLK_CMD_DEL_DEV		0x05
#define	UBLK_CMD_START_DEV	0x06
#define	UBLK_CMD_STOP_DEV	0x07
#define	UBLK_CMD_SET_PARAMS	0x08
#define	UBLK_CMD_GET_PARAMS	0x09

/*
 * IO commands, issued by ublk server, and handled by ublk driver.
 *
 * FETCH_REQ: issued via sqe(URING_CMD) beforehand for fetching IO request
 *      from ublk driver, should be issued only when starting device. After
 *      the associated cqe is returned, request's tag can be retrieved via
 *      cqe->userdata.
 *
 * COMMIT_AND_FETCH_REQ: issued via sqe(URING_CMD) after ublkserver handled
 *      this IO request, request's handling result is committed to ublk
 *      driver, meantime FETCH_REQ is piggyback, and FETCH_REQ has to be
 *      handled before completing io request.
 *
 * NEED_GET_DATA: only used for write requests to set io addr and copy data
 *      When NEED_GET_DATA is set, ublksrv has to issue UBLK_IO_NEED_GET_DATA
 *      command after ublk driver returns UBLK_IO_RES_NEED_GET_DATA.
 *
 *      It is only used if ublksrv set UBLK_F_NEED_GET_DATA flag
 *      while starting a ublk device.
 */
#define	UBLK_IO_FETCH_REQ		0x20
#define	UBLK_IO_COMMIT_AND_FETCH_REQ	0x21
#define	UBLK_IO_NEED_GET_DATA	0x22

/* only ABORT means that no re-fetch */
#define UBLK_IO_RES_OK			0
#define UBLK_IO_RES_NEED_GET_DATA	1
#define UBLK_IO_RES_ABORT		(-ENODEV)

#define UBLKSRV_CMD_BUF_OFFSET	0
#define UBLKSRV_IO_BUF_OFFSET	0x80000000

/* tag bit is 12bit, so at most 4096 IOs for each queue */
#define UBLK_MAX_QUEUE_DEPTH	4096

/*
 * zero copy requires 4k block size, and can remap ublk driver's io
 * request into ublksrv's vm space
 */
#define UBLK_F_SUPPORT_ZERO_COPY	(1ULL << 0)

/*
 * Force to complete io cmd via io_uring_cmd_complete_in_task so that
 * performance comparison is done easily with using task_work_add
 */
#define UBLK_F_URING_CMD_COMP_IN_TASK	(1ULL << 1)

/*
 * User should issue io cmd again for write requests to
 * set io buffer address and copy data from bio vectors
 * to the userspace io buffer.
 *
 * In this mode, task_work is not used.
 */
#define UBLK_F_NEED_GET_DATA (1UL << 2)

/* device state */
#define UBLK_S_DEV_DEAD	0
#define UBLK_S_DEV_LIVE	1

/* shipped via sqe->cmd of io_uring command */
struct ublksrv_ctrl_cmd {
	/* sent to which device, must be valid */
	__u32	dev_id;

	/* sent to which queue, must be -1 if the cmd isn't for queue */
	__u16	queue_id;
	/*
	 * cmd specific buffer, can be IN or OUT.
	 */
	__u16	len;
	__u64	addr;

	/* inline data */
	__u64	data[2];
};

struct ublksrv_ctrl_dev_info {
	__u16	nr_hw_queues;
	__u16	queue_depth;
	__u16	state;
	__u16	pad0;

	__u32	max_io_buf_bytes;
	__u32	dev_id;

	__s32	ublksrv_pid;
	__u32	pad1;

	__u64	flags;

	/* For ublksrv internal use, invisible to ublk driver */
	__u64	ublksrv_flags;

	__u64	reserved0;
	__u64	reserved1;
	__u64   reserved2;
};

#define		UBLK_IO_OP_READ		0
#define		UBLK_IO_OP_WRITE		1
#define		UBLK_IO_OP_FLUSH		2
#define		UBLK_IO_OP_DISCARD	3
#define		UBLK_IO_OP_WRITE_SAME	4
#define		UBLK_IO_OP_WRITE_ZEROES	5

#define		UBLK_IO_F_FAILFAST_DEV		(1U << 8)
#define		UBLK_IO_F_FAILFAST_TRANSPORT	(1U << 9)
#define		UBLK_IO_F_FAILFAST_DRIVER	(1U << 10)
#define		UBLK_IO_F_META			(1U << 11)
#define		UBLK_IO_F_FUA			(1U << 13)
#define		UBLK_IO_F_NOUNMAP		(1U << 15)
#define		UBLK_IO_F_SWAP			(1U << 16)

/*
 * io cmd is described by this structure, and stored in share memory, indexed
 * by request tag.
 *
 * The data is stored by ublk driver, and read by ublksrv after one fetch command
 * returns.
 */
struct ublksrv_io_desc {
	/* op: bit 0-7, flags: bit 8-31 */
	__u32		op_flags;

	__u32		nr_sectors;

	/* start sector for this io */
	__u64		start_sector;

	/* buffer address in ublksrv daemon vm space, from ublk driver */
	__u64		addr;
};

static inline __u8 ublksrv_get_op(const struct ublksrv_io_desc *iod)
{
	return iod->op_flags & 0xff;
}

static inline __u32 ublksrv_get_flags(const struct ublksrv_io_desc *iod)
{
	return iod->op_flags >> 8;
}

/* issued to ublk driver via /dev/ublkcN */
struct ublksrv_io_cmd {
	__u16	q_id;

	/* for fetch/commit which result */
	__u16	tag;

	/* io result, it is valid for COMMIT* command only */
	__s32	result;

	/*
	 * userspace buffer address in ublksrv daemon process, valid for
	 * FETCH* command only
	 */
	__u64	addr;
};

struct ublk_param_basic {
#define UBLK_ATTR_READ_ONLY            (1 << 0)
#define UBLK_ATTR_ROTATIONAL           (1 << 1)
#define UBLK_ATTR_VOLATILE_CACHE       (1 << 2)
#define UBLK_ATTR_FUA                  (1 << 3)
	__u32	attrs;
	__u8	logical_bs_shift;
	__u8	physical_bs_shift;
	__u8	io_opt_shift;
	__u8	io_min_shift;

	__u32	max_sectors;
	__u32	chunk_sectors;

	__u64   dev_sectors;
	__u64   virt_boundary_mask;
};

struct ublk_param_discard {
	__u32	discard_alignment;

	__u32	discard_granularity;
	__u32	max_discard_sectors;

	__u32	max_write_zeroes_sectors;
	__u16	max_discard_segments;
	__u16	reserved0;
};

struct ublk_params {
	/*
	 * Total length of parameters, userspace has to set 'len' for both
	 * SET_PARAMS and GET_PARAMS command, and driver may update len
	 * if two sides use different version of 'ublk_params', same with
	 * 'types' fields.
	 */
	__u32	len;
#define UBLK_PARAM_TYPE_BASIC           (1 << 0)
#define UBLK_PARAM_TYPE_DISCARD         (1 << 1)
	__u32	types;			/* types of parameter included */

	struct ublk_param_basic		basic;
	struct ublk_param_discard	discard;
};

#endif
