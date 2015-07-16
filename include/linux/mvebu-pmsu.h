/*
 * Copyright (C) 2012 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MVEBU_PMSU_H__
#define __MVEBU_PMSU_H__

#ifdef CONFIG_MACH_MVEBU_V7
int mvebu_pmsu_dfs_request(int cpu);
#else
static inline int mvebu_pmsu_dfs_request(int cpu) { return -ENODEV; }
#endif

#endif /* __MVEBU_PMSU_H__ */
