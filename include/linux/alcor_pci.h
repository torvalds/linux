/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Oleksij Rempel <linux@rempel-privat.de>
 *
 * Driver for Alcor Micro AU6601 and AU6621 controllers
 */

#ifndef __ALCOR_PCI_H
#define __ALCOR_PCI_H

#define ALCOR_SD_CARD 0
#define ALCOR_MS_CARD 1

#define DRV_NAME_ALCOR_PCI			"alcor_pci"
#define DRV_NAME_ALCOR_PCI_SDMMC		"alcor_sdmmc"
#define DRV_NAME_ALCOR_PCI_MS			"alcor_ms"

#define PCI_ID_ALCOR_MICRO			0x1AEA
#define PCI_ID_AU6601				0x6601
#define PCI_ID_AU6621				0x6621
#define PCI_ID_AU6625				0x6625

#define MHZ_TO_HZ(freq)				((freq) * 1000 * 1000)

#define AU6601_BASE_CLOCK			31000000
#define AU6601_MIN_CLOCK			150000
#define AU6601_MAX_CLOCK			208000000
#define AU6601_MAX_DMA_SEGMENTS			64
#define AU6601_MAX_PIO_SEGMENTS			1
#define AU6601_MAX_DMA_BLOCK_SIZE		0x1000
#define AU6601_MAX_PIO_BLOCK_SIZE		0x200
#define AU6601_MAX_DMA_BLOCKS			1
#define AU6601_DMA_LOCAL_SEGMENTS		1

/* registers spotter by reverse engineering but still
 * with unknown functionality:
 * 0x10 - ADMA phy address. AU6621 only?
 * 0x51 - LED ctrl?
 * 0x52 - unknown
 * 0x61 - LED related? Always toggled BIT0
 * 0x63 - Same as 0x61?
 * 0x77 - unknown
 */

/* SDMA phy address. Higher then 0x0800.0000?
 * The au6601 and au6621 have different DMA engines with different issues. One
 * For example au6621 engine is triggered by addr change. No other interaction
 * is needed. This means, if we get two buffers with same address, then engine
 * will stall.
 */
#define AU6601_REG_SDMA_ADDR			0x00
#define AU6601_SDMA_MASK			0xffffffff

#define AU6601_DMA_BOUNDARY			0x05
#define AU6621_DMA_PAGE_CNT			0x05
/* PIO */
#define AU6601_REG_BUFFER			0x08
/* ADMA ctrl? AU6621 only. */
#define AU6621_DMA_CTRL				0x0c
#define AU6621_DMA_ENABLE			BIT(0)
/* CMD index */
#define AU6601_REG_CMD_OPCODE			0x23
/* CMD parametr */
#define AU6601_REG_CMD_ARG			0x24
/* CMD response 4x4 Bytes */
#define AU6601_REG_CMD_RSP0			0x30
#define AU6601_REG_CMD_RSP1			0x34
#define AU6601_REG_CMD_RSP2			0x38
#define AU6601_REG_CMD_RSP3			0x3C
/* default timeout set to 125: 125 * 40ms = 5 sec
 * how exactly it is calculated?
 */
#define AU6601_TIME_OUT_CTRL			0x69
/* Block size for SDMA or PIO */
#define AU6601_REG_BLOCK_SIZE			0x6c
/* Some power related reg, used together with AU6601_OUTPUT_ENABLE */
#define AU6601_POWER_CONTROL			0x70

/* PLL ctrl */
#define AU6601_CLK_SELECT			0x72
#define	AU6601_CLK_OVER_CLK			0x80
#define	AU6601_CLK_384_MHZ			0x30
#define	AU6601_CLK_125_MHZ			0x20
#define	AU6601_CLK_48_MHZ			0x10
#define	AU6601_CLK_EXT_PLL			0x04
#define AU6601_CLK_X2_MODE			0x02
#define AU6601_CLK_ENABLE			0x01
#define AU6601_CLK_31_25_MHZ			0x00

#define AU6601_CLK_DIVIDER			0x73

#define AU6601_INTERFACE_MODE_CTRL		0x74
#define AU6601_DLINK_MODE			0x80
#define	AU6601_INTERRUPT_DELAY_TIME		0x40
#define	AU6601_SIGNAL_REQ_CTRL			0x30
#define AU6601_MS_CARD_WP			BIT(3)
#define AU6601_SD_CARD_WP			BIT(0)

/* same register values are used for:
 *  - AU6601_OUTPUT_ENABLE
 *  - AU6601_POWER_CONTROL
 */
#define AU6601_ACTIVE_CTRL			0x75
#define AU6601_XD_CARD				BIT(4)
/* AU6601_MS_CARD_ACTIVE - will cativate MS card section? */
#define AU6601_MS_CARD				BIT(3)
#define AU6601_SD_CARD				BIT(0)

