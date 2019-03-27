/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Aleksandr Rybalko under sponsorship from the
 * FreeBSD Foundation.
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

enum vt_color_format {
	COLOR_FORMAT_BW = 0,
	COLOR_FORMAT_GRAY,
	COLOR_FORMAT_VGA,		/* Color Index. */
	COLOR_FORMAT_RGB,
	COLOR_FORMAT_ARGB,
        COLOR_FORMAT_CMYK,
        COLOR_FORMAT_HSL,
        COLOR_FORMAT_YUV,
        COLOR_FORMAT_YCbCr,
        COLOR_FORMAT_YPbPr,

        COLOR_FORMAT_MAX = 15,
};

#define NCOLORS	16

/*
 * Between console's palette and VGA's one:
 *   - blue and red are swapped (1 <-> 4)
 *   - yellow and cyan are swapped (3 <-> 6)
 */
static const int cons_to_vga_colors[NCOLORS] = {
	0,  4,  2,  6,  1,  5,  3,  7,
	8, 12, 10, 14,  9, 13, 11, 15
};

/* Helper to fill color map used by driver */
int vt_generate_cons_palette(uint32_t *palette, int format, uint32_t rmax,
    int roffset, uint32_t gmax, int goffset, uint32_t bmax, int boffset);
