/*
 *  Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org>
 *                        Steven J. Hill <sjhill@realitydiluted.com>
 *		          Thomas Gleixner <tglx@linutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Info:
 *	Contains standard defines and IDs for NAND flash devices
 *
 * Changelog:
 *	See git changelog.
 */
#ifndef __LINUX_MTD_RAWNAND_H
#define __LINUX_MTD_RAWNAND_H

#include <linux/mtd/mtd.h>
#include <linux/mtd/flashchip.h>
#include <linux/mtd/bbm.h>
#include <linux/mtd/jedec.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/onfi.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/types.h>

struct nand_chip;

/* The maximum number of NAND chips in an array */
#define NAND_MAX_CHIPS		8

/*
 * Constants for hardware specific CLE/ALE/NCE function
 *
 * These are bits which can be or'ed to set/clear multiple
 * bits in one go.
 */
/* Select the chip by setting nCE to low */
#define NAND_NCE		0x01
/* Select the command latch by setting CLE to high */
#define NAND_CLE		0x02
/* Select the address latch by setting ALE to high */
#define NAND_ALE		0x04

#define NAND_CTRL_CLE		(NAND_NCE | NAND_CLE)
#define NAND_CTRL_ALE		(NAND_NCE | NAND_ALE)
#define NAND_CTRL_CHANGE	0x80

/*
 * Standard NAND flash commands
 */
#define NAND_CMD_READ0		0
#define NAND_CMD_READ1		1
#define NAND_CMD_RNDOUT		5
#define NAND_CMD_PAGEPROG	0x10
#define NAND_CMD_READOOB	0x50
#define NAND_CMD_ERASE1		0x60
#define NAND_CMD_STATUS		0x70
#define NAND_CMD_SEQIN		0x80
#define NAND_CMD_RNDIN		0x85
#define NAND_CMD_READID		0x90
#define NAND_CMD_ERASE2		0xd0
#define NAND_CMD_PARAM		0xec
#define NAND_CMD_GET_FEATURES	0xee
#define NAND_CMD_SET_FEATURES	0xef
#define NAND_CMD_RESET		0xff

/* Extended commands for large page devices */
#define NAND_CMD_READSTART	0x30
#define NAND_CMD_RNDOUTSTART	0xE0
#define NAND_CMD_CACHEDPROG	0x15

#define NAND_CMD_NONE		-1

/* Status bits */
#define NAND_STATUS_FAIL	0x01
#define NAND_STATUS_FAIL_N1	0x02
#define NAND_STATUS_TRUE_READY	0x20
#define NAND_STATUS_READY	0x40
#define NAND_STATUS_WP		0x80

#define NAND_DATA_IFACE_CHECK_ONLY	-1

/*
 * Constants for ECC_MODES
 */
typedef enum {
	NAND_ECC_NONE,
	NAND_ECC_SOFT,
	NAND_ECC_HW,
	NAND_ECC_HW_SYNDROME,
	NAND_ECC_HW_OOB_FIRST,
	NAND_ECC_ON_DIE,
} nand_ecc_modes_t;

enum nand_ecc_algo {
	NAND_ECC_UNKNOWN,
	NAND_ECC_HAMMING,
	NAND_ECC_BCH,
	NAND_ECC_RS,
};

/*
 * Constants for Hardware ECC
 */
/* Reset Hardware ECC for read */
#define NAND_ECC_READ		0
/* Reset Hardware ECC for write */
#define NAND_ECC_WRITE		1
/* Enable Hardware ECC before syndrome is read back from flash */
#define NAND_ECC_READSYN	2

/*
 * Enable generic NAND 'page erased' check. This check is only done when
 * ecc.correct() returns -EBADMSG.
 * Set this flag if your implementation does not fix bitflips in erased
 * pages and you want to rely on the default implementation.
 */
#define NAND_ECC_GENERIC_ERASED_CHECK	BIT(0)
#define NAND_ECC_MAXIMIZE		BIT(1)

/*
 * When using software implementation of Hamming, we can specify which byte
 * ordering should be used.
 */
#define NAND_ECC_SOFT_HAMMING_SM_ORDER	BIT(2)

/*
 * Option constants for bizarre disfunctionality and real
 * features.
 */
/* Buswidth is 16 bit */
#define NAND_BUSWIDTH_16	0x00000002
/* Chip has cache program function */
#define NAND_CACHEPRG		0x00000008
/*
 * Chip requires ready check on read (for auto-incremented sequential read).
 * True only for small page devices; large page devices do not support
 * autoincrement.
 */
#define NAND_NEED_READRDY	0x00000100

/* Chip does not allow subpage writes */
#define NAND_NO_SUBPAGE_WRITE	0x00000200

/* Device is one of 'new' xD cards that expose fake nand command set */
#define NAND_BROKEN_XD		0x00000400

/* Device behaves just like nand, but is readonly */
#define NAND_ROM		0x00000800

/* Device supports subpage reads */
#define NAND_SUBPAGE_READ	0x00001000

/*
 * Some MLC NANDs need data scrambling to limit bitflips caused by repeated
 * patterns.
 */
#define NAND_NEED_SCRAMBLING	0x00002000

/* Device needs 3rd row address cycle */
#define NAND_ROW_ADDR_3		0x00004000

/* Options valid for Samsung large page devices */
#define NAND_SAMSUNG_LP_OPTIONS NAND_CACHEPRG

/* Macros to identify the above */
#define NAND_HAS_SUBPAGE_READ(chip) ((chip->options & NAND_SUBPAGE_READ))

/* Non chip related options */
/* This option skips the bbt scan during initialization. */
#define NAND_SKIP_BBTSCAN	0x00010000
/* Chip may not exist, so silence any errors in scan */
#define NAND_SCAN_SILENT_NODEV	0x00040000
/*
 * Autodetect nand buswidth with readid/onfi.
 * This suppose the driver will configure the hardware in 8 bits mode
 * when calling nand_scan_ident, and update its configuration
 * before calling nand_scan_tail.
 */
#define NAND_BUSWIDTH_AUTO      0x00080000
/*
 * This option could be defined by controller drivers to protect against
 * kmap'ed, vmalloc'ed highmem buffers being passed from upper layers
 */
#define NAND_USE_BOUNCE_BUFFER	0x00100000

