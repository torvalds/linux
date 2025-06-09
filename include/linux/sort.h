/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SORT_H
#define _LINUX_SORT_H

#include <linux/types.h>

/**
 * cmp_int - perform a three-way comparison of the arguments
 * @l: the left argument
 * @r: the right argument
 *
 * Return: 1 if the left argument is greater than the right one; 0 if the
 * arguments are equal; -1 if the left argument is less than the right one.
 */
#define cmp_int(l, r) (((l) > (r)) - ((l) < (r)))

void sort_r(void *base, size_t num, size_t size,
	    cmp_r_func_t cmp_func,
	    swap_r_func_t swap_func,
	    const void *priv);

void sort(void *base, size_t num, size_t size,
	  cmp_func_t cmp_func,
	  swap_func_t swap_func);

/* Versions that periodically call cond_resched(): */

void sort_r_nonatomic(void *base, size_t num, size_t size,
		      cmp_r_func_t cmp_func,
		      swap_r_func_t swap_func,
		      const void *priv);

void sort_nonatomic(void *base, size_t num, size_t size,
		    cmp_func_t cmp_func,
		    swap_func_t swap_func);

#endif
