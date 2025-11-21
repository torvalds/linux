/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GCD_H
#define _GCD_H

#include <linux/compiler.h>
#include <linux/jump_label.h>

DECLARE_STATIC_KEY_TRUE(efficient_ffs_key);

unsigned long gcd(unsigned long a, unsigned long b) __attribute_const__;

#endif /* _GCD_H */
