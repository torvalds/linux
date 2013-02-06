/* sound/soc/samsung/srp_reg.h
 *
 * Audio RP Registers for Samsung Exynos4
 *
 * Copyright (c) 2010 Samsung Electronics
 * http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _SRP_ALP_REG_H_
#define _SRP_ALP_REG_H_

#define SRP_IRAM_BASE		(0x02020000)
#define SRP_DMEM_BASE		(0x03000000)
#define SRP_ASSCLK_BASE		(0x03810000)
#define SRP_COMMBOX_BASE	(0x03820000)

/* Commbox Offset */
#define SRP_CONT		(0x0000)
#define SRP_CFGR		(0x0004)
#define SRP_INTERRUPT		(0x0008)
#define SRP_PENDING		(0x000C)
#define SRP_INTERRUPT_CODE	(0x0010)
#define SRP_POWER_MODE		(0x0014)
#define SRP_ERROR_CODE		(0x0018)
#define SRP_ARM_INTERRUPT_CODE	(0x001C)
#define SRP_SUSPENDED_IP	(0x0020)
#define SRP_SUSPENDED_SP	(0x0024)
#define SRP_FRAME_INDEX		(0x0028)
#define SRP_PCM_BUFF_SIZE	(0x002C)
#define SRP_PCM_BUFF0		(0x0030)
#define SRP_PCM_BUFF1		(0x0034)
#define SRP_READ_BITSTREAM_SIZE	(0x0038)

#define SRP_LOAD_CGA_SA_ADDR		(0x0100)
#define SRP_BITSTREAM_BUFF_DRAM_ADDR1	(0x0104)
#define SRP_BITSTREAM_SIZE		(0x0108)
#define SRP_BITSTREAM_BUFF_DRAM_ADDR0	(0x010C)
#define SRP_CODE_START_ADDR		(0x0110)
#define SRP_PCM_DUMP_ADDR		(0x0114)
#define SRP_DATA_START_ADDR		(0x0118)
#define SRP_BITSTREAM_BUFF_DRAM_SIZE	(0x011C)
#define SRP_CONF_START_ADDR		(0x0120)
#define SRP_GAIN_CTRL_FACTOR_L		(0x0124)
#define SRP_UART_INFORMATION		(0x0128)
#define SRP_GAIN_CTRL_FACTOR_R		(0x012C)
#define SRP_INTREN			(0x0180)
#define SRP_INTRMASK			(0x0184)
#define SRP_INTRSRC			(0x0188)
#define SRP_INTRIRQ			(0x0308)

/*
 * SRP Configuration register
 */
#define SRP_CFGR_OUTPUT_PCM_8BIT	(0x0 << 0)
#define SRP_CFGR_OUTPUT_PCM_16BIT	(0x1 << 0)
#define SRP_CFGR_OUTPUT_PCM_24BIT	(0x2 << 0)
#define SRP_CFGR_BOOT_INST_EXT_MEM	(0x0 << 2)
#define SRP_CFGR_BOOT_INST_INT_CC	(0x1 << 2)
#define SRP_CFGR_NOTUSE_ICACHE_MEM	(0x0 << 3)
#define SRP_CFGR_USE_ICACHE_MEM		(0x1 << 3)
#define SRP_CFGR_FLOW_CTRL_ON		(0x1 << 4)
#define SRP_CFGR_FLOW_CTRL_OFF		(0x0 << 4)
#define SRP_CFGR_USE_I2S_INTR		(0x1 << 6)
#define SRP_CFGR_NOTUSE_I2S_INTR	(0x0 << 6)

/*
 * SRP Pending control register
 */
#define SRP_RUN		(0x0 << 0)
#define SRP_STALL	(0x1 << 0)

/*
 * Interrupt Code & Information
 */

/* for SRP_INTERRUPT_CODE */
#define SRP_INTR_CODE_MASK		(0x0FFF)

#define SRP_INTR_CODE_PLAYDONE		(0x0001 << 0)
#define SRP_INTR_CODE_ERROR		(0x0001 << 1)
#define SRP_INTR_CODE_REQUEST		(0x0001 << 2)
#define SRP_INTR_CODE_POLLINGWAIT	(0x0001 << 9)
#define SRP_INTR_CODE_UART_OUTPUT	(0x0001 << 10)
#define SRP_INTR_CODE_NOTIFY_OBUF	(0x0001 << 11)

