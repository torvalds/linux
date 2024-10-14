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

struct nand_device;

/**
 * struct nand_memory_organization - Memory organization structure
 * @bits_per_cell: number of bits per NAND cell
 * @pagesize: page size
 * @oobsize: OOB area size
 * @pages_per_eraseblock: number of pages per eraseblock
 * @eraseblocks_per_lun: number of eraseblocks per LUN (Logical Unit Number)
 * @max_bad_eraseblocks_per_lun: maximum number of eraseblocks per LUN
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
	unsigned int max_bad_eraseblocks_per_lun;
	unsigned int planes_per_lun;
	unsigned int luns_per_target;
	unsigned int ntargets;
};

#define NAND_MEMORG(bpc, ps, os, ppe, epl, mbb, ppl, lpt, nt)	\
	{							\
		.bits_per_cell = (bpc),				\
		.pagesize = (ps),				\
		.oobsize = (os),				\
		.pages_per_eraseblock = (ppe),			\
		.eraseblocks_per_lun = (epl),			\
		.max_bad_eraseblocks_per_lun = (mbb),		\
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
 * enum nand_page_io_req_type - Direction of an I/O request
 * @NAND_PAGE_READ: from the chip, to the controller
 * @NAND_PAGE_WRITE: from the controller, to the chip
 */
enum nand_page_io_req_type {
	NAND_PAGE_READ = 0,
	NAND_PAGE_WRITE,
};

/**
 * struct nand_page_io_req - NAND I/O request object
 * @type: the type of page I/O: read or write
 * @pos: the position this I/O request is targeting
 * @dataoffs: the offset within the page
 * @datalen: number of data bytes to read from/write to this page
 * @databuf: buffer to store data in or get data from
 * @ooboffs: the OOB offset within the page
 * @ooblen: the number of OOB bytes to read from/write to this page
 * @oobbuf: buffer to store OOB data in or get OOB data from
 * @mode: one of the %MTD_OPS_XXX mode
 * @continuous: no need to start over the operation at the end of each page, the
 * NAND device will automatically prepare the next one
 *
 * This object is used to pass per-page I/O requests to NAND sub-layers. This
 * way all useful information are already formatted in a useful way and
 * specific NAND layers can focus on translating these information into
 * specific commands/operations.
 */
struct nand_page_io_req {
	enum nand_page_io_req_type type;
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
	bool continuous;
};

const struct mtd_ooblayout_ops *nand_get_small_page_ooblayout(void);
const struct mtd_ooblayout_ops *nand_get_large_page_ooblayout(void);
const struct mtd_ooblayout_ops *nand_get_large_page_hamming_ooblayout(void);

/**
 * enum nand_ecc_engine_type - NAND ECC engine type
 * @NAND_ECC_ENGINE_TYPE_INVALID: Invalid value
 * @NAND_ECC_ENGINE_TYPE_NONE: No ECC correction
 * @NAND_ECC_ENGINE_TYPE_SOFT: Software ECC correction
 * @NAND_ECC_ENGINE_TYPE_ON_HOST: On host hardware ECC correction
 * @NAND_ECC_ENGINE_TYPE_ON_DIE: On chip hardware ECC correction
 */
enum nand_ecc_engine_type {
	NAND_ECC_ENGINE_TYPE_INVALID,
	NAND_ECC_ENGINE_TYPE_NONE,
	NAND_ECC_ENGINE_TYPE_SOFT,
	NAND_ECC_ENGINE_TYPE_ON_HOST,
	NAND_ECC_ENGINE_TYPE_ON_DIE,
};

/**
 * enum nand_ecc_placement - NAND ECC bytes placement
 * @NAND_ECC_PLACEMENT_UNKNOWN: The actual position of the ECC bytes is unknown
 * @NAND_ECC_PLACEMENT_OOB: The ECC bytes are located in the OOB area
 * @NAND_ECC_PLACEMENT_INTERLEAVED: Syndrome layout, there are ECC bytes
 *                                  interleaved with regular data in the main
 *                                  area
 */
enum nand_ecc_placement {
	NAND_ECC_PLACEMENT_UNKNOWN,
	NAND_ECC_PLACEMENT_OOB,
	NAND_ECC_PLACEMENT_INTERLEAVED,
};

