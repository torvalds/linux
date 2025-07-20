/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPI_SH_MSIOF_H__
#define __SPI_SH_MSIOF_H__

#include <linux/bitfield.h>
#include <linux/bits.h>

#define SITMDR1	0x00	/* Transmit Mode Register 1 */
#define SITMDR2	0x04	/* Transmit Mode Register 2 */
#define SITMDR3	0x08	/* Transmit Mode Register 3 */
#define SIRMDR1	0x10	/* Receive Mode Register 1 */
#define SIRMDR2	0x14	/* Receive Mode Register 2 */
#define SIRMDR3	0x18	/* Receive Mode Register 3 */
#define SITSCR	0x20	/* Transmit Clock Select Register */
#define SIRSCR	0x22	/* Receive Clock Select Register (SH, A1, APE6) */
#define SICTR	0x28	/* Control Register */
#define SIFCTR	0x30	/* FIFO Control Register */
#define SISTR	0x40	/* Status Register */
#define SIIER	0x44	/* Interrupt Enable Register */
#define SITDR1	0x48	/* Transmit Control Data Register 1 (SH, A1) */
#define SITDR2	0x4c	/* Transmit Control Data Register 2 (SH, A1) */
#define SITFDR	0x50	/* Transmit FIFO Data Register */
#define SIRDR1	0x58	/* Receive Control Data Register 1 (SH, A1) */
#define SIRDR2	0x5c	/* Receive Control Data Register 2 (SH, A1) */
#define SIRFDR	0x60	/* Receive FIFO Data Register */

/* SITMDR1 and SIRMDR1 */
#define SIMDR1_TRMD		BIT(31)		/* Transfer Mode (1 = Master mode) */
#define SIMDR1_SYNCMD		GENMASK(29, 28)	/* SYNC Mode */
#define SIMDR1_SYNCMD_PULSE	0U		/*   Frame start sync pulse */
#define SIMDR1_SYNCMD_SPI	2U		/*   Level mode/SPI */
#define SIMDR1_SYNCMD_LR	3U		/*   L/R mode */
#define SIMDR1_SYNCAC		BIT(25)		/* Sync Polarity (1 = Active-low) */
#define SIMDR1_BITLSB		BIT(24)		/* MSB/LSB First (1 = LSB first) */
#define SIMDR1_DTDL		GENMASK(22, 20)	/* Data Pin Bit Delay for MSIOF_SYNC */
#define SIMDR1_SYNCDL		GENMASK(18, 16)	/* Frame Sync Signal Timing Delay */
#define SIMDR1_FLD		GENMASK(3, 2)	/* Frame Sync Signal Interval (0-3) */
#define SIMDR1_XXSTP		BIT(0)		/* Transmission/Reception Stop on FIFO */
/* SITMDR1 */
#define SITMDR1_PCON		BIT(30)		/* Transfer Signal Connection */
#define SITMDR1_SYNCCH		GENMASK(27, 26)	/* Sync Signal Channel Select */
						/* 0=MSIOF_SYNC, 1=MSIOF_SS1, 2=MSIOF_SS2 */

/* SITMDR2 and SIRMDR2 */
#define SIMDR2_GRP		GENMASK(31, 30)	/* Group Count */
#define SIMDR2_BITLEN1		GENMASK(28, 24)	/* Data Size (8-32 bits) */
#define SIMDR2_WDLEN1		GENMASK(23, 16)	/* Word Count (1-64/256 (SH, A1))) */
#define SIMDR2_GRPMASK		GENMASK(3, 0)	/* Group Output Mask 1-4 (SH, A1) */

/* SITMDR3 and SIRMDR3 */
#define SIMDR3_BITLEN2		GENMASK(28, 24)	/* Data Size (8-32 bits) */
#define SIMDR3_WDLEN2		GENMASK(23, 16)	/* Word Count (1-64/256 (SH, A1))) */

/* SITSCR and SIRSCR */
#define SISCR_BRPS		GENMASK(12, 8)	/* Prescaler Setting (1-32) */
#define SISCR_BRDV		GENMASK(2, 0)	/* Baud Rate Generator's Division Ratio */

/* SICTR */
#define SICTR_TSCKIZ		GENMASK(31, 30)	/* Transmit Clock I/O Polarity Select */
#define SICTR_TSCKIZ_SCK	BIT(31)		/*   Disable SCK when TX disabled */
#define SICTR_TSCKIZ_POL	BIT(30)		/*   Transmit Clock Polarity */
#define SICTR_RSCKIZ		GENMASK(29, 28) /* Receive Clock Polarity Select */
#define SICTR_RSCKIZ_SCK	BIT(29)		/*   Must match CTR_TSCKIZ_SCK */
#define SICTR_RSCKIZ_POL	BIT(28)		/*   Receive Clock Polarity */
#define SICTR_TEDG		BIT(27)		/* Transmit Timing (1 = falling edge) */
#define SICTR_REDG		BIT(26)		/* Receive Timing (1 = falling edge) */
#define SICTR_TXDIZ		GENMASK(23, 22)	/* Pin Output When TX is Disabled */
#define SICTR_TXDIZ_LOW		0U		/*   0 */
#define SICTR_TXDIZ_HIGH	1U		/*   1 */
#define SICTR_TXDIZ_HIZ		2U		/*   High-impedance */
#define SICTR_TSCKE		BIT(15)		/* Transmit Serial Clock Output Enable */
#define SICTR_TFSE		BIT(14)		/* Transmit Frame Sync Signal Output Enable */
#define SICTR_TXE		BIT(9)		/* Transmit Enable */
#define SICTR_RXE		BIT(8)		/* Receive Enable */
#define SICTR_TXRST		BIT(1)		/* Transmit Reset */
#define SICTR_RXRST		BIT(0)		/* Receive Reset */

