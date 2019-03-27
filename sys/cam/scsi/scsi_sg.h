/*
 * Structures and definitions for SCSI commands to the SG passthrough device.
 *
 * $FreeBSD$
 */

#ifndef _SCSI_SG_H
#define _SCSI_SG_H

#define SGIOC	'"'
#define SG_SET_TIMEOUT		_IOW(SGIOC, 0x01, u_int)
#define SG_GET_TIMEOUT		_IO(SGIOC, 0x02)
#define SG_EMULATED_HOST	_IOR(SGIOC, 0x03, int)
#define SG_SET_TRANSFORM	_IO(SGIOC, 0x04)
#define SG_GET_TRANSFORM	_IO(SGIOC, 0x05)
#define SG_GET_COMMAND_Q	_IOW(SGIOC, 0x70, int)
#define SG_SET_COMMAND_Q	_IOR(SGIOC, 0x71, int)
#define SG_GET_RESERVED_SIZE	_IOR(SGIOC, 0x72, int)
#define SG_SET_RESERVED_SIZE	_IOW(SGIOC, 0x75, int)
#define SG_GET_SCSI_ID		_IOR(SGIOC, 0x76, struct sg_scsi_id)
#define SG_SET_FORCE_LOW_DMA	_IOW(SGIOC, 0x79, int)
#define SG_GET_LOW_DMA		_IOR(SGIOC, 0x7a, int)
#define SG_SET_FORCE_PACK_ID	_IOW(SGIOC, 0x7b, int)
#define SG_GET_PACK_ID		_IOR(SGIOC, 0x7c, int)
#define SG_GET_NUM_WAITING	_IOR(SGIOC, 0x7d, int)
#define SG_SET_DEBUG		_IOW(SGIOC, 0x7e, int)
#define SG_GET_SG_TABLESIZE	_IOR(SGIOC, 0x7f, int)
#define SG_GET_VERSION_NUM	_IOR(SGIOC, 0x82, int)
#define SG_NEXT_CMD_LEN		_IOW(SGIOC, 0x83, int)
#define SG_SCSI_RESET		_IOW(SGIOC, 0x84, int)
#define SG_IO			_IOWR(SGIOC, 0x85, struct sg_io_hdr)
#define SG_GET_REQUEST_TABLE	_IO(SGIOC, 0x86)
#define SG_SET_KEEP_ORPHAN	_IOW(SGIOC, 0x87, int)
#define SG_GET_KEEP_ORPHAN	_IOR(SGIOC, 0x88, int)
#define SG_GET_ACCESS_COUNT	_IOR(SGIOC, 0x89, int)

struct sg_io_hdr {
	int		interface_id;
	int		dxfer_direction;
	u_char		cmd_len;
	u_char		mx_sb_len;
	u_short		iovec_count;
	u_int		dxfer_len;
	void		*dxferp;
	u_char		*cmdp;
	u_char		*sbp;
	u_int		timeout;
	u_int		flags;
	int		pack_id;
	void		*usr_ptr;
	u_char		status;
	u_char		masked_status;
	u_char		msg_status;
	u_char		sb_len_wr;
	u_short		host_status;
	u_short		driver_status;
	int		resid;
	u_int		duration;
	u_int		info;
};

struct sg_io_hdr32 {
	int		interface_id;
	int		dxfer_direction;
	u_char		cmd_len;
	u_char		mx_sb_len;
	u_short		iovec_count;
	u_int		dxfer_len;
	uint32_t	dxferp;
	uint32_t	cmdp;
	uint32_t	sbp;
	u_int		timeout;
	u_int		flags;
	int		pack_id;
	uint32_t	usr_ptr;
	u_char		status;
	u_char		masked_status;
	u_char		msg_status;
	u_char		sb_len_wr;
	u_short		host_status;
	u_short		driver_status;
	int		resid;
	u_int		duration;
	u_int		info;
};

#define SG_DXFER_NONE		-1
#define SG_DXFER_TO_DEV		-2
#define SG_DXFER_FROM_DEV	-3
#define SG_DXFER_TO_FROM_DEV	-4
#define SG_DXFER_UNKNOWN	-5

#define SG_MAX_SENSE 16
struct sg_header {
	int		pack_len;
	int		reply_len;
	int		pack_id;
	int		result;
	u_int		twelve_byte:1;
	u_int		target_status:5;
	u_int		host_status:8;
	u_int		driver_status:8;
	u_int		other_flags:10;
	u_char		sense_buffer[SG_MAX_SENSE];
};

struct sg_scsi_id {
	int		host_no;
	int		channel;
	int		scsi_id;
	int		lun;
	int		scsi_type;
	short		h_cmd_per_lun;
	short		d_queue_depth;
	int		unused[2];
};

struct scsi_idlun {
	uint32_t	dev_id;
	uint32_t	host_unique_id;
};

/*
 * Host codes
 */
#define DID_OK		0x00	/* OK */
#define DID_NO_CONNECT	0x01	/* timeout during connect */
#define DID_BUS_BUSY	0x02	/* timeout during command */
#define DID_TIME_OUT	0x03	/* other timeout */
#define DID_BAD_TARGET	0x04	/* bad target */
#define DID_ABORT	0x05	/* abort */
#define DID_PARITY	0x06	/* parity error */
#define DID_ERROR	0x07	/* internal error */
#define DID_RESET	0x08	/* reset by somebody */
#define DID_BAD_INTR	0x09	/* unexpected interrupt */
#define DID_PASSTHROUGH	0x0a	/* passthrough */
#define DID_SOFT_ERROR	0x0b	/* low driver wants retry */
#define DID_IMM_RETRY	0x0c	/* retry without decreasing retrycnt */

/*
 * Driver codes
 */
#define DRIVER_OK	0x00
#define DRIVER_BUSY	0x01
#define DRIVER_SOFT	0x02
#define DRIVER_MEDIA	0x03
#define DRIVER_ERROR	0x04

#define DRIVER_INVALID	0x05
#define DRIVER_TIMEOUT	0x06
#define DRIVER_HARD	0x07
#define DRIVER_SENSE	0x08

#define SUGGEST_RETRY	0x10
#define SUGGEST_ABORT	0x20
#define SUGGEST_REMAP	0x30
#define SUGGEST_DIE	0x40
#define SUGGEST_SENSE	0x80
#define SUGGEST_IS_OK	0xff

#define DRIVER_MASK	0x0f
#define SUGGEST_MASK	0xf0

/* Other definitions */
/* HZ isn't always available, so simulate it */
#define SG_DEFAULT_HZ		1000
#define SG_DEFAULT_TIMEOUT	(60*SG_DEFAULT_HZ)

#endif /* !_SCSI_SG_H */
