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

#ifndef _DT_BINDINGS_DRAM_ROCKCHIP_RK3128_H
#define _DT_BINDINGS_DRAM_ROCKCHIP_RK3128_H

#define BIT(nr)			(1UL << (nr))

#define DDR3_DS_34ohm		BIT(1)
#define DDR3_DS_40ohm		(0x0)

#define LP2_DS_34ohm		(0x1)
#define LP2_DS_40ohm		(0x2)
#define LP2_DS_48ohm		(0x3)
#define LP2_DS_60ohm		(0x4)
#define LP2_DS_68_6ohm		(0x5)	/* optional */
#define LP2_DS_80ohm		(0x6)
#define LP2_DS_120ohm		(0x7)	/* optional */

#define DDR3_ODT_DIS		(0)
#define DDR3_ODT_40ohm		(BIT(2) | BIT(6))
#define DDR3_ODT_60ohm		BIT(2)
#define DDR3_ODT_120ohm		BIT(6)

#define PHY_RON_DISABLE		(0)
#define PHY_RON_309ohm		(1)
#define PHY_RON_155ohm		(2)
#define PHY_RON_103ohm		(3)
#define PHY_RON_77ohm		(4)
#define PHY_RON_63ohm		(5)
#define PHY_RON_52ohm		(6)
#define PHY_RON_45ohm		(7)
#define PHY_RON_62ohm		(9)
#define PHY_RON_44ohm		(11)
#define PHY_RON_39ohm		(12)
#define PHY_RON_34ohm		(13)
#define PHY_RON_31ohm		(14)
#define PHY_RON_28ohm		(15)

#define PHY_RTT_DISABLE		(0)
#define PHY_RTT_816ohm		(1)
#define PHY_RTT_431ohm		(2)
#define PHY_RTT_287ohm		(3)
#define PHY_RTT_216ohm		(4)
#define PHY_RTT_172ohm		(5)
#define PHY_RTT_145ohm		(6)
#define PHY_RTT_124ohm		(7)
#define PHY_RTT_215ohm		(8)
#define PHY_RTT_144ohm		(10)
#define PHY_RTT_123ohm		(11)
#define PHY_RTT_108ohm		(12)
#define PHY_RTT_96ohm		(13)
#define PHY_RTT_86ohm		(14)
#define PHY_RTT_78ohm		(15)

#endif /* _DT_BINDINGS_DRAM_ROCKCHIP_RK3128_H */
