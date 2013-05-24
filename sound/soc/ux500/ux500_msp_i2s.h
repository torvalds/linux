/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */


#ifndef UX500_MSP_I2S_H
#define UX500_MSP_I2S_H

#include <linux/platform_device.h>

#define MSP_INPUT_FREQ_APB 48000000

/*** Stereo mode. Used for APB data accesses as 16 bits accesses (mono),
 *   32 bits accesses (stereo).
 ***/
enum msp_stereo_mode {
	MSP_MONO,
	MSP_STEREO
};

/* Direction (Transmit/Receive mode) */
enum msp_direction {
	MSP_TX = 1,
	MSP_RX = 2
};

/* Transmit and receive configuration register */
#define MSP_BIG_ENDIAN           0x00000000
#define MSP_LITTLE_ENDIAN        0x00001000
#define MSP_UNEXPECTED_FS_ABORT  0x00000000
#define MSP_UNEXPECTED_FS_IGNORE 0x00008000
#define MSP_NON_MODE_BIT_MASK    0x00009000

/* Global configuration register */
#define RX_ENABLE             0x00000001
#define RX_FIFO_ENABLE        0x00000002
#define RX_SYNC_SRG           0x00000010
#define RX_CLK_POL_RISING     0x00000020
#define RX_CLK_SEL_SRG        0x00000040
#define TX_ENABLE             0x00000100
#define TX_FIFO_ENABLE        0x00000200
#define TX_SYNC_SRG_PROG      0x00001800
#define TX_SYNC_SRG_AUTO      0x00001000
#define TX_CLK_POL_RISING     0x00002000
#define TX_CLK_SEL_SRG        0x00004000
#define TX_EXTRA_DELAY_ENABLE 0x00008000
#define SRG_ENABLE            0x00010000
#define FRAME_GEN_ENABLE      0x00100000
#define SRG_CLK_SEL_APB       0x00000000
#define RX_FIFO_SYNC_HI       0x00000000
#define TX_FIFO_SYNC_HI       0x00000000
#define SPI_CLK_MODE_NORMAL   0x00000000

#define MSP_FRAME_SIZE_AUTO -1

#define MSP_DR		0x00
#define MSP_GCR		0x04
#define MSP_TCF		0x08
#define MSP_RCF		0x0c
#define MSP_SRG		0x10
#define MSP_FLR		0x14
#define MSP_DMACR	0x18

#define MSP_IMSC	0x20
#define MSP_RIS		0x24
#define MSP_MIS		0x28
#define MSP_ICR		0x2c
#define MSP_MCR		0x30
#define MSP_RCV		0x34
#define MSP_RCM		0x38

#define MSP_TCE0	0x40
#define MSP_TCE1	0x44
#define MSP_TCE2	0x48
#define MSP_TCE3	0x4c

#define MSP_RCE0	0x60
#define MSP_RCE1	0x64
#define MSP_RCE2	0x68
#define MSP_RCE3	0x6c
#define MSP_IODLY	0x70

#define MSP_ITCR	0x80
#define MSP_ITIP	0x84
#define MSP_ITOP	0x88
#define MSP_TSTDR	0x8c

#define MSP_PID0	0xfe0
#define MSP_PID1	0xfe4
#define MSP_PID2	0xfe8
#define MSP_PID3	0xfec

#define MSP_CID0	0xff0
#define MSP_CID1	0xff4
#define MSP_CID2	0xff8
#define MSP_CID3	0xffc

/* Protocol dependant parameters list */
#define RX_ENABLE_MASK		BIT(0)
#define RX_FIFO_ENABLE_MASK	BIT(1)
#define RX_FSYNC_MASK		BIT(2)
#define DIRECT_COMPANDING_MASK	BIT(3)
#define RX_SYNC_SEL_MASK	BIT(4)
#define RX_CLK_POL_MASK		BIT(5)
#define RX_CLK_SEL_MASK		BIT(6)
#define LOOPBACK_MASK		BIT(7)
#define TX_ENABLE_MASK		BIT(8)
#define TX_FIFO_ENABLE_MASK	BIT(9)
#define TX_FSYNC_MASK		BIT(10)
#define TX_MSP_TDR_TSR		BIT(11)
#define TX_SYNC_SEL_MASK	(BIT(12) | BIT(11))
#define TX_CLK_POL_MASK		BIT(13)
#define TX_CLK_SEL_MASK		BIT(14)
#define TX_EXTRA_DELAY_MASK	BIT(15)
#define SRG_ENABLE_MASK		BIT(16)
#define SRG_CLK_POL_MASK	BIT(17)
#define SRG_CLK_SEL_MASK	(BIT(19) | BIT(18))
#define FRAME_GEN_EN_MASK	BIT(20)
#define SPI_CLK_MODE_MASK	(BIT(22) | BIT(21))
#define SPI_BURST_MODE_MASK	BIT(23)

