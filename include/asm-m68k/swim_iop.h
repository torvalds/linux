/*
 * SWIM access through the IOP
 * Written by Joshua M. Thompson
 */

/* IOP number and channel number for the SWIM */

#define SWIM_IOP	IOP_NUM_ISM
#define SWIM_CHAN	1

/* Command code: */

#define CMD_INIT		0x01	/* Initialize                  */
#define CMD_SHUTDOWN		0x02	/* Shutdown                    */
#define CMD_START_POLL		0x03	/* Start insert/eject polling  */
#define CMD_STOP_POLL		0x04	/* Stop insert/eject polling   */
#define CMD_SETHFSTAG		0x05	/* Set HFS tag buffer address  */
#define CMD_STATUS		0x06	/* Status                      */
#define CMD_EJECT		0x07	/* Eject                       */
#define CMD_FORMAT		0x08	/* Format                      */
#define CMD_FORMAT_VERIFY	0x09	/* Format and Verify           */
#define CMD_WRITE		0x0A	/* Write                       */
#define CMD_READ		0x0B	/* Read                        */
#define CMD_READ_VERIFY		0x0C	/* Read and Verify             */
#define CMD_CACHE_CTRL		0x0D	/* Cache control               */
#define CMD_TAGBUFF_CTRL	0x0E	/* Tag buffer control          */
#define CMD_GET_ICON		0x0F	/* Get Icon                    */

/* Drive types: */

/* note: apple sez DRV_FDHD is 4, but I get back a type */
/*       of 5 when I do a drive status check on my FDHD */

#define	DRV_NONE	0	/* No drive             */
#define	DRV_UNKNOWN	1	/* Unspecified drive    */
#define	DRV_400K	2	/* 400K                 */
#define	DRV_800K	3	/* 400K/800K            */
#define	DRV_FDHD	5	/* 400K/800K/720K/1440K */
#define	DRV_HD20	7	/* Apple HD20           */

/* Format types: */

#define	FMT_HD20	0x0001	/*  Apple HD20 */
#define	FMT_400K	0x0002	/*  400K (GCR) */
#define	FMT_800K	0x0004	/*  800K (GCR) */
#define	FMT_720K	0x0008	/*  720K (MFM) */
#define	FMT_1440K	0x0010	/* 1.44M (MFM) */

#define	FMD_KIND_400K	1
#define	FMD_KIND_800K	2
#define	FMD_KIND_720K	3
#define	FMD_KIND_1440K	1

/* Icon Flags: */

#define	ICON_MEDIA	0x01	/* Have IOP supply media icon */
#define	ICON_DRIVE	0x01	/* Have IOP supply drive icon */

/* Error codes: */

#define	gcrOnMFMErr	-400	/* GCR (400/800K) on HD media */
#define	verErr		-84	/* verify failed */
#define	fmt2Err		-83	/* can't get enough sync during format */
#define	fmt1Err		-82	/* can't find sector 0 after track format */
#define	sectNFErr	-81	/* can't find sector */
#define	seekErr		-80	/* drive error during seek */
#define	spdAdjErr	-79	/* can't set drive speed */
#define	twoSideErr	-78	/* drive is single-sided */
#define	initIWMErr	-77	/* error during initialization */
#define	tk0badErr	-76	/* track zero is bad */
#define	cantStepErr	-75	/* drive error during step */
#define	wrUnderrun	-74	/* write underrun occurred */
#define	badDBtSlp	-73	/* bad data bitslip marks */
#define	badDCksum	-72	/* bad data checksum */
#define	noDtaMkErr	-71	/* can't find data mark */
#define	badBtSlpErr	-70	/* bad address bitslip marks */
#define	badCksmErr	-69	/* bad address-mark checksum */
#define	dataVerErr	-68	/* read-verify failed */
#define	noAdrMkErr	-67	/* can't find an address mark */
#define	noNybErr	-66	/* no nybbles? disk is probably degaussed */
#define	offLinErr	-65	/* no disk in drive */
#define	noDriveErr	-64	/* drive isn't connected */
#define	nsDrvErr	-56	/* no such drive */
#define	paramErr	-50	/* bad positioning information */
#define	wPrErr		-44	/* write protected */
#define	openErr		-23	/* already initialized */

#ifndef __ASSEMBLY__

