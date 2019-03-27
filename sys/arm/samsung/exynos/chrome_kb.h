/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <dev/ofw/openfirm.h>

void ckb_ec_intr(void *);

#define	KEYMAP_LEN	75

pcell_t default_keymap[KEYMAP_LEN] = {
	0x0001007d, /* lmeta */
	0x0002003b, /* F1 */
	0x00030030, /* B */
	0x00040044, /* F10 */
	0x00060031, /* N */
	0x0008000d, /* = */
	0x000a0064, /* ralt */

	0x01010001, /* escape */
	0x0102003e, /* F4 */
	0x01030022, /* G */
	0x01040041, /* F7 */
	0x01060023, /* H */
	0x01080028, /* ' */
	0x01090043, /* F9 */
	0x010b000e, /* backspace */

	0x0200001d, /* lctrl */
	0x0201000f, /* tab */
	0x0202003d, /* F3 */
	0x02030014, /* t */
	0x02040040, /* F6 */
	0x0205001b, /* ] */
	0x02060015, /* y */
	0x02070056, /* 102nd */
	0x0208001a, /* [ */
	0x02090042, /* F8 */

	0x03010029, /* grave */
	0x0302003c, /* F2 */
	0x03030006, /* 5 */
	0x0304003f, /* F5 */
	0x03060007, /* 6 */
	0x0308000c, /* - */
	0x030b002b, /* \ */

	0x04000061, /* rctrl */
	0x0401001e, /* a */
	0x04020020, /* d */
	0x04030021, /* f */
	0x0404001f, /* s */
	0x04050025, /* k */
	0x04060024, /* j */
	0x04080027, /* ; */
	0x04090026, /* l */
	0x040a002b, /* \ */
	0x040b001c, /* enter */

	0x0501002c, /* z */
	0x0502002e, /* c */
	0x0503002f, /* v */
	0x0504002d, /* x */
	0x05050033, /* , */
	0x05060032, /* m */
	0x0507002a, /* lsh */
	0x05080035, /* / */
	0x05090034, /* . */
	0x050B0039, /* space */

	0x06010002, /* 1 */
	0x06020004, /* 3 */
	0x06030005, /* 4 */
	0x06040003, /* 2 */
	0x06050009, /* 8 */
	0x06060008, /* 7 */
	0x0608000b, /* 0 */
	0x0609000a, /* 9 */
	0x060a0038, /* lalt */
	0x060b0064, /* down */
	0x060c0062, /* right */

	0x07010010, /* q */
	0x07020012, /* e */
	0x07030013, /* r */
	0x07040011, /* w */
	0x07050017, /* i */
	0x07060016, /* u */
	0x07070036, /* rsh */
	0x07080019, /* p */
	0x07090018, /* o */
	0x070b005F, /* up */
	0x070c0061, /* left */
};
