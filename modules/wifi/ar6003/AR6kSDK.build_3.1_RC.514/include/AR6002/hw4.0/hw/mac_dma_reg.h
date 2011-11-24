//
// Copyright (c) 2002-2009 Atheros Communications Inc.
// All rights reserved.
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
//

/*****************************************************************************/
/* AR6003 WLAN MAC DMA register definitions                                  */
/*****************************************************************************/

#ifndef _AR6000_DMAREG_H_
#define _AR6000_DMAREG_H_

/*
 * Definitions for the Atheros AR6003 chipset.
 */

/* DMA Control and Interrupt Registers */
#define MAC_DMA_CR_ADDRESS                       0x00000008 /* MAC control register */
#define MAC_DMA_CR_RXE_MASK                      0x00000004 /* Receive enable */
#define MAC_DMA_CR_RXD_MASK                      0x00000020 /* Receive disable */
#define MAC_DMA_CR_SWI_MASK                      0x00000040 /* One-shot software interrupt */

#define MAC_DMA_RXDP_ADDRESS                     0x0000000C /* MAC receive queue descriptor pointer */

#define MAC_DMA_CFG_ADDRESS                      0x00000014 /* MAC configuration and status register */
#define MAC_DMA_CFG_SWTD_MASK                    0x00000001 /* byteswap tx descriptor words */
#define MAC_DMA_CFG_SWTB_MASK                    0x00000002 /* byteswap tx data buffer words */
#define MAC_DMA_CFG_SWRD_MASK                    0x00000004 /* byteswap rx descriptor words */
#define MAC_DMA_CFG_SWRB_MASK                    0x00000008 /* byteswap rx data buffer words */
#define MAC_DMA_CFG_SWRG_MASK                    0x00000010 /* byteswap register access data words */
#define MAC_DMA_CFG_AP_ADHOC_INDICATION_MASK     0x00000020 /* AP/adhoc indication (0-AP, 1-Adhoc) */
#define MAC_DMA_CFG_PHOK_MASK                    0x00000100 /* PHY OK status */
#define MAC_DMA_CFG_CLK_GATE_DIS_MASK            0x00000400 /* Clock gating disable  */

#define MAC_DMA_MIRT_ADDRESS                     0x00000020 /* Maximum rate threshold register */
#define MAC_DMA_MIRT_THRESH_MASK                 0x0000FFFF 

#define MAC_DMA_IER_ADDRESS                      0x00000024  /* MAC Interrupt enable register */
#define MAC_DMA_IER_ENABLE_MASK                  0x00000001 /* Global interrupt enable */
#define MAC_DMA_IER_DISABLE_MASK                 0x00000000 /* Global interrupt disable */

#define MAC_DMA_TIMT_ADDRESS                     0x00000028 /* Transmit Interrupt Mitigation Threshold */
#define MAC_DMA_TIMT_LAST_PACKER_THRESH_MASK     0x0000FFFF /* Last packet threshold mask */
#define MAC_DMA_TIMT_FIRST_PACKER_THRESH_MASK    0xFFFF0000 /* First packet threshold mask */

#define MAC_DMA_RIMT_ADDRESS                     0x0000002C /* Receive Interrupt Mitigation Threshold */
#define MAC_DMA_RIMT_LAST_PACKER_THRESH_MASK     0x0000FFFF /* Last packet threshold mask */
#define MAC_DMA_RIMT_FIRST_PACKER_THRESH_MASK    0xFFFF0000 /* First packet threshold mask */

#define MAC_DMA_TXCFG_ADDRESS                    0x00000030  /* MAC tx DMA size config register */
#define MAC_DMA_FTRIG_MASK                       0x000003F0 /* Mask for Frame trigger level */
#define MAC_DMA_FTRIG_LSB                        4          /* Shift for Frame trigger level */
#define MAC_DMA_FTRIG_IMMED                      0x00000000 /* bytes in PCU TX FIFO before air */
#define MAC_DMA_FTRIG_64B                        0x00000010 /* default */
#define MAC_DMA_FTRIG_128B                       0x00000020
#define MAC_DMA_FTRIG_192B                       0x00000030
#define MAC_DMA_FTRIG_256B                       0x00000040 /* 5 bits total */
#define MAC_DMA_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY_MASK 0x00000800

#define MAC_DMA_RXCFG_ADDRESS                     0x00000034  /* MAC rx DMA size config register */
#define MAC_DMA_RXCFG_ZLFDMA_MASK                 0x00000010 /* Enable DMA of zero-length frame */
#define MAC_DMA_RXCFG_DMASIZE_4B                  0x00000000 /* DMA size 4 bytes (TXCFG + RXCFG) */
#define MAC_DMA_RXCFG_DMASIZE_8B                  0x00000001 /* DMA size 8 bytes */
#define MAC_DMA_RXCFG_DMASIZE_16B                 0x00000002 /* DMA size 16 bytes */
#define MAC_DMA_RXCFG_DMASIZE_32B                 0x00000003 /* DMA size 32 bytes */
#define MAC_DMA_RXCFG_DMASIZE_64B                 0x00000004 /* DMA size 64 bytes */
#define MAC_DMA_RXCFG_DMASIZE_128B                0x00000005 /* DMA size 128 bytes */
#define MAC_DMA_RXCFG_DMASIZE_256B                0x00000006 /* DMA size 256 bytes */
#define MAC_DMA_RXCFG_DMASIZE_512B                0x00000007 /* DMA size 512 bytes */

#define MAC_DMA_MIBC_ADDRESS                      0x00000040  /* MAC MIB control register */
#define MAC_DMA_MIBC_COW_MASK                     0x00000001 /* counter overflow warning */
#define MAC_DMA_MIBC_FMC_MASK                     0x00000002 /* freeze MIB counters */
#define MAC_DMA_MIBC_CMC_MASK                     0x00000004 /* clear MIB counters */
#define MAC_DMA_MIBC_MCS_MASK                     0x00000008 /* MIB counter strobe, increment all */

#define MAC_DMA_TOPS_ADDRESS                      0x00000044  /* MAC timeout prescale count */
#define MAC_DMA_TOPS_MASK                         0x0000FFFF /* Mask for timeout prescale */

#define MAC_DMA_RXNPTO_ADDRESS                    0x00000048  /* MAC no frame received timeout */
#define MAC_DMA_RXNPTO_MASK                       0x000003FF /* Mask for no frame received timeout */

#define MAC_DMA_TXNPTO_ADDRESS                    0x0000004C  /* MAC no frame trasmitted timeout */
#define MAC_DMA_TXNPTO_MASK                       0x000003FF /* Mask for no frame transmitted timeout */
#define MAC_DMA_TXNPTO_QCU_MASK                   0x000FFC00 /* Mask indicating the set of QCUs */
                                                       /* for which frame completions will cause */
                                                       /* a reset of the no frame xmit'd timeout */

