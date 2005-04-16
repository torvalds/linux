#ifndef S390_CMB_H
#define S390_CMB_H
/**
 * struct cmbdata -- channel measurement block data for user space
 *
 * @size:	size of the stored data
 * @ssch_rsch_count: XXX
 * @sample_count:
 * @device_connect_time:
 * @function_pending_time:
 * @device_disconnect_time:
 * @control_unit_queuing_time:
 * @device_active_only_time:
 * @device_busy_time:
 * @initial_command_response_time:
 *
 * all values are stored as 64 bit for simplicity, especially
 * in 32 bit emulation mode. All time values are normalized to
 * nanoseconds.
 * Currently, two formats are known, which differ by the size of
 * this structure, i.e. the last two members are only set when
 * the extended channel measurement facility (first shipped in
 * z990 machines) is activated.
 * Potentially, more fields could be added, which results in a
 * new ioctl number.
 **/
struct cmbdata {
	__u64 size;
	__u64 elapsed_time;
 /* basic and exended format: */
	__u64 ssch_rsch_count;
	__u64 sample_count;
	__u64 device_connect_time;
	__u64 function_pending_time;
	__u64 device_disconnect_time;
	__u64 control_unit_queuing_time;
	__u64 device_active_only_time;
 /* extended format only: */
	__u64 device_busy_time;
	__u64 initial_command_response_time;
};

/* enable channel measurement */
#define BIODASDCMFENABLE	_IO(DASD_IOCTL_LETTER,32)
/* enable channel measurement */
#define BIODASDCMFDISABLE	_IO(DASD_IOCTL_LETTER,33)
/* reset channel measurement block */
#define BIODASDRESETCMB		_IO(DASD_IOCTL_LETTER,34)
/* read channel measurement data */
#define BIODASDREADCMB		_IOWR(DASD_IOCTL_LETTER,32,u64)
/* read channel measurement data */
#define BIODASDREADALLCMB	_IOWR(DASD_IOCTL_LETTER,33,struct cmbdata)

#ifdef __KERNEL__

/**
 * enable_cmf() - switch on the channel measurement for a specific device
 *  @cdev:	The ccw device to be enabled
 *  returns 0 for success or a negative error value.
 *
 *  Context:
 *    non-atomic
 **/
extern int enable_cmf(struct ccw_device *cdev);

/**
 * disable_cmf() - switch off the channel measurement for a specific device
 *  @cdev:	The ccw device to be disabled
 *  returns 0 for success or a negative error value.
 *
 *  Context:
 *    non-atomic
 **/
extern int disable_cmf(struct ccw_device *cdev);

/**
 * cmf_read() - read one value from the current channel measurement block
 * @cmf:	the channel to be read
 * @index:	the name of the value that is read
 *
 *  Context:
 *    any
 **/

extern u64 cmf_read(struct ccw_device *cdev, int index);
/**
 * cmf_readall() - read one value from the current channel measurement block
 * @cmf:	the channel to be read
 * @data:	a pointer to a data block that will be filled
 *
 *  Context:
 *    any
 **/
extern int cmf_readall(struct ccw_device *cdev, struct cmbdata*data);
extern void cmf_reset(struct ccw_device *cdev);

#endif /* __KERNEL__ */
#endif /* S390_CMB_H */