/* SIFCTR */
#define SIFCTR_TFWM		GENMASK(31, 29)	/* Transmit FIFO Watermark */
#define SIFCTR_TFWM_64		0U		/*  Transfer Request when 64 empty stages */
#define SIFCTR_TFWM_32		1U		/*  Transfer Request when 32 empty stages */
#define SIFCTR_TFWM_24		2U		/*  Transfer Request when 24 empty stages */
#define SIFCTR_TFWM_16		3U		/*  Transfer Request when 16 empty stages */
#define SIFCTR_TFWM_12		4U		/*  Transfer Request when 12 empty stages */
#define SIFCTR_TFWM_8		5U		/*  Transfer Request when 8 empty stages */
#define SIFCTR_TFWM_4		6U		/*  Transfer Request when 4 empty stages */
#define SIFCTR_TFWM_1		7U		/*  Transfer Request when 1 empty stage */
#define SIFCTR_TFUA		GENMASK(28, 20) /* Transmit FIFO Usable Area */
#define SIFCTR_RFWM		GENMASK(15, 13)	/* Receive FIFO Watermark */
#define SIFCTR_RFWM_1		0U		/*  Transfer Request when 1 valid stages */
#define SIFCTR_RFWM_4		1U		/*  Transfer Request when 4 valid stages */
#define SIFCTR_RFWM_8		2U		/*  Transfer Request when 8 valid stages */
#define SIFCTR_RFWM_16		3U		/*  Transfer Request when 16 valid stages */
#define SIFCTR_RFWM_32		4U		/*  Transfer Request when 32 valid stages */
#define SIFCTR_RFWM_64		5U		/*  Transfer Request when 64 valid stages */
#define SIFCTR_RFWM_128		6U		/*  Transfer Request when 128 valid stages */
#define SIFCTR_RFWM_256		7U		/*  Transfer Request when 256 valid stages */
#define SIFCTR_RFUA		GENMASK(12, 4)	/* Receive FIFO Usable Area (0x40 = full) */

/* SISTR */
#define SISTR_TFEMP		BIT(29) /* Transmit FIFO Empty */
#define SISTR_TDREQ		BIT(28) /* Transmit Data Transfer Request */
#define SISTR_TEOF		BIT(23) /* Frame Transmission End */
#define SISTR_TFSERR		BIT(21) /* Transmit Frame Synchronization Error */
#define SISTR_TFOVF		BIT(20) /* Transmit FIFO Overflow */
#define SISTR_TFUDF		BIT(19) /* Transmit FIFO Underflow */
#define SISTR_RFFUL		BIT(13) /* Receive FIFO Full */
#define SISTR_RDREQ		BIT(12) /* Receive Data Transfer Request */
#define SISTR_REOF		BIT(7)  /* Frame Reception End */
#define SISTR_RFSERR		BIT(5)  /* Receive Frame Synchronization Error */
#define SISTR_RFUDF		BIT(4)  /* Receive FIFO Underflow */
#define SISTR_RFOVF		BIT(3)  /* Receive FIFO Overflow */

/* SIIER */
#define SIIER_TDMAE		BIT(31) /* Transmit Data DMA Transfer Req. Enable */
#define SIIER_TFEMPE		BIT(29) /* Transmit FIFO Empty Enable */
#define SIIER_TDREQE		BIT(28) /* Transmit Data Transfer Request Enable */
#define SIIER_TEOFE		BIT(23) /* Frame Transmission End Enable */
#define SIIER_TFSERRE		BIT(21) /* Transmit Frame Sync Error Enable */
#define SIIER_TFOVFE		BIT(20) /* Transmit FIFO Overflow Enable */
#define SIIER_TFUDFE		BIT(19) /* Transmit FIFO Underflow Enable */
#define SIIER_RDMAE		BIT(15) /* Receive Data DMA Transfer Req. Enable */
#define SIIER_RFFULE		BIT(13) /* Receive FIFO Full Enable */
#define SIIER_RDREQE		BIT(12) /* Receive Data Transfer Request Enable */
#define SIIER_REOFE		BIT(7)  /* Frame Reception End Enable */
#define SIIER_RFSERRE		BIT(5)  /* Receive Frame Sync Error Enable */
#define SIIER_RFUDFE		BIT(4)  /* Receive FIFO Underflow Enable */
#define SIIER_RFOVFE		BIT(3)  /* Receive FIFO Overflow Enable */

enum {
	MSIOF_SPI_HOST,
	MSIOF_SPI_TARGET,
};

struct sh_msiof_spi_info {
	int tx_fifo_override;
	int rx_fifo_override;
	u16 num_chipselect;
	int mode;
	unsigned int dma_tx_id;
	unsigned int dma_rx_id;
	u32 dtdl;
	u32 syncdl;
};

#endif /* __SPI_SH_MSIOF_H__ */
