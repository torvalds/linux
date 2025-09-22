/*	$OpenBSD: rtsxreg.h,v 1.5 2020/08/24 15:06:10 kettenis Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _RTSXREG_H_
#define _RTSXREG_H_

/* Host command buffer control register. */
#define	RTSX_HCBAR		0x00
#define	RTSX_HCBCTLR		0x04
#define	RTSX_START_CMD		(1U << 31)
#define	RTSX_HW_AUTO_RSP	(1U << 30)
#define	RTSX_STOP_CMD		(1U << 28)

/* Host data buffer control register. */
#define	RTSX_HDBAR		0x08
#define	RTSX_HDBCTLR		0x0C
#define	RTSX_TRIG_DMA		(1U << 31)
#define	RTSX_DMA_READ		(1U << 29)
#define	RTSX_STOP_DMA		(1U << 28)
#define	RTSX_ADMA_MODE		(2U << 26)

/* Interrupt pending register. */
#define	RTSX_BIPR		0x14
#define	RTSX_CMD_DONE_INT	(1U << 31)
#define	RTSX_DATA_DONE_INT	(1U << 30)
#define	RTSX_TRANS_OK_INT	(1U << 29)
#define	RTSX_TRANS_FAIL_INT	(1U << 28)
#define	RTSX_XD_INT		(1U << 27)
#define	RTSX_MS_INT		(1U << 26)
#define	RTSX_SD_INT		(1U << 25)
#define	RTSX_SD_WRITE_PROTECT	(1U << 19)
#define	RTSX_XD_EXIST		(1U << 18)
#define	RTSX_MS_EXIST		(1U << 17)
#define	RTSX_SD_EXIST		(1U << 16)
#define	RTSX_CARD_EXIST		(RTSX_XD_EXIST|RTSX_MS_EXIST|RTSX_SD_EXIST)
#define	RTSX_CARD_INT		(RTSX_XD_INT|RTSX_MS_INT|RTSX_SD_INT)

/* Chip register access. */
#define	RTSX_HAIMR		0x10
#define	RTSX_HAIMR_WRITE	0x40000000
#define	RTSX_HAIMR_BUSY		0x80000000

/* Interrupt enable register. */
#define	RTSX_BIER		0x18
#define	RTSX_CMD_DONE_INT_EN	(1U << 31)
#define	RTSX_DATA_DONE_INT_EN	(1U << 30)
#define	RTSX_TRANS_OK_INT_EN	(1U << 29)
#define	RTSX_TRANS_FAIL_INT_EN	(1U << 28)
#define	RTSX_XD_INT_EN		(1U << 27)
#define	RTSX_MS_INT_EN		(1U << 26)
#define	RTSX_SD_INT_EN		(1U << 25)
#define	RTSX_GPIO0_INT_EN	(1U << 24)
#define	RTSX_MS_OC_INT_EN	(1U << 23)
#define	RTSX_SD_OC_INT_EN	(1U << 22)

/* Power on/off. */
#define	RTSX_FPDCTL	0xFC00
#define	RTSX_SSC_POWER_DOWN	0x01
#define	RTSX_SD_OC_POWER_DOWN	0x02
#define	RTSX_MS_OC_POWER_DOWN	0x04
#define	RTSX_ALL_POWER_DOWN	0x07
#define	RTSX_OC_POWER_DOWN	0x06

/* Card power control register. */
#define	RTSX_CARD_PWR_CTL	0xFD50
#define	RTSX_SD_PWR_ON		0x00
#define	RTSX_SD_PARTIAL_PWR_ON	0x01
#define	RTSX_SD_PWR_OFF		0x03
#define	RTSX_SD_PWR_MASK	0x03
#define	RTSX_PMOS_STRG_MASK	0x10
#define	RTSX_PMOS_STRG_400mA	0x00
#define	RTSX_PMOS_STRG_800mA	0x10

#define	RTSX_MS_PWR_OFF		0x0C
#define	RTSX_MS_PWR_ON		0x00
#define	RTSX_MS_PARTIAL_PWR_ON	0x04

#define	RTSX_CARD_SHARE_MODE	0xFD52
#define	RTSX_CARD_SHARE_48_XD	0x02
#define	RTSX_CARD_SHARE_48_SD	0x04
#define	RTSX_CARD_SHARE_48_MS	0x08
#define	RTSX_CARD_DRIVE_SEL	0xFE53

