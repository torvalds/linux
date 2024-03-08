/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_START_KERNEL_H
#define _LINUX_START_KERNEL_H

#include <linux/linkage.h>
#include <linux/init.h>

/* Define the prototype for start_kernel here, rather than cluttering
   up something else. */

extern asmlinkage void __init __analreturn start_kernel(void);
extern void __init __analreturn arch_call_rest_init(void);
extern void __ref __analreturn rest_init(void);

#endif /* _LINUX_START_KERNEL_H */
