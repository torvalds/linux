/* SPDX-License-Identifier: GPL-2.0 */
/*
 * QCOM QPIC common APIs header file
 *
 * Copyright (c) 2023 Qualcomm Inc.
 * Authors:	Md sadre Alam	<quic_mdalam@quicinc.com>
 *
 */
#ifndef __MTD_NAND_QPIC_COMMON_H__
#define __MTD_NAND_QPIC_COMMON_H__

/* NANDc reg offsets */
#define	NAND_FLASH_CMD			0x00
#define	NAND_ADDR0			0x04
#define	NAND_ADDR1			0x08
#define	NAND_FLASH_CHIP_SELECT		0x0c
#define	NAND_EXEC_CMD			0x10
#define	NAND_FLASH_STATUS		0x14
#define	NAND_BUFFER_STATUS		0x18
#define	NAND_DEV0_CFG0			0x20
#define	NAND_DEV0_CFG1			0x24
#define	NAND_DEV0_ECC_CFG		0x28
#define	NAND_AUTO_STATUS_EN		0x2c
#define	NAND_DEV1_CFG0			0x30
#define	NAND_DEV1_CFG1			0x34
#define	NAND_READ_ID			0x40
#define	NAND_READ_STATUS		0x44
#define	NAND_DEV_CMD0			0xa0
#define	NAND_DEV_CMD1			0xa4
#define	NAND_DEV_CMD2			0xa8
#define	NAND_DEV_CMD_VLD		0xac
#define	SFLASHC_BURST_CFG		0xe0
#define	NAND_ERASED_CW_DETECT_CFG	0xe8
#define	NAND_ERASED_CW_DETECT_STATUS	0xec
#define	NAND_EBI2_ECC_BUF_CFG		0xf0
#define	FLASH_BUF_ACC			0x100

#define	NAND_CTRL			0xf00
#define	NAND_VERSION			0xf08
#define	NAND_READ_LOCATION_0		0xf20
#define	NAND_READ_LOCATION_1		0xf24
#define	NAND_READ_LOCATION_2		0xf28
#define	NAND_READ_LOCATION_3		0xf2c
#define	NAND_READ_LOCATION_LAST_CW_0	0xf40
#define	NAND_READ_LOCATION_LAST_CW_1	0xf44
#define	NAND_READ_LOCATION_LAST_CW_2	0xf48
#define	NAND_READ_LOCATION_LAST_CW_3	0xf4c

/* dummy register offsets, used by qcom_write_reg_dma */
#define	NAND_DEV_CMD1_RESTORE		0xdead
#define	NAND_DEV_CMD_VLD_RESTORE	0xbeef

/* NAND_FLASH_CMD bits */
#define	PAGE_ACC			BIT(4)
#define	LAST_PAGE			BIT(5)

/* NAND_FLASH_CHIP_SELECT bits */
#define	NAND_DEV_SEL			0
#define	DM_EN				BIT(2)

/* NAND_FLASH_STATUS bits */
#define	FS_OP_ERR			BIT(4)
#define	FS_READY_BSY_N			BIT(5)
#define	FS_MPU_ERR			BIT(8)
#define	FS_DEVICE_STS_ERR		BIT(16)
#define	FS_DEVICE_WP			BIT(23)

/* NAND_BUFFER_STATUS bits */
#define	BS_UNCORRECTABLE_BIT		BIT(8)
#define	BS_CORRECTABLE_ERR_MSK		0x1f

/* NAND_DEVn_CFG0 bits */
#define	DISABLE_STATUS_AFTER_WRITE	BIT(4)
#define	CW_PER_PAGE			6
#define	CW_PER_PAGE_MASK		GENMASK(8, 6)
#define	UD_SIZE_BYTES			9
#define	UD_SIZE_BYTES_MASK		GENMASK(18, 9)
#define	ECC_PARITY_SIZE_BYTES_RS	GENMASK(22, 19)
#define	SPARE_SIZE_BYTES		23
#define	SPARE_SIZE_BYTES_MASK		GENMASK(26, 23)
#define	NUM_ADDR_CYCLES			27
#define	NUM_ADDR_CYCLES_MASK		GENMASK(29, 27)
#define	STATUS_BFR_READ			BIT(30)
#define	SET_RD_MODE_AFTER_STATUS	BIT(31)

