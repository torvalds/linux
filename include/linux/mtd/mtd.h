/*
 * $Id: mtd.h,v 1.61 2005/11/07 11:14:54 gleixner Exp $
 *
 * Copyright (C) 1999-2003 David Woodhouse <dwmw2@infradead.org> et al.
 *
 * Released under GPL
 */

#ifndef __MTD_MTD_H__
#define __MTD_MTD_H__

#ifndef __KERNEL__
#error This is a kernel header. Perhaps include mtd-user.h instead?
#endif

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/uio.h>
#include <linux/notifier.h>

#include <linux/mtd/compatmac.h>
#include <mtd/mtd-abi.h>

#define MTD_CHAR_MAJOR 90
#define MTD_BLOCK_MAJOR 31
#define MAX_MTD_DEVICES 16

#define MTD_ERASE_PENDING      	0x01
#define MTD_ERASING		0x02
#define MTD_ERASE_SUSPEND	0x04
#define MTD_ERASE_DONE          0x08
#define MTD_ERASE_FAILED        0x10

/* If the erase fails, fail_addr might indicate exactly which block failed.  If
   fail_addr = 0xffffffff, the failure was not at the device level or was not
   specific to any particular block. */
struct erase_info {
	struct mtd_info *mtd;
	u_int32_t addr;
	u_int32_t len;
	u_int32_t fail_addr;
	u_long time;
	u_long retries;
	u_int dev;
	u_int cell;
	void (*callback) (struct erase_info *self);
	u_long priv;
	u_char state;
	struct erase_info *next;
};

struct mtd_erase_region_info {
	u_int32_t offset;			/* At which this region starts, from the beginning of the MTD */
	u_int32_t erasesize;		/* For this region */
	u_int32_t numblocks;		/* Number of blocks of erasesize in this region */
};

struct mtd_info {
	u_char type;
	u_int32_t flags;
	u_int32_t size;	 // Total size of the MTD

	/* "Major" erase size for the device. Naïve users may take this
	 * to be the only erase size available, or may use the more detailed
	 * information below if they desire
	 */
	u_int32_t erasesize;
	/* Smallest availlable size for writing to the device.  For NAND,
	 * this is the page size, for some NOR chips, the size of ECC
	 * covered blocks.
	 */
	u_int32_t writesize;

	u_int32_t oobsize;   // Amount of OOB data per block (e.g. 16)
	u_int32_t ecctype;
	u_int32_t eccsize;

	/*
	 * Reuse some of the above unused fields in the case of NOR flash
	 * with configurable programming regions to avoid modifying the
	 * user visible structure layout/size.  Only valid when the
	 * MTD_PROGRAM_REGIONS flag is set.
	 * (Maybe we should have an union for those?)
	 */
#define MTD_PROGREGION_CTRLMODE_VALID(mtd)  (mtd)->oobsize
#define MTD_PROGREGION_CTRLMODE_INVALID(mtd)  (mtd)->ecctype

	// Kernel-only stuff starts here.
	char *name;
	int index;

	// oobinfo is a nand_oobinfo structure, which can be set by iotcl (MEMSETOOBINFO)
	struct nand_oobinfo oobinfo;
	u_int32_t oobavail;  // Number of bytes in OOB area available for fs

	/* Data for variable erase regions. If numeraseregions is zero,
	 * it means that the whole device has erasesize as given above.
	 */
	int numeraseregions;
	struct mtd_erase_region_info *eraseregions;

	/* This really shouldn't be here. It can go away in 2.5 */
	u_int32_t bank_size;

	int (*erase) (struct mtd_info *mtd, struct erase_info *instr);

	/* This stuff for eXecute-In-Place */
	int (*point) (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char **mtdbuf);

	/* We probably shouldn't allow XIP if the unpoint isn't a NULL */
	void (*unpoint) (struct mtd_info *mtd, u_char * addr, loff_t from, size_t len);


