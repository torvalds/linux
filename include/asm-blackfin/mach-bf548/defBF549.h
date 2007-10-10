/*
 * File:         include/asm-blackfin/mach-bf548/defBF549.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _DEF_BF549_H
#define _DEF_BF549_H

/* Include all Core registers and bit definitions */
#include <asm/mach-common/def_LPBlackfin.h>


/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF549 */

/* Include defBF54x_base.h for the set of #defines that are common to all ADSP-BF54x processors */
#include "defBF54x_base.h"

/* The following are the #defines needed by ADSP-BF549 that are not in the common header */

/* Timer Registers */

#define                    TIMER8_CONFIG  0xffc00600   /* Timer 8 Configuration Register */
#define                   TIMER8_COUNTER  0xffc00604   /* Timer 8 Counter Register */
#define                    TIMER8_PERIOD  0xffc00608   /* Timer 8 Period Register */
#define                     TIMER8_WIDTH  0xffc0060c   /* Timer 8 Width Register */
#define                    TIMER9_CONFIG  0xffc00610   /* Timer 9 Configuration Register */
#define                   TIMER9_COUNTER  0xffc00614   /* Timer 9 Counter Register */
#define                    TIMER9_PERIOD  0xffc00618   /* Timer 9 Period Register */
#define                     TIMER9_WIDTH  0xffc0061c   /* Timer 9 Width Register */
#define                   TIMER10_CONFIG  0xffc00620   /* Timer 10 Configuration Register */
#define                  TIMER10_COUNTER  0xffc00624   /* Timer 10 Counter Register */
#define                   TIMER10_PERIOD  0xffc00628   /* Timer 10 Period Register */
#define                    TIMER10_WIDTH  0xffc0062c   /* Timer 10 Width Register */

/* Timer Group of 3 Registers */

#define                    TIMER_ENABLE1  0xffc00640   /* Timer Group of 3 Enable Register */
#define                   TIMER_DISABLE1  0xffc00644   /* Timer Group of 3 Disable Register */
#define                    TIMER_STATUS1  0xffc00648   /* Timer Group of 3 Status Register */

/* SPORT0 Registers */

#define                      SPORT0_TCR1  0xffc00800   /* SPORT0 Transmit Configuration 1 Register */
#define                      SPORT0_TCR2  0xffc00804   /* SPORT0 Transmit Configuration 2 Register */
#define                   SPORT0_TCLKDIV  0xffc00808   /* SPORT0 Transmit Serial Clock Divider Register */
#define                    SPORT0_TFSDIV  0xffc0080c   /* SPORT0 Transmit Frame Sync Divider Register */
#define                        SPORT0_TX  0xffc00810   /* SPORT0 Transmit Data Register */
#define                        SPORT0_RX  0xffc00818   /* SPORT0 Receive Data Register */
#define                      SPORT0_RCR1  0xffc00820   /* SPORT0 Receive Configuration 1 Register */
#define                      SPORT0_RCR2  0xffc00824   /* SPORT0 Receive Configuration 2 Register */
#define                   SPORT0_RCLKDIV  0xffc00828   /* SPORT0 Receive Serial Clock Divider Register */
#define                    SPORT0_RFSDIV  0xffc0082c   /* SPORT0 Receive Frame Sync Divider Register */
#define                      SPORT0_STAT  0xffc00830   /* SPORT0 Status Register */
#define                      SPORT0_CHNL  0xffc00834   /* SPORT0 Current Channel Register */
#define                     SPORT0_MCMC1  0xffc00838   /* SPORT0 Multi channel Configuration Register 1 */
#define                     SPORT0_MCMC2  0xffc0083c   /* SPORT0 Multi channel Configuration Register 2 */
#define                     SPORT0_MTCS0  0xffc00840   /* SPORT0 Multi channel Transmit Select Register 0 */
#define                     SPORT0_MTCS1  0xffc00844   /* SPORT0 Multi channel Transmit Select Register 1 */
#define                     SPORT0_MTCS2  0xffc00848   /* SPORT0 Multi channel Transmit Select Register 2 */
#define                     SPORT0_MTCS3  0xffc0084c   /* SPORT0 Multi channel Transmit Select Register 3 */
#define                     SPORT0_MRCS0  0xffc00850   /* SPORT0 Multi channel Receive Select Register 0 */
#define                     SPORT0_MRCS1  0xffc00854   /* SPORT0 Multi channel Receive Select Register 1 */
#define                     SPORT0_MRCS2  0xffc00858   /* SPORT0 Multi channel Receive Select Register 2 */
#define                     SPORT0_MRCS3  0xffc0085c   /* SPORT0 Multi channel Receive Select Register 3 */

/* EPPI0 Registers */

#define                     EPPI0_STATUS  0xffc01000   /* EPPI0 Status Register */
#define                     EPPI0_HCOUNT  0xffc01004   /* EPPI0 Horizontal Transfer Count Register */
#define                     EPPI0_HDELAY  0xffc01008   /* EPPI0 Horizontal Delay Count Register */
#define                     EPPI0_VCOUNT  0xffc0100c   /* EPPI0 Vertical Transfer Count Register */
#define                     EPPI0_VDELAY  0xffc01010   /* EPPI0 Vertical Delay Count Register */
#define                      EPPI0_FRAME  0xffc01014   /* EPPI0 Lines per Frame Register */
#define                       EPPI0_LINE  0xffc01018   /* EPPI0 Samples per Line Register */
#define                     EPPI0_CLKDIV  0xffc0101c   /* EPPI0 Clock Divide Register */
#define                    EPPI0_CONTROL  0xffc01020   /* EPPI0 Control Register */
#define                   EPPI0_FS1W_HBL  0xffc01024   /* EPPI0 FS1 Width Register / EPPI0 Horizontal Blanking Samples Per Line Register */
#define                  EPPI0_FS1P_AVPL  0xffc01028   /* EPPI0 FS1 Period Register / EPPI0 Active Video Samples Per Line Register */
#define                   EPPI0_FS2W_LVB  0xffc0102c   /* EPPI0 FS2 Width Register / EPPI0 Lines of Vertical Blanking Register */
#define                  EPPI0_FS2P_LAVF  0xffc01030   /* EPPI0 FS2 Period Register/ EPPI0 Lines of Active Video Per Field Register */
#define                       EPPI0_CLIP  0xffc01034   /* EPPI0 Clipping Register */

/* UART2 Registers */

#define                        UART2_DLL  0xffc02100   /* Divisor Latch Low Byte */
#define                        UART2_DLH  0xffc02104   /* Divisor Latch High Byte */
#define                       UART2_GCTL  0xffc02108   /* Global Control Register */
#define                        UART2_LCR  0xffc0210c   /* Line Control Register */
#define                        UART2_MCR  0xffc02110   /* Modem Control Register */
#define                        UART2_LSR  0xffc02114   /* Line Status Register */
#define                        UART2_MSR  0xffc02118   /* Modem Status Register */
#define                        UART2_SCR  0xffc0211c   /* Scratch Register */
#define                    UART2_IER_SET  0xffc02120   /* Interrupt Enable Register Set */
#define                  UART2_IER_CLEAR  0xffc02124   /* Interrupt Enable Register Clear */
#define                        UART2_RBR  0xffc0212c   /* Receive Buffer Register */

/* Two Wire Interface Registers (TWI1) */

#define                     TWI1_REGBASE  0xffc02200
#define                      TWI1_CLKDIV  0xffc02200   /* Clock Divider Register */
#define                     TWI1_CONTROL  0xffc02204   /* TWI Control Register */
#define                  TWI1_SLAVE_CTRL  0xffc02208   /* TWI Slave Mode Control Register */
#define                  TWI1_SLAVE_STAT  0xffc0220c   /* TWI Slave Mode Status Register */
#define                  TWI1_SLAVE_ADDR  0xffc02210   /* TWI Slave Mode Address Register */
#define                 TWI1_MASTER_CTRL  0xffc02214   /* TWI Master Mode Control Register */
#define                 TWI1_MASTER_STAT  0xffc02218   /* TWI Master Mode Status Register */
#define                 TWI1_MASTER_ADDR  0xffc0221c   /* TWI Master Mode Address Register */
#define                    TWI1_INT_STAT  0xffc02220   /* TWI Interrupt Status Register */
#define                    TWI1_INT_MASK  0xffc02224   /* TWI Interrupt Mask Register */
#define                   TWI1_FIFO_CTRL  0xffc02228   /* TWI FIFO Control Register */
#define                   TWI1_FIFO_STAT  0xffc0222c   /* TWI FIFO Status Register */
#define                   TWI1_XMT_DATA8  0xffc02280   /* TWI FIFO Transmit Data Single Byte Register */
#define                  TWI1_XMT_DATA16  0xffc02284   /* TWI FIFO Transmit Data Double Byte Register */
#define                   TWI1_RCV_DATA8  0xffc02288   /* TWI FIFO Receive Data Single Byte Register */
#define                  TWI1_RCV_DATA16  0xffc0228c   /* TWI FIFO Receive Data Double Byte Register */

/* SPI2  Registers */

#define                     SPI2_REGBASE  0xffc02400
#define                         SPI2_CTL  0xffc02400   /* SPI2 Control Register */
#define                         SPI2_FLG  0xffc02404   /* SPI2 Flag Register */
#define                        SPI2_STAT  0xffc02408   /* SPI2 Status Register */
#define                        SPI2_TDBR  0xffc0240c   /* SPI2 Transmit Data Buffer Register */
#define                        SPI2_RDBR  0xffc02410   /* SPI2 Receive Data Buffer Register */
#define                        SPI2_BAUD  0xffc02414   /* SPI2 Baud Rate Register */
#define                      SPI2_SHADOW  0xffc02418   /* SPI2 Receive Data Buffer Shadow Register */

/* MXVR Registers */

#define                      MXVR_CONFIG  0xffc02700   /* MXVR Configuration Register */
#define                     MXVR_STATE_0  0xffc02708   /* MXVR State Register 0 */
#define                     MXVR_STATE_1  0xffc0270c   /* MXVR State Register 1 */
#define                  MXVR_INT_STAT_0  0xffc02710   /* MXVR Interrupt Status Register 0 */
#define                  MXVR_INT_STAT_1  0xffc02714   /* MXVR Interrupt Status Register 1 */
#define                    MXVR_INT_EN_0  0xffc02718   /* MXVR Interrupt Enable Register 0 */
#define                    MXVR_INT_EN_1  0xffc0271c   /* MXVR Interrupt Enable Register 1 */
#define                    MXVR_POSITION  0xffc02720   /* MXVR Node Position Register */
#define                MXVR_MAX_POSITION  0xffc02724   /* MXVR Maximum Node Position Register */
#define                       MXVR_DELAY  0xffc02728   /* MXVR Node Frame Delay Register */
#define                   MXVR_MAX_DELAY  0xffc0272c   /* MXVR Maximum Node Frame Delay Register */
#define                       MXVR_LADDR  0xffc02730   /* MXVR Logical Address Register */
#define                       MXVR_GADDR  0xffc02734   /* MXVR Group Address Register */
#define                       MXVR_AADDR  0xffc02738   /* MXVR Alternate Address Register */

/* MXVR Allocation Table Registers */

#define                     MXVR_ALLOC_0  0xffc0273c   /* MXVR Allocation Table Register 0 */
#define                     MXVR_ALLOC_1  0xffc02740   /* MXVR Allocation Table Register 1 */
#define                     MXVR_ALLOC_2  0xffc02744   /* MXVR Allocation Table Register 2 */
#define                     MXVR_ALLOC_3  0xffc02748   /* MXVR Allocation Table Register 3 */
#define                     MXVR_ALLOC_4  0xffc0274c   /* MXVR Allocation Table Register 4 */
#define                     MXVR_ALLOC_5  0xffc02750   /* MXVR Allocation Table Register 5 */
#define                     MXVR_ALLOC_6  0xffc02754   /* MXVR Allocation Table Register 6 */
#define                     MXVR_ALLOC_7  0xffc02758   /* MXVR Allocation Table Register 7 */
#define                     MXVR_ALLOC_8  0xffc0275c   /* MXVR Allocation Table Register 8 */
#define                     MXVR_ALLOC_9  0xffc02760   /* MXVR Allocation Table Register 9 */
#define                    MXVR_ALLOC_10  0xffc02764   /* MXVR Allocation Table Register 10 */
#define                    MXVR_ALLOC_11  0xffc02768   /* MXVR Allocation Table Register 11 */
#define                    MXVR_ALLOC_12  0xffc0276c   /* MXVR Allocation Table Register 12 */
#define                    MXVR_ALLOC_13  0xffc02770   /* MXVR Allocation Table Register 13 */
#define                    MXVR_ALLOC_14  0xffc02774   /* MXVR Allocation Table Register 14 */

/* MXVR Channel Assign Registers */

#define                MXVR_SYNC_LCHAN_0  0xffc02778   /* MXVR Sync Data Logical Channel Assign Register 0 */
#define                MXVR_SYNC_LCHAN_1  0xffc0277c   /* MXVR Sync Data Logical Channel Assign Register 1 */
#define                MXVR_SYNC_LCHAN_2  0xffc02780   /* MXVR Sync Data Logical Channel Assign Register 2 */
#define                MXVR_SYNC_LCHAN_3  0xffc02784   /* MXVR Sync Data Logical Channel Assign Register 3 */
#define                MXVR_SYNC_LCHAN_4  0xffc02788   /* MXVR Sync Data Logical Channel Assign Register 4 */
#define                MXVR_SYNC_LCHAN_5  0xffc0278c   /* MXVR Sync Data Logical Channel Assign Register 5 */
#define                MXVR_SYNC_LCHAN_6  0xffc02790   /* MXVR Sync Data Logical Channel Assign Register 6 */
#define                MXVR_SYNC_LCHAN_7  0xffc02794   /* MXVR Sync Data Logical Channel Assign Register 7 */

/* MXVR DMA0 Registers */

#define                 MXVR_DMA0_CONFIG  0xffc02798   /* MXVR Sync Data DMA0 Config Register */
#define             MXVR_DMA0_START_ADDR  0xffc0279c   /* MXVR Sync Data DMA0 Start Address */
#define                  MXVR_DMA0_COUNT  0xffc027a0   /* MXVR Sync Data DMA0 Loop Count Register */
#define              MXVR_DMA0_CURR_ADDR  0xffc027a4   /* MXVR Sync Data DMA0 Current Address */
#define             MXVR_DMA0_CURR_COUNT  0xffc027a8   /* MXVR Sync Data DMA0 Current Loop Count */

/* MXVR DMA1 Registers */

#define                 MXVR_DMA1_CONFIG  0xffc027ac   /* MXVR Sync Data DMA1 Config Register */
#define             MXVR_DMA1_START_ADDR  0xffc027b0   /* MXVR Sync Data DMA1 Start Address */
#define                  MXVR_DMA1_COUNT  0xffc027b4   /* MXVR Sync Data DMA1 Loop Count Register */
#define              MXVR_DMA1_CURR_ADDR  0xffc027b8   /* MXVR Sync Data DMA1 Current Address */
#define             MXVR_DMA1_CURR_COUNT  0xffc027bc   /* MXVR Sync Data DMA1 Current Loop Count */

/* MXVR DMA2 Registers */

#define                 MXVR_DMA2_CONFIG  0xffc027c0   /* MXVR Sync Data DMA2 Config Register */
#define             MXVR_DMA2_START_ADDR  0xffc027c4   /* MXVR Sync Data DMA2 Start Address */
#define                  MXVR_DMA2_COUNT  0xffc027c8   /* MXVR Sync Data DMA2 Loop Count Register */
#define              MXVR_DMA2_CURR_ADDR  0xffc027cc   /* MXVR Sync Data DMA2 Current Address */
#define             MXVR_DMA2_CURR_COUNT  0xffc027d0   /* MXVR Sync Data DMA2 Current Loop Count */

/* MXVR DMA3 Registers */

#define                 MXVR_DMA3_CONFIG  0xffc027d4   /* MXVR Sync Data DMA3 Config Register */
#define             MXVR_DMA3_START_ADDR  0xffc027d8   /* MXVR Sync Data DMA3 Start Address */
#define                  MXVR_DMA3_COUNT  0xffc027dc   /* MXVR Sync Data DMA3 Loop Count Register */
#define              MXVR_DMA3_CURR_ADDR  0xffc027e0   /* MXVR Sync Data DMA3 Current Address */
#define             MXVR_DMA3_CURR_COUNT  0xffc027e4   /* MXVR Sync Data DMA3 Current Loop Count */

/* MXVR DMA4 Registers */

#define                 MXVR_DMA4_CONFIG  0xffc027e8   /* MXVR Sync Data DMA4 Config Register */
#define             MXVR_DMA4_START_ADDR  0xffc027ec   /* MXVR Sync Data DMA4 Start Address */
#define                  MXVR_DMA4_COUNT  0xffc027f0   /* MXVR Sync Data DMA4 Loop Count Register */
#define              MXVR_DMA4_CURR_ADDR  0xffc027f4   /* MXVR Sync Data DMA4 Current Address */
#define             MXVR_DMA4_CURR_COUNT  0xffc027f8   /* MXVR Sync Data DMA4 Current Loop Count */

/* MXVR DMA5 Registers */

#define                 MXVR_DMA5_CONFIG  0xffc027fc   /* MXVR Sync Data DMA5 Config Register */
#define             MXVR_DMA5_START_ADDR  0xffc02800   /* MXVR Sync Data DMA5 Start Address */
#define                  MXVR_DMA5_COUNT  0xffc02804   /* MXVR Sync Data DMA5 Loop Count Register */
#define              MXVR_DMA5_CURR_ADDR  0xffc02808   /* MXVR Sync Data DMA5 Current Address */
#define             MXVR_DMA5_CURR_COUNT  0xffc0280c   /* MXVR Sync Data DMA5 Current Loop Count */

/* MXVR DMA6 Registers */

#define                 MXVR_DMA6_CONFIG  0xffc02810   /* MXVR Sync Data DMA6 Config Register */
#define             MXVR_DMA6_START_ADDR  0xffc02814   /* MXVR Sync Data DMA6 Start Address */
#define                  MXVR_DMA6_COUNT  0xffc02818   /* MXVR Sync Data DMA6 Loop Count Register */
#define              MXVR_DMA6_CURR_ADDR  0xffc0281c   /* MXVR Sync Data DMA6 Current Address */
#define             MXVR_DMA6_CURR_COUNT  0xffc02820   /* MXVR Sync Data DMA6 Current Loop Count */

/* MXVR DMA7 Registers */

#define                 MXVR_DMA7_CONFIG  0xffc02824   /* MXVR Sync Data DMA7 Config Register */
#define             MXVR_DMA7_START_ADDR  0xffc02828   /* MXVR Sync Data DMA7 Start Address */
#define                  MXVR_DMA7_COUNT  0xffc0282c   /* MXVR Sync Data DMA7 Loop Count Register */
#define              MXVR_DMA7_CURR_ADDR  0xffc02830   /* MXVR Sync Data DMA7 Current Address */
#define             MXVR_DMA7_CURR_COUNT  0xffc02834   /* MXVR Sync Data DMA7 Current Loop Count */

/* MXVR Asynch Packet Registers */

#define                      MXVR_AP_CTL  0xffc02838   /* MXVR Async Packet Control Register */
#define             MXVR_APRB_START_ADDR  0xffc0283c   /* MXVR Async Packet RX Buffer Start Addr Register */
#define              MXVR_APRB_CURR_ADDR  0xffc02840   /* MXVR Async Packet RX Buffer Current Addr Register */
#define             MXVR_APTB_START_ADDR  0xffc02844   /* MXVR Async Packet TX Buffer Start Addr Register */
#define              MXVR_APTB_CURR_ADDR  0xffc02848   /* MXVR Async Packet TX Buffer Current Addr Register */

/* MXVR Control Message Registers */

#define                      MXVR_CM_CTL  0xffc0284c   /* MXVR Control Message Control Register */
#define             MXVR_CMRB_START_ADDR  0xffc02850   /* MXVR Control Message RX Buffer Start Addr Register */
#define              MXVR_CMRB_CURR_ADDR  0xffc02854   /* MXVR Control Message RX Buffer Current Address */
#define             MXVR_CMTB_START_ADDR  0xffc02858   /* MXVR Control Message TX Buffer Start Addr Register */
#define              MXVR_CMTB_CURR_ADDR  0xffc0285c   /* MXVR Control Message TX Buffer Current Address */

/* MXVR Remote Read Registers */

#define             MXVR_RRDB_START_ADDR  0xffc02860   /* MXVR Remote Read Buffer Start Addr Register */
#define              MXVR_RRDB_CURR_ADDR  0xffc02864   /* MXVR Remote Read Buffer Current Addr Register */

/* MXVR Pattern Data Registers */

#define                  MXVR_PAT_DATA_0  0xffc02868   /* MXVR Pattern Data Register 0 */
#define                    MXVR_PAT_EN_0  0xffc0286c   /* MXVR Pattern Enable Register 0 */
#define                  MXVR_PAT_DATA_1  0xffc02870   /* MXVR Pattern Data Register 1 */
#define                    MXVR_PAT_EN_1  0xffc02874   /* MXVR Pattern Enable Register 1 */

/* MXVR Frame Counter Registers */

#define                 MXVR_FRAME_CNT_0  0xffc02878   /* MXVR Frame Counter 0 */
#define                 MXVR_FRAME_CNT_1  0xffc0287c   /* MXVR Frame Counter 1 */

/* MXVR Routing Table Registers */

#define                   MXVR_ROUTING_0  0xffc02880   /* MXVR Routing Table Register 0 */
#define                   MXVR_ROUTING_1  0xffc02884   /* MXVR Routing Table Register 1 */
#define                   MXVR_ROUTING_2  0xffc02888   /* MXVR Routing Table Register 2 */
#define                   MXVR_ROUTING_3  0xffc0288c   /* MXVR Routing Table Register 3 */
#define                   MXVR_ROUTING_4  0xffc02890   /* MXVR Routing Table Register 4 */
#define                   MXVR_ROUTING_5  0xffc02894   /* MXVR Routing Table Register 5 */
#define                   MXVR_ROUTING_6  0xffc02898   /* MXVR Routing Table Register 6 */
#define                   MXVR_ROUTING_7  0xffc0289c   /* MXVR Routing Table Register 7 */
#define                   MXVR_ROUTING_8  0xffc028a0   /* MXVR Routing Table Register 8 */
#define                   MXVR_ROUTING_9  0xffc028a4   /* MXVR Routing Table Register 9 */
#define                  MXVR_ROUTING_10  0xffc028a8   /* MXVR Routing Table Register 10 */
#define                  MXVR_ROUTING_11  0xffc028ac   /* MXVR Routing Table Register 11 */
#define                  MXVR_ROUTING_12  0xffc028b0   /* MXVR Routing Table Register 12 */
#define                  MXVR_ROUTING_13  0xffc028b4   /* MXVR Routing Table Register 13 */
#define                  MXVR_ROUTING_14  0xffc028b8   /* MXVR Routing Table Register 14 */

/* MXVR Counter-Clock-Control Registers */

#define                   MXVR_BLOCK_CNT  0xffc028c0   /* MXVR Block Counter */
#define                     MXVR_CLK_CTL  0xffc028d0   /* MXVR Clock Control Register */
#define                  MXVR_CDRPLL_CTL  0xffc028d4   /* MXVR Clock/Data Recovery PLL Control Register */
#define                   MXVR_FMPLL_CTL  0xffc028d8   /* MXVR Frequency Multiply PLL Control Register */
#define                     MXVR_PIN_CTL  0xffc028dc   /* MXVR Pin Control Register */
#define                    MXVR_SCLK_CNT  0xffc028e0   /* MXVR System Clock Counter Register */

/* CAN Controller 1 Config 1 Registers */