/* NAND_DEVn_CFG0 bits */
#define	DEV0_CFG1_ECC_DISABLE		BIT(0)
#define	WIDE_FLASH			BIT(1)
#define	NAND_RECOVERY_CYCLES		2
#define	NAND_RECOVERY_CYCLES_MASK	GENMASK(4, 2)
#define	CS_ACTIVE_BSY			BIT(5)
#define	BAD_BLOCK_BYTE_NUM		6
#define	BAD_BLOCK_BYTE_NUM_MASK		GENMASK(15, 6)
#define	BAD_BLOCK_IN_SPARE_AREA		BIT(16)
#define	WR_RD_BSY_GAP			17
#define	WR_RD_BSY_GAP_MASK		GENMASK(22, 17)
#define	ENABLE_BCH_ECC			BIT(27)

/* NAND_DEV0_ECC_CFG bits */
#define	ECC_CFG_ECC_DISABLE		BIT(0)
#define	ECC_SW_RESET			BIT(1)
#define	ECC_MODE			4
#define	ECC_MODE_MASK			GENMASK(5, 4)
#define	ECC_PARITY_SIZE_BYTES_BCH	8
#define	ECC_PARITY_SIZE_BYTES_BCH_MASK	GENMASK(12, 8)
#define	ECC_NUM_DATA_BYTES		16
#define	ECC_NUM_DATA_BYTES_MASK		GENMASK(25, 16)
#define	ECC_FORCE_CLK_OPEN		BIT(30)

/* NAND_DEV_CMD1 bits */
#define	READ_ADDR_MASK			GENMASK(7, 0)

/* NAND_DEV_CMD_VLD bits */
#define	READ_START_VLD			BIT(0)
#define	READ_STOP_VLD			BIT(1)
#define	WRITE_START_VLD			BIT(2)
#define	ERASE_START_VLD			BIT(3)
#define	SEQ_READ_START_VLD		BIT(4)

/* NAND_EBI2_ECC_BUF_CFG bits */
#define	NUM_STEPS			0
#define	NUM_STEPS_MASK			GENMASK(9, 0)

/* NAND_ERASED_CW_DETECT_CFG bits */
#define	ERASED_CW_ECC_MASK		1
#define	AUTO_DETECT_RES			0
#define	MASK_ECC			BIT(ERASED_CW_ECC_MASK)
#define	RESET_ERASED_DET		BIT(AUTO_DETECT_RES)
#define	ACTIVE_ERASED_DET		(0 << AUTO_DETECT_RES)
#define	CLR_ERASED_PAGE_DET		(RESET_ERASED_DET | MASK_ECC)
#define	SET_ERASED_PAGE_DET		(ACTIVE_ERASED_DET | MASK_ECC)

/* NAND_ERASED_CW_DETECT_STATUS bits */
#define	PAGE_ALL_ERASED			BIT(7)
#define	CODEWORD_ALL_ERASED		BIT(6)
#define	PAGE_ERASED			BIT(5)
#define	CODEWORD_ERASED			BIT(4)
#define	ERASED_PAGE			(PAGE_ALL_ERASED | PAGE_ERASED)
#define	ERASED_CW			(CODEWORD_ALL_ERASED | CODEWORD_ERASED)

/* NAND_READ_LOCATION_n bits */
#define READ_LOCATION_OFFSET		0
#define READ_LOCATION_OFFSET_MASK	GENMASK(9, 0)
#define READ_LOCATION_SIZE		16
#define READ_LOCATION_SIZE_MASK		GENMASK(25, 16)
#define READ_LOCATION_LAST		31
#define READ_LOCATION_LAST_MASK		BIT(31)

/* Version Mask */
#define	NAND_VERSION_MAJOR_MASK		0xf0000000
#define	NAND_VERSION_MAJOR_SHIFT	28
#define	NAND_VERSION_MINOR_MASK		0x0fff0000
#define	NAND_VERSION_MINOR_SHIFT	16

