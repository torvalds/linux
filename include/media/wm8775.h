/*
    wm8775.h - definition for wm8775 inputs and outputs

    Copyright (C) 2006 Hans Verkuil (hverkuil@xs4all.nl)

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

#ifndef _WM8775_H_
#define _WM8775_H_

/* The WM8775 has 4 inputs and one output. Zero or more inputs
   are multiplexed together to the output. Hence there are
   16 combinations.
   If only one input is active (the normal case) then the
   input values 1, 2, 4 or 8 should be used. */

#define WM8775_AIN1 1
#define WM8775_AIN2 2
#define WM8775_AIN3 4
#define WM8775_AIN4 8

#endif
