/*
    general.c - Part of libsensors, a Linux library for reading sensor data.
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

#include "error.h"
#include "general.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define A_BUNCH 16

void sensors_malloc_array(void *list, int *num_el, int *max_el, int el_size)
{
	void **my_list = (void **)list;

	*my_list = malloc(el_size*A_BUNCH);
	if (! *my_list)
		sensors_fatal_error(__func__, "Allocating new elements");
	*max_el = A_BUNCH;
	*num_el = 0;
}

void sensors_free_array(void *list, int *num_el, int *max_el)
{
	void **my_list = (void **)list;

	free(*my_list);
	*my_list = NULL;
	*num_el = 0;
	*max_el = 0;
}

void sensors_add_array_el(const void *el, void *list, int *num_el,
			  int *max_el, int el_size)
{
	int new_max_el;
	void **my_list = (void *)list;
	if (*num_el + 1 > *max_el) {
		new_max_el = *max_el + A_BUNCH;
		*my_list = realloc(*my_list, new_max_el * el_size);
		if (! *my_list)
			sensors_fatal_error(__func__,
					    "Allocating new elements");
		*max_el = new_max_el;
	}
	memcpy(((char *) *my_list) + *num_el * el_size, el, el_size);
	(*num_el) ++;
}

void sensors_add_array_els(const void *els, int nr_els, void *list,
			   int *num_el, int *max_el, int el_size)
{
	int new_max_el;
	void **my_list = (void *)list;
	if (*num_el + nr_els > *max_el) {
		new_max_el = (*max_el + nr_els + A_BUNCH);
		new_max_el -= new_max_el % A_BUNCH;
		*my_list = realloc(*my_list, new_max_el * el_size);
		if (! *my_list)
			sensors_fatal_error(__func__,
					    "Allocating new elements");
		*max_el = new_max_el;
	}
	memcpy(((char *)*my_list) + *num_el * el_size, els, el_size * nr_els);
	*num_el += nr_els;
}
