/* 4 slots for argument spill area.  1 for cpreturn, 1 for stack.
   Return spill offset of 40 and 20.  Aligned to 16 bytes for n32.  */

#ifdef	__mips16
#define RA $7
#else
#define RA $31
#endif

	.section .init,"ax",@progbits
#ifdef __mips64
	ld      RA,40($sp)
	daddu	$sp,$sp,48
#else
	lw	RA,20($sp)
	addu	$sp,$sp,32
#endif
	j	RA

	.section .fini,"ax",@progbits
#ifdef	__mips64
	ld	RA,40($sp)
	daddu	$sp,$sp,48
#else
	lw	RA,20($sp)
	addu	$sp,$sp,32
#endif
	j	RA

