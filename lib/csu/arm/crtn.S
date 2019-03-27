#include <machine/asm.h>
__FBSDID("$FreeBSD$");

	.section .init,"ax",%progbits
	ldmea	fp, {fp, sp, pc}
	mov	pc, lr

	.section .fini,"ax",%progbits
	ldmea	fp, {fp, sp, pc}
	mov	pc, lr

	.section .note.GNU-stack,"",%progbits
