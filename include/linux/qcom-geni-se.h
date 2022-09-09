/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_QCOM_GENI_SE
#define _LINUX_QCOM_GENI_SE

#include <linux/interconnect.h>

/**
 * enum geni_se_xfer_mode: Transfer modes supported by Serial Engines
 *
 * @GENI_SE_INVALID: Invalid mode
 * @GENI_SE_FIFO: FIFO mode. Data is transferred with SE FIFO
 * by programmed IO method
 * @GENI_SE_DMA: Serial Engine DMA mode. Data is transferred
 * with SE by DMAengine internal to SE
 * @GENI_GPI_DMA: GPI DMA mode. Data is transferred using a DMAengine
 * configured by a firmware residing on a GSI engine. This DMA name is
 * interchangeably used as GSI or GPI which seem to imply the same DMAengine
 */

enum geni_se_xfer_mode {
	GENI_SE_INVALID,
	GENI_SE_FIFO,
	GENI_SE_DMA,
	GENI_GPI_DMA,
};

/* Protocols supported by GENI Serial Engines */
enum geni_se_protocol_type {
	GENI_SE_NONE,
	GENI_SE_SPI,
	GENI_SE_UART,
	GENI_SE_I2C,
	GENI_SE_I3C,
};

struct geni_wrapper;
struct clk;

enum geni_icc_path_index {
	GENI_TO_CORE,
	CPU_TO_GENI,
	GENI_TO_DDR
};

struct geni_icc_path {
	struct icc_path *path;
	unsigned int avg_bw;
};

/**
 * struct geni_se - GENI Serial Engine
 * @base:		Base Address of the Serial Engine's register block
 * @dev:		Pointer to the Serial Engine device
 * @wrapper:		Pointer to the parent QUP Wrapper core
 * @clk:		Handle to the core serial engine clock
 * @num_clk_levels:	Number of valid clock levels in clk_perf_tbl
 * @clk_perf_tbl:	Table of clock frequency input to serial engine clock
 * @icc_paths:		Array of ICC paths for SE
 */
struct geni_se {
	void __iomem *base;
	struct device *dev;
	struct geni_wrapper *wrapper;
	struct clk *clk;
	unsigned int num_clk_levels;
	unsigned long *clk_perf_tbl;
	struct geni_icc_path icc_paths[3];
};

/* Common SE registers */
#define GENI_FORCE_DEFAULT_REG		0x20
#define SE_GENI_STATUS			0x40
#define GENI_SER_M_CLK_CFG		0x48
#define GENI_SER_S_CLK_CFG		0x4c
#define GENI_IF_DISABLE_RO		0x64
#define GENI_FW_REVISION_RO		0x68
#define SE_GENI_CLK_SEL			0x7c
#define SE_GENI_DMA_MODE_EN		0x258
#define SE_GENI_M_CMD0			0x600
#define SE_GENI_M_CMD_CTRL_REG		0x604
#define SE_GENI_M_IRQ_STATUS		0x610
#define SE_GENI_M_IRQ_EN		0x614
#define SE_GENI_M_IRQ_CLEAR		0x618
#define SE_GENI_S_CMD0			0x630
#define SE_GENI_S_CMD_CTRL_REG		0x634
#define SE_GENI_S_IRQ_STATUS		0x640
#define SE_GENI_S_IRQ_EN		0x644
#define SE_GENI_S_IRQ_CLEAR		0x648
#define SE_GENI_TX_FIFOn		0x700
#define SE_GENI_RX_FIFOn		0x780
#define SE_GENI_TX_FIFO_STATUS		0x800
#define SE_GENI_RX_FIFO_STATUS		0x804
#define SE_GENI_TX_WATERMARK_REG	0x80c
#define SE_GENI_RX_WATERMARK_REG	0x810
#define SE_GENI_RX_RFR_WATERMARK_REG	0x814
#define SE_GENI_IOS			0x908
#define SE_DMA_TX_IRQ_STAT		0xc40
#define SE_DMA_TX_IRQ_CLR		0xc44
#define SE_DMA_TX_FSM_RST		0xc58
#define SE_DMA_RX_IRQ_STAT		0xd40
#define SE_DMA_RX_IRQ_CLR		0xd44
#define SE_DMA_RX_FSM_RST		0xd58
#define SE_HW_PARAM_0			0xe24
#define SE_HW_PARAM_1			0xe28

