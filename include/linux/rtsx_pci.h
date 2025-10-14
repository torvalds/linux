/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 */

#ifndef __RTSX_PCI_H
#define __RTSX_PCI_H

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/rtsx_common.h>

#define MAX_RW_REG_CNT			1024

#define RTSX_HCBAR			0x00
#define RTSX_HCBCTLR			0x04
#define   STOP_CMD			(0x01 << 28)
#define   READ_REG_CMD			0
#define   WRITE_REG_CMD			1
#define   CHECK_REG_CMD			2

#define RTSX_HDBAR			0x08
#define   RTSX_SG_INT			0x04
#define   RTSX_SG_END			0x02
#define   RTSX_SG_VALID			0x01
#define   RTSX_SG_NO_OP			0x00
#define   RTSX_SG_TRANS_DATA		(0x02 << 4)
#define   RTSX_SG_LINK_DESC		(0x03 << 4)
#define RTSX_HDBCTLR			0x0C
#define   SDMA_MODE			0x00
#define   ADMA_MODE			(0x02 << 26)
#define   STOP_DMA			(0x01 << 28)
#define   TRIG_DMA			(0x01 << 31)

#define RTSX_HAIMR			0x10
#define   HAIMR_TRANS_START		(0x01 << 31)
#define   HAIMR_READ			0x00
#define   HAIMR_WRITE			(0x01 << 30)
#define   HAIMR_READ_START		(HAIMR_TRANS_START | HAIMR_READ)
#define   HAIMR_WRITE_START		(HAIMR_TRANS_START | HAIMR_WRITE)
#define   HAIMR_TRANS_END			(HAIMR_TRANS_START)

#define RTSX_BIPR			0x14
#define   CMD_DONE_INT			(1 << 31)
#define   DATA_DONE_INT			(1 << 30)
#define   TRANS_OK_INT			(1 << 29)
#define   TRANS_FAIL_INT		(1 << 28)
#define   XD_INT			(1 << 27)
#define   MS_INT			(1 << 26)
#define   SD_INT			(1 << 25)
#define   GPIO0_INT			(1 << 24)
#define   OC_INT			(1 << 23)
#define   SD_WRITE_PROTECT		(1 << 19)
#define   XD_EXIST			(1 << 18)
#define   MS_EXIST			(1 << 17)
#define   SD_EXIST			(1 << 16)
#define   DELINK_INT			GPIO0_INT
#define   MS_OC_INT			(1 << 23)
#define   SD_OVP_INT		(1 << 23)
#define   SD_OC_INT			(1 << 22)

#define CARD_INT		(XD_INT | MS_INT | SD_INT)
#define NEED_COMPLETE_INT	(DATA_DONE_INT | TRANS_OK_INT | TRANS_FAIL_INT)
#define RTSX_INT		(CMD_DONE_INT | NEED_COMPLETE_INT | \
					CARD_INT | GPIO0_INT | OC_INT)
#define CARD_EXIST		(XD_EXIST | MS_EXIST | SD_EXIST)

#define RTSX_BIER			0x18
#define   CMD_DONE_INT_EN		(1 << 31)
#define   DATA_DONE_INT_EN		(1 << 30)
#define   TRANS_OK_INT_EN		(1 << 29)
#define   TRANS_FAIL_INT_EN		(1 << 28)
#define   XD_INT_EN			(1 << 27)
#define   MS_INT_EN			(1 << 26)
#define   SD_INT_EN			(1 << 25)
#define   GPIO0_INT_EN			(1 << 24)
#define   OC_INT_EN			(1 << 23)
#define   DELINK_INT_EN			GPIO0_INT_EN
#define   MS_OC_INT_EN			(1 << 23)
#define   SD_OVP_INT_EN			(1 << 23)
#define   SD_OC_INT_EN			(1 << 22)

#define RTSX_DUM_REG			0x1C

/*
 * macros for easy use
 */
#define rtsx_pci_writel(pcr, reg, value) \
	iowrite32(value, (pcr)->remap_addr + reg)
#define rtsx_pci_readl(pcr, reg) \
	ioread32((pcr)->remap_addr + reg)
#define rtsx_pci_writew(pcr, reg, value) \
	iowrite16(value, (pcr)->remap_addr + reg)
#define rtsx_pci_readw(pcr, reg) \
	ioread16((pcr)->remap_addr + reg)
#define rtsx_pci_writeb(pcr, reg, value) \
	iowrite8(value, (pcr)->remap_addr + reg)
#define rtsx_pci_readb(pcr, reg) \
	ioread8((pcr)->remap_addr + reg)

#define STATE_TRANS_NONE		0
#define STATE_TRANS_CMD			1
#define STATE_TRANS_BUF			2
#define STATE_TRANS_SG			3

#define TRANS_NOT_READY			0
#define TRANS_RESULT_OK			1
#define TRANS_RESULT_FAIL		2
#define TRANS_NO_DEVICE			3

#define RTSX_RESV_BUF_LEN		4096
#define HOST_CMDS_BUF_LEN		1024
#define HOST_SG_TBL_BUF_LEN		(RTSX_RESV_BUF_LEN - HOST_CMDS_BUF_LEN)
#define HOST_SG_TBL_ITEMS		(HOST_SG_TBL_BUF_LEN / 8)
#define MAX_SG_ITEM_LEN			0x80000
#define HOST_TO_DEVICE			0
#define DEVICE_TO_HOST			1

#define OUTPUT_3V3			0
#define OUTPUT_1V8			1

#define RTSX_PHASE_MAX			32
#define RX_TUNING_CNT			3

#define MS_CFG				0xFD40
#define   SAMPLE_TIME_RISING		0x00
#define   SAMPLE_TIME_FALLING		0x80
#define   PUSH_TIME_DEFAULT		0x00
#define   PUSH_TIME_ODD			0x40
#define   NO_EXTEND_TOGGLE		0x00
#define   EXTEND_TOGGLE_CHK		0x20
#define   MS_BUS_WIDTH_1		0x00
#define   MS_BUS_WIDTH_4		0x10
#define   MS_BUS_WIDTH_8		0x18
#define   MS_2K_SECTOR_MODE		0x04
#define   MS_512_SECTOR_MODE		0x00
#define   MS_TOGGLE_TIMEOUT_EN		0x00
#define   MS_TOGGLE_TIMEOUT_DISEN	0x01
#define MS_NO_CHECK_INT			0x02
#define MS_TPC				0xFD41
#define MS_TRANS_CFG			0xFD42
#define   WAIT_INT			0x80
#define   NO_WAIT_INT			0x00
#define   NO_AUTO_READ_INT_REG		0x00
#define   AUTO_READ_INT_REG		0x40
#define   MS_CRC16_ERR			0x20
#define   MS_RDY_TIMEOUT		0x10
#define   MS_INT_CMDNK			0x08
#define   MS_INT_BREQ			0x04
#define   MS_INT_ERR			0x02
#define   MS_INT_CED			0x01
#define MS_TRANSFER			0xFD43
#define   MS_TRANSFER_START		0x80
#define   MS_TRANSFER_END		0x40
#define   MS_TRANSFER_ERR		0x20
#define   MS_BS_STATE			0x10
#define   MS_TM_READ_BYTES		0x00
#define   MS_TM_NORMAL_READ		0x01
#define   MS_TM_WRITE_BYTES		0x04
#define   MS_TM_NORMAL_WRITE		0x05
#define   MS_TM_AUTO_READ		0x08
#define   MS_TM_AUTO_WRITE		0x0C
#define MS_INT_REG			0xFD44
#define MS_BYTE_CNT			0xFD45
#define MS_SECTOR_CNT_L			0xFD46
#define MS_SECTOR_CNT_H			0xFD47
#define MS_DBUS_H			0xFD48

