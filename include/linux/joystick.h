/*
 *  Copyright (C) 1996-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _LINUX_JOYSTICK_H
#define _LINUX_JOYSTICK_H

#include <uapi/linux/joystick.h>

#if BITS_PER_LONG == 64
#define JS_DATA_SAVE_TYPE JS_DATA_SAVE_TYPE_64
#elif BITS_PER_LONG == 32
#define JS_DATA_SAVE_TYPE JS_DATA_SAVE_TYPE_32
#else
#error Unexpected BITS_PER_LONG
#endif
#endif /* _LINUX_JOYSTICK_H */
