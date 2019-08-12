/* SPDX-License-Identifier: GPL-2.0-only */
/* Driver for Realtek RTS5139 USB card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Roger Tseng <rogerable@realtek.com>
 */

#ifndef __RTSX_USB_H
#define __RTSX_USB_H

#include <linux/usb.h>

/* related module names */
#define RTSX_USB_SD_CARD	0
#define RTSX_USB_MS_CARD	1

/* endpoint numbers */
#define EP_BULK_OUT		1
#define EP_BULK_IN		2
#define EP_INTR_IN		3

/* USB vendor requests */
#define RTSX_USB_REQ_REG_OP	0x00
#define RTSX_USB_REQ_POLL	0x02

/* miscellaneous parameters */
#define MIN_DIV_N		60
#define MAX_DIV_N		120

#define MAX_PHASE		15
#define RX_TUNING_CNT		3

#define QFN24			0
#define LQFP48			1
#define CHECK_PKG(ucr, pkg)	((ucr)->package == (pkg))

/* data structures */
struct rtsx_ucr {
	u16			vendor_id;
	u16			product_id;

	int			package;
	u8			ic_version;
	bool			is_rts5179;

	unsigned int		cur_clk;

	u8			*cmd_buf;
	unsigned int		cmd_idx;
	u8			*rsp_buf;

	struct usb_device	*pusb_dev;
	struct usb_interface	*pusb_intf;
	struct usb_sg_request	current_sg;
	unsigned char		*iobuf;
	dma_addr_t		iobuf_dma;

	struct timer_list	sg_timer;
	struct mutex		dev_mutex;
};

/* buffer size */
#define IOBUF_SIZE		1024

/* prototypes of exported functions */
extern int rtsx_usb_get_card_status(struct rtsx_ucr *ucr, u16 *status);

extern int rtsx_usb_read_register(struct rtsx_ucr *ucr, u16 addr, u8 *data);
extern int rtsx_usb_write_register(struct rtsx_ucr *ucr, u16 addr, u8 mask,
		u8 data);

extern int rtsx_usb_ep0_write_register(struct rtsx_ucr *ucr, u16 addr, u8 mask,
		u8 data);
extern int rtsx_usb_ep0_read_register(struct rtsx_ucr *ucr, u16 addr,
		u8 *data);

extern void rtsx_usb_add_cmd(struct rtsx_ucr *ucr, u8 cmd_type,
		u16 reg_addr, u8 mask, u8 data);
extern int rtsx_usb_send_cmd(struct rtsx_ucr *ucr, u8 flag, int timeout);
extern int rtsx_usb_get_rsp(struct rtsx_ucr *ucr, int rsp_len, int timeout);
extern int rtsx_usb_transfer_data(struct rtsx_ucr *ucr, unsigned int pipe,
			      void *buf, unsigned int len, int use_sg,
			      unsigned int *act_len, int timeout);

extern int rtsx_usb_read_ppbuf(struct rtsx_ucr *ucr, u8 *buf, int buf_len);
extern int rtsx_usb_write_ppbuf(struct rtsx_ucr *ucr, u8 *buf, int buf_len);
extern int rtsx_usb_switch_clock(struct rtsx_ucr *ucr, unsigned int card_clock,
		u8 ssc_depth, bool initial_mode, bool double_clk, bool vpclk);
extern int rtsx_usb_card_exclusive_check(struct rtsx_ucr *ucr, int card);

/* card status */
#define SD_CD		0x01
#define MS_CD		0x02
#define XD_CD		0x04
#define CD_MASK		(SD_CD | MS_CD | XD_CD)
#define SD_WP		0x08

/* reader command field offset & parameters */
#define READ_REG_CMD		0
#define WRITE_REG_CMD		1
#define CHECK_REG_CMD		2