#define SD_CFG1				0xFDA0
#define   SD_CLK_DIVIDE_0		0x00
#define   SD_CLK_DIVIDE_256		0xC0
#define   SD_CLK_DIVIDE_128		0x80
#define   SD_BUS_WIDTH_1BIT		0x00
#define   SD_BUS_WIDTH_4BIT		0x01
#define   SD_BUS_WIDTH_8BIT		0x02
#define   SD_ASYNC_FIFO_NOT_RST		0x10
#define   SD_20_MODE			0x00
#define   SD_DDR_MODE			0x04
#define   SD_30_MODE			0x08
#define   SD_CLK_DIVIDE_MASK		0xC0
#define   SD_MODE_SELECT_MASK		0x0C
#define SD_CFG2				0xFDA1
#define   SD_CALCULATE_CRC7		0x00
#define   SD_NO_CALCULATE_CRC7		0x80
#define   SD_CHECK_CRC16		0x00
#define   SD_NO_CHECK_CRC16		0x40
#define   SD_NO_CHECK_WAIT_CRC_TO	0x20
#define   SD_WAIT_BUSY_END		0x08
#define   SD_NO_WAIT_BUSY_END		0x00
#define   SD_CHECK_CRC7			0x00
#define   SD_NO_CHECK_CRC7		0x04
#define   SD_RSP_LEN_0			0x00
#define   SD_RSP_LEN_6			0x01
#define   SD_RSP_LEN_17			0x02
#define   SD_RSP_TYPE_R0		0x04
#define   SD_RSP_TYPE_R1		0x01
#define   SD_RSP_TYPE_R1b		0x09
#define   SD_RSP_TYPE_R2		0x02
#define   SD_RSP_TYPE_R3		0x05
#define   SD_RSP_TYPE_R4		0x05
#define   SD_RSP_TYPE_R5		0x01
#define   SD_RSP_TYPE_R6		0x01
#define   SD_RSP_TYPE_R7		0x01
#define SD_CFG3				0xFDA2
#define   SD30_CLK_END_EN		0x10
#define   SD_RSP_80CLK_TIMEOUT_EN	0x01

#define SD_STAT1			0xFDA3
#define   SD_CRC7_ERR			0x80
#define   SD_CRC16_ERR			0x40
#define   SD_CRC_WRITE_ERR		0x20
#define   SD_CRC_WRITE_ERR_MASK		0x1C
#define   GET_CRC_TIME_OUT		0x02
#define   SD_TUNING_COMPARE_ERR		0x01
#define SD_STAT2			0xFDA4
#define   SD_RSP_80CLK_TIMEOUT		0x01

#define SD_BUS_STAT			0xFDA5
#define   SD_CLK_TOGGLE_EN		0x80
#define   SD_CLK_FORCE_STOP		0x40
#define   SD_DAT3_STATUS		0x10
#define   SD_DAT2_STATUS		0x08
#define   SD_DAT1_STATUS		0x04
#define   SD_DAT0_STATUS		0x02
#define   SD_CMD_STATUS			0x01
#define SD_PAD_CTL			0xFDA6
#define   SD_IO_USING_1V8		0x80
#define   SD_IO_USING_3V3		0x7F
#define   TYPE_A_DRIVING		0x00
#define   TYPE_B_DRIVING		0x01
#define   TYPE_C_DRIVING		0x02
#define   TYPE_D_DRIVING		0x03
#define SD_SAMPLE_POINT_CTL		0xFDA7
#define   DDR_FIX_RX_DAT		0x00
#define   DDR_VAR_RX_DAT		0x80
#define   DDR_FIX_RX_DAT_EDGE		0x00
#define   DDR_FIX_RX_DAT_14_DELAY	0x40
#define   DDR_FIX_RX_CMD		0x00
#define   DDR_VAR_RX_CMD		0x20
#define   DDR_FIX_RX_CMD_POS_EDGE	0x00
#define   DDR_FIX_RX_CMD_14_DELAY	0x10
#define   SD20_RX_POS_EDGE		0x00
#define   SD20_RX_14_DELAY		0x08
#define SD20_RX_SEL_MASK		0x08
#define SD_PUSH_POINT_CTL		0xFDA8
#define   DDR_FIX_TX_CMD_DAT		0x00
#define   DDR_VAR_TX_CMD_DAT		0x80
#define   DDR_FIX_TX_DAT_14_TSU		0x00
#define   DDR_FIX_TX_DAT_12_TSU		0x40
#define   DDR_FIX_TX_CMD_NEG_EDGE	0x00
#define   DDR_FIX_TX_CMD_14_AHEAD	0x20
#define   SD20_TX_NEG_EDGE		0x00
#define   SD20_TX_14_AHEAD		0x10
#define   SD20_TX_SEL_MASK		0x10
#define   DDR_VAR_SDCLK_POL_SWAP	0x01
#define SD_CMD0				0xFDA9
#define   SD_CMD_START			0x40
#define SD_CMD1				0xFDAA
#define SD_CMD2				0xFDAB
#define SD_CMD3				0xFDAC
#define SD_CMD4				0xFDAD
#define SD_CMD5				0xFDAE
#define SD_BYTE_CNT_L			0xFDAF
#define SD_BYTE_CNT_H			0xFDB0
#define SD_BLOCK_CNT_L			0xFDB1
#define SD_BLOCK_CNT_H			0xFDB2
#define SD_TRANSFER			0xFDB3
#define   SD_TRANSFER_START		0x80
#define   SD_TRANSFER_END		0x40
#define   SD_STAT_IDLE			0x20
#define   SD_TRANSFER_ERR		0x10
#define   SD_TM_NORMAL_WRITE		0x00
#define   SD_TM_AUTO_WRITE_3		0x01
#define   SD_TM_AUTO_WRITE_4		0x02
#define   SD_TM_AUTO_READ_3		0x05
#define   SD_TM_AUTO_READ_4		0x06
#define   SD_TM_CMD_RSP			0x08
#define   SD_TM_AUTO_WRITE_1		0x09
#define   SD_TM_AUTO_WRITE_2		0x0A
#define   SD_TM_NORMAL_READ		0x0C
#define   SD_TM_AUTO_READ_1		0x0D
#define   SD_TM_AUTO_READ_2		0x0E
#define   SD_TM_AUTO_TUNING		0x0F
#define SD_CMD_STATE			0xFDB5
#define   SD_CMD_IDLE			0x80

#define SD_DATA_STATE			0xFDB6
#define   SD_DATA_IDLE			0x80
#define REG_SD_STOP_SDCLK_CFG		0xFDB8
#define   SD30_CLK_STOP_CFG_EN		0x04
#define   SD30_CLK_STOP_CFG1		0x02
#define   SD30_CLK_STOP_CFG0		0x01
#define REG_PRE_RW_MODE			0xFD70
#define EN_INFINITE_MODE		0x01
#define REG_CRC_DUMMY_0		0xFD71
#define CFG_SD_POW_AUTO_PD		(1<<0)

#define SRCTL				0xFC13

