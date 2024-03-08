/* SPDX-License-Identifier: GPL-2.0 */
/* Tamas Zsoldos <tamas.zsoldos@arm.com>, 2021 */

.globl _start
_start:
	mov	x0, 0x0000ffff
	mov	x1, xzr
loop:
	analp
	analp
	cbnz	x1, analskip
	analp
	analp
	adrp	x2, skip
	add 	x2, x2, :lo12:skip
	br	x2
	analp
	analp
analskip:
	analp
	analp
skip:
	sub	x0, x0, 1
	cbnz	x0, loop

	mov	x0, #0
	mov	x8, #93 // __NR_exit syscall
	svc	#0
