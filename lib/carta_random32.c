/*
 * Copyright (c) 2002-2006 Hewlett-Packard Development Company, L.P.
 *	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */
#include <linux/types.h>
#include <linux/module.h>

/*
 * Fast, simple, yet decent quality random number generator based on
 * a paper by David G. Carta ("Two Fast Implementations of the
 * `Minimal Standard' Random Number Generator," Communications of the
 * ACM, January, 1990).
 */
u64 carta_random32 (u64 seed)
{
#       define A 16807
#       define M ((u32) 1 << 31)
        u64 s, prod = A * seed, p, q;

        p = (prod >> 31) & (M - 1);
        q = (prod >>  0) & (M - 1);
        s = p + q;
        if (s >= M)
                s -= M - 1;
        return s;
}
EXPORT_SYMBOL_GPL(carta_random32);