/* Card clock. */
#define	RTSX_CARD_CLK_EN	0xFD69 
#define	RTSX_XD_CLK_EN		0x02
#define	RTSX_SD_CLK_EN		0x04
#define	RTSX_MS_CLK_EN		0x08
#define	RTSX_SPI_CLK_EN		0x10
#define	RTSX_CARD_CLK_EN_ALL	\
    (RTSX_XD_CLK_EN|RTSX_SD_CLK_EN|RTSX_MS_CLK_EN|RTSX_SPI_CLK_EN)

#define RTSX_SDIO_CTRL		0xFD6B
#define RTSX_SDIO_BUS_CTRL	0x01
#define RTSX_SDIO_CD_CTRL	0x02

/* Internal clock. */
#define	RTSX_CLK_CTL		0xFC02
#define	RTSX_CLK_LOW_FREQ	0x01

/* Internal clock divisor values. */
#define	RTSX_CLK_DIV		0xFC03
#define	RTSX_CLK_DIV_1		0x01
#define	RTSX_CLK_DIV_2		0x02
#define	RTSX_CLK_DIV_4		0x03
#define	RTSX_CLK_DIV_8		0x04

/* Internal clock selection. */
#define	RTSX_CLK_SEL	0xFC04
#define	RTSX_SSC_80	0
#define	RTSX_SSC_100	1
#define	RTSX_SSC_120	2
#define	RTSX_SSC_150	3
#define	RTSX_SSC_200	4

#define	RTSX_SSC_DIV_N_0	0xFC0F

#define	RTSX_SSC_CTL1	0xFC11
#define	RTSX_RSTB		0x80
#define	RTSX_SSC_8X_EN		0x40
#define	RTSX_SSC_FIX_FRAC	0x20
#define	RTSX_SSC_SEL_1M		0x00
#define	RTSX_SSC_SEL_2M		0x08
#define	RTSX_SSC_SEL_2M		0x08
#define	RTSX_SSC_SEL_4M		0x10
#define	RTSX_SSC_SEL_8M		0x18
#define	RTSX_SSC_CTL2	0xFC12
#define	RTSX_SSC_DEPTH_MASK	0x07

/* RC oscillator, default is 2M */
#define	RTSX_RCCTL		0xFC14
#define	RTSX_RCCTL_F_400K	0x0
#define	RTSX_RCCTL_F_2M		0x1

/* RTS5229-only. */
#define	RTSX_OLT_LED_CTL	0xFC1E
#define	RTSX_OLT_LED_PERIOD	0x02
#define	RTSX_OLT_LED_AUTOBLINK	0x08

#define	RTSX_GPIO_CTL		0xFC1F
#define	RTSX_GPIO_LED_ON	0x02

/* Host controller commands. */
#define	RTSX_READ_REG_CMD	0
#define	RTSX_WRITE_REG_CMD	1
#define	RTSX_CHECK_REG_CMD	2


#define	RTSX_OCPCTL	0xFC15
#define	RTSX_OCPSTAT	0xFC16
#define	RTSX_OCPGLITCH	0xFC17
#define	RTSX_OCPPARA1	0xFC18
#define	RTSX_OCPPARA2	0xFC19

/* FPGA */
#define	RTSX_FPGA_PULL_CTL	0xFC1D
#define	RTSX_FPGA_MS_PULL_CTL_BIT	0x10
#define	RTSX_FPGA_SD_PULL_CTL_BIT	0x08

/* Clock source configuration register. */
#define	RTSX_CARD_CLK_SOURCE	0xFC2E
#define	RTSX_CRC_FIX_CLK	(0x00 << 0)
#define	RTSX_CRC_VAR_CLK0	(0x01 << 0)
#define	RTSX_CRC_VAR_CLK1	(0x02 << 0)
#define	RTSX_SD30_FIX_CLK	(0x00 << 2)
#define	RTSX_SD30_VAR_CLK0	(0x01 << 2)
#define	RTSX_SD30_VAR_CLK1	(0x02 << 2)
#define	RTSX_SAMPLE_FIX_CLK	(0x00 << 4)
#define	RTSX_SAMPLE_VAR_CLK0	(0x01 << 4)
#define	RTSX_SAMPLE_VAR_CLK1	(0x02 << 4)


/* ASIC */
#define	RTSX_CARD_PULL_CTL1	0xFD60
#define	RTSX_CARD_PULL_CTL2	0xFD61
#define	RTSX_CARD_PULL_CTL3	0xFD62

