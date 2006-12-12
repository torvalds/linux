#ifndef _IDE_H
#define _IDE_H
/*
 *  linux/include/linux/ide.h
 *
 *  Copyright (C) 1994-2002  Linus Torvalds & authors
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/hdsmart.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/bio.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/completion.h>
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/semaphore.h>

/******************************************************************************
 * IDE driver configuration options (play with these as desired):
 *
 * REALLY_SLOW_IO can be defined in ide.c and ide-cd.c, if necessary
 */
#define INITIAL_MULT_COUNT	0	/* off=0; on=2,4,8,16,32, etc.. */

#ifndef SUPPORT_SLOW_DATA_PORTS		/* 1 to support slow data ports */
#define SUPPORT_SLOW_DATA_PORTS	1	/* 0 to reduce kernel size */
#endif
#ifndef SUPPORT_VLB_SYNC		/* 1 to support weird 32-bit chips */
#define SUPPORT_VLB_SYNC	1	/* 0 to reduce kernel size */
#endif
#ifndef OK_TO_RESET_CONTROLLER		/* 1 needed for good error recovery */
#define OK_TO_RESET_CONTROLLER	1	/* 0 for use with AH2372A/B interface */
#endif

#ifndef DISABLE_IRQ_NOSYNC
#define DISABLE_IRQ_NOSYNC	0
#endif

/*
 * Used to indicate "no IRQ", should be a value that cannot be an IRQ
 * number.
 */
 
#define IDE_NO_IRQ		(-1)

/*
 *  "No user-serviceable parts" beyond this point  :)
 *****************************************************************************/

typedef unsigned char	byte;	/* used everywhere */

/*
 * Probably not wise to fiddle with these
 */
#define ERROR_MAX	8	/* Max read/write errors per sector */
#define ERROR_RESET	3	/* Reset controller every 4th retry */
#define ERROR_RECAL	1	/* Recalibrate every 2nd retry */

/*
 * Tune flags
 */
#define IDE_TUNE_NOAUTO		2
#define IDE_TUNE_AUTO		1
#define IDE_TUNE_DEFAULT	0

/*
 * state flags
 */

#define DMA_PIO_RETRY	1	/* retrying in PIO */

#define HWIF(drive)		((ide_hwif_t *)((drive)->hwif))
#define HWGROUP(drive)		((ide_hwgroup_t *)(HWIF(drive)->hwgroup))

/*
 * Definitions for accessing IDE controller registers
 */
#define IDE_NR_PORTS		(10)

#define IDE_DATA_OFFSET		(0)
#define IDE_ERROR_OFFSET	(1)
#define IDE_NSECTOR_OFFSET	(2)
#define IDE_SECTOR_OFFSET	(3)
#define IDE_LCYL_OFFSET		(4)
#define IDE_HCYL_OFFSET		(5)
#define IDE_SELECT_OFFSET	(6)
#define IDE_STATUS_OFFSET	(7)
#define IDE_CONTROL_OFFSET	(8)
#define IDE_IRQ_OFFSET		(9)

#define IDE_FEATURE_OFFSET	IDE_ERROR_OFFSET
#define IDE_COMMAND_OFFSET	IDE_STATUS_OFFSET

#define IDE_CONTROL_OFFSET_HOB	(7)

#define IDE_DATA_REG		(HWIF(drive)->io_ports[IDE_DATA_OFFSET])
#define IDE_ERROR_REG		(HWIF(drive)->io_ports[IDE_ERROR_OFFSET])
#define IDE_NSECTOR_REG		(HWIF(drive)->io_ports[IDE_NSECTOR_OFFSET])
#define IDE_SECTOR_REG		(HWIF(drive)->io_ports[IDE_SECTOR_OFFSET])
#define IDE_LCYL_REG		(HWIF(drive)->io_ports[IDE_LCYL_OFFSET])
#define IDE_HCYL_REG		(HWIF(drive)->io_ports[IDE_HCYL_OFFSET])
#define IDE_SELECT_REG		(HWIF(drive)->io_ports[IDE_SELECT_OFFSET])
#define IDE_STATUS_REG		(HWIF(drive)->io_ports[IDE_STATUS_OFFSET])
#define IDE_CONTROL_REG		(HWIF(drive)->io_ports[IDE_CONTROL_OFFSET])
#define IDE_IRQ_REG		(HWIF(drive)->io_ports[IDE_IRQ_OFFSET])

#define IDE_FEATURE_REG		IDE_ERROR_REG
#define IDE_COMMAND_REG		IDE_STATUS_REG
#define IDE_ALTSTATUS_REG	IDE_CONTROL_REG
#define IDE_IREASON_REG		IDE_NSECTOR_REG
#define IDE_BCOUNTL_REG		IDE_LCYL_REG
#define IDE_BCOUNTH_REG		IDE_HCYL_REG

#define OK_STAT(stat,good,bad)	(((stat)&((good)|(bad)))==(good))
#define BAD_R_STAT		(BUSY_STAT   | ERR_STAT)
#define BAD_W_STAT		(BAD_R_STAT  | WRERR_STAT)
#define BAD_STAT		(BAD_R_STAT  | DRQ_STAT)
#define DRIVE_READY		(READY_STAT  | SEEK_STAT)
#define DATA_READY		(DRQ_STAT)

#define BAD_CRC			(ABRT_ERR    | ICRC_ERR)

#define SATA_NR_PORTS		(3)	/* 16 possible ?? */

#define SATA_STATUS_OFFSET	(0)
#define SATA_STATUS_REG		(HWIF(drive)->sata_scr[SATA_STATUS_OFFSET])
#define SATA_ERROR_OFFSET	(1)
#define SATA_ERROR_REG		(HWIF(drive)->sata_scr[SATA_ERROR_OFFSET])
#define SATA_CONTROL_OFFSET	(2)
#define SATA_CONTROL_REG	(HWIF(drive)->sata_scr[SATA_CONTROL_OFFSET])

#define SATA_MISC_OFFSET	(0)
#define SATA_MISC_REG		(HWIF(drive)->sata_misc[SATA_MISC_OFFSET])
#define SATA_PHY_OFFSET		(1)
#define SATA_PHY_REG		(HWIF(drive)->sata_misc[SATA_PHY_OFFSET])
#define SATA_IEN_OFFSET		(2)
#define SATA_IEN_REG		(HWIF(drive)->sata_misc[SATA_IEN_OFFSET])

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
#define SECTOR_WORDS	(SECTOR_SIZE / 4)	/* number of 32bit words per sector */
#define IDE_LARGE_SEEK(b1,b2,t)	(((b1) > (b2) + (t)) || ((b2) > (b1) + (t)))

