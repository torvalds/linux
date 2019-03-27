/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEV_ATH_AR9300REG_H
#define _DEV_ATH_AR9300REG_H

#include "osprey_reg_map.h"
#include "wasp_reg_map.h"

/******************************************************************************
 * MAC Register Map
******************************************************************************/
#define AR_MAC_DMA_OFFSET(_x)   offsetof(struct mac_dma_reg, _x)

/*
 * MAC DMA Registers
 */

/* MAC Control Register - only write values of 1 have effect */
#define AR_CR                AR_MAC_DMA_OFFSET(MAC_DMA_CR)
#define AR_CR_LP_RXE         0x00000004 // Receive LPQ enable
#define AR_CR_HP_RXE         0x00000008 // Receive HPQ enable
#define AR_CR_RXD            0x00000020 // Receive disable
#define AR_CR_SWI            0x00000040 // One-shot software interrupt
#define AR_CR_RXE            (AR_CR_LP_RXE|AR_CR_HP_RXE)

/* MAC configuration and status register */
#define AR_CFG               AR_MAC_DMA_OFFSET(MAC_DMA_CFG)
#define AR_CFG_SWTD          0x00000001 // byteswap tx descriptor words
#define AR_CFG_SWTB          0x00000002 // byteswap tx data buffer words
#define AR_CFG_SWRD          0x00000004 // byteswap rx descriptor words
#define AR_CFG_SWRB          0x00000008 // byteswap rx data buffer words
#define AR_CFG_SWRG          0x00000010 // byteswap register access data words
#define AR_CFG_AP_ADHOC_INDICATION 0x00000020 // AP/adhoc indication (0-AP 1-Adhoc)
#define AR_CFG_PHOK          0x00000100 // PHY OK status
#define AR_CFG_CLK_GATE_DIS  0x00000400 // Clock gating disable
#define AR_CFG_EEBS          0x00000200 // EEPROM busy
#define AR_CFG_PCI_MASTER_REQ_Q_THRESH          0x00060000 // Mask of PCI core master request queue full threshold
#define AR_CFG_PCI_MASTER_REQ_Q_THRESH_S        17         // Shift for PCI core master request queue full threshold
#define AR_CFG_MISSING_TX_INTR_FIX_ENABLE       0x00080000 // See EV 61133 for details.

/* Rx DMA Data Buffer Pointer Threshold - High and Low Priority register */
#define AR_RXBP_THRESH          AR_MAC_DMA_OFFSET(MAC_DMA_RXBUFPTR_THRESH)
#define AR_RXBP_THRESH_HP       0x0000000f
#define AR_RXBP_THRESH_HP_S     0
#define AR_RXBP_THRESH_LP       0x00003f00
#define AR_RXBP_THRESH_LP_S     8

/* Tx DMA Descriptor Pointer Threshold register */
#define AR_TXDP_THRESH       AR_MAC_DMA_OFFSET(MAC_DMA_TXDPPTR_THRESH)

/* Mac Interrupt rate threshold register */
#define AR_MIRT              AR_MAC_DMA_OFFSET(MAC_DMA_MIRT)
#define AR_MIRT_VAL          0x0000ffff // in uS
#define AR_MIRT_VAL_S        16

/* MAC Global Interrupt enable register */
#define AR_IER               AR_MAC_DMA_OFFSET(MAC_DMA_GLOBAL_IER)
#define AR_IER_ENABLE        0x00000001 // Global interrupt enable
#define AR_IER_DISABLE       0x00000000 // Global interrupt disable

/* Mac Tx Interrupt mitigation threshold */
#define AR_TIMT              AR_MAC_DMA_OFFSET(MAC_DMA_TIMT)
#define AR_TIMT_LAST         0x0000ffff // Last packet threshold
#define AR_TIMT_LAST_S       0
#define AR_TIMT_FIRST        0xffff0000 // First packet threshold
#define AR_TIMT_FIRST_S      16

/* Mac Rx Interrupt mitigation threshold */
#define AR_RIMT              AR_MAC_DMA_OFFSET(MAC_DMA_RIMT)
#define AR_RIMT_LAST         0x0000ffff // Last packet threshold
#define AR_RIMT_LAST_S       0
#define AR_RIMT_FIRST        0xffff0000 // First packet threshold
#define AR_RIMT_FIRST_S      16

#define AR_DMASIZE_4B        0x00000000 // DMA size 4 bytes (TXCFG + RXCFG)
#define AR_DMASIZE_8B        0x00000001 // DMA size 8 bytes
#define AR_DMASIZE_16B       0x00000002 // DMA size 16 bytes
#define AR_DMASIZE_32B       0x00000003 // DMA size 32 bytes
#define AR_DMASIZE_64B       0x00000004 // DMA size 64 bytes
#define AR_DMASIZE_128B      0x00000005 // DMA size 128 bytes
#define AR_DMASIZE_256B      0x00000006 // DMA size 256 bytes
#define AR_DMASIZE_512B      0x00000007 // DMA size 512 bytes

/* MAC Tx DMA size config register */
#define AR_TXCFG             AR_MAC_DMA_OFFSET(MAC_DMA_TXCFG)
#define AR_TXCFG_DMASZ_MASK  0x00000007
#define AR_TXCFG_DMASZ_4B    0
#define AR_TXCFG_DMASZ_8B    1
#define AR_TXCFG_DMASZ_16B   2
#define AR_TXCFG_DMASZ_32B   3
#define AR_TXCFG_DMASZ_64B   4
#define AR_TXCFG_DMASZ_128B  5
#define AR_TXCFG_DMASZ_256B  6
#define AR_TXCFG_DMASZ_512B  7
#define AR_FTRIG             0x000003F0 // Mask for Frame trigger level
#define AR_FTRIG_S           4          // Shift for Frame trigger level
#define AR_FTRIG_IMMED       0x00000000 // bytes in PCU TX FIFO before air
#define AR_FTRIG_64B         0x00000010 // default
#define AR_FTRIG_128B        0x00000020
#define AR_FTRIG_192B        0x00000030
#define AR_FTRIG_256B        0x00000040 // 5 bits total
#define AR_FTRIG_512B        0x00000080 // 5 bits total
#define AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY 0x00000800
#define AR_TXCFG_RTS_FAIL_EXCESSIVE_RETRIES     0x00080000
#define AR_TXCFG_RTS_FAIL_EXCESSIVE_RETRIES_S   19

/* MAC Rx DMA size config register */
#define AR_RXCFG             AR_MAC_DMA_OFFSET(MAC_DMA_RXCFG)
#define AR_RXCFG_CHIRP       0x00000008 // Only double chirps
#define AR_RXCFG_ZLFDMA      0x00000010 // Enable DMA of zero-length frame
#define AR_RXCFG_DMASZ_MASK  0x00000007
#define AR_RXCFG_DMASZ_4B    0
#define AR_RXCFG_DMASZ_8B    1
#define AR_RXCFG_DMASZ_16B   2
#define AR_RXCFG_DMASZ_32B   3
#define AR_RXCFG_DMASZ_64B   4
#define AR_RXCFG_DMASZ_128B  5
#define AR_RXCFG_DMASZ_256B  6
#define AR_RXCFG_DMASZ_512B  7

/* MAC Rx jumbo descriptor last address register */
#define AR_RXJLA             AR_MAC_DMA_OFFSET(MAC_DMA_RXJLA)


/* MAC MIB control register */
#define AR_MIBC              AR_MAC_DMA_OFFSET(MAC_DMA_MIBC)
#define AR_MIBC_COW          0x00000001 // counter overflow warning
#define AR_MIBC_FMC          0x00000002 // freeze MIB counters
#define AR_MIBC_CMC          0x00000004 // clear MIB counters
#define AR_MIBC_MCS          0x00000008 // MIB counter strobe increment all

/* MAC timeout prescale count */
#define AR_TOPS              AR_MAC_DMA_OFFSET(MAC_DMA_TOPS)
#define AR_TOPS_MASK         0x0000FFFF // Mask for timeout prescale

/* MAC no frame received timeout */
#define AR_RXNPTO            AR_MAC_DMA_OFFSET(MAC_DMA_RXNPTO)
#define AR_RXNPTO_MASK       0x000003FF // Mask for no frame received timeout

/* MAC no frame trasmitted timeout */
#define AR_TXNPTO            AR_MAC_DMA_OFFSET(MAC_DMA_TXNPTO)
#define AR_TXNPTO_MASK       0x000003FF // Mask for no frame transmitted timeout
#define AR_TXNPTO_QCU_MASK   0x000FFC00 // Mask indicating the set of QCUs
                                        // for which frame completions will cause
                                        // a reset of the no frame transmitted timeout

/* MAC receive frame gap timeout */
#define AR_RPGTO             AR_MAC_DMA_OFFSET(MAC_DMA_RPGTO)
#define AR_RPGTO_MASK        0x000003FF // Mask for receive frame gap timeout

/* MAC miscellaneous control/status register */
#define AR_MACMISC           AR_MAC_DMA_OFFSET(MAC_DMA_MACMISC)
#define AR_MACMISC_PCI_EXT_FORCE        0x00000010 //force msb to 10 to ahb
#define AR_MACMISC_DMA_OBS              0x000001E0 // Mask for DMA observation bus mux select
#define AR_MACMISC_DMA_OBS_S            5          // Shift for DMA observation bus mux select
#define AR_MACMISC_DMA_OBS_LINE_0       0          // Observation DMA line 0
#define AR_MACMISC_DMA_OBS_LINE_1       1          // Observation DMA line 1
#define AR_MACMISC_DMA_OBS_LINE_2       2          // Observation DMA line 2
#define AR_MACMISC_DMA_OBS_LINE_3       3          // Observation DMA line 3
#define AR_MACMISC_DMA_OBS_LINE_4       4          // Observation DMA line 4
#define AR_MACMISC_DMA_OBS_LINE_5       5          // Observation DMA line 5
#define AR_MACMISC_DMA_OBS_LINE_6       6          // Observation DMA line 6
#define AR_MACMISC_DMA_OBS_LINE_7       7          // Observation DMA line 7
#define AR_MACMISC_DMA_OBS_LINE_8       8          // Observation DMA line 8
#define AR_MACMISC_MISC_OBS             0x00000E00 // Mask for MISC observation bus mux select
#define AR_MACMISC_MISC_OBS_S           9          // Shift for MISC observation bus mux select
#define AR_MACMISC_MISC_OBS_BUS_LSB     0x00007000 // Mask for MAC observation bus mux select (lsb)
#define AR_MACMISC_MISC_OBS_BUS_LSB_S   12         // Shift for MAC observation bus mux select (lsb)
#define AR_MACMISC_MISC_OBS_BUS_MSB     0x00038000 // Mask for MAC observation bus mux select (msb)
#define AR_MACMISC_MISC_OBS_BUS_MSB_S   15         // Shift for MAC observation bus mux select (msb)
#define AR_MACMISC_MISC_OBS_BUS_1       1          // MAC observation bus mux select

/* MAC Interrupt Config register */
#define AR_INTCFG             AR_MAC_DMA_OFFSET(MAC_DMA_INTER)
#define AR_INTCFG_REQ         0x00000001    // Interrupt request flag
                                            // Indicates whether the DMA engine should generate
                                            // an interrupt upon completion of the frame
#define AR_INTCFG_MSI_RXOK    0x00000000    // Rx interrupt for MSI logic is RXOK
#define AR_INTCFG_MSI_RXINTM  0x00000004    // Rx interrupt for MSI logic is RXINTM
#define AR_INTCFG_MSI_RXMINTR 0x00000006    // Rx interrupt for MSI logic is RXMINTR
#define AR_INTCFG_MSI_TXOK    0x00000000    // Rx interrupt for MSI logic is TXOK
#define AR_INTCFG_MSI_TXINTM  0x00000010    // Rx interrupt for MSI logic is TXINTM
#define AR_INTCFG_MSI_TXMINTR 0x00000018    // Rx interrupt for MSI logic is TXMINTR

/* MAC DMA Data Buffer length, in bytes */
#define AR_DATABUF            AR_MAC_DMA_OFFSET(MAC_DMA_DATABUF)
#define AR_DATABUF_MASK       0x00000FFF

/* MAC global transmit timeout */
#define AR_GTXTO                    AR_MAC_DMA_OFFSET(MAC_DMA_GTT)
#define AR_GTXTO_TIMEOUT_COUNTER    0x0000FFFF  // Mask for timeout counter (in TUs)
#define AR_GTXTO_TIMEOUT_LIMIT      0xFFFF0000  // Mask for timeout limit (in TUs)
#define AR_GTXTO_TIMEOUT_LIMIT_S    16          // Shift for timeout limit

/* MAC global transmit timeout mode */
#define AR_GTTM               AR_MAC_DMA_OFFSET(MAC_DMA_GTTM)
#define AR_GTTM_USEC          0x00000001 // usec strobe
#define AR_GTTM_IGNORE_IDLE   0x00000002 // ignore channel idle
#define AR_GTTM_RESET_IDLE    0x00000004 // reset counter on channel idle low
#define AR_GTTM_CST_USEC      0x00000008 // CST usec strobe

/* MAC carrier sense timeout */
#define AR_CST                    AR_MAC_DMA_OFFSET(MAC_DMA_CST)
#define AR_CST_TIMEOUT_COUNTER    0x0000FFFF  // Mask for timeout counter (in TUs)
#define AR_CST_TIMEOUT_LIMIT      0xFFFF0000  // Mask for timeout limit (in  TUs)
#define AR_CST_TIMEOUT_LIMIT_S    16          // Shift for timeout limit

/* MAC Indicates the size of High and Low priority rx_dp FIFOs */
#define AR_RXDP_SIZE          AR_MAC_DMA_OFFSET(MAC_DMA_RXDP_SIZE)
#define AR_RXDP_LP_SZ_MASK    0x0000007f
#define AR_RXDP_LP_SZ_S       0
#define AR_RXDP_HP_SZ_MASK    0x00001f00
#define AR_RXDP_HP_SZ_S       8

/* MAC Rx High Priority Queue RXDP Pointer (lower 32 bits) */
#define AR_HP_RXDP            AR_MAC_DMA_OFFSET(MAC_DMA_RX_QUEUE_HP_RXDP)

/* MAC Rx Low Priority Queue RXDP Pointer (lower 32 bits) */
#define AR_LP_RXDP            AR_MAC_DMA_OFFSET(MAC_DMA_RX_QUEUE_LP_RXDP)


/* Primary Interrupt Status Register */
#define AR_ISR               AR_MAC_DMA_OFFSET(MAC_DMA_ISR_P)
#define AR_ISR_HP_RXOK       0x00000001 // At least one frame rx on high-priority queue sans errors
#define AR_ISR_LP_RXOK       0x00000002 // At least one frame rx on low-priority queue sans errors
#define AR_ISR_RXERR         0x00000004 // Receive error interrupt
#define AR_ISR_RXNOPKT       0x00000008 // No frame received within timeout clock
#define AR_ISR_RXEOL         0x00000010 // Received descriptor empty interrupt
#define AR_ISR_RXORN         0x00000020 // Receive FIFO overrun interrupt
#define AR_ISR_TXOK          0x00000040 // Transmit okay interrupt
#define AR_ISR_TXERR         0x00000100 // Transmit error interrupt
#define AR_ISR_TXNOPKT       0x00000200 // No frame transmitted interrupt
#define AR_ISR_TXEOL         0x00000400 // Transmit descriptor empty interrupt
#define AR_ISR_TXURN         0x00000800 // Transmit FIFO underrun interrupt
#define AR_ISR_MIB           0x00001000 // MIB interrupt - see MIBC
#define AR_ISR_SWI           0x00002000 // Software interrupt
#define AR_ISR_RXPHY         0x00004000 // PHY receive error interrupt
#define AR_ISR_RXKCM         0x00008000 // Key-cache miss interrupt
#define AR_ISR_SWBA          0x00010000 // Software beacon alert interrupt
#define AR_ISR_BRSSI         0x00020000 // Beacon threshold interrupt
#define AR_ISR_BMISS         0x00040000 // Beacon missed interrupt
#define AR_ISR_TXMINTR       0x00080000 // Maximum interrupt transmit rate
#define AR_ISR_BNR           0x00100000 // Beacon not ready interrupt
#define AR_ISR_RXCHIRP       0x00200000 // Phy received a 'chirp'
#define AR_ISR_HCFPOLL       0x00400000 // Received directed HCF poll
#define AR_ISR_BCNMISC       0x00800000 // CST, GTT, TIM, CABEND, DTIMSYNC, BCNTO, CABTO,
                                        // TSFOOR, DTIM, and TBTT_TIME bits bits from ISR_S2
#define AR_ISR_TIM           0x00800000 // TIM interrupt
#define AR_ISR_RXMINTR       0x01000000 // Maximum interrupt receive rate
#define AR_ISR_QCBROVF       0x02000000 // QCU CBR overflow interrupt
#define AR_ISR_QCBRURN       0x04000000 // QCU CBR underrun interrupt
#define AR_ISR_QTRIG         0x08000000 // QCU scheduling trigger interrupt
#define AR_ISR_GENTMR        0x10000000 // OR of generic timer bits in ISR 5
#define AR_ISR_HCFTO         0x20000000 // HCF poll timeout
#define AR_ISR_TXINTM        0x40000000 // Tx interrupt after mitigation
#define AR_ISR_RXINTM        0x80000000 // Rx interrupt after mitigation

/* MAC Secondary interrupt status register 0 */
#define AR_ISR_S0               AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S0)
#define AR_ISR_S0_QCU_TXOK      0x000003FF // Mask for TXOK (QCU 0-9)
#define AR_ISR_S0_QCU_TXOK_S    0          // Shift for TXOK (QCU 0-9)

/* MAC Secondary interrupt status register 1 */
#define AR_ISR_S1              AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S1)
#define AR_ISR_S1_QCU_TXERR    0x000003FF // Mask for TXERR (QCU 0-9)
#define AR_ISR_S1_QCU_TXERR_S  0          // Shift for TXERR (QCU 0-9)
#define AR_ISR_S1_QCU_TXEOL    0x03FF0000 // Mask for TXEOL (QCU 0-9)
#define AR_ISR_S1_QCU_TXEOL_S  16         // Shift for TXEOL (QCU 0-9)

/* MAC Secondary interrupt status register 2 */
#define AR_ISR_S2              AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S2)
#define AR_ISR_S2_QCU_TXURN    0x000003FF // Mask for TXURN (QCU 0-9)
#define AR_ISR_S2_BBPANIC      0x00010000 // Panic watchdog IRQ from BB
#define AR_ISR_S2_CST          0x00400000 // Carrier sense timeout
#define AR_ISR_S2_GTT          0x00800000 // Global transmit timeout
#define AR_ISR_S2_TIM          0x01000000 // TIM
#define AR_ISR_S2_CABEND       0x02000000 // CABEND
#define AR_ISR_S2_DTIMSYNC     0x04000000 // DTIMSYNC
#define AR_ISR_S2_BCNTO        0x08000000 // BCNTO
#define AR_ISR_S2_CABTO        0x10000000 // CABTO
#define AR_ISR_S2_DTIM         0x20000000 // DTIM
#define AR_ISR_S2_TSFOOR       0x40000000 // Rx TSF out of range
#define AR_ISR_S2_TBTT_TIME    0x80000000 // TBTT-referenced timer

/* MAC Secondary interrupt status register 3 */
#define AR_ISR_S3                AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S3)
#define AR_ISR_S3_QCU_QCBROVF    0x000003FF // Mask for QCBROVF (QCU 0-9)
#define AR_ISR_S3_QCU_QCBRURN    0x03FF0000 // Mask for QCBRURN (QCU 0-9)

/* MAC Secondary interrupt status register 4 */
#define AR_ISR_S4              AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S4)
#define AR_ISR_S4_QCU_QTRIG    0x000003FF // Mask for QTRIG (QCU 0-9)
#define AR_ISR_S4_RESV0        0xFFFFFC00 // Reserved

/* MAC Secondary interrupt status register 5 */
#define AR_ISR_S5                   AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S5)
#define AR_ISR_S5_TIMER_TRIG        0x000000FF // Mask for timer trigger (0-7)
#define AR_ISR_S5_TIMER_THRESH      0x0007FE00 // Mask for timer threshold(0-7)
#define AR_ISR_S5_TIM_TIMER         0x00000010 // TIM Timer ISR
#define AR_ISR_S5_DTIM_TIMER        0x00000020 // DTIM Timer ISR
#define AR_ISR_S5_GENTIMER_TRIG     0x0000FF80 // ISR for generic timer trigger 7
#define AR_ISR_S5_GENTIMER_TRIG_S   0
#define AR_ISR_S5_GENTIMER_THRESH   0xFF800000 // ISR for generic timer threshold 7
#define AR_ISR_S5_GENTIMER_THRESH_S 16

/* Primary Interrupt Mask Register */
#define AR_IMR               AR_MAC_DMA_OFFSET(MAC_DMA_IMR_P)
#define AR_IMR_RXOK_HP       0x00000001 // Receive high-priority interrupt enable mask
#define AR_IMR_RXOK_LP       0x00000002 // Receive low-priority interrupt enable mask
#define AR_IMR_RXERR         0x00000004 // Receive error interrupt
#define AR_IMR_RXNOPKT       0x00000008 // No frame received within timeout clock
#define AR_IMR_RXEOL         0x00000010 // Received descriptor empty interrupt
#define AR_IMR_RXORN         0x00000020 // Receive FIFO overrun interrupt
#define AR_IMR_TXOK          0x00000040 // Transmit okay interrupt
#define AR_IMR_TXERR         0x00000100 // Transmit error interrupt
#define AR_IMR_TXNOPKT       0x00000200 // No frame transmitted interrupt
#define AR_IMR_TXEOL         0x00000400 // Transmit descriptor empty interrupt
#define AR_IMR_TXURN         0x00000800 // Transmit FIFO underrun interrupt
#define AR_IMR_MIB           0x00001000 // MIB interrupt - see MIBC
#define AR_IMR_SWI           0x00002000 // Software interrupt
#define AR_IMR_RXPHY         0x00004000 // PHY receive error interrupt
#define AR_IMR_RXKCM         0x00008000 // Key-cache miss interrupt
#define AR_IMR_SWBA          0x00010000 // Software beacon alert interrupt
#define AR_IMR_BRSSI         0x00020000 // Beacon threshold interrupt
#define AR_IMR_BMISS         0x00040000 // Beacon missed interrupt
#define AR_IMR_TXMINTR       0x00080000 // Maximum interrupt transmit rate
#define AR_IMR_BNR           0x00100000 // BNR interrupt
#define AR_IMR_RXCHIRP       0x00200000 // RXCHIRP interrupt
#define AR_IMR_BCNMISC       0x00800000 // Venice: BCNMISC
#define AR_IMR_TIM           0x00800000 // TIM interrupt
#define AR_IMR_RXMINTR       0x01000000 // Maximum interrupt receive rate
#define AR_IMR_QCBROVF       0x02000000 // QCU CBR overflow interrupt
#define AR_IMR_QCBRURN       0x04000000 // QCU CBR underrun interrupt
#define AR_IMR_QTRIG         0x08000000 // QCU scheduling trigger interrupt
#define AR_IMR_GENTMR        0x10000000 // Generic timer interrupt
#define AR_IMR_TXINTM        0x40000000 // Tx interrupt after mitigation
#define AR_IMR_RXINTM        0x80000000 // Rx interrupt after mitigation

/* MAC Secondary interrupt mask register 0 */
#define AR_IMR_S0               AR_MAC_DMA_OFFSET(MAC_DMA_IMR_S0)
#define AR_IMR_S0_QCU_TXOK      0x000003FF // Mask for TXOK (QCU 0-9)
#define AR_IMR_S0_QCU_TXOK_S    0          // Shift for TXOK (QCU 0-9)

/* MAC Secondary interrupt mask register 1 */
#define AR_IMR_S1              AR_MAC_DMA_OFFSET(MAC_DMA_IMR_S1)
#define AR_IMR_S1_QCU_TXERR    0x000003FF // Mask for TXERR (QCU 0-9)
#define AR_IMR_S1_QCU_TXERR_S  0          // Shift for TXERR (QCU 0-9)
#define AR_IMR_S1_QCU_TXEOL    0x03FF0000 // Mask for TXEOL (QCU 0-9)
#define AR_IMR_S1_QCU_TXEOL_S  16         // Shift for TXEOL (QCU 0-9)

/* MAC Secondary interrupt mask register 2 */
#define AR_IMR_S2              AR_MAC_DMA_OFFSET(MAC_DMA_IMR_S2)
#define AR_IMR_S2_QCU_TXURN    0x000003FF // Mask for TXURN (QCU 0-9)
#define AR_IMR_S2_QCU_TXURN_S  0          // Shift for TXURN (QCU 0-9)
#define AR_IMR_S2_BBPANIC      0x00010000 // Panic watchdog IRQ from BB
#define AR_IMR_S2_CST          0x00400000 // Carrier sense timeout
#define AR_IMR_S2_GTT          0x00800000 // Global transmit timeout
#define AR_IMR_S2_TIM          0x01000000 // TIM
#define AR_IMR_S2_CABEND       0x02000000 // CABEND
#define AR_IMR_S2_DTIMSYNC     0x04000000 // DTIMSYNC
#define AR_IMR_S2_BCNTO        0x08000000 // BCNTO
#define AR_IMR_S2_CABTO        0x10000000 // CABTO
#define AR_IMR_S2_DTIM         0x20000000 // DTIM
#define AR_IMR_S2_TSFOOR       0x40000000 // TSF out of range

/* MAC Secondary interrupt mask register 3 */
#define AR_IMR_S3                AR_MAC_DMA_OFFSET(MAC_DMA_IMR_S3)
#define AR_IMR_S3_QCU_QCBROVF    0x000003FF // Mask for QCBROVF (QCU 0-9)
#define AR_IMR_S3_QCU_QCBRURN    0x03FF0000 // Mask for QCBRURN (QCU 0-9)
#define AR_IMR_S3_QCU_QCBRURN_S  16         // Shift for QCBRURN (QCU 0-9)

/* MAC Secondary interrupt mask register 4 */
#define AR_IMR_S4              AR_MAC_DMA_OFFSET(MAC_DMA_IMR_S4)
#define AR_IMR_S4_QCU_QTRIG    0x000003FF // Mask for QTRIG (QCU 0-9)
#define AR_IMR_S4_RESV0        0xFFFFFC00 // Reserved

