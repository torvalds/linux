/*
 * File:         include/asm-blackfin/mach-bf548/cdefBF542.h
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

#ifndef _CDEF_BF542_H
#define _CDEF_BF542_H

/* include all Core registers and bit definitions */
#include "defBF542.h"

/* include core sbfin_read_()ecific register pointer definitions */
#include <asm/mach-common/cdef_LPBlackfin.h>

/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF542 */

/* include cdefBF54x_base.h for the set of #defines that are common to all ADSP-BF54x bfin_read_()rocessors */
#include "cdefBF54x_base.h"

/* The following are the #defines needed by ADSP-BF542 that are not in the common header */

/* ATAPI Registers */

#define bfin_read_ATAPI_CONTROL()		bfin_read16(ATAPI_CONTROL)
#define bfin_write_ATAPI_CONTROL(val)		bfin_write16(ATAPI_CONTROL, val)
#define bfin_read_ATAPI_STATUS()		bfin_read16(ATAPI_STATUS)
#define bfin_write_ATAPI_STATUS(val)		bfin_write16(ATAPI_STATUS, val)
#define bfin_read_ATAPI_DEV_ADDR()		bfin_read16(ATAPI_DEV_ADDR)
#define bfin_write_ATAPI_DEV_ADDR(val)		bfin_write16(ATAPI_DEV_ADDR, val)
#define bfin_read_ATAPI_DEV_TXBUF()		bfin_read16(ATAPI_DEV_TXBUF)
#define bfin_write_ATAPI_DEV_TXBUF(val)		bfin_write16(ATAPI_DEV_TXBUF, val)
#define bfin_read_ATAPI_DEV_RXBUF()		bfin_read16(ATAPI_DEV_RXBUF)
#define bfin_write_ATAPI_DEV_RXBUF(val)		bfin_write16(ATAPI_DEV_RXBUF, val)
#define bfin_read_ATAPI_INT_MASK()		bfin_read16(ATAPI_INT_MASK)
#define bfin_write_ATAPI_INT_MASK(val)		bfin_write16(ATAPI_INT_MASK, val)
#define bfin_read_ATAPI_INT_STATUS()		bfin_read16(ATAPI_INT_STATUS)
#define bfin_write_ATAPI_INT_STATUS(val)	bfin_write16(ATAPI_INT_STATUS, val)
#define bfin_read_ATAPI_XFER_LEN()		bfin_read16(ATAPI_XFER_LEN)
#define bfin_write_ATAPI_XFER_LEN(val)		bfin_write16(ATAPI_XFER_LEN, val)
#define bfin_read_ATAPI_LINE_STATUS()		bfin_read16(ATAPI_LINE_STATUS)
#define bfin_write_ATAPI_LINE_STATUS(val)	bfin_write16(ATAPI_LINE_STATUS, val)
#define bfin_read_ATAPI_SM_STATE()		bfin_read16(ATAPI_SM_STATE)
#define bfin_write_ATAPI_SM_STATE(val)		bfin_write16(ATAPI_SM_STATE, val)
#define bfin_read_ATAPI_TERMINATE()		bfin_read16(ATAPI_TERMINATE)
#define bfin_write_ATAPI_TERMINATE(val)		bfin_write16(ATAPI_TERMINATE, val)
#define bfin_read_ATAPI_PIO_TFRCNT()		bfin_read16(ATAPI_PIO_TFRCNT)
#define bfin_write_ATAPI_PIO_TFRCNT(val)	bfin_write16(ATAPI_PIO_TFRCNT, val)
#define bfin_read_ATAPI_DMA_TFRCNT()		bfin_read16(ATAPI_DMA_TFRCNT)
#define bfin_write_ATAPI_DMA_TFRCNT(val)	bfin_write16(ATAPI_DMA_TFRCNT, val)
#define bfin_read_ATAPI_UMAIN_TFRCNT()		bfin_read16(ATAPI_UMAIN_TFRCNT)
#define bfin_write_ATAPI_UMAIN_TFRCNT(val)	bfin_write16(ATAPI_UMAIN_TFRCNT, val)
#define bfin_read_ATAPI_UDMAOUT_TFRCNT()	bfin_read16(ATAPI_UDMAOUT_TFRCNT)
#define bfin_write_ATAPI_UDMAOUT_TFRCNT(val)	bfin_write16(ATAPI_UDMAOUT_TFRCNT, val)
#define bfin_read_ATAPI_REG_TIM_0()		bfin_read16(ATAPI_REG_TIM_0)
#define bfin_write_ATAPI_REG_TIM_0(val)		bfin_write16(ATAPI_REG_TIM_0, val)
#define bfin_read_ATAPI_PIO_TIM_0()		bfin_read16(ATAPI_PIO_TIM_0)
#define bfin_write_ATAPI_PIO_TIM_0(val)		bfin_write16(ATAPI_PIO_TIM_0, val)
#define bfin_read_ATAPI_PIO_TIM_1()		bfin_read16(ATAPI_PIO_TIM_1)
#define bfin_write_ATAPI_PIO_TIM_1(val)		bfin_write16(ATAPI_PIO_TIM_1, val)
#define bfin_read_ATAPI_MULTI_TIM_0()		bfin_read16(ATAPI_MULTI_TIM_0)
#define bfin_write_ATAPI_MULTI_TIM_0(val)	bfin_write16(ATAPI_MULTI_TIM_0, val)
#define bfin_read_ATAPI_MULTI_TIM_1()		bfin_read16(ATAPI_MULTI_TIM_1)
#define bfin_write_ATAPI_MULTI_TIM_1(val)	bfin_write16(ATAPI_MULTI_TIM_1, val)
#define bfin_read_ATAPI_MULTI_TIM_2()		bfin_read16(ATAPI_MULTI_TIM_2)
#define bfin_write_ATAPI_MULTI_TIM_2(val)	bfin_write16(ATAPI_MULTI_TIM_2, val)
#define bfin_read_ATAPI_ULTRA_TIM_0()		bfin_read16(ATAPI_ULTRA_TIM_0)
#define bfin_write_ATAPI_ULTRA_TIM_0(val)	bfin_write16(ATAPI_ULTRA_TIM_0, val)
#define bfin_read_ATAPI_ULTRA_TIM_1()		bfin_read16(ATAPI_ULTRA_TIM_1)
#define bfin_write_ATAPI_ULTRA_TIM_1(val)	bfin_write16(ATAPI_ULTRA_TIM_1, val)
#define bfin_read_ATAPI_ULTRA_TIM_2()		bfin_read16(ATAPI_ULTRA_TIM_2)
#define bfin_write_ATAPI_ULTRA_TIM_2(val)	bfin_write16(ATAPI_ULTRA_TIM_2, val)
#define bfin_read_ATAPI_ULTRA_TIM_3()		bfin_read16(ATAPI_ULTRA_TIM_3)
#define bfin_write_ATAPI_ULTRA_TIM_3(val)	bfin_write16(ATAPI_ULTRA_TIM_3, val)

/* SDH Registers */