/*
 * Timeouts for various operations:
 */
#define WAIT_DRQ	(HZ/10)		/* 100msec - spec allows up to 20ms */
#define WAIT_READY	(5*HZ)		/* 5sec - some laptops are very slow */
#define WAIT_PIDENTIFY	(10*HZ)	/* 10sec  - should be less than 3ms (?), if all ATAPI CD is closed at boot */
#define WAIT_WORSTCASE	(30*HZ)	/* 30sec  - worst case when spinning up */
#define WAIT_CMD	(10*HZ)	/* 10sec  - maximum wait for an IRQ to happen */
#define WAIT_MIN_SLEEP	(2*HZ/100)	/* 20msec - minimum sleep time */

/*
 * Check for an interrupt and acknowledge the interrupt status
 */
struct hwif_s;
typedef int (ide_ack_intr_t)(struct hwif_s *);

#ifndef NO_DMA
#define NO_DMA  255
#endif

/*
 * hwif_chipset_t is used to keep track of the specific hardware
 * chipset used by each IDE interface, if known.
 */
typedef enum {	ide_unknown,	ide_generic,	ide_pci,
		ide_cmd640,	ide_dtc2278,	ide_ali14xx,
		ide_qd65xx,	ide_umc8672,	ide_ht6560b,
		ide_rz1000,	ide_trm290,
		ide_cmd646,	ide_cy82c693,	ide_4drives,
		ide_pmac,	ide_etrax100,	ide_acorn,
		ide_au1xxx, ide_forced
} hwif_chipset_t;

/*
 * Structure to hold all information about the location of this port
 */
typedef struct hw_regs_s {
	unsigned long	io_ports[IDE_NR_PORTS];	/* task file registers */
	int		irq;			/* our irq number */
	int		dma;			/* our dma entry */
	ide_ack_intr_t	*ack_intr;		/* acknowledge interrupt */
	hwif_chipset_t  chipset;
	struct device	*dev;
} hw_regs_t;

/*
 * Register new hardware with ide
 */
int ide_register_hw(hw_regs_t *hw, struct hwif_s **hwifp);
int ide_register_hw_with_fixup(hw_regs_t *, struct hwif_s **, void (*)(struct hwif_s *));

/*
 * Set up hw_regs_t structure before calling ide_register_hw (optional)
 */
void ide_setup_ports(	hw_regs_t *hw,
			unsigned long base,
			int *offsets,
			unsigned long ctrl,
			unsigned long intr,
			ide_ack_intr_t *ack_intr,
#if 0
			ide_io_ops_t *iops,
#endif
			int irq);

static inline void ide_std_init_ports(hw_regs_t *hw,
				      unsigned long io_addr,
				      unsigned long ctl_addr)
{
	unsigned int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hw->io_ports[i] = io_addr++;

	hw->io_ports[IDE_CONTROL_OFFSET] = ctl_addr;
}

#include <asm/ide.h>

#if !defined(MAX_HWIFS) || defined(CONFIG_EMBEDDED)
#undef MAX_HWIFS
#define MAX_HWIFS	CONFIG_IDE_MAX_HWIFS
#endif

/* needed on alpha, x86/x86_64, ia64, mips, ppc32 and sh */
#ifndef IDE_ARCH_OBSOLETE_DEFAULTS
# define ide_default_io_base(index)	(0)
# define ide_default_irq(base)		(0)
# define ide_init_default_irq(base)	(0)
#endif

/*
 * ide_init_hwif_ports() is OBSOLETE and will be removed in 2.7 series.
 * New ports shouldn't define IDE_ARCH_OBSOLETE_INIT in <asm/ide.h>.
 */
#ifdef IDE_ARCH_OBSOLETE_INIT
static inline void ide_init_hwif_ports(hw_regs_t *hw,
				       unsigned long io_addr,
				       unsigned long ctl_addr,
				       int *irq)
{
	if (!ctl_addr)
		ide_std_init_ports(hw, io_addr, ide_default_io_ctl(io_addr));
	else
		ide_std_init_ports(hw, io_addr, ctl_addr);

	if (irq)
		*irq = 0;

	hw->io_ports[IDE_IRQ_OFFSET] = 0;

#ifdef CONFIG_PPC32
	if (ppc_ide_md.ide_init_hwif)
		ppc_ide_md.ide_init_hwif(hw, io_addr, ctl_addr, irq);
#endif
}
#else
static inline void ide_init_hwif_ports(hw_regs_t *hw,
				       unsigned long io_addr,
				       unsigned long ctl_addr,
				       int *irq)
{
	if (io_addr || ctl_addr)
		printk(KERN_WARNING "%s: must not be called\n", __FUNCTION__);
}
#endif /* IDE_ARCH_OBSOLETE_INIT */

/* Currently only m68k, apus and m8xx need it */
#ifndef IDE_ARCH_ACK_INTR
# define ide_ack_intr(hwif) (1)
#endif

/* Currently only Atari needs it */
#ifndef IDE_ARCH_LOCK
# define ide_release_lock()			do {} while (0)
# define ide_get_lock(hdlr, data)		do {} while (0)
#endif /* IDE_ARCH_LOCK */

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
 * set_tune	: tune interface for drive
 * serviced	: service command
 * reserved	: unused
 */
typedef union {
	unsigned all			: 8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned set_geometry	: 1;
		unsigned recalibrate	: 1;
		unsigned set_multmode	: 1;
		unsigned set_tune	: 1;
		unsigned serviced	: 1;
		unsigned reserved	: 3;
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned reserved	: 3;
		unsigned serviced	: 1;
		unsigned set_tune	: 1;
		unsigned set_multmode	: 1;
		unsigned recalibrate	: 1;
		unsigned set_geometry	: 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} special_t;

/*
 * ATA DATA Register Special.
 * ATA NSECTOR Count Register().
 * ATAPI Byte Count Register.
 * Channel index ordering pairs.
 */
typedef union {
	unsigned all			:16;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned low		:8;	/* LSB */
		unsigned high		:8;	/* MSB */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned high		:8;	/* MSB */
		unsigned low		:8;	/* LSB */
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} ata_nsector_t, ata_data_t, atapi_bcount_t, ata_index_t;