/* GENI_FORCE_DEFAULT_REG fields */
#define FORCE_DEFAULT	BIT(0)

/* GENI_STATUS fields */
#define M_GENI_CMD_ACTIVE		BIT(0)
#define S_GENI_CMD_ACTIVE		BIT(12)

/* GENI_SER_M_CLK_CFG/GENI_SER_S_CLK_CFG */
#define SER_CLK_EN			BIT(0)
#define CLK_DIV_MSK			GENMASK(15, 4)
#define CLK_DIV_SHFT			4

/* GENI_IF_DISABLE_RO fields */
#define FIFO_IF_DISABLE			(BIT(0))

/* GENI_FW_REVISION_RO fields */
#define FW_REV_PROTOCOL_MSK		GENMASK(15, 8)
#define FW_REV_PROTOCOL_SHFT		8

/* GENI_CLK_SEL fields */
#define CLK_SEL_MSK			GENMASK(2, 0)

/* SE_GENI_DMA_MODE_EN */
#define GENI_DMA_MODE_EN		BIT(0)

/* GENI_M_CMD0 fields */
#define M_OPCODE_MSK			GENMASK(31, 27)
#define M_OPCODE_SHFT			27
#define M_PARAMS_MSK			GENMASK(26, 0)

/* GENI_M_CMD_CTRL_REG */
#define M_GENI_CMD_CANCEL		BIT(2)
#define M_GENI_CMD_ABORT		BIT(1)
#define M_GENI_DISABLE			BIT(0)

/* GENI_S_CMD0 fields */
#define S_OPCODE_MSK			GENMASK(31, 27)
#define S_OPCODE_SHFT			27
#define S_PARAMS_MSK			GENMASK(26, 0)

/* GENI_S_CMD_CTRL_REG */
#define S_GENI_CMD_CANCEL		BIT(2)
#define S_GENI_CMD_ABORT		BIT(1)
#define S_GENI_DISABLE			BIT(0)

/* GENI_M_IRQ_EN fields */
#define M_CMD_DONE_EN			BIT(0)
#define M_CMD_OVERRUN_EN		BIT(1)
#define M_ILLEGAL_CMD_EN		BIT(2)
#define M_CMD_FAILURE_EN		BIT(3)
#define M_CMD_CANCEL_EN			BIT(4)
#define M_CMD_ABORT_EN			BIT(5)
#define M_TIMESTAMP_EN			BIT(6)
#define M_RX_IRQ_EN			BIT(7)
#define M_GP_SYNC_IRQ_0_EN		BIT(8)
#define M_GP_IRQ_0_EN			BIT(9)
#define M_GP_IRQ_1_EN			BIT(10)
#define M_GP_IRQ_2_EN			BIT(11)
#define M_GP_IRQ_3_EN			BIT(12)
#define M_GP_IRQ_4_EN			BIT(13)
#define M_GP_IRQ_5_EN			BIT(14)
#define M_IO_DATA_DEASSERT_EN		BIT(22)
#define M_IO_DATA_ASSERT_EN		BIT(23)
#define M_RX_FIFO_RD_ERR_EN		BIT(24)
#define M_RX_FIFO_WR_ERR_EN		BIT(25)
#define M_RX_FIFO_WATERMARK_EN		BIT(26)
#define M_RX_FIFO_LAST_EN		BIT(27)
#define M_TX_FIFO_RD_ERR_EN		BIT(28)
#define M_TX_FIFO_WR_ERR_EN		BIT(29)
#define M_TX_FIFO_WATERMARK_EN		BIT(30)
#define M_SEC_IRQ_EN			BIT(31)
#define M_COMMON_GENI_M_IRQ_EN	(GENMASK(6, 1) | \
				M_IO_DATA_DEASSERT_EN | \
				M_IO_DATA_ASSERT_EN | M_RX_FIFO_RD_ERR_EN | \
				M_RX_FIFO_WR_ERR_EN | M_TX_FIFO_RD_ERR_EN | \
				M_TX_FIFO_WR_ERR_EN)

