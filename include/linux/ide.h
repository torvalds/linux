#ifndef _IDE_H
#define _IDE_H
/*
 *  linux/include/linux/ide.h
 *
 *  Copyright (C) 1994-2002  Linus Torvalds & authors
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/ata.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/bio.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/completion.h>
#include <linux/pm.h>
#ifdef CONFIG_BLK_DEV_IDEACPI
#include <acpi/acpi.h>
#endif
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/mutex.h>

#if defined(CONFIG_CRIS) || defined(CONFIG_FRV) || defined(CONFIG_MN10300)
# define SUPPORT_VLB_SYNC 0
#else
# define SUPPORT_VLB_SYNC 1
#endif

/*
 * Probably not wise to fiddle with these
 */
#define IDE_DEFAULT_MAX_FAILURES	1
#define ERROR_MAX	8	/* Max read/write errors per sector */
#define ERROR_RESET	3	/* Reset controller every 4th retry */
#define ERROR_RECAL	1	/* Recalibrate every 2nd retry */

/* Error codes returned in rq->errors to the higher part of the driver. */
enum {
	IDE_DRV_ERROR_GENERAL	= 101,
	IDE_DRV_ERROR_FILEMARK	= 102,
	IDE_DRV_ERROR_EOD	= 103,
};

/*
 * Definitions for accessing IDE controller registers
 */
#define IDE_NR_PORTS		(10)

struct ide_io_ports {
	unsigned long	data_addr;

	union {
		unsigned long error_addr;	/*   read:  error */
		unsigned long feature_addr;	/*  write: feature */
	};

	unsigned long	nsect_addr;
	unsigned long	lbal_addr;
	unsigned long	lbam_addr;
	unsigned long	lbah_addr;

	unsigned long	device_addr;

	union {
		unsigned long status_addr;	/*  read: status  */
		unsigned long command_addr;	/* write: command */
	};

	unsigned long	ctl_addr;

	unsigned long	irq_addr;
};

#define OK_STAT(stat,good,bad)	(((stat)&((good)|(bad)))==(good))

#define BAD_R_STAT	(ATA_BUSY | ATA_ERR)
#define BAD_W_STAT	(BAD_R_STAT | ATA_DF)
#define BAD_STAT	(BAD_R_STAT | ATA_DRQ)
#define DRIVE_READY	(ATA_DRDY | ATA_DSC)

#define BAD_CRC		(ATA_ABORTED | ATA_ICRC)

#define SATA_NR_PORTS		(3)	/* 16 possible ?? */

#define SATA_STATUS_OFFSET	(0)
#define SATA_ERROR_OFFSET	(1)
#define SATA_CONTROL_OFFSET	(2)

/*
 * Our Physical Region Descriptor (PRD) table should be large enough
 * to handle the biggest I/O request we are likely to see.  Since requests
 * can have no more than 256 sectors, and since the typical blocksize is
 * two or more sectors, we could get by with a limit of 128 entries here for
 * the usual worst case.  Most requests seem to include some contiguous blocks,
 * further reducing the number of table entries required.
 *
 * The driver reverts to PIO mode for individual requests that exceed
 * this limit (possible with 512 byte blocksizes, eg. MSDOS f/s), so handling
 * 100% of all crazy scenarios here is not necessary.
 *
 * As it turns out though, we must allocate a full 4KB page for this,
 * so the two PRD tables (ide0 & ide1) will each get half of that,
 * allowing each to have about 256 entries (8 bytes each) from this.
 */
#define PRD_BYTES       8
#define PRD_ENTRIES	256

/*
 * Some more useful definitions
 */
#define PARTN_BITS	6	/* number of minor dev bits for partitions */
#define MAX_DRIVES	2	/* per interface; 2 assumed by lots of code */
#define SECTOR_SIZE	512

/*
 * Timeouts for various operations:
 */
enum {
	/* spec allows up to 20ms */
	WAIT_DRQ	= HZ / 10,	/* 100ms */
	/* some laptops are very slow */
	WAIT_READY	= 5 * HZ,	/* 5s */
	/* should be less than 3ms (?), if all ATAPI CD is closed at boot */
	WAIT_PIDENTIFY	= 10 * HZ,	/* 10s */
	/* worst case when spinning up */
	WAIT_WORSTCASE	= 30 * HZ,	/* 30s */
	/* maximum wait for an IRQ to happen */
	WAIT_CMD	= 10 * HZ,	/* 10s */
	/* Some drives require a longer IRQ timeout. */
	WAIT_FLOPPY_CMD	= 50 * HZ,	/* 50s */
	/*
	 * Some drives (for example, Seagate STT3401A Travan) require a very
	 * long timeout, because they don't return an interrupt or clear their
	 * BSY bit until after the command completes (even retension commands).
	 */
	WAIT_TAPE_CMD	= 900 * HZ,	/* 900s */
	/* minimum sleep time */
	WAIT_MIN_SLEEP	= HZ / 50,	/* 20ms */
};

/*
 * Op codes for special requests to be handled by ide_special_rq().
 * Values should be in the range of 0x20 to 0x3f.
 */
#define REQ_DRIVE_RESET		0x20
#define REQ_DEVSET_EXEC		0x21
#define REQ_PARK_HEADS		0x22
#define REQ_UNPARK_HEADS	0x23

/*
 * Check for an interrupt and acknowledge the interrupt status
 */
struct hwif_s;
typedef int (ide_ack_intr_t)(struct hwif_s *);

/*
 * hwif_chipset_t is used to keep track of the specific hardware
 * chipset used by each IDE interface, if known.
 */
enum {		ide_unknown,	ide_generic,	ide_pci,
		ide_cmd640,	ide_dtc2278,	ide_ali14xx,
		ide_qd65xx,	ide_umc8672,	ide_ht6560b,
		ide_4drives,	ide_pmac,	ide_acorn,
		ide_au1xxx,	ide_palm3710
};

typedef u8 hwif_chipset_t;

/*
 * Structure to hold all information about the location of this port
 */
typedef struct hw_regs_s {
	union {
		struct ide_io_ports	io_ports;
		unsigned long		io_ports_array[IDE_NR_PORTS];
	};

	int		irq;			/* our irq number */
	ide_ack_intr_t	*ack_intr;		/* acknowledge interrupt */
	hwif_chipset_t  chipset;
	struct device	*dev, *parent;
	unsigned long	config;
} hw_regs_t;

static inline void ide_std_init_ports(hw_regs_t *hw,
				      unsigned long io_addr,
				      unsigned long ctl_addr)
{
	unsigned int i;

	for (i = 0; i <= 7; i++)
		hw->io_ports_array[i] = io_addr++;

	hw->io_ports.ctl_addr = ctl_addr;
}

#define MAX_HWIFS	10

/*
 * Now for the data we need to maintain per-drive:  ide_drive_t
 */

#define ide_scsi	0x21
#define ide_disk	0x20
#define ide_optical	0x7
#define ide_cdrom	0x5
#define ide_tape	0x1
#define ide_floppy	0x0

/*
 * Special Driver Flags
 *
 * set_geometry	: respecify drive geometry
 * recalibrate	: seek to cyl 0
 * set_multmode	: set multmode count
 * reserved	: unused
 */
typedef union {
	unsigned all			: 8;
	struct {
		unsigned set_geometry	: 1;
		unsigned recalibrate	: 1;
		unsigned set_multmode	: 1;
		unsigned reserved	: 5;
	} b;
} special_t;

/*
 * Status returned from various ide_ functions
 */
typedef enum {
	ide_stopped,	/* no drive operation was started */
	ide_started,	/* a drive operation was started, handler was set */
} ide_startstop_t;

