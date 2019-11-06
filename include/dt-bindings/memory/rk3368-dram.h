/* Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
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

#ifndef _DT_BINDINGS_DRAM_ROCKCHIP_RK3368_H
#define _DT_BINDINGS_DRAM_ROCKCHIP_RK3368_H

#define DDR3_DS_34ohm		(0x2)
#define DDR3_DS_40ohm		(0x0)

#define LP2_DS_34ohm		(0x1)
#define LP2_DS_40ohm		(0x2)
#define LP2_DS_48ohm		(0x3)
#define LP2_DS_60ohm		(0x4)
#define LP2_DS_68_6ohm		(0x5)/* optional */
#define LP2_DS_80ohm		(0x6)
#define LP2_DS_120ohm		(0x7)/* optional */

#define LP3_DS_34ohm		(0x1)
#define LP3_DS_40ohm		(0x2)
#define LP3_DS_48ohm		(0x3)
#define LP3_DS_60ohm		(0x4)
#define LP3_DS_80ohm		(0x6)
#define LP3_DS_34D_40U		(0x9)
#define LP3_DS_40D_48U		(0xa)
#define LP3_DS_34D_48U		(0xb)

#define DDR3_ODT_DIS		(0)
#define DDR3_ODT_40ohm		(0x44)
#define DDR3_ODT_60ohm		(0x4)
#define DDR3_ODT_120ohm		(0x40)

#define LP3_ODT_DIS		(0)
#define LP3_ODT_60ohm		(1)
#define LP3_ODT_120ohm		(2)
#define LP3_ODT_240ohm		(3)

#define PHY_RON_DISABLE		(0)
#define PHY_RON_272ohm		(1)
#define PHY_RON_135ohm		(2)
#define PHY_RON_91ohm		(3)
#define PHY_RON_38ohm		(7)
#define PHY_RON_68ohm		(8)
#define PHY_RON_54ohm		(9)
#define PHY_RON_45ohm		(10)
#define PHY_RON_39ohm		(11)
#define PHY_RON_34ohm		(12)
#define PHY_RON_30ohm		(13)
#define PHY_RON_27ohm		(14)
#define PHY_RON_25ohm		(15)

#define PHY_RTT_DISABLE		(0)
#define PHY_RTT_1116ohm		(1)
#define PHY_RTT_558ohm		(2)
#define PHY_RTT_372ohm		(3)
#define PHY_RTT_279ohm		(4)
#define PHY_RTT_223ohm		(5)
#define PHY_RTT_186ohm		(6)
#define PHY_RTT_159ohm		(7)
#define PHY_RTT_139ohm		(8)
#define PHY_RTT_124ohm		(9)
#define PHY_RTT_112ohm		(10)
#define PHY_RTT_101ohm		(11)
#define PHY_RTT_93ohm		(12)
#define PHY_RTT_86ohm		(13)
#define PHY_RTT_80ohm		(14)
#define PHY_RTT_74ohm		(15)

#define ENABLE_DDR_2T		(1)
#define DISABLE_DDR_2T		(0)

#endif /*_DT_BINDINGS_DRAM_ROCKCHIP_RK3368_H*/