/* GENI_S_IRQ_EN fields */
#define S_CMD_DONE_EN			BIT(0)
#define S_CMD_OVERRUN_EN		BIT(1)
#define S_ILLEGAL_CMD_EN		BIT(2)
#define S_CMD_FAILURE_EN		BIT(3)
#define S_CMD_CANCEL_EN			BIT(4)
#define S_CMD_ABORT_EN			BIT(5)
#define S_GP_SYNC_IRQ_0_EN		BIT(8)
#define S_GP_IRQ_0_EN			BIT(9)
#define S_GP_IRQ_1_EN			BIT(10)
#define S_GP_IRQ_2_EN			BIT(11)
#define S_GP_IRQ_3_EN			BIT(12)
#define S_GP_IRQ_4_EN			BIT(13)
#define S_GP_IRQ_5_EN			BIT(14)
#define S_IO_DATA_DEASSERT_EN		BIT(22)
#define S_IO_DATA_ASSERT_EN		BIT(23)
#define S_RX_FIFO_RD_ERR_EN		BIT(24)
#define S_RX_FIFO_WR_ERR_EN		BIT(25)
#define S_RX_FIFO_WATERMARK_EN		BIT(26)
#define S_RX_FIFO_LAST_EN		BIT(27)
#define S_COMMON_GENI_S_IRQ_EN	(GENMASK(5, 1) | GENMASK(13, 9) | \
				 S_RX_FIFO_RD_ERR_EN | S_RX_FIFO_WR_ERR_EN)

/*  GENI_/TX/RX/RX_RFR/_WATERMARK_REG fields */
#define WATERMARK_MSK			GENMASK(5, 0)

/* GENI_TX_FIFO_STATUS fields */
#define TX_FIFO_WC			GENMASK(27, 0)

/*  GENI_RX_FIFO_STATUS fields */
#define RX_LAST				BIT(31)
#define RX_LAST_BYTE_VALID_MSK		GENMASK(30, 28)
#define RX_LAST_BYTE_VALID_SHFT		28
#define RX_FIFO_WC_MSK			GENMASK(24, 0)

/* SE_GENI_IOS fields */
#define IO2_DATA_IN			BIT(1)
#define RX_DATA_IN			BIT(0)

/* SE_DMA_TX_IRQ_STAT Register fields */
#define TX_DMA_DONE			BIT(0)
#define TX_EOT				BIT(1)
#define TX_SBE				BIT(2)
#define TX_RESET_DONE			BIT(3)

/* SE_DMA_RX_IRQ_STAT Register fields */
#define RX_DMA_DONE			BIT(0)
#define RX_EOT				BIT(1)
#define RX_SBE				BIT(2)
#define RX_RESET_DONE			BIT(3)
#define RX_FLUSH_DONE			BIT(4)
#define RX_GENI_GP_IRQ			GENMASK(10, 5)
#define RX_GENI_CANCEL_IRQ		BIT(11)
#define RX_GENI_GP_IRQ_EXT		GENMASK(13, 12)

/* SE_HW_PARAM_0 fields */
#define TX_FIFO_WIDTH_MSK		GENMASK(29, 24)
#define TX_FIFO_WIDTH_SHFT		24
#define TX_FIFO_DEPTH_MSK		GENMASK(21, 16)
#define TX_FIFO_DEPTH_SHFT		16

/* SE_HW_PARAM_1 fields */
#define RX_FIFO_WIDTH_MSK		GENMASK(29, 24)
#define RX_FIFO_WIDTH_SHFT		24
#define RX_FIFO_DEPTH_MSK		GENMASK(21, 16)
#define RX_FIFO_DEPTH_SHFT		16

#define HW_VER_MAJOR_MASK		GENMASK(31, 28)
#define HW_VER_MAJOR_SHFT		28
#define HW_VER_MINOR_MASK		GENMASK(27, 16)
#define HW_VER_MINOR_SHFT		16
#define HW_VER_STEP_MASK		GENMASK(15, 0)

