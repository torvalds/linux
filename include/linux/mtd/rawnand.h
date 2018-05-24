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

#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/flashchip.h>
#include <linux/mtd/bbm.h>
#include <linux/types.h>

struct mtd_info;
struct nand_flash_dev;
struct device_node;

/* Scan and identify a NAND device */
int nand_scan(struct mtd_info *mtd, int max_chips);
/*
 * Separate phases of nand_scan(), allowing board driver to intervene
 * and override command or ECC setup according to flash type.
 */
int nand_scan_ident(struct mtd_info *mtd, int max_chips,
			   struct nand_flash_dev *table);
int nand_scan_tail(struct mtd_info *mtd);

/* Unregister the MTD device and free resources held by the NAND device */
void nand_release(struct mtd_info *mtd);

/* Internal helper for board drivers which need to override command function */
void nand_wait_ready(struct mtd_info *mtd);

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

/* Bit mask for flags passed to do_nand_read_ecc */
#define NAND_GET_DEVICE		0x80


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
#define NAND_HAS_CACHEPROG(chip) ((chip->options & NAND_CACHEPRG))
#define NAND_HAS_SUBPAGE_READ(chip) ((chip->options & NAND_SUBPAGE_READ))
#define NAND_HAS_SUBPAGE_WRITE(chip) !((chip)->options & NAND_NO_SUBPAGE_WRITE)

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
 * In case your controller is implementing ->cmd_ctrl() and is relying on the
 * default ->cmdfunc() implementation, you may want to let the core handle the
 * tCCS delay which is required when a column change (RNDIN or RNDOUT) is
 * requested.
 * If your controller already takes care of this delay, you don't need to set
 * this flag.
 */
#define NAND_WAIT_TCCS		0x00200000

/* Options set by nand scan */
/* Nand scan has allocated controller struct */
#define NAND_CONTROLLER_ALLOC	0x80000000

/* Cell info constants */
#define NAND_CI_CHIPNR_MSK	0x03
#define NAND_CI_CELLTYPE_MSK	0x0C
#define NAND_CI_CELLTYPE_SHIFT	2

/* Keep gcc happy */
struct nand_chip;

/* ONFI features */
#define ONFI_FEATURE_16_BIT_BUS		(1 << 0)
#define ONFI_FEATURE_EXT_PARAM_PAGE	(1 << 7)

/* ONFI timing mode, used in both asynchronous and synchronous mode */
#define ONFI_TIMING_MODE_0		(1 << 0)
#define ONFI_TIMING_MODE_1		(1 << 1)
#define ONFI_TIMING_MODE_2		(1 << 2)
#define ONFI_TIMING_MODE_3		(1 << 3)
#define ONFI_TIMING_MODE_4		(1 << 4)
#define ONFI_TIMING_MODE_5		(1 << 5)
#define ONFI_TIMING_MODE_UNKNOWN	(1 << 6)

/* ONFI feature number/address */
#define ONFI_FEATURE_NUMBER		256
#define ONFI_FEATURE_ADDR_TIMING_MODE	0x1

/* Vendor-specific feature address (Micron) */
#define ONFI_FEATURE_ADDR_READ_RETRY	0x89
#define ONFI_FEATURE_ON_DIE_ECC		0x90
#define   ONFI_FEATURE_ON_DIE_ECC_EN	BIT(3)

/* ONFI subfeature parameters length */
#define ONFI_SUBFEATURE_PARAM_LEN	4

/* ONFI optional commands SET/GET FEATURES supported? */
#define ONFI_OPT_CMD_SET_GET_FEATURES	(1 << 2)

struct nand_onfi_params {
	/* rev info and features block */
	/* 'O' 'N' 'F' 'I'  */
	u8 sig[4];
	__le16 revision;
	__le16 features;
	__le16 opt_cmd;
	u8 reserved0[2];
	__le16 ext_param_page_length; /* since ONFI 2.1 */
	u8 num_of_param_pages;        /* since ONFI 2.1 */
	u8 reserved1[17];

