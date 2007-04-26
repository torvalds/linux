/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  SCD Constants and Macros			File: sb1250_scd.h
    *
    *  This module contains constants and macros useful for
    *  manipulating the System Control and Debug module on the 1250.
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

#ifndef _SB1250_SCD_H
#define _SB1250_SCD_H

#include "sb1250_defs.h"

/*  *********************************************************************
    *  System control/debug registers
    ********************************************************************* */

/*
 * System Revision Register (Table 4-1)
 */

#define M_SYS_RESERVED		    _SB_MAKEMASK(8,0)

#define S_SYS_REVISION              _SB_MAKE64(8)
#define M_SYS_REVISION              _SB_MAKEMASK(8,S_SYS_REVISION)
#define V_SYS_REVISION(x)           _SB_MAKEVALUE(x,S_SYS_REVISION)
#define G_SYS_REVISION(x)           _SB_GETVALUE(x,S_SYS_REVISION,M_SYS_REVISION)

#define K_SYS_REVISION_BCM1250_PASS1	0x01

#define K_SYS_REVISION_BCM1250_PASS2	0x03
#define K_SYS_REVISION_BCM1250_A1	0x03	/* Pass 2.0 WB */
#define K_SYS_REVISION_BCM1250_A2	0x04	/* Pass 2.0 FC */
#define K_SYS_REVISION_BCM1250_A3	0x05	/* Pass 2.1 FC */
#define K_SYS_REVISION_BCM1250_A4	0x06	/* Pass 2.1 WB */
#define K_SYS_REVISION_BCM1250_A6	0x07	/* OR 0x04 (A2) w/WID != 0 */
#define K_SYS_REVISION_BCM1250_A8	0x0b	/* A8/A10 */
#define K_SYS_REVISION_BCM1250_A9	0x08
#define K_SYS_REVISION_BCM1250_A10	K_SYS_REVISION_BCM1250_A8

#define K_SYS_REVISION_BCM1250_PASS2_2	0x10
#define K_SYS_REVISION_BCM1250_B0	K_SYS_REVISION_BCM1250_B1
#define K_SYS_REVISION_BCM1250_B1	0x10
#define K_SYS_REVISION_BCM1250_B2	0x11

#define K_SYS_REVISION_BCM1250_C0	0x20
#define K_SYS_REVISION_BCM1250_C1	0x21
#define K_SYS_REVISION_BCM1250_C2	0x22
#define K_SYS_REVISION_BCM1250_C3	0x23

#if SIBYTE_HDR_FEATURE_CHIP(1250)
/* XXX: discourage people from using these constants.  */
#define K_SYS_REVISION_PASS1	    K_SYS_REVISION_BCM1250_PASS1
#define K_SYS_REVISION_PASS2	    K_SYS_REVISION_BCM1250_PASS2
#define K_SYS_REVISION_PASS2_2	    K_SYS_REVISION_BCM1250_PASS2_2
#define K_SYS_REVISION_PASS3	    K_SYS_REVISION_BCM1250_PASS3
#define K_SYS_REVISION_BCM1250_PASS3	K_SYS_REVISION_BCM1250_C0
#endif /* 1250 */

#define K_SYS_REVISION_BCM112x_A1	0x20
#define K_SYS_REVISION_BCM112x_A2	0x21
#define K_SYS_REVISION_BCM112x_A3	0x22
#define K_SYS_REVISION_BCM112x_A4	0x23
#define K_SYS_REVISION_BCM112x_B0	0x30

#define K_SYS_REVISION_BCM1480_S0	0x01
#define K_SYS_REVISION_BCM1480_A1	0x02
#define K_SYS_REVISION_BCM1480_A2	0x03
#define K_SYS_REVISION_BCM1480_A3	0x04
#define K_SYS_REVISION_BCM1480_B0	0x11

/*Cache size - 23:20  of revision register*/
#define S_SYS_L2C_SIZE            _SB_MAKE64(20)
#define M_SYS_L2C_SIZE            _SB_MAKEMASK(4,S_SYS_L2C_SIZE)
#define V_SYS_L2C_SIZE(x)         _SB_MAKEVALUE(x,S_SYS_L2C_SIZE)
#define G_SYS_L2C_SIZE(x)         _SB_GETVALUE(x,S_SYS_L2C_SIZE,M_SYS_L2C_SIZE)

#define K_SYS_L2C_SIZE_1MB	0
#define K_SYS_L2C_SIZE_512KB	5
#define K_SYS_L2C_SIZE_256KB	2
#define K_SYS_L2C_SIZE_128KB	1

#define K_SYS_L2C_SIZE_BCM1250	K_SYS_L2C_SIZE_512KB
#define K_SYS_L2C_SIZE_BCM1125	K_SYS_L2C_SIZE_256KB
#define K_SYS_L2C_SIZE_BCM1122	K_SYS_L2C_SIZE_128KB


/* Number of CPU cores, bits 27:24  of revision register*/
#define S_SYS_NUM_CPUS            _SB_MAKE64(24)
#define M_SYS_NUM_CPUS            _SB_MAKEMASK(4,S_SYS_NUM_CPUS)
#define V_SYS_NUM_CPUS(x)         _SB_MAKEVALUE(x,S_SYS_NUM_CPUS)
#define G_SYS_NUM_CPUS(x)         _SB_GETVALUE(x,S_SYS_NUM_CPUS,M_SYS_NUM_CPUS)


