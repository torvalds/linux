#ifndef BSG_H
#define BSG_H

#define BSG_PROTOCOL_SCSI		0

#define BSG_SUB_PROTOCOL_SCSI_CMD	0
#define BSG_SUB_PROTOCOL_SCSI_TMF	1
#define BSG_SUB_PROTOCOL_SCSI_TRANSPORT	2

struct sg_io_v4 {
	__s32 guard;		/* [i] 'Q' to differentiate from v3 */
	__u32 protocol;		/* [i] 0 -> SCSI , .... */
	__u32 subprotocol;	/* [i] 0 -> SCSI command, 1 -> SCSI task
				   management function, .... */

	__u32 request_len;	/* [i] in bytes */
	__u64 request;		/* [i], [*i] {SCSI: cdb} */
	__u32 request_attr;	/* [i] {SCSI: task attribute} */
	__u32 request_tag;	/* [i] {SCSI: task tag (only if flagged)} */
	__u32 request_priority;	/* [i] {SCSI: task priority} */
	__u32 max_response_len;	/* [i] in bytes */
	__u64 response;		/* [i], [*o] {SCSI: (auto)sense data} */

	/* "din_" for data in (from device); "dout_" for data out (to device) */
	__u32 dout_xfer_len;	/* [i] bytes to be transferred to device */
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
	__s32 din_resid;	/* [o] actual_din_xfer_len - din_xfer_len */
	__u32 generated_tag;	/* [o] {SCSI: task tag that transport chose} */
	__u32 spare_out;	/* [o] */

	__u32 padding;
};

#ifdef __KERNEL__

#if defined(CONFIG_BLK_DEV_BSG)
struct bsg_class_device {
	struct class_device *class_dev;
	struct device *dev;
	int minor;
	struct request_queue *queue;
};

extern int bsg_register_queue(struct request_queue *, struct device *, const char *);
extern void bsg_unregister_queue(struct request_queue *);
#else
static inline int bsg_register_queue(struct request_queue * rq, struct device *dev, const char *name)
{
	return 0;
}
static inline void bsg_unregister_queue(struct request_queue *rq)
{
}
#endif

#endif /* __KERNEL__ */

#endif