#define	RTSX_PULL_CTL_DISABLE12		0x55
#define	RTSX_PULL_CTL_DISABLE3		0xD5
#define	RTSX_PULL_CTL_DISABLE3_TYPE_C	0xE5
#define	RTSX_PULL_CTL_ENABLE12		0xAA
#define	RTSX_PULL_CTL_ENABLE3		0xE9
#define	RTSX_PULL_CTL_ENABLE3_TYPE_C	0xD9

/* SD configuration register 1 (clock divider, bus mode and width). */
#define	RTSX_SD_CFG1		0xFDA0
#define	RTSX_CLK_DIVIDE_0	0x00
#define	RTSX_CLK_DIVIDE_128	0x80
#define	RTSX_CLK_DIVIDE_256	0xC0
#define	RTSX_CLK_DIVIDE_MASK	0xC0
#define	RTSX_SD20_MODE		0x00
#define	RTSX_SDDDR_MODE		0x04
#define	RTSX_SD30_MODE		0x08
#define	RTSX_SD_MODE_MASK	0x0C
#define	RTSX_BUS_WIDTH_1	0x00
#define	RTSX_BUS_WIDTH_4	0x01
#define	RTSX_BUS_WIDTH_8	0x02
#define	RTSX_BUS_WIDTH_MASK	0x03

/* SD configuration register 2 (SD command response flags). */
#define	RTSX_SD_CFG2		0xFDA1
#define	RTSX_SD_CALCULATE_CRC7		0x00
#define	RTSX_SD_NO_CALCULATE_CRC7	0x80
#define	RTSX_SD_CHECK_CRC16		0x00
#define	RTSX_SD_NO_CHECK_CRC16		0x40
#define	RTSX_SD_NO_CHECK_WAIT_CRC_TO	0x20
#define	RTSX_SD_WAIT_BUSY_END		0x08
#define	RTSX_SD_NO_WAIT_BUSY_END	0x00
#define	RTSX_SD_CHECK_CRC7		0x00
#define	RTSX_SD_NO_CHECK_CRC7		0x04
#define	RTSX_SD_RSP_LEN_0		0x00
#define	RTSX_SD_RSP_LEN_6		0x01
#define	RTSX_SD_RSP_LEN_17		0x02
/* SD command response types. */
#define	RTSX_SD_RSP_TYPE_R0	0x04
#define	RTSX_SD_RSP_TYPE_R1	0x01
#define	RTSX_SD_RSP_TYPE_R1B	0x09
#define	RTSX_SD_RSP_TYPE_R2	0x02
#define	RTSX_SD_RSP_TYPE_R3	0x05
#define	RTSX_SD_RSP_TYPE_R4	0x05
#define	RTSX_SD_RSP_TYPE_R5	0x01
#define	RTSX_SD_RSP_TYPE_R6	0x01
#define	RTSX_SD_RSP_TYPE_R7	0x01

#define	RTSX_SD_STAT1		0xFDA3
#define	RTSX_SD_CRC7_ERR			0x80
#define	RTSX_SD_CRC16_ERR			0x40
#define	RTSX_SD_CRC_WRITE_ERR			0x20
#define	RTSX_SD_CRC_WRITE_ERR_MASK	    	0x1C
#define	RTSX_GET_CRC_TIME_OUT			0x02
#define	RTSX_SD_TUNING_COMPARE_ERR		0x01
#define	RTSX_SD_STAT2		0xFDA4
#define	RTSX_SD_RSP_80CLK_TIMEOUT	0x01

#define	RTSX_SD_CRC_ERR	(RTSX_SD_CRC7_ERR|RTSX_SD_CRC16_ERR|RTSX_SD_CRC_WRITE_ERR)

/* SD bus status register. */
#define	RTSX_SD_BUS_STAT	0xFDA5
#define	RTSX_SD_CLK_TOGGLE_EN	0x80
#define	RTSX_SD_CLK_FORCE_STOP	0x40
#define	RTSX_SD_DAT3_STATUS	0x10
#define	RTSX_SD_DAT2_STATUS	0x08
#define	RTSX_SD_DAT1_STATUS	0x04
#define	RTSX_SD_DAT0_STATUS	0x02
#define	RTSX_SD_CMD_STATUS	0x01

#define	RTSX_SD_PAD_CTL		0xFDA6
#define	RTSX_SD_IO_USING_1V8	0x80