#define MAC_DMA_RPGTO_ADDRESS                     0x00000050  /* MAC receive frame gap timeout */
#define MAC_DMA_RPGTO_MASK                        0x000003FF /* Mask for receive frame gap timeout */

#define MAC_DMA_RPCNT_ADDRESS                     0x00000054  /* MAC receive frame count limit */
#define MAC_DMA_RPCNT_MASK                        0x0000001F /* Mask for receive frame count limit */

#define MAC_DMA_MACMISC_ADDRESS                   0x00000058  /* MAC miscellaneous control/status register */
#define MAC_DMA_MACMISC_DMA_OBS_MASK              0x000001E0 /* Mask for DMA observation bus mux select */
#define MAC_DMA_MACMISC_DMA_OBS_LSB               5          /* Shift for DMA observation bus mux select */
#define MAC_DMA_MACMISC_MISC_OBS                  0x00000E00 /* Mask for MISC observation bus mux select */
#define MAC_DMA_MACMISC_MISC_OBS_LSB              9          /* Shift for MISC observation bus mux select */
#define MAC_DMA_MACMISC_MAC_OBS_BUS_LSB           0x00007000 /* Mask for MAC observation bus mux select (lsb) */
#define MAC_DMA_MACMISC_MAC_OBS_BUS_LSB_LSB       12         /* Shift for MAC observation bus mux select (lsb) */
#define MAC_DMA_MACMISC_MAC_OBS_BUS_MSB           0x00038000 /* Mask for MAC observation bus mux select (msb) */
#define MAC_DMA_MACMISC_MAC_OBS_BUS_MSB_LSB       15         /* Shift for MAC observation bus mux select (msb) */


#define MAC_DMA_ISR_ADDRESS                        0x00000080  /* MAC Primary interrupt status register */
/*
 * Interrupt Status Registers
 *
 * Only the bits in the ISR_P register and the IMR_P registers
 * control whether the MAC's INTA# output is asserted.  The bits in
 * the secondary interrupt status/mask registers control what bits
 * are set in the primary interrupt status register; however the
 * IMR_S* registers DO NOT determine whether INTA# is asserted.
 * That is INTA# is asserted only when the logical AND of ISR_P
 * and IMR_P is non-zero.  The secondary interrupt mask/status
 * registers affect what bits are set in ISR_P but they do not
 * directly affect whether INTA# is asserted.
 */
#define MAC_DMA_ISR_RXOK_MASK                    0x00000001 /* At least one frame received sans errors */
#define MAC_DMA_ISR_RXDESC_MASK                  0x00000002 /* Receive interrupt request */
#define MAC_DMA_ISR_RXERR_MASK                   0x00000004 /* Receive error interrupt */
#define MAC_DMA_ISR_RXNOPKT_MASK                 0x00000008 /* No frame received within timeout clock */
#define MAC_DMA_ISR_RXEOL_MASK                   0x00000010 /* Received descriptor empty interrupt */
#define MAC_DMA_ISR_RXORN_MASK                   0x00000020 /* Receive FIFO overrun interrupt */
#define MAC_DMA_ISR_TXOK_MASK                    0x00000040 /* Transmit okay interrupt */
#define MAC_DMA_ISR_TXDESC_MASK                  0x00000080 /* Transmit interrupt request */
#define MAC_DMA_ISR_TXERR_MASK                   0x00000100 /* Transmit error interrupt */
#define MAC_DMA_ISR_TXNOPKT_MASK                 0x00000200 /* No frame transmitted interrupt */
#define MAC_DMA_ISR_TXEOL_MASK                   0x00000400 /* Transmit descriptor empty interrupt */
#define MAC_DMA_ISR_TXURN_MASK                   0x00000800 /* Transmit FIFO underrun interrupt */
#define MAC_DMA_ISR_MIB_MASK                     0x00001000 /* MIB interrupt - see MIBC */
#define MAC_DMA_ISR_SWI_MASK                     0x00002000 /* Software interrupt */
#define MAC_DMA_ISR_RXPHY_MASK                   0x00004000 /* PHY receive error interrupt */
#define MAC_DMA_ISR_RXKCM_MASK                   0x00008000 /* Key-cache miss interrupt */
#define MAC_DMA_ISR_BRSSI_HI_MASK                0x00010000 /* Beacon rssi high threshold interrupt */
#define MAC_DMA_ISR_BRSSI_LO_MASK                0x00020000 /* Beacon threshold interrupt */
#define MAC_DMA_ISR_BMISS_MASK                   0x00040000 /* Beacon missed interrupt */
#define MAC_DMA_ISR_TXMINTR_MASK                 0x00080000 /* Maximum transmit interrupt rate */
#define MAC_DMA_ISR_BNR_MASK                     0x00100000 /* Beacon not ready interrupt */
#define MAC_DMA_ISR_HIUERR_MASK                  0x00200000 /* An unexpected bus error has occurred */
#define MAC_DMA_ISR_BCNMISC_MASK                 0x00800000 /* 'or' of TIM, CABEND, DTIMSYNC, BCNTO */
#define MAC_DMA_ISR_RXMINTR_MASK                 0x01000000 /* Maximum receive interrupt rate */
#define MAC_DMA_ISR_QCBROVF_MASK                 0x02000000 /* QCU CBR overflow interrupt */
#define MAC_DMA_ISR_QCBRURN_MASK                 0x04000000 /* QCU CBR underrun interrupt */
#define MAC_DMA_ISR_QTRIG_MASK                   0x08000000 /* QCU scheduling trigger interrupt */
#define MAC_DMA_ISR_TIMER_MASK                   0x10000000 /* GENTMR interrupt */
#define MAC_DMA_ISR_HCFTO_MASK                   0x20000000 /* HCFTO interrupt   */
#define MAC_DMA_ISR_TXINTM_MASK                  0x40000000 /* Transmit completion mitigation interrupt */
#define MAC_DMA_ISR_RXINTM_MASK                  0x80000000 /* Receive completion mitigation interrupt */

#define MAC_DMA_ISR_S0_ADDRESS                   0x00000084  /* MAC Secondary interrupt status register 0 */
#define MAC_DMA_ISR_S0_QCU_TXOK_MASK             0x000003FF /* Mask for TXOK (QCU 0-9) */
#define MAC_DMA_ISR_S0_QCU_TXOK_LSB              0
#define MAC_DMA_ISR_S0_QCU_TXDESC_MASK           0x03FF0000 /* Mask for TXDESC (QCU 0-9) */
#define MAC_DMA_ISR_S0_QCU_TXDESC_LSB            16