/*
 * ATA-IDE Select Register, aka Device-Head
 *
 * head		: always zeros here
 * unit		: drive select number: 0/1
 * bit5		: always 1
 * lba		: using LBA instead of CHS
 * bit7		: always 1
 */
typedef union {
	unsigned all			: 8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned head		: 4;
		unsigned unit		: 1;
		unsigned bit5		: 1;
		unsigned lba		: 1;
		unsigned bit7		: 1;
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned bit7		: 1;
		unsigned lba		: 1;
		unsigned bit5		: 1;
		unsigned unit		: 1;
		unsigned head		: 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} select_t, ata_select_t;

/*
 * The ATA-IDE Status Register.
 * The ATAPI Status Register.
 *
 * check	: Error occurred
 * idx		: Index Error
 * corr		: Correctable error occurred
 * drq		: Data is request by the device
 * dsc		: Disk Seek Complete			: ata
 *		: Media access command finished		: atapi
 * df		: Device Fault				: ata
 *		: Reserved				: atapi
 * drdy		: Ready, Command Mode Capable		: ata
 *		: Ignored for ATAPI commands		: atapi
 * bsy		: Disk is Busy
 *		: The device has access to the command block
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned check		:1;
		unsigned idx		:1;
		unsigned corr		:1;
		unsigned drq		:1;
		unsigned dsc		:1;
		unsigned df		:1;
		unsigned drdy		:1;
		unsigned bsy		:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned bsy		:1;
		unsigned drdy		:1;
		unsigned df		:1;
		unsigned dsc		:1;
		unsigned drq		:1;
		unsigned corr           :1;
		unsigned idx		:1;
		unsigned check		:1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} ata_status_t, atapi_status_t;

/*
 * ATAPI Feature Register
 *
 * dma		: Using DMA or PIO
 * reserved321	: Reserved
 * reserved654	: Reserved (Tag Type)
 * reserved7	: Reserved
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned dma		:1;
		unsigned reserved321	:3;
		unsigned reserved654	:3;
		unsigned reserved7	:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned reserved7	:1;
		unsigned reserved654	:3;
		unsigned reserved321	:3;
		unsigned dma		:1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_feature_t;

/*
 * ATAPI Interrupt Reason Register.
 *
 * cod		: Information transferred is command (1) or data (0)
 * io		: The device requests us to read (1) or write (0)
 * reserved	: Reserved
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned cod		:1;
		unsigned io		:1;
		unsigned reserved	:6;
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned reserved	:6;
		unsigned io		:1;
		unsigned cod		:1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_ireason_t;

/*
 * The ATAPI error register.
 *
 * ili		: Illegal Length Indication
 * eom		: End Of Media Detected
 * abrt		: Aborted command - As defined by ATA
 * mcr		: Media Change Requested - As defined by ATA
 * sense_key	: Sense key of the last failed packet command
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned ili		:1;
		unsigned eom		:1;
		unsigned abrt		:1;
		unsigned mcr		:1;
		unsigned sense_key	:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned sense_key	:4;
		unsigned mcr		:1;
		unsigned abrt		:1;
		unsigned eom		:1;
		unsigned ili		:1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_error_t;

/*
 * Status returned from various ide_ functions
 */
typedef enum {
	ide_stopped,	/* no drive operation was started */
	ide_started,	/* a drive operation was started, handler was set */
} ide_startstop_t;

struct ide_driver_s;
struct ide_settings_s;

typedef struct ide_drive_s {
	char		name[4];	/* drive name, such as "hda" */
        char            driver_req[10];	/* requests specific driver */

	request_queue_t		*queue;	/* request queue */

	struct request		*rq;	/* current request */
	struct ide_drive_s 	*next;	/* circular list of hwgroup drives */
	void		*driver_data;	/* extra driver data */
	struct hd_driveid	*id;	/* drive model identification info */
	struct proc_dir_entry *proc;	/* /proc/ide/ directory entry */
	struct ide_settings_s *settings;/* /proc/ide/ drive settings */

	struct hwif_s		*hwif;	/* actually (ide_hwif_t *) */

	unsigned long sleep;		/* sleep until this time */
	unsigned long service_start;	/* time we started last request */
	unsigned long service_time;	/* service time of last request */
	unsigned long timeout;		/* max time to wait for irq */

	special_t	special;	/* special action flags */
	select_t	select;		/* basic drive/head select reg value */

	u8	keep_settings;		/* restore settings after drive reset */
	u8	autodma;		/* device can safely use dma on host */
	u8	using_dma;		/* disk is using dma for read/write */
	u8	retry_pio;		/* retrying dma capable host in pio */
	u8	state;			/* retry state */
	u8	waiting_for_dma;	/* dma currently in progress */
	u8	unmask;			/* okay to unmask other irqs */
	u8	bswap;			/* byte swap data */
	u8	noflush;		/* don't attempt flushes */
	u8	dsc_overlap;		/* DSC overlap */
	u8	nice1;			/* give potential excess bandwidth */

	unsigned present	: 1;	/* drive is physically present */
	unsigned dead		: 1;	/* device ejected hint */
	unsigned id_read	: 1;	/* 1=id read from disk 0 = synthetic */
	unsigned noprobe 	: 1;	/* from:  hdx=noprobe */
	unsigned removable	: 1;	/* 1 if need to do check_media_change */
	unsigned attach		: 1;	/* needed for removable devices */
	unsigned forced_geom	: 1;	/* 1 if hdx=c,h,s was given at boot */
	unsigned no_unmask	: 1;	/* disallow setting unmask bit */
	unsigned no_io_32bit	: 1;	/* disallow enabling 32bit I/O */
	unsigned atapi_overlap	: 1;	/* ATAPI overlap (not supported) */
	unsigned nice0		: 1;	/* give obvious excess bandwidth */
	unsigned nice2		: 1;	/* give a share in our own bandwidth */
	unsigned doorlocking	: 1;	/* for removable only: door lock/unlock works */
	unsigned autotune	: 2;	/* 0=default, 1=autotune, 2=noautotune */
	unsigned remap_0_to_1	: 1;	/* 0=noremap, 1=remap 0->1 (for EZDrive) */
	unsigned blocked        : 1;	/* 1=powermanagment told us not to do anything, so sleep nicely */
	unsigned vdma		: 1;	/* 1=doing PIO over DMA 0=doing normal DMA */
	unsigned addressing;		/*      : 3;
					 *  0=28-bit
					 *  1=48-bit
					 *  2=48-bit doing 28-bit
					 *  3=64-bit
					 */
	unsigned scsi		: 1;	/* 0=default, 1=ide-scsi emulation */
	unsigned sleeping	: 1;	/* 1=sleeping & sleep field valid */
	unsigned post_reset	: 1;

        u8	quirk_list;	/* considered quirky, set for a specific host */
        u8	init_speed;	/* transfer rate set at boot */
        u8	pio_speed;      /* unused by core, used by some drivers for fallback from DMA */
        u8	current_speed;	/* current transfer rate set */
        u8	dn;		/* now wide spread use */
        u8	wcache;		/* status of write cache */
	u8	acoustic;	/* acoustic management */
	u8	media;		/* disk, cdrom, tape, floppy, ... */
	u8	ctl;		/* "normal" value for IDE_CONTROL_REG */
	u8	ready_stat;	/* min status value for drive ready */
	u8	mult_count;	/* current multiple sector setting */
	u8	mult_req;	/* requested multiple sector setting */
	u8	tune_req;	/* requested drive tuning setting */
	u8	io_32bit;	/* 0=16-bit, 1=32-bit, 2/3=32bit+sync */
	u8	bad_wstat;	/* used for ignoring WRERR_STAT */
	u8	nowerr;		/* used for ignoring WRERR_STAT */
	u8	sect0;		/* offset of first sector for DM6:DDO */
	u8	head;		/* "real" number of heads */
	u8	sect;		/* "real" sectors per track */
	u8	bios_head;	/* BIOS/fdisk/LILO number of heads */
	u8	bios_sect;	/* BIOS/fdisk/LILO sectors per track */

	unsigned int	bios_cyl;	/* BIOS/fdisk/LILO number of cyls */
	unsigned int	cyl;		/* "real" number of cyls */
	unsigned int	drive_data;	/* use by tuneproc/selectproc */
	unsigned int	usage;		/* current "open()" count for drive */
	unsigned int	failures;	/* current failure count */
	unsigned int	max_failures;	/* maximum allowed failure count */
	u64		probed_capacity;/* initial reported media capacity (ide-cd only currently) */

	u64		capacity64;	/* total number of sectors */

	int		lun;		/* logical unit */
	int		crc_count;	/* crc counter to reduce drive speed */
	struct list_head list;
	struct device	gendev;
	struct completion gendev_rel_comp;	/* to deal with device release() */
} ide_drive_t;

