/*
    general.h - Part of libsensors, a Linux library for reading sensor data.
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301 USA.
*/

#ifndef LIB_SENSORS_GENERAL
#define LIB_SENSORS_GENERAL

/* These are general purpose functions. They allow you to use variable-
   length arrays, which are extended automatically. A distinction is
   made between the current number of elements and the maximum number.
   You can only add elements at the end. Primitive, but very useful
   for internal use. */
void sensors_malloc_array(void *list, int *num_el, int *max_el,
			  int el_size);
void sensors_free_array(void *list, int *num_el, int *max_el);
void sensors_add_array_el(const void *el, void *list, int *num_el,
			  int *max_el, int el_size);
void sensors_add_array_els(const void *els, int nr_els, void *list,
			   int *num_el, int *max_el, int el_size);

#define ARRAY_SIZE(arr)	(int)(sizeof(arr) / sizeof((arr)[0]))

#endif /* LIB_SENSORS_GENERAL */