/* XXX: discourage people from using these constants.  */
#define S_SYS_PART                  _SB_MAKE64(16)
#define M_SYS_PART                  _SB_MAKEMASK(16,S_SYS_PART)
#define V_SYS_PART(x)               _SB_MAKEVALUE(x,S_SYS_PART)
#define G_SYS_PART(x)               _SB_GETVALUE(x,S_SYS_PART,M_SYS_PART)

/* XXX: discourage people from using these constants.  */
#define K_SYS_PART_SB1250           0x1250
#define K_SYS_PART_BCM1120          0x1121
#define K_SYS_PART_BCM1125          0x1123
#define K_SYS_PART_BCM1125H         0x1124
#define K_SYS_PART_BCM1122          0x1113


/* The "peripheral set" (SOC type) is the low 4 bits of the "part" field.  */
#define S_SYS_SOC_TYPE              _SB_MAKE64(16)
#define M_SYS_SOC_TYPE              _SB_MAKEMASK(4,S_SYS_SOC_TYPE)
#define V_SYS_SOC_TYPE(x)           _SB_MAKEVALUE(x,S_SYS_SOC_TYPE)
#define G_SYS_SOC_TYPE(x)           _SB_GETVALUE(x,S_SYS_SOC_TYPE,M_SYS_SOC_TYPE)

#define K_SYS_SOC_TYPE_BCM1250      0x0
#define K_SYS_SOC_TYPE_BCM1120      0x1
#define K_SYS_SOC_TYPE_BCM1250_ALT  0x2		/* 1250pass2 w/ 1/4 L2.  */
#define K_SYS_SOC_TYPE_BCM1125      0x3
#define K_SYS_SOC_TYPE_BCM1125H     0x4
#define K_SYS_SOC_TYPE_BCM1250_ALT2 0x5		/* 1250pass2 w/ 1/2 L2.  */
#define K_SYS_SOC_TYPE_BCM1x80      0x6
#define K_SYS_SOC_TYPE_BCM1x55      0x7

/*
 * Calculate correct SOC type given a copy of system revision register.
 *
 * (For the assembler version, sysrev and dest may be the same register.
 * Also, it clobbers AT.)
 */
#ifdef __ASSEMBLY__
#define SYS_SOC_TYPE(dest, sysrev)					\
	.set push ;							\
	.set reorder ;							\
	dsrl	dest, sysrev, S_SYS_SOC_TYPE ;				\
	andi	dest, dest, (M_SYS_SOC_TYPE >> S_SYS_SOC_TYPE);		\
	beq	dest, K_SYS_SOC_TYPE_BCM1250_ALT, 991f ;		\
	beq	dest, K_SYS_SOC_TYPE_BCM1250_ALT2, 991f	 ;		\
	b	992f ;							\
991:	li	dest, K_SYS_SOC_TYPE_BCM1250 ;				\
992:									\
	.set pop
#else
#define SYS_SOC_TYPE(sysrev)						\
	((G_SYS_SOC_TYPE(sysrev) == K_SYS_SOC_TYPE_BCM1250_ALT		\
	  || G_SYS_SOC_TYPE(sysrev) == K_SYS_SOC_TYPE_BCM1250_ALT2)	\
	 ? K_SYS_SOC_TYPE_BCM1250 : G_SYS_SOC_TYPE(sysrev))
#endif

#define S_SYS_WID                   _SB_MAKE64(32)
#define M_SYS_WID                   _SB_MAKEMASK(32,S_SYS_WID)
#define V_SYS_WID(x)                _SB_MAKEVALUE(x,S_SYS_WID)
#define G_SYS_WID(x)                _SB_GETVALUE(x,S_SYS_WID,M_SYS_WID)

/*
 * System Manufacturing Register
 * Register: SCD_SYSTEM_MANUF
 */

#if SIBYTE_HDR_FEATURE_1250_112x
/* Wafer ID: bits 31:0 */
#define S_SYS_WAFERID1_200        _SB_MAKE64(0)
#define M_SYS_WAFERID1_200        _SB_MAKEMASK(32,S_SYS_WAFERID1_200)
#define V_SYS_WAFERID1_200(x)     _SB_MAKEVALUE(x,S_SYS_WAFERID1_200)
#define G_SYS_WAFERID1_200(x)     _SB_GETVALUE(x,S_SYS_WAFERID1_200,M_SYS_WAFERID1_200)

#define S_SYS_BIN                 _SB_MAKE64(32)
#define M_SYS_BIN                 _SB_MAKEMASK(4,S_SYS_BIN)
#define V_SYS_BIN(x)              _SB_MAKEVALUE(x,S_SYS_BIN)
#define G_SYS_BIN(x)              _SB_GETVALUE(x,S_SYS_BIN,M_SYS_BIN)

/* Wafer ID: bits 39:36 */
#define S_SYS_WAFERID2_200        _SB_MAKE64(36)
#define M_SYS_WAFERID2_200        _SB_MAKEMASK(4,S_SYS_WAFERID2_200)
#define V_SYS_WAFERID2_200(x)     _SB_MAKEVALUE(x,S_SYS_WAFERID2_200)
#define G_SYS_WAFERID2_200(x)     _SB_GETVALUE(x,S_SYS_WAFERID2_200,M_SYS_WAFERID2_200)

