/*
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fstyp.h"

struct exfat_vbr {
	char		ev_jmp[3];
	char		ev_fsname[8];
	char		ev_zeros[53];
	uint64_t	ev_part_offset;
	uint64_t	ev_vol_length;
	uint32_t	ev_fat_offset;
	uint32_t	ev_fat_length;
	uint32_t	ev_cluster_offset;
	uint32_t	ev_cluster_count;
	uint32_t	ev_rootdir_cluster;
	uint32_t	ev_vol_serial;
	uint16_t	ev_fs_revision;
	uint16_t	ev_vol_flags;
	uint8_t		ev_log_bytes_per_sect;
	uint8_t		ev_log_sect_per_clust;
	uint8_t		ev_num_fats;
	uint8_t		ev_drive_sel;
	uint8_t		ev_percent_used;
} __packed;

int
fstyp_exfat(FILE *fp, char *label, size_t size)
{
	struct exfat_vbr *ev;

	ev = (struct exfat_vbr *)read_buf(fp, 0, 512);
	if (ev == NULL || strncmp(ev->ev_fsname, "EXFAT   ", 8) != 0)
		goto fail;

	/*
	 * Reading the volume label requires walking the root directory to look
	 * for a special label file.  Left as an exercise for the reader.
	 */
	free(ev);
	return (0);

fail:
	free(ev);
	return (1);
}