	/* manufacturer information block */
	char manufacturer[12];
	char model[20];
	u8 jedec_id;
	__le16 date_code;
	u8 reserved2[13];

	/* memory organization block */
	__le32 byte_per_page;
	__le16 spare_bytes_per_page;
	__le32 data_bytes_per_ppage;
	__le16 spare_bytes_per_ppage;
	__le32 pages_per_block;
	__le32 blocks_per_lun;
	u8 lun_count;
	u8 addr_cycles;
	u8 bits_per_cell;
	__le16 bb_per_lun;
	__le16 block_endurance;
	u8 guaranteed_good_blocks;
	__le16 guaranteed_block_endurance;
	u8 programs_per_page;
	u8 ppage_attr;
	u8 ecc_bits;
	u8 interleaved_bits;
	u8 interleaved_ops;
	u8 reserved3[13];

	/* electrical parameter block */
	u8 io_pin_capacitance_max;
	__le16 async_timing_mode;
	__le16 program_cache_timing_mode;
	__le16 t_prog;
	__le16 t_bers;
	__le16 t_r;
	__le16 t_ccs;
	__le16 src_sync_timing_mode;
	u8 src_ssync_features;
	__le16 clk_pin_capacitance_typ;
	__le16 io_pin_capacitance_typ;
	__le16 input_pin_capacitance_typ;
	u8 input_pin_capacitance_max;
	u8 driver_strength_support;
	__le16 t_int_r;
	__le16 t_adl;
	u8 reserved4[8];

	/* vendor */
	__le16 vendor_revision;
	u8 vendor[88];

	__le16 crc;
} __packed;

#define ONFI_CRC_BASE	0x4F4E

/* Extended ECC information Block Definition (since ONFI 2.1) */
struct onfi_ext_ecc_info {
	u8 ecc_bits;
	u8 codeword_size;
	__le16 bb_per_lun;
	__le16 block_endurance;
	u8 reserved[2];
} __packed;

#define ONFI_SECTION_TYPE_0	0	/* Unused section. */
#define ONFI_SECTION_TYPE_1	1	/* for additional sections. */
#define ONFI_SECTION_TYPE_2	2	/* for ECC information. */
struct onfi_ext_section {
	u8 type;
	u8 length;
} __packed;

#define ONFI_EXT_SECTION_MAX 8

/* Extended Parameter Page Definition (since ONFI 2.1) */
struct onfi_ext_param_page {
	__le16 crc;
	u8 sig[4];             /* 'E' 'P' 'P' 'S' */
	u8 reserved0[10];
	struct onfi_ext_section sections[ONFI_EXT_SECTION_MAX];

	/*
	 * The actual size of the Extended Parameter Page is in
	 * @ext_param_page_length of nand_onfi_params{}.
	 * The following are the variable length sections.
	 * So we do not add any fields below. Please see the ONFI spec.
	 */
} __packed;

struct jedec_ecc_info {
	u8 ecc_bits;
	u8 codeword_size;
	__le16 bb_per_lun;
	__le16 block_endurance;
	u8 reserved[2];
} __packed;

/* JEDEC features */
#define JEDEC_FEATURE_16_BIT_BUS	(1 << 0)

struct nand_jedec_params {
	/* rev info and features block */
	/* 'J' 'E' 'S' 'D'  */
	u8 sig[4];
	__le16 revision;
	__le16 features;
	u8 opt_cmd[3];
	__le16 sec_cmd;
	u8 num_of_param_pages;
	u8 reserved0[18];

	/* manufacturer information block */
	char manufacturer[12];
	char model[20];
	u8 jedec_id[6];
	u8 reserved1[10];

