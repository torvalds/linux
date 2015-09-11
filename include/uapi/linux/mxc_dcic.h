/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved
 */

/*
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

/*!
 * @file uapi/linux/mxc_dcic.h
 *
 * @brief MXC DCIC private header file
 *
 * @ingroup MXC DCIC
 */
#ifndef __ASM_ARCH_MXC_DCIC_H__
#define __ASM_ARCH_MXC_DCIC_H__

#define DCIC_IOC_ALLOC_ROI_NUM	_IO('D', 10)
#define DCIC_IOC_FREE_ROI_NUM	_IO('D', 11)
#define DCIC_IOC_CONFIG_DCIC	_IO('D', 12)
#define DCIC_IOC_CONFIG_ROI		_IO('D', 13)
#define DCIC_IOC_GET_RESULT		_IO('D', 14)

struct roi_params {
	unsigned int roi_n;
	unsigned int ref_sig;
	unsigned int start_y;
	unsigned int start_x;
	unsigned int end_y;
	unsigned int end_x;
	char freeze;
};

#endif
