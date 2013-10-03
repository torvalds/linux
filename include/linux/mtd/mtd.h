/*
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org> et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __MTD_MTD_H__
#define __MTD_MTD_H__

#include <linux/types.h>
#include <linux/uio.h>
#include <linux/notifier.h>
#include <linux/device.h>

#include <mtd/mtd-abi.h>

#include <asm/div64.h>

#define MTD_CHAR_MAJOR 90
#define MTD_BLOCK_MAJOR 31

#define MTD_ERASE_PENDING	0x01
#define MTD_ERASING		0x02
#define MTD_ERASE_SUSPEND	0x04
#define MTD_ERASE_DONE		0x08
#define MTD_ERASE_FAILED	0x10

#define MTD_FAIL_ADDR_UNKNOWN -1LL

/*
 * If the erase fails, fail_addr might indicate exactly which block failed. If
 * fail_addr = MTD_FAIL_ADDR_UNKNOWN, the failure was not at the device level
 * or was not specific to any particular block.
 */
struct erase_info {
	struct mtd_info *mtd;
	uint64_t addr;
	uint64_t len;
	uint64_t fail_addr;
	u_long time;
	u_long retries;
	unsigned dev;
	unsigned cell;
	void (*callback) (struct erase_info *self);
	u_long priv;
	u_char state;
	struct erase_info *next;
};

struct mtd_erase_region_info {
	uint64_t offset;		/* At which this region starts, from the beginning of the MTD */
	uint32_t erasesize;		/* For this region */
	uint32_t numblocks;		/* Number of blocks of erasesize in this region */
	unsigned long *lockmap;		/* If keeping bitmap of locks */
};

/**
 * struct mtd_oob_ops - oob operation operands
 * @mode:	operation mode
 *
 * @len:	number of data bytes to write/read
 *
 * @retlen:	number of data bytes written/read
 *
 * @ooblen:	number of oob bytes to write/read
 * @oobretlen:	number of oob bytes written/read
 * @ooboffs:	offset of oob data in the oob area (only relevant when
 *		mode = MTD_OPS_PLACE_OOB or MTD_OPS_RAW)
 * @datbuf:	data buffer - if NULL only oob data are read/written
 * @oobbuf:	oob data buffer
 *
 * Note, it is allowed to read more than one OOB area at one go, but not write.
 * The interface assumes that the OOB write requests program only one page's
 * OOB area.
 */
struct mtd_oob_ops {
	unsigned int	mode;
	size_t		len;
	size_t		retlen;
	size_t		ooblen;
	size_t		oobretlen;
	uint32_t	ooboffs;
	uint8_t		*datbuf;
	uint8_t		*oobbuf;
};

#define MTD_MAX_OOBFREE_ENTRIES_LARGE	32
#define MTD_MAX_ECCPOS_ENTRIES_LARGE	640
/*
 * Internal ECC layout control structure. For historical reasons, there is a
 * similar, smaller struct nand_ecclayout_user (in mtd-abi.h) that is retained
 * for export to user-space via the ECCGETLAYOUT ioctl.
 * nand_ecclayout should be expandable in the future simply by the above macros.
 */
struct nand_ecclayout {
	__u32 eccbytes;
	__u32 eccpos[MTD_MAX_ECCPOS_ENTRIES_LARGE];
	__u32 oobavail;
	struct nand_oobfree oobfree[MTD_MAX_OOBFREE_ENTRIES_LARGE];
};

struct module;	/* only needed for owner field in mtd_info */

struct mtd_info {
	u_char type;
	uint32_t flags;
	uint64_t size;	 // Total size of the MTD

	/* "Major" erase size for the device. Naïve users may take this
	 * to be the only erase size available, or may use the more detailed
	 * information below if they desire
	 */
	uint32_t erasesize;
	/* Minimal writable flash unit size. In case of NOR flash it is 1 (even
	 * though individual bits can be cleared), in case of NAND flash it is
	 * one NAND page (or half, or one-fourths of it), in case of ECC-ed NOR
	 * it is of ECC block size, etc. It is illegal to have writesize = 0.
	 * Any driver registering a struct mtd_info must ensure a writesize of
	 * 1 or larger.
	 */
	uint32_t writesize;

	/*
	 * Size of the write buffer used by the MTD. MTD devices having a write
	 * buffer can write multiple writesize chunks at a time. E.g. while
	 * writing 4 * writesize bytes to a device with 2 * writesize bytes
	 * buffer the MTD driver can (but doesn't have to) do 2 writesize
	 * operations, but not 4. Currently, all NANDs have writebufsize
	 * equivalent to writesize (NAND page size). Some NOR flashes do have
	 * writebufsize greater than writesize.
	 */
	uint32_t writebufsize;