#define                         CAN1_MC1  0xffc03200   /* CAN Controller 1 Mailbox Configuration Register 1 */
#define                         CAN1_MD1  0xffc03204   /* CAN Controller 1 Mailbox Direction Register 1 */
#define                        CAN1_TRS1  0xffc03208   /* CAN Controller 1 Transmit Request Set Register 1 */
#define                        CAN1_TRR1  0xffc0320c   /* CAN Controller 1 Transmit Request Reset Register 1 */
#define                         CAN1_TA1  0xffc03210   /* CAN Controller 1 Transmit Acknowledge Register 1 */
#define                         CAN1_AA1  0xffc03214   /* CAN Controller 1 Abort Acknowledge Register 1 */
#define                        CAN1_RMP1  0xffc03218   /* CAN Controller 1 Receive Message Pending Register 1 */
#define                        CAN1_RML1  0xffc0321c   /* CAN Controller 1 Receive Message Lost Register 1 */
#define                      CAN1_MBTIF1  0xffc03220   /* CAN Controller 1 Mailbox Transmit Interrupt Flag Register 1 */
#define                      CAN1_MBRIF1  0xffc03224   /* CAN Controller 1 Mailbox Receive Interrupt Flag Register 1 */
#define                       CAN1_MBIM1  0xffc03228   /* CAN Controller 1 Mailbox Interrupt Mask Register 1 */
#define                        CAN1_RFH1  0xffc0322c   /* CAN Controller 1 Remote Frame Handling Enable Register 1 */
#define                       CAN1_OPSS1  0xffc03230   /* CAN Controller 1 Overwrite Protection Single Shot Transmit Register 1 */

/* CAN Controller 1 Config 2 Registers */

#define                         CAN1_MC2  0xffc03240   /* CAN Controller 1 Mailbox Configuration Register 2 */
#define                         CAN1_MD2  0xffc03244   /* CAN Controller 1 Mailbox Direction Register 2 */
#define                        CAN1_TRS2  0xffc03248   /* CAN Controller 1 Transmit Request Set Register 2 */
#define                        CAN1_TRR2  0xffc0324c   /* CAN Controller 1 Transmit Request Reset Register 2 */
#define                         CAN1_TA2  0xffc03250   /* CAN Controller 1 Transmit Acknowledge Register 2 */
#define                         CAN1_AA2  0xffc03254   /* CAN Controller 1 Abort Acknowledge Register 2 */
#define                        CAN1_RMP2  0xffc03258   /* CAN Controller 1 Receive Message Pending Register 2 */
#define                        CAN1_RML2  0xffc0325c   /* CAN Controller 1 Receive Message Lost Register 2 */
#define                      CAN1_MBTIF2  0xffc03260   /* CAN Controller 1 Mailbox Transmit Interrupt Flag Register 2 */
#define                      CAN1_MBRIF2  0xffc03264   /* CAN Controller 1 Mailbox Receive Interrupt Flag Register 2 */
#define                       CAN1_MBIM2  0xffc03268   /* CAN Controller 1 Mailbox Interrupt Mask Register 2 */
#define                        CAN1_RFH2  0xffc0326c   /* CAN Controller 1 Remote Frame Handling Enable Register 2 */
#define                       CAN1_OPSS2  0xffc03270   /* CAN Controller 1 Overwrite Protection Single Shot Transmit Register 2 */

/* CAN Controller 1 Clock/Interrupt/Counter Registers */

#define                       CAN1_CLOCK  0xffc03280   /* CAN Controller 1 Clock Register */
#define                      CAN1_TIMING  0xffc03284   /* CAN Controller 1 Timing Register */
#define                       CAN1_DEBUG  0xffc03288   /* CAN Controller 1 Debug Register */
#define                      CAN1_STATUS  0xffc0328c   /* CAN Controller 1 Global Status Register */
#define                         CAN1_CEC  0xffc03290   /* CAN Controller 1 Error Counter Register */
#define                         CAN1_GIS  0xffc03294   /* CAN Controller 1 Global Interrupt Status Register */
#define                         CAN1_GIM  0xffc03298   /* CAN Controller 1 Global Interrupt Mask Register */
#define                         CAN1_GIF  0xffc0329c   /* CAN Controller 1 Global Interrupt Flag Register */
#define                     CAN1_CONTROL  0xffc032a0   /* CAN Controller 1 Master Control Register */
#define                        CAN1_INTR  0xffc032a4   /* CAN Controller 1 Interrupt Pending Register */
#define                        CAN1_MBTD  0xffc032ac   /* CAN Controller 1 Mailbox Temporary Disable Register */
#define                         CAN1_EWR  0xffc032b0   /* CAN Controller 1 Programmable Warning Level Register */
#define                         CAN1_ESR  0xffc032b4   /* CAN Controller 1 Error Status Register */
#define                       CAN1_UCCNT  0xffc032c4   /* CAN Controller 1 Universal Counter Register */
#define                        CAN1_UCRC  0xffc032c8   /* CAN Controller 1 Universal Counter Force Reload Register */
#define                       CAN1_UCCNF  0xffc032cc   /* CAN Controller 1 Universal Counter Configuration Register */

/* CAN Controller 1 Mailbox Acceptance Registers */

#define                       CAN1_AM00L  0xffc03300   /* CAN Controller 1 Mailbox 0 Acceptance Mask High Register */
#define                       CAN1_AM00H  0xffc03304   /* CAN Controller 1 Mailbox 0 Acceptance Mask Low Register */
#define                       CAN1_AM01L  0xffc03308   /* CAN Controller 1 Mailbox 1 Acceptance Mask High Register */
#define                       CAN1_AM01H  0xffc0330c   /* CAN Controller 1 Mailbox 1 Acceptance Mask Low Register */
#define                       CAN1_AM02L  0xffc03310   /* CAN Controller 1 Mailbox 2 Acceptance Mask High Register */
#define                       CAN1_AM02H  0xffc03314   /* CAN Controller 1 Mailbox 2 Acceptance Mask Low Register */
#define                       CAN1_AM03L  0xffc03318   /* CAN Controller 1 Mailbox 3 Acceptance Mask High Register */
#define                       CAN1_AM03H  0xffc0331c   /* CAN Controller 1 Mailbox 3 Acceptance Mask Low Register */
#define                       CAN1_AM04L  0xffc03320   /* CAN Controller 1 Mailbox 4 Acceptance Mask High Register */
#define                       CAN1_AM04H  0xffc03324   /* CAN Controller 1 Mailbox 4 Acceptance Mask Low Register */
#define                       CAN1_AM05L  0xffc03328   /* CAN Controller 1 Mailbox 5 Acceptance Mask High Register */
#define                       CAN1_AM05H  0xffc0332c   /* CAN Controller 1 Mailbox 5 Acceptance Mask Low Register */
#define                       CAN1_AM06L  0xffc03330   /* CAN Controller 1 Mailbox 6 Acceptance Mask High Register */
#define                       CAN1_AM06H  0xffc03334   /* CAN Controller 1 Mailbox 6 Acceptance Mask Low Register */
#define                       CAN1_AM07L  0xffc03338   /* CAN Controller 1 Mailbox 7 Acceptance Mask High Register */
#define                       CAN1_AM07H  0xffc0333c   /* CAN Controller 1 Mailbox 7 Acceptance Mask Low Register */
#define                       CAN1_AM08L  0xffc03340   /* CAN Controller 1 Mailbox 8 Acceptance Mask High Register */
#define                       CAN1_AM08H  0xffc03344   /* CAN Controller 1 Mailbox 8 Acceptance Mask Low Register */
#define                       CAN1_AM09L  0xffc03348   /* CAN Controller 1 Mailbox 9 Acceptance Mask High Register */
#define                       CAN1_AM09H  0xffc0334c   /* CAN Controller 1 Mailbox 9 Acceptance Mask Low Register */
#define                       CAN1_AM10L  0xffc03350   /* CAN Controller 1 Mailbox 10 Acceptance Mask High Register */
#define                       CAN1_AM10H  0xffc03354   /* CAN Controller 1 Mailbox 10 Acceptance Mask Low Register */
#define                       CAN1_AM11L  0xffc03358   /* CAN Controller 1 Mailbox 11 Acceptance Mask High Register */
#define                       CAN1_AM11H  0xffc0335c   /* CAN Controller 1 Mailbox 11 Acceptance Mask Low Register */
#define                       CAN1_AM12L  0xffc03360   /* CAN Controller 1 Mailbox 12 Acceptance Mask High Register */
#define                       CAN1_AM12H  0xffc03364   /* CAN Controller 1 Mailbox 12 Acceptance Mask Low Register */
#define                       CAN1_AM13L  0xffc03368   /* CAN Controller 1 Mailbox 13 Acceptance Mask High Register */
#define                       CAN1_AM13H  0xffc0336c   /* CAN Controller 1 Mailbox 13 Acceptance Mask Low Register */
#define                       CAN1_AM14L  0xffc03370   /* CAN Controller 1 Mailbox 14 Acceptance Mask High Register */
#define                       CAN1_AM14H  0xffc03374   /* CAN Controller 1 Mailbox 14 Acceptance Mask Low Register */
#define                       CAN1_AM15L  0xffc03378   /* CAN Controller 1 Mailbox 15 Acceptance Mask High Register */
#define                       CAN1_AM15H  0xffc0337c   /* CAN Controller 1 Mailbox 15 Acceptance Mask Low Register */

/* CAN Controller 1 Mailbox Acceptance Registers */

#define                       CAN1_AM16L  0xffc03380   /* CAN Controller 1 Mailbox 16 Acceptance Mask High Register */
#define                       CAN1_AM16H  0xffc03384   /* CAN Controller 1 Mailbox 16 Acceptance Mask Low Register */
#define                       CAN1_AM17L  0xffc03388   /* CAN Controller 1 Mailbox 17 Acceptance Mask High Register */
#define                       CAN1_AM17H  0xffc0338c   /* CAN Controller 1 Mailbox 17 Acceptance Mask Low Register */
#define                       CAN1_AM18L  0xffc03390   /* CAN Controller 1 Mailbox 18 Acceptance Mask High Register */
#define                       CAN1_AM18H  0xffc03394   /* CAN Controller 1 Mailbox 18 Acceptance Mask Low Register */
#define                       CAN1_AM19L  0xffc03398   /* CAN Controller 1 Mailbox 19 Acceptance Mask High Register */
#define                       CAN1_AM19H  0xffc0339c   /* CAN Controller 1 Mailbox 19 Acceptance Mask Low Register */
#define                       CAN1_AM20L  0xffc033a0   /* CAN Controller 1 Mailbox 20 Acceptance Mask High Register */
#define                       CAN1_AM20H  0xffc033a4   /* CAN Controller 1 Mailbox 20 Acceptance Mask Low Register */
#define                       CAN1_AM21L  0xffc033a8   /* CAN Controller 1 Mailbox 21 Acceptance Mask High Register */
#define                       CAN1_AM21H  0xffc033ac   /* CAN Controller 1 Mailbox 21 Acceptance Mask Low Register */
#define                       CAN1_AM22L  0xffc033b0   /* CAN Controller 1 Mailbox 22 Acceptance Mask High Register */
#define                       CAN1_AM22H  0xffc033b4   /* CAN Controller 1 Mailbox 22 Acceptance Mask Low Register */
#define                       CAN1_AM23L  0xffc033b8   /* CAN Controller 1 Mailbox 23 Acceptance Mask High Register */
#define                       CAN1_AM23H  0xffc033bc   /* CAN Controller 1 Mailbox 23 Acceptance Mask Low Register */
#define                       CAN1_AM24L  0xffc033c0   /* CAN Controller 1 Mailbox 24 Acceptance Mask High Register */
#define                       CAN1_AM24H  0xffc033c4   /* CAN Controller 1 Mailbox 24 Acceptance Mask Low Register */
#define                       CAN1_AM25L  0xffc033c8   /* CAN Controller 1 Mailbox 25 Acceptance Mask High Register */
#define                       CAN1_AM25H  0xffc033cc   /* CAN Controller 1 Mailbox 25 Acceptance Mask Low Register */
#define                       CAN1_AM26L  0xffc033d0   /* CAN Controller 1 Mailbox 26 Acceptance Mask High Register */
#define                       CAN1_AM26H  0xffc033d4   /* CAN Controller 1 Mailbox 26 Acceptance Mask Low Register */
#define                       CAN1_AM27L  0xffc033d8   /* CAN Controller 1 Mailbox 27 Acceptance Mask High Register */
#define                       CAN1_AM27H  0xffc033dc   /* CAN Controller 1 Mailbox 27 Acceptance Mask Low Register */
#define                       CAN1_AM28L  0xffc033e0   /* CAN Controller 1 Mailbox 28 Acceptance Mask High Register */
#define                       CAN1_AM28H  0xffc033e4   /* CAN Controller 1 Mailbox 28 Acceptance Mask Low Register */
#define                       CAN1_AM29L  0xffc033e8   /* CAN Controller 1 Mailbox 29 Acceptance Mask High Register */
#define                       CAN1_AM29H  0xffc033ec   /* CAN Controller 1 Mailbox 29 Acceptance Mask Low Register */
#define                       CAN1_AM30L  0xffc033f0   /* CAN Controller 1 Mailbox 30 Acceptance Mask High Register */
#define                       CAN1_AM30H  0xffc033f4   /* CAN Controller 1 Mailbox 30 Acceptance Mask Low Register */
#define                       CAN1_AM31L  0xffc033f8   /* CAN Controller 1 Mailbox 31 Acceptance Mask High Register */
#define                       CAN1_AM31H  0xffc033fc   /* CAN Controller 1 Mailbox 31 Acceptance Mask Low Register */

/* CAN Controller 1 Mailbox Data Registers */

#define                  CAN1_MB00_DATA0  0xffc03400   /* CAN Controller 1 Mailbox 0 Data 0 Register */
#define                  CAN1_MB00_DATA1  0xffc03404   /* CAN Controller 1 Mailbox 0 Data 1 Register */
#define                  CAN1_MB00_DATA2  0xffc03408   /* CAN Controller 1 Mailbox 0 Data 2 Register */
#define                  CAN1_MB00_DATA3  0xffc0340c   /* CAN Controller 1 Mailbox 0 Data 3 Register */
#define                 CAN1_MB00_LENGTH  0xffc03410   /* CAN Controller 1 Mailbox 0 Length Register */
#define              CAN1_MB00_TIMESTAMP  0xffc03414   /* CAN Controller 1 Mailbox 0 Timestamp Register */
#define                    CAN1_MB00_ID0  0xffc03418   /* CAN Controller 1 Mailbox 0 ID0 Register */
#define                    CAN1_MB00_ID1  0xffc0341c   /* CAN Controller 1 Mailbox 0 ID1 Register */
#define                  CAN1_MB01_DATA0  0xffc03420   /* CAN Controller 1 Mailbox 1 Data 0 Register */
#define                  CAN1_MB01_DATA1  0xffc03424   /* CAN Controller 1 Mailbox 1 Data 1 Register */
#define                  CAN1_MB01_DATA2  0xffc03428   /* CAN Controller 1 Mailbox 1 Data 2 Register */
#define                  CAN1_MB01_DATA3  0xffc0342c   /* CAN Controller 1 Mailbox 1 Data 3 Register */
#define                 CAN1_MB01_LENGTH  0xffc03430   /* CAN Controller 1 Mailbox 1 Length Register */
#define              CAN1_MB01_TIMESTAMP  0xffc03434   /* CAN Controller 1 Mailbox 1 Timestamp Register */
#define                    CAN1_MB01_ID0  0xffc03438   /* CAN Controller 1 Mailbox 1 ID0 Register */
#define                    CAN1_MB01_ID1  0xffc0343c   /* CAN Controller 1 Mailbox 1 ID1 Register */
#define                  CAN1_MB02_DATA0  0xffc03440   /* CAN Controller 1 Mailbox 2 Data 0 Register */
#define                  CAN1_MB02_DATA1  0xffc03444   /* CAN Controller 1 Mailbox 2 Data 1 Register */
#define                  CAN1_MB02_DATA2  0xffc03448   /* CAN Controller 1 Mailbox 2 Data 2 Register */
#define                  CAN1_MB02_DATA3  0xffc0344c   /* CAN Controller 1 Mailbox 2 Data 3 Register */
#define                 CAN1_MB02_LENGTH  0xffc03450   /* CAN Controller 1 Mailbox 2 Length Register */
#define              CAN1_MB02_TIMESTAMP  0xffc03454   /* CAN Controller 1 Mailbox 2 Timestamp Register */
#define                    CAN1_MB02_ID0  0xffc03458   /* CAN Controller 1 Mailbox 2 ID0 Register */
#define                    CAN1_MB02_ID1  0xffc0345c   /* CAN Controller 1 Mailbox 2 ID1 Register */
#define                  CAN1_MB03_DATA0  0xffc03460   /* CAN Controller 1 Mailbox 3 Data 0 Register */
#define                  CAN1_MB03_DATA1  0xffc03464   /* CAN Controller 1 Mailbox 3 Data 1 Register */
#define                  CAN1_MB03_DATA2  0xffc03468   /* CAN Controller 1 Mailbox 3 Data 2 Register */
#define                  CAN1_MB03_DATA3  0xffc0346c   /* CAN Controller 1 Mailbox 3 Data 3 Register */
#define                 CAN1_MB03_LENGTH  0xffc03470   /* CAN Controller 1 Mailbox 3 Length Register */
#define              CAN1_MB03_TIMESTAMP  0xffc03474   /* CAN Controller 1 Mailbox 3 Timestamp Register */
#define                    CAN1_MB03_ID0  0xffc03478   /* CAN Controller 1 Mailbox 3 ID0 Register */
#define                    CAN1_MB03_ID1  0xffc0347c   /* CAN Controller 1 Mailbox 3 ID1 Register */
#define                  CAN1_MB04_DATA0  0xffc03480   /* CAN Controller 1 Mailbox 4 Data 0 Register */
#define                  CAN1_MB04_DATA1  0xffc03484   /* CAN Controller 1 Mailbox 4 Data 1 Register */
#define                  CAN1_MB04_DATA2  0xffc03488   /* CAN Controller 1 Mailbox 4 Data 2 Register */
#define                  CAN1_MB04_DATA3  0xffc0348c   /* CAN Controller 1 Mailbox 4 Data 3 Register */
#define                 CAN1_MB04_LENGTH  0xffc03490   /* CAN Controller 1 Mailbox 4 Length Register */
#define              CAN1_MB04_TIMESTAMP  0xffc03494   /* CAN Controller 1 Mailbox 4 Timestamp Register */
#define                    CAN1_MB04_ID0  0xffc03498   /* CAN Controller 1 Mailbox 4 ID0 Register */
#define                    CAN1_MB04_ID1  0xffc0349c   /* CAN Controller 1 Mailbox 4 ID1 Register */
#define                  CAN1_MB05_DATA0  0xffc034a0   /* CAN Controller 1 Mailbox 5 Data 0 Register */
#define                  CAN1_MB05_DATA1  0xffc034a4   /* CAN Controller 1 Mailbox 5 Data 1 Register */
#define                  CAN1_MB05_DATA2  0xffc034a8   /* CAN Controller 1 Mailbox 5 Data 2 Register */
#define                  CAN1_MB05_DATA3  0xffc034ac   /* CAN Controller 1 Mailbox 5 Data 3 Register */
#define                 CAN1_MB05_LENGTH  0xffc034b0   /* CAN Controller 1 Mailbox 5 Length Register */
#define              CAN1_MB05_TIMESTAMP  0xffc034b4   /* CAN Controller 1 Mailbox 5 Timestamp Register */
#define                    CAN1_MB05_ID0  0xffc034b8   /* CAN Controller 1 Mailbox 5 ID0 Register */
#define                    CAN1_MB05_ID1  0xffc034bc   /* CAN Controller 1 Mailbox 5 ID1 Register */
#define                  CAN1_MB06_DATA0  0xffc034c0   /* CAN Controller 1 Mailbox 6 Data 0 Register */
#define                  CAN1_MB06_DATA1  0xffc034c4   /* CAN Controller 1 Mailbox 6 Data 1 Register */
#define                  CAN1_MB06_DATA2  0xffc034c8   /* CAN Controller 1 Mailbox 6 Data 2 Register */
#define                  CAN1_MB06_DATA3  0xffc034cc   /* CAN Controller 1 Mailbox 6 Data 3 Register */
#define                 CAN1_MB06_LENGTH  0xffc034d0   /* CAN Controller 1 Mailbox 6 Length Register */
#define              CAN1_MB06_TIMESTAMP  0xffc034d4   /* CAN Controller 1 Mailbox 6 Timestamp Register */
#define                    CAN1_MB06_ID0  0xffc034d8   /* CAN Controller 1 Mailbox 6 ID0 Register */
#define                    CAN1_MB06_ID1  0xffc034dc   /* CAN Controller 1 Mailbox 6 ID1 Register */
#define                  CAN1_MB07_DATA0  0xffc034e0   /* CAN Controller 1 Mailbox 7 Data 0 Register */
#define                  CAN1_MB07_DATA1  0xffc034e4   /* CAN Controller 1 Mailbox 7 Data 1 Register */
#define                  CAN1_MB07_DATA2  0xffc034e8   /* CAN Controller 1 Mailbox 7 Data 2 Register */
#define                  CAN1_MB07_DATA3  0xffc034ec   /* CAN Controller 1 Mailbox 7 Data 3 Register */
#define                 CAN1_MB07_LENGTH  0xffc034f0   /* CAN Controller 1 Mailbox 7 Length Register */
#define              CAN1_MB07_TIMESTAMP  0xffc034f4   /* CAN Controller 1 Mailbox 7 Timestamp Register */
#define                    CAN1_MB07_ID0  0xffc034f8   /* CAN Controller 1 Mailbox 7 ID0 Register */
#define                    CAN1_MB07_ID1  0xffc034fc   /* CAN Controller 1 Mailbox 7 ID1 Register */
#define                  CAN1_MB08_DATA0  0xffc03500   /* CAN Controller 1 Mailbox 8 Data 0 Register */
#define                  CAN1_MB08_DATA1  0xffc03504   /* CAN Controller 1 Mailbox 8 Data 1 Register */
#define                  CAN1_MB08_DATA2  0xffc03508   /* CAN Controller 1 Mailbox 8 Data 2 Register */
#define                  CAN1_MB08_DATA3  0xffc0350c   /* CAN Controller 1 Mailbox 8 Data 3 Register */
#define                 CAN1_MB08_LENGTH  0xffc03510   /* CAN Controller 1 Mailbox 8 Length Register */
#define              CAN1_MB08_TIMESTAMP  0xffc03514   /* CAN Controller 1 Mailbox 8 Timestamp Register */
#define                    CAN1_MB08_ID0  0xffc03518   /* CAN Controller 1 Mailbox 8 ID0 Register */
#define                    CAN1_MB08_ID1  0xffc0351c   /* CAN Controller 1 Mailbox 8 ID1 Register */
#define                  CAN1_MB09_DATA0  0xffc03520   /* CAN Controller 1 Mailbox 9 Data 0 Register */
#define                  CAN1_MB09_DATA1  0xffc03524   /* CAN Controller 1 Mailbox 9 Data 1 Register */
#define                  CAN1_MB09_DATA2  0xffc03528   /* CAN Controller 1 Mailbox 9 Data 2 Register */
#define                  CAN1_MB09_DATA3  0xffc0352c   /* CAN Controller 1 Mailbox 9 Data 3 Register */
#define                 CAN1_MB09_LENGTH  0xffc03530   /* CAN Controller 1 Mailbox 9 Length Register */
#define              CAN1_MB09_TIMESTAMP  0xffc03534   /* CAN Controller 1 Mailbox 9 Timestamp Register */
#define                    CAN1_MB09_ID0  0xffc03538   /* CAN Controller 1 Mailbox 9 ID0 Register */
#define                    CAN1_MB09_ID1  0xffc0353c   /* CAN Controller 1 Mailbox 9 ID1 Register */
#define                  CAN1_MB10_DATA0  0xffc03540   /* CAN Controller 1 Mailbox 10 Data 0 Register */
#define                  CAN1_MB10_DATA1  0xffc03544   /* CAN Controller 1 Mailbox 10 Data 1 Register */
#define                  CAN1_MB10_DATA2  0xffc03548   /* CAN Controller 1 Mailbox 10 Data 2 Register */
#define                  CAN1_MB10_DATA3  0xffc0354c   /* CAN Controller 1 Mailbox 10 Data 3 Register */
#define                 CAN1_MB10_LENGTH  0xffc03550   /* CAN Controller 1 Mailbox 10 Length Register */
#define              CAN1_MB10_TIMESTAMP  0xffc03554   /* CAN Controller 1 Mailbox 10 Timestamp Register */
#define                    CAN1_MB10_ID0  0xffc03558   /* CAN Controller 1 Mailbox 10 ID0 Register */
#define                    CAN1_MB10_ID1  0xffc0355c   /* CAN Controller 1 Mailbox 10 ID1 Register */
#define                  CAN1_MB11_DATA0  0xffc03560   /* CAN Controller 1 Mailbox 11 Data 0 Register */
#define                  CAN1_MB11_DATA1  0xffc03564   /* CAN Controller 1 Mailbox 11 Data 1 Register */
#define                  CAN1_MB11_DATA2  0xffc03568   /* CAN Controller 1 Mailbox 11 Data 2 Register */
#define                  CAN1_MB11_DATA3  0xffc0356c   /* CAN Controller 1 Mailbox 11 Data 3 Register */
#define                 CAN1_MB11_LENGTH  0xffc03570   /* CAN Controller 1 Mailbox 11 Length Register */
#define              CAN1_MB11_TIMESTAMP  0xffc03574   /* CAN Controller 1 Mailbox 11 Timestamp Register */
#define                    CAN1_MB11_ID0  0xffc03578   /* CAN Controller 1 Mailbox 11 ID0 Register */
#define                    CAN1_MB11_ID1  0xffc0357c   /* CAN Controller 1 Mailbox 11 ID1 Register */
#define                  CAN1_MB12_DATA0  0xffc03580   /* CAN Controller 1 Mailbox 12 Data 0 Register */
#define                  CAN1_MB12_DATA1  0xffc03584   /* CAN Controller 1 Mailbox 12 Data 1 Register */
#define                  CAN1_MB12_DATA2  0xffc03588   /* CAN Controller 1 Mailbox 12 Data 2 Register */
#define                  CAN1_MB12_DATA3  0xffc0358c   /* CAN Controller 1 Mailbox 12 Data 3 Register */
#define                 CAN1_MB12_LENGTH  0xffc03590   /* CAN Controller 1 Mailbox 12 Length Register */
#define              CAN1_MB12_TIMESTAMP  0xffc03594   /* CAN Controller 1 Mailbox 12 Timestamp Register */
#define                    CAN1_MB12_ID0  0xffc03598   /* CAN Controller 1 Mailbox 12 ID0 Register */
#define                    CAN1_MB12_ID1  0xffc0359c   /* CAN Controller 1 Mailbox 12 ID1 Register */
#define                  CAN1_MB13_DATA0  0xffc035a0   /* CAN Controller 1 Mailbox 13 Data 0 Register */
#define                  CAN1_MB13_DATA1  0xffc035a4   /* CAN Controller 1 Mailbox 13 Data 1 Register */
#define                  CAN1_MB13_DATA2  0xffc035a8   /* CAN Controller 1 Mailbox 13 Data 2 Register */
#define                  CAN1_MB13_DATA3  0xffc035ac   /* CAN Controller 1 Mailbox 13 Data 3 Register */
#define                 CAN1_MB13_LENGTH  0xffc035b0   /* CAN Controller 1 Mailbox 13 Length Register */
#define              CAN1_MB13_TIMESTAMP  0xffc035b4   /* CAN Controller 1 Mailbox 13 Timestamp Register */
#define                    CAN1_MB13_ID0  0xffc035b8   /* CAN Controller 1 Mailbox 13 ID0 Register */
#define                    CAN1_MB13_ID1  0xffc035bc   /* CAN Controller 1 Mailbox 13 ID1 Register */
#define                  CAN1_MB14_DATA0  0xffc035c0   /* CAN Controller 1 Mailbox 14 Data 0 Register */
#define                  CAN1_MB14_DATA1  0xffc035c4   /* CAN Controller 1 Mailbox 14 Data 1 Register */
#define                  CAN1_MB14_DATA2  0xffc035c8   /* CAN Controller 1 Mailbox 14 Data 2 Register */
#define                  CAN1_MB14_DATA3  0xffc035cc   /* CAN Controller 1 Mailbox 14 Data 3 Register */
#define                 CAN1_MB14_LENGTH  0xffc035d0   /* CAN Controller 1 Mailbox 14 Length Register */
#define              CAN1_MB14_TIMESTAMP  0xffc035d4   /* CAN Controller 1 Mailbox 14 Timestamp Register */
#define                    CAN1_MB14_ID0  0xffc035d8   /* CAN Controller 1 Mailbox 14 ID0 Register */
#define                    CAN1_MB14_ID1  0xffc035dc   /* CAN Controller 1 Mailbox 14 ID1 Register */
#define                  CAN1_MB15_DATA0  0xffc035e0   /* CAN Controller 1 Mailbox 15 Data 0 Register */
#define                  CAN1_MB15_DATA1  0xffc035e4   /* CAN Controller 1 Mailbox 15 Data 1 Register */
#define                  CAN1_MB15_DATA2  0xffc035e8   /* CAN Controller 1 Mailbox 15 Data 2 Register */
#define                  CAN1_MB15_DATA3  0xffc035ec   /* CAN Controller 1 Mailbox 15 Data 3 Register */
#define                 CAN1_MB15_LENGTH  0xffc035f0   /* CAN Controller 1 Mailbox 15 Length Register */
#define              CAN1_MB15_TIMESTAMP  0xffc035f4   /* CAN Controller 1 Mailbox 15 Timestamp Register */
#define                    CAN1_MB15_ID0  0xffc035f8   /* CAN Controller 1 Mailbox 15 ID0 Register */
#define                    CAN1_MB15_ID1  0xffc035fc   /* CAN Controller 1 Mailbox 15 ID1 Register */

