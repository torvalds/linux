/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org> et al.
 */

#ifndef __MTD_MTD_H__
#define __MTD_MTD_H__

#include <linux/types.h>
#include <linux/uio.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/nvmem-provider.h>

#include <mtd/mtd-abi.h>

#include <asm/div64.h>

#define MTD_FAIL_ADDR_UNKNOWN -1LL

struct mtd_info;

/*
 * If the erase fails, fail_addr might indicate exactly which block failed. If
 * fail_addr = MTD_FAIL_ADDR_UNKNOWN, the failure was not at the device level
 * or was not specific to any particular block.
 */
struct erase_info {
	uint64_t addr;
	uint64_t len;
	uint64_t fail_addr;
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
 * Note, some MTD drivers do not allow you to write more than one OOB area at
 * one go. If you try to do that on such an MTD device, -EINVAL will be
 * returned. If you want to make your implementation portable on all kind of MTD
 * devices you should split the write request into several sub-requests when the
 * request crosses a page boundary.
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
/**
 * struct mtd_oob_region - oob region definition
 * @offset: region offset
 * @length: region length
 *
 * This structure describes a region of the OOB area, and is used
 * to retrieve ECC or free bytes sections.
 * Each section is defined by an offset within the OOB area and a
 * length.
 */
struct mtd_oob_region {
	u32 offset;
	u32 length;
};

/*
 * struct mtd_ooblayout_ops - NAND OOB layout operations
 * @ecc: function returning an ECC region in the OOB area.
 *	 Should return -ERANGE if %section exceeds the total number of
 *	 ECC sections.
 * @free: function returning a free region in the OOB area.
 *	  Should return -ERANGE if %section exceeds the total number of
 *	  free sections.
 */
struct mtd_ooblayout_ops {
	int (*ecc)(struct mtd_info *mtd, int section,
		   struct mtd_oob_region *oobecc);
	int (*free)(struct mtd_info *mtd, int section,
		    struct mtd_oob_region *oobfree);
};

/**
 * struct mtd_pairing_info - page pairing information
 *
 * @pair: pair id
 * @group: group id
 *
 * The term "pair" is used here, even though TLC NANDs might group pages by 3
 * (3 bits in a single cell). A pair should regroup all pages that are sharing
 * the same cell. Pairs are then indexed in ascending order.
 *
 * @group is defining the position of a page in a given pair. It can also be
 * seen as the bit position in the cell: page attached to bit 0 belongs to
 * group 0, page attached to bit 1 belongs to group 1, etc.
 *
 * Example:
 * The H27UCG8T2BTR-BC datasheet describes the following pairing scheme:
 *
 *		group-0		group-1
 *
 *  pair-0	page-0		page-4
 *  pair-1	page-1		page-5
 *  pair-2	page-2		page-8
 *  ...
 *  pair-127	page-251	page-255
 *
 *
 * Note that the "group" and "pair" terms were extracted from Samsung and
 * Hynix datasheets, and might be referenced under other names in other
 * datasheets (Micron is describing this concept as "shared pages").
 */
struct mtd_pairing_info {
	int pair;
	int group;
};

/**
 * struct mtd_pairing_scheme - page pairing scheme description
 *
 * @ngroups: number of groups. Should be related to the number of bits
 *	     per cell.
 * @get_info: converts a write-unit (page number within an erase block) into
 *	      mtd_pairing information (pair + group). This function should
 *	      fill the info parameter based on the wunit index or return
 *	      -EINVAL if the wunit parameter is invalid.
 * @get_wunit: converts pairing information into a write-unit (page) number.
 *	       This function should return the wunit index pointed by the
 *	       pairing information described in the info argument. It should
 *	       return -EINVAL, if there's no wunit corresponding to the
 *	       passed pairing information.
 *
 * See mtd_pairing_info documentation for a detailed explanation of the
 * pair and group concepts.
 *
 * The mtd_pairing_scheme structure provides a generic solution to represent
 * NAND page pairing scheme. Instead of exposing two big tables to do the
 * write-unit <-> (pair + group) conversions, we ask the MTD drivers to
 * implement the ->get_info() and ->get_wunit() functions.
 *
 * MTD users will then be able to query these information by using the
 * mtd_pairing_info_to_wunit() and mtd_wunit_to_pairing_info() helpers.
 *
 * @ngroups is here to help MTD users iterating over all the pages in a
 * given pair. This value can be retrieved by MTD users using the
 * mtd_pairing_groups() helper.
 *
 * Examples are given in the mtd_pairing_info_to_wunit() and
 * mtd_wunit_to_pairing_info() documentation.
 */
struct mtd_pairing_scheme {
	int ngroups;
	int (*get_info)(struct mtd_info *mtd, int wunit,
			struct mtd_pairing_info *info);
	int (*get_wunit)(struct mtd_info *mtd,
			 const struct mtd_pairing_info *info);
};

struct module;	/* only needed for owner field in mtd_info */

/**
 * struct mtd_debug_info - debugging information for an MTD device.
 *
 * @dfs_dir: direntry object of the MTD device debugfs directory
 */
struct mtd_debug_info {
	struct dentry *dfs_dir;

