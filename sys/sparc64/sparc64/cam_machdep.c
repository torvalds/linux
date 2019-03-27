/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Marius Strobl <marius@FreeBSD.org>
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

#include <cam/cam.h>
#include <cam/cam_ccb.h>

#include <machine/md_var.h>

int
scsi_da_bios_params(struct ccb_calc_geometry *ccg)
{
	uint32_t secs_per_cylinder, size_mb;

	/*
	 * The VTOC8 disk label only uses 16-bit fields for cylinders, heads
	 * and sectors so the geometry of large disks has to be adjusted.
	 * We generally use the sizing used by cam_calc_geometry(9), except
	 * when it would overflow the cylinders, in which case we use 255
	 * heads and sectors.  This allows disks up to the 2TB limit of the
	 * extended VTOC8.
	 * XXX this doesn't match the sizing used by OpenSolaris, as that
	 * would exceed the 8-bit ccg->heads and ccg->secs_per_track.
	 */
	if (ccg->block_size == 0)
		return (0);
	size_mb = (1024L * 1024L) / ccg->block_size;
	if (size_mb == 0)
		return (0);
	size_mb = ccg->volume_size / size_mb;
	if (ccg->volume_size > (uint64_t)65535 * 255 * 63) {
		ccg->heads = 255;
		ccg->secs_per_track = 255;
	} else if (size_mb > 1024) {
		ccg->heads = 255;
		ccg->secs_per_track = 63;
	} else {
		ccg->heads = 64;
		ccg->secs_per_track = 32;
	}
	secs_per_cylinder = ccg->heads * ccg->secs_per_track;
	if (secs_per_cylinder == 0)
		return (0);
	ccg->cylinders = ccg->volume_size / secs_per_cylinder;
	return (1);
}