#define PACKET_TYPE		4
#define CNT_H			5
#define CNT_L			6
#define STAGE_FLAG		7
#define CMD_OFFSET		8
#define SEQ_WRITE_DATA_OFFSET	12

#define BATCH_CMD		0
#define SEQ_READ		1
#define SEQ_WRITE		2

#define STAGE_R			0x01
#define STAGE_DI		0x02
#define STAGE_DO		0x04
#define STAGE_MS_STATUS		0x08
#define STAGE_XD_STATUS		0x10
#define MODE_C			0x00
#define MODE_CR			(STAGE_R)
#define MODE_CDIR		(STAGE_R | STAGE_DI)
#define MODE_CDOR		(STAGE_R | STAGE_DO)

#define EP0_OP_SHIFT		14
#define EP0_READ_REG_CMD	2
#define EP0_WRITE_REG_CMD	3

#define rtsx_usb_cmd_hdr_tag(ucr)		\
	do {					\
		ucr->cmd_buf[0] = 'R';		\
		ucr->cmd_buf[1] = 'T';		\
		ucr->cmd_buf[2] = 'C';		\
		ucr->cmd_buf[3] = 'R';		\
	} while (0)

static inline void rtsx_usb_init_cmd(struct rtsx_ucr *ucr)
{
	rtsx_usb_cmd_hdr_tag(ucr);
	ucr->cmd_idx = 0;
	ucr->cmd_buf[PACKET_TYPE] = BATCH_CMD;
}

/* internal register address */
#define FPDCTL				0xFC00
#define SSC_DIV_N_0			0xFC07
#define SSC_CTL1			0xFC09
#define SSC_CTL2			0xFC0A
#define CFG_MODE			0xFC0E
#define CFG_MODE_1			0xFC0F
#define RCCTL				0xFC14
#define SOF_WDOG			0xFC28
#define SYS_DUMMY0			0xFC30

#define MS_BLKEND			0xFD30
#define MS_READ_START			0xFD31
#define MS_READ_COUNT			0xFD32
#define MS_WRITE_START			0xFD33
#define MS_WRITE_COUNT			0xFD34
#define MS_COMMAND			0xFD35
#define MS_OLD_BLOCK_0			0xFD36
#define MS_OLD_BLOCK_1			0xFD37
#define MS_NEW_BLOCK_0			0xFD38
#define MS_NEW_BLOCK_1			0xFD39
#define MS_LOG_BLOCK_0			0xFD3A
#define MS_LOG_BLOCK_1			0xFD3B
#define MS_BUS_WIDTH			0xFD3C
#define MS_PAGE_START			0xFD3D
#define MS_PAGE_LENGTH			0xFD3E
#define MS_CFG				0xFD40
#define MS_TPC				0xFD41
#define MS_TRANS_CFG			0xFD42
#define MS_TRANSFER			0xFD43
#define MS_INT_REG			0xFD44
#define MS_BYTE_CNT			0xFD45
#define MS_SECTOR_CNT_L			0xFD46
#define MS_SECTOR_CNT_H			0xFD47
#define MS_DBUS_H			0xFD48

#define CARD_DMA1_CTL			0xFD5C
#define CARD_PULL_CTL1			0xFD60
#define CARD_PULL_CTL2			0xFD61
#define CARD_PULL_CTL3			0xFD62
#define CARD_PULL_CTL4			0xFD63
#define CARD_PULL_CTL5			0xFD64
#define CARD_PULL_CTL6			0xFD65
#define CARD_EXIST			0xFD6F
#define CARD_INT_PEND			0xFD71

#define LDO_POWER_CFG			0xFD7B

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
#define SD_VPCLK0_CTL			0xFC2A
#define SD_VPCLK1_CTL			0xFC2B
#define SD_DCMPS0_CTL			0xFC2C
#define SD_DCMPS1_CTL			0xFC2D

#define CARD_DMA1_CTL			0xFD5C

#define HW_VERSION			0xFC01

