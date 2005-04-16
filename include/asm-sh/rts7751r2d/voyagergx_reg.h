/* -------------------------------------------------------------------- */
/* voyagergx_reg.h                                                      */
/* -------------------------------------------------------------------- */
/*  This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Copyright 2003 (c) Lineo uSolutions,Inc.
*/
/* -------------------------------------------------------------------- */

#ifndef _VOYAGER_GX_REG_H
#define _VOYAGER_GX_REG_H

#define VOYAGER_BASE			0xb3e00000
#define VOYAGER_USBH_BASE		(0x40000 + VOYAGER_BASE)
#define VOYAGER_UART_BASE		(0x30000 + VOYAGER_BASE)
#define	VOYAGER_AC97_BASE		(0xa0000 + VOYAGER_BASE)

#define VOYAGER_IRQ_NUM			32
#define VOYAGER_IRQ_BASE		50
#define VOYAGER_USBH_IRQ		VOYAGER_IRQ_BASE + 6
#define VOYAGER_8051_IRQ		VOYAGER_IRQ_BASE + 10
#define VOYAGER_UART0_IRQ		VOYAGER_IRQ_BASE + 12
#define VOYAGER_UART1_IRQ		VOYAGER_IRQ_BASE + 13
#define	VOYAGER_AC97_IRQ		VOYAGER_IRQ_BASE + 17

/* ----- MISC controle  register ------------------------------ */
#define MISC_CTRL			(0x000004 + VOYAGER_BASE)
#define MISC_CTRL_USBCLK_48		(3 << 28)
#define MISC_CTRL_USBCLK_96		(2 << 28)
#define MISC_CTRL_USBCLK_CRYSTAL	(1 << 28)

/* ----- GPIO[31:0] register --------------------------------- */
#define GPIO_MUX_LOW			(0x000008 + VOYAGER_BASE)
#define GPIO_MUX_LOW_AC97		0x1F000000
#define GPIO_MUX_LOW_8051		0x0000ffff
#define GPIO_MUX_LOW_PWM		(1 << 29)

/* ----- GPIO[63:32] register --------------------------------- */
#define GPIO_MUX_HIGH			(0x00000C + VOYAGER_BASE)

/* ----- DRAM controle  register ------------------------------- */
#define DRAM_CTRL			(0x000010 + VOYAGER_BASE)
#define DRAM_CTRL_EMBEDDED		(1 << 31)
#define DRAM_CTRL_CPU_BURST_1		(0 << 28)
#define DRAM_CTRL_CPU_BURST_2		(1 << 28)
#define DRAM_CTRL_CPU_BURST_4		(2 << 28)
#define DRAM_CTRL_CPU_BURST_8		(3 << 28)
#define DRAM_CTRL_CPU_CAS_LATENCY	(1 << 27)
#define DRAM_CTRL_CPU_SIZE_2		(0 << 24)
#define DRAM_CTRL_CPU_SIZE_4		(1 << 24)
#define DRAM_CTRL_CPU_SIZE_64		(4 << 24)
#define DRAM_CTRL_CPU_SIZE_32		(5 << 24)
#define DRAM_CTRL_CPU_SIZE_16		(6 << 24)
#define DRAM_CTRL_CPU_SIZE_8		(7 << 24)
#define DRAM_CTRL_CPU_COLUMN_SIZE_1024	(0 << 22)
#define DRAM_CTRL_CPU_COLUMN_SIZE_512	(2 << 22)
#define DRAM_CTRL_CPU_COLUMN_SIZE_256	(3 << 22)
#define DRAM_CTRL_CPU_ACTIVE_PRECHARGE	(1 << 21)
#define DRAM_CTRL_CPU_RESET		(1 << 20)
#define DRAM_CTRL_CPU_BANKS		(1 << 19)
#define DRAM_CTRL_CPU_WRITE_PRECHARGE	(1 << 18)
#define DRAM_CTRL_BLOCK_WRITE		(1 << 17)
#define DRAM_CTRL_REFRESH_COMMAND	(1 << 16)
#define DRAM_CTRL_SIZE_4		(0 << 13)
#define DRAM_CTRL_SIZE_8		(1 << 13)
#define DRAM_CTRL_SIZE_16		(2 << 13)
#define DRAM_CTRL_SIZE_32		(3 << 13)
#define DRAM_CTRL_SIZE_64		(4 << 13)
#define DRAM_CTRL_SIZE_2		(5 << 13)
#define DRAM_CTRL_COLUMN_SIZE_256	(0 << 11)
#define DRAM_CTRL_COLUMN_SIZE_512	(2 << 11)
#define DRAM_CTRL_COLUMN_SIZE_1024	(3 << 11)
#define DRAM_CTRL_BLOCK_WRITE_TIME	(1 << 10)
#define DRAM_CTRL_BLOCK_WRITE_PRECHARGE	(1 << 9)
#define DRAM_CTRL_ACTIVE_PRECHARGE	(1 << 8)
#define DRAM_CTRL_RESET			(1 << 7)
#define DRAM_CTRL_REMAIN_ACTIVE		(1 << 6)
#define DRAM_CTRL_BANKS			(1 << 1)
#define DRAM_CTRL_WRITE_PRECHARGE	(1 << 0)

