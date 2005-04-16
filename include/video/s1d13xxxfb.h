/* drivers/video/s1d3xxxfb.h
 *
 * (c) 2004 Simtec Electronics
 * (c) 2005 Thibaut VARENE <varenet@parisc-linux.org>
 *
 * Header file for Epson S1D13XXX driver code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef	S1D13XXXFB_H
#define	S1D13XXXFB_H

#define S1D_PALETTE_SIZE		256
#define S1D_CHIP_REV			7	/* expected chip revision number for s1d13806 */
#define S1D_FBID			"S1D13806"
#define S1D_DEVICENAME			"s1d13806fb"

/* register definitions (tested on s1d13896) */
#define S1DREG_REV_CODE			0x0000	/* Revision Code Register */
#define S1DREG_MISC			0x0001	/* Miscellaneous Register */
#define S1DREG_GPIO_CNF0		0x0004	/* General IO Pins Configuration Register 0 */
#define S1DREG_GPIO_CNF1		0x0005	/* General IO Pins Configuration Register 1 */
#define S1DREG_GPIO_CTL0		0x0008	/* General IO Pins Control Register 0 */
#define S1DREG_GPIO_CTL1		0x0009	/* General IO Pins Control Register 1 */
#define S1DREG_CNF_STATUS		0x000C	/* Configuration Status Readback Register */
#define S1DREG_CLK_CNF			0x0010	/* Memory Clock Configuration Register */
#define S1DREG_LCD_CLK_CNF		0x0014	/* LCD Pixel Clock Configuration Register */
#define S1DREG_CRT_CLK_CNF		0x0018	/* CRT/TV Pixel Clock Configuration Register */
#define S1DREG_MPLUG_CLK_CNF		0x001C	/* MediaPlug Clock Configuration Register */
#define S1DREG_CPU2MEM_WST_SEL		0x001E	/* CPU To Memory Wait State Select Register */
#define S1DREG_MEM_CNF			0x0020	/* Memory Configuration Register */
#define S1DREG_SDRAM_REF_RATE		0x0021	/* SDRAM Refresh Rate Register */
#define S1DREG_SDRAM_TC0		0x002A	/* SDRAM Timing Control Register 0 */
#define S1DREG_SDRAM_TC1		0x002B	/* SDRAM Timing Control Register 1 */
#define S1DREG_PANEL_TYPE		0x0030	/* Panel Type Register */
#define S1DREG_MOD_RATE			0x0031	/* MOD Rate Register */
#define S1DREG_LCD_DISP_HWIDTH		0x0032	/* LCD Horizontal Display Width Register: ((val)+1)*8)=pix/line */
#define S1DREG_LCD_NDISP_HPER		0x0034	/* LCD Horizontal Non-Display Period Register: ((val)+1)*8)=NDpix/line */
#define S1DREG_TFT_FPLINE_START		0x0035	/* TFT FPLINE Start Position Register */
#define S1DREG_TFT_FPLINE_PWIDTH	0x0036	/* TFT FPLINE Pulse Width Register. */
#define S1DREG_LCD_DISP_VHEIGHT0	0x0038	/* LCD Vertical Display Height Register 0 */
#define S1DREG_LCD_DISP_VHEIGHT1	0x0039	/* LCD Vertical Display Height Register 1 */
#define S1DREG_LCD_NDISP_VPER		0x003A	/* LCD Vertical Non-Display Period Register: (val)+1=NDlines */
#define S1DREG_TFT_FPFRAME_START	0x003B	/* TFT FPFRAME Start Position Register */
#define S1DREG_TFT_FPFRAME_PWIDTH	0x003C	/* TFT FPFRAME Pulse Width Register */
#define S1DREG_LCD_DISP_MODE		0x0040	/* LCD Display Mode Register */
#define S1DREG_LCD_MISC			0x0041	/* LCD Miscellaneous Register */
#define S1DREG_LCD_DISP_START0		0x0042	/* LCD Display Start Address Register 0 */
#define S1DREG_LCD_DISP_START1		0x0043	/* LCD Display Start Address Register 1 */
#define S1DREG_LCD_DISP_START2		0x0044	/* LCD Display Start Address Register 2 */
#define S1DREG_LCD_MEM_OFF0		0x0046	/* LCD Memory Address Offset Register 0 */
#define S1DREG_LCD_MEM_OFF1		0x0047	/* LCD Memory Address Offset Register 1 */
#define S1DREG_LCD_PIX_PAN		0x0048	/* LCD Pixel Panning Register */
#define S1DREG_LCD_DISP_FIFO_HTC	0x004A	/* LCD Display FIFO High Threshold Control Register */
#define S1DREG_LCD_DISP_FIFO_LTC	0x004B	/* LCD Display FIFO Low Threshold Control Register */
#define S1DREG_CRT_DISP_HWIDTH		0x0050	/* CRT/TV Horizontal Display Width Register: ((val)+1)*8)=pix/line */
#define S1DREG_CRT_NDISP_HPER		0x0052	/* CRT/TV Horizontal Non-Display Period Register */
#define S1DREG_CRT_HRTC_START		0x0053	/* CRT/TV HRTC Start Position Register */
#define S1DREG_CRT_HRTC_PWIDTH		0x0054	/* CRT/TV HRTC Pulse Width Register */
#define S1DREG_CRT_DISP_VHEIGHT0	0x0056	/* CRT/TV Vertical Display Height Register 0 */
#define S1DREG_CRT_DISP_VHEIGHT1	0x0057	/* CRT/TV Vertical Display Height Register 1 */
#define S1DREG_CRT_NDISP_VPER		0x0058	/* CRT/TV Vertical Non-Display Period Register */
#define S1DREG_CRT_VRTC_START		0x0059	/* CRT/TV VRTC Start Position Register */
#define S1DREG_CRT_VRTC_PWIDTH		0x005A	/* CRT/TV VRTC Pulse Width Register */
#define S1DREG_TV_OUT_CTL		0x005B	/* TV Output Control Register */
#define S1DREG_CRT_DISP_MODE		0x0060	/* CRT/TV Display Mode Register */
#define S1DREG_CRT_DISP_START0		0x0062	/* CRT/TV Display Start Address Register 0 */
#define S1DREG_CRT_DISP_START1		0x0063	/* CRT/TV Display Start Address Register 1 */
#define S1DREG_CRT_DISP_START2		0x0064	/* CRT/TV Display Start Address Register 2 */
#define S1DREG_CRT_MEM_OFF0		0x0066	/* CRT/TV Memory Address Offset Register 0 */
#define S1DREG_CRT_MEM_OFF1		0x0067	/* CRT/TV Memory Address Offset Register 1 */
#define S1DREG_CRT_PIX_PAN		0x0068	/* CRT/TV Pixel Panning Register */
#define S1DREG_CRT_DISP_FIFO_HTC	0x006A	/* CRT/TV Display FIFO High Threshold Control Register */
#define S1DREG_CRT_DISP_FIFO_LTC	0x006B	/* CRT/TV Display FIFO Low Threshold Control Register */
#define S1DREG_LCD_CUR_CTL		0x0070	/* LCD Ink/Cursor Control Register */
#define S1DREG_LCD_CUR_START		0x0071	/* LCD Ink/Cursor Start Address Register */
#define S1DREG_LCD_CUR_XPOS0		0x0072	/* LCD Cursor X Position Register 0 */
#define S1DREG_LCD_CUR_XPOS1		0x0073	/* LCD Cursor X Position Register 1 */
#define S1DREG_LCD_CUR_YPOS0		0x0074	/* LCD Cursor Y Position Register 0 */
#define S1DREG_LCD_CUR_YPOS1		0x0075	/* LCD Cursor Y Position Register 1 */
#define S1DREG_LCD_CUR_BCTL0		0x0076	/* LCD Ink/Cursor Blue Color 0 Register */
#define S1DREG_LCD_CUR_GCTL0		0x0077	/* LCD Ink/Cursor Green Color 0 Register */
#define S1DREG_LCD_CUR_RCTL0		0x0078	/* LCD Ink/Cursor Red Color 0 Register */
#define S1DREG_LCD_CUR_BCTL1		0x007A	/* LCD Ink/Cursor Blue Color 1 Register */
#define S1DREG_LCD_CUR_GCTL1		0x007B	/* LCD Ink/Cursor Green Color 1 Register */
#define S1DREG_LCD_CUR_RCTL1		0x007C	/* LCD Ink/Cursor Red Color 1 Register */
#define S1DREG_LCD_CUR_FIFO_HTC		0x007E	/* LCD Ink/Cursor FIFO High Threshold Register */
#define S1DREG_CRT_CUR_CTL		0x0080	/* CRT/TV Ink/Cursor Control Register */
#define S1DREG_CRT_CUR_START		0x0081	/* CRT/TV Ink/Cursor Start Address Register */
#define S1DREG_CRT_CUR_XPOS0		0x0082	/* CRT/TV Cursor X Position Register 0 */
#define S1DREG_CRT_CUR_XPOS1		0x0083	/* CRT/TV Cursor X Position Register 1 */
#define S1DREG_CRT_CUR_YPOS0		0x0084	/* CRT/TV Cursor Y Position Register 0 */
#define S1DREG_CRT_CUR_YPOS1		0x0085	/* CRT/TV Cursor Y Position Register 1 */
#define S1DREG_CRT_CUR_BCTL0		0x0086	/* CRT/TV Ink/Cursor Blue Color 0 Register */
#define S1DREG_CRT_CUR_GCTL0		0x0087	/* CRT/TV Ink/Cursor Green Color 0 Register */
#define S1DREG_CRT_CUR_RCTL0		0x0088	/* CRT/TV Ink/Cursor Red Color 0 Register */
#define S1DREG_CRT_CUR_BCTL1		0x008A	/* CRT/TV Ink/Cursor Blue Color 1 Register */
#define S1DREG_CRT_CUR_GCTL1		0x008B	/* CRT/TV Ink/Cursor Green Color 1 Register */
#define S1DREG_CRT_CUR_RCTL1		0x008C	/* CRT/TV Ink/Cursor Red Color 1 Register */
#define S1DREG_CRT_CUR_FIFO_HTC		0x008E	/* CRT/TV Ink/Cursor FIFO High Threshold Register */
#define S1DREG_BBLT_CTL0		0x0100	/* BitBLT Control Register 0 */
#define S1DREG_BBLT_CTL1		0x0101	/* BitBLT Control Register 1 */
#define S1DREG_BBLT_CC_EXP		0x0102	/* BitBLT Code/Color Expansion Register */
#define S1DREG_BBLT_OP			0x0103	/* BitBLT Operation Register */
#define S1DREG_BBLT_SRC_START0		0x0104	/* BitBLT Source Start Address Register 0 */
#define S1DREG_BBLT_SRC_START1		0x0105	/* BitBLT Source Start Address Register 1 */
#define S1DREG_BBLT_SRC_START2		0x0106	/* BitBLT Source Start Address Register 2 */
#define S1DREG_BBLT_DST_START0		0x0108	/* BitBLT Destination Start Address Register 0 */
#define S1DREG_BBLT_DST_START1		0x0109	/* BitBLT Destination Start Address Register 1 */
#define S1DREG_BBLT_DST_START2		0x010A	/* BitBLT Destination Start Address Register 2 */
#define S1DREG_BBLT_MEM_OFF0		0x010C	/* BitBLT Memory Address Offset Register 0 */
#define S1DREG_BBLT_MEM_OFF1		0x010D	/* BitBLT Memory Address Offset Register 1 */
#define S1DREG_BBLT_WIDTH0		0x0110	/* BitBLT Width Register 0 */
#define S1DREG_BBLT_WIDTH1		0x0111	/* BitBLT Width Register 1 */
#define S1DREG_BBLT_HEIGHT0		0x0112	/* BitBLT Height Register 0 */
#define S1DREG_BBLT_HEIGHT1		0x0113	/* BitBLT Height Register 1 */
#define S1DREG_BBLT_BGC0		0x0114	/* BitBLT Background Color Register 0 */
#define S1DREG_BBLT_BGC1		0x0115	/* BitBLT Background Color Register 1 */
#define S1DREG_BBLT_FGC0		0x0118	/* BitBLT Foreground Color Register 0 */
#define S1DREG_BBLT_FGC1		0x0119	/* BitBLT Foreground Color Register 1 */
#define S1DREG_LKUP_MODE		0x01E0	/* Look-Up Table Mode Register */
#define S1DREG_LKUP_ADDR		0x01E2	/* Look-Up Table Address Register */
#define S1DREG_LKUP_DATA		0x01E4	/* Look-Up Table Data Register */
#define S1DREG_PS_CNF			0x01F0	/* Power Save Configuration Register */
#define S1DREG_PS_STATUS		0x01F1	/* Power Save Status Register */
#define S1DREG_CPU2MEM_WDOGT		0x01F4	/* CPU-to-Memory Access Watchdog Timer Register */
#define S1DREG_COM_DISP_MODE		0x01FC	/* Common Display Mode Register */

#define S1DREG_DELAYOFF			0xFFFE
#define S1DREG_DELAYON			0xFFFF

/* Note: all above defines should go in separate header files
   when implementing other S1D13xxx chip support. */

struct s1d13xxxfb_regval {
	u16	addr;
	u8	value;
};


struct s1d13xxxfb_par {
	void __iomem	*regs;
	unsigned char	display;

	unsigned int	pseudo_palette[16];
#ifdef CONFIG_PM
	void		*regs_save;	/* pm saves all registers here */
	void		*disp_save;	/* pm saves entire screen here */
#endif
};

struct s1d13xxxfb_pdata {
	const struct s1d13xxxfb_regval	*initregs;
	const unsigned int		initregssize;
	void				(*platform_init_video)(void);
#ifdef CONFIG_PM
	int				(*platform_suspend_video)(void);
	int				(*platform_resume_video)(void);
#endif
};

#endif

