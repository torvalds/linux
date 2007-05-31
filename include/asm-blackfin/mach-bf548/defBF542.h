/*
 * File:         include/asm-blackfin/mach-bf548/defBF542.h
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

#ifndef _DEF_BF542_H
#define _DEF_BF542_H

/* Include all Core registers and bit definitions */
#include <asm/mach-common/def_LPBlackfin.h>

/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF542 */

/* Include defBF54x_base.h for the set of #defines that are common to all ADSP-BF54x processors */
#include "defBF54x_base.h"

/* The following are the #defines needed by ADSP-BF542 that are not in the common header */

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


/* ********************************************************** */
/*     SINGLE BIT MACRO PAIRS (bit mask and negated one)      */
/*     and MULTI BIT READ MACROS                              */
/* ********************************************************** */

/* Bit masks for KPAD_CTL */

#define                   KPAD_EN  0x1        /* Keypad Enable */
#define                  nKPAD_EN  0x0       
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
#define                 nKPAD_IRQ  0x0       
#define              KPAD_MROWCOL  0x6        /* Multiple Row/Column Keypress Status */
#define              KPAD_PRESSED  0x8        /* Key press current status */
#define             nKPAD_PRESSED  0x0       

/* Bit masks for KPAD_SOFTEVAL */

#define           KPAD_SOFTEVAL_E  0x2        /* Software Programmable Force Evaluate */
#define          nKPAD_SOFTEVAL_E  0x0       

/* Bit masks for SDH_COMMAND */

#define                   CMD_IDX  0x3f       /* Command Index */
#define                   CMD_RSP  0x40       /* Response */
#define                  nCMD_RSP  0x0       
#define                 CMD_L_RSP  0x80       /* Long Response */
#define                nCMD_L_RSP  0x0       
#define                 CMD_INT_E  0x100      /* Command Interrupt */
#define                nCMD_INT_E  0x0       
#define                CMD_PEND_E  0x200      /* Command Pending */
#define               nCMD_PEND_E  0x0       
#define                     CMD_E  0x400      /* Command Enable */
#define                    nCMD_E  0x0       

/* Bit masks for SDH_PWR_CTL */

#define                    PWR_ON  0x3        /* Power On */
#if 0
#define                       TBD  0x3c       /* TBD */
#endif
#define                 SD_CMD_OD  0x40       /* Open Drain Output */
#define                nSD_CMD_OD  0x0       
#define                   ROD_CTL  0x80       /* Rod Control */
#define                  nROD_CTL  0x0       

/* Bit masks for SDH_CLK_CTL */

#define                    CLKDIV  0xff       /* MC_CLK Divisor */
#define                     CLK_E  0x100      /* MC_CLK Bus Clock Enable */
#define                    nCLK_E  0x0       
#define                  PWR_SV_E  0x200      /* Power Save Enable */
#define                 nPWR_SV_E  0x0       
#define             CLKDIV_BYPASS  0x400      /* Bypass Divisor */
#define            nCLKDIV_BYPASS  0x0       
#define                  WIDE_BUS  0x800      /* Wide Bus Mode Enable */
#define                 nWIDE_BUS  0x0       

/* Bit masks for SDH_RESP_CMD */

#define                  RESP_CMD  0x3f       /* Response Command */

/* Bit masks for SDH_DATA_CTL */

#define                     DTX_E  0x1        /* Data Transfer Enable */
#define                    nDTX_E  0x0       
#define                   DTX_DIR  0x2        /* Data Transfer Direction */
#define                  nDTX_DIR  0x0       
#define                  DTX_MODE  0x4        /* Data Transfer Mode */
#define                 nDTX_MODE  0x0       
#define                 DTX_DMA_E  0x8        /* Data Transfer DMA Enable */
#define                nDTX_DMA_E  0x0       
#define              DTX_BLK_LGTH  0xf0       /* Data Transfer Block Length */

/* Bit masks for SDH_STATUS */

#define              CMD_CRC_FAIL  0x1        /* CMD CRC Fail */
#define             nCMD_CRC_FAIL  0x0       
#define              DAT_CRC_FAIL  0x2        /* Data CRC Fail */
#define             nDAT_CRC_FAIL  0x0       
#define               CMD_TIMEOUT  0x4        /* CMD Time Out */
#define              nCMD_TIMEOUT  0x0       
#define               DAT_TIMEOUT  0x8        /* Data Time Out */
#define              nDAT_TIMEOUT  0x0       
#define               TX_UNDERRUN  0x10       /* Transmit Underrun */
#define              nTX_UNDERRUN  0x0       
#define                RX_OVERRUN  0x20       /* Receive Overrun */
#define               nRX_OVERRUN  0x0       
#define              CMD_RESP_END  0x40       /* CMD Response End */
#define             nCMD_RESP_END  0x0       
#define                  CMD_SENT  0x80       /* CMD Sent */
#define                 nCMD_SENT  0x0       
#define                   DAT_END  0x100      /* Data End */
#define                  nDAT_END  0x0       
#define             START_BIT_ERR  0x200      /* Start Bit Error */
#define            nSTART_BIT_ERR  0x0       
#define               DAT_BLK_END  0x400      /* Data Block End */
#define              nDAT_BLK_END  0x0       
#define                   CMD_ACT  0x800      /* CMD Active */
#define                  nCMD_ACT  0x0       
#define                    TX_ACT  0x1000     /* Transmit Active */
#define                   nTX_ACT  0x0       
#define                    RX_ACT  0x2000     /* Receive Active */
#define                   nRX_ACT  0x0       
#define              TX_FIFO_STAT  0x4000     /* Transmit FIFO Status */
#define             nTX_FIFO_STAT  0x0       
#define              RX_FIFO_STAT  0x8000     /* Receive FIFO Status */
#define             nRX_FIFO_STAT  0x0       
#define              TX_FIFO_FULL  0x10000    /* Transmit FIFO Full */
#define             nTX_FIFO_FULL  0x0       
#define              RX_FIFO_FULL  0x20000    /* Receive FIFO Full */
#define             nRX_FIFO_FULL  0x0       
#define              TX_FIFO_ZERO  0x40000    /* Transmit FIFO Empty */
#define             nTX_FIFO_ZERO  0x0       
#define               RX_DAT_ZERO  0x80000    /* Receive FIFO Empty */
#define              nRX_DAT_ZERO  0x0       
#define                TX_DAT_RDY  0x100000   /* Transmit Data Available */
#define               nTX_DAT_RDY  0x0       
#define               RX_FIFO_RDY  0x200000   /* Receive Data Available */
#define              nRX_FIFO_RDY  0x0       