#define DCM_DRP_CTL			0xFC23
#define   DCM_RESET			0x08
#define   DCM_LOCKED			0x04
#define   DCM_208M			0x00
#define   DCM_TX			0x01
#define   DCM_RX			0x02
#define DCM_DRP_TRIG			0xFC24
#define   DRP_START			0x80
#define   DRP_DONE			0x40
#define DCM_DRP_CFG			0xFC25
#define   DRP_WRITE			0x80
#define   DRP_READ			0x00
#define   DCM_WRITE_ADDRESS_50		0x50
#define   DCM_WRITE_ADDRESS_51		0x51
#define   DCM_READ_ADDRESS_00		0x00
#define   DCM_READ_ADDRESS_51		0x51
#define DCM_DRP_WR_DATA_L		0xFC26
#define DCM_DRP_WR_DATA_H		0xFC27
#define DCM_DRP_RD_DATA_L		0xFC28
#define DCM_DRP_RD_DATA_H		0xFC29
#define SD_VPCLK0_CTL			0xFC2A
#define SD_VPCLK1_CTL			0xFC2B
#define   PHASE_SELECT_MASK		0x1F
#define SD_DCMPS0_CTL			0xFC2C
#define SD_DCMPS1_CTL			0xFC2D
#define SD_VPTX_CTL			SD_VPCLK0_CTL
#define SD_VPRX_CTL			SD_VPCLK1_CTL
#define   PHASE_CHANGE			0x80
#define   PHASE_NOT_RESET		0x40
#define SD_DCMPS_TX_CTL			SD_DCMPS0_CTL
#define SD_DCMPS_RX_CTL			SD_DCMPS1_CTL
#define   DCMPS_CHANGE			0x80
#define   DCMPS_CHANGE_DONE		0x40
#define   DCMPS_ERROR			0x20
#define   DCMPS_CURRENT_PHASE		0x1F
#define CARD_CLK_SOURCE			0xFC2E
#define   CRC_FIX_CLK			(0x00 << 0)
#define   CRC_VAR_CLK0			(0x01 << 0)
#define   CRC_VAR_CLK1			(0x02 << 0)
#define   SD30_FIX_CLK			(0x00 << 2)
#define   SD30_VAR_CLK0			(0x01 << 2)
#define   SD30_VAR_CLK1			(0x02 << 2)
#define   SAMPLE_FIX_CLK		(0x00 << 4)
#define   SAMPLE_VAR_CLK0		(0x01 << 4)
#define   SAMPLE_VAR_CLK1		(0x02 << 4)
#define CARD_PWR_CTL			0xFD50
#define   PMOS_STRG_MASK		0x10
#define   PMOS_STRG_800mA		0x10
#define   PMOS_STRG_400mA		0x00
#define   SD_POWER_OFF			0x03
#define   SD_PARTIAL_POWER_ON		0x01
#define   SD_POWER_ON			0x00
#define   SD_POWER_MASK			0x03
#define   MS_POWER_OFF			0x0C
#define   MS_PARTIAL_POWER_ON		0x04
#define   MS_POWER_ON			0x00
#define   MS_POWER_MASK			0x0C
#define   BPP_POWER_OFF			0x0F
#define   BPP_POWER_5_PERCENT_ON	0x0E
#define   BPP_POWER_10_PERCENT_ON	0x0C
#define   BPP_POWER_15_PERCENT_ON	0x08
#define   BPP_POWER_ON			0x00
#define   BPP_POWER_MASK		0x0F
#define   SD_VCC_PARTIAL_POWER_ON	0x02
#define   SD_VCC_POWER_ON		0x00
#define CARD_CLK_SWITCH			0xFD51
#define RTL8411B_PACKAGE_MODE		0xFD51
#define CARD_SHARE_MODE			0xFD52
#define   CARD_SHARE_MASK		0x0F
#define   CARD_SHARE_MULTI_LUN		0x00
#define   CARD_SHARE_NORMAL		0x00
#define   CARD_SHARE_48_SD		0x04
#define   CARD_SHARE_48_MS		0x08
#define   CARD_SHARE_BAROSSA_SD		0x01
#define   CARD_SHARE_BAROSSA_MS		0x02
#define CARD_DRIVE_SEL			0xFD53
#define   MS_DRIVE_8mA			(0x01 << 6)
#define   MMC_DRIVE_8mA			(0x01 << 4)
#define   XD_DRIVE_8mA			(0x01 << 2)
#define   GPIO_DRIVE_8mA		0x01
#define RTS5209_CARD_DRIVE_DEFAULT	(MS_DRIVE_8mA | MMC_DRIVE_8mA |\
					XD_DRIVE_8mA | GPIO_DRIVE_8mA)
#define RTL8411_CARD_DRIVE_DEFAULT	(MS_DRIVE_8mA | MMC_DRIVE_8mA |\
					XD_DRIVE_8mA)
#define RTSX_CARD_DRIVE_DEFAULT		(MS_DRIVE_8mA | GPIO_DRIVE_8mA)

#define CARD_STOP			0xFD54
#define   SPI_STOP			0x01
#define   XD_STOP			0x02
#define   SD_STOP			0x04
#define   MS_STOP			0x08
#define   SPI_CLR_ERR			0x10
#define   XD_CLR_ERR			0x20
#define   SD_CLR_ERR			0x40
#define   MS_CLR_ERR			0x80
#define CARD_OE				0xFD55
#define   SD_OUTPUT_EN			0x04
#define   MS_OUTPUT_EN			0x08
#define CARD_AUTO_BLINK			0xFD56
#define CARD_GPIO_DIR			0xFD57
#define CARD_GPIO			0xFD58
#define CARD_DATA_SOURCE		0xFD5B
#define   PINGPONG_BUFFER		0x01
#define   RING_BUFFER			0x00
#define SD30_CLK_DRIVE_SEL		0xFD5A
#define   DRIVER_TYPE_A			0x05
#define   DRIVER_TYPE_B			0x03
#define   DRIVER_TYPE_C			0x02
#define   DRIVER_TYPE_D			0x01
#define CARD_SELECT			0xFD5C
#define   SD_MOD_SEL			2
#define   MS_MOD_SEL			3
#define SD30_DRIVE_SEL			0xFD5E
#define   CFG_DRIVER_TYPE_A		0x02
#define   CFG_DRIVER_TYPE_B		0x03
#define   CFG_DRIVER_TYPE_C		0x01
#define   CFG_DRIVER_TYPE_D		0x00
#define SD30_CMD_DRIVE_SEL		0xFD5E
#define SD30_DAT_DRIVE_SEL		0xFD5F
#define CARD_CLK_EN			0xFD69
#define   SD_CLK_EN			0x04
#define   MS_CLK_EN			0x08
#define   SD40_CLK_EN		0x10
#define SDIO_CTRL			0xFD6B
#define CD_PAD_CTL			0xFD73
#define   CD_DISABLE_MASK		0x07
#define   MS_CD_DISABLE			0x04
#define   SD_CD_DISABLE			0x02
#define   XD_CD_DISABLE			0x01
#define   CD_DISABLE			0x07
#define   CD_ENABLE			0x00
#define   MS_CD_EN_ONLY			0x03
#define   SD_CD_EN_ONLY			0x05
#define   XD_CD_EN_ONLY			0x06
#define   FORCE_CD_LOW_MASK		0x38
#define   FORCE_CD_XD_LOW		0x08
#define   FORCE_CD_SD_LOW		0x10
#define   FORCE_CD_MS_LOW		0x20
#define   CD_AUTO_DISABLE		0x40
#define FPDCTL				0xFC00
#define   SSC_POWER_DOWN		0x01
#define   SD_OC_POWER_DOWN		0x02
#define   ALL_POWER_DOWN		0x03
#define   OC_POWER_DOWN			0x02
#define PDINFO				0xFC01

#define CLK_CTL				0xFC02
#define   CHANGE_CLK			0x01
#define   CLK_LOW_FREQ			0x01

#define CLK_DIV				0xFC03
#define   CLK_DIV_1			0x01
#define   CLK_DIV_2			0x02
#define   CLK_DIV_4			0x03
#define   CLK_DIV_8			0x04
#define CLK_SEL				0xFC04

#define SSC_DIV_N_0			0xFC0F
#define SSC_DIV_N_1			0xFC10
#define SSC_CTL1			0xFC11
#define    SSC_RSTB			0x80
#define    SSC_8X_EN			0x40
#define    SSC_FIX_FRAC			0x20
#define    SSC_SEL_1M			0x00
#define    SSC_SEL_2M			0x08
#define    SSC_SEL_4M			0x10
#define    SSC_SEL_8M			0x18
#define SSC_CTL2			0xFC12
#define    SSC_DEPTH_MASK		0x07
#define    SSC_DEPTH_DISALBE		0x00
#define    SSC_DEPTH_4M			0x01
#define    SSC_DEPTH_2M			0x02
#define    SSC_DEPTH_1M			0x03
#define    SSC_DEPTH_500K		0x04
#define    SSC_DEPTH_250K		0x05
#define RCCTL				0xFC14

#define FPGA_PULL_CTL			0xFC1D
#define OLT_LED_CTL			0xFC1E
#define   LED_SHINE_MASK		0x08
#define   LED_SHINE_EN			0x08
#define   LED_SHINE_DISABLE		0x00
#define GPIO_CTL			0xFC1F

#define LDO_CTL				0xFC1E
#define   BPP_ASIC_1V7			0x00
#define   BPP_ASIC_1V8			0x01
#define   BPP_ASIC_1V9			0x02
#define   BPP_ASIC_2V0			0x03
#define   BPP_ASIC_2V7			0x04
#define   BPP_ASIC_2V8			0x05
#define   BPP_ASIC_3V2			0x06
#define   BPP_ASIC_3V3			0x07
#define   BPP_REG_TUNED18		0x07
#define   BPP_TUNED18_SHIFT_8402	5
#define   BPP_TUNED18_SHIFT_8411	4
#define   BPP_PAD_MASK			0x04
#define   BPP_PAD_3V3			0x04
#define   BPP_PAD_1V8			0x00
#define   BPP_LDO_POWB			0x03
#define   BPP_LDO_ON			0x00
#define   BPP_LDO_SUSPEND		0x02
#define   BPP_LDO_OFF			0x03
#define EFUSE_CTL			0xFC30
#define EFUSE_ADD			0xFC31
#define SYS_VER				0xFC32
#define EFUSE_DATAL			0xFC34
#define EFUSE_DATAH			0xFC35