/* CAN Controller 1 Mailbox Data Registers */

#define                  CAN1_MB16_DATA0  0xffc03600   /* CAN Controller 1 Mailbox 16 Data 0 Register */
#define                  CAN1_MB16_DATA1  0xffc03604   /* CAN Controller 1 Mailbox 16 Data 1 Register */
#define                  CAN1_MB16_DATA2  0xffc03608   /* CAN Controller 1 Mailbox 16 Data 2 Register */
#define                  CAN1_MB16_DATA3  0xffc0360c   /* CAN Controller 1 Mailbox 16 Data 3 Register */
#define                 CAN1_MB16_LENGTH  0xffc03610   /* CAN Controller 1 Mailbox 16 Length Register */
#define              CAN1_MB16_TIMESTAMP  0xffc03614   /* CAN Controller 1 Mailbox 16 Timestamp Register */
#define                    CAN1_MB16_ID0  0xffc03618   /* CAN Controller 1 Mailbox 16 ID0 Register */
#define                    CAN1_MB16_ID1  0xffc0361c   /* CAN Controller 1 Mailbox 16 ID1 Register */
#define                  CAN1_MB17_DATA0  0xffc03620   /* CAN Controller 1 Mailbox 17 Data 0 Register */
#define                  CAN1_MB17_DATA1  0xffc03624   /* CAN Controller 1 Mailbox 17 Data 1 Register */
#define                  CAN1_MB17_DATA2  0xffc03628   /* CAN Controller 1 Mailbox 17 Data 2 Register */
#define                  CAN1_MB17_DATA3  0xffc0362c   /* CAN Controller 1 Mailbox 17 Data 3 Register */
#define                 CAN1_MB17_LENGTH  0xffc03630   /* CAN Controller 1 Mailbox 17 Length Register */
#define              CAN1_MB17_TIMESTAMP  0xffc03634   /* CAN Controller 1 Mailbox 17 Timestamp Register */
#define                    CAN1_MB17_ID0  0xffc03638   /* CAN Controller 1 Mailbox 17 ID0 Register */
#define                    CAN1_MB17_ID1  0xffc0363c   /* CAN Controller 1 Mailbox 17 ID1 Register */
#define                  CAN1_MB18_DATA0  0xffc03640   /* CAN Controller 1 Mailbox 18 Data 0 Register */
#define                  CAN1_MB18_DATA1  0xffc03644   /* CAN Controller 1 Mailbox 18 Data 1 Register */
#define                  CAN1_MB18_DATA2  0xffc03648   /* CAN Controller 1 Mailbox 18 Data 2 Register */
#define                  CAN1_MB18_DATA3  0xffc0364c   /* CAN Controller 1 Mailbox 18 Data 3 Register */
#define                 CAN1_MB18_LENGTH  0xffc03650   /* CAN Controller 1 Mailbox 18 Length Register */
#define              CAN1_MB18_TIMESTAMP  0xffc03654   /* CAN Controller 1 Mailbox 18 Timestamp Register */
#define                    CAN1_MB18_ID0  0xffc03658   /* CAN Controller 1 Mailbox 18 ID0 Register */
#define                    CAN1_MB18_ID1  0xffc0365c   /* CAN Controller 1 Mailbox 18 ID1 Register */
#define                  CAN1_MB19_DATA0  0xffc03660   /* CAN Controller 1 Mailbox 19 Data 0 Register */
#define                  CAN1_MB19_DATA1  0xffc03664   /* CAN Controller 1 Mailbox 19 Data 1 Register */
#define                  CAN1_MB19_DATA2  0xffc03668   /* CAN Controller 1 Mailbox 19 Data 2 Register */
#define                  CAN1_MB19_DATA3  0xffc0366c   /* CAN Controller 1 Mailbox 19 Data 3 Register */
#define                 CAN1_MB19_LENGTH  0xffc03670   /* CAN Controller 1 Mailbox 19 Length Register */
#define              CAN1_MB19_TIMESTAMP  0xffc03674   /* CAN Controller 1 Mailbox 19 Timestamp Register */
#define                    CAN1_MB19_ID0  0xffc03678   /* CAN Controller 1 Mailbox 19 ID0 Register */
#define                    CAN1_MB19_ID1  0xffc0367c   /* CAN Controller 1 Mailbox 19 ID1 Register */
#define                  CAN1_MB20_DATA0  0xffc03680   /* CAN Controller 1 Mailbox 20 Data 0 Register */
#define                  CAN1_MB20_DATA1  0xffc03684   /* CAN Controller 1 Mailbox 20 Data 1 Register */
#define                  CAN1_MB20_DATA2  0xffc03688   /* CAN Controller 1 Mailbox 20 Data 2 Register */
#define                  CAN1_MB20_DATA3  0xffc0368c   /* CAN Controller 1 Mailbox 20 Data 3 Register */
#define                 CAN1_MB20_LENGTH  0xffc03690   /* CAN Controller 1 Mailbox 20 Length Register */
#define              CAN1_MB20_TIMESTAMP  0xffc03694   /* CAN Controller 1 Mailbox 20 Timestamp Register */
#define                    CAN1_MB20_ID0  0xffc03698   /* CAN Controller 1 Mailbox 20 ID0 Register */
#define                    CAN1_MB20_ID1  0xffc0369c   /* CAN Controller 1 Mailbox 20 ID1 Register */
#define                  CAN1_MB21_DATA0  0xffc036a0   /* CAN Controller 1 Mailbox 21 Data 0 Register */
#define                  CAN1_MB21_DATA1  0xffc036a4   /* CAN Controller 1 Mailbox 21 Data 1 Register */
#define                  CAN1_MB21_DATA2  0xffc036a8   /* CAN Controller 1 Mailbox 21 Data 2 Register */
#define                  CAN1_MB21_DATA3  0xffc036ac   /* CAN Controller 1 Mailbox 21 Data 3 Register */
#define                 CAN1_MB21_LENGTH  0xffc036b0   /* CAN Controller 1 Mailbox 21 Length Register */
#define              CAN1_MB21_TIMESTAMP  0xffc036b4   /* CAN Controller 1 Mailbox 21 Timestamp Register */
#define                    CAN1_MB21_ID0  0xffc036b8   /* CAN Controller 1 Mailbox 21 ID0 Register */
#define                    CAN1_MB21_ID1  0xffc036bc   /* CAN Controller 1 Mailbox 21 ID1 Register */
#define                  CAN1_MB22_DATA0  0xffc036c0   /* CAN Controller 1 Mailbox 22 Data 0 Register */
#define                  CAN1_MB22_DATA1  0xffc036c4   /* CAN Controller 1 Mailbox 22 Data 1 Register */
#define                  CAN1_MB22_DATA2  0xffc036c8   /* CAN Controller 1 Mailbox 22 Data 2 Register */
#define                  CAN1_MB22_DATA3  0xffc036cc   /* CAN Controller 1 Mailbox 22 Data 3 Register */
#define                 CAN1_MB22_LENGTH  0xffc036d0   /* CAN Controller 1 Mailbox 22 Length Register */
#define              CAN1_MB22_TIMESTAMP  0xffc036d4   /* CAN Controller 1 Mailbox 22 Timestamp Register */
#define                    CAN1_MB22_ID0  0xffc036d8   /* CAN Controller 1 Mailbox 22 ID0 Register */
#define                    CAN1_MB22_ID1  0xffc036dc   /* CAN Controller 1 Mailbox 22 ID1 Register */
#define                  CAN1_MB23_DATA0  0xffc036e0   /* CAN Controller 1 Mailbox 23 Data 0 Register */
#define                  CAN1_MB23_DATA1  0xffc036e4   /* CAN Controller 1 Mailbox 23 Data 1 Register */
#define                  CAN1_MB23_DATA2  0xffc036e8   /* CAN Controller 1 Mailbox 23 Data 2 Register */
#define                  CAN1_MB23_DATA3  0xffc036ec   /* CAN Controller 1 Mailbox 23 Data 3 Register */
#define                 CAN1_MB23_LENGTH  0xffc036f0   /* CAN Controller 1 Mailbox 23 Length Register */
#define              CAN1_MB23_TIMESTAMP  0xffc036f4   /* CAN Controller 1 Mailbox 23 Timestamp Register */
#define                    CAN1_MB23_ID0  0xffc036f8   /* CAN Controller 1 Mailbox 23 ID0 Register */
#define                    CAN1_MB23_ID1  0xffc036fc   /* CAN Controller 1 Mailbox 23 ID1 Register */
#define                  CAN1_MB24_DATA0  0xffc03700   /* CAN Controller 1 Mailbox 24 Data 0 Register */
#define                  CAN1_MB24_DATA1  0xffc03704   /* CAN Controller 1 Mailbox 24 Data 1 Register */
#define                  CAN1_MB24_DATA2  0xffc03708   /* CAN Controller 1 Mailbox 24 Data 2 Register */
#define                  CAN1_MB24_DATA3  0xffc0370c   /* CAN Controller 1 Mailbox 24 Data 3 Register */
#define                 CAN1_MB24_LENGTH  0xffc03710   /* CAN Controller 1 Mailbox 24 Length Register */
#define              CAN1_MB24_TIMESTAMP  0xffc03714   /* CAN Controller 1 Mailbox 24 Timestamp Register */
#define                    CAN1_MB24_ID0  0xffc03718   /* CAN Controller 1 Mailbox 24 ID0 Register */
#define                    CAN1_MB24_ID1  0xffc0371c   /* CAN Controller 1 Mailbox 24 ID1 Register */
#define                  CAN1_MB25_DATA0  0xffc03720   /* CAN Controller 1 Mailbox 25 Data 0 Register */
#define                  CAN1_MB25_DATA1  0xffc03724   /* CAN Controller 1 Mailbox 25 Data 1 Register */
#define                  CAN1_MB25_DATA2  0xffc03728   /* CAN Controller 1 Mailbox 25 Data 2 Register */
#define                  CAN1_MB25_DATA3  0xffc0372c   /* CAN Controller 1 Mailbox 25 Data 3 Register */
#define                 CAN1_MB25_LENGTH  0xffc03730   /* CAN Controller 1 Mailbox 25 Length Register */
#define              CAN1_MB25_TIMESTAMP  0xffc03734   /* CAN Controller 1 Mailbox 25 Timestamp Register */
#define                    CAN1_MB25_ID0  0xffc03738   /* CAN Controller 1 Mailbox 25 ID0 Register */
#define                    CAN1_MB25_ID1  0xffc0373c   /* CAN Controller 1 Mailbox 25 ID1 Register */
#define                  CAN1_MB26_DATA0  0xffc03740   /* CAN Controller 1 Mailbox 26 Data 0 Register */
#define                  CAN1_MB26_DATA1  0xffc03744   /* CAN Controller 1 Mailbox 26 Data 1 Register */
#define                  CAN1_MB26_DATA2  0xffc03748   /* CAN Controller 1 Mailbox 26 Data 2 Register */
#define                  CAN1_MB26_DATA3  0xffc0374c   /* CAN Controller 1 Mailbox 26 Data 3 Register */
#define                 CAN1_MB26_LENGTH  0xffc03750   /* CAN Controller 1 Mailbox 26 Length Register */
#define              CAN1_MB26_TIMESTAMP  0xffc03754   /* CAN Controller 1 Mailbox 26 Timestamp Register */
#define                    CAN1_MB26_ID0  0xffc03758   /* CAN Controller 1 Mailbox 26 ID0 Register */
#define                    CAN1_MB26_ID1  0xffc0375c   /* CAN Controller 1 Mailbox 26 ID1 Register */
#define                  CAN1_MB27_DATA0  0xffc03760   /* CAN Controller 1 Mailbox 27 Data 0 Register */
#define                  CAN1_MB27_DATA1  0xffc03764   /* CAN Controller 1 Mailbox 27 Data 1 Register */
#define                  CAN1_MB27_DATA2  0xffc03768   /* CAN Controller 1 Mailbox 27 Data 2 Register */
#define                  CAN1_MB27_DATA3  0xffc0376c   /* CAN Controller 1 Mailbox 27 Data 3 Register */
#define                 CAN1_MB27_LENGTH  0xffc03770   /* CAN Controller 1 Mailbox 27 Length Register */
#define              CAN1_MB27_TIMESTAMP  0xffc03774   /* CAN Controller 1 Mailbox 27 Timestamp Register */
#define                    CAN1_MB27_ID0  0xffc03778   /* CAN Controller 1 Mailbox 27 ID0 Register */
#define                    CAN1_MB27_ID1  0xffc0377c   /* CAN Controller 1 Mailbox 27 ID1 Register */
#define                  CAN1_MB28_DATA0  0xffc03780   /* CAN Controller 1 Mailbox 28 Data 0 Register */
#define                  CAN1_MB28_DATA1  0xffc03784   /* CAN Controller 1 Mailbox 28 Data 1 Register */
#define                  CAN1_MB28_DATA2  0xffc03788   /* CAN Controller 1 Mailbox 28 Data 2 Register */
#define                  CAN1_MB28_DATA3  0xffc0378c   /* CAN Controller 1 Mailbox 28 Data 3 Register */
#define                 CAN1_MB28_LENGTH  0xffc03790   /* CAN Controller 1 Mailbox 28 Length Register */
#define              CAN1_MB28_TIMESTAMP  0xffc03794   /* CAN Controller 1 Mailbox 28 Timestamp Register */
#define                    CAN1_MB28_ID0  0xffc03798   /* CAN Controller 1 Mailbox 28 ID0 Register */
#define                    CAN1_MB28_ID1  0xffc0379c   /* CAN Controller 1 Mailbox 28 ID1 Register */
#define                  CAN1_MB29_DATA0  0xffc037a0   /* CAN Controller 1 Mailbox 29 Data 0 Register */
#define                  CAN1_MB29_DATA1  0xffc037a4   /* CAN Controller 1 Mailbox 29 Data 1 Register */
#define                  CAN1_MB29_DATA2  0xffc037a8   /* CAN Controller 1 Mailbox 29 Data 2 Register */
#define                  CAN1_MB29_DATA3  0xffc037ac   /* CAN Controller 1 Mailbox 29 Data 3 Register */
#define                 CAN1_MB29_LENGTH  0xffc037b0   /* CAN Controller 1 Mailbox 29 Length Register */
#define              CAN1_MB29_TIMESTAMP  0xffc037b4   /* CAN Controller 1 Mailbox 29 Timestamp Register */
#define                    CAN1_MB29_ID0  0xffc037b8   /* CAN Controller 1 Mailbox 29 ID0 Register */
#define                    CAN1_MB29_ID1  0xffc037bc   /* CAN Controller 1 Mailbox 29 ID1 Register */
#define                  CAN1_MB30_DATA0  0xffc037c0   /* CAN Controller 1 Mailbox 30 Data 0 Register */
#define                  CAN1_MB30_DATA1  0xffc037c4   /* CAN Controller 1 Mailbox 30 Data 1 Register */
#define                  CAN1_MB30_DATA2  0xffc037c8   /* CAN Controller 1 Mailbox 30 Data 2 Register */
#define                  CAN1_MB30_DATA3  0xffc037cc   /* CAN Controller 1 Mailbox 30 Data 3 Register */
#define                 CAN1_MB30_LENGTH  0xffc037d0   /* CAN Controller 1 Mailbox 30 Length Register */
#define              CAN1_MB30_TIMESTAMP  0xffc037d4   /* CAN Controller 1 Mailbox 30 Timestamp Register */
#define                    CAN1_MB30_ID0  0xffc037d8   /* CAN Controller 1 Mailbox 30 ID0 Register */
#define                    CAN1_MB30_ID1  0xffc037dc   /* CAN Controller 1 Mailbox 30 ID1 Register */
#define                  CAN1_MB31_DATA0  0xffc037e0   /* CAN Controller 1 Mailbox 31 Data 0 Register */
#define                  CAN1_MB31_DATA1  0xffc037e4   /* CAN Controller 1 Mailbox 31 Data 1 Register */
#define                  CAN1_MB31_DATA2  0xffc037e8   /* CAN Controller 1 Mailbox 31 Data 2 Register */
#define                  CAN1_MB31_DATA3  0xffc037ec   /* CAN Controller 1 Mailbox 31 Data 3 Register */
#define                 CAN1_MB31_LENGTH  0xffc037f0   /* CAN Controller 1 Mailbox 31 Length Register */
#define              CAN1_MB31_TIMESTAMP  0xffc037f4   /* CAN Controller 1 Mailbox 31 Timestamp Register */
#define                    CAN1_MB31_ID0  0xffc037f8   /* CAN Controller 1 Mailbox 31 ID0 Register */
#define                    CAN1_MB31_ID1  0xffc037fc   /* CAN Controller 1 Mailbox 31 ID1 Register */

/* ATAPI Registers */

#define                    ATAPI_CONTROL  0xffc03800   /* ATAPI Control Register */
#define                     ATAPI_STATUS  0xffc03804   /* ATAPI Status Register */
#define                   ATAPI_DEV_ADDR  0xffc03808   /* ATAPI Device Register Address */
#define                  ATAPI_DEV_TXBUF  0xffc0380c   /* ATAPI Device Register Write Data */
#define                  ATAPI_DEV_RXBUF  0xffc03810   /* ATAPI Device Register Read Data */
#define                   ATAPI_INT_MASK  0xffc03814   /* ATAPI Interrupt Mask Register */
#define                 ATAPI_INT_STATUS  0xffc03818   /* ATAPI Interrupt Status Register */
#define                   ATAPI_XFER_LEN  0xffc0381c   /* ATAPI Length of Transfer */
#define                ATAPI_LINE_STATUS  0xffc03820   /* ATAPI Line Status */
#define                   ATAPI_SM_STATE  0xffc03824   /* ATAPI State Machine Status */
#define                  ATAPI_TERMINATE  0xffc03828   /* ATAPI Host Terminate */
#define                 ATAPI_PIO_TFRCNT  0xffc0382c   /* ATAPI PIO mode transfer count */
#define                 ATAPI_DMA_TFRCNT  0xffc03830   /* ATAPI DMA mode transfer count */
#define               ATAPI_UMAIN_TFRCNT  0xffc03834   /* ATAPI UDMAIN transfer count */
#define             ATAPI_UDMAOUT_TFRCNT  0xffc03838   /* ATAPI UDMAOUT transfer count */
#define                  ATAPI_REG_TIM_0  0xffc03840   /* ATAPI Register Transfer Timing 0 */
#define                  ATAPI_PIO_TIM_0  0xffc03844   /* ATAPI PIO Timing 0 Register */
#define                  ATAPI_PIO_TIM_1  0xffc03848   /* ATAPI PIO Timing 1 Register */
#define                ATAPI_MULTI_TIM_0  0xffc03850   /* ATAPI Multi-DMA Timing 0 Register */
#define                ATAPI_MULTI_TIM_1  0xffc03854   /* ATAPI Multi-DMA Timing 1 Register */
#define                ATAPI_MULTI_TIM_2  0xffc03858   /* ATAPI Multi-DMA Timing 2 Register */
#define                ATAPI_ULTRA_TIM_0  0xffc03860   /* ATAPI Ultra-DMA Timing 0 Register */
#define                ATAPI_ULTRA_TIM_1  0xffc03864   /* ATAPI Ultra-DMA Timing 1 Register */
#define                ATAPI_ULTRA_TIM_2  0xffc03868   /* ATAPI Ultra-DMA Timing 2 Register */
#define                ATAPI_ULTRA_TIM_3  0xffc0386c   /* ATAPI Ultra-DMA Timing 3 Register */

/* SDH Registers */