	/* memory organization block */
	__le32 byte_per_page;
	__le16 spare_bytes_per_page;
	u8 reserved2[6];
	__le32 pages_per_block;
	__le32 blocks_per_lun;
	u8 lun_count;
	u8 addr_cycles;
	u8 bits_per_cell;
	u8 programs_per_page;
	u8 multi_plane_addr;
	u8 multi_plane_op_attr;
	u8 reserved3[38];

	/* electrical parameter block */
	__le16 async_sdr_speed_grade;
	__le16 toggle_ddr_speed_grade;
	__le16 sync_ddr_speed_grade;
	u8 async_sdr_features;
	u8 toggle_ddr_features;
	u8 sync_ddr_features;
	__le16 t_prog;
	__le16 t_bers;
	__le16 t_r;
	__le16 t_r_multi_plane;
	__le16 t_ccs;
	__le16 io_pin_capacitance_typ;
	__le16 input_pin_capacitance_typ;
	__le16 clk_pin_capacitance_typ;
	u8 driver_strength_support;
	__le16 t_adl;
	u8 reserved4[36];

	/* ECC and endurance block */
	u8 guaranteed_good_blocks;
	__le16 guaranteed_block_endurance;
	struct jedec_ecc_info ecc_info[4];
	u8 reserved5[29];

	/* reserved */
	u8 reserved6[148];

	/* vendor */
	__le16 vendor_rev_num;
	u8 reserved7[88];

	/* CRC for Parameter Page */
	__le16 crc;
} __packed;

/**
 * struct onfi_params - ONFI specific parameters that will be reused
 * @version: ONFI version (BCD encoded), 0 if ONFI is not supported
 * @tPROG: Page program time
 * @tBERS: Block erase time
 * @tR: Page read time
 * @tCCS: Change column setup time
 * @async_timing_mode: Supported asynchronous timing mode
 * @vendor_revision: Vendor specific revision number
 * @vendor: Vendor specific data
 */
struct onfi_params {
	int version;
	u16 tPROG;
	u16 tBERS;
	u16 tR;
	u16 tCCS;
	u16 async_timing_mode;
	u16 vendor_revision;
	u8 vendor[88];
};

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
	char model[100];
	bool supports_set_get_features;
	DECLARE_BITMAP(set_feature_list, ONFI_FEATURE_NUMBER);
	DECLARE_BITMAP(get_feature_list, ONFI_FEATURE_NUMBER);

	/* ONFI parameters */
	struct onfi_params onfi;
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
 * struct nand_hw_control - Control structure for hardware controller (e.g ECC generator) shared among independent devices
 * @lock:               protection lock
 * @active:		the mtd device which holds the controller currently
 * @wq:			wait queue to sleep on if a NAND operation is in
 *			progress used instead of the per chip wait queue
 *			when a hw controller is available.
 */
struct nand_hw_control {
	spinlock_t lock;
	struct nand_chip *active;
	wait_queue_head_t wq;
};

static inline void nand_hw_control_init(struct nand_hw_control *nfc)
{
	nfc->active = NULL;
	spin_lock_init(&nfc->lock);
	init_waitqueue_head(&nfc->wq);
}

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
	void (*hwctl)(struct mtd_info *mtd, int mode);
	int (*calculate)(struct mtd_info *mtd, const uint8_t *dat,
			uint8_t *ecc_code);
	int (*correct)(struct mtd_info *mtd, uint8_t *dat, uint8_t *read_ecc,
			uint8_t *calc_ecc);
	int (*read_page_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			uint8_t *buf, int oob_required, int page);
	int (*write_page_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			const uint8_t *buf, int oob_required, int page);
	int (*read_page)(struct mtd_info *mtd, struct nand_chip *chip,
			uint8_t *buf, int oob_required, int page);
	int (*read_subpage)(struct mtd_info *mtd, struct nand_chip *chip,
			uint32_t offs, uint32_t len, uint8_t *buf, int page);
	int (*write_subpage)(struct mtd_info *mtd, struct nand_chip *chip,
			uint32_t offset, uint32_t data_len,
			const uint8_t *data_buf, int oob_required, int page);
	int (*write_page)(struct mtd_info *mtd, struct nand_chip *chip,
			const uint8_t *buf, int oob_required, int page);
	int (*write_oob_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			int page);
	int (*read_oob_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			int page);
	int (*read_oob)(struct mtd_info *mtd, struct nand_chip *chip, int page);
	int (*write_oob)(struct mtd_info *mtd, struct nand_chip *chip,
			int page);
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
 * @type:	type of the timing
 * @timings:	The timing, type according to @type
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
 * struct nand_manufacturer_ops - NAND Manufacturer operations
 * @detect: detect the NAND memory organization and capabilities
 * @init: initialize all vendor specific fields (like the ->read_retry()
 *	  implementation) if any.
 * @cleanup: the ->init() function may have allocated resources, ->cleanup()
 *	     is here to let vendor specific code release those resources.
 */