#define to_ide_device(dev)container_of(dev, ide_drive_t, gendev)

#define IDE_CHIPSET_PCI_MASK	\
    ((1<<ide_pci)|(1<<ide_cmd646)|(1<<ide_ali14xx))
#define IDE_CHIPSET_IS_PCI(c)	((IDE_CHIPSET_PCI_MASK >> (c)) & 1)

struct ide_pci_device_s;

typedef struct hwif_s {
	struct hwif_s *next;		/* for linked-list in ide_hwgroup_t */
	struct hwif_s *mate;		/* other hwif from same PCI chip */
	struct hwgroup_s *hwgroup;	/* actually (ide_hwgroup_t *) */
	struct proc_dir_entry *proc;	/* /proc/ide/ directory entry */

	char name[6];			/* name of interface, eg. "ide0" */

		/* task file registers for pata and sata */
	unsigned long	io_ports[IDE_NR_PORTS];
	unsigned long	sata_scr[SATA_NR_PORTS];
	unsigned long	sata_misc[SATA_NR_PORTS];

	hw_regs_t	hw;		/* Hardware info */
	ide_drive_t	drives[MAX_DRIVES];	/* drive info */

	u8 major;	/* our major number */
	u8 index;	/* 0 for ide0; 1 for ide1; ... */
	u8 channel;	/* for dual-port chips: 0=primary, 1=secondary */
	u8 straight8;	/* Alan's straight 8 check */
	u8 bus_state;	/* power state of the IDE bus */

	u8 atapi_dma;	/* host supports atapi_dma */
	u8 ultra_mask;
	u8 mwdma_mask;
	u8 swdma_mask;

	hwif_chipset_t chipset;	/* sub-module for tuning.. */

	struct pci_dev  *pci_dev;	/* for pci chipsets */
	struct ide_pci_device_s	*cds;	/* chipset device struct */

	void (*rw_disk)(ide_drive_t *, struct request *);

#if 0
	ide_hwif_ops_t	*hwifops;
#else
	/* routine to tune PIO mode for drives */
	void	(*tuneproc)(ide_drive_t *, u8);
	/* routine to retune DMA modes for drives */
	int	(*speedproc)(ide_drive_t *, u8);
	/* tweaks hardware to select drive */
	void	(*selectproc)(ide_drive_t *);
	/* chipset polling based on hba specifics */
	int	(*reset_poll)(ide_drive_t *);
	/* chipset specific changes to default for device-hba resets */
	void	(*pre_reset)(ide_drive_t *);
	/* routine to reset controller after a disk reset */
	void	(*resetproc)(ide_drive_t *);
	/* special interrupt handling for shared pci interrupts */
	void	(*intrproc)(ide_drive_t *);
	/* special host masking for drive selection */
	void	(*maskproc)(ide_drive_t *, int);
	/* check host's drive quirk list */
	int	(*quirkproc)(ide_drive_t *);
	/* driver soft-power interface */
	int	(*busproc)(ide_drive_t *, int);
//	/* host rate limiter */
//	u8	(*ratemask)(ide_drive_t *);
//	/* device rate limiter */
//	u8	(*ratefilter)(ide_drive_t *, u8);
#endif

	void (*ata_input_data)(ide_drive_t *, void *, u32);
	void (*ata_output_data)(ide_drive_t *, void *, u32);

	void (*atapi_input_bytes)(ide_drive_t *, void *, u32);
	void (*atapi_output_bytes)(ide_drive_t *, void *, u32);

	int (*dma_setup)(ide_drive_t *);
	void (*dma_exec_cmd)(ide_drive_t *, u8);
	void (*dma_start)(ide_drive_t *);
	int (*ide_dma_end)(ide_drive_t *drive);
	int (*ide_dma_check)(ide_drive_t *drive);
	int (*ide_dma_on)(ide_drive_t *drive);
	int (*ide_dma_off_quietly)(ide_drive_t *drive);
	int (*ide_dma_test_irq)(ide_drive_t *drive);
	int (*ide_dma_host_on)(ide_drive_t *drive);
	int (*ide_dma_host_off)(ide_drive_t *drive);
	int (*ide_dma_lostirq)(ide_drive_t *drive);
	int (*ide_dma_timeout)(ide_drive_t *drive);

	void (*OUTB)(u8 addr, unsigned long port);
	void (*OUTBSYNC)(ide_drive_t *drive, u8 addr, unsigned long port);
	void (*OUTW)(u16 addr, unsigned long port);
	void (*OUTL)(u32 addr, unsigned long port);
	void (*OUTSW)(unsigned long port, void *addr, u32 count);
	void (*OUTSL)(unsigned long port, void *addr, u32 count);

	u8  (*INB)(unsigned long port);
	u16 (*INW)(unsigned long port);
	u32 (*INL)(unsigned long port);
	void (*INSW)(unsigned long port, void *addr, u32 count);
	void (*INSL)(unsigned long port, void *addr, u32 count);

	/* dma physical region descriptor table (cpu view) */
	unsigned int	*dmatable_cpu;
	/* dma physical region descriptor table (dma view) */
	dma_addr_t	dmatable_dma;
	/* Scatter-gather list used to build the above */
	struct scatterlist *sg_table;
	int sg_max_nents;		/* Maximum number of entries in it */
	int sg_nents;			/* Current number of entries in it */
	int sg_dma_direction;		/* dma transfer direction */

	/* data phase of the active command (currently only valid for PIO/DMA) */
	int		data_phase;

	unsigned int nsect;
	unsigned int nleft;
	unsigned int cursg;
	unsigned int cursg_ofs;

	int		mmio;		/* hosts iomio (0) or custom (2) select */
	int		rqsize;		/* max sectors per request */
	int		irq;		/* our irq number */

	unsigned long	dma_master;	/* reference base addr dmabase */
	unsigned long	dma_base;	/* base addr for dma ports */
	unsigned long	dma_command;	/* dma command register */
	unsigned long	dma_vendor1;	/* dma vendor 1 register */
	unsigned long	dma_status;	/* dma status register */
	unsigned long	dma_vendor3;	/* dma vendor 3 register */
	unsigned long	dma_prdtable;	/* actual prd table address */

	unsigned long	config_data;	/* for use by chipset-specific code */
	unsigned long	select_data;	/* for use by chipset-specific code */

	unsigned long	extra_base;	/* extra addr for dma ports */
	unsigned	extra_ports;	/* number of extra dma ports */

	unsigned	noprobe    : 1;	/* don't probe for this interface */
	unsigned	present    : 1;	/* this interface exists */
	unsigned	hold       : 1; /* this interface is always present */
	unsigned	serialized : 1;	/* serialized all channel operation */
	unsigned	sharing_irq: 1;	/* 1 = sharing irq with another hwif */
	unsigned	reset      : 1;	/* reset after probe */
	unsigned	autodma    : 1;	/* auto-attempt using DMA at boot */
	unsigned	udma_four  : 1;	/* 1=ATA-66 capable, 0=default */
	unsigned	no_lba48   : 1; /* 1 = cannot do LBA48 */
	unsigned	no_lba48_dma : 1; /* 1 = cannot do LBA48 DMA */
	unsigned	no_dsc     : 1;	/* 0 default, 1 dsc_overlap disabled */
	unsigned	auto_poll  : 1; /* supports nop auto-poll */
	unsigned	sg_mapped  : 1;	/* sg_table and sg_nents are ready */
	unsigned	no_io_32bit : 1; /* 1 = can not do 32-bit IO ops */
	unsigned	err_stops_fifo : 1; /* 1=data FIFO is cleared by an error */
	unsigned	atapi_irq_bogon : 1; /* Generates spurious DMA interrupts in PIO mode */

	struct device	gendev;
	struct completion gendev_rel_comp; /* To deal with device release() */

	void		*hwif_data;	/* extra hwif data */

	unsigned dma;
} ____cacheline_internodealigned_in_smp ide_hwif_t;