/*
 * In case your controller is implementing ->legacy.cmd_ctrl() and is relying
 * on the default ->cmdfunc() implementation, you may want to let the core
 * handle the tCCS delay which is required when a column change (RNDIN or
 * RNDOUT) is requested.
 * If your controller already takes care of this delay, you don't need to set
 * this flag.
 */
#define NAND_WAIT_TCCS		0x00200000

/*
 * Whether the NAND chip is a boot medium. Drivers might use this information
 * to select ECC algorithms supported by the boot ROM or similar restrictions.
 */
#define NAND_IS_BOOT_MEDIUM	0x00400000

/*
 * Do not try to tweak the timings at runtime. This is needed when the
 * controller initializes the timings on itself or when it relies on
 * configuration done by the bootloader.
 */
#define NAND_KEEP_TIMINGS	0x00800000

/* Cell info constants */
#define NAND_CI_CHIPNR_MSK	0x03
#define NAND_CI_CELLTYPE_MSK	0x0C
#define NAND_CI_CELLTYPE_SHIFT	2

/**
 * struct nand_parameters - NAND generic parameters from the parameter page
 * @model: Model name
 * @supports_set_get_features: The NAND chip supports setting/getting features
 * @set_feature_list: Bitmap of features that can be set
 * @get_feature_list: Bitmap of features that can be get
 * @onfi: ONFI specific parameters
 */
struct nand_parameters {
	/* Generic parameters */
	const char *model;
	bool supports_set_get_features;
	DECLARE_BITMAP(set_feature_list, ONFI_FEATURE_NUMBER);
	DECLARE_BITMAP(get_feature_list, ONFI_FEATURE_NUMBER);

	/* ONFI parameters */
	struct onfi_params *onfi;
};

/* The maximum expected count of bytes in the NAND ID sequence */
#define NAND_MAX_ID_LEN 8

/**
 * struct nand_id - NAND id structure
 * @data: buffer containing the id bytes.
 * @len: ID length.
 */
struct nand_id {
	u8 data[NAND_MAX_ID_LEN];
	int len;
};

/**
 * struct nand_ecc_step_info - ECC step information of ECC engine
 * @stepsize: data bytes per ECC step
 * @strengths: array of supported strengths
 * @nstrengths: number of supported strengths
 */
struct nand_ecc_step_info {
	int stepsize;
	const int *strengths;
	int nstrengths;
};

/**
 * struct nand_ecc_caps - capability of ECC engine
 * @stepinfos: array of ECC step information
 * @nstepinfos: number of ECC step information
 * @calc_ecc_bytes: driver's hook to calculate ECC bytes per step
 */
struct nand_ecc_caps {
	const struct nand_ecc_step_info *stepinfos;
	int nstepinfos;
	int (*calc_ecc_bytes)(int step_size, int strength);
};

/* a shorthand to generate struct nand_ecc_caps with only one ECC stepsize */
#define NAND_ECC_CAPS_SINGLE(__name, __calc, __step, ...)	\
static const int __name##_strengths[] = { __VA_ARGS__ };	\
static const struct nand_ecc_step_info __name##_stepinfo = {	\
	.stepsize = __step,					\
	.strengths = __name##_strengths,			\
	.nstrengths = ARRAY_SIZE(__name##_strengths),		\
};								\
static const struct nand_ecc_caps __name = {			\
	.stepinfos = &__name##_stepinfo,			\
	.nstepinfos = 1,					\
	.calc_ecc_bytes = __calc,				\
}

/**
 * struct nand_ecc_ctrl - Control structure for ECC
 * @mode:	ECC mode
 * @algo:	ECC algorithm
 * @steps:	number of ECC steps per page
 * @size:	data bytes per ECC step
 * @bytes:	ECC bytes per step
 * @strength:	max number of correctible bits per ECC step
 * @total:	total number of ECC bytes per page
 * @prepad:	padding information for syndrome based ECC generators
 * @postpad:	padding information for syndrome based ECC generators
 * @options:	ECC specific options (see NAND_ECC_XXX flags defined above)
 * @priv:	pointer to private ECC control data
 * @calc_buf:	buffer for calculated ECC, size is oobsize.
 * @code_buf:	buffer for ECC read from flash, size is oobsize.
 * @hwctl:	function to control hardware ECC generator. Must only
 *		be provided if an hardware ECC is available
 * @calculate:	function for ECC calculation or readback from ECC hardware
 * @correct:	function for ECC correction, matching to ECC generator (sw/hw).
 *		Should return a positive number representing the number of
 *		corrected bitflips, -EBADMSG if the number of bitflips exceed
 *		ECC strength, or any other error code if the error is not
 *		directly related to correction.
 *		If -EBADMSG is returned the input buffers should be left
 *		untouched.
 * @read_page_raw:	function to read a raw page without ECC. This function
 *			should hide the specific layout used by the ECC
 *			controller and always return contiguous in-band and
 *			out-of-band data even if they're not stored
 *			contiguously on the NAND chip (e.g.
 *			NAND_ECC_HW_SYNDROME interleaves in-band and
 *			out-of-band data).
 * @write_page_raw:	function to write a raw page without ECC. This function
 *			should hide the specific layout used by the ECC
 *			controller and consider the passed data as contiguous
 *			in-band and out-of-band data. ECC controller is
 *			responsible for doing the appropriate transformations
 *			to adapt to its specific layout (e.g.
 *			NAND_ECC_HW_SYNDROME interleaves in-band and
 *			out-of-band data).
 * @read_page:	function to read a page according to the ECC generator
 *		requirements; returns maximum number of bitflips corrected in
 *		any single ECC step, -EIO hw error
 * @read_subpage:	function to read parts of the page covered by ECC;
 *			returns same as read_page()
 * @write_subpage:	function to write parts of the page covered by ECC.
 * @write_page:	function to write a page according to the ECC generator
 *		requirements.
 * @write_oob_raw:	function to write chip OOB data without ECC
 * @read_oob_raw:	function to read chip OOB data without ECC
 * @read_oob:	function to read chip OOB data
 * @write_oob:	function to write chip OOB data
 */
