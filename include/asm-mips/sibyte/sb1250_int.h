/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  Interrupt Mapper definitions		File: sb1250_int.h
    *
    *  This module contains constants for manipulating the SB1250's
    *  interrupt mapper and definitions for the interrupt sources.
    *
    *  SB1250 specification level:  User's manual 1/02/02
    *
    *********************************************************************
    *
    *  Copyright 2000,2001,2002,2003
    *  Broadcom Corporation. All rights reserved.
    *
    *  This program is free software; you can redistribute it and/or
    *  modify it under the terms of the GNU General Public License as
    *  published by the Free Software Foundation; either version 2 of
    *  the License, or (at your option) any later version.
    *
    *  This program is distributed in the hope that it will be useful,
    *  but WITHOUT ANY WARRANTY; without even the implied warranty of
    *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    *  GNU General Public License for more details.
    *
    *  You should have received a copy of the GNU General Public License
    *  along with this program; if not, write to the Free Software
    *  Foundation, Inc., 59 Temple Place, Suite 330, Boston,
    *  MA 02111-1307 USA
    ********************************************************************* */


#ifndef _SB1250_INT_H
#define _SB1250_INT_H

#include "sb1250_defs.h"

/*  *********************************************************************
    *  Interrupt Mapper Constants
    ********************************************************************* */

/*
 * Interrupt sources (Table 4-8, UM 0.2)
 *
 * First, the interrupt numbers.
 */

#define K_INT_SOURCES               64

#define K_INT_WATCHDOG_TIMER_0      0
#define K_INT_WATCHDOG_TIMER_1      1
#define K_INT_TIMER_0               2
#define K_INT_TIMER_1               3
#define K_INT_TIMER_2               4
#define K_INT_TIMER_3               5
#define K_INT_SMB_0                 6
#define K_INT_SMB_1                 7
#define K_INT_UART_0                8
#define K_INT_UART_1                9
#define K_INT_SER_0                 10
#define K_INT_SER_1                 11
#define K_INT_PCMCIA                12
#define K_INT_ADDR_TRAP             13
#define K_INT_PERF_CNT              14
#define K_INT_TRACE_FREEZE          15
#define K_INT_BAD_ECC               16
#define K_INT_COR_ECC               17
#define K_INT_IO_BUS                18
#define K_INT_MAC_0                 19
#define K_INT_MAC_1                 20
#define K_INT_MAC_2                 21
#define K_INT_DM_CH_0               22
#define K_INT_DM_CH_1               23
#define K_INT_DM_CH_2               24
#define K_INT_DM_CH_3               25
#define K_INT_MBOX_0                26
#define K_INT_MBOX_1                27
#define K_INT_MBOX_2                28
#define K_INT_MBOX_3                29
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define K_INT_CYCLE_CP0_INT	    30
#define K_INT_CYCLE_CP1_INT	    31
#endif /* 1250 PASS2 || 112x PASS1 */
#define K_INT_GPIO_0                32
#define K_INT_GPIO_1                33
#define K_INT_GPIO_2                34
#define K_INT_GPIO_3                35
#define K_INT_GPIO_4                36
#define K_INT_GPIO_5                37
#define K_INT_GPIO_6                38
#define K_INT_GPIO_7                39
#define K_INT_GPIO_8                40
#define K_INT_GPIO_9                41
#define K_INT_GPIO_10               42
#define K_INT_GPIO_11               43
#define K_INT_GPIO_12               44
#define K_INT_GPIO_13               45
#define K_INT_GPIO_14               46
#define K_INT_GPIO_15               47
#define K_INT_LDT_FATAL             48
#define K_INT_LDT_NONFATAL          49
#define K_INT_LDT_SMI               50
#define K_INT_LDT_NMI               51
#define K_INT_LDT_INIT              52
#define K_INT_LDT_STARTUP           53
#define K_INT_LDT_EXT               54
#define K_INT_PCI_ERROR             55
#define K_INT_PCI_INTA              56
#define K_INT_PCI_INTB              57
#define K_INT_PCI_INTC              58
#define K_INT_PCI_INTD              59
#define K_INT_SPARE_2               60
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define K_INT_MAC_0_CH1		    61
#define K_INT_MAC_1_CH1		    62
#define K_INT_MAC_2_CH1		    63
#endif /* 1250 PASS2 || 112x PASS1 */

/*
 * Mask values for each interrupt
 */