/* Wafer ID: bits 39:0 */
#define S_SYS_WAFERID_300         _SB_MAKE64(0)
#define M_SYS_WAFERID_300         _SB_MAKEMASK(40,S_SYS_WAFERID_300)
#define V_SYS_WAFERID_300(x)      _SB_MAKEVALUE(x,S_SYS_WAFERID_300)
#define G_SYS_WAFERID_300(x)      _SB_GETVALUE(x,S_SYS_WAFERID_300,M_SYS_WAFERID_300)

#define S_SYS_XPOS                _SB_MAKE64(40)
#define M_SYS_XPOS                _SB_MAKEMASK(6,S_SYS_XPOS)
#define V_SYS_XPOS(x)             _SB_MAKEVALUE(x,S_SYS_XPOS)
#define G_SYS_XPOS(x)             _SB_GETVALUE(x,S_SYS_XPOS,M_SYS_XPOS)

#define S_SYS_YPOS                _SB_MAKE64(46)
#define M_SYS_YPOS                _SB_MAKEMASK(6,S_SYS_YPOS)
#define V_SYS_YPOS(x)             _SB_MAKEVALUE(x,S_SYS_YPOS)
#define G_SYS_YPOS(x)             _SB_GETVALUE(x,S_SYS_YPOS,M_SYS_YPOS)
#endif

/*
 * System Config Register (Table 4-2)
 * Register: SCD_SYSTEM_CFG
 */

#if SIBYTE_HDR_FEATURE_1250_112x
#define M_SYS_LDT_PLL_BYP           _SB_MAKEMASK1(3)
#define M_SYS_PCI_SYNC_TEST_MODE    _SB_MAKEMASK1(4)
#define M_SYS_IOB0_DIV              _SB_MAKEMASK1(5)
#define M_SYS_IOB1_DIV              _SB_MAKEMASK1(6)

#define S_SYS_PLL_DIV               _SB_MAKE64(7)
#define M_SYS_PLL_DIV               _SB_MAKEMASK(5,S_SYS_PLL_DIV)
#define V_SYS_PLL_DIV(x)            _SB_MAKEVALUE(x,S_SYS_PLL_DIV)
#define G_SYS_PLL_DIV(x)            _SB_GETVALUE(x,S_SYS_PLL_DIV,M_SYS_PLL_DIV)

#define M_SYS_SER0_ENABLE           _SB_MAKEMASK1(12)
#define M_SYS_SER0_RSTB_EN          _SB_MAKEMASK1(13)
#define M_SYS_SER1_ENABLE           _SB_MAKEMASK1(14)
#define M_SYS_SER1_RSTB_EN          _SB_MAKEMASK1(15)
#define M_SYS_PCMCIA_ENABLE         _SB_MAKEMASK1(16)

#define S_SYS_BOOT_MODE             _SB_MAKE64(17)
#define M_SYS_BOOT_MODE             _SB_MAKEMASK(2,S_SYS_BOOT_MODE)
#define V_SYS_BOOT_MODE(x)          _SB_MAKEVALUE(x,S_SYS_BOOT_MODE)
#define G_SYS_BOOT_MODE(x)          _SB_GETVALUE(x,S_SYS_BOOT_MODE,M_SYS_BOOT_MODE)
#define K_SYS_BOOT_MODE_ROM32       0
#define K_SYS_BOOT_MODE_ROM8        1
#define K_SYS_BOOT_MODE_SMBUS_SMALL 2
#define K_SYS_BOOT_MODE_SMBUS_BIG   3

#define M_SYS_PCI_HOST              _SB_MAKEMASK1(19)
#define M_SYS_PCI_ARBITER           _SB_MAKEMASK1(20)
#define M_SYS_SOUTH_ON_LDT          _SB_MAKEMASK1(21)
#define M_SYS_BIG_ENDIAN            _SB_MAKEMASK1(22)
#define M_SYS_GENCLK_EN             _SB_MAKEMASK1(23)
#define M_SYS_LDT_TEST_EN           _SB_MAKEMASK1(24)
#define M_SYS_GEN_PARITY_EN         _SB_MAKEMASK1(25)

#define S_SYS_CONFIG                26
#define M_SYS_CONFIG                _SB_MAKEMASK(6,S_SYS_CONFIG)
#define V_SYS_CONFIG(x)             _SB_MAKEVALUE(x,S_SYS_CONFIG)
#define G_SYS_CONFIG(x)             _SB_GETVALUE(x,S_SYS_CONFIG,M_SYS_CONFIG)

/* The following bits are writeable by JTAG only. */

#define M_SYS_CLKSTOP               _SB_MAKEMASK1(32)
#define M_SYS_CLKSTEP               _SB_MAKEMASK1(33)

#define S_SYS_CLKCOUNT              34
#define M_SYS_CLKCOUNT              _SB_MAKEMASK(8,S_SYS_CLKCOUNT)
#define V_SYS_CLKCOUNT(x)           _SB_MAKEVALUE(x,S_SYS_CLKCOUNT)
#define G_SYS_CLKCOUNT(x)           _SB_GETVALUE(x,S_SYS_CLKCOUNT,M_SYS_CLKCOUNT)

#define M_SYS_PLL_BYPASS            _SB_MAKEMASK1(42)

#define S_SYS_PLL_IREF		    43
#define M_SYS_PLL_IREF		    _SB_MAKEMASK(2,S_SYS_PLL_IREF)