/*
 *  internal ide interrupt handler type
 */
typedef ide_startstop_t (ide_pre_handler_t)(ide_drive_t *, struct request *);
typedef ide_startstop_t (ide_handler_t)(ide_drive_t *);
typedef int (ide_expiry_t)(ide_drive_t *);

typedef struct hwgroup_s {
		/* irq handler, if active */
	ide_startstop_t	(*handler)(ide_drive_t *);
		/* irq handler, suspended if active */
	ide_startstop_t	(*handler_save)(ide_drive_t *);
		/* BOOL: protects all fields below */
	volatile int busy;
		/* BOOL: wake us up on timer expiry */
	unsigned int sleeping	: 1;
		/* BOOL: polling active & poll_timeout field valid */
	unsigned int polling	: 1;
	 	/* BOOL: in a polling reset situation. Must not trigger another reset yet */
	unsigned int resetting  : 1;

		/* current drive */
	ide_drive_t *drive;
		/* ptr to current hwif in linked-list */
	ide_hwif_t *hwif;

		/* for pci chipsets */
	struct pci_dev *pci_dev;
		/* chipset device struct */
	struct ide_pci_device_s *cds;

		/* current request */
	struct request *rq;
		/* failsafe timer */
	struct timer_list timer;
		/* local copy of current write rq */
	struct request wrq;
		/* timeout value during long polls */
	unsigned long poll_timeout;
		/* queried upon timeouts */
	int (*expiry)(ide_drive_t *);
		/* ide_system_bus_speed */
	int pio_clock;

	unsigned char cmd_buf[4];
} ide_hwgroup_t;

/* structure attached to the request for IDE_TASK_CMDS */

/*
 * configurable drive settings
 */

#define TYPE_INT	0
#define TYPE_INTA	1
#define TYPE_BYTE	2
#define TYPE_SHORT	3

#define SETTING_READ	(1 << 0)
#define SETTING_WRITE	(1 << 1)
#define SETTING_RW	(SETTING_READ | SETTING_WRITE)

typedef int (ide_procset_t)(ide_drive_t *, int);
typedef struct ide_settings_s {
	char			*name;
	int			rw;
	int			read_ioctl;
	int			write_ioctl;
	int			data_type;
	int			min;
	int			max;
	int			mul_factor;
	int			div_factor;
	void			*data;
	ide_procset_t		*set;
	int			auto_remove;
	struct ide_settings_s	*next;
} ide_settings_t;