#define M_INT_WATCHDOG_TIMER_0      _SB_MAKEMASK1(K_INT_WATCHDOG_TIMER_0)
#define M_INT_WATCHDOG_TIMER_1      _SB_MAKEMASK1(K_INT_WATCHDOG_TIMER_1)
#define M_INT_TIMER_0               _SB_MAKEMASK1(K_INT_TIMER_0)
#define M_INT_TIMER_1               _SB_MAKEMASK1(K_INT_TIMER_1)
#define M_INT_TIMER_2               _SB_MAKEMASK1(K_INT_TIMER_2)
#define M_INT_TIMER_3               _SB_MAKEMASK1(K_INT_TIMER_3)
#define M_INT_SMB_0                 _SB_MAKEMASK1(K_INT_SMB_0)
#define M_INT_SMB_1                 _SB_MAKEMASK1(K_INT_SMB_1)
#define M_INT_UART_0                _SB_MAKEMASK1(K_INT_UART_0)
#define M_INT_UART_1                _SB_MAKEMASK1(K_INT_UART_1)
#define M_INT_SER_0                 _SB_MAKEMASK1(K_INT_SER_0)
#define M_INT_SER_1                 _SB_MAKEMASK1(K_INT_SER_1)
#define M_INT_PCMCIA                _SB_MAKEMASK1(K_INT_PCMCIA)
#define M_INT_ADDR_TRAP             _SB_MAKEMASK1(K_INT_ADDR_TRAP)
#define M_INT_PERF_CNT              _SB_MAKEMASK1(K_INT_PERF_CNT)
#define M_INT_TRACE_FREEZE          _SB_MAKEMASK1(K_INT_TRACE_FREEZE)
#define M_INT_BAD_ECC               _SB_MAKEMASK1(K_INT_BAD_ECC)
#define M_INT_COR_ECC               _SB_MAKEMASK1(K_INT_COR_ECC)
#define M_INT_IO_BUS                _SB_MAKEMASK1(K_INT_IO_BUS)
#define M_INT_MAC_0                 _SB_MAKEMASK1(K_INT_MAC_0)
#define M_INT_MAC_1                 _SB_MAKEMASK1(K_INT_MAC_1)
#define M_INT_MAC_2                 _SB_MAKEMASK1(K_INT_MAC_2)
#define M_INT_DM_CH_0               _SB_MAKEMASK1(K_INT_DM_CH_0)
#define M_INT_DM_CH_1               _SB_MAKEMASK1(K_INT_DM_CH_1)
#define M_INT_DM_CH_2               _SB_MAKEMASK1(K_INT_DM_CH_2)
#define M_INT_DM_CH_3               _SB_MAKEMASK1(K_INT_DM_CH_3)
#define M_INT_MBOX_0                _SB_MAKEMASK1(K_INT_MBOX_0)
#define M_INT_MBOX_1                _SB_MAKEMASK1(K_INT_MBOX_1)
#define M_INT_MBOX_2                _SB_MAKEMASK1(K_INT_MBOX_2)
#define M_INT_MBOX_3                _SB_MAKEMASK1(K_INT_MBOX_3)
#define M_INT_MBOX_ALL              _SB_MAKEMASK(4,K_INT_MBOX_0)
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_INT_CYCLE_CP0_INT	    _SB_MAKEMASK1(K_INT_CYCLE_CP0_INT)
#define M_INT_CYCLE_CP1_INT	    _SB_MAKEMASK1(K_INT_CYCLE_CP1_INT)
#endif /* 1250 PASS2 || 112x PASS1 */
#define M_INT_GPIO_0                _SB_MAKEMASK1(K_INT_GPIO_0)
#define M_INT_GPIO_1                _SB_MAKEMASK1(K_INT_GPIO_1)
#define M_INT_GPIO_2                _SB_MAKEMASK1(K_INT_GPIO_2)
#define M_INT_GPIO_3                _SB_MAKEMASK1(K_INT_GPIO_3)
#define M_INT_GPIO_4                _SB_MAKEMASK1(K_INT_GPIO_4)
#define M_INT_GPIO_5                _SB_MAKEMASK1(K_INT_GPIO_5)
#define M_INT_GPIO_6                _SB_MAKEMASK1(K_INT_GPIO_6)
#define M_INT_GPIO_7                _SB_MAKEMASK1(K_INT_GPIO_7)
#define M_INT_GPIO_8                _SB_MAKEMASK1(K_INT_GPIO_8)
#define M_INT_GPIO_9                _SB_MAKEMASK1(K_INT_GPIO_9)
#define M_INT_GPIO_10               _SB_MAKEMASK1(K_INT_GPIO_10)
#define M_INT_GPIO_11               _SB_MAKEMASK1(K_INT_GPIO_11)
#define M_INT_GPIO_12               _SB_MAKEMASK1(K_INT_GPIO_12)
#define M_INT_GPIO_13               _SB_MAKEMASK1(K_INT_GPIO_13)
#define M_INT_GPIO_14               _SB_MAKEMASK1(K_INT_GPIO_14)
#define M_INT_GPIO_15               _SB_MAKEMASK1(K_INT_GPIO_15)
#define M_INT_LDT_FATAL             _SB_MAKEMASK1(K_INT_LDT_FATAL)
#define M_INT_LDT_NONFATAL          _SB_MAKEMASK1(K_INT_LDT_NONFATAL)
#define M_INT_LDT_SMI               _SB_MAKEMASK1(K_INT_LDT_SMI)
#define M_INT_LDT_NMI               _SB_MAKEMASK1(K_INT_LDT_NMI)
#define M_INT_LDT_INIT              _SB_MAKEMASK1(K_INT_LDT_INIT)
#define M_INT_LDT_STARTUP           _SB_MAKEMASK1(K_INT_LDT_STARTUP)
#define M_INT_LDT_EXT               _SB_MAKEMASK1(K_INT_LDT_EXT)
#define M_INT_PCI_ERROR             _SB_MAKEMASK1(K_INT_PCI_ERROR)
#define M_INT_PCI_INTA              _SB_MAKEMASK1(K_INT_PCI_INTA)
#define M_INT_PCI_INTB              _SB_MAKEMASK1(K_INT_PCI_INTB)
#define M_INT_PCI_INTC              _SB_MAKEMASK1(K_INT_PCI_INTC)
#define M_INT_PCI_INTD              _SB_MAKEMASK1(K_INT_PCI_INTD)
#define M_INT_SPARE_2               _SB_MAKEMASK1(K_INT_SPARE_2)
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_INT_MAC_0_CH1		    _SB_MAKEMASK1(K_INT_MAC_0_CH1)
#define M_INT_MAC_1_CH1		    _SB_MAKEMASK1(K_INT_MAC_1_CH1)
#define M_INT_MAC_2_CH1		    _SB_MAKEMASK1(K_INT_MAC_2_CH1)
#endif /* 1250 PASS2 || 112x PASS1 */

