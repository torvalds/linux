/*-
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/gpt.h>

#ifndef LITTLE_ENDIAN
#error gpt.c works only for little endian architectures
#endif

#include "stand.h"
#include "crc32.h"
#include "drv.h"
#include "gpt.h"

static struct gpt_hdr hdr_primary, hdr_backup, *gpthdr;
static uint64_t hdr_primary_lba, hdr_backup_lba;
static struct gpt_ent table_primary[MAXTBLENTS], table_backup[MAXTBLENTS];
static struct gpt_ent *gpttable;
static int curent, bootonce;

/*
 * Buffer below 64kB passed on gptread(), which can hold at least
 * one sector of data (512 bytes).
 */
static char *secbuf;

static void
gptupdate(const char *which, struct dsk *dskp, struct gpt_hdr *hdr,
    struct gpt_ent *table)
{
	int entries_per_sec, firstent;
	daddr_t slba;

	/*
	 * We need to update the following for both primary and backup GPT:
	 * 1. Sector on disk that contains current partition.
	 * 2. Partition table checksum.
	 * 3. Header checksum.
	 * 4. Header on disk.
	 */

	entries_per_sec = DEV_BSIZE / hdr->hdr_entsz;
	slba = curent / entries_per_sec;
	firstent = slba * entries_per_sec;
	bcopy(&table[firstent], secbuf, DEV_BSIZE);
	slba += hdr->hdr_lba_table;
	if (drvwrite(dskp, secbuf, slba, 1)) {
		printf("%s: unable to update %s GPT partition table\n",
		    BOOTPROG, which);
		return;
	}
	hdr->hdr_crc_table = crc32(table, hdr->hdr_entries * hdr->hdr_entsz);
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = crc32(hdr, hdr->hdr_size);
	bzero(secbuf, DEV_BSIZE);
	bcopy(hdr, secbuf, hdr->hdr_size);
	if (drvwrite(dskp, secbuf, hdr->hdr_lba_self, 1)) {
		printf("%s: unable to update %s GPT header\n", BOOTPROG, which);
		return;
	}
}

int
gptfind(const uuid_t *uuid, struct dsk *dskp, int part)
{
	struct gpt_ent *ent;
	int firsttry;

	if (part >= 0) {
		if (part == 0 || part > gpthdr->hdr_entries) {
			printf("%s: invalid partition index\n", BOOTPROG);
			return (-1);
		}
		ent = &gpttable[part - 1];
		if (bcmp(&ent->ent_type, uuid, sizeof(uuid_t)) != 0) {
			printf("%s: specified partition is not UFS\n",
			    BOOTPROG);
			return (-1);
		}
		curent = part - 1;
		goto found;
	}

	firsttry = (curent == -1);
	curent++;
	if (curent >= gpthdr->hdr_entries) {
		curent = gpthdr->hdr_entries;
		return (-1);
	}
	if (bootonce) {
		/*
		 * First look for partition with both GPT_ENT_ATTR_BOOTME and
		 * GPT_ENT_ATTR_BOOTONCE flags.
		 */
		for (; curent < gpthdr->hdr_entries; curent++) {
			ent = &gpttable[curent];
			if (bcmp(&ent->ent_type, uuid, sizeof(uuid_t)) != 0)
				continue;
			if (!(ent->ent_attr & GPT_ENT_ATTR_BOOTME))
				continue;
			if (!(ent->ent_attr & GPT_ENT_ATTR_BOOTONCE))
				continue;
			/* Ok, found one. */
			goto found;
		}
		bootonce = 0;
		curent = 0;
	}
	for (; curent < gpthdr->hdr_entries; curent++) {
		ent = &gpttable[curent];
		if (bcmp(&ent->ent_type, uuid, sizeof(uuid_t)) != 0)
			continue;
		if (!(ent->ent_attr & GPT_ENT_ATTR_BOOTME))
			continue;
		if (ent->ent_attr & GPT_ENT_ATTR_BOOTONCE)
			continue;
		/* Ok, found one. */
		goto found;
	}
	if (firsttry) {
		/*
		 * No partition with BOOTME flag was found, try to boot from
		 * first UFS partition.
		 */
		for (curent = 0; curent < gpthdr->hdr_entries; curent++) {
			ent = &gpttable[curent];
			if (bcmp(&ent->ent_type, uuid, sizeof(uuid_t)) != 0)
				continue;
			/* Ok, found one. */
			goto found;
		}
	}
	return (-1);
found:
	dskp->part = curent + 1;
	ent = &gpttable[curent];
	dskp->start = ent->ent_lba_start;
	if (ent->ent_attr & GPT_ENT_ATTR_BOOTONCE) {
		/*
		 * Clear BOOTME, but leave BOOTONCE set before trying to
		 * boot from this partition.
		 */
		if (hdr_primary_lba > 0) {
			table_primary[curent].ent_attr &= ~GPT_ENT_ATTR_BOOTME;
			gptupdate("primary", dskp, &hdr_primary, table_primary);
		}
		if (hdr_backup_lba > 0) {
			table_backup[curent].ent_attr &= ~GPT_ENT_ATTR_BOOTME;
			gptupdate("backup", dskp, &hdr_backup, table_backup);
		}
	}
	return (0);
}