#define SRP_INTR_CODE_REQUEST_MASK	(0x0007 << 6)
#define SRP_INTR_CODE_NOTIFY_INFO	(0x0007 << 6)
#define SRP_INTR_CODE_IBUF_REQUEST_ULP	(0x0006 << 6)
#define SRP_INTR_CODE_IBUF_REQUEST	(0x0005 << 6)
#define SRP_INTR_CODE_ULP		(0x0004 << 6)
#define SRP_INTR_CODE_OBUF_FULL         (0x0003 << 6)
#define SRP_INTR_CODE_PLAYEND		(0x0002 << 6)
#define SRP_INTR_CODE_DRAM_REQUEST	(0x0001 << 6)

#define SRP_INTR_CODE_IBUF_MASK		(0x0003 << 4)
#define SRP_INTR_CODE_IBUF0_EMPTY	(0x0001 << 4)
#define SRP_INTR_CODE_IBUF1_EMPTY	(0x0002 << 4)

#define SRP_INTR_CODE_OBUF_MASK		(0x0003 << 4)
#define SRP_INTR_CODE_OBUF0_FULL	(0x0001 << 4)
#define SRP_INTR_CODE_OBUF1_FULL	(0x0002 << 4)

/* for SRP_INFORMATION */
#define SRP_INTR_INFO_MASK		(0xFFFF)

/* for SRP_ARM_INTERRUPT_CODE */
#define SRP_ARM_INTR_CODE_MASK		(0x007F)
/* ARM to RP */
#define SRP_ARM_INTR_CODE_SA_ON		(0x0001 << 0)
#define SRP_ARM_INTR_CODE_PAUSE_REQ	(0x0001 << 1)
#define SRP_ARM_INTR_CODE_PAUSE_STA	(0x0001 << 2)
#define SRP_ARM_INTR_CODE_ULP_ATYPE	(0x0000 << 3)
#define SRP_ARM_INTR_CODE_ULP_BTYPE	(0x0001 << 3)
#define SRP_ARM_INTR_CODE_ULP_CTYPE	(0x0002 << 3)
/* RP to ARM */
#define SRP_ARM_INTR_CODE_CHINF_MASK	(0x0003)
#define SRP_ARM_INTR_CODE_CHINF_SHIFT	(5)
#define SRP_ARM_INTR_CODE_SRINF_MASK	(0xFFFF)
#define SRP_ARM_INTR_CODE_SRINF_SHIFT	(14)

/* ARM to RP */
#define SRP_ARM_INTR_CODE_PCM_DUMP_ON	(0x0001 << 7)
/* RP to ARM */
#define SRP_ARM_INTR_CODE_FRAME_MASK	(0x0007 << 8)
#define SRP_ARM_INTR_CODE_FRAME_NULL	(0x0000 << 8)
#define SRP_ARM_INTR_CODE_FRAME_576	(0x0001 << 8)
#define SRP_ARM_INTR_CODE_FRAME_1152	(0x0002 << 8)
#define SRP_ARM_INTR_CODE_FRAME_384	(0x0003 << 8)
#define SRP_ARM_INTR_CODE_FRAME_1024	(0x0004 << 8)
/* ARM to RP */
#define SRP_ARM_INTR_CODE_FORCE_MONO	(0x0001 << 11)
#define SRP_ARM_INTR_CODE_AM_FILTER_LOAD	(0x0001 << 12)
#define SRP_ARM_INTR_CODE_SUPPORT_MONO	(0x0001 << 13)

/* For Suspend/Resume */
#define SRP_POWER_MODE_MASK		(0xFFFF)
#define SRP_POWER_MODE_TRIGGER		(0x1)
#define SRP_SW_RESET_TRIGGER		(0x1 << 2)
#define SRP_SUSPENED_CHECKED		(0x1 << 1)
#define SRP_SW_RESET_DONE		(0x1 << 3)
/* INTREN */
#define SRP_INTR_EN			(0x1)
#define SRP_INTR_DI			(0x0)
/* INTRMASK */
#define SRP_INTR_MASK			(0x7F)
#define SRP_ARM_INTR_MASK		(0x1 << 6)
#define SRP_DMA_INTR_MASK		(0x1 << 5)
#define SRP_TMR_INTR_MASK		(0x1F << 0)
/* INTRSRC */
#define SRP_INTRSRC_MASK		(0x7F)
#define SRP_ARM_INTR_SRC		(0x1 << 6)
#define SRP_DMA_INTR_SRC		(0x1 << 5)
#define SRP_TMR_INTR_SRC		(0x1F << 0)
/* INTRIRQ */
#define SRP_INTRIRQ_MASK		(0xFFFF << 0)
#define SRP_INTRIRQ_CONF		(0x100 << 0)
#endif