/* NAND OP_CMDs */
#define	OP_PAGE_READ			0x2
#define	OP_PAGE_READ_WITH_ECC		0x3
#define	OP_PAGE_READ_WITH_ECC_SPARE	0x4
#define	OP_PAGE_READ_ONFI_READ		0x5
#define	OP_PROGRAM_PAGE			0x6
#define	OP_PAGE_PROGRAM_WITH_ECC	0x7
#define	OP_PROGRAM_PAGE_SPARE		0x9
#define	OP_BLOCK_ERASE			0xa
#define	OP_CHECK_STATUS			0xc
#define	OP_FETCH_ID			0xb
#define	OP_RESET_DEVICE			0xd

/* Default Value for NAND_DEV_CMD_VLD */
#define NAND_DEV_CMD_VLD_VAL		(READ_START_VLD | WRITE_START_VLD | \
					 ERASE_START_VLD | SEQ_READ_START_VLD)

/* NAND_CTRL bits */
#define	BAM_MODE_EN			BIT(0)

/*
 * the NAND controller performs reads/writes with ECC in 516 byte chunks.
 * the driver calls the chunks 'step' or 'codeword' interchangeably
 */
#define	NANDC_STEP_SIZE			512

/*
 * the largest page size we support is 8K, this will have 16 steps/codewords
 * of 512 bytes each
 */
#define	MAX_NUM_STEPS			(SZ_8K / NANDC_STEP_SIZE)

/* we read at most 3 registers per codeword scan */
#define	MAX_REG_RD			(3 * MAX_NUM_STEPS)

/* ECC modes supported by the controller */
#define	ECC_NONE	BIT(0)
#define	ECC_RS_4BIT	BIT(1)
#define	ECC_BCH_4BIT	BIT(2)
#define	ECC_BCH_8BIT	BIT(3)

/*
 * Returns the actual register address for all NAND_DEV_ registers
 * (i.e. NAND_DEV_CMD0, NAND_DEV_CMD1, NAND_DEV_CMD2 and NAND_DEV_CMD_VLD)
 */
#define dev_cmd_reg_addr(nandc, reg) ((nandc)->props->dev_cmd_reg_start + (reg))

/* Returns the dma address for reg read buffer */
#define reg_buf_dma_addr(chip, vaddr) \
	((chip)->reg_read_dma + \
	((u8 *)(vaddr) - (u8 *)(chip)->reg_read_buf))

#define QPIC_PER_CW_CMD_ELEMENTS	32
#define QPIC_PER_CW_CMD_SGL		32
#define QPIC_PER_CW_DATA_SGL		8

#define QPIC_NAND_COMPLETION_TIMEOUT	msecs_to_jiffies(2000)

/*
 * Flags used in DMA descriptor preparation helper functions
 * (i.e. qcom_read_reg_dma/qcom_write_reg_dma/qcom_read_data_dma/qcom_write_data_dma)
 */
/* Don't set the EOT in current tx BAM sgl */
#define NAND_BAM_NO_EOT			BIT(0)
/* Set the NWD flag in current BAM sgl */
#define NAND_BAM_NWD			BIT(1)
/* Finish writing in the current BAM sgl and start writing in another BAM sgl */
#define NAND_BAM_NEXT_SGL		BIT(2)
/*
 * Erased codeword status is being used two times in single transfer so this
 * flag will determine the current value of erased codeword status register
 */
#define NAND_ERASED_CW_SET		BIT(4)

#define MAX_ADDRESS_CYCLE		5

/*
 * This data type corresponds to the BAM transaction which will be used for all
 * NAND transfers.
 * @bam_ce - the array of BAM command elements
 * @cmd_sgl - sgl for NAND BAM command pipe
 * @data_sgl - sgl for NAND BAM consumer/producer pipe
 * @last_data_desc - last DMA desc in data channel (tx/rx).
 * @last_cmd_desc - last DMA desc in command channel.
 * @txn_done - completion for NAND transfer.
 * @bam_ce_pos - the index in bam_ce which is available for next sgl
 * @bam_ce_start - the index in bam_ce which marks the start position ce
 *		   for current sgl. It will be used for size calculation
 *		   for current sgl
 * @cmd_sgl_pos - current index in command sgl.
 * @cmd_sgl_start - start index in command sgl.
 * @tx_sgl_pos - current index in data sgl for tx.
 * @tx_sgl_start - start index in data sgl for tx.
 * @rx_sgl_pos - current index in data sgl for rx.
 * @rx_sgl_start - start index in data sgl for rx.
 */