/**
 * enum nand_ecc_algo - NAND ECC algorithm
 * @NAND_ECC_ALGO_UNKNOWN: Unknown algorithm
 * @NAND_ECC_ALGO_HAMMING: Hamming algorithm
 * @NAND_ECC_ALGO_BCH: Bose-Chaudhuri-Hocquenghem algorithm
 * @NAND_ECC_ALGO_RS: Reed-Solomon algorithm
 */
enum nand_ecc_algo {
	NAND_ECC_ALGO_UNKNOWN,
	NAND_ECC_ALGO_HAMMING,
	NAND_ECC_ALGO_BCH,
	NAND_ECC_ALGO_RS,
};

/**
 * struct nand_ecc_props - NAND ECC properties
 * @engine_type: ECC engine type
 * @placement: OOB placement (if relevant)
 * @algo: ECC algorithm (if relevant)
 * @strength: ECC strength
 * @step_size: Number of bytes per step
 * @flags: Misc properties
 */
struct nand_ecc_props {
	enum nand_ecc_engine_type engine_type;
	enum nand_ecc_placement placement;
	enum nand_ecc_algo algo;
	unsigned int strength;
	unsigned int step_size;
	unsigned int flags;
};

#define NAND_ECCREQ(str, stp) { .strength = (str), .step_size = (stp) }

/* NAND ECC misc flags */
#define NAND_ECC_MAXIMIZE_STRENGTH BIT(0)

/**
 * struct nand_bbt - bad block table object
 * @cache: in memory BBT cache
 */
struct nand_bbt {
	unsigned long *cache;
};

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
 * struct nand_ecc_context - Context for the ECC engine
 * @conf: basic ECC engine parameters
 * @nsteps: number of ECC steps
 * @total: total number of bytes used for storing ECC codes, this is used by
 *         generic OOB layouts
 * @priv: ECC engine driver private data
 */
struct nand_ecc_context {
	struct nand_ecc_props conf;
	unsigned int nsteps;
	unsigned int total;
	void *priv;
};

/**
 * struct nand_ecc_engine_ops - ECC engine operations
 * @init_ctx: given a desired user configuration for the pointed NAND device,
 *            requests the ECC engine driver to setup a configuration with
 *            values it supports.
 * @cleanup_ctx: clean the context initialized by @init_ctx.
 * @prepare_io_req: is called before reading/writing a page to prepare the I/O
 *                  request to be performed with ECC correction.
 * @finish_io_req: is called after reading/writing a page to terminate the I/O
 *                 request and ensure proper ECC correction.
 */
struct nand_ecc_engine_ops {
	int (*init_ctx)(struct nand_device *nand);
	void (*cleanup_ctx)(struct nand_device *nand);
	int (*prepare_io_req)(struct nand_device *nand,
			      struct nand_page_io_req *req);
	int (*finish_io_req)(struct nand_device *nand,
			     struct nand_page_io_req *req);
};

/**
 * enum nand_ecc_engine_integration - How the NAND ECC engine is integrated
 * @NAND_ECC_ENGINE_INTEGRATION_INVALID: Invalid value
 * @NAND_ECC_ENGINE_INTEGRATION_PIPELINED: Pipelined engine, performs on-the-fly
 *                                         correction, does not need to copy
 *                                         data around
 * @NAND_ECC_ENGINE_INTEGRATION_EXTERNAL: External engine, needs to bring the
 *                                        data into its own area before use
 */
enum nand_ecc_engine_integration {
	NAND_ECC_ENGINE_INTEGRATION_INVALID,
	NAND_ECC_ENGINE_INTEGRATION_PIPELINED,
	NAND_ECC_ENGINE_INTEGRATION_EXTERNAL,
};

/**
 * struct nand_ecc_engine - ECC engine abstraction for NAND devices
 * @dev: Host device
 * @node: Private field for registration time
 * @ops: ECC engine operations
 * @integration: How the engine is integrated with the host
 *               (only relevant on %NAND_ECC_ENGINE_TYPE_ON_HOST engines)
 * @priv: Private data
 */