static int
gptread_hdr(const char *which, struct dsk *dskp, struct gpt_hdr *hdr,
    uint64_t hdrlba)
{
	uint32_t crc;

	if (drvread(dskp, secbuf, hdrlba, 1)) {
		printf("%s: unable to read %s GPT header\n", BOOTPROG, which);
		return (-1);
	}
	bcopy(secbuf, hdr, sizeof(*hdr));
	if (bcmp(hdr->hdr_sig, GPT_HDR_SIG, sizeof(hdr->hdr_sig)) != 0 ||
	    hdr->hdr_lba_self != hdrlba || hdr->hdr_revision < 0x00010000 ||
	    hdr->hdr_entsz < sizeof(struct gpt_ent) ||
	    hdr->hdr_entries > MAXTBLENTS || DEV_BSIZE % hdr->hdr_entsz != 0) {
		printf("%s: invalid %s GPT header\n", BOOTPROG, which);
		return (-1);
	}
	crc = hdr->hdr_crc_self;
	hdr->hdr_crc_self = 0;
	if (crc32(hdr, hdr->hdr_size) != crc) {
		printf("%s: %s GPT header checksum mismatch\n", BOOTPROG,
		    which);
		return (-1);
	}
	hdr->hdr_crc_self = crc;
	return (0);
}

void
gptbootfailed(struct dsk *dskp)
{

	if (!(gpttable[curent].ent_attr & GPT_ENT_ATTR_BOOTONCE))
		return;

	if (hdr_primary_lba > 0) {
		table_primary[curent].ent_attr &= ~GPT_ENT_ATTR_BOOTONCE;
		table_primary[curent].ent_attr |= GPT_ENT_ATTR_BOOTFAILED;
		gptupdate("primary", dskp, &hdr_primary, table_primary);
	}
	if (hdr_backup_lba > 0) {
		table_backup[curent].ent_attr &= ~GPT_ENT_ATTR_BOOTONCE;
		table_backup[curent].ent_attr |= GPT_ENT_ATTR_BOOTFAILED;
		gptupdate("backup", dskp, &hdr_backup, table_backup);
	}
}

static void
gptbootconv(const char *which, struct dsk *dskp, struct gpt_hdr *hdr,
    struct gpt_ent *table)
{
	struct gpt_ent *ent;
	daddr_t slba;
	int table_updated, sector_updated;
	int entries_per_sec, nent, part;

	table_updated = 0;
	entries_per_sec = DEV_BSIZE / hdr->hdr_entsz;
	for (nent = 0, slba = hdr->hdr_lba_table;
	     slba < hdr->hdr_lba_table + hdr->hdr_entries / entries_per_sec;
	     slba++, nent += entries_per_sec) {
		sector_updated = 0;
		for (part = 0; part < entries_per_sec; part++) {
			ent = &table[nent + part];
			if ((ent->ent_attr & (GPT_ENT_ATTR_BOOTME |
			    GPT_ENT_ATTR_BOOTONCE |
			    GPT_ENT_ATTR_BOOTFAILED)) !=
			    GPT_ENT_ATTR_BOOTONCE) {
				continue;
			}
			ent->ent_attr &= ~GPT_ENT_ATTR_BOOTONCE;
			ent->ent_attr |= GPT_ENT_ATTR_BOOTFAILED;
			table_updated = 1;
			sector_updated = 1;
		}
		if (!sector_updated)
			continue;
		bcopy(&table[nent], secbuf, DEV_BSIZE);
		if (drvwrite(dskp, secbuf, slba, 1)) {
			printf("%s: unable to update %s GPT partition table\n",
			    BOOTPROG, which);
		}
	}
	if (!table_updated)
		return;
	hdr->hdr_crc_table = crc32(table, hdr->hdr_entries * hdr->hdr_entsz);
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = crc32(hdr, hdr->hdr_size);
	bzero(secbuf, DEV_BSIZE);
	bcopy(hdr, secbuf, hdr->hdr_size);
	if (drvwrite(dskp, secbuf, hdr->hdr_lba_self, 1))
		printf("%s: unable to update %s GPT header\n", BOOTPROG, which);
}