#define RXEN_SHIFT		0
#define RFFEN_SHIFT		1
#define RFSPOL_SHIFT		2
#define DCM_SHIFT		3
#define RFSSEL_SHIFT		4
#define RCKPOL_SHIFT		5
#define RCKSEL_SHIFT		6
#define LBM_SHIFT		7
#define TXEN_SHIFT		8
#define TFFEN_SHIFT		9
#define TFSPOL_SHIFT		10
#define TFSSEL_SHIFT		11
#define TCKPOL_SHIFT		13
#define TCKSEL_SHIFT		14
#define TXDDL_SHIFT		15
#define SGEN_SHIFT		16
#define SCKPOL_SHIFT		17
#define SCKSEL_SHIFT		18
#define FGEN_SHIFT		20
#define SPICKM_SHIFT		21
#define TBSWAP_SHIFT		28

#define RCKPOL_MASK		BIT(0)
#define TCKPOL_MASK		BIT(0)
#define SPICKM_MASK		(BIT(1) | BIT(0))
#define MSP_RX_CLKPOL_BIT(n)     ((n & RCKPOL_MASK) << RCKPOL_SHIFT)
#define MSP_TX_CLKPOL_BIT(n)     ((n & TCKPOL_MASK) << TCKPOL_SHIFT)

#define P1ELEN_SHIFT		0
#define P1FLEN_SHIFT		3
#define DTYP_SHIFT		10
#define ENDN_SHIFT		12
#define DDLY_SHIFT		13
#define FSIG_SHIFT		15
#define P2ELEN_SHIFT		16
#define P2FLEN_SHIFT		19
#define P2SM_SHIFT		26
#define P2EN_SHIFT		27
#define FSYNC_SHIFT		15

#define P1ELEN_MASK		0x00000007
#define P2ELEN_MASK		0x00070000
#define P1FLEN_MASK		0x00000378
#define P2FLEN_MASK		0x03780000
#define DDLY_MASK		0x00003000
#define DTYP_MASK		0x00000600
#define P2SM_MASK		0x04000000
#define P2EN_MASK		0x08000000
#define ENDN_MASK		0x00001000
#define TFSPOL_MASK		0x00000400
#define TBSWAP_MASK		0x30000000
#define COMPANDING_MODE_MASK	0x00000c00
#define FSYNC_MASK		0x00008000

#define MSP_P1_ELEM_LEN_BITS(n)		(n & P1ELEN_MASK)
#define MSP_P2_ELEM_LEN_BITS(n)		(((n) << P2ELEN_SHIFT) & P2ELEN_MASK)
#define MSP_P1_FRAME_LEN_BITS(n)	(((n) << P1FLEN_SHIFT) & P1FLEN_MASK)
#define MSP_P2_FRAME_LEN_BITS(n)	(((n) << P2FLEN_SHIFT) & P2FLEN_MASK)
#define MSP_DATA_DELAY_BITS(n)		(((n) << DDLY_SHIFT) & DDLY_MASK)
#define MSP_DATA_TYPE_BITS(n)		(((n) << DTYP_SHIFT) & DTYP_MASK)
#define MSP_P2_START_MODE_BIT(n)	((n << P2SM_SHIFT) & P2SM_MASK)
#define MSP_P2_ENABLE_BIT(n)		((n << P2EN_SHIFT) & P2EN_MASK)
#define MSP_SET_ENDIANNES_BIT(n)	((n << ENDN_SHIFT) & ENDN_MASK)
#define MSP_FSYNC_POL(n)		((n << TFSPOL_SHIFT) & TFSPOL_MASK)
#define MSP_DATA_WORD_SWAP(n)		((n << TBSWAP_SHIFT) & TBSWAP_MASK)
#define MSP_SET_COMPANDING_MODE(n)	((n << DTYP_SHIFT) & \
						COMPANDING_MODE_MASK)