extern struct semaphore ide_setting_sem;
extern int ide_add_setting(ide_drive_t *drive, const char *name, int rw, int read_ioctl, int write_ioctl, int data_type, int min, int max, int mul_factor, int div_factor, void *data, ide_procset_t *set);
extern ide_settings_t *ide_find_setting_by_name(ide_drive_t *drive, char *name);
extern int ide_read_setting(ide_drive_t *t, ide_settings_t *setting);
extern int ide_write_setting(ide_drive_t *drive, ide_settings_t *setting, int val);
extern void ide_add_generic_settings(ide_drive_t *drive);

/*
 * /proc/ide interface
 */
typedef struct {
	const char	*name;
	mode_t		mode;
	read_proc_t	*read_proc;
	write_proc_t	*write_proc;
} ide_proc_entry_t;

#ifdef CONFIG_PROC_FS
extern struct proc_dir_entry *proc_ide_root;

extern void proc_ide_create(void);
extern void proc_ide_destroy(void);
extern void create_proc_ide_interfaces(void);
void destroy_proc_ide_interface(ide_hwif_t *);
extern void ide_add_proc_entries(struct proc_dir_entry *, ide_proc_entry_t *, void *);
extern void ide_remove_proc_entries(struct proc_dir_entry *, ide_proc_entry_t *);
read_proc_t proc_ide_read_capacity;
read_proc_t proc_ide_read_geometry;

#ifdef CONFIG_BLK_DEV_IDEPCI
void ide_pci_create_host_proc(const char *, get_info_t *);
#endif

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
static inline void create_proc_ide_interfaces(void) { ; }
static inline void destroy_proc_ide_interface(ide_hwif_t *hwif) { ; }
#define PROC_IDE_READ_RETURN(page,start,off,count,eof,len) return 0;
#endif

/*
 * Power Management step value (rq->pm->pm_step).
 *
 * The step value starts at 0 (ide_pm_state_start_suspend) for a
 * suspend operation or 1000 (ide_pm_state_start_resume) for a
 * resume operation.
 *
 * For each step, the core calls the subdriver start_power_step() first.
 * This can return:
 *	- ide_stopped :	In this case, the core calls us back again unless
 *			step have been set to ide_power_state_completed.
 *	- ide_started :	In this case, the channel is left busy until an
 *			async event (interrupt) occurs.
 * Typically, start_power_step() will issue a taskfile request with
 * do_rw_taskfile().
 *
 * Upon reception of the interrupt, the core will call complete_power_step()
 * with the error code if any. This routine should update the step value
 * and return. It should not start a new request. The core will call
 * start_power_step for the new step value, unless step have been set to
 * ide_power_state_completed.
 *
 * Subdrivers are expected to define their own additional power
 * steps from 1..999 for suspend and from 1001..1999 for resume,
 * other values are reserved for future use.
 */

enum {
	ide_pm_state_completed		= -1,
	ide_pm_state_start_suspend	= 0,
	ide_pm_state_start_resume	= 1000,
};

/*
 * Subdrivers support.
 *
 * The gendriver.owner field should be set to the module owner of this driver.
 * The gendriver.name field should be set to the name of this driver
 */
typedef struct ide_driver_s {
	const char			*version;
	u8				media;
	unsigned supports_dsc_overlap	: 1;
	ide_startstop_t	(*do_request)(ide_drive_t *, struct request *, sector_t);
	int		(*end_request)(ide_drive_t *, int, int);
	ide_startstop_t	(*error)(ide_drive_t *, struct request *rq, u8, u8);
	ide_startstop_t	(*abort)(ide_drive_t *, struct request *rq);
	ide_proc_entry_t	*proc;
	struct device_driver	gen_driver;
	int		(*probe)(ide_drive_t *);
	void		(*remove)(ide_drive_t *);
	void		(*shutdown)(ide_drive_t *);
} ide_driver_t;

#define to_ide_driver(drv) container_of(drv, ide_driver_t, gen_driver)

int generic_ide_ioctl(ide_drive_t *, struct file *, struct block_device *, unsigned, unsigned long);

/*
 * ide_hwifs[] is the master data structure used to keep track
 * of just about everything in ide.c.  Whenever possible, routines
 * should be using pointers to a drive (ide_drive_t *) or
 * pointers to a hwif (ide_hwif_t *), rather than indexing this
 * structure directly (the allocation/layout may change!).
 *
 */
#ifndef _IDE_C
extern	ide_hwif_t	ide_hwifs[];		/* master data repository */
#endif
extern int noautodma;

extern int ide_end_request (ide_drive_t *drive, int uptodate, int nrsecs);
int ide_end_dequeued_request(ide_drive_t *drive, struct request *rq,
			     int uptodate, int nr_sectors);

/*
 * This is used on exit from the driver to designate the next irq handler
 * and also to start the safety timer.
 */
extern void ide_set_handler (ide_drive_t *drive, ide_handler_t *handler, unsigned int timeout, ide_expiry_t *expiry);

/*
 * This is used on exit from the driver to designate the next irq handler
 * and start the safety time safely and atomically from the IRQ handler
 * with respect to the command issue (which it also does)
 */
extern void ide_execute_command(ide_drive_t *, task_ioreg_t cmd, ide_handler_t *, unsigned int, ide_expiry_t *);

ide_startstop_t __ide_error(ide_drive_t *, struct request *, u8, u8);

/*
 * ide_error() takes action based on the error returned by the controller.
 * The caller should return immediately after invoking this.
 *
 * (drive, msg, status)
 */
ide_startstop_t ide_error (ide_drive_t *drive, const char *msg, byte stat);

ide_startstop_t __ide_abort(ide_drive_t *, struct request *);

/*
 * Abort a running command on the controller triggering the abort
 * from a host side, non error situation
 * (drive, msg)
 */
extern ide_startstop_t ide_abort(ide_drive_t *, const char *);

extern void ide_fix_driveid(struct hd_driveid *);
/*
 * ide_fixstring() cleans up and (optionally) byte-swaps a text string,
 * removing leading/trailing blanks and compressing internal blanks.
 * It is primarily used to tidy up the model name/number fields as
 * returned by the WIN_[P]IDENTIFY commands.
 *
 * (s, bytecount, byteswap)
 */
extern void ide_fixstring(u8 *, const int, const int);

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return 1 after doing "*startstop = ide_error()", and the
 * caller should return the updated value of "startstop" in this case.
 * "startstop" is unchanged when the function returns 0;
 * (startstop, drive, good, bad, timeout)
 */
extern int ide_wait_stat(ide_startstop_t *, ide_drive_t *, u8, u8, unsigned long);