#define S_SYS_PLL_VCO		    45
#define M_SYS_PLL_VCO		    _SB_MAKEMASK(2,S_SYS_PLL_VCO)

#define S_SYS_PLL_VREG		    47
#define M_SYS_PLL_VREG		    _SB_MAKEMASK(2,S_SYS_PLL_VREG)

#define M_SYS_MEM_RESET             _SB_MAKEMASK1(49)
#define M_SYS_L2C_RESET             _SB_MAKEMASK1(50)
#define M_SYS_IO_RESET_0            _SB_MAKEMASK1(51)
#define M_SYS_IO_RESET_1            _SB_MAKEMASK1(52)
#define M_SYS_SCD_RESET             _SB_MAKEMASK1(53)

/* End of bits writable by JTAG only. */

#define M_SYS_CPU_RESET_0           _SB_MAKEMASK1(54)
#define M_SYS_CPU_RESET_1           _SB_MAKEMASK1(55)

#define M_SYS_UNICPU0               _SB_MAKEMASK1(56)
#define M_SYS_UNICPU1               _SB_MAKEMASK1(57)

#define M_SYS_SB_SOFTRES            _SB_MAKEMASK1(58)
#define M_SYS_EXT_RESET             _SB_MAKEMASK1(59)
#define M_SYS_SYSTEM_RESET          _SB_MAKEMASK1(60)

#define M_SYS_MISR_MODE             _SB_MAKEMASK1(61)
#define M_SYS_MISR_RESET            _SB_MAKEMASK1(62)

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_SYS_SW_FLAG		    _SB_MAKEMASK1(63)
#endif /* 1250 PASS2 || 112x PASS1 */

#endif


/*
 * Mailbox Registers (Table 4-3)
 * Registers: SCD_MBOX_CPU_x
 */

#define S_MBOX_INT_3                0
#define M_MBOX_INT_3                _SB_MAKEMASK(16,S_MBOX_INT_3)
#define S_MBOX_INT_2                16
#define M_MBOX_INT_2                _SB_MAKEMASK(16,S_MBOX_INT_2)
#define S_MBOX_INT_1                32
#define M_MBOX_INT_1                _SB_MAKEMASK(16,S_MBOX_INT_1)
#define S_MBOX_INT_0                48
#define M_MBOX_INT_0                _SB_MAKEMASK(16,S_MBOX_INT_0)

/*
 * Watchdog Registers (Table 4-8) (Table 4-9) (Table 4-10)
 * Registers: SCD_WDOG_INIT_CNT_x
 */

#define V_SCD_WDOG_FREQ             1000000

#define S_SCD_WDOG_INIT             0
#define M_SCD_WDOG_INIT             _SB_MAKEMASK(23,S_SCD_WDOG_INIT)

#define S_SCD_WDOG_CNT              0
#define M_SCD_WDOG_CNT              _SB_MAKEMASK(23,S_SCD_WDOG_CNT)

#define S_SCD_WDOG_ENABLE           0
#define M_SCD_WDOG_ENABLE           _SB_MAKEMASK1(S_SCD_WDOG_ENABLE)

#define S_SCD_WDOG_RESET_TYPE       2
#define M_SCD_WDOG_RESET_TYPE       _SB_MAKEMASK(3,S_SCD_WDOG_RESET_TYPE)
#define V_SCD_WDOG_RESET_TYPE(x)    _SB_MAKEVALUE(x,S_SCD_WDOG_RESET_TYPE)
#define G_SCD_WDOG_RESET_TYPE(x)    _SB_GETVALUE(x,S_SCD_WDOG_RESET_TYPE,M_SCD_WDOG_RESET_TYPE)

#define K_SCD_WDOG_RESET_FULL       0	/* actually, (x & 1) == 0  */
#define K_SCD_WDOG_RESET_SOFT       1
#define K_SCD_WDOG_RESET_CPU0       3
#define K_SCD_WDOG_RESET_CPU1       5
#define K_SCD_WDOG_RESET_BOTH_CPUS  7

/* This feature is present in 1250 C0 and later, but *not* in 112x A revs.  */
#if SIBYTE_HDR_FEATURE(1250, PASS3)
#define S_SCD_WDOG_HAS_RESET        8
#define M_SCD_WDOG_HAS_RESET        _SB_MAKEMASK1(S_SCD_WDOG_HAS_RESET)
#endif


/*
 * Timer Registers (Table 4-11) (Table 4-12) (Table 4-13)
 */

#define V_SCD_TIMER_FREQ            1000000
#define V_SCD_TIMER_WIDTH           23

#define S_SCD_TIMER_INIT            0
#define M_SCD_TIMER_INIT            _SB_MAKEMASK(V_SCD_TIMER_WIDTH,S_SCD_TIMER_INIT)
#define V_SCD_TIMER_INIT(x)         _SB_MAKEVALUE(x,S_SCD_TIMER_INIT)
#define G_SCD_TIMER_INIT(x)         _SB_GETVALUE(x,S_SCD_TIMER_INIT,M_SCD_TIMER_INIT)

#define S_SCD_TIMER_CNT             0
#define M_SCD_TIMER_CNT             _SB_MAKEMASK(V_SCD_TIMER_WIDTH,S_SCD_TIMER_CNT)
#define V_SCD_TIMER_CNT(x)         _SB_MAKEVALUE(x,S_SCD_TIMER_CNT)
#define G_SCD_TIMER_CNT(x)         _SB_GETVALUE(x,S_SCD_TIMER_CNT,M_SCD_TIMER_CNT)

