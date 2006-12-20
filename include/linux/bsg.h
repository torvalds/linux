#ifndef BSG_H
#define BSG_H

struct sg_io_v4 {
	s32 guard;		/* [i] 'Q' to differentiate from v3 */
	u32 protocol;		/* [i] 0 -> SCSI , .... */
	u32 subprotocol;	/* [i] 0 -> SCSI command, 1 -> SCSI task
				   management function, .... */

	u32 request_len;	/* [i] in bytes */
	u64 request;		/* [i], [*i] {SCSI: cdb} */
	u32 request_attr;	/* [i] {SCSI: task attribute} */
	u32 request_tag;	/* [i] {SCSI: task tag (only if flagged)} */
	u32 request_priority;	/* [i] {SCSI: task priority} */
	u32 max_response_len;	/* [i] in bytes */
	u64 response;		/* [i], [*o] {SCSI: (auto)sense data} */

	/* "din_" for data in (from device); "dout_" for data out (to device) */
	u32 dout_xfer_len;	/* [i] bytes to be transferred to device */
	u32 din_xfer_len;	/* [i] bytes to be transferred from device */
	u64 dout_xferp;		/* [i], [*i] */
	u64 din_xferp;		/* [i], [*o] */

	u32 timeout;		/* [i] units: millisecond */
	u32 flags;		/* [i] bit mask */
	u64 usr_ptr;		/* [i->o] unused internally */
	u32 spare_in;		/* [i] */

	u32 driver_status;	/* [o] 0 -> ok */
	u32 transport_status;	/* [o] 0 -> ok */
	u32 device_status;	/* [o] {SCSI: command completion status} */
	u32 retry_delay;	/* [o] {SCSI: status auxiliary information} */
	u32 info;		/* [o] additional information */
	u32 duration;		/* [o] time to complete, in milliseconds */
	u32 response_len;	/* [o] bytes of response actually written */
	s32 din_resid;		/* [o] actual_din_xfer_len - din_xfer_len */
	u32 generated_tag;	/* [o] {SCSI: task tag that transport chose} */
	u32 spare_out;		/* [o] */

	u32 padding;
};

#ifdef __KERNEL__

#if defined(CONFIG_BLK_DEV_BSG)
struct bsg_class_device {
	struct class_device *class_dev;
	struct device *dev;
	int minor;
	struct gendisk *disk;
	struct list_head list;
};

extern int bsg_register_disk(struct gendisk *);
extern void bsg_unregister_disk(struct gendisk *);
#else
struct bsg_class_device { };
#define bsg_register_disk(disk)		(0)
#define bsg_unregister_disk(disk)	do { } while (0)
#endif

#endif /* __KERNEL__ */

#endif