struct bam_transaction {
	struct bam_cmd_element *bam_ce;
	struct scatterlist *cmd_sgl;
	struct scatterlist *data_sgl;
	struct dma_async_tx_descriptor *last_data_desc;
	struct dma_async_tx_descriptor *last_cmd_desc;
	struct completion txn_done;
	struct_group(bam_positions,
		u32 bam_ce_pos;
		u32 bam_ce_start;
		u32 cmd_sgl_pos;
		u32 cmd_sgl_start;
		u32 tx_sgl_pos;
		u32 tx_sgl_start;
		u32 rx_sgl_pos;
		u32 rx_sgl_start;

	);
};

/*
 * This data type corresponds to the nand dma descriptor
 * @dma_desc - low level DMA engine descriptor
 * @list - list for desc_info
 *
 * @adm_sgl - sgl which will be used for single sgl dma descriptor. Only used by
 *	      ADM
 * @bam_sgl - sgl which will be used for dma descriptor. Only used by BAM
 * @sgl_cnt - number of SGL in bam_sgl. Only used by BAM
 * @dir - DMA transfer direction
 */
struct desc_info {
	struct dma_async_tx_descriptor *dma_desc;
	struct list_head node;

	union {
		struct scatterlist adm_sgl;
		struct {
			struct scatterlist *bam_sgl;
			int sgl_cnt;
		};
	};
	enum dma_data_direction dir;
};

/*
 * holds the current register values that we want to write. acts as a contiguous
 * chunk of memory which we use to write the controller registers through DMA.
 */
struct nandc_regs {
	__le32 cmd;
	__le32 addr0;
	__le32 addr1;
	__le32 chip_sel;
	__le32 exec;

	__le32 cfg0;
	__le32 cfg1;
	__le32 ecc_bch_cfg;

	__le32 clrflashstatus;
	__le32 clrreadstatus;

	__le32 cmd1;
	__le32 vld;

	__le32 orig_cmd1;
	__le32 orig_vld;

	__le32 ecc_buf_cfg;
	__le32 read_location0;
	__le32 read_location1;
	__le32 read_location2;
	__le32 read_location3;
	__le32 read_location_last0;
	__le32 read_location_last1;
	__le32 read_location_last2;
	__le32 read_location_last3;
	__le32 spi_cfg;
	__le32 num_addr_cycle;
	__le32 busy_wait_cnt;
	__le32 flash_feature;

	__le32 erased_cw_detect_cfg_clr;
	__le32 erased_cw_detect_cfg_set;
};

/*
 * NAND controller data struct
 *
 * @dev:			parent device
 *
 * @base:			MMIO base
 *
 * @core_clk:			controller clock
 * @aon_clk:			another controller clock
 * @iomacro_clk:		io macro clock
 *
 * @regs:			a contiguous chunk of memory for DMA register
 *				writes. contains the register values to be
 *				written to controller
 *
 * @props:			properties of current NAND controller,
 *				initialized via DT match data
 *
 * @controller:			base controller structure
 * @qspi:			qpic spi structure
 * @host_list:			list containing all the chips attached to the
 *				controller
 *
 * @chan:			dma channel
 * @cmd_crci:			ADM DMA CRCI for command flow control
 * @data_crci:			ADM DMA CRCI for data flow control
 *
 * @desc_list:			DMA descriptor list (list of desc_infos)
 *
 * @data_buffer:		our local DMA buffer for page read/writes,
 *				used when we can't use the buffer provided
 *				by upper layers directly
 * @reg_read_buf:		local buffer for reading back registers via DMA
 *
 * @base_phys:			physical base address of controller registers
 * @base_dma:			dma base address of controller registers
 * @reg_read_dma:		contains dma address for register read buffer
 *
 * @buf_size/count/start:	markers for chip->legacy.read_buf/write_buf
 *				functions
 * @max_cwperpage:		maximum QPIC codewords required. calculated
 *				from all connected NAND devices pagesize
 *
 * @reg_read_pos:		marker for data read in reg_read_buf
 *
 * @cmd1/vld:			some fixed controller register values
 *
 * @exec_opwrite:		flag to select correct number of code word
 *				while reading status
 */