/* Bit masks for SDH_STATUS_CLR */

#define         CMD_CRC_FAIL_STAT  0x1        /* CMD CRC Fail Status */
#define        nCMD_CRC_FAIL_STAT  0x0       
#define         DAT_CRC_FAIL_STAT  0x2        /* Data CRC Fail Status */
#define        nDAT_CRC_FAIL_STAT  0x0       
#define          CMD_TIMEOUT_STAT  0x4        /* CMD Time Out Status */
#define         nCMD_TIMEOUT_STAT  0x0       
#define          DAT_TIMEOUT_STAT  0x8        /* Data Time Out status */
#define         nDAT_TIMEOUT_STAT  0x0       
#define          TX_UNDERRUN_STAT  0x10       /* Transmit Underrun Status */
#define         nTX_UNDERRUN_STAT  0x0       
#define           RX_OVERRUN_STAT  0x20       /* Receive Overrun Status */
#define          nRX_OVERRUN_STAT  0x0       
#define         CMD_RESP_END_STAT  0x40       /* CMD Response End Status */
#define        nCMD_RESP_END_STAT  0x0       
#define             CMD_SENT_STAT  0x80       /* CMD Sent Status */
#define            nCMD_SENT_STAT  0x0       
#define              DAT_END_STAT  0x100      /* Data End Status */
#define             nDAT_END_STAT  0x0       
#define        START_BIT_ERR_STAT  0x200      /* Start Bit Error Status */
#define       nSTART_BIT_ERR_STAT  0x0       
#define          DAT_BLK_END_STAT  0x400      /* Data Block End Status */
#define         nDAT_BLK_END_STAT  0x0       

/* Bit masks for SDH_MASK0 */

#define         CMD_CRC_FAIL_MASK  0x1        /* CMD CRC Fail Mask */
#define        nCMD_CRC_FAIL_MASK  0x0       
#define         DAT_CRC_FAIL_MASK  0x2        /* Data CRC Fail Mask */
#define        nDAT_CRC_FAIL_MASK  0x0       
#define          CMD_TIMEOUT_MASK  0x4        /* CMD Time Out Mask */
#define         nCMD_TIMEOUT_MASK  0x0       
#define          DAT_TIMEOUT_MASK  0x8        /* Data Time Out Mask */
#define         nDAT_TIMEOUT_MASK  0x0       
#define          TX_UNDERRUN_MASK  0x10       /* Transmit Underrun Mask */
#define         nTX_UNDERRUN_MASK  0x0       
#define           RX_OVERRUN_MASK  0x20       /* Receive Overrun Mask */
#define          nRX_OVERRUN_MASK  0x0       
#define         CMD_RESP_END_MASK  0x40       /* CMD Response End Mask */
#define        nCMD_RESP_END_MASK  0x0       
#define             CMD_SENT_MASK  0x80       /* CMD Sent Mask */
#define            nCMD_SENT_MASK  0x0       
#define              DAT_END_MASK  0x100      /* Data End Mask */
#define             nDAT_END_MASK  0x0       
#define        START_BIT_ERR_MASK  0x200      /* Start Bit Error Mask */
#define       nSTART_BIT_ERR_MASK  0x0       
#define          DAT_BLK_END_MASK  0x400      /* Data Block End Mask */
#define         nDAT_BLK_END_MASK  0x0       
#define              CMD_ACT_MASK  0x800      /* CMD Active Mask */
#define             nCMD_ACT_MASK  0x0       
#define               TX_ACT_MASK  0x1000     /* Transmit Active Mask */
#define              nTX_ACT_MASK  0x0       
#define               RX_ACT_MASK  0x2000     /* Receive Active Mask */
#define              nRX_ACT_MASK  0x0       
#define         TX_FIFO_STAT_MASK  0x4000     /* Transmit FIFO Status Mask */
#define        nTX_FIFO_STAT_MASK  0x0       
#define         RX_FIFO_STAT_MASK  0x8000     /* Receive FIFO Status Mask */
#define        nRX_FIFO_STAT_MASK  0x0       
#define         TX_FIFO_FULL_MASK  0x10000    /* Transmit FIFO Full Mask */
#define        nTX_FIFO_FULL_MASK  0x0       
#define         RX_FIFO_FULL_MASK  0x20000    /* Receive FIFO Full Mask */
#define        nRX_FIFO_FULL_MASK  0x0       
#define         TX_FIFO_ZERO_MASK  0x40000    /* Transmit FIFO Empty Mask */
#define        nTX_FIFO_ZERO_MASK  0x0       
#define          RX_DAT_ZERO_MASK  0x80000    /* Receive FIFO Empty Mask */
#define         nRX_DAT_ZERO_MASK  0x0       
#define           TX_DAT_RDY_MASK  0x100000   /* Transmit Data Available Mask */
#define          nTX_DAT_RDY_MASK  0x0       
#define          RX_FIFO_RDY_MASK  0x200000   /* Receive Data Available Mask */
#define         nRX_FIFO_RDY_MASK  0x0       