#define bfin_read_SDH_PWR_CTL()		bfin_read16(SDH_PWR_CTL)
#define bfin_write_SDH_PWR_CTL(val)	bfin_write16(SDH_PWR_CTL, val)
#define bfin_read_SDH_CLK_CTL()		bfin_read16(SDH_CLK_CTL)
#define bfin_write_SDH_CLK_CTL(val)	bfin_write16(SDH_CLK_CTL, val)
#define bfin_read_SDH_ARGUMENT()	bfin_read32(SDH_ARGUMENT)
#define bfin_write_SDH_ARGUMENT(val)	bfin_write32(SDH_ARGUMENT, val)
#define bfin_read_SDH_COMMAND()		bfin_read16(SDH_COMMAND)
#define bfin_write_SDH_COMMAND(val)	bfin_write16(SDH_COMMAND, val)
#define bfin_read_SDH_RESP_CMD()	bfin_read16(SDH_RESP_CMD)
#define bfin_write_SDH_RESP_CMD(val)	bfin_write16(SDH_RESP_CMD, val)
#define bfin_read_SDH_RESPONSE0()	bfin_read32(SDH_RESPONSE0)
#define bfin_write_SDH_RESPONSE0(val)	bfin_write32(SDH_RESPONSE0, val)
#define bfin_read_SDH_RESPONSE1()	bfin_read32(SDH_RESPONSE1)
#define bfin_write_SDH_RESPONSE1(val)	bfin_write32(SDH_RESPONSE1, val)
#define bfin_read_SDH_RESPONSE2()	bfin_read32(SDH_RESPONSE2)
#define bfin_write_SDH_RESPONSE2(val)	bfin_write32(SDH_RESPONSE2, val)
#define bfin_read_SDH_RESPONSE3()	bfin_read32(SDH_RESPONSE3)
#define bfin_write_SDH_RESPONSE3(val)	bfin_write32(SDH_RESPONSE3, val)
#define bfin_read_SDH_DATA_TIMER()	bfin_read32(SDH_DATA_TIMER)
#define bfin_write_SDH_DATA_TIMER(val)	bfin_write32(SDH_DATA_TIMER, val)
#define bfin_read_SDH_DATA_LGTH()	bfin_read16(SDH_DATA_LGTH)
#define bfin_write_SDH_DATA_LGTH(val)	bfin_write16(SDH_DATA_LGTH, val)
#define bfin_read_SDH_DATA_CTL()	bfin_read16(SDH_DATA_CTL)
#define bfin_write_SDH_DATA_CTL(val)	bfin_write16(SDH_DATA_CTL, val)
#define bfin_read_SDH_DATA_CNT()	fin_read16(SDH_DATA_CNT)
#define bfin_write_SDH_DATA_CNT(val)	bfin_write16(SDH_DATA_CNT, val)
#define bfin_read_SDH_STATUS()		bfin_read32(SDH_STATUS)
#define bfin_write_SDH_STATUS(val)	bfin_write32(SDH_STATUS, val)
#define bfin_read_SDH_STATUS_CLR()	fin_read16(SDH_STATUS_CLR)
#define bfin_write_SDH_STATUS_CLR(val)	fin_write16(SDH_STATUS_CLR, val)
#define bfin_read_SDH_MASK0()		bfin_read32(SDH_MASK0)
#define bfin_write_SDH_MASK0(val)	bfin_write32(SDH_MASK0, val)
#define bfin_read_SDH_MASK1()		bfin_read32(SDH_MASK1)
#define bfin_write_SDH_MASK1(val)	bfin_write32(SDH_MASK1, val)
#define bfin_read_SDH_FIFO_CNT()	bfin_read16(SDH_FIFO_CNT)
#define bfin_write_SDH_FIFO_CNT(val)	bfin_write16(SDH_FIFO_CNT, val)
#define bfin_read_SDH_FIFO()		bfin_read32(SDH_FIFO)
#define bfin_write_SDH_FIFO(val)	bfin_write32(SDH_FIFO, val)
#define bfin_read_SDH_E_STATUS()	bfin_read16(SDH_E_STATUS)
#define bfin_write_SDH_E_STATUS(val)	bfin_write16(SDH_E_STATUS, val)
#define bfin_read_SDH_E_MASK()		bfin_read16(SDH_E_MASK)
#define bfin_write_SDH_E_MASK(val)	bfin_write16(SDH_E_MASK, val)
#define bfin_read_SDH_CFG()		bfin_read16(SDH_CFG)
#define bfin_write_SDH_CFG(val)		bfin_write16(SDH_CFG, val)
#define bfin_read_SDH_RD_WAIT_EN()	bfin_read16(SDH_RD_WAIT_EN)
#define bfin_write_SDH_RD_WAIT_EN(val)	bfin_write16(SDH_RD_WAIT_EN, val)
#define bfin_read_SDH_PID0()		bfin_read16(SDH_PID0)
#define bfin_write_SDH_PID0(val)	bfin_write16(SDH_PID0, val)
#define bfin_read_SDH_PID1()		bfin_read16(SDH_PID1)
#define bfin_write_SDH_PID1(val)	bfin_write16(SDH_PID1, val)
#define bfin_read_SDH_PID2()		bfin_read16(SDH_PID2)
#define bfin_write_SDH_PID2(val)	bfin_write16(SDH_PID2, val)
#define bfin_read_SDH_PID3()		bfin_read16(SDH_PID3)
#define bfin_write_SDH_PID3(val)	bfin_write16(SDH_PID3, val)
#define bfin_read_SDH_PID4()		bfin_read16(SDH_PID4)
#define bfin_write_SDH_PID4(val)	bfin_write16(SDH_PID4, val)
#define bfin_read_SDH_PID5()		bfin_read16(SDH_PID5)
#define bfin_write_SDH_PID5(val)	bfin_write16(SDH_PID5, val)
#define bfin_read_SDH_PID6()		bfin_read16(SDH_PID6)
#define bfin_write_SDH_PID6(val)	bfin_write16(SDH_PID6, val)
#define bfin_read_SDH_PID7()		bfin_read16(SDH_PID7)
#define bfin_write_SDH_PID7(val)	bfin_write16(SDH_PID7, val)

/* USB Control Registers */