	uint32_t oobsize;   // Amount of OOB data per block (e.g. 16)
	uint32_t oobavail;  // Available OOB bytes per block

	/*
	 * If erasesize is a power of 2 then the shift is stored in
	 * erasesize_shift otherwise erasesize_shift is zero. Ditto writesize.
	 */
	unsigned int erasesize_shift;
	unsigned int writesize_shift;
	/* Masks based on erasesize_shift and writesize_shift */
	unsigned int erasesize_mask;
	unsigned int writesize_mask;

	/*
	 * read ops return -EUCLEAN if max number of bitflips corrected on any
	 * one region comprising an ecc step equals or exceeds this value.
	 * Settable by driver, else defaults to ecc_strength.  User can override
	 * in sysfs.  N.B. The meaning of the -EUCLEAN return code has changed;
	 * see Documentation/ABI/testing/sysfs-class-mtd for more detail.
	 */
	unsigned int bitflip_threshold;

	// Kernel-only stuff starts here.
	const char *name;
	int index;

	/* ECC layout structure pointer - read only! */
	struct nand_ecclayout *ecclayout;

	/* the ecc step size. */
	unsigned int ecc_step_size;

	/* max number of correctible bit errors per ecc step */
	unsigned int ecc_strength;

	/* Data for variable erase regions. If numeraseregions is zero,
	 * it means that the whole device has erasesize as given above.
	 */
	int numeraseregions;
	struct mtd_erase_region_info *eraseregions;

	/*
	 * Do not call via these pointers, use corresponding mtd_*()
	 * wrappers instead.
	 */
	int (*_erase) (struct mtd_info *mtd, struct erase_info *instr);
	int (*_point) (struct mtd_info *mtd, loff_t from, size_t len,
		       size_t *retlen, void **virt, resource_size_t *phys);
	int (*_unpoint) (struct mtd_info *mtd, loff_t from, size_t len);
	unsigned long (*_get_unmapped_area) (struct mtd_info *mtd,
					     unsigned long len,
					     unsigned long offset,
					     unsigned long flags);
	int (*_read) (struct mtd_info *mtd, loff_t from, size_t len,
		      size_t *retlen, u_char *buf);
	int (*_write) (struct mtd_info *mtd, loff_t to, size_t len,
		       size_t *retlen, const u_char *buf);
	int (*_panic_write) (struct mtd_info *mtd, loff_t to, size_t len,
			     size_t *retlen, const u_char *buf);
	int (*_read_oob) (struct mtd_info *mtd, loff_t from,
			  struct mtd_oob_ops *ops);
	int (*_write_oob) (struct mtd_info *mtd, loff_t to,
			   struct mtd_oob_ops *ops);
	int (*_get_fact_prot_info) (struct mtd_info *mtd, struct otp_info *buf,
				    size_t len);
	int (*_read_fact_prot_reg) (struct mtd_info *mtd, loff_t from,
				    size_t len, size_t *retlen, u_char *buf);
	int (*_get_user_prot_info) (struct mtd_info *mtd, struct otp_info *buf,
				    size_t len);
	int (*_read_user_prot_reg) (struct mtd_info *mtd, loff_t from,
				    size_t len, size_t *retlen, u_char *buf);
	int (*_write_user_prot_reg) (struct mtd_info *mtd, loff_t to,
				     size_t len, size_t *retlen, u_char *buf);
	int (*_lock_user_prot_reg) (struct mtd_info *mtd, loff_t from,
				    size_t len);
	int (*_writev) (struct mtd_info *mtd, const struct kvec *vecs,
			unsigned long count, loff_t to, size_t *retlen);
	void (*_sync) (struct mtd_info *mtd);
	int (*_lock) (struct mtd_info *mtd, loff_t ofs, uint64_t len);
	int (*_unlock) (struct mtd_info *mtd, loff_t ofs, uint64_t len);
	int (*_is_locked) (struct mtd_info *mtd, loff_t ofs, uint64_t len);
	int (*_block_isbad) (struct mtd_info *mtd, loff_t ofs);
	int (*_block_markbad) (struct mtd_info *mtd, loff_t ofs);
	int (*_suspend) (struct mtd_info *mtd);
	void (*_resume) (struct mtd_info *mtd);
	/*
	 * If the driver is something smart, like UBI, it may need to maintain
	 * its own reference counting. The below functions are only for driver.
	 */
	int (*_get_device) (struct mtd_info *mtd);
	void (*_put_device) (struct mtd_info *mtd);

	/* Backing device capabilities for this device
	 * - provides mmap capabilities
	 */
	struct backing_dev_info *backing_dev_info;

	struct notifier_block reboot_notifier;  /* default mode before reboot */

	/* ECC status information */
	struct mtd_ecc_stats ecc_stats;
	/* Subpage shift (NAND) */
	int subpage_sft;

	void *priv;