struct swim_drvstatus {
	__u16	curr_track;	/* Current track number                   */
	__u8	write_prot;	/* 0x80 if disk is write protected        */
	__u8	disk_in_drive;	/* 0x01 or 0x02 if a disk is in the drive */
	__u8	installed;	/* 0x01 if drive installed, 0xFF if not   */
	__u8	num_sides;	/* 0x80 if two-sided format supported     */
	__u8	two_sided;	/* 0xff if two-sided format diskette      */
	__u8	new_interface;	/* 0x00 if old 400K drive, 0xFF if newer  */
	__u16	errors;		/* Disk error count                       */
	struct {		/* 32 bits */
		__u16	reserved;
		__u16	:4;
		__u16	external:1;	/* Drive is external        */
		__u16	scsi:1;		/* Drive is a SCSI drive    */
		__u16	fixed:1;	/* Drive has fixed media    */
		__u16	secondary:1;	/* Drive is secondary drive */
		__u8	type;		/* Drive type               */
	} info;
	__u8	mfm_drive;	/* 0xFF if this is an FDHD drive    */
	__u8	mfm_disk;	/* 0xFF if 720K/1440K (MFM) disk    */
	__u8	mfm_format;	/* 0x00 if 720K, 0xFF if 1440K      */
	__u8	ctlr_type;	/* 0x00 if IWM, 0xFF if SWIM        */
	__u16	curr_format;	/* Current format type              */
	__u16	allowed_fmt;	/* Allowed format types             */
	__u32	num_blocks;	/* Number of blocks on disk         */
	__u8	icon_flags;	/* Icon flags                       */
	__u8	unusued;
};

/* Commands issued from the host to the IOP: */

struct swimcmd_init {
	__u8	code;		/* CMD_INIT */
	__u8	unusued;
	__u16	error;
	__u8	drives[28];	/* drive type list */
};

struct swimcmd_startpoll {
	__u8	code;		/* CMD_START_POLL */
	__u8	unusued;
	__u16	error;
};

struct swimcmd_sethfstag {
	__u8	code;		/* CMD_SETHFSTAG */
	__u8	unusued;
	__u16	error;
	caddr_t	tagbuf;		/* HFS tag buffer address */
};

struct swimcmd_status {
	__u8	code;		/* CMD_STATUS */
	__u8	drive_num;
	__u16	error;
	struct swim_drvstatus status;
};

struct swimcmd_eject {
	__u8	code;		/* CMD_EJECT */
	__u8	drive_num;
	__u16	error;
	struct swim_drvstatus status;
};

struct swimcmd_format {
	__u8	code;		/* CMD_FORMAT */
	__u8	drive_num;
	__u16	error;
	union {
		struct {
			__u16 fmt;	   /* format kind                  */
			__u8  hdrbyte;	   /* fmt byte for hdr (0=default) */
			__u8  interleave;  /* interleave (0 = default)     */
			caddr_t	databuf;   /* sector data buff (0=default  */
			caddr_t	tagbuf;	   /* tag data buffer (0=default)  */
		} f;
		struct swim_drvstatus status;
	} p;
};

struct swimcmd_fmtverify {
	__u8	code;		/* CMD_FORMAT_VERIFY */
	__u8	drive_num;
	__u16	error;
};

struct swimcmd_rw {
	__u8	code;		/* CMD_READ, CMD_WRITE or CMD_READ_VERIFY */
	__u8	drive_num;
	__u16	error;
	caddr_t	buffer;		/* R/W buffer address */
	__u32	first_block;	/* Starting block     */
	__u32	num_blocks;	/* Number of blocks   */
	__u8	tag[12];	/* tag data           */
};

struct swimcmd_cachectl {
	__u8	code;		/* CMD_CACHE_CTRL */
	__u8	unused;
	__u16	error;
	__u8	enable;		/* Nonzero to enable cache                */
	__u8	install;	/* +1 = install, -1 = remove, 0 = neither */
};

struct swimcmd_tagbufctl {
	__u8	code;		/* CMD_TAGBUFF_CTRL */
	__u8	unused;
	__u16	error;
	caddr_t	buf;		/* buffer address or 0 to disable */
};

struct swimcmd_geticon {
	__u8	code;		/* CMD_GET_ICON */
	__u8	drive_num;
	__u16	error;
	caddr_t	buffer;		/* Nuffer address */
	__u16	kind;		/* 0 = media icon, 1 = drive icon */
	__u16	unused;
	__u16	max_bytes;	/* maximum  byte count */
};

/* Messages from the SWIM IOP to the host CPU: */

struct swimmsg_status {
	__u8	code;		/* 1 = insert, 2 = eject, 3 = status changed */
	__u8	drive_num;
	__u16	error;
	struct swim_drvstatus status;
};

#endif /* __ASSEMBLY__ */