struct nand_ecc_ctrl {
	nand_ecc_modes_t mode;
	enum nand_ecc_algo algo;
	int steps;
	int size;
	int bytes;
	int total;
	int strength;
	int prepad;
	int postpad;
	unsigned int options;
	void *priv;
	u8 *calc_buf;
	u8 *code_buf;
	void (*hwctl)(struct nand_chip *chip, int mode);
	int (*calculate)(struct nand_chip *chip, const uint8_t *dat,
			 uint8_t *ecc_code);
	int (*correct)(struct nand_chip *chip, uint8_t *dat, uint8_t *read_ecc,
		       uint8_t *calc_ecc);
	int (*read_page_raw)(struct nand_chip *chip, uint8_t *buf,
			     int oob_required, int page);
	int (*write_page_raw)(struct nand_chip *chip, const uint8_t *buf,
			      int oob_required, int page);
	int (*read_page)(struct nand_chip *chip, uint8_t *buf,
			 int oob_required, int page);
	int (*read_subpage)(struct nand_chip *chip, uint32_t offs,
			    uint32_t len, uint8_t *buf, int page);
	int (*write_subpage)(struct nand_chip *chip, uint32_t offset,
			     uint32_t data_len, const uint8_t *data_buf,
			     int oob_required, int page);
	int (*write_page)(struct nand_chip *chip, const uint8_t *buf,
			  int oob_required, int page);
	int (*write_oob_raw)(struct nand_chip *chip, int page);
	int (*read_oob_raw)(struct nand_chip *chip, int page);
	int (*read_oob)(struct nand_chip *chip, int page);
	int (*write_oob)(struct nand_chip *chip, int page);
};

/**
 * struct nand_sdr_timings - SDR NAND chip timings
 *
 * This struct defines the timing requirements of a SDR NAND chip.
 * These information can be found in every NAND datasheets and the timings
 * meaning are described in the ONFI specifications:
 * www.onfi.org/~/media/ONFI/specs/onfi_3_1_spec.pdf (chapter 4.15 Timing
 * Parameters)
 *
 * All these timings are expressed in picoseconds.
 *
 * @tBERS_max: Block erase time
 * @tCCS_min: Change column setup time
 * @tPROG_max: Page program time
 * @tR_max: Page read time
 * @tALH_min: ALE hold time
 * @tADL_min: ALE to data loading time
 * @tALS_min: ALE setup time
 * @tAR_min: ALE to RE# delay
 * @tCEA_max: CE# access time
 * @tCEH_min: CE# high hold time
 * @tCH_min:  CE# hold time
 * @tCHZ_max: CE# high to output hi-Z
 * @tCLH_min: CLE hold time
 * @tCLR_min: CLE to RE# delay
 * @tCLS_min: CLE setup time
 * @tCOH_min: CE# high to output hold
 * @tCS_min: CE# setup time
 * @tDH_min: Data hold time
 * @tDS_min: Data setup time
 * @tFEAT_max: Busy time for Set Features and Get Features
 * @tIR_min: Output hi-Z to RE# low
 * @tITC_max: Interface and Timing Mode Change time
 * @tRC_min: RE# cycle time
 * @tREA_max: RE# access time
 * @tREH_min: RE# high hold time
 * @tRHOH_min: RE# high to output hold
 * @tRHW_min: RE# high to WE# low
 * @tRHZ_max: RE# high to output hi-Z
 * @tRLOH_min: RE# low to output hold
 * @tRP_min: RE# pulse width
 * @tRR_min: Ready to RE# low (data only)
 * @tRST_max: Device reset time, measured from the falling edge of R/B# to the
 *	      rising edge of R/B#.
 * @tWB_max: WE# high to SR[6] low
 * @tWC_min: WE# cycle time
 * @tWH_min: WE# high hold time
 * @tWHR_min: WE# high to RE# low
 * @tWP_min: WE# pulse width
 * @tWW_min: WP# transition to WE# low
 */
struct nand_sdr_timings {
	u64 tBERS_max;
	u32 tCCS_min;
	u64 tPROG_max;
	u64 tR_max;
	u32 tALH_min;
	u32 tADL_min;
	u32 tALS_min;
	u32 tAR_min;
	u32 tCEA_max;
	u32 tCEH_min;
	u32 tCH_min;
	u32 tCHZ_max;
	u32 tCLH_min;
	u32 tCLR_min;
	u32 tCLS_min;
	u32 tCOH_min;
	u32 tCS_min;
	u32 tDH_min;
	u32 tDS_min;
	u32 tFEAT_max;
	u32 tIR_min;
	u32 tITC_max;
	u32 tRC_min;
	u32 tREA_max;
	u32 tREH_min;
	u32 tRHOH_min;
	u32 tRHW_min;
	u32 tRHZ_max;
	u32 tRLOH_min;
	u32 tRP_min;
	u32 tRR_min;
	u64 tRST_max;
	u32 tWB_max;
	u32 tWC_min;
	u32 tWH_min;
	u32 tWHR_min;
	u32 tWP_min;
	u32 tWW_min;
};

/**
 * enum nand_data_interface_type - NAND interface timing type
 * @NAND_SDR_IFACE:	Single Data Rate interface
 */
enum nand_data_interface_type {
	NAND_SDR_IFACE,
};

/**
 * struct nand_data_interface - NAND interface timing
 * @type:	 type of the timing
 * @timings:	 The timing, type according to @type
 * @timings.sdr: Use it when @type is %NAND_SDR_IFACE.
 */
struct nand_data_interface {
	enum nand_data_interface_type type;
	union {
		struct nand_sdr_timings sdr;
	} timings;
};

/**
 * nand_get_sdr_timings - get SDR timing from data interface
 * @conf:	The data interface
 */
static inline const struct nand_sdr_timings *
nand_get_sdr_timings(const struct nand_data_interface *conf)
{
	if (conf->type != NAND_SDR_IFACE)
		return ERR_PTR(-EINVAL);

	return &conf->timings.sdr;
}

/**
 * struct nand_op_cmd_instr - Definition of a command instruction
 * @opcode: the command to issue in one cycle
 */
struct nand_op_cmd_instr {
	u8 opcode;
};

/**
 * struct nand_op_addr_instr - Definition of an address instruction
 * @naddrs: length of the @addrs array
 * @addrs: array containing the address cycles to issue
 */
struct nand_op_addr_instr {
	unsigned int naddrs;
	const u8 *addrs;
};

/**
 * struct nand_op_data_instr - Definition of a data instruction
 * @len: number of data bytes to move
 * @buf: buffer to fill
 * @buf.in: buffer to fill when reading from the NAND chip
 * @buf.out: buffer to read from when writing to the NAND chip
 * @force_8bit: force 8-bit access
 *
 * Please note that "in" and "out" are inverted from the ONFI specification
 * and are from the controller perspective, so a "in" is a read from the NAND
 * chip while a "out" is a write to the NAND chip.
 */
