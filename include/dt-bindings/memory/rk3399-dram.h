/* Copyright (c) 2016 Fuzhou Rockchip Electronics Co., Ltd
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

#ifndef _DT_BINDINGS_DRAM_ROCKCHIP_RK3399_H
#define _DT_BINDINGS_DRAM_ROCKCHIP_RK3399_H

#define DDR3_DS_34ohm		(34)
#define DDR3_DS_40ohm		(40)

#define DDR3_ODT_DIS		(0)
#define DDR3_ODT_40ohm		(40)
#define DDR3_ODT_60ohm		(60)
#define DDR3_ODT_120ohm		(120)

#define LP2_DS_34ohm		(34)
#define LP2_DS_40ohm		(40)
#define LP2_DS_48ohm		(48)
#define LP2_DS_60ohm		(60)
#define LP2_DS_68_6ohm		(68)	/* optional */
#define LP2_DS_80ohm		(80)
#define LP2_DS_120ohm		(120)	/* optional */

#define LP3_DS_34ohm		(34)
#define LP3_DS_40ohm		(40)
#define LP3_DS_48ohm		(48)
#define LP3_DS_60ohm		(60)
#define LP3_DS_80ohm		(80)
#define LP3_DS_34D_40U		(3440)
#define LP3_DS_40D_48U		(4048)
#define LP3_DS_34D_48U		(3448)

#define LP3_ODT_DIS		(0)
#define LP3_ODT_60ohm		(60)
#define LP3_ODT_120ohm		(120)
#define LP3_ODT_240ohm		(240)

#define LP4_PDDS_40ohm		(40)
#define LP4_PDDS_48ohm		(48)
#define LP4_PDDS_60ohm		(60)
#define LP4_PDDS_80ohm		(80)
#define LP4_PDDS_120ohm		(120)
#define LP4_PDDS_240ohm		(240)

#define LP4_DQ_ODT_40ohm	(40)
#define LP4_DQ_ODT_48ohm	(48)
#define LP4_DQ_ODT_60ohm	(60)
#define LP4_DQ_ODT_80ohm	(80)
#define LP4_DQ_ODT_120ohm	(120)
#define LP4_DQ_ODT_240ohm	(240)
#define LP4_DQ_ODT_DIS		(0)

#define LP4_CA_ODT_40ohm	(40)
#define LP4_CA_ODT_48ohm	(48)
#define LP4_CA_ODT_60ohm	(60)
#define LP4_CA_ODT_80ohm	(80)
#define LP4_CA_ODT_120ohm	(120)
#define LP4_CA_ODT_240ohm	(240)
#define LP4_CA_ODT_DIS		(0)

#define PHY_DRV_ODT_Hi_Z	(0)
#define PHY_DRV_ODT_240		(240)
#define PHY_DRV_ODT_120		(120)
#define PHY_DRV_ODT_80		(80)
#define PHY_DRV_ODT_60		(60)
#define PHY_DRV_ODT_48		(48)
#define PHY_DRV_ODT_40		(40)
#define PHY_DRV_ODT_34_3	(34)

#endif /* _DT_BINDINGS_DRAM_ROCKCHIP_RK3399_H */