/* MAC Secondary interrupt mask register 5 */
#define AR_IMR_S5                   AR_MAC_DMA_OFFSET(MAC_DMA_IMR_S5)
#define AR_IMR_S5_TIMER_TRIG        0x000000FF // Mask for timer trigger (0-7)
#define AR_IMR_S5_TIMER_THRESH      0x0000FF00 // Mask for timer threshold(0-7)
#define AR_IMR_S5_TIM_TIMER         0x00000010 // TIM Timer Mask
#define AR_IMR_S5_DTIM_TIMER        0x00000020 // DTIM Timer Mask
#define AR_IMR_S5_GENTIMER7         0x00000080 // Mask for timer 7 trigger
#define AR_IMR_S5_GENTIMER_TRIG     0x0000FF80 // Mask for generic timer trigger 7-15
#define AR_IMR_S5_GENTIMER_TRIG_S   0
#define AR_IMR_S5_GENTIMER_THRESH   0xFF800000 // Mask for generic timer threshold 7-15
#define AR_IMR_S5_GENTIMER_THRESH_S 16


/* Interrupt status registers (read-and-clear access secondary shadow copies) */

/* MAC Primary interrupt status register read-and-clear access */
#define AR_ISR_RAC      AR_MAC_DMA_OFFSET(MAC_DMA_ISR_P_RAC)
/* MAC Secondary interrupt status register 0 - shadow copy */
#define AR_ISR_S0_S     AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S0_S)
#define AR_ISR_S0_QCU_TXOK      0x000003FF // Mask for TXOK (QCU 0-9)
#define AR_ISR_S0_QCU_TXOK_S    0          // Shift for TXOK (QCU 0-9)

/* MAC Secondary interrupt status register 1 - shadow copy */
#define AR_ISR_S1_S            AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S1_S)
#define AR_ISR_S1_QCU_TXERR    0x000003FF // Mask for TXERR (QCU 0-9)
#define AR_ISR_S1_QCU_TXERR_S  0          // Shift for TXERR (QCU 0-9)
#define AR_ISR_S1_QCU_TXEOL    0x03FF0000 // Mask for TXEOL (QCU 0-9)
#define AR_ISR_S1_QCU_TXEOL_S  16         // Shift for TXEOL (QCU 0-9)

/* MAC Secondary interrupt status register 2 - shadow copy */
#define AR_ISR_S2_S           AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S2_S)
/* MAC Secondary interrupt status register 3 - shadow copy */
#define AR_ISR_S3_S           AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S3_S)
/* MAC Secondary interrupt status register 4 - shadow copy */
#define AR_ISR_S4_S           AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S4_S)
/* MAC Secondary interrupt status register 5 - shadow copy */
#define AR_ISR_S5_S           AR_MAC_DMA_OFFSET(MAC_DMA_ISR_S5_S)

/* MAC DMA Debug Registers */
#define AR_DMADBG_0           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_0)
#define AR_DMADBG_1           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_1)
#define AR_DMADBG_2           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_2)
#define AR_DMADBG_3           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_3)
#define AR_DMADBG_4           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_4)
#define AR_DMADBG_5           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_5)
#define AR_DMADBG_6           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_6)
#define AR_DMADBG_7           AR_MAC_DMA_OFFSET(MAC_DMA_DMADBG_7)
#define AR_DMATXDP_QCU_7_0    AR_MAC_DMA_OFFSET(MAC_DMA_QCU_TXDP_REMAINING_QCU_7_0)
#define AR_DMATXDP_QCU_9_8    AR_MAC_DMA_OFFSET(MAC_DMA_QCU_TXDP_REMAINING_QCU_9_8)

#define AR_DMADBG_RX_STATE    0x00000F00 // Mask for Rx DMA State machine


/*
 * MAC QCU Registers
 */
#define AR_MAC_QCU_OFFSET(_x)   offsetof(struct mac_qcu_reg, _x)

#define AR_NUM_QCU      10     // Only use QCU 0-9 for forward QCU compatibility
#define AR_QCU_0        0x0001
#define AR_QCU_1        0x0002
#define AR_QCU_2        0x0004
#define AR_QCU_3        0x0008
#define AR_QCU_4        0x0010
#define AR_QCU_5        0x0020
#define AR_QCU_6        0x0040
#define AR_QCU_7        0x0080
#define AR_QCU_8        0x0100
#define AR_QCU_9        0x0200

/* MAC Transmit Queue descriptor pointer */
#define AR_Q0_TXDP      AR_MAC_QCU_OFFSET(MAC_QCU_TXDP)
#define AR_QTXDP(_i)    (AR_Q0_TXDP + ((_i)<<2))

/* MAC Transmit Status Ring Start Address */
#define AR_Q_STATUS_RING_START    AR_MAC_QCU_OFFSET(MAC_QCU_STATUS_RING_START)
/* MAC Transmit Status Ring End Address */
#define AR_Q_STATUS_RING_END      AR_MAC_QCU_OFFSET(MAC_QCU_STATUS_RING_END)
/* Current Address in the Transmit Status Ring pointed to by the MAC */
#define AR_Q_STATUS_RING_CURRENT  AR_MAC_QCU_OFFSET(MAC_QCU_STATUS_RING_CURRENT)

/* MAC Transmit Queue enable */
#define AR_Q_TXE             AR_MAC_QCU_OFFSET(MAC_QCU_TXE)
#define AR_Q_TXE_M           0x000003FF // Mask for TXE (QCU 0-9)

/* MAC Transmit Queue disable */
#define AR_Q_TXD             AR_MAC_QCU_OFFSET(MAC_QCU_TXD)
#define AR_Q_TXD_M           0x000003FF // Mask for TXD (QCU 0-9)

/* MAC CBR configuration */
#define AR_Q0_CBRCFG         AR_MAC_QCU_OFFSET(MAC_QCU_CBR)
#define AR_QCBRCFG(_i)      (AR_Q0_CBRCFG + ((_i)<<2))
#define AR_Q_CBRCFG_INTERVAL     0x00FFFFFF // Mask for CBR interval (us)
#define AR_Q_CBRCFG_INTERVAL_S   0          // Shift for CBR interval (us)
#define AR_Q_CBRCFG_OVF_THRESH   0xFF000000 // Mask for CBR overflow threshold
#define AR_Q_CBRCFG_OVF_THRESH_S 24         // Shift for CBR overflow threshold

/* MAC ready_time configuration */
#define AR_Q0_RDYTIMECFG         AR_MAC_QCU_OFFSET(MAC_QCU_RDYTIME)
#define AR_QRDYTIMECFG(_i)       (AR_Q0_RDYTIMECFG + ((_i)<<2))
#define AR_Q_RDYTIMECFG_DURATION   0x00FFFFFF // Mask for ready_time duration (us)
#define AR_Q_RDYTIMECFG_DURATION_S 0          // Shift for ready_time duration (us)
#define AR_Q_RDYTIMECFG_EN         0x01000000 // ready_time enable

/* MAC OneShotArm set control */
#define AR_Q_ONESHOTARM_SC       AR_MAC_QCU_OFFSET(MAC_QCU_ONESHOT_ARM_SC)
#define AR_Q_ONESHOTARM_SC_M     0x000003FF // Mask for #define AR_Q_ONESHOTARM_SC (QCU 0-9)
#define AR_Q_ONESHOTARM_SC_RESV0 0xFFFFFC00 // Reserved

/* MAC OneShotArm clear control */
#define AR_Q_ONESHOTARM_CC       AR_MAC_QCU_OFFSET(MAC_QCU_ONESHOT_ARM_CC)
#define AR_Q_ONESHOTARM_CC_M     0x000003FF // Mask for #define AR_Q_ONESHOTARM_CC (QCU 0-9)
#define AR_Q_ONESHOTARM_CC_RESV0 0xFFFFFC00 // Reserved

/* MAC Miscellaneous QCU settings */
#define AR_Q0_MISC                        AR_MAC_QCU_OFFSET(MAC_QCU_MISC)
#define AR_QMISC(_i)                      (AR_Q0_MISC + ((_i)<<2))
#define AR_Q_MISC_FSP                     0x0000000F // Mask for Frame Scheduling Policy
#define AR_Q_MISC_FSP_S                   0
#define AR_Q_MISC_FSP_ASAP                0          // ASAP
#define AR_Q_MISC_FSP_CBR                 1          // CBR
#define AR_Q_MISC_FSP_DBA_GATED           2          // DMA Beacon Alert gated
#define AR_Q_MISC_FSP_TIM_GATED           3          // TIM gated
#define AR_Q_MISC_FSP_BEACON_SENT_GATED   4          // Beacon-sent-gated
#define AR_Q_MISC_FSP_BEACON_RCVD_GATED   5          // Beacon-received-gated
#define AR_Q_MISC_ONE_SHOT_EN             0x00000010 // OneShot enable
#define AR_Q_MISC_CBR_INCR_DIS1           0x00000020 // Disable CBR expired counter incr (empty q)
#define AR_Q_MISC_CBR_INCR_DIS0           0x00000040 // Disable CBR expired counter incr (empty beacon q)
#define AR_Q_MISC_BEACON_USE              0x00000080 // Beacon use indication
#define AR_Q_MISC_CBR_EXP_CNTR_LIMIT_EN   0x00000100 // CBR expired counter limit enable
#define AR_Q_MISC_RDYTIME_EXP_POLICY      0x00000200 // Enable TXE cleared on ready_time expired or VEOL
#define AR_Q_MISC_RESET_CBR_EXP_CTR       0x00000400 // Reset CBR expired counter
#define AR_Q_MISC_DCU_EARLY_TERM_REQ      0x00000800 // DCU frame early termination request control
#define AR_Q_MISC_RESV0                   0xFFFFF000 // Reserved

/* MAC Miscellaneous QCU status */
#define AR_Q0_STS                     AR_MAC_QCU_OFFSET(MAC_QCU_CNT)
#define AR_QSTS(_i)                   (AR_Q0_STS + ((_i)<<2))
#define AR_Q_STS_PEND_FR_CNT          0x00000003 // Mask for Pending Frame Count
#define AR_Q_STS_RESV0                0x000000FC // Reserved
#define AR_Q_STS_CBR_EXP_CNT          0x0000FF00 // Mask for CBR expired counter
#define AR_Q_STS_RESV1                0xFFFF0000 // Reserved

/* MAC ReadyTimeShutdown status */
#define AR_Q_RDYTIMESHDN    AR_MAC_QCU_OFFSET(MAC_QCU_RDYTIME_SHDN)
#define AR_Q_RDYTIMESHDN_M  0x000003FF // Mask for ReadyTimeShutdown status (QCU 0-9)

/* MAC Descriptor CRC check */
#define AR_Q_DESC_CRCCHK    AR_MAC_QCU_OFFSET(MAC_QCU_DESC_CRC_CHK)
#define AR_Q_DESC_CRCCHK_EN 1 // Enable CRC check on the descriptor fetched from HOST

#define AR_MAC_QCU_EOL      AR_MAC_QCU_OFFSET(MAC_QCU_EOL)
#define AR_MAC_QCU_EOL_DUR_CAL_EN   0x000003FF // Adjusts EOL for frame duration (QCU 0-9) 
#define AR_MAC_QCU_EOL_DUR_CAL_EN_S 0

/*
 * MAC DCU Registers
 */

#define AR_MAC_DCU_OFFSET(_x)   offsetof(struct mac_dcu_reg, _x)

#define AR_NUM_DCU      10     // Only use 10 DCU's for forward QCU/DCU compatibility
#define AR_DCU_0        0x0001
#define AR_DCU_1        0x0002
#define AR_DCU_2        0x0004
#define AR_DCU_3        0x0008
#define AR_DCU_4        0x0010
#define AR_DCU_5        0x0020
#define AR_DCU_6        0x0040
#define AR_DCU_7        0x0080
#define AR_DCU_8        0x0100
#define AR_DCU_9        0x0200

/* MAC QCU Mask */
#define AR_D0_QCUMASK     AR_MAC_DCU_OFFSET(MAC_DCU_QCUMASK)
#define AR_DQCUMASK(_i)   (AR_D0_QCUMASK + ((_i)<<2))
#define AR_D_QCUMASK         0x000003FF // Mask for QCU Mask (QCU 0-9)
#define AR_D_QCUMASK_RESV0   0xFFFFFC00 // Reserved

/* DCU transmit filter cmd (w/only) */
#define AR_D_TXBLK_CMD  AR_MAC_DCU_OFFSET(MAC_DCU_TXFILTER_DCU0_31_0)
#define AR_D_TXBLK_DATA(i) (AR_D_TXBLK_CMD+(i)) // DCU transmit filter data


/* MAC DCU-global IFS settings: SIFS duration */
#define AR_D_GBL_IFS_SIFS         AR_MAC_DCU_OFFSET(MAC_DCU_GBL_IFS_SIFS)
#define AR_D_GBL_IFS_SIFS_M       0x0000FFFF // Mask for SIFS duration (core clocks)
#define AR_D_GBL_IFS_SIFS_RESV0   0xFFFFFFFF // Reserved

/* MAC DCU-global IFS settings: slot duration */
#define AR_D_GBL_IFS_SLOT         AR_MAC_DCU_OFFSET(MAC_DCU_GBL_IFS_SLOT)
#define AR_D_GBL_IFS_SLOT_M       0x0000FFFF // Mask for Slot duration (core clocks)
#define AR_D_GBL_IFS_SLOT_RESV0   0xFFFF0000 // Reserved

/* MAC Retry limits */
#define AR_D0_RETRY_LIMIT     AR_MAC_DCU_OFFSET(MAC_DCU_RETRY_LIMIT)
#define AR_DRETRY_LIMIT(_i)   (AR_D0_RETRY_LIMIT + ((_i)<<2))
#define AR_D_RETRY_LIMIT_FR_SH       0x0000000F // Mask for frame short retry limit
#define AR_D_RETRY_LIMIT_FR_SH_S     0          // Shift for frame short retry limit
#define AR_D_RETRY_LIMIT_STA_SH      0x00003F00 // Mask for station short retry limit
#define AR_D_RETRY_LIMIT_STA_SH_S    8          // Shift for station short retry limit
#define AR_D_RETRY_LIMIT_STA_LG      0x000FC000 // Mask for station short retry limit
#define AR_D_RETRY_LIMIT_STA_LG_S    14         // Shift for station short retry limit
#define AR_D_RETRY_LIMIT_RESV0       0xFFF00000 // Reserved

/* MAC DCU-global IFS settings: EIFS duration */
#define AR_D_GBL_IFS_EIFS         AR_MAC_DCU_OFFSET(MAC_DCU_GBL_IFS_EIFS)
#define AR_D_GBL_IFS_EIFS_M       0x0000FFFF // Mask for Slot duration (core clocks)
#define AR_D_GBL_IFS_EIFS_RESV0   0xFFFF0000 // Reserved

/* MAC ChannelTime settings */
#define AR_D0_CHNTIME     AR_MAC_DCU_OFFSET(MAC_DCU_CHANNEL_TIME)
#define AR_DCHNTIME(_i)   (AR_D0_CHNTIME + ((_i)<<2))
#define AR_D_CHNTIME_DUR         0x000FFFFF // Mask for ChannelTime duration (us)
#define AR_D_CHNTIME_DUR_S       0          // Shift for ChannelTime duration (us)
#define AR_D_CHNTIME_EN          0x00100000 // ChannelTime enable
#define AR_D_CHNTIME_RESV0       0xFFE00000 // Reserved

/* MAC DCU-global IFS settings: Miscellaneous */
#define AR_D_GBL_IFS_MISC        AR_MAC_DCU_OFFSET(MAC_DCU_GBL_IFS_MISC)
#define AR_D_GBL_IFS_MISC_LFSR_SLICE_SEL        0x00000007 // Mask forLFSR slice select
#define AR_D_GBL_IFS_MISC_TURBO_MODE            0x00000008 // Turbo mode indication
#define AR_D_GBL_IFS_MISC_DCU_ARBITER_DLY       0x00300000 // Mask for DCU arbiter delay
#define AR_D_GBL_IFS_MISC_RANDOM_LFSR_SLICE_DIS 0x01000000 // Random LSFR slice disable
#define AR_D_GBL_IFS_MISC_SLOT_XMIT_WIND_LEN    0x06000000 // Slot transmission window length mask
#define AR_D_GBL_IFS_MISC_FORCE_XMIT_SLOT_BOUND 0x08000000 // Force transmission on slot boundaries
#define AR_D_GBL_IFS_MISC_IGNORE_BACKOFF        0x10000000 // Ignore backoff

/* MAC Miscellaneous DCU-specific settings */
#define AR_D0_MISC        AR_MAC_DCU_OFFSET(MAC_DCU_MISC)
#define AR_DMISC(_i)      (AR_D0_MISC + ((_i)<<2))
#define AR_D_MISC_BKOFF_THRESH        0x0000003F // Mask for Backoff threshold setting
#define AR_D_MISC_RETRY_CNT_RESET_EN  0x00000040 // End of tx series station RTS/data failure count reset policy
#define AR_D_MISC_CW_RESET_EN         0x00000080 // End of tx series CW reset enable
#define AR_D_MISC_FRAG_WAIT_EN        0x00000100 // Fragment Starvation Policy
#define AR_D_MISC_FRAG_BKOFF_EN       0x00000200 // Backoff during a frag burst
#define AR_D_MISC_CW_BKOFF_EN         0x00001000 // Use binary exponential CW backoff
#define AR_D_MISC_VIR_COL_HANDLING    0x0000C000 // Mask for Virtual collision handling policy
#define AR_D_MISC_VIR_COL_HANDLING_S  14         // Shift for Virtual collision handling policy
#define AR_D_MISC_VIR_COL_HANDLING_DEFAULT 0     // Normal
#define AR_D_MISC_VIR_COL_HANDLING_IGNORE  1     // Ignore
#define AR_D_MISC_BEACON_USE          0x00010000 // Beacon use indication
#define AR_D_MISC_ARB_LOCKOUT_CNTRL   0x00060000 // Mask for DCU arbiter lockout control
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_S 17         // Shift for DCU arbiter lockout control
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_NONE     0   // No lockout
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_INTRA_FR 1   // Intra-frame
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL   2   // Global
#define AR_D_MISC_ARB_LOCKOUT_IGNORE  0x00080000 // DCU arbiter lockout ignore control
#define AR_D_MISC_SEQ_NUM_INCR_DIS    0x00100000 // Sequence number increment disable
#define AR_D_MISC_POST_FR_BKOFF_DIS   0x00200000 // Post-frame backoff disable
#define AR_D_MISC_VIT_COL_CW_BKOFF_EN 0x00400000 // Virtual coll. handling policy
#define AR_D_MISC_BLOWN_IFS_RETRY_EN  0x00800000 // Initiate Retry procedure on Blown IFS
#define AR_D_MISC_RESV0               0xFF000000 // Reserved

/* MAC Frame sequence number control/status */
#define AR_D_SEQNUM      AR_MAC_DCU_OFFSET(MAC_DCU_SEQ)

/* MAC DCU transmit pause control/status */
#define AR_D_TXPSE                 AR_MAC_DCU_OFFSET(MAC_DCU_PAUSE)
#define AR_D_TXPSE_CTRL            0x000003FF // Mask of DCUs to pause (DCUs 0-9)
#define AR_D_TXPSE_RESV0           0x0000FC00 // Reserved
#define AR_D_TXPSE_STATUS          0x00010000 // Transmit pause status
#define AR_D_TXPSE_RESV1           0xFFFE0000 // Reserved

/* MAC DCU WOW Keep-Alive Config register */
#define AR_D_WOW_KACFG             AR_MAC_DCU_OFFSET(MAC_DCU_WOW_KACFG)

/* MAC DCU transmission slot mask */
#define AR_D_TXSLOTMASK            AR_MAC_DCU_OFFSET(MAC_DCU_TXSLOT)
#define AR_D_TXSLOTMASK_NUM        0x0000000F // slot numbers

/* MAC DCU-specific IFS settings */
#define AR_D0_LCL_IFS     AR_MAC_DCU_OFFSET(MAC_DCU_LCL_IFS)
#define AR_DLCL_IFS(_i)   (AR_D0_LCL_IFS + ((_i)<<2))
#define AR_D9_LCL_IFS     AR_DLCL_IFS(9)
#define AR_D_LCL_IFS_CWMIN       0x000003FF // Mask for CW_MIN
#define AR_D_LCL_IFS_CWMIN_S     0          // Shift for CW_MIN
#define AR_D_LCL_IFS_CWMAX       0x000FFC00 // Mask for CW_MAX
#define AR_D_LCL_IFS_CWMAX_S     10         // Shift for CW_MAX
#define AR_D_LCL_IFS_AIFS        0x0FF00000 // Mask for AIFS
#define AR_D_LCL_IFS_AIFS_S      20         // Shift for AIFS
    /*
     *  Note:  even though this field is 8 bits wide the
     *  maximum supported AIFS value is 0xfc.  Setting the AIFS value
     *  to 0xfd 0xfe or 0xff will not work correctly and will cause
     *  the DCU to hang.
     */
#define AR_D_LCL_IFS_RESV0    0xF0000000 // Reserved


#define AR_CFG_LED                     0x1f04 /* LED control */
#define AR_CFG_SCLK_RATE_IND           0x00000003 /* sleep clock indication */
#define AR_CFG_SCLK_RATE_IND_S         0
#define AR_CFG_SCLK_32MHZ              0x00000000 /* Sleep clock rate */
#define AR_CFG_SCLK_4MHZ               0x00000001 /* Sleep clock rate */
#define AR_CFG_SCLK_1MHZ               0x00000002 /* Sleep clock rate */
#define AR_CFG_SCLK_32KHZ              0x00000003 /* Sleep clock rate */
#define AR_CFG_LED_BLINK_SLOW          0x00000008 /* LED slowest blink rate mode */
#define AR_CFG_LED_BLINK_THRESH_SEL    0x00000070 /* LED blink threshold select */
#define AR_CFG_LED_MODE_SEL            0x00000380 /* LED mode: bits 7..9 */
#define AR_CFG_LED_MODE_SEL_S          7          /* LED mode: bits 7..9 */
#define AR_CFG_LED_POWER               0x00000280 /* Power LED: bit 9=1, bit 7=<LED State> */
#define AR_CFG_LED_POWER_S             7          /* LED mode: bits 7..9 */
#define AR_CFG_LED_NETWORK             0x00000300 /* Network LED: bit 9=1, bit 8=<LED State> */
#define AR_CFG_LED_NETWORK_S           7          /* LED mode: bits 7..9 */
#define AR_CFG_LED_MODE_PROP           0x0        /* Blink prop to filtered tx/rx */
#define AR_CFG_LED_MODE_RPROP          0x1        /* Blink prop to unfiltered tx/rx */
#define AR_CFG_LED_MODE_SPLIT          0x2        /* Blink power for tx/net for rx */
#define AR_CFG_LED_MODE_RAND           0x3        /* Blink randomly */
#define AR_CFG_LED_MODE_POWER_OFF      0x4        /* Power LED OFF */
#define AR_CFG_LED_MODE_POWER_ON       0x5        /* Power LED ON   */
#define AR_CFG_LED_MODE_NETWORK_OFF    0x4        /* Network LED OFF */
#define AR_CFG_LED_MODE_NETWORK_ON     0x6        /* Network LED ON  */
#define AR_CFG_LED_ASSOC_CTL           0x00000c00 /* LED control: bits 10..11 */
#define AR_CFG_LED_ASSOC_CTL_S         10         /* LED control: bits 10..11 */
#define AR_CFG_LED_ASSOC_NONE          0x0        /* 0x00000000: STA is not associated or trying */
#define AR_CFG_LED_ASSOC_ACTIVE        0x1        /* 0x00000400: STA is associated */
#define AR_CFG_LED_ASSOC_PENDING       0x2        /* 0x00000800: STA is trying to associate */

#define AR_CFG_LED_BLINK_SLOW          0x00000008 /* LED slowest blink rate mode: bit 3 */
#define AR_CFG_LED_BLINK_SLOW_S        3          /* LED slowest blink rate mode: bit 3 */

#define AR_CFG_LED_BLINK_THRESH_SEL    0x00000070 /* LED blink threshold select: bits 4..6 */
#define AR_CFG_LED_BLINK_THRESH_SEL_S  4          /* LED blink threshold select: bits 4..6 */

#define AR_MAC_SLEEP                0x1f00
#define AR_MAC_SLEEP_MAC_AWAKE      0x00000000 // mac is now awake
#define AR_MAC_SLEEP_MAC_ASLEEP     0x00000001 // mac is now asleep



/******************************************************************************
 * Host Interface Register Map
******************************************************************************/
// DMA & PCI Registers in PCI space (usable during sleep)

#define AR_HOSTIF_REG(_ah, _reg) (AH9300(_ah)->ah_hostifregs._reg)
#define AR9300_HOSTIF_OFFSET(_x)   offsetof(struct host_intf_reg, _x)
#define AR9340_HOSTIF_OFFSET(_x)   offsetof(struct host_intf_reg_ar9340, _x)

/* Interface Reset Control Register */
#define AR_RC_AHB            0x00000001 // ahb reset
#define AR_RC_APB            0x00000002 // apb reset
#define AR_RC_HOSTIF         0x00000100 // host interface reset

/* PCI express work-arounds */
#define AR_WA_D3_TO_L1_DISABLE          (1 << 14)
#define AR_WA_UNTIE_RESET_EN            (1 << 15) /* Enable PCI Reset to POR (power-on-reset) */
#define AR_WA_D3_TO_L1_DISABLE_REAL     (1 << 16)
#define AR_WA_ASPM_TIMER_BASED_DISABLE  (1 << 17)
#define AR_WA_RESET_EN                  (1 << 18) /* Sw Control to enable PCI-Reset to POR (bit 15) */
#define AR_WA_ANALOG_SHIFT              (1 << 20)
#define AR_WA_POR_SHORT                 (1 << 21) /* PCI-E Phy reset control */
#define AR_WA_COLD_RESET_OVERRIDE       (1 << 13) /* PCI-E Cold reset override */

/* power management state */
#define AR_PM_STATE_PME_D3COLD_VAUX 0x00100000 //for wow

/* CXPL Debug signals which help debug Link Negotiation */
/* CXPL Debug signals which help debug Link Negotiation */

