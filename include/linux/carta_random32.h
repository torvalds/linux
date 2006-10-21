/*
 * Fast, simple, yet decent quality random number generator based on
 * a paper by David G. Carta ("Two Fast Implementations of the
 * `Minimal Standard' Random Number Generator," Communications of the
 * ACM, January, 1990).
 *
 * Copyright (c) 2002-2006 Hewlett-Packard Development Company, L.P.
 *	Contributed by Stephane Eranian <eranian@hpl.hp.com>
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
#ifndef _LINUX_CARTA_RANDOM32_H_
#define _LINUX_CARTA_RANDOM32_H_

u64 carta_random32(u64 seed);

#endif /* _LINUX_CARTA_RANDOM32_H_ */
