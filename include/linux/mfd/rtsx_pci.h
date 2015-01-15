/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 */

#ifndef __RTSX_PCI_H
#define __RTSX_PCI_H

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/mfd/rtsx_common.h>

#define MAX_RW_REG_CNT			1024

/* PCI Operation Register Address */
#define RTSX_HCBAR			0x00
#define RTSX_HCBCTLR			0x04
#define RTSX_HDBAR			0x08
#define RTSX_HDBCTLR			0x0C
#define RTSX_HAIMR			0x10
#define RTSX_BIPR			0x14
#define RTSX_BIER			0x18

/* Host command buffer control register */
#define STOP_CMD			(0x01 << 28)

/* Host data buffer control register */
#define SDMA_MODE			0x00
#define ADMA_MODE			(0x02 << 26)
#define STOP_DMA			(0x01 << 28)
#define TRIG_DMA			(0x01 << 31)

/* Host access internal memory register */
#define HAIMR_TRANS_START		(0x01 << 31)
#define HAIMR_READ			0x00
#define HAIMR_WRITE			(0x01 << 30)
#define HAIMR_READ_START		(HAIMR_TRANS_START | HAIMR_READ)
#define HAIMR_WRITE_START		(HAIMR_TRANS_START | HAIMR_WRITE)
#define HAIMR_TRANS_END			(HAIMR_TRANS_START)

/* Bus interrupt pending register */
#define CMD_DONE_INT			(1 << 31)
#define DATA_DONE_INT			(1 << 30)
#define TRANS_OK_INT			(1 << 29)
#define TRANS_FAIL_INT			(1 << 28)
#define XD_INT				(1 << 27)
#define MS_INT				(1 << 26)
#define SD_INT				(1 << 25)
#define GPIO0_INT			(1 << 24)
#define OC_INT				(1 << 23)
#define SD_WRITE_PROTECT		(1 << 19)
#define XD_EXIST			(1 << 18)
#define MS_EXIST			(1 << 17)
#define SD_EXIST			(1 << 16)
#define DELINK_INT			GPIO0_INT
#define MS_OC_INT			(1 << 23)
#define SD_OC_INT			(1 << 22)

#define CARD_INT		(XD_INT | MS_INT | SD_INT)
#define NEED_COMPLETE_INT	(DATA_DONE_INT | TRANS_OK_INT | TRANS_FAIL_INT)
#define RTSX_INT		(CMD_DONE_INT | NEED_COMPLETE_INT | \
					CARD_INT | GPIO0_INT | OC_INT)

#define CARD_EXIST		(XD_EXIST | MS_EXIST | SD_EXIST)

/* Bus interrupt enable register */
#define CMD_DONE_INT_EN		(1 << 31)
#define DATA_DONE_INT_EN	(1 << 30)
#define TRANS_OK_INT_EN		(1 << 29)
#define TRANS_FAIL_INT_EN	(1 << 28)
#define XD_INT_EN		(1 << 27)
#define MS_INT_EN		(1 << 26)
#define SD_INT_EN		(1 << 25)
#define GPIO0_INT_EN		(1 << 24)
#define OC_INT_EN		(1 << 23)
#define DELINK_INT_EN		GPIO0_INT_EN
#define MS_OC_INT_EN		(1 << 23)
#define SD_OC_INT_EN		(1 << 22)

#define READ_REG_CMD		0
#define WRITE_REG_CMD		1
#define CHECK_REG_CMD		2

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

#define rtsx_pci_read_config_byte(pcr, where, val) \
	pci_read_config_byte((pcr)->pci, where, val)

#define rtsx_pci_write_config_byte(pcr, where, val) \
	pci_write_config_byte((pcr)->pci, where, val)

#define rtsx_pci_read_config_dword(pcr, where, val) \
	pci_read_config_dword((pcr)->pci, where, val)

#define rtsx_pci_write_config_dword(pcr, where, val) \
	pci_write_config_dword((pcr)->pci, where, val)

#define STATE_TRANS_NONE	0
#define STATE_TRANS_CMD		1
#define STATE_TRANS_BUF		2
#define STATE_TRANS_SG		3

#define TRANS_NOT_READY		0
#define TRANS_RESULT_OK		1
#define TRANS_RESULT_FAIL	2
#define TRANS_NO_DEVICE		3

