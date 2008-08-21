 /*
  * File:        include/asm-blackfin/mach-common/cdef_LPBlackfin.h
  * Based on:
  * Author:      unknown
  *              COPYRIGHT 2005 Analog Devices
  * Created:     ?
  * Description:
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

#ifndef _CDEF_LPBLACKFIN_H
#define _CDEF_LPBLACKFIN_H

/*#if !defined(__ADSPLPBLACKFIN__)
#warning cdef_LPBlackfin.h should only be included for 532 compatible chips.
#endif
*/
#include <asm/mach-common/def_LPBlackfin.h>

/*Cache & SRAM Memory*/
#define bfin_read_SRAM_BASE_ADDRESS()        bfin_read32(SRAM_BASE_ADDRESS)
#define bfin_write_SRAM_BASE_ADDRESS(val)    bfin_write32(SRAM_BASE_ADDRESS,val)
#define bfin_read_DMEM_CONTROL()             bfin_read32(DMEM_CONTROL)
#define bfin_write_DMEM_CONTROL(val)         bfin_write32(DMEM_CONTROL,val)
#define bfin_read_DCPLB_STATUS()             bfin_read32(DCPLB_STATUS)
#define bfin_write_DCPLB_STATUS(val)         bfin_write32(DCPLB_STATUS,val)
#define bfin_read_DCPLB_FAULT_ADDR()         bfin_read32(DCPLB_FAULT_ADDR)
#define bfin_write_DCPLB_FAULT_ADDR(val)     bfin_write32(DCPLB_FAULT_ADDR,val)
/*
#define MMR_TIMEOUT            0xFFE00010
*/
#define bfin_read_DCPLB_ADDR0()              bfin_read32(DCPLB_ADDR0)
#define bfin_write_DCPLB_ADDR0(val)          bfin_write32(DCPLB_ADDR0,val)
#define bfin_read_DCPLB_ADDR1()              bfin_read32(DCPLB_ADDR1)
#define bfin_write_DCPLB_ADDR1(val)          bfin_write32(DCPLB_ADDR1,val)
#define bfin_read_DCPLB_ADDR2()              bfin_read32(DCPLB_ADDR2)
#define bfin_write_DCPLB_ADDR2(val)          bfin_write32(DCPLB_ADDR2,val)
#define bfin_read_DCPLB_ADDR3()              bfin_read32(DCPLB_ADDR3)
#define bfin_write_DCPLB_ADDR3(val)          bfin_write32(DCPLB_ADDR3,val)
#define bfin_read_DCPLB_ADDR4()              bfin_read32(DCPLB_ADDR4)
#define bfin_write_DCPLB_ADDR4(val)          bfin_write32(DCPLB_ADDR4,val)
#define bfin_read_DCPLB_ADDR5()              bfin_read32(DCPLB_ADDR5)
#define bfin_write_DCPLB_ADDR5(val)          bfin_write32(DCPLB_ADDR5,val)
#define bfin_read_DCPLB_ADDR6()              bfin_read32(DCPLB_ADDR6)
#define bfin_write_DCPLB_ADDR6(val)          bfin_write32(DCPLB_ADDR6,val)
#define bfin_read_DCPLB_ADDR7()              bfin_read32(DCPLB_ADDR7)
#define bfin_write_DCPLB_ADDR7(val)          bfin_write32(DCPLB_ADDR7,val)
#define bfin_read_DCPLB_ADDR8()              bfin_read32(DCPLB_ADDR8)
#define bfin_write_DCPLB_ADDR8(val)          bfin_write32(DCPLB_ADDR8,val)
#define bfin_read_DCPLB_ADDR9()              bfin_read32(DCPLB_ADDR9)
#define bfin_write_DCPLB_ADDR9(val)          bfin_write32(DCPLB_ADDR9,val)
#define bfin_read_DCPLB_ADDR10()             bfin_read32(DCPLB_ADDR10)
#define bfin_write_DCPLB_ADDR10(val)         bfin_write32(DCPLB_ADDR10,val)
#define bfin_read_DCPLB_ADDR11()             bfin_read32(DCPLB_ADDR11)
#define bfin_write_DCPLB_ADDR11(val)         bfin_write32(DCPLB_ADDR11,val)
#define bfin_read_DCPLB_ADDR12()             bfin_read32(DCPLB_ADDR12)
#define bfin_write_DCPLB_ADDR12(val)         bfin_write32(DCPLB_ADDR12,val)
#define bfin_read_DCPLB_ADDR13()             bfin_read32(DCPLB_ADDR13)
#define bfin_write_DCPLB_ADDR13(val)         bfin_write32(DCPLB_ADDR13,val)
#define bfin_read_DCPLB_ADDR14()             bfin_read32(DCPLB_ADDR14)
#define bfin_write_DCPLB_ADDR14(val)         bfin_write32(DCPLB_ADDR14,val)
#define bfin_read_DCPLB_ADDR15()             bfin_read32(DCPLB_ADDR15)
#define bfin_write_DCPLB_ADDR15(val)         bfin_write32(DCPLB_ADDR15,val)
#define bfin_read_DCPLB_DATA0()              bfin_read32(DCPLB_DATA0)
#define bfin_write_DCPLB_DATA0(val)          bfin_write32(DCPLB_DATA0,val)
#define bfin_read_DCPLB_DATA1()              bfin_read32(DCPLB_DATA1)
#define bfin_write_DCPLB_DATA1(val)          bfin_write32(DCPLB_DATA1,val)
#define bfin_read_DCPLB_DATA2()              bfin_read32(DCPLB_DATA2)
#define bfin_write_DCPLB_DATA2(val)          bfin_write32(DCPLB_DATA2,val)
#define bfin_read_DCPLB_DATA3()              bfin_read32(DCPLB_DATA3)
#define bfin_write_DCPLB_DATA3(val)          bfin_write32(DCPLB_DATA3,val)
#define bfin_read_DCPLB_DATA4()              bfin_read32(DCPLB_DATA4)
#define bfin_write_DCPLB_DATA4(val)          bfin_write32(DCPLB_DATA4,val)
#define bfin_read_DCPLB_DATA5()              bfin_read32(DCPLB_DATA5)
#define bfin_write_DCPLB_DATA5(val)          bfin_write32(DCPLB_DATA5,val)
#define bfin_read_DCPLB_DATA6()              bfin_read32(DCPLB_DATA6)
#define bfin_write_DCPLB_DATA6(val)          bfin_write32(DCPLB_DATA6,val)
#define bfin_read_DCPLB_DATA7()              bfin_read32(DCPLB_DATA7)
#define bfin_write_DCPLB_DATA7(val)          bfin_write32(DCPLB_DATA7,val)
#define bfin_read_DCPLB_DATA8()              bfin_read32(DCPLB_DATA8)
#define bfin_write_DCPLB_DATA8(val)          bfin_write32(DCPLB_DATA8,val)
#define bfin_read_DCPLB_DATA9()              bfin_read32(DCPLB_DATA9)
#define bfin_write_DCPLB_DATA9(val)          bfin_write32(DCPLB_DATA9,val)
#define bfin_read_DCPLB_DATA10()             bfin_read32(DCPLB_DATA10)
#define bfin_write_DCPLB_DATA10(val)         bfin_write32(DCPLB_DATA10,val)
#define bfin_read_DCPLB_DATA11()             bfin_read32(DCPLB_DATA11)
#define bfin_write_DCPLB_DATA11(val)         bfin_write32(DCPLB_DATA11,val)
#define bfin_read_DCPLB_DATA12()             bfin_read32(DCPLB_DATA12)
#define bfin_write_DCPLB_DATA12(val)         bfin_write32(DCPLB_DATA12,val)
#define bfin_read_DCPLB_DATA13()             bfin_read32(DCPLB_DATA13)
#define bfin_write_DCPLB_DATA13(val)         bfin_write32(DCPLB_DATA13,val)
#define bfin_read_DCPLB_DATA14()             bfin_read32(DCPLB_DATA14)
#define bfin_write_DCPLB_DATA14(val)         bfin_write32(DCPLB_DATA14,val)
#define bfin_read_DCPLB_DATA15()             bfin_read32(DCPLB_DATA15)
#define bfin_write_DCPLB_DATA15(val)         bfin_write32(DCPLB_DATA15,val)
#define bfin_read_DTEST_COMMAND()            bfin_read32(DTEST_COMMAND)
#define bfin_write_DTEST_COMMAND(val)        bfin_write32(DTEST_COMMAND,val)
/*
#define DTEST_INDEX            0xFFE00304
*/
#define bfin_read_DTEST_DATA0()              bfin_read32(DTEST_DATA0)
#define bfin_write_DTEST_DATA0(val)          bfin_write32(DTEST_DATA0,val)
#define bfin_read_DTEST_DATA1()              bfin_read32(DTEST_DATA1)
#define bfin_write_DTEST_DATA1(val)          bfin_write32(DTEST_DATA1,val)
/*
#define DTEST_DATA2            0xFFE00408
#define DTEST_DATA3            0xFFE0040C
*/
#define bfin_read_IMEM_CONTROL()             bfin_read32(IMEM_CONTROL)
#define bfin_write_IMEM_CONTROL(val)         bfin_write32(IMEM_CONTROL,val)
#define bfin_read_ICPLB_STATUS()             bfin_read32(ICPLB_STATUS)
#define bfin_write_ICPLB_STATUS(val)         bfin_write32(ICPLB_STATUS,val)
#define bfin_read_ICPLB_FAULT_ADDR()         bfin_read32(ICPLB_FAULT_ADDR)
#define bfin_write_ICPLB_FAULT_ADDR(val)     bfin_write32(ICPLB_FAULT_ADDR,val)
#define bfin_read_ICPLB_ADDR0()              bfin_read32(ICPLB_ADDR0)
#define bfin_write_ICPLB_ADDR0(val)          bfin_write32(ICPLB_ADDR0,val)
#define bfin_read_ICPLB_ADDR1()              bfin_read32(ICPLB_ADDR1)
#define bfin_write_ICPLB_ADDR1(val)          bfin_write32(ICPLB_ADDR1,val)
#define bfin_read_ICPLB_ADDR2()              bfin_read32(ICPLB_ADDR2)
#define bfin_write_ICPLB_ADDR2(val)          bfin_write32(ICPLB_ADDR2,val)
#define bfin_read_ICPLB_ADDR3()              bfin_read32(ICPLB_ADDR3)
#define bfin_write_ICPLB_ADDR3(val)          bfin_write32(ICPLB_ADDR3,val)
#define bfin_read_ICPLB_ADDR4()              bfin_read32(ICPLB_ADDR4)
#define bfin_write_ICPLB_ADDR4(val)          bfin_write32(ICPLB_ADDR4,val)
#define bfin_read_ICPLB_ADDR5()              bfin_read32(ICPLB_ADDR5)
#define bfin_write_ICPLB_ADDR5(val)          bfin_write32(ICPLB_ADDR5,val)
#define bfin_read_ICPLB_ADDR6()              bfin_read32(ICPLB_ADDR6)
#define bfin_write_ICPLB_ADDR6(val)          bfin_write32(ICPLB_ADDR6,val)
#define bfin_read_ICPLB_ADDR7()              bfin_read32(ICPLB_ADDR7)
#define bfin_write_ICPLB_ADDR7(val)          bfin_write32(ICPLB_ADDR7,val)
#define bfin_read_ICPLB_ADDR8()              bfin_read32(ICPLB_ADDR8)
#define bfin_write_ICPLB_ADDR8(val)          bfin_write32(ICPLB_ADDR8,val)
#define bfin_read_ICPLB_ADDR9()              bfin_read32(ICPLB_ADDR9)
#define bfin_write_ICPLB_ADDR9(val)          bfin_write32(ICPLB_ADDR9,val)
#define bfin_read_ICPLB_ADDR10()             bfin_read32(ICPLB_ADDR10)
#define bfin_write_ICPLB_ADDR10(val)         bfin_write32(ICPLB_ADDR10,val)
#define bfin_read_ICPLB_ADDR11()             bfin_read32(ICPLB_ADDR11)
#define bfin_write_ICPLB_ADDR11(val)         bfin_write32(ICPLB_ADDR11,val)
#define bfin_read_ICPLB_ADDR12()             bfin_read32(ICPLB_ADDR12)
#define bfin_write_ICPLB_ADDR12(val)         bfin_write32(ICPLB_ADDR12,val)
#define bfin_read_ICPLB_ADDR13()             bfin_read32(ICPLB_ADDR13)
#define bfin_write_ICPLB_ADDR13(val)         bfin_write32(ICPLB_ADDR13,val)
#define bfin_read_ICPLB_ADDR14()             bfin_read32(ICPLB_ADDR14)
#define bfin_write_ICPLB_ADDR14(val)         bfin_write32(ICPLB_ADDR14,val)
#define bfin_read_ICPLB_ADDR15()             bfin_read32(ICPLB_ADDR15)
#define bfin_write_ICPLB_ADDR15(val)         bfin_write32(ICPLB_ADDR15,val)
#define bfin_read_ICPLB_DATA0()              bfin_read32(ICPLB_DATA0)
#define bfin_write_ICPLB_DATA0(val)          bfin_write32(ICPLB_DATA0,val)
#define bfin_read_ICPLB_DATA1()              bfin_read32(ICPLB_DATA1)
#define bfin_write_ICPLB_DATA1(val)          bfin_write32(ICPLB_DATA1,val)
#define bfin_read_ICPLB_DATA2()              bfin_read32(ICPLB_DATA2)
#define bfin_write_ICPLB_DATA2(val)          bfin_write32(ICPLB_DATA2,val)
#define bfin_read_ICPLB_DATA3()              bfin_read32(ICPLB_DATA3)
#define bfin_write_ICPLB_DATA3(val)          bfin_write32(ICPLB_DATA3,val)
#define bfin_read_ICPLB_DATA4()              bfin_read32(ICPLB_DATA4)
#define bfin_write_ICPLB_DATA4(val)          bfin_write32(ICPLB_DATA4,val)
#define bfin_read_ICPLB_DATA5()              bfin_read32(ICPLB_DATA5)
#define bfin_write_ICPLB_DATA5(val)          bfin_write32(ICPLB_DATA5,val)
#define bfin_read_ICPLB_DATA6()              bfin_read32(ICPLB_DATA6)
#define bfin_write_ICPLB_DATA6(val)          bfin_write32(ICPLB_DATA6,val)
#define bfin_read_ICPLB_DATA7()              bfin_read32(ICPLB_DATA7)
#define bfin_write_ICPLB_DATA7(val)          bfin_write32(ICPLB_DATA7,val)
#define bfin_read_ICPLB_DATA8()              bfin_read32(ICPLB_DATA8)
#define bfin_write_ICPLB_DATA8(val)          bfin_write32(ICPLB_DATA8,val)
#define bfin_read_ICPLB_DATA9()              bfin_read32(ICPLB_DATA9)
#define bfin_write_ICPLB_DATA9(val)          bfin_write32(ICPLB_DATA9,val)
#define bfin_read_ICPLB_DATA10()             bfin_read32(ICPLB_DATA10)
#define bfin_write_ICPLB_DATA10(val)         bfin_write32(ICPLB_DATA10,val)
#define bfin_read_ICPLB_DATA11()             bfin_read32(ICPLB_DATA11)
#define bfin_write_ICPLB_DATA11(val)         bfin_write32(ICPLB_DATA11,val)
#define bfin_read_ICPLB_DATA12()             bfin_read32(ICPLB_DATA12)
#define bfin_write_ICPLB_DATA12(val)         bfin_write32(ICPLB_DATA12,val)
#define bfin_read_ICPLB_DATA13()             bfin_read32(ICPLB_DATA13)
#define bfin_write_ICPLB_DATA13(val)         bfin_write32(ICPLB_DATA13,val)
#define bfin_read_ICPLB_DATA14()             bfin_read32(ICPLB_DATA14)
#define bfin_write_ICPLB_DATA14(val)         bfin_write32(ICPLB_DATA14,val)
#define bfin_read_ICPLB_DATA15()             bfin_read32(ICPLB_DATA15)
#define bfin_write_ICPLB_DATA15(val)         bfin_write32(ICPLB_DATA15,val)
#define bfin_read_ITEST_COMMAND()            bfin_read32(ITEST_COMMAND)
#define bfin_write_ITEST_COMMAND(val)        bfin_write32(ITEST_COMMAND,val)
#if 0
#define ITEST_INDEX            0xFFE01304   /* Instruction Test Index Register */
#endif
#define bfin_read_ITEST_DATA0()              bfin_read32(ITEST_DATA0)
#define bfin_write_ITEST_DATA0(val)          bfin_write32(ITEST_DATA0,val)
#define bfin_read_ITEST_DATA1()              bfin_read32(ITEST_DATA1)
#define bfin_write_ITEST_DATA1(val)          bfin_write32(ITEST_DATA1,val)

