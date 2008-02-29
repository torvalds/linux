! entry.S macro define
	
	.macro	cli
	stc	sr, r0
	or	#0xf0, r0
	ldc	r0, sr
	.endm

	.macro	sti
	mov	#0xf0, r11
	extu.b	r11, r11
	not	r11, r11
	stc	sr, r10
	and	r11, r10
#ifdef CONFIG_CPU_HAS_SR_RB
	stc	k_g_imask, r11
	or	r11, r10
#endif
	ldc	r10, sr
	.endm

	.macro	get_current_thread_info, ti, tmp
#ifdef CONFIG_CPU_HAS_SR_RB
	stc	r7_bank, \ti
#else
	mov	#((THREAD_SIZE - 1) >> 10) ^ 0xff, \tmp
	shll8	\tmp
	shll2	\tmp
	mov	r15, \ti
	and	\tmp, \ti
#endif	
	.endm