struct nand_op_data_instr {
	unsigned int len;
	union {
		void *in;
		const void *out;
	} buf;
	bool force_8bit;
};

/**
 * struct nand_op_waitrdy_instr - Definition of a wait ready instruction
 * @timeout_ms: maximum delay while waiting for the ready/busy pin in ms
 */
struct nand_op_waitrdy_instr {
	unsigned int timeout_ms;
};

/**
 * enum nand_op_instr_type - Definition of all instruction types
 * @NAND_OP_CMD_INSTR: command instruction
 * @NAND_OP_ADDR_INSTR: address instruction
 * @NAND_OP_DATA_IN_INSTR: data in instruction
 * @NAND_OP_DATA_OUT_INSTR: data out instruction
 * @NAND_OP_WAITRDY_INSTR: wait ready instruction
 */
enum nand_op_instr_type {
	NAND_OP_CMD_INSTR,
	NAND_OP_ADDR_INSTR,
	NAND_OP_DATA_IN_INSTR,
	NAND_OP_DATA_OUT_INSTR,
	NAND_OP_WAITRDY_INSTR,
};

/**
 * struct nand_op_instr - Instruction object
 * @type: the instruction type
 * @ctx:  extra data associated to the instruction. You'll have to use the
 *        appropriate element depending on @type
 * @ctx.cmd: use it if @type is %NAND_OP_CMD_INSTR
 * @ctx.addr: use it if @type is %NAND_OP_ADDR_INSTR
 * @ctx.data: use it if @type is %NAND_OP_DATA_IN_INSTR
 *	      or %NAND_OP_DATA_OUT_INSTR
 * @ctx.waitrdy: use it if @type is %NAND_OP_WAITRDY_INSTR
 * @delay_ns: delay the controller should apply after the instruction has been
 *	      issued on the bus. Most modern controllers have internal timings
 *	      control logic, and in this case, the controller driver can ignore
 *	      this field.
 */
struct nand_op_instr {
	enum nand_op_instr_type type;
	union {
		struct nand_op_cmd_instr cmd;
		struct nand_op_addr_instr addr;
		struct nand_op_data_instr data;
		struct nand_op_waitrdy_instr waitrdy;
	} ctx;
	unsigned int delay_ns;
};

/*
 * Special handling must be done for the WAITRDY timeout parameter as it usually
 * is either tPROG (after a prog), tR (before a read), tRST (during a reset) or
 * tBERS (during an erase) which all of them are u64 values that cannot be
 * divided by usual kernel macros and must be handled with the special
 * DIV_ROUND_UP_ULL() macro.
 *
 * Cast to type of dividend is needed here to guarantee that the result won't
 * be an unsigned long long when the dividend is an unsigned long (or smaller),
 * which is what the compiler does when it sees ternary operator with 2
 * different return types (picks the largest type to make sure there's no
 * loss).
 */
#define __DIVIDE(dividend, divisor) ({						\
	(__typeof__(dividend))(sizeof(dividend) <= sizeof(unsigned long) ?	\
			       DIV_ROUND_UP(dividend, divisor) :		\
			       DIV_ROUND_UP_ULL(dividend, divisor)); 		\
	})
#define PSEC_TO_NSEC(x) __DIVIDE(x, 1000)
#define PSEC_TO_MSEC(x) __DIVIDE(x, 1000000000)

#define NAND_OP_CMD(id, ns)						\
	{								\
		.type = NAND_OP_CMD_INSTR,				\
		.ctx.cmd.opcode = id,					\
		.delay_ns = ns,						\
	}

#define NAND_OP_ADDR(ncycles, cycles, ns)				\
	{								\
		.type = NAND_OP_ADDR_INSTR,				\
		.ctx.addr = {						\
			.naddrs = ncycles,				\
			.addrs = cycles,				\
		},							\
		.delay_ns = ns,						\
	}

#define NAND_OP_DATA_IN(l, b, ns)					\
	{								\
		.type = NAND_OP_DATA_IN_INSTR,				\
		.ctx.data = {						\
			.len = l,					\
			.buf.in = b,					\
			.force_8bit = false,				\
		},							\
		.delay_ns = ns,						\
	}

#define NAND_OP_DATA_OUT(l, b, ns)					\
	{								\
		.type = NAND_OP_DATA_OUT_INSTR,				\
		.ctx.data = {						\
			.len = l,					\
			.buf.out = b,					\
			.force_8bit = false,				\
		},							\
		.delay_ns = ns,						\
	}

#define NAND_OP_8BIT_DATA_IN(l, b, ns)					\
	{								\
		.type = NAND_OP_DATA_IN_INSTR,				\
		.ctx.data = {						\
			.len = l,					\
			.buf.in = b,					\
			.force_8bit = true,				\
		},							\
		.delay_ns = ns,						\
	}

#define NAND_OP_8BIT_DATA_OUT(l, b, ns)					\
	{								\
		.type = NAND_OP_DATA_OUT_INSTR,				\
		.ctx.data = {						\
			.len = l,					\
			.buf.out = b,					\
			.force_8bit = true,				\
		},							\
		.delay_ns = ns,						\
	}

#define NAND_OP_WAIT_RDY(tout_ms, ns)					\
	{								\
		.type = NAND_OP_WAITRDY_INSTR,				\
		.ctx.waitrdy.timeout_ms = tout_ms,			\
		.delay_ns = ns,						\
	}

/**
 * struct nand_subop - a sub operation
 * @instrs: array of instructions
 * @ninstrs: length of the @instrs array
 * @first_instr_start_off: offset to start from for the first instruction
 *			   of the sub-operation
 * @last_instr_end_off: offset to end at (excluded) for the last instruction
 *			of the sub-operation
 *
 * Both @first_instr_start_off and @last_instr_end_off only apply to data or
 * address instructions.
 *
 * When an operation cannot be handled as is by the NAND controller, it will
 * be split by the parser into sub-operations which will be passed to the
 * controller driver.
 */
struct nand_subop {
	const struct nand_op_instr *instrs;
	unsigned int ninstrs;
	unsigned int first_instr_start_off;
	unsigned int last_instr_end_off;
};

