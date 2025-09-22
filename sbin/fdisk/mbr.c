/*	$OpenBSD: mbr.c,v 1.125 2025/05/23 00:20:02 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "part.h"
#include "disk.h"
#include "misc.h"
#include "mbr.h"
#include "gpt.h"

struct dos_mbr		default_dmbr;

void		mbr_to_dos_mbr(const struct mbr *, struct dos_mbr *);
void		dos_mbr_to_mbr(const struct dos_mbr *, const uint64_t,
    const uint64_t, struct mbr *);

void
MBR_init(struct mbr *mbr)
{
	struct dos_partition	dp;
	struct prt		bootprt, obsdprt;
	daddr_t			daddr;

	memset(&gmbr, 0, sizeof(gmbr));
	memset(&gh, 0, sizeof(gh));
	memset(&gp, 0, sizeof(gp));

	if (mbr->mbr_lba_self != 0) {
		/* Extended MBR - save lba's, set sig, zap everything else. */
		memset(mbr->mbr_code, 0, sizeof(mbr->mbr_code));
		memset(mbr->mbr_prt, 0, sizeof(mbr->mbr_prt));
		mbr->mbr_signature = DOSMBR_SIGNATURE;
		return;
	}

	memset(&obsdprt, 0, sizeof(obsdprt));
	memset(&bootprt, 0, sizeof(bootprt));

	if (disk.dk_bootprt.prt_ns > 0) {
		bootprt = disk.dk_bootprt;
	} else {
		memcpy(&dp, &default_dmbr.dmbr_parts[0], sizeof(dp));
		PRT_dp_to_prt(&dp, 0, 0, &bootprt);
	}

	if (bootprt.prt_ns > 0) {
		/* Start OpenBSD partition immediately after bootprt. */
		obsdprt.prt_bs = bootprt.prt_bs + bootprt.prt_ns;
	} else if (disk.dk_heads > 1 || disk.dk_cylinders > 1) {
		/*
		 * Start OpenBSD partition on power of 2 block number
		 * after the first track.
		 */
		daddr = 1;
		while (daddr < DL_SECTOBLK(&dl, disk.dk_sectors))
			daddr *= 2;
		obsdprt.prt_bs = DL_BLKTOSEC(&dl, daddr);
	} else {
		/* Start OpenBSD partition immediately after MBR. */
		obsdprt.prt_bs = 1;
	}

	if (obsdprt.prt_bs >= disk.dk_size) {
		memset(&obsdprt, 0, sizeof(obsdprt));
	} else {
		obsdprt.prt_ns = disk.dk_size - obsdprt.prt_bs;
		obsdprt.prt_id = DOSPTYP_OPENBSD;
		if (bootprt.prt_flag != DOSACTIVE)
			obsdprt.prt_flag = DOSACTIVE;
	}

	memset(mbr, 0, sizeof(*mbr));
	memcpy(mbr->mbr_code, default_dmbr.dmbr_boot, sizeof(mbr->mbr_code));
	mbr->mbr_prt[0] = bootprt;
	mbr->mbr_prt[3] = obsdprt;
	mbr->mbr_signature = DOSMBR_SIGNATURE;
}

void
dos_mbr_to_mbr(const struct dos_mbr *dmbr, const uint64_t lba_self,
    const uint64_t lba_firstembr, struct mbr *mbr)
{
	struct dos_partition	dos_parts[NDOSPART];
	uint8_t			*p;
	unsigned int		 i;

	p = (uint8_t *)dmbr;
	mbr->mbr_dmbrzeros = 0;
	for (i = 0; i < sizeof(struct dos_mbr) && *p == 0; i++, p++)
		mbr->mbr_dmbrzeros++;

	memcpy(mbr->mbr_code, dmbr->dmbr_boot, sizeof(mbr->mbr_code));
	mbr->mbr_lba_self = lba_self;
	mbr->mbr_lba_firstembr = lba_firstembr;
	mbr->mbr_signature = letoh16(dmbr->dmbr_sign);

	memcpy(dos_parts, dmbr->dmbr_parts, sizeof(dos_parts));

	for (i = 0; i < nitems(mbr->mbr_prt); i++) {
		memset(&mbr->mbr_prt[i], 0, sizeof(mbr->mbr_prt[i]));
		if (i < nitems(dmbr->dmbr_parts))
			PRT_dp_to_prt(&dos_parts[i], lba_self, lba_firstembr,
			    &mbr->mbr_prt[i]);
	}
}

