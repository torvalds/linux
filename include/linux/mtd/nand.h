/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright 2017 - Free Electrons
 *
 *  Authors:
 *	Boris Brezillon <boris.brezillon@free-electrons.com>
 *	Peter Pan <peterpandong@micron.com>
 */

#ifndef __LINUX_MTD_NAND_H
#define __LINUX_MTD_NAND_H

#include <linux/mtd/mtd.h>

/**
 * struct nand_memory_organization - Memory organization structure
 * @bits_per_cell: number of bits per NAND cell
 * @pagesize: page size
 * @oobsize: OOB area size
 * @pages_per_eraseblock: number of pages per eraseblock
 * @eraseblocks_per_lun: number of eraseblocks per LUN (Logical Unit Number)
 * @planes_per_lun: number of planes per LUN
 * @luns_per_target: number of LUN per target (target is a synonym for die)
 * @ntargets: total number of targets exposed by the NAND device
 */
struct nand_memory_organization {
	unsigned int bits_per_cell;
	unsigned int pagesize;
	unsigned int oobsize;
	unsigned int pages_per_eraseblock;
	unsigned int eraseblocks_per_lun;
	unsigned int planes_per_lun;
	unsigned int luns_per_target;
	unsigned int ntargets;
};

#define NAND_MEMORG(bpc, ps, os, ppe, epl, ppl, lpt, nt)	\
	{							\
		.bits_per_cell = (bpc),				\
		.pagesize = (ps),				\
		.oobsize = (os),				\
		.pages_per_eraseblock = (ppe),			\
		.eraseblocks_per_lun = (epl),			\
		.planes_per_lun = (ppl),			\
		.luns_per_target = (lpt),			\
		.ntargets = (nt),				\
	}

/**
 * struct nand_row_converter - Information needed to convert an absolute offset
 *			       into a row address
 * @lun_addr_shift: position of the LUN identifier in the row address
 * @eraseblock_addr_shift: position of the eraseblock identifier in the row
 *			   address
 */
struct nand_row_converter {
	unsigned int lun_addr_shift;
	unsigned int eraseblock_addr_shift;
};

/**
 * struct nand_pos - NAND position object
 * @target: the NAND target/die
 * @lun: the LUN identifier
 * @plane: the plane within the LUN
 * @eraseblock: the eraseblock within the LUN
 * @page: the page within the LUN
 *
 * These information are usually used by specific sub-layers to select the
 * appropriate target/die and generate a row address to pass to the device.
 */
struct nand_pos {
	unsigned int target;
	unsigned int lun;
	unsigned int plane;
	unsigned int eraseblock;
	unsigned int page;
};

/**
 * struct nand_page_io_req - NAND I/O request object
 * @pos: the position this I/O request is targeting
 * @dataoffs: the offset within the page
 * @datalen: number of data bytes to read from/write to this page
 * @databuf: buffer to store data in or get data from
 * @ooboffs: the OOB offset within the page
 * @ooblen: the number of OOB bytes to read from/write to this page
 * @oobbuf: buffer to store OOB data in or get OOB data from
 * @mode: one of the %MTD_OPS_XXX mode
 *
 * This object is used to pass per-page I/O requests to NAND sub-layers. This
 * way all useful information are already formatted in a useful way and
 * specific NAND layers can focus on translating these information into
 * specific commands/operations.
 */
struct nand_page_io_req {
	struct nand_pos pos;
	unsigned int dataoffs;
	unsigned int datalen;
	union {
		const void *out;
		void *in;
	} databuf;
	unsigned int ooboffs;
	unsigned int ooblen;
	union {
		const void *out;
		void *in;
	} oobbuf;
	int mode;
};

/**
 * struct nand_ecc_req - NAND ECC requirements
 * @strength: ECC strength
 * @step_size: ECC step/block size
 */
struct nand_ecc_req {
	unsigned int strength;
	unsigned int step_size;
};

#define NAND_ECCREQ(str, stp) { .strength = (str), .step_size = (stp) }

