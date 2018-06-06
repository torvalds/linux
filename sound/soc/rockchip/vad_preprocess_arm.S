/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip VAD Preprocess
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

	.arch armv7-a
	.fpu softvfp
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 4
	.eabi_attribute 34, 1
	.eabi_attribute 18, 4
	.file	"vad_preprocess_arm.S"
	.text
	.align	2
	.global	vad_preprocess_init
	.type	vad_preprocess_init, %function
vad_preprocess_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L4
	ldr	r3, [r0, #8]
	strh	r3, [r2]	@ movhi
	ldr	r3, [r0, #4]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r0, #12]
	strh	r3, [r2, #4]	@ movhi
	ldr	r3, [r0]
	strh	r3, [r2, #6]	@ movhi
	ldr	r3, [r0, #16]
	tst	r3, #512
	ubfx	r3, r3, #0, #9
	eorne	r3, r3, #65280
	eorne	r3, r3, #255
	uxtheq	r3, r3
	strh	r3, [r2, #8]	@ movhi
	bx	lr
.L5:
	.align	2
.L4:
	.word	.LANCHOR0
	.fnend
	.size	vad_preprocess_init, .-vad_preprocess_init
	.align	2
	.global	vad_preprocess
	.type	vad_preprocess, %function
vad_preprocess:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L27
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	lr, #15349
	ldrsh	r2, [r3, #8]
	ldrh	ip, [r3, #10]
	ldr	r1, .L27+4
	mul	r0, r2, r0
	ldrh	r4, [r3, #12]
	smulbb	r1, ip, r1
	add	r2, r0, #31
	cmp	r0, #0
	movlt	r0, r2
	ldrh	r2, [r3, #14]
	mov	r0, r0, asr #5
	mla	r1, lr, r0, r1
	smlabb	r1, r4, lr, r1
	ldr	r4, .L27+8
	ldrsh	lr, [r3, #16]
	smulbb	r4, r2, r4
	rsb	r4, r4, r1
	movw	r1, #14379
	mls	r4, lr, r1, r4
	cmp	r4, #1
	mov	r5, r4, asr #31
	sbcs	r1, r5, #0
	blt	.L7
	adds	r4, r4, #8192
	adc	r5, r5, #0
	b	.L24
.L7:
	subs	r4, r4, #8192
	movw	r8, #16383
	sbc	r5, r5, #0
	mov	r9, #0
	mov	r6, r5, asr #31
	mov	r7, r6, asr #31
	and	r6, r6, r8
	and	r7, r7, r9
	adds	r4, r4, r6
	adc	r5, r5, r7
.L24:
	strh	ip, [r3, #12]	@ movhi
	mov	r1, r4, lsr #14
	ldrh	ip, [r3, #18]
	orr	r1, r1, r5, asl #18
	strh	r0, [r3, #10]	@ movhi
	add	ip, ip, #1
	uxth	r1, r1
	ldr	r0, .L27+12
	uxth	ip, ip
	strh	r1, [r3, #14]	@ movhi
	strh	ip, [r3, #18]	@ movhi
	sxth	r1, r1
	sxth	ip, ip
	cmp	r1, #0
	and	r0, r0, ip
	rsblt	r1, r1, #0
	cmp	r0, #0
	strh	r2, [r3, #16]	@ movhi
	sublt	r0, r0, #1
	ldr	r2, [r3, #20]
	mvnlt	r0, r0, asl #24
	add	r2, r1, r2
	mvnlt	r0, r0, lsr #24
	addlt	r0, r0, #1
	cmp	r0, #0
	str	r2, [r3, #20]
	bne	.L9
	ldr	r0, [r3, #24]
	ldr	ip, .L27
	cmp	r0, #99
	bgt	.L11
	add	r2, r2, #128
	add	ip, ip, r0, asl #1
	add	lr, r2, #255
	cmp	r2, #0
	movlt	r2, lr
	mov	r2, r2, asr #8
	strh	r2, [ip, #28]	@ movhi
	b	.L12
.L11:
	add	lr, ip, #28
	add	ip, ip, #226
.L13:
	ldrh	r4, [lr, #2]
	strh	r4, [lr], #2	@ movhi
	cmp	lr, ip
	bne	.L13
	add	r2, r2, #128
	add	ip, r2, #255
	cmp	r2, #0
	movlt	r2, ip
	mov	r2, r2, asr #8
	strh	r2, [r3, #226]	@ movhi
.L12:
	cmp	r0, #99
	ldrh	r2, [r3, #28]
	ldrle	r4, .L27+16
	movle	lr, #1
	bgt	.L26
.L15:
	cmp	lr, r0
	bge	.L17
	ldrsh	ip, [r4], #2
	sxth	r2, r2
	add	lr, lr, #1
	cmp	ip, r2
	movge	ip, r2
	uxth	r2, ip
	b	.L15
.L26:
	ldr	ip, .L27+16
	add	r4, ip, #198
.L18:
	ldrsh	lr, [ip], #2
	sxth	r2, r2
	cmp	r2, lr
	movge	r2, lr
	cmp	ip, r4
	uxth	r2, r2
	bne	.L18
.L17:
	ldrh	lr, [r3, #6]
	mov	ip, #128
	mov	r4, #230
	add	r0, r0, #1
	str	r0, [r3, #24]
	smlabb	ip, lr, r4, ip
	mov	lr, #26
	smlabb	r2, r2, lr, ip
	add	ip, r2, #255
	cmp	r2, #0
	movlt	r2, ip
	mov	r2, r2, asr #8
	strh	r2, [r3, #6]	@ movhi
	mov	r2, #0
	str	r2, [r3, #20]
	strh	r2, [r3, #18]	@ movhi
.L9:
	ldrh	r2, [r3, #6]
	ldrh	ip, [r3, #2]
	ldrsh	r3, [r3]
	ldr	r0, .L27
	smlabb	r3, r2, ip, r3
	add	r2, r0, #428
	cmp	r1, r3
	ble	.L19
	ldrh	r3, [r2]
	ldrsh	r0, [r0, #4]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2]	@ movhi
	sxth	r3, r3
	cmp	r0, r3
	movge	r0, #0
	movlt	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L19:
	mov	r0, #0
	strh	r0, [r2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L28:
	.align	2
.L27:
	.word	.LANCHOR0
	.word	-30697
	.word	-30632
	.word	-2147483393
	.word	.LANCHOR0+30
	.fnend
	.size	vad_preprocess, .-vad_preprocess
	.align	2
	.global	vad_preprocess_destroy
	.type	vad_preprocess_destroy, %function
vad_preprocess_destroy:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L32
	mov	r3, #0
	mov	ip, r3
	strh	r3, [r2, #10]	@ movhi
	strh	r3, [r2, #12]	@ movhi
	strh	r3, [r2, #14]	@ movhi
	strh	r3, [r2, #16]	@ movhi
	strh	r3, [r2, #18]	@ movhi
	add	r2, r2, #428
	strh	r3, [r2]	@ movhi
.L30:
	ldr	r2, .L32
	mov	r1, #0
	add	r0, r2, #28
	strh	ip, [r3, r0]	@ movhi
	add	r3, r3, #2
	cmp	r3, #200
	bne	.L30
	mov	r3, #32
	str	r1, [r2, #20]
	strh	r1, [r2, #6]	@ movhi
	strh	r3, [r2, #8]	@ movhi
	str	r1, [r2, #24]
	bx	lr
.L33:
	.align	2
.L32:
	.word	.LANCHOR0
	.fnend
	.size	vad_preprocess_destroy, .-vad_preprocess_destroy
	.align	2
	.global	vad_preprocess_update_params
	.type	vad_preprocess_update_params, %function
vad_preprocess_update_params:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L35
	ldrsh	r3, [r3, #6]
	str	r3, [r0]
	bx	lr
.L36:
	.align	2
.L35:
	.word	.LANCHOR0
	.fnend
	.size	vad_preprocess_update_params, .-vad_preprocess_update_params
	.bss
	.align	2
.LANCHOR0 = . + 0
	.type	g_sound_thd, %object
	.size	g_sound_thd, 2
g_sound_thd:
	.space	2
	.type	g_noise_level, %object
	.size	g_noise_level, 2
g_noise_level:
	.space	2
	.type	g_vad_con_thd, %object
	.size	g_vad_con_thd, 2
g_vad_con_thd:
	.space	2
	.type	g_noise_abs, %object
	.size	g_noise_abs, 2
g_noise_abs:
	.space	2
	.type	g_signal_gain, %object
	.size	g_signal_gain, 2
g_signal_gain:
	.space	2
	.type	g_xn_1, %object
	.size	g_xn_1, 2
g_xn_1:
	.space	2
	.type	g_xn_2, %object
	.size	g_xn_2, 2
g_xn_2:
	.space	2
	.type	g_yn_1, %object
	.size	g_yn_1, 2
g_yn_1:
	.space	2
	.type	g_yn_2, %object
	.size	g_yn_2, 2
g_yn_2:
	.space	2
	.type	g_sample_cnt, %object
	.size	g_sample_cnt, 2
g_sample_cnt:
	.space	2
	.type	g_sum_abs_frm, %object
	.size	g_sum_abs_frm, 4
g_sum_abs_frm:
	.space	4
	.type	frm_count, %object
	.size	frm_count, 4
frm_count:
	.space	4
	.type	g_ave_abs_rec, %object
	.size	g_ave_abs_rec, 400
g_ave_abs_rec:
	.space	400
	.type	g_vad_cnt, %object
	.size	g_vad_cnt, 2
g_vad_cnt:
	.space	2
	.ident	"GCC: (GNU) 4.9 20150123 (prerelease)"
	.section	.note.GNU-stack,"",%progbits
