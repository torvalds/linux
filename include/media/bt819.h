/*
    bt819.h - bt819 notifications

    Copyright (C) 2009 Hans Verkuil (hverkuil@xs4all.nl)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _BT819_H_
#define _BT819_H_

#include <linux/ioctl.h>

/* v4l2_device notifications. */

/* Needed to reset the FIFO buffer when changing the input
   or the video standard. */
#define BT819_FIFO_RESET_LOW 	_IO('b', 0)
#define BT819_FIFO_RESET_HIGH 	_IO('b', 1)

#endif