enum {
	IDE_VALID_ERROR 		= (1 << 1),
	IDE_VALID_FEATURE		= IDE_VALID_ERROR,
	IDE_VALID_NSECT 		= (1 << 2),
	IDE_VALID_LBAL			= (1 << 3),
	IDE_VALID_LBAM			= (1 << 4),
	IDE_VALID_LBAH			= (1 << 5),
	IDE_VALID_DEVICE		= (1 << 6),
	IDE_VALID_LBA			= IDE_VALID_LBAL |
					  IDE_VALID_LBAM |
					  IDE_VALID_LBAH,
	IDE_VALID_OUT_TF		= IDE_VALID_FEATURE |
					  IDE_VALID_NSECT |
					  IDE_VALID_LBA,
	IDE_VALID_IN_TF 		= IDE_VALID_NSECT |
					  IDE_VALID_LBA,
	IDE_VALID_OUT_HOB		= IDE_VALID_OUT_TF,
	IDE_VALID_IN_HOB		= IDE_VALID_ERROR |
					  IDE_VALID_NSECT |
					  IDE_VALID_LBA,
};

enum {
	IDE_TFLAG_LBA48			= (1 << 0),
	IDE_TFLAG_WRITE			= (1 << 1),
	IDE_TFLAG_CUSTOM_HANDLER	= (1 << 2),
	IDE_TFLAG_DMA_PIO_FALLBACK	= (1 << 3),
	/* force 16-bit I/O operations */
	IDE_TFLAG_IO_16BIT		= (1 << 4),
	/* struct ide_cmd was allocated using kmalloc() */
	IDE_TFLAG_DYN			= (1 << 5),
	IDE_TFLAG_FS			= (1 << 6),
	IDE_TFLAG_MULTI_PIO		= (1 << 7),
};

enum {
	IDE_FTFLAG_FLAGGED		= (1 << 0),
	IDE_FTFLAG_SET_IN_FLAGS		= (1 << 1),
	IDE_FTFLAG_OUT_DATA		= (1 << 2),
	IDE_FTFLAG_IN_DATA		= (1 << 3),
};

struct ide_taskfile {
	u8	data;		/* 0: data byte (for TASKFILE ioctl) */
	union {			/* 1: */
		u8 error;	/*  read: error */
		u8 feature;	/* write: feature */
	};
	u8	nsect;		/* 2: number of sectors */
	u8	lbal;		/* 3: LBA low */
	u8	lbam;		/* 4: LBA mid */
	u8	lbah;		/* 5: LBA high */
	u8	device;		/* 6: device select */
	union {			/* 7: */
		u8 status;	/*  read: status */
		u8 command;	/* write: command */
	};
};

struct ide_cmd {
	struct ide_taskfile	tf;
	struct ide_taskfile	hob;
	struct {
		struct {
			u8		tf;
			u8		hob;
		} out, in;
	} valid;

	u8			tf_flags;
	u8			ftf_flags;	/* for TASKFILE ioctl */
	int			protocol;

	int			sg_nents;	  /* number of sg entries */
	int			orig_sg_nents;
	int			sg_dma_direction; /* DMA transfer direction */

	unsigned int		nbytes;
	unsigned int		nleft;
	unsigned int		last_xfer_len;

	struct scatterlist	*cursg;
	unsigned int		cursg_ofs;

	struct request		*rq;		/* copy of request */
	void			*special;	/* valid_t generally */
};

/* ATAPI packet command flags */
enum {
	/* set when an error is considered normal - no retry (ide-tape) */
	PC_FLAG_ABORT			= (1 << 0),
	PC_FLAG_SUPPRESS_ERROR		= (1 << 1),
	PC_FLAG_WAIT_FOR_DSC		= (1 << 2),
	PC_FLAG_DMA_OK			= (1 << 3),
	PC_FLAG_DMA_IN_PROGRESS		= (1 << 4),
	PC_FLAG_DMA_ERROR		= (1 << 5),
	PC_FLAG_WRITING			= (1 << 6),
};

/*
 * With each packet command, we allocate a buffer of IDE_PC_BUFFER_SIZE bytes.
 * This is used for several packet commands (not for READ/WRITE commands).
 */
#define IDE_PC_BUFFER_SIZE	64
#define ATAPI_WAIT_PC		(60 * HZ)

struct ide_atapi_pc {
	/* actual packet bytes */
	u8 c[12];
	/* incremented on each retry */
	int retries;
	int error;

	/* bytes to transfer */
	int req_xfer;
	/* bytes actually transferred */
	int xferred;

	/* data buffer */
	u8 *buf;
	/* current buffer position */
	u8 *cur_pos;
	int buf_size;
	/* missing/available data on the current buffer */
	int b_count;

	/* the corresponding request */
	struct request *rq;

	unsigned long flags;

	/*
	 * those are more or less driver-specific and some of them are subject
	 * to change/removal later.
	 */
	u8 pc_buf[IDE_PC_BUFFER_SIZE];

	/* idetape only */
	struct idetape_bh *bh;
	char *b_data;

	unsigned long timeout;
};

struct ide_devset;
struct ide_driver;

#ifdef CONFIG_BLK_DEV_IDEACPI
struct ide_acpi_drive_link;
struct ide_acpi_hwif_link;
#endif

struct ide_drive_s;

struct ide_disk_ops {
	int		(*check)(struct ide_drive_s *, const char *);
	int		(*get_capacity)(struct ide_drive_s *);
	void		(*setup)(struct ide_drive_s *);
	void		(*flush)(struct ide_drive_s *);
	int		(*init_media)(struct ide_drive_s *, struct gendisk *);
	int		(*set_doorlock)(struct ide_drive_s *, struct gendisk *,
					int);
	ide_startstop_t	(*do_request)(struct ide_drive_s *, struct request *,
				      sector_t);
	int		(*ioctl)(struct ide_drive_s *, struct block_device *,
				 fmode_t, unsigned int, unsigned long);
};

/* ATAPI device flags */
enum {
	IDE_AFLAG_DRQ_INTERRUPT		= (1 << 0),

	/* ide-cd */
	/* Drive cannot eject the disc. */
	IDE_AFLAG_NO_EJECT		= (1 << 1),
	/* Drive is a pre ATAPI 1.2 drive. */
	IDE_AFLAG_PRE_ATAPI12		= (1 << 2),
	/* TOC addresses are in BCD. */
	IDE_AFLAG_TOCADDR_AS_BCD	= (1 << 3),
	/* TOC track numbers are in BCD. */
	IDE_AFLAG_TOCTRACKS_AS_BCD	= (1 << 4),
	/* Saved TOC information is current. */
	IDE_AFLAG_TOC_VALID		= (1 << 6),
	/* We think that the drive door is locked. */
	IDE_AFLAG_DOOR_LOCKED		= (1 << 7),
	/* SET_CD_SPEED command is unsupported. */
	IDE_AFLAG_NO_SPEED_SELECT	= (1 << 8),
	IDE_AFLAG_VERTOS_300_SSD	= (1 << 9),
	IDE_AFLAG_VERTOS_600_ESD	= (1 << 10),
	IDE_AFLAG_SANYO_3CD		= (1 << 11),
	IDE_AFLAG_FULL_CAPS_PAGE	= (1 << 12),
	IDE_AFLAG_PLAY_AUDIO_OK		= (1 << 13),
	IDE_AFLAG_LE_SPEED_FIELDS	= (1 << 14),

	/* ide-floppy */
	/* Avoid commands not supported in Clik drive */
	IDE_AFLAG_CLIK_DRIVE		= (1 << 15),
	/* Requires BH algorithm for packets */
	IDE_AFLAG_ZIP_DRIVE		= (1 << 16),
	/* Supports format progress report */
	IDE_AFLAG_SRFP			= (1 << 17),