/* ----- Arvitration control register -------------------------- */
#define ARBITRATION_CTRL		(0x000014 + VOYAGER_BASE)
#define ARBITRATION_CTRL_CPUMEM		(1 << 29)
#define ARBITRATION_CTRL_INTMEM		(1 << 28)
#define ARBITRATION_CTRL_USB_OFF	(0 << 24)
#define ARBITRATION_CTRL_USB_PRIORITY_1	(1 << 24)
#define ARBITRATION_CTRL_USB_PRIORITY_2	(2 << 24)
#define ARBITRATION_CTRL_USB_PRIORITY_3	(3 << 24)
#define ARBITRATION_CTRL_USB_PRIORITY_4	(4 << 24)
#define ARBITRATION_CTRL_USB_PRIORITY_5	(5 << 24)
#define ARBITRATION_CTRL_USB_PRIORITY_6	(6 << 24)
#define ARBITRATION_CTRL_USB_PRIORITY_7	(7 << 24)
#define ARBITRATION_CTRL_PANEL_OFF	(0 << 20)
#define ARBITRATION_CTRL_PANEL_PRIORITY_1	(1 << 20)
#define ARBITRATION_CTRL_PANEL_PRIORITY_2	(2 << 20)
#define ARBITRATION_CTRL_PANEL_PRIORITY_3	(3 << 20)
#define ARBITRATION_CTRL_PANEL_PRIORITY_4	(4 << 20)
#define ARBITRATION_CTRL_PANEL_PRIORITY_5	(5 << 20)
#define ARBITRATION_CTRL_PANEL_PRIORITY_6	(6 << 20)
#define ARBITRATION_CTRL_PANEL_PRIORITY_7	(7 << 20)
#define ARBITRATION_CTRL_ZVPORT_OFF	(0 << 16)
#define ARBITRATION_CTRL_ZVPORTL_PRIORITY_1	(1 << 16)
#define ARBITRATION_CTRL_ZVPORTL_PRIORITY_2	(2 << 16)
#define ARBITRATION_CTRL_ZVPORTL_PRIORITY_3	(3 << 16)
#define ARBITRATION_CTRL_ZVPORTL_PRIORITY_4	(4 << 16)
#define ARBITRATION_CTRL_ZVPORTL_PRIORITY_5	(5 << 16)
#define ARBITRATION_CTRL_ZVPORTL_PRIORITY_6	(6 << 16)
#define ARBITRATION_CTRL_ZVPORTL_PRIORITY_7	(7 << 16)
#define ARBITRATION_CTRL_CMD_INTPR_OFF	(0 << 12)
#define ARBITRATION_CTRL_CMD_INTPR_PRIORITY_1	(1 << 12)
#define ARBITRATION_CTRL_CMD_INTPR_PRIORITY_2	(2 << 12)
#define ARBITRATION_CTRL_CMD_INTPR_PRIORITY_3	(3 << 12)
#define ARBITRATION_CTRL_CMD_INTPR_PRIORITY_4	(4 << 12)
#define ARBITRATION_CTRL_CMD_INTPR_PRIORITY_5	(5 << 12)
#define ARBITRATION_CTRL_CMD_INTPR_PRIORITY_6	(6 << 12)
#define ARBITRATION_CTRL_CMD_INTPR_PRIORITY_7	(7 << 12)
#define ARBITRATION_CTRL_DMA_OFF	(0 << 8)
#define ARBITRATION_CTRL_DMA_PRIORITY_1	(1 << 8)
#define ARBITRATION_CTRL_DMA_PRIORITY_2	(2 << 8)
#define ARBITRATION_CTRL_DMA_PRIORITY_3	(3 << 8)
#define ARBITRATION_CTRL_DMA_PRIORITY_4	(4 << 8)
#define ARBITRATION_CTRL_DMA_PRIORITY_5	(5 << 8)
#define ARBITRATION_CTRL_DMA_PRIORITY_6	(6 << 8)
#define ARBITRATION_CTRL_DMA_PRIORITY_7	(7 << 8)
#define ARBITRATION_CTRL_VIDEO_OFF	(0 << 4)
#define ARBITRATION_CTRL_VIDEO_PRIORITY_1	(1 << 4)
#define ARBITRATION_CTRL_VIDEO_PRIORITY_2	(2 << 4)
#define ARBITRATION_CTRL_VIDEO_PRIORITY_3	(3 << 4)
#define ARBITRATION_CTRL_VIDEO_PRIORITY_4	(4 << 4)
#define ARBITRATION_CTRL_VIDEO_PRIORITY_5	(5 << 4)
#define ARBITRATION_CTRL_VIDEO_PRIORITY_6	(6 << 4)
#define ARBITRATION_CTRL_VIDEO_PRIORITY_7	(7 << 4)
#define ARBITRATION_CTRL_CRT_OFF	(0 << 0)
#define ARBITRATION_CTRL_CRT_PRIORITY_1	(1 << 0)
#define ARBITRATION_CTRL_CRT_PRIORITY_2	(2 << 0)
#define ARBITRATION_CTRL_CRT_PRIORITY_3	(3 << 0)
#define ARBITRATION_CTRL_CRT_PRIORITY_4	(4 << 0)
#define ARBITRATION_CTRL_CRT_PRIORITY_5	(5 << 0)
#define ARBITRATION_CTRL_CRT_PRIORITY_6	(6 << 0)
#define ARBITRATION_CTRL_CRT_PRIORITY_7	(7 << 0)

