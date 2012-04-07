/*
 * anatop.h - Anatop MFD driver
 *
 *  Copyright (C) 2012 Ying-Chun Liu (PaulLiu) <paul.liu@linaro.org>
 *  Copyright (C) 2012 Linaro
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_MFD_ANATOP_H
#define __LINUX_MFD_ANATOP_H

#include <linux/spinlock.h>

/**
 * anatop - MFD data
 * @ioreg: ioremap register
 * @reglock: spinlock for register read/write
 */
struct anatop {
	void *ioreg;
	spinlock_t reglock;
};

extern u32 anatop_get_bits(struct anatop *, u32, int, int);
extern void anatop_set_bits(struct anatop *, u32, int, int, u32);

#endif /*  __LINUX_MFD_ANATOP_H */