#define                      SDH_PWR_CTL  0xffc03900   /* SDH Power Control */
#define                      SDH_CLK_CTL  0xffc03904   /* SDH Clock Control */
#define                     SDH_ARGUMENT  0xffc03908   /* SDH Argument */
#define                      SDH_COMMAND  0xffc0390c   /* SDH Command */
#define                     SDH_RESP_CMD  0xffc03910   /* SDH Response Command */
#define                    SDH_RESPONSE0  0xffc03914   /* SDH Response0 */
#define                    SDH_RESPONSE1  0xffc03918   /* SDH Response1 */
#define                    SDH_RESPONSE2  0xffc0391c   /* SDH Response2 */
#define                    SDH_RESPONSE3  0xffc03920   /* SDH Response3 */
#define                   SDH_DATA_TIMER  0xffc03924   /* SDH Data Timer */
#define                    SDH_DATA_LGTH  0xffc03928   /* SDH Data Length */
#define                     SDH_DATA_CTL  0xffc0392c   /* SDH Data Control */
#define                     SDH_DATA_CNT  0xffc03930   /* SDH Data Counter */
#define                       SDH_STATUS  0xffc03934   /* SDH Status */
#define                   SDH_STATUS_CLR  0xffc03938   /* SDH Status Clear */
#define                        SDH_MASK0  0xffc0393c   /* SDH Interrupt0 Mask */
#define                        SDH_MASK1  0xffc03940   /* SDH Interrupt1 Mask */
#define                     SDH_FIFO_CNT  0xffc03948   /* SDH FIFO Counter */
#define                         SDH_FIFO  0xffc03980   /* SDH Data FIFO */
#define                     SDH_E_STATUS  0xffc039c0   /* SDH Exception Status */
#define                       SDH_E_MASK  0xffc039c4   /* SDH Exception Mask */
#define                          SDH_CFG  0xffc039c8   /* SDH Configuration */
#define                   SDH_RD_WAIT_EN  0xffc039cc   /* SDH Read Wait Enable */
#define                         SDH_PID0  0xffc039d0   /* SDH Peripheral Identification0 */
#define                         SDH_PID1  0xffc039d4   /* SDH Peripheral Identification1 */
#define                         SDH_PID2  0xffc039d8   /* SDH Peripheral Identification2 */
#define                         SDH_PID3  0xffc039dc   /* SDH Peripheral Identification3 */
#define                         SDH_PID4  0xffc039e0   /* SDH Peripheral Identification4 */
#define                         SDH_PID5  0xffc039e4   /* SDH Peripheral Identification5 */
#define                         SDH_PID6  0xffc039e8   /* SDH Peripheral Identification6 */
#define                         SDH_PID7  0xffc039ec   /* SDH Peripheral Identification7 */

/* HOST Port Registers */

#define                     HOST_CONTROL  0xffc03a00   /* HOST Control Register */
#define                      HOST_STATUS  0xffc03a04   /* HOST Status Register */
#define                     HOST_TIMEOUT  0xffc03a08   /* HOST Acknowledge Mode Timeout Register */

/* USB Control Registers */

#define                        USB_FADDR  0xffc03c00   /* Function address register */
#define                        USB_POWER  0xffc03c04   /* Power management register */
#define                       USB_INTRTX  0xffc03c08   /* Interrupt register for endpoint 0 and Tx endpoint 1 to 7 */
#define                       USB_INTRRX  0xffc03c0c   /* Interrupt register for Rx endpoints 1 to 7 */
#define                      USB_INTRTXE  0xffc03c10   /* Interrupt enable register for IntrTx */
#define                      USB_INTRRXE  0xffc03c14   /* Interrupt enable register for IntrRx */
#define                      USB_INTRUSB  0xffc03c18   /* Interrupt register for common USB interrupts */
#define                     USB_INTRUSBE  0xffc03c1c   /* Interrupt enable register for IntrUSB */
#define                        USB_FRAME  0xffc03c20   /* USB frame number */
#define                        USB_INDEX  0xffc03c24   /* Index register for selecting the indexed endpoint registers */
#define                     USB_TESTMODE  0xffc03c28   /* Enabled USB 20 test modes */
#define                     USB_GLOBINTR  0xffc03c2c   /* Global Interrupt Mask register and Wakeup Exception Interrupt */
#define                   USB_GLOBAL_CTL  0xffc03c30   /* Global Clock Control for the core */

/* USB Packet Control Registers */

#define                USB_TX_MAX_PACKET  0xffc03c40   /* Maximum packet size for Host Tx endpoint */
#define                         USB_CSR0  0xffc03c44   /* Control Status register for endpoint 0 and Control Status register for Host Tx endpoint */
#define                        USB_TXCSR  0xffc03c44   /* Control Status register for endpoint 0 and Control Status register for Host Tx endpoint */
#define                USB_RX_MAX_PACKET  0xffc03c48   /* Maximum packet size for Host Rx endpoint */
#define                        USB_RXCSR  0xffc03c4c   /* Control Status register for Host Rx endpoint */
#define                       USB_COUNT0  0xffc03c50   /* Number of bytes received in endpoint 0 FIFO and Number of bytes received in Host Tx endpoint */
#define                      USB_RXCOUNT  0xffc03c50   /* Number of bytes received in endpoint 0 FIFO and Number of bytes received in Host Tx endpoint */
#define                       USB_TXTYPE  0xffc03c54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint */
#define                    USB_NAKLIMIT0  0xffc03c58   /* Sets the NAK response timeout on Endpoint 0 and on Bulk transfers for Host Tx endpoint */
#define                   USB_TXINTERVAL  0xffc03c58   /* Sets the NAK response timeout on Endpoint 0 and on Bulk transfers for Host Tx endpoint */
#define                       USB_RXTYPE  0xffc03c5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint */
#define                   USB_RXINTERVAL  0xffc03c60   /* Sets the polling interval for Interrupt and Isochronous transfers or the NAK response timeout on Bulk transfers */
#define                      USB_TXCOUNT  0xffc03c68   /* Number of bytes to be written to the selected endpoint Tx FIFO */

/* USB Endpoint FIFO Registers */

#define                     USB_EP0_FIFO  0xffc03c80   /* Endpoint 0 FIFO */
#define                     USB_EP1_FIFO  0xffc03c88   /* Endpoint 1 FIFO */
#define                     USB_EP2_FIFO  0xffc03c90   /* Endpoint 2 FIFO */
#define                     USB_EP3_FIFO  0xffc03c98   /* Endpoint 3 FIFO */
#define                     USB_EP4_FIFO  0xffc03ca0   /* Endpoint 4 FIFO */
#define                     USB_EP5_FIFO  0xffc03ca8   /* Endpoint 5 FIFO */
#define                     USB_EP6_FIFO  0xffc03cb0   /* Endpoint 6 FIFO */
#define                     USB_EP7_FIFO  0xffc03cb8   /* Endpoint 7 FIFO */

/* USB OTG Control Registers */

#define                  USB_OTG_DEV_CTL  0xffc03d00   /* OTG Device Control Register */
#define                 USB_OTG_VBUS_IRQ  0xffc03d04   /* OTG VBUS Control Interrupts */
#define                USB_OTG_VBUS_MASK  0xffc03d08   /* VBUS Control Interrupt Enable */

/* USB Phy Control Registers */

#define                     USB_LINKINFO  0xffc03d48   /* Enables programming of some PHY-side delays */
#define                        USB_VPLEN  0xffc03d4c   /* Determines duration of VBUS pulse for VBUS charging */
#define                      USB_HS_EOF1  0xffc03d50   /* Time buffer for High-Speed transactions */
#define                      USB_FS_EOF1  0xffc03d54   /* Time buffer for Full-Speed transactions */
#define                      USB_LS_EOF1  0xffc03d58   /* Time buffer for Low-Speed transactions */

/* (APHY_CNTRL is for ADI usage only) */

#define                   USB_APHY_CNTRL  0xffc03de0   /* Register that increases visibility of Analog PHY */

/* (APHY_CALIB is for ADI usage only) */

#define                   USB_APHY_CALIB  0xffc03de4   /* Register used to set some calibration values */
#define                  USB_APHY_CNTRL2  0xffc03de8   /* Register used to prevent re-enumeration once Moab goes into hibernate mode */

/* (PHY_TEST is for ADI usage only) */

#define                     USB_PHY_TEST  0xffc03dec   /* Used for reducing simulation time and simplifies FIFO testability */
#define                  USB_PLLOSC_CTRL  0xffc03df0   /* Used to program different parameters for USB PLL and Oscillator */
#define                   USB_SRP_CLKDIV  0xffc03df4   /* Used to program clock divide value for the clock fed to the SRP detection logic */

/* USB Endpoint 0 Control Registers */

#define                USB_EP_NI0_TXMAXP  0xffc03e00   /* Maximum packet size for Host Tx endpoint0 */
#define                 USB_EP_NI0_TXCSR  0xffc03e04   /* Control Status register for endpoint 0 */
#define                USB_EP_NI0_RXMAXP  0xffc03e08   /* Maximum packet size for Host Rx endpoint0 */
#define                 USB_EP_NI0_RXCSR  0xffc03e0c   /* Control Status register for Host Rx endpoint0 */
#define               USB_EP_NI0_RXCOUNT  0xffc03e10   /* Number of bytes received in endpoint 0 FIFO */
#define                USB_EP_NI0_TXTYPE  0xffc03e14   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint0 */
#define            USB_EP_NI0_TXINTERVAL  0xffc03e18   /* Sets the NAK response timeout on Endpoint 0 */
#define                USB_EP_NI0_RXTYPE  0xffc03e1c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint0 */
#define            USB_EP_NI0_RXINTERVAL  0xffc03e20   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint0 */

/* USB Endpoint 1 Control Registers */

#define               USB_EP_NI0_TXCOUNT  0xffc03e28   /* Number of bytes to be written to the endpoint0 Tx FIFO */
#define                USB_EP_NI1_TXMAXP  0xffc03e40   /* Maximum packet size for Host Tx endpoint1 */
#define                 USB_EP_NI1_TXCSR  0xffc03e44   /* Control Status register for endpoint1 */
#define                USB_EP_NI1_RXMAXP  0xffc03e48   /* Maximum packet size for Host Rx endpoint1 */
#define                 USB_EP_NI1_RXCSR  0xffc03e4c   /* Control Status register for Host Rx endpoint1 */
#define               USB_EP_NI1_RXCOUNT  0xffc03e50   /* Number of bytes received in endpoint1 FIFO */
#define                USB_EP_NI1_TXTYPE  0xffc03e54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint1 */
#define            USB_EP_NI1_TXINTERVAL  0xffc03e58   /* Sets the NAK response timeout on Endpoint1 */
#define                USB_EP_NI1_RXTYPE  0xffc03e5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint1 */
#define            USB_EP_NI1_RXINTERVAL  0xffc03e60   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint1 */

/* USB Endpoint 2 Control Registers */

#define               USB_EP_NI1_TXCOUNT  0xffc03e68   /* Number of bytes to be written to the+H102 endpoint1 Tx FIFO */
#define                USB_EP_NI2_TXMAXP  0xffc03e80   /* Maximum packet size for Host Tx endpoint2 */
#define                 USB_EP_NI2_TXCSR  0xffc03e84   /* Control Status register for endpoint2 */
#define                USB_EP_NI2_RXMAXP  0xffc03e88   /* Maximum packet size for Host Rx endpoint2 */
#define                 USB_EP_NI2_RXCSR  0xffc03e8c   /* Control Status register for Host Rx endpoint2 */
#define               USB_EP_NI2_RXCOUNT  0xffc03e90   /* Number of bytes received in endpoint2 FIFO */
#define                USB_EP_NI2_TXTYPE  0xffc03e94   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint2 */
#define            USB_EP_NI2_TXINTERVAL  0xffc03e98   /* Sets the NAK response timeout on Endpoint2 */
#define                USB_EP_NI2_RXTYPE  0xffc03e9c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint2 */
#define            USB_EP_NI2_RXINTERVAL  0xffc03ea0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint2 */

/* USB Endpoint 3 Control Registers */

#define               USB_EP_NI2_TXCOUNT  0xffc03ea8   /* Number of bytes to be written to the endpoint2 Tx FIFO */
#define                USB_EP_NI3_TXMAXP  0xffc03ec0   /* Maximum packet size for Host Tx endpoint3 */
#define                 USB_EP_NI3_TXCSR  0xffc03ec4   /* Control Status register for endpoint3 */
#define                USB_EP_NI3_RXMAXP  0xffc03ec8   /* Maximum packet size for Host Rx endpoint3 */
#define                 USB_EP_NI3_RXCSR  0xffc03ecc   /* Control Status register for Host Rx endpoint3 */
#define               USB_EP_NI3_RXCOUNT  0xffc03ed0   /* Number of bytes received in endpoint3 FIFO */
#define                USB_EP_NI3_TXTYPE  0xffc03ed4   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint3 */
#define            USB_EP_NI3_TXINTERVAL  0xffc03ed8   /* Sets the NAK response timeout on Endpoint3 */
#define                USB_EP_NI3_RXTYPE  0xffc03edc   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint3 */
#define            USB_EP_NI3_RXINTERVAL  0xffc03ee0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint3 */

/* USB Endpoint 4 Control Registers */

#define               USB_EP_NI3_TXCOUNT  0xffc03ee8   /* Number of bytes to be written to the H124endpoint3 Tx FIFO */
#define                USB_EP_NI4_TXMAXP  0xffc03f00   /* Maximum packet size for Host Tx endpoint4 */
#define                 USB_EP_NI4_TXCSR  0xffc03f04   /* Control Status register for endpoint4 */
#define                USB_EP_NI4_RXMAXP  0xffc03f08   /* Maximum packet size for Host Rx endpoint4 */
#define                 USB_EP_NI4_RXCSR  0xffc03f0c   /* Control Status register for Host Rx endpoint4 */
#define               USB_EP_NI4_RXCOUNT  0xffc03f10   /* Number of bytes received in endpoint4 FIFO */
#define                USB_EP_NI4_TXTYPE  0xffc03f14   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint4 */
#define            USB_EP_NI4_TXINTERVAL  0xffc03f18   /* Sets the NAK response timeout on Endpoint4 */
#define                USB_EP_NI4_RXTYPE  0xffc03f1c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint4 */
#define            USB_EP_NI4_RXINTERVAL  0xffc03f20   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint4 */

/* USB Endpoint 5 Control Registers */

#define               USB_EP_NI4_TXCOUNT  0xffc03f28   /* Number of bytes to be written to the endpoint4 Tx FIFO */
#define                USB_EP_NI5_TXMAXP  0xffc03f40   /* Maximum packet size for Host Tx endpoint5 */
#define                 USB_EP_NI5_TXCSR  0xffc03f44   /* Control Status register for endpoint5 */
#define                USB_EP_NI5_RXMAXP  0xffc03f48   /* Maximum packet size for Host Rx endpoint5 */
#define                 USB_EP_NI5_RXCSR  0xffc03f4c   /* Control Status register for Host Rx endpoint5 */
#define               USB_EP_NI5_RXCOUNT  0xffc03f50   /* Number of bytes received in endpoint5 FIFO */
#define                USB_EP_NI5_TXTYPE  0xffc03f54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint5 */
#define            USB_EP_NI5_TXINTERVAL  0xffc03f58   /* Sets the NAK response timeout on Endpoint5 */
#define                USB_EP_NI5_RXTYPE  0xffc03f5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint5 */
#define            USB_EP_NI5_RXINTERVAL  0xffc03f60   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint5 */

/* USB Endpoint 6 Control Registers */

#define               USB_EP_NI5_TXCOUNT  0xffc03f68   /* Number of bytes to be written to the H145endpoint5 Tx FIFO */
#define                USB_EP_NI6_TXMAXP  0xffc03f80   /* Maximum packet size for Host Tx endpoint6 */
#define                 USB_EP_NI6_TXCSR  0xffc03f84   /* Control Status register for endpoint6 */
#define                USB_EP_NI6_RXMAXP  0xffc03f88   /* Maximum packet size for Host Rx endpoint6 */
#define                 USB_EP_NI6_RXCSR  0xffc03f8c   /* Control Status register for Host Rx endpoint6 */
#define               USB_EP_NI6_RXCOUNT  0xffc03f90   /* Number of bytes received in endpoint6 FIFO */
#define                USB_EP_NI6_TXTYPE  0xffc03f94   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint6 */
#define            USB_EP_NI6_TXINTERVAL  0xffc03f98   /* Sets the NAK response timeout on Endpoint6 */
#define                USB_EP_NI6_RXTYPE  0xffc03f9c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint6 */
#define            USB_EP_NI6_RXINTERVAL  0xffc03fa0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint6 */

/* USB Endpoint 7 Control Registers */

#define               USB_EP_NI6_TXCOUNT  0xffc03fa8   /* Number of bytes to be written to the endpoint6 Tx FIFO */
#define                USB_EP_NI7_TXMAXP  0xffc03fc0   /* Maximum packet size for Host Tx endpoint7 */
#define                 USB_EP_NI7_TXCSR  0xffc03fc4   /* Control Status register for endpoint7 */
#define                USB_EP_NI7_RXMAXP  0xffc03fc8   /* Maximum packet size for Host Rx endpoint7 */
#define                 USB_EP_NI7_RXCSR  0xffc03fcc   /* Control Status register for Host Rx endpoint7 */
#define               USB_EP_NI7_RXCOUNT  0xffc03fd0   /* Number of bytes received in endpoint7 FIFO */
#define                USB_EP_NI7_TXTYPE  0xffc03fd4   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint7 */
#define            USB_EP_NI7_TXINTERVAL  0xffc03fd8   /* Sets the NAK response timeout on Endpoint7 */
#define                USB_EP_NI7_RXTYPE  0xffc03fdc   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint7 */
#define            USB_EP_NI7_RXINTERVAL  0xffc03ff0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint7 */
#define               USB_EP_NI7_TXCOUNT  0xffc03ff8   /* Number of bytes to be written to the endpoint7 Tx FIFO */
#define                USB_DMA_INTERRUPT  0xffc04000   /* Indicates pending interrupts for the DMA channels */

/* USB Channel 0 Config Registers */

#define                  USB_DMA0CONTROL  0xffc04004   /* DMA master channel 0 configuration */
#define                  USB_DMA0ADDRLOW  0xffc04008   /* Lower 16-bits of memory source/destination address for DMA master channel 0 */
#define                 USB_DMA0ADDRHIGH  0xffc0400c   /* Upper 16-bits of memory source/destination address for DMA master channel 0 */
#define                 USB_DMA0COUNTLOW  0xffc04010   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 0 */
#define                USB_DMA0COUNTHIGH  0xffc04014   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 0 */

/* USB Channel 1 Config Registers */

#define                  USB_DMA1CONTROL  0xffc04024   /* DMA master channel 1 configuration */
#define                  USB_DMA1ADDRLOW  0xffc04028   /* Lower 16-bits of memory source/destination address for DMA master channel 1 */
#define                 USB_DMA1ADDRHIGH  0xffc0402c   /* Upper 16-bits of memory source/destination address for DMA master channel 1 */
#define                 USB_DMA1COUNTLOW  0xffc04030   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 1 */
#define                USB_DMA1COUNTHIGH  0xffc04034   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 1 */

/* USB Channel 2 Config Registers */

#define                  USB_DMA2CONTROL  0xffc04044   /* DMA master channel 2 configuration */
#define                  USB_DMA2ADDRLOW  0xffc04048   /* Lower 16-bits of memory source/destination address for DMA master channel 2 */
#define                 USB_DMA2ADDRHIGH  0xffc0404c   /* Upper 16-bits of memory source/destination address for DMA master channel 2 */
#define                 USB_DMA2COUNTLOW  0xffc04050   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 2 */
#define                USB_DMA2COUNTHIGH  0xffc04054   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 2 */

/* USB Channel 3 Config Registers */

#define                  USB_DMA3CONTROL  0xffc04064   /* DMA master channel 3 configuration */
#define                  USB_DMA3ADDRLOW  0xffc04068   /* Lower 16-bits of memory source/destination address for DMA master channel 3 */
#define                 USB_DMA3ADDRHIGH  0xffc0406c   /* Upper 16-bits of memory source/destination address for DMA master channel 3 */
#define                 USB_DMA3COUNTLOW  0xffc04070   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 3 */
#define                USB_DMA3COUNTHIGH  0xffc04074   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 3 */

/* USB Channel 4 Config Registers */

#define                  USB_DMA4CONTROL  0xffc04084   /* DMA master channel 4 configuration */
#define                  USB_DMA4ADDRLOW  0xffc04088   /* Lower 16-bits of memory source/destination address for DMA master channel 4 */
#define                 USB_DMA4ADDRHIGH  0xffc0408c   /* Upper 16-bits of memory source/destination address for DMA master channel 4 */
#define                 USB_DMA4COUNTLOW  0xffc04090   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 4 */
#define                USB_DMA4COUNTHIGH  0xffc04094   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 4 */

/* USB Channel 5 Config Registers */

#define                  USB_DMA5CONTROL  0xffc040a4   /* DMA master channel 5 configuration */
#define                  USB_DMA5ADDRLOW  0xffc040a8   /* Lower 16-bits of memory source/destination address for DMA master channel 5 */
#define                 USB_DMA5ADDRHIGH  0xffc040ac   /* Upper 16-bits of memory source/destination address for DMA master channel 5 */
#define                 USB_DMA5COUNTLOW  0xffc040b0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 5 */
#define                USB_DMA5COUNTHIGH  0xffc040b4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 5 */

/* USB Channel 6 Config Registers */

#define                  USB_DMA6CONTROL  0xffc040c4   /* DMA master channel 6 configuration */
#define                  USB_DMA6ADDRLOW  0xffc040c8   /* Lower 16-bits of memory source/destination address for DMA master channel 6 */
#define                 USB_DMA6ADDRHIGH  0xffc040cc   /* Upper 16-bits of memory source/destination address for DMA master channel 6 */
#define                 USB_DMA6COUNTLOW  0xffc040d0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 6 */
#define                USB_DMA6COUNTHIGH  0xffc040d4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 6 */

/* USB Channel 7 Config Registers */

#define                  USB_DMA7CONTROL  0xffc040e4   /* DMA master channel 7 configuration */
#define                  USB_DMA7ADDRLOW  0xffc040e8   /* Lower 16-bits of memory source/destination address for DMA master channel 7 */
#define                 USB_DMA7ADDRHIGH  0xffc040ec   /* Upper 16-bits of memory source/destination address for DMA master channel 7 */
#define                 USB_DMA7COUNTLOW  0xffc040f0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 7 */
#define                USB_DMA7COUNTHIGH  0xffc040f4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 7 */

/* Keypad Registers */

#define                         KPAD_CTL  0xffc04100   /* Controls keypad module enable and disable */
#define                    KPAD_PRESCALE  0xffc04104   /* Establish a time base for programing the KPAD_MSEL register */
#define                        KPAD_MSEL  0xffc04108   /* Selects delay parameters for keypad interface sensitivity */
#define                      KPAD_ROWCOL  0xffc0410c   /* Captures the row and column output values of the keys pressed */
#define                        KPAD_STAT  0xffc04110   /* Holds and clears the status of the keypad interface interrupt */
#define                    KPAD_SOFTEVAL  0xffc04114   /* Lets software force keypad interface to check for keys being pressed */

/* Pixel Compositor (PIXC) Registers */

#define                         PIXC_CTL  0xffc04400   /* Overlay enable, resampling mode, I/O data format, transparency enable, watermark level, FIFO status */
#define                         PIXC_PPL  0xffc04404   /* Holds the number of pixels per line of the display */
#define                         PIXC_LPF  0xffc04408   /* Holds the number of lines per frame of the display */
#define                     PIXC_AHSTART  0xffc0440c   /* Contains horizontal start pixel information of the overlay data (set A) */
#define                       PIXC_AHEND  0xffc04410   /* Contains horizontal end pixel information of the overlay data (set A) */
#define                     PIXC_AVSTART  0xffc04414   /* Contains vertical start pixel information of the overlay data (set A) */
#define                       PIXC_AVEND  0xffc04418   /* Contains vertical end pixel information of the overlay data (set A) */
#define                     PIXC_ATRANSP  0xffc0441c   /* Contains the transparency ratio (set A) */
#define                     PIXC_BHSTART  0xffc04420   /* Contains horizontal start pixel information of the overlay data (set B) */
#define                       PIXC_BHEND  0xffc04424   /* Contains horizontal end pixel information of the overlay data (set B) */
#define                     PIXC_BVSTART  0xffc04428   /* Contains vertical start pixel information of the overlay data (set B) */
#define                       PIXC_BVEND  0xffc0442c   /* Contains vertical end pixel information of the overlay data (set B) */
#define                     PIXC_BTRANSP  0xffc04430   /* Contains the transparency ratio (set B) */
#define                    PIXC_INTRSTAT  0xffc0443c   /* Overlay interrupt configuration/status */
#define                       PIXC_RYCON  0xffc04440   /* Color space conversion matrix register. Contains the R/Y conversion coefficients */
#define                       PIXC_GUCON  0xffc04444   /* Color space conversion matrix register. Contains the G/U conversion coefficients */
#define                       PIXC_BVCON  0xffc04448   /* Color space conversion matrix register. Contains the B/V conversion coefficients */
#define                      PIXC_CCBIAS  0xffc0444c   /* Bias values for the color space conversion matrix */
#define                          PIXC_TC  0xffc04450   /* Holds the transparent color value */