#define M_SCD_TIMER_ENABLE          _SB_MAKEMASK1(0)
#define M_SCD_TIMER_MODE            _SB_MAKEMASK1(1)
#define M_SCD_TIMER_MODE_CONTINUOUS M_SCD_TIMER_MODE

/*
 * System Performance Counters
 */

#if SIBYTE_HDR_FEATURE_1250_112x
#define S_SPC_CFG_SRC0            0
#define M_SPC_CFG_SRC0            _SB_MAKEMASK(8,S_SPC_CFG_SRC0)
#define V_SPC_CFG_SRC0(x)         _SB_MAKEVALUE(x,S_SPC_CFG_SRC0)
#define G_SPC_CFG_SRC0(x)         _SB_GETVALUE(x,S_SPC_CFG_SRC0,M_SPC_CFG_SRC0)

#define S_SPC_CFG_SRC1            8
#define M_SPC_CFG_SRC1            _SB_MAKEMASK(8,S_SPC_CFG_SRC1)
#define V_SPC_CFG_SRC1(x)         _SB_MAKEVALUE(x,S_SPC_CFG_SRC1)
#define G_SPC_CFG_SRC1(x)         _SB_GETVALUE(x,S_SPC_CFG_SRC1,M_SPC_CFG_SRC1)

#define S_SPC_CFG_SRC2            16
#define M_SPC_CFG_SRC2            _SB_MAKEMASK(8,S_SPC_CFG_SRC2)
#define V_SPC_CFG_SRC2(x)         _SB_MAKEVALUE(x,S_SPC_CFG_SRC2)
#define G_SPC_CFG_SRC2(x)         _SB_GETVALUE(x,S_SPC_CFG_SRC2,M_SPC_CFG_SRC2)

#define S_SPC_CFG_SRC3            24
#define M_SPC_CFG_SRC3            _SB_MAKEMASK(8,S_SPC_CFG_SRC3)
#define V_SPC_CFG_SRC3(x)         _SB_MAKEVALUE(x,S_SPC_CFG_SRC3)
#define G_SPC_CFG_SRC3(x)         _SB_GETVALUE(x,S_SPC_CFG_SRC3,M_SPC_CFG_SRC3)

#define M_SPC_CFG_CLEAR		_SB_MAKEMASK1(32)
#define M_SPC_CFG_ENABLE	_SB_MAKEMASK1(33)
#endif


/*
 * Bus Watcher
 */

#define S_SCD_BERR_TID            8
#define M_SCD_BERR_TID            _SB_MAKEMASK(10,S_SCD_BERR_TID)
#define V_SCD_BERR_TID(x)         _SB_MAKEVALUE(x,S_SCD_BERR_TID)
#define G_SCD_BERR_TID(x)         _SB_GETVALUE(x,S_SCD_BERR_TID,M_SCD_BERR_TID)

#define S_SCD_BERR_RID            18
#define M_SCD_BERR_RID            _SB_MAKEMASK(4,S_SCD_BERR_RID)
#define V_SCD_BERR_RID(x)         _SB_MAKEVALUE(x,S_SCD_BERR_RID)
#define G_SCD_BERR_RID(x)         _SB_GETVALUE(x,S_SCD_BERR_RID,M_SCD_BERR_RID)

#define S_SCD_BERR_DCODE          22
#define M_SCD_BERR_DCODE          _SB_MAKEMASK(3,S_SCD_BERR_DCODE)
#define V_SCD_BERR_DCODE(x)       _SB_MAKEVALUE(x,S_SCD_BERR_DCODE)
#define G_SCD_BERR_DCODE(x)       _SB_GETVALUE(x,S_SCD_BERR_DCODE,M_SCD_BERR_DCODE)

#define M_SCD_BERR_MULTERRS       _SB_MAKEMASK1(30)


#define S_SCD_L2ECC_CORR_D        0
#define M_SCD_L2ECC_CORR_D        _SB_MAKEMASK(8,S_SCD_L2ECC_CORR_D)
#define V_SCD_L2ECC_CORR_D(x)     _SB_MAKEVALUE(x,S_SCD_L2ECC_CORR_D)
#define G_SCD_L2ECC_CORR_D(x)     _SB_GETVALUE(x,S_SCD_L2ECC_CORR_D,M_SCD_L2ECC_CORR_D)

#define S_SCD_L2ECC_BAD_D         8
#define M_SCD_L2ECC_BAD_D         _SB_MAKEMASK(8,S_SCD_L2ECC_BAD_D)
#define V_SCD_L2ECC_BAD_D(x)      _SB_MAKEVALUE(x,S_SCD_L2ECC_BAD_D)
#define G_SCD_L2ECC_BAD_D(x)      _SB_GETVALUE(x,S_SCD_L2ECC_BAD_D,M_SCD_L2ECC_BAD_D)

#define S_SCD_L2ECC_CORR_T        16
#define M_SCD_L2ECC_CORR_T        _SB_MAKEMASK(8,S_SCD_L2ECC_CORR_T)
#define V_SCD_L2ECC_CORR_T(x)     _SB_MAKEVALUE(x,S_SCD_L2ECC_CORR_T)
#define G_SCD_L2ECC_CORR_T(x)     _SB_GETVALUE(x,S_SCD_L2ECC_CORR_T,M_SCD_L2ECC_CORR_T)