/* XXX check bit feilds */
/* Power Management Control Register */
#define AR_PCIE_PM_CTRL_ENA         0x00080000
#define AR_PMCTRL_WOW_PME_CLR       0x00200000  /* Clear WoW event */
#define AR_PMCTRL_HOST_PME_EN       0x00400000  /* Send OOB WAKE_L on WoW event */
#define AR_PMCTRL_D3COLD_VAUX       0x00800000
#define AR_PMCTRL_PWR_STATE_MASK    0x0F000000  /* Power State Mask */
#define AR_PMCTRL_PWR_STATE_D1D3    0x0F000000  /* Activate D1 and D3 */
#define AR_PMCTRL_PWR_STATE_D0      0x08000000  /* Activate D0 */
#define AR_PMCTRL_PWR_PM_CTRL_ENA   0x00008000  /* Enable power management */
#define AR_PMCTRL_AUX_PWR_DET       0x10000000  /* Puts Chip in L2 state */



/* APB and Local Bus Timeout Counters */
#define AR_HOST_TIMEOUT_APB_CNTR    0x0000FFFF
#define AR_HOST_TIMEOUT_APB_CNTR_S  0
#define AR_HOST_TIMEOUT_LCL_CNTR    0xFFFF0000
#define AR_HOST_TIMEOUT_LCL_CNTR_S  16

/* EEPROM Control Register */
#define AR_EEPROM_ABSENT         0x00000100
#define AR_EEPROM_CORRUPT        0x00000200
#define AR_EEPROM_PROT_MASK      0x03FFFC00
#define AR_EEPROM_PROT_MASK_S    10

// Protect Bits RP is read protect WP is write protect
#define EEPROM_PROTECT_RP_0_31        0x0001
#define EEPROM_PROTECT_WP_0_31        0x0002
#define EEPROM_PROTECT_RP_32_63       0x0004
#define EEPROM_PROTECT_WP_32_63       0x0008
#define EEPROM_PROTECT_RP_64_127      0x0010
#define EEPROM_PROTECT_WP_64_127      0x0020
#define EEPROM_PROTECT_RP_128_191     0x0040
#define EEPROM_PROTECT_WP_128_191     0x0080
#define EEPROM_PROTECT_RP_192_255     0x0100
#define EEPROM_PROTECT_WP_192_255     0x0200
#define EEPROM_PROTECT_RP_256_511     0x0400
#define EEPROM_PROTECT_WP_256_511     0x0800
#define EEPROM_PROTECT_RP_512_1023    0x1000
#define EEPROM_PROTECT_WP_512_1023    0x2000
#define EEPROM_PROTECT_RP_1024_2047   0x4000
#define EEPROM_PROTECT_WP_1024_2047   0x8000

/* RF silent */
#define AR_RFSILENT_FORCE             0x01

/* MAC silicon Rev ID */
#define AR_SREV_ID                    0x000000FF /* Mask to read SREV info */
#define AR_SREV_VERSION               0x000000F0 /* Mask for Chip version */
#define AR_SREV_VERSION_S             4          /* Mask to shift Major Rev Info */
#define AR_SREV_REVISION              0x00000007 /* Mask for Chip revision level */

/* Sowl extension to SREV. AR_SREV_ID must be 0xFF */
#define AR_SREV_ID2                           0xFFFFFFFF /* Mask to read SREV info */
#define AR_SREV_VERSION2                      0xFFFC0000 /* Mask for Chip version */
#define AR_SREV_VERSION2_S                    18         /* Mask to shift Major Rev Info */
#define AR_SREV_TYPE2                         0x0003F000 /* Mask for Chip type */
#define AR_SREV_TYPE2_S                       12         /* Mask to shift Major Rev Info */
#define AR_SREV_TYPE2_CHAIN                   0x00001000 /* chain mode (1 = 3 chains, 0 = 2 chains) */
#define AR_SREV_TYPE2_HOST_MODE               0x00002000 /* host mode (1 = PCI, 0 = PCIe) */
/* Jupiter has a different TYPE2 definition. */
#define AR_SREV_TYPE2_JUPITER_CHAIN           0x00001000 /* chain (1 = 2 chains, 0 = 1 chain) */
#define AR_SREV_TYPE2_JUPITER_BAND            0x00002000 /* band (1 =  dual band, 0 = single band) */
#define AR_SREV_TYPE2_JUPITER_BT              0x00004000 /* BT (1 = shared BT, 0 = no BT) */
#define AR_SREV_TYPE2_JUPITER_MODE            0x00008000 /* mode (1 = premium, 0 = standard) */
#define AR_SREV_REVISION2                     0x00000F00
#define AR_SREV_REVISION2_S               8

#define AR_RADIO_SREV_MAJOR                   0xf0
#define AR_RAD5133_SREV_MAJOR                 0xc0 /* Fowl: 2+5G/3x3 */
#define AR_RAD2133_SREV_MAJOR                 0xd0 /* Fowl: 2G/3x3   */
#define AR_RAD5122_SREV_MAJOR                 0xe0 /* Fowl: 5G/2x2   */
#define AR_RAD2122_SREV_MAJOR                 0xf0 /* Fowl: 2+5G/2x2 */

#if 0
#define AR_AHB_MODE                           0x4024 // ahb mode for dma
#define AR_AHB_EXACT_WR_EN                    0x00000000 // write exact bytes
#define AR_AHB_BUF_WR_EN                      0x00000001 // buffer write upto cacheline
#define AR_AHB_EXACT_RD_EN                    0x00000000 // read exact bytes
#define AR_AHB_CACHELINE_RD_EN                0x00000002 // read upto end of cacheline
#define AR_AHB_PREFETCH_RD_EN                 0x00000004 // prefetch upto page boundary
#define AR_AHB_PAGE_SIZE_1K                   0x00000000 // set page-size as 1k
#define AR_AHB_PAGE_SIZE_2K                   0x00000008 // set page-size as 2k
#define AR_AHB_PAGE_SIZE_4K                   0x00000010 // set page-size as 4k
#endif

#define AR_INTR_RTC_IRQ                       0x00000001 // rtc in shutdown state
#define AR_INTR_MAC_IRQ                       0x00000002 // pending mac interrupt
#if 0
/* 
 * the following definitions might be differents for WASP so 
 * disable them to avoid improper use 
 */
#define AR_INTR_EEP_PROT_ACCESS               0x00000004 // eeprom protected area access
#define AR_INTR_MAC_AWAKE                     0x00020000 // mac is awake
#define AR_INTR_MAC_ASLEEP                    0x00040000 // mac is asleep
#endif
#define AR_INTR_SPURIOUS                      0xFFFFFFFF

/* TODO: fill in other values */

/* Synchronous Interrupt Cause Register */

/* Synchronous Interrupt Enable Register */
#define AR_INTR_SYNC_ENABLE_GPIO      0xFFFC0000 // enable interrupts: bits 18..31
#define AR_INTR_SYNC_ENABLE_GPIO_S    18         // enable interrupts: bits 18..31

/*
 * synchronous interrupt signals
 */
enum {
    AR9300_INTR_SYNC_RTC_IRQ                = 0x00000001,
    AR9300_INTR_SYNC_MAC_IRQ                = 0x00000002,
    AR9300_INTR_SYNC_EEPROM_ILLEGAL_ACCESS  = 0x00000004,
    AR9300_INTR_SYNC_APB_TIMEOUT            = 0x00000008,
    AR9300_INTR_SYNC_PCI_MODE_CONFLICT      = 0x00000010,
    AR9300_INTR_SYNC_HOST1_FATAL            = 0x00000020,
    AR9300_INTR_SYNC_HOST1_PERR             = 0x00000040,
    AR9300_INTR_SYNC_TRCV_FIFO_PERR         = 0x00000080,
    AR9300_INTR_SYNC_RADM_CPL_EP            = 0x00000100,
    AR9300_INTR_SYNC_RADM_CPL_DLLP_ABORT    = 0x00000200,
    AR9300_INTR_SYNC_RADM_CPL_TLP_ABORT     = 0x00000400,
    AR9300_INTR_SYNC_RADM_CPL_ECRC_ERR      = 0x00000800,
    AR9300_INTR_SYNC_RADM_CPL_TIMEOUT       = 0x00001000,
    AR9300_INTR_SYNC_LOCAL_TIMEOUT          = 0x00002000,
    AR9300_INTR_SYNC_PM_ACCESS              = 0x00004000,
    AR9300_INTR_SYNC_MAC_AWAKE              = 0x00008000,
    AR9300_INTR_SYNC_MAC_ASLEEP             = 0x00010000,
    AR9300_INTR_SYNC_MAC_SLEEP_ACCESS       = 0x00020000,
    AR9300_INTR_SYNC_ALL                    = 0x0003FFFF,

    /*
     * Do not enable and turn on mask for both sync and async interrupt, since
     * chip can generate interrupt storm.
     */
    AR9300_INTR_SYNC_DEF_NO_HOST1_PERR      = (AR9300_INTR_SYNC_HOST1_FATAL         |
                                               AR9300_INTR_SYNC_RADM_CPL_EP         |
                                               AR9300_INTR_SYNC_RADM_CPL_DLLP_ABORT |
                                               AR9300_INTR_SYNC_RADM_CPL_TLP_ABORT  |
                                               AR9300_INTR_SYNC_RADM_CPL_ECRC_ERR   |
                                               AR9300_INTR_SYNC_RADM_CPL_TIMEOUT    |
                                               AR9300_INTR_SYNC_LOCAL_TIMEOUT       |
                                               AR9300_INTR_SYNC_MAC_SLEEP_ACCESS),
    AR9300_INTR_SYNC_DEFAULT                = (AR9300_INTR_SYNC_DEF_NO_HOST1_PERR   |
                                               AR9300_INTR_SYNC_HOST1_PERR),

    AR9300_INTR_SYNC_SPURIOUS               = 0xFFFFFFFF,
    
    /* WASP */
    AR9340_INTR_SYNC_RTC_IRQ                = 0x00000001,
    AR9340_INTR_SYNC_MAC_IRQ                = 0x00000002,
    AR9340_INTR_SYNC_HOST1_FATAL            = 0x00000004,
    AR9340_INTR_SYNC_HOST1_PERR             = 0x00000008,
    AR9340_INTR_SYNC_LOCAL_TIMEOUT          = 0x00000010,
    AR9340_INTR_SYNC_MAC_ASLEEP             = 0x00000020,
    AR9340_INTR_SYNC_MAC_SLEEP_ACCESS       = 0x00000040,

    AR9340_INTR_SYNC_DEFAULT                = (AR9340_INTR_SYNC_HOST1_FATAL         |
                                               AR9340_INTR_SYNC_HOST1_PERR          |
                                               AR9340_INTR_SYNC_LOCAL_TIMEOUT       |
                                               AR9340_INTR_SYNC_MAC_SLEEP_ACCESS),

    AR9340_INTR_SYNC_SPURIOUS               = 0xFFFFFFFF,
};

/* Asynchronous Interrupt Mask Register */
#define AR_INTR_ASYNC_MASK_GPIO          0xFFFC0000 // asynchronous interrupt mask: bits 18..31
#define AR_INTR_ASYNC_MASK_GPIO_S        18         // asynchronous interrupt mask: bits 18..31
#define AR_INTR_ASYNC_MASK_MCI           0x00000080
#define AR_INTR_ASYNC_MASK_MCI_S         7

/* Synchronous Interrupt Mask Register */
#define AR_INTR_SYNC_MASK_GPIO           0xFFFC0000 // synchronous interrupt mask: bits 18..31
#define AR_INTR_SYNC_MASK_GPIO_S         18         // synchronous interrupt mask: bits 18..31

/* Asynchronous Interrupt Cause Register */
#define AR_INTR_ASYNC_CAUSE_GPIO         0xFFFC0000 // GPIO interrupts: bits 18..31
#define AR_INTR_ASYNC_CAUSE_MCI          0x00000080
#define AR_INTR_ASYNC_USED               (AR_INTR_MAC_IRQ | AR_INTR_ASYNC_CAUSE_GPIO | AR_INTR_ASYNC_CAUSE_MCI)

/* Asynchronous Interrupt Enable Register */
#define AR_INTR_ASYNC_ENABLE_GPIO        0xFFFC0000 // enable interrupts: bits 18..31
#define AR_INTR_ASYNC_ENABLE_GPIO_S      18         // enable interrupts: bits 18..31
#define AR_INTR_ASYNC_ENABLE_MCI         0x00000080
#define AR_INTR_ASYNC_ENABLE_MCI_S       7

/* PCIE PHY Data Register */

/* PCIE PHY Load Register */
#define AR_PCIE_PM_CTRL_ENA              0x00080000

#define AR93XX_NUM_GPIO                  16 // 0 to 15

/* GPIO Output Register */
#define AR_GPIO_OUT_VAL                  0x000FFFF
#define AR_GPIO_OUT_VAL_S                0

/* GPIO Input Register */
#define AR_GPIO_IN_VAL                   0x000FFFF
#define AR_GPIO_IN_VAL_S                 0

/* Host GPIO output enable bits */
#define AR_GPIO_OE_OUT_DRV               0x3    // 2 bit field mask, shifted by 2*bitpos
#define AR_GPIO_OE_OUT_DRV_NO            0x0    // tristate
#define AR_GPIO_OE_OUT_DRV_LOW           0x1    // drive if low
#define AR_GPIO_OE_OUT_DRV_HI            0x2    // drive if high
#define AR_GPIO_OE_OUT_DRV_ALL           0x3    // drive always

/* Host GPIO output enable bits */

/* Host GPIO Interrupt Polarity */
#define AR_GPIO_INTR_POL_VAL             0x0001FFFF    // bits 16:0 correspond to gpio 16:0
#define AR_GPIO_INTR_POL_VAL_S           0             // bits 16:0 correspond to gpio 16:0

/* Host GPIO Input Value */
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF     0x00000004 // default value for bt_priority_async
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_S       2
#define AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF    0x00000008 // default value for bt_frequency_async
#define AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_S      3
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_DEF       0x00000010 // default value for bt_active_async
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_S         4
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_DEF        0x00000080 // default value for rfsilent_bb_l
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_DEF_S      7
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB      0x00000400 // 0 == set bt_priority_async to default, 1 == connect bt_prority_async to baseband
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB_S    10
#define AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_BB     0x00000800 // 0 == set bt_frequency_async to default, 1 == connect bt_frequency_async to baseband
#define AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_BB_S   11
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB        0x00001000 // 0 == set bt_active_async to default, 1 == connect bt_active_async to baseband
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB_S      12
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_BB         0x00008000 // 0 == set rfsilent_bb_l to default, 1 == connect rfsilent_bb_l to baseband
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_BB_S       15
#define AR_GPIO_RTC_RESET_OVERRIDE_ENABLE        0x00010000
#define AR_GPIO_JTAG_DISABLE                     0x00020000 // 1 == disable JTAG

/* GPIO Input Mux1 */
#define AR_GPIO_INPUT_MUX1_BT_PRIORITY           0x00000f00 /* bits 8..11: input mux for BT priority input */
#define AR_GPIO_INPUT_MUX1_BT_PRIORITY_S         8          /* bits 8..11: input mux for BT priority input */
#define AR_GPIO_INPUT_MUX1_BT_FREQUENCY          0x0000f000 /* bits 12..15: input mux for BT frequency input */
#define AR_GPIO_INPUT_MUX1_BT_FREQUENCY_S        12         /* bits 12..15: input mux for BT frequency input */
#define AR_GPIO_INPUT_MUX1_BT_ACTIVE             0x000f0000 /* bits 16..19: input mux for BT active input */
#define AR_GPIO_INPUT_MUX1_BT_ACTIVE_S           16         /* bits 16..19: input mux for BT active input */

/* GPIO Input Mux2 */
#define AR_GPIO_INPUT_MUX2_CLK25                 0x0000000f // bits 0..3: input mux for clk25 input
#define AR_GPIO_INPUT_MUX2_CLK25_S               0          // bits 0..3: input mux for clk25 input
#define AR_GPIO_INPUT_MUX2_RFSILENT              0x000000f0 // bits 4..7: input mux for rfsilent_bb_l input
#define AR_GPIO_INPUT_MUX2_RFSILENT_S            4          // bits 4..7: input mux for rfsilent_bb_l input
#define AR_GPIO_INPUT_MUX2_RTC_RESET             0x00000f00 // bits 8..11: input mux for RTC Reset input
#define AR_GPIO_INPUT_MUX2_RTC_RESET_S           8          // bits 8..11: input mux for RTC Reset input

/* GPIO Output Mux1 */
/* GPIO Output Mux2 */
/* GPIO Output Mux3 */

#define AR_GPIO_OUTPUT_MUX_AS_OUTPUT             0
#define AR_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED 1
#define AR_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED     2
#define AR_GPIO_OUTPUT_MUX_AS_TX_FRAME           3
#define AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL  4
#define AR_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED    5
#define AR_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED      6
#define AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA      0x16
#define AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK       0x17
#define AR_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA        0x18
#define AR_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK         0x19
#define AR_GPIO_OUTPUT_MUX_AS_WL_IN_TX           0x14
#define AR_GPIO_OUTPUT_MUX_AS_WL_IN_RX           0x13
#define AR_GPIO_OUTPUT_MUX_AS_BT_IN_TX           9
#define AR_GPIO_OUTPUT_MUX_AS_BT_IN_RX           8
#define AR_GPIO_OUTPUT_MUX_AS_RUCKUS_STROBE      0x1d
#define AR_GPIO_OUTPUT_MUX_AS_RUCKUS_DATA        0x1e

#define AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL0     0x1d
#define AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL1     0x1e
#define AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL2     0x1b
/* The above three seems to be functional values for peacock chip. For some
 * reason these are continued for different boards as simple place holders.
 * Now continuing to use these and adding the extra definitions for Scropion
 */
#define AR_GPIO_OUTPUT_MUX_AS_SWCOM3             0x26 

#define AR_ENABLE_SMARTANTENNA 0x00000001

/* Host GPIO Input State */

/* Host Spare */

/* Host PCIE Core Reset Enable */

/* Host CLKRUN */


/* Host EEPROM Status */
#define AR_EEPROM_STATUS_DATA_VAL                0x0000ffff
#define AR_EEPROM_STATUS_DATA_VAL_S              0
#define AR_EEPROM_STATUS_DATA_BUSY               0x00010000
#define AR_EEPROM_STATUS_DATA_BUSY_ACCESS        0x00020000
#define AR_EEPROM_STATUS_DATA_PROT_ACCESS        0x00040000
#define AR_EEPROM_STATUS_DATA_ABSENT_ACCESS      0x00080000

/* Host Observation Control */

/* Host RF Silent */

/* Host GPIO PDPU */
#define AR_GPIO_PDPU_OPTION                      0x03
#define AR_GPIO_PULL_DOWN                        0x02

/* Host GPIO Drive Strength */

/* Host Miscellaneous */

/* Host PCIE MSI Control Register */
#define AR_PCIE_MSI_ENABLE                       0x00000001
#define AR_PCIE_MSI_HW_DBI_WR_EN                 0x02000000
#define AR_PCIE_MSI_HW_INT_PENDING_ADDR          0xFFA0C1FF // bits 8..11: value must be 0x5060 
#define AR_PCIE_MSI_HW_INT_PENDING_ADDR_MSI_64   0xFFA0C9FF // bits 8..11: value must be 0x5064 


#define AR_INTR_PRIO_TX                          0x00000001
#define AR_INTR_PRIO_RXLP                        0x00000002
#define AR_INTR_PRIO_RXHP                        0x00000004

/* OTP Interface Register */
#define AR_ENT_OTP                AR9300_HOSTIF_OFFSET(HOST_INTF_OTP)

#define AR_ENT_OTP_DUAL_BAND_DISABLE            0x00010000
#define AR_ENT_OTP_CHAIN2_DISABLE               0x00020000
#define AR_ENT_OTP_5MHZ_DISABLE                 0x00040000
#define AR_ENT_OTP_10MHZ_DISABLE                0x00080000
#define AR_ENT_OTP_49GHZ_DISABLE                0x00100000
#define AR_ENT_OTP_LOOPBACK_DISABLE             0x00200000
#define AR_ENT_OTP_TPC_PERF_DISABLE             0x00400000
#define AR_ENT_OTP_MIN_PKT_SIZE_DISABLE         0x00800000
#define AR_ENT_OTP_SPECTRAL_PRECISION           0x03000000

/* OTP EFUSE registers */
#define AR_OTP_EFUSE_OFFSET(_x)   offsetof(struct efuse_reg_WLAN, _x)
#define AR_OTP_EFUSE_INTF0        AR_OTP_EFUSE_OFFSET(OTP_INTF0)    
#define AR_OTP_EFUSE_INTF5        AR_OTP_EFUSE_OFFSET(OTP_INTF5)   
#define AR_OTP_EFUSE_PGENB_SETUP_HOLD_TIME  AR_OTP_EFUSE_OFFSET(OTP_PGENB_SETUP_HOLD_TIME)
#define AR_OTP_EFUSE_MEM          AR_OTP_EFUSE_OFFSET(OTP_MEM)

/******************************************************************************
 * RTC Register Map
******************************************************************************/

#define AR_RTC_OFFSET(_x)   offsetof(struct rtc_reg, _x)

/* Reset Control */
#define AR_RTC_RC               AR_RTC_OFFSET(RESET_CONTROL)
#define AR_RTC_RC_M             0x00000003
#define AR_RTC_RC_MAC_WARM      0x00000001
#define AR_RTC_RC_MAC_COLD      0x00000002

/* Crystal Control */
#define AR_RTC_XTAL_CONTROL     AR_RTC_OFFSET(XTAL_CONTROL)

/* Reg Control 0 */
#define AR_RTC_REG_CONTROL0     AR_RTC_OFFSET(REG_CONTROL0)

/* Reg Control 1 */
#define AR_RTC_REG_CONTROL1     AR_RTC_OFFSET(REG_CONTROL1)
#define AR_RTC_REG_CONTROL1_SWREG_PROGRAM   0x00000001

/* TCXO Detect */
#define AR_RTC_TCXO_DETECT      AR_RTC_OFFSET(TCXO_DETECT)

/* Crystal Test */
#define AR_RTC_XTAL_TEST        AR_RTC_OFFSET(XTAL_TEST)

/* Sets the ADC/DAC clock quadrature */
#define AR_RTC_QUADRATURE       AR_RTC_OFFSET(QUADRATURE)

/* PLL Control */
#define AR_RTC_PLL_CONTROL      AR_RTC_OFFSET(PLL_CONTROL)
#define AR_RTC_PLL_DIV          0x000003ff
#define AR_RTC_PLL_DIV_S        0
#define AR_RTC_PLL_REFDIV       0x00003C00
#define AR_RTC_PLL_REFDIV_S     10
#define AR_RTC_PLL_CLKSEL       0x0000C000
#define AR_RTC_PLL_CLKSEL_S     14
#define AR_RTC_PLL_BYPASS       0x00010000
#define AR_RTC_PLL_BYPASS_S     16


/* PLL Control 2: for Hornet */
#define AR_RTC_PLL_CONTROL2      AR_RTC_OFFSET(PLL_CONTROL2)

/* PLL Settle */
#define AR_RTC_PLL_SETTLE        AR_RTC_OFFSET(PLL_SETTLE)

/* Crystal Settle */
#define AR_RTC_XTAL_SETTLE       AR_RTC_OFFSET(XTAL_SETTLE)

/* Controls CLK_OUT pin clock speed */
#define AR_RTC_CLOCK_OUT         AR_RTC_OFFSET(CLOCK_OUT)

/* Forces bias block on at all times */
#define AR_RTC_BIAS_OVERRIDE     AR_RTC_OFFSET(BIAS_OVERRIDE)

/* System Sleep status bits */
#define AR_RTC_SYSTEM_SLEEP      AR_RTC_OFFSET(SYSTEM_SLEEP)

/* Controls sleep options for MAC */
#define AR_RTC_MAC_SLEEP_CONTROL AR_RTC_OFFSET(MAC_SLEEP_CONTROL)

/* Keep Awake Timer */
#define AR_RTC_KEEP_AWAKE        AR_RTC_OFFSET(KEEP_AWAKE)

/* Create a 32kHz clock derived from HF */
#define AR_RTC_DERIVED_RTC_CLK   AR_RTC_OFFSET(DERIVED_RTC_CLK)


/******************************************************************************
 * RTC SYNC Register Map
******************************************************************************/

#define AR_RTC_SYNC_OFFSET(_x)   offsetof(struct rtc_sync_reg, _x)

/* reset RTC */
#define AR_RTC_RESET            AR_RTC_SYNC_OFFSET(RTC_SYNC_RESET)
#define AR_RTC_RESET_EN         0x00000001  /* Reset RTC bit */

/* system sleep status */
#define AR_RTC_STATUS               AR_RTC_SYNC_OFFSET(RTC_SYNC_STATUS)
#define AR_RTC_STATUS_M             0x0000003f
#define AR_RTC_STATUS_SHUTDOWN      0x00000001
#define AR_RTC_STATUS_ON            0x00000002
#define AR_RTC_STATUS_SLEEP         0x00000004
#define AR_RTC_STATUS_WAKEUP        0x00000008
#define AR_RTC_STATUS_SLEEP_ACCESS  0x00000010
#define AR_RTC_STATUS_PLL_CHANGING  0x00000020

/* RTC Derived Register */
#define AR_RTC_SLEEP_CLK            AR_RTC_SYNC_OFFSET(RTC_SYNC_DERIVED)
#define AR_RTC_FORCE_DERIVED_CLK    0x00000002
#define AR_RTC_FORCE_SWREG_PRD      0x00000004
#define AR_RTC_PCIE_RST_PWDN_EN     0x00000008

/* RTC Force Wake Register */
#define AR_RTC_FORCE_WAKE           AR_RTC_SYNC_OFFSET(RTC_SYNC_FORCE_WAKE)
#define AR_RTC_FORCE_WAKE_EN        0x00000001  /* enable force wake */
#define AR_RTC_FORCE_WAKE_ON_INT    0x00000002  /* auto-wake on MAC interrupt */

/* RTC interrupt cause/clear */
#define AR_RTC_INTR_CAUSE       AR_RTC_SYNC_OFFSET(RTC_SYNC_INTR_CAUSE)
/* RTC interrupt enable */
#define AR_RTC_INTR_ENABLE      AR_RTC_SYNC_OFFSET(RTC_SYNC_INTR_ENABLE)
/* RTC interrupt mask */
#define AR_RTC_INTR_MASK        AR_RTC_SYNC_OFFSET(RTC_SYNC_INTR_MASK)



/******************************************************************************
 * Analog Interface Register Map
******************************************************************************/

#define AR_AN_OFFSET(_x)   offsetof(struct analog_intf_reg_csr, _x)

