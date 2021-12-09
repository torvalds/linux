// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021 ARM Limited.

.arch_extension sve

.globl rdvl_sve
rdvl_sve:
	hint	34			// BTI C
	rdvl	x0, #1
	ret