#define S_SCD_L2ECC_BAD_T         24
#define M_SCD_L2ECC_BAD_T         _SB_MAKEMASK(8,S_SCD_L2ECC_BAD_T)
#define V_SCD_L2ECC_BAD_T(x)      _SB_MAKEVALUE(x,S_SCD_L2ECC_BAD_T)
#define G_SCD_L2ECC_BAD_T(x)      _SB_GETVALUE(x,S_SCD_L2ECC_BAD_T,M_SCD_L2ECC_BAD_T)

#define S_SCD_MEM_ECC_CORR        0
#define M_SCD_MEM_ECC_CORR        _SB_MAKEMASK(8,S_SCD_MEM_ECC_CORR)
#define V_SCD_MEM_ECC_CORR(x)     _SB_MAKEVALUE(x,S_SCD_MEM_ECC_CORR)
#define G_SCD_MEM_ECC_CORR(x)     _SB_GETVALUE(x,S_SCD_MEM_ECC_CORR,M_SCD_MEM_ECC_CORR)

#define S_SCD_MEM_ECC_BAD         8
#define M_SCD_MEM_ECC_BAD         _SB_MAKEMASK(8,S_SCD_MEM_ECC_BAD)
#define V_SCD_MEM_ECC_BAD(x)      _SB_MAKEVALUE(x,S_SCD_MEM_ECC_BAD)
#define G_SCD_MEM_ECC_BAD(x)      _SB_GETVALUE(x,S_SCD_MEM_ECC_BAD,M_SCD_MEM_ECC_BAD)

#define S_SCD_MEM_BUSERR          16
#define M_SCD_MEM_BUSERR          _SB_MAKEMASK(8,S_SCD_MEM_BUSERR)
#define V_SCD_MEM_BUSERR(x)       _SB_MAKEVALUE(x,S_SCD_MEM_BUSERR)
#define G_SCD_MEM_BUSERR(x)       _SB_GETVALUE(x,S_SCD_MEM_BUSERR,M_SCD_MEM_BUSERR)


/*
 * Address Trap Registers
 */

#if SIBYTE_HDR_FEATURE_1250_112x
#define M_ATRAP_INDEX		  _SB_MAKEMASK(4,0)
#define M_ATRAP_ADDRESS		  _SB_MAKEMASK(40,0)

#define S_ATRAP_CFG_CNT            0
#define M_ATRAP_CFG_CNT            _SB_MAKEMASK(3,S_ATRAP_CFG_CNT)
#define V_ATRAP_CFG_CNT(x)         _SB_MAKEVALUE(x,S_ATRAP_CFG_CNT)
#define G_ATRAP_CFG_CNT(x)         _SB_GETVALUE(x,S_ATRAP_CFG_CNT,M_ATRAP_CFG_CNT)

#define M_ATRAP_CFG_WRITE	   _SB_MAKEMASK1(3)
#define M_ATRAP_CFG_ALL	  	   _SB_MAKEMASK1(4)
#define M_ATRAP_CFG_INV	   	   _SB_MAKEMASK1(5)
#define M_ATRAP_CFG_USESRC	   _SB_MAKEMASK1(6)
#define M_ATRAP_CFG_SRCINV	   _SB_MAKEMASK1(7)

#define S_ATRAP_CFG_AGENTID     8
#define M_ATRAP_CFG_AGENTID     _SB_MAKEMASK(4,S_ATRAP_CFG_AGENTID)
#define V_ATRAP_CFG_AGENTID(x)  _SB_MAKEVALUE(x,S_ATRAP_CFG_AGENTID)
#define G_ATRAP_CFG_AGENTID(x)  _SB_GETVALUE(x,S_ATRAP_CFG_AGENTID,M_ATRAP_CFG_AGENTID)

#define K_BUS_AGENT_CPU0	0
#define K_BUS_AGENT_CPU1	1
#define K_BUS_AGENT_IOB0	2
#define K_BUS_AGENT_IOB1	3
#define K_BUS_AGENT_SCD	4
#define K_BUS_AGENT_L2C	6
#define K_BUS_AGENT_MC	7

#define S_ATRAP_CFG_CATTR     12
#define M_ATRAP_CFG_CATTR     _SB_MAKEMASK(3,S_ATRAP_CFG_CATTR)
#define V_ATRAP_CFG_CATTR(x)  _SB_MAKEVALUE(x,S_ATRAP_CFG_CATTR)
#define G_ATRAP_CFG_CATTR(x)  _SB_GETVALUE(x,S_ATRAP_CFG_CATTR,M_ATRAP_CFG_CATTR)

#define K_ATRAP_CFG_CATTR_IGNORE	0
#define K_ATRAP_CFG_CATTR_UNC    	1
#define K_ATRAP_CFG_CATTR_CACHEABLE	2
#define K_ATRAP_CFG_CATTR_NONCOH  	3
#define K_ATRAP_CFG_CATTR_COHERENT	4
#define K_ATRAP_CFG_CATTR_NOTUNC	5
#define K_ATRAP_CFG_CATTR_NOTNONCOH	6
#define K_ATRAP_CFG_CATTR_NOTCOHERENT   7