	const char *partname;
	const char *partid;
};

/**
 * struct mtd_part - MTD partition specific fields
 *
 * @node: list node used to add an MTD partition to the parent partition list
 * @offset: offset of the partition relatively to the parent offset
 * @flags: original flags (before the mtdpart logic decided to tweak them based
 *	   on flash constraints, like eraseblock/pagesize alignment)
 *
 * This struct is embedded in mtd_info and contains partition-specific
 * properties/fields.
 */
struct mtd_part {
	struct list_head node;
	u64 offset;
	u32 flags;
};

/**
 * struct mtd_master - MTD master specific fields
 *
 * @partitions_lock: lock protecting accesses to the partition list. Protects
 *		     not only the master partition list, but also all
 *		     sub-partitions.
 * @suspended: et to 1 when the device is suspended, 0 otherwise
 *
 * This struct is embedded in mtd_info and contains master-specific
 * properties/fields. The master is the root MTD device from the MTD partition
 * point of view.
 */
struct mtd_master {
	struct mutex partitions_lock;
	unsigned int suspended : 1;
};

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

	/* Kernel-only stuff starts here. */
	const char *name;
	int index;

	/* OOB layout description */
	const struct mtd_ooblayout_ops *ooblayout;

	/* NAND pairing scheme, only provided for MLC/TLC NANDs */
	const struct mtd_pairing_scheme *pairing;

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
	int (*_get_fact_prot_info) (struct mtd_info *mtd, size_t len,
				    size_t *retlen, struct otp_info *buf);
	int (*_read_fact_prot_reg) (struct mtd_info *mtd, loff_t from,
				    size_t len, size_t *retlen, u_char *buf);
	int (*_get_user_prot_info) (struct mtd_info *mtd, size_t len,
				    size_t *retlen, struct otp_info *buf);
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
	int (*_block_isreserved) (struct mtd_info *mtd, loff_t ofs);
	int (*_block_isbad) (struct mtd_info *mtd, loff_t ofs);
	int (*_block_markbad) (struct mtd_info *mtd, loff_t ofs);
	int (*_max_bad_blocks) (struct mtd_info *mtd, loff_t ofs, size_t len);
	int (*_suspend) (struct mtd_info *mtd);
	void (*_resume) (struct mtd_info *mtd);
	void (*_reboot) (struct mtd_info *mtd);
	/*
	 * If the driver is something smart, like UBI, it may need to maintain
	 * its own reference counting. The below functions are only for driver.
	 */
	int (*_get_device) (struct mtd_info *mtd);
	void (*_put_device) (struct mtd_info *mtd);

