/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/linkage.h>

.text
.balign 4
.code 32

	/* void tee_smc_call(struct smc_param *param); */
	.globl	tee_smc_call
ENTRY(tee_smc_call)
	push	{r4-r8, lr}
	mov	r8, r0
	ldm	r8, {r0-r7}
.arch_extension sec
	smc	#0
	stm	r8, {r0-r7}
	pop	{r4-r8, pc}
ENDPROC(tee_smc_call)