/*
 * Start a reset operation for an IDE interface.
 * The caller should return immediately after invoking this.
 */
extern ide_startstop_t ide_do_reset (ide_drive_t *);

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
extern void ide_init_drive_cmd (struct request *rq);

/*
 * this function returns error location sector offset in case of a write error
 */
extern u64 ide_get_error_location(ide_drive_t *, char *);

/*
 * "action" parameter type for ide_do_drive_cmd() below.
 */
typedef enum {
	ide_wait,	/* insert rq at end of list, and wait for it */
	ide_preempt,	/* insert rq in front of current request */
	ide_head_wait,	/* insert rq in front of current request and wait for it */
	ide_end		/* insert rq at end of list, but don't wait for it */
} ide_action_t;

extern int ide_do_drive_cmd(ide_drive_t *, struct request *, ide_action_t);

/*
 * Clean up after success/failure of an explicit drive cmd.
 * stat/err are used only when (HWGROUP(drive)->rq->cmd == IDE_DRIVE_CMD).
 * stat/err are used only when (HWGROUP(drive)->rq->cmd == IDE_DRIVE_TASK_MASK).
 *
 * (ide_drive_t *drive, u8 stat, u8 err)
 */
extern void ide_end_drive_cmd(ide_drive_t *, u8, u8);

/*
 * Issue ATA command and wait for completion.
 * Use for implementing commands in kernel
 *
 *  (ide_drive_t *drive, u8 cmd, u8 nsect, u8 feature, u8 sectors, u8 *buf)
 */
extern int ide_wait_cmd(ide_drive_t *, u8, u8, u8, u8, u8 *);

typedef struct ide_task_s {
/*
 *	struct hd_drive_task_hdr	tf;
 *	task_struct_t		tf;
 *	struct hd_drive_hob_hdr		hobf;
 *	hob_struct_t		hobf;
 */
	task_ioreg_t		tfRegister[8];
	task_ioreg_t		hobRegister[8];
	ide_reg_valid_t		tf_out_flags;
	ide_reg_valid_t		tf_in_flags;
	int			data_phase;
	int			command_type;
	ide_pre_handler_t	*prehandler;
	ide_handler_t		*handler;
	struct request		*rq;		/* copy of request */
	void			*special;	/* valid_t generally */
} ide_task_t;

extern u32 ide_read_24(ide_drive_t *);

extern void SELECT_DRIVE(ide_drive_t *);
extern void SELECT_INTERRUPT(ide_drive_t *);
extern void SELECT_MASK(ide_drive_t *, int);
extern void QUIRK_LIST(ide_drive_t *);

extern int drive_is_ready(ide_drive_t *);
extern int wait_for_ready(ide_drive_t *, int /* timeout */);

/*
 * taskfile io for disks for now...and builds request from ide_ioctl
 */
extern ide_startstop_t do_rw_taskfile(ide_drive_t *, ide_task_t *);

/*
 * Special Flagged Register Validation Caller
 */
extern ide_startstop_t flagged_taskfile(ide_drive_t *, ide_task_t *);

extern ide_startstop_t set_multmode_intr(ide_drive_t *);
extern ide_startstop_t set_geometry_intr(ide_drive_t *);
extern ide_startstop_t recal_intr(ide_drive_t *);
extern ide_startstop_t task_no_data_intr(ide_drive_t *);
extern ide_startstop_t task_in_intr(ide_drive_t *);
extern ide_startstop_t pre_task_out_intr(ide_drive_t *, struct request *);

extern int ide_raw_taskfile(ide_drive_t *, ide_task_t *, u8 *);

int ide_taskfile_ioctl(ide_drive_t *, unsigned int, unsigned long);
int ide_cmd_ioctl(ide_drive_t *, unsigned int, unsigned long);
int ide_task_ioctl(ide_drive_t *, unsigned int, unsigned long);

extern int system_bus_clock(void);

extern int ide_driveid_update(ide_drive_t *);
extern int ide_ata66_check(ide_drive_t *, ide_task_t *);
extern int ide_config_drive_speed(ide_drive_t *, u8);
extern u8 eighty_ninty_three (ide_drive_t *);
extern int set_transfer(ide_drive_t *, ide_task_t *);
extern int taskfile_lib_get_identify(ide_drive_t *drive, u8 *);

extern int ide_wait_not_busy(ide_hwif_t *hwif, unsigned long timeout);

/*
 * ide_stall_queue() can be used by a drive to give excess bandwidth back
 * to the hwgroup by sleeping for timeout jiffies.
 */
extern void ide_stall_queue(ide_drive_t *drive, unsigned long timeout);

extern int ide_spin_wait_hwgroup(ide_drive_t *);
extern void ide_timer_expiry(unsigned long);
extern irqreturn_t ide_intr(int irq, void *dev_id);
extern void do_ide_request(request_queue_t *);

void ide_init_disk(struct gendisk *, ide_drive_t *);

extern int ideprobe_init(void);

extern void ide_scan_pcibus(int scan_direction) __init;
extern int __ide_pci_register_driver(struct pci_driver *driver, struct module *owner);
#define ide_pci_register_driver(d) __ide_pci_register_driver(d, THIS_MODULE)
void ide_pci_setup_ports(struct pci_dev *, struct ide_pci_device_s *, int, ata_index_t *);
extern void ide_setup_pci_noise (struct pci_dev *dev, struct ide_pci_device_s *d);

extern void default_hwif_iops(ide_hwif_t *);
extern void default_hwif_mmiops(ide_hwif_t *);
extern void default_hwif_transport(ide_hwif_t *);

void ide_register_subdriver(ide_drive_t *, ide_driver_t *);
void ide_unregister_subdriver(ide_drive_t *, ide_driver_t *);

#define ON_BOARD		1
#define NEVER_BOARD		0

#ifdef CONFIG_BLK_DEV_OFFBOARD
#  define OFF_BOARD		ON_BOARD
#else /* CONFIG_BLK_DEV_OFFBOARD */
#  define OFF_BOARD		NEVER_BOARD
#endif /* CONFIG_BLK_DEV_OFFBOARD */

#define NODMA 0
#define NOAUTODMA 1
#define AUTODMA 2

typedef struct ide_pci_enablebit_s {
	u8	reg;	/* byte pci reg holding the enable-bit */
	u8	mask;	/* mask to isolate the enable-bit */
	u8	val;	/* value of masked reg when "enabled" */
} ide_pci_enablebit_t;

