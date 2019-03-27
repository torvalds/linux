/*-
 * Copyright (c) 2014 Juniper Networks, Inc.
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

#include <sys/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gpt.h>
#include <mbr.h>

#include "endian.h"
#include "image.h"
#include "mkimg.h"
#include "scheme.h"

static mkimg_uuid_t gpt_uuid_efi = GPT_ENT_TYPE_EFI;
static mkimg_uuid_t gpt_uuid_freebsd = GPT_ENT_TYPE_FREEBSD;
static mkimg_uuid_t gpt_uuid_freebsd_boot = GPT_ENT_TYPE_FREEBSD_BOOT;
static mkimg_uuid_t gpt_uuid_freebsd_nandfs = GPT_ENT_TYPE_FREEBSD_NANDFS;
static mkimg_uuid_t gpt_uuid_freebsd_swap = GPT_ENT_TYPE_FREEBSD_SWAP;
static mkimg_uuid_t gpt_uuid_freebsd_ufs = GPT_ENT_TYPE_FREEBSD_UFS;
static mkimg_uuid_t gpt_uuid_freebsd_vinum = GPT_ENT_TYPE_FREEBSD_VINUM;
static mkimg_uuid_t gpt_uuid_freebsd_zfs = GPT_ENT_TYPE_FREEBSD_ZFS;
static mkimg_uuid_t gpt_uuid_mbr = GPT_ENT_TYPE_MBR;
static mkimg_uuid_t gpt_uuid_ms_basic_data = GPT_ENT_TYPE_MS_BASIC_DATA;

static struct mkimg_alias gpt_aliases[] = {
    {	ALIAS_EFI, ALIAS_PTR2TYPE(&gpt_uuid_efi) },
    {	ALIAS_FREEBSD, ALIAS_PTR2TYPE(&gpt_uuid_freebsd) },
    {	ALIAS_FREEBSD_BOOT, ALIAS_PTR2TYPE(&gpt_uuid_freebsd_boot) },
    {	ALIAS_FREEBSD_NANDFS, ALIAS_PTR2TYPE(&gpt_uuid_freebsd_nandfs) },
    {	ALIAS_FREEBSD_SWAP, ALIAS_PTR2TYPE(&gpt_uuid_freebsd_swap) },
    {	ALIAS_FREEBSD_UFS, ALIAS_PTR2TYPE(&gpt_uuid_freebsd_ufs) },
    {	ALIAS_FREEBSD_VINUM, ALIAS_PTR2TYPE(&gpt_uuid_freebsd_vinum) },
    {	ALIAS_FREEBSD_ZFS, ALIAS_PTR2TYPE(&gpt_uuid_freebsd_zfs) },
    {	ALIAS_MBR, ALIAS_PTR2TYPE(&gpt_uuid_mbr) },
    {	ALIAS_NTFS, ALIAS_PTR2TYPE(&gpt_uuid_ms_basic_data) },
    {	ALIAS_NONE, 0 }		/* Keep last! */
};

/* CRC32 code derived from work by Gary S. Brown. */
static const uint32_t crc32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

