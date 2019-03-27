/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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

#define	IOMUXC(n)	(n * 0x04)
#define	IOMUXCN		135
#define	IOMUXC_PTA6	0x000	/* Software MUX Pad Control Register 0 */
#define	IOMUXC_PTA8	0x004	/* Software MUX Pad Control Register 1 */
#define	IOMUXC_PTA9	0x008	/* Software MUX Pad Control Register 2 */
#define	IOMUXC_PTA10	0x00C	/* Software MUX Pad Control Register 3 */
#define	IOMUXC_PTA11	0x010	/* Software MUX Pad Control Register 4 */
#define	IOMUXC_PTA12	0x014	/* Software MUX Pad Control Register 5 */
#define	IOMUXC_PTA16	0x018	/* Software MUX Pad Control Register 6 */
#define	IOMUXC_PTA17	0x01C	/* Software MUX Pad Control Register 7 */
#define	IOMUXC_PTA18	0x020	/* Software MUX Pad Control Register 8 */
#define	IOMUXC_PTA19	0x024	/* Software MUX Pad Control Register 9 */
#define	IOMUXC_PTA20	0x028	/* Software MUX Pad Control Register 10 */
#define	IOMUXC_PTA21	0x02C	/* Software MUX Pad Control Register 11 */
#define	IOMUXC_PTA22	0x030	/* Software MUX Pad Control Register 12 */
#define	IOMUXC_PTA23	0x034	/* Software MUX Pad Control Register 13 */
#define	IOMUXC_PTA24	0x038	/* Software MUX Pad Control Register 14 */
#define	IOMUXC_PTA25	0x03C	/* Software MUX Pad Control Register 15 */
#define	IOMUXC_PTA26	0x040	/* Software MUX Pad Control Register 16 */
#define	IOMUXC_PTA27	0x044	/* Software MUX Pad Control Register 17 */
#define	IOMUXC_PTA28	0x048	/* Software MUX Pad Control Register 18 */
#define	IOMUXC_PTA29	0x04C	/* Software MUX Pad Control Register 19 */
#define	IOMUXC_PTA30	0x050	/* Software MUX Pad Control Register 20 */
#define	IOMUXC_PTA31	0x054	/* Software MUX Pad Control Register 21 */
#define	IOMUXC_PTB0	0x058	/* Software MUX Pad Control Register 22 */
#define	IOMUXC_PTB1	0x05C	/* Software MUX Pad Control Register 23 */
#define	IOMUXC_PTB2	0x060	/* Software MUX Pad Control Register 24 */
#define	IOMUXC_PTB3	0x064	/* Software MUX Pad Control Register 25 */
#define	IOMUXC_PTB4	0x068	/* Software MUX Pad Control Register 26 */
#define	IOMUXC_PTB5	0x06C	/* Software MUX Pad Control Register 27 */
#define	IOMUXC_PTB6	0x070	/* Software MUX Pad Control Register 28 */
#define	IOMUXC_PTB7	0x074	/* Software MUX Pad Control Register 29 */
#define	IOMUXC_PTB8	0x078	/* Software MUX Pad Control Register 30 */
#define	IOMUXC_PTB9	0x07C	/* Software MUX Pad Control Register 31 */
#define	IOMUXC_PTB10	0x080	/* Software MUX Pad Control Register 32 */
#define	IOMUXC_PTB11	0x084	/* Software MUX Pad Control Register 33 */
#define	IOMUXC_PTB12	0x088	/* Software MUX Pad Control Register 34 */
#define	IOMUXC_PTB13	0x08C	/* Software MUX Pad Control Register 35 */
#define	IOMUXC_PTB14	0x090	/* Software MUX Pad Control Register 36 */
#define	IOMUXC_PTB15	0x094	/* Software MUX Pad Control Register 37 */
#define	IOMUXC_PTB16	0x098	/* Software MUX Pad Control Register 38 */
#define	IOMUXC_PTB17	0x09C	/* Software MUX Pad Control Register 39 */
#define	IOMUXC_PTB18	0x0A0	/* Software MUX Pad Control Register 40 */
#define	IOMUXC_PTB19	0x0A4	/* Software MUX Pad Control Register 41 */
#define	IOMUXC_PTB20	0x0A8	/* Software MUX Pad Control Register 42 */
#define	IOMUXC_PTB21	0x0AC	/* Software MUX Pad Control Register 43 */
#define	IOMUXC_PTB22	0x0B0	/* Software MUX Pad Control Register 44 */
#define	IOMUXC_PTC0	0x0B4	/* Software MUX Pad Control Register 45 */
#define	IOMUXC_PTC1	0x0B8	/* Software MUX Pad Control Register 46 */
#define	IOMUXC_PTC2	0x0BC	/* Software MUX Pad Control Register 47 */
#define	IOMUXC_PTC3	0x0C0	/* Software MUX Pad Control Register 48 */
#define	IOMUXC_PTC4	0x0C4	/* Software MUX Pad Control Register 49 */
#define	IOMUXC_PTC5	0x0C8	/* Software MUX Pad Control Register 50 */
#define	IOMUXC_PTC6	0x0CC	/* Software MUX Pad Control Register 51 */
#define	IOMUXC_PTC7	0x0D0	/* Software MUX Pad Control Register 52 */
#define	IOMUXC_PTC8	0x0D4	/* Software MUX Pad Control Register 53 */
#define	IOMUXC_PTC9	0x0D8	/* Software MUX Pad Control Register 54 */
#define	IOMUXC_PTC10	0x0DC	/* Software MUX Pad Control Register 55 */
#define	IOMUXC_PTC11	0x0E0	/* Software MUX Pad Control Register 56 */
#define	IOMUXC_PTC12	0x0E4	/* Software MUX Pad Control Register 57 */
#define	IOMUXC_PTC13	0x0E8	/* Software MUX Pad Control Register 58 */
#define	IOMUXC_PTC14	0x0EC	/* Software MUX Pad Control Register 59 */
#define	IOMUXC_PTC15	0x0F0	/* Software MUX Pad Control Register 60 */
#define	IOMUXC_PTC16	0x0F4	/* Software MUX Pad Control Register 61 */
#define	IOMUXC_PTC17	0x0F8	/* Software MUX Pad Control Register 62 */
#define	IOMUXC_PTD31	0x0FC	/* Software MUX Pad Control Register 63 */
#define	IOMUXC_PTD30	0x100	/* Software MUX Pad Control Register 64 */
#define	IOMUXC_PTD29	0x104	/* Software MUX Pad Control Register 65 */
#define	IOMUXC_PTD28	0x108	/* Software MUX Pad Control Register 66 */
#define	IOMUXC_PTD27	0x10C	/* Software MUX Pad Control Register 67 */
#define	IOMUXC_PTD26	0x110	/* Software MUX Pad Control Register 68 */
#define	IOMUXC_PTD25	0x114	/* Software MUX Pad Control Register 69 */
#define	IOMUXC_PTD24	0x118	/* Software MUX Pad Control Register 70 */
#define	IOMUXC_PTD23	0x11C	/* Software MUX Pad Control Register 71 */
#define	IOMUXC_PTD22	0x120	/* Software MUX Pad Control Register 72 */
#define	IOMUXC_PTD21	0x124	/* Software MUX Pad Control Register 73 */
#define	IOMUXC_PTD20	0x128	/* Software MUX Pad Control Register 74 */
#define	IOMUXC_PTD19	0x12C	/* Software MUX Pad Control Register 75 */
#define	IOMUXC_PTD18	0x130	/* Software MUX Pad Control Register 76 */
#define	IOMUXC_PTD17	0x134	/* Software MUX Pad Control Register 77 */
#define	IOMUXC_PTD16	0x138	/* Software MUX Pad Control Register 78 */
#define	IOMUXC_PTD0	0x13C	/* Software MUX Pad Control Register 79 */
#define	IOMUXC_PTD1	0x140	/* Software MUX Pad Control Register 80 */
#define	IOMUXC_PTD2	0x144	/* Software MUX Pad Control Register 81 */
#define	IOMUXC_PTD3	0x148	/* Software MUX Pad Control Register 82 */
#define	IOMUXC_PTD4	0x14C	/* Software MUX Pad Control Register 83 */
#define	IOMUXC_PTD5	0x150	/* Software MUX Pad Control Register 84 */
#define	IOMUXC_PTD6	0x154	/* Software MUX Pad Control Register 85 */
#define	IOMUXC_PTD7	0x158	/* Software MUX Pad Control Register 86 */
#define	IOMUXC_PTD8	0x15C	/* Software MUX Pad Control Register 87 */
#define	IOMUXC_PTD9	0x160	/* Software MUX Pad Control Register 88 */
#define	IOMUXC_PTD10	0x164	/* Software MUX Pad Control Register 89 */
#define	IOMUXC_PTD11	0x168	/* Software MUX Pad Control Register 90 */
#define	IOMUXC_PTD12	0x16C	/* Software MUX Pad Control Register 91 */
#define	IOMUXC_PTD13	0x170	/* Software MUX Pad Control Register 92 */
#define	IOMUXC_PTB23	0x174	/* Software MUX Pad Control Register 93 */
#define	IOMUXC_PTB24	0x178	/* Software MUX Pad Control Register 94 */
#define	IOMUXC_PTB25	0x17C	/* Software MUX Pad Control Register 95 */
#define	IOMUXC_PTB26	0x180	/* Software MUX Pad Control Register 96 */
#define	IOMUXC_PTB27	0x184	/* Software MUX Pad Control Register 97 */
#define	IOMUXC_PTB28	0x188	/* Software MUX Pad Control Register 98 */
#define	IOMUXC_PTC26	0x18C	/* Software MUX Pad Control Register 99 */
#define	IOMUXC_PTC27	0x190	/* Software MUX Pad Control Register 100 */
#define	IOMUXC_PTC28	0x194	/* Software MUX Pad Control Register 101 */
#define	IOMUXC_PTC29	0x198	/* Software MUX Pad Control Register 102 */
#define	IOMUXC_PTC30	0x19C	/* Software MUX Pad Control Register 103 */
#define	IOMUXC_PTC31	0x1A0	/* Software MUX Pad Control Register 104 */
#define	IOMUXC_PTE0	0x1A4	/* Software MUX Pad Control Register 105 */
#define	IOMUXC_PTE1	0x1A8	/* Software MUX Pad Control Register 106 */
#define	IOMUXC_PTE2	0x1AC	/* Software MUX Pad Control Register 107 */
#define	IOMUXC_PTE3	0x1B0	/* Software MUX Pad Control Register 108 */
#define	IOMUXC_PTE4	0x1B4	/* Software MUX Pad Control Register 109 */
#define	IOMUXC_PTE5	0x1B8	/* Software MUX Pad Control Register 110 */
#define	IOMUXC_PTE6	0x1BC	/* Software MUX Pad Control Register 111 */
#define	IOMUXC_PTE7	0x1C0	/* Software MUX Pad Control Register 112 */
#define	IOMUXC_PTE8	0x1C4	/* Software MUX Pad Control Register 113 */
#define	IOMUXC_PTE9	0x1C8	/* Software MUX Pad Control Register 114 */
#define	IOMUXC_PTE10	0x1CC	/* Software MUX Pad Control Register 115 */
#define	IOMUXC_PTE11	0x1D0	/* Software MUX Pad Control Register 116 */
#define	IOMUXC_PTE12	0x1D4	/* Software MUX Pad Control Register 117 */
#define	IOMUXC_PTE13	0x1D8	/* Software MUX Pad Control Register 118 */
#define	IOMUXC_PTE14	0x1DC	/* Software MUX Pad Control Register 119 */
#define	IOMUXC_PTE15	0x1E0	/* Software MUX Pad Control Register 120 */
#define	IOMUXC_PTE16	0x1E4	/* Software MUX Pad Control Register 121 */
#define	IOMUXC_PTE17	0x1E8	/* Software MUX Pad Control Register 122 */
#define	IOMUXC_PTE18	0x1EC	/* Software MUX Pad Control Register 123 */
#define	IOMUXC_PTE19	0x1F0	/* Software MUX Pad Control Register 124 */
#define	IOMUXC_PTE20	0x1F4	/* Software MUX Pad Control Register 125 */
#define	IOMUXC_PTE21	0x1F8	/* Software MUX Pad Control Register 126 */
#define	IOMUXC_PTE22	0x1FC	/* Software MUX Pad Control Register 127 */
#define	IOMUXC_PTE23	0x200	/* Software MUX Pad Control Register 128 */
#define	IOMUXC_PTE24	0x204	/* Software MUX Pad Control Register 129 */
#define	IOMUXC_PTE25	0x208	/* Software MUX Pad Control Register 130 */
#define	IOMUXC_PTE26	0x20C	/* Software MUX Pad Control Register 131 */
#define	IOMUXC_PTE27	0x210	/* Software MUX Pad Control Register 132 */
#define	IOMUXC_PTE28	0x214	/* Software MUX Pad Control Register 133 */
#define	IOMUXC_PTA7	0x218	/* Software MUX Pad Control Register 134 */
