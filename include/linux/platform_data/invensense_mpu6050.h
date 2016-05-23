/*
* Copyright (C) 2012 Invensense, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#ifndef __INV_MPU6050_PLATFORM_H_
#define __INV_MPU6050_PLATFORM_H_

/**
 * struct inv_mpu6050_platform_data - Platform data for the mpu driver
 * @orientation:	Orientation matrix of the chip (deprecated in favor of
 *			mounting matrix retrieved from device-tree)
 *
 * Contains platform specific information on how to configure the MPU6050 to
 * work on this platform.  The orientation matricies are 3x3 rotation matricies
 * that are applied to the data to rotate from the mounting orientation to the
 * platform orientation.  The values must be one of 0, 1, or -1 and each row and
 * column should have exactly 1 non-zero value.
 *
 * Deprecated in favor of mounting matrix retrieved from device-tree.
 */
struct inv_mpu6050_platform_data {
	__s8 orientation[9];
};

#endif
