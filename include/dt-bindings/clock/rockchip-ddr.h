/*
 *
 * Copyright (C) 2017 ROCKCHIP, Inc.
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

#ifndef _DT_BINDINGS_CLOCK_ROCKCHIP_DDR_H
#define _DT_BINDINGS_CLOCK_ROCKCHIP_DDR_H

#define DDR2_DEFAULT	(0)

#define DDR3_800D	(0)	/* 5-5-5 */
#define DDR3_800E	(1)	/* 6-6-6 */
#define DDR3_1066E	(2)	/* 6-6-6 */
#define DDR3_1066F	(3)	/* 7-7-7 */
#define DDR3_1066G	(4)	/* 8-8-8 */
#define DDR3_1333F	(5)	/* 7-7-7 */
#define DDR3_1333G	(6)	/* 8-8-8 */
#define DDR3_1333H	(7)	/* 9-9-9 */
#define DDR3_1333J	(8)	/* 10-10-10 */
#define DDR3_1600G	(9)	/* 8-8-8 */
#define DDR3_1600H	(10)	/* 9-9-9 */
#define DDR3_1600J	(11)	/* 10-10-10 */
#define DDR3_1600K	(12)	/* 11-11-11 */
#define DDR3_1866J	(13)	/* 10-10-10 */
#define DDR3_1866K	(14)	/* 11-11-11 */
#define DDR3_1866L	(15)	/* 12-12-12 */
#define DDR3_1866M	(16)	/* 13-13-13 */
#define DDR3_2133K	(17)	/* 11-11-11 */
#define DDR3_2133L	(18)	/* 12-12-12 */
#define DDR3_2133M	(19)	/* 13-13-13 */
#define DDR3_2133N	(20)	/* 14-14-14 */
#define DDR3_DEFAULT	(21)
#define DDR_DDR2	(22)
#define DDR_LPDDR	(23)
#define DDR_LPDDR2	(24)

#define DDR4_1600J	(0)	/* 10-10-10 */
#define DDR4_1600K	(1)	/* 11-11-11 */
#define DDR4_1600L	(2)	/* 12-12-12 */
#define DDR4_1866L	(3)	/* 12-12-12 */
#define DDR4_1866M	(4)	/* 13-13-13 */
#define DDR4_1866N	(5)	/* 14-14-14 */
#define DDR4_2133N	(6)	/* 14-14-14 */
#define DDR4_2133P	(7)	/* 15-15-15 */
#define DDR4_2133R	(8)	/* 16-16-16 */
#define DDR4_2400P	(9)	/* 15-15-15 */
#define DDR4_2400R	(10)	/* 16-16-16 */
#define DDR4_2400U	(11)	/* 18-18-18 */
#define DDR4_DEFAULT	(12)

#define PAUSE_CPU_STACK_SIZE	16

#endif