/* Handshake MDMA 0 Registers */

#define                   HMDMA0_CONTROL  0xffc04500   /* Handshake MDMA0 Control Register */
#define                    HMDMA0_ECINIT  0xffc04504   /* Handshake MDMA0 Initial Edge Count Register */
#define                    HMDMA0_BCINIT  0xffc04508   /* Handshake MDMA0 Initial Block Count Register */
#define                  HMDMA0_ECURGENT  0xffc0450c   /* Handshake MDMA0 Urgent Edge Count Threshhold Register */
#define                HMDMA0_ECOVERFLOW  0xffc04510   /* Handshake MDMA0 Edge Count Overflow Interrupt Register */
#define                    HMDMA0_ECOUNT  0xffc04514   /* Handshake MDMA0 Current Edge Count Register */
#define                    HMDMA0_BCOUNT  0xffc04518   /* Handshake MDMA0 Current Block Count Register */

/* Handshake MDMA 1 Registers */

#define                   HMDMA1_CONTROL  0xffc04540   /* Handshake MDMA1 Control Register */
#define                    HMDMA1_ECINIT  0xffc04544   /* Handshake MDMA1 Initial Edge Count Register */
#define                    HMDMA1_BCINIT  0xffc04548   /* Handshake MDMA1 Initial Block Count Register */
#define                  HMDMA1_ECURGENT  0xffc0454c   /* Handshake MDMA1 Urgent Edge Count Threshhold Register */
#define                HMDMA1_ECOVERFLOW  0xffc04550   /* Handshake MDMA1 Edge Count Overflow Interrupt Register */
#define                    HMDMA1_ECOUNT  0xffc04554   /* Handshake MDMA1 Current Edge Count Register */
#define                    HMDMA1_BCOUNT  0xffc04558   /* Handshake MDMA1 Current Block Count Register */


/* ********************************************************** */
/*     SINGLE BIT MACRO PAIRS (bit mask and negated one)      */
/*     and MULTI BIT READ MACROS                              */
/* ********************************************************** */

/* Bit masks for PIXC_CTL */

#define                   PIXC_EN  0x1        /* Pixel Compositor Enable */
#define                  OVR_A_EN  0x2        /* Overlay A Enable */
#define                  OVR_B_EN  0x4        /* Overlay B Enable */
#define                  IMG_FORM  0x8        /* Image Data Format */
#define                  OVR_FORM  0x10       /* Overlay Data Format */
#define                  OUT_FORM  0x20       /* Output Data Format */
#define                   UDS_MOD  0x40       /* Resampling Mode */
#define                     TC_EN  0x80       /* Transparent Color Enable */
#define                  IMG_STAT  0x300      /* Image FIFO Status */
#define                  OVR_STAT  0xc00      /* Overlay FIFO Status */
#define                    WM_LVL  0x3000     /* FIFO Watermark Level */

/* Bit masks for PIXC_AHSTART */

#define                  A_HSTART  0xfff      /* Horizontal Start Coordinates */

/* Bit masks for PIXC_AHEND */

#define                    A_HEND  0xfff      /* Horizontal End Coordinates */

/* Bit masks for PIXC_AVSTART */

#define                  A_VSTART  0x3ff      /* Vertical Start Coordinates */

/* Bit masks for PIXC_AVEND */

#define                    A_VEND  0x3ff      /* Vertical End Coordinates */

/* Bit masks for PIXC_ATRANSP */

#define                  A_TRANSP  0xf        /* Transparency Value */

/* Bit masks for PIXC_BHSTART */

#define                  B_HSTART  0xfff      /* Horizontal Start Coordinates */

/* Bit masks for PIXC_BHEND */

#define                    B_HEND  0xfff      /* Horizontal End Coordinates */

/* Bit masks for PIXC_BVSTART */

#define                  B_VSTART  0x3ff      /* Vertical Start Coordinates */

/* Bit masks for PIXC_BVEND */

#define                    B_VEND  0x3ff      /* Vertical End Coordinates */

/* Bit masks for PIXC_BTRANSP */

#define                  B_TRANSP  0xf        /* Transparency Value */

/* Bit masks for PIXC_INTRSTAT */

#define                OVR_INT_EN  0x1        /* Interrupt at End of Last Valid Overlay */
#define                FRM_INT_EN  0x2        /* Interrupt at End of Frame */
#define              OVR_INT_STAT  0x4        /* Overlay Interrupt Status */
#define              FRM_INT_STAT  0x8        /* Frame Interrupt Status */

/* Bit masks for PIXC_RYCON */

#define                       A11  0x3ff      /* A11 in the Coefficient Matrix */
#define                       A12  0xffc00    /* A12 in the Coefficient Matrix */
#define                       A13  0x3ff00000 /* A13 in the Coefficient Matrix */
#define                  RY_MULT4  0x40000000 /* Multiply Row by 4 */

/* Bit masks for PIXC_GUCON */

#define                       A21  0x3ff      /* A21 in the Coefficient Matrix */
#define                       A22  0xffc00    /* A22 in the Coefficient Matrix */
#define                       A23  0x3ff00000 /* A23 in the Coefficient Matrix */
#define                  GU_MULT4  0x40000000 /* Multiply Row by 4 */

/* Bit masks for PIXC_BVCON */

#define                       A31  0x3ff      /* A31 in the Coefficient Matrix */
#define                       A32  0xffc00    /* A32 in the Coefficient Matrix */
#define                       A33  0x3ff00000 /* A33 in the Coefficient Matrix */
#define                  BV_MULT4  0x40000000 /* Multiply Row by 4 */

/* Bit masks for PIXC_CCBIAS */

#define                       A14  0x3ff      /* A14 in the Bias Vector */
#define                       A24  0xffc00    /* A24 in the Bias Vector */
#define                       A34  0x3ff00000 /* A34 in the Bias Vector */

/* Bit masks for PIXC_TC */

#define                  RY_TRANS  0xff       /* Transparent Color - R/Y Component */
#define                  GU_TRANS  0xff00     /* Transparent Color - G/U Component */
#define                  BV_TRANS  0xff0000   /* Transparent Color - B/V Component */

/* Bit masks for HOST_CONTROL */

#define                   HOST_EN  0x1        /* Host Enable */
#define                  HOST_END  0x2        /* Host Endianess */
#define                 DATA_SIZE  0x4        /* Data Size */
#define                  HOST_RST  0x8        /* Host Reset */
#define                  HRDY_OVR  0x20       /* Host Ready Override */
#define                  INT_MODE  0x40       /* Interrupt Mode */
#define                     BT_EN  0x80       /* Bus Timeout Enable */
#define                       EHW  0x100      /* Enable Host Write */
#define                       EHR  0x200      /* Enable Host Read */
#define                       BDR  0x400      /* Burst DMA Requests */

/* Bit masks for HOST_STATUS */

#define                     READY  0x1        /* DMA Ready */
#define                  FIFOFULL  0x2        /* FIFO Full */
#define                 FIFOEMPTY  0x4        /* FIFO Empty */
#define              DMA_COMPLETE  0x8        /* DMA Complete */
#define                      HSHK  0x10       /* Host Handshake */
#define                   TIMEOUT  0x20       /* Host Timeout */
#define                      HIRQ  0x40       /* Host Interrupt Request */
#define                ALLOW_CNFG  0x80       /* Allow New Configuration */
#define                   DMA_DIR  0x100      /* DMA Direction */
#define                       BTE  0x200      /* Bus Timeout Enabled */

/* Bit masks for HOST_TIMEOUT */

#define             COUNT_TIMEOUT  0x7ff      /* Host Timeout count */

/* Bit masks for MXVR_CONFIG */

#define                    MXVREN  0x1        /* MXVR Enable */
#define                      MMSM  0x2        /* MXVR Master/Slave Mode Select */
#define                    ACTIVE  0x4        /* Active Mode */
#define                    SDELAY  0x8        /* Synchronous Data Delay */
#define                   NCMRXEN  0x10       /* Normal Control Message Receive Enable */
#define                   RWRRXEN  0x20       /* Remote Write Receive Enable */
#define                     MTXEN  0x40       /* MXVR Transmit Data Enable */
#define                    MTXONB  0x80       /* MXVR Phy Transmitter On */
#define                   EPARITY  0x100      /* Even Parity Select */
#define                       MSB  0x1e00     /* Master Synchronous Boundary */
#define                    APRXEN  0x2000     /* Asynchronous Packet Receive Enable */
#define                    WAKEUP  0x4000     /* Wake-Up */
#define                     LMECH  0x8000     /* Lock Mechanism Select */

/* Bit masks for MXVR_STATE_0 */

#define                      NACT  0x1        /* Network Activity */
#define                    SBLOCK  0x2        /* Super Block Lock */
#define                   FMPLLST  0xc        /* Frequency Multiply PLL SM State */
#define                  CDRPLLST  0xe0       /* Clock/Data Recovery PLL SM State */
#define                     APBSY  0x100      /* Asynchronous Packet Transmit Buffer Busy */
#define                     APARB  0x200      /* Asynchronous Packet Arbitrating */
#define                      APTX  0x400      /* Asynchronous Packet Transmitting */
#define                      APRX  0x800      /* Receiving Asynchronous Packet */
#define                     CMBSY  0x1000     /* Control Message Transmit Buffer Busy */
#define                     CMARB  0x2000     /* Control Message Arbitrating */
#define                      CMTX  0x4000     /* Control Message Transmitting */
#define                      CMRX  0x8000     /* Receiving Control Message */
#define                    MRXONB  0x10000    /* MRXONB Pin State */
#define                     RGSIP  0x20000    /* Remote Get Source In Progress */
#define                     DALIP  0x40000    /* Resource Deallocate In Progress */
#define                      ALIP  0x80000    /* Resource Allocate In Progress */
#define                     RRDIP  0x100000   /* Remote Read In Progress */
#define                     RWRIP  0x200000   /* Remote Write In Progress */
#define                     FLOCK  0x400000   /* Frame Lock */
#define                     BLOCK  0x800000   /* Block Lock */
#define                       RSB  0xf000000  /* Received Synchronous Boundary */
#define                   DERRNUM  0xf0000000 /* DMA Error Channel Number */

/* Bit masks for MXVR_STATE_1 */

#define                   SRXNUMB  0xf        /* Synchronous Receive FIFO Number of Bytes */
#define                   STXNUMB  0xf0       /* Synchronous Transmit FIFO Number of Bytes */
#define                    APCONT  0x100      /* Asynchronous Packet Continuation */
#define                  OBERRNUM  0xe00      /* DMA Out of Bounds Error Channel Number */
#define                DMAACTIVE0  0x10000    /* DMA0 Active */
#define                DMAACTIVE1  0x20000    /* DMA1 Active */
#define                DMAACTIVE2  0x40000    /* DMA2 Active */
#define                DMAACTIVE3  0x80000    /* DMA3 Active */
#define                DMAACTIVE4  0x100000   /* DMA4 Active */
#define                DMAACTIVE5  0x200000   /* DMA5 Active */
#define                DMAACTIVE6  0x400000   /* DMA6 Active */
#define                DMAACTIVE7  0x800000   /* DMA7 Active */
#define                  DMAPMEN0  0x1000000  /* DMA0 Pattern Matching Enabled */
#define                  DMAPMEN1  0x2000000  /* DMA1 Pattern Matching Enabled */
#define                  DMAPMEN2  0x4000000  /* DMA2 Pattern Matching Enabled */
#define                  DMAPMEN3  0x8000000  /* DMA3 Pattern Matching Enabled */
#define                  DMAPMEN4  0x10000000 /* DMA4 Pattern Matching Enabled */
#define                  DMAPMEN5  0x20000000 /* DMA5 Pattern Matching Enabled */
#define                  DMAPMEN6  0x40000000 /* DMA6 Pattern Matching Enabled */
#define                  DMAPMEN7  0x80000000 /* DMA7 Pattern Matching Enabled */

/* Bit masks for MXVR_INT_STAT_0 */

#define                      NI2A  0x1        /* Network Inactive to Active */
#define                      NA2I  0x2        /* Network Active to Inactive */
#define                     SBU2L  0x4        /* Super Block Unlock to Lock */
#define                     SBL2U  0x8        /* Super Block Lock to Unlock */
#define                       PRU  0x10       /* Position Register Updated */
#define                      MPRU  0x20       /* Maximum Position Register Updated */
#define                       DRU  0x40       /* Delay Register Updated */
#define                      MDRU  0x80       /* Maximum Delay Register Updated */
#define                       SBU  0x100      /* Synchronous Boundary Updated */
#define                       ATU  0x200      /* Allocation Table Updated */
#define                      FCZ0  0x400      /* Frame Counter 0 Zero */
#define                      FCZ1  0x800      /* Frame Counter 1 Zero */
#define                      PERR  0x1000     /* Parity Error */
#define                      MH2L  0x2000     /* MRXONB High to Low */
#define                      ML2H  0x4000     /* MRXONB Low to High */
#define                       WUP  0x8000     /* Wake-Up Preamble Received */
#define                      FU2L  0x10000    /* Frame Unlock to Lock */
#define                      FL2U  0x20000    /* Frame Lock to Unlock */
#define                      BU2L  0x40000    /* Block Unlock to Lock */
#define                      BL2U  0x80000    /* Block Lock to Unlock */
#define                     OBERR  0x100000   /* DMA Out of Bounds Error */
#define                       PFL  0x200000   /* PLL Frequency Locked */
#define                       SCZ  0x400000   /* System Clock Counter Zero */
#define                      FERR  0x800000   /* FIFO Error */
#define                       CMR  0x1000000  /* Control Message Received */
#define                     CMROF  0x2000000  /* Control Message Receive Buffer Overflow */
#define                      CMTS  0x4000000  /* Control Message Transmit Buffer Successfully Sent */
#define                      CMTC  0x8000000  /* Control Message Transmit Buffer Successfully Cancelled */
#define                      RWRC  0x10000000 /* Remote Write Control Message Completed */
#define                       BCZ  0x20000000 /* Block Counter Zero */
#define                     BMERR  0x40000000 /* Biphase Mark Coding Error */
#define                      DERR  0x80000000 /* DMA Error */

/* Bit masks for MXVR_INT_STAT_1 */

#define                    HDONE0  0x1        /* DMA0 Half Done */
#define                     DONE0  0x2        /* DMA0 Done */
#define                       APR  0x4        /* Asynchronous Packet Received */
#define                     APROF  0x8        /* Asynchronous Packet Receive Buffer Overflow */
#define                    HDONE1  0x10       /* DMA1 Half Done */
#define                     DONE1  0x20       /* DMA1 Done */
#define                      APTS  0x40       /* Asynchronous Packet Transmit Buffer Successfully Sent */
#define                      APTC  0x80       /* Asynchronous Packet Transmit Buffer Successfully Cancelled */
#define                    HDONE2  0x100      /* DMA2 Half Done */
#define                     DONE2  0x200      /* DMA2 Done */
#define                     APRCE  0x400      /* Asynchronous Packet Receive CRC Error */
#define                     APRPE  0x800      /* Asynchronous Packet Receive Packet Error */
#define                    HDONE3  0x1000     /* DMA3 Half Done */
#define                     DONE3  0x2000     /* DMA3 Done */
#define                    HDONE4  0x10000    /* DMA4 Half Done */
#define                     DONE4  0x20000    /* DMA4 Done */
#define                    HDONE5  0x100000   /* DMA5 Half Done */
#define                     DONE5  0x200000   /* DMA5 Done */
#define                    HDONE6  0x1000000  /* DMA6 Half Done */
#define                     DONE6  0x2000000  /* DMA6 Done */
#define                    HDONE7  0x10000000 /* DMA7 Half Done */
#define                     DONE7  0x20000000 /* DMA7 Done */

/* Bit masks for MXVR_INT_EN_0 */

#define                    NI2AEN  0x1        /* Network Inactive to Active Interrupt Enable */
#define                    NA2IEN  0x2        /* Network Active to Inactive Interrupt Enable */
#define                   SBU2LEN  0x4        /* Super Block Unlock to Lock Interrupt Enable */
#define                   SBL2UEN  0x8        /* Super Block Lock to Unlock Interrupt Enable */
#define                     PRUEN  0x10       /* Position Register Updated Interrupt Enable */
#define                    MPRUEN  0x20       /* Maximum Position Register Updated Interrupt Enable */
#define                     DRUEN  0x40       /* Delay Register Updated Interrupt Enable */
#define                    MDRUEN  0x80       /* Maximum Delay Register Updated Interrupt Enable */
#define                     SBUEN  0x100      /* Synchronous Boundary Updated Interrupt Enable */
#define                     ATUEN  0x200      /* Allocation Table Updated Interrupt Enable */
#define                    FCZ0EN  0x400      /* Frame Counter 0 Zero Interrupt Enable */
#define                    FCZ1EN  0x800      /* Frame Counter 1 Zero Interrupt Enable */
#define                    PERREN  0x1000     /* Parity Error Interrupt Enable */
#define                    MH2LEN  0x2000     /* MRXONB High to Low Interrupt Enable */
#define                    ML2HEN  0x4000     /* MRXONB Low to High Interrupt Enable */
#define                     WUPEN  0x8000     /* Wake-Up Preamble Received Interrupt Enable */
#define                    FU2LEN  0x10000    /* Frame Unlock to Lock Interrupt Enable */
#define                    FL2UEN  0x20000    /* Frame Lock to Unlock Interrupt Enable */
#define                    BU2LEN  0x40000    /* Block Unlock to Lock Interrupt Enable */
#define                    BL2UEN  0x80000    /* Block Lock to Unlock Interrupt Enable */
#define                   OBERREN  0x100000   /* DMA Out of Bounds Error Interrupt Enable */
#define                     PFLEN  0x200000   /* PLL Frequency Locked Interrupt Enable */
#define                     SCZEN  0x400000   /* System Clock Counter Zero Interrupt Enable */
#define                    FERREN  0x800000   /* FIFO Error Interrupt Enable */
#define                     CMREN  0x1000000  /* Control Message Received Interrupt Enable */
#define                   CMROFEN  0x2000000  /* Control Message Receive Buffer Overflow Interrupt Enable */
#define                    CMTSEN  0x4000000  /* Control Message Transmit Buffer Successfully Sent Interrupt Enable */
#define                    CMTCEN  0x8000000  /* Control Message Transmit Buffer Successfully Cancelled Interrupt Enable */
#define                    RWRCEN  0x10000000 /* Remote Write Control Message Completed Interrupt Enable */
#define                     BCZEN  0x20000000 /* Block Counter Zero Interrupt Enable */
#define                   BMERREN  0x40000000 /* Biphase Mark Coding Error Interrupt Enable */
#define                    DERREN  0x80000000 /* DMA Error Interrupt Enable */

/* Bit masks for MXVR_INT_EN_1 */

#define                  HDONEEN0  0x1        /* DMA0 Half Done Interrupt Enable */
#define                   DONEEN0  0x2        /* DMA0 Done Interrupt Enable */
#define                     APREN  0x4        /* Asynchronous Packet Received Interrupt Enable */
#define                   APROFEN  0x8        /* Asynchronous Packet Receive Buffer Overflow Interrupt Enable */
#define                  HDONEEN1  0x10       /* DMA1 Half Done Interrupt Enable */
#define                   DONEEN1  0x20       /* DMA1 Done Interrupt Enable */
#define                    APTSEN  0x40       /* Asynchronous Packet Transmit Buffer Successfully Sent Interrupt Enable */
#define                    APTCEN  0x80       /* Asynchronous Packet Transmit Buffer Successfully Cancelled Interrupt Enable */
#define                  HDONEEN2  0x100      /* DMA2 Half Done Interrupt Enable */
#define                   DONEEN2  0x200      /* DMA2 Done Interrupt Enable */
#define                   APRCEEN  0x400      /* Asynchronous Packet Receive CRC Error Interrupt Enable */
#define                   APRPEEN  0x800      /* Asynchronous Packet Receive Packet Error Interrupt Enable */
#define                  HDONEEN3  0x1000     /* DMA3 Half Done Interrupt Enable */
#define                   DONEEN3  0x2000     /* DMA3 Done Interrupt Enable */
#define                  HDONEEN4  0x10000    /* DMA4 Half Done Interrupt Enable */
#define                   DONEEN4  0x20000    /* DMA4 Done Interrupt Enable */
#define                  HDONEEN5  0x100000   /* DMA5 Half Done Interrupt Enable */
#define                   DONEEN5  0x200000   /* DMA5 Done Interrupt Enable */
#define                  HDONEEN6  0x1000000  /* DMA6 Half Done Interrupt Enable */
#define                   DONEEN6  0x2000000  /* DMA6 Done Interrupt Enable */
#define                  HDONEEN7  0x10000000 /* DMA7 Half Done Interrupt Enable */
#define                   DONEEN7  0x20000000 /* DMA7 Done Interrupt Enable */

/* Bit masks for MXVR_POSITION */

#define                  POSITION  0x3f       /* Node Position */
#define                    PVALID  0x8000     /* Node Position Valid */

/* Bit masks for MXVR_MAX_POSITION */

#define                 MPOSITION  0x3f       /* Maximum Node Position */
#define                   MPVALID  0x8000     /* Maximum Node Position Valid */

/* Bit masks for MXVR_DELAY */

#define                     DELAY  0x3f       /* Node Frame Delay */
#define                    DVALID  0x8000     /* Node Frame Delay Valid */

/* Bit masks for MXVR_MAX_DELAY */

#define                    MDELAY  0x3f       /* Maximum Node Frame Delay */
#define                   MDVALID  0x8000     /* Maximum Node Frame Delay Valid */

/* Bit masks for MXVR_LADDR */

#define                     LADDR  0xffff     /* Logical Address */
#define                    LVALID  0x80000000 /* Logical Address Valid */

/* Bit masks for MXVR_GADDR */

#define                    GADDRL  0xff       /* Group Address Lower Byte */
#define                    GVALID  0x8000     /* Group Address Valid */

/* Bit masks for MXVR_AADDR */

#define                     AADDR  0xffff     /* Alternate Address */
#define                    AVALID  0x80000000 /* Alternate Address Valid */

/* Bit masks for MXVR_ALLOC_0 */

#define                       CL0  0x7f       /* Channel 0 Connection Label */
#define                      CIU0  0x80       /* Channel 0 In Use */
#define                       CL1  0x7f00     /* Channel 0 Connection Label */
#define                      CIU1  0x8000     /* Channel 0 In Use */
#define                       CL2  0x7f0000   /* Channel 0 Connection Label */
#define                      CIU2  0x800000   /* Channel 0 In Use */
#define                       CL3  0x7f000000 /* Channel 0 Connection Label */
#define                      CIU3  0x80000000 /* Channel 0 In Use */

/* Bit masks for MXVR_ALLOC_1 */

#define                       CL4  0x7f       /* Channel 4 Connection Label */
#define                      CIU4  0x80       /* Channel 4 In Use */
#define                       CL5  0x7f00     /* Channel 5 Connection Label */
#define                      CIU5  0x8000     /* Channel 5 In Use */
#define                       CL6  0x7f0000   /* Channel 6 Connection Label */
#define                      CIU6  0x800000   /* Channel 6 In Use */
#define                       CL7  0x7f000000 /* Channel 7 Connection Label */
#define                      CIU7  0x80000000 /* Channel 7 In Use */

/* Bit masks for MXVR_ALLOC_2 */

#define                       CL8  0x7f       /* Channel 8 Connection Label */
#define                      CIU8  0x80       /* Channel 8 In Use */
#define                       CL9  0x7f00     /* Channel 9 Connection Label */
#define                      CIU9  0x8000     /* Channel 9 In Use */
#define                      CL10  0x7f0000   /* Channel 10 Connection Label */
#define                     CIU10  0x800000   /* Channel 10 In Use */
#define                      CL11  0x7f000000 /* Channel 11 Connection Label */
#define                     CIU11  0x80000000 /* Channel 11 In Use */