struct qcom_nand_controller {
	struct device *dev;

	void __iomem *base;

	struct clk *core_clk;
	struct clk *aon_clk;

	struct nandc_regs *regs;
	struct bam_transaction *bam_txn;

	const struct qcom_nandc_props *props;

	struct nand_controller *controller;
	struct qpic_spi_nand *qspi;
	struct list_head host_list;

	union {
		/* will be used only by QPIC for BAM DMA */
		struct {
			struct dma_chan *tx_chan;
			struct dma_chan *rx_chan;
			struct dma_chan *cmd_chan;
		};

		/* will be used only by EBI2 for ADM DMA */
		struct {
			struct dma_chan *chan;
			unsigned int cmd_crci;
			unsigned int data_crci;
		};
	};

	struct list_head desc_list;

	u8		*data_buffer;
	__le32		*reg_read_buf;

	phys_addr_t base_phys;
	dma_addr_t base_dma;
	dma_addr_t reg_read_dma;

	int		buf_size;
	int		buf_count;
	int		buf_start;
	unsigned int	max_cwperpage;

	int reg_read_pos;

	u32 cmd1, vld;
	bool exec_opwrite;
};

/*
 * This data type corresponds to the NAND controller properties which varies
 * among different NAND controllers.
 * @ecc_modes - ecc mode for NAND
 * @dev_cmd_reg_start - NAND_DEV_CMD_* registers starting offset
 * @supports_bam - whether NAND controller is using BAM
 * @nandc_part_of_qpic - whether NAND controller is part of qpic IP
 * @qpic_version2 - flag to indicate QPIC IP version 2
 * @use_codeword_fixup - whether NAND has different layout for boot partitions
 */
struct qcom_nandc_props {
	u32 ecc_modes;
	u32 dev_cmd_reg_start;
	u32 bam_offset;
	bool supports_bam;
	bool nandc_part_of_qpic;
	bool qpic_version2;
	bool use_codeword_fixup;
};

void qcom_free_bam_transaction(struct qcom_nand_controller *nandc);
struct bam_transaction *qcom_alloc_bam_transaction(struct qcom_nand_controller *nandc);
void qcom_clear_bam_transaction(struct qcom_nand_controller *nandc);
void qcom_qpic_bam_dma_done(void *data);
void qcom_nandc_dev_to_mem(struct qcom_nand_controller *nandc, bool is_cpu);
int qcom_prepare_bam_async_desc(struct qcom_nand_controller *nandc,
				struct dma_chan *chan, unsigned long flags);
int qcom_prep_bam_dma_desc_cmd(struct qcom_nand_controller *nandc, bool read,
			       int reg_off, const void *vaddr, int size, unsigned int flags);
int qcom_prep_bam_dma_desc_data(struct qcom_nand_controller *nandc, bool read,
				const void *vaddr, int size, unsigned int flags);
int qcom_prep_adm_dma_desc(struct qcom_nand_controller *nandc, bool read, int reg_off,
			   const void *vaddr, int size, bool flow_control);
int qcom_read_reg_dma(struct qcom_nand_controller *nandc, int first, int num_regs,
		      unsigned int flags);
int qcom_write_reg_dma(struct qcom_nand_controller *nandc, __le32 *vaddr, int first,
		       int num_regs, unsigned int flags);
int qcom_read_data_dma(struct qcom_nand_controller *nandc, int reg_off, const u8 *vaddr,
		       int size, unsigned int flags);
int qcom_write_data_dma(struct qcom_nand_controller *nandc, int reg_off, const u8 *vaddr,
			int size, unsigned int flags);
int qcom_submit_descs(struct qcom_nand_controller *nandc);
void qcom_clear_read_regs(struct qcom_nand_controller *nandc);
void qcom_nandc_unalloc(struct qcom_nand_controller *nandc);
int qcom_nandc_alloc(struct qcom_nand_controller *nandc);
#endif