/* Sample point control register. */
#define	RTSX_SD_SAMPLE_POINT_CTL	0xFDA7
#define	RTSX_DDR_FIX_RX_DAT                  0x00
#define	RTSX_DDR_VAR_RX_DAT                  0x80
#define	RTSX_DDR_FIX_RX_DAT_EDGE             0x00
#define	RTSX_DDR_FIX_RX_DAT_14_DELAY         0x40
#define	RTSX_DDR_FIX_RX_CMD                  0x00
#define	RTSX_DDR_VAR_RX_CMD                  0x20
#define	RTSX_DDR_FIX_RX_CMD_POS_EDGE         0x00
#define	RTSX_DDR_FIX_RX_CMD_14_DELAY         0x10
#define	RTSX_SD20_RX_POS_EDGE                0x00
#define	RTSX_SD20_RX_14_DELAY                0x08
#define	RTSX_SD20_RX_SEL_MASK                0x08

#define	RTSX_SD_PUSH_POINT_CTL	0xFDA8
#define	RTSX_SD20_TX_NEG_EDGE	0x00

#define	RTSX_SD_CMD0		0xFDA9
#define	RTSX_SD_CMD1		0xFDAA
#define	RTSX_SD_CMD2		0xFDAB
#define	RTSX_SD_CMD3		0xFDAC
#define	RTSX_SD_CMD4		0xFDAD
#define	RTSX_SD_CMD5		0xFDAE
#define	RTSX_SD_BYTE_CNT_L	0xFDAF
#define	RTSX_SD_BYTE_CNT_H	0xFDB0
#define	RTSX_SD_BLOCK_CNT_L	0xFDB1
#define	RTSX_SD_BLOCK_CNT_H	0xFDB2

/*
 * Transfer modes.
 */
#define	RTSX_SD_TRANSFER	0xFDB3

/* Write one or two bytes from SD_CMD2 and SD_CMD3 to the card. */
#define	RTSX_TM_NORMAL_WRITE	0x00

/* Write (SD_BYTE_CNT * SD_BLOCK_COUNTS) bytes from ring buffer to card. */
#define	RTSX_TM_AUTO_WRITE3	0x01

/* Like AUTO_WRITE3, plus automatically send CMD 12 when done.
 * The response to CMD 12 is written to SD_CMD{0,1,2,3,4}. */ 
#define	RTSX_TM_AUTO_WRITE4	0x02

/* Read (SD_BYTE_CNT * SD_BLOCK_CNT) bytes from card into ring buffer. */
#define	RTSX_TM_AUTO_READ3	0x05

/* Like AUTO_READ3, plus automatically send CMD 12 when done.
 * The response to CMD 12 is written to SD_CMD{0,1,2,3,4}. */ 
#define	RTSX_TM_AUTO_READ4	0x06

/* Send an SD command described in SD_CMD{0,1,2,3,4} to the card and put
 * the response into SD_CMD{0,1,2,3,4}. Long responses (17 byte) are put
 * into ping-pong buffer 2 instead. */
#define	RTSX_TM_CMD_RSP		0x08

/* Send write command, get response from the card, write data from ring
 * buffer to card, and send CMD 12 when done.
 * The response to CMD 12 is written to SD_CMD{0,1,2,3,4}. */ 
#define	RTSX_TM_AUTO_WRITE1	0x09

/* Like AUTO_WRITE1 except no CMD 12 is sent. */
#define	RTSX_TM_AUTO_WRITE2	0x0A

/* Send read command, read up to 512 bytes (SD_BYTE_CNT * SD_BLOCK_CNT)
 * from the card into the ring buffer or ping-pong buffer 2. */
#define	RTSX_TM_NORMAL_READ	0x0C

/* Same as WRITE1, except data is read from the card to the ring buffer. */
#define	RTSX_TM_AUTO_READ1	0x0D

/* Same as WRITE2, except data is read from the card to the ring buffer. */
#define	RTSX_TM_AUTO_READ2	0x0E

/* Send CMD 19 and receive response and tuning pattern from card and
 * report the result. */
#define	RTSX_TM_AUTO_TUNING	0x0F

/* transfer control */
#define	RTSX_SD_TRANSFER_START	0x80
#define	RTSX_SD_TRANSFER_END	0x40
#define	RTSX_SD_STAT_IDLE	0x20
#define	RTSX_SD_TRANSFER_ERR	0x10

#define	RTSX_SD_CMD_STATE	0xFDB5
#define	RTSX_SD_DATA_STATE	0xFDB6

