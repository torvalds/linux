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

#define UBLK_F_USER_RECOVERY	(1UL << 3)

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

/* device state */
#define UBLK_S_DEV_DEAD	0
#define UBLK_S_DEV_LIVE	1
#define UBLK_S_DEV_QUIESCED	2

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
	__u32	types;			/* types of parameter included */

	struct ublk_param_basic		basic;
	struct ublk_param_discard	discard;
	struct ublk_param_devt		devt;
	struct ublk_param_zoned	zoned;
};

#endif