/* XXX */
#if 1
// AR9280: rf long shift registers
#define AR_AN_RF2G1_CH0         0x7810
#define AR_AN_RF2G1_CH0_OB      0x03800000
#define AR_AN_RF2G1_CH0_OB_S    23
#define AR_AN_RF2G1_CH0_DB      0x1C000000
#define AR_AN_RF2G1_CH0_DB_S    26

#define AR_AN_RF5G1_CH0         0x7818
#define AR_AN_RF5G1_CH0_OB5     0x00070000
#define AR_AN_RF5G1_CH0_OB5_S   16
#define AR_AN_RF5G1_CH0_DB5     0x00380000
#define AR_AN_RF5G1_CH0_DB5_S   19

#define AR_AN_RF2G1_CH1         0x7834
#define AR_AN_RF2G1_CH1_OB      0x03800000
#define AR_AN_RF2G1_CH1_OB_S    23
#define AR_AN_RF2G1_CH1_DB      0x1C000000
#define AR_AN_RF2G1_CH1_DB_S    26

#define AR_AN_RF5G1_CH1         0x783C
#define AR_AN_RF5G1_CH1_OB5     0x00070000
#define AR_AN_RF5G1_CH1_OB5_S   16
#define AR_AN_RF5G1_CH1_DB5     0x00380000
#define AR_AN_RF5G1_CH1_DB5_S   19

#define AR_AN_TOP2                  0x7894
#define AR_AN_TOP2_XPABIAS_LVL      0xC0000000
#define AR_AN_TOP2_XPABIAS_LVL_S    30
#define AR_AN_TOP2_LOCALBIAS        0x00200000
#define AR_AN_TOP2_LOCALBIAS_S      21
#define AR_AN_TOP2_PWDCLKIND        0x00400000
#define AR_AN_TOP2_PWDCLKIND_S      22

#define AR_AN_SYNTH9            0x7868
#define AR_AN_SYNTH9_REFDIVA    0xf8000000
#define AR_AN_SYNTH9_REFDIVA_S  27

// AR9285 Analog registers 
#define AR9285_AN_RF2G1          0x7820
#define AR9285_AN_RF2G2          0x7824

#define AR9285_AN_RF2G3         0x7828
#define AR9285_AN_RF2G3_OB_0    0x00E00000
#define AR9285_AN_RF2G3_OB_0_S    21
#define AR9285_AN_RF2G3_OB_1    0x001C0000
#define AR9285_AN_RF2G3_OB_1_S    18
#define AR9285_AN_RF2G3_OB_2    0x00038000
#define AR9285_AN_RF2G3_OB_2_S    15
#define AR9285_AN_RF2G3_OB_3    0x00007000
#define AR9285_AN_RF2G3_OB_3_S    12
#define AR9285_AN_RF2G3_OB_4    0x00000E00
#define AR9285_AN_RF2G3_OB_4_S    9

#define AR9285_AN_RF2G3_DB1_0    0x000001C0
#define AR9285_AN_RF2G3_DB1_0_S    6
#define AR9285_AN_RF2G3_DB1_1    0x00000038
#define AR9285_AN_RF2G3_DB1_1_S    3
#define AR9285_AN_RF2G3_DB1_2    0x00000007
#define AR9285_AN_RF2G3_DB1_2_S    0
#define AR9285_AN_RF2G4         0x782C
#define AR9285_AN_RF2G4_DB1_3    0xE0000000
#define AR9285_AN_RF2G4_DB1_3_S    29
#define AR9285_AN_RF2G4_DB1_4    0x1C000000
#define AR9285_AN_RF2G4_DB1_4_S    26

#define AR9285_AN_RF2G4_DB2_0    0x03800000
#define AR9285_AN_RF2G4_DB2_0_S    23
#define AR9285_AN_RF2G4_DB2_1    0x00700000
#define AR9285_AN_RF2G4_DB2_1_S    20
#define AR9285_AN_RF2G4_DB2_2    0x000E0000
#define AR9285_AN_RF2G4_DB2_2_S    17
#define AR9285_AN_RF2G4_DB2_3    0x0001C000
#define AR9285_AN_RF2G4_DB2_3_S    14
#define AR9285_AN_RF2G4_DB2_4    0x00003800
#define AR9285_AN_RF2G4_DB2_4_S    11

#define AR9285_AN_RF2G6          0x7834
#define AR9285_AN_RF2G7          0x7838
#define AR9285_AN_RF2G9          0x7840
#define AR9285_AN_RXTXBB1        0x7854
#define AR9285_AN_TOP2           0x7868

#define AR9285_AN_TOP3                  0x786c
#define AR9285_AN_TOP3_XPABIAS_LVL      0x0000000C
#define AR9285_AN_TOP3_XPABIAS_LVL_S    2

#define AR9285_AN_TOP4           0x7870
#define AR9285_AN_TOP4_DEFAULT   0x10142c00
#endif


/******************************************************************************
 * MAC PCU Register Map
******************************************************************************/

#define AR_MAC_PCU_OFFSET(_x)   offsetof(struct mac_pcu_reg, _x)

/* MAC station ID0 - low 32 bits */
#define AR_STA_ID0                 AR_MAC_PCU_OFFSET(MAC_PCU_STA_ADDR_L32)
/* MAC station ID1 - upper 16 bits */
#define AR_STA_ID1                 AR_MAC_PCU_OFFSET(MAC_PCU_STA_ADDR_U16)
#define AR_STA_ID1_SADH_MASK       0x0000FFFF // Mask for 16 msb of MAC addr
#define AR_STA_ID1_STA_AP          0x00010000 // Device is AP
#define AR_STA_ID1_ADHOC           0x00020000 // Device is ad-hoc
#define AR_STA_ID1_PWR_SAV         0x00040000 // Power save in generated frames
#define AR_STA_ID1_KSRCHDIS        0x00080000 // Key search disable
#define AR_STA_ID1_PCF             0x00100000 // Observe PCF
#define AR_STA_ID1_USE_DEFANT      0x00200000 // Use default antenna
#define AR_STA_ID1_DEFANT_UPDATE   0x00400000 // Update default ant w/TX antenna
#define AR_STA_ID1_RTS_USE_DEF     0x00800000 // Use default antenna to send RTS
#define AR_STA_ID1_ACKCTS_6MB      0x01000000 // Use 6Mb/s rate for ACK & CTS
#define AR_STA_ID1_BASE_RATE_11B   0x02000000 // Use 11b base rate for ACK & CTS
#define AR_STA_ID1_SECTOR_SELF_GEN 0x04000000 // default ant for generated frames
#define AR_STA_ID1_CRPT_MIC_ENABLE 0x08000000 // Enable Michael
#define AR_STA_ID1_KSRCH_MODE      0x10000000 // Look-up unique key when !keyID
#define AR_STA_ID1_PRESERVE_SEQNUM 0x20000000 // Don't replace seq num
#define AR_STA_ID1_CBCIV_ENDIAN    0x40000000 // IV endian-ness in CBC nonce
#define AR_STA_ID1_MCAST_KSRCH     0x80000000 // Adhoc key search enable

/* MAC BSSID low 32 bits */
#define AR_BSS_ID0           AR_MAC_PCU_OFFSET(MAC_PCU_BSSID_L32)
/* MAC BSSID upper 16 bits / AID */
#define AR_BSS_ID1           AR_MAC_PCU_OFFSET(MAC_PCU_BSSID_U16)
#define AR_BSS_ID1_U16       0x0000FFFF // Mask for upper 16 bits of BSSID
#define AR_BSS_ID1_AID       0x07FF0000 // Mask for association ID
#define AR_BSS_ID1_AID_S     16         // Shift for association ID

/*
 * Added to support dual BSSID/TSF which are needed in the application
 * of Mesh networking. See bug 35189. Note that the only function added
 * with this BSSID2 is to receive multi/broadcast from BSSID2 as well
 */
/* MAC BSSID low 32 bits */
#define AR_BSS2_ID0          AR_MAC_PCU_OFFSET(MAC_PCU_BSSID2_L32)
/* MAC BSSID upper 16 bits / AID */
#define AR_BSS2_ID1          AR_MAC_PCU_OFFSET(MAC_PCU_BSSID2_U16)

/* MAC Beacon average RSSI
 *
 * This register holds the average RSSI with 1/16 dB resolution.
 * The RSSI is averaged over multiple beacons which matched our BSSID.
 * Note that AVE_VALUE is 12 bits with 4 bits below the normal 8 bits.
 * These lowest 4 bits provide for a resolution of 1/16 dB.
 *
 */
#define AR_BCN_RSSI_AVE      AR_MAC_PCU_OFFSET(MAC_PCU_BCN_RSSI_AVE)
#define AR_BCN_RSSI_AVE_VAL  0x00000FFF // Beacon RSSI value
#define AR_BCN_RSSI_AVE_VAL_S 0

/* MAC ACK & CTS time-out */
#define AR_TIME_OUT          AR_MAC_PCU_OFFSET(MAC_PCU_ACK_CTS_TIMEOUT)
#define AR_TIME_OUT_ACK      0x00003FFF // Mask for ACK time-out
#define AR_TIME_OUT_ACK_S    0
#define AR_TIME_OUT_CTS      0x3FFF0000 // Mask for CTS time-out
#define AR_TIME_OUT_CTS_S    16

/* beacon RSSI warning / bmiss threshold */
#define AR_RSSI_THR          AR_MAC_PCU_OFFSET(MAC_PCU_BCN_RSSI_CTL)
#define AR_RSSI_THR_VAL      0x000000FF // Beacon RSSI warning threshold
#define AR_RSSI_THR_VAL_S    0
#define AR_RSSI_THR_BM_THR   0x0000FF00 // Mask for Missed beacon threshold
#define AR_RSSI_THR_BM_THR_S 8          // Shift for Missed beacon threshold
#define AR_RSSI_BCN_WEIGHT   0x1F000000 // RSSI average weight
#define AR_RSSI_BCN_WEIGHT_S 24
#define AR_RSSI_BCN_RSSI_RST 0x20000000 // Reset RSSI value

/* MAC transmit latency register */
#define AR_USEC              AR_MAC_PCU_OFFSET(MAC_PCU_USEC_LATENCY)
#define AR_USEC_USEC         0x000000FF // Mask for clock cycles in 1 usec
#define AR_USEC_USEC_S       0          // Shift for clock cycles in 1 usec
#define AR_USEC_TX_LAT       0x007FC000 // tx latency to start of SIGNAL (usec)
#define AR_USEC_TX_LAT_S     14         // tx latency to start of SIGNAL (usec)
#define AR_USEC_RX_LAT       0x1F800000 // rx latency to start of SIGNAL (usec)
#define AR_USEC_RX_LAT_S     23         // rx latency to start of SIGNAL (usec)

#define AR_SLOT_HALF         13
#define AR_SLOT_QUARTER      21

#define AR_USEC_RX_LATENCY                0x1f800000
#define AR_USEC_RX_LATENCY_S              23
#define AR_RX_LATENCY_FULL                37
#define AR_RX_LATENCY_HALF                74
#define AR_RX_LATENCY_QUARTER             148
#define AR_RX_LATENCY_FULL_FAST_CLOCK     41
#define AR_RX_LATENCY_HALF_FAST_CLOCK     82
#define AR_RX_LATENCY_QUARTER_FAST_CLOCK  163

#define AR_USEC_TX_LATENCY                0x007fc000
#define AR_USEC_TX_LATENCY_S              14
#define AR_TX_LATENCY_FULL                54
#define AR_TX_LATENCY_HALF                108
#define AR_TX_LATENCY_QUARTER             216
#define AR_TX_LATENCY_FULL_FAST_CLOCK     54
#define AR_TX_LATENCY_HALF_FAST_CLOCK     119
#define AR_TX_LATENCY_QUARTER_FAST_CLOCK  238

#define AR_USEC_HALF                      19
#define AR_USEC_QUARTER                   9
#define AR_USEC_HALF_FAST_CLOCK           21
#define AR_USEC_QUARTER_FAST_CLOCK        10

#define AR_EIFS_HALF                     175
#define AR_EIFS_QUARTER                  340

#define AR_RESET_TSF        AR_MAC_PCU_OFFSET(MAC_PCU_RESET_TSF)
#define AR_RESET_TSF_ONCE   0x01000000 // reset tsf once ; self-clears bit
#define AR_RESET_TSF2_ONCE  0x02000000 // reset tsf2 once ; self-clears bit

/* MAC CFP Interval (TU/msec) */
#define AR_CFP_PERIOD                   0x8024  /* MAC CFP Interval (TU/msec) */
#define AR_TIMER0                       0x8028  /* MAC Next beacon time (TU/msec) */
#define AR_TIMER1                       0x802c  /* MAC DMA beacon alert time (1/8 TU) */
#define AR_TIMER2                       0x8030  /* MAC Software beacon alert (1/8 TU) */
#define AR_TIMER3                       0x8034  /* MAC ATIM window time */

/* MAC maximum CFP duration */
#define AR_MAX_CFP_DUR      AR_MAC_PCU_OFFSET(MAC_PCU_MAX_CFP_DUR)
#define AR_CFP_VAL          0x0000FFFF // CFP value in uS

/* MAC receive filter register */
#define AR_RX_FILTER        AR_MAC_PCU_OFFSET(MAC_PCU_RX_FILTER)
#define AR_RX_FILTER_ALL    0x00000000 // Disallow all frames
#define AR_RX_UCAST         0x00000001 // Allow unicast frames
#define AR_RX_MCAST         0x00000002 // Allow multicast frames
#define AR_RX_BCAST         0x00000004 // Allow broadcast frames
#define AR_RX_CONTROL       0x00000008 // Allow control frames
#define AR_RX_BEACON        0x00000010 // Allow beacon frames
#define AR_RX_PROM          0x00000020 // Promiscuous mode all packets
#define AR_RX_PROBE_REQ     0x00000080 // Any probe request frameA
#define AR_RX_MY_BEACON     0x00000200 // Any beacon frame with matching BSSID
#define AR_RX_COMPR_BAR     0x00000400 // Compressed directed block ack request
#define AR_RX_COMPR_BA      0x00000800 // Compressed directed block ack
#define AR_RX_UNCOM_BA_BAR  0x00001000 // Uncompressed directed BA or BAR
#define AR_RX_HWBCNPROC_EN  0x00020000 // Enable hw beacon processing (see AR_HWBCNPROC1)
#define AR_RX_CONTROL_WRAPPER 0x00080000 // Control wrapper. Jupiter only.
#define AR_RX_4ADDRESS      0x00100000 // 4-Address frames

#define AR_PHY_ERR_MASK_REG    AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERROR_MASK_CONT)


/* MAC multicast filter lower 32 bits */
#define AR_MCAST_FIL0       AR_MAC_PCU_OFFSET(MAC_PCU_MCAST_FILTER_L32)
/* MAC multicast filter upper 32 bits */
#define AR_MCAST_FIL1       AR_MAC_PCU_OFFSET(MAC_PCU_MCAST_FILTER_U32)

/* MAC PCU diagnostic switches */
#define AR_DIAG_SW                  AR_MAC_PCU_OFFSET(MAC_PCU_DIAG_SW)
#define AR_DIAG_CACHE_ACK           0x00000001 // disable ACK when no valid key
#define AR_DIAG_ACK_DIS             0x00000002 // disable ACK generation
#define AR_DIAG_CTS_DIS             0x00000004 // disable CTS generation
#define AR_DIAG_ENCRYPT_DIS         0x00000008 // disable encryption
#define AR_DIAG_DECRYPT_DIS         0x00000010 // disable decryption
#define AR_DIAG_RX_DIS              0x00000020 // disable receive
#define AR_DIAG_LOOP_BACK           0x00000040 // enable loopback
#define AR_DIAG_CORR_FCS            0x00000080 // corrupt FCS
#define AR_DIAG_CHAN_INFO           0x00000100 // dump channel info
#define AR_DIAG_FRAME_NV0           0x00020000 // accept w/protocol version !0
#define AR_DIAG_OBS_PT_SEL1         0x000C0000 // observation point select
#define AR_DIAG_OBS_PT_SEL1_S       18 // Shift for observation point select
#define AR_DIAG_FORCE_RX_CLEAR      0x00100000 // force rx_clear high
#define AR_DIAG_IGNORE_VIRT_CS      0x00200000 // ignore virtual carrier sense
#define AR_DIAG_FORCE_CH_IDLE_HIGH  0x00400000 // force channel idle high
#define AR_DIAG_EIFS_CTRL_ENA       0x00800000 // use framed and ~wait_wep if 0
#define AR_DIAG_DUAL_CHAIN_INFO     0x01000000 // dual chain channel info
#define AR_DIAG_RX_ABORT            0x02000000 //  abort rx
#define AR_DIAG_SATURATE_CYCLE_CNT  0x04000000 // saturate cycle cnts (no shift)
#define AR_DIAG_OBS_PT_SEL2         0x08000000 // Mask for observation point sel
#define AR_DIAG_OBS_PT_SEL2_S       27
#define AR_DIAG_RX_CLEAR_CTL_LOW    0x10000000 // force rx_clear (ctl) low (i.e. busy)
#define AR_DIAG_RX_CLEAR_EXT_LOW    0x20000000 // force rx_clear (ext) low (i.e. busy)

/* MAC local clock lower 32 bits */
#define AR_TSF_L32          AR_MAC_PCU_OFFSET(MAC_PCU_TSF_L32)
/* MAC local clock upper 32 bits */
#define AR_TSF_U32          AR_MAC_PCU_OFFSET(MAC_PCU_TSF_U32)

/*
 * Secondary TSF support added for dual BSSID/TSF
 * which is needed in the application of DirectConnect or
 * Mesh networking
 */
/* MAC local clock lower 32 bits */
#define AR_TSF2_L32         AR_MAC_PCU_OFFSET(MAC_PCU_TSF2_L32)
/* MAC local clock upper 32 bits */
#define AR_TSF2_U32         AR_MAC_PCU_OFFSET(MAC_PCU_TSF2_U32)

/* ADDAC test register */
#define AR_TST_ADDAC        AR_MAC_PCU_OFFSET(MAC_PCU_TST_ADDAC)

#define AR_TST_ADDAC_TST_MODE 0x1
#define AR_TST_ADDAC_TST_MODE_S 0
#define AR_TST_ADDAC_TST_LOOP_ENA 0x2
#define AR_TST_ADDAC_TST_LOOP_ENA_S 1
#define AR_TST_ADDAC_BEGIN_CAPTURE 0x80000
#define AR_TST_ADDAC_BEGIN_CAPTURE_S 19

/* default antenna register */
#define AR_DEF_ANTENNA      AR_MAC_PCU_OFFSET(MAC_PCU_DEF_ANTENNA)

/* MAC AES mute mask */
#define AR_AES_MUTE_MASK0       AR_MAC_PCU_OFFSET(MAC_PCU_AES_MUTE_MASK_0)
#define AR_AES_MUTE_MASK0_FC    0x0000FFFF // frame ctrl mask bits
#define AR_AES_MUTE_MASK0_QOS   0xFFFF0000 // qos ctrl mask bits
#define AR_AES_MUTE_MASK0_QOS_S 16

/* MAC AES mute mask 1 */
#define AR_AES_MUTE_MASK1       AR_MAC_PCU_OFFSET(MAC_PCU_AES_MUTE_MASK_1)
#define AR_AES_MUTE_MASK1_SEQ                       0x0000FFFF // seq + frag mask bits
#define AR_AES_MUTE_MASK1_FC_MGMT                   0xFFFF0000   // frame ctrl mask for mgmt frames (Sowl)
#define AR_AES_MUTE_MASK1_FC_MGMT_S                 16

/* control clock domain */
#define AR_GATED_CLKS       AR_MAC_PCU_OFFSET(MAC_PCU_GATED_CLKS)
#define AR_GATED_CLKS_TX    0x00000002
#define AR_GATED_CLKS_RX    0x00000004
#define AR_GATED_CLKS_REG   0x00000008

/* MAC PCU observation bus 2 */
#define AR_OBS_BUS_CTRL     AR_MAC_PCU_OFFSET(MAC_PCU_OBS_BUS_2)
#define AR_OBS_BUS_SEL_1    0x00040000
#define AR_OBS_BUS_SEL_2    0x00080000
#define AR_OBS_BUS_SEL_3    0x000C0000
#define AR_OBS_BUS_SEL_4    0x08040000
#define AR_OBS_BUS_SEL_5    0x08080000

/* MAC PCU observation bus 1 */
#define AR_OBS_BUS_1               AR_MAC_PCU_OFFSET(MAC_PCU_OBS_BUS_1)
#define AR_OBS_BUS_1_PCU           0x00000001
#define AR_OBS_BUS_1_RX_END        0x00000002
#define AR_OBS_BUS_1_RX_WEP        0x00000004
#define AR_OBS_BUS_1_RX_BEACON     0x00000008
#define AR_OBS_BUS_1_RX_FILTER     0x00000010
#define AR_OBS_BUS_1_TX_HCF        0x00000020
#define AR_OBS_BUS_1_QUIET_TIME    0x00000040
#define AR_OBS_BUS_1_CHAN_IDLE     0x00000080
#define AR_OBS_BUS_1_TX_HOLD       0x00000100
#define AR_OBS_BUS_1_TX_FRAME      0x00000200
#define AR_OBS_BUS_1_RX_FRAME      0x00000400
#define AR_OBS_BUS_1_RX_CLEAR      0x00000800
#define AR_OBS_BUS_1_WEP_STATE     0x0003F000
#define AR_OBS_BUS_1_WEP_STATE_S   12
#define AR_OBS_BUS_1_RX_STATE      0x01F00000
#define AR_OBS_BUS_1_RX_STATE_S    20
#define AR_OBS_BUS_1_TX_STATE      0x7E000000
#define AR_OBS_BUS_1_TX_STATE_S    25

/* MAC PCU dynamic MIMO power save */
#define AR_PCU_SMPS                  AR_MAC_PCU_OFFSET(MAC_PCU_DYM_MIMO_PWR_SAVE)
#define AR_PCU_SMPS_MAC_CHAINMASK    0x00000001     // Use the Rx Chainmask of MAC's setting
#define AR_PCU_SMPS_HW_CTRL_EN       0x00000002     // Enable hardware control of dynamic MIMO PS
#define AR_PCU_SMPS_SW_CTRL_HPWR     0x00000004     // Software controlled High power chainmask setting
#define AR_PCU_SMPS_LPWR_CHNMSK      0x00000070     // Low power setting of Rx Chainmask
#define AR_PCU_SMPS_LPWR_CHNMSK_S    4
#define AR_PCU_SMPS_HPWR_CHNMSK      0x00000700     // High power setting of Rx Chainmask
#define AR_PCU_SMPS_HPWR_CHNMSK_S    8
#define AR_PCU_SMPS_LPWR_CHNMSK_VAL  0x1

/* MAC PCU frame start time trigger for the AP's Downlink Traffic in TDMA mode */
#define AR_TDMA_TXSTARTTRIG_LSB     AR_MAC_PCU_OFFSET(MAC_PCU_TDMA_TXFRAME_START_TIME_TRIGGER_LSB)
#define AR_TDMA_TXSTARTTRIG_MSB     AR_MAC_PCU_OFFSET(MAC_PCU_TDMA_TXFRAME_START_TIME_TRIGGER_MSB)

/* MAC Time stamp of the last beacon received */
#define AR_LAST_TSTP        AR_MAC_PCU_OFFSET(MAC_PCU_LAST_BEACON_TSF)
/* MAC current NAV value */
#define AR_NAV              AR_MAC_PCU_OFFSET(MAC_PCU_NAV)
/* MAC RTS exchange success counter */
#define AR_RTS_OK           AR_MAC_PCU_OFFSET(MAC_PCU_RTS_SUCCESS_CNT)
/* MAC RTS exchange failure counter */
#define AR_RTS_FAIL         AR_MAC_PCU_OFFSET(MAC_PCU_RTS_FAIL_CNT)
/* MAC ACK failure counter */
#define AR_ACK_FAIL         AR_MAC_PCU_OFFSET(MAC_PCU_ACK_FAIL_CNT)
/* MAC FCS check failure counter */
#define AR_FCS_FAIL         AR_MAC_PCU_OFFSET(MAC_PCU_FCS_FAIL_CNT)
/* MAC Valid beacon value */
#define AR_BEACON_CNT       AR_MAC_PCU_OFFSET(MAC_PCU_BEACON_CNT)

/* MAC PCU tdma slot alert control */
#define AR_TDMA_SLOT_ALERT_CNTL     AR_MAC_PCU_OFFSET(MAC_PCU_TDMA_SLOT_ALERT_CNTL)

/* MAC PCU Basic MCS set for MCS 0 to 31 */
#define AR_BASIC_SET        AR_MAC_PCU_OFFSET(MAC_PCU_BASIC_SET)
#define ALL_RATE            0xff

/* MAC_PCU_ _SEQ */
#define AR_MGMT_SEQ         AR_MAC_PCU_OFFSET(MAC_PCU_MGMT_SEQ)
#define AR_MGMT_SEQ_MIN     0xFFF       /* sequence minimum value*/
#define AR_MGMT_SEQ_MIN_S   0
#define AR_MIN_HW_SEQ       0
#define AR_MGMT_SEQ_MAX     0xFFF0000   /* sequence maximum value*/
#define AR_MGMT_SEQ_MAX_S   16
#define AR_MAX_HW_SEQ       0xFF
/*MAC PCU Key Cache Antenna 1 */
#define AR_TX_ANT_1     AR_MAC_PCU_OFFSET(MAC_PCU_TX_ANT_1)
/*MAC PCU Key Cache Antenna 2 */
#define AR_TX_ANT_2     AR_MAC_PCU_OFFSET(MAC_PCU_TX_ANT_2)
/*MAC PCU Key Cache Antenna 3 */
#define AR_TX_ANT_3     AR_MAC_PCU_OFFSET(MAC_PCU_TX_ANT_3)
/*MAC PCU Key Cache Antenna 4 */
#define AR_TX_ANT_4     AR_MAC_PCU_OFFSET(MAC_PCU_TX_ANT_4)


/* Extended range mode */
#define AR_XRMODE                   AR_MAC_PCU_OFFSET(MAC_PCU_XRMODE)
/* Extended range mode delay */
#define AR_XRDEL                    AR_MAC_PCU_OFFSET(MAC_PCU_XRDEL)
/* Extended range mode timeout */
#define AR_XRTO                     AR_MAC_PCU_OFFSET(MAC_PCU_XRTO)
/* Extended range mode chirp */
#define AR_XRCRP                    AR_MAC_PCU_OFFSET(MAC_PCU_XRCRP)
/* Extended range stomp */
#define AR_XRSTMP                   AR_MAC_PCU_OFFSET(MAC_PCU_XRSTMP)