static uint32_t
crc32(const void *buf, size_t sz)
{
	const uint8_t *p = (const uint8_t *)buf;
	uint32_t crc = ~0U;

	while (sz--)
		crc = crc32_tab[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	return (crc ^ ~0U);
}

static u_int
gpt_tblsz(void)
{
	u_int ents;

	ents = secsz / sizeof(struct gpt_ent);
	return ((nparts + ents - 1) / ents);
}

static lba_t
gpt_metadata(u_int where, lba_t blk)
{

	if (where == SCHEME_META_IMG_START || where == SCHEME_META_IMG_END) {
		blk += gpt_tblsz();
		blk += (where == SCHEME_META_IMG_START) ? 2 : 1;
	}
	return (round_block(blk));
}

static int
gpt_write_pmbr(lba_t blks, void *bootcode)
{
	u_char *pmbr;
	uint32_t secs;
	int error;

	secs = (blks > UINT32_MAX) ? UINT32_MAX : (uint32_t)blks - 1;

	pmbr = malloc(secsz);
	if (pmbr == NULL)
		return (errno);
	if (bootcode != NULL) {
		memcpy(pmbr, bootcode, DOSPARTOFF);
		memset(pmbr + DOSPARTOFF, 0, secsz - DOSPARTOFF);
	} else
		memset(pmbr, 0, secsz);
	pmbr[DOSPARTOFF + 2] = 2;
	pmbr[DOSPARTOFF + 4] = 0xee;
	pmbr[DOSPARTOFF + 5] = 0xff;
	pmbr[DOSPARTOFF + 6] = 0xff;
	pmbr[DOSPARTOFF + 7] = 0xff;
	le32enc(pmbr + DOSPARTOFF + 8, 1);
	le32enc(pmbr + DOSPARTOFF + 12, secs);
	le16enc(pmbr + DOSMAGICOFFSET, DOSMAGIC);
	error = image_write(0, pmbr, 1);
	free(pmbr);
	return (error);
}

static struct gpt_ent *
gpt_mktbl(u_int tblsz)
{
	mkimg_uuid_t uuid;
	struct gpt_ent *tbl, *ent;
	struct part *part;
	int c, idx;

	tbl = calloc(tblsz, secsz);
	if (tbl == NULL)
		return (NULL);

	TAILQ_FOREACH(part, &partlist, link) {
		ent = tbl + part->index;
		mkimg_uuid_enc(&ent->ent_type, ALIAS_TYPE2PTR(part->type));
		mkimg_uuid(&uuid);
		mkimg_uuid_enc(&ent->ent_uuid, &uuid);
		le64enc(&ent->ent_lba_start, part->block);
		le64enc(&ent->ent_lba_end, part->block + part->size - 1);
		if (part->label != NULL) {
			idx = 0;
			while ((c = part->label[idx]) != '\0') {
				le16enc(ent->ent_name + idx, c);
				idx++;
			}
		}
	}
	return (tbl);
}

static int
gpt_write_hdr(struct gpt_hdr *hdr, uint64_t self, uint64_t alt, uint64_t tbl)
{
	uint32_t crc;

	le64enc(&hdr->hdr_lba_self, self);
	le64enc(&hdr->hdr_lba_alt, alt);
	le64enc(&hdr->hdr_lba_table, tbl);
	hdr->hdr_crc_self = 0;
	crc = crc32(hdr, offsetof(struct gpt_hdr, padding));
	le64enc(&hdr->hdr_crc_self, crc);
	return (image_write(self, hdr, 1));
}

static int
gpt_write(lba_t imgsz, void *bootcode)
{
	mkimg_uuid_t uuid;
	struct gpt_ent *tbl;
	struct gpt_hdr *hdr;
	uint32_t crc;
	u_int tblsz;
	int error;

	/* PMBR */
	error = gpt_write_pmbr(imgsz, bootcode);
	if (error)
		return (error);

	/* GPT table(s) */
	tblsz = gpt_tblsz();
	tbl = gpt_mktbl(tblsz);
	if (tbl == NULL)
		return (errno);
	error = image_write(2, tbl, tblsz);
	if (error)
		goto out;
	error = image_write(imgsz - (tblsz + 1), tbl, tblsz);
	if (error)
		goto out;

	/* GPT header(s) */
	hdr = malloc(secsz);
	if (hdr == NULL) {
		error = errno;
		goto out;
	}
	memset(hdr, 0, secsz);
	memcpy(hdr->hdr_sig, GPT_HDR_SIG, sizeof(hdr->hdr_sig));
	le32enc(&hdr->hdr_revision, GPT_HDR_REVISION);
	le32enc(&hdr->hdr_size, offsetof(struct gpt_hdr, padding));
	le64enc(&hdr->hdr_lba_start, 2 + tblsz);
	le64enc(&hdr->hdr_lba_end, imgsz - tblsz - 2);
	mkimg_uuid(&uuid);
	mkimg_uuid_enc(&hdr->hdr_uuid, &uuid);
	le32enc(&hdr->hdr_entries, tblsz * secsz / sizeof(struct gpt_ent));
	le32enc(&hdr->hdr_entsz, sizeof(struct gpt_ent));
	crc = crc32(tbl, tblsz * secsz);
	le32enc(&hdr->hdr_crc_table, crc);
	error = gpt_write_hdr(hdr, 1, imgsz - 1, 2);
	if (!error)
		error = gpt_write_hdr(hdr, imgsz - 1, 1, imgsz - tblsz - 1);
	free(hdr);

 out:
	free(tbl);
	return (error);
}

static struct mkimg_scheme gpt_scheme = {
	.name = "gpt",
	.description = "GUID Partition Table",
	.aliases = gpt_aliases,
	.metadata = gpt_metadata,
	.write = gpt_write,
	.nparts = 4096,
	.labellen = 36,
	.bootcode = 512,
	.maxsecsz = 4096
};

SCHEME_DEFINE(gpt_scheme);
