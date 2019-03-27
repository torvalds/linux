/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Marius Strobl <marius@FreeBSD.org>
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
#include <geom/geom_disk.h>
#include <machine/md_var.h>

void
sparc64_ata_disk_firmware_geom_adjust(struct disk *disk)
{

	/*
	 * The VTOC8 disk label only uses 16-bit fields for cylinders, heads
	 * and sectors so the geometry of large disks has to be adjusted.
	 * If the disk is > 32GB at 16 heads and 63 sectors, adjust to 255
	 * sectors (this matches what the OpenSolaris dad(7D) driver does).
	 * If the disk is even > 128GB, additionally adjust the heads to
	 * 255.  This allows disks up to the 2TB limit of the extended VTOC8.
	 * XXX the OpenSolaris dad(7D) driver limits the mediasize to 128GB.
	 */
	if (disk->d_mediasize > (off_t)65535 * 16 * 63 * disk->d_sectorsize)
		disk->d_fwsectors = 255;
	if (disk->d_mediasize > (off_t)65535 * 16 * 255 * disk->d_sectorsize)
		disk->d_fwheads = 255;
}
