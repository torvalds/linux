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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/ioctl.h>

#define XR_USB_SERIAL_IOC_MAGIC       	'v'

#define XR_USB_SERIAL_GET_REG           	_IOWR(XR_USB_SERIAL_IOC_MAGIC, 1, int)
#define XR_USB_SERIAL_SET_REG           	_IOWR(XR_USB_SERIAL_IOC_MAGIC, 2, int)
#define XR_USB_SERIAL_SET_ADDRESS_MATCH 	_IO(XR_USB_SERIAL_IOC_MAGIC, 3)
#define XR_USB_SERIAL_SET_PRECISE_FLAGS     	_IO(XR_USB_SERIAL_IOC_MAGIC, 4)
#define XR_USB_SERIAL_TEST_MODE         	_IO(XR_USB_SERIAL_IOC_MAGIC, 5)
#define XR_USB_SERIAL_LOOPBACK          	_IO(XR_USB_SERIAL_IOC_MAGIC, 6)

#define VZ_ADDRESS_UNICAST_S        	0
#define VZ_ADDRESS_BROADCAST_S      	8
#define VZ_ADDRESS_MATCH(U, B)          (0x8000000 | ((B) << VZ_ADDRESS_BROADCAST_S) | ((U) << VZ_ADDRESS_UNICAST_S))
#define VZ_ADDRESS_MATCH_DISABLE    	0
