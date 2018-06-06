/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip VAD Preprocess
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

	.syntax unified
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
	.thumb
	.file	"vad_preprocess_thumb.S"
	.text
	.align	1
	.global	vad_preprocess_init
	.thumb
	.thumb_func
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
	itte	ne
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
	.align	1
	.global	vad_preprocess
	.thumb
	.thumb_func
	.type	vad_preprocess, %function
vad_preprocess:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L29
	movw	r2, #34839
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r1, #15349
	ldrsh	r5, [r3, #8]
	muls	r0, r5, r0
	ldrh	r7, [r3, #10]
	it	mi
	addmi	r0, r0, #31
	ldrh	r4, [r3, #12]
	asrs	r0, r0, #5
	ldrh	r6, [r3, #14]
	smulbb	r2, r7, r2
	mla	r2, r1, r0, r2
	smlabb	r2, r4, r1, r2
	movw	r1, #34904
	smulbb	r1, r6, r1
	ldrsh	r4, [r3, #16]
	subs	r1, r2, r1
	movw	r2, #14379
	mls	r4, r4, r2, r1
	cmp	r4, #1
	asr	r5, r4, #31
	sbcs	r2, r5, #0
	blt	.L8
	adds	r4, r4, #8192
	adc	r5, r5, #0
	b	.L10
.L8:
	subs	r4, r4, #8192
	adc	r5, r5, #-1
	cmp	r4, #0
	sbcs	r2, r5, #0
	bge	.L10
	movw	r8, #16383
	mov	r9, #0
	adds	r4, r4, r8
	adc	r5, r5, r9
.L10:
	lsrs	r1, r4, #14
	ldrh	r4, [r3, #18]
	orr	r1, r1, r5, lsl #18
	ldr	r2, .L29+4
	adds	r4, r4, #1
	strh	r0, [r3, #10]	@ movhi
	uxth	r1, r1
	strh	r1, [r3, #14]	@ movhi
	uxth	r4, r4
	strh	r4, [r3, #18]	@ movhi
	sxth	r1, r1
	ldr	r0, [r3, #20]
	sxth	r4, r4
	cmp	r1, #0
	and	r2, r2, r4
	it	lt
	rsblt	r1, r1, #0
	cmp	r2, #0
	add	r0, r0, r1
	it	lt
	addlt	r2, r2, #-1
	strh	r7, [r3, #12]	@ movhi
	it	lt
	ornlt	r2, r2, #255
	strh	r6, [r3, #16]	@ movhi
	it	lt
	addlt	r2, r2, #1
	str	r0, [r3, #20]
	cmp	r2, #0
	bne	.L11
	ldr	r4, [r3, #24]
	ldr	r2, .L29
	cmp	r4, #99
	bgt	.L13
	adds	r5, r0, #128
	add	r2, r2, r4, lsl #1
	it	mi
	addwmi	r5, r0, #383
	asrs	r5, r5, #8
	strh	r5, [r2, #28]	@ movhi
	b	.L15
.L13:
	add	r5, r2, #28
	adds	r2, r2, #226
.L16:
	ldrh	r6, [r5, #2]
	strh	r6, [r5], #2	@ movhi
	cmp	r5, r2
	bne	.L16
	adds	r2, r0, #128
	it	mi
	addwmi	r2, r0, #383
	ldr	r0, .L29
	asrs	r2, r2, #8
	strh	r2, [r0, #226]	@ movhi
.L15:
	cmp	r4, #99
	ldrh	r2, [r3, #28]
	mov	r0, #1
	bgt	.L18
	ldr	r5, .L29+8
.L19:
	cmp	r0, r4
	bge	.L21
	ldrsh	r6, [r5, r0, lsl #1]
	sxth	r2, r2
	adds	r0, r0, #1
	cmp	r2, r6
	it	ge
	movge	r2, r6
	uxth	r2, r2
	b	.L19
.L18:
	ldr	r6, .L29+8
.L22:
	ldrsh	r5, [r6, r0, lsl #1]
	sxth	r2, r2
	adds	r0, r0, #1
	cmp	r2, r5
	it	ge
	movge	r2, r5
	cmp	r0, #100
	uxth	r2, r2
	bne	.L22
.L21:
	ldrh	r5, [r3, #6]
	movs	r0, #128
	movs	r6, #230
	adds	r4, r4, #1
	str	r4, [r3, #24]
	smlabb	r0, r5, r6, r0
	movs	r5, #26
	smlabb	r2, r2, r5, r0
	ldr	r0, .L29
	cmp	r2, #0
	it	lt
	addlt	r2, r2, #255
	asrs	r2, r2, #8
	strh	r2, [r0, #6]	@ movhi
	movs	r2, #0
	str	r2, [r3, #20]
	strh	r2, [r3, #18]	@ movhi
.L11:
	ldrh	r0, [r3, #6]
	ldrh	r4, [r3, #2]
	ldrsh	r3, [r3]
	ldr	r2, .L29
	smlabb	r3, r0, r4, r3
	cmp	r1, r3
	ble	.L24
	ldrh	r3, [r2, #428]
	ldrsh	r0, [r2, #4]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #428]	@ movhi
	sxth	r3, r3
	cmp	r0, r3
	ite	ge
	movge	r0, #0
	movlt	r0, #1
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L24:
	movs	r0, #0
	strh	r0, [r2, #428]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L30:
	.align	2
.L29:
	.word	.LANCHOR0
	.word	-2147483393
	.word	.LANCHOR0+28
	.fnend
	.size	vad_preprocess, .-vad_preprocess
	.align	1
	.global	vad_preprocess_destroy
	.thumb
	.thumb_func
	.type	vad_preprocess_destroy, %function
vad_preprocess_destroy:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #0
	ldr	r2, .L34
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r3
	strh	r3, [r2, #10]	@ movhi
	strh	r3, [r2, #12]	@ movhi
	strh	r3, [r2, #14]	@ movhi
	strh	r3, [r2, #16]	@ movhi
	strh	r3, [r2, #18]	@ movhi
	strh	r3, [r2, #428]	@ movhi
.L32:
	ldr	r2, .L34
	movs	r1, #0
	add	r0, r2, #28
	strh	r4, [r0, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #100
	bne	.L32
	movs	r3, #32
	str	r1, [r2, #20]
	strh	r1, [r2, #6]	@ movhi
	strh	r3, [r2, #8]	@ movhi
	str	r1, [r2, #24]
	pop	{r4, pc}
.L35:
	.align	2
.L34:
	.word	.LANCHOR0
	.fnend
	.size	vad_preprocess_destroy, .-vad_preprocess_destroy
	.align	1
	.global	vad_preprocess_update_params
	.thumb
	.thumb_func
	.type	vad_preprocess_update_params, %function
vad_preprocess_update_params:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L37
	ldrsh	r3, [r3, #6]
	str	r3, [r0]
	bx	lr
.L38:
	.align	2
.L37:
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