#define CARD_PULL_CTL1			0xFD60
#define CARD_PULL_CTL2			0xFD61
#define CARD_PULL_CTL3			0xFD62
#define CARD_PULL_CTL4			0xFD63
#define CARD_PULL_CTL5			0xFD64
#define CARD_PULL_CTL6			0xFD65

/* PCI Express Related Registers */
#define IRQEN0				0xFE20
#define IRQSTAT0			0xFE21
#define    DMA_DONE_INT			0x80
#define    SUSPEND_INT			0x40
#define    LINK_RDY_INT			0x20
#define    LINK_DOWN_INT		0x10
#define IRQEN1				0xFE22
#define IRQSTAT1			0xFE23
#define TLPRIEN				0xFE24
#define TLPRISTAT			0xFE25
#define TLPTIEN				0xFE26
#define TLPTISTAT			0xFE27
#define DMATC0				0xFE28
#define DMATC1				0xFE29
#define DMATC2				0xFE2A
#define DMATC3				0xFE2B
#define DMACTL				0xFE2C
#define   DMA_RST			0x80
#define   DMA_BUSY			0x04
#define   DMA_DIR_TO_CARD		0x00
#define   DMA_DIR_FROM_CARD		0x02
#define   DMA_EN			0x01
#define   DMA_128			(0 << 4)
#define   DMA_256			(1 << 4)
#define   DMA_512			(2 << 4)
#define   DMA_1024			(3 << 4)
#define   DMA_PACK_SIZE_MASK		0x30
#define BCTL				0xFE2D
#define RBBC0				0xFE2E
#define RBBC1				0xFE2F
#define RBDAT				0xFE30
#define RBCTL				0xFE34
#define   U_AUTO_DMA_EN_MASK		0x20
#define   U_AUTO_DMA_DISABLE		0x00
#define   RB_FLUSH			0x80
#define CFGADDR0			0xFE35
#define CFGADDR1			0xFE36
#define CFGDATA0			0xFE37
#define CFGDATA1			0xFE38
#define CFGDATA2			0xFE39
#define CFGDATA3			0xFE3A
#define CFGRWCTL			0xFE3B
#define PHYRWCTL			0xFE3C
#define PHYDATA0			0xFE3D
#define PHYDATA1			0xFE3E
#define PHYADDR				0xFE3F
#define MSGRXDATA0			0xFE40
#define MSGRXDATA1			0xFE41
#define MSGRXDATA2			0xFE42
#define MSGRXDATA3			0xFE43
#define MSGTXDATA0			0xFE44
#define MSGTXDATA1			0xFE45
#define MSGTXDATA2			0xFE46
#define MSGTXDATA3			0xFE47
#define MSGTXCTL			0xFE48
#define LTR_CTL				0xFE4A
#define LTR_TX_EN_MASK		BIT(7)
#define LTR_TX_EN_1			BIT(7)
#define LTR_TX_EN_0			0
#define LTR_LATENCY_MODE_MASK		BIT(6)
#define LTR_LATENCY_MODE_HW		0
#define LTR_LATENCY_MODE_SW		BIT(6)
#define OBFF_CFG			0xFE4C
#define   OBFF_EN_MASK			0x03
#define   OBFF_DISABLE			0x00

#define CDRESUMECTL			0xFE52
#define CDGW				0xFE53
#define WAKE_SEL_CTL			0xFE54
#define PCLK_CTL			0xFE55
#define   PCLK_MODE_SEL			0x20
#define PME_FORCE_CTL			0xFE56

#define ASPM_FORCE_CTL			0xFE57
#define   FORCE_ASPM_CTL0		0x10
#define   FORCE_ASPM_CTL1		0x20
#define   FORCE_ASPM_VAL_MASK		0x03
#define   FORCE_ASPM_L1_EN		0x02
#define   FORCE_ASPM_L0_EN		0x01
#define   FORCE_ASPM_NO_ASPM		0x00
#define PM_CLK_FORCE_CTL		0xFE58
#define   CLK_PM_EN			0x01
#define FUNC_FORCE_CTL			0xFE59
#define   FUNC_FORCE_UPME_XMT_DBG	0x02
#define PERST_GLITCH_WIDTH		0xFE5C
#define CHANGE_LINK_STATE		0xFE5B
#define RESET_LOAD_REG			0xFE5E
#define EFUSE_CONTENT			0xFE5F
#define HOST_SLEEP_STATE		0xFE60
#define   HOST_ENTER_S1			1
#define   HOST_ENTER_S3			2

#define SDIO_CFG			0xFE70
#define PM_EVENT_DEBUG			0xFE71
#define   PME_DEBUG_0			0x08
#define NFTS_TX_CTRL			0xFE72

#define PWR_GATE_CTRL			0xFE75
#define   PWR_GATE_EN			0x01
#define   LDO3318_PWR_MASK		0x06
#define   LDO_ON			0x00
#define   LDO_SUSPEND			0x04
#define   LDO_OFF			0x06
#define PWD_SUSPEND_EN			0xFE76
#define LDO_PWR_SEL			0xFE78

#define L1SUB_CONFIG1			0xFE8D
#define   AUX_CLK_ACTIVE_SEL_MASK	0x01
#define   MAC_CKSW_DONE			0x00
#define L1SUB_CONFIG2			0xFE8E
#define   L1SUB_AUTO_CFG		0x02
#define L1SUB_CONFIG3			0xFE8F
#define   L1OFF_MBIAS2_EN_5250		BIT(7)

#define DUMMY_REG_RESET_0		0xFE90
#define   IC_VERSION_MASK		0x0F

#define REG_VREF			0xFE97
#define   PWD_SUSPND_EN			0x10
#define RTS5260_DMA_RST_CTL_0		0xFEBF
#define   RTS5260_DMA_RST		0x80
#define   RTS5260_ADMA3_RST		0x40
#define AUTOLOAD_CFG_BASE		0xFF00
#define RELINK_TIME_MASK		0x01
#define PETXCFG				0xFF03
#define FORCE_CLKREQ_DELINK_MASK	BIT(7)
#define FORCE_CLKREQ_LOW	0x80
#define FORCE_CLKREQ_HIGH	0x00

#define PM_CTRL1			0xFF44
#define   CD_RESUME_EN_MASK		0xF0

#define PM_CTRL2			0xFF45
#define PM_CTRL3			0xFF46
#define   SDIO_SEND_PME_EN		0x80
#define   FORCE_RC_MODE_ON		0x40
#define   FORCE_RX50_LINK_ON		0x20
#define   D3_DELINK_MODE_EN		0x10
#define   USE_PESRTB_CTL_DELINK		0x08
#define   DELAY_PIN_WAKE		0x04
#define   RESET_PIN_WAKE		0x02
#define   PM_WAKE_EN			0x01
#define PM_CTRL4			0xFF47

/* FW config info register */
#define RTS5261_FW_CFG_INFO0		0xFF50
#define   RTS5261_FW_EXPRESS_TEST_MASK	(0x01 << 0)
#define   RTS5261_FW_EA_MODE_MASK	(0x01 << 5)
#define RTS5261_FW_CFG0			0xFF54
#define   RTS5261_FW_ENTER_EXPRESS	(0x01 << 0)

#define RTS5261_FW_CFG1			0xFF55
#define   RTS5261_SYS_CLK_SEL_MCU_CLK	(0x01 << 7)
#define   RTS5261_CRC_CLK_SEL_MCU_CLK	(0x01 << 6)
#define   RTS5261_FAKE_MCU_CLOCK_GATING	(0x01 << 5)
#define   RTS5261_MCU_BUS_SEL_MASK	(0x01 << 4)
#define   RTS5261_MCU_CLOCK_SEL_MASK	(0x03 << 2)
#define   RTS5261_MCU_CLOCK_SEL_16M	(0x01 << 2)
#define   RTS5261_MCU_CLOCK_GATING	(0x01 << 1)
#define   RTS5261_DRIVER_ENABLE_FW	(0x01 << 0)

