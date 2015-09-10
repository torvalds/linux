/*
 *
 * Copyright (C) 2011-2014 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DT_BINDINGS_DRAM_ROCKCHIP_RK3368_H
#define _DT_BINDINGS_DRAM_ROCKCHIP_RK3368_H

#define DDR3_DS_34ohm		(1<<1)
#define DDR3_DS_40ohm		(0x0)

#define LP2_DS_34ohm		(0x1)
#define LP2_DS_40ohm		(0x2)
#define LP2_DS_48ohm		(0x3)
#define LP2_DS_60ohm		(0x4)
#define LP2_DS_68_6ohm		(0x5)/*optional*/
#define LP2_DS_80ohm		(0x6)
#define LP2_DS_120ohm		(0x7)/*optional*/

#define LP3_DS_34ohm		(0x1)
#define LP3_DS_40ohm		(0x2)
#define LP3_DS_48ohm		(0x3)
#define LP3_DS_60ohm		(0x4)
#define LP3_DS_80ohm		(0x6)
#define LP3_DS_34D_40U		(0x9)
#define LP3_DS_40D_48U		(0xa)
#define LP3_DS_34D_48U		(0xb)

#define DDR3_ODT_DIS		(0)
#define DDR3_ODT_40ohm		((1<<2)|(1<<6))
#define DDR3_ODT_60ohm		(1<<2)
#define DDR3_ODT_120ohm		(1<<6)

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

#endif /*_DT_BINDINGS_DRAM_ROCKCHIP_RK3368_H*/