#define bfin_read_USB_FADDR()		bfin_read16(USB_FADDR)
#define bfin_write_USB_FADDR(val)	bfin_write16(USB_FADDR, val)
#define bfin_read_USB_POWER()		bfin_read16(USB_POWER)
#define bfin_write_USB_POWER(val)	bfin_write16(USB_POWER, val)
#define bfin_read_USB_INTRTX()		bfin_read16(USB_INTRTX)
#define bfin_write_USB_INTRTX(val)	bfin_write16(USB_INTRTX, val)
#define bfin_read_USB_INTRRX()		bfin_read16(USB_INTRRX)
#define bfin_write_USB_INTRRX(val)	bfin_write16(USB_INTRRX, val)
#define bfin_read_USB_INTRTXE()		bfin_read16(USB_INTRTXE)
#define bfin_write_USB_INTRTXE(val)	bfin_write16(USB_INTRTXE, val)
#define bfin_read_USB_INTRRXE()		bfin_read16(USB_INTRRXE)
#define bfin_write_USB_INTRRXE(val)	bfin_write16(USB_INTRRXE, val)
#define bfin_read_USB_INTRUSB()		bfin_read16(USB_INTRUSB)
#define bfin_write_USB_INTRUSB(val)	bfin_write16(USB_INTRUSB, val)
#define bfin_read_USB_INTRUSBE()	bfin_read16(USB_INTRUSBE)
#define bfin_write_USB_INTRUSBE(val)	bfin_write16(USB_INTRUSBE, val)
#define bfin_read_USB_FRAME()		bfin_read16(USB_FRAME)
#define bfin_write_USB_FRAME(val)	bfin_write16(USB_FRAME, val)
#define bfin_read_USB_INDEX()		bfin_read16(USB_INDEX)
#define bfin_write_USB_INDEX(val)	bfin_write16(USB_INDEX, val)
#define bfin_read_USB_TESTMODE()	fin_read16(USB_TESTMODE)
#define bfin_write_USB_TESTMODE(val)	fin_write16(USB_TESTMODE, val)
#define bfin_read_USB_GLOBINTR()	bfin_read16(USB_GLOBINTR)
#define bfin_write_USB_GLOBINTR(val)	bfin_write16(USB_GLOBINTR, val)
#define bfin_read_USB_GLOBAL_CTL()	bfin_read16(USB_GLOBAL_CTL)
#define bfin_write_USB_GLOBAL_CTL(val)	bfin_write16(USB_GLOBAL_CTL, val)

/* USB Packet Control Registers */

#define bfin_read_USB_TX_MAX_PACKET()		bfin_read16(USB_TX_MAX_PACKET)
#define bfin_write_USB_TX_MAX_PACKET(val)	bfin_write16(USB_TX_MAX_PACKET, val)
#define bfin_read_USB_CSR0()			bfin_read16(USB_CSR0)
#define bfin_write_USB_CSR0(val)		bfin_write16(USB_CSR0, val)
#define bfin_read_USB_TXCSR()			bfin_read16(USB_TXCSR)
#define bfin_write_USB_TXCSR(val)		bfin_write16(USB_TXCSR, val)
#define bfin_read_USB_RX_MAX_PACKET()		bfin_read16(USB_RX_MAX_PACKET)
#define bfin_write_USB_RX_MAX_PACKET(val)	bfin_write16(USB_RX_MAX_PACKET, val)
#define bfin_read_USB_RXCSR()			bfin_read16(USB_RXCSR)
#define bfin_write_USB_RXCSR(val)		bfin_write16(USB_RXCSR, val)
#define bfin_read_USB_COUNT0()			bfin_read16(USB_COUNT0)
#define bfin_write_USB_COUNT0(val)		bfin_write16(USB_COUNT0, val)
#define bfin_read_USB_RXCOUNT()			bfin_read16(USB_RXCOUNT)
#define bfin_write_USB_RXCOUNT(val)		bfin_write16(USB_RXCOUNT, val)
#define bfin_read_USB_TXTYPE()			bfin_read16(USB_TXTYPE)
#define bfin_write_USB_TXTYPE(val)		bfin_write16(USB_TXTYPE, val)
#define bfin_read_USB_NAKLIMIT0()		bfin_read16(USB_NAKLIMIT0)
#define bfin_write_USB_NAKLIMIT0(val)		bfin_write16(USB_NAKLIMIT0, val)
#define bfin_read_USB_TXINTERVAL()		bfin_read16(USB_TXINTERVAL)
#define bfin_write_USB_TXINTERVAL(val)		bfin_write16(USB_TXINTERVAL, val)
#define bfin_read_USB_RXTYPE()			bfin_read16(USB_RXTYPE)
#define bfin_write_USB_RXTYPE(val)		bfin_write16(USB_RXTYPE, val)
#define bfin_read_USB_RXINTERVAL()		bfin_read16(USB_RXINTERVAL)
#define bfin_write_USB_RXINTERVAL(val)		bfin_write16(USB_RXINTERVAL, val)
#define bfin_read_USB_TXCOUNT()			bfin_read16(USB_TXCOUNT)
#define bfin_write_USB_TXCOUNT(val)		bfin_write16(USB_TXCOUNT, val)

/* USB Endbfin_read_()oint FIFO Registers */

#define bfin_read_USB_EP0_FIFO()		bfin_read16(USB_EP0_FIFO)
#define bfin_write_USB_EP0_FIFO(val)		bfin_write16(USB_EP0_FIFO, val)
#define bfin_read_USB_EP1_FIFO()		bfin_read16(USB_EP1_FIFO)
#define bfin_write_USB_EP1_FIFO(val)		bfin_write16(USB_EP1_FIFO, val)
#define bfin_read_USB_EP2_FIFO()		bfin_read16(USB_EP2_FIFO)
#define bfin_write_USB_EP2_FIFO(val)		bfin_write16(USB_EP2_FIFO, val)
#define bfin_read_USB_EP3_FIFO()		bfin_read16(USB_EP3_FIFO)
#define bfin_write_USB_EP3_FIFO(val)		bfin_write16(USB_EP3_FIFO, val)
#define bfin_read_USB_EP4_FIFO()		bfin_read16(USB_EP4_FIFO)
#define bfin_write_USB_EP4_FIFO(val)		bfin_write16(USB_EP4_FIFO, val)
#define bfin_read_USB_EP5_FIFO()		bfin_read16(USB_EP5_FIFO)
#define bfin_write_USB_EP5_FIFO(val)		bfin_write16(USB_EP5_FIFO, val)
#define bfin_read_USB_EP6_FIFO()		bfin_read16(USB_EP6_FIFO)
#define bfin_write_USB_EP6_FIFO(val)		bfin_write16(USB_EP6_FIFO, val)
#define bfin_read_USB_EP7_FIFO()		bfin_read16(USB_EP7_FIFO)
#define bfin_write_USB_EP7_FIFO(val)		bfin_write16(USB_EP7_FIFO, val)

/* USB OTG Control Registers */

#define bfin_read_USB_OTG_DEV_CTL()		bfin_read16(USB_OTG_DEV_CTL)
#define bfin_write_USB_OTG_DEV_CTL(val)		bfin_write16(USB_OTG_DEV_CTL, val)
#define bfin_read_USB_OTG_VBUS_IRQ()		bfin_read16(USB_OTG_VBUS_IRQ)
#define bfin_write_USB_OTG_VBUS_IRQ(val)	fin_write16(USB_OTG_VBUS_IRQ, val)
#define bfin_read_USB_OTG_VBUS_MASK()		bfin_read16(USB_OTG_VBUS_MASK)
#define bfin_write_USB_OTG_VBUS_MASK(val)	bfin_write16(USB_OTG_VBUS_MASK, val)

/* USB Phy Control Registers */

