/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_IOCTL_H
#define _SCSI_IOCTL_H 

#define SCSI_IOCTL_SEND_COMMAND 1
#define SCSI_IOCTL_TEST_UNIT_READY 2
#define SCSI_IOCTL_BENCHMARK_COMMAND 3
#define SCSI_IOCTL_SYNC 4			/* Request synchronous parameters */
#define SCSI_IOCTL_START_UNIT 5
#define SCSI_IOCTL_STOP_UNIT 6
/* The door lock/unlock constants are compatible with Sun constants for
   the cdrom */
#define SCSI_IOCTL_DOORLOCK 0x5380		/* lock the eject mechanism */
#define SCSI_IOCTL_DOORUNLOCK 0x5381		/* unlock the mechanism	  */

#define	SCSI_REMOVAL_PREVENT	1
#define	SCSI_REMOVAL_ALLOW	0

#ifdef __KERNEL__

struct gendisk;
struct scsi_device;
struct sg_io_hdr;

/*
 * Structures used for scsi_ioctl et al.
 */

typedef struct scsi_ioctl_command {
	unsigned int inlen;
	unsigned int outlen;
	unsigned char data[];
} Scsi_Ioctl_Command;

typedef struct scsi_idlun {
	__u32 dev_id;
	__u32 host_unique_id;
} Scsi_Idlun;

/* Fibre Channel WWN, port_id struct */
typedef struct scsi_fctargaddress {
	__u32 host_port_id;
	unsigned char host_wwn[8]; // include NULL term.
} Scsi_FCTargAddress;

int scsi_ioctl_block_when_processing_errors(struct scsi_device *sdev,
		int cmd, bool ndelay);
int scsi_ioctl(struct scsi_device *sdev, bool open_for_write, int cmd,
		void __user *arg);
int get_sg_io_hdr(struct sg_io_hdr *hdr, const void __user *argp);
int put_sg_io_hdr(const struct sg_io_hdr *hdr, void __user *argp);
bool scsi_cmd_allowed(unsigned char *cmd, bool open_for_write);

#endif /* __KERNEL__ */
#endif /* _SCSI_IOCTL_H */
