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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/libkern.h>

#include <dev/vt/colors/vt_termcolors.h>

static struct {
	unsigned char r;	/* Red percentage value. */
	unsigned char g;	/* Green percentage value. */
	unsigned char b;	/* Blue percentage value. */
} color_def[NCOLORS] = {
	{0,	0,	0},	/* black */
	{50,	0,	0},	/* dark red */
	{0,	50,	0},	/* dark green */
	{77,	63,	0},	/* dark yellow */
	{20,	40,	64},	/* dark blue */
	{50,	0,	50},	/* dark magenta */
	{0,	50,	50},	/* dark cyan */
	{75,	75,	75},	/* light gray */

	{18,	20,	21},	/* dark gray */
	{100,	0,	0},	/* light red */
	{0,	100,	0},	/* light green */
	{100,	100,	0},	/* light yellow */
	{45,	62,	81},	/* light blue */
	{100,	0,	100},	/* light magenta */
	{0,	100,	100},	/* light cyan */
	{100,	100,	100},	/* white */
};

static int
vt_parse_rgb_triplet(const char *rgb, unsigned char *r,
    unsigned char *g, unsigned char *b)
{
	unsigned long v;
	const char *ptr;
	char *endptr;

	ptr = rgb;

	/* Handle #rrggbb case */
	if (*ptr == '#') {
		if (strlen(ptr) != 7)
			return (-1);
		v = strtoul(ptr + 1, &endptr, 16);
		if (*endptr != '\0')
			return (-1);

		*r = (v >> 16) & 0xff;
		*g = (v >>  8) & 0xff;
		*b = v & 0xff;

		return (0);
	}

	/* "r, g, b" case */
	v = strtoul(ptr, &endptr, 10);
	if (ptr == endptr)
		return (-1);
	if (v > 255)
		return (-1);
	*r = v & 0xff;
	ptr = endptr;

	/* skip separator */
	while (*ptr == ',' || *ptr == ' ')
		ptr++;

	v = strtoul(ptr, &endptr, 10);
	if (ptr == endptr)
		return (-1);
	if (v > 255)
		return (-1);
	*g = v & 0xff;
	ptr = endptr;

	/* skip separator */
	while (*ptr == ',' || *ptr == ' ')
		ptr++;

	v = strtoul(ptr, &endptr, 10);
	if (ptr == endptr)
		return (-1);
	if (v > 255)
		return (-1);
	*b = v & 0xff;
	ptr = endptr;

	/* skip trailing spaces */
	while (*ptr == ' ')
		ptr++;

	/* unexpected characters at the end of the string */
	if (*ptr != 0)
		return (-1);

	return (0);
}

static void
vt_palette_init(void)
{
	int i;
	char rgb[32];
	char tunable[32];
	unsigned char r, g, b;

	for (i = 0; i < NCOLORS; i++) {
		snprintf(tunable, sizeof(tunable),
		    "kern.vt.color.%d.rgb", i);
		if (TUNABLE_STR_FETCH(tunable, rgb, sizeof(rgb))) {
			if (vt_parse_rgb_triplet(rgb, &r, &g, &b) == 0) {
				/* convert to percentages */
				color_def[i].r = r*100/255;
				color_def[i].g = g*100/255;
				color_def[i].b = b*100/255;
			}
		}
	}
}

int
vt_generate_cons_palette(uint32_t *palette, int format, uint32_t rmax,
    int roffset, uint32_t gmax, int goffset, uint32_t bmax, int boffset)
{
	int i;

	switch (format) {
	case COLOR_FORMAT_VGA:
		for (i = 0; i < NCOLORS; i++)
			palette[i] = cons_to_vga_colors[i];
		break;
	case COLOR_FORMAT_RGB:
		vt_palette_init();
#define	CF(_f, _i) ((_f ## max * color_def[(_i)]._f / 100) << _f ## offset)
		for (i = 0; i < NCOLORS; i++)
			palette[i] = CF(r, i) | CF(g, i) | CF(b, i);
#undef	CF
		break;
	default:
		return (ENODEV);
	}

	return (0);
}