/**
 * struct nand_bbt - bad block table object
 * @cache: in memory BBT cache
 */
struct nand_bbt {
	unsigned long *cache;
};

struct nand_device;

/**
 * struct nand_ops - NAND operations
 * @erase: erase a specific block. No need to check if the block is bad before
 *	   erasing, this has been taken care of by the generic NAND layer
 * @markbad: mark a specific block bad. No need to check if the block is
 *	     already marked bad, this has been taken care of by the generic
 *	     NAND layer. This method should just write the BBM (Bad Block
 *	     Marker) so that future call to struct_nand_ops->isbad() return
 *	     true
 * @isbad: check whether a block is bad or not. This method should just read
 *	   the BBM and return whether the block is bad or not based on what it
 *	   reads
 *
 * These are all low level operations that should be implemented by specialized
 * NAND layers (SPI NAND, raw NAND, ...).
 */
struct nand_ops {
	int (*erase)(struct nand_device *nand, const struct nand_pos *pos);
	int (*markbad)(struct nand_device *nand, const struct nand_pos *pos);
	bool (*isbad)(struct nand_device *nand, const struct nand_pos *pos);
};

/**
 * struct nand_device - NAND device
 * @mtd: MTD instance attached to the NAND device
 * @memorg: memory layout
 * @eccreq: ECC requirements
 * @rowconv: position to row address converter
 * @bbt: bad block table info
 * @ops: NAND operations attached to the NAND device
 *
 * Generic NAND object. Specialized NAND layers (raw NAND, SPI NAND, OneNAND)
 * should declare their own NAND object embedding a nand_device struct (that's
 * how inheritance is done).
 * struct_nand_device->memorg and struct_nand_device->eccreq should be filled
 * at device detection time to reflect the NAND device
 * capabilities/requirements. Once this is done nanddev_init() can be called.
 * It will take care of converting NAND information into MTD ones, which means
 * the specialized NAND layers should never manually tweak
 * struct_nand_device->mtd except for the ->_read/write() hooks.
 */
struct nand_device {
	struct mtd_info mtd;
	struct nand_memory_organization memorg;
	struct nand_ecc_req eccreq;
	struct nand_row_converter rowconv;
	struct nand_bbt bbt;
	const struct nand_ops *ops;
};

/**
 * struct nand_io_iter - NAND I/O iterator
 * @req: current I/O request
 * @oobbytes_per_page: maximum number of OOB bytes per page
 * @dataleft: remaining number of data bytes to read/write
 * @oobleft: remaining number of OOB bytes to read/write
 *
 * Can be used by specialized NAND layers to iterate over all pages covered
 * by an MTD I/O request, which should greatly simplifies the boiler-plate
 * code needed to read/write data from/to a NAND device.
 */
struct nand_io_iter {
	struct nand_page_io_req req;
	unsigned int oobbytes_per_page;
	unsigned int dataleft;
	unsigned int oobleft;
};

/**
 * mtd_to_nanddev() - Get the NAND device attached to the MTD instance
 * @mtd: MTD instance
 *
 * Return: the NAND device embedding @mtd.
 */
static inline struct nand_device *mtd_to_nanddev(struct mtd_info *mtd)
{
	return container_of(mtd, struct nand_device, mtd);
}

/**
 * nanddev_to_mtd() - Get the MTD device attached to a NAND device
 * @nand: NAND device
 *
 * Return: the MTD device embedded in @nand.
 */
static inline struct mtd_info *nanddev_to_mtd(struct nand_device *nand)
{
	return &nand->mtd;
}

/*
 * nanddev_bits_per_cell() - Get the number of bits per cell
 * @nand: NAND device
 *
 * Return: the number of bits per cell.
 */
static inline unsigned int nanddev_bits_per_cell(const struct nand_device *nand)
{
	return nand->memorg.bits_per_cell;
}

/**
 * nanddev_page_size() - Get NAND page size
 * @nand: NAND device
 *
 * Return: the page size.
 */
static inline size_t nanddev_page_size(const struct nand_device *nand)
{
	return nand->memorg.pagesize;
}