/* Bit masks for SDH_FIFO_CNT */

#define                FIFO_COUNT  0x7fff     /* FIFO Count */

/* Bit masks for SDH_E_STATUS */

#define              SDIO_INT_DET  0x2        /* SDIO Int Detected */
#define             nSDIO_INT_DET  0x0       
#define               SD_CARD_DET  0x10       /* SD Card Detect */
#define              nSD_CARD_DET  0x0       

/* Bit masks for SDH_E_MASK */

#define                  SDIO_MSK  0x2        /* Mask SDIO Int Detected */
#define                 nSDIO_MSK  0x0       
#define                   SCD_MSK  0x40       /* Mask Card Detect */
#define                  nSCD_MSK  0x0       

/* Bit masks for SDH_CFG */

#define                   CLKS_EN  0x1        /* Clocks Enable */
#define                  nCLKS_EN  0x0       
#define                      SD4E  0x4        /* SDIO 4-Bit Enable */
#define                     nSD4E  0x0       
#define                       MWE  0x8        /* Moving Window Enable */
#define                      nMWE  0x0       
#define                    SD_RST  0x10       /* SDMMC Reset */
#define                   nSD_RST  0x0       
#define                 PUP_SDDAT  0x20       /* Pull-up SD_DAT */
#define                nPUP_SDDAT  0x0       
#define                PUP_SDDAT3  0x40       /* Pull-up SD_DAT3 */
#define               nPUP_SDDAT3  0x0       
#define                 PD_SDDAT3  0x80       /* Pull-down SD_DAT3 */
#define                nPD_SDDAT3  0x0       

/* Bit masks for SDH_RD_WAIT_EN */

#define                       RWR  0x1        /* Read Wait Request */
#define                      nRWR  0x0       

/* Bit masks for ATAPI_CONTROL */

#define                 PIO_START  0x1        /* Start PIO/Reg Op */
#define                nPIO_START  0x0       
#define               MULTI_START  0x2        /* Start Multi-DMA Op */
#define              nMULTI_START  0x0       
#define               ULTRA_START  0x4        /* Start Ultra-DMA Op */
#define              nULTRA_START  0x0       
#define                  XFER_DIR  0x8        /* Transfer Direction */
#define                 nXFER_DIR  0x0       
#define                  IORDY_EN  0x10       /* IORDY Enable */
#define                 nIORDY_EN  0x0       
#define                FIFO_FLUSH  0x20       /* Flush FIFOs */
#define               nFIFO_FLUSH  0x0       
#define                  SOFT_RST  0x40       /* Soft Reset */
#define                 nSOFT_RST  0x0       
#define                   DEV_RST  0x80       /* Device Reset */
#define                  nDEV_RST  0x0       
#define                TFRCNT_RST  0x100      /* Trans Count Reset */
#define               nTFRCNT_RST  0x0       
#define               END_ON_TERM  0x200      /* End/Terminate Select */
#define              nEND_ON_TERM  0x0       
#define               PIO_USE_DMA  0x400      /* PIO-DMA Enable */
#define              nPIO_USE_DMA  0x0       
#define          UDMAIN_FIFO_THRS  0xf000     /* Ultra DMA-IN FIFO Threshold */

/* Bit masks for ATAPI_STATUS */

#define               PIO_XFER_ON  0x1        /* PIO transfer in progress */
#define              nPIO_XFER_ON  0x0       
#define             MULTI_XFER_ON  0x2        /* Multi-word DMA transfer in progress */
#define            nMULTI_XFER_ON  0x0       
#define             ULTRA_XFER_ON  0x4        /* Ultra DMA transfer in progress */
#define            nULTRA_XFER_ON  0x0       
#define               ULTRA_IN_FL  0xf0       /* Ultra DMA Input FIFO Level */

/* Bit masks for ATAPI_DEV_ADDR */

#define                  DEV_ADDR  0x1f       /* Device Address */

/* Bit masks for ATAPI_INT_MASK */