enum {
	/* Uses ISA control ports not PCI ones. */
	IDEPCI_FLAG_ISA_PORTS		= (1 << 0),
};

typedef struct ide_pci_device_s {
	char			*name;
	int			(*init_setup)(struct pci_dev *, struct ide_pci_device_s *);
	void			(*init_setup_dma)(struct pci_dev *, struct ide_pci_device_s *, ide_hwif_t *);
	unsigned int		(*init_chipset)(struct pci_dev *, const char *);
	void			(*init_iops)(ide_hwif_t *);
	void                    (*init_hwif)(ide_hwif_t *);
	void			(*init_dma)(ide_hwif_t *, unsigned long);
	void			(*fixup)(ide_hwif_t *);
	u8			channels;
	u8			autodma;
	ide_pci_enablebit_t	enablebits[2];
	u8			bootable;
	unsigned int		extra;
	struct ide_pci_device_s	*next;
	u8			flags;
} ide_pci_device_t;

extern int ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);
extern int ide_setup_pci_devices(struct pci_dev *, struct pci_dev *, ide_pci_device_t *);

void ide_map_sg(ide_drive_t *, struct request *);
void ide_init_sg_cmd(ide_drive_t *, struct request *);

#define BAD_DMA_DRIVE		0
#define GOOD_DMA_DRIVE		1

#ifdef CONFIG_BLK_DEV_IDEDMA
struct drive_list_entry {
	const char *id_model;
	const char *id_firmware;
};

int ide_in_drive_list(struct hd_driveid *, const struct drive_list_entry *);
int __ide_dma_bad_drive(ide_drive_t *);
int __ide_dma_good_drive(ide_drive_t *);
int ide_use_dma(ide_drive_t *);
int __ide_dma_off(ide_drive_t *);
void ide_dma_verbose(ide_drive_t *);
ide_startstop_t ide_dma_intr(ide_drive_t *);

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
extern int ide_build_sglist(ide_drive_t *, struct request *);
extern int ide_build_dmatable(ide_drive_t *, struct request *);
extern void ide_destroy_dmatable(ide_drive_t *);
extern int ide_release_dma(ide_hwif_t *);
extern void ide_setup_dma(ide_hwif_t *, unsigned long, unsigned int);

extern int __ide_dma_host_off(ide_drive_t *);
extern int __ide_dma_off_quietly(ide_drive_t *);
extern int __ide_dma_host_on(ide_drive_t *);
extern int __ide_dma_on(ide_drive_t *);
extern int __ide_dma_check(ide_drive_t *);
extern int ide_dma_setup(ide_drive_t *);
extern void ide_dma_start(ide_drive_t *);
extern int __ide_dma_end(ide_drive_t *);
extern int __ide_dma_lostirq(ide_drive_t *);
extern int __ide_dma_timeout(ide_drive_t *);
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI */

#else
static inline int ide_use_dma(ide_drive_t *drive) { return 0; }
static inline int __ide_dma_off(ide_drive_t *drive) { return 0; }
static inline void ide_dma_verbose(ide_drive_t *drive) { ; }
#endif /* CONFIG_BLK_DEV_IDEDMA */

#ifndef CONFIG_BLK_DEV_IDEDMA_PCI
static inline void ide_release_dma(ide_hwif_t *drive) {;}
#endif

extern int ide_hwif_request_regions(ide_hwif_t *hwif);
extern void ide_hwif_release_regions(ide_hwif_t* hwif);
extern void ide_unregister (unsigned int index);

void ide_register_region(struct gendisk *);
void ide_unregister_region(struct gendisk *);

void ide_undecoded_slave(ide_hwif_t *);

int probe_hwif_init_with_fixup(ide_hwif_t *, void (*)(ide_hwif_t *));
extern int probe_hwif_init(ide_hwif_t *);

static inline void *ide_get_hwifdata (ide_hwif_t * hwif)
{
	return hwif->hwif_data;
}

static inline void ide_set_hwifdata (ide_hwif_t * hwif, void *data)
{
	hwif->hwif_data = data;
}

/* ide-lib.c */
extern u8 ide_dma_speed(ide_drive_t *drive, u8 mode);
extern u8 ide_rate_filter(u8 mode, u8 speed); 
extern int ide_dma_enable(ide_drive_t *drive);
extern char *ide_xfer_verbose(u8 xfer_rate);
extern void ide_toggle_bounce(ide_drive_t *drive, int on);
extern int ide_set_xfer_rate(ide_drive_t *drive, u8 rate);

u8 ide_dump_status(ide_drive_t *, const char *, u8);

typedef struct ide_pio_timings_s {
	int	setup_time;	/* Address setup (ns) minimum */
	int	active_time;	/* Active pulse (ns) minimum */
	int	cycle_time;	/* Cycle time (ns) minimum = (setup + active + recovery) */
} ide_pio_timings_t;

typedef struct ide_pio_data_s {
	u8 pio_mode;
	u8 use_iordy;
	u8 overridden;
	u8 blacklisted;
	unsigned int cycle_time;
} ide_pio_data_t;

extern u8 ide_get_best_pio_mode (ide_drive_t *drive, u8 mode_wanted, u8 max_mode, ide_pio_data_t *d);
extern const ide_pio_timings_t ide_pio_timings[6];


extern spinlock_t ide_lock;
extern struct semaphore ide_cfg_sem;
/*
 * Structure locking:
 *
 * ide_cfg_sem and ide_lock together protect changes to
 * ide_hwif_t->{next,hwgroup}
 * ide_drive_t->next
 *
 * ide_hwgroup_t->busy: ide_lock
 * ide_hwgroup_t->hwif: ide_lock
 * ide_hwif_t->mate: constant, no locking
 * ide_drive_t->hwif: constant, no locking
 */

#define local_irq_set(flags)	do { local_save_flags((flags)); local_irq_enable_in_hardirq(); } while (0)

extern struct bus_type ide_bus_type;

/* check if CACHE FLUSH (EXT) command is supported (bits defined in ATA-6) */
#define ide_id_has_flush_cache(id)	((id)->cfs_enable_2 & 0x3000)

/* some Maxtor disks have bit 13 defined incorrectly so check bit 10 too */
#define ide_id_has_flush_cache_ext(id)	\
	(((id)->cfs_enable_2 & 0x2400) == 0x2400)

static inline int hwif_to_node(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	return dev ? pcibus_to_node(dev->bus) : -1;
}

#endif /* _IDE_H */