#define RTSX_RESV_BUF_LEN	4096
#define HOST_CMDS_BUF_LEN	1024
#define HOST_SG_TBL_BUF_LEN	(RTSX_RESV_BUF_LEN - HOST_CMDS_BUF_LEN)
#define HOST_SG_TBL_ITEMS	(HOST_SG_TBL_BUF_LEN / 8)
#define MAX_SG_ITEM_LEN		0x80000

#define HOST_TO_DEVICE		0
#define DEVICE_TO_HOST		1

#define RTSX_PHASE_MAX		32
#define RX_TUNING_CNT		3

/* SG descriptor */
#define SG_INT			0x04
#define SG_END			0x02
#define SG_VALID		0x01

#define SG_NO_OP		0x00
#define SG_TRANS_DATA		(0x02 << 4)
#define SG_LINK_DESC		(0x03 << 4)

/* Output voltage */
#define OUTPUT_3V3		0
#define OUTPUT_1V8		1

/* Card Clock Enable Register */
#define SD_CLK_EN			0x04
#define MS_CLK_EN			0x08

/* Card Select Register */
#define SD_MOD_SEL			2
#define MS_MOD_SEL			3

/* Card Output Enable Register */
#define SD_OUTPUT_EN			0x04
#define MS_OUTPUT_EN			0x08

/* CARD_SHARE_MODE */
#define CARD_SHARE_MASK			0x0F
#define CARD_SHARE_MULTI_LUN		0x00
#define	CARD_SHARE_NORMAL		0x00
#define	CARD_SHARE_48_SD		0x04
#define	CARD_SHARE_48_MS		0x08
/* CARD_SHARE_MODE for barossa */
#define CARD_SHARE_BAROSSA_SD		0x01
#define CARD_SHARE_BAROSSA_MS		0x02

/* CARD_DRIVE_SEL */
#define MS_DRIVE_8mA			(0x01 << 6)
#define MMC_DRIVE_8mA			(0x01 << 4)
#define XD_DRIVE_8mA			(0x01 << 2)
#define GPIO_DRIVE_8mA			0x01
#define RTS5209_CARD_DRIVE_DEFAULT	(MS_DRIVE_8mA | MMC_DRIVE_8mA |\
						XD_DRIVE_8mA | GPIO_DRIVE_8mA)
#define RTL8411_CARD_DRIVE_DEFAULT	(MS_DRIVE_8mA | MMC_DRIVE_8mA |\
						XD_DRIVE_8mA)
#define RTSX_CARD_DRIVE_DEFAULT		(MS_DRIVE_8mA | GPIO_DRIVE_8mA)

/* SD30_DRIVE_SEL */
#define DRIVER_TYPE_A			0x05
#define DRIVER_TYPE_B			0x03
#define DRIVER_TYPE_C			0x02
#define DRIVER_TYPE_D			0x01
#define CFG_DRIVER_TYPE_A		0x02
#define CFG_DRIVER_TYPE_B		0x03
#define CFG_DRIVER_TYPE_C		0x01
#define CFG_DRIVER_TYPE_D		0x00

/* FPDCTL */
#define SSC_POWER_DOWN			0x01
#define SD_OC_POWER_DOWN		0x02
#define ALL_POWER_DOWN			0x07
#define OC_POWER_DOWN			0x06

/* CLK_CTL */
#define CHANGE_CLK			0x01

/* LDO_CTL */
#define BPP_ASIC_1V7			0x00
#define BPP_ASIC_1V8			0x01
#define BPP_ASIC_1V9			0x02
#define BPP_ASIC_2V0			0x03
#define BPP_ASIC_2V7			0x04
#define BPP_ASIC_2V8			0x05
#define BPP_ASIC_3V2			0x06
#define BPP_ASIC_3V3			0x07
#define BPP_REG_TUNED18			0x07
#define BPP_TUNED18_SHIFT_8402		5
#define BPP_TUNED18_SHIFT_8411		4
#define BPP_PAD_MASK			0x04
#define BPP_PAD_3V3			0x04
#define BPP_PAD_1V8			0x00
#define BPP_LDO_POWB			0x03
#define BPP_LDO_ON			0x00
#define BPP_LDO_SUSPEND			0x02
#define BPP_LDO_OFF			0x03

