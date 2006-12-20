#ifndef BSG_H
#define BSG_H

struct sg_io_v4 {
	int32_t guard;		/* [i] 'Q' to differentiate from v3 */
	uint32_t protocol;	/* [i] 0 -> SCSI , .... */
	uint32_t subprotocol;	/* [i] 0 -> SCSI command, 1 -> SCSI task
				   management function, .... */

	uint32_t request_len;	/* [i] in bytes */
	uint64_t request;	/* [i], [*i] {SCSI: cdb} */
	uint32_t request_attr;	/* [i] {SCSI: task attribute} */
	uint32_t request_tag;	/* [i] {SCSI: task tag (only if flagged)} */
	uint32_t request_priority;	/* [i] {SCSI: task priority} */
	uint32_t max_response_len;	/* [i] in bytes */
	uint64_t response;	/* [i], [*o] {SCSI: (auto)sense data} */

	/* "din_" for data in (from device); "dout_" for data out (to device) */
	uint32_t dout_xfer_len;	/* [i] bytes to be transferred to device */
	uint32_t din_xfer_len;	/* [i] bytes to be transferred from device */
	uint64_t dout_xferp;	/* [i], [*i] */
	uint64_t din_xferp;	/* [i], [*o] */

	uint32_t timeout;	/* [i] units: millisecond */
	uint32_t flags;		/* [i] bit mask */
	uint64_t usr_ptr;	/* [i->o] unused internally */
	uint32_t spare_in;	/* [i] */

	uint32_t driver_status;	/* [o] 0 -> ok */
	uint32_t transport_status;	/* [o] 0 -> ok */
	uint32_t device_status;	/* [o] {SCSI: command completion status} */
	uint32_t retry_delay;	/* [o] {SCSI: status auxiliary information} */
	uint32_t info;		/* [o] additional information */
	uint32_t duration;	/* [o] time to complete, in milliseconds */
	uint32_t response_len;	/* [o] bytes of response actually written */
	int32_t din_resid;	/* [o] actual_din_xfer_len - din_xfer_len */
	uint32_t generated_tag;	/* [o] {SCSI: task tag that transport chose} */
	uint32_t spare_out;	/* [o] */

	uint32_t padding;
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