#define        ATAPI_DEV_INT_MASK  0x1        /* Device interrupt mask */
#define       nATAPI_DEV_INT_MASK  0x0       
#define             PIO_DONE_MASK  0x2        /* PIO transfer done interrupt mask */
#define            nPIO_DONE_MASK  0x0       
#define           MULTI_DONE_MASK  0x4        /* Multi-DMA transfer done interrupt mask */
#define          nMULTI_DONE_MASK  0x0       
#define          UDMAIN_DONE_MASK  0x8        /* Ultra-DMA in transfer done interrupt mask */
#define         nUDMAIN_DONE_MASK  0x0       
#define         UDMAOUT_DONE_MASK  0x10       /* Ultra-DMA out transfer done interrupt mask */
#define        nUDMAOUT_DONE_MASK  0x0       
#define       HOST_TERM_XFER_MASK  0x20       /* Host terminate current transfer interrupt mask */
#define      nHOST_TERM_XFER_MASK  0x0       
#define           MULTI_TERM_MASK  0x40       /* Device terminate Multi-DMA transfer interrupt mask */
#define          nMULTI_TERM_MASK  0x0       
#define          UDMAIN_TERM_MASK  0x80       /* Device terminate Ultra-DMA-in transfer interrupt mask */
#define         nUDMAIN_TERM_MASK  0x0       
#define         UDMAOUT_TERM_MASK  0x100      /* Device terminate Ultra-DMA-out transfer interrupt mask */
#define        nUDMAOUT_TERM_MASK  0x0       

/* Bit masks for ATAPI_INT_STATUS */

#define             ATAPI_DEV_INT  0x1        /* Device interrupt status */
#define            nATAPI_DEV_INT  0x0       
#define              PIO_DONE_INT  0x2        /* PIO transfer done interrupt status */
#define             nPIO_DONE_INT  0x0       
#define            MULTI_DONE_INT  0x4        /* Multi-DMA transfer done interrupt status */
#define           nMULTI_DONE_INT  0x0       
#define           UDMAIN_DONE_INT  0x8        /* Ultra-DMA in transfer done interrupt status */
#define          nUDMAIN_DONE_INT  0x0       
#define          UDMAOUT_DONE_INT  0x10       /* Ultra-DMA out transfer done interrupt status */
#define         nUDMAOUT_DONE_INT  0x0       
#define        HOST_TERM_XFER_INT  0x20       /* Host terminate current transfer interrupt status */
#define       nHOST_TERM_XFER_INT  0x0       
#define            MULTI_TERM_INT  0x40       /* Device terminate Multi-DMA transfer interrupt status */
#define           nMULTI_TERM_INT  0x0       
#define           UDMAIN_TERM_INT  0x80       /* Device terminate Ultra-DMA-in transfer interrupt status */
#define          nUDMAIN_TERM_INT  0x0       
#define          UDMAOUT_TERM_INT  0x100      /* Device terminate Ultra-DMA-out transfer interrupt status */
#define         nUDMAOUT_TERM_INT  0x0       

/* Bit masks for ATAPI_LINE_STATUS */

#define                ATAPI_INTR  0x1        /* Device interrupt to host line status */
#define               nATAPI_INTR  0x0       
#define                ATAPI_DASP  0x2        /* Device dasp to host line status */
#define               nATAPI_DASP  0x0       
#define                ATAPI_CS0N  0x4        /* ATAPI chip select 0 line status */
#define               nATAPI_CS0N  0x0       
#define                ATAPI_CS1N  0x8        /* ATAPI chip select 1 line status */
#define               nATAPI_CS1N  0x0       
#define                ATAPI_ADDR  0x70       /* ATAPI address line status */
#define              ATAPI_DMAREQ  0x80       /* ATAPI DMA request line status */
#define             nATAPI_DMAREQ  0x0       
#define             ATAPI_DMAACKN  0x100      /* ATAPI DMA acknowledge line status */
#define            nATAPI_DMAACKN  0x0       
#define               ATAPI_DIOWN  0x200      /* ATAPI write line status */
#define              nATAPI_DIOWN  0x0       
#define               ATAPI_DIORN  0x400      /* ATAPI read line status */
#define              nATAPI_DIORN  0x0       
#define               ATAPI_IORDY  0x800      /* ATAPI IORDY line status */
#define              nATAPI_IORDY  0x0       

/* Bit masks for ATAPI_SM_STATE */

#define                PIO_CSTATE  0xf        /* PIO mode state machine current state */
#define                DMA_CSTATE  0xf0       /* DMA mode state machine current state */
#define             UDMAIN_CSTATE  0xf00      /* Ultra DMA-In mode state machine current state */
#define            UDMAOUT_CSTATE  0xf000     /* ATAPI IORDY line status */

/* Bit masks for ATAPI_TERMINATE */

#define           ATAPI_HOST_TERM  0x1        /* Host terminationation */
#define          nATAPI_HOST_TERM  0x0       

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

/* Bit masks for USB_FADDR */

#define          FUNCTION_ADDRESS  0x7f       /* Function address */

/* Bit masks for USB_POWER */

#define           ENABLE_SUSPENDM  0x1        /* enable SuspendM output */
#define          nENABLE_SUSPENDM  0x0       
#define              SUSPEND_MODE  0x2        /* Suspend Mode indicator */
#define             nSUSPEND_MODE  0x0       
#define               RESUME_MODE  0x4        /* DMA Mode */
#define              nRESUME_MODE  0x0       
#define                     RESET  0x8        /* Reset indicator */
#define                    nRESET  0x0       
#define                   HS_MODE  0x10       /* High Speed mode indicator */
#define                  nHS_MODE  0x0       
#define                 HS_ENABLE  0x20       /* high Speed Enable */
#define                nHS_ENABLE  0x0       
#define                 SOFT_CONN  0x40       /* Soft connect */
#define                nSOFT_CONN  0x0       
#define                ISO_UPDATE  0x80       /* Isochronous update */
#define               nISO_UPDATE  0x0       

