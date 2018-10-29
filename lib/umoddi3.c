// SPDX-License-Identifier: GPL-2.0

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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.
 */

#include <linux/module.h>
#include <linux/libgcc.h>

extern unsigned long long __udivmoddi4(unsigned long long u,
				       unsigned long long v,
				       unsigned long long *rp);

unsigned long long __umoddi3(unsigned long long u, unsigned long long v)
{
	unsigned long long w;
	(void)__udivmoddi4(u, v, &w);
	return w;
}
EXPORT_SYMBOL(__umoddi3);
