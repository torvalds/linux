/**
 * Copyright (C) 2010-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

.section .text

.global hdmi_dma_copy_16_neon_lut
.global hdmi_dma_copy_16_neon_fast
.global hdmi_dma_copy_24_neon_lut
.global hdmi_dma_copy_24_neon_fast


/**
 * hdmi_dma_copy_16_neon_lut
 * Convert pcm sample to iec sample. Pcm sample is 16 bits.
 * Frame index's between 0 and 47 inclusively. Channel count can be 1, 2, 4, 8.
 * Frame count should be multipliable by 4, and Sample count by 8.
 *
 * C Prototype
 *   void hdmi_dma_copy_16_neon_lut(unsigned short *src, unsigned int *dst,
 *			int samples, unsigned char *lookup_table);
 * Return value
 *   None
 * Parameters
 *   src		Source PCM16 samples
 *   dst		Dest buffer to store pcm with header
 *   samples		Contains sample count (=frame_count * channel_count)
 *   lookup_table	Preconstructed header table. Channels interleaved.
 */

hdmi_dma_copy_16_neon_lut:
	mov     r12, #1		/* construct vector(1) */
	vdup.8 d6, r12

hdmi_dma_copy_16_neon_lut_start:

	/* get 8 samples to q0 */
	vld1.16 {d0, d1}, [r0]!	/* TODO: aligned */

	/* pld [r1, #(64*4)] */

	/* xor every bit */
	vcnt.8     q1, q0	/* count of 1s */
	vpadd.i8 d2, d2, d3	/* only care about the LST in every element */
	vand       d2, d2, d6	/* clear other bits while keep the least bit */
	vshl.u8    d2, d2, #3	/* bit p: d2 = d2 << 3 */

	/* get packet header */
	vld1.8       {d5}, [r3]!
	veor        d4, d5, d2	/* xor bit c */

	/* store: (d4 << 16 | q0) << 8 */
	vmovl.u8  q2, d4	/* expand from char to short */
	vzip.16     q0, q2
	vshl.u32   q0, q0, #8
	vshl.u32   q1, q2, #8
	vst1.32     {d0, d1, d2, d3}, [r1]!

	/* decrease sample count */
	subs  r2, r2, #8
	bne   hdmi_dma_copy_16_neon_lut_start

	mov pc, lr

/**
 * hdmi_dma_copy_16_neon_fast
 * Convert pcm sample to iec sample. Pcm sample is 16 bits.
 * Frame index's between 48 and 191 inclusively.
 * Channel count can be 1, 2, 4 or 8.
 * Frame count should be multipliable by 4, and Sample count by 8.
 *
 * C Prototype
 * void hdmi_dma_copy_16_neon_fast(unsigned short *src,
 * 		unsigned int *dst, int samples);
 * Return value
 *   None
 * Parameters
 *   src		Source PCM16 samples
 *   dst		Dest buffer to store pcm with header
 *   samples		Contains sample count (=frame_count * channel_count)
 */

hdmi_dma_copy_16_neon_fast:
	mov     r12, #1		/* construct vector(1) */
	vdup.8 d6, r12

hdmi_dma_copy_16_neon_fast_start:
	/* get 8 samples to q0 */
	vld1.16 {d0, d1}, [r0]!	/* TODO: aligned */

	/* pld [r1, #(64*4)] */

	/* xor every bit */
	vcnt.8     q1, q0	/* count of 1s */
	vpadd.i8 d2, d2, d3
	vand       d2, d2, d6	/* clear other bits while keep the LST */
	/* finally we construct packet header */
	vshl.u8    d4, d2, #3	/* bit p: d2 = d2 << 3 */

	/* get packet header: always 0 */

	/* store: (d4 << 16 | q0) << 8 */
	vmovl.u8  q2, d4	/* expand from char to short */
	vzip.16     q0, q2
	vshl.u32   q0, q0, #8
	vshl.u32   q1, q2, #8
	vst1.32     {d0, d1, d2, d3}, [r1]!

	/* decrease sample count */
	subs  r2, r2, #8
	bne   hdmi_dma_copy_16_neon_fast_start

	mov pc, lr



