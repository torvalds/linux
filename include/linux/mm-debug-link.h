/*
 *  include/linux/mm-debug-link.h
 *
 * MM Debug Link driver header file
 *
 * Adapted from sld-hub driver written by Graham Moore (grmoore@altera.com)
 *
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MM_DEBUG_LINK_H__

#include <linux/ioctl.h>

/*
 * The size in bytes of the connection identification ROM
 * within altera_mm_debug_link. (A constant feature of the hardware.)
 */
#define MM_DEBUG_LINK_ID_SIZE		((size_t)16)
#define MM_DEBUG_LINK_CODE		0xA1
#define MM_DEBUG_LINK_IOCTL_READ_ID	_IOR((MM_DEBUG_LINK_CODE), 1, \
					unsigned char[MM_DEBUG_LINK_ID_SIZE])
#define MM_DEBUG_LINK_IOCTL_WRITE_MIXER	_IOW((MM_DEBUG_LINK_CODE), 2, int)
#define MM_DEBUG_LINK_IOCTL_ENABLE	_IOW((MM_DEBUG_LINK_CODE), 3, int)
#define MM_DEBUG_LINK_IOCTL_DEBUG_RESET	_IOW((MM_DEBUG_LINK_CODE), 4, int)

#endif /* #ifndef __MM_DEBUG_LINK_H__ */