/* ----- Command list status register -------------------------- */
#define CMD_INTPR_STATUS		(0x000024 + VOYAGER_BASE)

/* ----- Interrupt status register ----------------------------- */
#define INT_STATUS			(0x00002c + VOYAGER_BASE)
#define INT_STATUS_UH			(1 << 6)
#define INT_STATUS_MC			(1 << 10)
#define INT_STATUS_U0			(1 << 12)
#define INT_STATUS_U1			(1 << 13)
#define	INT_STATUS_AC			(1 << 17)

/* ----- Interrupt mask register ------------------------------ */
#define VOYAGER_INT_MASK		(0x000030 + VOYAGER_BASE)
#define VOYAGER_INT_MASK_AC		(1 << 17)

/* ----- Current Gate register ---------------------------------*/
#define CURRENT_GATE			(0x000038 + VOYAGER_BASE)

/* ----- Power mode 0 gate register --------------------------- */
#define POWER_MODE0_GATE		(0x000040 + VOYAGER_BASE)
#define POWER_MODE0_GATE_G		(1 << 6)
#define POWER_MODE0_GATE_U0		(1 << 7)
#define POWER_MODE0_GATE_U1		(1 << 8)
#define POWER_MODE0_GATE_UH		(1 << 11)
#define	POWER_MODE0_GATE_AC		(1 << 18)

/* ----- Power mode 1 gate register --------------------------- */
#define POWER_MODE1_GATE		(0x000048 + VOYAGER_BASE)
#define POWER_MODE1_GATE_G		(1 << 6)
#define POWER_MODE1_GATE_U0		(1 << 7)
#define POWER_MODE1_GATE_U1		(1 << 8)
#define POWER_MODE1_GATE_UH		(1 << 11)
#define	POWER_MODE1_GATE_AC		(1 << 18)

/* ----- Power mode 0 clock register -------------------------- */
#define POWER_MODE0_CLOCK		(0x000044 + VOYAGER_BASE)

/* ----- Power mode 1 clock register -------------------------- */
#define POWER_MODE1_CLOCK		(0x00004C + VOYAGER_BASE)

/* ----- Power mode controll register ------------------------- */
#define POWER_MODE_CTRL			(0x000054 + VOYAGER_BASE)

/* ----- Miscellaneous Timing register ------------------------ */
#define SYSTEM_DRAM_CTRL		(0x000068 + VOYAGER_BASE)