/* CD_PAD_CTL */
#define CD_DISABLE_MASK			0x07
#define MS_CD_DISABLE			0x04
#define SD_CD_DISABLE			0x02
#define XD_CD_DISABLE			0x01
#define CD_DISABLE			0x07
#define CD_ENABLE			0x00
#define MS_CD_EN_ONLY			0x03
#define SD_CD_EN_ONLY			0x05
#define XD_CD_EN_ONLY			0x06
#define FORCE_CD_LOW_MASK		0x38
#define FORCE_CD_XD_LOW			0x08
#define FORCE_CD_SD_LOW			0x10
#define FORCE_CD_MS_LOW			0x20
#define CD_AUTO_DISABLE			0x40

/* SD_STAT1 */
#define	SD_CRC7_ERR			0x80
#define	SD_CRC16_ERR			0x40
#define	SD_CRC_WRITE_ERR		0x20
#define	SD_CRC_WRITE_ERR_MASK		0x1C
#define	GET_CRC_TIME_OUT		0x02
#define	SD_TUNING_COMPARE_ERR		0x01

/* SD_STAT2 */
#define	SD_RSP_80CLK_TIMEOUT		0x01

/* SD_BUS_STAT */
#define	SD_CLK_TOGGLE_EN		0x80
#define	SD_CLK_FORCE_STOP	        0x40
#define	SD_DAT3_STATUS		        0x10
#define	SD_DAT2_STATUS		        0x08
#define	SD_DAT1_STATUS		        0x04
#define	SD_DAT0_STATUS		        0x02
#define	SD_CMD_STATUS			0x01

/* SD_PAD_CTL */
#define	SD_IO_USING_1V8		        0x80
#define	SD_IO_USING_3V3		        0x7F
#define	TYPE_A_DRIVING		        0x00
#define	TYPE_B_DRIVING			0x01
#define	TYPE_C_DRIVING			0x02
#define	TYPE_D_DRIVING		        0x03

/* SD_SAMPLE_POINT_CTL */
#define	DDR_FIX_RX_DAT			0x00
#define	DDR_VAR_RX_DAT			0x80
#define	DDR_FIX_RX_DAT_EDGE		0x00
#define	DDR_FIX_RX_DAT_14_DELAY		0x40
#define	DDR_FIX_RX_CMD			0x00
#define	DDR_VAR_RX_CMD			0x20
#define	DDR_FIX_RX_CMD_POS_EDGE		0x00
#define	DDR_FIX_RX_CMD_14_DELAY		0x10
#define	SD20_RX_POS_EDGE		0x00
#define	SD20_RX_14_DELAY		0x08
#define SD20_RX_SEL_MASK		0x08

/* SD_PUSH_POINT_CTL */
#define	DDR_FIX_TX_CMD_DAT		0x00
#define	DDR_VAR_TX_CMD_DAT		0x80
#define	DDR_FIX_TX_DAT_14_TSU		0x00
#define	DDR_FIX_TX_DAT_12_TSU		0x40
#define	DDR_FIX_TX_CMD_NEG_EDGE		0x00
#define	DDR_FIX_TX_CMD_14_AHEAD		0x20
#define	SD20_TX_NEG_EDGE		0x00
#define	SD20_TX_14_AHEAD		0x10
#define SD20_TX_SEL_MASK		0x10
#define	DDR_VAR_SDCLK_POL_SWAP		0x01

/* SD_TRANSFER */
#define	SD_TRANSFER_START		0x80
#define	SD_TRANSFER_END			0x40
#define SD_STAT_IDLE			0x20
#define	SD_TRANSFER_ERR			0x10
/* SD Transfer Mode definition */
#define	SD_TM_NORMAL_WRITE		0x00
#define	SD_TM_AUTO_WRITE_3		0x01
#define	SD_TM_AUTO_WRITE_4		0x02
#define	SD_TM_AUTO_READ_3		0x05
#define	SD_TM_AUTO_READ_4		0x06
#define	SD_TM_CMD_RSP			0x08
#define	SD_TM_AUTO_WRITE_1		0x09
#define	SD_TM_AUTO_WRITE_2		0x0A
#define	SD_TM_NORMAL_READ		0x0C
#define	SD_TM_AUTO_READ_1		0x0D
#define	SD_TM_AUTO_READ_2		0x0E
#define	SD_TM_AUTO_TUNING		0x0F