/* Bit masks for USB_INTRTX */

#define                    EP0_TX  0x1        /* Tx Endpoint 0 interrupt */
#define                   nEP0_TX  0x0       
#define                    EP1_TX  0x2        /* Tx Endpoint 1 interrupt */
#define                   nEP1_TX  0x0       
#define                    EP2_TX  0x4        /* Tx Endpoint 2 interrupt */
#define                   nEP2_TX  0x0       
#define                    EP3_TX  0x8        /* Tx Endpoint 3 interrupt */
#define                   nEP3_TX  0x0       
#define                    EP4_TX  0x10       /* Tx Endpoint 4 interrupt */
#define                   nEP4_TX  0x0       
#define                    EP5_TX  0x20       /* Tx Endpoint 5 interrupt */
#define                   nEP5_TX  0x0       
#define                    EP6_TX  0x40       /* Tx Endpoint 6 interrupt */
#define                   nEP6_TX  0x0       
#define                    EP7_TX  0x80       /* Tx Endpoint 7 interrupt */
#define                   nEP7_TX  0x0       

/* Bit masks for USB_INTRRX */

#define                    EP1_RX  0x2        /* Rx Endpoint 1 interrupt */
#define                   nEP1_RX  0x0       
#define                    EP2_RX  0x4        /* Rx Endpoint 2 interrupt */
#define                   nEP2_RX  0x0       
#define                    EP3_RX  0x8        /* Rx Endpoint 3 interrupt */
#define                   nEP3_RX  0x0       
#define                    EP4_RX  0x10       /* Rx Endpoint 4 interrupt */
#define                   nEP4_RX  0x0       
#define                    EP5_RX  0x20       /* Rx Endpoint 5 interrupt */
#define                   nEP5_RX  0x0       
#define                    EP6_RX  0x40       /* Rx Endpoint 6 interrupt */
#define                   nEP6_RX  0x0       
#define                    EP7_RX  0x80       /* Rx Endpoint 7 interrupt */
#define                   nEP7_RX  0x0       

/* Bit masks for USB_INTRTXE */

#define                  EP0_TX_E  0x1        /* Endpoint 0 interrupt Enable */
#define                 nEP0_TX_E  0x0       
#define                  EP1_TX_E  0x2        /* Tx Endpoint 1 interrupt  Enable */
#define                 nEP1_TX_E  0x0       
#define                  EP2_TX_E  0x4        /* Tx Endpoint 2 interrupt  Enable */
#define                 nEP2_TX_E  0x0       
#define                  EP3_TX_E  0x8        /* Tx Endpoint 3 interrupt  Enable */
#define                 nEP3_TX_E  0x0       
#define                  EP4_TX_E  0x10       /* Tx Endpoint 4 interrupt  Enable */
#define                 nEP4_TX_E  0x0       
#define                  EP5_TX_E  0x20       /* Tx Endpoint 5 interrupt  Enable */
#define                 nEP5_TX_E  0x0       
#define                  EP6_TX_E  0x40       /* Tx Endpoint 6 interrupt  Enable */
#define                 nEP6_TX_E  0x0       
#define                  EP7_TX_E  0x80       /* Tx Endpoint 7 interrupt  Enable */
#define                 nEP7_TX_E  0x0       

/* Bit masks for USB_INTRRXE */

#define                  EP1_RX_E  0x2        /* Rx Endpoint 1 interrupt  Enable */
#define                 nEP1_RX_E  0x0       
#define                  EP2_RX_E  0x4        /* Rx Endpoint 2 interrupt  Enable */
#define                 nEP2_RX_E  0x0       
#define                  EP3_RX_E  0x8        /* Rx Endpoint 3 interrupt  Enable */
#define                 nEP3_RX_E  0x0       
#define                  EP4_RX_E  0x10       /* Rx Endpoint 4 interrupt  Enable */
#define                 nEP4_RX_E  0x0       
#define                  EP5_RX_E  0x20       /* Rx Endpoint 5 interrupt  Enable */
#define                 nEP5_RX_E  0x0       
#define                  EP6_RX_E  0x40       /* Rx Endpoint 6 interrupt  Enable */
#define                 nEP6_RX_E  0x0       
#define                  EP7_RX_E  0x80       /* Rx Endpoint 7 interrupt  Enable */
#define                 nEP7_RX_E  0x0       

/* Bit masks for USB_INTRUSB */

#define                 SUSPEND_B  0x1        /* Suspend indicator */
#define                nSUSPEND_B  0x0       
#define                  RESUME_B  0x2        /* Resume indicator */
#define                 nRESUME_B  0x0       
#define          RESET_OR_BABLE_B  0x4        /* Reset/babble indicator */
#define         nRESET_OR_BABLE_B  0x0       
#define                     SOF_B  0x8        /* Start of frame */
#define                    nSOF_B  0x0       
#define                    CONN_B  0x10       /* Connection indicator */
#define                   nCONN_B  0x0       
#define                  DISCON_B  0x20       /* Disconnect indicator */
#define                 nDISCON_B  0x0       
#define             SESSION_REQ_B  0x40       /* Session Request */
#define            nSESSION_REQ_B  0x0       
#define              VBUS_ERROR_B  0x80       /* Vbus threshold indicator */
#define             nVBUS_ERROR_B  0x0       