/* ----- PWM register ------------------------------------------*/
#define PWM_0				(0x010020 + VOYAGER_BASE)
#define PWM_0_HC(x)			(((x)&0x0fff)<<20)
#define PWM_0_LC(x)			(((x)&0x0fff)<<8 )
#define PWM_0_CLK_DEV(x)		(((x)&0x000f)<<4 )
#define PWM_0_EN			(1<<0)

/* ----- I2C register ----------------------------------------- */
#define I2C_BYTECOUNT			(0x010040 + VOYAGER_BASE)
#define I2C_CONTROL			(0x010041 + VOYAGER_BASE)
#define I2C_STATUS			(0x010042 + VOYAGER_BASE)
#define I2C_RESET			(0x010042 + VOYAGER_BASE)
#define I2C_SADDRESS			(0x010043 + VOYAGER_BASE)
#define I2C_DATA			(0x010044 + VOYAGER_BASE)

/* ----- Controle register bits ----------------------------------------- */
#define I2C_CONTROL_E			(1 << 0)
#define I2C_CONTROL_MODE		(1 << 1)
#define I2C_CONTROL_STATUS		(1 << 2)
#define I2C_CONTROL_INT			(1 << 4)
#define I2C_CONTROL_INTACK		(1 << 5)
#define I2C_CONTROL_REPEAT		(1 << 6)

/* ----- Status register bits ----------------------------------------- */
#define I2C_STATUS_BUSY			(1 << 0)
#define I2C_STATUS_ACK			(1 << 1)
#define I2C_STATUS_ERROR		(1 << 2)
#define I2C_STATUS_COMPLETE		(1 << 3)

/* ----- Reset register  ---------------------------------------------- */
#define I2C_RESET_ERROR			(1 << 2)

/* ----- transmission frequencies ------------------------------------- */
#define I2C_SADDRESS_SELECT		(1 << 0)