#endif	/* 1250/112x */

/*
 * Trace Buffer Config register
 */

#if SIBYTE_HDR_FEATURE_1250_112x

#define M_SCD_TRACE_CFG_RESET           _SB_MAKEMASK1(0)
#define M_SCD_TRACE_CFG_START_READ      _SB_MAKEMASK1(1)
#define M_SCD_TRACE_CFG_START           _SB_MAKEMASK1(2)
#define M_SCD_TRACE_CFG_STOP            _SB_MAKEMASK1(3)
#define M_SCD_TRACE_CFG_FREEZE          _SB_MAKEMASK1(4)
#define M_SCD_TRACE_CFG_FREEZE_FULL     _SB_MAKEMASK1(5)
#define M_SCD_TRACE_CFG_DEBUG_FULL      _SB_MAKEMASK1(6)
#define M_SCD_TRACE_CFG_FULL            _SB_MAKEMASK1(7)
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_SCD_TRACE_CFG_FORCECNT        _SB_MAKEMASK1(8)
#endif /* 1250 PASS2 || 112x PASS1 */

#define S_SCD_TRACE_CFG_CUR_ADDR        10
#define M_SCD_TRACE_CFG_CUR_ADDR        _SB_MAKEMASK(8,S_SCD_TRACE_CFG_CUR_ADDR)
#define V_SCD_TRACE_CFG_CUR_ADDR(x)     _SB_MAKEVALUE(x,S_SCD_TRACE_CFG_CUR_ADDR)
#define G_SCD_TRACE_CFG_CUR_ADDR(x)     _SB_GETVALUE(x,S_SCD_TRACE_CFG_CUR_ADDR,M_SCD_TRACE_CFG_CUR_ADDR)

#endif	/* 1250/112x */

/*
 * Trace Event registers
 */

#define S_SCD_TREVT_ADDR_MATCH          0
#define M_SCD_TREVT_ADDR_MATCH          _SB_MAKEMASK(4,S_SCD_TREVT_ADDR_MATCH)
#define V_SCD_TREVT_ADDR_MATCH(x)       _SB_MAKEVALUE(x,S_SCD_TREVT_ADDR_MATCH)
#define G_SCD_TREVT_ADDR_MATCH(x)       _SB_GETVALUE(x,S_SCD_TREVT_ADDR_MATCH,M_SCD_TREVT_ADDR_MATCH)

#define M_SCD_TREVT_REQID_MATCH         _SB_MAKEMASK1(4)
#define M_SCD_TREVT_DATAID_MATCH        _SB_MAKEMASK1(5)
#define M_SCD_TREVT_RESPID_MATCH        _SB_MAKEMASK1(6)
#define M_SCD_TREVT_INTERRUPT           _SB_MAKEMASK1(7)
#define M_SCD_TREVT_DEBUG_PIN           _SB_MAKEMASK1(9)
#define M_SCD_TREVT_WRITE               _SB_MAKEMASK1(10)
#define M_SCD_TREVT_READ                _SB_MAKEMASK1(11)

#define S_SCD_TREVT_REQID               12
#define M_SCD_TREVT_REQID               _SB_MAKEMASK(4,S_SCD_TREVT_REQID)
#define V_SCD_TREVT_REQID(x)            _SB_MAKEVALUE(x,S_SCD_TREVT_REQID)
#define G_SCD_TREVT_REQID(x)            _SB_GETVALUE(x,S_SCD_TREVT_REQID,M_SCD_TREVT_REQID)

#define S_SCD_TREVT_RESPID              16
#define M_SCD_TREVT_RESPID              _SB_MAKEMASK(4,S_SCD_TREVT_RESPID)
#define V_SCD_TREVT_RESPID(x)           _SB_MAKEVALUE(x,S_SCD_TREVT_RESPID)
#define G_SCD_TREVT_RESPID(x)           _SB_GETVALUE(x,S_SCD_TREVT_RESPID,M_SCD_TREVT_RESPID)

#define S_SCD_TREVT_DATAID              20
#define M_SCD_TREVT_DATAID              _SB_MAKEMASK(4,S_SCD_TREVT_DATAID)
#define V_SCD_TREVT_DATAID(x)           _SB_MAKEVALUE(x,S_SCD_TREVT_DATAID)
#define G_SCD_TREVT_DATAID(x)           _SB_GETVALUE(x,S_SCD_TREVT_DATAID,M_SCD_TREVT_DATID)

#define S_SCD_TREVT_COUNT               24
#define M_SCD_TREVT_COUNT               _SB_MAKEMASK(8,S_SCD_TREVT_COUNT)
#define V_SCD_TREVT_COUNT(x)            _SB_MAKEVALUE(x,S_SCD_TREVT_COUNT)
#define G_SCD_TREVT_COUNT(x)            _SB_GETVALUE(x,S_SCD_TREVT_COUNT,M_SCD_TREVT_COUNT)

/*
 * Trace Sequence registers
 */

#define S_SCD_TRSEQ_EVENT4              0
#define M_SCD_TRSEQ_EVENT4              _SB_MAKEMASK(4,S_SCD_TRSEQ_EVENT4)
#define V_SCD_TRSEQ_EVENT4(x)           _SB_MAKEVALUE(x,S_SCD_TRSEQ_EVENT4)
#define G_SCD_TRSEQ_EVENT4(x)           _SB_GETVALUE(x,S_SCD_TRSEQ_EVENT4,M_SCD_TRSEQ_EVENT4)