/* Bit masks for USB_INTRUSBE */

#define                SUSPEND_BE  0x1        /* Suspend indicator int enable */
#define               nSUSPEND_BE  0x0       
#define                 RESUME_BE  0x2        /* Resume indicator int enable */
#define                nRESUME_BE  0x0       
#define         RESET_OR_BABLE_BE  0x4        /* Reset/babble indicator int enable */
#define        nRESET_OR_BABLE_BE  0x0       
#define                    SOF_BE  0x8        /* Start of frame int enable */
#define                   nSOF_BE  0x0       
#define                   CONN_BE  0x10       /* Connection indicator int enable */
#define                  nCONN_BE  0x0       
#define                 DISCON_BE  0x20       /* Disconnect indicator int enable */
#define                nDISCON_BE  0x0       
#define            SESSION_REQ_BE  0x40       /* Session Request int enable */
#define           nSESSION_REQ_BE  0x0       
#define             VBUS_ERROR_BE  0x80       /* Vbus threshold indicator int enable */
#define            nVBUS_ERROR_BE  0x0       

/* Bit masks for USB_FRAME */

#define              FRAME_NUMBER  0x7ff      /* Frame number */

/* Bit masks for USB_INDEX */

#define         SELECTED_ENDPOINT  0xf        /* selected endpoint */

/* Bit masks for USB_GLOBAL_CTL */

#define                GLOBAL_ENA  0x1        /* enables USB module */
#define               nGLOBAL_ENA  0x0       
#define                EP1_TX_ENA  0x2        /* Transmit endpoint 1 enable */
#define               nEP1_TX_ENA  0x0       
#define                EP2_TX_ENA  0x4        /* Transmit endpoint 2 enable */
#define               nEP2_TX_ENA  0x0       
#define                EP3_TX_ENA  0x8        /* Transmit endpoint 3 enable */
#define               nEP3_TX_ENA  0x0       
#define                EP4_TX_ENA  0x10       /* Transmit endpoint 4 enable */
#define               nEP4_TX_ENA  0x0       
#define                EP5_TX_ENA  0x20       /* Transmit endpoint 5 enable */
#define               nEP5_TX_ENA  0x0       
#define                EP6_TX_ENA  0x40       /* Transmit endpoint 6 enable */
#define               nEP6_TX_ENA  0x0       
#define                EP7_TX_ENA  0x80       /* Transmit endpoint 7 enable */
#define               nEP7_TX_ENA  0x0       
#define                EP1_RX_ENA  0x100      /* Receive endpoint 1 enable */
#define               nEP1_RX_ENA  0x0       
#define                EP2_RX_ENA  0x200      /* Receive endpoint 2 enable */
#define               nEP2_RX_ENA  0x0       
#define                EP3_RX_ENA  0x400      /* Receive endpoint 3 enable */
#define               nEP3_RX_ENA  0x0       
#define                EP4_RX_ENA  0x800      /* Receive endpoint 4 enable */
#define               nEP4_RX_ENA  0x0       
#define                EP5_RX_ENA  0x1000     /* Receive endpoint 5 enable */
#define               nEP5_RX_ENA  0x0       
#define                EP6_RX_ENA  0x2000     /* Receive endpoint 6 enable */
#define               nEP6_RX_ENA  0x0       
#define                EP7_RX_ENA  0x4000     /* Receive endpoint 7 enable */
#define               nEP7_RX_ENA  0x0       

/* Bit masks for USB_OTG_DEV_CTL */

#define                   SESSION  0x1        /* session indicator */
#define                  nSESSION  0x0       
#define                  HOST_REQ  0x2        /* Host negotiation request */
#define                 nHOST_REQ  0x0       
#define                 HOST_MODE  0x4        /* indicates USBDRC is a host */
#define                nHOST_MODE  0x0       
#define                     VBUS0  0x8        /* Vbus level indicator[0] */
#define                    nVBUS0  0x0       
#define                     VBUS1  0x10       /* Vbus level indicator[1] */
#define                    nVBUS1  0x0       
#define                     LSDEV  0x20       /* Low-speed indicator */
#define                    nLSDEV  0x0       
#define                     FSDEV  0x40       /* Full or High-speed indicator */
#define                    nFSDEV  0x0       
#define                  B_DEVICE  0x80       /* A' or 'B' device indicator */
#define                 nB_DEVICE  0x0       

/* Bit masks for USB_OTG_VBUS_IRQ */

#define             DRIVE_VBUS_ON  0x1        /* indicator to drive VBUS control circuit */
#define            nDRIVE_VBUS_ON  0x0       
#define            DRIVE_VBUS_OFF  0x2        /* indicator to shut off charge pump */
#define           nDRIVE_VBUS_OFF  0x0       
#define           CHRG_VBUS_START  0x4        /* indicator for external circuit to start charging VBUS */
#define          nCHRG_VBUS_START  0x0       
#define             CHRG_VBUS_END  0x8        /* indicator for external circuit to end charging VBUS */
#define            nCHRG_VBUS_END  0x0       
#define        DISCHRG_VBUS_START  0x10       /* indicator to start discharging VBUS */
#define       nDISCHRG_VBUS_START  0x0       
#define          DISCHRG_VBUS_END  0x20       /* indicator to stop discharging VBUS */
#define         nDISCHRG_VBUS_END  0x0       