#define MAC_DMA_ISR_S1_ADDRESS                   0x00000088  /* MAC Secondary interrupt status register 1 */
#define MAC_DMA_ISR_S1_QCU_TXERR_MASK            0x000003FF /* Mask for TXERR (QCU 0-9) */
#define MAC_DMA_ISR_S1_QCU_TXERR_LSB             0
#define MAC_DMA_ISR_S1_QCU_TXEOL_MASK            0x03FF0000 /* Mask for TXEOL (QCU 0-9) */
#define MAC_DMA_ISR_S1_QCU_TXEOL_LSB             16

#define MAC_DMA_ISR_S2_ADDRESS                      0x0000008c  /* MAC Secondary interrupt status register 2 */
#define MAC_DMA_ISR_S2_QCU_TXURN_MASK               0x000003FF /* Mask for TXURN (QCU 0-9) */
#define MAC_DMA_ISR_S2_QCU_TXURN_LSB                0 /* Shift for TXURN (QCU 0-9) */
#define MAC_DMA_ISR_S2_RX_INT_MASK			        0x00000800
#define MAC_DMA_ISR_S2_WL_STOMPED_MASK		        0x00001000
#define MAC_DMA_ISR_S2_RX_PTR_BAD_MASK		        0x00002000
#define MAC_DMA_ISR_S2_BT_LOW_PRIORITY_RISING_MASK  0x00004000
#define MAC_DMA_ISR_S2_BT_LOW_PRIORITY_FALLING_MASK	0x00008000
#define MAC_DMA_ISR_S2_BB_PANIC_IRQ_MASK            0x00010000
#define MAC_DMA_ISR_S2_BT_STOMPED_MASK		        0x00020000
#define MAC_DMA_ISR_S2_BT_ACTIVE_RISING_MASK	    0x00040000
#define MAC_DMA_ISR_S2_BT_ACTIVE_FALLING_MASK	    0x00080000
#define MAC_DMA_ISR_S2_BT_PRIORITY_RISING_MASK	    0x00100000
#define MAC_DMA_ISR_S2_BT_PRIORITY_FALLING_MASK	    0x00200000
#define MAC_DMA_ISR_S2_CST_MASK			            0x00400000
#define MAC_DMA_ISR_S2_GTT_MASK			            0x00800000
#define MAC_DMA_ISR_S2_TIM_MASK                     0x01000000 /* TIM */
#define MAC_DMA_ISR_S2_CABEND_MASK                  0x02000000 /* CABEND */
#define MAC_DMA_ISR_S2_DTIMSYNC_MASK                0x04000000 /* DTIMSYNC */
#define MAC_DMA_ISR_S2_BCNTO_MASK                   0x08000000 /* BCNTO */
#define MAC_DMA_ISR_S2_CABTO_MASK                   0x10000000 /* CABTO */
#define MAC_DMA_ISR_S2_DTIM_MASK                    0x20000000 /* DTIM */
#define MAC_DMA_ISR_S2_TSFOOR_MASK                  0x40000000 /* TSFOOR */

#define MAC_DMA_ISR_S3_ADDRESS                   0x00000090  /* MAC Secondary interrupt status register 3 */
#define MAC_DMA_ISR_S3_QCU_QCBROVF_MASK          0x000003FF /* Mask for QCBROVF (QCU 0-9) */
#define MAC_DMA_ISR_S3_QCU_QCBRURN_MASK          0x03FF0000 /* Mask for QCBRURN (QCU 0-9) */

#define MAC_DMA_ISR_S4_ADDRESS                   0x00000094  /* MAC Secondary interrupt status register 4 */
#define MAC_DMA_ISR_S4_QCU_QTRIG_MASK            0x000003FF /* Mask for QTRIG (QCU 0-9) */

#define MAC_DMA_ISR_S5_ADDRESS                   0x00000098  /* MAC Secondary interrupt status register 5 */
#define MAC_DMA_ISR_S5_TBTT_TIMER_TRIGGER_MASK   0x00000001
#define MAC_DMA_ISR_S5_DBA_TIMER_TRIGGER_MASK    0x00000002
#define MAC_DMA_ISR_S5_SBA_TIMER_TRIGGER_MASK    0x00000004
#define MAC_DMA_ISR_S5_HCF_TIMER_TRIGGER_MASK    0x00000008
#define MAC_DMA_ISR_S5_TIM_TIMER_TRIGGER_MASK    0x00000010
#define MAC_DMA_ISR_S5_DTIM_TIMER_TRIGGER_MASK   0x00000020
#define MAC_DMA_ISR_S5_QUIET_TIMER_TRIGGER_MASK  0x00000040
#define MAC_DMA_ISR_S5_NDP_TIMER_TRIGGER_MASK    0x00000080
#define MAC_DMA_ISR_S5_GENERIC_TIMER2_TRIGGER_MASK 0x0000FF00
#define MAC_DMA_ISR_S5_GENERIC_TIMER2_TRIGGER_LSB 8 
#define MAC_DMA_ISR_S5_GENERIC_TIMER2_TRIGGER(_i) (0x00000100 << (_i))
#define MAC_DMA_ISR_S5_TIMER_OVERFLOW_MASK       0x00010000
#define MAC_DMA_ISR_S5_DBA_TIMER_THRESHOLD_MASK  0x00020000
#define MAC_DMA_ISR_S5_SBA_TIMER_THRESHOLD_MASK  0x00040000
#define MAC_DMA_ISR_S5_HCF_TIMER_THRESHOLD_MASK  0x00080000
#define MAC_DMA_ISR_S5_TIM_TIMER_THRESHOLD_MASK  0x00100000
#define MAC_DMA_ISR_S5_DTIM_TIMER_THRESHOLD_MASK  0x00200000
#define MAC_DMA_ISR_S5_QUIET_TIMER_THRESHOLD_MASK 0x00400000
#define MAC_DMA_ISR_S5_NDP_TIMER_THRESHOLD_MASK   0x00800000
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_THRESHOLD_MASK 0xFF000000
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_THRESHOLD_LSB  24 
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_THRESHOLD(_i) (0x01000000 << (_i))

#define MAC_DMA_IMR_ADDRESS                      0x000000A0  /* MAC Primary interrupt mask register */
/*
 * Interrupt Mask Registers
 *
 * Only the bits in the IMR control whether the MAC's INTA#
 * output will be asserted.  The bits in the secondary interrupt
 * mask registers control what bits get set in the primary
 * interrupt status register; however the IMR_S* registers
 * DO NOT determine whether INTA# is asserted.
 */