/* SD_VPTX_CTL / SD_VPRX_CTL */
#define PHASE_CHANGE			0x80
#define PHASE_NOT_RESET			0x40

/* SD_DCMPS_TX_CTL / SD_DCMPS_RX_CTL */
#define DCMPS_CHANGE			0x80
#define DCMPS_CHANGE_DONE		0x40
#define DCMPS_ERROR			0x20
#define DCMPS_CURRENT_PHASE		0x1F

/* SD Configure 1 Register */
#define SD_CLK_DIVIDE_0			0x00
#define	SD_CLK_DIVIDE_256		0xC0
#define	SD_CLK_DIVIDE_128		0x80
#define	SD_BUS_WIDTH_1BIT		0x00
#define	SD_BUS_WIDTH_4BIT		0x01
#define	SD_BUS_WIDTH_8BIT		0x02
#define	SD_ASYNC_FIFO_NOT_RST		0x10
#define	SD_20_MODE			0x00
#define	SD_DDR_MODE			0x04
#define	SD_30_MODE			0x08

#define SD_CLK_DIVIDE_MASK		0xC0

/* SD_CMD_STATE */
#define SD_CMD_IDLE			0x80

/* SD_DATA_STATE */
#define SD_DATA_IDLE			0x80

/* DCM_DRP_CTL */
#define DCM_RESET			0x08
#define DCM_LOCKED			0x04
#define DCM_208M			0x00
#define DCM_TX			        0x01
#define DCM_RX			        0x02

/* DCM_DRP_TRIG */
#define DRP_START			0x80
#define DRP_DONE			0x40

/* DCM_DRP_CFG */
#define DRP_WRITE			0x80
#define DRP_READ			0x00
#define DCM_WRITE_ADDRESS_50		0x50
#define DCM_WRITE_ADDRESS_51		0x51
#define DCM_READ_ADDRESS_00		0x00
#define DCM_READ_ADDRESS_51		0x51

/* IRQSTAT0 */
#define DMA_DONE_INT			0x80
#define SUSPEND_INT			0x40
#define LINK_RDY_INT			0x20
#define LINK_DOWN_INT			0x10

/* DMACTL */
#define DMA_RST				0x80
#define DMA_BUSY			0x04
#define DMA_DIR_TO_CARD			0x00
#define DMA_DIR_FROM_CARD		0x02
#define DMA_EN				0x01
#define DMA_128				(0 << 4)
#define DMA_256				(1 << 4)
#define DMA_512				(2 << 4)
#define DMA_1024			(3 << 4)
#define DMA_PACK_SIZE_MASK		0x30

/* SSC_CTL1 */
#define SSC_RSTB			0x80
#define SSC_8X_EN			0x40
#define SSC_FIX_FRAC			0x20
#define SSC_SEL_1M			0x00
#define SSC_SEL_2M			0x08
#define SSC_SEL_4M			0x10
#define SSC_SEL_8M			0x18

/* SSC_CTL2 */
#define SSC_DEPTH_MASK			0x07
#define SSC_DEPTH_DISALBE		0x00
#define SSC_DEPTH_4M			0x01
#define SSC_DEPTH_2M			0x02
#define SSC_DEPTH_1M			0x03
#define SSC_DEPTH_500K			0x04
#define SSC_DEPTH_250K			0x05

/* System Clock Control Register */
#define CLK_LOW_FREQ			0x01

/* System Clock Divider Register */
#define CLK_DIV_1			0x01
#define CLK_DIV_2			0x02
#define CLK_DIV_4			0x03
#define CLK_DIV_8			0x04

/* MS_CFG */
#define	SAMPLE_TIME_RISING		0x00
#define	SAMPLE_TIME_FALLING		0x80
#define	PUSH_TIME_DEFAULT		0x00
#define	PUSH_TIME_ODD			0x40
#define	NO_EXTEND_TOGGLE		0x00
#define	EXTEND_TOGGLE_CHK		0x20
#define	MS_BUS_WIDTH_1			0x00
#define	MS_BUS_WIDTH_4			0x10
#define	MS_BUS_WIDTH_8			0x18
#define	MS_2K_SECTOR_MODE		0x04
#define	MS_512_SECTOR_MODE		0x00
#define	MS_TOGGLE_TIMEOUT_EN		0x00
#define	MS_TOGGLE_TIMEOUT_DISEN		0x01
#define MS_NO_CHECK_INT			0x02

