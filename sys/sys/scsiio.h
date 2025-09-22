/*	$OpenBSD: scsiio.h,v 1.10 2012/09/05 17:17:47 deraadt Exp $	*/
/*	$NetBSD: scsiio.h,v 1.3 1994/06/29 06:45:09 cgd Exp $	*/

#ifndef _SYS_SCSIIO_H_
#define _SYS_SCSIIO_H_


#include <sys/types.h>
#include <sys/ioctl.h>

#define	SENSEBUFLEN 48
#define	CMDBUFLEN   16

typedef struct	scsireq {
	u_long	flags;		/* info about the request status and type */
	u_long	timeout;
	u_char	cmd[CMDBUFLEN];
	u_char	cmdlen;
	caddr_t	databuf;	/* address in user space of buffer */
	u_long	datalen;	/* size of user buffer (request) */
	u_long	datalen_used;	/* size of user buffer (used)*/
	u_char	sense[SENSEBUFLEN]; /* returned sense will be in here */
	u_char	senselen;	/* sensedata request size (MAX of SENSEBUFLEN)*/
	u_char	senselen_used;	/* return value only */
	u_char	status;		/* what the scsi status was from the adapter */
	u_char	retsts;		/* the return status for the command */
	int	error;		/* error bits */
} scsireq_t;

/* bit definitions for flags */
#define SCCMD_READ		0x00000001
#define SCCMD_WRITE		0x00000002
#define SCCMD_IOV		0x00000004
#define SCCMD_ESCAPE		0x00000010
#define SCCMD_TARGET		0x00000020


/* definitions for the return status (retsts) */
#define SCCMD_OK	0x00
#define SCCMD_TIMEOUT	0x01
#define SCCMD_BUSY	0x02
#define SCCMD_SENSE	0x03
#define SCCMD_UNKNOWN	0x04

#define SCIOCCOMMAND	_IOWR('Q', 1, scsireq_t)

#define SC_DB_CMDS	0x00000001	/* show all scsi cmds and errors */
#define SC_DB_FLOW	0x00000002	/* show routines entered	*/
#define SC_DB_FLOW2	0x00000004	/* show path INSIDE routines	*/
#define SC_DB_DMA	0x00000008	/* show DMA segments etc	*/
#define SCIOCDEBUG	_IOW('Q', 2, int)	/* from 0 to 15 */

struct scsi_addr {
	int     type;
#define TYPE_SCSI	0
#define TYPE_ATAPI	1
	int	scbus;		/* -1 if wildcard */
	int	target;		/* -1 if wildcard */
	int	lun;		/* -1 if wildcard */
};

#define SCIOCRESET	_IO('Q', 7)	/* reset the device */
#define SCIOCIDENTIFY	_IOR('Q', 9, struct scsi_addr) 

struct sbioc_device {
	void		*sd_cookie;
	int		sd_target;
	int		sd_lun;
};

#define SBIOCPROBE	_IOWR('Q', 127, struct sbioc_device)
#define SBIOCDETACH	_IOWR('Q', 128, struct sbioc_device)

#endif /* _SYS_SCSIIO_H_ */