#define bfin_read_USB_LINKINFO()		bfin_read16(USB_LINKINFO)
#define bfin_write_USB_LINKINFO(val)		bfin_write16(USB_LINKINFO, val)
#define bfin_read_USB_VPLEN()			bfin_read16(USB_VPLEN)
#define bfin_write_USB_VPLEN(val)		bfin_write16(USB_VPLEN, val)
#define bfin_read_USB_HS_EOF1()			bfin_read16(USB_HS_EOF1)
#define bfin_write_USB_HS_EOF1(val)		bfin_write16(USB_HS_EOF1, val)
#define bfin_read_USB_FS_EOF1()			bfin_read16(USB_FS_EOF1)
#define bfin_write_USB_FS_EOF1(val)		bfin_write16(USB_FS_EOF1, val)
#define bfin_read_USB_LS_EOF1()			bfin_read16(USB_LS_EOF1)
#define bfin_write_USB_LS_EOF1(val)		bfin_write16(USB_LS_EOF1, val)

/* (APHY_CNTRL is for ADI usage only) */

#define bfin_read_USB_APHY_CNTRL()		bfin_read16(USB_APHY_CNTRL)
#define bfin_write_USB_APHY_CNTRL(val)		bfin_write16(USB_APHY_CNTRL, val)

/* (APHY_CALIB is for ADI usage only) */

#define bfin_read_USB_APHY_CALIB()		bfin_read16(USB_APHY_CALIB)
#define bfin_write_USB_APHY_CALIB(val)		bfin_write16(USB_APHY_CALIB, val)
#define bfin_read_USB_APHY_CNTRL2()		bfin_read16(USB_APHY_CNTRL2)
#define bfin_write_USB_APHY_CNTRL2(val)		bfin_write16(USB_APHY_CNTRL2, val)

/* (PHY_TEST is for ADI usage only) */

#define bfin_read_USB_PHY_TEST()		bfin_read16(USB_PHY_TEST)
#define bfin_write_USB_PHY_TEST(val)		bfin_write16(USB_PHY_TEST, val)
#define bfin_read_USB_PLLOSC_CTRL()		bfin_read16(USB_PLLOSC_CTRL)
#define bfin_write_USB_PLLOSC_CTRL(val)		bfin_write16(USB_PLLOSC_CTRL, val)
#define bfin_read_USB_SRP_CLKDIV()		bfin_read16(USB_SRP_CLKDIV)
#define bfin_write_USB_SRP_CLKDIV(val)		bfin_write16(USB_SRP_CLKDIV, val)

/* USB Endbfin_read_()oint 0 Control Registers */

#define bfin_read_USB_EP_NI0_TXMAXP()		bfin_read16(USB_EP_NI0_TXMAXP)
#define bfin_write_USB_EP_NI0_TXMAXP(val)	bfin_write16(USB_EP_NI0_TXMAXP, val)
#define bfin_read_USB_EP_NI0_TXCSR()		bfin_read16(USB_EP_NI0_TXCSR)
#define bfin_write_USB_EP_NI0_TXCSR(val)	bfin_write16(USB_EP_NI0_TXCSR, val)
#define bfin_read_USB_EP_NI0_RXMAXP()		bfin_read16(USB_EP_NI0_RXMAXP)
#define bfin_write_USB_EP_NI0_RXMAXP(val)	bfin_write16(USB_EP_NI0_RXMAXP, val)
#define bfin_read_USB_EP_NI0_RXCSR()		bfin_read16(USB_EP_NI0_RXCSR)
#define bfin_write_USB_EP_NI0_RXCSR(val)	bfin_write16(USB_EP_NI0_RXCSR, val)
#define bfin_read_USB_EP_NI0_RXCOUNT()		bfin_read16(USB_EP_NI0_RXCOUNT)
#define bfin_write_USB_EP_NI0_RXCOUNT(val)	bfin_write16(USB_EP_NI0_RXCOUNT, val)
#define bfin_read_USB_EP_NI0_TXTYPE()		bfin_read16(USB_EP_NI0_TXTYPE)
#define bfin_write_USB_EP_NI0_TXTYPE(val)	bfin_write16(USB_EP_NI0_TXTYPE, val)
#define bfin_read_USB_EP_NI0_TXINTERVAL()	bfin_read16(USB_EP_NI0_TXINTERVAL)
#define bfin_write_USB_EP_NI0_TXINTERVAL(val)	bfin_write16(USB_EP_NI0_TXINTERVAL, val)
#define bfin_read_USB_EP_NI0_RXTYPE()		bfin_read16(USB_EP_NI0_RXTYPE)
#define bfin_write_USB_EP_NI0_RXTYPE(val)	bfin_write16(USB_EP_NI0_RXTYPE, val)
#define bfin_read_USB_EP_NI0_RXINTERVAL()	bfin_read16(USB_EP_NI0_RXINTERVAL)
#define bfin_write_USB_EP_NI0_RXINTERVAL(val)	bfin_write16(USB_EP_NI0_RXINTERVAL, val)

/* USB Endbfin_read_()oint 1 Control Registers */

#define bfin_read_USB_EP_NI0_TXCOUNT()		bfin_read16(USB_EP_NI0_TXCOUNT)
#define bfin_write_USB_EP_NI0_TXCOUNT(val)	bfin_write16(USB_EP_NI0_TXCOUNT, val)
#define bfin_read_USB_EP_NI1_TXMAXP()		bfin_read16(USB_EP_NI1_TXMAXP)
#define bfin_write_USB_EP_NI1_TXMAXP(val)	bfin_write16(USB_EP_NI1_TXMAXP, val)
#define bfin_read_USB_EP_NI1_TXCSR()		bfin_read16(USB_EP_NI1_TXCSR)
#define bfin_write_USB_EP_NI1_TXCSR(val)	bfin_write16(USB_EP_NI1_TXCSR, val)
#define bfin_read_USB_EP_NI1_RXMAXP()		bfin_read16(USB_EP_NI1_RXMAXP)
#define bfin_write_USB_EP_NI1_RXMAXP(val)	bfin_write16(USB_EP_NI1_RXMAXP, val)
#define bfin_read_USB_EP_NI1_RXCSR()		bfin_read16(USB_EP_NI1_RXCSR)
#define bfin_write_USB_EP_NI1_RXCSR(val)	bfin_write16(USB_EP_NI1_RXCSR, val)
#define bfin_read_USB_EP_NI1_RXCOUNT()		bfin_read16(USB_EP_NI1_RXCOUNT)
#define bfin_write_USB_EP_NI1_RXCOUNT(val)	bfin_write16(USB_EP_NI1_RXCOUNT, val)
#define bfin_read_USB_EP_NI1_TXTYPE()		bfin_read16(USB_EP_NI1_TXTYPE)
#define bfin_write_USB_EP_NI1_TXTYPE(val)	bfin_write16(USB_EP_NI1_TXTYPE, val)
#define bfin_read_USB_EP_NI1_TXINTERVAL()	bfin_read16(USB_EP_NI1_TXINTERVAL)
#define bfin_write_USB_EP_NI1_TXINTERVAL(val)	bfin_write16(USB_EP_NI1_TXINTERVAL, val)
#define bfin_read_USB_EP_NI1_RXTYPE()		bfin_read16(USB_EP_NI1_RXTYPE)
#define bfin_write_USB_EP_NI1_RXTYPE(val)	bfin_write16(USB_EP_NI1_RXTYPE, val)
#define bfin_read_USB_EP_NI1_RXINTERVAL()	bfin_read16(USB_EP_NI1_RXINTERVAL)
#define bfin_write_USB_EP_NI1_RXINTERVAL(val)	bfin_write16(USB_EP_NI1_RXINTERVAL, val)