/* card slot state. It should automatically detect type of
 * the card
 */
#define AU6601_DETECT_STATUS			0x76
#define AU6601_DETECT_EN			BIT(7)
#define AU6601_MS_DETECTED			BIT(3)
#define AU6601_SD_DETECTED			BIT(0)
#define AU6601_DETECT_STATUS_M			0xf

#define AU6601_REG_SW_RESET			0x79
#define AU6601_BUF_CTRL_RESET			BIT(7)
#define AU6601_RESET_DATA			BIT(3)
#define AU6601_RESET_CMD			BIT(0)

#define AU6601_OUTPUT_ENABLE			0x7a

#define AU6601_PAD_DRIVE0			0x7b
#define AU6601_PAD_DRIVE1			0x7c
#define AU6601_PAD_DRIVE2			0x7d
/* read EEPROM? */
#define AU6601_FUNCTION				0x7f

#define AU6601_CMD_XFER_CTRL			0x81
#define	AU6601_CMD_17_BYTE_CRC			0xc0
#define	AU6601_CMD_6_BYTE_WO_CRC		0x80
#define	AU6601_CMD_6_BYTE_CRC			0x40
#define	AU6601_CMD_START_XFER			0x20
#define	AU6601_CMD_STOP_WAIT_RDY		0x10
#define	AU6601_CMD_NO_RESP			0x00

#define AU6601_REG_BUS_CTRL			0x82
#define AU6601_BUS_WIDTH_4BIT			0x20
#define AU6601_BUS_WIDTH_8BIT			0x10
#define AU6601_BUS_WIDTH_1BIT			0x00

#define AU6601_DATA_XFER_CTRL			0x83
#define AU6601_DATA_WRITE			BIT(7)
#define AU6601_DATA_DMA_MODE			BIT(6)
#define AU6601_DATA_START_XFER			BIT(0)

#define AU6601_DATA_PIN_STATE			0x84
#define AU6601_BUS_STAT_CMD			BIT(15)
/* BIT(4) - BIT(7) are permanently 1.
 * May be reserved or not attached DAT4-DAT7
 */
#define AU6601_BUS_STAT_DAT3			BIT(3)
#define AU6601_BUS_STAT_DAT2			BIT(2)
#define AU6601_BUS_STAT_DAT1			BIT(1)
#define AU6601_BUS_STAT_DAT0			BIT(0)
#define AU6601_BUS_STAT_DAT_MASK		0xf

#define AU6601_OPT				0x85
#define	AU6601_OPT_CMD_LINE_LEVEL		0x80
#define	AU6601_OPT_NCRC_16_CLK			BIT(4)
#define	AU6601_OPT_CMD_NWT			BIT(3)
#define	AU6601_OPT_STOP_CLK			BIT(2)
#define	AU6601_OPT_DDR_MODE			BIT(1)
#define	AU6601_OPT_SD_18V			BIT(0)

#define AU6601_CLK_DELAY			0x86
#define	AU6601_CLK_DATA_POSITIVE_EDGE		0x80
#define	AU6601_CLK_CMD_POSITIVE_EDGE		0x40
#define	AU6601_CLK_POSITIVE_EDGE_ALL		(AU6601_CLK_CMD_POSITIVE_EDGE \
						| AU6601_CLK_DATA_POSITIVE_EDGE)


#define AU6601_REG_INT_STATUS			0x90
#define AU6601_REG_INT_ENABLE			0x94
#define AU6601_INT_DATA_END_BIT_ERR		BIT(22)
#define AU6601_INT_DATA_CRC_ERR			BIT(21)
#define AU6601_INT_DATA_TIMEOUT_ERR		BIT(20)
#define AU6601_INT_CMD_INDEX_ERR		BIT(19)
#define AU6601_INT_CMD_END_BIT_ERR		BIT(18)
#define AU6601_INT_CMD_CRC_ERR			BIT(17)
#define AU6601_INT_CMD_TIMEOUT_ERR		BIT(16)
#define AU6601_INT_ERROR			BIT(15)
#define AU6601_INT_OVER_CURRENT_ERR		BIT(8)
#define AU6601_INT_CARD_INSERT			BIT(7)
#define AU6601_INT_CARD_REMOVE			BIT(6)
#define AU6601_INT_READ_BUF_RDY			BIT(5)
#define AU6601_INT_WRITE_BUF_RDY		BIT(4)
#define AU6601_INT_DMA_END			BIT(3)
#define AU6601_INT_DATA_END			BIT(1)
#define AU6601_INT_CMD_END			BIT(0)

#define AU6601_INT_NORMAL_MASK			0x00007FFF
#define AU6601_INT_ERROR_MASK			0xFFFF8000