/* Enhanced sleep control 1 */
#define AR_SLEEP1               AR_MAC_PCU_OFFSET(MAC_PCU_SLP1)
#define AR_SLEEP1_ASSUME_DTIM   0x00080000 // Assume DTIM on missed beacon
#define AR_SLEEP1_CAB_TIMEOUT   0xFFE00000 // Cab timeout(TU) mask
#define AR_SLEEP1_CAB_TIMEOUT_S 21         // Cab timeout(TU) shift

/* Enhanced sleep control 2 */
#define AR_SLEEP2               AR_MAC_PCU_OFFSET(MAC_PCU_SLP2)
#define AR_SLEEP2_BEACON_TIMEOUT    0xFFE00000 // Beacon timeout(TU) mask
#define AR_SLEEP2_BEACON_TIMEOUT_S  21         // Beacon timeout(TU) shift

/*MAC_PCU_SELF_GEN_DEFAULT*/
#define AR_SELFGEN              AR_MAC_PCU_OFFSET(MAC_PCU_SELF_GEN_DEFAULT)
#define AR_MMSS                 0x00000007
#define AR_MMSS_S               0
#define AR_SELFGEN_MMSS_NO RESTRICTION  0
#define AR_SELFGEN_MMSS_ONEOVER4_us     1
#define AR_SELFGEN_MMSS_ONEOVER2_us     2
#define AR_SELFGEN_MMSS_ONE_us          3
#define AR_SELFGEN_MMSS_TWO_us          4
#define AR_SELFGEN_MMSS_FOUR_us         5
#define AR_SELFGEN_MMSS_EIGHT_us        6
#define AR_SELFGEN_MMSS_SIXTEEN_us      7

#define AR_CEC                  0x00000018
#define AR_CEC_S                3
/* Although in original standard 0 is for 1 stream and 1 is for 2 stream */
/*  due to H/W resaon, Here should set 1 for 1 stream and 2 for 2 stream */
#define AR_SELFGEN_CEC_ONE_SPACETIMESTREAM      1   
#define AR_SELFGEN_CEC_TWO_SPACETIMESTREAM      2   

/* BSSID mask lower 32 bits */
#define AR_BSSMSKL              AR_MAC_PCU_OFFSET(MAC_PCU_ADDR1_MASK_L32)
/* BSSID mask upper 16 bits */
#define AR_BSSMSKU              AR_MAC_PCU_OFFSET(MAC_PCU_ADDR1_MASK_U16)

/* Transmit power control for gen frames */
#define AR_TPC                 AR_MAC_PCU_OFFSET(MAC_PCU_TPC)
#define AR_TPC_ACK             0x0000003f // ack frames mask
#define AR_TPC_ACK_S           0x00       // ack frames shift
#define AR_TPC_CTS             0x00003f00 // cts frames mask
#define AR_TPC_CTS_S           0x08       // cts frames shift
#define AR_TPC_CHIRP           0x003f0000 // chirp frames mask
#define AR_TPC_CHIRP_S         16         // chirp frames shift
#define AR_TPC_RPT             0x3f000000 // rpt frames mask
#define AR_TPC_RPT_S           24         // rpt frames shift

/* Profile count transmit frames */
#define AR_TFCNT           AR_MAC_PCU_OFFSET(MAC_PCU_TX_FRAME_CNT)
/* Profile count receive frames */
#define AR_RFCNT           AR_MAC_PCU_OFFSET(MAC_PCU_RX_FRAME_CNT)
/* Profile count receive clear */
#define AR_RCCNT           AR_MAC_PCU_OFFSET(MAC_PCU_RX_CLEAR_CNT)
/* Profile count cycle counter */
#define AR_CCCNT           AR_MAC_PCU_OFFSET(MAC_PCU_CYCLE_CNT)

/* Quiet time programming for TGh */
#define AR_QUIET1                      AR_MAC_PCU_OFFSET(MAC_PCU_QUIET_TIME_1)
#define AR_QUIET1_NEXT_QUIET_S         0            // TSF of next quiet period (TU)
#define AR_QUIET1_NEXT_QUIET_M         0x0000ffff
#define AR_QUIET1_QUIET_ENABLE         0x00010000   // Enable Quiet time operation
#define AR_QUIET1_QUIET_ACK_CTS_ENABLE 0x00020000   // ack/cts in quiet period
#define AR_QUIET1_QUIET_ACK_CTS_ENABLE_S 17
#define AR_QUIET2                      AR_MAC_PCU_OFFSET(MAC_PCU_QUIET_TIME_2)
#define AR_QUIET2_QUIET_PERIOD_S       0            // Periodicity of quiet period (TU)
#define AR_QUIET2_QUIET_PERIOD_M       0x0000ffff
#define AR_QUIET2_QUIET_DUR_S          16           // quiet period (TU)
#define AR_QUIET2_QUIET_DUR            0xffff0000

/* locate no_ack in qos */
#define AR_QOS_NO_ACK              AR_MAC_PCU_OFFSET(MAC_PCU_QOS_NO_ACK)
#define AR_QOS_NO_ACK_TWO_BIT      0x0000000f // 2 bit sentinel for no-ack
#define AR_QOS_NO_ACK_TWO_BIT_S    0
#define AR_QOS_NO_ACK_BIT_OFF      0x00000070 // offset for no-ack
#define AR_QOS_NO_ACK_BIT_OFF_S    4
#define AR_QOS_NO_ACK_BYTE_OFF     0x00000180 // from end of header
#define AR_QOS_NO_ACK_BYTE_OFF_S   7

/* Phy errors to be filtered */
#define AR_PHY_ERR             AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERROR_MASK)
    /* XXX validate! XXX */
#define AR_PHY_ERR_DCHIRP      0x00000008   // Bit  3 enables double chirp
#define AR_PHY_ERR_RADAR       0x00000020   // Bit  5 is Radar signal
#define AR_PHY_ERR_OFDM_TIMING 0x00020000   // Bit 17 is AH_FALSE detect for OFDM
#define AR_PHY_ERR_CCK_TIMING  0x02000000   // Bit 25 is AH_FALSE detect for CCK

/* MAC PCU extended range latency */
#define AR_XRLAT               AR_MAC_PCU_OFFSET(MAC_PCU_XRLAT)

/* MAC PCU Receive Buffer settings */
#define AR_RXFIFO_CFG          AR_MAC_PCU_OFFSET(MAC_PCU_RXBUF)
#define AR_RXFIFO_CFG_REG_RD_ENA_S 11
#define AR_RXFIFO_CFG_REG_RD_ENA   (0x1 << AR_RXFIFO_CFG_REG_RD_ENA_S)

/* MAC PCU QoS control */
#define AR_MIC_QOS_CONTROL AR_MAC_PCU_OFFSET(MAC_PCU_MIC_QOS_CONTROL)
/* MAC PCU Michael QoS select */
#define AR_MIC_QOS_SELECT  AR_MAC_PCU_OFFSET(MAC_PCU_MIC_QOS_SELECT)

/* PCU Miscellaneous Mode */
#define AR_PCU_MISC                AR_MAC_PCU_OFFSET(MAC_PCU_MISC_MODE)
#define AR_PCU_FORCE_BSSID_MATCH   0x00000001    // force bssid to match
#define AR_PCU_MIC_NEW_LOC_ENA     0x00000004    // tx/rx mic key are together
#define AR_PCU_TX_ADD_TSF          0x00000008    // add tx_tsf + int_tsf
#define AR_PCU_CCK_SIFS_MODE       0x00000010    // assume 11b sifs programmed
#define AR_PCU_RX_ANT_UPDT         0x00000800    // KC_RX_ANT_UPDATE
#define AR_PCU_TXOP_TBTT_LIMIT_ENA 0x00001000    // enforce txop / tbtt
#define AR_PCU_MISS_BCN_IN_SLEEP   0x00004000    // count bmiss's when sleeping
#define AR_PCU_BUG_12306_FIX_ENA   0x00020000    // use rx_clear to count sifs
#define AR_PCU_FORCE_QUIET_COLL    0x00040000    // kill xmit for channel change
#define AR_PCU_BT_ANT_PREVENT_RX   0x00100000
#define AR_PCU_BT_ANT_PREVENT_RX_S 20
#define AR_PCU_TBTT_PROTECT        0x00200000    // no xmit upto tbtt + 20 uS
#define AR_PCU_CLEAR_VMF           0x01000000    // clear vmf mode (fast cc)
#define AR_PCU_CLEAR_BA_VALID      0x04000000    // clear ba state
#define AR_PCU_SEL_EVM             0x08000000    // select EVM data or PLCP header
#define AR_PCU_ALWAYS_PERFORM_KEYSEARCH 0x10000000 /* always perform key search */
/* count of filtered ofdm */
#define AR_FILT_OFDM           AR_MAC_PCU_OFFSET(MAC_PCU_FILTER_OFDM_CNT)
#define AR_FILT_OFDM_COUNT     0x00FFFFFF        // count of filtered ofdm

/* count of filtered cck */
#define AR_FILT_CCK            AR_MAC_PCU_OFFSET(MAC_PCU_FILTER_CCK_CNT)
#define AR_FILT_CCK_COUNT      0x00FFFFFF        // count of filtered cck

/* MAC PCU PHY error counter 1 */
#define AR_PHY_ERR_1           AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERR_CNT_1)
#define AR_PHY_ERR_1_COUNT     0x00FFFFFF        // phy errs that pass mask_1
/* MAC PCU PHY error mask 1 */
#define AR_PHY_ERR_MASK_1      AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERR_CNT_1_MASK)

/* MAC PCU PHY error counter 2 */
#define AR_PHY_ERR_2           AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERR_CNT_2)
#define AR_PHY_ERR_2_COUNT     0x00FFFFFF        // phy errs that pass mask_2
/* MAC PCU PHY error mask 2 */
#define AR_PHY_ERR_MASK_2      AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERR_CNT_2_MASK)

#define AR_PHY_COUNTMAX        (3 << 22)         // Max counted before intr
#define AR_MIBCNT_INTRMASK     (3 << 22)         // Mask top 2 bits of counters

/* interrupt if rx_tsf-int_tsf */
#define AR_TSFOOR_THRESHOLD     AR_MAC_PCU_OFFSET(MAC_PCU_TSF_THRESHOLD)
#define AR_TSFOOR_THRESHOLD_VAL 0x0000FFFF       // field width

/* MAC PCU PHY error counter 3 */
#define AR_PHY_ERR_3           AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERR_CNT_3)
#define AR_PHY_ERR_3_COUNT     0x00FFFFFF        // phy errs that pass mask_3
/* MAC PCU PHY error mask 3 */
#define AR_PHY_ERR_MASK_3      AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERR_CNT_3_MASK)

/* Bluetooth coexistance mode */
#define AR_BT_COEX_MODE            AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_MODE)
#define AR_BT_TIME_EXTEND          0x000000ff
#define AR_BT_TIME_EXTEND_S        0
#define AR_BT_TXSTATE_EXTEND       0x00000100
#define AR_BT_TXSTATE_EXTEND_S     8
#define AR_BT_TX_FRAME_EXTEND      0x00000200
#define AR_BT_TX_FRAME_EXTEND_S    9
#define AR_BT_MODE                 0x00000c00
#define AR_BT_MODE_S               10
#define AR_BT_QUIET                0x00001000
#define AR_BT_QUIET_S              12
#define AR_BT_QCU_THRESH           0x0001e000
#define AR_BT_QCU_THRESH_S         13
#define AR_BT_RX_CLEAR_POLARITY    0x00020000
#define AR_BT_RX_CLEAR_POLARITY_S  17
#define AR_BT_PRIORITY_TIME        0x00fc0000
#define AR_BT_PRIORITY_TIME_S      18
#define AR_BT_FIRST_SLOT_TIME      0xff000000
#define AR_BT_FIRST_SLOT_TIME_S    24

/* BlueTooth coexistance WLAN weights */
#define AR_BT_COEX_WL_WEIGHTS0     AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_WL_WEIGHTS0)
#define AR_BT_BT_WGHT              0x0000ffff
#define AR_BT_BT_WGHT_S            0
#define AR_BT_WL_WGHT              0xffff0000
#define AR_BT_WL_WGHT_S            16

/* HCF timeout: Slotted behavior */
#define AR_HCFTO                   AR_MAC_PCU_OFFSET(MAC_PCU_HCF_TIMEOUT)

/* BlueTooth mode 2: Slotted behavior */
#define AR_BT_COEX_MODE2           AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_MODE2)
#define AR_BT_BCN_MISS_THRESH      0x000000ff
#define AR_BT_BCN_MISS_THRESH_S    0
#define AR_BT_BCN_MISS_CNT         0x0000ff00
#define AR_BT_BCN_MISS_CNT_S       8
#define AR_BT_HOLD_RX_CLEAR        0x00010000
#define AR_BT_HOLD_RX_CLEAR_S      16
#define AR_BT_SLEEP_ALLOW_BT       0x00020000
#define AR_BT_SLEEP_ALLOW_BT_S     17
#define AR_BT_PROTECT_AFTER_WAKE   0x00080000
#define AR_BT_PROTECT_AFTER_WAKE_S 19
#define AR_BT_DISABLE_BT_ANT       0x00100000
#define AR_BT_DISABLE_BT_ANT_S     20
#define AR_BT_QUIET_2_WIRE         0x00200000
#define AR_BT_QUIET_2_WIRE_S       21
#define AR_BT_WL_ACTIVE_MODE       0x00c00000
#define AR_BT_WL_ACTIVE_MODE_S     22
#define AR_BT_WL_TXRX_SEPARATE     0x01000000
#define AR_BT_WL_TXRX_SEPARATE_S   24
#define AR_BT_RS_DISCARD_EXTEND    0x02000000
#define AR_BT_RS_DISCARD_EXTEND_S  25
#define AR_BT_TSF_BT_ACTIVE_CTRL   0x0c000000
#define AR_BT_TSF_BT_ACTIVE_CTRL_S 26
#define AR_BT_TSF_BT_PRIORITY_CTRL 0x30000000
#define AR_BT_TSF_BT_PRIORITY_CTRL_S 28
#define AR_BT_INTERRUPT_ENABLE     0x40000000
#define AR_BT_INTERRUPT_ENABLE_S   30
#define AR_BT_PHY_ERR_BT_COLL_ENABLE 0x80000000
#define AR_BT_PHY_ERR_BT_COLL_ENABLE_S 31

/* Generic Timers 2 */
#define AR_GEN_TIMERS2_0                    AR_MAC_PCU_OFFSET(MAC_PCU_GENERIC_TIMERS2)
#define AR_GEN_TIMERS2_NEXT(_i)            (AR_GEN_TIMERS2_0 + ((_i)<<2))
#define AR_GEN_TIMERS2_PERIOD(_i)          (AR_GEN_TIMERS2_NEXT(8) + ((_i)<<2))

#define AR_GEN_TIMERS2_0_NEXT               AR_GEN_TIMERS2_NEXT(0)
#define AR_GEN_TIMERS2_1_NEXT               AR_GEN_TIMERS2_NEXT(1)
#define AR_GEN_TIMERS2_2_NEXT               AR_GEN_TIMERS2_NEXT(2)
#define AR_GEN_TIMERS2_3_NEXT               AR_GEN_TIMERS2_NEXT(3)
#define AR_GEN_TIMERS2_4_NEXT               AR_GEN_TIMERS2_NEXT(4)
#define AR_GEN_TIMERS2_5_NEXT               AR_GEN_TIMERS2_NEXT(5)
#define AR_GEN_TIMERS2_6_NEXT               AR_GEN_TIMERS2_NEXT(6)
#define AR_GEN_TIMERS2_7_NEXT               AR_GEN_TIMERS2_NEXT(7)
#define AR_GEN_TIMERS2_0_PERIOD             AR_GEN_TIMERS2_PERIOD(0)
#define AR_GEN_TIMERS2_1_PERIOD             AR_GEN_TIMERS2_PERIOD(1)
#define AR_GEN_TIMERS2_2_PERIOD             AR_GEN_TIMERS2_PERIOD(2)
#define AR_GEN_TIMERS2_3_PERIOD             AR_GEN_TIMERS2_PERIOD(3)
#define AR_GEN_TIMERS2_4_PERIOD             AR_GEN_TIMERS2_PERIOD(4)
#define AR_GEN_TIMERS2_5_PERIOD             AR_GEN_TIMERS2_PERIOD(5)
#define AR_GEN_TIMERS2_6_PERIOD             AR_GEN_TIMERS2_PERIOD(6)
#define AR_GEN_TIMERS2_7_PERIOD             AR_GEN_TIMERS2_PERIOD(7)

#define AR_GEN_TIMER_BANK_1_LEN             8
#define AR_FIRST_NDP_TIMER                  7
#define AR_NUM_GEN_TIMERS                   16
#define AR_GEN_TIMER_RESERVED               8

/* Generic Timers 2 Mode */
#define AR_GEN_TIMERS2_MODE        AR_MAC_PCU_OFFSET(MAC_PCU_GENERIC_TIMERS2_MODE)

/* BlueTooth coexistance WLAN weights 1 */
#define AR_BT_COEX_WL_WEIGHTS1     AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_WL_WEIGHTS1)

/* BlueTooth Coexistence TSF Snapshot for BT_ACTIVE */
#define AR_BT_TSF_ACTIVE           AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_TSF_BT_ACTIVE)

/* BlueTooth Coexistence TSF Snapshot for BT_PRIORITY */
#define AR_BT_TSF_PRIORITY         AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_TSF_BT_PRIORITY)

/* SIFS, TX latency and ACK shift */
#define AR_TXSIFS              AR_MAC_PCU_OFFSET(MAC_PCU_TXSIFS)
#define AR_TXSIFS_TIME         0x000000FF        // uS in SIFS
#define AR_TXSIFS_TX_LATENCY   0x00000F00        // uS for transmission thru bb
#define AR_TXSIFS_TX_LATENCY_S 8
#define AR_TXSIFS_ACK_SHIFT    0x00007000        // chan width for ack
#define AR_TXSIFS_ACK_SHIFT_S  12

/* BlueTooth mode 3 */
#define AR_BT_COEX_MODE3           AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_MODE3)


/* TXOP for legacy non-qos */
#define AR_TXOP_X          AR_MAC_PCU_OFFSET(MAC_PCU_TXOP_X)
#define AR_TXOP_X_VAL      0x000000FF

/* TXOP for TID 0 to 3 */
#define AR_TXOP_0_3    AR_MAC_PCU_OFFSET(MAC_PCU_TXOP_0_3)
/* TXOP for TID 4 to 7 */
#define AR_TXOP_4_7    AR_MAC_PCU_OFFSET(MAC_PCU_TXOP_4_7)
/* TXOP for TID 8 to 11 */
#define AR_TXOP_8_11   AR_MAC_PCU_OFFSET(MAC_PCU_TXOP_8_11)
/* TXOP for TID 12 to 15 */
#define AR_TXOP_12_15  AR_MAC_PCU_OFFSET(MAC_PCU_TXOP_12_15)

/* Generic Timers */
#define AR_GEN_TIMERS_0           AR_MAC_PCU_OFFSET(MAC_PCU_GENERIC_TIMERS)
#define AR_GEN_TIMERS(_i)         (AR_GEN_TIMERS_0 + ((_i)<<2))

/* generic timers based on tsf - all uS */
#define AR_NEXT_TBTT_TIMER                  AR_GEN_TIMERS(0)
#define AR_NEXT_DMA_BEACON_ALERT            AR_GEN_TIMERS(1)
#define AR_NEXT_SWBA                        AR_GEN_TIMERS(2)
#define AR_NEXT_HCF                         AR_GEN_TIMERS(3)
#define AR_NEXT_TIM                         AR_GEN_TIMERS(4)
#define AR_NEXT_DTIM                        AR_GEN_TIMERS(5)
#define AR_NEXT_QUIET_TIMER                 AR_GEN_TIMERS(6)
#define AR_NEXT_NDP_TIMER                   AR_GEN_TIMERS(7)
#define AR_BEACON_PERIOD                    AR_GEN_TIMERS(8)
#define AR_DMA_BEACON_PERIOD                AR_GEN_TIMERS(9)
#define AR_SWBA_PERIOD                      AR_GEN_TIMERS(10)
#define AR_HCF_PERIOD                       AR_GEN_TIMERS(11)
#define AR_TIM_PERIOD                       AR_GEN_TIMERS(12)
#define AR_DTIM_PERIOD                      AR_GEN_TIMERS(13)
#define AR_QUIET_PERIOD                     AR_GEN_TIMERS(14)
#define AR_NDP_PERIOD                       AR_GEN_TIMERS(15)

/* Generic Timers Mode */
#define AR_TIMER_MODE                       AR_MAC_PCU_OFFSET(MAC_PCU_GENERIC_TIMERS_MODE)
#define AR_TBTT_TIMER_EN                    0x00000001
#define AR_DBA_TIMER_EN                     0x00000002
#define AR_SWBA_TIMER_EN                    0x00000004
#define AR_HCF_TIMER_EN                     0x00000008
#define AR_TIM_TIMER_EN                     0x00000010
#define AR_DTIM_TIMER_EN                    0x00000020
#define AR_QUIET_TIMER_EN                   0x00000040
#define AR_NDP_TIMER_EN                     0x00000080
#define AR_TIMER_OVERFLOW_INDEX             0x00000700
#define AR_TIMER_OVERFLOW_INDEX_S           8
#define AR_TIMER_THRESH                     0xFFFFF000
#define AR_TIMER_THRESH_S                   12

#define AR_SLP32_MODE                  AR_MAC_PCU_OFFSET(MAC_PCU_SLP32_MODE)
#define AR_SLP32_HALF_CLK_LATENCY      0x000FFFFF    // rising <-> falling edge
#define AR_SLP32_ENA                   0x00100000
#define AR_SLP32_TSF_WRITE_STATUS      0x00200000    // tsf update in progress

#define AR_SLP32_WAKE                  AR_MAC_PCU_OFFSET(MAC_PCU_SLP32_WAKE)
#define AR_SLP32_WAKE_XTL_TIME         0x0000FFFF    // time to wake crystal

#define AR_SLP32_INC                   AR_MAC_PCU_OFFSET(MAC_PCU_SLP32_INC)
#define AR_SLP32_TST_INC               0x000FFFFF

/* Sleep MIB cycle count 32kHz cycles for which mac is asleep */
#define AR_SLP_CNT         AR_MAC_PCU_OFFSET(MAC_PCU_SLP_MIB1)
#define AR_SLP_CYCLE_CNT   0x8254    // absolute number of 32kHz cycles

/* Sleep MIB cycle count 2 */
#define AR_SLP_MIB2        AR_MAC_PCU_OFFSET(MAC_PCU_SLP_MIB2)

/* Sleep MIB control status */
#define AR_SLP_MIB_CTRL    AR_MAC_PCU_OFFSET(MAC_PCU_SLP_MIB3)
#define AR_SLP_MIB_CLEAR   0x00000001    // clear pending
#define AR_SLP_MIB_PENDING 0x00000002    // clear counters

//#ifdef AR9300_EMULATION
// MAC trace buffer registers (emulation only)
#define AR_MAC_PCU_LOGIC_ANALYZER               AR_MAC_PCU_OFFSET(MAC_PCU_LOGIC_ANALYZER)
#define AR_MAC_PCU_LOGIC_ANALYZER_CTL           0x0000000F
#define AR_MAC_PCU_LOGIC_ANALYZER_HOLD          0x00000001
#define AR_MAC_PCU_LOGIC_ANALYZER_CLEAR         0x00000002
#define AR_MAC_PCU_LOGIC_ANALYZER_STATE         0x00000004
#define AR_MAC_PCU_LOGIC_ANALYZER_ENABLE        0x00000008
#define AR_MAC_PCU_LOGIC_ANALYZER_QCU_SEL       0x000000F0
#define AR_MAC_PCU_LOGIC_ANALYZER_QCU_SEL_S     4
#define AR_MAC_PCU_LOGIC_ANALYZER_INT_ADDR      0x0003FF00
#define AR_MAC_PCU_LOGIC_ANALYZER_INT_ADDR_S    8

#define AR_MAC_PCU_LOGIC_ANALYZER_DIAG_MODE     0xFFFC0000
#define AR_MAC_PCU_LOGIC_ANALYZER_DIAG_MODE_S   18
#define AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20614   0x00040000
#define AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20768   0x20000000
#define AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20803   0x40000000
#define AR_MAC_PCU_LOGIC_ANALYZER_PSTABUG75996  0x9d500010
#define AR_MAC_PCU_LOGIC_ANALYZER_VC_MODE       0x9d400010

#define AR_MAC_PCU_LOGIC_ANALYZER_32L           AR_MAC_PCU_OFFSET(MAC_PCU_LOGIC_ANALYZER_32L)
#define AR_MAC_PCU_LOGIC_ANALYZER_16U           AR_MAC_PCU_OFFSET(MAC_PCU_LOGIC_ANALYZER_16U)

#define AR_MAC_PCU_TRACE_REG_START      0xE000
#define AR_MAC_PCU_TRACE_REG_END        0xFFFC
#define AR_MAC_PCU_TRACE_BUFFER_LENGTH (AR_MAC_PCU_TRACE_REG_END - AR_MAC_PCU_TRACE_REG_START + sizeof(uint32_t))
//#endif  // AR9300_EMULATION

/* MAC PCU global mode register */
#define AR_2040_MODE                    AR_MAC_PCU_OFFSET(MAC_PCU_20_40_MODE)
#define AR_2040_JOINED_RX_CLEAR         0x00000001   // use ctl + ext rx_clear for cca

/* MAC PCU H transfer timeout register */
#define AR_H_XFER_TIMEOUT               AR_MAC_PCU_OFFSET(MAC_PCU_H_XFER_TIMEOUT)
#define AR_EXBF_IMMDIATE_RESP           0x00000040
#define AR_EXBF_NOACK_NO_RPT            0x00000100
#define AR_H_XFER_TIMEOUT_COUNT         0xf
#define AR_H_XFER_TIMEOUT_COUNT_S       0

/*
 * Additional cycle counter. See also AR_CCCNT
 * extension channel rx clear count
 * counts number of cycles rx_clear (ext) is low (i.e. busy)
 * when the MAC is not actively transmitting/receiving
 */
#define AR_EXTRCCNT             AR_MAC_PCU_OFFSET(MAC_PCU_RX_CLEAR_DIFF_CNT)