	/* ide-tape */
	IDE_AFLAG_IGNORE_DSC		= (1 << 18),
	/* 0 When the tape position is unknown */
	IDE_AFLAG_ADDRESS_VALID		= (1 <<	19),
	/* Device already opened */
	IDE_AFLAG_BUSY			= (1 << 20),
	/* Attempt to auto-detect the current user block size */
	IDE_AFLAG_DETECT_BS		= (1 << 21),
	/* Currently on a filemark */
	IDE_AFLAG_FILEMARK		= (1 << 22),
	/* 0 = no tape is loaded, so we don't rewind after ejecting */
	IDE_AFLAG_MEDIUM_PRESENT	= (1 << 23),

	IDE_AFLAG_NO_AUTOCLOSE		= (1 << 24),
};

/* device flags */
enum {
	/* restore settings after device reset */
	IDE_DFLAG_KEEP_SETTINGS		= (1 << 0),
	/* device is using DMA for read/write */
	IDE_DFLAG_USING_DMA		= (1 << 1),
	/* okay to unmask other IRQs */
	IDE_DFLAG_UNMASK		= (1 << 2),
	/* don't attempt flushes */
	IDE_DFLAG_NOFLUSH		= (1 << 3),
	/* DSC overlap */
	IDE_DFLAG_DSC_OVERLAP		= (1 << 4),
	/* give potential excess bandwidth */
	IDE_DFLAG_NICE1			= (1 << 5),
	/* device is physically present */
	IDE_DFLAG_PRESENT		= (1 << 6),
	/* id read from device (synthetic if not set) */
	IDE_DFLAG_ID_READ		= (1 << 8),
	IDE_DFLAG_NOPROBE		= (1 << 9),
	/* need to do check_media_change() */
	IDE_DFLAG_REMOVABLE		= (1 << 10),
	/* needed for removable devices */
	IDE_DFLAG_ATTACH		= (1 << 11),
	IDE_DFLAG_FORCED_GEOM		= (1 << 12),
	/* disallow setting unmask bit */
	IDE_DFLAG_NO_UNMASK		= (1 << 13),
	/* disallow enabling 32-bit I/O */
	IDE_DFLAG_NO_IO_32BIT		= (1 << 14),
	/* for removable only: door lock/unlock works */
	IDE_DFLAG_DOORLOCKING		= (1 << 15),
	/* disallow DMA */
	IDE_DFLAG_NODMA			= (1 << 16),
	/* powermanagment told us not to do anything, so sleep nicely */
	IDE_DFLAG_BLOCKED		= (1 << 17),
	/* sleeping & sleep field valid */
	IDE_DFLAG_SLEEPING		= (1 << 18),
	IDE_DFLAG_POST_RESET		= (1 << 19),
	IDE_DFLAG_UDMA33_WARNED		= (1 << 20),
	IDE_DFLAG_LBA48			= (1 << 21),
	/* status of write cache */
	IDE_DFLAG_WCACHE		= (1 << 22),
	/* used for ignoring ATA_DF */
	IDE_DFLAG_NOWERR		= (1 << 23),
	/* retrying in PIO */
	IDE_DFLAG_DMA_PIO_RETRY		= (1 << 24),
	IDE_DFLAG_LBA			= (1 << 25),
	/* don't unload heads */
	IDE_DFLAG_NO_UNLOAD		= (1 << 26),
	/* heads unloaded, please don't reset port */
	IDE_DFLAG_PARKED		= (1 << 27),
	IDE_DFLAG_MEDIA_CHANGED		= (1 << 28),
	/* write protect */
	IDE_DFLAG_WP			= (1 << 29),
	IDE_DFLAG_FORMAT_IN_PROGRESS	= (1 << 30),
};

struct ide_drive_s {
	char		name[4];	/* drive name, such as "hda" */
        char            driver_req[10];	/* requests specific driver */

	struct request_queue	*queue;	/* request queue */

	struct request		*rq;	/* current request */
	void		*driver_data;	/* extra driver data */
	u16			*id;	/* identification info */
#ifdef CONFIG_IDE_PROC_FS
	struct proc_dir_entry *proc;	/* /proc/ide/ directory entry */
	const struct ide_proc_devset *settings; /* /proc/ide/ drive settings */
#endif
	struct hwif_s		*hwif;	/* actually (ide_hwif_t *) */

	const struct ide_disk_ops *disk_ops;

	unsigned long dev_flags;

	unsigned long sleep;		/* sleep until this time */
	unsigned long timeout;		/* max time to wait for irq */

	special_t	special;	/* special action flags */

	u8	select;			/* basic drive/head select reg value */
	u8	retry_pio;		/* retrying dma capable host in pio */
	u8	waiting_for_dma;	/* dma currently in progress */
	u8	dma;			/* atapi dma flag */

        u8	quirk_list;	/* considered quirky, set for a specific host */
        u8	init_speed;	/* transfer rate set at boot */
        u8	current_speed;	/* current transfer rate set */
	u8	desired_speed;	/* desired transfer rate set */
        u8	dn;		/* now wide spread use */
	u8	acoustic;	/* acoustic management */
	u8	media;		/* disk, cdrom, tape, floppy, ... */
	u8	ready_stat;	/* min status value for drive ready */
	u8	mult_count;	/* current multiple sector setting */
	u8	mult_req;	/* requested multiple sector setting */
	u8	io_32bit;	/* 0=16-bit, 1=32-bit, 2/3=32bit+sync */
	u8	bad_wstat;	/* used for ignoring ATA_DF */
	u8	head;		/* "real" number of heads */
	u8	sect;		/* "real" sectors per track */
	u8	bios_head;	/* BIOS/fdisk/LILO number of heads */
	u8	bios_sect;	/* BIOS/fdisk/LILO sectors per track */

	/* delay this long before sending packet command */
	u8 pc_delay;

	unsigned int	bios_cyl;	/* BIOS/fdisk/LILO number of cyls */
	unsigned int	cyl;		/* "real" number of cyls */
	unsigned int	drive_data;	/* used by set_pio_mode/dev_select() */
	unsigned int	failures;	/* current failure count */
	unsigned int	max_failures;	/* maximum allowed failure count */
	u64		probed_capacity;/* initial reported media capacity (ide-cd only currently) */

	u64		capacity64;	/* total number of sectors */

	int		lun;		/* logical unit */
	int		crc_count;	/* crc counter to reduce drive speed */

	unsigned long	debug_mask;	/* debugging levels switch */

#ifdef CONFIG_BLK_DEV_IDEACPI
	struct ide_acpi_drive_link *acpidata;
#endif
	struct list_head list;
	struct device	gendev;
	struct completion gendev_rel_comp;	/* to deal with device release() */

	/* current packet command */
	struct ide_atapi_pc *pc;

	/* last failed packet command */
	struct ide_atapi_pc *failed_pc;

	/* callback for packet commands */
	int  (*pc_callback)(struct ide_drive_s *, int);

	void (*pc_update_buffers)(struct ide_drive_s *, struct ide_atapi_pc *);
	int  (*pc_io_buffers)(struct ide_drive_s *, struct ide_atapi_pc *,
			      unsigned int, int);

	ide_startstop_t (*irq_handler)(struct ide_drive_s *);

	unsigned long atapi_flags;

	struct ide_atapi_pc request_sense_pc;
	struct request request_sense_rq;
};

typedef struct ide_drive_s ide_drive_t;

#define to_ide_device(dev)		container_of(dev, ide_drive_t, gendev)

#define to_ide_drv(obj, cont_type)	\
	container_of(obj, struct cont_type, dev)