/* Event/Interrupt Registers*/

#define bfin_read_EVT0()                     bfin_read32(EVT0)
#define bfin_write_EVT0(val)                 bfin_write32(EVT0,val)
#define bfin_read_EVT1()                     bfin_read32(EVT1)
#define bfin_write_EVT1(val)                 bfin_write32(EVT1,val)
#define bfin_read_EVT2()                     bfin_read32(EVT2)
#define bfin_write_EVT2(val)                 bfin_write32(EVT2,val)
#define bfin_read_EVT3()                     bfin_read32(EVT3)
#define bfin_write_EVT3(val)                 bfin_write32(EVT3,val)
#define bfin_read_EVT4()                     bfin_read32(EVT4)
#define bfin_write_EVT4(val)                 bfin_write32(EVT4,val)
#define bfin_read_EVT5()                     bfin_read32(EVT5)
#define bfin_write_EVT5(val)                 bfin_write32(EVT5,val)
#define bfin_read_EVT6()                     bfin_read32(EVT6)
#define bfin_write_EVT6(val)                 bfin_write32(EVT6,val)
#define bfin_read_EVT7()                     bfin_read32(EVT7)
#define bfin_write_EVT7(val)                 bfin_write32(EVT7,val)
#define bfin_read_EVT8()                     bfin_read32(EVT8)
#define bfin_write_EVT8(val)                 bfin_write32(EVT8,val)
#define bfin_read_EVT9()                     bfin_read32(EVT9)
#define bfin_write_EVT9(val)                 bfin_write32(EVT9,val)
#define bfin_read_EVT10()                    bfin_read32(EVT10)
#define bfin_write_EVT10(val)                bfin_write32(EVT10,val)
#define bfin_read_EVT11()                    bfin_read32(EVT11)
#define bfin_write_EVT11(val)                bfin_write32(EVT11,val)
#define bfin_read_EVT12()                    bfin_read32(EVT12)
#define bfin_write_EVT12(val)                bfin_write32(EVT12,val)
#define bfin_read_EVT13()                    bfin_read32(EVT13)
#define bfin_write_EVT13(val)                bfin_write32(EVT13,val)
#define bfin_read_EVT14()                    bfin_read32(EVT14)
#define bfin_write_EVT14(val)                bfin_write32(EVT14,val)
#define bfin_read_EVT15()                    bfin_read32(EVT15)
#define bfin_write_EVT15(val)                bfin_write32(EVT15,val)
#define bfin_read_IMASK()                    bfin_read32(IMASK)
#define bfin_write_IMASK(val)                bfin_write32(IMASK,val)
#define bfin_read_IPEND()                    bfin_read32(IPEND)
#define bfin_write_IPEND(val)                bfin_write32(IPEND,val)
#define bfin_read_ILAT()                     bfin_read32(ILAT)
#define bfin_write_ILAT(val)                 bfin_write32(ILAT,val)