/* MS_TRANS_CFG */
#define	WAIT_INT			0x80
#define	NO_WAIT_INT			0x00
#define	NO_AUTO_READ_INT_REG		0x00
#define	AUTO_READ_INT_REG		0x40
#define	MS_CRC16_ERR			0x20
#define	MS_RDY_TIMEOUT			0x10
#define	MS_INT_CMDNK			0x08
#define	MS_INT_BREQ			0x04
#define	MS_INT_ERR			0x02
#define	MS_INT_CED			0x01

/* MS_TRANSFER */
#define	MS_TRANSFER_START		0x80
#define	MS_TRANSFER_END			0x40
#define	MS_TRANSFER_ERR			0x20
#define	MS_BS_STATE			0x10
#define	MS_TM_READ_BYTES		0x00
#define	MS_TM_NORMAL_READ		0x01
#define	MS_TM_WRITE_BYTES		0x04
#define	MS_TM_NORMAL_WRITE		0x05
#define	MS_TM_AUTO_READ			0x08
#define	MS_TM_AUTO_WRITE		0x0C

/* SD Configure 2 Register */
#define	SD_CALCULATE_CRC7		0x00
#define	SD_NO_CALCULATE_CRC7		0x80
#define	SD_CHECK_CRC16			0x00
#define	SD_NO_CHECK_CRC16		0x40
#define SD_NO_CHECK_WAIT_CRC_TO		0x20
#define	SD_WAIT_BUSY_END		0x08
#define	SD_NO_WAIT_BUSY_END		0x00
#define	SD_CHECK_CRC7			0x00
#define	SD_NO_CHECK_CRC7		0x04
#define	SD_RSP_LEN_0			0x00
#define	SD_RSP_LEN_6			0x01
#define	SD_RSP_LEN_17			0x02
/* SD/MMC Response Type Definition */
#define	SD_RSP_TYPE_R0			0x04
#define	SD_RSP_TYPE_R1			0x01
#define	SD_RSP_TYPE_R1b			0x09
#define	SD_RSP_TYPE_R2			0x02
#define	SD_RSP_TYPE_R3			0x05
#define	SD_RSP_TYPE_R4			0x05
#define	SD_RSP_TYPE_R5			0x01
#define	SD_RSP_TYPE_R6			0x01
#define	SD_RSP_TYPE_R7			0x01

/* SD_CONFIGURE3 */
#define	SD_RSP_80CLK_TIMEOUT_EN		0x01

/* Card Transfer Reset Register */
#define SPI_STOP			0x01
#define XD_STOP				0x02
#define SD_STOP				0x04
#define MS_STOP				0x08
#define SPI_CLR_ERR			0x10
#define XD_CLR_ERR			0x20
#define SD_CLR_ERR			0x40
#define MS_CLR_ERR			0x80

/* Card Data Source Register */
#define PINGPONG_BUFFER			0x01
#define RING_BUFFER			0x00

/* Card Power Control Register */
#define PMOS_STRG_MASK			0x10
#define PMOS_STRG_800mA			0x10
#define PMOS_STRG_400mA			0x00
#define SD_POWER_OFF			0x03
#define SD_PARTIAL_POWER_ON		0x01
#define SD_POWER_ON			0x00
#define SD_POWER_MASK			0x03
#define MS_POWER_OFF			0x0C
#define MS_PARTIAL_POWER_ON		0x04
#define MS_POWER_ON			0x00
#define MS_POWER_MASK			0x0C
#define BPP_POWER_OFF			0x0F
#define BPP_POWER_5_PERCENT_ON		0x0E
#define BPP_POWER_10_PERCENT_ON		0x0C
#define BPP_POWER_15_PERCENT_ON		0x08
#define BPP_POWER_ON			0x00
#define BPP_POWER_MASK			0x0F
#define SD_VCC_PARTIAL_POWER_ON		0x02
#define SD_VCC_POWER_ON			0x00

/* PWR_GATE_CTRL */
#define PWR_GATE_EN			0x01
#define LDO3318_PWR_MASK		0x06
#define LDO_ON				0x00
#define LDO_SUSPEND			0x04
#define LDO_OFF				0x06