struct nand_manufacturer_ops {
	void (*detect)(struct nand_chip *chip);
	int (*init)(struct nand_chip *chip);
	void (*cleanup)(struct nand_chip *chip);
};

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
 * @in: buffer to fill when reading from the NAND chip
 * @out: buffer to read from when writing to the NAND chip
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
 * @cmd/@addr/@data/@waitrdy: extra data associated to the instruction.
 *                            You'll have to use the appropriate element
 *                            depending on @type
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

int nand_subop_get_addr_start_off(const struct nand_subop *subop,
				  unsigned int op_id);
int nand_subop_get_num_addr_cyc(const struct nand_subop *subop,
				unsigned int op_id);
int nand_subop_get_data_start_off(const struct nand_subop *subop,
				  unsigned int op_id);
int nand_subop_get_data_len(const struct nand_subop *subop,
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
 * @addr/@data: address or data constraint (number of cycles or data length)
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
 * @instrs: array of instructions to execute
 * @ninstrs: length of the @instrs array
 *
 * The actual operation structure that will be passed to chip->exec_op().
 */
struct nand_operation {
	const struct nand_op_instr *instrs;
	unsigned int ninstrs;
};

#define NAND_OPERATION(_instrs)					\
	{							\
		.instrs = _instrs,				\
		.ninstrs = ARRAY_SIZE(_instrs),			\
	}

int nand_op_parser_exec_op(struct nand_chip *chip,
			   const struct nand_op_parser *parser,
			   const struct nand_operation *op, bool check_only);

/**
 * struct nand_chip - NAND Private Flash Chip Data
 * @mtd:		MTD device registered to the MTD framework
 * @IO_ADDR_R:		[BOARDSPECIFIC] address to read the 8 I/O lines of the
 *			flash device
 * @IO_ADDR_W:		[BOARDSPECIFIC] address to write the 8 I/O lines of the
 *			flash device.
 * @read_byte:		[REPLACEABLE] read one byte from the chip
 * @read_word:		[REPLACEABLE] read one word from the chip
 * @write_byte:		[REPLACEABLE] write a single byte to the chip on the
 *			low 8 I/O lines
 * @write_buf:		[REPLACEABLE] write data from the buffer to the chip
 * @read_buf:		[REPLACEABLE] read data from the chip into the buffer
 * @select_chip:	[REPLACEABLE] select chip nr
 * @block_bad:		[REPLACEABLE] check if a block is bad, using OOB markers
 * @block_markbad:	[REPLACEABLE] mark a block bad
 * @cmd_ctrl:		[BOARDSPECIFIC] hardwarespecific function for controlling
 *			ALE/CLE/nCE. Also used to write command and address
 * @dev_ready:		[BOARDSPECIFIC] hardwarespecific function for accessing
 *			device ready/busy line. If set to NULL no access to
 *			ready/busy is available and the ready/busy information
 *			is read from the chip status register.
 * @cmdfunc:		[REPLACEABLE] hardwarespecific function for writing
 *			commands to the chip.
 * @waitfunc:		[REPLACEABLE] hardwarespecific function for wait on
 *			ready.
 * @exec_op:		controller specific method to execute NAND operations.
 *			This method replaces ->cmdfunc(),
 *			->{read,write}_{buf,byte,word}(), ->dev_ready() and
 *			->waifunc().
 * @setup_read_retry:	[FLASHSPECIFIC] flash (vendor) specific function for
 *			setting the read-retry mode. Mostly needed for MLC NAND.
 * @ecc:		[BOARDSPECIFIC] ECC control structure
 * @buf_align:		minimum buffer alignment required by a platform
 * @hwcontrol:		platform-specific hardware control structure
 * @erase:		[REPLACEABLE] erase function
 * @scan_bbt:		[REPLACEABLE] function to scan bad block table
 * @chip_delay:		[BOARDSPECIFIC] chip dependent delay for transferring
 *			data from array to read regs (tR).
 * @state:		[INTERN] the current state of the NAND device
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
 * @bits_per_cell:	[INTERN] number of bits per cell. i.e., 1 means SLC.
 * @ecc_strength_ds:	[INTERN] ECC correctability from the datasheet.
 *			Minimum amount of bit errors per @ecc_step_ds guaranteed
 *			to be correctable. If unknown, set to zero.
 * @ecc_step_ds:	[INTERN] ECC step required by the @ecc_strength_ds,
 *			also from the datasheet. It is the recommended ECC step
 *			size, if known; if unknown, set to zero.
 * @onfi_timing_mode_default: [INTERN] default ONFI timing mode. This field is
 *			      set to the actually used ONFI mode if the chip is
 *			      ONFI compliant or deduced from the datasheet if
 *			      the NAND chip is not ONFI compliant.
 * @numchips:		[INTERN] number of physical chips
 * @chipsize:		[INTERN] the size of one chip for multichip arrays
 * @pagemask:		[INTERN] page number mask = number of (pages / chip) - 1
 * @data_buf:		[INTERN] buffer for data, size is (page size + oobsize).
 * @pagebuf:		[INTERN] holds the pagenumber which is currently in
 *			data_buf.
 * @pagebuf_bitflips:	[INTERN] holds the bitflip count for the page which is
 *			currently in data_buf.
 * @subpagesize:	[INTERN] holds the subpagesize
 * @id:			[INTERN] holds NAND ID
 * @parameters:		[INTERN] holds generic parameters under an easily
 *			readable form.
 * @max_bb_per_die:	[INTERN] the max number of bad blocks each die of a
 *			this nand device will encounter their life times.
 * @blocks_per_die:	[INTERN] The number of PEBs in a die
 * @data_interface:	[INTERN] NAND interface timing information
 * @read_retries:	[INTERN] the number of read retry modes supported
 * @set_features:	[REPLACEABLE] set the NAND chip features
 * @get_features:	[REPLACEABLE] get the NAND chip features
 * @setup_data_interface: [OPTIONAL] setup the data interface and timing. If
 *			  chipnr is set to %NAND_DATA_IFACE_CHECK_ONLY this
 *			  means the configuration should not be applied but
 *			  only checked.
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
 */

struct nand_chip {
	struct mtd_info mtd;
	void __iomem *IO_ADDR_R;
	void __iomem *IO_ADDR_W;

	uint8_t (*read_byte)(struct mtd_info *mtd);
	u16 (*read_word)(struct mtd_info *mtd);
	void (*write_byte)(struct mtd_info *mtd, uint8_t byte);
	void (*write_buf)(struct mtd_info *mtd, const uint8_t *buf, int len);
	void (*read_buf)(struct mtd_info *mtd, uint8_t *buf, int len);
	void (*select_chip)(struct mtd_info *mtd, int chip);
	int (*block_bad)(struct mtd_info *mtd, loff_t ofs);
	int (*block_markbad)(struct mtd_info *mtd, loff_t ofs);
	void (*cmd_ctrl)(struct mtd_info *mtd, int dat, unsigned int ctrl);
	int (*dev_ready)(struct mtd_info *mtd);
	void (*cmdfunc)(struct mtd_info *mtd, unsigned command, int column,
			int page_addr);
	int(*waitfunc)(struct mtd_info *mtd, struct nand_chip *this);
	int (*exec_op)(struct nand_chip *chip,
		       const struct nand_operation *op,
		       bool check_only);
	int (*erase)(struct mtd_info *mtd, int page);
	int (*scan_bbt)(struct mtd_info *mtd);
	int (*set_features)(struct mtd_info *mtd, struct nand_chip *chip,
			    int feature_addr, uint8_t *subfeature_para);
	int (*get_features)(struct mtd_info *mtd, struct nand_chip *chip,
			    int feature_addr, uint8_t *subfeature_para);
	int (*setup_read_retry)(struct mtd_info *mtd, int retry_mode);
	int (*setup_data_interface)(struct mtd_info *mtd, int chipnr,
				    const struct nand_data_interface *conf);

	int chip_delay;
	unsigned int options;
	unsigned int bbt_options;

	int page_shift;
	int phys_erase_shift;
	int bbt_erase_shift;
	int chip_shift;
	int numchips;
	uint64_t chipsize;
	int pagemask;
	u8 *data_buf;
	int pagebuf;
	unsigned int pagebuf_bitflips;
	int subpagesize;
	uint8_t bits_per_cell;
	uint16_t ecc_strength_ds;
	uint16_t ecc_step_ds;
	int onfi_timing_mode_default;
	int badblockpos;
	int badblockbits;

	struct nand_id id;
	struct nand_parameters parameters;
	u16 max_bb_per_die;
	u32 blocks_per_die;

	struct nand_data_interface data_interface;

	int read_retries;

	flstate_t state;

	uint8_t *oob_poi;
	struct nand_hw_control *controller;

	struct nand_ecc_ctrl ecc;
	unsigned long buf_align;
	struct nand_hw_control hwcontrol;

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

static inline int nand_exec_op(struct nand_chip *chip,
			       const struct nand_operation *op)
{
	if (!chip->exec_op)
		return -ENOTSUPP;

	return chip->exec_op(chip, op, false);
}

extern const struct mtd_ooblayout_ops nand_ooblayout_sp_ops;
extern const struct mtd_ooblayout_ops nand_ooblayout_lp_ops;

static inline void nand_set_flash_node(struct nand_chip *chip,
				       struct device_node *np)
{
	mtd_set_of_node(&chip->mtd, np);
}

static inline struct device_node *nand_get_flash_node(struct nand_chip *chip)
{
	return mtd_get_of_node(&chip->mtd);
}

static inline struct nand_chip *mtd_to_nand(struct mtd_info *mtd)
{
	return container_of(mtd, struct nand_chip, mtd);
}

static inline struct mtd_info *nand_to_mtd(struct nand_chip *chip)
{
	return &chip->mtd;
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

/*
 * NAND Flash Manufacturer ID Codes
 */
#define NAND_MFR_TOSHIBA	0x98
#define NAND_MFR_ESMT		0xc8
#define NAND_MFR_SAMSUNG	0xec
#define NAND_MFR_FUJITSU	0x04
#define NAND_MFR_NATIONAL	0x8f
#define NAND_MFR_RENESAS	0x07
#define NAND_MFR_STMICRO	0x20
#define NAND_MFR_HYNIX		0xad
#define NAND_MFR_MICRON		0x2c
#define NAND_MFR_AMD		0x01
#define NAND_MFR_MACRONIX	0xc2
#define NAND_MFR_EON		0x92
#define NAND_MFR_SANDISK	0x45
#define NAND_MFR_INTEL		0x89
#define NAND_MFR_ATO		0x9b
#define NAND_MFR_WINBOND	0xef


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

/**
 * struct nand_manufacturer - NAND Flash Manufacturer structure
 * @name:	Manufacturer name
 * @id:		manufacturer ID code of device.
 * @ops:	manufacturer operations
*/
struct nand_manufacturer {
	int id;
	char *name;
	const struct nand_manufacturer_ops *ops;
};

const struct nand_manufacturer *nand_get_manufacturer(u8 id);

static inline const char *
nand_manufacturer_name(const struct nand_manufacturer *manufacturer)
{
	return manufacturer ? manufacturer->name : "Unknown";
}

extern struct nand_flash_dev nand_flash_ids[];

extern const struct nand_manufacturer_ops toshiba_nand_manuf_ops;
extern const struct nand_manufacturer_ops samsung_nand_manuf_ops;
extern const struct nand_manufacturer_ops hynix_nand_manuf_ops;
extern const struct nand_manufacturer_ops micron_nand_manuf_ops;
extern const struct nand_manufacturer_ops amd_nand_manuf_ops;
extern const struct nand_manufacturer_ops macronix_nand_manuf_ops;

int nand_default_bbt(struct mtd_info *mtd);
int nand_markbad_bbt(struct mtd_info *mtd, loff_t offs);
int nand_isreserved_bbt(struct mtd_info *mtd, loff_t offs);
int nand_isbad_bbt(struct mtd_info *mtd, loff_t offs, int allowbbt);
int nand_erase_nand(struct mtd_info *mtd, struct erase_info *instr,
		    int allowbbt);
int nand_do_read(struct mtd_info *mtd, loff_t from, size_t len,
		 size_t *retlen, uint8_t *buf);

/**
 * struct platform_nand_chip - chip level device structure
 * @nr_chips:		max. number of chips to scan for
 * @chip_offset:	chip number offset
 * @nr_partitions:	number of partitions pointed to by partitions (or zero)
 * @partitions:		mtd partition list
 * @chip_delay:		R/B delay value in us
 * @options:		Option flags, e.g. 16bit buswidth
 * @bbt_options:	BBT option flags, e.g. NAND_BBT_USE_FLASH
 * @part_probe_types:	NULL-terminated array of probe types
 */
struct platform_nand_chip {
	int nr_chips;
	int chip_offset;
	int nr_partitions;
	struct mtd_partition *partitions;
	int chip_delay;
	unsigned int options;
	unsigned int bbt_options;
	const char **part_probe_types;
};

/* Keep gcc happy */
struct platform_device;

/**
 * struct platform_nand_ctrl - controller level device structure
 * @probe:		platform specific function to probe/setup hardware
 * @remove:		platform specific function to remove/teardown hardware
 * @hwcontrol:		platform specific hardware control structure
 * @dev_ready:		platform specific function to read ready/busy pin
 * @select_chip:	platform specific chip select function
 * @cmd_ctrl:		platform specific function for controlling
 *			ALE/CLE/nCE. Also used to write command and address
 * @write_buf:		platform specific function for write buffer
 * @read_buf:		platform specific function for read buffer
 * @read_byte:		platform specific function to read one byte from chip
 * @priv:		private data to transport driver specific settings
 *
 * All fields are optional and depend on the hardware driver requirements
 */
struct platform_nand_ctrl {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	void (*hwcontrol)(struct mtd_info *mtd, int cmd);
	int (*dev_ready)(struct mtd_info *mtd);
	void (*select_chip)(struct mtd_info *mtd, int chip);
	void (*cmd_ctrl)(struct mtd_info *mtd, int dat, unsigned int ctrl);
	void (*write_buf)(struct mtd_info *mtd, const uint8_t *buf, int len);
	void (*read_buf)(struct mtd_info *mtd, uint8_t *buf, int len);
	unsigned char (*read_byte)(struct mtd_info *mtd);
	void *priv;
};

/**
 * struct platform_nand_data - container structure for platform-specific data
 * @chip:		chip level chip structure
 * @ctrl:		controller level device structure
 */
struct platform_nand_data {
	struct platform_nand_chip chip;
	struct platform_nand_ctrl ctrl;
};

/* return the supported asynchronous timing mode. */
static inline int onfi_get_async_timing_mode(struct nand_chip *chip)
{
	if (!chip->parameters.onfi.version)
		return ONFI_TIMING_MODE_UNKNOWN;

	return chip->parameters.onfi.async_timing_mode;
}

int onfi_fill_data_interface(struct nand_chip *chip,
			     enum nand_data_interface_type type,
			     int timing_mode);

/*
 * Check if it is a SLC nand.
 * The !nand_is_slc() can be used to check the MLC/TLC nand chips.
 * We do not distinguish the MLC and TLC now.
 */
static inline bool nand_is_slc(struct nand_chip *chip)
{
	WARN(chip->bits_per_cell == 0,
	     "chip->bits_per_cell is used uninitialized\n");
	return chip->bits_per_cell == 1;
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

/* get timing characteristics from ONFI timing mode. */
const struct nand_sdr_timings *onfi_async_timing_mode_to_sdr_timings(int mode);

int nand_check_erased_ecc_chunk(void *data, int datalen,
				void *ecc, int ecclen,
				void *extraoob, int extraooblen,
				int threshold);

int nand_check_ecc_caps(struct nand_chip *chip,
			const struct nand_ecc_caps *caps, int oobavail);

int nand_match_ecc_req(struct nand_chip *chip,
		       const struct nand_ecc_caps *caps,  int oobavail);

int nand_maximize_ecc(struct nand_chip *chip,
		      const struct nand_ecc_caps *caps, int oobavail);

/* Default write_oob implementation */
int nand_write_oob_std(struct mtd_info *mtd, struct nand_chip *chip, int page);

/* Default write_oob syndrome implementation */
int nand_write_oob_syndrome(struct mtd_info *mtd, struct nand_chip *chip,
			    int page);

/* Default read_oob implementation */
int nand_read_oob_std(struct mtd_info *mtd, struct nand_chip *chip, int page);

/* Default read_oob syndrome implementation */
int nand_read_oob_syndrome(struct mtd_info *mtd, struct nand_chip *chip,
			   int page);

/* Wrapper to use in order for controllers/vendors to GET/SET FEATURES */
int nand_get_features(struct nand_chip *chip, int addr, u8 *subfeature_param);
int nand_set_features(struct nand_chip *chip, int addr, u8 *subfeature_param);
/* Stub used by drivers that do not support GET/SET FEATURES operations */
int nand_get_set_features_notsupp(struct mtd_info *mtd, struct nand_chip *chip,
				  int addr, u8 *subfeature_param);

/* Default read_page_raw implementation */
int nand_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
		       uint8_t *buf, int oob_required, int page);

/* Default write_page_raw implementation */
int nand_write_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
			const uint8_t *buf, int oob_required, int page);

/* Reset and initialize a NAND device */
int nand_reset(struct nand_chip *chip, int chipnr);

/* NAND operation helpers */
int nand_reset_op(struct nand_chip *chip);
int nand_readid_op(struct nand_chip *chip, u8 addr, void *buf,
		   unsigned int len);
int nand_status_op(struct nand_chip *chip, u8 *status);
int nand_exit_status_op(struct nand_chip *chip);
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

/* Free resources held by the NAND device */
void nand_cleanup(struct nand_chip *chip);

/* Default extended ID decoding function */
void nand_decode_ext_id(struct nand_chip *chip);

/*
 * External helper for controller drivers that have to implement the WAITRDY
 * instruction and have no physical pin to check it.
 */
int nand_soft_waitrdy(struct nand_chip *chip, unsigned long timeout_ms);

#endif /* __LINUX_MTD_RAWNAND_H */