/* USB Endbfin_read_()oint 2 Control Registers */

#define bfin_read_USB_EP_NI1_TXCOUNT()		bfin_read16(USB_EP_NI1_TXCOUNT)
#define bfin_write_USB_EP_NI1_TXCOUNT(val)	bfin_write16(USB_EP_NI1_TXCOUNT, val)
#define bfin_read_USB_EP_NI2_TXMAXP()		bfin_read16(USB_EP_NI2_TXMAXP)
#define bfin_write_USB_EP_NI2_TXMAXP(val)	bfin_write16(USB_EP_NI2_TXMAXP, val)
#define bfin_read_USB_EP_NI2_TXCSR()		bfin_read16(USB_EP_NI2_TXCSR)
#define bfin_write_USB_EP_NI2_TXCSR(val)	bfin_write16(USB_EP_NI2_TXCSR, val)
#define bfin_read_USB_EP_NI2_RXMAXP()		bfin_read16(USB_EP_NI2_RXMAXP)
#define bfin_write_USB_EP_NI2_RXMAXP(val)	bfin_write16(USB_EP_NI2_RXMAXP, val)
#define bfin_read_USB_EP_NI2_RXCSR()		bfin_read16(USB_EP_NI2_RXCSR)
#define bfin_write_USB_EP_NI2_RXCSR(val)	bfin_write16(USB_EP_NI2_RXCSR, val)
#define bfin_read_USB_EP_NI2_RXCOUNT()		bfin_read16(USB_EP_NI2_RXCOUNT)
#define bfin_write_USB_EP_NI2_RXCOUNT(val)	bfin_write16(USB_EP_NI2_RXCOUNT, val)
#define bfin_read_USB_EP_NI2_TXTYPE()		bfin_read16(USB_EP_NI2_TXTYPE)
#define bfin_write_USB_EP_NI2_TXTYPE(val)	bfin_write16(USB_EP_NI2_TXTYPE, val)
#define bfin_read_USB_EP_NI2_TXINTERVAL()	bfin_read16(USB_EP_NI2_TXINTERVAL)
#define bfin_write_USB_EP_NI2_TXINTERVAL(val)	bfin_write16(USB_EP_NI2_TXINTERVAL, val)
#define bfin_read_USB_EP_NI2_RXTYPE()		bfin_read16(USB_EP_NI2_RXTYPE)
#define bfin_write_USB_EP_NI2_RXTYPE(val)	bfin_write16(USB_EP_NI2_RXTYPE, val)
#define bfin_read_USB_EP_NI2_RXINTERVAL()	bfin_read16(USB_EP_NI2_RXINTERVAL)
#define bfin_write_USB_EP_NI2_RXINTERVAL(val)	bfin_write16(USB_EP_NI2_RXINTERVAL, val)

/* USB Endbfin_read_()oint 3 Control Registers */

#define bfin_read_USB_EP_NI2_TXCOUNT()		bfin_read16(USB_EP_NI2_TXCOUNT)
#define bfin_write_USB_EP_NI2_TXCOUNT(val)	bfin_write16(USB_EP_NI2_TXCOUNT, val)
#define bfin_read_USB_EP_NI3_TXMAXP()		bfin_read16(USB_EP_NI3_TXMAXP)
#define bfin_write_USB_EP_NI3_TXMAXP(val)	bfin_write16(USB_EP_NI3_TXMAXP, val)
#define bfin_read_USB_EP_NI3_TXCSR()		bfin_read16(USB_EP_NI3_TXCSR)
#define bfin_write_USB_EP_NI3_TXCSR(val)	bfin_write16(USB_EP_NI3_TXCSR, val)
#define bfin_read_USB_EP_NI3_RXMAXP()		bfin_read16(USB_EP_NI3_RXMAXP)
#define bfin_write_USB_EP_NI3_RXMAXP(val)	bfin_write16(USB_EP_NI3_RXMAXP, val)
#define bfin_read_USB_EP_NI3_RXCSR()		bfin_read16(USB_EP_NI3_RXCSR)
#define bfin_write_USB_EP_NI3_RXCSR(val)	bfin_write16(USB_EP_NI3_RXCSR, val)
#define bfin_read_USB_EP_NI3_RXCOUNT()		bfin_read16(USB_EP_NI3_RXCOUNT)
#define bfin_write_USB_EP_NI3_RXCOUNT(val)	bfin_write16(USB_EP_NI3_RXCOUNT, val)
#define bfin_read_USB_EP_NI3_TXTYPE()		bfin_read16(USB_EP_NI3_TXTYPE)
#define bfin_write_USB_EP_NI3_TXTYPE(val)	bfin_write16(USB_EP_NI3_TXTYPE, val)
#define bfin_read_USB_EP_NI3_TXINTERVAL()	bfin_read16(USB_EP_NI3_TXINTERVAL)
#define bfin_write_USB_EP_NI3_TXINTERVAL(val)	bfin_write16(USB_EP_NI3_TXINTERVAL, val)
#define bfin_read_USB_EP_NI3_RXTYPE()		bfin_read16(USB_EP_NI3_RXTYPE)
#define bfin_write_USB_EP_NI3_RXTYPE(val)	bfin_write16(USB_EP_NI3_RXTYPE, val)
#define bfin_read_USB_EP_NI3_RXINTERVAL()	bfin_read16(USB_EP_NI3_RXINTERVAL)
#define bfin_write_USB_EP_NI3_RXINTERVAL(val)	bfin_write16(USB_EP_NI3_RXINTERVAL, val)

/* USB Endbfin_read_()oint 4 Control Registers */

#define bfin_read_USB_EP_NI3_TXCOUNT()		bfin_read16(USB_EP_NI3_TXCOUNT)
#define bfin_write_USB_EP_NI3_TXCOUNT(val)	bfin_write16(USB_EP_NI3_TXCOUNT, val)
#define bfin_read_USB_EP_NI4_TXMAXP()		bfin_read16(USB_EP_NI4_TXMAXP)
#define bfin_write_USB_EP_NI4_TXMAXP(val)	bfin_write16(USB_EP_NI4_TXMAXP, val)
#define bfin_read_USB_EP_NI4_TXCSR()		bfin_read16(USB_EP_NI4_TXCSR)
#define bfin_write_USB_EP_NI4_TXCSR(val)	bfin_write16(USB_EP_NI4_TXCSR, val)
#define bfin_read_USB_EP_NI4_RXMAXP()		bfin_read16(USB_EP_NI4_RXMAXP)
#define bfin_write_USB_EP_NI4_RXMAXP(val)	bfin_write16(USB_EP_NI4_RXMAXP, val)
#define bfin_read_USB_EP_NI4_RXCSR()		bfin_read16(USB_EP_NI4_RXCSR)
#define bfin_write_USB_EP_NI4_RXCSR(val)	bfin_write16(USB_EP_NI4_RXCSR, val)
#define bfin_read_USB_EP_NI4_RXCOUNT()		bfin_read16(USB_EP_NI4_RXCOUNT)
#define bfin_write_USB_EP_NI4_RXCOUNT(val)	bfin_write16(USB_EP_NI4_RXCOUNT, val)
#define bfin_read_USB_EP_NI4_TXTYPE()		bfin_read16(USB_EP_NI4_TXTYPE)
#define bfin_write_USB_EP_NI4_TXTYPE(val)	bfin_write16(USB_EP_NI4_TXTYPE, val)
#define bfin_read_USB_EP_NI4_TXINTERVAL()	bfin_read16(USB_EP_NI4_TXINTERVAL)
#define bfin_write_USB_EP_NI4_TXINTERVAL(val)	bfin_write16(USB_EP_NI4_TXINTERVAL, val)
#define bfin_read_USB_EP_NI4_RXTYPE()		bfin_read16(USB_EP_NI4_RXTYPE)
#define bfin_write_USB_EP_NI4_RXTYPE(val)	bfin_write16(USB_EP_NI4_RXTYPE, val)
#define bfin_read_USB_EP_NI4_RXINTERVAL()	bfin_read16(USB_EP_NI4_RXINTERVAL)
#define bfin_write_USB_EP_NI4_RXINTERVAL(val)	bfin_write16(USB_EP_NI4_RXINTERVAL, val)