/**
 * hdmi_dma_copy_24_neon_lut
 * Convert pcm sample to iec sample. Pcm sample is 24 bits.
 * Frame index's between 0 and 47 inclusively. Channel count can be 1, 2, 4, 8.
 * Frame count should be multipliable by 4, and Sample count by 8.
 *
 * C Prototype
 *   void hdmi_dma_copy_24_neon_lut(unsigned int *src, unsigned int *dst,
 *			int samples, unsigned char *lookup_table);
 * Return value
 *   None
 * Parameters
 *   src		Source PCM24 samples
 *   dst		Dest buffer to store pcm with header
 *   samples		Contains sample count (=frame_count * channel_count)
 *   lookup_table	Preconstructed header table. Channels interleaved.
 */

hdmi_dma_copy_24_neon_lut:
	vpush   {d8}

	mov     r12, #1		/* construct vector(1) */
	vdup.8 d8, r12

hdmi_dma_copy_24_neon_lut_start:

	/* get 8 samples to q0 and q1 */
	vld1.32 {d0, d1, d2, d3}, [r0]! /* TODO: aligned */

	/* pld [r1, #(64*4)] */

	/* xor every bit */
	vcnt.8     q2, q0	/* count of 1s */
	vpadd.i8 d4, d4, d5	/* only care about the LSB in every element */
	vcnt.8     q3, q1
	vpadd.i8 d6, d6, d7
	vpadd.i8 d4, d4, d6	/* d4: contains xor result and other dirty bits */
	vand       d4, d4, d8	/* clear other bits while keep the least bit */
	vshl.u8    d4, d4, #3	/* bit p: d4 = d4 << 3 */

	/* get packet header */
	vld1.8       {d5}, [r3]!/* d5: original header */
	veor        d5, d5, d4	/* fix bit p */

	/* store: (d5 << 24 | q0) */
	vmovl.u8  q3, d5	/* expand from char to short */
	vmovl.u16 q2, d6	/* expand from short to int */
	vmovl.u16 q3, d7
	vshl.u32    q2, q2, #24
	vshl.u32    q3, q3, #24
	vorr          q0, q0, q2
	vorr          q1, q1, q3
	vst1.32     {d0, d1, d2, d3}, [r1]!

	/* decrease sample count */
	subs  r2, r2, #8
	bne   hdmi_dma_copy_24_neon_lut_start

	vpop {d8}
	mov pc, lr

/**
 * hdmi_dma_copy_24_neon_fast
 * Convert pcm sample to iec sample. Pcm sample is 24 bits.
 * Frame index's between 48 and 191 inclusively.
 * Channel count can be 1, 2, 4 or 8.
 * Frame count should be multipliable by 4, and Sample count by 8.
 *
 * C Prototype
 * void hdmi_dma_copy_24_neon_fast(unsigned int *src,
 * 		unsigned int *dst, int samples);
 * Return value
 *   None
 * Parameters
 *   src		Source PCM24 samples
 *   dst		Dest buffer to store pcm with header
 *   samples		Contains sample count (=frame_count * channel_count)
 */

hdmi_dma_copy_24_neon_fast:
	vpush   {d8}

	mov     r12, #1		/* construct vector(1) */
	vdup.8 d8, r12

hdmi_dma_copy_24_neon_fast_start:
	/* get 8 samples to q0 and q1 */
	vld1.32 {d0, d1, d2, d3}, [r0]! /* TODO: aligned */

	/* pld [r1, #(64*4)] */

	/* xor every bit */
	vcnt.8     q2, q0	/* count of 1s */
	vpadd.i8 d4, d4, d5	/* only care about the LSB in every element */
	vcnt.8     q3, q1
	vpadd.i8 d6, d6, d7
	vpadd.i8 d4, d4, d6	/* d4: contains xor result and other dirty bits */
	vand       d4, d4, d8	/* clear other bits while keep the least bit */
	vshl.u8    d4, d4, #3	/* bit p: d4 = d4 << 3 */

	/* store: (d4 << 24 | q0)  */
	vmovl.u8  q3, d4	/* expand from char to short */
	vmovl.u16 q2, d6	/* expand from short to int */
	vmovl.u16 q3, d7
	vshl.u32    q2, q2, #24
	vshl.u32    q3, q3, #24
	vorr          q0, q0, q2
	vorr          q1, q1, q3
	vst1.32     {d0, d1, d2, d3}, [r1]!

	/* decrease sample count */
	subs  r2, r2, #8
	bne   hdmi_dma_copy_24_neon_fast_start

	vpop {d8}
	mov pc, lr
