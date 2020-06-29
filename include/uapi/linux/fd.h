/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_FD_H
#define _UAPI_LINUX_FD_H

#include <linux/ioctl.h>
#include <linux/compiler.h>

/* New file layout: Now the ioctl definitions immediately follow the
 * definitions of the structures that they use */

/*
 * Geometry
 */
struct floppy_struct {
	unsigned int	size,		/* nr of sectors total */
			sect,		/* sectors per track */
			head,		/* nr of heads */
			track,		/* nr of tracks */
			stretch;	/* bit 0 !=0 means double track steps */
					/* bit 1 != 0 means swap sides */
					/* bits 2..9 give the first sector */
					/*  number (the LSB is flipped) */
#define FD_STRETCH 1
#define FD_SWAPSIDES 2
#define FD_ZEROBASED 4
#define FD_SECTBASEMASK 0x3FC
#define FD_MKSECTBASE(s) (((s) ^ 1) << 2)
#define FD_SECTBASE(floppy) ((((floppy)->stretch & FD_SECTBASEMASK) >> 2) ^ 1)

	unsigned char	gap,		/* gap1 size */

			rate,		/* data rate. |= 0x40 for perpendicular */
#define FD_2M 0x4
#define FD_SIZECODEMASK 0x38
#define FD_SIZECODE(floppy) (((((floppy)->rate&FD_SIZECODEMASK)>> 3)+ 2) %8)
#define FD_SECTSIZE(floppy) ( (floppy)->rate & FD_2M ? \
			     512 : 128 << FD_SIZECODE(floppy) )
#define FD_PERP 0x40

			spec1,		/* stepping rate, head unload time */
			fmt_gap;	/* gap2 size */
	const char	* name; /* used only for predefined formats */
};


/* commands needing write access have 0x40 set */
/* commands needing super user access have 0x80 set */

#define FDCLRPRM _IO(2, 0x41)
/* clear user-defined parameters */

#define FDSETPRM _IOW(2, 0x42, struct floppy_struct) 
#define FDSETMEDIAPRM FDSETPRM
/* set user-defined parameters for current media */

#define FDDEFPRM _IOW(2, 0x43, struct floppy_struct) 
#define FDGETPRM _IOR(2, 0x04, struct floppy_struct)
#define FDDEFMEDIAPRM FDDEFPRM
#define FDGETMEDIAPRM FDGETPRM
/* set/get disk parameters */


#define	FDMSGON  _IO(2,0x45)
#define	FDMSGOFF _IO(2,0x46)
/* issue/don't issue kernel messages on media type change */


/* 
 * Formatting (obsolete)
 */
#define FD_FILL_BYTE 0xF6 /* format fill byte. */

struct format_descr {
	unsigned int device,head,track;
};

#define FDFMTBEG _IO(2,0x47)
/* begin formatting a disk */
#define	FDFMTTRK _IOW(2,0x48, struct format_descr)
/* format the specified track */
#define FDFMTEND _IO(2,0x49)
/* end formatting a disk */


/*
 * Error thresholds
 */
struct floppy_max_errors {
	unsigned int
	  abort,      /* number of errors to be reached before aborting */
	  read_track, /* maximal number of errors permitted to read an
		       * entire track at once */
	  reset,      /* maximal number of errors before a reset is tried */
	  recal,      /* maximal number of errors before a recalibrate is
		       * tried */

	  /*
	   * Threshold for reporting FDC errors to the console.
	   * Setting this to zero may flood your screen when using
	   * ultra cheap floppies ;-)
	   */
	  reporting;

};

#define FDSETEMSGTRESH	_IO(2,0x4a)
/* set fdc error reporting threshold */

#define FDFLUSH  _IO(2,0x4b)
/* flush buffers for media; either for verifying media, or for
 * handling a media change without closing the file descriptor */

#define FDSETMAXERRS _IOW(2, 0x4c, struct floppy_max_errors)
#define FDGETMAXERRS _IOR(2, 0x0e, struct floppy_max_errors)
/* set/get abortion and read_track threshold. See also floppy_drive_params
 * structure */


typedef char floppy_drive_name[16];
#define FDGETDRVTYP _IOR(2, 0x0f, floppy_drive_name)
/* get drive type: 5 1/4 or 3 1/2 */


/*
 * Drive parameters (user modifiable)
 */
struct floppy_drive_params {
	signed char cmos;		/* CMOS type */
	
	/* Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms 
	 * etc) and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
	 */
	unsigned long max_dtr;		/* Step rate, usec */
	unsigned long hlt;     		/* Head load/settle time, msec */
	unsigned long hut;     		/* Head unload time (remnant of 
					 * 8" drives) */
	unsigned long srt;     		/* Step rate, usec */