#define	RTSX_CARD_STOP		0xFD54
#define	RTSX_SPI_STOP		0x01
#define	RTSX_XD_STOP		0x02
#define	RTSX_SD_STOP		0x04
#define	RTSX_MS_STOP		0x08
#define	RTSX_SPI_CLR_ERR	0x10
#define	RTSX_XD_CLR_ERR		0x20
#define	RTSX_SD_CLR_ERR		0x40
#define	RTSX_MS_CLR_ERR		0x80
#define	RTSX_ALL_STOP		0x0F
#define	RTSX_ALL_CLR_ERR	0xF0

#define	RTSX_CARD_OE		0xFD55
#define	RTSX_XD_OUTPUT_EN	0x02
#define	RTSX_SD_OUTPUT_EN	0x04
#define	RTSX_MS_OUTPUT_EN	0x08
#define	RTSX_SPI_OUTPUT_EN	0x10
#define	RTSX_CARD_OUTPUT_EN	(RTSX_XD_OUTPUT_EN|RTSX_SD_OUTPUT_EN|\
				RTSX_MS_OUTPUT_EN)

#define	RTSX_CARD_DATA_SOURCE	0xFD5B
#define	RTSX_RING_BUFFER	0x00
#define	RTSX_PINGPONG_BUFFER	0x01
#define	RTSX_CARD_SELECT	0xFD5C
#define	RTSX_XD_MOD_SEL		0x01
#define	RTSX_SD_MOD_SEL		0x02
#define	RTSX_MS_MOD_SEL		0x03
#define	RTSX_SPI_MOD_SEL	0x04

#define	RTSX_CARD_GPIO_DIR	0xFD57
#define	RTSX_CARD_GPIO		0xFD58
#define	RTSX_CARD_GPIO_LED_OFF	0x01

/* ping-pong buffer 2 */
#define	RTSX_PPBUF_BASE2	0xFA00
#define	RTSX_PPBUF_SIZE		256

#define	RTSX_SUPPORT_VOLTAGE	(MMC_OCR_3_3V_3_4V|MMC_OCR_3_2V_3_3V|\
				MMC_OCR_3_1V_3_2V|MMC_OCR_3_0V_3_1V)

#define	RTSX_CFG_PCI		0x1C
#define	RTSX_CFG_ASIC		0x10

#define	RTSX_IRQEN0		0xFE20
#define	RTSX_LINK_DOWN_INT_EN	0x10
#define	RTSX_LINK_READY_INT_EN	0x20
#define	RTSX_SUSPEND_INT_EN	0x40
#define	RTSX_DMA_DONE_INT_EN	0x80
#define	RTSX_IRQSTAT0		0xFE21
#define	RTSX_LINK_DOWN_INT	0x10
#define	RTSX_LINK_READY_INT	0x20
#define	RTSX_SUSPEND_INT	0x40
#define	RTSX_DMA_DONE_INT	0x80

#define	RTSX_DMATC0		0xFE28
#define	RTSX_DMATC1		0xFE29
#define	RTSX_DMATC2		0xFE2A
#define	RTSX_DMATC3		0xFE2B

#define	RTSX_DMACTL		0xFE2C
#define	RTSX_DMA_DIR_TO_CARD	0x00
#define	RTSX_DMA_EN		0x01
#define	RTSX_DMA_DIR_FROM_CARD	0x02
#define	RTSX_DMA_BUSY		0x04
#define	RTSX_DMA_RST		0x80
#define	RTSX_DMA_128		(0 << 4)
#define	RTSX_DMA_256		(1 << 4)
#define	RTSX_DMA_512		(2 << 4)
#define	RTSX_DMA_1024		(3 << 4)
#define	RTSX_DMA_PACK_SIZE_MASK	0x30

#define	RTSX_RBCTL		0xFE34
#define	RTSX_RB_FLUSH		0x80

#define	RTSX_CFGADDR0		0xFE35
#define	RTSX_CFGADDR1		0xFE36
#define	RTSX_CFGDATA0		0xFE37
#define	RTSX_CFGDATA1		0xFE38
#define	RTSX_CFGDATA2		0xFE39
#define	RTSX_CFGDATA3		0xFE3A
#define	RTSX_CFGRWCTL		0xFE3B
#define	RTSX_CFG_WRITE_DATA0	0x01
#define	RTSX_CFG_WRITE_DATA1	0x02
#define	RTSX_CFG_WRITE_DATA2	0x04
#define	RTSX_CFG_WRITE_DATA3	0x08
#define	RTSX_CFG_BUSY		0x80

