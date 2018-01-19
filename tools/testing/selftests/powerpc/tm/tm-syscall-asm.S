/* SPDX-License-Identifier: GPL-2.0 */
#include <ppc-asm.h>
#include <asm/unistd.h>

	.text
FUNC_START(getppid_tm_active)
	tbegin.
	beq 1f
	li	r0, __NR_getppid
	sc
	tend.
	blr
1:
	li	r3, -1
	blr

FUNC_START(getppid_tm_suspended)
	tbegin.
	beq 1f
	li	r0, __NR_getppid
	tsuspend.
	sc
	tresume.
	tend.
	blr
1:
	li	r3, -1
	blr
