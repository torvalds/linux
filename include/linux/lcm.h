#ifndef _LCM_H
#define _LCM_H

#include <linux/compiler.h>

unsigned long lcm(unsigned long a, unsigned long b) __attribute_const__;
unsigned long lcm_not_zero(unsigned long a, unsigned long b) __attribute_const__;

#endif /* _LCM_H */
