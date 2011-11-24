//------------------------------------------------------------------------------
// <copyright file="omap2420_spi_hw.h" company="Atheros">
//    Copyright (c) 2007-2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef __OMAP2420_SPI_HW_H___
#define __OMAP2420_SPI_HW_H___

#define OMAP_SPI_MODULE_CLOCK     48000000

#define OMAP_SPIF1_BASE  0x48098000 /* McSPI1 */
#define OMAP_SPIF2_BASE  0x4809A000 /* McSPI2 */


struct _OMAP_DMA_REGS {
    UINT32 CCR;  
    UINT32 CLNK_CTRL;         
    UINT32 CICR;
    UINT32 CSR; 
    UINT32 CSDP;
    UINT32 CEN;
    UINT32 CFN;
    UINT32 CSSA;
    UINT32 CDSA;
    UINT32 CSEI;
    UINT32 CSFI;
    UINT32 CDEI;
    UINT32 CDFI;
    UINT32 CSAC;
    UINT32 CDAC;
    UINT32 CCEN;
    UINT32 CCFN;
}__attribute__ ((packed));

typedef struct _OMAP_DMA_REGS *POMAP_DMA_REGS;


#define RESET_MAX_COUNT  100000
#define OMAP_SPIF_REV 0x00
    /* system configuration */
#define OMAP_SPIF_SCR_REG 0x10
#define OMAP_SPIF_SCR_OCP_MAIN          (0x01 << 8)
#define OMAP_SPIF_SCR_FUNC_CLK_MAIN     (0x02 << 8)
#define OMAP_SPIF_SCR_OCP_FUNC_CLK_MAIN (0x03 << 8)
#define OMAP_SPIF_SCR_FORCE_IDLE (0x00 << 3)
#define OMAP_SPIF_SCR_NO_IDLE    (0x01 << 3)
#define OMAP_SPIF_SCR_SMART_IDLE (0x02 << 3)
#define OMAP_SPIF_SCR_RESET      0x02
#define OMAP_SPIF_SCR_OCP_GATE   0x01
    /* system status */
#define OMAP_SPIF_SSR_REG 0x14
#define OMAP_SPIF_SSR_RESET_DONE 0x1
    /* interrupt status */
#define OMAP_SPIF_ISR_REG 0x18
#define OMAP_SPIF_ISR_WAKEUP        (1 << 16) /* slave mode only */

#define OMAP_SPIF_ISR_RX3_FULL      (1 << 14)
#define OMAP_SPIF_ISR_TX3_UNDERFLOW (1 << 13)
#define OMAP_SPIF_ISR_TX3_EMPTY     (1 << 12)

#define OMAP_SPIF_ISR_RX2_FULL      (1 << 10)
#define OMAP_SPIF_ISR_TX2_UNDERFLOW (1 << 9)
#define OMAP_SPIF_ISR_TX2_EMPTY     (1 << 8)

#define OMAP_SPIF_ISR_RX1_FULL      (1 << 6)
#define OMAP_SPIF_ISR_TX1_UNDERFLOW (1 << 5)
#define OMAP_SPIF_ISR_TX1_EMPTY     (1 << 4)

#define OMAP_SPIF_ISR_RX0_OVERFLOW  (1 << 3) /* slave only */
#define OMAP_SPIF_ISR_RX0_FULL      (1 << 2)
#define OMAP_SPIF_ISR_TX0_UNDERFLOW (1 << 1)
#define OMAP_SPIF_ISR_TX0_EMPTY     (1 << 0)
    /* interrupt enable */
#define OMAP_SPIF_IER_REG 0x1C

    /* system control register */
#define OMAP_SPIF_SYST_REG 0x024
#define OMAP_SPIF_SYST_SET_ALL__IRQ (1 << 11)
#define OMAP_SPIF_SYST_CS_CLK_OUT   0
#define OMAP_SPIF_SYST_CS_CLK_IN    (1 << 10)
#define OMAP_SPIF_SYST_D1_IN        (1 << 9)
#define OMAP_SPIF_SYST_D1_OUT       0
#define OMAP_SPIF_SYST_DO_IN        (1 << 8)
#define OMAP_SPIF_SYST_DO_OUT       0
#define OMAP_SPIF_SYST_WAKEUP       (1 << 7)
#define OMAP_SPIF_SYST_CLK_SIG_HI   (1 << 6)
#define OMAP_SPIF_SYST_SIMO_HI      (1 << 5)
#define OMAP_SPIF_SYST_SOMI_HI      (1 << 4)
#define OMAP_SPIF_SYST_ENx_HI(cs)   (1 < (cs))

    /* module control register */