/* antenna mask for self generated files */
#define AR_SELFGEN_MASK         AR_MAC_PCU_OFFSET(MAC_PCU_SELF_GEN_ANTENNA_MASK)

/* control registers for block BA control fields */
#define AR_BA_BAR_CONTROL       AR_MAC_PCU_OFFSET(MAC_PCU_BA_BAR_CONTROL)

/* legacy PLCP spoof */
#define AR_LEG_PLCP_SPOOF       AR_MAC_PCU_OFFSET(MAC_PCU_LEGACY_PLCP_SPOOF)

/* PHY error mask and EIFS mask continued */
#define AR_PHY_ERR_MASK_CONT    AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERROR_MASK_CONT)

/* MAC PCU transmit timer */
#define AR_TX_TIMER             AR_MAC_PCU_OFFSET(MAC_PCU_TX_TIMER)

/* MAC PCU transmit buffer control */
#define AR_PCU_TXBUF_CTRL               AR_MAC_PCU_OFFSET(MAC_PCU_TXBUF_CTRL)
#define AR_PCU_TXBUF_CTRL_SIZE_MASK     0x7FF
#define AR_PCU_TXBUF_CTRL_USABLE_SIZE   0x700

/*
 * MAC PCU miscellaneous mode 2
 * WAR flags for various bugs, see mac_pcu_reg documentation.
 */
#define AR_PCU_MISC_MODE2               AR_MAC_PCU_OFFSET(MAC_PCU_MISC_MODE2)
#define AR_PCU_MISC_MODE2_BUG_21532_ENABLE             0x00000001
#define AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE           0x00000002   /* Decrypt MGT frames using MFP method */
#define AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT   0x00000004   /* Don't decrypt MGT frames at all */

#define AR_BUG_58603_FIX_ENABLE                        0x00000008   /* Enable fix for bug 58603. This allows
                                                                     * the use of AR_AGG_WEP_ENABLE.
                                                                     */

#define AR_PCU_MISC_MODE2_PROM_VC_MODE                 0xa148103b   /* Enable promiscous in azimuth mode */

#define AR_PCU_MISC_MODE2_RESERVED                     0x00000038

#define AR_ADHOC_MCAST_KEYID_ENABLE                    0x00000040   /* This bit enables the Multicast search
                                                                     * based on both MAC Address and Key ID.
                                                                     * If bit is 0, then Multicast search is
                                                                     * based on MAC address only.
                                                                     * For Merlin and above only.
                                                                     */

#define AR_PCU_MISC_MODE2_CFP_IGNORE                   0x00000080
#define AR_PCU_MISC_MODE2_MGMT_QOS                     0x0000FF00 
#define AR_PCU_MISC_MODE2_MGMT_QOS_S                   8
#define AR_PCU_MISC_MODE2_ENABLE_LOAD_NAV_BEACON_DURATION 0x00010000
#define AR_AGG_WEP_ENABLE                              0x00020000   /* This field enables AGG_WEP feature,
                                                                     * when it is enable, AGG_WEP would takes
                                                                     * charge of the encryption interface of
                                                                     * pcu_txsm.
                                                                     */
#define AR_PCU_MISC_MODE2_HWWAR1                       0x00100000
#define AR_PCU_MISC_MODE2_PROXY_STA                    0x01000000  /* see EV 75996 */ 
#define AR_PCU_MISC_MODE2_HWWAR2                       0x02000000
#define AR_DECOUPLE_DECRYPTION                         0x08000000

#define AR_PCU_MISC_MODE2_RESERVED2                    0xFFFE0000

/* MAC PCU Alternate AES QoS mute mask */
#define AR_ALT_AES_MUTE_MASK            AR_MAC_PCU_OFFSET(MAC_PCU_ALT_AES_MUTE_MASK)

/* Async Fifo registers - debug only */
#define AR_ASYNC_FIFO_1                 AR_MAC_PCU_OFFSET(ASYNC_FIFO_REG1)
#define AR_ASYNC_FIFO_2                 AR_MAC_PCU_OFFSET(ASYNC_FIFO_REG2)
#define AR_ASYNC_FIFO_3                 AR_MAC_PCU_OFFSET(ASYNC_FIFO_REG3)

/* Maps the 16 user priority TID values to Access categories */
#define AR_TID_TO_AC_MAP                AR_MAC_PCU_OFFSET(MAC_PCU_TID_TO_AC)

/* High Priority Queue Control */
#define AR_HP_Q_CONTROL                 AR_MAC_PCU_OFFSET(MAC_PCU_HP_QUEUE)

/* Rx High Priority Queue Control */
#define AR_HPQ_CONTROL                  AR_MAC_PCU_OFFSET(MAC_PCU_HP_QUEUE)
#define AR_HPQ_ENABLE                   0x00000001
#define AR_HPQ_MASK_BE                  0x00000002
#define AR_HPQ_MASK_BK                  0x00000004
#define AR_HPQ_MASK_VI                  0x00000008
#define AR_HPQ_MASK_VO                  0x00000010
#define AR_HPQ_UAPSD                    0x00000020
#define AR_HPQ_FRAME_FILTER_0           0x00000040
#define AR_HPQ_FRAME_BSSID_MATCH_0      0x00000080
#define AR_HPQ_UAPSD_TRIGGER_EN         0x00100000

#define AR_BT_COEX_BT_WEIGHTS0          AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_BT_WEIGHTS0)
#define AR_BT_COEX_BT_WEIGHTS1          AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_BT_WEIGHTS1)
#define AR_BT_COEX_BT_WEIGHTS2          AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_BT_WEIGHTS2)
#define AR_BT_COEX_BT_WEIGHTS3          AR_MAC_PCU_OFFSET(MAC_PCU_BLUETOOTH_BT_WEIGHTS3)

#define AR_AGC_SATURATION_CNT0          AR_MAC_PCU_OFFSET(MAC_PCU_AGC_SATURATION_CNT0)
#define AR_AGC_SATURATION_CNT1          AR_MAC_PCU_OFFSET(MAC_PCU_AGC_SATURATION_CNT1)
#define AR_AGC_SATURATION_CNT2          AR_MAC_PCU_OFFSET(MAC_PCU_AGC_SATURATION_CNT2)

/* Hardware beacon processing */
#define AR_HWBCNPROC1                   AR_MAC_PCU_OFFSET(MAC_PCU_HW_BCN_PROC1)
#define AR_HWBCNPROC1_CRC_ENABLE        0x00000001  /* Enable hw beacon processing */
#define AR_HWBCNPROC1_RESET_CRC         0x00000002  /* Reset the last beacon CRC calculated */
#define AR_HWBCNPROC1_EXCLUDE_BCN_INTVL 0x00000004  /* Exclude Beacon interval in CRC calculation */
#define AR_HWBCNPROC1_EXCLUDE_CAP_INFO  0x00000008  /* Exclude Beacon capability information in CRC calculation */
#define AR_HWBCNPROC1_EXCLUDE_TIM_ELM   0x00000010  /* Exclude Beacon TIM element in CRC calculation */
#define AR_HWBCNPROC1_EXCLUDE_ELM0      0x00000020  /* Exclude element ID ELM0 in CRC calculation */
#define AR_HWBCNPROC1_EXCLUDE_ELM1      0x00000040  /* Exclude element ID ELM1 in CRC calculation */
#define AR_HWBCNPROC1_EXCLUDE_ELM2      0x00000080  /* Exclude element ID ELM2 in CRC calculation */
#define AR_HWBCNPROC1_ELM0_ID           0x0000FF00  /* Element ID 0 */
#define AR_HWBCNPROC1_ELM0_ID_S         8
#define AR_HWBCNPROC1_ELM1_ID           0x00FF0000  /* Element ID 1 */
#define AR_HWBCNPROC1_ELM1_ID_S         16
#define AR_HWBCNPROC1_ELM2_ID           0xFF000000  /* Element ID 2 */
#define AR_HWBCNPROC1_ELM2_ID_S         24

#define AR_HWBCNPROC2                   AR_MAC_PCU_OFFSET(MAC_PCU_HW_BCN_PROC2)
#define AR_HWBCNPROC2_FILTER_INTERVAL_ENABLE    0x00000001  /* Enable filtering beacons based on filter interval */
#define AR_HWBCNPROC2_RESET_INTERVAL            0x00000002  /* Reset internal interval counter interval */
#define AR_HWBCNPROC2_EXCLUDE_ELM3              0x00000004  /* Exclude element ID ELM3 in CRC calculation */
#define AR_HWBCNPROC2_RSVD                      0x000000F8  /* reserved */
#define AR_HWBCNPROC2_FILTER_INTERVAL           0x0000FF00  /* Filter interval for beacons */
#define AR_HWBCNPROC2_FILTER_INTERVAL_S         8
#define AR_HWBCNPROC2_ELM3_ID                   0x00FF0000  /* Element ID 3 */
#define AR_HWBCNPROC2_ELM3_ID_S                 16
#define AR_HWBCNPROC2_RSVD2                     0xFF000000  /* reserved */

#define AR_MAC_PCU_MISC_MODE3           AR_MAC_PCU_OFFSET(MAC_PCU_MISC_MODE3)
#define AR_BUG_61936_FIX_ENABLE         0x00000040  /* EV61936 - rx descriptor corruption */
#define AR_TIME_BASED_DISCARD_EN        0x80000000
#define AR_TIME_BASED_DISCARD_EN_S      31

#define AR_MAC_PCU_GEN_TIMER_TSF_SEL    AR_MAC_PCU_OFFSET(MAC_PCU_GENERIC_TIMERS_TSF_SEL)

#define AR_MAC_PCU_TBD_FILTER           AR_MAC_PCU_OFFSET(MAC_PCU_TBD_FILTER)
#define AR_MAC_PCU_USE_WBTIMER_TX_TS    0x00000001
#define AR_MAC_PCU_USE_WBTIMER_TX_TS_S  0
#define AR_MAC_PCU_USE_WBTIMER_RX_TS    0x00000002
#define AR_MAC_PCU_USE_WBTIMER_RX_TS_S  1

#define AR_TXBUF_BA                     AR_MAC_PCU_OFFSET(MAC_PCU_TXBUF_BA)


/* MAC Key Cache */
#define AR_KEYTABLE_0           AR_MAC_PCU_OFFSET(MAC_PCU_KEY_CACHE)
#define AR_KEYTABLE(_n)         (AR_KEYTABLE_0 + ((_n)*32))
#define AR_KEY_CACHE_SIZE       128
#define AR_RSVD_KEYTABLE_ENTRIES 4
#define AR_KEY_TYPE             0x00000007 // MAC Key Type Mask
#define AR_KEYTABLE_TYPE_40     0x00000000  /* WEP 40 bit key */
#define AR_KEYTABLE_TYPE_104    0x00000001  /* WEP 104 bit key */
#define AR_KEYTABLE_TYPE_128    0x00000003  /* WEP 128 bit key */
#define AR_KEYTABLE_TYPE_TKIP   0x00000004  /* TKIP and Michael */
#define AR_KEYTABLE_TYPE_AES    0x00000005  /* AES/OCB 128 bit key */
#define AR_KEYTABLE_TYPE_CCM    0x00000006  /* AES/CCM 128 bit key */
#define AR_KEYTABLE_TYPE_CLR    0x00000007  /* no encryption */
#define AR_KEYTABLE_ANT         0x00000008  /* previous transmit antenna */
#define AR_KEYTABLE_UAPSD       0x000001E0  /* UAPSD AC mask */
#define AR_KEYTABLE_UAPSD_S     5
#define AR_KEYTABLE_PWRMGT      0x00000200  /* hw managed PowerMgt bit */

#define AR_KEYTABLE_MMSS        0x00001c00  /* remote's MMSS*/
#define AR_KEYTABLE_MMSS_S      10
#define AR_KEYTABLE_CEC         0x00006000  /* remote's CEC*/
#define AR_KEYTABLE_CEC_S       13
#define AR_KEYTABLE_STAGGED     0x00010000  /* remote's stagged sounding*/
#define AR_KEYTABLE_STAGGED_S   16

#define AR_KEYTABLE_VALID       0x00008000  /* key and MAC address valid */
#define AR_KEYTABLE_KEY0(_n)    (AR_KEYTABLE(_n) + 0)   /* key bit 0-31 */
#define AR_KEYTABLE_KEY1(_n)    (AR_KEYTABLE(_n) + 4)   /* key bit 32-47 */
#define AR_KEYTABLE_KEY2(_n)    (AR_KEYTABLE(_n) + 8)   /* key bit 48-79 */
#define AR_KEYTABLE_KEY3(_n)    (AR_KEYTABLE(_n) + 12)  /* key bit 80-95 */
#define AR_KEYTABLE_KEY4(_n)    (AR_KEYTABLE(_n) + 16)  /* key bit 96-127 */
#define AR_KEYTABLE_TYPE(_n)    (AR_KEYTABLE(_n) + 20)  /* key type */
#define AR_KEYTABLE_MAC0(_n)    (AR_KEYTABLE(_n) + 24)  /* MAC address 1-32 */
#define AR_KEYTABLE_MAC1(_n)    (AR_KEYTABLE(_n) + 28)  /* MAC address 33-47 */
#define AR_KEYTABLE_DIR_ACK_BIT    0x00000010  /* Directed ACK bit */



/*
 * MAC WoW Registers.
 */
#define    AR_WOW_PATTERN_REG          AR_MAC_PCU_OFFSET(MAC_PCU_WOW1)
#define    AR_WOW_PAT_BACKOFF          0x00000004
#define    AR_WOW_BACK_OFF_SHIFT(x)    ((x & 0xf) << 27)    /* in usecs */
#define    AR_WOW_MAC_INTR_EN          0x00040000
#define    AR_WOW_MAGIC_EN             0x00010000
#define    AR_WOW_PATTERN_EN(x)        ((x & 0xff) << 0)
#define    AR_WOW_PATTERN_FOUND_SHIFT  8
#define    AR_WOW_PATTERN_FOUND(x)     (x & (0xff << AR_WOW_PATTERN_FOUND_SHIFT))
#define    AR_WOW_PATTERN_FOUND_MASK   ((0xff) << AR_WOW_PATTERN_FOUND_SHIFT)
#define    AR_WOW_MAGIC_PAT_FOUND      0x00020000
#define    AR_WOW_MAC_INTR             0x00080000
#define    AR_WOW_KEEP_ALIVE_FAIL      0x00100000
#define    AR_WOW_BEACON_FAIL          0x00200000


#define    AR_WOW_COUNT_REG            AR_MAC_PCU_OFFSET(MAC_PCU_WOW2)
#define    AR_WOW_AIFS_CNT(x)          ((x & 0xff) << 0)
#define    AR_WOW_SLOT_CNT(x)          ((x & 0xff) << 8)
#define    AR_WOW_KEEP_ALIVE_CNT(x)    ((x & 0xff) << 16)
/*
 * Default values for Wow Configuration for backoff, aifs, slot, keep-alive, etc.
 * to be programmed into various registers.
 */
#define    AR_WOW_CNT_AIFS_CNT         0x00000022    // AR_WOW_COUNT_REG
#define    AR_WOW_CNT_SLOT_CNT         0x00000009    // AR_WOW_COUNT_REG
/*
 * Keepalive count applicable for Merlin 2.0 and above.
 */
#define    AR_WOW_CNT_KA_CNT           0x00000008    // AR_WOW_COUNT_REG


#define    AR_WOW_BCN_EN_REG           AR_MAC_PCU_OFFSET(MAC_PCU_WOW3_BEACON_FAIL)
#define    AR_WOW_BEACON_FAIL_EN       0x00000001

#define    AR_WOW_BCN_TIMO_REG         AR_MAC_PCU_OFFSET(MAC_PCU_WOW3_BEACON)
#define    AR_WOW_BEACON_TIMO          0x40000000  /* Valid if BCN_EN is set */
#define    AR_WOW_BEACON_TIMO_MAX      0xFFFFFFFF  /* Max. value for Beacon Timeout */

#define    AR_WOW_KEEP_ALIVE_TIMO_REG  AR_MAC_PCU_OFFSET(MAC_PCU_WOW3_KEEP_ALIVE)
#define    AR_WOW_KEEP_ALIVE_TIMO      0x00007A12
#define    AR_WOW_KEEP_ALIVE_NEVER     0xFFFFFFFF

#define    AR_WOW_KEEP_ALIVE_REG       AR_MAC_PCU_OFFSET(MAC_PCU_WOW_KA)
#define    AR_WOW_KEEP_ALIVE_AUTO_DIS  0x00000001
#define    AR_WOW_KEEP_ALIVE_FAIL_DIS  0x00000002

#define    AR_WOW_US_SCALAR_REG        AR_MAC_PCU_OFFSET(PCU_1US)

#define    AR_WOW_KEEP_ALIVE_DELAY_REG AR_MAC_PCU_OFFSET(PCU_KA)
#define    AR_WOW_KEEP_ALIVE_DELAY     0x000003E8 // 1 msec

#define    AR_WOW_PATTERN_MATCH_REG    AR_MAC_PCU_OFFSET(WOW_EXACT)
#define    AR_WOW_PAT_END_OF_PKT(x)    ((x & 0xf) << 0)
#define    AR_WOW_PAT_OFF_MATCH(x)     ((x & 0xf) << 8)

#define    AR_WOW_PATTERN_MATCH_REG_2  AR_MAC_PCU_OFFSET(WOW2_EXACT)
#define    AR_WOW_PATTERN_OFF1_REG     AR_MAC_PCU_OFFSET(PCU_WOW4) /* Pattern bytes 0 -> 3 */
#define    AR_WOW_PATTERN_OFF2_REG     AR_MAC_PCU_OFFSET(PCU_WOW5) /* Pattern bytes 4 -> 7 */
#define    AR_WOW_PATTERN_OFF3_REG     AR_MAC_PCU_OFFSET(PCU_WOW6) /* Pattern bytes 8 -> 11 */
#define    AR_WOW_PATTERN_OFF4_REG     AR_MAC_PCU_OFFSET(PCU_WOW7) /* Pattern bytes 12 -> 15 */

/* start address of the frame in RxBUF */
#define    AR_WOW_RXBUF_START_ADDR     AR_MAC_PCU_OFFSET(MAC_PCU_WOW6)

/* Pattern detect and enable bits */
#define    AR_WOW_PATTERN_DETECT_ENABLE   AR_MAC_PCU_OFFSET(MAC_PCU_WOW4)

/* Rx Abort Enable */
#define    AR_WOW_RX_ABORT_ENABLE      AR_MAC_PCU_OFFSET(MAC_PCU_WOW5)

/* PHY error counter 1, 2, and 3 mask continued */
#define    AR_PHY_ERR_CNT_MASK_CONT    AR_MAC_PCU_OFFSET(MAC_PCU_PHY_ERR_CNT_MASK_CONT)

/* AZIMUTH mode reg can be used for proxySTA */
#define AR_AZIMUTH_MODE                 AR_MAC_PCU_OFFSET(MAC_PCU_AZIMUTH_MODE)
#define AR_AZIMUTH_KEY_SEARCH_AD1       0x00000002
#define AR_AZIMUTH_CTS_MATCH_TX_AD2     0x00000040
#define AR_AZIMUTH_BA_USES_AD1          0x00000080
#define AR_AZIMUTH_FILTER_PASS_HOLD     0x00000200

/* Length of Pattern Match for Pattern */
#define    AR_WOW_LENGTH1_REG          AR_MAC_PCU_OFFSET(MAC_PCU_WOW_LENGTH1)
#define    AR_WOW_LENGTH2_REG          AR_MAC_PCU_OFFSET(MAC_PCU_WOW_LENGTH2)
#define    AR_WOW_LENGTH3_REG          AR_MAC_PCU_OFFSET(MAC_PCU_WOW_LENGTH3)
#define    AR_WOW_LENGTH4_REG          AR_MAC_PCU_OFFSET(MAC_PCU_WOW_LENGTH4)

#define    AR_LOC_CTL_REG              AR_MAC_PCU_OFFSET(MAC_PCU_LOCATION_MODE_CONTROL)
#define    AR_LOC_TIMER_REG            AR_MAC_PCU_OFFSET(MAC_PCU_LOCATION_MODE_TIMER)
#define    AR_LOC_CTL_REG_FS           0x1

/* Register to enable pattern match for less than 256 bytes packets */
#define AR_WOW_PATTERN_MATCH_LT_256B_REG    AR_MAC_PCU_OFFSET(WOW_PATTERN_MATCH_LESS_THAN_256_BYTES)


#define    AR_WOW_STATUS(x) (x & (AR_WOW_PATTERN_FOUND_MASK | AR_WOW_MAGIC_PAT_FOUND | \
                               AR_WOW_KEEP_ALIVE_FAIL | AR_WOW_BEACON_FAIL))
#define AR_WOW_CLEAR_EVENTS(x)     (x & ~(AR_WOW_PATTERN_EN(0xff) | \
       AR_WOW_MAGIC_EN | AR_WOW_MAC_INTR_EN | AR_WOW_BEACON_FAIL | \
       AR_WOW_KEEP_ALIVE_FAIL))


/*
 * Keep it long for Beacon workaround - ensures no AH_FALSE alarm
 */
#define AR_WOW_BMISSTHRESHOLD       0x20


/* WoW - Transmit buffer for keep alive frames */
#define AR_WOW_TRANSMIT_BUFFER      AR_MAC_PCU_OFFSET(MAC_PCU_BUF)
#define AR_WOW_TXBUF(_i)    (AR_WOW_TRANSMIT_BUFFER + ((_i)<<2))

#define AR_WOW_KA_DESC_WORD2        AR_WOW_TXBUF(0)
#define AR_WOW_KA_DESC_WORD3        AR_WOW_TXBUF(1)
#define AR_WOW_KA_DESC_WORD4        AR_WOW_TXBUF(2)
#define AR_WOW_KA_DESC_WORD5        AR_WOW_TXBUF(3)
#define AR_WOW_KA_DESC_WORD6        AR_WOW_TXBUF(4)
#define AR_WOW_KA_DESC_WORD7        AR_WOW_TXBUF(5)
#define AR_WOW_KA_DESC_WORD8        AR_WOW_TXBUF(6)
#define AR_WOW_KA_DESC_WORD9        AR_WOW_TXBUF(7)
#define AR_WOW_KA_DESC_WORD10       AR_WOW_TXBUF(8)
#define AR_WOW_KA_DESC_WORD11       AR_WOW_TXBUF(9)
#define AR_WOW_KA_DESC_WORD12       AR_WOW_TXBUF(10)
#define AR_WOW_KA_DESC_WORD13       AR_WOW_TXBUF(11)

/* KA_DATA_WORD = 6 words. Depending on the number of
 * descriptor words, it can start at AR_WOW_TXBUF(12)
 * or AR_WOW_TXBUF(13) */

#define AR_WOW_OFFLOAD_GTK_DATA_START       AR_WOW_TXBUF(19)

#define AR_WOW_KA_DATA_WORD_END_JUPITER     AR_WOW_TXBUF(60)

#define AR_WOW_SW_NULL_PARAMETER            AR_WOW_TXBUF(61)
#define AR_WOW_SW_NULL_LONG_PERIOD_MASK     0x0000FFFF
#define AR_WOW_SW_NULL_LONG_PERIOD_MASK_S   0
#define AR_WOW_SW_NULL_SHORT_PERIOD_MASK    0xFFFF0000
#define AR_WOW_SW_NULL_SHORT_PERIOD_MASK_S  16

#define AR_WOW_OFFLOAD_COMMAND_JUPITER      AR_WOW_TXBUF(62)
#define AR_WOW_OFFLOAD_ENA_GTK              0x80000000
#define AR_WOW_OFFLOAD_ENA_ACER_MAGIC       0x40000000
#define AR_WOW_OFFLOAD_ENA_STD_MAGIC        0x20000000
#define AR_WOW_OFFLOAD_ENA_SWKA             0x10000000
#define AR_WOW_OFFLOAD_ENA_ARP_OFFLOAD      0x08000000
#define AR_WOW_OFFLOAD_ENA_NS_OFFLOAD       0x04000000
#define AR_WOW_OFFLOAD_ENA_4WAY_WAKE        0x02000000
#define AR_WOW_OFFLOAD_ENA_GTK_ERROR_WAKE   0x01000000
#define AR_WOW_OFFLOAD_ENA_AP_LOSS_WAKE     0x00800000
#define AR_WOW_OFFLOAD_ENA_BT_SLEEP         0x00080000
#define AR_WOW_OFFLOAD_ENA_SW_NULL          0x00040000
#define AR_WOW_OFFLOAD_ENA_HWKA_FAIL        0x00020000
#define AR_WOW_OFFLOAD_ENA_DEVID_SWAR       0x00010000

#define AR_WOW_OFFLOAD_STATUS_JUPITER       AR_WOW_TXBUF(63)

/* WoW Transmit Buffer for patterns */
#define AR_WOW_TB_PATTERN0          AR_WOW_TXBUF(64)
#define AR_WOW_TB_PATTERN1          AR_WOW_TXBUF(128)
#define AR_WOW_TB_PATTERN2          AR_WOW_TXBUF(192)
#define AR_WOW_TB_PATTERN3          AR_WOW_TXBUF(256)
#define AR_WOW_TB_PATTERN4          AR_WOW_TXBUF(320)
#define AR_WOW_TB_PATTERN5          AR_WOW_TXBUF(384)
#define AR_WOW_TB_PATTERN6          AR_WOW_TXBUF(448)
#define AR_WOW_TB_PATTERN7          AR_WOW_TXBUF(512)
#define AR_WOW_TB_MASK0             AR_WOW_TXBUF(768)
#define AR_WOW_TB_MASK1             AR_WOW_TXBUF(776)
#define AR_WOW_TB_MASK2             AR_WOW_TXBUF(784)
#define AR_WOW_TB_MASK3             AR_WOW_TXBUF(792)
#define AR_WOW_TB_MASK4             AR_WOW_TXBUF(800)
#define AR_WOW_TB_MASK5             AR_WOW_TXBUF(808)
#define AR_WOW_TB_MASK6             AR_WOW_TXBUF(816)
#define AR_WOW_TB_MASK7             AR_WOW_TXBUF(824)


#define AR_WOW_OFFLOAD_GTK_TXDESC_PARAM_START   AR_WOW_TXBUF(825)
#define AR_WOW_OFFLOAD_GTK_TXDESC_PARAM_START_JUPITER   AR_WOW_TXBUF(832)
#define AR_WOW_OFFLOAD_GTK_TXDESC_PARAM_WORDS   4