#define MAC_DMA_IMR_RXOK_MASK                    0x00000001 /* At least one frame received sans errors */
#define MAC_DMA_IMR_RXDESC_MASK                  0x00000002 /* Receive interrupt request */
#define MAC_DMA_IMR_RXERR_MASK                   0x00000004 /* Receive error interrupt */
#define MAC_DMA_IMR_RXNOPKT_MASK                 0x00000008 /* No frame received within timeout clock */
#define MAC_DMA_IMR_RXEOL_MASK                   0x00000010 /* Received descriptor empty interrupt */
#define MAC_DMA_IMR_RXORN_MASK                   0x00000020 /* Receive FIFO overrun interrupt */
#define MAC_DMA_IMR_TXOK_MASK                    0x00000040 /* Transmit okay interrupt */
#define MAC_DMA_IMR_TXDESC_MASK                  0x00000080 /* Transmit interrupt request */
#define MAC_DMA_IMR_TXERR_MASK                   0x00000100 /* Transmit error interrupt */
#define MAC_DMA_IMR_TXNOPKT_MASK                 0x00000200 /* No frame transmitted interrupt */
#define MAC_DMA_IMR_TXEOL_MASK                   0x00000400 /* Transmit descriptor empty interrupt */
#define MAC_DMA_IMR_TXURN_MASK                   0x00000800 /* Transmit FIFO underrun interrupt */
#define MAC_DMA_IMR_MIB_MASK                     0x00001000 /* MIB interrupt - see MIBC */
#define MAC_DMA_IMR_SWI_MASK                     0x00002000 /* Software interrupt */
#define MAC_DMA_IMR_RXPHY_MASK                   0x00004000 /* PHY receive error interrupt */
#define MAC_DMA_IMR_RXKCM_MASK                   0x00008000 /* Key-cache miss interrupt */
#define MAC_DMA_IMR_BRSSI_HI_MASK                0x00010000 /* Beacon rssi hi threshold interrupt */
#define MAC_DMA_IMR_BRSSI_LO_MASK                0x00020000 /* Beacon rssi lo threshold interrupt */
#define MAC_DMA_IMR_BMISS_MASK                   0x00040000 /* Beacon missed interrupt */
#define MAC_DMA_IMR_TXMINTR_MASK                 0x00080000 /* Maximum transmit interrupt rate */
#define MAC_DMA_IMR_BNR_MASK                     0x00100000 /* BNR interrupt */
#define MAC_DMA_IMR_HIUERR_MASK                  0x00200000 /* An unexpected bus error has occurred */
#define MAC_DMA_IMR_BCNMISC_MASK                 0x00800000 /* Beacon Misc */
#define MAC_DMA_IMR_RXMINTR_MASK                 0x01000000 /* Maximum receive interrupt rate */
#define MAC_DMA_IMR_QCBROVF_MASK                 0x02000000 /* QCU CBR overflow interrupt */
#define MAC_DMA_IMR_QCBRURN_MASK                 0x04000000 /* QCU CBR underrun interrupt */
#define MAC_DMA_IMR_QTRIG_MASK                   0x08000000 /* QCU scheduling trigger interrupt */
#define MAC_DMA_IMR_TIMER_MASK                   0x10000000 /* GENTMR interrupt */
#define MAC_DMA_IMR_HCFTO_MASK                   0x20000000 /* HCFTO interrupt*/
#define MAC_DMA_IMR_TXINTM_MASK                  0x40000000 /* Transmit completion mitigation interrupt */
#define MAC_DMA_IMR_RXINTM_MASK                  0x80000000 /* Receive completion mitigation interrupt */

#define MAC_DMA_IMR_S0_ADDRESS                   0x000000A4  /* MAC Secondary interrupt mask register 0 */
#define MAC_DMA_IMR_S0_QCU_TXOK_MASK             0x000003FF /* TXOK (QCU 0-9) */
#define MAC_DMA_IMR_S0_QCU_TXOK_LSB              0
#define MAC_DMA_IMR_S0_QCU_TXDESC_MASK           0x03FF0000 /* TXDESC (QCU 0-9) */
#define MAC_DMA_IMR_S0_QCU_TXDESC_LSB            16

#define MAC_DMA_IMR_S1_ADDRESS                   0x000000A8  /* MAC Secondary interrupt mask register 1 */
#define MAC_DMA_IMR_S1_QCU_TXERR_MASK            0x000003FF /* TXERR (QCU 0-9) */
#define MAC_DMA_IMR_S1_QCU_TXERR_LSB             0
#define MAC_DMA_IMR_S1_QCU_TXEOL_MASK            0x03FF0000 /* TXEOL (QCU 0-9) */
#define MAC_DMA_IMR_S1_QCU_TXEOL_LSB             16

#define MAC_DMA_IMR_S2_ADDRESS                      0x000000AC  /* MAC Secondary interrupt mask register 2 */
#define MAC_DMA_IMR_S2_QCU_TXURN_MASK               0x000003FF /* Mask for TXURN (QCU 0-9) */
#define MAC_DMA_IMR_S2_QCU_TXURN_LSB                0
#define MAC_DMA_IMR_S2_RX_INT_MASK			        0x00000800
#define MAC_DMA_IMR_S2_WL_STOMPED_MASK		        0x00001000
#define MAC_DMA_IMR_S2_RX_PTR_BAD_MASK		        0x00002000
#define MAC_DMA_IMR_S2_BT_LOW_PRIORITY_RISING_MASK  0x00004000
#define MAC_DMA_IMR_S2_BT_LOW_PRIORITY_FALLING_MASK	0x00008000
#define MAC_DMA_IMR_S2_BB_PANIC_IRQ_MASK            0x00010000
#define MAC_DMA_IMR_S2_BT_STOMPED_MASK		        0x00020000
#define MAC_DMA_IMR_S2_BT_ACTIVE_RISING_MASK	    0x00040000
#define MAC_DMA_IMR_S2_BT_ACTIVE_FALLING_MASK	    0x00080000
#define MAC_DMA_IMR_S2_BT_PRIORITY_RISING_MASK	    0x00100000
#define MAC_DMA_IMR_S2_BT_PRIORITY_FALLING_MASK	    0x00200000
#define MAC_DMA_IMR_S2_CST_MASK			            0x00400000
#define MAC_DMA_IMR_S2_GTT_MASK			            0x00800000
#define MAC_DMA_IMR_S2_TIM_MASK                     0x01000000 /* TIM */
#define MAC_DMA_IMR_S2_CABEND_MASK                  0x02000000 /* CABEND */
#define MAC_DMA_IMR_S2_DTIMSYNC_MASK                0x04000000 /* DTIMSYNC */
#define MAC_DMA_IMR_S2_BCNTO_MASK                   0x08000000 /* BCNTO */
#define MAC_DMA_IMR_S2_CABTO_MASK                   0x10000000 /* CABTO */
#define MAC_DMA_IMR_S2_DTIM_MASK                    0x20000000 /* DTIM */
#define MAC_DMA_IMR_S2_TSFOOR_MASK                  0x40000000 /* TSFOOR */