	/*
	 * flag indicates a panic write, low level drivers can take appropriate
	 * action if required to ensure writes go through
	 */
	bool oops_panic_write;

	struct notifier_block reboot_notifier;  /* default mode before reboot */

	/* ECC status information */
	struct mtd_ecc_stats ecc_stats;
	/* Subpage shift (NAND) */
	int subpage_sft;

	void *priv;

	struct module *owner;
	struct device dev;
	int usecount;
	struct mtd_debug_info dbg;
	struct nvmem_device *nvmem;

	/*
	 * Parent device from the MTD partition point of view.
	 *
	 * MTD masters do not have any parent, MTD partitions do. The parent
	 * MTD device can itself be a partition.
	 */
	struct mtd_info *parent;

	/* List of partitions attached to this MTD device */
	struct list_head partitions;

	union {
		struct mtd_part part;
		struct mtd_master master;
	};
};

static inline struct mtd_info *mtd_get_master(struct mtd_info *mtd)
{
	while (mtd->parent)
		mtd = mtd->parent;

	return mtd;
}

static inline u64 mtd_get_master_ofs(struct mtd_info *mtd, u64 ofs)
{
	while (mtd->parent) {
		ofs += mtd->part.offset;
		mtd = mtd->parent;
	}

	return ofs;
}

static inline bool mtd_is_partition(const struct mtd_info *mtd)
{
	return mtd->parent;
}

static inline bool mtd_has_partitions(const struct mtd_info *mtd)
{
	return !list_empty(&mtd->partitions);
}

int mtd_ooblayout_ecc(struct mtd_info *mtd, int section,
		      struct mtd_oob_region *oobecc);
int mtd_ooblayout_find_eccregion(struct mtd_info *mtd, int eccbyte,
				 int *section,
				 struct mtd_oob_region *oobregion);
int mtd_ooblayout_get_eccbytes(struct mtd_info *mtd, u8 *eccbuf,
			       const u8 *oobbuf, int start, int nbytes);
int mtd_ooblayout_set_eccbytes(struct mtd_info *mtd, const u8 *eccbuf,
			       u8 *oobbuf, int start, int nbytes);
int mtd_ooblayout_free(struct mtd_info *mtd, int section,
		       struct mtd_oob_region *oobfree);
int mtd_ooblayout_get_databytes(struct mtd_info *mtd, u8 *databuf,
				const u8 *oobbuf, int start, int nbytes);
int mtd_ooblayout_set_databytes(struct mtd_info *mtd, const u8 *databuf,
				u8 *oobbuf, int start, int nbytes);
int mtd_ooblayout_count_freebytes(struct mtd_info *mtd);
int mtd_ooblayout_count_eccbytes(struct mtd_info *mtd);

static inline void mtd_set_ooblayout(struct mtd_info *mtd,
				     const struct mtd_ooblayout_ops *ooblayout)
{
	mtd->ooblayout = ooblayout;
}

static inline void mtd_set_pairing_scheme(struct mtd_info *mtd,
				const struct mtd_pairing_scheme *pairing)
{
	mtd->pairing = pairing;
}

static inline void mtd_set_of_node(struct mtd_info *mtd,
				   struct device_node *np)
{
	mtd->dev.of_node = np;
	if (!mtd->name)
		of_property_read_string(np, "label", &mtd->name);
}

static inline struct device_node *mtd_get_of_node(struct mtd_info *mtd)
{
	return dev_of_node(&mtd->dev);
}

static inline u32 mtd_oobavail(struct mtd_info *mtd, struct mtd_oob_ops *ops)
{
	return ops->mode == MTD_OPS_AUTO_OOB ? mtd->oobavail : mtd->oobsize;
}

static inline int mtd_max_bad_blocks(struct mtd_info *mtd,
				     loff_t ofs, size_t len)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->_max_bad_blocks)
		return -ENOTSUPP;

	if (mtd->size < (len + ofs) || ofs < 0)
		return -EINVAL;

	return master->_max_bad_blocks(master, mtd_get_master_ofs(mtd, ofs),
				       len);
}