#define SSC_CLK_FPGA_SEL		0xFC02
#define CLK_DIV				0xFC03
#define SFSM_ED				0xFC04

#define CD_DEGLITCH_WIDTH		0xFC20
#define CD_DEGLITCH_EN			0xFC21
#define AUTO_DELINK_EN			0xFC23

#define FPGA_PULL_CTL			0xFC1D
#define CARD_CLK_SOURCE			0xFC2E

#define CARD_SHARE_MODE			0xFD51
#define CARD_DRIVE_SEL			0xFD52
#define CARD_STOP			0xFD53
#define CARD_OE				0xFD54
#define CARD_AUTO_BLINK			0xFD55
#define CARD_GPIO			0xFD56
#define SD30_DRIVE_SEL			0xFD57

#define CARD_DATA_SOURCE		0xFD5D
#define CARD_SELECT			0xFD5E

#define CARD_CLK_EN			0xFD79
#define CARD_PWR_CTL			0xFD7A

#define OCPCTL				0xFD80
#define OCPPARA1			0xFD81
#define OCPPARA2			0xFD82
#define OCPSTAT				0xFD83

#define HS_USB_STAT			0xFE01
#define HS_VCONTROL			0xFE26
#define HS_VSTAIN			0xFE27
#define HS_VLOADM			0xFE28
#define HS_VSTAOUT			0xFE29

#define MC_IRQ				0xFF00
#define MC_IRQEN			0xFF01
#define MC_FIFO_CTL			0xFF02
#define MC_FIFO_BC0			0xFF03
#define MC_FIFO_BC1			0xFF04
#define MC_FIFO_STAT			0xFF05
#define MC_FIFO_MODE			0xFF06
#define MC_FIFO_RD_PTR0			0xFF07
#define MC_FIFO_RD_PTR1			0xFF08
#define MC_DMA_CTL			0xFF10
#define MC_DMA_TC0			0xFF11
#define MC_DMA_TC1			0xFF12
#define MC_DMA_TC2			0xFF13
#define MC_DMA_TC3			0xFF14
#define MC_DMA_RST			0xFF15

#define RBUF_SIZE_MASK			0xFBFF
#define RBUF_BASE			0xF000
#define PPBUF_BASE1			0xF800
#define PPBUF_BASE2			0xFA00

/* internal register value macros */
#define POWER_OFF			0x03
#define PARTIAL_POWER_ON		0x02
#define POWER_ON			0x00
#define POWER_MASK			0x03
#define LDO3318_PWR_MASK		0x0C
#define LDO_ON				0x00
#define LDO_SUSPEND			0x08
#define LDO_OFF				0x0C
#define DV3318_AUTO_PWR_OFF		0x10
#define FORCE_LDO_POWERB		0x60

/* LDO_POWER_CFG */
#define TUNE_SD18_MASK			0x1C
#define TUNE_SD18_1V7			0x00
#define TUNE_SD18_1V8			(0x01 << 2)
#define TUNE_SD18_1V9			(0x02 << 2)
#define TUNE_SD18_2V0			(0x03 << 2)
#define TUNE_SD18_2V7			(0x04 << 2)
#define TUNE_SD18_2V8			(0x05 << 2)
#define TUNE_SD18_2V9			(0x06 << 2)
#define TUNE_SD18_3V3			(0x07 << 2)

/* CLK_DIV */
#define CLK_CHANGE			0x80
#define CLK_DIV_1			0x00
#define CLK_DIV_2			0x01
#define CLK_DIV_4			0x02
#define CLK_DIV_8			0x03

#define SSC_POWER_MASK			0x01
#define SSC_POWER_DOWN			0x01
#define SSC_POWER_ON			0x00

#define FPGA_VER			0x80
#define HW_VER_MASK			0x0F

#define EXTEND_DMA1_ASYNC_SIGNAL	0x02

