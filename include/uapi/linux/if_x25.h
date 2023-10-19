/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  Linux X.25 packet to device interface
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#ifndef _IF_X25_H
#define _IF_X25_H

#include <linux/types.h>

/* Documentation/networking/x25-iface.rst */
#define X25_IFACE_DATA		0x00
#define X25_IFACE_CONNECT	0x01
#define X25_IFACE_DISCONNECT	0x02
#define X25_IFACE_PARAMS	0x03

#endif /* _IF_X25_H */