struct nand_ecc_engine {
	struct device *dev;
	struct list_head node;
	struct nand_ecc_engine_ops *ops;
	enum nand_ecc_engine_integration integration;
	void *priv;
};

void of_get_nand_ecc_user_config(struct nand_device *nand);
int nand_ecc_init_ctx(struct nand_device *nand);
void nand_ecc_cleanup_ctx(struct nand_device *nand);
int nand_ecc_prepare_io_req(struct nand_device *nand,
			    struct nand_page_io_req *req);
int nand_ecc_finish_io_req(struct nand_device *nand,
			   struct nand_page_io_req *req);
bool nand_ecc_is_strong_enough(struct nand_device *nand);

#if IS_REACHABLE(CONFIG_MTD_NAND_CORE)
int nand_ecc_register_on_host_hw_engine(struct nand_ecc_engine *engine);
int nand_ecc_unregister_on_host_hw_engine(struct nand_ecc_engine *engine);
#else
static inline int
nand_ecc_register_on_host_hw_engine(struct nand_ecc_engine *engine)
{
	return -ENOTSUPP;
}
static inline int
nand_ecc_unregister_on_host_hw_engine(struct nand_ecc_engine *engine)
{
	return -ENOTSUPP;
}
#endif

struct nand_ecc_engine *nand_ecc_get_sw_engine(struct nand_device *nand);
struct nand_ecc_engine *nand_ecc_get_on_die_hw_engine(struct nand_device *nand);
struct nand_ecc_engine *nand_ecc_get_on_host_hw_engine(struct nand_device *nand);
void nand_ecc_put_on_host_hw_engine(struct nand_device *nand);
struct device *nand_ecc_get_engine_dev(struct device *host);

#if IS_ENABLED(CONFIG_MTD_NAND_ECC_SW_HAMMING)
struct nand_ecc_engine *nand_ecc_sw_hamming_get_engine(void);
#else
static inline struct nand_ecc_engine *nand_ecc_sw_hamming_get_engine(void)
{
	return NULL;
}
#endif /* CONFIG_MTD_NAND_ECC_SW_HAMMING */

#if IS_ENABLED(CONFIG_MTD_NAND_ECC_SW_BCH)
struct nand_ecc_engine *nand_ecc_sw_bch_get_engine(void);
#else
static inline struct nand_ecc_engine *nand_ecc_sw_bch_get_engine(void)
{
	return NULL;
}
#endif /* CONFIG_MTD_NAND_ECC_SW_BCH */

/**
 * struct nand_ecc_req_tweak_ctx - Help for automatically tweaking requests
 * @orig_req: Pointer to the original IO request
 * @nand: Related NAND device, to have access to its memory organization
 * @page_buffer_size: Real size of the page buffer to use (can be set by the
 *                    user before the tweaking mechanism initialization)
 * @oob_buffer_size: Real size of the OOB buffer to use (can be set by the
 *                   user before the tweaking mechanism initialization)
 * @spare_databuf: Data bounce buffer
 * @spare_oobbuf: OOB bounce buffer
 * @bounce_data: Flag indicating a data bounce buffer is used
 * @bounce_oob: Flag indicating an OOB bounce buffer is used
 */
struct nand_ecc_req_tweak_ctx {
	struct nand_page_io_req orig_req;
	struct nand_device *nand;
	unsigned int page_buffer_size;
	unsigned int oob_buffer_size;
	void *spare_databuf;
	void *spare_oobbuf;
	bool bounce_data;
	bool bounce_oob;
};

int nand_ecc_init_req_tweaking(struct nand_ecc_req_tweak_ctx *ctx,
			       struct nand_device *nand);
void nand_ecc_cleanup_req_tweaking(struct nand_ecc_req_tweak_ctx *ctx);
void nand_ecc_tweak_req(struct nand_ecc_req_tweak_ctx *ctx,
			struct nand_page_io_req *req);
void nand_ecc_restore_req(struct nand_ecc_req_tweak_ctx *ctx,
			  struct nand_page_io_req *req);

