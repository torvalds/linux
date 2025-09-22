/*	$OpenBSD: cpufunc.h,v 1.1 2020/11/29 12:54:36 kettenis Exp $	*/

/* Public Domain */

#ifndef _MACHINE_CPUFUNC_H_
#define _MACHINE_CPUFUNC_H_

#include <arm/cpufunc.h>

#ifdef _KERNEL

register_t smc_call(register_t, register_t, register_t, register_t);

#endif	/* _KERNEL */

#endif	/* _MACHINE_CPUFUNC_H_ */
