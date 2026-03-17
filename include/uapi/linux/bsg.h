/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPIBSG_H
#define _UAPIBSG_H

#ifdef __KERNEL__
#include <linux/build_bug.h>
#endif /* __KERNEL__ */
#include <linux/types.h>

#define BSG_PROTOCOL_SCSI		0

#define BSG_SUB_PROTOCOL_SCSI_CMD	0
#define BSG_SUB_PROTOCOL_SCSI_TMF	1
#define BSG_SUB_PROTOCOL_SCSI_TRANSPORT	2

/*
 * For flag constants below:
 * sg.h sg_io_hdr also has bits defined for it's flags member. These
 * two flag values (0x10 and 0x20) have the same meaning in sg.h . For
 * bsg the BSG_FLAG_Q_AT_HEAD flag is ignored since it is the deafult.
 */
#define BSG_FLAG_Q_AT_TAIL 0x10 /* default is Q_AT_HEAD */
#define BSG_FLAG_Q_AT_HEAD 0x20

struct sg_io_v4 {
	__s32 guard;		/* [i] 'Q' to differentiate from v3 */
	__u32 protocol;		/* [i] 0 -> SCSI , .... */
	__u32 subprotocol;	/* [i] 0 -> SCSI command, 1 -> SCSI task
				   management function, .... */

	__u32 request_len;	/* [i] in bytes */
	__u64 request;		/* [i], [*i] {SCSI: cdb} */
	__u64 request_tag;	/* [i] {SCSI: task tag (only if flagged)} */
	__u32 request_attr;	/* [i] {SCSI: task attribute} */
	__u32 request_priority;	/* [i] {SCSI: task priority} */
	__u32 request_extra;	/* [i] {spare, for padding} */
	__u32 max_response_len;	/* [i] in bytes */
	__u64 response;		/* [i], [*o] {SCSI: (auto)sense data} */

        /* "dout_": data out (to device); "din_": data in (from device) */
	__u32 dout_iovec_count;	/* [i] 0 -> "flat" dout transfer else
				   dout_xfer points to array of iovec */
	__u32 dout_xfer_len;	/* [i] bytes to be transferred to device */
	__u32 din_iovec_count;	/* [i] 0 -> "flat" din transfer */
	__u32 din_xfer_len;	/* [i] bytes to be transferred from device */
	__u64 dout_xferp;	/* [i], [*i] */
	__u64 din_xferp;	/* [i], [*o] */

	__u32 timeout;		/* [i] units: millisecond */
	__u32 flags;		/* [i] bit mask */
	__u64 usr_ptr;		/* [i->o] unused internally */
	__u32 spare_in;		/* [i] */

	__u32 driver_status;	/* [o] 0 -> ok */
	__u32 transport_status;	/* [o] 0 -> ok */
	__u32 device_status;	/* [o] {SCSI: command completion status} */
	__u32 retry_delay;	/* [o] {SCSI: status auxiliary information} */
	__u32 info;		/* [o] additional information */
	__u32 duration;		/* [o] time to complete, in milliseconds */
	__u32 response_len;	/* [o] bytes of response actually written */
	__s32 din_resid;	/* [o] din_xfer_len - actual_din_xfer_len */
	__s32 dout_resid;	/* [o] dout_xfer_len - actual_dout_xfer_len */
	__u64 generated_tag;	/* [o] {SCSI: transport generated task tag} */
	__u32 spare_out;	/* [o] */

	__u32 padding;
};

struct bsg_uring_cmd {
	__u64 request;		/* [i], [*i] command descriptor address */
	__u32 request_len;	/* [i] command descriptor length in bytes */
	__u32 protocol;		/* [i] protocol type (BSG_PROTOCOL_*) */
	__u32 subprotocol;	/* [i] subprotocol type (BSG_SUB_PROTOCOL_*) */
	__u32 max_response_len;	/* [i] response buffer size in bytes */

	__u64 response;		/* [i], [*o] response data address */
	__u64 dout_xferp;	/* [i], [*i] */
	__u32 dout_xfer_len;	/* [i] bytes to be transferred to device */
	__u32 dout_iovec_count;	/* [i] 0 -> "flat" dout transfer else
				 * dout_xferp points to array of iovec
				 */
	__u64 din_xferp;	/* [i], [*o] */
	__u32 din_xfer_len;	/* [i] bytes to be transferred from device */
	__u32 din_iovec_count;	/* [i] 0 -> "flat" din transfer */

	__u32 timeout_ms;	/* [i] timeout in milliseconds */
	__u8  reserved[12];	/* reserved for future extension */
};

#ifdef __KERNEL__
/* Must match IORING_OP_URING_CMD payload size (e.g. SQE128). */
static_assert(sizeof(struct bsg_uring_cmd) == 80);
#endif /* __KERNEL__ */


/*
 * SCSI BSG io_uring completion (res2, 64-bit)
 *
 * When using BSG_PROTOCOL_SCSI + BSG_SUB_PROTOCOL_SCSI_CMD with
 * IORING_OP_URING_CMD, the completion queue entry (CQE) contains:
 *   - result: errno (0 on success)
 *   - res2: packed SCSI status
 *
 * res2 bit layout:
 *   [0..7]   device_status  (SCSI status byte, e.g. CHECK_CONDITION)
 *   [8..15]  driver_status  (e.g. DRIVER_SENSE when sense data is valid)
 *   [16..23] host_status    (e.g. DID_OK, DID_TIME_OUT)
 *   [24..31] sense_len_wr   (bytes of sense data written to response buffer)
 *   [32..63] resid_len      (residual transfer length)
 */
static inline __u8 bsg_scsi_res2_device_status(__u64 res2)
{
	return res2 & 0xff;
}
static inline __u8 bsg_scsi_res2_driver_status(__u64 res2)
{
	return res2 >> 8;
}
static inline __u8 bsg_scsi_res2_host_status(__u64 res2)
{
	return res2 >> 16;
}
static inline __u8 bsg_scsi_res2_sense_len(__u64 res2)
{
	return res2 >> 24;
}
static inline __u32 bsg_scsi_res2_resid_len(__u64 res2)
{
	return res2 >> 32;
}
static inline __u64 bsg_scsi_res2_build(__u8 device_status, __u8 driver_status,
					__u8 host_status, __u8 sense_len_wr,
					__u32 resid_len)
{
	return ((__u64)(__u32)(resid_len) << 32) |
		((__u64)sense_len_wr << 24) |
		((__u64)host_status << 16) |
		((__u64)driver_status << 8) |
		(__u64)device_status;
}

#endif /* _UAPIBSG_H */