#define GENI_SE_VERSION_MAJOR(ver) ((ver & HW_VER_MAJOR_MASK) >> HW_VER_MAJOR_SHFT)
#define GENI_SE_VERSION_MINOR(ver) ((ver & HW_VER_MINOR_MASK) >> HW_VER_MINOR_SHFT)
#define GENI_SE_VERSION_STEP(ver) (ver & HW_VER_STEP_MASK)

/* QUP SE VERSION value for major number 2 and minor number 5 */
#define QUP_SE_VERSION_2_5                  0x20050000

/*
 * Define bandwidth thresholds that cause the underlying Core 2X interconnect
 * clock to run at the named frequency. These baseline values are recommended
 * by the hardware team, and are not dynamically scaled with GENI bandwidth
 * beyond basic on/off.
 */
#define CORE_2X_19_2_MHZ		960
#define CORE_2X_50_MHZ			2500
#define CORE_2X_100_MHZ			5000
#define CORE_2X_150_MHZ			7500
#define CORE_2X_200_MHZ			10000
#define CORE_2X_236_MHZ			16383

#define GENI_DEFAULT_BW			Bps_to_icc(1000)

#if IS_ENABLED(CONFIG_QCOM_GENI_SE)

u32 geni_se_get_qup_hw_version(struct geni_se *se);

/**
 * geni_se_read_proto() - Read the protocol configured for a serial engine
 * @se:		Pointer to the concerned serial engine.
 *
 * Return: Protocol value as configured in the serial engine.
 */