/* CARD_CLK_SOURCE */
#define CRC_FIX_CLK			(0x00 << 0)
#define CRC_VAR_CLK0			(0x01 << 0)
#define CRC_VAR_CLK1			(0x02 << 0)
#define SD30_FIX_CLK			(0x00 << 2)
#define SD30_VAR_CLK0			(0x01 << 2)
#define SD30_VAR_CLK1			(0x02 << 2)
#define SAMPLE_FIX_CLK			(0x00 << 4)
#define SAMPLE_VAR_CLK0			(0x01 << 4)
#define SAMPLE_VAR_CLK1			(0x02 << 4)

/* HOST_SLEEP_STATE */
#define HOST_ENTER_S1			1
#define HOST_ENTER_S3			2

#define MS_CFG				0xFD40
#define MS_TPC				0xFD41
#define MS_TRANS_CFG			0xFD42
#define MS_TRANSFER			0xFD43
#define MS_INT_REG			0xFD44
#define MS_BYTE_CNT			0xFD45
#define MS_SECTOR_CNT_L			0xFD46
#define MS_SECTOR_CNT_H			0xFD47
#define MS_DBUS_H			0xFD48

#define SD_CFG1				0xFDA0
#define SD_CFG2				0xFDA1
#define SD_CFG3				0xFDA2
#define SD_STAT1			0xFDA3
#define SD_STAT2			0xFDA4
#define SD_BUS_STAT			0xFDA5
#define SD_PAD_CTL			0xFDA6
#define SD_SAMPLE_POINT_CTL		0xFDA7
#define SD_PUSH_POINT_CTL		0xFDA8
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
#define SD_CMD_STATE			0xFDB5
#define SD_DATA_STATE			0xFDB6

#define SRCTL				0xFC13

#define	DCM_DRP_CTL			0xFC23
#define	DCM_DRP_TRIG			0xFC24
#define	DCM_DRP_CFG			0xFC25
#define	DCM_DRP_WR_DATA_L		0xFC26
#define	DCM_DRP_WR_DATA_H		0xFC27
#define	DCM_DRP_RD_DATA_L		0xFC28
#define	DCM_DRP_RD_DATA_H		0xFC29
#define SD_VPCLK0_CTL			0xFC2A
#define SD_VPCLK1_CTL			0xFC2B
#define SD_DCMPS0_CTL			0xFC2C
#define SD_DCMPS1_CTL			0xFC2D
#define SD_VPTX_CTL			SD_VPCLK0_CTL
#define SD_VPRX_CTL			SD_VPCLK1_CTL
#define SD_DCMPS_TX_CTL			SD_DCMPS0_CTL
#define SD_DCMPS_RX_CTL			SD_DCMPS1_CTL
#define CARD_CLK_SOURCE			0xFC2E

#define CARD_PWR_CTL			0xFD50
#define CARD_CLK_SWITCH			0xFD51
#define RTL8411B_PACKAGE_MODE		0xFD51
#define CARD_SHARE_MODE			0xFD52
#define CARD_DRIVE_SEL			0xFD53
#define CARD_STOP			0xFD54
#define CARD_OE				0xFD55
#define CARD_AUTO_BLINK			0xFD56
#define CARD_GPIO_DIR			0xFD57
#define CARD_GPIO			0xFD58
#define CARD_DATA_SOURCE		0xFD5B
#define SD30_CLK_DRIVE_SEL		0xFD5A
#define CARD_SELECT			0xFD5C
#define SD30_DRIVE_SEL			0xFD5E
#define SD30_CMD_DRIVE_SEL		0xFD5E
#define SD30_DAT_DRIVE_SEL		0xFD5F
#define CARD_CLK_EN			0xFD69
#define SDIO_CTRL			0xFD6B
#define CD_PAD_CTL			0xFD73

#define FPDCTL				0xFC00
#define PDINFO				0xFC01

#define CLK_CTL				0xFC02
#define CLK_DIV				0xFC03
#define CLK_SEL				0xFC04

#define SSC_DIV_N_0			0xFC0F
#define SSC_DIV_N_1			0xFC10
#define SSC_CTL1			0xFC11
#define SSC_CTL2			0xFC12

#define RCCTL				0xFC14