unsigned int nand_subop_get_addr_start_off(const struct nand_subop *subop,
					   unsigned int op_id);
unsigned int nand_subop_get_num_addr_cyc(const struct nand_subop *subop,
					 unsigned int op_id);
unsigned int nand_subop_get_data_start_off(const struct nand_subop *subop,
					   unsigned int op_id);
unsigned int nand_subop_get_data_len(const struct nand_subop *subop,
				     unsigned int op_id);

/**
 * struct nand_op_parser_addr_constraints - Constraints for address instructions
 * @maxcycles: maximum number of address cycles the controller can issue in a
 *	       single step
 */
struct nand_op_parser_addr_constraints {
	unsigned int maxcycles;
};

/**
 * struct nand_op_parser_data_constraints - Constraints for data instructions
 * @maxlen: maximum data length that the controller can handle in a single step
 */
struct nand_op_parser_data_constraints {
	unsigned int maxlen;
};

/**
 * struct nand_op_parser_pattern_elem - One element of a pattern
 * @type: the instructuction type
 * @optional: whether this element of the pattern is optional or mandatory
 * @ctx: address or data constraint
 * @ctx.addr: address constraint (number of cycles)
 * @ctx.data: data constraint (data length)
 */
struct nand_op_parser_pattern_elem {
	enum nand_op_instr_type type;
	bool optional;
	union {
		struct nand_op_parser_addr_constraints addr;
		struct nand_op_parser_data_constraints data;
	} ctx;
};

#define NAND_OP_PARSER_PAT_CMD_ELEM(_opt)			\
	{							\
		.type = NAND_OP_CMD_INSTR,			\
		.optional = _opt,				\
	}

#define NAND_OP_PARSER_PAT_ADDR_ELEM(_opt, _maxcycles)		\
	{							\
		.type = NAND_OP_ADDR_INSTR,			\
		.optional = _opt,				\
		.ctx.addr.maxcycles = _maxcycles,		\
	}

#define NAND_OP_PARSER_PAT_DATA_IN_ELEM(_opt, _maxlen)		\
	{							\
		.type = NAND_OP_DATA_IN_INSTR,			\
		.optional = _opt,				\
		.ctx.data.maxlen = _maxlen,			\
	}

#define NAND_OP_PARSER_PAT_DATA_OUT_ELEM(_opt, _maxlen)		\
	{							\
		.type = NAND_OP_DATA_OUT_INSTR,			\
		.optional = _opt,				\
		.ctx.data.maxlen = _maxlen,			\
	}

#define NAND_OP_PARSER_PAT_WAITRDY_ELEM(_opt)			\
	{							\
		.type = NAND_OP_WAITRDY_INSTR,			\
		.optional = _opt,				\
	}

/**
 * struct nand_op_parser_pattern - NAND sub-operation pattern descriptor
 * @elems: array of pattern elements
 * @nelems: number of pattern elements in @elems array
 * @exec: the function that will issue a sub-operation
 *
 * A pattern is a list of elements, each element reprensenting one instruction
 * with its constraints. The pattern itself is used by the core to match NAND
 * chip operation with NAND controller operations.
 * Once a match between a NAND controller operation pattern and a NAND chip
 * operation (or a sub-set of a NAND operation) is found, the pattern ->exec()
 * hook is called so that the controller driver can issue the operation on the
 * bus.
 *
 * Controller drivers should declare as many patterns as they support and pass
 * this list of patterns (created with the help of the following macro) to
 * the nand_op_parser_exec_op() helper.
 */
struct nand_op_parser_pattern {
	const struct nand_op_parser_pattern_elem *elems;
	unsigned int nelems;
	int (*exec)(struct nand_chip *chip, const struct nand_subop *subop);
};

#define NAND_OP_PARSER_PATTERN(_exec, ...)							\
	{											\
		.exec = _exec,									\
		.elems = (struct nand_op_parser_pattern_elem[]) { __VA_ARGS__ },		\
		.nelems = sizeof((struct nand_op_parser_pattern_elem[]) { __VA_ARGS__ }) /	\
			  sizeof(struct nand_op_parser_pattern_elem),				\
	}

/**
 * struct nand_op_parser - NAND controller operation parser descriptor
 * @patterns: array of supported patterns
 * @npatterns: length of the @patterns array
 *
 * The parser descriptor is just an array of supported patterns which will be
 * iterated by nand_op_parser_exec_op() everytime it tries to execute an
 * NAND operation (or tries to determine if a specific operation is supported).
 *
 * It is worth mentioning that patterns will be tested in their declaration
 * order, and the first match will be taken, so it's important to order patterns
 * appropriately so that simple/inefficient patterns are placed at the end of
 * the list. Usually, this is where you put single instruction patterns.
 */
struct nand_op_parser {
	const struct nand_op_parser_pattern *patterns;
	unsigned int npatterns;
};

#define NAND_OP_PARSER(...)									\
	{											\
		.patterns = (struct nand_op_parser_pattern[]) { __VA_ARGS__ },			\
		.npatterns = sizeof((struct nand_op_parser_pattern[]) { __VA_ARGS__ }) /	\
			     sizeof(struct nand_op_parser_pattern),				\
	}

/**
 * struct nand_operation - NAND operation descriptor
 * @cs: the CS line to select for this NAND operation
 * @instrs: array of instructions to execute
 * @ninstrs: length of the @instrs array
 *
 * The actual operation structure that will be passed to chip->exec_op().
 */
struct nand_operation {
	unsigned int cs;
	const struct nand_op_instr *instrs;
	unsigned int ninstrs;
};

#define NAND_OPERATION(_cs, _instrs)				\
	{							\
		.cs = _cs,					\
		.instrs = _instrs,				\
		.ninstrs = ARRAY_SIZE(_instrs),			\
	}

int nand_op_parser_exec_op(struct nand_chip *chip,
			   const struct nand_op_parser *parser,
			   const struct nand_operation *op, bool check_only);