void
mbr_to_dos_mbr(const struct mbr *mbr, struct dos_mbr *dos_mbr)
{
	struct dos_partition	dos_partition;
	unsigned int		i;

	memcpy(dos_mbr->dmbr_boot, mbr->mbr_code, sizeof(dos_mbr->dmbr_boot));
	dos_mbr->dmbr_sign = htole16(DOSMBR_SIGNATURE);

	for (i = 0; i < nitems(dos_mbr->dmbr_parts); i++) {
		memset(&dos_partition, 0, sizeof(dos_partition));
		if (i < nitems(mbr->mbr_prt)) {
			PRT_prt_to_dp(&mbr->mbr_prt[i], mbr->mbr_lba_self,
			    mbr->mbr_lba_firstembr, &dos_partition);
		}
		memcpy(&dos_mbr->dmbr_parts[i], &dos_partition,
		    sizeof(dos_mbr->dmbr_parts[i]));
	}
}

void
MBR_print(const struct mbr *mbr, const char *units)
{
	unsigned int		i;

	DISK_printgeometry("s");

	printf("Offset: %llu\t", mbr->mbr_lba_self);
	printf("Signature: 0x%X\n", (int)mbr->mbr_signature);
	PRT_print_parthdr();

	for (i = 0; i < nitems(mbr->mbr_prt); i++)
		PRT_print_part(i, &mbr->mbr_prt[i], units);
}

int
MBR_recover_partition(char *line, struct mbr *mbr)
{
	uint64_t		 bs, ns;
	unsigned int		 pn, id;
	unsigned char		 flag;

	if (sscanf(line, "%c%u: %2X %*u %*u %*u - %*u %*u %*u " "[ %llu: %llu ]",
	    &flag, &pn, &id, &bs, &ns) != 5)
		return -1;
	if (pn >= nitems(mbr->mbr_prt) || (flag != ' ' && flag != '*'))
		return -1;

	mbr->mbr_prt[pn].prt_bs = bs;
	mbr->mbr_prt[pn].prt_ns = ns;
	mbr->mbr_prt[pn].prt_flag = (flag == '*') ? DOSACTIVE : 0;
	mbr->mbr_prt[pn].prt_id = id;

	return 0;
}

int
MBR_read(const uint64_t lba_self, const uint64_t lba_firstembr, struct mbr *mbr)
{
	struct dos_mbr		 dos_mbr;

	if (DISK_readbytes(&dos_mbr, lba_self, sizeof(dos_mbr)))
		return -1;

	dos_mbr_to_mbr(&dos_mbr, lba_self, lba_firstembr, mbr);

	return 0;
}

int
MBR_write(const struct mbr *mbr)
{
	struct dos_mbr		 dos_mbr;

	mbr_to_dos_mbr(mbr, &dos_mbr);

	if (DISK_writebytes(&dos_mbr, mbr->mbr_lba_self, sizeof(dos_mbr)))
		return -1;

	/* Refresh in-kernel disklabel from the updated disk information. */
	if (ioctl(disk.dk_fd, DIOCRLDINFO, 0) == -1)
		warn("DIOCRLDINFO");

	return 0;
}

int
MBR_valid_prt(const struct mbr *mbr)
{
	uint64_t		bs, ns;
	unsigned int		i, nprt;
	unsigned char		id;

	if (mbr->mbr_dmbrzeros == sizeof(struct dos_mbr))
		return 1;	/* All zeros struct dos_mbr is editable. */

	nprt = 0;
	for (i = 0; i < nitems(mbr->mbr_prt); i++) {
		bs = mbr->mbr_prt[i].prt_bs;
		ns = mbr->mbr_prt[i].prt_ns;
		id = mbr->mbr_prt[i].prt_id;
		if ((bs == 0 && ns == 0 && id == 0) ||
		    (bs < DL_GETDSIZE(&dl) && ns > 0 && ns <= DL_GETDSIZE(&dl)))
			nprt++;
	}

	return nprt > 0 && mbr->mbr_signature == DOSMBR_SIGNATURE;
}