/**
 * struct nand_ecc - Information relative to the ECC
 * @defaults: Default values, depend on the underlying subsystem
 * @requirements: ECC requirements from the NAND chip perspective
 * @user_conf: User desires in terms of ECC parameters
 * @ctx: ECC context for the ECC engine, derived from the device @requirements
 *       the @user_conf and the @defaults
 * @ondie_engine: On-die ECC engine reference, if any
 * @engine: ECC engine actually bound
 */
struct nand_ecc {
	struct nand_ecc_props defaults;
	struct nand_ecc_props requirements;
	struct nand_ecc_props user_conf;
	struct nand_ecc_context ctx;
	struct nand_ecc_engine *ondie_engine;
	struct nand_ecc_engine *engine;
};

/**
 * struct nand_device - NAND device
 * @mtd: MTD instance attached to the NAND device
 * @memorg: memory layout
 * @ecc: NAND ECC object attached to the NAND device
 * @rowconv: position to row address converter
 * @bbt: bad block table info
 * @ops: NAND operations attached to the NAND device
 *
 * Generic NAND object. Specialized NAND layers (raw NAND, SPI NAND, OneNAND)
 * should declare their own NAND object embedding a nand_device struct (that's
 * how inheritance is done).
 * struct_nand_device->memorg and struct_nand_device->ecc.requirements should
 * be filled at device detection time to reflect the NAND device
 * capabilities/requirements. Once this is done nanddev_init() can be called.
 * It will take care of converting NAND information into MTD ones, which means
 * the specialized NAND layers should never manually tweak
 * struct_nand_device->mtd except for the ->_read/write() hooks.
 */