#define FPGA_PULL_CTL			0xFC1D
#define OLT_LED_CTL			0xFC1E
#define GPIO_CTL			0xFC1F

#define LDO_CTL				0xFC1E
#define SYS_VER				0xFC32

#define CARD_PULL_CTL1			0xFD60
#define CARD_PULL_CTL2			0xFD61
#define CARD_PULL_CTL3			0xFD62
#define CARD_PULL_CTL4			0xFD63
#define CARD_PULL_CTL5			0xFD64
#define CARD_PULL_CTL6			0xFD65

/* PCI Express Related Registers */
#define IRQEN0				0xFE20
#define IRQSTAT0			0xFE21
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
#define BCTL				0xFE2D
#define RBBC0				0xFE2E
#define RBBC1				0xFE2F
#define RBDAT				0xFE30
#define RBCTL				0xFE34
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
#define PETXCFG				0xFE49
#define LTR_CTL				0xFE4A
#define OBFF_CFG			0xFE4C

#define CDRESUMECTL			0xFE52
#define WAKE_SEL_CTL			0xFE54
#define PME_FORCE_CTL			0xFE56
#define ASPM_FORCE_CTL			0xFE57
#define PM_CLK_FORCE_CTL		0xFE58
#define FUNC_FORCE_CTL			0xFE59
#define PERST_GLITCH_WIDTH		0xFE5C
#define CHANGE_LINK_STATE		0xFE5B
#define RESET_LOAD_REG			0xFE5E
#define EFUSE_CONTENT			0xFE5F
#define HOST_SLEEP_STATE		0xFE60
#define SDIO_CFG			0xFE70

#define NFTS_TX_CTRL			0xFE72

#define PWR_GATE_CTRL			0xFE75
#define PWD_SUSPEND_EN			0xFE76
#define LDO_PWR_SEL			0xFE78

#define DUMMY_REG_RESET_0		0xFE90

#define AUTOLOAD_CFG_BASE		0xFF00

#define PM_CTRL1			0xFF44
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

/* Memory mapping */
#define SRAM_BASE			0xE600
#define RBUF_BASE			0xF400
#define PPBUF_BASE1			0xF800
#define PPBUF_BASE2			0xFA00
#define IMAGE_FLAG_ADDR0		0xCE80
#define IMAGE_FLAG_ADDR1		0xCE81

/* Phy register */
#define PHY_PCR				0x00
#define PHY_RCR0			0x01
#define PHY_RCR1			0x02
#define PHY_RCR2			0x03
#define PHY_RTCR			0x04
#define PHY_RDR				0x05
#define PHY_TCR0			0x06
#define PHY_TCR1			0x07
#define PHY_TUNE			0x08
#define PHY_IMR				0x09
#define PHY_BPCR			0x0A
#define PHY_BIST			0x0B
#define PHY_RAW_L			0x0C
#define PHY_RAW_H			0x0D
#define PHY_RAW_DATA			0x0E
#define PHY_HOST_CLK_CTRL		0x0F
#define PHY_DMR				0x10
#define PHY_BACR			0x11
#define PHY_IER				0x12
#define PHY_BCSR			0x13
#define PHY_BPR				0x14
#define PHY_BPNR2			0x15
#define PHY_BPNR			0x16
#define PHY_BRNR2			0x17
#define PHY_BENR			0x18
#define PHY_REG_REV			0x19
#define PHY_FLD0			0x1A
#define PHY_FLD1			0x1B
#define PHY_FLD2			0x1C
#define PHY_FLD3			0x1D
#define PHY_FLD4			0x1E
#define PHY_DUM_REG			0x1F

#define LCTLR				0x80
#define   LCTLR_EXT_SYNC		0x80
#define   LCTLR_COMMON_CLOCK_CFG	0x40
#define   LCTLR_RETRAIN_LINK		0x20
#define   LCTLR_LINK_DISABLE		0x10
#define   LCTLR_RCB			0x08
#define   LCTLR_RESERVED		0x04
#define   LCTLR_ASPM_CTL_MASK		0x03

#define PCR_SETTING_REG1		0x724
#define PCR_SETTING_REG2		0x814
#define PCR_SETTING_REG3		0x747

