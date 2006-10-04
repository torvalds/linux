#ifdef CONFIG_SMP
	.macro LOCK_PREFIX
1:	lock
	.section .smp_locks,"a"
	.align 4
	.long 1b
	.previous
	.endm
#else
	.macro LOCK_PREFIX
	.endm
#endif