/* Bit masks for MXVR_ALLOC_3 */

#define                      CL12  0x7f       /* Channel 12 Connection Label */
#define                     CIU12  0x80       /* Channel 12 In Use */
#define                      CL13  0x7f00     /* Channel 13 Connection Label */
#define                     CIU13  0x8000     /* Channel 13 In Use */
#define                      CL14  0x7f0000   /* Channel 14 Connection Label */
#define                     CIU14  0x800000   /* Channel 14 In Use */
#define                      CL15  0x7f000000 /* Channel 15 Connection Label */
#define                     CIU15  0x80000000 /* Channel 15 In Use */

/* Bit masks for MXVR_ALLOC_4 */

#define                      CL16  0x7f       /* Channel 16 Connection Label */
#define                     CIU16  0x80       /* Channel 16 In Use */
#define                      CL17  0x7f00     /* Channel 17 Connection Label */
#define                     CIU17  0x8000     /* Channel 17 In Use */
#define                      CL18  0x7f0000   /* Channel 18 Connection Label */
#define                     CIU18  0x800000   /* Channel 18 In Use */
#define                      CL19  0x7f000000 /* Channel 19 Connection Label */
#define                     CIU19  0x80000000 /* Channel 19 In Use */

/* Bit masks for MXVR_ALLOC_5 */

#define                      CL20  0x7f       /* Channel 20 Connection Label */
#define                     CIU20  0x80       /* Channel 20 In Use */
#define                      CL21  0x7f00     /* Channel 21 Connection Label */
#define                     CIU21  0x8000     /* Channel 21 In Use */
#define                      CL22  0x7f0000   /* Channel 22 Connection Label */
#define                     CIU22  0x800000   /* Channel 22 In Use */
#define                      CL23  0x7f000000 /* Channel 23 Connection Label */
#define                     CIU23  0x80000000 /* Channel 23 In Use */

/* Bit masks for MXVR_ALLOC_6 */

#define                      CL24  0x7f       /* Channel 24 Connection Label */
#define                     CIU24  0x80       /* Channel 24 In Use */
#define                      CL25  0x7f00     /* Channel 25 Connection Label */
#define                     CIU25  0x8000     /* Channel 25 In Use */
#define                      CL26  0x7f0000   /* Channel 26 Connection Label */
#define                     CIU26  0x800000   /* Channel 26 In Use */
#define                      CL27  0x7f000000 /* Channel 27 Connection Label */
#define                     CIU27  0x80000000 /* Channel 27 In Use */

/* Bit masks for MXVR_ALLOC_7 */

#define                      CL28  0x7f       /* Channel 28 Connection Label */
#define                     CIU28  0x80       /* Channel 28 In Use */
#define                      CL29  0x7f00     /* Channel 29 Connection Label */
#define                     CIU29  0x8000     /* Channel 29 In Use */
#define                      CL30  0x7f0000   /* Channel 30 Connection Label */
#define                     CIU30  0x800000   /* Channel 30 In Use */
#define                      CL31  0x7f000000 /* Channel 31 Connection Label */
#define                     CIU31  0x80000000 /* Channel 31 In Use */

/* Bit masks for MXVR_ALLOC_8 */

#define                      CL32  0x7f       /* Channel 32 Connection Label */
#define                     CIU32  0x80       /* Channel 32 In Use */
#define                      CL33  0x7f00     /* Channel 33 Connection Label */
#define                     CIU33  0x8000     /* Channel 33 In Use */
#define                      CL34  0x7f0000   /* Channel 34 Connection Label */
#define                     CIU34  0x800000   /* Channel 34 In Use */
#define                      CL35  0x7f000000 /* Channel 35 Connection Label */
#define                     CIU35  0x80000000 /* Channel 35 In Use */

/* Bit masks for MXVR_ALLOC_9 */

#define                      CL36  0x7f       /* Channel 36 Connection Label */
#define                     CIU36  0x80       /* Channel 36 In Use */
#define                      CL37  0x7f00     /* Channel 37 Connection Label */
#define                     CIU37  0x8000     /* Channel 37 In Use */
#define                      CL38  0x7f0000   /* Channel 38 Connection Label */
#define                     CIU38  0x800000   /* Channel 38 In Use */
#define                      CL39  0x7f000000 /* Channel 39 Connection Label */
#define                     CIU39  0x80000000 /* Channel 39 In Use */

/* Bit masks for MXVR_ALLOC_10 */

#define                      CL40  0x7f       /* Channel 40 Connection Label */
#define                     CIU40  0x80       /* Channel 40 In Use */
#define                      CL41  0x7f00     /* Channel 41 Connection Label */
#define                     CIU41  0x8000     /* Channel 41 In Use */
#define                      CL42  0x7f0000   /* Channel 42 Connection Label */
#define                     CIU42  0x800000   /* Channel 42 In Use */
#define                      CL43  0x7f000000 /* Channel 43 Connection Label */
#define                     CIU43  0x80000000 /* Channel 43 In Use */

/* Bit masks for MXVR_ALLOC_11 */

#define                      CL44  0x7f       /* Channel 44 Connection Label */
#define                     CIU44  0x80       /* Channel 44 In Use */
#define                      CL45  0x7f00     /* Channel 45 Connection Label */
#define                     CIU45  0x8000     /* Channel 45 In Use */
#define                      CL46  0x7f0000   /* Channel 46 Connection Label */
#define                     CIU46  0x800000   /* Channel 46 In Use */
#define                      CL47  0x7f000000 /* Channel 47 Connection Label */
#define                     CIU47  0x80000000 /* Channel 47 In Use */

/* Bit masks for MXVR_ALLOC_12 */

#define                      CL48  0x7f       /* Channel 48 Connection Label */
#define                     CIU48  0x80       /* Channel 48 In Use */
#define                      CL49  0x7f00     /* Channel 49 Connection Label */
#define                     CIU49  0x8000     /* Channel 49 In Use */
#define                      CL50  0x7f0000   /* Channel 50 Connection Label */
#define                     CIU50  0x800000   /* Channel 50 In Use */
#define                      CL51  0x7f000000 /* Channel 51 Connection Label */
#define                     CIU51  0x80000000 /* Channel 51 In Use */

/* Bit masks for MXVR_ALLOC_13 */

#define                      CL52  0x7f       /* Channel 52 Connection Label */
#define                     CIU52  0x80       /* Channel 52 In Use */
#define                      CL53  0x7f00     /* Channel 53 Connection Label */
#define                     CIU53  0x8000     /* Channel 53 In Use */
#define                      CL54  0x7f0000   /* Channel 54 Connection Label */
#define                     CIU54  0x800000   /* Channel 54 In Use */
#define                      CL55  0x7f000000 /* Channel 55 Connection Label */
#define                     CIU55  0x80000000 /* Channel 55 In Use */

/* Bit masks for MXVR_ALLOC_14 */

#define                      CL56  0x7f       /* Channel 56 Connection Label */
#define                     CIU56  0x80       /* Channel 56 In Use */
#define                      CL57  0x7f00     /* Channel 57 Connection Label */
#define                     CIU57  0x8000     /* Channel 57 In Use */
#define                      CL58  0x7f0000   /* Channel 58 Connection Label */
#define                     CIU58  0x800000   /* Channel 58 In Use */
#define                      CL59  0x7f000000 /* Channel 59 Connection Label */
#define                     CIU59  0x80000000 /* Channel 59 In Use */

/* MXVR_SYNC_LCHAN_0 Masks */

#define LCHANPC0     0x0000000Flu
#define LCHANPC1     0x000000F0lu
#define LCHANPC2     0x00000F00lu
#define LCHANPC3     0x0000F000lu
#define LCHANPC4     0x000F0000lu
#define LCHANPC5     0x00F00000lu
#define LCHANPC6     0x0F000000lu
#define LCHANPC7     0xF0000000lu


/* MXVR_SYNC_LCHAN_1 Masks */

#define LCHANPC8     0x0000000Flu
#define LCHANPC9     0x000000F0lu
#define LCHANPC10    0x00000F00lu
#define LCHANPC11    0x0000F000lu
#define LCHANPC12    0x000F0000lu
#define LCHANPC13    0x00F00000lu
#define LCHANPC14    0x0F000000lu
#define LCHANPC15    0xF0000000lu


/* MXVR_SYNC_LCHAN_2 Masks */

#define LCHANPC16    0x0000000Flu
#define LCHANPC17    0x000000F0lu
#define LCHANPC18    0x00000F00lu
#define LCHANPC19    0x0000F000lu
#define LCHANPC20    0x000F0000lu
#define LCHANPC21    0x00F00000lu
#define LCHANPC22    0x0F000000lu
#define LCHANPC23    0xF0000000lu


/* MXVR_SYNC_LCHAN_3 Masks */

#define LCHANPC24    0x0000000Flu
#define LCHANPC25    0x000000F0lu
#define LCHANPC26    0x00000F00lu
#define LCHANPC27    0x0000F000lu
#define LCHANPC28    0x000F0000lu
#define LCHANPC29    0x00F00000lu
#define LCHANPC30    0x0F000000lu
#define LCHANPC31    0xF0000000lu


/* MXVR_SYNC_LCHAN_4 Masks */

#define LCHANPC32    0x0000000Flu
#define LCHANPC33    0x000000F0lu
#define LCHANPC34    0x00000F00lu
#define LCHANPC35    0x0000F000lu
#define LCHANPC36    0x000F0000lu
#define LCHANPC37    0x00F00000lu
#define LCHANPC38    0x0F000000lu
#define LCHANPC39    0xF0000000lu


/* MXVR_SYNC_LCHAN_5 Masks */

#define LCHANPC40    0x0000000Flu
#define LCHANPC41    0x000000F0lu
#define LCHANPC42    0x00000F00lu
#define LCHANPC43    0x0000F000lu
#define LCHANPC44    0x000F0000lu
#define LCHANPC45    0x00F00000lu
#define LCHANPC46    0x0F000000lu
#define LCHANPC47    0xF0000000lu


/* MXVR_SYNC_LCHAN_6 Masks */

#define LCHANPC48    0x0000000Flu
#define LCHANPC49    0x000000F0lu
#define LCHANPC50    0x00000F00lu
#define LCHANPC51    0x0000F000lu
#define LCHANPC52    0x000F0000lu
#define LCHANPC53    0x00F00000lu
#define LCHANPC54    0x0F000000lu
#define LCHANPC55    0xF0000000lu


/* MXVR_SYNC_LCHAN_7 Masks */

#define LCHANPC56    0x0000000Flu
#define LCHANPC57    0x000000F0lu
#define LCHANPC58    0x00000F00lu
#define LCHANPC59    0x0000F000lu

/* Bit masks for MXVR_DMAx_CONFIG */

#define                    MDMAEN  0x1        /* DMA Channel Enable */
#define                        DD  0x2        /* DMA Channel Direction */
#define                 BY4SWAPEN  0x20       /* DMA Channel Four Byte Swap Enable */
#define                     LCHAN  0x3c0      /* DMA Channel Logical Channel */
#define                 BITSWAPEN  0x400      /* DMA Channel Bit Swap Enable */
#define                 BY2SWAPEN  0x800      /* DMA Channel Two Byte Swap Enable */
#define                     MFLOW  0x7000     /* DMA Channel Operation Flow */
#define                   FIXEDPM  0x80000    /* DMA Channel Fixed Pattern Matching Select */
#define                  STARTPAT  0x300000   /* DMA Channel Start Pattern Select */
#define                   STOPPAT  0xc00000   /* DMA Channel Stop Pattern Select */
#define                  COUNTPOS  0x1c000000 /* DMA Channel Count Position */

/* Bit masks for MXVR_AP_CTL */

#define                   STARTAP  0x1        /* Start Asynchronous Packet Transmission */
#define                  CANCELAP  0x2        /* Cancel Asynchronous Packet Transmission */
#define                   RESETAP  0x4        /* Reset Asynchronous Packet Arbitration */
#define                    APRBE0  0x4000     /* Asynchronous Packet Receive Buffer Entry 0 */
#define                    APRBE1  0x8000     /* Asynchronous Packet Receive Buffer Entry 1 */

/* Bit masks for MXVR_APRB_START_ADDR */

#define      MXVR_APRB_START_ADDR_MASK  0x1fffffe  /* Asynchronous Packet Receive Buffer Start Address */

/* Bit masks for MXVR_APRB_CURR_ADDR */

#define       MXVR_APRB_CURR_ADDR_MASK  0xffffffff /* Asynchronous Packet Receive Buffer Current Address */

/* Bit masks for MXVR_APTB_START_ADDR */

#define       MXVR_APTB_START_ADDR_MASK  0x1fffffe  /* Asynchronous Packet Transmit Buffer Start Address */

/* Bit masks for MXVR_APTB_CURR_ADDR */

#define        MXVR_APTB_CURR_ADDR_MASK  0xffffffff /* Asynchronous Packet Transmit Buffer Current Address */

/* Bit masks for MXVR_CM_CTL */

#define                   STARTCM  0x1        /* Start Control Message Transmission */
#define                  CANCELCM  0x2        /* Cancel Control Message Transmission */
#define                    CMRBE0  0x10000    /* Control Message Receive Buffer Entry 0 */
#define                    CMRBE1  0x20000    /* Control Message Receive Buffer Entry 1 */
#define                    CMRBE2  0x40000    /* Control Message Receive Buffer Entry 2 */
#define                    CMRBE3  0x80000    /* Control Message Receive Buffer Entry 3 */
#define                    CMRBE4  0x100000   /* Control Message Receive Buffer Entry 4 */
#define                    CMRBE5  0x200000   /* Control Message Receive Buffer Entry 5 */
#define                    CMRBE6  0x400000   /* Control Message Receive Buffer Entry 6 */
#define                    CMRBE7  0x800000   /* Control Message Receive Buffer Entry 7 */
#define                    CMRBE8  0x1000000  /* Control Message Receive Buffer Entry 8 */
#define                    CMRBE9  0x2000000  /* Control Message Receive Buffer Entry 9 */
#define                   CMRBE10  0x4000000  /* Control Message Receive Buffer Entry 10 */
#define                   CMRBE11  0x8000000  /* Control Message Receive Buffer Entry 11 */
#define                   CMRBE12  0x10000000 /* Control Message Receive Buffer Entry 12 */
#define                   CMRBE13  0x20000000 /* Control Message Receive Buffer Entry 13 */
#define                   CMRBE14  0x40000000 /* Control Message Receive Buffer Entry 14 */
#define                   CMRBE15  0x80000000 /* Control Message Receive Buffer Entry 15 */

/* Bit masks for MXVR_CMRB_START_ADDR */

#define      MXVR_CMRB_START_ADDR_MASK  0x1fffffe  /* Control Message Receive Buffer Start Address */

/* Bit masks for MXVR_CMRB_CURR_ADDR */

#define       MXVR_CMRB_CURR_ADDR_MASK  0xffffffff /* Control Message Receive Buffer Current Address */

/* Bit masks for MXVR_CMTB_START_ADDR */

#define      MXVR_CMTB_START_ADDR_MASK  0x1fffffe  /* Control Message Transmit Buffer Start Address */

/* Bit masks for MXVR_CMTB_CURR_ADDR */

#define       MXVR_CMTB_CURR_ADDR_MASK  0xffffffff /* Control Message Transmit Buffer Current Address */

/* Bit masks for MXVR_RRDB_START_ADDR */

#define      MXVR_RRDB_START_ADDR_MASK  0x1fffffe  /* Remote Read Buffer Start Address */

/* Bit masks for MXVR_RRDB_CURR_ADDR */

#define       MXVR_RRDB_CURR_ADDR_MASK  0xffffffff /* Remote Read Buffer Current Address */

/* Bit masks for MXVR_PAT_DATAx */

#define              MATCH_DATA_0  0xff       /* Pattern Match Data Byte 0 */
#define              MATCH_DATA_1  0xff00     /* Pattern Match Data Byte 1 */
#define              MATCH_DATA_2  0xff0000   /* Pattern Match Data Byte 2 */
#define              MATCH_DATA_3  0xff000000 /* Pattern Match Data Byte 3 */

/* Bit masks for MXVR_PAT_EN_0 */

#define              MATCH_EN_0_0  0x1        /* Pattern Match Enable Byte 0 Bit 0 */
#define              MATCH_EN_0_1  0x2        /* Pattern Match Enable Byte 0 Bit 1 */
#define              MATCH_EN_0_2  0x4        /* Pattern Match Enable Byte 0 Bit 2 */
#define              MATCH_EN_0_3  0x8        /* Pattern Match Enable Byte 0 Bit 3 */
#define              MATCH_EN_0_4  0x10       /* Pattern Match Enable Byte 0 Bit 4 */
#define              MATCH_EN_0_5  0x20       /* Pattern Match Enable Byte 0 Bit 5 */
#define              MATCH_EN_0_6  0x40       /* Pattern Match Enable Byte 0 Bit 6 */
#define              MATCH_EN_0_7  0x80       /* Pattern Match Enable Byte 0 Bit 7 */
#define              MATCH_EN_1_0  0x100      /* Pattern Match Enable Byte 1 Bit 0 */
#define              MATCH_EN_1_1  0x200      /* Pattern Match Enable Byte 1 Bit 1 */
#define              MATCH_EN_1_2  0x400      /* Pattern Match Enable Byte 1 Bit 2 */
#define              MATCH_EN_1_3  0x800      /* Pattern Match Enable Byte 1 Bit 3 */
#define              MATCH_EN_1_4  0x1000     /* Pattern Match Enable Byte 1 Bit 4 */
#define              MATCH_EN_1_5  0x2000     /* Pattern Match Enable Byte 1 Bit 5 */
#define              MATCH_EN_1_6  0x4000     /* Pattern Match Enable Byte 1 Bit 6 */
#define              MATCH_EN_1_7  0x8000     /* Pattern Match Enable Byte 1 Bit 7 */
#define              MATCH_EN_2_0  0x10000    /* Pattern Match Enable Byte 2 Bit 0 */
#define              MATCH_EN_2_1  0x20000    /* Pattern Match Enable Byte 2 Bit 1 */
#define              MATCH_EN_2_2  0x40000    /* Pattern Match Enable Byte 2 Bit 2 */
#define              MATCH_EN_2_3  0x80000    /* Pattern Match Enable Byte 2 Bit 3 */
#define              MATCH_EN_2_4  0x100000   /* Pattern Match Enable Byte 2 Bit 4 */
#define              MATCH_EN_2_5  0x200000   /* Pattern Match Enable Byte 2 Bit 5 */
#define              MATCH_EN_2_6  0x400000   /* Pattern Match Enable Byte 2 Bit 6 */
#define              MATCH_EN_2_7  0x800000   /* Pattern Match Enable Byte 2 Bit 7 */
#define              MATCH_EN_3_0  0x1000000  /* Pattern Match Enable Byte 3 Bit 0 */
#define              MATCH_EN_3_1  0x2000000  /* Pattern Match Enable Byte 3 Bit 1 */
#define              MATCH_EN_3_2  0x4000000  /* Pattern Match Enable Byte 3 Bit 2 */
#define              MATCH_EN_3_3  0x8000000  /* Pattern Match Enable Byte 3 Bit 3 */
#define              MATCH_EN_3_4  0x10000000 /* Pattern Match Enable Byte 3 Bit 4 */
#define              MATCH_EN_3_5  0x20000000 /* Pattern Match Enable Byte 3 Bit 5 */
#define              MATCH_EN_3_6  0x40000000 /* Pattern Match Enable Byte 3 Bit 6 */
#define              MATCH_EN_3_7  0x80000000 /* Pattern Match Enable Byte 3 Bit 7 */

/* Bit masks for MXVR_PAT_EN_1 */

#define              MATCH_EN_0_0  0x1        /* Pattern Match Enable Byte 0 Bit 0 */
#define              MATCH_EN_0_1  0x2        /* Pattern Match Enable Byte 0 Bit 1 */
#define              MATCH_EN_0_2  0x4        /* Pattern Match Enable Byte 0 Bit 2 */
#define              MATCH_EN_0_3  0x8        /* Pattern Match Enable Byte 0 Bit 3 */
#define              MATCH_EN_0_4  0x10       /* Pattern Match Enable Byte 0 Bit 4 */
#define              MATCH_EN_0_5  0x20       /* Pattern Match Enable Byte 0 Bit 5 */
#define              MATCH_EN_0_6  0x40       /* Pattern Match Enable Byte 0 Bit 6 */
#define              MATCH_EN_0_7  0x80       /* Pattern Match Enable Byte 0 Bit 7 */
#define              MATCH_EN_1_0  0x100      /* Pattern Match Enable Byte 1 Bit 0 */
#define              MATCH_EN_1_1  0x200      /* Pattern Match Enable Byte 1 Bit 1 */
#define              MATCH_EN_1_2  0x400      /* Pattern Match Enable Byte 1 Bit 2 */
#define              MATCH_EN_1_3  0x800      /* Pattern Match Enable Byte 1 Bit 3 */
#define              MATCH_EN_1_4  0x1000     /* Pattern Match Enable Byte 1 Bit 4 */
#define              MATCH_EN_1_5  0x2000     /* Pattern Match Enable Byte 1 Bit 5 */
#define              MATCH_EN_1_6  0x4000     /* Pattern Match Enable Byte 1 Bit 6 */
#define              MATCH_EN_1_7  0x8000     /* Pattern Match Enable Byte 1 Bit 7 */
#define              MATCH_EN_2_0  0x10000    /* Pattern Match Enable Byte 2 Bit 0 */
#define              MATCH_EN_2_1  0x20000    /* Pattern Match Enable Byte 2 Bit 1 */
#define              MATCH_EN_2_2  0x40000    /* Pattern Match Enable Byte 2 Bit 2 */
#define              MATCH_EN_2_3  0x80000    /* Pattern Match Enable Byte 2 Bit 3 */
#define              MATCH_EN_2_4  0x100000   /* Pattern Match Enable Byte 2 Bit 4 */
#define              MATCH_EN_2_5  0x200000   /* Pattern Match Enable Byte 2 Bit 5 */
#define              MATCH_EN_2_6  0x400000   /* Pattern Match Enable Byte 2 Bit 6 */
#define              MATCH_EN_2_7  0x800000   /* Pattern Match Enable Byte 2 Bit 7 */
#define              MATCH_EN_3_0  0x1000000  /* Pattern Match Enable Byte 3 Bit 0 */
#define              MATCH_EN_3_1  0x2000000  /* Pattern Match Enable Byte 3 Bit 1 */
#define              MATCH_EN_3_2  0x4000000  /* Pattern Match Enable Byte 3 Bit 2 */
#define              MATCH_EN_3_3  0x8000000  /* Pattern Match Enable Byte 3 Bit 3 */
#define              MATCH_EN_3_4  0x10000000 /* Pattern Match Enable Byte 3 Bit 4 */
#define              MATCH_EN_3_5  0x20000000 /* Pattern Match Enable Byte 3 Bit 5 */
#define              MATCH_EN_3_6  0x40000000 /* Pattern Match Enable Byte 3 Bit 6 */
#define              MATCH_EN_3_7  0x80000000 /* Pattern Match Enable Byte 3 Bit 7 */

/* Bit masks for MXVR_FRAME_CNT_0 */

#define                      FCNT  0xffff     /* Frame Count */

/* Bit masks for MXVR_FRAME_CNT_1 */

#define                      FCNT  0xffff     /* Frame Count */

/* Bit masks for MXVR_ROUTING_0 */

#define                    TX_CH0  0x3f       /* Transmit Channel 0 */
#define                  MUTE_CH0  0x80       /* Mute Channel 0 */
#define                    TX_CH1  0x3f00     /* Transmit Channel 0 */
#define                  MUTE_CH1  0x8000     /* Mute Channel 0 */
#define                    TX_CH2  0x3f0000   /* Transmit Channel 0 */
#define                  MUTE_CH2  0x800000   /* Mute Channel 0 */
#define                    TX_CH3  0x3f000000 /* Transmit Channel 0 */
#define                  MUTE_CH3  0x80000000 /* Mute Channel 0 */

/* Bit masks for MXVR_ROUTING_1 */

#define                    TX_CH4  0x3f       /* Transmit Channel 4 */
#define                  MUTE_CH4  0x80       /* Mute Channel 4 */
#define                    TX_CH5  0x3f00     /* Transmit Channel 5 */
#define                  MUTE_CH5  0x8000     /* Mute Channel 5 */
#define                    TX_CH6  0x3f0000   /* Transmit Channel 6 */
#define                  MUTE_CH6  0x800000   /* Mute Channel 6 */
#define                    TX_CH7  0x3f000000 /* Transmit Channel 7 */
#define                  MUTE_CH7  0x80000000 /* Mute Channel 7 */