#define MSP_SET_FSYNC_IGNORE(n)		((n << FSYNC_SHIFT) & FSYNC_MASK)

/* Flag register */
#define RX_BUSY			BIT(0)
#define RX_FIFO_EMPTY		BIT(1)
#define RX_FIFO_FULL		BIT(2)
#define TX_BUSY			BIT(3)
#define TX_FIFO_EMPTY		BIT(4)
#define TX_FIFO_FULL		BIT(5)

#define RBUSY_SHIFT		0
#define RFE_SHIFT		1
#define RFU_SHIFT		2
#define TBUSY_SHIFT		3
#define TFE_SHIFT		4
#define TFU_SHIFT		5

/* Multichannel control register */
#define RMCEN_SHIFT		0
#define RMCSF_SHIFT		1
#define RCMPM_SHIFT		3
#define TMCEN_SHIFT		5
#define TNCSF_SHIFT		6

/* Sample rate generator register */
#define SCKDIV_SHIFT		0
#define FRWID_SHIFT		10
#define FRPER_SHIFT		16

#define SCK_DIV_MASK		0x0000003FF
#define FRAME_WIDTH_BITS(n)	(((n) << FRWID_SHIFT)  & 0x0000FC00)
#define FRAME_PERIOD_BITS(n)	(((n) << FRPER_SHIFT) & 0x1FFF0000)

/* DMA controller register */
#define RX_DMA_ENABLE		BIT(0)
#define TX_DMA_ENABLE		BIT(1)

#define RDMAE_SHIFT		0
#define TDMAE_SHIFT		1

/* Interrupt Register */
#define RX_SERVICE_INT		BIT(0)
#define RX_OVERRUN_ERROR_INT	BIT(1)
#define RX_FSYNC_ERR_INT	BIT(2)
#define RX_FSYNC_INT		BIT(3)
#define TX_SERVICE_INT		BIT(4)
#define TX_UNDERRUN_ERR_INT	BIT(5)
#define TX_FSYNC_ERR_INT	BIT(6)
#define TX_FSYNC_INT		BIT(7)
#define ALL_INT			0x000000ff

/* MSP test control register */
#define MSP_ITCR_ITEN		BIT(0)
#define MSP_ITCR_TESTFIFO	BIT(1)

#define RMCEN_BIT   0
#define RMCSF_BIT   1
#define RCMPM_BIT   3
#define TMCEN_BIT   5
#define TNCSF_BIT   6

/* Single or dual phase mode */
enum msp_phase_mode {
	MSP_SINGLE_PHASE,
	MSP_DUAL_PHASE
};

/* Frame length */
enum msp_frame_length {
	MSP_FRAME_LEN_1 = 0,
	MSP_FRAME_LEN_2 = 1,
	MSP_FRAME_LEN_4 = 3,
	MSP_FRAME_LEN_8 = 7,
	MSP_FRAME_LEN_12 = 11,
	MSP_FRAME_LEN_16 = 15,
	MSP_FRAME_LEN_20 = 19,
	MSP_FRAME_LEN_32 = 31,
	MSP_FRAME_LEN_48 = 47,
	MSP_FRAME_LEN_64 = 63
};

/* Element length */
enum msp_elem_length {
	MSP_ELEM_LEN_8 = 0,
	MSP_ELEM_LEN_10 = 1,
	MSP_ELEM_LEN_12 = 2,
	MSP_ELEM_LEN_14 = 3,
	MSP_ELEM_LEN_16 = 4,
	MSP_ELEM_LEN_20 = 5,
	MSP_ELEM_LEN_24 = 6,
	MSP_ELEM_LEN_32 = 7
};

enum msp_data_xfer_width {
	MSP_DATA_TRANSFER_WIDTH_BYTE,
	MSP_DATA_TRANSFER_WIDTH_HALFWORD,
	MSP_DATA_TRANSFER_WIDTH_WORD
};

