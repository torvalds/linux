/* $OpenBSD: amdisplayreg.h,v 1.5 2019/05/06 03:45:58 mlarkin Exp $ */
/*
 * Copyright (c) 2016 Ian Sutton <ians@openbsd.org>
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

/* AM335x LCDC register offsets */
#define LCD_PID                                 0x00
#define LCD_CTRL                                0x04
#define   LCD_CTRL_CLKDIV                       (0xFF << 8)
#define   LCD_CTRL_AUTO_UFLOW_RESTART           (1 << 1)
#define   LCD_CTRL_MODESEL                      (1 << 0)
#define   LCD_CTRL_CLKDIV_SHAMT                 8
#define LCD_RASTER_CTRL                         0x28
#define   LCD_RASTER_CTRL_TFT24UNPACKED         (1 << 26)
#define   LCD_RASTER_CTRL_TFT24                 (1 << 25)
#define   LCD_RASTER_CTRL_STN565                (1 << 24)
#define   LCD_RASTER_CTRL_TFTMAP                (1 << 23)
#define   LCD_RASTER_CTRL_NIBMODE               (1 << 22)
#define   LCD_RASTER_CTRL_PALMODE               (3 << 20)
#define   LCD_RASTER_CTRL_REQDLY                (0xFF << 12)
#define   LCD_RASTER_CTRL_LCDTFT                (1 << 7)
#define   LCD_RASTER_CTRL_LCDEN                 (1 << 0)
#define   LCD_RASTER_CTRL_PALMODE_SHAMT         20
#define   LCD_RASTER_CTRL_REQDLY_SHAMT          12
#define LCD_RASTER_TIMING_0                     0x2C
#define   LCD_RASTER_TIMING_0_HBP               (0xFF << 24)
#define   LCD_RASTER_TIMING_0_HFP               (0xFF << 16)
#define   LCD_RASTER_TIMING_0_HSW               (0x3F << 10)
#define   LCD_RASTER_TIMING_0_PPLLSB            (0x3  <<  4)
#define   LCD_RASTER_TIMING_0_PPLMSB            (0x1  <<  3)
#define   LCD_RASTER_TIMING_0_HBP_SHAMT         24
#define   LCD_RASTER_TIMING_0_HFP_SHAMT         16
#define   LCD_RASTER_TIMING_0_HSW_SHAMT         10
#define   LCD_RASTER_TIMING_0_PPLLSB_SHAMT      4
#define   LCD_RASTER_TIMING_0_PPLMSB_SHAMT      3
#define LCD_RASTER_TIMING_1                     0x30
#define   LCD_RASTER_TIMING_1_VBP               (0xFF  << 24)
#define   LCD_RASTER_TIMING_1_VFP               (0xFF  << 16)
#define   LCD_RASTER_TIMING_1_VSW               (0x3C  << 10)
#define   LCD_RASTER_TIMING_1_LPP               (0x3FF <<  0)
#define   LCD_RASTER_TIMING_1_VBP_SHAMT         24
#define   LCD_RASTER_TIMING_1_VFP_SHAMT         16
#define   LCD_RASTER_TIMING_1_VSW_SHAMT         10
#define LCD_RASTER_TIMING_2                     0x34
#define   LCD_RASTER_TIMING_2_HSW_HIGHBITS      (0xF  << 27)
#define   LCD_RASTER_TIMING_2_LPP_B10           (0x1  << 26)
#define   LCD_RASTER_TIMING_2_PHSVS_ON_OFF      (0x1  << 25)
#define   LCD_RASTER_TIMING_2_PHSVS_RF          (0x1  << 24)
#define   LCD_RASTER_TIMING_2_IEO               (0x1  << 23)
#define   LCD_RASTER_TIMING_2_IPC               (0x1  << 22)
#define   LCD_RASTER_TIMING_2_IHS               (0x1  << 21)
#define   LCD_RASTER_TIMING_2_IVS               (0x1  << 20)
#define   LCD_RASTER_TIMING_2_ACBI              (0xF  << 16)
#define   LCD_RASTER_TIMING_2_ACB               (0xFF <<  8)
#define   LCD_RASTER_TIMING_2_HBP_HIGHBITS      (0x3  <<  4)
#define   LCD_RASTER_TIMING_2_HFP_HIGHBITS      (0x3  <<  0)
#define   LCD_RASTER_TIMING_2_HSW_HIGHBITS_SHAMT        27
#define   LCD_RASTER_TIMING_2_LPP_B10_SHAMT     26
#define   LCD_RASTER_TIMING_2_ACBI_SHAMT        16
#define   LCD_RASTER_TIMING_2_ACB_SHAMT         8
#define   LCD_RASTER_TIMING_2_HPB_HIGHBITS_SHAMT        4
#define LCD_RASTER_SUBPANEL                     0x38
#define   LCD_RASTER_SUBPANEL_SPEN              (0x1    << 31)
#define   LCD_RASTER_SUBPANEL_HOLS              (0x1    << 29)
#define   LCD_RASTER_SUBPANEL_LPPT              (0x2FF  << 16)
#define   LCD_RASTER_SUBPANEL_DPDLSB            (0xFFFF <<  0)
#define   LCD_RASTER_SUBPANEL_LPPT_SHAMT
#define LCD_RASTER_SUBPANEL_2                   0x3C
#define   LCD_RASTER_SUBPANEL2_LPPT_B10         (0x1  << 8)
#define   LCD_RASTER_SUBPANEL2_DPDMSB           (0xFF << 0)
#define LCD_LCDDMA_CTRL                         0x40
#define   LCD_LCDDMA_CTRL_DMA_MASTER_PRIO       (0x7 << 0x10)
#define   LCD_LCDDMA_CTRL_TH_FIFO_READY         (0x7 << 0x08)
#define   LCD_LCDDMA_CTRL_BURST_SIZE            (0x7 << 0x04)
#define   LCD_LCDDMA_CTRL_BYTE_SWAP             (0x1 << 0x03)
#define   LCD_LCDDMA_CTRL_BIGENDIAN             (0x1 << 0x01)
#define   LCD_LCDDMA_CTRL_FRAME_MODE            (0x1 << 0x00)
#define   LCD_LCDDMA_CTRL_DMA_MASTER_PRIO_SHAMT 0xFF
#define   LCD_LCDDMA_CTRL_TH_FIFO_READY_SHAMT   0x08
#define   LCD_LCDDMA_CTRL_BURST_SIZE_SHAMT      0x04
#define LCD_LCDDMA_FB0                          0x44
#define   LCD_LCDDMA_FB0_BASE                   0xFFFC
#define   LCD_LCDDMA_FB0_BASE_SHAMT             0
#define LCD_LCDDMA_FB0_CEIL                     0x48
#define   LCD_LCDDMA_FB0_CEILING                0xFFFC
#define   LCD_LCDDMA_FB0_CEILING_SHAMT          0
#define LCD_LCDDMA_FB1                          0x4C
#define   LCD_LCDDMA_FB1_BASE                   0xFFFC
#define   LCD_LCDDMA_FB1_BASE_SHAMT             0
#define LCD_LCDDMA_FB1_CEIL                     0x50
#define   LCD_LCDDMA_FB1_CEILING                0xFFFC
#define   LCD_LCDDMA_FB1_CEILING_SHAMT          0
#define LCD_SYSCONFIG                           0x54
#define   LCD_SYSCONFIG_STANDBYMODE             (2 << 4)
#define   LCD_SYSCONFIG_IDLEMODE                (2 << 2)
#define   LCD_SYSCONFIG_STANDBYMODE_SHAMT       4
#define   LCD_SYSCONFIG_IDLEMODE_SHAMT          2
#define LCD_IRQSTATUS_RAW                       0x58
#define LCD_IRQSTATUS                           0x5C
#define LCD_IRQENABLE_SET                       0x60
#define LCD_IRQENABLE_CLEAR                     0x64
#define LCD_IRQ_END                             0x68
#define LCD_CLKC_ENABLE                         0x6C
#define   LCD_CLKC_ENABLE_DMA_CLK_EN            (1 << 2)
#define   LCD_CLKC_ENABLE_LIDD_CLK_EN           (1 << 1)
#define   LCD_CLKC_ENABLE_CORE_CLK_EN           (1 << 0)
#define LCD_CLKC_RESET                          0x70
#define   LCD_CLKC_RESET_MAIN_RST               (1 << 3)
#define   LCD_CLKC_RESET_DMA_RST                (1 << 2)
#define   LCD_CLKC_RESET_LIDD_RST               (1 << 1)
#define   LCD_CLKC_RESET_CORE_RST               (1 << 0)

/* AM335x LCDC intr. masks */
#define LCD_IRQ_EOF1    (1 << 9)
#define LCD_IRQ_EOF0    (1 << 8)
#define LCD_IRQ_PL      (1 << 6)
#define LCD_IRQ_FUF     (1 << 5)
#define LCD_IRQ_ACB     (1 << 3)
#define LCD_IRQ_SYNC    (1 << 2)
#define LCD_IRQ_RR_DONE (1 << 1)
#define LCD_IRQ_DONE    (1 << 0)

/* EDID reading */
#define EDID_LENGTH     0x80

/* phandle for pin muxing */
#define LCD_FDT_PHANDLE 0x2f