#define MAC_DMA_IMR_S3_ADDRESS                   0x000000B0  /* MAC Secondary interrupt mask register 3 */
#define MAC_DMA_IMR_S3_QCU_QCBROVF_MASK          0x000003FF /* Mask for QCBROVF (QCU 0-9) */
#define MAC_DMA_IMR_S3_QCU_QCBRURN_MASK          0x03FF0000 /* Mask for QCBRURN (QCU 0-9) */
#define MAC_DMA_IMR_S3_QCU_QCBRURN_LSB           16 

#define MAC_DMA_IMR_S4_ADDRESS                   0x000000B4  /* MAC Secondary interrupt mask register 4 */
#define MAC_DMA_IMR_S4_QCU_QTRIG_MASK            0x000003FF /* Mask for QTRIG (QCU 0-9) */

#define MAC_DMA_IMR_S5_ADDRESS                   0x000000B8  /* MAC Secondary interrupt mask register 5 */
#define MAC_DMA_IMR_S5_TBTT_TIMER_TRIGGER_MASK   0x00000001
#define MAC_DMA_IMR_S5_DBA_TIMER_TRIGGER_MASK    0x00000002
#define MAC_DMA_IMR_S5_SBA_TIMER_TRIGGER_MASK    0x00000004
#define MAC_DMA_IMR_S5_HCF_TIMER_TRIGGER_MASK    0x00000008
#define MAC_DMA_IMR_S5_TIM_TIMER_TRIGGER_MASK    0x00000010
#define MAC_DMA_IMR_S5_DTIM_TIMER_TRIGGER_MASK   0x00000020
#define MAC_DMA_IMR_S5_QUIET_TIMER_TRIGGER_MASK  0x00000040
#define MAC_DMA_IMR_S5_NDP_TIMER_TRIGGER_MASK    0x00000080
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_TRIGGER_MASK 0x0000FF00
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_TRIGGER_LSB 8 
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_TRIGGER(_i)    (0x100 << (_i))
#define MAC_DMA_IMR_S5_TIMER_OVERFLOW_MASK       0x00010000
#define MAC_DMA_IMR_S5_DBA_TIMER_THRESHOLD_MASK  0x00020000
#define MAC_DMA_IMR_S5_SBA_TIMER_THRESHOLD_MASK  0x00040000
#define MAC_DMA_IMR_S5_HCF_TIMER_THRESHOLD_MASK  0x00080000
#define MAC_DMA_IMR_S5_TIM_TIMER_THRESHOLD_MASK  0x00100000
#define MAC_DMA_IMR_S5_DTIM_TIMER_THRESHOLD_MASK 0x00200000
#define MAC_DMA_IMR_S5_QUIET_TIMER_THRESHOLD_MASK 0000400000
#define MAC_DMA_IMR_S5_NDP_TIMER_THRESHOLD_MASK  0x00800000
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_THRESHOLD_MASK 0xFF000000
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_THRESHOLD_LSB  24 
#define MAC_DMA_IMR_S5_GENERIC_TIMER2_THRESHOLD(_i) (0x01000000 << (_i))

#define MAC_DMA_ISR_RAC_ADDRESS                  0x000000C0  /* ISR read-and-clear access */

/* Shadow copies with read-and-clear access */
#define MAC_DMA_ISR_S0_S_ADDRESS                 0x000000C4  /* ISR_S0 shadow copy */
#define MAC_DMA_ISR_S1_S_ADDRESS                 0x000000C8  /* ISR_S1 shadow copy */
#define MAC_DMA_ISR_S2_S_ADDRESS                 0x000000Cc  /* ISR_S2 shadow copy */
#define MAC_DMA_ISR_S3_S_ADDRESS                 0x000000D0  /* ISR_S3 shadow copy */
#define MAC_DMA_ISR_S4_S_ADDRESS                 0x000000D4  /* ISR_S4 shadow copy */
#define MAC_DMA_ISR_S5_S_ADDRESS                 0x000000D8  /* ISR_S5 shadow copy */

#define MAC_DMA_Q0_TXDP_ADDRESS                  0x00000800  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q1_TXDP_ADDRESS                  0x00000804  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q2_TXDP_ADDRESS                  0x00000808  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q3_TXDP_ADDRESS                  0x0000080C  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q4_TXDP_ADDRESS                  0x00000810  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q5_TXDP_ADDRESS                  0x00000814  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q6_TXDP_ADDRESS                  0x00000818  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q7_TXDP_ADDRESS                  0x0000081C  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q8_TXDP_ADDRESS                  0x00000820  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_Q9_TXDP_ADDRESS                  0x00000824  /* MAC Transmit Queue descriptor pointer */
#define MAC_DMA_QTXDP_ADDRESS(_i)                (MAC_DMA_Q0_TXDP_ADDRESS + ((_i)<<2))

#define MAC_DMA_Q_TXE_ADDRESS                    0x00000840  /* MAC Transmit Queue enable */
#define MAC_DMA_Q_TXD_ADDRESS                    0x00000880  /* MAC Transmit Queue disable */
/* QCU registers */

#define MAC_DMA_Q0_CBRCFG_ADDRESS                0x000008C0  /* MAC CBR configuration */
#define MAC_DMA_Q1_CBRCFG_ADDRESS                0x000008C4  /* MAC CBR configuration */
#define MAC_DMA_Q2_CBRCFG_ADDRESS                0x000008C8  /* MAC CBR configuration */
#define MAC_DMA_Q3_CBRCFG_ADDRESS                0x000008CC  /* MAC CBR configuration */
#define MAC_DMA_Q4_CBRCFG_ADDRESS                0x000008D0  /* MAC CBR configuration */
#define MAC_DMA_Q5_CBRCFG_ADDRESS                0x000008D4  /* MAC CBR configuration */
#define MAC_DMA_Q6_CBRCFG_ADDRESS                0x000008D8  /* MAC CBR configuration */
#define MAC_DMA_Q7_CBRCFG_ADDRESS                0x000008DC  /* MAC CBR configuration */
#define MAC_DMA_Q8_CBRCFG_ADDRESS                0x000008E0  /* MAC CBR configuration */
#define MAC_DMA_Q9_CBRCFG_ADDRESS                0x000008E4  /* MAC CBR configuration */
#define MAC_DMA_QCBRCFG_ADDRESS(_i)             (MAC_DMA_Q0_CBRCFG_ADDRESS + ((_i)<<2))

