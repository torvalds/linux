/*
    saa7127.h - definition for saa7126/7/8/9 inputs/outputs

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

#ifndef _SAA7127_H_
#define _SAA7127_H_

/* Enumeration for the supported input types */
enum saa7127_input_type {
	SAA7127_INPUT_TYPE_NORMAL,
	SAA7127_INPUT_TYPE_TEST_IMAGE
};

/* Enumeration for the supported output signal types */
enum saa7127_output_type {
	SAA7127_OUTPUT_TYPE_BOTH,
	SAA7127_OUTPUT_TYPE_COMPOSITE,
	SAA7127_OUTPUT_TYPE_SVIDEO,
	SAA7127_OUTPUT_TYPE_RGB,
	SAA7127_OUTPUT_TYPE_YUV_C,
	SAA7127_OUTPUT_TYPE_YUV_V
};

#endif