/* ----- Display Controll register ----------------------------------------- */
#define PANEL_DISPLAY_CTRL		(0x080000 + VOYAGER_BASE)
#define PANEL_DISPLAY_CTRL_BIAS         (1<<26)
#define PANEL_PAN_CTRL			(0x080004 + VOYAGER_BASE)
#define PANEL_COLOR_KEY			(0x080008 + VOYAGER_BASE)
#define PANEL_FB_ADDRESS		(0x08000C + VOYAGER_BASE)
#define PANEL_FB_WIDTH			(0x080010 + VOYAGER_BASE)
#define PANEL_WINDOW_WIDTH		(0x080014 + VOYAGER_BASE)
#define PANEL_WINDOW_HEIGHT		(0x080018 + VOYAGER_BASE)
#define PANEL_PLANE_TL			(0x08001C + VOYAGER_BASE)
#define PANEL_PLANE_BR			(0x080020 + VOYAGER_BASE)
#define PANEL_HORIZONTAL_TOTAL		(0x080024 + VOYAGER_BASE)
#define PANEL_HORIZONTAL_SYNC		(0x080028 + VOYAGER_BASE)
#define PANEL_VERTICAL_TOTAL		(0x08002C + VOYAGER_BASE)
#define PANEL_VERTICAL_SYNC		(0x080030 + VOYAGER_BASE)
#define PANEL_CURRENT_LINE		(0x080034 + VOYAGER_BASE)
#define VIDEO_DISPLAY_CTRL		(0x080040 + VOYAGER_BASE)
#define VIDEO_FB_0_ADDRESS		(0x080044 + VOYAGER_BASE)
#define VIDEO_FB_WIDTH			(0x080048 + VOYAGER_BASE)
#define VIDEO_FB_0_LAST_ADDRESS		(0x08004C + VOYAGER_BASE)
#define VIDEO_PLANE_TL			(0x080050 + VOYAGER_BASE)
#define VIDEO_PLANE_BR			(0x080054 + VOYAGER_BASE)
#define VIDEO_SCALE			(0x080058 + VOYAGER_BASE)
#define VIDEO_INITIAL_SCALE		(0x08005C + VOYAGER_BASE)
#define VIDEO_YUV_CONSTANTS		(0x080060 + VOYAGER_BASE)
#define VIDEO_FB_1_ADDRESS		(0x080064 + VOYAGER_BASE)
#define VIDEO_FB_1_LAST_ADDRESS		(0x080068 + VOYAGER_BASE)
#define VIDEO_ALPHA_DISPLAY_CTRL	(0x080080 + VOYAGER_BASE)
#define VIDEO_ALPHA_FB_ADDRESS		(0x080084 + VOYAGER_BASE)
#define VIDEO_ALPHA_FB_WIDTH		(0x080088 + VOYAGER_BASE)
#define VIDEO_ALPHA_FB_LAST_ADDRESS	(0x08008C + VOYAGER_BASE)
#define VIDEO_ALPHA_PLANE_TL		(0x080090 + VOYAGER_BASE)
#define VIDEO_ALPHA_PLANE_BR		(0x080094 + VOYAGER_BASE)
#define VIDEO_ALPHA_SCALE		(0x080098 + VOYAGER_BASE)
#define VIDEO_ALPHA_INITIAL_SCALE	(0x08009C + VOYAGER_BASE)
#define VIDEO_ALPHA_CHROMA_KEY		(0x0800A0 + VOYAGER_BASE)
#define PANEL_HWC_ADDRESS		(0x0800F0 + VOYAGER_BASE)
#define PANEL_HWC_LOCATION		(0x0800F4 + VOYAGER_BASE)
#define PANEL_HWC_COLOR_12		(0x0800F8 + VOYAGER_BASE)
#define PANEL_HWC_COLOR_3		(0x0800FC + VOYAGER_BASE)
#define ALPHA_DISPLAY_CTRL		(0x080100 + VOYAGER_BASE)
#define ALPHA_FB_ADDRESS		(0x080104 + VOYAGER_BASE)
#define ALPHA_FB_WIDTH			(0x080108 + VOYAGER_BASE)
#define ALPHA_PLANE_TL			(0x08010C + VOYAGER_BASE)
#define ALPHA_PLANE_BR			(0x080110 + VOYAGER_BASE)
#define ALPHA_CHROMA_KEY		(0x080114 + VOYAGER_BASE)
#define CRT_DISPLAY_CTRL		(0x080200 + VOYAGER_BASE)
#define CRT_FB_ADDRESS			(0x080204 + VOYAGER_BASE)
#define CRT_FB_WIDTH			(0x080208 + VOYAGER_BASE)
#define CRT_HORIZONTAL_TOTAL		(0x08020C + VOYAGER_BASE)
#define CRT_HORIZONTAL_SYNC		(0x080210 + VOYAGER_BASE)
#define CRT_VERTICAL_TOTAL		(0x080214 + VOYAGER_BASE)
#define CRT_VERTICAL_SYNC		(0x080218 + VOYAGER_BASE)
#define CRT_SIGNATURE_ANALYZER		(0x08021C + VOYAGER_BASE)
#define CRT_CURRENT_LINE		(0x080220 + VOYAGER_BASE)
#define CRT_MONITOR_DETECT		(0x080224 + VOYAGER_BASE)
#define CRT_HWC_ADDRESS			(0x080230 + VOYAGER_BASE)
#define CRT_HWC_LOCATION		(0x080234 + VOYAGER_BASE)
#define CRT_HWC_COLOR_12		(0x080238 + VOYAGER_BASE)
#define CRT_HWC_COLOR_3			(0x08023C + VOYAGER_BASE)
#define CRT_PALETTE_RAM			(0x080400 + VOYAGER_BASE)
#define PANEL_PALETTE_RAM		(0x080800 + VOYAGER_BASE)
#define VIDEO_PALETTE_RAM		(0x080C00 + VOYAGER_BASE)

/* ----- 8051 Controle register ----------------------------------------- */
#define VOYAGER_8051_BASE		(0x000c0000 + VOYAGER_BASE)
#define VOYAGER_8051_RESET		(0x000b0000 + VOYAGER_BASE)
#define VOYAGER_8051_SELECT		(0x000b0004 + VOYAGER_BASE)
#define VOYAGER_8051_CPU_INT		(0x000b000c + VOYAGER_BASE)

/* ----- AC97 Controle register ----------------------------------------- */
#define AC97_TX_SLOT0			(0x00000000 + VOYAGER_AC97_BASE)
#define AC97_CONTROL_STATUS		(0x00000080 + VOYAGER_AC97_BASE)
#define AC97C_READ			(1 << 19)
#define AC97C_WD_BIT			(1 << 2)
#define AC97C_INDEX_MASK		0x7f
/* -------------------------------------------------------------------- */

#endif /* _VOYAGER_GX_REG_H */