/**
 * nanddev_per_page_oobsize() - Get NAND OOB size
 * @nand: NAND device
 *
 * Return: the OOB size.
 */
static inline unsigned int
nanddev_per_page_oobsize(const struct nand_device *nand)
{
	return nand->memorg.oobsize;
}

/**
 * nanddev_pages_per_eraseblock() - Get the number of pages per eraseblock
 * @nand: NAND device
 *
 * Return: the number of pages per eraseblock.
 */
static inline unsigned int
nanddev_pages_per_eraseblock(const struct nand_device *nand)
{
	return nand->memorg.pages_per_eraseblock;
}

/**
 * nanddev_per_page_oobsize() - Get NAND erase block size
 * @nand: NAND device
 *
 * Return: the eraseblock size.
 */
static inline size_t nanddev_eraseblock_size(const struct nand_device *nand)
{
	return nand->memorg.pagesize * nand->memorg.pages_per_eraseblock;
}

/**
 * nanddev_eraseblocks_per_lun() - Get the number of eraseblocks per LUN
 * @nand: NAND device
 *
 * Return: the number of eraseblocks per LUN.
 */
static inline unsigned int
nanddev_eraseblocks_per_lun(const struct nand_device *nand)
{
	return nand->memorg.eraseblocks_per_lun;
}

/**
 * nanddev_target_size() - Get the total size provided by a single target/die
 * @nand: NAND device
 *
 * Return: the total size exposed by a single target/die in bytes.
 */
static inline u64 nanddev_target_size(const struct nand_device *nand)
{
	return (u64)nand->memorg.luns_per_target *
	       nand->memorg.eraseblocks_per_lun *
	       nand->memorg.pages_per_eraseblock *
	       nand->memorg.pagesize;
}

/**
 * nanddev_ntarget() - Get the total of targets
 * @nand: NAND device
 *
 * Return: the number of targets/dies exposed by @nand.
 */
static inline unsigned int nanddev_ntargets(const struct nand_device *nand)
{
	return nand->memorg.ntargets;
}

/**
 * nanddev_neraseblocks() - Get the total number of erasablocks
 * @nand: NAND device
 *
 * Return: the total number of eraseblocks exposed by @nand.
 */
static inline unsigned int nanddev_neraseblocks(const struct nand_device *nand)
{
	return nand->memorg.ntargets * nand->memorg.luns_per_target *
	       nand->memorg.eraseblocks_per_lun;
}

/**
 * nanddev_size() - Get NAND size
 * @nand: NAND device
 *
 * Return: the total size (in bytes) exposed by @nand.
 */
static inline u64 nanddev_size(const struct nand_device *nand)
{
	return nanddev_target_size(nand) * nanddev_ntargets(nand);
}

/**
 * nanddev_get_memorg() - Extract memory organization info from a NAND device
 * @nand: NAND device
 *
 * This can be used by the upper layer to fill the memorg info before calling
 * nanddev_init().
 *
 * Return: the memorg object embedded in the NAND device.
 */
static inline struct nand_memory_organization *
nanddev_get_memorg(struct nand_device *nand)
{
	return &nand->memorg;
}

int nanddev_init(struct nand_device *nand, const struct nand_ops *ops,
		 struct module *owner);
void nanddev_cleanup(struct nand_device *nand);

/**
 * nanddev_register() - Register a NAND device
 * @nand: NAND device
 *
 * Register a NAND device.
 * This function is just a wrapper around mtd_device_register()
 * registering the MTD device embedded in @nand.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
static inline int nanddev_register(struct nand_device *nand)
{
	return mtd_device_register(&nand->mtd, NULL, 0);
}

/**
 * nanddev_unregister() - Unregister a NAND device
 * @nand: NAND device
 *
 * Unregister a NAND device.
 * This function is just a wrapper around mtd_device_unregister()
 * unregistering the MTD device embedded in @nand.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
static inline int nanddev_unregister(struct nand_device *nand)
{
	return mtd_device_unregister(&nand->mtd);
}

/**
 * nanddev_set_of_node() - Attach a DT node to a NAND device
 * @nand: NAND device
 * @np: DT node
 *
 * Attach a DT node to a NAND device.
 */