#define MAC_DMA_Q_CBRCFG_CBR_INTERVAL_MASK        0x00FFFFFF /* Mask for CBR interval (us) */
#define MAC_DMA_Q_CBRCFG_CBR_INTERVAL_LSB         0   /* Shift for CBR interval */
#define MAC_DMA_Q_CBRCFG_CBR_OVF_THRESH_MASK      0xFF000000 /* Mask for CBR overflow threshold */
#define MAC_DMA_Q_CBRCFG_CBR_OVF_THRESH_LSB       24  /* Shift for CBR overflow thresh */


#define MAC_DMA_Q0_RDYTIMECFG_ADDRESS             0x00000900  /* MAC ReadyTime configuration */
#define MAC_DMA_Q1_RDYTIMECFG_ADDRESS             0x00000904  /* MAC ReadyTime configuration */
#define MAC_DMA_Q2_RDYTIMECFG_ADDRESS             0x00000908  /* MAC ReadyTime configuration */
#define MAC_DMA_Q3_RDYTIMECFG_ADDRESS             0x0000090C  /* MAC ReadyTime configuration */
#define MAC_DMA_Q4_RDYTIMECFG_ADDRESS             0x00000910  /* MAC ReadyTime configuration */
#define MAC_DMA_Q5_RDYTIMECFG_ADDRESS             0x00000914  /* MAC ReadyTime configuration */
#define MAC_DMA_Q6_RDYTIMECFG_ADDRESS             0x00000918  /* MAC ReadyTime configuration */
#define MAC_DMA_Q7_RDYTIMECFG_ADDRESS             0x0000091C  /* MAC ReadyTime configuration */
#define MAC_DMA_Q8_RDYTIMECFG_ADDRESS             0x00000920  /* MAC ReadyTime configuration */
#define MAC_DMA_Q9_RDYTIMECFG_ADDRESS             0x00000924  /* MAC ReadyTime configuration */
#define MAC_DMA_QRDYTIMECFG_ADDRESS(_i)           (MAC_DMA_Q0_RDYTIMECFG_ADDRESS + ((_i)<<2))

#define MAC_DMA_Q_RDYTIMECFG_INT_MASK             0x00FFFFFF /* CBR interval (us) */
#define MAC_DMA_Q_RDYTIMECFG_INT_LSB              0  /* Shift for ReadyTime Interval (us) */
#define MAC_DMA_Q_RDYTIMECFG_ENA_MASK             0x01000000 /* CBR enable */

#define MAC_DMA_Q_ONESHOTMAC_DMAM_SC_ADDRESS      0x00000940  /* MAC OneShotArm set control */
#define MAC_DMA_Q_ONESHOTMAC_DMAM_CC_ADDRESS      0x00000980  /* MAC OneShotArm clear control */

#define MAC_DMA_Q0_MISC_ADDRESS                   0x000009C0  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q1_MISC_ADDRESS                   0x000009C4  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q2_MISC_ADDRESS                   0x000009C8  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q3_MISC_ADDRESS                   0x000009CC  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q4_MISC_ADDRESS                   0x000009D0  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q5_MISC_ADDRESS                   0x000009D4  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q6_MISC_ADDRESS                   0x000009D8  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q7_MISC_ADDRESS                   0x000009DC  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q8_MISC_ADDRESS                   0x000009E0  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_Q9_MISC_ADDRESS                   0x000009E4  /* MAC Miscellaneous QCU settings */
#define MAC_DMA_QMISC_ADDRESS(_i)                 (MAC_DMA_Q0_MISC_ADDRESS + ((_i)<<2))

#define MAC_DMA_Q_MISC_FSP_MASK                   0x0000000F /* Frame Scheduling Policy mask */
#define MAC_DMA_Q_MISC_FSP_ASAP                   0   /* ASAP */
#define MAC_DMA_Q_MISC_FSP_CBR                    1   /* CBR */
#define MAC_DMA_Q_MISC_FSP_DBA_GATED              2   /* DMA Beacon Alert gated */
#define MAC_DMA_Q_MISC_FSP_TIM_GATED              3   /* TIM gated */
#define MAC_DMA_Q_MISC_FSP_BEACON_SENT_GATED      4   /* Beacon-sent-gated */
#define MAC_DMA_Q_MISC_ONE_SHOT_EN_MASK           0x00000010 /* OneShot enable */
#define MAC_DMA_Q_MISC_CBR_INCR_DIS1_MASK         0x00000020 /* Disable CBR expired counter incr
                                                        (empty q) */
#define MAC_DMA_Q_MISC_CBR_INCR_DIS0_MASK         0x00000040 /* Disable CBR expired counter incr
                                                        (empty beacon q) */
#define MAC_DMA_Q_MISC_BEACON_USE_MASK            0x00000080 /* Beacon use indication */
#define MAC_DMA_Q_MISC_CBR_EXP_CNTR_LIMIT_MASK    0x00000100 /* CBR expired counter limit enable */
#define MAC_DMA_Q_MISC_RDYTIME_EXP_POLICY_MASK    0x00000200 /* Enable TXE cleared on ReadyTime expired or VEOL */
#define MAC_DMA_Q_MISC_RESET_CBR_EXP_CTR_MASK     0x00000400 /* Reset CBR expired counter */
#define MAC_DMA_Q_MISC_DCU_EARLY_TERM_REQ_MASK    0x00000800 /* DCU frame early termination request control */

#define MAC_DMA_Q0_STS_ADDRESS                   0x00000A00  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q1_STS_ADDRESS                   0x00000A04  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q2_STS_ADDRESS                   0x00000A08  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q3_STS_ADDRESS                   0x00000A0C  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q4_STS_ADDRESS                   0x00000A10  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q5_STS_ADDRESS                   0x00000A14  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q6_STS_ADDRESS                   0x00000A18  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q7_STS_ADDRESS                   0x00000A1C  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q8_STS_ADDRESS                   0x00000A20  /* MAC Miscellaneous QCU status */
#define MAC_DMA_Q9_STS_ADDRESS                   0x00000A24  /* MAC Miscellaneous QCU status */
#define MAC_DMA_QSTS_ADDRESS(_i)                 (MAC_DMA_Q0_STS_ADDRESS + ((_i)<<2))

#define MAC_DMA_Q_STS_PEND_FR_CNT_MASK           0x00000003 /* Mask for Pending Frame Count */
#define MAC_DMA_Q_STS_CBR_EXP_CNT_MASK           0x0000FF00 /* Mask for CBR expired counter */