static inline u32 geni_se_read_proto(struct geni_se *se)
{
	u32 val;

	val = readl_relaxed(se->base + GENI_FW_REVISION_RO);

	return (val & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
}

/**
 * geni_se_setup_m_cmd() - Setup the primary sequencer
 * @se:		Pointer to the concerned serial engine.
 * @cmd:	Command/Operation to setup in the primary sequencer.
 * @params:	Parameter for the sequencer command.
 *
 * This function is used to configure the primary sequencer with the
 * command and its associated parameters.
 */
static inline void geni_se_setup_m_cmd(struct geni_se *se, u32 cmd, u32 params)
{
	u32 m_cmd;

	m_cmd = (cmd << M_OPCODE_SHFT) | (params & M_PARAMS_MSK);
	writel(m_cmd, se->base + SE_GENI_M_CMD0);
}

/**
 * geni_se_setup_s_cmd() - Setup the secondary sequencer
 * @se:		Pointer to the concerned serial engine.
 * @cmd:	Command/Operation to setup in the secondary sequencer.
 * @params:	Parameter for the sequencer command.
 *
 * This function is used to configure the secondary sequencer with the
 * command and its associated parameters.
 */
static inline void geni_se_setup_s_cmd(struct geni_se *se, u32 cmd, u32 params)
{
	u32 s_cmd;

	s_cmd = readl_relaxed(se->base + SE_GENI_S_CMD0);
	s_cmd &= ~(S_OPCODE_MSK | S_PARAMS_MSK);
	s_cmd |= (cmd << S_OPCODE_SHFT);
	s_cmd |= (params & S_PARAMS_MSK);
	writel(s_cmd, se->base + SE_GENI_S_CMD0);
}

/**
 * geni_se_cancel_m_cmd() - Cancel the command configured in the primary
 *                          sequencer
 * @se:	Pointer to the concerned serial engine.
 *
 * This function is used to cancel the currently configured command in the
 * primary sequencer.
 */
static inline void geni_se_cancel_m_cmd(struct geni_se *se)
{
	writel_relaxed(M_GENI_CMD_CANCEL, se->base + SE_GENI_M_CMD_CTRL_REG);
}

/**
 * geni_se_cancel_s_cmd() - Cancel the command configured in the secondary
 *                          sequencer
 * @se:	Pointer to the concerned serial engine.
 *
 * This function is used to cancel the currently configured command in the
 * secondary sequencer.
 */
static inline void geni_se_cancel_s_cmd(struct geni_se *se)
{
	writel_relaxed(S_GENI_CMD_CANCEL, se->base + SE_GENI_S_CMD_CTRL_REG);
}

/**
 * geni_se_abort_m_cmd() - Abort the command configured in the primary sequencer
 * @se:	Pointer to the concerned serial engine.
 *
 * This function is used to force abort the currently configured command in the
 * primary sequencer.
 */
static inline void geni_se_abort_m_cmd(struct geni_se *se)
{
	writel_relaxed(M_GENI_CMD_ABORT, se->base + SE_GENI_M_CMD_CTRL_REG);
}

/**
 * geni_se_abort_s_cmd() - Abort the command configured in the secondary
 *                         sequencer
 * @se:	Pointer to the concerned serial engine.
 *
 * This function is used to force abort the currently configured command in the
 * secondary sequencer.
 */
static inline void geni_se_abort_s_cmd(struct geni_se *se)
{
	writel_relaxed(S_GENI_CMD_ABORT, se->base + SE_GENI_S_CMD_CTRL_REG);
}

/**
 * geni_se_get_tx_fifo_depth() - Get the TX fifo depth of the serial engine
 * @se:	Pointer to the concerned serial engine.
 *
 * This function is used to get the depth i.e. number of elements in the
 * TX fifo of the serial engine.
 *
 * Return: TX fifo depth in units of FIFO words.
 */
static inline u32 geni_se_get_tx_fifo_depth(struct geni_se *se)
{
	u32 val;

	val = readl_relaxed(se->base + SE_HW_PARAM_0);

	return (val & TX_FIFO_DEPTH_MSK) >> TX_FIFO_DEPTH_SHFT;
}

/**
 * geni_se_get_tx_fifo_width() - Get the TX fifo width of the serial engine
 * @se:	Pointer to the concerned serial engine.
 *
 * This function is used to get the width i.e. word size per element in the
 * TX fifo of the serial engine.
 *
 * Return: TX fifo width in bits
 */
static inline u32 geni_se_get_tx_fifo_width(struct geni_se *se)
{
	u32 val;

	val = readl_relaxed(se->base + SE_HW_PARAM_0);

	return (val & TX_FIFO_WIDTH_MSK) >> TX_FIFO_WIDTH_SHFT;
}

/**
 * geni_se_get_rx_fifo_depth() - Get the RX fifo depth of the serial engine
 * @se:	Pointer to the concerned serial engine.
 *
 * This function is used to get the depth i.e. number of elements in the
 * RX fifo of the serial engine.
 *
 * Return: RX fifo depth in units of FIFO words
 */
static inline u32 geni_se_get_rx_fifo_depth(struct geni_se *se)
{
	u32 val;

	val = readl_relaxed(se->base + SE_HW_PARAM_1);

	return (val & RX_FIFO_DEPTH_MSK) >> RX_FIFO_DEPTH_SHFT;
}

void geni_se_init(struct geni_se *se, u32 rx_wm, u32 rx_rfr);

void geni_se_select_mode(struct geni_se *se, enum geni_se_xfer_mode mode);

void geni_se_config_packing(struct geni_se *se, int bpw, int pack_words,
			    bool msb_to_lsb, bool tx_cfg, bool rx_cfg);

int geni_se_resources_off(struct geni_se *se);

int geni_se_resources_on(struct geni_se *se);

int geni_se_clk_tbl_get(struct geni_se *se, unsigned long **tbl);

int geni_se_clk_freq_match(struct geni_se *se, unsigned long req_freq,
			   unsigned int *index, unsigned long *res_freq,
			   bool exact);

int geni_se_tx_dma_prep(struct geni_se *se, void *buf, size_t len,
			dma_addr_t *iova);

int geni_se_rx_dma_prep(struct geni_se *se, void *buf, size_t len,
			dma_addr_t *iova);

void geni_se_tx_dma_unprep(struct geni_se *se, dma_addr_t iova, size_t len);

void geni_se_rx_dma_unprep(struct geni_se *se, dma_addr_t iova, size_t len);

int geni_icc_get(struct geni_se *se, const char *icc_ddr);

int geni_icc_set_bw(struct geni_se *se);
void geni_icc_set_tag(struct geni_se *se, u32 tag);

int geni_icc_enable(struct geni_se *se);

int geni_icc_disable(struct geni_se *se);
#endif
#endif