/* Bit masks for USB_OTG_VBUS_MASK */

#define         DRIVE_VBUS_ON_ENA  0x1        /* enable DRIVE_VBUS_ON interrupt */
#define        nDRIVE_VBUS_ON_ENA  0x0       
#define        DRIVE_VBUS_OFF_ENA  0x2        /* enable DRIVE_VBUS_OFF interrupt */
#define       nDRIVE_VBUS_OFF_ENA  0x0       
#define       CHRG_VBUS_START_ENA  0x4        /* enable CHRG_VBUS_START interrupt */
#define      nCHRG_VBUS_START_ENA  0x0       
#define         CHRG_VBUS_END_ENA  0x8        /* enable CHRG_VBUS_END interrupt */
#define        nCHRG_VBUS_END_ENA  0x0       
#define    DISCHRG_VBUS_START_ENA  0x10       /* enable DISCHRG_VBUS_START interrupt */
#define   nDISCHRG_VBUS_START_ENA  0x0       
#define      DISCHRG_VBUS_END_ENA  0x20       /* enable DISCHRG_VBUS_END interrupt */
#define     nDISCHRG_VBUS_END_ENA  0x0       

/* Bit masks for USB_CSR0 */

#define                  RXPKTRDY  0x1        /* data packet receive indicator */
#define                 nRXPKTRDY  0x0       
#define                  TXPKTRDY  0x2        /* data packet in FIFO indicator */
#define                 nTXPKTRDY  0x0       
#define                STALL_SENT  0x4        /* STALL handshake sent */
#define               nSTALL_SENT  0x0       
#define                   DATAEND  0x8        /* Data end indicator */
#define                  nDATAEND  0x0       
#define                  SETUPEND  0x10       /* Setup end */
#define                 nSETUPEND  0x0       
#define                 SENDSTALL  0x20       /* Send STALL handshake */
#define                nSENDSTALL  0x0       
#define         SERVICED_RXPKTRDY  0x40       /* used to clear the RxPktRdy bit */
#define        nSERVICED_RXPKTRDY  0x0       
#define         SERVICED_SETUPEND  0x80       /* used to clear the SetupEnd bit */
#define        nSERVICED_SETUPEND  0x0       
#define                 FLUSHFIFO  0x100      /* flush endpoint FIFO */
#define                nFLUSHFIFO  0x0       
#define          STALL_RECEIVED_H  0x4        /* STALL handshake received host mode */
#define         nSTALL_RECEIVED_H  0x0       
#define                SETUPPKT_H  0x8        /* send Setup token host mode */
#define               nSETUPPKT_H  0x0       
#define                   ERROR_H  0x10       /* timeout error indicator host mode */
#define                  nERROR_H  0x0       
#define                  REQPKT_H  0x20       /* Request an IN transaction host mode */
#define                 nREQPKT_H  0x0       
#define               STATUSPKT_H  0x40       /* Status stage transaction host mode */
#define              nSTATUSPKT_H  0x0       
#define             NAK_TIMEOUT_H  0x80       /* EP0 halted after a NAK host mode */
#define            nNAK_TIMEOUT_H  0x0       

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
#define               nTXPKTRDY_T  0x0       
#define          FIFO_NOT_EMPTY_T  0x2        /* FIFO not empty */
#define         nFIFO_NOT_EMPTY_T  0x0       
#define                UNDERRUN_T  0x4        /* TxPktRdy not set  for an IN token */
#define               nUNDERRUN_T  0x0       
#define               FLUSHFIFO_T  0x8        /* flush endpoint FIFO */
#define              nFLUSHFIFO_T  0x0       
#define              STALL_SEND_T  0x10       /* issue a Stall handshake */
#define             nSTALL_SEND_T  0x0       
#define              STALL_SENT_T  0x20       /* Stall handshake transmitted */
#define             nSTALL_SENT_T  0x0       
#define        CLEAR_DATATOGGLE_T  0x40       /* clear endpoint data toggle */
#define       nCLEAR_DATATOGGLE_T  0x0       
#define                INCOMPTX_T  0x80       /* indicates that a large packet is split */
#define               nINCOMPTX_T  0x0       
#define              DMAREQMODE_T  0x400      /* DMA mode (0 or 1) selection */
#define             nDMAREQMODE_T  0x0       
#define        FORCE_DATATOGGLE_T  0x800      /* Force data toggle */
#define       nFORCE_DATATOGGLE_T  0x0       
#define              DMAREQ_ENA_T  0x1000     /* Enable DMA request for Tx EP */
#define             nDMAREQ_ENA_T  0x0       
#define                     ISO_T  0x4000     /* enable Isochronous transfers */
#define                    nISO_T  0x0       
#define                 AUTOSET_T  0x8000     /* allows TxPktRdy to be set automatically */
#define                nAUTOSET_T  0x0       
#define                  ERROR_TH  0x4        /* error condition host mode */
#define                 nERROR_TH  0x0       
#define         STALL_RECEIVED_TH  0x20       /* Stall handshake received host mode */
#define        nSTALL_RECEIVED_TH  0x0       
#define            NAK_TIMEOUT_TH  0x80       /* NAK timeout host mode */
#define           nNAK_TIMEOUT_TH  0x0       

