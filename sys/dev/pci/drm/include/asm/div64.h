/* Public domain. */

#ifndef _ASM_DIV64_H
#define _ASM_DIV64_H

#include <sys/types.h>

#define do_div(n, base) ({				\
	uint32_t __base = (base);			\
	uint32_t __rem = ((uint64_t)(n)) % __base;	\
	(n) = ((uint64_t)(n)) / __base;			\
	__rem;						\
})

#endif