/* Bit masks for MXVR_ROUTING_2 */

#define                    TX_CH8  0x3f       /* Transmit Channel 8 */
#define                  MUTE_CH8  0x80       /* Mute Channel 8 */
#define                    TX_CH9  0x3f00     /* Transmit Channel 9 */
#define                  MUTE_CH9  0x8000     /* Mute Channel 9 */
#define                   TX_CH10  0x3f0000   /* Transmit Channel 10 */
#define                 MUTE_CH10  0x800000   /* Mute Channel 10 */
#define                   TX_CH11  0x3f000000 /* Transmit Channel 11 */
#define                 MUTE_CH11  0x80000000 /* Mute Channel 11 */

/* Bit masks for MXVR_ROUTING_3 */

#define                   TX_CH12  0x3f       /* Transmit Channel 12 */
#define                 MUTE_CH12  0x80       /* Mute Channel 12 */
#define                   TX_CH13  0x3f00     /* Transmit Channel 13 */
#define                 MUTE_CH13  0x8000     /* Mute Channel 13 */
#define                   TX_CH14  0x3f0000   /* Transmit Channel 14 */
#define                 MUTE_CH14  0x800000   /* Mute Channel 14 */
#define                   TX_CH15  0x3f000000 /* Transmit Channel 15 */
#define                 MUTE_CH15  0x80000000 /* Mute Channel 15 */

/* Bit masks for MXVR_ROUTING_4 */

#define                   TX_CH16  0x3f       /* Transmit Channel 16 */
#define                 MUTE_CH16  0x80       /* Mute Channel 16 */
#define                   TX_CH17  0x3f00     /* Transmit Channel 17 */
#define                 MUTE_CH17  0x8000     /* Mute Channel 17 */
#define                   TX_CH18  0x3f0000   /* Transmit Channel 18 */
#define                 MUTE_CH18  0x800000   /* Mute Channel 18 */
#define                   TX_CH19  0x3f000000 /* Transmit Channel 19 */
#define                 MUTE_CH19  0x80000000 /* Mute Channel 19 */

/* Bit masks for MXVR_ROUTING_5 */

#define                   TX_CH20  0x3f       /* Transmit Channel 20 */
#define                 MUTE_CH20  0x80       /* Mute Channel 20 */
#define                   TX_CH21  0x3f00     /* Transmit Channel 21 */
#define                 MUTE_CH21  0x8000     /* Mute Channel 21 */
#define                   TX_CH22  0x3f0000   /* Transmit Channel 22 */
#define                 MUTE_CH22  0x800000   /* Mute Channel 22 */
#define                   TX_CH23  0x3f000000 /* Transmit Channel 23 */
#define                 MUTE_CH23  0x80000000 /* Mute Channel 23 */

/* Bit masks for MXVR_ROUTING_6 */

#define                   TX_CH24  0x3f       /* Transmit Channel 24 */
#define                 MUTE_CH24  0x80       /* Mute Channel 24 */
#define                   TX_CH25  0x3f00     /* Transmit Channel 25 */
#define                 MUTE_CH25  0x8000     /* Mute Channel 25 */
#define                   TX_CH26  0x3f0000   /* Transmit Channel 26 */
#define                 MUTE_CH26  0x800000   /* Mute Channel 26 */
#define                   TX_CH27  0x3f000000 /* Transmit Channel 27 */
#define                 MUTE_CH27  0x80000000 /* Mute Channel 27 */

/* Bit masks for MXVR_ROUTING_7 */

#define                   TX_CH28  0x3f       /* Transmit Channel 28 */
#define                 MUTE_CH28  0x80       /* Mute Channel 28 */
#define                   TX_CH29  0x3f00     /* Transmit Channel 29 */
#define                 MUTE_CH29  0x8000     /* Mute Channel 29 */
#define                   TX_CH30  0x3f0000   /* Transmit Channel 30 */
#define                 MUTE_CH30  0x800000   /* Mute Channel 30 */
#define                   TX_CH31  0x3f000000 /* Transmit Channel 31 */
#define                 MUTE_CH31  0x80000000 /* Mute Channel 31 */

/* Bit masks for MXVR_ROUTING_8 */

#define                   TX_CH32  0x3f       /* Transmit Channel 32 */
#define                 MUTE_CH32  0x80       /* Mute Channel 32 */
#define                   TX_CH33  0x3f00     /* Transmit Channel 33 */
#define                 MUTE_CH33  0x8000     /* Mute Channel 33 */
#define                   TX_CH34  0x3f0000   /* Transmit Channel 34 */
#define                 MUTE_CH34  0x800000   /* Mute Channel 34 */
#define                   TX_CH35  0x3f000000 /* Transmit Channel 35 */
#define                 MUTE_CH35  0x80000000 /* Mute Channel 35 */

/* Bit masks for MXVR_ROUTING_9 */

#define                   TX_CH36  0x3f       /* Transmit Channel 36 */
#define                 MUTE_CH36  0x80       /* Mute Channel 36 */
#define                   TX_CH37  0x3f00     /* Transmit Channel 37 */
#define                 MUTE_CH37  0x8000     /* Mute Channel 37 */
#define                   TX_CH38  0x3f0000   /* Transmit Channel 38 */
#define                 MUTE_CH38  0x800000   /* Mute Channel 38 */
#define                   TX_CH39  0x3f000000 /* Transmit Channel 39 */
#define                 MUTE_CH39  0x80000000 /* Mute Channel 39 */

/* Bit masks for MXVR_ROUTING_10 */

#define                   TX_CH40  0x3f       /* Transmit Channel 40 */
#define                 MUTE_CH40  0x80       /* Mute Channel 40 */
#define                   TX_CH41  0x3f00     /* Transmit Channel 41 */
#define                 MUTE_CH41  0x8000     /* Mute Channel 41 */
#define                   TX_CH42  0x3f0000   /* Transmit Channel 42 */
#define                 MUTE_CH42  0x800000   /* Mute Channel 42 */
#define                   TX_CH43  0x3f000000 /* Transmit Channel 43 */
#define                 MUTE_CH43  0x80000000 /* Mute Channel 43 */

/* Bit masks for MXVR_ROUTING_11 */

#define                   TX_CH44  0x3f       /* Transmit Channel 44 */
#define                 MUTE_CH44  0x80       /* Mute Channel 44 */
#define                   TX_CH45  0x3f00     /* Transmit Channel 45 */
#define                 MUTE_CH45  0x8000     /* Mute Channel 45 */
#define                   TX_CH46  0x3f0000   /* Transmit Channel 46 */
#define                 MUTE_CH46  0x800000   /* Mute Channel 46 */
#define                   TX_CH47  0x3f000000 /* Transmit Channel 47 */
#define                 MUTE_CH47  0x80000000 /* Mute Channel 47 */

/* Bit masks for MXVR_ROUTING_12 */

#define                   TX_CH48  0x3f       /* Transmit Channel 48 */
#define                 MUTE_CH48  0x80       /* Mute Channel 48 */
#define                   TX_CH49  0x3f00     /* Transmit Channel 49 */
#define                 MUTE_CH49  0x8000     /* Mute Channel 49 */
#define                   TX_CH50  0x3f0000   /* Transmit Channel 50 */
#define                 MUTE_CH50  0x800000   /* Mute Channel 50 */
#define                   TX_CH51  0x3f000000 /* Transmit Channel 51 */
#define                 MUTE_CH51  0x80000000 /* Mute Channel 51 */

/* Bit masks for MXVR_ROUTING_13 */

#define                   TX_CH52  0x3f       /* Transmit Channel 52 */
#define                 MUTE_CH52  0x80       /* Mute Channel 52 */
#define                   TX_CH53  0x3f00     /* Transmit Channel 53 */
#define                 MUTE_CH53  0x8000     /* Mute Channel 53 */
#define                   TX_CH54  0x3f0000   /* Transmit Channel 54 */
#define                 MUTE_CH54  0x800000   /* Mute Channel 54 */
#define                   TX_CH55  0x3f000000 /* Transmit Channel 55 */
#define                 MUTE_CH55  0x80000000 /* Mute Channel 55 */

/* Bit masks for MXVR_ROUTING_14 */

#define                   TX_CH56  0x3f       /* Transmit Channel 56 */
#define                 MUTE_CH56  0x80       /* Mute Channel 56 */
#define                   TX_CH57  0x3f00     /* Transmit Channel 57 */
#define                 MUTE_CH57  0x8000     /* Mute Channel 57 */
#define                   TX_CH58  0x3f0000   /* Transmit Channel 58 */
#define                 MUTE_CH58  0x800000   /* Mute Channel 58 */
#define                   TX_CH59  0x3f000000 /* Transmit Channel 59 */
#define                 MUTE_CH59  0x80000000 /* Mute Channel 59 */

/* Bit masks for MXVR_BLOCK_CNT */

#define                      BCNT  0xffff     /* Block Count */

/* Bit masks for MXVR_CLK_CTL */

#define                  MXTALCEN  0x1        /* MXVR Crystal Oscillator Clock Enable */
#define                  MXTALFEN  0x2        /* MXVR Crystal Oscillator Feedback Enable */
#define                  MXTALMUL  0x30       /* MXVR Crystal Multiplier */
#define                  CLKX3SEL  0x80       /* Clock Generation Source Select */
#define                   MMCLKEN  0x100      /* Master Clock Enable */
#define                  MMCLKMUL  0x1e00     /* Master Clock Multiplication Factor */
#define                   PLLSMPS  0xe000     /* MXVR PLL State Machine Prescaler */
#define                   MBCLKEN  0x10000    /* Bit Clock Enable */
#define                  MBCLKDIV  0x1e0000   /* Bit Clock Divide Factor */
#define                     INVRX  0x800000   /* Invert Receive Data */
#define                     MFSEN  0x1000000  /* Frame Sync Enable */
#define                    MFSDIV  0x1e000000 /* Frame Sync Divide Factor */
#define                    MFSSEL  0x60000000 /* Frame Sync Select */
#define                   MFSSYNC  0x80000000 /* Frame Sync Synchronization Select */

/* Bit masks for MXVR_CDRPLL_CTL */

#define                   CDRSMEN  0x1        /* MXVR CDRPLL State Machine Enable */
#define                   CDRRSTB  0x2        /* MXVR CDRPLL Reset */
#define                   CDRSVCO  0x4        /* MXVR CDRPLL Start VCO */
#define                   CDRMODE  0x8        /* MXVR CDRPLL CDR Mode Select */
#define                   CDRSCNT  0x3f0      /* MXVR CDRPLL Start Counter */
#define                   CDRLCNT  0xfc00     /* MXVR CDRPLL Lock Counter */
#define                 CDRSHPSEL  0x3f0000   /* MXVR CDRPLL Shaper Select */
#define                  CDRSHPEN  0x800000   /* MXVR CDRPLL Shaper Enable */
#define                  CDRCPSEL  0xff000000 /* MXVR CDRPLL Charge Pump Current Select */

/* Bit masks for MXVR_FMPLL_CTL */

#define                    FMSMEN  0x1        /* MXVR FMPLL State Machine Enable */
#define                    FMRSTB  0x2        /* MXVR FMPLL Reset */
#define                    FMSVCO  0x4        /* MXVR FMPLL Start VCO */
#define                    FMSCNT  0x3f0      /* MXVR FMPLL Start Counter */
#define                    FMLCNT  0xfc00     /* MXVR FMPLL Lock Counter */
#define                   FMCPSEL  0xff000000 /* MXVR FMPLL Charge Pump Current Select */

/* Bit masks for MXVR_PIN_CTL */

#define                  MTXONBOD  0x1        /* MTXONB Open Drain Select */
#define                   MTXONBG  0x2        /* MTXONB Gates MTX Select */
#define                     MFSOE  0x10       /* MFS Output Enable */
#define                  MFSGPSEL  0x20       /* MFS General Purpose Output Select */
#define                  MFSGPDAT  0x40       /* MFS General Purpose Output Data */

/* Bit masks for MXVR_SCLK_CNT */

#define                      SCNT  0xffff     /* System Clock Count */

/* Bit masks for KPAD_CTL */

#define                   KPAD_EN  0x1        /* Keypad Enable */
#define              KPAD_IRQMODE  0x6        /* Key Press Interrupt Enable */
#define                KPAD_ROWEN  0x1c00     /* Row Enable Width */
#define                KPAD_COLEN  0xe000     /* Column Enable Width */

/* Bit masks for KPAD_PRESCALE */

#define         KPAD_PRESCALE_VAL  0x3f       /* Key Prescale Value */

/* Bit masks for KPAD_MSEL */

#define                DBON_SCALE  0xff       /* Debounce Scale Value */
#define              COLDRV_SCALE  0xff00     /* Column Driver Scale Value */

/* Bit masks for KPAD_ROWCOL */

#define                  KPAD_ROW  0xff       /* Rows Pressed */
#define                  KPAD_COL  0xff00     /* Columns Pressed */

/* Bit masks for KPAD_STAT */

#define                  KPAD_IRQ  0x1        /* Keypad Interrupt Status */
#define              KPAD_MROWCOL  0x6        /* Multiple Row/Column Keypress Status */
#define              KPAD_PRESSED  0x8        /* Key press current status */

/* Bit masks for KPAD_SOFTEVAL */

#define           KPAD_SOFTEVAL_E  0x2        /* Software Programmable Force Evaluate */

/* Bit masks for SDH_COMMAND */

#define                   CMD_IDX  0x3f       /* Command Index */
#define                   CMD_RSP  0x40       /* Response */
#define                 CMD_L_RSP  0x80       /* Long Response */
#define                 CMD_INT_E  0x100      /* Command Interrupt */
#define                CMD_PEND_E  0x200      /* Command Pending */
#define                     CMD_E  0x400      /* Command Enable */

/* Bit masks for SDH_PWR_CTL */

#define                    PWR_ON  0x3        /* Power On */
#if 0
#define                       TBD  0x3c       /* TBD */
#endif
#define                 SD_CMD_OD  0x40       /* Open Drain Output */
#define                   ROD_CTL  0x80       /* Rod Control */

/* Bit masks for SDH_CLK_CTL */

#define                    CLKDIV  0xff       /* MC_CLK Divisor */
#define                     CLK_E  0x100      /* MC_CLK Bus Clock Enable */
#define                  PWR_SV_E  0x200      /* Power Save Enable */
#define             CLKDIV_BYPASS  0x400      /* Bypass Divisor */
#define                  WIDE_BUS  0x800      /* Wide Bus Mode Enable */

/* Bit masks for SDH_RESP_CMD */

#define                  RESP_CMD  0x3f       /* Response Command */

/* Bit masks for SDH_DATA_CTL */

#define                     DTX_E  0x1        /* Data Transfer Enable */
#define                   DTX_DIR  0x2        /* Data Transfer Direction */
#define                  DTX_MODE  0x4        /* Data Transfer Mode */
#define                 DTX_DMA_E  0x8        /* Data Transfer DMA Enable */
#define              DTX_BLK_LGTH  0xf0       /* Data Transfer Block Length */

/* Bit masks for SDH_STATUS */

#define              CMD_CRC_FAIL  0x1        /* CMD CRC Fail */
#define              DAT_CRC_FAIL  0x2        /* Data CRC Fail */
#define               CMD_TIME_OUT  0x4        /* CMD Time Out */
#define               DAT_TIME_OUT  0x8        /* Data Time Out */
#define               TX_UNDERRUN  0x10       /* Transmit Underrun */
#define                RX_OVERRUN  0x20       /* Receive Overrun */
#define              CMD_RESP_END  0x40       /* CMD Response End */
#define                  CMD_SENT  0x80       /* CMD Sent */
#define                   DAT_END  0x100      /* Data End */
#define             START_BIT_ERR  0x200      /* Start Bit Error */
#define               DAT_BLK_END  0x400      /* Data Block End */
#define                   CMD_ACT  0x800      /* CMD Active */
#define                    TX_ACT  0x1000     /* Transmit Active */
#define                    RX_ACT  0x2000     /* Receive Active */
#define              TX_FIFO_STAT  0x4000     /* Transmit FIFO Status */
#define              RX_FIFO_STAT  0x8000     /* Receive FIFO Status */
#define              TX_FIFO_FULL  0x10000    /* Transmit FIFO Full */
#define              RX_FIFO_FULL  0x20000    /* Receive FIFO Full */
#define              TX_FIFO_ZERO  0x40000    /* Transmit FIFO Empty */
#define               RX_DAT_ZERO  0x80000    /* Receive FIFO Empty */
#define                TX_DAT_RDY  0x100000   /* Transmit Data Available */
#define               RX_FIFO_RDY  0x200000   /* Receive Data Available */

/* Bit masks for SDH_STATUS_CLR */

#define         CMD_CRC_FAIL_STAT  0x1        /* CMD CRC Fail Status */
#define         DAT_CRC_FAIL_STAT  0x2        /* Data CRC Fail Status */
#define          CMD_TIMEOUT_STAT  0x4        /* CMD Time Out Status */
#define          DAT_TIMEOUT_STAT  0x8        /* Data Time Out status */
#define          TX_UNDERRUN_STAT  0x10       /* Transmit Underrun Status */
#define           RX_OVERRUN_STAT  0x20       /* Receive Overrun Status */
#define         CMD_RESP_END_STAT  0x40       /* CMD Response End Status */
#define             CMD_SENT_STAT  0x80       /* CMD Sent Status */
#define              DAT_END_STAT  0x100      /* Data End Status */
#define        START_BIT_ERR_STAT  0x200      /* Start Bit Error Status */
#define          DAT_BLK_END_STAT  0x400      /* Data Block End Status */

/* Bit masks for SDH_MASK0 */

#define         CMD_CRC_FAIL_MASK  0x1        /* CMD CRC Fail Mask */
#define         DAT_CRC_FAIL_MASK  0x2        /* Data CRC Fail Mask */
#define          CMD_TIMEOUT_MASK  0x4        /* CMD Time Out Mask */
#define          DAT_TIMEOUT_MASK  0x8        /* Data Time Out Mask */
#define          TX_UNDERRUN_MASK  0x10       /* Transmit Underrun Mask */
#define           RX_OVERRUN_MASK  0x20       /* Receive Overrun Mask */
#define         CMD_RESP_END_MASK  0x40       /* CMD Response End Mask */
#define             CMD_SENT_MASK  0x80       /* CMD Sent Mask */
#define              DAT_END_MASK  0x100      /* Data End Mask */
#define        START_BIT_ERR_MASK  0x200      /* Start Bit Error Mask */
#define          DAT_BLK_END_MASK  0x400      /* Data Block End Mask */
#define              CMD_ACT_MASK  0x800      /* CMD Active Mask */
#define               TX_ACT_MASK  0x1000     /* Transmit Active Mask */
#define               RX_ACT_MASK  0x2000     /* Receive Active Mask */
#define         TX_FIFO_STAT_MASK  0x4000     /* Transmit FIFO Status Mask */
#define         RX_FIFO_STAT_MASK  0x8000     /* Receive FIFO Status Mask */
#define         TX_FIFO_FULL_MASK  0x10000    /* Transmit FIFO Full Mask */
#define         RX_FIFO_FULL_MASK  0x20000    /* Receive FIFO Full Mask */
#define         TX_FIFO_ZERO_MASK  0x40000    /* Transmit FIFO Empty Mask */
#define          RX_DAT_ZERO_MASK  0x80000    /* Receive FIFO Empty Mask */
#define           TX_DAT_RDY_MASK  0x100000   /* Transmit Data Available Mask */
#define          RX_FIFO_RDY_MASK  0x200000   /* Receive Data Available Mask */

/* Bit masks for SDH_FIFO_CNT */

#define                FIFO_COUNT  0x7fff     /* FIFO Count */

/* Bit masks for SDH_E_STATUS */

#define              SDIO_INT_DET  0x2        /* SDIO Int Detected */
#define               SD_CARD_DET  0x10       /* SD Card Detect */

/* Bit masks for SDH_E_MASK */

#define                  SDIO_MSK  0x2        /* Mask SDIO Int Detected */
#define                   SCD_MSK  0x40       /* Mask Card Detect */

/* Bit masks for SDH_CFG */

#define                   CLKS_EN  0x1        /* Clocks Enable */
#define                      SD4E  0x4        /* SDIO 4-Bit Enable */
#define                       MWE  0x8        /* Moving Window Enable */
#define                    SD_RST  0x10       /* SDMMC Reset */
#define                 PUP_SDDAT  0x20       /* Pull-up SD_DAT */
#define                PUP_SDDAT3  0x40       /* Pull-up SD_DAT3 */
#define                 PD_SDDAT3  0x80       /* Pull-down SD_DAT3 */

/* Bit masks for SDH_RD_WAIT_EN */

#define                       RWR  0x1        /* Read Wait Request */

/* Bit masks for ATAPI_CONTROL */

#define                 PIO_START  0x1        /* Start PIO/Reg Op */
#define               MULTI_START  0x2        /* Start Multi-DMA Op */
#define               ULTRA_START  0x4        /* Start Ultra-DMA Op */
#define                  XFER_DIR  0x8        /* Transfer Direction */
#define                  IORDY_EN  0x10       /* IORDY Enable */
#define                FIFO_FLUSH  0x20       /* Flush FIFOs */
#define                  SOFT_RST  0x40       /* Soft Reset */
#define                   DEV_RST  0x80       /* Device Reset */
#define                TFRCNT_RST  0x100      /* Trans Count Reset */
#define               END_ON_TERM  0x200      /* End/Terminate Select */
#define               PIO_USE_DMA  0x400      /* PIO-DMA Enable */
#define          UDMAIN_FIFO_THRS  0xf000     /* Ultra DMA-IN FIFO Threshold */

/* Bit masks for ATAPI_STATUS */

#define               PIO_XFER_ON  0x1        /* PIO transfer in progress */
#define             MULTI_XFER_ON  0x2        /* Multi-word DMA transfer in progress */
#define             ULTRA_XFER_ON  0x4        /* Ultra DMA transfer in progress */
#define               ULTRA_IN_FL  0xf0       /* Ultra DMA Input FIFO Level */

/* Bit masks for ATAPI_DEV_ADDR */

#define                  DEV_ADDR  0x1f       /* Device Address */

/* Bit masks for ATAPI_INT_MASK */

#define        ATAPI_DEV_INT_MASK  0x1        /* Device interrupt mask */
#define             PIO_DONE_MASK  0x2        /* PIO transfer done interrupt mask */
#define           MULTI_DONE_MASK  0x4        /* Multi-DMA transfer done interrupt mask */
#define          UDMAIN_DONE_MASK  0x8        /* Ultra-DMA in transfer done interrupt mask */
#define         UDMAOUT_DONE_MASK  0x10       /* Ultra-DMA out transfer done interrupt mask */
#define       HOST_TERM_XFER_MASK  0x20       /* Host terminate current transfer interrupt mask */
#define           MULTI_TERM_MASK  0x40       /* Device terminate Multi-DMA transfer interrupt mask */
#define          UDMAIN_TERM_MASK  0x80       /* Device terminate Ultra-DMA-in transfer interrupt mask */
#define         UDMAOUT_TERM_MASK  0x100      /* Device terminate Ultra-DMA-out transfer interrupt mask */

/* Bit masks for ATAPI_INT_STATUS */

#define             ATAPI_DEV_INT  0x1        /* Device interrupt status */
#define              PIO_DONE_INT  0x2        /* PIO transfer done interrupt status */
#define            MULTI_DONE_INT  0x4        /* Multi-DMA transfer done interrupt status */
#define           UDMAIN_DONE_INT  0x8        /* Ultra-DMA in transfer done interrupt status */
#define          UDMAOUT_DONE_INT  0x10       /* Ultra-DMA out transfer done interrupt status */
#define        HOST_TERM_XFER_INT  0x20       /* Host terminate current transfer interrupt status */
#define            MULTI_TERM_INT  0x40       /* Device terminate Multi-DMA transfer interrupt status */
#define           UDMAIN_TERM_INT  0x80       /* Device terminate Ultra-DMA-in transfer interrupt status */
#define          UDMAOUT_TERM_INT  0x100      /* Device terminate Ultra-DMA-out transfer interrupt status */

/* Bit masks for ATAPI_LINE_STATUS */