/* Bit masks for USB_TXCOUNT */

#define                  TX_COUNT  0x1fff     /* Number of bytes to be written to the selected endpoint Tx FIFO */

/* Bit masks for USB_RXCSR */

#define                RXPKTRDY_R  0x1        /* data packet in FIFO indicator */
#define               nRXPKTRDY_R  0x0       
#define               FIFO_FULL_R  0x2        /* FIFO not empty */
#define              nFIFO_FULL_R  0x0       
#define                 OVERRUN_R  0x4        /* TxPktRdy not set  for an IN token */
#define                nOVERRUN_R  0x0       
#define               DATAERROR_R  0x8        /* Out packet cannot be loaded into Rx  FIFO */
#define              nDATAERROR_R  0x0       
#define               FLUSHFIFO_R  0x10       /* flush endpoint FIFO */
#define              nFLUSHFIFO_R  0x0       
#define              STALL_SEND_R  0x20       /* issue a Stall handshake */
#define             nSTALL_SEND_R  0x0       
#define              STALL_SENT_R  0x40       /* Stall handshake transmitted */
#define             nSTALL_SENT_R  0x0       
#define        CLEAR_DATATOGGLE_R  0x80       /* clear endpoint data toggle */
#define       nCLEAR_DATATOGGLE_R  0x0       
#define                INCOMPRX_R  0x100      /* indicates that a large packet is split */
#define               nINCOMPRX_R  0x0       
#define              DMAREQMODE_R  0x800      /* DMA mode (0 or 1) selection */
#define             nDMAREQMODE_R  0x0       
#define                 DISNYET_R  0x1000     /* disable Nyet handshakes */
#define                nDISNYET_R  0x0       
#define              DMAREQ_ENA_R  0x2000     /* Enable DMA request for Tx EP */
#define             nDMAREQ_ENA_R  0x0       
#define                     ISO_R  0x4000     /* enable Isochronous transfers */
#define                    nISO_R  0x0       
#define               AUTOCLEAR_R  0x8000     /* allows TxPktRdy to be set automatically */
#define              nAUTOCLEAR_R  0x0       
#define                  ERROR_RH  0x4        /* TxPktRdy not set  for an IN token host mode */
#define                 nERROR_RH  0x0       
#define                 REQPKT_RH  0x20       /* request an IN transaction host mode */
#define                nREQPKT_RH  0x0       
#define         STALL_RECEIVED_RH  0x40       /* Stall handshake received host mode */
#define        nSTALL_RECEIVED_RH  0x0       
#define               INCOMPRX_RH  0x100      /* indicates that a large packet is split host mode */
#define              nINCOMPRX_RH  0x0       
#define             DMAREQMODE_RH  0x800      /* DMA mode (0 or 1) selection host mode */
#define            nDMAREQMODE_RH  0x0       
#define                AUTOREQ_RH  0x4000     /* sets ReqPkt automatically host mode */
#define               nAUTOREQ_RH  0x0       

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
#define                 nDMA0_INT  0x0       
#define                  DMA1_INT  0x2        /* DMA1 pending interrupt */
#define                 nDMA1_INT  0x0       
#define                  DMA2_INT  0x4        /* DMA2 pending interrupt */
#define                 nDMA2_INT  0x0       
#define                  DMA3_INT  0x8        /* DMA3 pending interrupt */
#define                 nDMA3_INT  0x0       
#define                  DMA4_INT  0x10       /* DMA4 pending interrupt */
#define                 nDMA4_INT  0x0       
#define                  DMA5_INT  0x20       /* DMA5 pending interrupt */
#define                 nDMA5_INT  0x0       
#define                  DMA6_INT  0x40       /* DMA6 pending interrupt */
#define                 nDMA6_INT  0x0       
#define                  DMA7_INT  0x80       /* DMA7 pending interrupt */
#define                 nDMA7_INT  0x0       

/* Bit masks for USB_DMAxCONTROL */

#define                   DMA_ENA  0x1        /* DMA enable */
#define                  nDMA_ENA  0x0       
#define                 DIRECTION  0x2        /* direction of DMA transfer */
#define                nDIRECTION  0x0       
#define                      MODE  0x4        /* DMA Bus error */
#define                     nMODE  0x0       
#define                   INT_ENA  0x8        /* Interrupt enable */
#define                  nINT_ENA  0x0       
#define                     EPNUM  0xf0       /* EP number */
#define                  BUSERROR  0x100      /* DMA Bus error */
#define                 nBUSERROR  0x0       

/* Bit masks for USB_DMAxADDRHIGH */

#define             DMA_ADDR_HIGH  0xffff     /* Upper 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxADDRLOW */

#define              DMA_ADDR_LOW  0xffff     /* Lower 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxCOUNTHIGH */

#define            DMA_COUNT_HIGH  0xffff     /* Upper 16-bits of byte count of DMA transfer for DMA master channel */

/* Bit masks for USB_DMAxCOUNTLOW */

#define             DMA_COUNT_LOW  0xffff     /* Lower 16-bits of byte count of DMA transfer for DMA master channel */


/* ******************************************* */
/*     MULTI BIT MACRO ENUMERATIONS            */
/* ******************************************* */


#endif /* _DEF_BF542_H */
