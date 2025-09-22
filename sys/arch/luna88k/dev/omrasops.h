/* $OpenBSD: omrasops.h,v 1.7 2021/09/25 21:34:21 aoyama Exp $ */

/*
 * Copyright (c) 2013 Kenji Aoyama
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Helper macros
 */
#define W(addr)  ((u_int32_t *)(addr))
#define P0(addr) ((u_int32_t *)((u_int8_t *)(addr) +  0x40000))
#define P1(addr) ((u_int32_t *)((u_int8_t *)(addr) +  0x80000))
#define P2(addr) ((u_int32_t *)((u_int8_t *)(addr) +  0xC0000))
#define P3(addr) ((u_int32_t *)((u_int8_t *)(addr) + 0x100000))

/*
 * Replacement Rules (rops) (derived from hp300)
 */
#define RR_CLEAR	0x0
#define RR_COPY		0x3

/*
 * ROP function
 *
 * LUNA's frame buffer uses Hitachi HM53462 video RAM, which has raster
 * (logic) operation, or ROP, function.  To use ROP function on LUNA, write
 * a 32bit `mask' value to the specified address corresponding to each ROP
 * logic.
 *
 * D: the data writing to the video RAM
 * M: the data already stored on the video RAM
 */

/* operation		index	the video RAM contents will be */
#define ROP_ZERO	 0	/* all 0	*/
#define ROP_AND1	 1	/* D & M	*/ 
#define ROP_AND2	 2	/* ~D & M	*/
/* Not used on LUNA	 3			*/
#define ROP_AND3	 4	/* D & ~M	*/
#define ROP_THROUGH	 5	/* D		*/
#define ROP_EOR		 6	/* (~D & M) | (D & ~M)	*/
#define ROP_OR1		 7	/* D | M	*/
#define ROP_NOR		 8	/* ~D | ~M	*/
#define ROP_ENOR	 9	/* (D & M) | (~D & ~M)	*/
#define ROP_INV1	10	/* ~D		*/
#define ROP_OR2		11	/* ~D | M	*/
#define ROP_INV2	12	/* ~M		*/
#define ROP_OR3		13	/* D | ~M	*/
#define ROP_NAND	14	/* ~D | ~M	*/
#define ROP_ONE		15	/* all 1	*/