#define ide_drv_g(disk, cont_type)	\
	container_of((disk)->private_data, struct cont_type, driver)

struct ide_port_info;

struct ide_tp_ops {
	void	(*exec_command)(struct hwif_s *, u8);
	u8	(*read_status)(struct hwif_s *);
	u8	(*read_altstatus)(struct hwif_s *);
	void	(*write_devctl)(struct hwif_s *, u8);

	void	(*dev_select)(ide_drive_t *);
	void	(*tf_load)(ide_drive_t *, struct ide_cmd *);
	void	(*tf_read)(ide_drive_t *, struct ide_cmd *);

	void	(*input_data)(ide_drive_t *, struct ide_cmd *,
			      void *, unsigned int);
	void	(*output_data)(ide_drive_t *, struct ide_cmd *,
			       void *, unsigned int);
};

extern const struct ide_tp_ops default_tp_ops;

/**
 * struct ide_port_ops - IDE port operations
 *
 * @init_dev:		host specific initialization of a device
 * @set_pio_mode:	routine to program host for PIO mode
 * @set_dma_mode:	routine to program host for DMA mode
 * @reset_poll:		chipset polling based on hba specifics
 * @pre_reset:		chipset specific changes to default for device-hba resets
 * @resetproc:		routine to reset controller after a disk reset
 * @maskproc:		special host masking for drive selection
 * @quirkproc:		check host's drive quirk list
 * @clear_irq:		clear IRQ
 *
 * @mdma_filter:	filter MDMA modes
 * @udma_filter:	filter UDMA modes
 *
 * @cable_detect:	detect cable type
 */
struct ide_port_ops {
	void	(*init_dev)(ide_drive_t *);
	void	(*set_pio_mode)(ide_drive_t *, const u8);
	void	(*set_dma_mode)(ide_drive_t *, const u8);
	int	(*reset_poll)(ide_drive_t *);
	void	(*pre_reset)(ide_drive_t *);
	void	(*resetproc)(ide_drive_t *);
	void	(*maskproc)(ide_drive_t *, int);
	void	(*quirkproc)(ide_drive_t *);
	void	(*clear_irq)(ide_drive_t *);

	u8	(*mdma_filter)(ide_drive_t *);
	u8	(*udma_filter)(ide_drive_t *);

	u8	(*cable_detect)(struct hwif_s *);
};

struct ide_dma_ops {
	void	(*dma_host_set)(struct ide_drive_s *, int);
	int	(*dma_setup)(struct ide_drive_s *, struct ide_cmd *);
	void	(*dma_start)(struct ide_drive_s *);
	int	(*dma_end)(struct ide_drive_s *);
	int	(*dma_test_irq)(struct ide_drive_s *);
	void	(*dma_lost_irq)(struct ide_drive_s *);
	/* below ones are optional */
	int	(*dma_check)(struct ide_drive_s *, struct ide_cmd *);
	int	(*dma_timer_expiry)(struct ide_drive_s *);
	void	(*dma_clear)(struct ide_drive_s *);
	/*
	 * The following method is optional and only required to be
	 * implemented for the SFF-8038i compatible controllers.
	 */
	u8	(*dma_sff_read_status)(struct hwif_s *);
};

struct ide_host;

typedef struct hwif_s {
	struct hwif_s *mate;		/* other hwif from same PCI chip */
	struct proc_dir_entry *proc;	/* /proc/ide/ directory entry */

	struct ide_host *host;

	char name[6];			/* name of interface, eg. "ide0" */

	struct ide_io_ports	io_ports;

	unsigned long	sata_scr[SATA_NR_PORTS];

	ide_drive_t	*devices[MAX_DRIVES + 1];

	u8 major;	/* our major number */
	u8 index;	/* 0 for ide0; 1 for ide1; ... */
	u8 channel;	/* for dual-port chips: 0=primary, 1=secondary */

	u32 host_flags;

	u8 pio_mask;

	u8 ultra_mask;
	u8 mwdma_mask;
	u8 swdma_mask;

	u8 cbl;		/* cable type */

	hwif_chipset_t chipset;	/* sub-module for tuning.. */

	struct device *dev;

	ide_ack_intr_t *ack_intr;

	void (*rw_disk)(ide_drive_t *, struct request *);

	const struct ide_tp_ops		*tp_ops;
	const struct ide_port_ops	*port_ops;
	const struct ide_dma_ops	*dma_ops;

	/* dma physical region descriptor table (cpu view) */
	unsigned int	*dmatable_cpu;
	/* dma physical region descriptor table (dma view) */
	dma_addr_t	dmatable_dma;

	/* maximum number of PRD table entries */
	int prd_max_nents;
	/* PRD entry size in bytes */
	int prd_ent_size;

	/* Scatter-gather list used to build the above */
	struct scatterlist *sg_table;
	int sg_max_nents;		/* Maximum number of entries in it */

	struct ide_cmd cmd;		/* current command */

	int		rqsize;		/* max sectors per request */
	int		irq;		/* our irq number */

	unsigned long	dma_base;	/* base addr for dma ports */

	unsigned long	config_data;	/* for use by chipset-specific code */
	unsigned long	select_data;	/* for use by chipset-specific code */

	unsigned long	extra_base;	/* extra addr for dma ports */
	unsigned	extra_ports;	/* number of extra dma ports */

	unsigned	present    : 1;	/* this interface exists */
	unsigned	busy	   : 1; /* serializes devices on a port */

	struct device		gendev;
	struct device		*portdev;

	struct completion gendev_rel_comp; /* To deal with device release() */

	void		*hwif_data;	/* extra hwif data */

#ifdef CONFIG_BLK_DEV_IDEACPI
	struct ide_acpi_hwif_link *acpidata;
#endif

	/* IRQ handler, if active */
	ide_startstop_t	(*handler)(ide_drive_t *);

	/* BOOL: polling active & poll_timeout field valid */
	unsigned int polling : 1;

	/* current drive */
	ide_drive_t *cur_dev;

	/* current request */
	struct request *rq;

	/* failsafe timer */
	struct timer_list timer;
	/* timeout value during long polls */
	unsigned long poll_timeout;
	/* queried upon timeouts */
	int (*expiry)(ide_drive_t *);

	int req_gen;
	int req_gen_timer;

	spinlock_t lock;
} ____cacheline_internodealigned_in_smp ide_hwif_t;

#define MAX_HOST_PORTS 4

struct ide_host {
	ide_hwif_t	*ports[MAX_HOST_PORTS + 1];
	unsigned int	n_ports;
	struct device	*dev[2];

	int		(*init_chipset)(struct pci_dev *);

	void		(*get_lock)(irq_handler_t, void *);
	void		(*release_lock)(void);

	irq_handler_t	irq_handler;

	unsigned long	host_flags;

	int		irq_flags;

	void		*host_priv;
	ide_hwif_t	*cur_port;	/* for hosts requiring serialization */

	/* used for hosts requiring serialization */
	volatile unsigned long	host_busy;
};

#define IDE_HOST_BUSY 0

/*
 *  internal ide interrupt handler type
 */
typedef ide_startstop_t (ide_handler_t)(ide_drive_t *);
typedef int (ide_expiry_t)(ide_drive_t *);

/* used by ide-cd, ide-floppy, etc. */
typedef void (xfer_func_t)(ide_drive_t *, struct ide_cmd *, void *, unsigned);

extern struct mutex ide_setting_mtx;

/*
 * configurable drive settings
 */

#define DS_SYNC	(1 << 0)

struct ide_devset {
	int		(*get)(ide_drive_t *);
	int		(*set)(ide_drive_t *, int);
	unsigned int	flags;
};