enum msp_frame_sync {
	MSP_FSYNC_UNIGNORE = 0,
	MSP_FSYNC_IGNORE = 1,
};

enum msp_phase2_start_mode {
	MSP_PHASE2_START_MODE_IMEDIATE,
	MSP_PHASE2_START_MODE_FSYNC
};

enum msp_btf {
	MSP_BTF_MS_BIT_FIRST = 0,
	MSP_BTF_LS_BIT_FIRST = 1
};

enum msp_fsync_pol {
	MSP_FSYNC_POL_ACT_HI = 0,
	MSP_FSYNC_POL_ACT_LO = 1
};

/* Data delay (in bit clock cycles) */
enum msp_delay {
	MSP_DELAY_0 = 0,
	MSP_DELAY_1 = 1,
	MSP_DELAY_2 = 2,
	MSP_DELAY_3 = 3
};

/* Configurations of clocks (transmit, receive or sample rate generator) */
enum msp_edge {
	MSP_FALLING_EDGE = 0,
	MSP_RISING_EDGE = 1,
};

enum msp_hws {
	MSP_SWAP_NONE = 0,
	MSP_SWAP_BYTE_PER_WORD = 1,
	MSP_SWAP_BYTE_PER_HALF_WORD = 2,
	MSP_SWAP_HALF_WORD_PER_WORD = 3
};

enum msp_compress_mode {
	MSP_COMPRESS_MODE_LINEAR = 0,
	MSP_COMPRESS_MODE_MU_LAW = 2,
	MSP_COMPRESS_MODE_A_LAW = 3
};

enum msp_spi_burst_mode {
	MSP_SPI_BURST_MODE_DISABLE = 0,
	MSP_SPI_BURST_MODE_ENABLE = 1
};

enum msp_expand_mode {
	MSP_EXPAND_MODE_LINEAR = 0,
	MSP_EXPAND_MODE_LINEAR_SIGNED = 1,
	MSP_EXPAND_MODE_MU_LAW = 2,
	MSP_EXPAND_MODE_A_LAW = 3
};

#define MSP_FRAME_PERIOD_IN_MONO_MODE 256
#define MSP_FRAME_PERIOD_IN_STEREO_MODE 32
#define MSP_FRAME_WIDTH_IN_STEREO_MODE 16

enum msp_protocol {
	MSP_I2S_PROTOCOL,
	MSP_PCM_PROTOCOL,
	MSP_PCM_COMPAND_PROTOCOL,
	MSP_INVALID_PROTOCOL
};

/*
 * No of registers to backup during
 * suspend resume
 */
#define MAX_MSP_BACKUP_REGS 36

enum enum_i2s_controller {
	MSP_0_I2S_CONTROLLER = 0,
	MSP_1_I2S_CONTROLLER,
	MSP_2_I2S_CONTROLLER,
	MSP_3_I2S_CONTROLLER,
};

enum i2s_direction_t {
	MSP_DIR_TX = 0x01,
	MSP_DIR_RX = 0x02,
};

enum msp_data_size {
	MSP_DATA_BITS_DEFAULT = -1,
	MSP_DATA_BITS_8 = 0x00,
	MSP_DATA_BITS_10,
	MSP_DATA_BITS_12,
	MSP_DATA_BITS_14,
	MSP_DATA_BITS_16,
	MSP_DATA_BITS_20,
	MSP_DATA_BITS_24,
	MSP_DATA_BITS_32,
};

enum msp_state {
	MSP_STATE_IDLE = 0,
	MSP_STATE_CONFIGURED = 1,
	MSP_STATE_RUNNING = 2,
};

enum msp_rx_comparison_enable_mode {
	MSP_COMPARISON_DISABLED = 0,
	MSP_COMPARISON_NONEQUAL_ENABLED = 2,
	MSP_COMPARISON_EQUAL_ENABLED = 3
};