static inline void nanddev_set_of_node(struct nand_device *nand,
				       struct device_node *np)
{
	mtd_set_of_node(&nand->mtd, np);
}

/**
 * nanddev_get_of_node() - Retrieve the DT node attached to a NAND device
 * @nand: NAND device
 *
 * Return: the DT node attached to @nand.
 */
static inline struct device_node *nanddev_get_of_node(struct nand_device *nand)
{
	return mtd_get_of_node(&nand->mtd);
}

/**
 * nanddev_offs_to_pos() - Convert an absolute NAND offset into a NAND position
 * @nand: NAND device
 * @offs: absolute NAND offset (usually passed by the MTD layer)
 * @pos: a NAND position object to fill in
 *
 * Converts @offs into a nand_pos representation.
 *
 * Return: the offset within the NAND page pointed by @pos.
 */
static inline unsigned int nanddev_offs_to_pos(struct nand_device *nand,
					       loff_t offs,
					       struct nand_pos *pos)
{
	unsigned int pageoffs;
	u64 tmp = offs;

	pageoffs = do_div(tmp, nand->memorg.pagesize);
	pos->page = do_div(tmp, nand->memorg.pages_per_eraseblock);
	pos->eraseblock = do_div(tmp, nand->memorg.eraseblocks_per_lun);
	pos->plane = pos->eraseblock % nand->memorg.planes_per_lun;
	pos->lun = do_div(tmp, nand->memorg.luns_per_target);
	pos->target = tmp;

	return pageoffs;
}

/**
 * nanddev_pos_cmp() - Compare two NAND positions
 * @a: First NAND position
 * @b: Second NAND position
 *
 * Compares two NAND positions.
 *
 * Return: -1 if @a < @b, 0 if @a == @b and 1 if @a > @b.
 */
static inline int nanddev_pos_cmp(const struct nand_pos *a,
				  const struct nand_pos *b)
{
	if (a->target != b->target)
		return a->target < b->target ? -1 : 1;

	if (a->lun != b->lun)
		return a->lun < b->lun ? -1 : 1;

	if (a->eraseblock != b->eraseblock)
		return a->eraseblock < b->eraseblock ? -1 : 1;

	if (a->page != b->page)
		return a->page < b->page ? -1 : 1;

	return 0;
}

/**
 * nanddev_pos_to_offs() - Convert a NAND position into an absolute offset
 * @nand: NAND device
 * @pos: the NAND position to convert
 *
 * Converts @pos NAND position into an absolute offset.
 *
 * Return: the absolute offset. Note that @pos points to the beginning of a
 *	   page, if one wants to point to a specific offset within this page
 *	   the returned offset has to be adjusted manually.
 */
static inline loff_t nanddev_pos_to_offs(struct nand_device *nand,
					 const struct nand_pos *pos)
{
	unsigned int npages;

	npages = pos->page +
		 ((pos->eraseblock +
		   (pos->lun +
		    (pos->target * nand->memorg.luns_per_target)) *
		   nand->memorg.eraseblocks_per_lun) *
		  nand->memorg.pages_per_eraseblock);

	return (loff_t)npages * nand->memorg.pagesize;
}

/**
 * nanddev_pos_to_row() - Extract a row address from a NAND position
 * @nand: NAND device
 * @pos: the position to convert
 *
 * Converts a NAND position into a row address that can then be passed to the
 * device.
 *
 * Return: the row address extracted from @pos.
 */
static inline unsigned int nanddev_pos_to_row(struct nand_device *nand,
					      const struct nand_pos *pos)
{
	return (pos->lun << nand->rowconv.lun_addr_shift) |
	       (pos->eraseblock << nand->rowconv.eraseblock_addr_shift) |
	       pos->page;
}

