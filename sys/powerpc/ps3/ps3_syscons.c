/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2014 Nathan Whitehorn
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/limits.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/fbio.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/platform.h>

#include <dev/ofw/openfirm.h>
#include <dev/vt/vt.h>
#include <dev/vt/hw/fb/vt_fb.h>
#include <dev/vt/colors/vt_termcolors.h>

#include "ps3-hvcall.h"

#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_MODE_SET	0x0100
#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC		0x0101
#define  L1GPU_DISPLAY_SYNC_HSYNC			1
#define  L1GPU_DISPLAY_SYNC_VSYNC			2
#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP		0x0102

static vd_init_t ps3fb_init;
static vd_probe_t ps3fb_probe;
void ps3fb_remap(void);

struct ps3fb_softc {
	struct fb_info	fb_info;

	uint64_t	sc_fbhandle;
	uint64_t	sc_fbcontext;
	uint64_t	sc_dma_control;
	uint64_t	sc_driver_info;
	uint64_t	sc_reports;
	uint64_t	sc_reports_size;
};

static struct vt_driver vt_ps3fb_driver = {
	.vd_name = "ps3fb",
	.vd_probe = ps3fb_probe,
	.vd_init = ps3fb_init,
	.vd_blank = vt_fb_blank,
	.vd_bitblt_text = vt_fb_bitblt_text,
	.vd_bitblt_bmp = vt_fb_bitblt_bitmap,
	.vd_drawrect = vt_fb_drawrect,
	.vd_setpixel = vt_fb_setpixel,
	.vd_fb_ioctl = vt_fb_ioctl,
	.vd_fb_mmap = vt_fb_mmap,
	/* Better than VGA, but still generic driver. */
	.vd_priority = VD_PRIORITY_GENERIC + 1,
};

VT_DRIVER_DECLARE(vt_ps3fb, vt_ps3fb_driver);
static struct ps3fb_softc ps3fb_softc;

static int
ps3fb_probe(struct vt_device *vd)
{
	struct ps3fb_softc *sc;
	int disable;
	char compatible[64];
	phandle_t root;

	disable = 0;
	TUNABLE_INT_FETCH("hw.syscons.disable", &disable);
	if (disable != 0)
		return (0);

	sc = &ps3fb_softc;

	TUNABLE_STR_FETCH("hw.platform", compatible, sizeof(compatible));
	if (strcmp(compatible, "ps3") == 0)
		return (CN_INTERNAL);

	root = OF_finddevice("/");
	if (OF_getprop(root, "compatible", compatible, sizeof(compatible)) <= 0)
                return (CN_DEAD);
	
	if (strncmp(compatible, "sony,ps3", sizeof(compatible)) != 0)
		return (CN_DEAD);

	return (CN_INTERNAL);
}

void
ps3fb_remap(void)
{
	struct ps3fb_softc *sc;
	vm_offset_t va, fb_paddr;

	sc = &ps3fb_softc;

	lv1_gpu_close();
	lv1_gpu_open(0);

	lv1_gpu_context_attribute(0, L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_MODE_SET,
	    0,0,0,0);
	lv1_gpu_context_attribute(0, L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_MODE_SET,
	    0,0,1,0);
	lv1_gpu_context_attribute(0, L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC,
	    0,L1GPU_DISPLAY_SYNC_VSYNC,0,0);
	lv1_gpu_context_attribute(0, L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC,
	    1,L1GPU_DISPLAY_SYNC_VSYNC,0,0);
	lv1_gpu_memory_allocate(roundup2(sc->fb_info.fb_size, 1024*1024),
	    0, 0, 0, 0, &sc->sc_fbhandle, &fb_paddr);
	lv1_gpu_context_allocate(sc->sc_fbhandle, 0, &sc->sc_fbcontext,
	    &sc->sc_dma_control, &sc->sc_driver_info, &sc->sc_reports,
	    &sc->sc_reports_size);

	lv1_gpu_context_attribute(sc->sc_fbcontext,
	    L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP, 0, 0, 0, 0);
	lv1_gpu_context_attribute(sc->sc_fbcontext,
	    L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP, 1, 0, 0, 0);

	sc->fb_info.fb_pbase = fb_paddr;
	for (va = 0; va < sc->fb_info.fb_size; va += PAGE_SIZE)
		pmap_kenter_attr(0x10000000 + va, fb_paddr + va,
		    VM_MEMATTR_WRITE_COMBINING);
	sc->fb_info.fb_flags &= ~FB_FLAG_NOWRITE;
}

static int
ps3fb_init(struct vt_device *vd)
{
	struct ps3fb_softc *sc;
	char linux_video_mode[24];
	int linux_video_mode_num = 0;

	/* Init softc */
	vd->vd_softc = sc = &ps3fb_softc;

	sc->fb_info.fb_depth = 32;
	sc->fb_info.fb_height = 1080;
	sc->fb_info.fb_width = 1920;

	/* See if the bootloader has passed a graphics mode to use */
	bzero(linux_video_mode, sizeof(linux_video_mode));
	TUNABLE_STR_FETCH("video", linux_video_mode, sizeof(linux_video_mode));
	sscanf(linux_video_mode, "ps3fb:mode:%d", &linux_video_mode_num);
	
	switch (linux_video_mode_num) {
	case 1:
	case 2:
		sc->fb_info.fb_height = 480;
		sc->fb_info.fb_width = 720;
		break;
	case 3:
	case 8:
		sc->fb_info.fb_height = 720;
		sc->fb_info.fb_width = 1280;
		break;
	case 4:
	case 5:
	case 9:
	case 10:
		sc->fb_info.fb_height = 1080;
		sc->fb_info.fb_width = 1920;
		break;
	case 6:
	case 7:
		sc->fb_info.fb_height = 576;
		sc->fb_info.fb_width = 720;
		break;
	case 11:
		sc->fb_info.fb_height = 768;
		sc->fb_info.fb_width = 1024;
		break;
	case 12:
		sc->fb_info.fb_height = 1024;
		sc->fb_info.fb_width = 1280;
		break;
	case 13:
		sc->fb_info.fb_height = 1200;
		sc->fb_info.fb_width = 1920;
		break;
	}
	
	/* Allow explicitly-specified values for us to override everything */
	TUNABLE_INT_FETCH("hw.ps3fb.height", &sc->fb_info.fb_height);
	TUNABLE_INT_FETCH("hw.ps3fb.width", &sc->fb_info.fb_width);

	sc->fb_info.fb_stride = sc->fb_info.fb_width*4;
	sc->fb_info.fb_size = sc->fb_info.fb_height * sc->fb_info.fb_stride;
	sc->fb_info.fb_bpp = sc->fb_info.fb_stride / sc->fb_info.fb_width * 8;

	/*
	 * Arbitrarily choose address for the framebuffer
	 */

	sc->fb_info.fb_vbase = 0x10000000;
	sc->fb_info.fb_flags |= FB_FLAG_NOWRITE; /* Not available yet */
	sc->fb_info.fb_cmsize = 16;

	/* 32-bit VGA palette */
	vt_generate_cons_palette(sc->fb_info.fb_cmap, COLOR_FORMAT_RGB,
	    255, 16, 255, 8, 255, 0);

	/* Set correct graphics context */
	lv1_gpu_context_attribute(sc->sc_fbcontext,
	    L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP, 0, 0, 0, 0);
	lv1_gpu_context_attribute(sc->sc_fbcontext,
	    L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP, 1, 0, 0, 0);

	vt_fb_init(vd);

	return (CN_INTERNAL);
}