/* USB Endbfin_read_()oint 5 Control Registers */

#define bfin_read_USB_EP_NI4_TXCOUNT()		bfin_read16(USB_EP_NI4_TXCOUNT)
#define bfin_write_USB_EP_NI4_TXCOUNT(val)	bfin_write16(USB_EP_NI4_TXCOUNT, val)
#define bfin_read_USB_EP_NI5_TXMAXP()		bfin_read16(USB_EP_NI5_TXMAXP)
#define bfin_write_USB_EP_NI5_TXMAXP(val)	bfin_write16(USB_EP_NI5_TXMAXP, val)
#define bfin_read_USB_EP_NI5_TXCSR()		bfin_read16(USB_EP_NI5_TXCSR)
#define bfin_write_USB_EP_NI5_TXCSR(val)	bfin_write16(USB_EP_NI5_TXCSR, val)
#define bfin_read_USB_EP_NI5_RXMAXP()		bfin_read16(USB_EP_NI5_RXMAXP)
#define bfin_write_USB_EP_NI5_RXMAXP(val)	bfin_write16(USB_EP_NI5_RXMAXP, val)
#define bfin_read_USB_EP_NI5_RXCSR()		bfin_read16(USB_EP_NI5_RXCSR)
#define bfin_write_USB_EP_NI5_RXCSR(val)	bfin_write16(USB_EP_NI5_RXCSR, val)
#define bfin_read_USB_EP_NI5_RXCOUNT()		bfin_read16(USB_EP_NI5_RXCOUNT)
#define bfin_write_USB_EP_NI5_RXCOUNT(val)	bfin_write16(USB_EP_NI5_RXCOUNT, val)
#define bfin_read_USB_EP_NI5_TXTYPE()		bfin_read16(USB_EP_NI5_TXTYPE)
#define bfin_write_USB_EP_NI5_TXTYPE(val)	bfin_write16(USB_EP_NI5_TXTYPE, val)
#define bfin_read_USB_EP_NI5_TXINTERVAL()	bfin_read16(USB_EP_NI5_TXINTERVAL)
#define bfin_write_USB_EP_NI5_TXINTERVAL(val)	bfin_write16(USB_EP_NI5_TXINTERVAL, val)
#define bfin_read_USB_EP_NI5_RXTYPE()		bfin_read16(USB_EP_NI5_RXTYPE)
#define bfin_write_USB_EP_NI5_RXTYPE(val)	bfin_write16(USB_EP_NI5_RXTYPE, val)
#define bfin_read_USB_EP_NI5_RXINTERVAL()	bfin_read16(USB_EP_NI5_RXINTERVAL)
#define bfin_write_USB_EP_NI5_RXINTERVAL(val)	bfin_write16(USB_EP_NI5_RXINTERVAL, val)

/* USB Endbfin_read_()oint 6 Control Registers */

#define bfin_read_USB_EP_NI5_TXCOUNT()		bfin_read16(USB_EP_NI5_TXCOUNT)
#define bfin_write_USB_EP_NI5_TXCOUNT(val)	bfin_write16(USB_EP_NI5_TXCOUNT, val)
#define bfin_read_USB_EP_NI6_TXMAXP()		bfin_read16(USB_EP_NI6_TXMAXP)
#define bfin_write_USB_EP_NI6_TXMAXP(val)	bfin_write16(USB_EP_NI6_TXMAXP, val)
#define bfin_read_USB_EP_NI6_TXCSR()		bfin_read16(USB_EP_NI6_TXCSR)
#define bfin_write_USB_EP_NI6_TXCSR(val)	bfin_write16(USB_EP_NI6_TXCSR, val)
#define bfin_read_USB_EP_NI6_RXMAXP()		bfin_read16(USB_EP_NI6_RXMAXP)
#define bfin_write_USB_EP_NI6_RXMAXP(val)	bfin_write16(USB_EP_NI6_RXMAXP, val)
#define bfin_read_USB_EP_NI6_RXCSR()		bfin_read16(USB_EP_NI6_RXCSR)
#define bfin_write_USB_EP_NI6_RXCSR(val)	bfin_write16(USB_EP_NI6_RXCSR, val)
#define bfin_read_USB_EP_NI6_RXCOUNT()		bfin_read16(USB_EP_NI6_RXCOUNT)
#define bfin_write_USB_EP_NI6_RXCOUNT(val)	bfin_write16(USB_EP_NI6_RXCOUNT, val)
#define bfin_read_USB_EP_NI6_TXTYPE()		bfin_read16(USB_EP_NI6_TXTYPE)
#define bfin_write_USB_EP_NI6_TXTYPE(val)	bfin_write16(USB_EP_NI6_TXTYPE, val)
#define bfin_read_USB_EP_NI6_TXINTERVAL()	bfin_read16(USB_EP_NI6_TXINTERVAL)
#define bfin_write_USB_EP_NI6_TXINTERVAL(val)	bfin_write16(USB_EP_NI6_TXINTERVAL, val)
#define bfin_read_USB_EP_NI6_RXTYPE()		bfin_read16(USB_EP_NI6_RXTYPE)
#define bfin_write_USB_EP_NI6_RXTYPE(val)	bfin_write16(USB_EP_NI6_RXTYPE, val)
#define bfin_read_USB_EP_NI6_RXINTERVAL()	bfin_read16(USB_EP_NI6_RXINTERVAL)
#define bfin_write_USB_EP_NI6_RXINTERVAL(val)	bfin_write16(USB_EP_NI6_RXINTERVAL, val)

/* USB Endbfin_read_()oint 7 Control Registers */

