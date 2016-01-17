/*
    cs53l32a.h - definition for cs53l32a inputs and outputs

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

#ifndef _CS53L32A_H_
#define _CS53L32A_H_

/* There are 2 physical inputs, but the second input can be
   placed in two modes, the first mode bypasses the PGA (gain),
   the second goes through the PGA. Hence there are three
   possible inputs to choose from. */

/* CS53L32A HW inputs */
#define CS53L32A_IN0 0
#define CS53L32A_IN1 1
#define CS53L32A_IN2 2

#endif
