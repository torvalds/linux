#pragma once

#include <linux/kernel.h>

#ifdef CC_HAVE_BUILTIN_OVERFLOW

#define overflow_usub __builtin_usub_overflow

#else

static inline bool overflow_usub(unsigned int a, unsigned int b,
				 unsigned int *res)
{
	*res = a - b;
	return *res > a ? true : false;
}

#endif
