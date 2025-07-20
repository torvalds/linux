/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef USER_BLK_DRV_CMD_INC_H
#define USER_BLK_DRV_CMD_INC_H

#include <linux/types.h>

/* ublk server command definition */

/*
 * Admin commands, issued by ublk server, and handled by ublk driver.
 *
 * Legacy command definition, don't use in new application, and don't
 * add new such definition any more
 */
#define	UBLK_CMD_GET_QUEUE_AFFINITY	0x01
#define	UBLK_CMD_GET_DEV_INFO	0x02
#define	UBLK_CMD_ADD_DEV		0x04
#define	UBLK_CMD_DEL_DEV		0x05
#define	UBLK_CMD_START_DEV	0x06
#define	UBLK_CMD_STOP_DEV	0x07
#define	UBLK_CMD_SET_PARAMS	0x08
#define	UBLK_CMD_GET_PARAMS	0x09
#define	UBLK_CMD_START_USER_RECOVERY	0x10
#define	UBLK_CMD_END_USER_RECOVERY	0x11
#define	UBLK_CMD_GET_DEV_INFO2		0x12

/* Any new ctrl command should encode by __IO*() */
#define UBLK_U_CMD_GET_QUEUE_AFFINITY	\
	_IOR('u', UBLK_CMD_GET_QUEUE_AFFINITY, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_GET_DEV_INFO		\
	_IOR('u', UBLK_CMD_GET_DEV_INFO, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_ADD_DEV		\
	_IOWR('u', UBLK_CMD_ADD_DEV, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_DEL_DEV		\
	_IOWR('u', UBLK_CMD_DEL_DEV, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_START_DEV		\
	_IOWR('u', UBLK_CMD_START_DEV, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_STOP_DEV		\
	_IOWR('u', UBLK_CMD_STOP_DEV, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_SET_PARAMS		\
	_IOWR('u', UBLK_CMD_SET_PARAMS, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_GET_PARAMS		\
	_IOR('u', UBLK_CMD_GET_PARAMS, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_START_USER_RECOVERY	\
	_IOWR('u', UBLK_CMD_START_USER_RECOVERY, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_END_USER_RECOVERY	\
	_IOWR('u', UBLK_CMD_END_USER_RECOVERY, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_GET_DEV_INFO2	\
	_IOR('u', UBLK_CMD_GET_DEV_INFO2, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_GET_FEATURES	\
	_IOR('u', 0x13, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_DEL_DEV_ASYNC	\
	_IOR('u', 0x14, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_UPDATE_SIZE		\
	_IOWR('u', 0x15, struct ublksrv_ctrl_cmd)
#define UBLK_U_CMD_QUIESCE_DEV		\
	_IOWR('u', 0x16, struct ublksrv_ctrl_cmd)

/*
 * 64bits are enough now, and it should be easy to extend in case of
 * running out of feature flags
 */
#define UBLK_FEATURES_LEN  8

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

/*
 * Legacy IO command definition, don't use in new application, and don't
 * add new such definition any more
 */
#define	UBLK_IO_FETCH_REQ		0x20
#define	UBLK_IO_COMMIT_AND_FETCH_REQ	0x21
#define	UBLK_IO_NEED_GET_DATA	0x22

/* Any new IO command should encode by __IOWR() */
#define	UBLK_U_IO_FETCH_REQ		\
	_IOWR('u', UBLK_IO_FETCH_REQ, struct ublksrv_io_cmd)
#define	UBLK_U_IO_COMMIT_AND_FETCH_REQ	\
	_IOWR('u', UBLK_IO_COMMIT_AND_FETCH_REQ, struct ublksrv_io_cmd)
#define	UBLK_U_IO_NEED_GET_DATA		\
	_IOWR('u', UBLK_IO_NEED_GET_DATA, struct ublksrv_io_cmd)
#define	UBLK_U_IO_REGISTER_IO_BUF	\
	_IOWR('u', 0x23, struct ublksrv_io_cmd)
#define	UBLK_U_IO_UNREGISTER_IO_BUF	\
	_IOWR('u', 0x24, struct ublksrv_io_cmd)

/* only ABORT means that no re-fetch */
#define UBLK_IO_RES_OK			0
#define UBLK_IO_RES_NEED_GET_DATA	1
#define UBLK_IO_RES_ABORT		(-ENODEV)

#define UBLKSRV_CMD_BUF_OFFSET	0
#define UBLKSRV_IO_BUF_OFFSET	0x80000000

/* tag bit is 16bit, so far limit at most 4096 IOs for each queue */
#define UBLK_MAX_QUEUE_DEPTH	4096

/* single IO buffer max size is 32MB */
#define UBLK_IO_BUF_OFF		0
#define UBLK_IO_BUF_BITS	25
#define UBLK_IO_BUF_BITS_MASK	((1ULL << UBLK_IO_BUF_BITS) - 1)

/* so at most 64K IOs for each queue */
#define UBLK_TAG_OFF		UBLK_IO_BUF_BITS
#define UBLK_TAG_BITS		16
#define UBLK_TAG_BITS_MASK	((1ULL << UBLK_TAG_BITS) - 1)

/* max 4096 queues */
#define UBLK_QID_OFF		(UBLK_TAG_OFF + UBLK_TAG_BITS)
#define UBLK_QID_BITS		12
#define UBLK_QID_BITS_MASK	((1ULL << UBLK_QID_BITS) - 1)

#define UBLK_MAX_NR_QUEUES	(1U << UBLK_QID_BITS)

#define UBLKSRV_IO_BUF_TOTAL_BITS	(UBLK_QID_OFF + UBLK_QID_BITS)
#define UBLKSRV_IO_BUF_TOTAL_SIZE	(1ULL << UBLKSRV_IO_BUF_TOTAL_BITS)

/*
 * ublk server can register data buffers for incoming I/O requests with a sparse
 * io_uring buffer table. The request buffer can then be used as the data buffer
 * for io_uring operations via the fixed buffer index.
 * Note that the ublk server can never directly access the request data memory.
 *
 * To use this feature, the ublk server must first register a sparse buffer
 * table on an io_uring instance.
 * When an incoming ublk request is received, the ublk server submits a
 * UBLK_U_IO_REGISTER_IO_BUF command to that io_uring instance. The
 * ublksrv_io_cmd's q_id and tag specify the request whose buffer to register
 * and addr is the index in the io_uring's buffer table to install the buffer.
 * SQEs can now be submitted to the io_uring to read/write the request's buffer
 * by enabling fixed buffers (e.g. using IORING_OP_{READ,WRITE}_FIXED or
 * IORING_URING_CMD_FIXED) and passing the registered buffer index in buf_index.
 * Once the last io_uring operation using the request's buffer has completed,
 * the ublk server submits a UBLK_U_IO_UNREGISTER_IO_BUF command with q_id, tag,
 * and addr again specifying the request buffer to unregister.
 * The ublk request is completed when its buffer is unregistered from all
 * io_uring instances and the ublk server issues UBLK_U_IO_COMMIT_AND_FETCH_REQ.
 *
 * Not available for UBLK_F_UNPRIVILEGED_DEV, as a ublk server can leak
 * uninitialized kernel memory by not reading into the full request buffer.
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

/*
 * - Block devices are recoverable if ublk server exits and restarts
 * - Outstanding I/O when ublk server exits is met with errors
 * - I/O issued while there is no ublk server queues
 */
#define UBLK_F_USER_RECOVERY	(1UL << 3)

/*
 * - Block devices are recoverable if ublk server exits and restarts
 * - Outstanding I/O when ublk server exits is reissued
 * - I/O issued while there is no ublk server queues
 */
#define UBLK_F_USER_RECOVERY_REISSUE	(1UL << 4)

/*
 * Unprivileged user can create /dev/ublkcN and /dev/ublkbN.
 *
 * /dev/ublk-control needs to be available for unprivileged user, and it
 * can be done via udev rule to make all control commands available to
 * unprivileged user. Except for the command of UBLK_CMD_ADD_DEV, all
 * other commands are only allowed for the owner of the specified device.
 *
 * When userspace sends UBLK_CMD_ADD_DEV, the device pair's owner_uid and
 * owner_gid are stored to ublksrv_ctrl_dev_info by kernel, so far only
 * the current user's uid/gid is stored, that said owner of the created
 * device is always the current user.
 *
 * We still need udev rule to apply OWNER/GROUP with the stored owner_uid
 * and owner_gid.
 *
 * Then ublk server can be run as unprivileged user, and /dev/ublkbN can
 * be accessed and managed by its owner represented by owner_uid/owner_gid.
 */
#define UBLK_F_UNPRIVILEGED_DEV	(1UL << 5)

/* use ioctl encoding for uring command */
#define UBLK_F_CMD_IOCTL_ENCODE	(1UL << 6)

/*
 *  Copy between request and user buffer by pread()/pwrite()
 *
 *  Not available for UBLK_F_UNPRIVILEGED_DEV, otherwise userspace may
 *  deceive us by not filling request buffer, then kernel uninitialized
 *  data may be leaked.
 */
#define UBLK_F_USER_COPY	(1UL << 7)

/*
 * User space sets this flag when setting up the device to request zoned storage support. Kernel may
 * deny the request by returning an error.
 */
#define UBLK_F_ZONED (1ULL << 8)

/*
 * - Block devices are recoverable if ublk server exits and restarts
 * - Outstanding I/O when ublk server exits is met with errors
 * - I/O issued while there is no ublk server is met with errors
 */
#define UBLK_F_USER_RECOVERY_FAIL_IO (1ULL << 9)

/*
 * Resizing a block device is possible with UBLK_U_CMD_UPDATE_SIZE
 * New size is passed in cmd->data[0] and is in units of sectors
 */
#define UBLK_F_UPDATE_SIZE		 (1ULL << 10)

/*
 * request buffer is registered automatically to uring_cmd's io_uring
 * context before delivering this io command to ublk server, meantime
 * it is un-registered automatically when completing this io command.
 *
 * For using this feature:
 *
 * - ublk server has to create sparse buffer table on the same `io_ring_ctx`
 *   for issuing `UBLK_IO_FETCH_REQ` and `UBLK_IO_COMMIT_AND_FETCH_REQ`.
 *   If uring_cmd isn't issued on same `io_ring_ctx`, it is ublk server's
 *   responsibility to unregister the buffer by issuing `IO_UNREGISTER_IO_BUF`
 *   manually, otherwise this ublk request won't complete.
 *
 * - ublk server passes auto buf register data via uring_cmd's sqe->addr,
 *   `struct ublk_auto_buf_reg` is populated from sqe->addr, please see
 *   the definition of ublk_sqe_addr_to_auto_buf_reg()
 *
 * - pass buffer index from `ublk_auto_buf_reg.index`
 *
 * - all reserved fields in `ublk_auto_buf_reg` need to be zeroed
 *
 * - pass flags from `ublk_auto_buf_reg.flags` if needed
 *
 * This way avoids extra cost from two uring_cmd, but also simplifies backend
 * implementation, such as, the dependency on IO_REGISTER_IO_BUF and
 * IO_UNREGISTER_IO_BUF becomes not necessary.
 *
 * If wrong data or flags are provided, both IO_FETCH_REQ and
 * IO_COMMIT_AND_FETCH_REQ are failed, for the latter, the ublk IO request
 * won't be completed until new IO_COMMIT_AND_FETCH_REQ command is issued
 * successfully
 */
#define UBLK_F_AUTO_BUF_REG 	(1ULL << 11)

/*
 * Control command `UBLK_U_CMD_QUIESCE_DEV` is added for quiescing device,
 * which state can be transitioned to `UBLK_S_DEV_QUIESCED` or
 * `UBLK_S_DEV_FAIL_IO` finally, and it needs ublk server cooperation for
 * handling `UBLK_IO_RES_ABORT` correctly.
 *
 * Typical use case is for supporting to upgrade ublk server application,
 * meantime keep ublk block device persistent during the period.
 *
 * This feature is only available when UBLK_F_USER_RECOVERY is enabled.
 *
 * Note, this command returns -EBUSY in case that all IO commands are being
 * handled by ublk server and not completed in specified time period which
 * is passed from the control command parameter.
 */
#define UBLK_F_QUIESCE		(1ULL << 12)

/*
 * If this feature is set, ublk_drv supports each (qid,tag) pair having
 * its own independent daemon task that is responsible for handling it.
 * If it is not set, daemons are per-queue instead, so for two pairs
 * (qid1,tag1) and (qid2,tag2), if qid1 == qid2, then the same task must
 * be responsible for handling (qid1,tag1) and (qid2,tag2).
 */
#define UBLK_F_PER_IO_DAEMON (1ULL << 13)

/* device state */
#define UBLK_S_DEV_DEAD	0
#define UBLK_S_DEV_LIVE	1
#define UBLK_S_DEV_QUIESCED	2
#define UBLK_S_DEV_FAIL_IO 	3

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
	__u64	data[1];

	/*
	 * Used for UBLK_F_UNPRIVILEGED_DEV and UBLK_CMD_GET_DEV_INFO2
	 * only, include null char
	 */
	__u16	dev_path_len;
	__u16	pad;
	__u32	reserved;
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

	__u32	owner_uid;	/* store by kernel */
	__u32	owner_gid;	/* store by kernel */
	__u64	reserved1;
	__u64   reserved2;
};

#define		UBLK_IO_OP_READ		0
#define		UBLK_IO_OP_WRITE		1
#define		UBLK_IO_OP_FLUSH		2
#define		UBLK_IO_OP_DISCARD		3
#define		UBLK_IO_OP_WRITE_SAME		4
#define		UBLK_IO_OP_WRITE_ZEROES		5
#define		UBLK_IO_OP_ZONE_OPEN		10
#define		UBLK_IO_OP_ZONE_CLOSE		11
#define		UBLK_IO_OP_ZONE_FINISH		12
#define		UBLK_IO_OP_ZONE_APPEND		13
#define		UBLK_IO_OP_ZONE_RESET_ALL	14
#define		UBLK_IO_OP_ZONE_RESET		15
/*
 * Construct a zone report. The report request is carried in `struct
 * ublksrv_io_desc`. The `start_sector` field must be the first sector of a zone
 * and shall indicate the first zone of the report. The `nr_zones` shall
 * indicate how many zones should be reported at most. The report shall be
 * delivered as a `struct blk_zone` array. To report fewer zones than requested,
 * zero the last entry of the returned array.
 *
 * Related definitions(blk_zone, blk_zone_cond, blk_zone_type, ...) in
 * include/uapi/linux/blkzoned.h are part of ublk UAPI.
 */
#define		UBLK_IO_OP_REPORT_ZONES		18

#define		UBLK_IO_F_FAILFAST_DEV		(1U << 8)
#define		UBLK_IO_F_FAILFAST_TRANSPORT	(1U << 9)
#define		UBLK_IO_F_FAILFAST_DRIVER	(1U << 10)
#define		UBLK_IO_F_META			(1U << 11)
#define		UBLK_IO_F_FUA			(1U << 13)
#define		UBLK_IO_F_NOUNMAP		(1U << 15)
#define		UBLK_IO_F_SWAP			(1U << 16)
/*
 * For UBLK_F_AUTO_BUF_REG & UBLK_AUTO_BUF_REG_FALLBACK only.
 *
 * This flag is set if auto buffer register is failed & ublk server passes
 * UBLK_AUTO_BUF_REG_FALLBACK, and ublk server need to register buffer
 * manually for handling the delivered IO command if this flag is observed
 *
 * ublk server has to check this flag if UBLK_AUTO_BUF_REG_FALLBACK is
 * passed in.
 */
#define		UBLK_IO_F_NEED_REG_BUF		(1U << 17)

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

	union {
		__u32		nr_sectors;
		__u32		nr_zones; /* for UBLK_IO_OP_REPORT_ZONES */
	};

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

/*
 * If this flag is set, fallback by completing the uring_cmd and setting
 * `UBLK_IO_F_NEED_REG_BUF` in case of auto-buf-register failure;
 * otherwise the client ublk request is failed silently
 *
 * If ublk server passes this flag, it has to check if UBLK_IO_F_NEED_REG_BUF
 * is set in `ublksrv_io_desc.op_flags`. If UBLK_IO_F_NEED_REG_BUF is set,
 * ublk server needs to register io buffer manually for handling IO command.
 */
#define UBLK_AUTO_BUF_REG_FALLBACK 	(1 << 0)
#define UBLK_AUTO_BUF_REG_F_MASK 	UBLK_AUTO_BUF_REG_FALLBACK

struct ublk_auto_buf_reg {
	/* index for registering the delivered request buffer */
	__u16  index;
	__u8   flags;
	__u8   reserved0;

	/*
	 * io_ring FD can be passed via the reserve field in future for
	 * supporting to register io buffer to external io_uring
	 */
	__u32  reserved1;
};

/*
 * For UBLK_F_AUTO_BUF_REG, auto buffer register data is carried via
 * uring_cmd's sqe->addr:
 *
 * 	- bit0 ~ bit15: buffer index
 * 	- bit16 ~ bit23: flags
 * 	- bit24 ~ bit31: reserved0
 * 	- bit32 ~ bit63: reserved1
 */
static inline struct ublk_auto_buf_reg ublk_sqe_addr_to_auto_buf_reg(
		__u64 sqe_addr)
{
	struct ublk_auto_buf_reg reg = {
		.index = (__u16)sqe_addr,
		.flags = (__u8)(sqe_addr >> 16),
		.reserved0 = (__u8)(sqe_addr >> 24),
		.reserved1 = (__u32)(sqe_addr >> 32),
	};

	return reg;
}

static inline __u64
ublk_auto_buf_reg_to_sqe_addr(const struct ublk_auto_buf_reg *buf)
{
	__u64 addr = buf->index | (__u64)buf->flags << 16 | (__u64)buf->reserved0 << 24 |
		(__u64)buf->reserved1 << 32;

	return addr;
}

/* issued to ublk driver via /dev/ublkcN */
struct ublksrv_io_cmd {
	__u16	q_id;

	/* for fetch/commit which result */
	__u16	tag;

	/* io result, it is valid for COMMIT* command only */
	__s32	result;

	union {
		/*
		 * userspace buffer address in ublksrv daemon process, valid for
		 * FETCH* command only
		 *
		 * `addr` should not be used when UBLK_F_USER_COPY is enabled,
		 * because userspace handles data copy by pread()/pwrite() over
		 * /dev/ublkcN. But in case of UBLK_F_ZONED, this union is
		 * re-used to pass back the allocated LBA for
		 * UBLK_IO_OP_ZONE_APPEND which actually depends on
		 * UBLK_F_USER_COPY
		 */
		__u64	addr;
		__u64	zone_append_lba;
	};
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

/*
 * read-only, can't set via UBLK_CMD_SET_PARAMS, disk_devt is available
 * after device is started
 */
struct ublk_param_devt {
	__u32   char_major;
	__u32   char_minor;
	__u32   disk_major;
	__u32   disk_minor;
};

struct ublk_param_zoned {
	__u32	max_open_zones;
	__u32	max_active_zones;
	__u32	max_zone_append_sectors;
	__u8	reserved[20];
};

struct ublk_param_dma_align {
	__u32	alignment;
	__u8	pad[4];
};

#define UBLK_MIN_SEGMENT_SIZE   4096
/*
 * If any one of the three segment parameter is set as 0, the behavior is
 * undefined.
 */
struct ublk_param_segment {
	/*
	 * seg_boundary_mask + 1 needs to be power_of_2(), and the sum has
	 * to be >= UBLK_MIN_SEGMENT_SIZE(4096)
	 */
	__u64 	seg_boundary_mask;

	/*
	 * max_segment_size could be override by virt_boundary_mask, so be
	 * careful when setting both.
	 *
	 * max_segment_size has to be >= UBLK_MIN_SEGMENT_SIZE(4096)
	 */
	__u32 	max_segment_size;
	__u16 	max_segments;
	__u8	pad[2];
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
#define UBLK_PARAM_TYPE_DEVT            (1 << 2)
#define UBLK_PARAM_TYPE_ZONED           (1 << 3)
#define UBLK_PARAM_TYPE_DMA_ALIGN       (1 << 4)
#define UBLK_PARAM_TYPE_SEGMENT         (1 << 5)
	__u32	types;			/* types of parameter included */

	struct ublk_param_basic		basic;
	struct ublk_param_discard	discard;
	struct ublk_param_devt		devt;
	struct ublk_param_zoned	zoned;
	struct ublk_param_dma_align	dma;
	struct ublk_param_segment	seg;
};

#endif