	unsigned long spinup;		/* time needed for spinup (expressed
					 * in jiffies) */
	unsigned long spindown;		/* timeout needed for spindown */
	unsigned char spindown_offset;	/* decides in which position the disk
					 * will stop */
	unsigned char select_delay;	/* delay to wait after select */
	unsigned char rps;		/* rotations per second */
	unsigned char tracks;		/* maximum number of tracks */
	unsigned long timeout;		/* timeout for interrupt requests */
	
	unsigned char interleave_sect;	/* if there are more sectors, use 
					 * interleave */
	
	struct floppy_max_errors max_errors;
	
	char flags;			/* various flags, including ftd_msg */
/*
 * Announce successful media type detection and media information loss after
 * disk changes.
 * Also used to enable/disable printing of overrun warnings.
 */

#define FTD_MSG 0x10
#define FD_BROKEN_DCL 0x20
#define FD_DEBUG 0x02
#define FD_SILENT_DCL_CLEAR 0x4
#define FD_INVERTED_DCL 0x80 /* must be 0x80, because of hardware 
				considerations */

	char read_track;		/* use readtrack during probing? */

/*
 * Auto-detection. Each drive type has eight formats which are
 * used in succession to try to read the disk. If the FDC cannot lock onto
 * the disk, the next format is tried. This uses the variable 'probing'.
 */

#define FD_AUTODETECT_SIZE 8

	short autodetect[FD_AUTODETECT_SIZE]; /* autodetected formats */
	
	int checkfreq; /* how often should the drive be checked for disk 
			* changes */
	int native_format; /* native format of this drive */
};

enum {
	FD_NEED_TWADDLE_BIT,	/* more magic */
	FD_VERIFY_BIT,		/* inquire for write protection */
	FD_DISK_NEWCHANGE_BIT,	/* change detected, and no action undertaken yet
				 * to clear media change status */
	FD_UNUSED_BIT,
	FD_DISK_CHANGED_BIT,	/* disk has been changed since last i/o */
	FD_DISK_WRITABLE_BIT,	/* disk is writable */
	FD_OPEN_SHOULD_FAIL_BIT
};

#define FDSETDRVPRM _IOW(2, 0x90, struct floppy_drive_params)
#define FDGETDRVPRM _IOR(2, 0x11, struct floppy_drive_params)
/* set/get drive parameters */


/*
 * Current drive state (not directly modifiable by user, readonly)
 */
struct floppy_drive_struct {
	unsigned long flags;
/* values for these flags */
#define FD_NEED_TWADDLE (1 << FD_NEED_TWADDLE_BIT)
#define FD_VERIFY (1 << FD_VERIFY_BIT)
#define FD_DISK_NEWCHANGE (1 << FD_DISK_NEWCHANGE_BIT)
#define FD_DISK_CHANGED (1 << FD_DISK_CHANGED_BIT)
#define FD_DISK_WRITABLE (1 << FD_DISK_WRITABLE_BIT)

	unsigned long spinup_date;
	unsigned long select_date;
	unsigned long first_read_date;
	short probed_format;
	short track; /* current track */
	short maxblock; /* id of highest block read */
	short maxtrack; /* id of highest half track read */
	int generation; /* how many diskchanges? */

/*
 * (User-provided) media information is _not_ discarded after a media change
 * if the corresponding keep_data flag is non-zero. Positive values are
 * decremented after each probe.
 */
	int keep_data;
	
	/* Prevent "aliased" accesses. */
	int fd_ref;
	int fd_device;
	unsigned long last_checked; /* when was the drive last checked for a disk 
			   * change? */
	
	char *dmabuf;
	int bufblocks;
};

#define FDGETDRVSTAT _IOR(2, 0x12, struct floppy_drive_struct)
#define FDPOLLDRVSTAT _IOR(2, 0x13, struct floppy_drive_struct)
/* get drive state: GET returns the cached state, POLL polls for new state */


/*
 * reset FDC
 */
enum reset_mode {
	FD_RESET_IF_NEEDED,	/* reset only if the reset flags is set */
	FD_RESET_IF_RAWCMD,	/* obsolete */
	FD_RESET_ALWAYS		/* reset always */
};
#define FDRESET _IO(2, 0x54)


/*
 * FDC state
 */
struct floppy_fdc_state {	
	int spec1;		/* spec1 value last used */
	int spec2;		/* spec2 value last used */
	int dtr;
	unsigned char version;	/* FDC version code */
	unsigned char dor;
	unsigned long address;	/* io address */
	unsigned int rawcmd:2;
	unsigned int reset:1;
	unsigned int need_configure:1;
	unsigned int perp_mode:2;
	unsigned int has_fifo:1;
	unsigned int driver_version;	/* version code for floppy driver */
#define FD_DRIVER_VERSION 0x100
/* user programs using the floppy API should use floppy_fdc_state to
 * get the version number of the floppy driver that they are running
 * on. If this version number is bigger than the one compiled into the
 * user program (the FD_DRIVER_VERSION define), it should be prepared
 * to bigger structures
 */