/**
 * struct nand_controller_ops - Controller operations
 *
 * @attach_chip: this method is called after the NAND detection phase after
 *		 flash ID and MTD fields such as erase size, page size and OOB
 *		 size have been set up. ECC requirements are available if
 *		 provided by the NAND chip or device tree. Typically used to
 *		 choose the appropriate ECC configuration and allocate
 *		 associated resources.
 *		 This hook is optional.
 * @detach_chip: free all resources allocated/claimed in
 *		 nand_controller_ops->attach_chip().
 *		 This hook is optional.
 * @exec_op:	 controller specific method to execute NAND operations.
 *		 This method replaces chip->legacy.cmdfunc(),
 *		 chip->legacy.{read,write}_{buf,byte,word}(),
 *		 chip->legacy.dev_ready() and chip->legacy.waifunc().
 * @setup_data_interface: setup the data interface and timing. If
 *			  chipnr is set to %NAND_DATA_IFACE_CHECK_ONLY this
 *			  means the configuration should not be applied but
 *			  only checked.
 *			  This hook is optional.
 */
struct nand_controller_ops {
	int (*attach_chip)(struct nand_chip *chip);
	void (*detach_chip)(struct nand_chip *chip);
	int (*exec_op)(struct nand_chip *chip,
		       const struct nand_operation *op,
		       bool check_only);
	int (*setup_data_interface)(struct nand_chip *chip, int chipnr,
				    const struct nand_data_interface *conf);
};

/**
 * struct nand_controller - Structure used to describe a NAND controller
 *
 * @lock:		lock used to serialize accesses to the NAND controller
 * @ops:		NAND controller operations.
 */
struct nand_controller {
	struct mutex lock;
	const struct nand_controller_ops *ops;
};

static inline void nand_controller_init(struct nand_controller *nfc)
{
	mutex_init(&nfc->lock);
}

/**
 * struct nand_legacy - NAND chip legacy fields/hooks
 * @IO_ADDR_R: address to read the 8 I/O lines of the flash device
 * @IO_ADDR_W: address to write the 8 I/O lines of the flash device
 * @select_chip: select/deselect a specific target/die
 * @read_byte: read one byte from the chip
 * @write_byte: write a single byte to the chip on the low 8 I/O lines
 * @write_buf: write data from the buffer to the chip
 * @read_buf: read data from the chip into the buffer
 * @cmd_ctrl: hardware specific function for controlling ALE/CLE/nCE. Also used
 *	      to write command and address
 * @cmdfunc: hardware specific function for writing commands to the chip.
 * @dev_ready: hardware specific function for accessing device ready/busy line.
 *	       If set to NULL no access to ready/busy is available and the
 *	       ready/busy information is read from the chip status register.
 * @waitfunc: hardware specific function for wait on ready.
 * @block_bad: check if a block is bad, using OOB markers
 * @block_markbad: mark a block bad
 * @set_features: set the NAND chip features
 * @get_features: get the NAND chip features
 * @chip_delay: chip dependent delay for transferring data from array to read
 *		regs (tR).
 * @dummy_controller: dummy controller implementation for drivers that can
 *		      only control a single chip
 *
 * If you look at this structure you're already wrong. These fields/hooks are
 * all deprecated.
 */
struct nand_legacy {
	void __iomem *IO_ADDR_R;
	void __iomem *IO_ADDR_W;
	void (*select_chip)(struct nand_chip *chip, int cs);
	u8 (*read_byte)(struct nand_chip *chip);
	void (*write_byte)(struct nand_chip *chip, u8 byte);
	void (*write_buf)(struct nand_chip *chip, const u8 *buf, int len);
	void (*read_buf)(struct nand_chip *chip, u8 *buf, int len);
	void (*cmd_ctrl)(struct nand_chip *chip, int dat, unsigned int ctrl);
	void (*cmdfunc)(struct nand_chip *chip, unsigned command, int column,
			int page_addr);
	int (*dev_ready)(struct nand_chip *chip);
	int (*waitfunc)(struct nand_chip *chip);
	int (*block_bad)(struct nand_chip *chip, loff_t ofs);
	int (*block_markbad)(struct nand_chip *chip, loff_t ofs);
	int (*set_features)(struct nand_chip *chip, int feature_addr,
			    u8 *subfeature_para);
	int (*get_features)(struct nand_chip *chip, int feature_addr,
			    u8 *subfeature_para);
	int chip_delay;
	struct nand_controller dummy_controller;
};

/**
 * struct nand_chip - NAND Private Flash Chip Data
 * @base:		Inherit from the generic NAND device
 * @legacy:		All legacy fields/hooks. If you develop a new driver,
 *			don't even try to use any of these fields/hooks, and if
 *			you're modifying an existing driver that is using those
 *			fields/hooks, you should consider reworking the driver
 *			avoid using them.
 * @setup_read_retry:	[FLASHSPECIFIC] flash (vendor) specific function for
 *			setting the read-retry mode. Mostly needed for MLC NAND.
 * @ecc:		[BOARDSPECIFIC] ECC control structure
 * @buf_align:		minimum buffer alignment required by a platform
 * @oob_poi:		"poison value buffer," used for laying out OOB data
 *			before writing
 * @page_shift:		[INTERN] number of address bits in a page (column
 *			address bits).
 * @phys_erase_shift:	[INTERN] number of address bits in a physical eraseblock
 * @bbt_erase_shift:	[INTERN] number of address bits in a bbt entry
 * @chip_shift:		[INTERN] number of address bits in one chip
 * @options:		[BOARDSPECIFIC] various chip options. They can partly
 *			be set to inform nand_scan about special functionality.
 *			See the defines for further explanation.
 * @bbt_options:	[INTERN] bad block specific options. All options used
 *			here must come from bbm.h. By default, these options
 *			will be copied to the appropriate nand_bbt_descr's.
 * @badblockpos:	[INTERN] position of the bad block marker in the oob
 *			area.
 * @badblockbits:	[INTERN] minimum number of set bits in a good block's
 *			bad block marker position; i.e., BBM == 11110111b is
 *			not bad when badblockbits == 7
 * @onfi_timing_mode_default: [INTERN] default ONFI timing mode. This field is
 *			      set to the actually used ONFI mode if the chip is
 *			      ONFI compliant or deduced from the datasheet if
 *			      the NAND chip is not ONFI compliant.
 * @pagemask:		[INTERN] page number mask = number of (pages / chip) - 1
 * @data_buf:		[INTERN] buffer for data, size is (page size + oobsize).
 * @pagecache:		Structure containing page cache related fields
 * @pagecache.bitflips:	Number of bitflips of the cached page
 * @pagecache.page:	Page number currently in the cache. -1 means no page is
 *			currently cached
 * @subpagesize:	[INTERN] holds the subpagesize
 * @id:			[INTERN] holds NAND ID
 * @parameters:		[INTERN] holds generic parameters under an easily
 *			readable form.
 * @data_interface:	[INTERN] NAND interface timing information
 * @cur_cs:		currently selected target. -1 means no target selected,
 *			otherwise we should always have cur_cs >= 0 &&
 *			cur_cs < nanddev_ntargets(). NAND Controller drivers
 *			should not modify this value, but they're allowed to
 *			read it.
 * @read_retries:	[INTERN] the number of read retry modes supported
 * @lock:		lock protecting the suspended field. Also used to
 *			serialize accesses to the NAND device.
 * @suspended:		set to 1 when the device is suspended, 0 when it's not.
 * @bbt:		[INTERN] bad block table pointer
 * @bbt_td:		[REPLACEABLE] bad block table descriptor for flash
 *			lookup.
 * @bbt_md:		[REPLACEABLE] bad block table mirror descriptor
 * @badblock_pattern:	[REPLACEABLE] bad block scan pattern used for initial
 *			bad block scan.
 * @controller:		[REPLACEABLE] a pointer to a hardware controller
 *			structure which is shared among multiple independent
 *			devices.
 * @priv:		[OPTIONAL] pointer to private chip data
 * @manufacturer:	[INTERN] Contains manufacturer information
 * @manufacturer.desc:	[INTERN] Contains manufacturer's description
 * @manufacturer.priv:	[INTERN] Contains manufacturer private information
 */

