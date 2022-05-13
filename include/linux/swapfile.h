/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAPFILE_H
#define _LINUX_SWAPFILE_H

/*
 * these were static in swapfile.c but frontswap.c needs them and we don't
 * want to expose them to the dozens of source files that include swap.h
 */
extern struct swap_info_struct *swap_info[];
extern unsigned long generic_max_swapfile_size(void);
extern unsigned long max_swapfile_size(void);

#endif /* _LINUX_SWAPFILE_H */