/* CFG_MODE*/
#define XTAL_FREE			0x80
#define CLK_MODE_MASK			0x03
#define CLK_MODE_12M_XTAL		0x00
#define CLK_MODE_NON_XTAL		0x01
#define CLK_MODE_24M_OSC		0x02
#define CLK_MODE_48M_OSC		0x03

/* CFG_MODE_1*/
#define RTS5179				0x02

#define NYET_EN				0x01
#define NYET_MSAK			0x01

#define SD30_DRIVE_MASK			0x07
#define SD20_DRIVE_MASK			0x03

#define DISABLE_SD_CD			0x08
#define DISABLE_MS_CD			0x10
#define DISABLE_XD_CD			0x20
#define SD_CD_DEGLITCH_EN		0x01
#define MS_CD_DEGLITCH_EN		0x02
#define XD_CD_DEGLITCH_EN		0x04

#define	CARD_SHARE_LQFP48		0x04
#define	CARD_SHARE_QFN24		0x00
#define CARD_SHARE_LQFP_SEL		0x04
#define	CARD_SHARE_XD			0x00
#define	CARD_SHARE_SD			0x01
#define	CARD_SHARE_MS			0x02
#define CARD_SHARE_MASK			0x03


/* SD30_DRIVE_SEL */
#define DRIVER_TYPE_A			0x05
#define DRIVER_TYPE_B			0x03
#define DRIVER_TYPE_C			0x02
#define DRIVER_TYPE_D			0x01

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

/* CARD_CLK_EN */
#define SD_CLK_EN			0x04
#define MS_CLK_EN			0x08

/* CARD_SELECT */
#define SD_MOD_SEL			2
#define MS_MOD_SEL			3

/* CARD_SHARE_MODE */
#define	CARD_SHARE_LQFP48		0x04
#define	CARD_SHARE_QFN24		0x00
#define CARD_SHARE_LQFP_SEL		0x04
#define	CARD_SHARE_XD			0x00
#define	CARD_SHARE_SD			0x01
#define	CARD_SHARE_MS			0x02
#define CARD_SHARE_MASK			0x03

/* SSC_CTL1 */
#define SSC_RSTB			0x80
#define SSC_8X_EN			0x40
#define SSC_FIX_FRAC			0x20
#define SSC_SEL_1M			0x00
#define SSC_SEL_2M			0x08
#define SSC_SEL_4M			0x10
#define SSC_SEL_8M			0x18

/* SSC_CTL2 */
#define SSC_DEPTH_MASK			0x03
#define SSC_DEPTH_DISALBE		0x00
#define SSC_DEPTH_2M			0x01
#define SSC_DEPTH_1M			0x02
#define SSC_DEPTH_512K			0x03

/* SD_VPCLK0_CTL */
#define PHASE_CHANGE			0x80
#define PHASE_NOT_RESET			0x40

/* SD_TRANSFER */
#define	SD_TRANSFER_START		0x80
#define	SD_TRANSFER_END			0x40
#define SD_STAT_IDLE			0x20
#define	SD_TRANSFER_ERR			0x10
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

/* SD_CFG1 */
#define SD_CLK_DIVIDE_0			0x00
#define	SD_CLK_DIVIDE_256		0xC0
#define	SD_CLK_DIVIDE_128		0x80
#define SD_CLK_DIVIDE_MASK		0xC0
#define	SD_BUS_WIDTH_1BIT		0x00
#define	SD_BUS_WIDTH_4BIT		0x01
#define	SD_BUS_WIDTH_8BIT		0x02
#define	SD_ASYNC_FIFO_RST		0x10
#define	SD_20_MODE			0x00
#define	SD_DDR_MODE			0x04
#define	SD_30_MODE			0x08