	int (*read) (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
	int (*write) (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf);

	int (*read_oob) (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
	int (*write_oob) (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf);

	/*
	 * Methods to access the protection register area, present in some
	 * flash devices. The user data is one time programmable but the
	 * factory data is read only.
	 */
	int (*get_fact_prot_info) (struct mtd_info *mtd, struct otp_info *buf, size_t len);
	int (*read_fact_prot_reg) (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
	int (*get_user_prot_info) (struct mtd_info *mtd, struct otp_info *buf, size_t len);
	int (*read_user_prot_reg) (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
	int (*write_user_prot_reg) (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
	int (*lock_user_prot_reg) (struct mtd_info *mtd, loff_t from, size_t len);

	/* kvec-based read/write methods.
	   NB: The 'count' parameter is the number of _vectors_, each of
	   which contains an (ofs, len) tuple.
	*/
	int (*writev) (struct mtd_info *mtd, const struct kvec *vecs, unsigned long count, loff_t to, size_t *retlen);

	/* Sync */
	void (*sync) (struct mtd_info *mtd);

	/* Chip-supported device locking */
	int (*lock) (struct mtd_info *mtd, loff_t ofs, size_t len);
	int (*unlock) (struct mtd_info *mtd, loff_t ofs, size_t len);

	/* Power Management functions */
	int (*suspend) (struct mtd_info *mtd);
	void (*resume) (struct mtd_info *mtd);

	/* Bad block management functions */
	int (*block_isbad) (struct mtd_info *mtd, loff_t ofs);
	int (*block_markbad) (struct mtd_info *mtd, loff_t ofs);

	struct notifier_block reboot_notifier;  /* default mode before reboot */

	void *priv;

	struct module *owner;
	int usecount;
};


	/* Kernel-side ioctl definitions */

extern int add_mtd_device(struct mtd_info *mtd);
extern int del_mtd_device (struct mtd_info *mtd);

extern struct mtd_info *get_mtd_device(struct mtd_info *mtd, int num);

extern void put_mtd_device(struct mtd_info *mtd);


struct mtd_notifier {
	void (*add)(struct mtd_info *mtd);
	void (*remove)(struct mtd_info *mtd);
	struct list_head list;
};


extern void register_mtd_user (struct mtd_notifier *new);
extern int unregister_mtd_user (struct mtd_notifier *old);

int default_mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
		       unsigned long count, loff_t to, size_t *retlen);

int default_mtd_readv(struct mtd_info *mtd, struct kvec *vecs,
		      unsigned long count, loff_t from, size_t *retlen);

#define MTD_ERASE(mtd, args...) (*(mtd->erase))(mtd, args)
#define MTD_POINT(mtd, a,b,c,d) (*(mtd->point))(mtd, a,b,c, (u_char **)(d))
#define MTD_UNPOINT(mtd, arg) (*(mtd->unpoint))(mtd, (u_char *)arg)
#define MTD_READ(mtd, args...) (*(mtd->read))(mtd, args)
#define MTD_WRITE(mtd, args...) (*(mtd->write))(mtd, args)
#define MTD_READV(mtd, args...) (*(mtd->readv))(mtd, args)
#define MTD_WRITEV(mtd, args...) (*(mtd->writev))(mtd, args)
#define MTD_READECC(mtd, args...) (*(mtd->read_ecc))(mtd, args)
#define MTD_WRITEECC(mtd, args...) (*(mtd->write_ecc))(mtd, args)
#define MTD_READOOB(mtd, args...) (*(mtd->read_oob))(mtd, args)
#define MTD_WRITEOOB(mtd, args...) (*(mtd->write_oob))(mtd, args)
#define MTD_SYNC(mtd) do { if (mtd->sync) (*(mtd->sync))(mtd);  } while (0)


#ifdef CONFIG_MTD_PARTITIONS
void mtd_erase_callback(struct erase_info *instr);
#else
static inline void mtd_erase_callback(struct erase_info *instr)
{
	if (instr->callback)
		instr->callback(instr);
}
#endif

/*
 * Debugging macro and defines
 */
#define MTD_DEBUG_LEVEL0	(0)	/* Quiet   */
#define MTD_DEBUG_LEVEL1	(1)	/* Audible */
#define MTD_DEBUG_LEVEL2	(2)	/* Loud    */
#define MTD_DEBUG_LEVEL3	(3)	/* Noisy   */

#ifdef CONFIG_MTD_DEBUG
#define DEBUG(n, args...)				\
 	do {						\
		if (n <= CONFIG_MTD_DEBUG_VERBOSE)	\
			printk(KERN_INFO args);		\
	} while(0)
#else /* CONFIG_MTD_DEBUG */
#define DEBUG(n, args...) do { } while(0)

#endif /* CONFIG_MTD_DEBUG */

#endif /* __MTD_MTD_H__ */