int mtd_wunit_to_pairing_info(struct mtd_info *mtd, int wunit,
			      struct mtd_pairing_info *info);
int mtd_pairing_info_to_wunit(struct mtd_info *mtd,
			      const struct mtd_pairing_info *info);
int mtd_pairing_groups(struct mtd_info *mtd);
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
int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops);

int mtd_get_fact_prot_info(struct mtd_info *mtd, size_t len, size_t *retlen,
			   struct otp_info *buf);
int mtd_read_fact_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf);
int mtd_get_user_prot_info(struct mtd_info *mtd, size_t len, size_t *retlen,
			   struct otp_info *buf);
int mtd_read_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf);
int mtd_write_user_prot_reg(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, u_char *buf);
int mtd_lock_user_prot_reg(struct mtd_info *mtd, loff_t from, size_t len);

int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
	       unsigned long count, loff_t to, size_t *retlen);

static inline void mtd_sync(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (master->_sync)
		master->_sync(master);
}

int mtd_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
int mtd_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
int mtd_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len);
int mtd_block_isreserved(struct mtd_info *mtd, loff_t ofs);
int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs);
int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs);

static inline int mtd_suspend(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ret;

	if (master->master.suspended)
		return 0;

	ret = master->_suspend ? master->_suspend(master) : 0;
	if (ret)
		return ret;

	master->master.suspended = 1;
	return 0;
}

static inline void mtd_resume(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);

	if (!master->master.suspended)
		return;

	if (master->_resume)
		master->_resume(master);

	master->master.suspended = 0;
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

/**
 * mtd_align_erase_req - Adjust an erase request to align things on eraseblock
 *			 boundaries.
 * @mtd: the MTD device this erase request applies on
 * @req: the erase request to adjust
 *
 * This function will adjust @req->addr and @req->len to align them on
 * @mtd->erasesize. Of course we expect @mtd->erasesize to be != 0.
 */
static inline void mtd_align_erase_req(struct mtd_info *mtd,
				       struct erase_info *req)
{
	u32 mod;

	if (WARN_ON(!mtd->erasesize))
		return;

	mod = mtd_mod_by_eb(req->addr, mtd);
	if (mod) {
		req->addr -= mod;
		req->len += mod;
	}

	mod = mtd_mod_by_eb(req->addr + req->len, mtd);
	if (mod)
		req->len += mtd->erasesize - mod;
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

static inline int mtd_wunit_per_eb(struct mtd_info *mtd)
{
	return mtd->erasesize / mtd->writesize;
}

static inline int mtd_offset_to_wunit(struct mtd_info *mtd, loff_t offs)
{
	return mtd_div_by_ws(mtd_mod_by_eb(offs, mtd), mtd);
}

static inline loff_t mtd_wunit_to_offset(struct mtd_info *mtd, loff_t base,
					 int wunit)
{
	return base + (wunit * mtd->writesize);
}


static inline int mtd_has_oob(const struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master((struct mtd_info *)mtd);

	return master->_read_oob && master->_write_oob;
}

static inline int mtd_type_is_nand(const struct mtd_info *mtd)
{
	return mtd->type == MTD_NANDFLASH || mtd->type == MTD_MLCNANDFLASH;
}

static inline int mtd_can_have_bb(const struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master((struct mtd_info *)mtd);

	return !!master->_block_isbad;
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

static inline int mtd_is_bitflip(int err) {
	return err == -EUCLEAN;
}

static inline int mtd_is_eccerr(int err) {
	return err == -EBADMSG;
}

static inline int mtd_is_bitflip_or_eccerr(int err) {
	return mtd_is_bitflip(err) || mtd_is_eccerr(err);
}

unsigned mtd_mmap_capabilities(struct mtd_info *mtd);

#endif /* __MTD_MTD_H__ */