/*Core Timer Registers*/
#define bfin_read_TCNTL()                    bfin_read32(TCNTL)
#define bfin_write_TCNTL(val)                bfin_write32(TCNTL,val)
#define bfin_read_TPERIOD()                  bfin_read32(TPERIOD)
#define bfin_write_TPERIOD(val)              bfin_write32(TPERIOD,val)
#define bfin_read_TSCALE()                   bfin_read32(TSCALE)
#define bfin_write_TSCALE(val)               bfin_write32(TSCALE,val)
#define bfin_read_TCOUNT()                   bfin_read32(TCOUNT)
#define bfin_write_TCOUNT(val)               bfin_write32(TCOUNT,val)

/*Debug/MP/Emulation Registers*/
#define bfin_read_DSPID()                    bfin_read32(DSPID)
#define bfin_write_DSPID(val)                bfin_write32(DSPID,val)
#define bfin_read_DBGCTL()                   bfin_read32(DBGCTL)
#define bfin_write_DBGCTL(val)               bfin_write32(DBGCTL,val)
#define bfin_read_DBGSTAT()                  bfin_read32(DBGSTAT)
#define bfin_write_DBGSTAT(val)              bfin_write32(DBGSTAT,val)
#define bfin_read_EMUDAT()                   bfin_read32(EMUDAT)
#define bfin_write_EMUDAT(val)               bfin_write32(EMUDAT,val)

