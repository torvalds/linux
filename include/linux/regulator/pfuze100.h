/*
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef __LINUX_REG_PFUZE100_H
#define __LINUX_REG_PFUZE100_H

#define PFUZE100_SW1AB		0
#define PFUZE100_SW1C		1
#define PFUZE100_SW2		2
#define PFUZE100_SW3A		3
#define PFUZE100_SW3B		4
#define PFUZE100_SW4		5
#define PFUZE100_SWBST		6
#define PFUZE100_VSNVS		7
#define PFUZE100_VREFDDR	8
#define PFUZE100_VGEN1		9
#define PFUZE100_VGEN2		10
#define PFUZE100_VGEN3		11
#define PFUZE100_VGEN4		12
#define PFUZE100_VGEN5		13
#define PFUZE100_VGEN6		14
#define PFUZE100_MAX_REGULATOR	15

#define PFUZE200_SW1AB		0
#define PFUZE200_SW2		1
#define PFUZE200_SW3A		2
#define PFUZE200_SW3B		3
#define PFUZE200_SWBST		4
#define PFUZE200_VSNVS		5
#define PFUZE200_VREFDDR	6
#define PFUZE200_VGEN1		7
#define PFUZE200_VGEN2		8
#define PFUZE200_VGEN3		9
#define PFUZE200_VGEN4		10
#define PFUZE200_VGEN5		11
#define PFUZE200_VGEN6		12

#define PFUZE3000_SW1A		0
#define PFUZE3000_SW1B		1
#define PFUZE3000_SW2		2
#define PFUZE3000_SW3		3
#define PFUZE3000_SWBST		4
#define PFUZE3000_VSNVS		5
#define PFUZE3000_VREFDDR	6
#define PFUZE3000_VLDO1		7
#define PFUZE3000_VLDO2		8
#define PFUZE3000_VCCSD		9
#define PFUZE3000_V33		10
#define PFUZE3000_VLDO3		11
#define PFUZE3000_VLDO4		12

struct regulator_init_data;

struct pfuze_regulator_platform_data {
	struct regulator_init_data *init_data[PFUZE100_MAX_REGULATOR];
};

#endif /* __LINUX_REG_PFUZE100_H */
