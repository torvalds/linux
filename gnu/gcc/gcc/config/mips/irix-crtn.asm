	.abicalls
	.set	noreorder
	.set	nomacro

	.section .gcc_init,"ax",@progbits
#if _MIPS_SIM == _ABIO32
	lw	$31,0($sp)
	jr	$31
	addiu	$sp,$sp,16
#else
	ld	$31,0($sp)
	ld	$28,8($sp)
	jr	$31
	daddiu	$sp,$sp,16
#endif

	.section .gcc_fini,"ax",@progbits
#if _MIPS_SIM == _ABIO32
	lw	$31,0($sp)
	jr	$31
	addiu	$sp,$sp,16
#else
	ld	$31,0($sp)
	ld	$28,8($sp)
	jr	$31
	daddiu	$sp,$sp,16
#endif
