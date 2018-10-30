/*
 * lib/lshrdi3.c
 *
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.
 */

#include <linux/module.h>
#include <linux/libgcc.h>

long long notrace __lshrdi3(long long u, word_type b)
{
	DWunion uu, w;
	word_type bm;

	if (b == 0)
		return u;

	uu.ll = u;
	bm = 32 - b;

	if (bm <= 0) {
		w.s.high = 0;
		w.s.low = (unsigned int) uu.s.high >> -bm;
	} else {
		const unsigned int carries = (unsigned int) uu.s.high << bm;

		w.s.high = (unsigned int) uu.s.high >> b;
		w.s.low = ((unsigned int) uu.s.low >> b) | carries;
	}

	return w.ll;
}
EXPORT_SYMBOL(__lshrdi3);
