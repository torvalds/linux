/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip VAD Preprocess
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

	.arch armv8-a
	.file	"vad_preprocess_arm64.S"
	.text
	.align	2
	.global	vad_preprocess_init
	.type	vad_preprocess_init, %function
vad_preprocess_init:
	adrp	x2, .LANCHOR0
	add	x1, x2, :lo12:.LANCHOR0
	ldr	w3, [x0, 8]
	strh	w3, [x2, #:lo12:.LANCHOR0]
	ldr	w2, [x0, 4]
	strh	w2, [x1, 2]
	ldr	w2, [x0, 12]
	strh	w2, [x1, 4]
	ldr	w2, [x0]
	strh	w2, [x1, 6]
	ldr	w2, [x0, 16]
	and	w0, w2, 511
	tbz	x2, 9, .L2
	mvn	w0, w0
.L2:
	strh	w0, [x1, 8]
	ret
	.size	vad_preprocess_init, .-vad_preprocess_init
	.align	2
	.global	vad_preprocess
	.type	vad_preprocess, %function
vad_preprocess:
	adrp	x4, .LANCHOR0
	add	x2, x4, :lo12:.LANCHOR0
	mov	w8, 15349
	ldrsh	w1, [x2, 8]
	ldrsh	w7, [x2, 10]
	ldrsh	w6, [x2, 12]
	ldrsh	w3, [x2, 16]
	mul	w0, w1, w0
	mov	w1, 32
	sdiv	w0, w0, w1
	ldrsh	w1, [x2, 14]
	mov	w2, -30697
	mul	w2, w7, w2
	mul	w5, w0, w8
	sxtw	x2, w2
	add	x2, x2, x5, sxtw
	smaddl	x3, w3, w8, x2
	mov	w2, -30632
	smsubl	x2, w6, w2, x3
	mov	w3, 14379
	smsubl	x1, w1, w3, x2
	cmp	x1, 0
	ble	.L5
	add	x1, x1, 8192
	asr	x1, x1, 14
.L6:
	add	x2, x4, :lo12:.LANCHOR0
	sxth	w1, w1
	cmp	w1, 0
	ldrh	w3, [x2, 18]
	strh	w0, [x2, 10]
	add	w3, w3, 1
	ldr	w0, [x2, 20]
	sxth	w3, w3
	strh	w1, [x2, 12]
	csneg	w1, w1, w1, ge
	strh	w3, [x2, 18]
	negs	w5, w3
	add	w0, w1, w0
	strh	w7, [x2, 16]
	and	w3, w3, 255
	strh	w6, [x2, 14]
	and	w5, w5, 255
	str	w0, [x2, 20]
	csneg	w3, w3, w5, mi
	cbnz	w3, .L7
	ldr	w3, [x2, 24]
	cmp	w3, 99
	bgt	.L8
	add	w0, w0, 128
	mov	w5, 256
	add	x2, x2, 32
	sdiv	w0, w0, w5
	strh	w0, [x2, w3, sxtw 1]
.L9:
	add	x2, x4, :lo12:.LANCHOR0
	cmp	w3, 99
	ldrsh	w0, [x2, 32]
	bgt	.L11
	add	x2, x2, 32
	mov	x5, 0
.L12:
	add	x5, x5, 1
	cmp	w3, w5
	bgt	.L13
.L14:
	add	x2, x4, :lo12:.LANCHOR0
	mov	w6, 230
	add	w3, w3, 1
	ldrsh	w5, [x2, 6]
	strh	wzr, [x2, 18]
	stp	wzr, w3, [x2, 20]
	mul	w5, w5, w6
	mov	w6, 26
	add	w5, w5, 128
	madd	w0, w0, w6, w5
	mov	w5, 256
	sdiv	w0, w0, w5
	strh	w0, [x2, 6]
.L7:
	add	x0, x4, :lo12:.LANCHOR0
	ldrsh	w3, [x4, #:lo12:.LANCHOR0]
	ldrsh	w2, [x0, 6]
	ldrsh	w5, [x0, 2]
	madd	w2, w2, w5, w3
	cmp	w1, w2
	ble	.L16
	ldrh	w1, [x0, 432]
	add	w1, w1, 1
	sxth	w1, w1
	strh	w1, [x0, 432]
	ldrsh	w0, [x0, 4]
	cmp	w0, w1
	cset	w0, lt
	ret
.L5:
	sub	x1, x1, #8192
	mov	x2, 16384
	sdiv	x1, x1, x2
	b	.L6
.L8:
	add	x5, x2, 34
	add	x2, x2, 232
.L10:
	ldrh	w6, [x5]
	add	x5, x5, 2
	strh	w6, [x5, -4]
	cmp	x2, x5
	bne	.L10
	add	w0, w0, 128
	mov	w5, 256
	add	x2, x4, :lo12:.LANCHOR0
	sdiv	w0, w0, w5
	strh	w0, [x2, 230]
	b	.L9
.L13:
	lsl	x6, x5, 1
	ldrsh	w7, [x6, x2]
	ldrh	w6, [x6, x2]
	cmp	w7, w0
	csel	w0, w6, w0, le
	sxth	w0, w0
	b	.L12
.L11:
	add	x5, x2, 34
	add	x2, x2, 232
.L15:
	ldrsh	w7, [x5]
	ldrh	w6, [x5], 2
	cmp	w7, w0
	csel	w0, w6, w0, le
	cmp	x2, x5
	sxth	w0, w0
	bne	.L15
	b	.L14
.L16:
	strh	wzr, [x0, 432]
	mov	w0, 0
	ret
	.size	vad_preprocess, .-vad_preprocess
	.align	2
	.global	vad_preprocess_destroy
	.type	vad_preprocess_destroy, %function
vad_preprocess_destroy:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x2, x0, 32
	mov	x1, 0
	strh	wzr, [x0, 10]
	strh	wzr, [x0, 16]
	strh	wzr, [x0, 12]
	strh	wzr, [x0, 14]
	strh	wzr, [x0, 18]
	strh	wzr, [x0, 432]
.L21:
	strh	wzr, [x1, x2]
	add	x1, x1, 2
	cmp	x1, 200
	bne	.L21
	mov	w1, 32
	strh	wzr, [x0, 6]
	strh	w1, [x0, 8]
	stp	wzr, wzr, [x0, 20]
	ret
	.size	vad_preprocess_destroy, .-vad_preprocess_destroy
	.align	2
	.global	vad_preprocess_update_params
	.type	vad_preprocess_update_params, %function
vad_preprocess_update_params:
	adrp	x1, .LANCHOR0+6
	ldrsh	w1, [x1, #:lo12:.LANCHOR0+6]
	str	w1, [x0]
	ret
	.size	vad_preprocess_update_params, .-vad_preprocess_update_params
	.bss
	.align	3
	.set	.LANCHOR0,. + 0
	.type	g_sound_thd, %object
	.size	g_sound_thd, 2
g_sound_thd:
	.zero	2
	.type	g_noise_level, %object
	.size	g_noise_level, 2
g_noise_level:
	.zero	2
	.type	g_vad_con_thd, %object
	.size	g_vad_con_thd, 2
g_vad_con_thd:
	.zero	2
	.type	g_noise_abs, %object
	.size	g_noise_abs, 2
g_noise_abs:
	.zero	2
	.type	g_signal_gain, %object
	.size	g_signal_gain, 2
g_signal_gain:
	.zero	2
	.type	g_xn_1, %object
	.size	g_xn_1, 2
g_xn_1:
	.zero	2
	.type	g_yn_1, %object
	.size	g_yn_1, 2
g_yn_1:
	.zero	2
	.type	g_yn_2, %object
	.size	g_yn_2, 2
g_yn_2:
	.zero	2
	.type	g_xn_2, %object
	.size	g_xn_2, 2
g_xn_2:
	.zero	2
	.type	g_sample_cnt, %object
	.size	g_sample_cnt, 2
g_sample_cnt:
	.zero	2
	.type	g_sum_abs_frm, %object
	.size	g_sum_abs_frm, 4
g_sum_abs_frm:
	.zero	4
	.type	frm_count, %object
	.size	frm_count, 4
frm_count:
	.zero	4
	.zero	4
	.type	g_ave_abs_rec, %object
	.size	g_ave_abs_rec, 400
g_ave_abs_rec:
	.zero	400
	.type	g_vad_cnt, %object
	.size	g_vad_cnt, 2
g_vad_cnt:
	.zero	2
	.ident	"GCC: (Linaro GCC 6.3-2017.05) 6.3.1 20170404"
	.section	.note.GNU-stack,"",@progbits
