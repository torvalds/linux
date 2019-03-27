/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/consio.h>				/* struct vt_mode */
#include <sys/endian.h>
#include <sys/fbio.h>				/* video_adapter_t */
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/terasic/mtl/terasic_mtl.h>

/*
 * Device driver for the Terasic Multitouch LCD (MTL).  Three separate
 * sub-drivers that support, respectively, access to device control registers,
 * the pixel frame buffer, and the text frame buffer.  The pixel frame buffer
 * is hooked up to vt(4), and the text frame buffer to syscons(4). 
 *
 * Eventually, the frame buffer control registers and touch screen input FIFO
 * will end up being separate sub-drivers as well.
 *
 * Note: sub-driver detach routines must check whether or not they have
 * attached as they may be called even if the attach routine hasn't been, on
 * an error.
 */

devclass_t	terasic_mtl_devclass;

int
terasic_mtl_attach(struct terasic_mtl_softc *sc)
{
	int error;

	error = terasic_mtl_reg_attach(sc);
	if (error)
		goto error;
	error = terasic_mtl_pixel_attach(sc);
	if (error)
		goto error;
	error = terasic_mtl_text_attach(sc);
	if (error)
		goto error;
	/*
	 * XXXRW: Once we've attached syscons or vt, we can't detach it, so do
	 * it last.
	 */
#if defined(DEV_VT)
	terasic_mtl_reg_pixel_endian_set(sc, BYTE_ORDER == BIG_ENDIAN);
	error = terasic_mtl_fbd_attach(sc);
	if (error)
		goto error;
	terasic_mtl_blend_pixel_set(sc, TERASIC_MTL_ALPHA_OPAQUE);
	terasic_mtl_blend_textfg_set(sc, TERASIC_MTL_ALPHA_TRANSPARENT);
	terasic_mtl_blend_textbg_set(sc, TERASIC_MTL_ALPHA_TRANSPARENT);
#endif
#if defined(DEV_SC)
	error = terasic_mtl_syscons_attach(sc);
	if (error)
		goto error;
	terasic_mtl_blend_pixel_set(sc, TERASIC_MTL_ALPHA_TRANSPARENT);
	terasic_mtl_blend_textfg_set(sc, TERASIC_MTL_ALPHA_OPAQUE);
	terasic_mtl_blend_textbg_set(sc, TERASIC_MTL_ALPHA_OPAQUE);
#endif
	terasic_mtl_blend_default_set(sc, TERASIC_MTL_COLOR_BLACK);
	return (0);
error:
	terasic_mtl_text_detach(sc);
	terasic_mtl_pixel_detach(sc);
	terasic_mtl_reg_detach(sc);
	return (error);
}

void
terasic_mtl_detach(struct terasic_mtl_softc *sc)
{

	/* XXXRW: syscons and vt can't detach, but try anyway, only to panic. */
#if defined(DEV_SC)
	terasic_mtl_syscons_detach(sc);
#endif
#if defined(DEV_VT)
	terasic_mtl_fbd_detach(sc);
#endif

	/* All other aspects of the driver can detach just fine. */
	terasic_mtl_text_detach(sc);
	terasic_mtl_pixel_detach(sc);
	terasic_mtl_reg_detach(sc);
}