#define                ATAPI_INTR  0x1        /* Device interrupt to host line status */
#define                ATAPI_DASP  0x2        /* Device dasp to host line status */
#define                ATAPI_CS0N  0x4        /* ATAPI chip select 0 line status */
#define                ATAPI_CS1N  0x8        /* ATAPI chip select 1 line status */
#define                ATAPI_ADDR  0x70       /* ATAPI address line status */
#define              ATAPI_DMAREQ  0x80       /* ATAPI DMA request line status */
#define             ATAPI_DMAACKN  0x100      /* ATAPI DMA acknowledge line status */
#define               ATAPI_DIOWN  0x200      /* ATAPI write line status */
#define               ATAPI_DIORN  0x400      /* ATAPI read line status */
#define               ATAPI_IORDY  0x800      /* ATAPI IORDY line status */

/* Bit masks for ATAPI_SM_STATE */

#define                PIO_CSTATE  0xf        /* PIO mode state machine current state */
#define                DMA_CSTATE  0xf0       /* DMA mode state machine current state */
#define             UDMAIN_CSTATE  0xf00      /* Ultra DMA-In mode state machine current state */
#define            UDMAOUT_CSTATE  0xf000     /* ATAPI IORDY line status */

/* Bit masks for ATAPI_TERMINATE */

#define           ATAPI_HOST_TERM  0x1        /* Host terminationation */

/* Bit masks for ATAPI_REG_TIM_0 */

#define                    T2_REG  0xff       /* End of cycle time for register access transfers */
#define                  TEOC_REG  0xff00     /* Selects DIOR/DIOW pulsewidth */

/* Bit masks for ATAPI_PIO_TIM_0 */

#define                    T1_REG  0xf        /* Time from address valid to DIOR/DIOW */
#define                T2_REG_PIO  0xff0      /* DIOR/DIOW pulsewidth */
#define                    T4_REG  0xf000     /* DIOW data hold */

/* Bit masks for ATAPI_PIO_TIM_1 */

#define              TEOC_REG_PIO  0xff       /* End of cycle time for PIO access transfers. */

/* Bit masks for ATAPI_MULTI_TIM_0 */

#define                        TD  0xff       /* DIOR/DIOW asserted pulsewidth */
#define                        TM  0xff00     /* Time from address valid to DIOR/DIOW */

/* Bit masks for ATAPI_MULTI_TIM_1 */

#define                       TKW  0xff       /* Selects DIOW negated pulsewidth */
#define                       TKR  0xff00     /* Selects DIOR negated pulsewidth */

/* Bit masks for ATAPI_MULTI_TIM_2 */

#define                        TH  0xff       /* Selects DIOW data hold */
#define                      TEOC  0xff00     /* Selects end of cycle for DMA */

/* Bit masks for ATAPI_ULTRA_TIM_0 */

#define                      TACK  0xff       /* Selects setup and hold times for TACK */
#define                      TENV  0xff00     /* Selects envelope time */

/* Bit masks for ATAPI_ULTRA_TIM_1 */

#define                      TDVS  0xff       /* Selects data valid setup time */
#define                 TCYC_TDVS  0xff00     /* Selects cycle time - TDVS time */

/* Bit masks for ATAPI_ULTRA_TIM_2 */

#define                       TSS  0xff       /* Selects time from STROBE edge to negation of DMARQ or assertion of STOP */
#define                      TMLI  0xff00     /* Selects interlock time */

/* Bit masks for ATAPI_ULTRA_TIM_3 */

#define                      TZAH  0xff       /* Selects minimum delay required for output */
#define               READY_PAUSE  0xff00     /* Selects ready to pause */

/* Bit masks for TIMER_ENABLE1 */

#define                    TIMEN8  0x1        /* Timer 8 Enable */
#define                    TIMEN9  0x2        /* Timer 9 Enable */
#define                   TIMEN10  0x4        /* Timer 10 Enable */

/* Bit masks for TIMER_DISABLE1 */

#define                   TIMDIS8  0x1        /* Timer 8 Disable */
#define                   TIMDIS9  0x2        /* Timer 9 Disable */
#define                  TIMDIS10  0x4        /* Timer 10 Disable */

/* Bit masks for TIMER_STATUS1 */

#define                    TIMIL8  0x1        /* Timer 8 Interrupt */
#define                    TIMIL9  0x2        /* Timer 9 Interrupt */
#define                   TIMIL10  0x4        /* Timer 10 Interrupt */
#define                 TOVF_ERR8  0x10       /* Timer 8 Counter Overflow */
#define                 TOVF_ERR9  0x20       /* Timer 9 Counter Overflow */
#define                TOVF_ERR10  0x40       /* Timer 10 Counter Overflow */
#define                     TRUN8  0x1000     /* Timer 8 Slave Enable Status */
#define                     TRUN9  0x2000     /* Timer 9 Slave Enable Status */
#define                    TRUN10  0x4000     /* Timer 10 Slave Enable Status */

/* Bit masks for EPPI0 are obtained from common base header for EPPIx (EPPI1 and EPPI2) */

/* Bit masks for USB_FADDR */

#define          FUNCTION_ADDRESS  0x7f       /* Function address */

/* Bit masks for USB_POWER */

#define           ENABLE_SUSPENDM  0x1        /* enable SuspendM output */
#define              SUSPEND_MODE  0x2        /* Suspend Mode indicator */
#define               RESUME_MODE  0x4        /* DMA Mode */
#define                     RESET  0x8        /* Reset indicator */
#define                   HS_MODE  0x10       /* High Speed mode indicator */
#define                 HS_ENABLE  0x20       /* high Speed Enable */
#define                 SOFT_CONN  0x40       /* Soft connect */
#define                ISO_UPDATE  0x80       /* Isochronous update */

/* Bit masks for USB_INTRTX */

#define                    EP0_TX  0x1        /* Tx Endpoint 0 interrupt */
#define                    EP1_TX  0x2        /* Tx Endpoint 1 interrupt */
#define                    EP2_TX  0x4        /* Tx Endpoint 2 interrupt */
#define                    EP3_TX  0x8        /* Tx Endpoint 3 interrupt */
#define                    EP4_TX  0x10       /* Tx Endpoint 4 interrupt */
#define                    EP5_TX  0x20       /* Tx Endpoint 5 interrupt */
#define                    EP6_TX  0x40       /* Tx Endpoint 6 interrupt */
#define                    EP7_TX  0x80       /* Tx Endpoint 7 interrupt */

/* Bit masks for USB_INTRRX */

#define                    EP1_RX  0x2        /* Rx Endpoint 1 interrupt */
#define                    EP2_RX  0x4        /* Rx Endpoint 2 interrupt */
#define                    EP3_RX  0x8        /* Rx Endpoint 3 interrupt */
#define                    EP4_RX  0x10       /* Rx Endpoint 4 interrupt */
#define                    EP5_RX  0x20       /* Rx Endpoint 5 interrupt */
#define                    EP6_RX  0x40       /* Rx Endpoint 6 interrupt */
#define                    EP7_RX  0x80       /* Rx Endpoint 7 interrupt */

/* Bit masks for USB_INTRTXE */

#define                  EP0_TX_E  0x1        /* Endpoint 0 interrupt Enable */
#define                  EP1_TX_E  0x2        /* Tx Endpoint 1 interrupt  Enable */
#define                  EP2_TX_E  0x4        /* Tx Endpoint 2 interrupt  Enable */
#define                  EP3_TX_E  0x8        /* Tx Endpoint 3 interrupt  Enable */
#define                  EP4_TX_E  0x10       /* Tx Endpoint 4 interrupt  Enable */
#define                  EP5_TX_E  0x20       /* Tx Endpoint 5 interrupt  Enable */
#define                  EP6_TX_E  0x40       /* Tx Endpoint 6 interrupt  Enable */
#define                  EP7_TX_E  0x80       /* Tx Endpoint 7 interrupt  Enable */

/* Bit masks for USB_INTRRXE */

#define                  EP1_RX_E  0x2        /* Rx Endpoint 1 interrupt  Enable */
#define                  EP2_RX_E  0x4        /* Rx Endpoint 2 interrupt  Enable */
#define                  EP3_RX_E  0x8        /* Rx Endpoint 3 interrupt  Enable */
#define                  EP4_RX_E  0x10       /* Rx Endpoint 4 interrupt  Enable */
#define                  EP5_RX_E  0x20       /* Rx Endpoint 5 interrupt  Enable */
#define                  EP6_RX_E  0x40       /* Rx Endpoint 6 interrupt  Enable */
#define                  EP7_RX_E  0x80       /* Rx Endpoint 7 interrupt  Enable */

/* Bit masks for USB_INTRUSB */

#define                 SUSPEND_B  0x1        /* Suspend indicator */
#define                  RESUME_B  0x2        /* Resume indicator */
#define          RESET_OR_BABLE_B  0x4        /* Reset/babble indicator */
#define                     SOF_B  0x8        /* Start of frame */
#define                    CONN_B  0x10       /* Connection indicator */
#define                  DISCON_B  0x20       /* Disconnect indicator */
#define             SESSION_REQ_B  0x40       /* Session Request */
#define              VBUS_ERROR_B  0x80       /* Vbus threshold indicator */

/* Bit masks for USB_INTRUSBE */

#define                SUSPEND_BE  0x1        /* Suspend indicator int enable */
#define                 RESUME_BE  0x2        /* Resume indicator int enable */
#define         RESET_OR_BABLE_BE  0x4        /* Reset/babble indicator int enable */
#define                    SOF_BE  0x8        /* Start of frame int enable */
#define                   CONN_BE  0x10       /* Connection indicator int enable */
#define                 DISCON_BE  0x20       /* Disconnect indicator int enable */
#define            SESSION_REQ_BE  0x40       /* Session Request int enable */
#define             VBUS_ERROR_BE  0x80       /* Vbus threshold indicator int enable */

/* Bit masks for USB_FRAME */

#define              FRAME_NUMBER  0x7ff      /* Frame number */

/* Bit masks for USB_INDEX */

#define         SELECTED_ENDPOINT  0xf        /* selected endpoint */

/* Bit masks for USB_GLOBAL_CTL */

#define                GLOBAL_ENA  0x1        /* enables USB module */
#define                EP1_TX_ENA  0x2        /* Transmit endpoint 1 enable */
#define                EP2_TX_ENA  0x4        /* Transmit endpoint 2 enable */
#define                EP3_TX_ENA  0x8        /* Transmit endpoint 3 enable */
#define                EP4_TX_ENA  0x10       /* Transmit endpoint 4 enable */
#define                EP5_TX_ENA  0x20       /* Transmit endpoint 5 enable */
#define                EP6_TX_ENA  0x40       /* Transmit endpoint 6 enable */
#define                EP7_TX_ENA  0x80       /* Transmit endpoint 7 enable */
#define                EP1_RX_ENA  0x100      /* Receive endpoint 1 enable */
#define                EP2_RX_ENA  0x200      /* Receive endpoint 2 enable */
#define                EP3_RX_ENA  0x400      /* Receive endpoint 3 enable */
#define                EP4_RX_ENA  0x800      /* Receive endpoint 4 enable */
#define                EP5_RX_ENA  0x1000     /* Receive endpoint 5 enable */
#define                EP6_RX_ENA  0x2000     /* Receive endpoint 6 enable */
#define                EP7_RX_ENA  0x4000     /* Receive endpoint 7 enable */

/* Bit masks for USB_OTG_DEV_CTL */

#define                   SESSION  0x1        /* session indicator */
#define                  HOST_REQ  0x2        /* Host negotiation request */
#define                 HOST_MODE  0x4        /* indicates USBDRC is a host */
#define                     VBUS0  0x8        /* Vbus level indicator[0] */
#define                     VBUS1  0x10       /* Vbus level indicator[1] */
#define                     LSDEV  0x20       /* Low-speed indicator */
#define                     FSDEV  0x40       /* Full or High-speed indicator */
#define                  B_DEVICE  0x80       /* A' or 'B' device indicator */

/* Bit masks for USB_OTG_VBUS_IRQ */

#define             DRIVE_VBUS_ON  0x1        /* indicator to drive VBUS control circuit */
#define            DRIVE_VBUS_OFF  0x2        /* indicator to shut off charge pump */
#define           CHRG_VBUS_START  0x4        /* indicator for external circuit to start charging VBUS */
#define             CHRG_VBUS_END  0x8        /* indicator for external circuit to end charging VBUS */
#define        DISCHRG_VBUS_START  0x10       /* indicator to start discharging VBUS */
#define          DISCHRG_VBUS_END  0x20       /* indicator to stop discharging VBUS */

/* Bit masks for USB_OTG_VBUS_MASK */

#define         DRIVE_VBUS_ON_ENA  0x1        /* enable DRIVE_VBUS_ON interrupt */
#define        DRIVE_VBUS_OFF_ENA  0x2        /* enable DRIVE_VBUS_OFF interrupt */
#define       CHRG_VBUS_START_ENA  0x4        /* enable CHRG_VBUS_START interrupt */
#define         CHRG_VBUS_END_ENA  0x8        /* enable CHRG_VBUS_END interrupt */
#define    DISCHRG_VBUS_START_ENA  0x10       /* enable DISCHRG_VBUS_START interrupt */
#define      DISCHRG_VBUS_END_ENA  0x20       /* enable DISCHRG_VBUS_END interrupt */

/* Bit masks for USB_CSR0 */

#define                  RXPKTRDY  0x1        /* data packet receive indicator */
#define                  TXPKTRDY  0x2        /* data packet in FIFO indicator */
#define                STALL_SENT  0x4        /* STALL handshake sent */
#define                   DATAEND  0x8        /* Data end indicator */
#define                  SETUPEND  0x10       /* Setup end */
#define                 SENDSTALL  0x20       /* Send STALL handshake */
#define         SERVICED_RXPKTRDY  0x40       /* used to clear the RxPktRdy bit */
#define         SERVICED_SETUPEND  0x80       /* used to clear the SetupEnd bit */
#define                 FLUSHFIFO  0x100      /* flush endpoint FIFO */
#define          STALL_RECEIVED_H  0x4        /* STALL handshake received host mode */
#define                SETUPPKT_H  0x8        /* send Setup token host mode */
#define                   ERROR_H  0x10       /* timeout error indicator host mode */
#define                  REQPKT_H  0x20       /* Request an IN transaction host mode */
#define               STATUSPKT_H  0x40       /* Status stage transaction host mode */
#define             NAK_TIMEOUT_H  0x80       /* EP0 halted after a NAK host mode */

/* Bit masks for USB_COUNT0 */

#define              EP0_RX_COUNT  0x7f       /* number of received bytes in EP0 FIFO */

/* Bit masks for USB_NAKLIMIT0 */

#define             EP0_NAK_LIMIT  0x1f       /* number of frames/micro frames after which EP0 timeouts */

/* Bit masks for USB_TX_MAX_PACKET */

#define         MAX_PACKET_SIZE_T  0x7ff      /* maximum data pay load in a frame */

/* Bit masks for USB_RX_MAX_PACKET */

#define         MAX_PACKET_SIZE_R  0x7ff      /* maximum data pay load in a frame */

/* Bit masks for USB_TXCSR */

#define                TXPKTRDY_T  0x1        /* data packet in FIFO indicator */
#define          FIFO_NOT_EMPTY_T  0x2        /* FIFO not empty */
#define                UNDERRUN_T  0x4        /* TxPktRdy not set  for an IN token */
#define               FLUSHFIFO_T  0x8        /* flush endpoint FIFO */
#define              STALL_SEND_T  0x10       /* issue a Stall handshake */
#define              STALL_SENT_T  0x20       /* Stall handshake transmitted */
#define        CLEAR_DATATOGGLE_T  0x40       /* clear endpoint data toggle */
#define                INCOMPTX_T  0x80       /* indicates that a large packet is split */
#define              DMAREQMODE_T  0x400      /* DMA mode (0 or 1) selection */
#define        FORCE_DATATOGGLE_T  0x800      /* Force data toggle */
#define              DMAREQ_ENA_T  0x1000     /* Enable DMA request for Tx EP */
#define                     ISO_T  0x4000     /* enable Isochronous transfers */
#define                 AUTOSET_T  0x8000     /* allows TxPktRdy to be set automatically */
#define                  ERROR_TH  0x4        /* error condition host mode */
#define         STALL_RECEIVED_TH  0x20       /* Stall handshake received host mode */
#define            NAK_TIMEOUT_TH  0x80       /* NAK timeout host mode */

/* Bit masks for USB_TXCOUNT */

#define                  TX_COUNT  0x1fff     /* Number of bytes to be written to the selected endpoint Tx FIFO */

/* Bit masks for USB_RXCSR */

#define                RXPKTRDY_R  0x1        /* data packet in FIFO indicator */
#define               FIFO_FULL_R  0x2        /* FIFO not empty */
#define                 OVERRUN_R  0x4        /* TxPktRdy not set  for an IN token */
#define               DATAERROR_R  0x8        /* Out packet cannot be loaded into Rx  FIFO */
#define               FLUSHFIFO_R  0x10       /* flush endpoint FIFO */
#define              STALL_SEND_R  0x20       /* issue a Stall handshake */
#define              STALL_SENT_R  0x40       /* Stall handshake transmitted */
#define        CLEAR_DATATOGGLE_R  0x80       /* clear endpoint data toggle */
#define                INCOMPRX_R  0x100      /* indicates that a large packet is split */
#define              DMAREQMODE_R  0x800      /* DMA mode (0 or 1) selection */
#define                 DISNYET_R  0x1000     /* disable Nyet handshakes */
#define              DMAREQ_ENA_R  0x2000     /* Enable DMA request for Tx EP */
#define                     ISO_R  0x4000     /* enable Isochronous transfers */
#define               AUTOCLEAR_R  0x8000     /* allows TxPktRdy to be set automatically */
#define                  ERROR_RH  0x4        /* TxPktRdy not set  for an IN token host mode */
#define                 REQPKT_RH  0x20       /* request an IN transaction host mode */
#define         STALL_RECEIVED_RH  0x40       /* Stall handshake received host mode */
#define               INCOMPRX_RH  0x100      /* indicates that a large packet is split host mode */
#define             DMAREQMODE_RH  0x800      /* DMA mode (0 or 1) selection host mode */
#define                AUTOREQ_RH  0x4000     /* sets ReqPkt automatically host mode */

/* Bit masks for USB_RXCOUNT */

#define                  RX_COUNT  0x1fff     /* Number of received bytes in the packet in the Rx FIFO */

/* Bit masks for USB_TXTYPE */

#define            TARGET_EP_NO_T  0xf        /* EP number */
#define                PROTOCOL_T  0xc        /* transfer type */

/* Bit masks for USB_TXINTERVAL */

#define          TX_POLL_INTERVAL  0xff       /* polling interval for selected Tx EP */

/* Bit masks for USB_RXTYPE */

#define            TARGET_EP_NO_R  0xf        /* EP number */
#define                PROTOCOL_R  0xc        /* transfer type */

/* Bit masks for USB_RXINTERVAL */

#define          RX_POLL_INTERVAL  0xff       /* polling interval for selected Rx EP */

/* Bit masks for USB_DMA_INTERRUPT */

#define                  DMA0_INT  0x1        /* DMA0 pending interrupt */
#define                  DMA1_INT  0x2        /* DMA1 pending interrupt */
#define                  DMA2_INT  0x4        /* DMA2 pending interrupt */
#define                  DMA3_INT  0x8        /* DMA3 pending interrupt */
#define                  DMA4_INT  0x10       /* DMA4 pending interrupt */
#define                  DMA5_INT  0x20       /* DMA5 pending interrupt */
#define                  DMA6_INT  0x40       /* DMA6 pending interrupt */
#define                  DMA7_INT  0x80       /* DMA7 pending interrupt */

/* Bit masks for USB_DMAxCONTROL */

#define                   DMA_ENA  0x1        /* DMA enable */
#define                 DIRECTION  0x2        /* direction of DMA transfer */
#define                      MODE  0x4        /* DMA Bus error */
#define                   INT_ENA  0x8        /* Interrupt enable */
#define                     EPNUM  0xf0       /* EP number */
#define                  BUSERROR  0x100      /* DMA Bus error */

/* Bit masks for USB_DMAxADDRHIGH */

#define             DMA_ADDR_HIGH  0xffff     /* Upper 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxADDRLOW */

#define              DMA_ADDR_LOW  0xffff     /* Lower 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxCOUNTHIGH */

#define            DMA_COUNT_HIGH  0xffff     /* Upper 16-bits of byte count of DMA transfer for DMA master channel */

/* Bit masks for USB_DMAxCOUNTLOW */

#define             DMA_COUNT_LOW  0xffff     /* Lower 16-bits of byte count of DMA transfer for DMA master channel */

/* Bit masks for HMDMAx_CONTROL */

#define                   HMDMAEN  0x1        /* Handshake MDMA Enable */
#define                       REP  0x2        /* Handshake MDMA Request Polarity */
#define                       UTE  0x8        /* Urgency Threshold Enable */
#define                       OIE  0x10       /* Overflow Interrupt Enable */
#define                      BDIE  0x20       /* Block Done Interrupt Enable */
#define                      MBDI  0x40       /* Mask Block Done Interrupt */
#define                       DRQ  0x300      /* Handshake MDMA Request Type */
#define                       RBC  0x1000     /* Force Reload of BCOUNT */
#define                        PS  0x2000     /* Pin Status */
#define                        OI  0x4000     /* Overflow Interrupt Generated */
#define                       BDI  0x8000     /* Block Done Interrupt Generated */

/* ******************************************* */
/*     MULTI BIT MACRO ENUMERATIONS            */
/* ******************************************* */

/* ************************ */
/*   MXVR Address Offsets   */
/* ************************ */

/* Control Message Receive Buffer (CMRB) Address Offsets */

#define CMRB_STRIDE       0x00000016lu

#define CMRB_DST_OFFSET   0x00000000lu
#define CMRB_SRC_OFFSET   0x00000002lu
#define CMRB_DATA_OFFSET  0x00000005lu

/* Control Message Transmit Buffer (CMTB) Address Offsets */

#define CMTB_PRIO_OFFSET    0x00000000lu
#define CMTB_DST_OFFSET     0x00000002lu
#define CMTB_SRC_OFFSET     0x00000004lu
#define CMTB_TYPE_OFFSET    0x00000006lu
#define CMTB_DATA_OFFSET    0x00000007lu

#define CMTB_ANSWER_OFFSET  0x0000000Alu

#define CMTB_STAT_N_OFFSET  0x00000018lu
#define CMTB_STAT_A_OFFSET  0x00000016lu
#define CMTB_STAT_D_OFFSET  0x0000000Elu
#define CMTB_STAT_R_OFFSET  0x00000014lu
#define CMTB_STAT_W_OFFSET  0x00000014lu
#define CMTB_STAT_G_OFFSET  0x00000014lu

/* Asynchronous Packet Receive Buffer (APRB) Address Offsets */

#define APRB_STRIDE       0x00000400lu

#define APRB_DST_OFFSET   0x00000000lu
#define APRB_LEN_OFFSET   0x00000002lu
#define APRB_SRC_OFFSET   0x00000004lu
#define APRB_DATA_OFFSET  0x00000006lu

/* Asynchronous Packet Transmit Buffer (APTB) Address Offsets */

#define APTB_PRIO_OFFSET  0x00000000lu
#define APTB_DST_OFFSET   0x00000002lu
#define APTB_LEN_OFFSET   0x00000004lu
#define APTB_SRC_OFFSET   0x00000006lu
#define APTB_DATA_OFFSET  0x00000008lu

/* Remote Read Buffer (RRDB) Address Offsets */

#define RRDB_WADDR_OFFSET 0x00000100lu
#define RRDB_WLEN_OFFSET  0x00000101lu

/* **************** */
/*   MXVR Macros    */
/* **************** */

/* MXVR_CONFIG Macros */

#define SET_MSB(x)       ( ( (x) & 0xF  ) << 9)

/* MXVR_INT_STAT_1 Macros */

#define DONEX(x)         (0x00000002 << (4 * (x)))
#define HDONEX(x)        (0x00000001 << (4 * (x)))

/* MXVR_INT_EN_1 Macros */

#define DONEENX(x)       (0x00000002 << (4 * (x)))
#define HDONEENX(x)      (0x00000001 << (4 * (x)))

/* MXVR_CDRPLL_CTL Macros */

#define SET_CDRSHPSEL(x) ( ( (x) & 0x3F ) << 16)

/* MXVR_FMPLL_CTL Macros */

#define SET_CDRCPSEL(x)  ( ( (x) & 0xFF ) << 24)
#define SET_FMCPSEL(x)   ( ( (x) & 0xFF ) << 24)

#endif /* _DEF_BF549_H */