#define AR_WOW_OFFLOAD_GTK_DATA_START_JUPITER       AR_WOW_TXBUF(836)
#define AR_WOW_OFFLOAD_GTK_DATA_WORDS_JUPITER       20

#define AR_WOW_OFFLOAD_ACER_MAGIC_START         AR_WOW_TXBUF(856)
#define AR_WOW_OFFLOAD_ACER_MAGIC_WORDS         2

#define AR_WOW_OFFLOAD_ACER_KA0_START           AR_WOW_TXBUF(858)
#define AR_WOW_OFFLOAD_ACER_KA0_PERIOD_MS       AR_WOW_TXBUF(858)
#define AR_WOW_OFFLOAD_ACER_KA0_SIZE            AR_WOW_TXBUF(859)
#define AR_WOW_OFFLOAD_ACER_KA0_DATA            AR_WOW_TXBUF(860)
#define AR_WOW_OFFLOAD_ACER_KA0_DATA_WORDS      20
#define AR_WOW_OFFLOAD_ACER_KA0_WORDS           22

#define AR_WOW_OFFLOAD_ACER_KA1_START           AR_WOW_TXBUF(880)
#define AR_WOW_OFFLOAD_ACER_KA1_PERIOD_MS       AR_WOW_TXBUF(880)
#define AR_WOW_OFFLOAD_ACER_KA1_SIZE            AR_WOW_TXBUF(881)
#define AR_WOW_OFFLOAD_ACER_KA1_DATA            AR_WOW_TXBUF(882)
#define AR_WOW_OFFLOAD_ACER_KA1_DATA_WORDS      20
#define AR_WOW_OFFLOAD_ACER_KA1_WORDS           22

#define AR_WOW_OFFLOAD_ARP0_START               AR_WOW_TXBUF(902)
#define AR_WOW_OFFLOAD_ARP0_VALID               AR_WOW_TXBUF(902)
#define AR_WOW_OFFLOAD_ARP0_RMT_IP              AR_WOW_TXBUF(903)
#define AR_WOW_OFFLOAD_ARP0_HOST_IP             AR_WOW_TXBUF(904)
#define AR_WOW_OFFLOAD_ARP0_MAC_L               AR_WOW_TXBUF(905)
#define AR_WOW_OFFLOAD_ARP0_MAC_H               AR_WOW_TXBUF(906)
#define AR_WOW_OFFLOAD_ARP0_WORDS               5

#define AR_WOW_OFFLOAD_ARP1_START               AR_WOW_TXBUF(907)
#define AR_WOW_OFFLOAD_ARP1_VALID               AR_WOW_TXBUF(907)
#define AR_WOW_OFFLOAD_ARP1_RMT_IP              AR_WOW_TXBUF(908)
#define AR_WOW_OFFLOAD_ARP1_HOST_IP             AR_WOW_TXBUF(909)
#define AR_WOW_OFFLOAD_ARP1_MAC_L               AR_WOW_TXBUF(910)
#define AR_WOW_OFFLOAD_ARP1_MAC_H               AR_WOW_TXBUF(911)
#define AR_WOW_OFFLOAD_ARP1_WORDS               5

#define AR_WOW_OFFLOAD_NS0_START                AR_WOW_TXBUF(912)
#define AR_WOW_OFFLOAD_NS0_VALID                AR_WOW_TXBUF(912)
#define AR_WOW_OFFLOAD_NS0_RMT_IPV6             AR_WOW_TXBUF(913)
#define AR_WOW_OFFLOAD_NS0_SOLICIT_IPV6         AR_WOW_TXBUF(917)
#define AR_WOW_OFFLOAD_NS0_MAC_L                AR_WOW_TXBUF(921)
#define AR_WOW_OFFLOAD_NS0_MAC_H                AR_WOW_TXBUF(922)
#define AR_WOW_OFFLOAD_NS0_TGT0_IPV6            AR_WOW_TXBUF(923)
#define AR_WOW_OFFLOAD_NS0_TGT1_IPV6            AR_WOW_TXBUF(927)
#define AR_WOW_OFFLOAD_NS0_WORDS                19

#define AR_WOW_OFFLOAD_NS1_START                AR_WOW_TXBUF(931)
#define AR_WOW_OFFLOAD_NS1_VALID                AR_WOW_TXBUF(931)
#define AR_WOW_OFFLOAD_NS1_RMT_IPV6             AR_WOW_TXBUF(932)
#define AR_WOW_OFFLOAD_NS1_SOLICIT_IPV6         AR_WOW_TXBUF(936)
#define AR_WOW_OFFLOAD_NS1_MAC_L                AR_WOW_TXBUF(940)
#define AR_WOW_OFFLOAD_NS1_MAC_H                AR_WOW_TXBUF(941)
#define AR_WOW_OFFLOAD_NS1_TGT0_IPV6            AR_WOW_TXBUF(942)
#define AR_WOW_OFFLOAD_NS1_TGT1_IPV6            AR_WOW_TXBUF(946)
#define AR_WOW_OFFLOAD_NS1_WORDS                19

#define AR_WOW_OFFLOAD_WLAN_REGSET_START        AR_WOW_TXBUF(950)
#define AR_WOW_OFFLOAD_WLAN_REGSET_NUM          AR_WOW_TXBUF(950)
#define AR_WOW_OFFLOAD_WLAN_REGSET_REGVAL       AR_WOW_TXBUF(951)
#define AR_WOW_OFFLOAD_WLAN_REGSET_MAX_PAIR     32
#define AR_WOW_OFFLOAD_WLAN_REGSET_WORDS        65  //(1 + AR_WOW_OFFLOAD_WLAN_REGSET_MAX_PAIR * 2)

/* Currently Pattern 0-7 are supported - so bit 0-7 are set */
#define AR_WOW_PATTERN_SUPPORTED    0xFF
#define AR_WOW_LENGTH_MAX           0xFF
#define AR_WOW_LENGTH1_SHIFT(_i)    ((0x3 - ((_i) & 0x3)) << 0x3)
#define AR_WOW_LENGTH1_MASK(_i)     (AR_WOW_LENGTH_MAX << AR_WOW_LENGTH1_SHIFT(_i))
#define AR_WOW_LENGTH2_SHIFT(_i)    ((0x7 - ((_i) & 0x7)) << 0x3)
#define AR_WOW_LENGTH2_MASK(_i)     (AR_WOW_LENGTH_MAX << AR_WOW_LENGTH2_SHIFT(_i))

/*
 * MAC Direct Connect registers
 *
 * Added to support dual BSSID/TSF which are needed in the application
 * of Mesh networking or Direct Connect.
 */

/*
 * Note that the only function added with this BSSID2 is to receive
 * multi/broadcast from BSSID2 as well
 */
/* MAC BSSID low 32 bits */
#define AR_BSS2_ID0           AR_MAC_PCU_OFFSET(MAC_PCU_BSSID2_L32)
/* MAC BSSID upper 16 bits / AID */
#define AR_BSS2_ID1           AR_MAC_PCU_OFFSET(MAC_PCU_BSSID2_U16)

/*
 * Secondary TSF support added for dual BSSID/TSF
 */
/* MAC local clock lower 32 bits */
#define AR_TSF2_L32          AR_MAC_PCU_OFFSET(MAC_PCU_TSF2_L32)
/* MAC local clock upper 32 bits */
#define AR_TSF2_U32          AR_MAC_PCU_OFFSET(MAC_PCU_TSF2_U32)

/* MAC Direct Connect Control */
#define AR_DIRECT_CONNECT    AR_MAC_PCU_OFFSET(MAC_PCU_DIRECT_CONNECT)
#define AR_DC_AP_STA_EN      0x00000001
#define AR_DC_AP_STA_EN_S    0

/*
 * tx_bf Register
 */
#define AR_SVD_OFFSET(_x)   offsetof(struct svd_reg, _x)

#define AR_TXBF_DBG         AR_SVD_OFFSET(TXBF_DBG)

#define AR_TXBF             AR_SVD_OFFSET(TXBF)
#define AR_TXBF_CB_TX       0x00000003
#define AR_TXBF_CB_TX_S     0
#define AR_TXBF_PSI_1_PHI_3         0
#define AR_TXBF_PSI_2_PHI_4         1        
#define AR_TXBF_PSI_3_PHI_5         2
#define AR_TXBF_PSI_4_PHI_6         3

#define AR_TXBF_NB_TX       0x0000000C
#define AR_TXBF_NB_TX_S     2
#define AR_TXBF_NUMBEROFBIT_4       0
#define AR_TXBF_NUMBEROFBIT_2       1
#define AR_TXBF_NUMBEROFBIT_6       2
#define AR_TXBF_NUMBEROFBIT_8       3

#define AR_TXBF_NG_RPT_TX   0x00000030
#define AR_TXBF_NG_RPT_TX_S 4
#define AR_TXBF_No_GROUP            0
#define AR_TXBF_TWO_GROUP           1
#define AR_TXBF_FOUR_GROUP          2

#define AR_TXBF_NG_CVCACHE  0x000000C0
#define AR_TXBF_NG_CVCACHE_S  6
#define AR_TXBF_FOUR_CLIENTS        0
#define AR_TXBF_EIGHT_CLIENTS       1
#define AR_TXBF_SIXTEEN_CLIENTS     2

#define AR_TXBF_TXCV_BFWEIGHT_METHOD 0x00000600
#define AR_TXBF_TXCV_BFWEIGHT_METHOD_S 9
#define AR_TXBF_NO_WEIGHTING        0
#define AR_TXBF_MAX_POWER           1
#define AR_TXBF_KEEP_RATIO          2

#define AR_TXBF_RLR_EN        0x00000800
#define AR_TXBF_RC_20_U_DONE  0x00001000
#define AR_TXBF_RC_20_L_DONE  0x00002000
#define AR_TXBF_RC_40_DONE    0x00004000
#define AR_TXBF_FORCE_UPDATE_V2BB   0x00008000

#define AR_TXBF_TIMER             AR_SVD_OFFSET(TXBF_TIMER)
#define AR_TXBF_TIMER_TIMEOUT     0x000000FF
#define AR_TXBF_TIMER_TIMEOUT_S   0
#define AR_TXBF_TIMER_ATIMEOU     0x0000FF00
#define AR_TXBF_TIMER_ATIMEOUT_S  8

/* for SVD cache update */
#define AR_TXBF_SW          AR_SVD_OFFSET(TXBF_SW)
#define AR_LRU_ACK          0x00000001
#define AR_LRU_ADDR         0x000003FE
#define AR_LRU_ADDR_S       1
#define AR_LRU_EN           0x00000800
#define AR_LRU_EN_S         11
#define AR_DEST_IDX         0x0007f000
#define AR_DEST_IDX_S       12
#define AR_LRU_WR_ACK       0x00080000
#define AR_LRU_WR_ACK_S     19
#define AR_LRU_RD_ACK       0x00100000
#define AR_LRU_RD_ACK_S     20

#define AR_RC0_0            AR_SVD_OFFSET(RC0)
#define AR_RC0(_idx)        (AR_RC0_0+(_idx))
#define AR_RC1_0            AR_SVD_OFFSET(RC1)
#define AR_RC1(_idx)        (AR_RC1_0+(_idx))

#define AR_CVCACHE_0        AR_SVD_OFFSET(CVCACHE)
#define AR_CVCACHE(_idx)    (AR_CVCACHE_0+(_idx))
/* for CV CACHE Header */
#define AR_CVCACHE_Ng_IDX   0x0000C000
#define AR_CVCACHE_Ng_IDX_S 14
#define AR_CVCACHE_BW40     0x00010000
#define AR_CVCACHE_BW40_S   16
#define AR_CVCACHE_IMPLICIT 0x00020000
#define AR_CVCACHE_IMPLICIT_S   17
#define AR_CVCACHE_DEST_IDX 0x01FC0000
#define AR_CVCACHE_DEST_IDX_S   18
#define AR_CVCACHE_Nc_IDX   0x06000000
#define AR_CVCACHE_Nc_IDX_S 25
#define AR_CVCACHE_Nr_IDX   0x18000000
#define AR_CVCACHE_Nr_IDX_S 27
#define AR_CVCACHE_EXPIRED  0x20000000
#define AR_CVCACHE_EXPIRED_S    29
#define AR_CVCACHE_WRITE    0x80000000
/* for CV cache data*/
#define AR_CVCACHE_RD_EN    0x40000000
#define AR_CVCACHE_DATA     0x3fffffff
/*
 * ANT DIV setting
 */
#define ANT_DIV_CONTROL_ALL (0x7e000000)
#define ANT_DIV_CONTROL_ALL_S (25)
#define ANT_DIV_ENABLE (0x1000000)
#define ANT_DIV_ENABLE_S (24)
#define FAST_DIV_ENABLE (0x2000)
#define FAST_DIV_ENABLE_S (13)

/* Global register */
#define AR_GLB_REG_OFFSET(_x)     offsetof(struct wlan_bt_glb_reg_pcie, _x)

#define AR_MBOX_CTRL_STATUS                 AR_GLB_REG_OFFSET(GLB_MBOX_CONTROL_STATUS)
#define AR_MBOX_INT_EMB_CPU                 0x0001
#define AR_MBOX_INT_WLAN                    0x0002
#define AR_MBOX_RESET                       0x0004
#define AR_MBOX_RAM_REQ_MASK                0x0018
#define AR_MBOX_RAM_REQ_NO_RAM              0x0000
#define AR_MBOX_RAM_REQ_USB                 0x0008 
#define AR_MBOX_RAM_REQ_WLAN_BUF            0x0010
#define AR_MBOX_RAM_REQ_PATCH_REAPPY        0x0018
#define AR_MBOX_RAM_CONF                    0x0020
#define AR_MBOX_WLAN_BUF                    0x0040
#define AR_MBOX_WOW_REQ                     0x0080
#define AR_MBOX_WOW_CONF                    0x0100
#define AR_MBOX_WOW_ERROR_MASK              0x1e00
#define AR_MBOX_WOW_ERROR_NONE              0x0000
#define AR_MBOX_WOW_ERROR_INVALID_MSG       0x0200
#define AR_MBOX_WOW_ERROR_MALFORMED_MSG     0x0400
#define AR_MBOX_WOW_ERROR_INVALID_RAM_IMAGE 0x0600

#define AR_WLAN_WOW_STATUS                 AR_GLB_REG_OFFSET(GLB_WLAN_WOW_STATUS)

#define AR_WLAN_WOW_ENABLE                 AR_GLB_REG_OFFSET(GLB_WLAN_WOW_ENABLE)

#define AR_EMB_CPU_WOW_STATUS               AR_GLB_REG_OFFSET(GLB_EMB_CPU_WOW_STATUS)
#define AR_EMB_CPU_WOW_STATUS_KEEP_ALIVE_FAIL 0x1
#define AR_EMB_CPU_WOW_STATUS_BEACON_MISS     0x2
#define AR_EMB_CPU_WOW_STATUS_PATTERN_MATCH   0x4
#define AR_EMB_CPU_WOW_STATUS_MAGIC_PATTERN   0x8
  
#define AR_EMB_CPU_WOW_ENABLE               AR_GLB_REG_OFFSET(GLB_EMB_CPU_WOW_ENABLE)
#define AR_EMB_CPU_WOW_ENABLE_KEEP_ALIVE_FAIL 0x1
#define AR_EMB_CPU_WOW_ENABLE_BEACON_MISS     0x2
#define AR_EMB_CPU_WOW_ENABLE_PATTERN_MATCH   0x4
#define AR_EMB_CPU_WOW_ENABLE_MAGIC_PATTERN   0x8

#define AR_SW_WOW_CONTROL                   AR_GLB_REG_OFFSET(GLB_SW_WOW_CONTROL)
#define AR_SW_WOW_ENABLE                    0x1
#define AR_SWITCH_TO_REFCLK                 0x2
#define AR_RESET_CONTROL                    0x4
#define AR_RESET_VALUE_MASK                 0x8
#define AR_HW_WOW_DISABLE                   0x10
#define AR_CLR_MAC_INTERRUPT                0x20
#define AR_CLR_KA_INTERRUPT                 0x40

/*
 * WLAN coex registers
 */
#define AR_WLAN_COEX_OFFSET(_x)   offsetof(struct wlan_coex_reg, _x)

#define AR_MCI_COMMAND0                 AR_WLAN_COEX_OFFSET(MCI_COMMAND0)
#define AR_MCI_COMMAND0_HEADER          0xFF
#define AR_MCI_COMMAND0_HEADER_S        0
#define AR_MCI_COMMAND0_LEN             0x1f00
#define AR_MCI_COMMAND0_LEN_S           8
#define AR_MCI_COMMAND0_DISABLE_TIMESTAMP 0x2000
#define AR_MCI_COMMAND0_DISABLE_TIMESTAMP_S 13

#define AR_MCI_COMMAND1                 AR_WLAN_COEX_OFFSET(MCI_COMMAND1)

#define AR_MCI_COMMAND2                 AR_WLAN_COEX_OFFSET(MCI_COMMAND2)
#define AR_MCI_COMMAND2_RESET_TX        0x01
#define AR_MCI_COMMAND2_RESET_TX_S      0
#define AR_MCI_COMMAND2_RESET_RX        0x02
#define AR_MCI_COMMAND2_RESET_RX_S      1
#define AR_MCI_COMMAND2_RESET_RX_NUM_CYCLES     0x3FC
#define AR_MCI_COMMAND2_RESET_RX_NUM_CYCLES_S   2
#define AR_MCI_COMMAND2_RESET_REQ_WAKEUP        0x400
#define AR_MCI_COMMAND2_RESET_REQ_WAKEUP_S      10

#define AR_MCI_RX_CTRL                  AR_WLAN_COEX_OFFSET(MCI_RX_CTRL)

#define AR_MCI_TX_CTRL                  AR_WLAN_COEX_OFFSET(MCI_TX_CTRL)
/* 0 = no division, 1 = divide by 2, 2 = divide by 4, 3 = divide by 8 */
#define AR_MCI_TX_CTRL_CLK_DIV          0x03
#define AR_MCI_TX_CTRL_CLK_DIV_S        0
#define AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE 0x04
#define AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE_S 2
#define AR_MCI_TX_CTRL_GAIN_UPDATE_FREQ 0xFFFFF8
#define AR_MCI_TX_CTRL_GAIN_UPDATE_FREQ_S 3
#define AR_MCI_TX_CTRL_GAIN_UPDATE_NUM  0xF000000
#define AR_MCI_TX_CTRL_GAIN_UPDATE_NUM_S 24

#define AR_MCI_MSG_ATTRIBUTES_TABLE     AR_WLAN_COEX_OFFSET(MCI_MSG_ATTRIBUTES_TABLE)
#define AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM 0xFFFF
#define AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM_S 0
#define AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR 0xFFFF0000
#define AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR_S 16

#define AR_MCI_SCHD_TABLE_0             AR_WLAN_COEX_OFFSET(MCI_SCHD_TABLE_0)
#define AR_MCI_SCHD_TABLE_1             AR_WLAN_COEX_OFFSET(MCI_SCHD_TABLE_1)
#define AR_MCI_GPM_0                    AR_WLAN_COEX_OFFSET(MCI_GPM_0)
#define AR_MCI_GPM_1                    AR_WLAN_COEX_OFFSET(MCI_GPM_1)
#define AR_MCI_GPM_WRITE_PTR            0xFFFF0000
#define AR_MCI_GPM_WRITE_PTR_S          16
#define AR_MCI_GPM_BUF_LEN              0x0000FFFF
#define AR_MCI_GPM_BUF_LEN_S            0

#define AR_MCI_INTERRUPT_RAW            AR_WLAN_COEX_OFFSET(MCI_INTERRUPT_RAW)
#define AR_MCI_INTERRUPT_EN             AR_WLAN_COEX_OFFSET(MCI_INTERRUPT_EN)
#define AR_MCI_INTERRUPT_SW_MSG_DONE            0x00000001
#define AR_MCI_INTERRUPT_SW_MSG_DONE_S          0
#define AR_MCI_INTERRUPT_CPU_INT_MSG            0x00000002
#define AR_MCI_INTERRUPT_CPU_INT_MSG_S          1
#define AR_MCI_INTERRUPT_RX_CKSUM_FAIL          0x00000004
#define AR_MCI_INTERRUPT_RX_CKSUM_FAIL_S        2
#define AR_MCI_INTERRUPT_RX_INVALID_HDR         0x00000008
#define AR_MCI_INTERRUPT_RX_INVALID_HDR_S       3
#define AR_MCI_INTERRUPT_RX_HW_MSG_FAIL         0x00000010
#define AR_MCI_INTERRUPT_RX_HW_MSG_FAIL_S       4
#define AR_MCI_INTERRUPT_RX_SW_MSG_FAIL         0x00000020
#define AR_MCI_INTERRUPT_RX_SW_MSG_FAIL_S       5
#define AR_MCI_INTERRUPT_TX_HW_MSG_FAIL         0x00000080
#define AR_MCI_INTERRUPT_TX_HW_MSG_FAIL_S       7
#define AR_MCI_INTERRUPT_TX_SW_MSG_FAIL         0x00000100
#define AR_MCI_INTERRUPT_TX_SW_MSG_FAIL_S       8
#define AR_MCI_INTERRUPT_RX_MSG                 0x00000200
#define AR_MCI_INTERRUPT_RX_MSG_S               9
#define AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE    0x00000400
#define AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE_S  10
#define AR_MCI_INTERRUPT_BT_PRI                 0x07fff800
#define AR_MCI_INTERRUPT_BT_PRI_S               11
#define AR_MCI_INTERRUPT_BT_PRI_THRESH          0x08000000
#define AR_MCI_INTERRUPT_BT_PRI_THRESH_S        27
#define AR_MCI_INTERRUPT_BT_FREQ                0x10000000
#define AR_MCI_INTERRUPT_BT_FREQ_S              28
#define AR_MCI_INTERRUPT_BT_STOMP               0x20000000
#define AR_MCI_INTERRUPT_BT_STOMP_S             29
#define AR_MCI_INTERRUPT_BB_AIC_IRQ             0x40000000
#define AR_MCI_INTERRUPT_BB_AIC_IRQ_S           30
#define AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT      0x80000000
#define AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT_S    31

#define AR_MCI_INTERRUPT_MSG_FAIL_MASK ( AR_MCI_INTERRUPT_RX_HW_MSG_FAIL | \
                                         AR_MCI_INTERRUPT_RX_SW_MSG_FAIL | \
                                         AR_MCI_INTERRUPT_TX_HW_MSG_FAIL | \
                                         AR_MCI_INTERRUPT_TX_SW_MSG_FAIL )

#define AR_MCI_INTERRUPT_DEFAULT              ( AR_MCI_INTERRUPT_SW_MSG_DONE |          \
                                                AR_MCI_INTERRUPT_RX_INVALID_HDR |       \
                                                AR_MCI_INTERRUPT_RX_HW_MSG_FAIL |       \
                                                AR_MCI_INTERRUPT_RX_SW_MSG_FAIL |       \
                                                AR_MCI_INTERRUPT_TX_HW_MSG_FAIL |       \
                                                AR_MCI_INTERRUPT_TX_SW_MSG_FAIL |       \
                                                AR_MCI_INTERRUPT_RX_MSG |               \
                                                AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE |  \
                                                AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT )

#define AR_MCI_REMOTE_CPU_INT           AR_WLAN_COEX_OFFSET(MCI_REMOTE_CPU_INT)
#define AR_MCI_REMOTE_CPU_INT_EN        AR_WLAN_COEX_OFFSET(MCI_REMOTE_CPU_INT_EN)

#define AR_MCI_INTERRUPT_RX_MSG_RAW     AR_WLAN_COEX_OFFSET(MCI_INTERRUPT_RX_MSG_RAW)
#define AR_MCI_INTERRUPT_RX_MSG_EN      AR_WLAN_COEX_OFFSET(MCI_INTERRUPT_RX_MSG_EN)
#define AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET    0x00000001
#define AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET_S  0
#define AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL     0x00000002
#define AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL_S   1
#define AR_MCI_INTERRUPT_RX_MSG_CONT_NACK       0x00000004
#define AR_MCI_INTERRUPT_RX_MSG_CONT_NACK_S     2
#define AR_MCI_INTERRUPT_RX_MSG_CONT_INFO       0x00000008
#define AR_MCI_INTERRUPT_RX_MSG_CONT_INFO_S     3
#define AR_MCI_INTERRUPT_RX_MSG_CONT_RST        0x00000010
#define AR_MCI_INTERRUPT_RX_MSG_CONT_RST_S      4
#define AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO       0x00000020
#define AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO_S     5
#define AR_MCI_INTERRUPT_RX_MSG_CPU_INT         0x00000040
#define AR_MCI_INTERRUPT_RX_MSG_CPU_INT_S       6
#define AR_MCI_INTERRUPT_RX_MSG_GPM             0x00000100
#define AR_MCI_INTERRUPT_RX_MSG_GPM_S           8
#define AR_MCI_INTERRUPT_RX_MSG_LNA_INFO        0x00000200
#define AR_MCI_INTERRUPT_RX_MSG_LNA_INFO_S      9
#define AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING    0x00000400
#define AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING_S  10
#define AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING      0x00000800
#define AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING_S    11
#define AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE        0x00001000
#define AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE_S      12
#ifdef AH_DEBUG
#define AR_MCI_INTERRUPT_RX_MSG_DEFAULT       ( AR_MCI_INTERRUPT_RX_MSG_GPM           | \
                                                AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET  | \
                                                AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING    | \
                                                AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING  | \
                                                AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO     | \
                                                AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL   | \
                                                AR_MCI_INTERRUPT_RX_MSG_LNA_INFO      | \
                                                AR_MCI_INTERRUPT_RX_MSG_CONT_NACK     | \
                                                AR_MCI_INTERRUPT_RX_MSG_CONT_INFO     | \
                                                AR_MCI_INTERRUPT_RX_MSG_CONT_RST      | \
                                                AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE )
#else
#define AR_MCI_INTERRUPT_RX_MSG_DEFAULT       ( AR_MCI_INTERRUPT_RX_MSG_GPM           | \
                                                AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET  | \
                                                AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING    | \
                                                AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING  | \
                                                AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE )
#endif
#define AR_MCI_INTERRUPT_RX_HW_MSG_MASK       ( AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO     | \
                                                AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL   | \
                                                AR_MCI_INTERRUPT_RX_MSG_LNA_INFO      | \
                                                AR_MCI_INTERRUPT_RX_MSG_CONT_NACK     | \
                                                AR_MCI_INTERRUPT_RX_MSG_CONT_INFO     | \
                                                AR_MCI_INTERRUPT_RX_MSG_CONT_RST )