/**
 * nanddev_pos_next_target() - Move a position to the next target/die
 * @nand: NAND device
 * @pos: the position to update
 *
 * Updates @pos to point to the start of the next target/die. Useful when you
 * want to iterate over all targets/dies of a NAND device.
 */
static inline void nanddev_pos_next_target(struct nand_device *nand,
					   struct nand_pos *pos)
{
	pos->page = 0;
	pos->plane = 0;
	pos->eraseblock = 0;
	pos->lun = 0;
	pos->target++;
}

/**
 * nanddev_pos_next_lun() - Move a position to the next LUN
 * @nand: NAND device
 * @pos: the position to update
 *
 * Updates @pos to point to the start of the next LUN. Useful when you want to
 * iterate over all LUNs of a NAND device.
 */
static inline void nanddev_pos_next_lun(struct nand_device *nand,
					struct nand_pos *pos)
{
	if (pos->lun >= nand->memorg.luns_per_target - 1)
		return nanddev_pos_next_target(nand, pos);

	pos->lun++;
	pos->page = 0;
	pos->plane = 0;
	pos->eraseblock = 0;
}

/**
 * nanddev_pos_next_eraseblock() - Move a position to the next eraseblock
 * @nand: NAND device
 * @pos: the position to update
 *
 * Updates @pos to point to the start of the next eraseblock. Useful when you
 * want to iterate over all eraseblocks of a NAND device.
 */
static inline void nanddev_pos_next_eraseblock(struct nand_device *nand,
					       struct nand_pos *pos)
{
	if (pos->eraseblock >= nand->memorg.eraseblocks_per_lun - 1)
		return nanddev_pos_next_lun(nand, pos);

	pos->eraseblock++;
	pos->page = 0;
	pos->plane = pos->eraseblock % nand->memorg.planes_per_lun;
}

/**
 * nanddev_pos_next_page() - Move a position to the next page
 * @nand: NAND device
 * @pos: the position to update
 *
 * Updates @pos to point to the start of the next page. Useful when you want to
 * iterate over all pages of a NAND device.
 */
static inline void nanddev_pos_next_page(struct nand_device *nand,
					 struct nand_pos *pos)
{
	if (pos->page >= nand->memorg.pages_per_eraseblock - 1)
		return nanddev_pos_next_eraseblock(nand, pos);

	pos->page++;
}

/**
 * nand_io_iter_init - Initialize a NAND I/O iterator
 * @nand: NAND device
 * @offs: absolute offset
 * @req: MTD request
 * @iter: NAND I/O iterator
 *
 * Initializes a NAND iterator based on the information passed by the MTD
 * layer.
 */
static inline void nanddev_io_iter_init(struct nand_device *nand,
					loff_t offs, struct mtd_oob_ops *req,
					struct nand_io_iter *iter)
{
	struct mtd_info *mtd = nanddev_to_mtd(nand);

	iter->req.mode = req->mode;
	iter->req.dataoffs = nanddev_offs_to_pos(nand, offs, &iter->req.pos);
	iter->req.ooboffs = req->ooboffs;
	iter->oobbytes_per_page = mtd_oobavail(mtd, req);
	iter->dataleft = req->len;
	iter->oobleft = req->ooblen;
	iter->req.databuf.in = req->datbuf;
	iter->req.datalen = min_t(unsigned int,
				  nand->memorg.pagesize - iter->req.dataoffs,
				  iter->dataleft);
	iter->req.oobbuf.in = req->oobbuf;
	iter->req.ooblen = min_t(unsigned int,
				 iter->oobbytes_per_page - iter->req.ooboffs,
				 iter->oobleft);
}

/**
 * nand_io_iter_next_page - Move to the next page
 * @nand: NAND device
 * @iter: NAND I/O iterator
 *
 * Updates the @iter to point to the next page.
 */