/*
 * Interrupt mappings
 */

#define K_INT_MAP_I0	0		/* interrupt pins on processor */
#define K_INT_MAP_I1	1
#define K_INT_MAP_I2	2
#define K_INT_MAP_I3	3
#define K_INT_MAP_I4	4
#define K_INT_MAP_I5	5
#define K_INT_MAP_NMI	6		/* nonmaskable */
#define K_INT_MAP_DINT	7		/* debug interrupt */

/*
 * LDT Interrupt Set Register (table 4-5)
 */

#define S_INT_LDT_INTMSG	      0
#define M_INT_LDT_INTMSG              _SB_MAKEMASK(3,S_INT_LDT_INTMSG)
#define V_INT_LDT_INTMSG(x)           _SB_MAKEVALUE(x,S_INT_LDT_INTMSG)
#define G_INT_LDT_INTMSG(x)           _SB_GETVALUE(x,S_INT_LDT_INTMSG,M_INT_LDT_INTMSG)

#define K_INT_LDT_INTMSG_FIXED	      0
#define K_INT_LDT_INTMSG_ARBITRATED   1
#define K_INT_LDT_INTMSG_SMI	      2
#define K_INT_LDT_INTMSG_NMI	      3
#define K_INT_LDT_INTMSG_INIT	      4
#define K_INT_LDT_INTMSG_STARTUP      5
#define K_INT_LDT_INTMSG_EXTINT	      6
#define K_INT_LDT_INTMSG_RESERVED     7

#define M_INT_LDT_EDGETRIGGER         0
#define M_INT_LDT_LEVELTRIGGER        _SB_MAKEMASK1(3)

#define M_INT_LDT_PHYSICALDEST        0
#define M_INT_LDT_LOGICALDEST         _SB_MAKEMASK1(4)

#define S_INT_LDT_INTDEST             5
#define M_INT_LDT_INTDEST             _SB_MAKEMASK(10,S_INT_LDT_INTDEST)
#define V_INT_LDT_INTDEST(x)          _SB_MAKEVALUE(x,S_INT_LDT_INTDEST)
#define G_INT_LDT_INTDEST(x)          _SB_GETVALUE(x,S_INT_LDT_INTDEST,M_INT_LDT_INTDEST)

#define S_INT_LDT_VECTOR              13
#define M_INT_LDT_VECTOR              _SB_MAKEMASK(8,S_INT_LDT_VECTOR)
#define V_INT_LDT_VECTOR(x)           _SB_MAKEVALUE(x,S_INT_LDT_VECTOR)
#define G_INT_LDT_VECTOR(x)           _SB_GETVALUE(x,S_INT_LDT_VECTOR,M_INT_LDT_VECTOR)

/*
 * Vector format (Table 4-6)
 */

#define M_LDTVECT_RAISEINT		0x00
#define M_LDTVECT_RAISEMBOX             0x40


#endif	/* 1250/112x */