#define S_SCD_TRSEQ_EVENT3              4
#define M_SCD_TRSEQ_EVENT3              _SB_MAKEMASK(4,S_SCD_TRSEQ_EVENT3)
#define V_SCD_TRSEQ_EVENT3(x)           _SB_MAKEVALUE(x,S_SCD_TRSEQ_EVENT3)
#define G_SCD_TRSEQ_EVENT3(x)           _SB_GETVALUE(x,S_SCD_TRSEQ_EVENT3,M_SCD_TRSEQ_EVENT3)

#define S_SCD_TRSEQ_EVENT2              8
#define M_SCD_TRSEQ_EVENT2              _SB_MAKEMASK(4,S_SCD_TRSEQ_EVENT2)
#define V_SCD_TRSEQ_EVENT2(x)           _SB_MAKEVALUE(x,S_SCD_TRSEQ_EVENT2)
#define G_SCD_TRSEQ_EVENT2(x)           _SB_GETVALUE(x,S_SCD_TRSEQ_EVENT2,M_SCD_TRSEQ_EVENT2)

#define S_SCD_TRSEQ_EVENT1              12
#define M_SCD_TRSEQ_EVENT1              _SB_MAKEMASK(4,S_SCD_TRSEQ_EVENT1)
#define V_SCD_TRSEQ_EVENT1(x)           _SB_MAKEVALUE(x,S_SCD_TRSEQ_EVENT1)
#define G_SCD_TRSEQ_EVENT1(x)           _SB_GETVALUE(x,S_SCD_TRSEQ_EVENT1,M_SCD_TRSEQ_EVENT1)

#define K_SCD_TRSEQ_E0                  0
#define K_SCD_TRSEQ_E1                  1
#define K_SCD_TRSEQ_E2                  2
#define K_SCD_TRSEQ_E3                  3
#define K_SCD_TRSEQ_E0_E1               4
#define K_SCD_TRSEQ_E1_E2               5
#define K_SCD_TRSEQ_E2_E3               6
#define K_SCD_TRSEQ_E0_E1_E2            7
#define K_SCD_TRSEQ_E0_E1_E2_E3         8
#define K_SCD_TRSEQ_E0E1                9
#define K_SCD_TRSEQ_E0E1E2              10
#define K_SCD_TRSEQ_E0E1E2E3            11
#define K_SCD_TRSEQ_E0E1_E2             12
#define K_SCD_TRSEQ_E0E1_E2E3           13
#define K_SCD_TRSEQ_E0E1_E2_E3          14
#define K_SCD_TRSEQ_IGNORED             15

#define K_SCD_TRSEQ_TRIGGER_ALL         (V_SCD_TRSEQ_EVENT1(K_SCD_TRSEQ_IGNORED) | \
                                         V_SCD_TRSEQ_EVENT2(K_SCD_TRSEQ_IGNORED) | \
                                         V_SCD_TRSEQ_EVENT3(K_SCD_TRSEQ_IGNORED) | \
                                         V_SCD_TRSEQ_EVENT4(K_SCD_TRSEQ_IGNORED))

#define S_SCD_TRSEQ_FUNCTION            16
#define M_SCD_TRSEQ_FUNCTION            _SB_MAKEMASK(4,S_SCD_TRSEQ_FUNCTION)
#define V_SCD_TRSEQ_FUNCTION(x)         _SB_MAKEVALUE(x,S_SCD_TRSEQ_FUNCTION)
#define G_SCD_TRSEQ_FUNCTION(x)         _SB_GETVALUE(x,S_SCD_TRSEQ_FUNCTION,M_SCD_TRSEQ_FUNCTION)

#define K_SCD_TRSEQ_FUNC_NOP            0
#define K_SCD_TRSEQ_FUNC_START          1
#define K_SCD_TRSEQ_FUNC_STOP           2
#define K_SCD_TRSEQ_FUNC_FREEZE         3

#define V_SCD_TRSEQ_FUNC_NOP            V_SCD_TRSEQ_FUNCTION(K_SCD_TRSEQ_FUNC_NOP)
#define V_SCD_TRSEQ_FUNC_START          V_SCD_TRSEQ_FUNCTION(K_SCD_TRSEQ_FUNC_START)
#define V_SCD_TRSEQ_FUNC_STOP           V_SCD_TRSEQ_FUNCTION(K_SCD_TRSEQ_FUNC_STOP)
#define V_SCD_TRSEQ_FUNC_FREEZE         V_SCD_TRSEQ_FUNCTION(K_SCD_TRSEQ_FUNC_FREEZE)

#define M_SCD_TRSEQ_ASAMPLE             _SB_MAKEMASK1(18)
#define M_SCD_TRSEQ_DSAMPLE             _SB_MAKEMASK1(19)
#define M_SCD_TRSEQ_DEBUGPIN            _SB_MAKEMASK1(20)
#define M_SCD_TRSEQ_DEBUGCPU            _SB_MAKEMASK1(21)
#define M_SCD_TRSEQ_CLEARUSE            _SB_MAKEMASK1(22)
#define M_SCD_TRSEQ_ALLD_A              _SB_MAKEMASK1(23)
#define M_SCD_TRSEQ_ALL_A               _SB_MAKEMASK1(24)

#endif
