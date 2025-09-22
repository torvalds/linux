/* 4 slots for argument spill area.  1 for cpreturn, 1 for stack.
   Return spill offset of 40 and 20.  Aligned to 16 bytes for n32.  */

	.section .init,"ax",@progbits
	.globl	_init
	.type	_init,@function
_init:
#ifdef __mips64
	daddu   $sp,$sp,-48
	sd      $31,40($sp)
#else
	addu	$sp,$sp,-32
	sw	$31,20($sp)
#endif

	.section .fini,"ax",@progbits
	.globl	_fini
	.type	_fini,@function
_fini:
#ifdef __mips64
	daddu   $sp,$sp,-48
	sd      $31,40($sp)
#else
	addu	$sp,$sp,-32
	sw	$31,20($sp)
#endif
