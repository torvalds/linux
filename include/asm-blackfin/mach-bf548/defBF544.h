/*
 * File:         include/asm-blackfin/mach-bf548/defBF544.h
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

#ifndef _DEF_BF544_H
#define _DEF_BF544_H

/* Include all Core registers and bit definitions */
#include <asm/mach-common/def_LPBlackfin.h>

/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF544 */

/* Include defBF54x_base.h for the set of #defines that are common to all ADSP-BF54x processors */
#include "defBF54x_base.h"

/* The following are the #defines needed by ADSP-BF544 that are not in the common header */

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

/* Two Wire Interface Registers (TWI1) */

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

/* HOST Port Registers */

#define                     HOST_CONTROL  0xffc03a00   /* HOST Control Register */
#define                      HOST_STATUS  0xffc03a04   /* HOST Status Register */
#define                     HOST_TIMEOUT  0xffc03a08   /* HOST Acknowledge Mode Timeout Register */

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
#define                  nPIXC_EN  0x0       
#define                  OVR_A_EN  0x2        /* Overlay A Enable */
#define                 nOVR_A_EN  0x0       
#define                  OVR_B_EN  0x4        /* Overlay B Enable */
#define                 nOVR_B_EN  0x0       
#define                  IMG_FORM  0x8        /* Image Data Format */
#define                 nIMG_FORM  0x0       
#define                  OVR_FORM  0x10       /* Overlay Data Format */
#define                 nOVR_FORM  0x0       
#define                  OUT_FORM  0x20       /* Output Data Format */
#define                 nOUT_FORM  0x0       
#define                   UDS_MOD  0x40       /* Resampling Mode */
#define                  nUDS_MOD  0x0       
#define                     TC_EN  0x80       /* Transparent Color Enable */
#define                    nTC_EN  0x0       
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
#define               nOVR_INT_EN  0x0       
#define                FRM_INT_EN  0x2        /* Interrupt at End of Frame */
#define               nFRM_INT_EN  0x0       
#define              OVR_INT_STAT  0x4        /* Overlay Interrupt Status */
#define             nOVR_INT_STAT  0x0       
#define              FRM_INT_STAT  0x8        /* Frame Interrupt Status */
#define             nFRM_INT_STAT  0x0       

/* Bit masks for PIXC_RYCON */

#define                       A11  0x3ff      /* A11 in the Coefficient Matrix */
#define                       A12  0xffc00    /* A12 in the Coefficient Matrix */
#define                       A13  0x3ff00000 /* A13 in the Coefficient Matrix */
#define                  RY_MULT4  0x40000000 /* Multiply Row by 4 */
#define                 nRY_MULT4  0x0       

/* Bit masks for PIXC_GUCON */

#define                       A21  0x3ff      /* A21 in the Coefficient Matrix */
#define                       A22  0xffc00    /* A22 in the Coefficient Matrix */
#define                       A23  0x3ff00000 /* A23 in the Coefficient Matrix */
#define                  GU_MULT4  0x40000000 /* Multiply Row by 4 */
#define                 nGU_MULT4  0x0       

/* Bit masks for PIXC_BVCON */

#define                       A31  0x3ff      /* A31 in the Coefficient Matrix */
#define                       A32  0xffc00    /* A32 in the Coefficient Matrix */
#define                       A33  0x3ff00000 /* A33 in the Coefficient Matrix */
#define                  BV_MULT4  0x40000000 /* Multiply Row by 4 */
#define                 nBV_MULT4  0x0       

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
#define                  nHOST_EN  0x0       
#define                  HOST_END  0x2        /* Host Endianess */
#define                 nHOST_END  0x0       
#define                 DATA_SIZE  0x4        /* Data Size */
#define                nDATA_SIZE  0x0       
#define                  HOST_RST  0x8        /* Host Reset */
#define                 nHOST_RST  0x0       
#define                  HRDY_OVR  0x20       /* Host Ready Override */
#define                 nHRDY_OVR  0x0       
#define                  INT_MODE  0x40       /* Interrupt Mode */
#define                 nINT_MODE  0x0       
#define                     BT_EN  0x80       /* Bus Timeout Enable */
#define                    nBT_EN  0x0       
#define                       EHW  0x100      /* Enable Host Write */
#define                      nEHW  0x0       
#define                       EHR  0x200      /* Enable Host Read */
#define                      nEHR  0x0       
#define                       BDR  0x400      /* Burst DMA Requests */
#define                      nBDR  0x0       

/* Bit masks for HOST_STATUS */