struct nand_chip {
	struct nand_device base;

	struct nand_legacy legacy;

	int (*setup_read_retry)(struct nand_chip *chip, int retry_mode);

	unsigned int options;
	unsigned int bbt_options;

	int page_shift;
	int phys_erase_shift;
	int bbt_erase_shift;
	int chip_shift;
	int pagemask;
	u8 *data_buf;

	struct {
		unsigned int bitflips;
		int page;
	} pagecache;

	int subpagesize;
	int onfi_timing_mode_default;
	int badblockpos;
	int badblockbits;

	struct nand_id id;
	struct nand_parameters parameters;

	struct nand_data_interface data_interface;

	int cur_cs;

	int read_retries;

	struct mutex lock;
	unsigned int suspended : 1;

	uint8_t *oob_poi;
	struct nand_controller *controller;

	struct nand_ecc_ctrl ecc;
	unsigned long buf_align;

	uint8_t *bbt;
	struct nand_bbt_descr *bbt_td;
	struct nand_bbt_descr *bbt_md;

	struct nand_bbt_descr *badblock_pattern;

	void *priv;

	struct {
		const struct nand_manufacturer *desc;
		void *priv;
	} manufacturer;
};

extern const struct mtd_ooblayout_ops nand_ooblayout_sp_ops;
extern const struct mtd_ooblayout_ops nand_ooblayout_lp_ops;

static inline struct nand_chip *mtd_to_nand(struct mtd_info *mtd)
{
	return container_of(mtd, struct nand_chip, base.mtd);
}

static inline struct mtd_info *nand_to_mtd(struct nand_chip *chip)
{
	return &chip->base.mtd;
}

static inline void *nand_get_controller_data(struct nand_chip *chip)
{
	return chip->priv;
}

static inline void nand_set_controller_data(struct nand_chip *chip, void *priv)
{
	chip->priv = priv;
}

static inline void nand_set_manufacturer_data(struct nand_chip *chip,
					      void *priv)
{
	chip->manufacturer.priv = priv;
}

static inline void *nand_get_manufacturer_data(struct nand_chip *chip)
{
	return chip->manufacturer.priv;
}

static inline void nand_set_flash_node(struct nand_chip *chip,
				       struct device_node *np)
{
	mtd_set_of_node(nand_to_mtd(chip), np);
}

static inline struct device_node *nand_get_flash_node(struct nand_chip *chip)
{
	return mtd_get_of_node(nand_to_mtd(chip));
}

/*
 * A helper for defining older NAND chips where the second ID byte fully
 * defined the chip, including the geometry (chip size, eraseblock size, page
 * size). All these chips have 512 bytes NAND page size.
 */
#define LEGACY_ID_NAND(nm, devid, chipsz, erasesz, opts)          \
	{ .name = (nm), {{ .dev_id = (devid) }}, .pagesize = 512, \
	  .chipsize = (chipsz), .erasesize = (erasesz), .options = (opts) }

/*
 * A helper for defining newer chips which report their page size and
 * eraseblock size via the extended ID bytes.
 *
 * The real difference between LEGACY_ID_NAND and EXTENDED_ID_NAND is that with
 * EXTENDED_ID_NAND, manufacturers overloaded the same device ID so that the
 * device ID now only represented a particular total chip size (and voltage,
 * buswidth), and the page size, eraseblock size, and OOB size could vary while
 * using the same device ID.
 */
#define EXTENDED_ID_NAND(nm, devid, chipsz, opts)                      \
	{ .name = (nm), {{ .dev_id = (devid) }}, .chipsize = (chipsz), \
	  .options = (opts) }

#define NAND_ECC_INFO(_strength, _step)	\
			{ .strength_ds = (_strength), .step_ds = (_step) }
#define NAND_ECC_STRENGTH(type)		((type)->ecc.strength_ds)
#define NAND_ECC_STEP(type)		((type)->ecc.step_ds)

/**
 * struct nand_flash_dev - NAND Flash Device ID Structure
 * @name: a human-readable name of the NAND chip
 * @dev_id: the device ID (the second byte of the full chip ID array)
 * @mfr_id: manufecturer ID part of the full chip ID array (refers the same
 *          memory address as @id[0])
 * @dev_id: device ID part of the full chip ID array (refers the same memory
 *          address as @id[1])
 * @id: full device ID array
 * @pagesize: size of the NAND page in bytes; if 0, then the real page size (as
 *            well as the eraseblock size) is determined from the extended NAND
 *            chip ID array)
 * @chipsize: total chip size in MiB
 * @erasesize: eraseblock size in bytes (determined from the extended ID if 0)
 * @options: stores various chip bit options
 * @id_len: The valid length of the @id.
 * @oobsize: OOB size
 * @ecc: ECC correctability and step information from the datasheet.
 * @ecc.strength_ds: The ECC correctability from the datasheet, same as the
 *                   @ecc_strength_ds in nand_chip{}.
 * @ecc.step_ds: The ECC step required by the @ecc.strength_ds, same as the
 *               @ecc_step_ds in nand_chip{}, also from the datasheet.
 *               For example, the "4bit ECC for each 512Byte" can be set with
 *               NAND_ECC_INFO(4, 512).
 * @onfi_timing_mode_default: the default ONFI timing mode entered after a NAND
 *			      reset. Should be deduced from timings described
 *			      in the datasheet.
 *
 */