static int
gptread_table(const char *which, const uuid_t *uuid, struct dsk *dskp,
    struct gpt_hdr *hdr, struct gpt_ent *table)
{
	struct gpt_ent *ent;
	int entries_per_sec;
	int part, nent;
	daddr_t slba;

	if (hdr->hdr_entries == 0)
		return (0);

	entries_per_sec = DEV_BSIZE / hdr->hdr_entsz;
	slba = hdr->hdr_lba_table;
	nent = 0;
	for (;;) {
		if (drvread(dskp, secbuf, slba, 1)) {
			printf("%s: unable to read %s GPT partition table\n",
			    BOOTPROG, which);
			return (-1);
		}
		ent = (struct gpt_ent *)secbuf;
		for (part = 0; part < entries_per_sec; part++, ent++) {
			bcopy(ent, &table[nent], sizeof(table[nent]));
			if (++nent >= hdr->hdr_entries)
				break;
		}
		if (nent >= hdr->hdr_entries)
			break;
		slba++;
	}
	if (crc32(table, nent * hdr->hdr_entsz) != hdr->hdr_crc_table) {
		printf("%s: %s GPT table checksum mismatch\n", BOOTPROG, which);
		return (-1);
	}
	return (0);
}

int
gptread(const uuid_t *uuid, struct dsk *dskp, char *buf)
{
	uint64_t altlba;

	/*
	 * Read and verify both GPT headers: primary and backup.
	 */

	secbuf = buf;
	hdr_primary_lba = hdr_backup_lba = 0;
	curent = -1;
	bootonce = 1;
	dskp->start = 0;

	if (gptread_hdr("primary", dskp, &hdr_primary, 1) == 0 &&
	    gptread_table("primary", uuid, dskp, &hdr_primary,
	    table_primary) == 0) {
		hdr_primary_lba = hdr_primary.hdr_lba_self;
		gpthdr = &hdr_primary;
		gpttable = table_primary;
	}

	if (hdr_primary_lba > 0) {
		/*
		 * If primary header is valid, we can get backup
		 * header location from there.
		 */
		altlba = hdr_primary.hdr_lba_alt;
	} else {
		altlba = drvsize(dskp);
		if (altlba > 0)
			altlba--;
	}
	if (altlba == 0)
		printf("%s: unable to locate backup GPT header\n", BOOTPROG);
	else if (gptread_hdr("backup", dskp, &hdr_backup, altlba) == 0 &&
	    gptread_table("backup", uuid, dskp, &hdr_backup,
	    table_backup) == 0) {
		hdr_backup_lba = hdr_backup.hdr_lba_self;
		if (hdr_primary_lba == 0) {
			gpthdr = &hdr_backup;
			gpttable = table_backup;
			printf("%s: using backup GPT\n", BOOTPROG);
		}
	}

	/*
	 * Convert all BOOTONCE without BOOTME flags into BOOTFAILED.
	 * BOOTONCE without BOOTME means that we tried to boot from it,
	 * but failed after leaving gptboot and machine was rebooted.
	 * We don't want to leave partitions marked as BOOTONCE only,
	 * because when we boot successfully start-up scripts should
	 * find at most one partition with only BOOTONCE flag and this
	 * will mean that we booted from that partition.
	 */
	if (hdr_primary_lba != 0)
		gptbootconv("primary", dskp, &hdr_primary, table_primary);
	if (hdr_backup_lba != 0)
		gptbootconv("backup", dskp, &hdr_backup, table_backup);

	if (hdr_primary_lba == 0 && hdr_backup_lba == 0)
		return (-1);
	return (0);
}