/*Trace Buffer Registers*/
#define bfin_read_TBUFCTL()                  bfin_read32(TBUFCTL)
#define bfin_write_TBUFCTL(val)              bfin_write32(TBUFCTL,val)
#define bfin_read_TBUFSTAT()                 bfin_read32(TBUFSTAT)
#define bfin_write_TBUFSTAT(val)             bfin_write32(TBUFSTAT,val)
#define bfin_read_TBUF()                     bfin_read32(TBUF)
#define bfin_write_TBUF(val)                 bfin_write32(TBUF,val)

/*Watch Point Control Registers*/
#define bfin_read_WPIACTL()                  bfin_read32(WPIACTL)
#define bfin_write_WPIACTL(val)              bfin_write32(WPIACTL,val)
#define bfin_read_WPIA0()                    bfin_read32(WPIA0)
#define bfin_write_WPIA0(val)                bfin_write32(WPIA0,val)
#define bfin_read_WPIA1()                    bfin_read32(WPIA1)
#define bfin_write_WPIA1(val)                bfin_write32(WPIA1,val)
#define bfin_read_WPIA2()                    bfin_read32(WPIA2)
#define bfin_write_WPIA2(val)                bfin_write32(WPIA2,val)
#define bfin_read_WPIA3()                    bfin_read32(WPIA3)
#define bfin_write_WPIA3(val)                bfin_write32(WPIA3,val)
#define bfin_read_WPIA4()                    bfin_read32(WPIA4)
#define bfin_write_WPIA4(val)                bfin_write32(WPIA4,val)
#define bfin_read_WPIA5()                    bfin_read32(WPIA5)
#define bfin_write_WPIA5(val)                bfin_write32(WPIA5,val)
#define bfin_read_WPIACNT0()                 bfin_read32(WPIACNT0)
#define bfin_write_WPIACNT0(val)             bfin_write32(WPIACNT0,val)
#define bfin_read_WPIACNT1()                 bfin_read32(WPIACNT1)
#define bfin_write_WPIACNT1(val)             bfin_write32(WPIACNT1,val)
#define bfin_read_WPIACNT2()                 bfin_read32(WPIACNT2)
#define bfin_write_WPIACNT2(val)             bfin_write32(WPIACNT2,val)
#define bfin_read_WPIACNT3()                 bfin_read32(WPIACNT3)
#define bfin_write_WPIACNT3(val)             bfin_write32(WPIACNT3,val)
#define bfin_read_WPIACNT4()                 bfin_read32(WPIACNT4)
#define bfin_write_WPIACNT4(val)             bfin_write32(WPIACNT4,val)
#define bfin_read_WPIACNT5()                 bfin_read32(WPIACNT5)
#define bfin_write_WPIACNT5(val)             bfin_write32(WPIACNT5,val)
#define bfin_read_WPDACTL()                  bfin_read32(WPDACTL)
#define bfin_write_WPDACTL(val)              bfin_write32(WPDACTL,val)
#define bfin_read_WPDA0()                    bfin_read32(WPDA0)
#define bfin_write_WPDA0(val)                bfin_write32(WPDA0,val)
#define bfin_read_WPDA1()                    bfin_read32(WPDA1)
#define bfin_write_WPDA1(val)                bfin_write32(WPDA1,val)
#define bfin_read_WPDACNT0()                 bfin_read32(WPDACNT0)
#define bfin_write_WPDACNT0(val)             bfin_write32(WPDACNT0,val)
#define bfin_read_WPDACNT1()                 bfin_read32(WPDACNT1)
#define bfin_write_WPDACNT1(val)             bfin_write32(WPDACNT1,val)
#define bfin_read_WPSTAT()                   bfin_read32(WPSTAT)
#define bfin_write_WPSTAT(val)               bfin_write32(WPSTAT,val)

/*Performance Monitor Registers*/
#define bfin_read_PFCTL()                    bfin_read32(PFCTL)
#define bfin_write_PFCTL(val)                bfin_write32(PFCTL,val)
#define bfin_read_PFCNTR0()                  bfin_read32(PFCNTR0)
#define bfin_write_PFCNTR0(val)              bfin_write32(PFCNTR0,val)
#define bfin_read_PFCNTR1()                  bfin_read32(PFCNTR1)
#define bfin_write_PFCNTR1(val)              bfin_write32(PFCNTR1,val)

/*
#define IPRIO                  0xFFE02110
*/

#endif				/* _CDEF_LPBLACKFIN_H */
