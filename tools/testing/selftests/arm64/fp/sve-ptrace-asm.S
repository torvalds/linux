// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2015-2019 ARM Limited.
// Original author: Dave Martin <Dave.Martin@arm.com>
#include <asm/unistd.h>

.arch_extension sve

.globl sve_store_patterns

sve_store_patterns:
	mov	x1, x0

	index	z0.b, #0, #1
	str	q0, [x1]

	mov	w8, #__NR_getpid
	svc	#0
	str	q0, [x1, #0x10]

	mov	z1.d, z0.d
	str	q0, [x1, #0x20]

	mov	w8, #__NR_getpid
	svc	#0
	str	q0, [x1, #0x30]

	mov	z1.d, z0.d
	str	q0, [x1, #0x40]

	ret

.size	sve_store_patterns, . - sve_store_patterns
.type	sve_store_patterns, @function