struct msp_multichannel_config {
	bool rx_multichannel_enable;
	bool tx_multichannel_enable;
	enum msp_rx_comparison_enable_mode rx_comparison_enable_mode;
	u8 padding;
	u32 comparison_value;
	u32 comparison_mask;
	u32 rx_channel_0_enable;
	u32 rx_channel_1_enable;
	u32 rx_channel_2_enable;
	u32 rx_channel_3_enable;
	u32 tx_channel_0_enable;
	u32 tx_channel_1_enable;
	u32 tx_channel_2_enable;
	u32 tx_channel_3_enable;
};

struct msp_protdesc {
	u32 rx_phase_mode;
	u32 tx_phase_mode;
	u32 rx_phase2_start_mode;
	u32 tx_phase2_start_mode;
	u32 rx_byte_order;
	u32 tx_byte_order;
	u32 rx_frame_len_1;
	u32 rx_frame_len_2;
	u32 tx_frame_len_1;
	u32 tx_frame_len_2;
	u32 rx_elem_len_1;
	u32 rx_elem_len_2;
	u32 tx_elem_len_1;
	u32 tx_elem_len_2;
	u32 rx_data_delay;
	u32 tx_data_delay;
	u32 rx_clk_pol;
	u32 tx_clk_pol;
	u32 rx_fsync_pol;
	u32 tx_fsync_pol;
	u32 rx_half_word_swap;
	u32 tx_half_word_swap;
	u32 compression_mode;
	u32 expansion_mode;
	u32 frame_sync_ignore;
	u32 frame_period;
	u32 frame_width;
	u32 clocks_per_frame;
};

struct i2s_message {
	enum i2s_direction_t i2s_direction;
	void *txdata;
	void *rxdata;
	size_t txbytes;
	size_t rxbytes;
	int dma_flag;
	int tx_offset;
	int rx_offset;
	bool cyclic_dma;
	dma_addr_t buf_addr;
	size_t buf_len;
	size_t period_len;
};

struct ux500_msp_config {
	unsigned int f_inputclk;
	unsigned int rx_clk_sel;
	unsigned int tx_clk_sel;
	unsigned int srg_clk_sel;
	unsigned int rx_fsync_pol;
	unsigned int tx_fsync_pol;
	unsigned int rx_fsync_sel;
	unsigned int tx_fsync_sel;
	unsigned int rx_fifo_config;
	unsigned int tx_fifo_config;
	unsigned int spi_clk_mode;
	unsigned int spi_burst_mode;
	unsigned int loopback_enable;
	unsigned int tx_data_enable;
	unsigned int default_protdesc;
	struct msp_protdesc protdesc;
	int multichannel_configured;
	struct msp_multichannel_config multichannel_config;
	unsigned int direction;
	unsigned int protocol;
	unsigned int frame_freq;
	unsigned int frame_size;
	enum msp_data_size data_size;
	unsigned int def_elem_len;
	unsigned int iodelay;
	void (*handler) (void *data);
	void *tx_callback_data;
	void *rx_callback_data;
};

struct ux500_msp {
	enum enum_i2s_controller id;
	void __iomem *registers;
	struct device *dev;
	struct stedma40_chan_cfg *dma_cfg_rx;
	struct stedma40_chan_cfg *dma_cfg_tx;
	struct dma_chan *tx_pipeid;
	struct dma_chan *rx_pipeid;
	enum msp_state msp_state;
	int (*transfer) (struct ux500_msp *msp, struct i2s_message *message);
	struct timer_list notify_timer;
	int def_elem_len;
	unsigned int dir_busy;
	int loopback_enable;
	u32 backup_regs[MAX_MSP_BACKUP_REGS];
	unsigned int f_bitclk;
};

struct ux500_msp_dma_params {
	unsigned int data_size;
	struct stedma40_chan_cfg *dma_cfg;
};

struct msp_i2s_platform_data;
int ux500_msp_i2s_init_msp(struct platform_device *pdev,
			struct ux500_msp **msp_p,
			struct msp_i2s_platform_data *platform_data);
void ux500_msp_i2s_cleanup_msp(struct platform_device *pdev,
			struct ux500_msp *msp);
int ux500_msp_i2s_open(struct ux500_msp *msp, struct ux500_msp_config *config);
int ux500_msp_i2s_close(struct ux500_msp *msp,
			unsigned int dir);
int ux500_msp_i2s_trigger(struct ux500_msp *msp, int cmd,
			int direction);

#endif