#define MAC_DMA_Q_RDYTIMESHDN_ADDRESS            0x00000A40  /* MAC ReadyTimeShutdown status */

/* DCU registers */

#define MAC_DMA_D0_QCUMASK_ADDRESS               0x00001000  /* MAC QCU Mask */
#define MAC_DMA_D1_QCUMASK_ADDRESS               0x00001004  /* MAC QCU Mask */
#define MAC_DMA_D2_QCUMASK_ADDRESS               0x00001008  /* MAC QCU Mask */
#define MAC_DMA_D3_QCUMASK_ADDRESS               0x0000100C  /* MAC QCU Mask */
#define MAC_DMA_D4_QCUMASK_ADDRESS               0x00001010  /* MAC QCU Mask */
#define MAC_DMA_D5_QCUMASK_ADDRESS               0x00001014  /* MAC QCU Mask */
#define MAC_DMA_D6_QCUMASK_ADDRESS               0x00001018  /* MAC QCU Mask */
#define MAC_DMA_D7_QCUMASK_ADDRESS               0x0000101C  /* MAC QCU Mask */
#define MAC_DMA_D8_QCUMASK_ADDRESS               0x00001020  /* MAC QCU Mask */
#define MAC_DMA_D9_QCUMASK_ADDRESS               0x00001024  /* MAC QCU Mask */
#define MAC_DMA_DQCUMASK_ADDRESS(_i)             (MAC_DMA_D0_QCUMASK_ADDRESS + ((_i)<<2))

#define MAC_DMA_D_QCUMASK_MASK                   0x000003FF /* Mask for QCU Mask (QCU 0-9) */

#define MAC_DMA_D_GBL_IFS_SIFS_ADDRESS           0x00001030  /* DCU global SIFS settings */


#define MAC_DMA_D0_LCL_IFS_ADDRESS               0x00001040  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D1_LCL_IFS_ADDRESS               0x00001044  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D2_LCL_IFS_ADDRESS               0x00001048  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D3_LCL_IFS_ADDRESS               0x0000104C  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D4_LCL_IFS_ADDRESS               0x00001050  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D5_LCL_IFS_ADDRESS               0x00001054  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D6_LCL_IFS_ADDRESS               0x00001058  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D7_LCL_IFS_ADDRESS               0x0000105C  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D8_LCL_IFS_ADDRESS               0x00001060  /* MAC DCU-specific IFS settings */
#define MAC_DMA_D9_LCL_IFS_ADDRESS               0x00001064  /* MAC DCU-specific IFS settings */
#define MAC_DMA_DLCL_IFS_ADDRESS(_i)             (MAC_DMA_D0_LCL_IFS_ADDRESS + ((_i)<<2))
#define MAC_DMA_D_LCL_IFS_CWMIN_MASK             0x000003FF /* Mask for CW_MIN */
#define MAC_DMA_D_LCL_IFS_CWMIN_LSB              0
#define MAC_DMA_D_LCL_IFS_CWMAX_MASK             0x000FFC00 /* Mask for CW_MAX */
#define MAC_DMA_D_LCL_IFS_CWMAX_LSB              10
#define MAC_DMA_D_LCL_IFS_AIFS_MASK              0x0FF00000 /* Mask for AIFS */
#define MAC_DMA_D_LCL_IFS_AIFS_LSB               20
/*
 *  Note:  even though this field is 8 bits wide the
 *  maximum supported AIFS value is 0xFc.  Setting the AIFS value
 *  to 0xFd 0xFe, or 0xFf will not work correctly and will cause
 *  the DCU to hang.
 */
#define MAC_DMA_D_GBL_IFS_SLOT_ADDRESS           0x00001070  /* DC global slot interval */

#define MAC_DMA_D0_RETRY_LIMIT_ADDRESS           0x00001080  /* MAC Retry limits */
#define MAC_DMA_D1_RETRY_LIMIT_ADDRESS           0x00001084  /* MAC Retry limits */
#define MAC_DMA_D2_RETRY_LIMIT_ADDRESS           0x00001088  /* MAC Retry limits */
#define MAC_DMA_D3_RETRY_LIMIT_ADDRESS           0x0000108C  /* MAC Retry limits */
#define MAC_DMA_D4_RETRY_LIMIT_ADDRESS           0x00001090  /* MAC Retry limits */
#define MAC_DMA_D5_RETRY_LIMIT_ADDRESS           0x00001094  /* MAC Retry limits */
#define MAC_DMA_D6_RETRY_LIMIT_ADDRESS           0x00001098  /* MAC Retry limits */
#define MAC_DMA_D7_RETRY_LIMIT_ADDRESS           0x0000109C  /* MAC Retry limits */
#define MAC_DMA_D8_RETRY_LIMIT_ADDRESS           0x000010A0  /* MAC Retry limits */
#define MAC_DMA_D9_RETRY_LIMIT_ADDRESS           0x000010A4  /* MAC Retry limits */
#define MAC_DMA_DRETRY_LIMIT_ADDRESS(_i)         (MAC_DMA_D0_RETRY_LIMIT_ADDRESS + ((_i)<<2))

#define MAC_DMA_D_RETRY_LIMIT_FR_RTS_MASK        0x0000000F /* frame RTS failure limit */
#define MAC_DMA_D_RETRY_LIMIT_FR_RTS_LSB         0
#define MAC_DMA_D_RETRY_LIMIT_STA_RTS_MASK       0x00003F00 /* station RTS failure limit */
#define MAC_DMA_D_RETRY_LIMIT_STA_RTS_LSB        8
#define MAC_DMA_D_RETRY_LIMIT_STA_DATA_MASK      0x000FC000 /* station short retry limit */
#define MAC_DMA_D_RETRY_LIMIT_STA_DATA_LSB       14

#define MAC_DMA_D_GBL_IFS_EIFS_ADDRESS           0x000010B0  /* DCU global EIFS setting */

#define MAC_DMA_D0_CHNTIME_ADDRESS               0x000010C0  /* MAC ChannelTime settings */
#define MAC_DMA_D1_CHNTIME_ADDRESS               0x000010C4  /* MAC ChannelTime settings */
#define MAC_DMA_D2_CHNTIME_ADDRESS               0x000010C8  /* MAC ChannelTime settings */
#define MAC_DMA_D3_CHNTIME_ADDRESS               0x000010CC  /* MAC ChannelTime settings */
#define MAC_DMA_D4_CHNTIME_ADDRESS               0x000010D0  /* MAC ChannelTime settings */
#define MAC_DMA_D5_CHNTIME_ADDRESS               0x000010D4  /* MAC ChannelTime settings */
#define MAC_DMA_D6_CHNTIME_ADDRESS               0x000010D8  /* MAC ChannelTime settings */
#define MAC_DMA_D7_CHNTIME_ADDRESS               0x000010DC  /* MAC ChannelTime settings */
#define MAC_DMA_D8_CHNTIME_ADDRESS               0x000010E0  /* MAC ChannelTime settings */
#define MAC_DMA_D9_CHNTIME_ADDRESS               0x000010E4  /* MAC ChannelTime settings */
#define MAC_DMA_DCHNTIME_ADDRESS(_i)             (MAC_DMA_D0_CHNTIME_ADDRESS + ((_i)<<2))