#define	RTSX_SDIOCFG_REG	0x724
#define	RTSX_SDIOCFG_NO_BYPASS_SDIO	0x02
#define	RTSX_SDIOCFG_HAVE_SDIO		0x04
#define	RTSX_SDIOCFG_SINGLE_LUN		0x08
#define	RTSX_SDIOCFG_SDIO_ONLY		0x80

#define	RTSX_HOST_SLEEP_STATE	0xFE60
#define	RTSX_HOST_ENTER_S1	0x01
#define	RTSX_HOST_ENTER_S3	0x02

#define	RTSX_SDIO_CFG		0xFE70
#define	RTSX_SDIO_BUS_AUTO_SWITCH	0x10

#define	RTSX_NFTS_TX_CTRL	0xFE72
#define	RTSX_INT_READ_CLR	0x02

#define	RTSX_PWR_GATE_CTRL	0xFE75
#define	RTSX_PWR_GATE_EN	0x01
#define	RTSX_LDO3318_ON		0x00
#define	RTSX_LDO3318_SUSPEND	0x04
#define	RTSX_LDO3318_OFF	0x06
#define	RTSX_LDO3318_VCC1	0x02
#define	RTSX_LDO3318_VCC2	0x04
#define	RTSX_PWD_SUSPEND_EN	0xFE76
#define	RTSX_LDO_PWR_SEL	0xFE78
#define	RTSX_LDO_PWR_SEL_3V3	0x01
#define	RTSX_LDO_PWR_SEL_DV33	0x03

#define	RTSX_PHY_RWCTL		0xFE3C
#define	RTSX_PHY_READ		0x00
#define	RTSX_PHY_WRITE		0x01
#define	RTSX_PHY_BUSY		0x80
#define	RTSX_PHY_DATA0		0xFE3D
#define	RTSX_PHY_DATA1		0xFE3E
#define	RTSX_PHY_ADDR		0xFE3F

#define	RTSX_PHY_VOLTAGE	0x08
#define	RTSX_PHY_VOLTAGE_MASK	0x3F

#define	RTSX_PETXCFG		0xFE49
#define	RTSX_PETXCFG_CLKREQ_PIN	0x08

#define	RTSX_CARD_AUTO_BLINK	0xFD56
#define	RTSX_LED_BLINK_EN	0x08
#define	RTSX_LED_BLINK_SPEED	0x05

#define	RTSX_WAKE_SEL_CTL	0xFE54
#define	RTSX_PME_FORCE_CTL	0xFE56

#define	RTSX_CHANGE_LINK_STATE	0xFE5B
#define	RTSX_CD_RST_CORE_EN		0x01
#define	RTSX_FORCE_RST_CORE_EN		0x02
#define	RTSX_NON_STICKY_RST_N_DBG	0x08
#define	RTSX_MAC_PHY_RST_N_DBG		0x10

#define	RTSX_PERST_GLITCH_WIDTH	0xFE5C

#define	RTSX_SD30_DRIVE_SEL	0xFE5E
#define	RTSX_SD30_DRIVE_SEL_3V3		0x01
#define	RTSX_SD30_DRIVE_SEL_1V8		0x03
#define	RTSX_SD30_DRIVE_SEL_MASK	0x07

#define	RTSX_DUMMY_REG		0xFE90

#define	RTSX_LDO_VCC_CFG1	0xFF73
#define	RTSX_LDO_VCC_REF_TUNE_MASK	0x30
#define	RTSX_LDO_VCC_REF_1V2		0x20
#define	RTSX_LDO_VCC_TUNE_MASK		0x07
#define	RTSX_LDO_VCC_1V8		0x04
#define	RTSX_LDO_VCC_3V3		0x07
#define	RTSX_LDO_VCC_LMT_EN		0x08

#define	RTSX_SG_INT		0x04
#define	RTSX_SG_END		0x02
#define	RTSX_SG_VALID		0x01

#define	RTSX_SG_NO_OP		0x00
#define	RTSX_SG_TRANS_DATA	(0x02 << 4)
#define	RTSX_SG_LINK_DESC	(0x03 << 4)

#define	RTSX_IC_VERSION_A	0x00
#define	RTSX_IC_VERSION_B	0x01
#define	RTSX_IC_VERSION_C	0x02
#define	RTSX_IC_VERSION_D	0x03

#endif