#define OMAP_SPIF_MCTRL_REG              0x028
#define OMAP_SPIF_MCTRL_SYSTEST          (1 << 3)
#define OMAP_SPIF_MCTRL_SLAVE            (1 << 2)
#define OMAP_SPIF_MCTRL_SINGLE_CHANNEL   (1 << 0)

#define OMAP_SPIF_CHCONFx_OFFSET(index)  (0x2C + (index)*0x14)
#define OMAP_SPI_CHCON_ENSLV(x)          ((x) << 21) /* slave only */
#define OMAP_SPI_CHCON_FORCE_CS_ON       (1 << 20)
#define OMAP_SPI_CHCON_TURBO_ON          (1 << 19)
#define OMAP_SPI_CHCON_D1_RX             (1 << 18)
#define OMAP_SPI_CHCON_D0_RX             0
#define OMAP_SPI_CHCON_D1_NO_TX          (1 << 17)
#define OMAP_SPI_CHCON_D1_TX             0             
#define OMAP_SPI_CHCON_D0_NO_TX          (1 << 16) 
#define OMAP_SPI_CHCON_D0_TX             0 
#define OMAP_SPI_CHCON_DMA_READ          (1 << 15)
#define OMAP_SPI_CHCON_DMA_WRITE         (1 << 14)
#define OMAP_SPI_CHCON_TRM_RX_TX         0x0
#define OMAP_SPI_CHCON_TRM_RX_ONLY       (1 << 12)
#define OMAP_SPI_CHCON_TRM_TX_ONLY       (2 << 12)
#define OMAP_SPI_CHCON_TRM_MASK          (0x3 << 12)
#define OMAP_SPI_CHCON_WORD_LENGTH(x)    (((x) - 1) << 7)
#define OMAP_SPI_CHCON_WORD_LENGTH_MASK  (0x1F << 7)
#define OMAP_SPI_CHCON_CS_ACTIVE_LOW     (1 << 6)
#define OMAP_SPI_CHCON_CLKD_MASK         (0xF << 2)
#define OMAP_SPI_CHCON_CLKD(x)           ((x) << 2)
#define OMAP_SPI_CHCON_CLK_ACTIVE_LOW    (1 << 1)
#define OMAP_SPI_CHCON_CLK_ACTIVE_HIGH   0
#define OMAP_SPI_CHCON_CLK_DATA_EVEN     (1 << 0)
#define OMAP_SPI_CHCON_CLK_DATA_ODD      0

#define OMAP_SPI_CHCON_SPI_MODE_0        (OMAP_SPI_CHCON_CLK_ACTIVE_HIGH | OMAP_SPI_CHCON_CLK_DATA_ODD)
#define OMAP_SPI_CHCON_SPI_MODE_1        (OMAP_SPI_CHCON_CLK_ACTIVE_HIGH | OMAP_SPI_CHCON_CLK_DATA_EVEN)
#define OMAP_SPI_CHCON_SPI_MODE_2        (OMAP_SPI_CHCON_CLK_ACTIVE_LOW | OMAP_SPI_CHCON_CLK_DATA_ODD)
#define OMAP_SPI_CHCON_SPI_MODE_3        (OMAP_SPI_CHCON_CLK_ACTIVE_LOW | OMAP_SPI_CHCON_CLK_DATA_EVEN)
#define OMAP_SPI_CHCON_SPI_MODE_MASK     (0x3)

#define OMAP_SPIF_CHSTATx_OFFSET(index)  (0x30 + (index)*0x14)
#define OMAP_SPIF_CHSTAT_EOT             (1 << 2)
#define OMAP_SPIF_CHSTAT_TX_EMPTY        (1 << 1)
#define OMAP_SPIF_CHSTAT_RX_FULL         (1 << 0)

#define OMAP_SPIF_CHCTRLx_OFFSET(index)  (0x34 + (index)*0x14)
#define OMAP_SPIF_CHCTRL_ENABLED         (1 << 0)
#define OMAP_SPIF_CHCTRL_BIG_ENDIAN      (1 << 1)

#define OMAP_SPIF_CHTXx_OFFSET(index)  (0x38 + (index)*0x14)
#define OMAP_SPIF_CHRXx_OFFSET(index)  (0x3C + (index)*0x14)

#define CLOCK_ON  TRUE
#define CLOCK_OFF FALSE

#define MAX_CLOCK_ENTRIES 16
#define MAX_PTVVALUE      0x0F
typedef struct _OMAP_SPIF_CLK_RATE_ENTRY {
    UINT8  PTVValue;        /* divisor control value */
    UINT32 ClockRate;       /* actual clock rate */
}OMAP_SPIF_CLK_RATE_ENTRY, *POMAP_SPIF_CLK_RATE_ENTRY;

#endif
