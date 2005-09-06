/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  L2 Cache constants and macros		File: sb1250_l2c.h
    *
    *  This module contains constants useful for manipulating the
    *  level 2 cache.
    *
    *  SB1250 specification level:  User's manual 1/02/02
    *
    *  Author:  Mitch Lichtenberg
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


#ifndef _SB1250_L2C_H
#define _SB1250_L2C_H

#include "sb1250_defs.h"

/*
 * Level 2 Cache Tag register (Table 5-3)
 */

#define S_L2C_TAG_MBZ               0
#define M_L2C_TAG_MBZ               _SB_MAKEMASK(5,S_L2C_TAG_MBZ)

#define S_L2C_TAG_INDEX             5
#define M_L2C_TAG_INDEX             _SB_MAKEMASK(12,S_L2C_TAG_INDEX)
#define V_L2C_TAG_INDEX(x)          _SB_MAKEVALUE(x,S_L2C_TAG_INDEX)
#define G_L2C_TAG_INDEX(x)          _SB_GETVALUE(x,S_L2C_TAG_INDEX,M_L2C_TAG_INDEX)

#define S_L2C_TAG_TAG               17
#define M_L2C_TAG_TAG               _SB_MAKEMASK(23,S_L2C_TAG_TAG)
#define V_L2C_TAG_TAG(x)            _SB_MAKEVALUE(x,S_L2C_TAG_TAG)
#define G_L2C_TAG_TAG(x)            _SB_GETVALUE(x,S_L2C_TAG_TAG,M_L2C_TAG_TAG)

#define S_L2C_TAG_ECC               40
#define M_L2C_TAG_ECC               _SB_MAKEMASK(6,S_L2C_TAG_ECC)
#define V_L2C_TAG_ECC(x)            _SB_MAKEVALUE(x,S_L2C_TAG_ECC)
#define G_L2C_TAG_ECC(x)            _SB_GETVALUE(x,S_L2C_TAG_ECC,M_L2C_TAG_ECC)

#define S_L2C_TAG_WAY               46
#define M_L2C_TAG_WAY               _SB_MAKEMASK(2,S_L2C_TAG_WAY)
#define V_L2C_TAG_WAY(x)            _SB_MAKEVALUE(x,S_L2C_TAG_WAY)
#define G_L2C_TAG_WAY(x)            _SB_GETVALUE(x,S_L2C_TAG_WAY,M_L2C_TAG_WAY)

#define M_L2C_TAG_DIRTY             _SB_MAKEMASK1(48)
#define M_L2C_TAG_VALID             _SB_MAKEMASK1(49)

/*
 * Format of level 2 cache management address (table 5-2)
 */

#define S_L2C_MGMT_INDEX            5
#define M_L2C_MGMT_INDEX            _SB_MAKEMASK(12,S_L2C_MGMT_INDEX)
#define V_L2C_MGMT_INDEX(x)         _SB_MAKEVALUE(x,S_L2C_MGMT_INDEX)
#define G_L2C_MGMT_INDEX(x)         _SB_GETVALUE(x,S_L2C_MGMT_INDEX,M_L2C_MGMT_INDEX)

#define S_L2C_MGMT_QUADRANT         15
#define M_L2C_MGMT_QUADRANT         _SB_MAKEMASK(2,S_L2C_MGMT_QUADRANT)
#define V_L2C_MGMT_QUADRANT(x)      _SB_MAKEVALUE(x,S_L2C_MGMT_QUADRANT)
#define G_L2C_MGMT_QUADRANT(x)      _SB_GETVALUE(x,S_L2C_MGMT_QUADRANT,M_L2C_MGMT_QUADRANT)

#define S_L2C_MGMT_HALF		    16
#define M_L2C_MGMT_HALF	            _SB_MAKEMASK(1,S_L2C_MGMT_HALF)

#define S_L2C_MGMT_WAY              17
#define M_L2C_MGMT_WAY              _SB_MAKEMASK(2,S_L2C_MGMT_WAY)
#define V_L2C_MGMT_WAY(x)           _SB_MAKEVALUE(x,S_L2C_MGMT_WAY)
#define G_L2C_MGMT_WAY(x)           _SB_GETVALUE(x,S_L2C_MGMT_WAY,M_L2C_MGMT_WAY)

#define S_L2C_MGMT_TAG              21
#define M_L2C_MGMT_TAG              _SB_MAKEMASK(6,S_L2C_MGMT_TAG)
#define V_L2C_MGMT_TAG(x)           _SB_MAKEVALUE(x,S_L2C_MGMT_TAG)
#define G_L2C_MGMT_TAG(x)           _SB_GETVALUE(x,S_L2C_MGMT_TAG,M_L2C_MGMT_TAG)

#define M_L2C_MGMT_DIRTY            _SB_MAKEMASK1(19)
#define M_L2C_MGMT_VALID            _SB_MAKEMASK1(20)

#define A_L2C_MGMT_TAG_BASE         0x00D0000000

#define L2C_ENTRIES_PER_WAY       4096
#define L2C_NUM_WAYS              4


#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
/*
 * L2 Read Misc. register (A_L2_READ_MISC)
 */
#define S_L2C_MISC_NO_WAY		10
#define M_L2C_MISC_NO_WAY		_SB_MAKEMASK(4,S_L2C_MISC_NO_WAY)
#define V_L2C_MISC_NO_WAY(x)		_SB_MAKEVALUE(x,S_L2C_MISC_NO_WAY)
#define G_L2C_MISC_NO_WAY(x)		_SB_GETVALUE(x,S_L2C_MISC_NO_WAY,M_L2C_MISC_NO_WAY)

#define M_L2C_MISC_ECC_CLEANUP_DIS	_SB_MAKEMASK1(9)
#define M_L2C_MISC_MC_PRIO_LOW		_SB_MAKEMASK1(8)
#define M_L2C_MISC_SOFT_DISABLE_T	_SB_MAKEMASK1(7)
#define M_L2C_MISC_SOFT_DISABLE_B	_SB_MAKEMASK1(6)
#define M_L2C_MISC_SOFT_DISABLE_R	_SB_MAKEMASK1(5)
#define M_L2C_MISC_SOFT_DISABLE_L	_SB_MAKEMASK1(4)
#define M_L2C_MISC_SCACHE_DISABLE_T	_SB_MAKEMASK1(3)
#define M_L2C_MISC_SCACHE_DISABLE_B	_SB_MAKEMASK1(2)
#define M_L2C_MISC_SCACHE_DISABLE_R	_SB_MAKEMASK1(1)
#define M_L2C_MISC_SCACHE_DISABLE_L	_SB_MAKEMASK1(0)
#endif /* 1250 PASS3 || 112x PASS1 */


#endif