struct nand_device {
	struct mtd_info mtd;
	struct nand_memory_organization memorg;
	struct nand_ecc ecc;
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
 * nanddev_pages_per_target() - Get the number of pages per target
 * @nand: NAND device
 *
 * Return: the number of pages per target.
 */
static inline unsigned int
nanddev_pages_per_target(const struct nand_device *nand)
{
	return nand->memorg.pages_per_eraseblock *
	       nand->memorg.eraseblocks_per_lun *
	       nand->memorg.luns_per_target;
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
 * nanddev_eraseblocks_per_target() - Get the number of eraseblocks per target
 * @nand: NAND device
 *
 * Return: the number of eraseblocks per target.
 */
static inline unsigned int
nanddev_eraseblocks_per_target(const struct nand_device *nand)
{
	return nand->memorg.eraseblocks_per_lun * nand->memorg.luns_per_target;
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
 * nanddev_neraseblocks() - Get the total number of eraseblocks
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

/**
 * nanddev_get_ecc_conf() - Extract the ECC configuration from a NAND device
 * @nand: NAND device
 */
static inline const struct nand_ecc_props *
nanddev_get_ecc_conf(struct nand_device *nand)
{
	return &nand->ecc.ctx.conf;
}

/**
 * nanddev_get_ecc_nsteps() - Extract the number of ECC steps
 * @nand: NAND device
 */
static inline unsigned int
nanddev_get_ecc_nsteps(struct nand_device *nand)
{
	return nand->ecc.ctx.nsteps;
}

/**
 * nanddev_get_ecc_bytes_per_step() - Extract the number of ECC bytes per step
 * @nand: NAND device
 */
static inline unsigned int
nanddev_get_ecc_bytes_per_step(struct nand_device *nand)
{
	return nand->ecc.ctx.total / nand->ecc.ctx.nsteps;
}

/**
 * nanddev_get_ecc_requirements() - Extract the ECC requirements from a NAND
 *                                  device
 * @nand: NAND device
 */
static inline const struct nand_ecc_props *
nanddev_get_ecc_requirements(struct nand_device *nand)
{
	return &nand->ecc.requirements;
}

/**
 * nanddev_set_ecc_requirements() - Assign the ECC requirements of a NAND
 *                                  device
 * @nand: NAND device
 * @reqs: Requirements
 */
static inline void
nanddev_set_ecc_requirements(struct nand_device *nand,
			     const struct nand_ecc_props *reqs)
{
	nand->ecc.requirements = *reqs;
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
 * nand_io_page_iter_init - Initialize a NAND I/O iterator
 * @nand: NAND device
 * @offs: absolute offset
 * @req: MTD request
 * @iter: NAND I/O iterator
 *
 * Initializes a NAND iterator based on the information passed by the MTD
 * layer for page jumps.
 */
static inline void nanddev_io_page_iter_init(struct nand_device *nand,
					     enum nand_page_io_req_type reqtype,
					     loff_t offs, struct mtd_oob_ops *req,
					     struct nand_io_iter *iter)
{
	struct mtd_info *mtd = nanddev_to_mtd(nand);

	iter->req.type = reqtype;
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
	iter->req.continuous = false;
}

/**
 * nand_io_block_iter_init - Initialize a NAND I/O iterator
 * @nand: NAND device
 * @offs: absolute offset
 * @req: MTD request
 * @iter: NAND I/O iterator
 *
 * Initializes a NAND iterator based on the information passed by the MTD
 * layer for block jumps (no OOB)
 *
 * In practice only reads may leverage this iterator.
 */
static inline void nanddev_io_block_iter_init(struct nand_device *nand,
					      enum nand_page_io_req_type reqtype,
					      loff_t offs, struct mtd_oob_ops *req,
					      struct nand_io_iter *iter)
{
	unsigned int offs_in_eb;

	iter->req.type = reqtype;
	iter->req.mode = req->mode;
	iter->req.dataoffs = nanddev_offs_to_pos(nand, offs, &iter->req.pos);
	iter->req.ooboffs = 0;
	iter->oobbytes_per_page = 0;
	iter->dataleft = req->len;
	iter->oobleft = 0;
	iter->req.databuf.in = req->datbuf;
	offs_in_eb = (nand->memorg.pagesize * iter->req.pos.page) + iter->req.dataoffs;
	iter->req.datalen = min_t(unsigned int,
				  nanddev_eraseblock_size(nand) - offs_in_eb,
				  iter->dataleft);
	iter->req.oobbuf.in = NULL;
	iter->req.ooblen = 0;
	iter->req.continuous = true;
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
 * nand_io_iter_next_block - Move to the next block
 * @nand: NAND device
 * @iter: NAND I/O iterator
 *
 * Updates the @iter to point to the next block.
 * No OOB handling available.
 */
static inline void nanddev_io_iter_next_block(struct nand_device *nand,
					      struct nand_io_iter *iter)
{
	nanddev_pos_next_eraseblock(nand, &iter->req.pos);
	iter->dataleft -= iter->req.datalen;
	iter->req.databuf.in += iter->req.datalen;
	iter->req.dataoffs = 0;
	iter->req.datalen = min_t(unsigned int, nanddev_eraseblock_size(nand),
				  iter->dataleft);
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
 * Should be used for iterating over pages that are contained in an MTD request.
 */
#define nanddev_io_for_each_page(nand, type, start, req, iter)		\
	for (nanddev_io_page_iter_init(nand, type, start, req, iter);	\
	     !nanddev_io_iter_end(nand, iter);				\
	     nanddev_io_iter_next_page(nand, iter))

/**
 * nand_io_for_each_block - Iterate over all NAND pages contained in an MTD I/O
 *			    request, one block at a time
 * @nand: NAND device
 * @start: start address to read/write from
 * @req: MTD I/O request
 * @iter: NAND I/O iterator
 *
 * Should be used for iterating over blocks that are contained in an MTD request.
 */
#define nanddev_io_for_each_block(nand, type, start, req, iter)		\
	for (nanddev_io_block_iter_init(nand, type, start, req, iter);	\
	     !nanddev_io_iter_end(nand, iter);				\
	     nanddev_io_iter_next_block(nand, iter))

bool nanddev_isbad(struct nand_device *nand, const struct nand_pos *pos);
bool nanddev_isreserved(struct nand_device *nand, const struct nand_pos *pos);
int nanddev_markbad(struct nand_device *nand, const struct nand_pos *pos);

/* ECC related functions */
int nanddev_ecc_engine_init(struct nand_device *nand);
void nanddev_ecc_engine_cleanup(struct nand_device *nand);

static inline void *nand_to_ecc_ctx(struct nand_device *nand)
{
	return nand->ecc.ctx.priv;
}

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
int nanddev_mtd_max_bad_blocks(struct mtd_info *mtd, loff_t offs, size_t len);

#endif /* __LINUX_MTD_NAND_H */