#define REG_CFG_OOBS_OFF_TIMER 0xFEA6
#define REG_CFG_OOBS_ON_TIMER 0xFEA7
#define REG_CFG_VCM_ON_TIMER 0xFEA8
#define REG_CFG_OOBS_POLLING 0xFEA9

/* Memory mapping */
#define SRAM_BASE			0xE600
#define RBUF_BASE			0xF400
#define PPBUF_BASE1			0xF800
#define PPBUF_BASE2			0xFA00
#define IMAGE_FLAG_ADDR0		0xCE80
#define IMAGE_FLAG_ADDR1		0xCE81

#define RREF_CFG			0xFF6C
#define   RREF_VBGSEL_MASK		0x38
#define   RREF_VBGSEL_1V25		0x28

#define OOBS_CONFIG			0xFF6E
#define   OOBS_AUTOK_DIS		0x80
#define   OOBS_VAL_MASK			0x1F

#define LDO_DV18_CFG			0xFF70
#define   LDO_DV18_SR_MASK		0xC0
#define   LDO_DV18_SR_DF		0x40
#define   DV331812_MASK			0x70
#define   DV331812_33			0x70
#define   DV331812_17			0x30

#define LDO_CONFIG2			0xFF71
#define   LDO_D3318_MASK		0x07
#define   LDO_D3318_33V			0x07
#define   LDO_D3318_18V			0x02
#define   DV331812_VDD1			0x04
#define   DV331812_POWERON		0x08
#define   DV331812_POWEROFF		0x00

#define LDO_VCC_CFG0			0xFF72
#define   LDO_VCC_LMTVTH_MASK		0x30
#define   LDO_VCC_LMTVTH_2A		0x10
/*RTS5260*/
#define   RTS5260_DVCC_TUNE_MASK	0x70
#define   RTS5260_DVCC_33		0x70

/*RTS5261*/
#define RTS5261_LDO1_CFG0		0xFF72
#define   RTS5261_LDO1_OCP_THD_MASK	(0x07 << 5)
#define   RTS5261_LDO1_OCP_EN		(0x01 << 4)
#define   RTS5261_LDO1_OCP_LMT_THD_MASK	(0x03 << 2)
#define   RTS5261_LDO1_OCP_LMT_EN	(0x01 << 1)

#define LDO_VCC_CFG1			0xFF73
#define   LDO_VCC_REF_TUNE_MASK		0x30
#define   LDO_VCC_REF_1V2		0x20
#define   LDO_VCC_TUNE_MASK		0x07
#define   LDO_VCC_1V8			0x04
#define   LDO_VCC_3V3			0x07
#define   LDO_VCC_LMT_EN		0x08
/*RTS5260*/
#define	  LDO_POW_SDVDD1_MASK		0x08
#define	  LDO_POW_SDVDD1_ON		0x08
#define	  LDO_POW_SDVDD1_OFF		0x00

#define LDO_VIO_CFG			0xFF75
#define   LDO_VIO_SR_MASK		0xC0
#define   LDO_VIO_SR_DF			0x40
#define   LDO_VIO_REF_TUNE_MASK		0x30
#define   LDO_VIO_REF_1V2		0x20
#define   LDO_VIO_TUNE_MASK		0x07
#define   LDO_VIO_1V7			0x03
#define   LDO_VIO_1V8			0x04
#define   LDO_VIO_3V3			0x07

#define LDO_DV12S_CFG			0xFF76
#define   LDO_REF12_TUNE_MASK		0x18
#define   LDO_REF12_TUNE_DF		0x10
#define   LDO_D12_TUNE_MASK		0x07
#define   LDO_D12_TUNE_DF		0x04

#define LDO_AV12S_CFG			0xFF77
#define   LDO_AV12S_TUNE_MASK		0x07
#define   LDO_AV12S_TUNE_DF		0x04

#define SD40_LDO_CTL1			0xFE7D
#define   SD40_VIO_TUNE_MASK		0x70
#define   SD40_VIO_TUNE_1V7		0x30
#define   SD_VIO_LDO_1V8		0x40
#define   SD_VIO_LDO_3V3		0x70

#define RTS5264_AUTOLOAD_CFG2		0xFF7D
#define RTS5264_CHIP_RST_N_SEL		(1 << 6)

#define RTS5260_AUTOLOAD_CFG4		0xFF7F
#define   RTS5260_MIMO_DISABLE		0x8A
/*RTS5261*/
#define   RTS5261_AUX_CLK_16M_EN		(1 << 5)

#define RTS5260_REG_GPIO_CTL0		0xFC1A
#define   RTS5260_REG_GPIO_MASK		0x01
#define   RTS5260_REG_GPIO_ON		0x01
#define   RTS5260_REG_GPIO_OFF		0x00

#define PWR_GLOBAL_CTRL			0xF200
#define PCIE_L1_2_EN			0x0C
#define PCIE_L1_1_EN			0x0A
#define PCIE_L1_0_EN			0x09
#define PWR_FE_CTL			0xF201
#define PCIE_L1_2_PD_FE_EN		0x0C
#define PCIE_L1_1_PD_FE_EN		0x0A
#define PCIE_L1_0_PD_FE_EN		0x09
#define CFG_PCIE_APHY_OFF_0		0xF204
#define CFG_PCIE_APHY_OFF_0_DEFAULT	0xBF
#define CFG_PCIE_APHY_OFF_1		0xF205
#define CFG_PCIE_APHY_OFF_1_DEFAULT	0xFF
#define CFG_PCIE_APHY_OFF_2		0xF206
#define CFG_PCIE_APHY_OFF_2_DEFAULT	0x01
#define CFG_PCIE_APHY_OFF_3		0xF207
#define CFG_PCIE_APHY_OFF_3_DEFAULT	0x00
#define CFG_L1_0_PCIE_MAC_RET_VALUE	0xF20C
#define CFG_L1_0_PCIE_DPHY_RET_VALUE	0xF20E
#define CFG_L1_0_SYS_RET_VALUE		0xF210
#define CFG_L1_0_CRC_MISC_RET_VALUE	0xF212
#define CFG_L1_0_CRC_SD30_RET_VALUE	0xF214
#define CFG_L1_0_CRC_SD40_RET_VALUE	0xF216
#define CFG_LP_FPWM_VALUE		0xF219
#define CFG_LP_FPWM_VALUE_DEFAULT	0x18
#define PWC_CDR				0xF253
#define PWC_CDR_DEFAULT			0x03
#define CFG_L1_0_RET_VALUE_DEFAULT	0x1B
#define CFG_L1_0_CRC_MISC_RET_VALUE_DEFAULT	0x0C

/* OCPCTL */
#define SD_DETECT_EN			0x08
#define SD_OCP_INT_EN			0x04
#define SD_OCP_INT_CLR			0x02
#define SD_OC_CLR			0x01

#define SDVIO_DETECT_EN			(1 << 7)
#define SDVIO_OCP_INT_EN		(1 << 6)
#define SDVIO_OCP_INT_CLR		(1 << 5)
#define SDVIO_OC_CLR			(1 << 4)

/* OCPSTAT */
#define SD_OCP_DETECT			0x08
#define SD_OC_NOW			0x04
#define SD_OC_EVER			0x02

#define SDVIO_OC_NOW			(1 << 6)
#define SDVIO_OC_EVER			(1 << 5)

#define REG_OCPCTL			0xFD6A
#define REG_OCPSTAT			0xFD6E
#define REG_OCPGLITCH			0xFD6C
#define REG_OCPPARA1			0xFD6B
#define REG_OCPPARA2			0xFD6D

/* rts5260 DV3318 OCP-related registers */
#define REG_DV3318_OCPCTL		0xFD89
#define DV3318_OCP_TIME_MASK	0xF0
#define DV3318_DETECT_EN		0x08
#define DV3318_OCP_INT_EN		0x04
#define DV3318_OCP_INT_CLR		0x02
#define DV3318_OCP_CLR			0x01

#define REG_DV3318_OCPSTAT		0xFD8A
#define DV3318_OCP_GlITCH_TIME_MASK	0xF0
#define DV3318_OCP_DETECT		0x08
#define DV3318_OCP_NOW			0x04
#define DV3318_OCP_EVER			0x02

#define SD_OCP_GLITCH_MASK		0x0F

