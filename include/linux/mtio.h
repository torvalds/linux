/* 
 * linux/mtio.h header file for Linux. Written by H. Bergman
 *
 * Modified for special ioctls provided by zftape in September 1997
 * by C.-J. Heine.
 */

#ifndef _LINUX_MTIO_H
#define _LINUX_MTIO_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Structures and definitions for mag tape io control commands
 */

/* structure for MTIOCTOP - mag tape op command */
struct	mtop {
	short	mt_op;		/* operations defined below */
	int	mt_count;	/* how many of them */
};

/* Magnetic Tape operations [Not all operations supported by all drivers]: */
#define MTRESET 0	/* +reset drive in case of problems */
#define MTFSF	1	/* forward space over FileMark,
			 * position at first record of next file 
			 */
#define MTBSF	2	/* backward space FileMark (position before FM) */
#define MTFSR	3	/* forward space record */
#define MTBSR	4	/* backward space record */
#define MTWEOF	5	/* write an end-of-file record (mark) */
#define MTREW	6	/* rewind */
#define MTOFFL	7	/* rewind and put the drive offline (eject?) */
#define MTNOP	8	/* no op, set status only (read with MTIOCGET) */
#define MTRETEN 9	/* retension tape */
#define MTBSFM	10	/* +backward space FileMark, position at FM */
#define MTFSFM  11	/* +forward space FileMark, position at FM */
#define MTEOM	12	/* goto end of recorded media (for appending files).
			 * MTEOM positions after the last FM, ready for
			 * appending another file.
			 */
#define MTERASE 13	/* erase tape -- be careful! */

#define MTRAS1  14	/* run self test 1 (nondestructive) */
#define MTRAS2	15	/* run self test 2 (destructive) */
#define MTRAS3  16	/* reserved for self test 3 */

#define MTSETBLK 20	/* set block length (SCSI) */
#define MTSETDENSITY 21	/* set tape density (SCSI) */
#define MTSEEK	22	/* seek to block (Tandberg, etc.) */
#define MTTELL	23	/* tell block (Tandberg, etc.) */
#define MTSETDRVBUFFER 24 /* set the drive buffering according to SCSI-2 */
			/* ordinary buffered operation with code 1 */
#define MTFSS	25	/* space forward over setmarks */
#define MTBSS	26	/* space backward over setmarks */
#define MTWSM	27	/* write setmarks */

#define MTLOCK  28	/* lock the drive door */
#define MTUNLOCK 29	/* unlock the drive door */
#define MTLOAD  30	/* execute the SCSI load command */
#define MTUNLOAD 31	/* execute the SCSI unload command */
#define MTCOMPRESSION 32/* control compression with SCSI mode page 15 */
#define MTSETPART 33	/* Change the active tape partition */
#define MTMKPART  34	/* Format the tape with one or two partitions */
#define MTWEOFI	35	/* write an end-of-file record (mark) in immediate mode */

/* structure for MTIOCGET - mag tape get status command */

struct	mtget {
	long	mt_type;	/* type of magtape device */
	long	mt_resid;	/* residual count: (not sure)
				 *	number of bytes ignored, or
				 *	number of files not skipped, or
				 *	number of records not skipped.
				 */
	/* the following registers are device dependent */
	long	mt_dsreg;	/* status register */
	long	mt_gstat;	/* generic (device independent) status */
	long	mt_erreg;	/* error register */
	/* The next two fields are not always used */
	__kernel_daddr_t mt_fileno;	/* number of current file on tape */
	__kernel_daddr_t mt_blkno;	/* current block number */
};



/*
 * Constants for mt_type. Not all of these are supported,
 * and these are not all of the ones that are supported.
 */
#define MT_ISUNKNOWN		0x01
#define MT_ISQIC02		0x02	/* Generic QIC-02 tape streamer */
#define MT_ISWT5150		0x03	/* Wangtek 5150EQ, QIC-150, QIC-02 */
#define MT_ISARCHIVE_5945L2	0x04	/* Archive 5945L-2, QIC-24, QIC-02? */
#define MT_ISCMSJ500		0x05	/* CMS Jumbo 500 (QIC-02?) */
#define MT_ISTDC3610		0x06	/* Tandberg 6310, QIC-24 */
#define MT_ISARCHIVE_VP60I	0x07	/* Archive VP60i, QIC-02 */
#define MT_ISARCHIVE_2150L	0x08	/* Archive Viper 2150L */
#define MT_ISARCHIVE_2060L	0x09	/* Archive Viper 2060L */
#define MT_ISARCHIVESC499	0x0A	/* Archive SC-499 QIC-36 controller */
#define MT_ISQIC02_ALL_FEATURES	0x0F	/* Generic QIC-02 with all features */
#define MT_ISWT5099EEN24	0x11	/* Wangtek 5099-een24, 60MB, QIC-24 */
#define MT_ISTEAC_MT2ST		0x12	/* Teac MT-2ST 155mb drive, Teac DC-1 card (Wangtek type) */
#define MT_ISEVEREX_FT40A	0x32	/* Everex FT40A (QIC-40) */
#define MT_ISDDS1		0x51	/* DDS device without partitions */
#define MT_ISDDS2		0x52	/* DDS device with partitions */
#define MT_ISONSTREAM_SC        0x61   /* OnStream SCSI tape drives (SC-x0)
					  and SCSI emulated (DI, DP, USB) */
