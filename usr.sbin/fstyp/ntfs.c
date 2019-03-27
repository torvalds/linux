/*-
 * Copyright (c) 2005 Takanori Watanabe
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

#define	NTFS_A_VOLUMENAME	0x60
#define	NTFS_FILEMAGIC		((uint32_t)(0x454C4946))
#define	NTFS_VOLUMEINO		3

struct ntfs_attr {
	uint32_t	a_type;
	uint32_t	reclen;
	uint8_t		a_flag;
	uint8_t		a_namelen;
	uint8_t		a_nameoff;
	uint8_t		reserved1;
	uint8_t		a_compression;
	uint8_t		reserved2;
	uint16_t	a_index;
	uint16_t	a_datalen;
	uint16_t	reserved3;
	uint16_t	a_dataoff;
	uint16_t	a_indexed;
} __packed;

struct ntfs_filerec {
	uint32_t	fr_hdrmagic;
	uint16_t	fr_hdrfoff;
	uint16_t	fr_hdrfnum;
	uint8_t		reserved[8];
	uint16_t	fr_seqnum;
	uint16_t	fr_nlink;
	uint16_t	fr_attroff;
	uint16_t	fr_flags;
	uint32_t	fr_size;
	uint32_t	fr_allocated;
	uint64_t	fr_mainrec;
	uint16_t	fr_attrnum;
} __packed;

struct ntfs_bootfile {
	uint8_t		reserved1[3];
	uint8_t		bf_sysid[8];
	uint16_t	bf_bps;
	uint8_t		bf_spc;
	uint8_t		reserved2[7];
	uint8_t		bf_media;
	uint8_t		reserved3[2];
	uint16_t	bf_spt;
	uint16_t	bf_heads;
	uint8_t		reserver4[12];
	uint64_t	bf_spv;
	uint64_t	bf_mftcn;
	uint64_t	bf_mftmirrcn;
	int8_t		bf_mftrecsz;
	uint32_t	bf_ibsz;
	uint32_t	bf_volsn;
} __packed;

int
fstyp_ntfs(FILE *fp, char *label, size_t size)
{
	struct ntfs_bootfile *bf;
	struct ntfs_filerec *fr;
	struct ntfs_attr *atr;
	off_t voloff;
	char *filerecp, *ap;
	int8_t mftrecsz;
	char vnchar;
	int recsize, j;

	filerecp = NULL;

	bf = (struct ntfs_bootfile *)read_buf(fp, 0, 512);
	if (bf == NULL || strncmp(bf->bf_sysid, "NTFS    ", 8) != 0)
		goto fail;

	mftrecsz = bf->bf_mftrecsz;
	recsize = (mftrecsz > 0) ? (mftrecsz * bf->bf_bps * bf->bf_spc) : (1 << -mftrecsz);

	voloff = bf->bf_mftcn * bf->bf_spc * bf->bf_bps +
	    recsize * NTFS_VOLUMEINO;

	filerecp = read_buf(fp, voloff, recsize);
	if (filerecp == NULL)
		goto fail;
	fr = (struct ntfs_filerec *)filerecp;

	if (fr->fr_hdrmagic != NTFS_FILEMAGIC)
		goto fail;

	for (ap = filerecp + fr->fr_attroff;
	    atr = (struct ntfs_attr *)ap, (int)atr->a_type != -1;
	    ap += atr->reclen) {
		if (atr->a_type == NTFS_A_VOLUMENAME) {
			if(atr->a_datalen >= size *2){
				goto fail;
			}
			/*
			 *UNICODE to ASCII.
			 * Should we need to use iconv(9)?
			 */
			for (j = 0; j < atr->a_datalen; j++) {
				vnchar = *(ap + atr->a_dataoff + j);
				if (j & 1) {
					if (vnchar) {
						goto fail;
					}
				} else {
					label[j / 2] = vnchar;
				}
			}
			label[j / 2] = 0;
			break;
		}
	}

	free(bf);
	free(filerecp);

	return (0);

fail:
	free(bf);
	free(filerecp);

	return (1);
}
