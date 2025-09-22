/*	$OpenBSD: bt458reg.h,v 1.2 2003/06/02 18:53:18 jason Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Brooktree Bt451, Bt457, Bt458 register definitions
 */
#define	BT_OV0	0x00		/* overlay 0 */
#define	BT_OV1	0x01		/* overlay 1 */
#define	BT_OV2	0x02		/* overlay 2 */
#define	BT_OV3	0x03		/* overlay 3 */
#define	BT_RMR	0x04		/* read mask */
#define	BT_BMR	0x05		/* blink mask */
#define	BT_CR	0x06		/* control */
#define	BT_CTR	0x07		/* control/test */

#define	BTCR_MPLX_5		0x80	/* multiplex select, 5:1 */
#define	BTCR_MPLX_4		0x00	/* multiplex select, 4:1 */
#define	BTCR_RAMENA		0x40	/* use color palette RAM */
#define	BTCR_BLINK_M		0x30	/* blink mask */
#define	BTCR_BLINK_1648		0x00	/*  16 on, 48 off */
#define	BTCR_BLINK_1616		0x10	/*  16 on, 16 off */
#define	BTCR_BLINK_3232		0x20	/*  32 on, 32 off */
#define	BTCR_BLINK_6464		0x30	/*  64 on, 64 off */
#define	BTCR_BLINKENA_OV1	0x08	/* OV1 blink enable */
#define	BTCR_BLINKENA_OV0	0x04	/* OV0 blink enable */
#define	BTCR_DISPENA_OV1	0x02	/* OV1 display enable */
#define	BTCR_DISPENA_OV0	0x01	/* OV0 display enable */

#define	BTCTR_R_ENA		0x01	/* red channel enable */
#define	BTCTR_G_ENA		0x02	/* green channel enable */
#define	BTCTR_B_ENA		0x04	/* blue channel enable */
#define	BTCTR_NIB_M		0x08	/* nibble mask: */
#define	BTCTR_NIB_LOW		0x08	/*  low */
#define	BTCTR_NIB_HIGH		0x00	/*  high */
