/*
 * VTI CMA3000_Dxx Accelerometer driver
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Hemanth V <hemanthv@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_CMA3000_H
#define _LINUX_CMA3000_H

#define CMAMODE_DEFAULT    0
#define CMAMODE_MEAS100    1
#define CMAMODE_MEAS400    2
#define CMAMODE_MEAS40     3
#define CMAMODE_MOTDET     4
#define CMAMODE_FF100      5
#define CMAMODE_FF400      6
#define CMAMODE_POFF       7

#define CMARANGE_2G   2000
#define CMARANGE_8G   8000

/**
 * struct cma3000_i2c_platform_data - CMA3000 Platform data
 * @fuzz_x: Noise on X Axis
 * @fuzz_y: Noise on Y Axis
 * @fuzz_z: Noise on Z Axis
 * @g_range: G range in milli g i.e 2000 or 8000
 * @mode: Operating mode
 * @mdthr: Motion detect threshold value
 * @mdfftmr: Motion detect and free fall time value
 * @ffthr: Free fall threshold value
 */

struct cma3000_platform_data {
	int fuzz_x;
	int fuzz_y;
	int fuzz_z;
	int g_range;
	uint8_t mode;
	uint8_t mdthr;
	uint8_t mdfftmr;
	uint8_t ffthr;
	unsigned long irqflags;
};

#endif