	struct module *owner;
	struct device dev;
	int usecount;
};

int mtd_erase(struct mtd_info *mtd, struct erase_info *instr);
int mtd_point(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
	      void **virt, resource_size_t *phys);
int mtd_unpoint(struct mtd_info *mtd, loff_t from, size_t len);
unsigned long mtd_get_unmapped_area(struct mtd_info *mtd, unsigned long len,
				    unsigned long offset, unsigned long flags);
int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
	     u_char *buf);
int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
	      const u_char *buf);
int mtd_panic_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
		    const u_char *buf);

int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops);

static inline int mtd_write_oob(struct mtd_info *mtd, loff_t to,
				struct mtd_oob_ops *ops)
{
	ops->retlen = ops->oobretlen = 0;
	if (!mtd->_write_oob)
		return -EOPNOTSUPP;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	return mtd->_write_oob(mtd, to, ops);
}

int mtd_get_fact_prot_info(struct mtd_info *mtd, struct otp_info *buf,
			   size_t len);
int mtd_read_fact_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf);
int mtd_get_user_prot_info(struct mtd_info *mtd, struct otp_info *buf,
			   size_t len);
int mtd_read_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf);
int mtd_write_user_prot_reg(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, u_char *buf);
int mtd_lock_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len);

int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
	       unsigned long count, loff_t to, size_t *retlen);

static inline void mtd_sync(struct mtd_info *mtd)
{
	if (mtd->_sync)
		mtd->_sync(mtd);
}

int mtd_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
int mtd_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
int mtd_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len);
int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs);
int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs);

static inline int mtd_suspend(struct mtd_info *mtd)
{
	return mtd->_suspend ? mtd->_suspend(mtd) : 0;
}

static inline void mtd_resume(struct mtd_info *mtd)
{
	if (mtd->_resume)
		mtd->_resume(mtd);
}

static inline uint32_t mtd_div_by_eb(uint64_t sz, struct mtd_info *mtd)
{
	if (mtd->erasesize_shift)
		return sz >> mtd->erasesize_shift;
	do_div(sz, mtd->erasesize);
	return sz;
}

static inline uint32_t mtd_mod_by_eb(uint64_t sz, struct mtd_info *mtd)
{
	if (mtd->erasesize_shift)
		return sz & mtd->erasesize_mask;
	return do_div(sz, mtd->erasesize);
}

static inline uint32_t mtd_div_by_ws(uint64_t sz, struct mtd_info *mtd)
{
	if (mtd->writesize_shift)
		return sz >> mtd->writesize_shift;
	do_div(sz, mtd->writesize);
	return sz;
}

static inline uint32_t mtd_mod_by_ws(uint64_t sz, struct mtd_info *mtd)
{
	if (mtd->writesize_shift)
		return sz & mtd->writesize_mask;
	return do_div(sz, mtd->writesize);
}

static inline int mtd_has_oob(const struct mtd_info *mtd)
{
	return mtd->_read_oob && mtd->_write_oob;
}

static inline int mtd_can_have_bb(const struct mtd_info *mtd)
{
	return !!mtd->_block_isbad;
}

	/* Kernel-side ioctl definitions */

struct mtd_partition;
struct mtd_part_parser_data;

extern int mtd_device_parse_register(struct mtd_info *mtd,
				     const char * const *part_probe_types,
				     struct mtd_part_parser_data *parser_data,
				     const struct mtd_partition *defparts,
				     int defnr_parts);
#define mtd_device_register(master, parts, nr_parts)	\
	mtd_device_parse_register(master, NULL, NULL, parts, nr_parts)
extern int mtd_device_unregister(struct mtd_info *master);
extern struct mtd_info *get_mtd_device(struct mtd_info *mtd, int num);
extern int __get_mtd_device(struct mtd_info *mtd);
extern void __put_mtd_device(struct mtd_info *mtd);
extern struct mtd_info *get_mtd_device_nm(const char *name);
extern void put_mtd_device(struct mtd_info *mtd);


struct mtd_notifier {
	void (*add)(struct mtd_info *mtd);
	void (*remove)(struct mtd_info *mtd);
	struct list_head list;
};


extern void register_mtd_user (struct mtd_notifier *new);
extern int unregister_mtd_user (struct mtd_notifier *old);
void *mtd_kmalloc_up_to(const struct mtd_info *mtd, size_t *size);

void mtd_erase_callback(struct erase_info *instr);

static inline int mtd_is_bitflip(int err) {
	return err == -EUCLEAN;
}

static inline int mtd_is_eccerr(int err) {
	return err == -EBADMSG;
}

static inline int mtd_is_bitflip_or_eccerr(int err) {
	return mtd_is_bitflip(err) || mtd_is_eccerr(err);
}

#endif /* __MTD_MTD_H__ */