/* SD_CFG2 */
#define	SD_CALCULATE_CRC7		0x00
#define	SD_NO_CALCULATE_CRC7		0x80
#define	SD_CHECK_CRC16			0x00
#define	SD_NO_CHECK_CRC16		0x40
#define SD_WAIT_CRC_TO_EN		0x20
#define	SD_WAIT_BUSY_END		0x08
#define	SD_NO_WAIT_BUSY_END		0x00
#define	SD_CHECK_CRC7			0x00
#define	SD_NO_CHECK_CRC7		0x04
#define	SD_RSP_LEN_0			0x00
#define	SD_RSP_LEN_6			0x01
#define	SD_RSP_LEN_17			0x02
#define	SD_RSP_TYPE_R0			0x04
#define	SD_RSP_TYPE_R1			0x01
#define	SD_RSP_TYPE_R1b			0x09
#define	SD_RSP_TYPE_R2			0x02
#define	SD_RSP_TYPE_R3			0x05
#define	SD_RSP_TYPE_R4			0x05
#define	SD_RSP_TYPE_R5			0x01
#define	SD_RSP_TYPE_R6			0x01
#define	SD_RSP_TYPE_R7			0x01

/* SD_STAT1 */
#define	SD_CRC7_ERR			0x80
#define	SD_CRC16_ERR			0x40
#define	SD_CRC_WRITE_ERR		0x20
#define	SD_CRC_WRITE_ERR_MASK		0x1C
#define	GET_CRC_TIME_OUT		0x02
#define	SD_TUNING_COMPARE_ERR		0x01

/* SD_DATA_STATE */
#define SD_DATA_IDLE			0x80

/* CARD_DATA_SOURCE */
#define PINGPONG_BUFFER			0x01
#define RING_BUFFER			0x00

/* CARD_OE */
#define SD_OUTPUT_EN			0x04
#define MS_OUTPUT_EN			0x08

/* CARD_STOP */
#define SD_STOP				0x04
#define MS_STOP				0x08
#define SD_CLR_ERR			0x40
#define MS_CLR_ERR			0x80

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
#define MS_TM_SET_CMD			0x06
#define MS_TM_COPY_PAGE			0x07
#define MS_TM_MULTI_READ		0x02
#define MS_TM_MULTI_WRITE		0x03

/* MC_FIFO_CTL */
#define FIFO_FLUSH			0x01

/* MC_DMA_RST */
#define DMA_RESET  0x01

/* MC_DMA_CTL */
#define DMA_TC_EQ_0			0x80
#define DMA_DIR_TO_CARD			0x00
#define DMA_DIR_FROM_CARD		0x02
#define DMA_EN				0x01
#define DMA_128				(0 << 2)
#define DMA_256				(1 << 2)
#define DMA_512				(2 << 2)
#define DMA_1024			(3 << 2)
#define DMA_PACK_SIZE_MASK		0x0C

/* CARD_INT_PEND */
#define XD_INT				0x10
#define MS_INT				0x08
#define SD_INT				0x04

/* LED operations*/
static inline int rtsx_usb_turn_on_led(struct rtsx_ucr *ucr)
{
	return  rtsx_usb_ep0_write_register(ucr, CARD_GPIO, 0x03, 0x02);
}

static inline int rtsx_usb_turn_off_led(struct rtsx_ucr *ucr)
{
	return rtsx_usb_ep0_write_register(ucr, CARD_GPIO, 0x03, 0x03);
}

/* HW error clearing */
static inline void rtsx_usb_clear_fsm_err(struct rtsx_ucr *ucr)
{
	rtsx_usb_ep0_write_register(ucr, SFSM_ED, 0xf8, 0xf8);
}

static inline void rtsx_usb_clear_dma_err(struct rtsx_ucr *ucr)
{
	rtsx_usb_ep0_write_register(ucr, MC_FIFO_CTL,
			FIFO_FLUSH, FIFO_FLUSH);
	rtsx_usb_ep0_write_register(ucr, MC_DMA_RST, DMA_RESET, DMA_RESET);
}
#endif /* __RTS51139_H */
