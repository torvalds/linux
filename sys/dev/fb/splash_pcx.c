/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@freebsd.org>
 * Copyright (c) 1999 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/fbio.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/splashreg.h>

static int splash_mode = -1;
static int splash_on = FALSE;

static int pcx_start(video_adapter_t *adp);
static int pcx_end(video_adapter_t *adp);
static int pcx_splash(video_adapter_t *adp, int on);
static int pcx_init(void *data, int sdepth);
static int pcx_draw(video_adapter_t *adp);

static splash_decoder_t pcx_decoder = {
	.name = "splash_pcx",
	.init = pcx_start,
	.term = pcx_end,
	.splash = pcx_splash,
	.data_type = SPLASH_IMAGE,
};

SPLASH_DECODER(splash_pcx, pcx_decoder);

static struct {
	int		 width;
	int		 height;
	int		 bpsl;
	int		 bpp;
	int		 planes;
	int		 zlen;
	const uint8_t	*zdata;
	uint8_t		*palette;
} pcx_info;

static int
pcx_start(video_adapter_t *adp)
{
	static int modes[] = {
		M_VGA_CG320,
		M_VESA_CG640x480,
		M_VESA_CG800x600,
		M_VESA_CG1024x768,
		-1,
	};
	video_info_t info;
	int i;

	if (pcx_decoder.data == NULL ||
	    pcx_decoder.data_size <= 0 ||
	    pcx_init(pcx_decoder.data, pcx_decoder.data_size))
		return (ENODEV);

	if (bootverbose)
		printf("splash_pcx: image good:\n"
		    "  width = %d\n"
		    "  height = %d\n"
		    "  depth = %d\n"
		    "  planes = %d\n",
		    pcx_info.width, pcx_info.height,
		    pcx_info.bpp, pcx_info.planes);

	for (i = 0; modes[i] >= 0; ++i) {
		if (vidd_get_info(adp, modes[i], &info) != 0)
			continue;
		if (bootverbose)
			printf("splash_pcx: considering mode %d:\n"
			    "  vi_width = %d\n"
			    "  vi_height = %d\n"
			    "  vi_depth = %d\n"
			    "  vi_planes = %d\n",
			    modes[i],
			    info.vi_width, info.vi_height,
			    info.vi_depth, info.vi_planes);
		if (info.vi_width >= pcx_info.width
		    && info.vi_height >= pcx_info.height
		    && info.vi_depth == pcx_info.bpp
		    && info.vi_planes == pcx_info.planes)
			break;
	}

	splash_mode = modes[i];
	if (splash_mode == -1)
		return (ENODEV);
	if (bootverbose)
		printf("splash_pcx: selecting mode %d\n", splash_mode);
	return (0);
}

static int
pcx_end(video_adapter_t *adp)
{
	/* nothing to do */
	return (0);
}

static int
pcx_splash(video_adapter_t *adp, int on)
{
	if (on) {
		if (!splash_on) {
			if (vidd_set_mode(adp, splash_mode) || pcx_draw(adp))
				return 1;
			splash_on = TRUE;
		}
		return (0);
	} else {
		splash_on = FALSE;
		return (0);
	}
}

struct pcx_header {
	uint8_t		 manufactor;
	uint8_t		 version;
	uint8_t		 encoding;
	uint8_t		 bpp;
	uint16_t	 xmin;
	uint16_t	 ymin;
	uint16_t	 xmax;
	uint16_t	 ymax;
	uint16_t	 hres;
	uint16_t	 vres;
	uint8_t		 colormap[48];
	uint8_t		 rsvd;
	uint8_t		 nplanes;
	uint16_t	 bpsl;
	uint16_t	 palinfo;
	uint16_t	 hsize;
	uint16_t	 vsize;
};

#define MAXSCANLINE 1024

static int
pcx_init(void *data, int size)
{
	const struct pcx_header *hdr = data;

	if (size < 128 + 1 + 1 + 768 ||
	    hdr->manufactor != 10 ||
	    hdr->version != 5 ||
	    hdr->encoding != 1 ||
	    hdr->nplanes != 1 ||
	    hdr->bpp != 8 ||
	    hdr->bpsl > MAXSCANLINE ||
	    ((uint8_t *)data)[size - 769] != 12) {
		printf("splash_pcx: invalid PCX image\n");
		return (1);
	}
	pcx_info.width = hdr->xmax - hdr->xmin + 1;
	pcx_info.height = hdr->ymax - hdr->ymin + 1;
	pcx_info.bpsl = hdr->bpsl;
	pcx_info.bpp = hdr->bpp;
	pcx_info.planes = hdr->nplanes;
	pcx_info.zlen = size - (128 + 1 + 768);
	pcx_info.zdata = (uint8_t *)data + 128;
	pcx_info.palette = (uint8_t *)data + size - 768;
	return (0);
}

static int
pcx_draw(video_adapter_t *adp)
{
	uint8_t *vidmem;
	int swidth, sheight, sbpsl, sdepth, splanes;
	int banksize, origin;
	int c, i, j, pos, scan, x, y;
	uint8_t line[MAXSCANLINE];

	if (pcx_info.zlen < 1)
		return (1);

	vidd_load_palette(adp, pcx_info.palette);

	vidmem = (uint8_t *)adp->va_window;
	swidth = adp->va_info.vi_width;
	sheight = adp->va_info.vi_height;
	sbpsl = adp->va_line_width;
	sdepth = adp->va_info.vi_depth;
	splanes = adp->va_info.vi_planes;
	banksize = adp->va_window_size;

	for (origin = 0; origin < sheight*sbpsl; origin += banksize) {
		vidd_set_win_org(adp, origin);
		bzero(vidmem, banksize);
	}

	x = (swidth - pcx_info.width) / 2;
	y = (sheight - pcx_info.height) / 2;
	origin = 0;
	pos = y * sbpsl + x;
	while (pos > banksize) {
		pos -= banksize;
		origin += banksize;
	}
	vidd_set_win_org(adp, origin);

	for (scan = i = 0; scan < pcx_info.height; ++scan, ++y, pos += sbpsl) {
		for (j = 0; j < pcx_info.bpsl && i < pcx_info.zlen; ++i) {
			if ((pcx_info.zdata[i] & 0xc0) == 0xc0) {
				c = pcx_info.zdata[i++] & 0x3f;
				if (i >= pcx_info.zlen)
					return (1);
			} else {
				c = 1;
			}
			if (j + c > pcx_info.bpsl)
				return (1);
			while (c--)
				line[j++] = pcx_info.zdata[i];
		}

		if (pos > banksize) {
			origin += banksize;
			pos -= banksize;
			vidd_set_win_org(adp, origin);
		}

		if (pos + pcx_info.width > banksize) {
			/* scanline crosses bank boundary */
			j = banksize - pos;
			bcopy(line, vidmem + pos, j);
			origin += banksize;
			pos -= banksize;
			vidd_set_win_org(adp, origin);
			bcopy(line + j, vidmem, pcx_info.width - j);
		} else {
			bcopy(line, vidmem + pos, pcx_info.width);
		}
	}

	return (0);
}