struct nand_flash_dev {
	char *name;
	union {
		struct {
			uint8_t mfr_id;
			uint8_t dev_id;
		};
		uint8_t id[NAND_MAX_ID_LEN];
	};
	unsigned int pagesize;
	unsigned int chipsize;
	unsigned int erasesize;
	unsigned int options;
	uint16_t id_len;
	uint16_t oobsize;
	struct {
		uint16_t strength_ds;
		uint16_t step_ds;
	} ecc;
	int onfi_timing_mode_default;
};

int nand_create_bbt(struct nand_chip *chip);

/*
 * Check if it is a SLC nand.
 * The !nand_is_slc() can be used to check the MLC/TLC nand chips.
 * We do not distinguish the MLC and TLC now.
 */
static inline bool nand_is_slc(struct nand_chip *chip)
{
	WARN(nanddev_bits_per_cell(&chip->base) == 0,
	     "chip->bits_per_cell is used uninitialized\n");
	return nanddev_bits_per_cell(&chip->base) == 1;
}

/**
 * Check if the opcode's address should be sent only on the lower 8 bits
 * @command: opcode to check
 */
static inline int nand_opcode_8bits(unsigned int command)
{
	switch (command) {
	case NAND_CMD_READID:
	case NAND_CMD_PARAM:
	case NAND_CMD_GET_FEATURES:
	case NAND_CMD_SET_FEATURES:
		return 1;
	default:
		break;
	}
	return 0;
}

int nand_check_erased_ecc_chunk(void *data, int datalen,
				void *ecc, int ecclen,
				void *extraoob, int extraooblen,
				int threshold);

int nand_ecc_choose_conf(struct nand_chip *chip,
			 const struct nand_ecc_caps *caps, int oobavail);

/* Default write_oob implementation */
int nand_write_oob_std(struct nand_chip *chip, int page);

/* Default read_oob implementation */
int nand_read_oob_std(struct nand_chip *chip, int page);

/* Stub used by drivers that do not support GET/SET FEATURES operations */
int nand_get_set_features_notsupp(struct nand_chip *chip, int addr,
				  u8 *subfeature_param);

/* Default read_page_raw implementation */
int nand_read_page_raw(struct nand_chip *chip, uint8_t *buf, int oob_required,
		       int page);

/* Default write_page_raw implementation */
int nand_write_page_raw(struct nand_chip *chip, const uint8_t *buf,
			int oob_required, int page);

/* Reset and initialize a NAND device */
int nand_reset(struct nand_chip *chip, int chipnr);

/* NAND operation helpers */
int nand_reset_op(struct nand_chip *chip);
int nand_readid_op(struct nand_chip *chip, u8 addr, void *buf,
		   unsigned int len);
int nand_status_op(struct nand_chip *chip, u8 *status);
int nand_erase_op(struct nand_chip *chip, unsigned int eraseblock);
int nand_read_page_op(struct nand_chip *chip, unsigned int page,
		      unsigned int offset_in_page, void *buf, unsigned int len);
int nand_change_read_column_op(struct nand_chip *chip,
			       unsigned int offset_in_page, void *buf,
			       unsigned int len, bool force_8bit);
int nand_read_oob_op(struct nand_chip *chip, unsigned int page,
		     unsigned int offset_in_page, void *buf, unsigned int len);
int nand_prog_page_begin_op(struct nand_chip *chip, unsigned int page,
			    unsigned int offset_in_page, const void *buf,
			    unsigned int len);
int nand_prog_page_end_op(struct nand_chip *chip);
int nand_prog_page_op(struct nand_chip *chip, unsigned int page,
		      unsigned int offset_in_page, const void *buf,
		      unsigned int len);
int nand_change_write_column_op(struct nand_chip *chip,
				unsigned int offset_in_page, const void *buf,
				unsigned int len, bool force_8bit);
int nand_read_data_op(struct nand_chip *chip, void *buf, unsigned int len,
		      bool force_8bit);
int nand_write_data_op(struct nand_chip *chip, const void *buf,
		       unsigned int len, bool force_8bit);

/* Scan and identify a NAND device */
int nand_scan_with_ids(struct nand_chip *chip, unsigned int max_chips,
		       struct nand_flash_dev *ids);

static inline int nand_scan(struct nand_chip *chip, unsigned int max_chips)
{
	return nand_scan_with_ids(chip, max_chips, NULL);
}

/* Internal helper for board drivers which need to override command function */
void nand_wait_ready(struct nand_chip *chip);

/*
 * Free resources held by the NAND device, must be called on error after a
 * sucessful nand_scan().
 */
void nand_cleanup(struct nand_chip *chip);
/* Unregister the MTD device and calls nand_cleanup() */
void nand_release(struct nand_chip *chip);

/*
 * External helper for controller drivers that have to implement the WAITRDY
 * instruction and have no physical pin to check it.
 */
int nand_soft_waitrdy(struct nand_chip *chip, unsigned long timeout_ms);
struct gpio_desc;
int nand_gpio_waitrdy(struct nand_chip *chip, struct gpio_desc *gpiod,
		      unsigned long timeout_ms);

/* Select/deselect a NAND target. */
void nand_select_target(struct nand_chip *chip, unsigned int cs);
void nand_deselect_target(struct nand_chip *chip);

/**
 * nand_get_data_buf() - Get the internal page buffer
 * @chip: NAND chip object
 *
 * Returns the pre-allocated page buffer after invalidating the cache. This
 * function should be used by drivers that do not want to allocate their own
 * bounce buffer and still need such a buffer for specific operations (most
 * commonly when reading OOB data only).
 *
 * Be careful to never call this function in the write/write_oob path, because
 * the core may have placed the data to be written out in this buffer.
 *
 * Return: pointer to the page cache buffer
 */
static inline void *nand_get_data_buf(struct nand_chip *chip)
{
	chip->pagecache.page = -1;

	return chip->data_buf;
}

#endif /* __LINUX_MTD_RAWNAND_H */
