/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#ifndef COMPILER_H
#define COMPILER_H

#define __always_unused __attribute__((__unused__))
#define __noreturn __attribute__((__noreturn__))
#define __unreachable() __builtin_unreachable()

/* curse(e) has value e, but the compiler cannot assume so */
#define curse(e) ({				\
	__typeof__(e) __curse_e = (e);		\
	asm ("" : "+r" (__curse_e));		\
	__curse_e;				\
})

#endif /* ! COMPILER_H */
