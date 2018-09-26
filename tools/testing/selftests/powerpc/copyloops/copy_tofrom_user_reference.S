#include <asm/ppc_asm.h>

_GLOBAL(copy_tofrom_user_reference)
	cmpdi	r5,0
	beq	4f

	mtctr	r5

1:	lbz	r6,0(r4)
2:	stb	r6,0(r3)
	addi	r3,r3,1
	addi	r4,r4,1
	bdnz	1b

3:	mfctr	r3
	blr

4:	mr	r3,r5
	blr

.section __ex_table,"a"
	.llong	1b,3b
	.llong	2b,3b
.text