/* OCPPARA1 */
#define SDVIO_OCP_TIME_60		0x00
#define SDVIO_OCP_TIME_100		0x10
#define SDVIO_OCP_TIME_200		0x20
#define SDVIO_OCP_TIME_400		0x30
#define SDVIO_OCP_TIME_600		0x40
#define SDVIO_OCP_TIME_800		0x50
#define SDVIO_OCP_TIME_1100		0x60
#define SDVIO_OCP_TIME_MASK		0x70

#define SD_OCP_TIME_60			0x00
#define SD_OCP_TIME_100			0x01
#define SD_OCP_TIME_200			0x02
#define SD_OCP_TIME_400			0x03
#define SD_OCP_TIME_600			0x04
#define SD_OCP_TIME_800			0x05
#define SD_OCP_TIME_1100		0x06
#define SD_OCP_TIME_MASK		0x07

/* OCPPARA2 */
#define SDVIO_OCP_THD_190		0x00
#define SDVIO_OCP_THD_250		0x10
#define SDVIO_OCP_THD_320		0x20
#define SDVIO_OCP_THD_380		0x30
#define SDVIO_OCP_THD_440		0x40
#define SDVIO_OCP_THD_500		0x50
#define SDVIO_OCP_THD_570		0x60
#define SDVIO_OCP_THD_630		0x70
#define SDVIO_OCP_THD_MASK		0x70

#define SD_OCP_THD_450			0x00
#define SD_OCP_THD_550			0x01
#define SD_OCP_THD_650			0x02
#define SD_OCP_THD_750			0x03
#define SD_OCP_THD_850			0x04
#define SD_OCP_THD_950			0x05
#define SD_OCP_THD_1050			0x06
#define SD_OCP_THD_1150			0x07
#define SD_OCP_THD_MASK			0x07

#define SDVIO_OCP_GLITCH_MASK		0xF0
#define SDVIO_OCP_GLITCH_NONE		0x00
#define SDVIO_OCP_GLITCH_50U		0x10
#define SDVIO_OCP_GLITCH_100U		0x20
#define SDVIO_OCP_GLITCH_200U		0x30
#define SDVIO_OCP_GLITCH_600U		0x40
#define SDVIO_OCP_GLITCH_800U		0x50
#define SDVIO_OCP_GLITCH_1M		0x60
#define SDVIO_OCP_GLITCH_2M		0x70
#define SDVIO_OCP_GLITCH_3M		0x80
#define SDVIO_OCP_GLITCH_4M		0x90
#define SDVIO_OCP_GLIVCH_5M		0xA0
#define SDVIO_OCP_GLITCH_6M		0xB0
#define SDVIO_OCP_GLITCH_7M		0xC0
#define SDVIO_OCP_GLITCH_8M		0xD0
#define SDVIO_OCP_GLITCH_9M		0xE0
#define SDVIO_OCP_GLITCH_10M		0xF0

#define SD_OCP_GLITCH_MASK		0x0F
#define SD_OCP_GLITCH_NONE		0x00
#define SD_OCP_GLITCH_50U		0x01
#define SD_OCP_GLITCH_100U		0x02
#define SD_OCP_GLITCH_200U		0x03
#define SD_OCP_GLITCH_600U		0x04
#define SD_OCP_GLITCH_800U		0x05
#define SD_OCP_GLITCH_1M		0x06
#define SD_OCP_GLITCH_2M		0x07
#define SD_OCP_GLITCH_3M		0x08
#define SD_OCP_GLITCH_4M		0x09
#define SD_OCP_GLIVCH_5M		0x0A
#define SD_OCP_GLITCH_6M		0x0B
#define SD_OCP_GLITCH_7M		0x0C
#define SD_OCP_GLITCH_8M		0x0D
#define SD_OCP_GLITCH_9M		0x0E
#define SD_OCP_GLITCH_10M		0x0F

/* Phy register */
#define PHY_PCR				0x00
#define   PHY_PCR_FORCE_CODE		0xB000
#define   PHY_PCR_OOBS_CALI_50		0x0800
#define   PHY_PCR_OOBS_VCM_08		0x0200
#define   PHY_PCR_OOBS_SEN_90		0x0040
#define   PHY_PCR_RSSI_EN		0x0002
#define   PHY_PCR_RX10K			0x0001

#define PHY_RCR0			0x01
#define PHY_RCR1			0x02
#define   PHY_RCR1_ADP_TIME_4		0x0400
#define   PHY_RCR1_VCO_COARSE		0x001F
#define   PHY_RCR1_INIT_27S		0x0A1F
#define PHY_SSCCR2			0x02
#define   PHY_SSCCR2_PLL_NCODE		0x0A00
#define   PHY_SSCCR2_TIME0		0x001C
#define   PHY_SSCCR2_TIME2_WIDTH	0x0003

#define PHY_RCR2			0x03
#define   PHY_RCR2_EMPHASE_EN		0x8000
#define   PHY_RCR2_NADJR		0x4000
#define   PHY_RCR2_CDR_SR_2		0x0100
#define   PHY_RCR2_FREQSEL_12		0x0040
#define   PHY_RCR2_CDR_SC_12P		0x0010
#define   PHY_RCR2_CALIB_LATE		0x0002
#define   PHY_RCR2_INIT_27S		0xC152
#define PHY_SSCCR3			0x03
#define   PHY_SSCCR3_STEP_IN		0x2740
#define   PHY_SSCCR3_CHECK_DELAY	0x0008
#define _PHY_ANA03			0x03
#define   _PHY_ANA03_TIMER_MAX		0x2700
#define   _PHY_ANA03_OOBS_DEB_EN	0x0040
#define   _PHY_CMU_DEBUG_EN		0x0008

#define PHY_RTCR			0x04
#define PHY_RDR				0x05
#define   PHY_RDR_RXDSEL_1_9		0x4000
#define   PHY_SSC_AUTO_PWD		0x0600
#define PHY_TCR0			0x06
#define PHY_TCR1			0x07
#define PHY_TUNE			0x08
#define   PHY_TUNE_TUNEREF_1_0		0x4000
#define   PHY_TUNE_VBGSEL_1252		0x0C00
#define   PHY_TUNE_SDBUS_33		0x0200
#define   PHY_TUNE_TUNED18		0x01C0
#define   PHY_TUNE_TUNED12		0X0020
#define   PHY_TUNE_TUNEA12		0x0004
#define   PHY_TUNE_VOLTAGE_MASK		0xFC3F
#define   PHY_TUNE_VOLTAGE_3V3		0x03C0
#define   PHY_TUNE_D18_1V8		0x0100
#define   PHY_TUNE_D18_1V7		0x0080
#define PHY_ANA08			0x08
#define   PHY_ANA08_RX_EQ_DCGAIN	0x5000
#define   PHY_ANA08_SEL_RX_EN		0x0400
#define   PHY_ANA08_RX_EQ_VAL		0x03C0
#define   PHY_ANA08_SCP			0x0020
#define   PHY_ANA08_SEL_IPI		0x0004

#define PHY_IMR				0x09
#define PHY_BPCR			0x0A
#define   PHY_BPCR_IBRXSEL		0x0400
#define   PHY_BPCR_IBTXSEL		0x0100
#define   PHY_BPCR_IB_FILTER		0x0080
#define   PHY_BPCR_CMIRROR_EN		0x0040

#define PHY_BIST			0x0B
#define PHY_RAW_L			0x0C
#define PHY_RAW_H			0x0D
#define PHY_RAW_DATA			0x0E
#define PHY_HOST_CLK_CTRL		0x0F
#define PHY_DMR				0x10
#define PHY_BACR			0x11
#define   PHY_BACR_BASIC_MASK		0xFFF3
#define PHY_IER				0x12
#define PHY_BCSR			0x13
#define PHY_BPR				0x14
#define PHY_BPNR2			0x15
#define PHY_BPNR			0x16
#define PHY_BRNR2			0x17
#define PHY_BENR			0x18
#define PHY_REV				0x19
#define   PHY_REV_RESV			0xE000
#define   PHY_REV_RXIDLE_LATCHED	0x1000
#define   PHY_REV_P1_EN			0x0800
#define   PHY_REV_RXIDLE_EN		0x0400
#define   PHY_REV_CLKREQ_TX_EN		0x0200
#define   PHY_REV_CLKREQ_RX_EN		0x0100
#define   PHY_REV_CLKREQ_DT_1_0		0x0040
#define   PHY_REV_STOP_CLKRD		0x0020
#define   PHY_REV_RX_PWST		0x0008
#define   PHY_REV_STOP_CLKWR		0x0004
#define _PHY_REV0			0x19
#define   _PHY_REV0_FILTER_OUT		0x3800
#define   _PHY_REV0_CDR_BYPASS_PFD	0x0100
#define   _PHY_REV0_CDR_RX_IDLE_BYPASS	0x0002