#define MAC_DMA_D_CHNTIME_DUR_MASK               0x000FFFFF /* ChannelTime duration (us) */
#define MAC_DMA_D_CHNTIME_DUR_LSB                0 /* Shift for ChannelTime duration */
#define MAC_DMA_D_CHNTIME_EN_MASK                0x00100000 /* ChannelTime enable */

#define MAC_DMA_D_GBL_IFS_MISC_ADDRESS           0x000010f0  /* DCU global misc. IFS settings */
#define MAC_DMA_D_GBL_IFS_MISC_LFSR_SLICE_SEL_MASK 0x00000007 /* LFSR slice select */
#define MAC_DMA_D_GBL_IFS_MISC_TURBO_MODE_MASK     0x00000008 /* Turbo mode indication */
#define MAC_DMA_D_GBL_IFS_MISC_DCU_ARBITER_DLY_MASK 0x00300000 /* DCU arbiter delay */
#define MAC_DMA_D_GBL_IFS_IGNORE_BACKOFF_MASK      0x10000000

#define MAC_DMA_D0_MISC_ADDRESS                  0x00001100  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D1_MISC_ADDRESS                  0x00001104  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D2_MISC_ADDRESS                  0x00001108  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D3_MISC_ADDRESS                  0x0000110C  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D4_MISC_ADDRESS                  0x00001110  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D5_MISC_ADDRESS                  0x00001114  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D6_MISC_ADDRESS                  0x00001118  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D7_MISC_ADDRESS                  0x0000111C  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D8_MISC_ADDRESS                  0x00001120  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_D9_MISC_ADDRESS                  0x00001124  /* MAC Miscellaneous DCU-specific settings */
#define MAC_DMA_DMISC_ADDRESS(_i)                (MAC_DMA_D0_MISC_ADDRESS + ((_i)<<2))

#define MAC_DMA_D0_EOL_ADDRESS                  0x00001180
#define MAC_DMA_D1_EOL_ADDRESS                  0x00001184
#define MAC_DMA_D2_EOL_ADDRESS                  0x00001188
#define MAC_DMA_D3_EOL_ADDRESS                  0x0000118C
#define MAC_DMA_D4_EOL_ADDRESS                  0x00001190
#define MAC_DMA_D5_EOL_ADDRESS                  0x00001194
#define MAC_DMA_D6_EOL_ADDRESS                  0x00001198
#define MAC_DMA_D7_EOL_ADDRESS                  0x0000119C
#define MAC_DMA_D8_EOL_ADDRESS                  0x00001200
#define MAC_DMA_D9_EOL_ADDRESS                  0x00001204
#define MAC_DMA_DEOL_ADDRESS(_i)                (MAC_DMA_D0_EOL_ADDRESS + ((_i)<<2))

#define MAC_DMA_D_MISC_BKOFF_THRESH_MASK         0x0000003F /* Backoff threshold */
#define MAC_DMA_D_MISC_BACK_OFF_THRESH_LSB       0
#define MAC_DMA_D_MISC_ETS_RTS_MASK              0x00000040 /* End of transmission series
                                                          station RTS/data failure
                                                          count reset policy */
#define MAC_DMA_D_MISC_ETS_CW_MASK               0x00000080 /* End of transmission series
                                                          CW reset policy */
#define MAC_DMA_D_MISC_FRAG_WAIT_EN_MASK         0x00000100  /* Fragment Starvation Policy */

#define MAC_DMA_D_MISC_FRAG_BKOFF_EN_MASK        0x00000200 /* Backoff during a frag burst */
#define MAC_DMA_D_MISC_HCF_POLL_EN_MASK          0x00000800 /* HFC poll enable */
#define MAC_DMA_D_MISC_BKOFF_PERSISTENCE_MASK    0x00001000 /* Backoff persistence factor
                                                          setting */
#define MAC_DMA_D_MISC_VIR_COL_HANDLING_MASK     0x0000C000 /* Mask for Virtual collision
                                                          handling policy */
#define MAC_DMA_D_MISC_VIR_COL_HANDLING_LSB      14
#define MAC_DMA_D_MISC_VIR_COL_HANDLING_DEFAULT  0   /* Normal */
#define MAC_DMA_D_MISC_VIR_COL_HANDLING_IGNORE   1   /* Ignore */
#define MAC_DMA_D_MISC_BEACON_USE_MASK           0x00010000 /*  Beacon use indication */
#define MAC_DMA_D_MISC_ARB_LOCKOUT_CNTRL_MASK 0x00060000 /*  Mask for DCU arbiter lockout control */
#define MAC_DMA_D_MISC_ARB_LOCKOUT_CNTRL_LSB  17
#define MAC_DMA_D_MISC_ARB_LOCKOUT_CNTRL_NONE     0        /*  No lockout*/
#define MAC_DMA_D_MISC_ARB_LOCKOUT_CNTRL_INTRA_FR 1        /*  Intra-frame*/
#define MAC_DMA_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL   2        /*  Global */
#define MAC_DMA_D_MISC_ARB_LOCKOUT_IGNORE_MASK 0x00080000 /*  DCU arbiter lockout ignore control */
#define MAC_DMA_D_MISC_SEQ_NUM_INCR_DIS_MASK    0x00100000 /* Sequence number increment disable */
#define MAC_DMA_D_MISC_POST_FR_BKOFF_DIS_MASK   0x00200000 /* Post-frame backoff disable */
#define MAC_DMA_D_MISC_VIRT_COLL_POLICY_MASK    0x00400000 /* Virtual coll. handling policy */
#define MAC_DMA_D_MISC_BLOWN_IFS_POLICY_MASK    0x00800000 /* Blown IFS handling policy */

#define MAC_DMA_D_SEQNUM_ADDRESS                0x00001140  /* MAC Frame sequence number */



#define MAC_DMA_D_FPCTL_ADDRESS                  0x00001230      /* DCU frame prefetch settings */
#define MAC_DMA_D_TXPSE_ADDRESS                  0x00001270      /* DCU transmit pause control/status */

#endif /* _AR6000_DMMAEG_H_ */