#define bfin_read_USB_EP_NI6_TXCOUNT()		bfin_read16(USB_EP_NI6_TXCOUNT)
#define bfin_write_USB_EP_NI6_TXCOUNT(val)	bfin_write16(USB_EP_NI6_TXCOUNT, val)
#define bfin_read_USB_EP_NI7_TXMAXP()		bfin_read16(USB_EP_NI7_TXMAXP)
#define bfin_write_USB_EP_NI7_TXMAXP(val)	bfin_write16(USB_EP_NI7_TXMAXP, val)
#define bfin_read_USB_EP_NI7_TXCSR()		bfin_read16(USB_EP_NI7_TXCSR)
#define bfin_write_USB_EP_NI7_TXCSR(val)	bfin_write16(USB_EP_NI7_TXCSR, val)
#define bfin_read_USB_EP_NI7_RXMAXP()		bfin_read16(USB_EP_NI7_RXMAXP)
#define bfin_write_USB_EP_NI7_RXMAXP(val)	bfin_write16(USB_EP_NI7_RXMAXP, val)
#define bfin_read_USB_EP_NI7_RXCSR()		bfin_read16(USB_EP_NI7_RXCSR)
#define bfin_write_USB_EP_NI7_RXCSR(val)	bfin_write16(USB_EP_NI7_RXCSR, val)
#define bfin_read_USB_EP_NI7_RXCOUNT()		bfin_read16(USB_EP_NI7_RXCOUNT)
#define bfin_write_USB_EP_NI7_RXCOUNT(val)	bfin_write16(USB_EP_NI7_RXCOUNT, val)
#define bfin_read_USB_EP_NI7_TXTYPE()		bfin_read16(USB_EP_NI7_TXTYPE)
#define bfin_write_USB_EP_NI7_TXTYPE(val)	bfin_write16(USB_EP_NI7_TXTYPE, val)
#define bfin_read_USB_EP_NI7_TXINTERVAL()	bfin_read16(USB_EP_NI7_TXINTERVAL)
#define bfin_write_USB_EP_NI7_TXINTERVAL(val)	bfin_write16(USB_EP_NI7_TXINTERVAL, val)
#define bfin_read_USB_EP_NI7_RXTYPE()		bfin_read16(USB_EP_NI7_RXTYPE)
#define bfin_write_USB_EP_NI7_RXTYPE(val)	bfin_write16(USB_EP_NI7_RXTYPE, val)
#define bfin_read_USB_EP_NI7_RXINTERVAL()	bfin_read16(USB_EP_NI7_RXINTERVAL)
#define bfin_write_USB_EP_NI7_RXINTERVAL(val)	bfin_write16(USB_EP_NI7_RXINTERVAL, val)
#define bfin_read_USB_EP_NI7_TXCOUNT()		bfin_read16(USB_EP_NI7_TXCOUNT)
#define bfin_write_USB_EP_NI7_TXCOUNT(val)	bfin_write16(USB_EP_NI7_TXCOUNT, val)
#define bfin_read_USB_DMA_INTERRUPT()		bfin_read16(USB_DMA_INTERRUPT)
#define bfin_write_USB_DMA_INTERRUPT(val)	bfin_write16(USB_DMA_INTERRUPT, val)

/* USB Channel 0 Config Registers */

#define bfin_read_USB_DMA0CONTROL()		bfin_read16(USB_DMA0CONTROL)
#define bfin_write_USB_DMA0CONTROL(val)		bfin_write16(USB_DMA0CONTROL, val)
#define bfin_read_USB_DMA0ADDRLOW()		bfin_read16(USB_DMA0ADDRLOW)
#define bfin_write_USB_DMA0ADDRLOW(val)		bfin_write16(USB_DMA0ADDRLOW, val)
#define bfin_read_USB_DMA0ADDRHIGH()		bfin_read16(USB_DMA0ADDRHIGH)
#define bfin_write_USB_DMA0ADDRHIGH(val)	bfin_write16(USB_DMA0ADDRHIGH, val)
#define bfin_read_USB_DMA0COUNTLOW()		bfin_read16(USB_DMA0COUNTLOW)
#define bfin_write_USB_DMA0COUNTLOW(val)	bfin_write16(USB_DMA0COUNTLOW, val)
#define bfin_read_USB_DMA0COUNTHIGH()		bfin_read16(USB_DMA0COUNTHIGH)
#define bfin_write_USB_DMA0COUNTHIGH(val)	bfin_write16(USB_DMA0COUNTHIGH, val)

/* USB Channel 1 Config Registers */

#define bfin_read_USB_DMA1CONTROL()		bfin_read16(USB_DMA1CONTROL)
#define bfin_write_USB_DMA1CONTROL(val)		bfin_write16(USB_DMA1CONTROL, val)
#define bfin_read_USB_DMA1ADDRLOW()		bfin_read16(USB_DMA1ADDRLOW)
#define bfin_write_USB_DMA1ADDRLOW(val)		bfin_write16(USB_DMA1ADDRLOW, val)
#define bfin_read_USB_DMA1ADDRHIGH()		bfin_read16(USB_DMA1ADDRHIGH)
#define bfin_write_USB_DMA1ADDRHIGH(val)	bfin_write16(USB_DMA1ADDRHIGH, val)
#define bfin_read_USB_DMA1COUNTLOW()		bfin_read16(USB_DMA1COUNTLOW)
#define bfin_write_USB_DMA1COUNTLOW(val)	bfin_write16(USB_DMA1COUNTLOW, val)
#define bfin_read_USB_DMA1COUNTHIGH()		bfin_read16(USB_DMA1COUNTHIGH)
#define bfin_write_USB_DMA1COUNTHIGH(val)	bfin_write16(USB_DMA1COUNTHIGH, val)

/* USB Channel 2 Config Registers */

#define bfin_read_USB_DMA2CONTROL()		bfin_read16(USB_DMA2CONTROL)
#define bfin_write_USB_DMA2CONTROL(val)		bfin_write16(USB_DMA2CONTROL, val)
#define bfin_read_USB_DMA2ADDRLOW()		bfin_read16(USB_DMA2ADDRLOW)
#define bfin_write_USB_DMA2ADDRLOW(val)		bfin_write16(USB_DMA2ADDRLOW, val)
#define bfin_read_USB_DMA2ADDRHIGH()		bfin_read16(USB_DMA2ADDRHIGH)
#define bfin_write_USB_DMA2ADDRHIGH(val)	bfin_write16(USB_DMA2ADDRHIGH, val)
#define bfin_read_USB_DMA2COUNTLOW()		bfin_read16(USB_DMA2COUNTLOW)
#define bfin_write_USB_DMA2COUNTLOW(val)	bfin_write16(USB_DMA2COUNTLOW, val)
#define bfin_read_USB_DMA2COUNTHIGH()		bfin_read16(USB_DMA2COUNTHIGH)
#define bfin_write_USB_DMA2COUNTHIGH(val)	bfin_write16(USB_DMA2COUNTHIGH, val)

/* USB Channel 3 Config Registers */