#define                     READY  0x1        /* DMA Ready */
#define                    nREADY  0x0       
#define                  FIFOFULL  0x2        /* FIFO Full */
#define                 nFIFOFULL  0x0       
#define                 FIFOEMPTY  0x4        /* FIFO Empty */
#define                nFIFOEMPTY  0x0       
#define                  COMPLETE  0x8        /* DMA Complete */
#define                 nCOMPLETE  0x0       
#define                      HSHK  0x10       /* Host Handshake */
#define                     nHSHK  0x0       
#define                   TIMEOUT  0x20       /* Host Timeout */
#define                  nTIMEOUT  0x0       
#define                      HIRQ  0x40       /* Host Interrupt Request */
#define                     nHIRQ  0x0       
#define                ALLOW_CNFG  0x80       /* Allow New Configuration */
#define               nALLOW_CNFG  0x0       
#define                   DMA_DIR  0x100      /* DMA Direction */
#define                  nDMA_DIR  0x0       
#define                       BTE  0x200      /* Bus Timeout Enabled */
#define                      nBTE  0x0       

/* Bit masks for HOST_TIMEOUT */

#define             COUNT_TIMEOUT  0x7ff      /* Host Timeout count */

/* Bit masks for TIMER_ENABLE1 */

#define                    TIMEN8  0x1        /* Timer 8 Enable */
#define                   nTIMEN8  0x0       
#define                    TIMEN9  0x2        /* Timer 9 Enable */
#define                   nTIMEN9  0x0       
#define                   TIMEN10  0x4        /* Timer 10 Enable */
#define                  nTIMEN10  0x0       

/* Bit masks for TIMER_DISABLE1 */

#define                   TIMDIS8  0x1        /* Timer 8 Disable */
#define                  nTIMDIS8  0x0       
#define                   TIMDIS9  0x2        /* Timer 9 Disable */
#define                  nTIMDIS9  0x0       
#define                  TIMDIS10  0x4        /* Timer 10 Disable */
#define                 nTIMDIS10  0x0       

/* Bit masks for TIMER_STATUS1 */

#define                    TIMIL8  0x1        /* Timer 8 Interrupt */
#define                   nTIMIL8  0x0       
#define                    TIMIL9  0x2        /* Timer 9 Interrupt */
#define                   nTIMIL9  0x0       
#define                   TIMIL10  0x4        /* Timer 10 Interrupt */
#define                  nTIMIL10  0x0       
#define                 TOVF_ERR8  0x10       /* Timer 8 Counter Overflow */
#define                nTOVF_ERR8  0x0       
#define                 TOVF_ERR9  0x20       /* Timer 9 Counter Overflow */
#define                nTOVF_ERR9  0x0       
#define                TOVF_ERR10  0x40       /* Timer 10 Counter Overflow */
#define               nTOVF_ERR10  0x0       
#define                     TRUN8  0x1000     /* Timer 8 Slave Enable Status */
#define                    nTRUN8  0x0       
#define                     TRUN9  0x2000     /* Timer 9 Slave Enable Status */
#define                    nTRUN9  0x0       
#define                    TRUN10  0x4000     /* Timer 10 Slave Enable Status */
#define                   nTRUN10  0x0       

/* Bit masks for EPPI0 are obtained from common base header for EPPIx (EPPI1 and EPPI2) */

/* Bit masks for HMDMAx_CONTROL */

#define                   HMDMAEN  0x1        /* Handshake MDMA Enable */
#define                  nHMDMAEN  0x0       
#define                       REP  0x2        /* Handshake MDMA Request Polarity */
#define                      nREP  0x0       
#define                       UTE  0x8        /* Urgency Threshold Enable */
#define                      nUTE  0x0       
#define                       OIE  0x10       /* Overflow Interrupt Enable */
#define                      nOIE  0x0       
#define                      BDIE  0x20       /* Block Done Interrupt Enable */
#define                     nBDIE  0x0       
#define                      MBDI  0x40       /* Mask Block Done Interrupt */
#define                     nMBDI  0x0       
#define                       DRQ  0x300      /* Handshake MDMA Request Type */
#define                       RBC  0x1000     /* Force Reload of BCOUNT */
#define                      nRBC  0x0       
#define                        PS  0x2000     /* Pin Status */
#define                       nPS  0x0       
#define                        OI  0x4000     /* Overflow Interrupt Generated */
#define                       nOI  0x0       
#define                       BDI  0x8000     /* Block Done Interrupt Generated */
#define                      nBDI  0x0       

/* ******************************************* */
/*     MULTI BIT MACRO ENUMERATIONS            */
/* ******************************************* */

#endif /* _DEF_BF544_H */