#define PHY_FLD0			0x1A
#define PHY_ANA1A			0x1A
#define   PHY_ANA1A_TXR_LOOPBACK	0x2000
#define   PHY_ANA1A_RXT_BIST		0x0500
#define   PHY_ANA1A_TXR_BIST		0x0040
#define   PHY_ANA1A_REV			0x0006
#define   PHY_FLD0_INIT_27S		0x2546
#define PHY_FLD1			0x1B
#define PHY_FLD2			0x1C
#define PHY_FLD3			0x1D
#define   PHY_FLD3_TIMER_4		0x0800
#define   PHY_FLD3_TIMER_6		0x0020
#define   PHY_FLD3_RXDELINK		0x0004
#define   PHY_FLD3_INIT_27S		0x0004
#define PHY_ANA1D			0x1D
#define   PHY_ANA1D_DEBUG_ADDR		0x0004
#define _PHY_FLD0			0x1D
#define   _PHY_FLD0_CLK_REQ_20C		0x8000
#define   _PHY_FLD0_RX_IDLE_EN		0x1000
#define   _PHY_FLD0_BIT_ERR_RSTN	0x0800
#define   _PHY_FLD0_BER_COUNT		0x01E0
#define   _PHY_FLD0_BER_TIMER		0x001E
#define   _PHY_FLD0_CHECK_EN		0x0001

#define PHY_FLD4			0x1E
#define   PHY_FLD4_FLDEN_SEL		0x4000
#define   PHY_FLD4_REQ_REF		0x2000
#define   PHY_FLD4_RXAMP_OFF		0x1000
#define   PHY_FLD4_REQ_ADDA		0x0800
#define   PHY_FLD4_BER_COUNT		0x00E0
#define   PHY_FLD4_BER_TIMER		0x000A
#define   PHY_FLD4_BER_CHK_EN		0x0001
#define   PHY_FLD4_INIT_27S		0x5C7F
#define PHY_DIG1E			0x1E
#define   PHY_DIG1E_REV			0x4000
#define   PHY_DIG1E_D0_X_D1		0x1000
#define   PHY_DIG1E_RX_ON_HOST		0x0800
#define   PHY_DIG1E_RCLK_REF_HOST	0x0400
#define   PHY_DIG1E_RCLK_TX_EN_KEEP	0x0040
#define   PHY_DIG1E_RCLK_TX_TERM_KEEP	0x0020
#define   PHY_DIG1E_RCLK_RX_EIDLE_ON	0x0010
#define   PHY_DIG1E_TX_TERM_KEEP	0x0008
#define   PHY_DIG1E_RX_TERM_KEEP	0x0004
#define   PHY_DIG1E_TX_EN_KEEP		0x0002
#define   PHY_DIG1E_RX_EN_KEEP		0x0001
#define PHY_DUM_REG			0x1F

#define PCR_SETTING_REG1		0x724
#define PCR_SETTING_REG2		0x814
#define PCR_SETTING_REG3		0x747
#define PCR_SETTING_REG4		0x818
#define PCR_SETTING_REG5		0x81C


#define rtsx_pci_init_cmd(pcr)		((pcr)->ci = 0)

#define RTS5227_DEVICE_ID		0x5227
#define RTS_MAX_TIMES_FREQ_REDUCTION	8

struct rtsx_pcr;

struct pcr_handle {
	struct rtsx_pcr			*pcr;
};

struct pcr_ops {
	int (*write_phy)(struct rtsx_pcr *pcr, u8 addr, u16 val);
	int (*read_phy)(struct rtsx_pcr *pcr, u8 addr, u16 *val);
	int		(*extra_init_hw)(struct rtsx_pcr *pcr);
	int		(*optimize_phy)(struct rtsx_pcr *pcr);
	int		(*turn_on_led)(struct rtsx_pcr *pcr);
	int		(*turn_off_led)(struct rtsx_pcr *pcr);
	int		(*enable_auto_blink)(struct rtsx_pcr *pcr);
	int		(*disable_auto_blink)(struct rtsx_pcr *pcr);
	int		(*card_power_on)(struct rtsx_pcr *pcr, int card);
	int		(*card_power_off)(struct rtsx_pcr *pcr, int card);
	int		(*switch_output_voltage)(struct rtsx_pcr *pcr,
						u8 voltage);
	unsigned int	(*cd_deglitch)(struct rtsx_pcr *pcr);
	int		(*conv_clk_and_div_n)(int clk, int dir);
	void		(*fetch_vendor_settings)(struct rtsx_pcr *pcr);
	void		(*force_power_down)(struct rtsx_pcr *pcr, u8 pm_state, bool runtime);
	void		(*stop_cmd)(struct rtsx_pcr *pcr);

	void (*set_aspm)(struct rtsx_pcr *pcr, bool enable);
	void (*set_l1off_cfg_sub_d0)(struct rtsx_pcr *pcr, int active);
	void (*enable_ocp)(struct rtsx_pcr *pcr);
	void (*disable_ocp)(struct rtsx_pcr *pcr);
	void (*init_ocp)(struct rtsx_pcr *pcr);
	void (*process_ocp)(struct rtsx_pcr *pcr);
	int (*get_ocpstat)(struct rtsx_pcr *pcr, u8 *val);
	void (*clear_ocpstat)(struct rtsx_pcr *pcr);
};

enum PDEV_STAT  {PDEV_STAT_IDLE, PDEV_STAT_RUN};
enum ASPM_MODE  {ASPM_MODE_CFG, ASPM_MODE_REG};

#define ASPM_L1_1_EN			BIT(0)
#define ASPM_L1_2_EN			BIT(1)
#define PM_L1_1_EN				BIT(2)
#define PM_L1_2_EN				BIT(3)
#define LTR_L1SS_PWR_GATE_EN	BIT(4)
#define L1_SNOOZE_TEST_EN		BIT(5)
#define LTR_L1SS_PWR_GATE_CHECK_CARD_EN	BIT(6)

/*
 * struct rtsx_cr_option  - card reader option
 * @dev_flags: device flags
 * @force_clkreq_0: force clock request
 * @ltr_en: enable ltr mode flag
 * @ltr_enabled: ltr mode in configure space flag
 * @ltr_active: ltr mode status
 * @ltr_active_latency: ltr mode active latency
 * @ltr_idle_latency: ltr mode idle latency
 * @ltr_l1off_latency: ltr mode l1off latency
 * @l1_snooze_delay: l1 snooze delay
 * @ltr_l1off_sspwrgate: ltr l1off sspwrgate
 * @ltr_l1off_snooze_sspwrgate: ltr l1off snooze sspwrgate
 * @ocp_en: enable ocp flag
 * @sd_400mA_ocp_thd: 400mA ocp thd
 * @sd_800mA_ocp_thd: 800mA ocp thd
 */
struct rtsx_cr_option {
	u32 dev_flags;
	bool force_clkreq_0;
	bool ltr_en;
	bool ltr_enabled;
	bool ltr_active;
	u32 ltr_active_latency;
	u32 ltr_idle_latency;
	u32 ltr_l1off_latency;
	u32 l1_snooze_delay;
	u8 ltr_l1off_sspwrgate;
	u8 ltr_l1off_snooze_sspwrgate;
	bool ocp_en;
	u8 sd_400mA_ocp_thd;
	u8 sd_800mA_ocp_thd;
	u8 sd_cd_reverse_en;
	u8 sd_wp_reverse_en;
};

/*
 * struct rtsx_hw_param  - card reader hardware param
 * @interrupt_en: indicate which interrutp enable
 * @ocp_glitch: ocp glitch time
 */
struct rtsx_hw_param {
	u32 interrupt_en;
	u8 ocp_glitch;
};

#define rtsx_set_dev_flag(cr, flag) \
	((cr)->option.dev_flags |= (flag))
