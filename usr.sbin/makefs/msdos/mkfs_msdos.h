/*	$OpenBSD: mkfs_msdos.h,v 1.6 2016/12/17 16:43:30 krw Exp $	*/
/*	$NetBSD: mkfs_msdos.h,v 1.3 2015/10/16 17:38:17 christos Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#define ALLOPTS \
AOPT(uint16_t, backup_sector, 1) \
AOPT(uint32_t, block_size, 1) \
AOPT(char *, bootstrap, -1) \
AOPT(uint16_t, bytes_per_sector, 1) \
AOPT(off_t, create_size, 0) \
AOPT(uint16_t, directory_entries, 1) \
AOPT(uint16_t, drive_heads, 1) \
AOPT(uint8_t,  fat_type, 12) \
AOPT(char *, floppy, -1) \
AOPT(uint32_t, hidden_sectors, 0) \
AOPT(uint16_t, info_sector, 1) \
AOPT(uint8_t, media_descriptor, 0) \
AOPT(uint8_t, num_FAT, 1) \
AOPT(char *, OEM_string, -1) \
AOPT(off_t, offset, 0) \
AOPT(uint16_t, reserved_sectors, 1) \
AOPT(uint8_t, sectors_per_cluster, 1) \
AOPT(uint32_t, sectors_per_fat, 1) \
AOPT(uint16_t, sectors_per_track, 1) \
AOPT(uint32_t, size, 1) \
AOPT(uint32_t, volume_id, 0) \
AOPT(char *, volume_label, -1)

struct msdos_options {
#define AOPT(_type, _name, _min) _type _name;
ALLOPTS
#undef AOPT
	uint32_t volume_id_set:1;
	uint32_t media_descriptor_set:1;
	uint32_t hidden_sectors_set:1;
};

int mkfs_msdos(const char *, const char *, const struct msdos_options *);