	unsigned char track[4];
	/* Position of the heads of the 4 units attached to this FDC,
	 * as stored on the FDC. In the future, the position as stored
	 * on the FDC might not agree with the actual physical
	 * position of these drive heads. By allowing such
	 * disagreement, it will be possible to reset the FDC without
	 * incurring the expensive cost of repositioning all heads.
	 * Right now, these positions are hard wired to 0. */

};

#define FDGETFDCSTAT _IOR(2, 0x15, struct floppy_fdc_state)


/*
 * Asynchronous Write error tracking
 */
struct floppy_write_errors {
	/* Write error logging.
	 *
	 * These fields can be cleared with the FDWERRORCLR ioctl.
	 * Only writes that were attempted but failed due to a physical media
	 * error are logged.  write(2) calls that fail and return an error code
	 * to the user process are not counted.
	 */

	unsigned int write_errors;  /* number of physical write errors 
				     * encountered */
	
	/* position of first and last write errors */
	unsigned long first_error_sector;
	int           first_error_generation;
	unsigned long last_error_sector;
	int           last_error_generation;
	
	unsigned int badness; /* highest retry count for a read or write 
			       * operation */
};

#define FDWERRORCLR  _IO(2, 0x56)
/* clear write error and badness information */
#define FDWERRORGET  _IOR(2, 0x17, struct floppy_write_errors)
/* get write error and badness information */


/*
 * Raw commands
 */
/* new interface flag: now we can do them in batches */
#define FDHAVEBATCHEDRAWCMD

struct floppy_raw_cmd {
	unsigned int flags;
#define FD_RAW_READ 1
#define FD_RAW_WRITE 2
#define FD_RAW_NO_MOTOR 4
#define FD_RAW_DISK_CHANGE 4 /* out: disk change flag was set */
#define FD_RAW_INTR 8    /* wait for an interrupt */
#define FD_RAW_SPIN 0x10 /* spin up the disk for this command */
#define FD_RAW_NO_MOTOR_AFTER 0x20 /* switch the motor off after command 
				    * completion */
#define FD_RAW_NEED_DISK 0x40  /* this command needs a disk to be present */
#define FD_RAW_NEED_SEEK 0x80  /* this command uses an implied seek (soft) */

/* more "in" flags */
#define FD_RAW_MORE 0x100  /* more records follow */
#define FD_RAW_STOP_IF_FAILURE 0x200 /* stop if we encounter a failure */
#define FD_RAW_STOP_IF_SUCCESS 0x400 /* stop if command successful */
#define FD_RAW_SOFTFAILURE 0x800 /* consider the return value for failure
				  * detection too */

/* more "out" flags */
#define FD_RAW_FAILURE 0x10000 /* command sent to fdc, fdc returned error */
#define FD_RAW_HARDFAILURE 0x20000 /* fdc had to be reset, or timed out */

	void __user *data;
	char *kernel_data; /* location of data buffer in the kernel */
	struct floppy_raw_cmd *next; /* used for chaining of raw cmd's 
				      * within the kernel */
	long length; /* in: length of dma transfer. out: remaining bytes */
	long phys_length; /* physical length, if different from dma length */
	int buffer_length; /* length of allocated buffer */

	unsigned char rate;

#define FD_RAW_CMD_SIZE 16
#define FD_RAW_REPLY_SIZE 16
#define FD_RAW_CMD_FULLSIZE (FD_RAW_CMD_SIZE + 1 + FD_RAW_REPLY_SIZE)

	/* The command may take up the space initially intended for the reply
	 * and the reply count. Needed for long 82078 commands such as RESTORE,
	 * which takes 17 command bytes.
	 */

	unsigned char cmd_count;
	union {
		struct {
			unsigned char cmd[FD_RAW_CMD_SIZE];
			unsigned char reply_count;
			unsigned char reply[FD_RAW_REPLY_SIZE];
		};
		unsigned char fullcmd[FD_RAW_CMD_FULLSIZE];
	};
	int track;
	int resultcode;

	int reserved1;
	int reserved2;
};

#define FDRAWCMD _IO(2, 0x58)
/* send a raw command to the fdc. Structure size not included, because of
 * batches */

#define FDTWADDLE _IO(2, 0x59)
/* flicker motor-on bit before reading a sector. Experimental */


#define FDEJECT _IO(2, 0x5a)
/* eject the disk */



#endif /* _UAPI_LINUX_FD_H */