/* Phy bits */
#define PHY_PCR_FORCE_CODE			0xB000
#define PHY_PCR_OOBS_CALI_50			0x0800
#define PHY_PCR_OOBS_VCM_08			0x0200
#define PHY_PCR_OOBS_SEN_90			0x0040
#define PHY_PCR_RSSI_EN				0x0002

#define PHY_RCR1_ADP_TIME			0x0100
#define PHY_RCR1_VCO_COARSE			0x001F

#define PHY_RCR2_EMPHASE_EN			0x8000
#define PHY_RCR2_NADJR				0x4000
#define PHY_RCR2_CDR_CP_10			0x0400
#define PHY_RCR2_CDR_SR_2			0x0100
#define PHY_RCR2_FREQSEL_12			0x0040
#define PHY_RCR2_CPADJEN			0x0020
#define PHY_RCR2_CDR_SC_8			0x0008
#define PHY_RCR2_CALIB_LATE			0x0002

#define PHY_RDR_RXDSEL_1_9			0x4000

#define PHY_TUNE_TUNEREF_1_0			0x4000
#define PHY_TUNE_VBGSEL_1252			0x0C00
#define PHY_TUNE_SDBUS_33			0x0200
#define PHY_TUNE_TUNED18			0x01C0
#define PHY_TUNE_TUNED12			0X0020

#define PHY_BPCR_IBRXSEL			0x0400
#define PHY_BPCR_IBTXSEL			0x0100
#define PHY_BPCR_IB_FILTER			0x0080
#define PHY_BPCR_CMIRROR_EN			0x0040

#define PHY_REG_REV_RESV			0xE000
#define PHY_REG_REV_RXIDLE_LATCHED		0x1000
#define PHY_REG_REV_P1_EN			0x0800
#define PHY_REG_REV_RXIDLE_EN			0x0400
#define PHY_REG_REV_CLKREQ_DLY_TIMER_1_0	0x0040
#define PHY_REG_REV_STOP_CLKRD			0x0020
#define PHY_REG_REV_RX_PWST			0x0008
#define PHY_REG_REV_STOP_CLKWR			0x0004

#define PHY_FLD3_TIMER_4			0x7800
#define PHY_FLD3_TIMER_6			0x00E0
#define PHY_FLD3_RXDELINK			0x0004

#define PHY_FLD4_FLDEN_SEL			0x4000
#define PHY_FLD4_REQ_REF			0x2000
#define PHY_FLD4_RXAMP_OFF			0x1000
#define PHY_FLD4_REQ_ADDA			0x0800
#define PHY_FLD4_BER_COUNT			0x00E0
#define PHY_FLD4_BER_TIMER			0x000A
#define PHY_FLD4_BER_CHK_EN			0x0001

#define rtsx_pci_init_cmd(pcr)		((pcr)->ci = 0)

struct rtsx_pcr;

struct pcr_handle {
	struct rtsx_pcr			*pcr;
};

struct pcr_ops {
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
	void		(*force_power_down)(struct rtsx_pcr *pcr, u8 pm_state);
};

enum PDEV_STAT  {PDEV_STAT_IDLE, PDEV_STAT_RUN};

struct rtsx_pcr {
	struct pci_dev			*pci;
	unsigned int			id;

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
	struct delayed_work		idle_work;

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

	int				num_slots;
	struct rtsx_slot		*slots;
};

#define CHK_PCI_PID(pcr, pid)		((pcr)->pci->device == (pid))
#define PCI_VID(pcr)			((pcr)->pci->vendor)
#define PCI_PID(pcr)			((pcr)->pci->device)

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
int rtsx_pci_transfer_data(struct rtsx_pcr *pcr, struct scatterlist *sglist,
		int num_sg, bool read, int timeout);
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

static inline int rtsx_pci_update_cfg_byte(struct rtsx_pcr *pcr, int addr,
		u8 mask, u8 append)
{
	int err;
	u8 val;

	err = pci_read_config_byte(pcr->pci, addr, &val);
	if (err < 0)
		return err;
	return pci_write_config_byte(pcr->pci, addr, (val & mask) | append);
}

static inline void rtsx_pci_write_be32(struct rtsx_pcr *pcr, u16 reg, u32 val)
{
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg,     0xFF, val >> 24);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg + 1, 0xFF, val >> 16);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg + 2, 0xFF, val >> 8);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, reg + 3, 0xFF, val);
}

#endif