#define AR_MCI_CPU_INT                  AR_WLAN_COEX_OFFSET(MCI_CPU_INT)

#define AR_MCI_RX_STATUS                AR_WLAN_COEX_OFFSET(MCI_RX_STATUS)
#define AR_MCI_RX_LAST_SCHD_MSG_INDEX   0x00000F00
#define AR_MCI_RX_LAST_SCHD_MSG_INDEX_S 8
#define AR_MCI_RX_REMOTE_SLEEP          0x00001000
#define AR_MCI_RX_REMOTE_SLEEP_S        12
#define AR_MCI_RX_MCI_CLK_REQ           0x00002000
#define AR_MCI_RX_MCI_CLK_REQ_S         13

#define AR_MCI_CONT_STATUS              AR_WLAN_COEX_OFFSET(MCI_CONT_STATUS)
#define AR_MCI_CONT_RSSI_POWER          0x000000FF
#define AR_MCI_CONT_RSSI_POWER_S        0
#define AR_MCI_CONT_RRIORITY            0x0000FF00
#define AR_MCI_CONT_RRIORITY_S          8
#define AR_MCI_CONT_TXRX                0x00010000
#define AR_MCI_CONT_TXRX_S              16

#define AR_MCI_BT_PRI0                  AR_WLAN_COEX_OFFSET(MCI_BT_PRI0) 
#define AR_MCI_BT_PRI1                  AR_WLAN_COEX_OFFSET(MCI_BT_PRI1) 
#define AR_MCI_BT_PRI2                  AR_WLAN_COEX_OFFSET(MCI_BT_PRI2) 
#define AR_MCI_BT_PRI3                  AR_WLAN_COEX_OFFSET(MCI_BT_PRI3) 
#define AR_MCI_BT_PRI                   AR_WLAN_COEX_OFFSET(MCI_BT_PRI) 
#define AR_MCI_WL_FREQ0                 AR_WLAN_COEX_OFFSET(MCI_WL_FREQ0) 
#define AR_MCI_WL_FREQ1                 AR_WLAN_COEX_OFFSET(MCI_WL_FREQ1) 
#define AR_MCI_WL_FREQ2                 AR_WLAN_COEX_OFFSET(MCI_WL_FREQ2) 
#define AR_MCI_GAIN                     AR_WLAN_COEX_OFFSET(MCI_GAIN) 
#define AR_MCI_WBTIMER1                 AR_WLAN_COEX_OFFSET(MCI_WBTIMER1) 
#define AR_MCI_WBTIMER2                 AR_WLAN_COEX_OFFSET(MCI_WBTIMER2) 
#define AR_MCI_WBTIMER3                 AR_WLAN_COEX_OFFSET(MCI_WBTIMER3) 
#define AR_MCI_WBTIMER4                 AR_WLAN_COEX_OFFSET(MCI_WBTIMER4) 
#define AR_MCI_MAXGAIN                  AR_WLAN_COEX_OFFSET(MCI_MAXGAIN) 
#define AR_MCI_HW_SCHD_TBL_CTL          AR_WLAN_COEX_OFFSET(MCI_HW_SCHD_TBL_CTL) 
#define AR_MCI_HW_SCHD_TBL_D0           AR_WLAN_COEX_OFFSET(MCI_HW_SCHD_TBL_D0) 
#define AR_MCI_HW_SCHD_TBL_D1           AR_WLAN_COEX_OFFSET(MCI_HW_SCHD_TBL_D1) 
#define AR_MCI_HW_SCHD_TBL_D2           AR_WLAN_COEX_OFFSET(MCI_HW_SCHD_TBL_D2) 
#define AR_MCI_HW_SCHD_TBL_D3           AR_WLAN_COEX_OFFSET(MCI_HW_SCHD_TBL_D3) 
#define AR_MCI_TX_PAYLOAD0              AR_WLAN_COEX_OFFSET(MCI_TX_PAYLOAD0) 
#define AR_MCI_TX_PAYLOAD1              AR_WLAN_COEX_OFFSET(MCI_TX_PAYLOAD1) 
#define AR_MCI_TX_PAYLOAD2              AR_WLAN_COEX_OFFSET(MCI_TX_PAYLOAD2) 
#define AR_MCI_TX_PAYLOAD3              AR_WLAN_COEX_OFFSET(MCI_TX_PAYLOAD3) 
#define AR_BTCOEX_WBTIMER               AR_WLAN_COEX_OFFSET(BTCOEX_WBTIMER) 

#define AR_BTCOEX_CTRL                  AR_WLAN_COEX_OFFSET(BTCOEX_CTRL)
#define AR_BTCOEX_CTRL_JUPITER_MODE     0x00000001
#define AR_BTCOEX_CTRL_JUPITER_MODE_S   0
#define AR_BTCOEX_CTRL_WBTIMER_EN       0x00000002
#define AR_BTCOEX_CTRL_WBTIMER_EN_S     1
#define AR_BTCOEX_CTRL_MCI_MODE_EN      0x00000004
#define AR_BTCOEX_CTRL_MCI_MODE_EN_S    2
#define AR_BTCOEX_CTRL_LNA_SHARED       0x00000008
#define AR_BTCOEX_CTRL_LNA_SHARED_S     3
#define AR_BTCOEX_CTRL_PA_SHARED        0x00000010
#define AR_BTCOEX_CTRL_PA_SHARED_S      4
#define AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN 0x00000020
#define AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN_S 5
#define AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN    0x00000040
#define AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN_S  6
#define AR_BTCOEX_CTRL_NUM_ANTENNAS     0x00000180
#define AR_BTCOEX_CTRL_NUM_ANTENNAS_S   7
#define AR_BTCOEX_CTRL_RX_CHAIN_MASK    0x00000E00
#define AR_BTCOEX_CTRL_RX_CHAIN_MASK_S  9
#define AR_BTCOEX_CTRL_AGGR_THRESH      0x00007000
#define AR_BTCOEX_CTRL_AGGR_THRESH_S    12
#define AR_BTCOEX_CTRL_1_CHAIN_BCN      0x00080000
#define AR_BTCOEX_CTRL_1_CHAIN_BCN_S    19
#define AR_BTCOEX_CTRL_1_CHAIN_ACK      0x00100000
#define AR_BTCOEX_CTRL_1_CHAIN_ACK_S    20
#define AR_BTCOEX_CTRL_WAIT_BA_MARGIN   0x1FE00000
#define AR_BTCOEX_CTRL_WAIT_BA_MARGIN_S 28
#define AR_BTCOEX_CTRL_REDUCE_TXPWR     0x20000000
#define AR_BTCOEX_CTRL_REDUCE_TXPWR_S   29
#define AR_BTCOEX_CTRL_SPDT_ENABLE_10   0x40000000
#define AR_BTCOEX_CTRL_SPDT_ENABLE_10_S 30
#define AR_BTCOEX_CTRL_SPDT_POLARITY    0x80000000
#define AR_BTCOEX_CTRL_SPDT_POLARITY_S  31

#define AR_BTCOEX_WL_WEIGHTS0           AR_WLAN_COEX_OFFSET(BTCOEX_WL_WEIGHTS0) 
#define AR_BTCOEX_WL_WEIGHTS1           AR_WLAN_COEX_OFFSET(BTCOEX_WL_WEIGHTS1) 
#define AR_BTCOEX_WL_WEIGHTS2           AR_WLAN_COEX_OFFSET(BTCOEX_WL_WEIGHTS2) 
#define AR_BTCOEX_WL_WEIGHTS3           AR_WLAN_COEX_OFFSET(BTCOEX_WL_WEIGHTS3) 
#define AR_BTCOEX_MAX_TXPWR(_x)         (AR_WLAN_COEX_OFFSET(BTCOEX_MAX_TXPWR) + ((_x) << 2))
#define AR_BTCOEX_WL_LNA                AR_WLAN_COEX_OFFSET(BTCOEX_WL_LNA) 
#define AR_BTCOEX_WL_LNA_TIMEOUT                        0x003FFFFF
#define AR_BTCOEX_WL_LNA_TIMEOUT_S                      0

#define AR_BTCOEX_RFGAIN_CTRL           AR_WLAN_COEX_OFFSET(BTCOEX_RFGAIN_CTRL) 

#define AR_BTCOEX_CTRL2                 AR_WLAN_COEX_OFFSET(BTCOEX_CTRL2) 
#define AR_BTCOEX_CTRL2_TXPWR_THRESH    0x0007F800
#define AR_BTCOEX_CTRL2_TXPWR_THRESH_S  11
#define AR_BTCOEX_CTRL2_TX_CHAIN_MASK   0x00380000
#define AR_BTCOEX_CTRL2_TX_CHAIN_MASK_S 19
#define AR_BTCOEX_CTRL2_RX_DEWEIGHT     0x00400000
#define AR_BTCOEX_CTRL2_RX_DEWEIGHT_S   22
#define AR_BTCOEX_CTRL2_GPIO_OBS_SEL    0x00800000
#define AR_BTCOEX_CTRL2_GPIO_OBS_SEL_S  23
#define AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL  0x01000000
#define AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL_S 24
#define AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE     0x02000000
#define AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE_S   25

#define AR_BTCOEX_RC                    AR_WLAN_COEX_OFFSET(BTCOEX_RC) 
#define AR_BTCOEX_MAX_RFGAIN(_x)        AR_WLAN_COEX_OFFSET(BTCOEX_MAX_RFGAIN[_x]) 
#define AR_BTCOEX_DBG                   AR_WLAN_COEX_OFFSET(BTCOEX_DBG) 
#define AR_MCI_LAST_HW_MSG_HDR          AR_WLAN_COEX_OFFSET(MCI_LAST_HW_MSG_HDR) 
#define AR_MCI_LAST_HW_MSG_BDY          AR_WLAN_COEX_OFFSET(MCI_LAST_HW_MSG_BDY) 

#define AR_MCI_SCHD_TABLE_2             AR_WLAN_COEX_OFFSET(MCI_SCHD_TABLE_2)
#define AR_MCI_SCHD_TABLE_2_MEM_BASED   0x00000001
#define AR_MCI_SCHD_TABLE_2_MEM_BASED_S 0
#define AR_MCI_SCHD_TABLE_2_HW_BASED    0x00000002
#define AR_MCI_SCHD_TABLE_2_HW_BASED_S  1

#define AR_BTCOEX_CTRL3                 AR_WLAN_COEX_OFFSET(BTCOEX_CTRL3)
#define AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT   0x00000FFF
#define AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT_S 0

/* QCA9565 */

#define AR_BTCOEX_WL_LNADIV                                0x1a64
#define AR_BTCOEX_WL_LNADIV_PREDICTED_PERIOD               0x00003FFF
#define AR_BTCOEX_WL_LNADIV_PREDICTED_PERIOD_S             0
#define AR_BTCOEX_WL_LNADIV_DPDT_IGNORE_PRIORITY           0x00004000
#define AR_BTCOEX_WL_LNADIV_DPDT_IGNORE_PRIORITY_S         14
#define AR_BTCOEX_WL_LNADIV_FORCE_ON                       0x00008000
#define AR_BTCOEX_WL_LNADIV_FORCE_ON_S                     15
#define AR_BTCOEX_WL_LNADIV_MODE_OPTION                    0x00030000
#define AR_BTCOEX_WL_LNADIV_MODE_OPTION_S                  16
#define AR_BTCOEX_WL_LNADIV_MODE                           0x007c0000
#define AR_BTCOEX_WL_LNADIV_MODE_S                         18
#define AR_BTCOEX_WL_LNADIV_ALLOWED_TX_ANTDIV_WL_TX_REQ    0x00800000
#define AR_BTCOEX_WL_LNADIV_ALLOWED_TX_ANTDIV_WL_TX_REQ_S  23
#define AR_BTCOEX_WL_LNADIV_DISABLE_TX_ANTDIV_ENABLE       0x01000000
#define AR_BTCOEX_WL_LNADIV_DISABLE_TX_ANTDIV_ENABLE_S     24
#define AR_BTCOEX_WL_LNADIV_CONTINUOUS_BT_ACTIVE_PROTECT   0x02000000
#define AR_BTCOEX_WL_LNADIV_CONTINUOUS_BT_ACTIVE_PROTECT_S 25
#define AR_BTCOEX_WL_LNADIV_BT_INACTIVE_THRESHOLD          0xFC000000
#define AR_BTCOEX_WL_LNADIV_BT_INACTIVE_THRESHOLD_S        26

#define AR_MCI_MISC                                     0x1a74
#define AR_MCI_MISC_HW_FIX_EN                           0x00000001
#define AR_MCI_MISC_HW_FIX_EN_S                         0

/******************************************************************************
 * WLAN BT Global Register Map
******************************************************************************/
#define AR_WLAN_BT_GLB_OFFSET(_x)   offsetof(struct wlan_bt_glb_reg_pcie, _x)

/*
 * WLAN BT Global Registers
 */

#define AR_GLB_GPIO_CONTROL                 AR_WLAN_BT_GLB_OFFSET(GLB_GPIO_CONTROL)
#define AR_GLB_WLAN_WOW_STATUS              AR_WLAN_BT_GLB_OFFSET(GLB_WLAN_WOW_STATUS)
#define AR_GLB_WLAN_WOW_ENABLE              AR_WLAN_BT_GLB_OFFSET(GLB_WLAN_WOW_ENABLE)
#define AR_GLB_EMB_CPU_WOW_STATUS           AR_WLAN_BT_GLB_OFFSET(GLB_EMB_CPU_WOW_STATUS)
#define AR_GLB_EMB_CPU_WOW_ENABLE           AR_WLAN_BT_GLB_OFFSET(GLB_EMB_CPU_WOW_ENABLE)
#define AR_GLB_MBOX_CONTROL_STATUS          AR_WLAN_BT_GLB_OFFSET(GLB_MBOX_CONTROL_STATUS)
#define AR_GLB_SW_WOW_CLK_CONTROL           AR_WLAN_BT_GLB_OFFSET(GLB_SW_WOW_CLK_CONTROL)
#define AR_GLB_APB_TIMEOUT                  AR_WLAN_BT_GLB_OFFSET(GLB_APB_TIMEOUT)
#define AR_GLB_OTP_LDO_CONTROL              AR_WLAN_BT_GLB_OFFSET(GLB_OTP_LDO_CONTROL)
#define AR_GLB_OTP_LDO_POWER_GOOD           AR_WLAN_BT_GLB_OFFSET(GLB_OTP_LDO_POWER_GOOD)
#define AR_GLB_OTP_LDO_STATUS               AR_WLAN_BT_GLB_OFFSET(GLB_OTP_LDO_STATUS)
#define AR_GLB_SWREG_DISCONT_MODE           AR_WLAN_BT_GLB_OFFSET(GLB_SWREG_DISCONT_MODE)
#define AR_GLB_BT_GPIO_REMAP_OUT_CONTROL0   AR_WLAN_BT_GLB_OFFSET(GLB_BT_GPIO_REMAP_OUT_CONTROL0)
#define AR_GLB_BT_GPIO_REMAP_OUT_CONTROL1   AR_WLAN_BT_GLB_OFFSET(GLB_BT_GPIO_REMAP_OUT_CONTROL1)
#define AR_GLB_BT_GPIO_REMAP_IN_CONTROL0    AR_WLAN_BT_GLB_OFFSET(GLB_BT_GPIO_REMAP_IN_CONTROL0)
#define AR_GLB_BT_GPIO_REMAP_IN_CONTROL1    AR_WLAN_BT_GLB_OFFSET(GLB_BT_GPIO_REMAP_IN_CONTROL1)
#define AR_GLB_BT_GPIO_REMAP_IN_CONTROL2    AR_WLAN_BT_GLB_OFFSET(GLB_BT_GPIO_REMAP_IN_CONTROL2)
#define AR_GLB_SCRATCH(_ah)             \
    (AR_SREV_APHRODITE(_ah)?          \
        AR_WLAN_BT_GLB_OFFSET(overlay_0x20044.Aphrodite_10.GLB_SCRATCH) : \
        (AR_SREV_JUPITER_20(_ah) ?          \
            AR_WLAN_BT_GLB_OFFSET(overlay_0x20044.Jupiter_20.GLB_SCRATCH) : \
            AR_WLAN_BT_GLB_OFFSET(overlay_0x20044.Jupiter_10.GLB_SCRATCH)))

#define AR_GLB_CONTROL  AR_WLAN_BT_GLB_OFFSET(overlay_0x20044.Jupiter_20.GLB_CONTROL)
#define AR_BTCOEX_CTRL_SPDT_ENABLE          0x00000001
#define AR_BTCOEX_CTRL_SPDT_ENABLE_S        0
#define AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL     0x00000002
#define AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL_S   1
#define AR_BTCOEX_CTRL_USE_LATCHED_BT_ANT   0x00000004
#define AR_BTCOEX_CTRL_USE_LATCHED_BT_ANT_S 2
#define AR_GLB_WLAN_UART_INTF_EN            0x00020000
#define AR_GLB_WLAN_UART_INTF_EN_S          17
#define AR_GLB_DS_JTAG_DISABLE              0x00040000
#define AR_GLB_DS_JTAG_DISABLE_S            18

#define AR_GLB_STATUS   AR_WLAN_BT_GLB_OFFSET(overlay_0x20044.Jupiter_20.GLB_STATUS)

/*
 * MAC Version and Revision
 */

#define AR_SREV_VERSION_OSPREY 0x1C0
#define AR_SREV_VERSION_AR9580 0x1C0
#define AR_SREV_VERSION_JUPITER 0x280
#define AR_SREV_VERSION_HORNET 0x200
#define AR_SREV_VERSION_WASP   0x300 /* XXX: Check Wasp version number */
#define AR_SREV_VERSION_SCORPION 0x400
#define AR_SREV_VERSION_POSEIDON 0x240
#define AR_SREV_VERSION_HONEYBEE 0x500
#define AR_SREV_VERSION_APHRODITE 0x2C0

#define AR_SREV_REVISION_OSPREY_10            0      /* Osprey 1.0 */
#define AR_SREV_REVISION_OSPREY_20            2      /* Osprey 2.0/2.1 */
#define AR_SREV_REVISION_OSPREY_22            3      /* Osprey 2.2 */
#define AR_SREV_REVISION_AR9580_10            4      /* AR9580/Peacock 1.0 */

#define AR_SREV_REVISION_HORNET_10            0      /* Hornet 1.0 */
#define AR_SREV_REVISION_HORNET_11            1      /* Hornet 1.1 */
#define AR_SREV_REVISION_HORNET_12            2      /* Hornet 1.2 */
#define AR_SREV_REVISION_HORNET_11_MASK       0xf    /* Hornet 1.1 revision mask */

#define AR_SREV_REVISION_POSEIDON_10          0      /* Poseidon 1.0 */
#define AR_SREV_REVISION_POSEIDON_11          1      /* Poseidon 1.1 */

#define AR_SREV_REVISION_WASP_10              0      /* Wasp 1.0 */
#define AR_SREV_REVISION_WASP_11              1      /* Wasp 1.1 */
#define AR_SREV_REVISION_WASP_12              2      /* Wasp 1.2 */
#define AR_SREV_REVISION_WASP_13              3      /* Wasp 1.3 */
#define AR_SREV_REVISION_WASP_MASK            0xf    /* Wasp revision mask */
#define AR_SREV_REVISION_WASP_MINOR_MINOR_MASK  0x10000 /* Wasp minor minor revision mask */
#define AR_SREV_REVISION_WASP_MINOR_MINOR_SHIFT 16      /* Wasp minor minor revision shift */

#define AR_SREV_REVISION_JUPITER_10           0      /* Jupiter 1.0 */
#define AR_SREV_REVISION_JUPITER_20           2      /* Jupiter 2.0 */
#define AR_SREV_REVISION_JUPITER_21           3      /* Jupiter 2.1 */

#define AR_SREV_REVISION_HONEYBEE_10          0      /* Honeybee 1.0 */
#define AR_SREV_REVISION_HONEYBEE_11          1      /* Honeybee 1.1 */
#define AR_SREV_REVISION_HONEYBEE_MASK        0xf    /* Honeybee revision mask */

#define AR_SREV_REVISION_APHRODITE_10         0      /* Aphrodite 1.0 */

#if defined(AH_SUPPORT_OSPREY)
#define AR_SREV_OSPREY(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_OSPREY))

#define AR_SREV_OSPREY_22(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_OSPREY) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_OSPREY_22))
#else
#define AR_SREV_OSPREY(_ah)                                        0
#define AR_SREV_OSPREY_10(_ah)                                     0
#define AR_SREV_OSPREY_20(_ah)                                     0
#define AR_SREV_OSPREY_22(_ah)                                     0
#define AR_SREV_OSPREY_20_OR_LATER(_ah)                            0
#define AR_SREV_OSPREY_22_OR_LATER(_ah)                            0
#endif /* #if defined(AH_SUPPORT_OSPREY) */

#define AR_SREV_AR9580(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_AR9580) && \
     (AH_PRIVATE((_ah))->ah_macRev >= AR_SREV_REVISION_AR9580_10))

#define AR_SREV_AR9580_10(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_AR9580) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_AR9580_10))

/* NOTE: When adding chips newer than Peacock, add chip check here.  */
#define AR_SREV_AR9580_10_OR_LATER(_ah) \
    (AR_SREV_AR9580(_ah) || AR_SREV_SCORPION(_ah) || AR_SREV_HONEYBEE(_ah))

#define AR_SREV_JUPITER(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_JUPITER))

#define AR_SREV_JUPITER_10(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_JUPITER) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_JUPITER_10))

#define AR_SREV_JUPITER_20(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_JUPITER) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_JUPITER_20))

#define AR_SREV_JUPITER_21(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_JUPITER) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_JUPITER_21))

#define AR_SREV_JUPITER_20_OR_LATER(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_JUPITER) && \
     (AH_PRIVATE((_ah))->ah_macRev >= AR_SREV_REVISION_JUPITER_20))

#define AR_SREV_JUPITER_21_OR_LATER(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_JUPITER) && \
     (AH_PRIVATE((_ah))->ah_macRev >= AR_SREV_REVISION_JUPITER_21))

#define AR_SREV_APHRODITE(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_APHRODITE))

#define AR_SREV_APHRODITE_10(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_APHRODITE) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_APHRODITE_10))

#if defined(AH_SUPPORT_HORNET)
#define AR_SREV_HORNET_10(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_HORNET) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_HORNET_10))

#define AR_SREV_HORNET_11(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_HORNET) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_HORNET_11))

#define AR_SREV_HORNET_12(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_HORNET) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_HORNET_12))

#define AR_SREV_HORNET(_ah) \
    ( AR_SREV_HORNET_10(_ah) || AR_SREV_HORNET_11(_ah) || AR_SREV_HORNET_12(_ah) )
#else
#define AR_SREV_HORNET_10(_ah)                                    0
#define AR_SREV_HORNET_11(_ah)                                    0
#define AR_SREV_HORNET_12(_ah)                                    0
#define AR_SREV_HORNET(_ah)                                       0
#endif /* #if defined(AH_SUPPORT_HORNET) */

#if defined(AH_SUPPORT_WASP)
#define AR_SREV_WASP(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_WASP))
#else
#define AR_SREV_WASP(_ah)                                         0
#endif /* #if defined(AH_SUPPORT_WASP) */

#if defined(AH_SUPPORT_HONEYBEE)
#define AR_SREV_HONEYBEE(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_HONEYBEE))
#define AR_SREV_HONEYBEE_10(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_HONEYBEE) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_HONEYBEE_10))
#define AR_SREV_HONEYBEE_11(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_HONEYBEE) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_HONEYBEE_11))
#else
#define AR_SREV_HONEYBEE(_ah)                                         0
#define AR_SREV_HONEYBEE_10(_ah) 0
#define AR_SREV_HONEYBEE_11(_ah) 0
#endif /* #if defined(AH_SUPPORT_HONEYBEE) */

#define AR_SREV_WASP_10(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_WASP) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_WASP_10))

#define AR_SREV_WASP_11(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_WASP) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_WASP_11))

#define AR_SREV_WASP_12(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_WASP) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_WASP_12))

#if defined(AH_SUPPORT_SCORPION)
#define AR_SREV_SCORPION(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_SCORPION))
#else
#define AR_SREV_SCORPION(_ah) 0
#endif /* #if defined(AH_SUPPORT_SCORPION) */

#if defined(AH_SUPPORT_POSEIDON)
#define AR_SREV_POSEIDON(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_POSEIDON))

#define AR_SREV_POSEIDON_10(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_POSEIDON) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_POSEIDON_10))

#define AR_SREV_POSEIDON_11(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_POSEIDON) && \
     (AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_POSEIDON_11))
#else
#define AR_SREV_POSEIDON(_ah)                                    0
#define AR_SREV_POSEIDON_10(_ah)                                 0
#define AR_SREV_POSEIDON_11(_ah)                                 0
#endif /* #if defined(AH_SUPPORT_POSEIDON) */

#define AR_SREV_POSEIDON_11_OR_LATER(_ah) \
    ((AH_PRIVATE((_ah))->ah_macVersion == AR_SREV_VERSION_POSEIDON) && \
     (AH_PRIVATE((_ah))->ah_macRev >= AR_SREV_REVISION_POSEIDON_11))

#define AR_SREV_POSEIDON_OR_LATER(_ah) \
    (AH_PRIVATE((_ah))->ah_macVersion >= AR_SREV_VERSION_POSEIDON)
#define AR_SREV_SOC(_ah) (AR_SREV_HORNET(_ah) || AR_SREV_POSEIDON(_ah) || AR_SREV_WASP(_ah) || AR_SREV_HONEYBEE(_ah))
/*
* Mask used to construct AAD for CCMP-AES
* Cisco spec defined bits 0-3 as mask 
* IEEE802.11w defined as bit 4.
*/
#define AR_MFP_QOS_MASK_IEEE      0x10 
#define AR_MFP_QOS_MASK_CISCO     0xf

/*
* frame control field mask:
* 0 0 0 0 0 0 0 0
* | | | | | | | | _ Order            bit
* | | | | | | | _ _ Protected Frame  bit
* | | | | | | _ _ _ More data        bit
* | | | | | _ _ _ _ Power management bit
* | | | | _ _ _ _ _ Retry            bit
* | | | _ _ _ _ _ _ More fragments   bit
* | | _ _ _ _ _ _ _ FromDS           bit
* | _ _ _ _ _ _ _ _ ToDS             bit
*/
#define AR_AES_MUTE_MASK1_FC_MGMT_MFP 0xC7FF
#endif