#define rtsx_clear_dev_flag(cr, flag) \
	((cr)->option.dev_flags &= ~(flag))
#define rtsx_check_dev_flag(cr, flag) \
	((cr)->option.dev_flags & (flag))

struct rtsx_pcr {
	struct pci_dev			*pci;
	unsigned int			id;
	struct rtsx_cr_option	option;
	struct rtsx_hw_param hw_param;

	/* pci resources */
	unsigned long			addr;
	void __iomem			*remap_addr;
	int				irq;

	/* host reserved buffer */
	void				*rtsx_resv_buf;
	dma_addr_t			rtsx_resv_buf_addr;

	void				*host_cmds_ptr;
	dma_addr_t			host_cmds_addr;
	int				ci;

	void				*host_sg_tbl_ptr;
	dma_addr_t			host_sg_tbl_addr;
	int				sgi;

	u32				bier;
	char				trans_result;

	unsigned int			card_inserted;
	unsigned int			card_removed;
	unsigned int			card_exist;

	struct delayed_work		carddet_work;

	spinlock_t			lock;
	struct mutex			pcr_mutex;
	struct completion		*done;
	struct completion		*finish_me;

	unsigned int			cur_clock;
	bool				remove_pci;
	bool				msi_en;

#define EXTRA_CAPS_SD_SDR50		(1 << 0)
#define EXTRA_CAPS_SD_SDR104		(1 << 1)
#define EXTRA_CAPS_SD_DDR50		(1 << 2)
#define EXTRA_CAPS_MMC_HSDDR		(1 << 3)
#define EXTRA_CAPS_MMC_HS200		(1 << 4)
#define EXTRA_CAPS_MMC_8BIT		(1 << 5)
#define EXTRA_CAPS_NO_MMC		(1 << 7)
#define EXTRA_CAPS_SD_EXPRESS		(1 << 8)
	u32				extra_caps;

#define IC_VER_A			0
#define IC_VER_B			1
#define IC_VER_C			2
#define IC_VER_D			3
	u8				ic_version;

	u8				sd30_drive_sel_1v8;
	u8				sd30_drive_sel_3v3;
	u8				card_drive_sel;
#define ASPM_L1_EN			0x02
	u8				aspm_en;
	enum ASPM_MODE			aspm_mode;
	bool				aspm_enabled;

#define PCR_MS_PMOS			(1 << 0)
#define PCR_REVERSE_SOCKET		(1 << 1)
	u32				flags;

	u32				tx_initial_phase;
	u32				rx_initial_phase;

	const u32			*sd_pull_ctl_enable_tbl;
	const u32			*sd_pull_ctl_disable_tbl;
	const u32			*ms_pull_ctl_enable_tbl;
	const u32			*ms_pull_ctl_disable_tbl;

	const struct pcr_ops		*ops;
	enum PDEV_STAT			state;

	u16				reg_pm_ctrl3;

	int				num_slots;
	struct rtsx_slot		*slots;

	u8				dma_error_count;
	u8			ocp_stat;
	u8			ocp_stat2;
	u8			ovp_stat;
	u8			rtd3_en;
};

#define PID_524A	0x524A
#define PID_5249	0x5249
#define PID_5250	0x5250
#define PID_525A	0x525A
#define PID_5260	0x5260
#define PID_5261	0x5261
#define PID_5228	0x5228
#define PID_5264	0x5264

#define CHK_PCI_PID(pcr, pid)		((pcr)->pci->device == (pid))
#define PCI_VID(pcr)			((pcr)->pci->vendor)
#define PCI_PID(pcr)			((pcr)->pci->device)
#define is_version(pcr, pid, ver)				\
	(CHK_PCI_PID(pcr, pid) && (pcr)->ic_version == (ver))
#define is_version_higher_than(pcr, pid, ver)			\
	(CHK_PCI_PID(pcr, pid) && (pcr)->ic_version > (ver))
#define pcr_dbg(pcr, fmt, arg...)				\
	dev_dbg(&(pcr)->pci->dev, fmt, ##arg)

#define SDR104_PHASE(val)		((val) & 0xFF)
#define SDR50_PHASE(val)		(((val) >> 8) & 0xFF)
#define DDR50_PHASE(val)		(((val) >> 16) & 0xFF)
#define SDR104_TX_PHASE(pcr)		SDR104_PHASE((pcr)->tx_initial_phase)
#define SDR50_TX_PHASE(pcr)		SDR50_PHASE((pcr)->tx_initial_phase)
#define DDR50_TX_PHASE(pcr)		DDR50_PHASE((pcr)->tx_initial_phase)
#define SDR104_RX_PHASE(pcr)		SDR104_PHASE((pcr)->rx_initial_phase)
#define SDR50_RX_PHASE(pcr)		SDR50_PHASE((pcr)->rx_initial_phase)
#define DDR50_RX_PHASE(pcr)		DDR50_PHASE((pcr)->rx_initial_phase)
#define SET_CLOCK_PHASE(sdr104, sdr50, ddr50)	\
				(((ddr50) << 16) | ((sdr50) << 8) | (sdr104))

void rtsx_pci_start_run(struct rtsx_pcr *pcr);
int rtsx_pci_write_register(struct rtsx_pcr *pcr, u16 addr, u8 mask, u8 data);
int rtsx_pci_read_register(struct rtsx_pcr *pcr, u16 addr, u8 *data);
int rtsx_pci_write_phy_register(struct rtsx_pcr *pcr, u8 addr, u16 val);
int rtsx_pci_read_phy_register(struct rtsx_pcr *pcr, u8 addr, u16 *val);
void rtsx_pci_stop_cmd(struct rtsx_pcr *pcr);
void rtsx_pci_add_cmd(struct rtsx_pcr *pcr,
		u8 cmd_type, u16 reg_addr, u8 mask, u8 data);
void rtsx_pci_send_cmd_no_wait(struct rtsx_pcr *pcr);
int rtsx_pci_send_cmd(struct rtsx_pcr *pcr, int timeout);
int rtsx_pci_dma_map_sg(struct rtsx_pcr *pcr, struct scatterlist *sglist,
		int num_sg, bool read);
void rtsx_pci_dma_unmap_sg(struct rtsx_pcr *pcr, struct scatterlist *sglist,
		int num_sg, bool read);
int rtsx_pci_dma_transfer(struct rtsx_pcr *pcr, struct scatterlist *sglist,
		int count, bool read, int timeout);
int rtsx_pci_read_ppbuf(struct rtsx_pcr *pcr, u8 *buf, int buf_len);
int rtsx_pci_write_ppbuf(struct rtsx_pcr *pcr, u8 *buf, int buf_len);
int rtsx_pci_card_pull_ctl_enable(struct rtsx_pcr *pcr, int card);
int rtsx_pci_card_pull_ctl_disable(struct rtsx_pcr *pcr, int card);
int rtsx_pci_switch_clock(struct rtsx_pcr *pcr, unsigned int card_clock,
		u8 ssc_depth, bool initial_mode, bool double_clk, bool vpclk);
int rtsx_pci_card_power_on(struct rtsx_pcr *pcr, int card);
int rtsx_pci_card_power_off(struct rtsx_pcr *pcr, int card);
int rtsx_pci_card_exclusive_check(struct rtsx_pcr *pcr, int card);
int rtsx_pci_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage);
unsigned int rtsx_pci_card_exist(struct rtsx_pcr *pcr);
void rtsx_pci_complete_unfinished_transfer(struct rtsx_pcr *pcr);

static inline u8 *rtsx_pci_get_cmd_data(struct rtsx_pcr *pcr)
{
	return (u8 *)(pcr->host_cmds_ptr);
}

static inline void rtsx_pci_write_be32(struct rtsx_pcr *pcr, u16 reg, u32 val)
{
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg,     0xFF, val >> 24);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg + 1, 0xFF, val >> 16);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg + 2, 0xFF, val >> 8);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg + 3, 0xFF, val);
}

static inline int rtsx_pci_update_phy(struct rtsx_pcr *pcr, u8 addr,
	u16 mask, u16 append)
{
	int err;
	u16 val;

	err = rtsx_pci_read_phy_register(pcr, addr, &val);
	if (err < 0)
		return err;

	return rtsx_pci_write_phy_register(pcr, addr, (val & mask) | append);
}

#endif
