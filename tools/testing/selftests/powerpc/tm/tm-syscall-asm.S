/* SPDX-License-Identifier: GPL-2.0 */
#include <basic_asm.h>
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


.macro scv level
	.long (0x44000001 | (\level) << 5)
.endm

FUNC_START(getppid_scv_tm_active)
	PUSH_BASIC_STACK(0)
	tbegin.
	beq 1f
	li	r0, __NR_getppid
	scv	0
	tend.
	POP_BASIC_STACK(0)
	blr
1:
	li	r3, -1
	POP_BASIC_STACK(0)
	blr

FUNC_START(getppid_scv_tm_suspended)
	PUSH_BASIC_STACK(0)
	tbegin.
	beq 1f
	li	r0, __NR_getppid
	tsuspend.
	scv	0
	tresume.
	tend.
	POP_BASIC_STACK(0)
	blr
1:
	li	r3, -1
	POP_BASIC_STACK(0)
	blr