#define __DEVSET(_flags, _get, _set) { \
	.flags	= _flags, \
	.get	= _get,	\
	.set	= _set,	\
}

#define ide_devset_get(name, field) \
static int get_##name(ide_drive_t *drive) \
{ \
	return drive->field; \
}

#define ide_devset_set(name, field) \
static int set_##name(ide_drive_t *drive, int arg) \
{ \
	drive->field = arg; \
	return 0; \
}

#define ide_devset_get_flag(name, flag) \
static int get_##name(ide_drive_t *drive) \
{ \
	return !!(drive->dev_flags & flag); \
}

#define ide_devset_set_flag(name, flag) \
static int set_##name(ide_drive_t *drive, int arg) \
{ \
	if (arg) \
		drive->dev_flags |= flag; \
	else \
		drive->dev_flags &= ~flag; \
	return 0; \
}

#define __IDE_DEVSET(_name, _flags, _get, _set) \
const struct ide_devset ide_devset_##_name = \
	__DEVSET(_flags, _get, _set)

#define IDE_DEVSET(_name, _flags, _get, _set) \
static __IDE_DEVSET(_name, _flags, _get, _set)

#define ide_devset_rw(_name, _func) \
IDE_DEVSET(_name, 0, get_##_func, set_##_func)

#define ide_devset_w(_name, _func) \
IDE_DEVSET(_name, 0, NULL, set_##_func)

#define ide_ext_devset_rw(_name, _func) \
__IDE_DEVSET(_name, 0, get_##_func, set_##_func)

#define ide_ext_devset_rw_sync(_name, _func) \
__IDE_DEVSET(_name, DS_SYNC, get_##_func, set_##_func)

#define ide_decl_devset(_name) \
extern const struct ide_devset ide_devset_##_name

ide_decl_devset(io_32bit);
ide_decl_devset(keepsettings);
ide_decl_devset(pio_mode);
ide_decl_devset(unmaskirq);
ide_decl_devset(using_dma);

#ifdef CONFIG_IDE_PROC_FS
/*
 * /proc/ide interface
 */

#define ide_devset_rw_field(_name, _field) \
ide_devset_get(_name, _field); \
ide_devset_set(_name, _field); \
IDE_DEVSET(_name, DS_SYNC, get_##_name, set_##_name)

#define ide_devset_rw_flag(_name, _field) \
ide_devset_get_flag(_name, _field); \
ide_devset_set_flag(_name, _field); \
IDE_DEVSET(_name, DS_SYNC, get_##_name, set_##_name)

struct ide_proc_devset {
	const char		*name;
	const struct ide_devset	*setting;
	int			min, max;
	int			(*mulf)(ide_drive_t *);
	int			(*divf)(ide_drive_t *);
};

#define __IDE_PROC_DEVSET(_name, _min, _max, _mulf, _divf) { \
	.name = __stringify(_name), \
	.setting = &ide_devset_##_name, \
	.min = _min, \
	.max = _max, \
	.mulf = _mulf, \
	.divf = _divf, \
}

#define IDE_PROC_DEVSET(_name, _min, _max) \
__IDE_PROC_DEVSET(_name, _min, _max, NULL, NULL)

typedef struct {
	const char	*name;
	mode_t		mode;
	read_proc_t	*read_proc;
	write_proc_t	*write_proc;
} ide_proc_entry_t;

void proc_ide_create(void);
void proc_ide_destroy(void);
void ide_proc_register_port(ide_hwif_t *);
void ide_proc_port_register_devices(ide_hwif_t *);
void ide_proc_unregister_device(ide_drive_t *);
void ide_proc_unregister_port(ide_hwif_t *);
void ide_proc_register_driver(ide_drive_t *, struct ide_driver *);
void ide_proc_unregister_driver(ide_drive_t *, struct ide_driver *);

read_proc_t proc_ide_read_capacity;
read_proc_t proc_ide_read_geometry;

/*
 * Standard exit stuff:
 */
#define PROC_IDE_READ_RETURN(page,start,off,count,eof,len) \
{					\
	len -= off;			\
	if (len < count) {		\
		*eof = 1;		\
		if (len <= 0)		\
			return 0;	\
	} else				\
		len = count;		\
	*start = page + off;		\
	return len;			\
}
#else
static inline void proc_ide_create(void) { ; }
static inline void proc_ide_destroy(void) { ; }
static inline void ide_proc_register_port(ide_hwif_t *hwif) { ; }
static inline void ide_proc_port_register_devices(ide_hwif_t *hwif) { ; }
static inline void ide_proc_unregister_device(ide_drive_t *drive) { ; }
static inline void ide_proc_unregister_port(ide_hwif_t *hwif) { ; }
static inline void ide_proc_register_driver(ide_drive_t *drive,
					    struct ide_driver *driver) { ; }
static inline void ide_proc_unregister_driver(ide_drive_t *drive,
					      struct ide_driver *driver) { ; }
#define PROC_IDE_READ_RETURN(page,start,off,count,eof,len) return 0;
#endif

enum {
	/* enter/exit functions */
	IDE_DBG_FUNC =			(1 << 0),
	/* sense key/asc handling */
	IDE_DBG_SENSE =			(1 << 1),
	/* packet commands handling */
	IDE_DBG_PC =			(1 << 2),
	/* request handling */
	IDE_DBG_RQ =			(1 << 3),
	/* driver probing/setup */
	IDE_DBG_PROBE =			(1 << 4),
};

/* DRV_NAME has to be defined in the driver before using the macro below */
#define __ide_debug_log(lvl, fmt, args...)				\
{									\
	if (unlikely(drive->debug_mask & lvl))				\
		printk(KERN_INFO DRV_NAME ": %s: " fmt "\n",		\
					  __func__, ## args);		\
}

/*
 * Power Management state machine (rq->pm->pm_step).
 *
 * For each step, the core calls ide_start_power_step() first.
 * This can return:
 *	- ide_stopped :	In this case, the core calls us back again unless
 *			step have been set to ide_power_state_completed.
 *	- ide_started :	In this case, the channel is left busy until an
 *			async event (interrupt) occurs.
 * Typically, ide_start_power_step() will issue a taskfile request with
 * do_rw_taskfile().
 *
 * Upon reception of the interrupt, the core will call ide_complete_power_step()
 * with the error code if any. This routine should update the step value
 * and return. It should not start a new request. The core will call
 * ide_start_power_step() for the new step value, unless step have been
 * set to IDE_PM_COMPLETED.
 */
enum {
	IDE_PM_START_SUSPEND,
	IDE_PM_FLUSH_CACHE	= IDE_PM_START_SUSPEND,
	IDE_PM_STANDBY,

	IDE_PM_START_RESUME,
	IDE_PM_RESTORE_PIO	= IDE_PM_START_RESUME,
	IDE_PM_IDLE,
	IDE_PM_RESTORE_DMA,

	IDE_PM_COMPLETED,
};

int generic_ide_suspend(struct device *, pm_message_t);
int generic_ide_resume(struct device *);

void ide_complete_power_step(ide_drive_t *, struct request *);
ide_startstop_t ide_start_power_step(ide_drive_t *, struct request *);
void ide_complete_pm_rq(ide_drive_t *, struct request *);
void ide_check_pm_state(ide_drive_t *, struct request *);

/*
 * Subdrivers support.
 *
 * The gendriver.owner field should be set to the module owner of this driver.
 * The gendriver.name field should be set to the name of this driver
 */
struct ide_driver {
	const char			*version;
	ide_startstop_t	(*do_request)(ide_drive_t *, struct request *, sector_t);
	struct device_driver	gen_driver;
	int		(*probe)(ide_drive_t *);
	void		(*remove)(ide_drive_t *);
	void		(*resume)(ide_drive_t *);
	void		(*shutdown)(ide_drive_t *);
#ifdef CONFIG_IDE_PROC_FS
	ide_proc_entry_t *		(*proc_entries)(ide_drive_t *);
	const struct ide_proc_devset *	(*proc_devsets)(ide_drive_t *);
#endif
};

#define to_ide_driver(drv) container_of(drv, struct ide_driver, gen_driver)

int ide_device_get(ide_drive_t *);
void ide_device_put(ide_drive_t *);

struct ide_ioctl_devset {
	unsigned int	get_ioctl;
	unsigned int	set_ioctl;
	const struct ide_devset *setting;
};

int ide_setting_ioctl(ide_drive_t *, struct block_device *, unsigned int,
		      unsigned long, const struct ide_ioctl_devset *);

int generic_ide_ioctl(ide_drive_t *, struct block_device *, unsigned, unsigned long);

extern int ide_vlb_clk;
extern int ide_pci_clk;

unsigned int ide_rq_bytes(struct request *);
int ide_end_rq(ide_drive_t *, struct request *, int, unsigned int);
void ide_kill_rq(ide_drive_t *, struct request *);

void __ide_set_handler(ide_drive_t *, ide_handler_t *, unsigned int);
void ide_set_handler(ide_drive_t *, ide_handler_t *, unsigned int);

void ide_execute_command(ide_drive_t *, struct ide_cmd *, ide_handler_t *,
			 unsigned int);

void ide_pad_transfer(ide_drive_t *, int, int);

ide_startstop_t ide_error(ide_drive_t *, const char *, u8);

void ide_fix_driveid(u16 *);

extern void ide_fixstring(u8 *, const int, const int);

int ide_busy_sleep(ide_hwif_t *, unsigned long, int);

int ide_wait_stat(ide_startstop_t *, ide_drive_t *, u8, u8, unsigned long);

ide_startstop_t ide_do_park_unpark(ide_drive_t *, struct request *);
ide_startstop_t ide_do_devset(ide_drive_t *, struct request *);

extern ide_startstop_t ide_do_reset (ide_drive_t *);

extern int ide_devset_execute(ide_drive_t *drive,
			      const struct ide_devset *setting, int arg);

void ide_complete_cmd(ide_drive_t *, struct ide_cmd *, u8, u8);
int ide_complete_rq(ide_drive_t *, int, unsigned int);

void ide_tf_dump(const char *, struct ide_cmd *);

void ide_exec_command(ide_hwif_t *, u8);
u8 ide_read_status(ide_hwif_t *);
u8 ide_read_altstatus(ide_hwif_t *);
void ide_write_devctl(ide_hwif_t *, u8);

void ide_dev_select(ide_drive_t *);
void ide_tf_load(ide_drive_t *, struct ide_cmd *);
void ide_tf_read(ide_drive_t *, struct ide_cmd *);

void ide_input_data(ide_drive_t *, struct ide_cmd *, void *, unsigned int);
void ide_output_data(ide_drive_t *, struct ide_cmd *, void *, unsigned int);

void SELECT_MASK(ide_drive_t *, int);

u8 ide_read_error(ide_drive_t *);
void ide_read_bcount_and_ireason(ide_drive_t *, u16 *, u8 *);

int ide_check_atapi_device(ide_drive_t *, const char *);

void ide_init_pc(struct ide_atapi_pc *);

/* Disk head parking */
extern wait_queue_head_t ide_park_wq;
ssize_t ide_park_show(struct device *dev, struct device_attribute *attr,
		      char *buf);
ssize_t ide_park_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t len);

/*
 * Special requests for ide-tape block device strategy routine.
 *
 * In order to service a character device command, we add special requests to
 * the tail of our block device request queue and wait for their completion.
 */
enum {
	REQ_IDETAPE_PC1		= (1 << 0), /* packet command (first stage) */
	REQ_IDETAPE_PC2		= (1 << 1), /* packet command (second stage) */
	REQ_IDETAPE_READ	= (1 << 2),
	REQ_IDETAPE_WRITE	= (1 << 3),
};

int ide_queue_pc_tail(ide_drive_t *, struct gendisk *, struct ide_atapi_pc *);

int ide_do_test_unit_ready(ide_drive_t *, struct gendisk *);
int ide_do_start_stop(ide_drive_t *, struct gendisk *, int);
int ide_set_media_lock(ide_drive_t *, struct gendisk *, int);
void ide_create_request_sense_cmd(ide_drive_t *, struct ide_atapi_pc *);
void ide_retry_pc(ide_drive_t *, struct gendisk *);

int ide_cd_expiry(ide_drive_t *);

int ide_cd_get_xferlen(struct request *);

ide_startstop_t ide_issue_pc(ide_drive_t *, struct ide_cmd *);

ide_startstop_t do_rw_taskfile(ide_drive_t *, struct ide_cmd *);

void ide_pio_bytes(ide_drive_t *, struct ide_cmd *, unsigned int, unsigned int);

void ide_finish_cmd(ide_drive_t *, struct ide_cmd *, u8);

int ide_raw_taskfile(ide_drive_t *, struct ide_cmd *, u8 *, u16);
int ide_no_data_taskfile(ide_drive_t *, struct ide_cmd *);

int ide_taskfile_ioctl(ide_drive_t *, unsigned long);

int ide_dev_read_id(ide_drive_t *, u8, u16 *);

extern int ide_driveid_update(ide_drive_t *);
extern int ide_config_drive_speed(ide_drive_t *, u8);
extern u8 eighty_ninty_three (ide_drive_t *);
extern int taskfile_lib_get_identify(ide_drive_t *drive, u8 *);

extern int ide_wait_not_busy(ide_hwif_t *hwif, unsigned long timeout);

extern void ide_stall_queue(ide_drive_t *drive, unsigned long timeout);

extern void ide_timer_expiry(unsigned long);
extern irqreturn_t ide_intr(int irq, void *dev_id);
extern void do_ide_request(struct request_queue *);

void ide_init_disk(struct gendisk *, ide_drive_t *);

#ifdef CONFIG_IDEPCI_PCIBUS_ORDER
extern int __ide_pci_register_driver(struct pci_driver *driver, struct module *owner, const char *mod_name);
#define ide_pci_register_driver(d) __ide_pci_register_driver(d, THIS_MODULE, KBUILD_MODNAME)
#else
#define ide_pci_register_driver(d) pci_register_driver(d)
#endif

static inline int ide_pci_is_in_compatibility_mode(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE && (dev->class & 5) != 5)
		return 1;
	return 0;
}

void ide_pci_setup_ports(struct pci_dev *, const struct ide_port_info *,
			 hw_regs_t *, hw_regs_t **);
void ide_setup_pci_noise(struct pci_dev *, const struct ide_port_info *);

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
int ide_pci_set_master(struct pci_dev *, const char *);
unsigned long ide_pci_dma_base(ide_hwif_t *, const struct ide_port_info *);
int ide_pci_check_simplex(ide_hwif_t *, const struct ide_port_info *);
int ide_hwif_setup_dma(ide_hwif_t *, const struct ide_port_info *);
#else
static inline int ide_hwif_setup_dma(ide_hwif_t *hwif,
				     const struct ide_port_info *d)
{
	return -EINVAL;
}
#endif

struct ide_pci_enablebit {
	u8	reg;	/* byte pci reg holding the enable-bit */
	u8	mask;	/* mask to isolate the enable-bit */
	u8	val;	/* value of masked reg when "enabled" */
};

enum {
	/* Uses ISA control ports not PCI ones. */
	IDE_HFLAG_ISA_PORTS		= (1 << 0),
	/* single port device */
	IDE_HFLAG_SINGLE		= (1 << 1),
	/* don't use legacy PIO blacklist */
	IDE_HFLAG_PIO_NO_BLACKLIST	= (1 << 2),
	/* set for the second port of QD65xx */
	IDE_HFLAG_QD_2ND_PORT		= (1 << 3),
	/* use PIO8/9 for prefetch off/on */
	IDE_HFLAG_ABUSE_PREFETCH	= (1 << 4),
	/* use PIO6/7 for fast-devsel off/on */
	IDE_HFLAG_ABUSE_FAST_DEVSEL	= (1 << 5),
	/* use 100-102 and 200-202 PIO values to set DMA modes */
	IDE_HFLAG_ABUSE_DMA_MODES	= (1 << 6),
	/*
	 * keep DMA setting when programming PIO mode, may be used only
	 * for hosts which have separate PIO and DMA timings (ie. PMAC)
	 */
	IDE_HFLAG_SET_PIO_MODE_KEEP_DMA	= (1 << 7),
	/* program host for the transfer mode after programming device */
	IDE_HFLAG_POST_SET_MODE		= (1 << 8),
	/* don't program host/device for the transfer mode ("smart" hosts) */
	IDE_HFLAG_NO_SET_MODE		= (1 << 9),
	/* trust BIOS for programming chipset/device for DMA */
	IDE_HFLAG_TRUST_BIOS_FOR_DMA	= (1 << 10),
	/* host is CS5510/CS5520 */
	IDE_HFLAG_CS5520		= (1 << 11),
	/* ATAPI DMA is unsupported */
	IDE_HFLAG_NO_ATAPI_DMA		= (1 << 12),
	/* set if host is a "non-bootable" controller */
	IDE_HFLAG_NON_BOOTABLE		= (1 << 13),
	/* host doesn't support DMA */
	IDE_HFLAG_NO_DMA		= (1 << 14),
	/* check if host is PCI IDE device before allowing DMA */
	IDE_HFLAG_NO_AUTODMA		= (1 << 15),
	/* host uses MMIO */
	IDE_HFLAG_MMIO			= (1 << 16),
	/* no LBA48 */
	IDE_HFLAG_NO_LBA48		= (1 << 17),
	/* no LBA48 DMA */
	IDE_HFLAG_NO_LBA48_DMA		= (1 << 18),
	/* data FIFO is cleared by an error */
	IDE_HFLAG_ERROR_STOPS_FIFO	= (1 << 19),
	/* serialize ports */
	IDE_HFLAG_SERIALIZE		= (1 << 20),
	/* host is DTC2278 */
	IDE_HFLAG_DTC2278		= (1 << 21),
	/* 4 devices on a single set of I/O ports */
	IDE_HFLAG_4DRIVES		= (1 << 22),
	/* host is TRM290 */
	IDE_HFLAG_TRM290		= (1 << 23),
	/* use 32-bit I/O ops */
	IDE_HFLAG_IO_32BIT		= (1 << 24),
	/* unmask IRQs */
	IDE_HFLAG_UNMASK_IRQS		= (1 << 25),
	IDE_HFLAG_BROKEN_ALTSTATUS	= (1 << 26),
	/* serialize ports if DMA is possible (for sl82c105) */
	IDE_HFLAG_SERIALIZE_DMA		= (1 << 27),
	/* force host out of "simplex" mode */
	IDE_HFLAG_CLEAR_SIMPLEX		= (1 << 28),
	/* DSC overlap is unsupported */
	IDE_HFLAG_NO_DSC		= (1 << 29),
	/* never use 32-bit I/O ops */
	IDE_HFLAG_NO_IO_32BIT		= (1 << 30),
	/* never unmask IRQs */
	IDE_HFLAG_NO_UNMASK_IRQS	= (1 << 31),
};

#ifdef CONFIG_BLK_DEV_OFFBOARD
# define IDE_HFLAG_OFF_BOARD	0
#else
# define IDE_HFLAG_OFF_BOARD	IDE_HFLAG_NON_BOOTABLE
#endif

struct ide_port_info {
	char			*name;

	int			(*init_chipset)(struct pci_dev *);

	void			(*get_lock)(irq_handler_t, void *);
	void			(*release_lock)(void);

	void			(*init_iops)(ide_hwif_t *);
	void                    (*init_hwif)(ide_hwif_t *);
	int			(*init_dma)(ide_hwif_t *,
					    const struct ide_port_info *);

	const struct ide_tp_ops		*tp_ops;
	const struct ide_port_ops	*port_ops;
	const struct ide_dma_ops	*dma_ops;

	struct ide_pci_enablebit	enablebits[2];

	hwif_chipset_t		chipset;

	u16			max_sectors;	/* if < than the default one */

	u32			host_flags;

	int			irq_flags;

	u8			pio_mask;
	u8			swdma_mask;
	u8			mwdma_mask;
	u8			udma_mask;
};

int ide_pci_init_one(struct pci_dev *, const struct ide_port_info *, void *);
int ide_pci_init_two(struct pci_dev *, struct pci_dev *,
		     const struct ide_port_info *, void *);
void ide_pci_remove(struct pci_dev *);

#ifdef CONFIG_PM
int ide_pci_suspend(struct pci_dev *, pm_message_t);
int ide_pci_resume(struct pci_dev *);
#else
#define ide_pci_suspend NULL
#define ide_pci_resume NULL
#endif

void ide_map_sg(ide_drive_t *, struct ide_cmd *);
void ide_init_sg_cmd(struct ide_cmd *, unsigned int);

#define BAD_DMA_DRIVE		0
#define GOOD_DMA_DRIVE		1

struct drive_list_entry {
	const char *id_model;
	const char *id_firmware;
};

int ide_in_drive_list(u16 *, const struct drive_list_entry *);

#ifdef CONFIG_BLK_DEV_IDEDMA
int ide_dma_good_drive(ide_drive_t *);
int __ide_dma_bad_drive(ide_drive_t *);
int ide_id_dma_bug(ide_drive_t *);

u8 ide_find_dma_mode(ide_drive_t *, u8);

static inline u8 ide_max_dma_mode(ide_drive_t *drive)
{
	return ide_find_dma_mode(drive, XFER_UDMA_6);
}

void ide_dma_off_quietly(ide_drive_t *);
void ide_dma_off(ide_drive_t *);
void ide_dma_on(ide_drive_t *);
int ide_set_dma(ide_drive_t *);
void ide_check_dma_crc(ide_drive_t *);
ide_startstop_t ide_dma_intr(ide_drive_t *);

int ide_allocate_dma_engine(ide_hwif_t *);
void ide_release_dma_engine(ide_hwif_t *);

int ide_dma_prepare(ide_drive_t *, struct ide_cmd *);
void ide_dma_unmap_sg(ide_drive_t *, struct ide_cmd *);

#ifdef CONFIG_BLK_DEV_IDEDMA_SFF
int config_drive_for_dma(ide_drive_t *);
int ide_build_dmatable(ide_drive_t *, struct ide_cmd *);
void ide_dma_host_set(ide_drive_t *, int);
int ide_dma_setup(ide_drive_t *, struct ide_cmd *);
extern void ide_dma_start(ide_drive_t *);
int ide_dma_end(ide_drive_t *);
int ide_dma_test_irq(ide_drive_t *);
int ide_dma_sff_timer_expiry(ide_drive_t *);
u8 ide_dma_sff_read_status(ide_hwif_t *);
extern const struct ide_dma_ops sff_dma_ops;
#else
static inline int config_drive_for_dma(ide_drive_t *drive) { return 0; }
#endif /* CONFIG_BLK_DEV_IDEDMA_SFF */

void ide_dma_lost_irq(ide_drive_t *);
ide_startstop_t ide_dma_timeout_retry(ide_drive_t *, int);

#else
static inline int ide_id_dma_bug(ide_drive_t *drive) { return 0; }
static inline u8 ide_find_dma_mode(ide_drive_t *drive, u8 speed) { return 0; }
static inline u8 ide_max_dma_mode(ide_drive_t *drive) { return 0; }
static inline void ide_dma_off_quietly(ide_drive_t *drive) { ; }
static inline void ide_dma_off(ide_drive_t *drive) { ; }
static inline void ide_dma_on(ide_drive_t *drive) { ; }
static inline void ide_dma_verbose(ide_drive_t *drive) { ; }
static inline int ide_set_dma(ide_drive_t *drive) { return 1; }
static inline void ide_check_dma_crc(ide_drive_t *drive) { ; }
static inline ide_startstop_t ide_dma_intr(ide_drive_t *drive) { return ide_stopped; }
static inline ide_startstop_t ide_dma_timeout_retry(ide_drive_t *drive, int error) { return ide_stopped; }
static inline void ide_release_dma_engine(ide_hwif_t *hwif) { ; }
static inline int ide_dma_prepare(ide_drive_t *drive,
				  struct ide_cmd *cmd) { return 1; }
static inline void ide_dma_unmap_sg(ide_drive_t *drive,
				    struct ide_cmd *cmd) { ; }
#endif /* CONFIG_BLK_DEV_IDEDMA */

#ifdef CONFIG_BLK_DEV_IDEACPI
int ide_acpi_init(void);
extern int ide_acpi_exec_tfs(ide_drive_t *drive);
extern void ide_acpi_get_timing(ide_hwif_t *hwif);
extern void ide_acpi_push_timing(ide_hwif_t *hwif);
void ide_acpi_init_port(ide_hwif_t *);
void ide_acpi_port_init_devices(ide_hwif_t *);
extern void ide_acpi_set_state(ide_hwif_t *hwif, int on);
#else
static inline int ide_acpi_init(void) { return 0; }
static inline int ide_acpi_exec_tfs(ide_drive_t *drive) { return 0; }
static inline void ide_acpi_get_timing(ide_hwif_t *hwif) { ; }
static inline void ide_acpi_push_timing(ide_hwif_t *hwif) { ; }
static inline void ide_acpi_init_port(ide_hwif_t *hwif) { ; }
static inline void ide_acpi_port_init_devices(ide_hwif_t *hwif) { ; }
static inline void ide_acpi_set_state(ide_hwif_t *hwif, int on) {}
#endif

void ide_register_region(struct gendisk *);
void ide_unregister_region(struct gendisk *);

void ide_undecoded_slave(ide_drive_t *);

void ide_port_apply_params(ide_hwif_t *);
int ide_sysfs_register_port(ide_hwif_t *);

struct ide_host *ide_host_alloc(const struct ide_port_info *, hw_regs_t **);
void ide_host_free(struct ide_host *);
int ide_host_register(struct ide_host *, const struct ide_port_info *,
		      hw_regs_t **);
int ide_host_add(const struct ide_port_info *, hw_regs_t **,
		 struct ide_host **);
void ide_host_remove(struct ide_host *);
int ide_legacy_device_add(const struct ide_port_info *, unsigned long);
void ide_port_unregister_devices(ide_hwif_t *);
void ide_port_scan(ide_hwif_t *);

static inline void *ide_get_hwifdata (ide_hwif_t * hwif)
{
	return hwif->hwif_data;
}

static inline void ide_set_hwifdata (ide_hwif_t * hwif, void *data)
{
	hwif->hwif_data = data;
}

extern void ide_toggle_bounce(ide_drive_t *drive, int on);

u64 ide_get_lba_addr(struct ide_cmd *, int);
u8 ide_dump_status(ide_drive_t *, const char *, u8);

struct ide_timing {
	u8  mode;
	u8  setup;	/* t1 */
	u16 act8b;	/* t2 for 8-bit io */
	u16 rec8b;	/* t2i for 8-bit io */
	u16 cyc8b;	/* t0 for 8-bit io */
	u16 active;	/* t2 or tD */
	u16 recover;	/* t2i or tK */
	u16 cycle;	/* t0 */
	u16 udma;	/* t2CYCTYP/2 */
};

enum {
	IDE_TIMING_SETUP	= (1 << 0),
	IDE_TIMING_ACT8B	= (1 << 1),
	IDE_TIMING_REC8B	= (1 << 2),
	IDE_TIMING_CYC8B	= (1 << 3),
	IDE_TIMING_8BIT		= IDE_TIMING_ACT8B | IDE_TIMING_REC8B |
				  IDE_TIMING_CYC8B,
	IDE_TIMING_ACTIVE	= (1 << 4),
	IDE_TIMING_RECOVER	= (1 << 5),
	IDE_TIMING_CYCLE	= (1 << 6),
	IDE_TIMING_UDMA		= (1 << 7),
	IDE_TIMING_ALL		= IDE_TIMING_SETUP | IDE_TIMING_8BIT |
				  IDE_TIMING_ACTIVE | IDE_TIMING_RECOVER |
				  IDE_TIMING_CYCLE | IDE_TIMING_UDMA,
};

struct ide_timing *ide_timing_find_mode(u8);
u16 ide_pio_cycle_time(ide_drive_t *, u8);
void ide_timing_merge(struct ide_timing *, struct ide_timing *,
		      struct ide_timing *, unsigned int);
int ide_timing_compute(ide_drive_t *, u8, struct ide_timing *, int, int);

#ifdef CONFIG_IDE_XFER_MODE
int ide_scan_pio_blacklist(char *);
const char *ide_xfer_verbose(u8);
u8 ide_get_best_pio_mode(ide_drive_t *, u8, u8);
int ide_set_pio_mode(ide_drive_t *, u8);
int ide_set_dma_mode(ide_drive_t *, u8);
void ide_set_pio(ide_drive_t *, u8);
int ide_set_xfer_rate(ide_drive_t *, u8);
#else
static inline void ide_set_pio(ide_drive_t *drive, u8 pio) { ; }
static inline int ide_set_xfer_rate(ide_drive_t *drive, u8 rate) { return -1; }
#endif

static inline void ide_set_max_pio(ide_drive_t *drive)
{
	ide_set_pio(drive, 255);
}

char *ide_media_string(ide_drive_t *);

extern struct device_attribute ide_dev_attrs[];
extern struct bus_type ide_bus_type;
extern struct class *ide_port_class;

static inline void ide_dump_identify(u8 *id)
{
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 2, id, 512, 0);
}

static inline int hwif_to_node(ide_hwif_t *hwif)
{
	return hwif->dev ? dev_to_node(hwif->dev) : -1;
}

static inline ide_drive_t *ide_get_pair_dev(ide_drive_t *drive)
{
	ide_drive_t *peer = drive->hwif->devices[(drive->dn ^ 1) & 1];

	return (peer->dev_flags & IDE_DFLAG_PRESENT) ? peer : NULL;
}

#define ide_port_for_each_dev(i, dev, port) \
	for ((i) = 0; ((dev) = (port)->devices[i]) || (i) < MAX_DRIVES; (i)++)

#define ide_port_for_each_present_dev(i, dev, port) \
	for ((i) = 0; ((dev) = (port)->devices[i]) || (i) < MAX_DRIVES; (i)++) \
		if ((dev)->dev_flags & IDE_DFLAG_PRESENT)

#define ide_host_for_each_port(i, port, host) \
	for ((i) = 0; ((port) = (host)->ports[i]) || (i) < MAX_HOST_PORTS; (i)++)

#endif /* _IDE_H */