#define MT_ISSCSI1		0x71	/* Generic ANSI SCSI-1 tape unit */
#define MT_ISSCSI2		0x72	/* Generic ANSI SCSI-2 tape unit */

/* QIC-40/80/3010/3020 ftape supported drives.
 * 20bit vendor ID + 0x800000 (see ftape-vendors.h)
 */
#define MT_ISFTAPE_UNKNOWN	0x800000 /* obsolete */
#define MT_ISFTAPE_FLAG	0x800000


/* structure for MTIOCPOS - mag tape get position command */

struct	mtpos {
	long 	mt_blkno;	/* current block number */
};


/* mag tape io control commands */
#define	MTIOCTOP	_IOW('m', 1, struct mtop)	/* do a mag tape op */
#define	MTIOCGET	_IOR('m', 2, struct mtget)	/* get tape status */
#define	MTIOCPOS	_IOR('m', 3, struct mtpos)	/* get tape position */


/* Generic Mag Tape (device independent) status macros for examining
 * mt_gstat -- HP-UX compatible.
 * There is room for more generic status bits here, but I don't
 * know which of them are reserved. At least three or so should
 * be added to make this really useful.
 */
#define GMT_EOF(x)              ((x) & 0x80000000)
#define GMT_BOT(x)              ((x) & 0x40000000)
#define GMT_EOT(x)              ((x) & 0x20000000)
#define GMT_SM(x)               ((x) & 0x10000000)  /* DDS setmark */
#define GMT_EOD(x)              ((x) & 0x08000000)  /* DDS EOD */
#define GMT_WR_PROT(x)          ((x) & 0x04000000)
/* #define GMT_ ? 		((x) & 0x02000000) */
#define GMT_ONLINE(x)           ((x) & 0x01000000)
#define GMT_D_6250(x)           ((x) & 0x00800000)
#define GMT_D_1600(x)           ((x) & 0x00400000)
#define GMT_D_800(x)            ((x) & 0x00200000)
/* #define GMT_ ? 		((x) & 0x00100000) */
/* #define GMT_ ? 		((x) & 0x00080000) */
#define GMT_DR_OPEN(x)          ((x) & 0x00040000)  /* door open (no tape) */
/* #define GMT_ ? 		((x) & 0x00020000) */
#define GMT_IM_REP_EN(x)        ((x) & 0x00010000)  /* immediate report mode */
#define GMT_CLN(x)              ((x) & 0x00008000)  /* cleaning requested */
/* 15 generic status bits unused */


/* SCSI-tape specific definitions */
/* Bitfield shifts in the status  */
#define MT_ST_BLKSIZE_SHIFT	0
#define MT_ST_BLKSIZE_MASK	0xffffff
#define MT_ST_DENSITY_SHIFT	24
#define MT_ST_DENSITY_MASK	0xff000000

#define MT_ST_SOFTERR_SHIFT	0
#define MT_ST_SOFTERR_MASK	0xffff

/* Bitfields for the MTSETDRVBUFFER ioctl */
#define MT_ST_OPTIONS		0xf0000000
#define MT_ST_BOOLEANS		0x10000000
#define MT_ST_SETBOOLEANS	0x30000000
#define MT_ST_CLEARBOOLEANS	0x40000000
#define MT_ST_WRITE_THRESHOLD	0x20000000
#define MT_ST_DEF_BLKSIZE	0x50000000
#define MT_ST_DEF_OPTIONS	0x60000000
#define MT_ST_TIMEOUTS		0x70000000
#define MT_ST_SET_TIMEOUT	(MT_ST_TIMEOUTS | 0x000000)
#define MT_ST_SET_LONG_TIMEOUT	(MT_ST_TIMEOUTS | 0x100000)
#define MT_ST_SET_CLN		0x80000000

#define MT_ST_BUFFER_WRITES	0x1
#define MT_ST_ASYNC_WRITES	0x2
#define MT_ST_READ_AHEAD	0x4
#define MT_ST_DEBUGGING		0x8
#define MT_ST_TWO_FM		0x10
#define MT_ST_FAST_MTEOM	0x20
#define MT_ST_AUTO_LOCK		0x40
#define MT_ST_DEF_WRITES	0x80
#define MT_ST_CAN_BSR		0x100
#define MT_ST_NO_BLKLIMS	0x200
#define MT_ST_CAN_PARTITIONS    0x400
#define MT_ST_SCSI2LOGICAL      0x800
#define MT_ST_SYSV              0x1000
#define MT_ST_NOWAIT            0x2000
#define MT_ST_SILI		0x4000

/* The mode parameters to be controlled. Parameter chosen with bits 20-28 */
#define MT_ST_CLEAR_DEFAULT	0xfffff
#define MT_ST_DEF_DENSITY	(MT_ST_DEF_OPTIONS | 0x100000)
#define MT_ST_DEF_COMPRESSION	(MT_ST_DEF_OPTIONS | 0x200000)
#define MT_ST_DEF_DRVBUFFER	(MT_ST_DEF_OPTIONS | 0x300000)

/* The offset for the arguments for the special HP changer load command. */
#define MT_ST_HPLOADER_OFFSET 10000

#endif /* _LINUX_MTIO_H */