static inline void nanddev_io_iter_next_page(struct nand_device *nand,
					     struct nand_io_iter *iter)
{
	nanddev_pos_next_page(nand, &iter->req.pos);
	iter->dataleft -= iter->req.datalen;
	iter->req.databuf.in += iter->req.datalen;
	iter->oobleft -= iter->req.ooblen;
	iter->req.oobbuf.in += iter->req.ooblen;
	iter->req.dataoffs = 0;
	iter->req.ooboffs = 0;
	iter->req.datalen = min_t(unsigned int, nand->memorg.pagesize,
				  iter->dataleft);
	iter->req.ooblen = min_t(unsigned int, iter->oobbytes_per_page,
				 iter->oobleft);
}

/**
 * nand_io_iter_end - Should end iteration or not
 * @nand: NAND device
 * @iter: NAND I/O iterator
 *
 * Check whether @iter has reached the end of the NAND portion it was asked to
 * iterate on or not.
 *
 * Return: true if @iter has reached the end of the iteration request, false
 *	   otherwise.
 */
static inline bool nanddev_io_iter_end(struct nand_device *nand,
				       const struct nand_io_iter *iter)
{
	if (iter->dataleft || iter->oobleft)
		return false;

	return true;
}

/**
 * nand_io_for_each_page - Iterate over all NAND pages contained in an MTD I/O
 *			   request
 * @nand: NAND device
 * @start: start address to read/write from
 * @req: MTD I/O request
 * @iter: NAND I/O iterator
 *
 * Should be used for iterate over pages that are contained in an MTD request.
 */
#define nanddev_io_for_each_page(nand, start, req, iter)		\
	for (nanddev_io_iter_init(nand, start, req, iter);		\
	     !nanddev_io_iter_end(nand, iter);				\
	     nanddev_io_iter_next_page(nand, iter))

bool nanddev_isbad(struct nand_device *nand, const struct nand_pos *pos);
bool nanddev_isreserved(struct nand_device *nand, const struct nand_pos *pos);
int nanddev_erase(struct nand_device *nand, const struct nand_pos *pos);
int nanddev_markbad(struct nand_device *nand, const struct nand_pos *pos);

/* BBT related functions */
enum nand_bbt_block_status {
	NAND_BBT_BLOCK_STATUS_UNKNOWN,
	NAND_BBT_BLOCK_GOOD,
	NAND_BBT_BLOCK_WORN,
	NAND_BBT_BLOCK_RESERVED,
	NAND_BBT_BLOCK_FACTORY_BAD,
	NAND_BBT_BLOCK_NUM_STATUS,
};

int nanddev_bbt_init(struct nand_device *nand);
void nanddev_bbt_cleanup(struct nand_device *nand);
int nanddev_bbt_update(struct nand_device *nand);
int nanddev_bbt_get_block_status(const struct nand_device *nand,
				 unsigned int entry);
int nanddev_bbt_set_block_status(struct nand_device *nand, unsigned int entry,
				 enum nand_bbt_block_status status);
int nanddev_bbt_markbad(struct nand_device *nand, unsigned int block);

/**
 * nanddev_bbt_pos_to_entry() - Convert a NAND position into a BBT entry
 * @nand: NAND device
 * @pos: the NAND position we want to get BBT entry for
 *
 * Return the BBT entry used to store information about the eraseblock pointed
 * by @pos.
 *
 * Return: the BBT entry storing information about eraseblock pointed by @pos.
 */
static inline unsigned int nanddev_bbt_pos_to_entry(struct nand_device *nand,
						    const struct nand_pos *pos)
{
	return pos->eraseblock +
	       ((pos->lun + (pos->target * nand->memorg.luns_per_target)) *
		nand->memorg.eraseblocks_per_lun);
}

/**
 * nanddev_bbt_is_initialized() - Check if the BBT has been initialized
 * @nand: NAND device
 *
 * Return: true if the BBT has been initialized, false otherwise.
 */
static inline bool nanddev_bbt_is_initialized(struct nand_device *nand)
{
	return !!nand->bbt.cache;
}

/* MTD -> NAND helper functions. */
int nanddev_mtd_erase(struct mtd_info *mtd, struct erase_info *einfo);

#endif /* __LINUX_MTD_NAND_H */