#define bfin_read_USB_DMA3CONTROL()		bfin_read16(USB_DMA3CONTROL)
#define bfin_write_USB_DMA3CONTROL(val)		bfin_write16(USB_DMA3CONTROL, val)
#define bfin_read_USB_DMA3ADDRLOW()		bfin_read16(USB_DMA3ADDRLOW)
#define bfin_write_USB_DMA3ADDRLOW(val)		bfin_write16(USB_DMA3ADDRLOW, val)
#define bfin_read_USB_DMA3ADDRHIGH()		bfin_read16(USB_DMA3ADDRHIGH)
#define bfin_write_USB_DMA3ADDRHIGH(val)	bfin_write16(USB_DMA3ADDRHIGH, val)
#define bfin_read_USB_DMA3COUNTLOW()		bfin_read16(USB_DMA3COUNTLOW)
#define bfin_write_USB_DMA3COUNTLOW(val)	bfin_write16(USB_DMA3COUNTLOW, val)
#define bfin_read_USB_DMA3COUNTHIGH()		bfin_read16(USB_DMA3COUNTHIGH)
#define bfin_write_USB_DMA3COUNTHIGH(val)	bfin_write16(USB_DMA3COUNTHIGH, val)

/* USB Channel 4 Config Registers */

#define bfin_read_USB_DMA4CONTROL()		bfin_read16(USB_DMA4CONTROL)
#define bfin_write_USB_DMA4CONTROL(val)		bfin_write16(USB_DMA4CONTROL, val)
#define bfin_read_USB_DMA4ADDRLOW()		bfin_read16(USB_DMA4ADDRLOW)
#define bfin_write_USB_DMA4ADDRLOW(val)		bfin_write16(USB_DMA4ADDRLOW, val)
#define bfin_read_USB_DMA4ADDRHIGH()		bfin_read16(USB_DMA4ADDRHIGH)
#define bfin_write_USB_DMA4ADDRHIGH(val)	bfin_write16(USB_DMA4ADDRHIGH, val)
#define bfin_read_USB_DMA4COUNTLOW()		bfin_read16(USB_DMA4COUNTLOW)
#define bfin_write_USB_DMA4COUNTLOW(val)	bfin_write16(USB_DMA4COUNTLOW, val)
#define bfin_read_USB_DMA4COUNTHIGH()		bfin_read16(USB_DMA4COUNTHIGH)
#define bfin_write_USB_DMA4COUNTHIGH(val)	bfin_write16(USB_DMA4COUNTHIGH, val)

/* USB Channel 5 Config Registers */

#define bfin_read_USB_DMA5CONTROL()		bfin_read16(USB_DMA5CONTROL)
#define bfin_write_USB_DMA5CONTROL(val)		bfin_write16(USB_DMA5CONTROL, val)
#define bfin_read_USB_DMA5ADDRLOW()		bfin_read16(USB_DMA5ADDRLOW)
#define bfin_write_USB_DMA5ADDRLOW(val)		bfin_write16(USB_DMA5ADDRLOW, val)
#define bfin_read_USB_DMA5ADDRHIGH()		bfin_read16(USB_DMA5ADDRHIGH)
#define bfin_write_USB_DMA5ADDRHIGH(val)	bfin_write16(USB_DMA5ADDRHIGH, val)
#define bfin_read_USB_DMA5COUNTLOW()		bfin_read16(USB_DMA5COUNTLOW)
#define bfin_write_USB_DMA5COUNTLOW(val)	bfin_write16(USB_DMA5COUNTLOW, val)
#define bfin_read_USB_DMA5COUNTHIGH()		bfin_read16(USB_DMA5COUNTHIGH)
#define bfin_write_USB_DMA5COUNTHIGH(val)	bfin_write16(USB_DMA5COUNTHIGH, val)

/* USB Channel 6 Config Registers */

#define bfin_read_USB_DMA6CONTROL()		bfin_read16(USB_DMA6CONTROL)
#define bfin_write_USB_DMA6CONTROL(val)		bfin_write16(USB_DMA6CONTROL, val)
#define bfin_read_USB_DMA6ADDRLOW()		bfin_read16(USB_DMA6ADDRLOW)
#define bfin_write_USB_DMA6ADDRLOW(val)		bfin_write16(USB_DMA6ADDRLOW, val)
#define bfin_read_USB_DMA6ADDRHIGH()		bfin_read16(USB_DMA6ADDRHIGH)
#define bfin_write_USB_DMA6ADDRHIGH(val)	bfin_write16(USB_DMA6ADDRHIGH, val)
#define bfin_read_USB_DMA6COUNTLOW()		bfin_read16(USB_DMA6COUNTLOW)
#define bfin_write_USB_DMA6COUNTLOW(val)	bfin_write16(USB_DMA6COUNTLOW, val)
#define bfin_read_USB_DMA6COUNTHIGH()		bfin_read16(USB_DMA6COUNTHIGH)
#define bfin_write_USB_DMA6COUNTHIGH(val)	bfin_write16(USB_DMA6COUNTHIGH, val)

/* USB Channel 7 Config Registers */

#define bfin_read_USB_DMA7CONTROL()		bfin_read16(USB_DMA7CONTROL)
#define bfin_write_USB_DMA7CONTROL(val)		bfin_write16(USB_DMA7CONTROL, val)
#define bfin_read_USB_DMA7ADDRLOW()		bfin_read16(USB_DMA7ADDRLOW)
#define bfin_write_USB_DMA7ADDRLOW(val)		bfin_write16(USB_DMA7ADDRLOW, val)
#define bfin_read_USB_DMA7ADDRHIGH()		bfin_read16(USB_DMA7ADDRHIGH)
#define bfin_write_USB_DMA7ADDRHIGH(val)	bfin_write16(USB_DMA7ADDRHIGH, val)
#define bfin_read_USB_DMA7COUNTLOW()		bfin_read16(USB_DMA7COUNTLOW)
#define bfin_write_USB_DMA7COUNTLOW(val)	bfin_write16(USB_DMA7COUNTLOW, val)
#define bfin_read_USB_DMA7COUNTHIGH()		bfin_read16(USB_DMA7COUNTHIGH)
#define bfin_write_USB_DMA7COUNTHIGH(val)	bfin_write16(USB_DMA7COUNTHIGH, val)

/* Keybfin_read_()ad Registers */

#define bfin_read_KPAD_CTL()			bfin_read16(KPAD_CTL)
#define bfin_write_KPAD_CTL(val)		bfin_write16(KPAD_CTL, val)
#define bfin_read_KPAD_PRESCALE()		bfin_read16(KPAD_PRESCALE)
#define bfin_write_KPAD_PRESCALE(val)		bfin_write16(KPAD_PRESCALE, val)
#define bfin_read_KPAD_MSEL()			bfin_read16(KPAD_MSEL)
#define bfin_write_KPAD_MSEL(val)		bfin_write16(KPAD_MSEL, val)
#define bfin_read_KPAD_ROWCOL()			bfin_read16(KPAD_ROWCOL)
#define bfin_write_KPAD_ROWCOL(val)		bfin_write16(KPAD_ROWCOL, val)
#define bfin_read_KPAD_STAT()			bfin_read16(KPAD_STAT)
#define bfin_write_KPAD_STAT(val)		bfin_write16(KPAD_STAT, val)
#define bfin_read_KPAD_SOFTEVAL()		bfin_read16(KPAD_SOFTEVAL)
#define bfin_write_KPAD_SOFTEVAL(val)		bfin_write16(KPAD_SOFTEVAL, val)

#endif /* _CDEF_BF542_H */