#define AU6601_INT_CMD_MASK	(AU6601_INT_CMD_END | \
		AU6601_INT_CMD_TIMEOUT_ERR | AU6601_INT_CMD_CRC_ERR | \
		AU6601_INT_CMD_END_BIT_ERR | AU6601_INT_CMD_INDEX_ERR)
#define AU6601_INT_DATA_MASK	(AU6601_INT_DATA_END | AU6601_INT_DMA_END | \
		AU6601_INT_READ_BUF_RDY | AU6601_INT_WRITE_BUF_RDY | \
		AU6601_INT_DATA_TIMEOUT_ERR | AU6601_INT_DATA_CRC_ERR | \
		AU6601_INT_DATA_END_BIT_ERR)
#define AU6601_INT_ALL_MASK			((u32)-1)

/* MS_CARD mode registers */

#define AU6601_MS_STATUS			0xa0

#define AU6601_MS_BUS_MODE_CTRL			0xa1
#define AU6601_MS_BUS_8BIT_MODE			0x03
#define AU6601_MS_BUS_4BIT_MODE			0x01
#define AU6601_MS_BUS_1BIT_MODE			0x00

#define AU6601_MS_TPC_CMD			0xa2
#define AU6601_MS_TPC_READ_PAGE_DATA		0x02
#define AU6601_MS_TPC_READ_REG			0x04
#define AU6601_MS_TPC_GET_INT			0x07
#define AU6601_MS_TPC_WRITE_PAGE_DATA		0x0D
#define AU6601_MS_TPC_WRITE_REG			0x0B
#define AU6601_MS_TPC_SET_RW_REG_ADRS		0x08
#define AU6601_MS_TPC_SET_CMD			0x0E
#define AU6601_MS_TPC_EX_SET_CMD		0x09
#define AU6601_MS_TPC_READ_SHORT_DATA		0x03
#define AU6601_MS_TPC_WRITE_SHORT_DATA		0x0C

#define AU6601_MS_TRANSFER_MODE			0xa3
#define	AU6601_MS_XFER_INT_TIMEOUT_CHK		BIT(2)
#define	AU6601_MS_XFER_DMA_ENABLE		BIT(1)
#define	AU6601_MS_XFER_START			BIT(0)

#define AU6601_MS_DATA_PIN_STATE		0xa4

#define AU6601_MS_INT_STATUS			0xb0
#define AU6601_MS_INT_ENABLE			0xb4
#define AU6601_MS_INT_OVER_CURRENT_ERROR	BIT(23)
#define AU6601_MS_INT_DATA_CRC_ERROR		BIT(21)
#define AU6601_MS_INT_INT_TIMEOUT		BIT(20)
#define AU6601_MS_INT_INT_RESP_ERROR		BIT(19)
#define AU6601_MS_INT_CED_ERROR			BIT(18)
#define AU6601_MS_INT_TPC_TIMEOUT		BIT(16)
#define AU6601_MS_INT_ERROR			BIT(15)
#define AU6601_MS_INT_CARD_INSERT		BIT(7)
#define AU6601_MS_INT_CARD_REMOVE		BIT(6)
#define AU6601_MS_INT_BUF_READ_RDY		BIT(5)
#define AU6601_MS_INT_BUF_WRITE_RDY		BIT(4)
#define AU6601_MS_INT_DMA_END			BIT(3)
#define AU6601_MS_INT_TPC_END			BIT(1)

#define AU6601_MS_INT_DATA_MASK			0x00000038
#define AU6601_MS_INT_TPC_MASK			0x003d8002
#define AU6601_MS_INT_TPC_ERROR			0x003d0000

#define ALCOR_PCIE_LINK_CTRL_OFFSET		0x10
#define ALCOR_PCIE_LINK_CAP_OFFSET		0x0c
#define ALCOR_CAP_START_OFFSET			0x34

struct alcor_dev_cfg {
	u8	dma;
};

struct alcor_pci_priv {
	struct pci_dev *pdev;
	struct pci_dev *parent_pdev;
	struct  device *dev;
	void __iomem *iobase;
	unsigned int irq;

	unsigned long id; /* idr id */

	struct alcor_dev_cfg	*cfg;
};

void alcor_write8(struct alcor_pci_priv *priv, u8 val, unsigned int addr);
void alcor_write16(struct alcor_pci_priv *priv, u16 val, unsigned int addr);
void alcor_write32(struct alcor_pci_priv *priv, u32 val, unsigned int addr);
void alcor_write32be(struct alcor_pci_priv *priv, u32 val, unsigned int addr);
u8 alcor_read8(struct alcor_pci_priv *priv, unsigned int addr);
u32 alcor_read32(struct alcor_pci_priv *priv, unsigned int addr);
u32 alcor_read32be(struct alcor_pci_priv *priv, unsigned int addr);
#endif
