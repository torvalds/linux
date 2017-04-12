/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This file is dual-licensed: you can use it either under the terms
 * of the GPL or the X11 license, at your option. Note that this dual
 * licensing only applies to this file, and not this project as a
 * whole.
 *
 *  a) This library is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of the
 *     License, or (at your option) any later version.
 *
 *     This library is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 * Or, alternatively,
 *
 *  b) Permission is hereby granted, free of charge, to any person
 *     obtaining a copy of this software and associated documentation
 *     files (the "Software"), to deal in the Software without
 *     restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or
 *     sell copies of the Software, and to permit persons to whom the
 *     Software is furnished to do so, subject to the following
 *     conditions:
 *
 *     The above copyright notice and this permission notice shall be
 *     included in all copies or substantial portions of the Software.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *     EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *     OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *     NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *     HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *     WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *     OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DT_BINDINGS_DRAM_ROCKCHIP_RK3288_H
#define _DT_BINDINGS_DRAM_ROCKCHIP_RK3288_H

#define DDR3_DS_34ohm			(34)
#define DDR3_DS_40ohm			(40)

#define DDR3_ODT_DIS			(0)
#define DDR3_ODT_40ohm			(40)
#define DDR3_ODT_60ohm			(60)
#define DDR3_ODT_120ohm			(120)

#define LP2_DS_34ohm			(34)
#define LP2_DS_40ohm			(40)
#define LP2_DS_48ohm			(48)
#define LP2_DS_60ohm			(60)
#define LP2_DS_68_6ohm			(68)	/* optional */
#define LP2_DS_80ohm			(80)
#define LP2_DS_120ohm			(120)	/* optional */

#define LP3_DS_34ohm			(34)
#define LP3_DS_40ohm			(40)
#define LP3_DS_48ohm			(48)
#define LP3_DS_60ohm			(60)
#define LP3_DS_80ohm			(80)
#define LP3_DS_34D_40U			(3440)
#define LP3_DS_40D_48U			(4048)
#define LP3_DS_34D_48U			(3448)

#define LP3_ODT_DIS			(0)
#define LP3_ODT_60ohm			(60)
#define LP3_ODT_120ohm			(120)
#define LP3_ODT_240ohm			(240)

/* PHY DRV ODT strength*/
#define PHY_DDR3_RON_114ohm		(7)
#define PHY_DDR3_RON_95ohm		(4)
#define PHY_DDR3_RON_81ohm		(5)
#define PHY_DDR3_RON_71ohm		(0xc)
#define PHY_DDR3_RON_63ohm		(0xd)
#define PHY_DDR3_RON_57ohm		(0xe)
#define PHY_DDR3_RON_52ohm		(0xf)
#define PHY_DDR3_RON_47ohm		(0xa)
#define PHY_DDR3_RON_44ohm		(0xb)
#define PHY_DDR3_RON_41ohm		(0x8)
#define PHY_DDR3_RON_38ohm		(0x9)
#define PHY_DDR3_RON_34ohm		(0x19)
#define PHY_DDR3_RON_30ohm		(0x1b)
#define PHY_DDR3_RON_26ohm		(0x1c)
#define PHY_DDR3_RON_23ohm		(0x15)
#define PHY_DDR3_RON_20ohm		(0x12)
#define PHY_DDR3_RON_18ohm		(0x11)

#define PHY_DDR3_RTT_368ohm		(0x1)
#define PHY_DDR3_RTT_155ohm		(0x2)
#define PHY_DDR3_RTT_113ohm		(0x3)
#define PHY_DDR3_RTT_80ohm		(0x6)
#define PHY_DDR3_RTT_64ohm		(0x7)
#define PHY_DDR3_RTT_54ohm		(0x4)
#define PHY_DDR3_RTT_40ohm		(0xc)
#define PHY_DDR3_RTT_30ohm		(0xf)

#define PHY_LP23_RON_110ohm		(4)
#define PHY_LP23_RON_83ohm		(0xc)
#define PHY_LP23_RON_73ohm		(0xd)
#define PHY_LP23_RON_66ohm		(0xe)
#define PHY_LP23_RON_60ohm		(0xf)
#define PHY_LP23_RON_55ohm		(0xa)
#define PHY_LP23_RON_51ohm		(0xb)
#define PHY_LP23_RON_44ohm		(0x9)
#define PHY_LP23_RON_39ohm		(0x19)
#define PHY_LP23_RON_35ohm		(0x1b)
#define PHY_LP23_RON_30ohm		(0x1c)
#define PHY_LP23_RON_26ohm		(0x16)
#define PHY_LP23_RON_22ohm		(0x10)

#define PHY_LP23_RTT_368ohm		(0x1)
#define PHY_LP23_RTT_155ohm		(0x2)
#define PHY_LP23_RTT_113ohm		(0x3)
#define PHY_LP23_RTT_80ohm		(0x6)
#define PHY_LP23_RTT_64ohm		(0x7)
#define PHY_LP23_RTT_54ohm		(0x4)
#define PHY_LP23_RTT_40ohm		(0xc)
#define PHY_LP23_RTT_30ohm		(0xf)

#endif /*_DT_BINDINGS_DRAM_ROCKCHIP_RK3288_H*/
