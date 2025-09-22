/*	$OpenBSD: build.c,v 1.9 2017/08/27 08:15:48 otto Exp $	*/

/*
 * Copyright (c) 2004 Theo de Raadt <deraadt@openbsd.org>
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
#include <sys/types.h>
#include <dev/pci/if_bnxreg.h>
#include <fcntl.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "bnxfw.h"

#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))

int	bnx_rv2p_proc1len;
int	bnx_rv2p_proc2len;

struct chunks {
	void *start;
	int *len;
};

#define FILENAME_B06 "bnx-b06"
struct chunks chunks_b06[] = {
	{ bnx_COM_b06FwText, &bnx_COM_b06FwTextLen },
	{ bnx_COM_b06FwData, &bnx_COM_b06FwDataLen },
	{ bnx_COM_b06FwRodata, &bnx_COM_b06FwRodataLen },
	{ bnx_COM_b06FwBss, &bnx_COM_b06FwBssLen },
	{ bnx_COM_b06FwSbss, &bnx_COM_b06FwSbssLen },

	{ bnx_RXP_b06FwText, &bnx_RXP_b06FwTextLen },
	{ bnx_RXP_b06FwData, &bnx_RXP_b06FwDataLen },
	{ bnx_RXP_b06FwRodata, &bnx_RXP_b06FwRodataLen },
	{ bnx_RXP_b06FwBss, &bnx_RXP_b06FwBssLen },
	{ bnx_RXP_b06FwSbss, &bnx_RXP_b06FwSbssLen },

	{ bnx_TPAT_b06FwText, &bnx_TPAT_b06FwTextLen },
	{ bnx_TPAT_b06FwData, &bnx_TPAT_b06FwDataLen },
	{ bnx_TPAT_b06FwRodata, &bnx_TPAT_b06FwRodataLen },
	{ bnx_TPAT_b06FwBss, &bnx_TPAT_b06FwBssLen },
	{ bnx_TPAT_b06FwSbss, &bnx_TPAT_b06FwSbssLen },

	{ bnx_TXP_b06FwText, &bnx_TXP_b06FwTextLen },
	{ bnx_TXP_b06FwData, &bnx_TXP_b06FwDataLen },
	{ bnx_TXP_b06FwRodata, &bnx_TXP_b06FwRodataLen },
	{ bnx_TXP_b06FwBss, &bnx_TXP_b06FwBssLen },
	{ bnx_TXP_b06FwSbss, &bnx_TXP_b06FwSbssLen }
};

#define FILENAME_B09 "bnx-b09"
struct chunks chunks_b09[] = {
	{ bnx_COM_b09FwText, &bnx_COM_b09FwTextLen },
	{ bnx_COM_b09FwData, &bnx_COM_b09FwDataLen },
	{ bnx_COM_b09FwRodata, &bnx_COM_b09FwRodataLen },
	{ bnx_COM_b09FwBss, &bnx_COM_b09FwBssLen },
	{ bnx_COM_b09FwSbss, &bnx_COM_b09FwSbssLen },

	{ bnx_RXP_b09FwText, &bnx_RXP_b09FwTextLen },
	{ bnx_RXP_b09FwData, &bnx_RXP_b09FwDataLen },
	{ bnx_RXP_b09FwRodata, &bnx_RXP_b09FwRodataLen },
	{ bnx_RXP_b09FwBss, &bnx_RXP_b09FwBssLen },
	{ bnx_RXP_b09FwSbss, &bnx_RXP_b09FwSbssLen },

	{ bnx_TPAT_b09FwText, &bnx_TPAT_b09FwTextLen },
	{ bnx_TPAT_b09FwData, &bnx_TPAT_b09FwDataLen },
	{ bnx_TPAT_b09FwRodata, &bnx_TPAT_b09FwRodataLen },
	{ bnx_TPAT_b09FwBss, &bnx_TPAT_b09FwBssLen },
	{ bnx_TPAT_b09FwSbss, &bnx_TPAT_b09FwSbssLen },

	{ bnx_TXP_b09FwText, &bnx_TXP_b09FwTextLen },
	{ bnx_TXP_b09FwData, &bnx_TXP_b09FwDataLen },
	{ bnx_TXP_b09FwRodata, &bnx_TXP_b09FwRodataLen },
	{ bnx_TXP_b09FwBss, &bnx_TXP_b09FwBssLen },
	{ bnx_TXP_b09FwSbss, &bnx_TXP_b09FwSbssLen }
};

#define FILENAME_RV2P "bnx-rv2p"
struct chunks chunks_rv2p[] = {
	{ bnx_rv2p_proc1, &bnx_rv2p_proc1len },
	{ bnx_rv2p_proc2, &bnx_rv2p_proc2len }
};

#define FILENAME_XI_RV2P "bnx-xi-rv2p"
struct chunks chunks_xi_rv2p[] = {
	{ bnx_xi_rv2p_proc1, &bnx_rv2p_proc1len },
	{ bnx_xi_rv2p_proc2, &bnx_rv2p_proc2len }
};

#define FILENAME_XI90_RV2P "bnx-xi90-rv2p"
struct chunks chunks_xi90_rv2p[] = {
	{ bnx_xi90_rv2p_proc1, &bnx_rv2p_proc1len },
	{ bnx_xi90_rv2p_proc2, &bnx_rv2p_proc2len }
};

void
hswapn(u_int32_t *p, int wcount)
{
	for (; wcount; wcount -=4) {
		*p = htonl(*p);
		p++;
	}
}

void
write_firmware(char *filename, void *header, size_t hlen,
    struct chunks *chunks, u_int nchunks)
{
	int fd, i, total;
	ssize_t rlen;

	printf("creating %s", filename);
	fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", filename);

	rlen = write(fd, header, hlen);
	if (rlen == -1)
		err(1, "%s", filename);
	if (rlen != hlen)
		errx(1, "%s: short write", filename);
	total = rlen;
	printf(" [%d", total);
	fflush(stdout);

	for (i = 0; i < nchunks; i++) {
		hswapn(chunks[i].start, *chunks[i].len);
		rlen = write(fd, chunks[i].start, *chunks[i].len);
		if (rlen == -1) {
			printf("\n");
			err(1, "%s", filename);
		}
		if (rlen != *chunks[i].len) {
			printf("\n");
			errx(1, "%s: short write", filename);
		}
		printf("+%zd", rlen);
		fflush(stdout);
		total += rlen;
	}

	printf("] total %d\n", total);

	close(fd);
}

int
main(int argc, char *argv[])
{
	struct	bnx_firmware_header *bf;
	struct	bnx_rv2p_header	*rh;

	bf = (struct bnx_firmware_header *)malloc(sizeof *bf);
	bzero(bf, sizeof *bf);

	/* initialize the file header */
	bf->bnx_COM_FwReleaseMajor = htonl(bnx_COM_b06FwReleaseMajor);
	bf->bnx_COM_FwReleaseMinor = htonl(bnx_COM_b06FwReleaseMinor);
	bf->bnx_COM_FwReleaseFix = htonl(bnx_COM_b06FwReleaseFix);
	bf->bnx_COM_FwStartAddr = htonl(bnx_COM_b06FwStartAddr);
	bf->bnx_COM_FwTextAddr = htonl(bnx_COM_b06FwTextAddr);
	bf->bnx_COM_FwTextLen = htonl(bnx_COM_b06FwTextLen);
	bf->bnx_COM_FwDataAddr = htonl(bnx_COM_b06FwDataAddr);
	bf->bnx_COM_FwDataLen = htonl(bnx_COM_b06FwDataLen);
	bf->bnx_COM_FwRodataAddr = htonl(bnx_COM_b06FwRodataAddr);
	bf->bnx_COM_FwRodataLen = htonl(bnx_COM_b06FwRodataLen);
	bf->bnx_COM_FwBssAddr = htonl(bnx_COM_b06FwBssAddr);
	bf->bnx_COM_FwBssLen = htonl(bnx_COM_b06FwBssLen);
	bf->bnx_COM_FwSbssAddr = htonl(bnx_COM_b06FwSbssAddr);
	bf->bnx_COM_FwSbssLen = htonl(bnx_COM_b06FwSbssLen);

	bf->bnx_RXP_FwReleaseMajor = htonl(bnx_RXP_b06FwReleaseMajor);
	bf->bnx_RXP_FwReleaseMinor = htonl(bnx_RXP_b06FwReleaseMinor);
	bf->bnx_RXP_FwReleaseFix = htonl(bnx_RXP_b06FwReleaseFix);
	bf->bnx_RXP_FwStartAddr = htonl(bnx_RXP_b06FwStartAddr);
	bf->bnx_RXP_FwTextAddr = htonl(bnx_RXP_b06FwTextAddr);
	bf->bnx_RXP_FwTextLen = htonl(bnx_RXP_b06FwTextLen);
	bf->bnx_RXP_FwDataAddr = htonl(bnx_RXP_b06FwDataAddr);
	bf->bnx_RXP_FwDataLen = htonl(bnx_RXP_b06FwDataLen);
	bf->bnx_RXP_FwRodataAddr = htonl(bnx_RXP_b06FwRodataAddr);
	bf->bnx_RXP_FwRodataLen = htonl(bnx_RXP_b06FwRodataLen);
	bf->bnx_RXP_FwBssAddr = htonl(bnx_RXP_b06FwBssAddr);
	bf->bnx_RXP_FwBssLen = htonl(bnx_RXP_b06FwBssLen);
	bf->bnx_RXP_FwSbssAddr = htonl(bnx_RXP_b06FwSbssAddr);
	bf->bnx_RXP_FwSbssLen = htonl(bnx_RXP_b06FwSbssLen);

	bf->bnx_TPAT_FwReleaseMajor = htonl(bnx_TPAT_b06FwReleaseMajor);
	bf->bnx_TPAT_FwReleaseMinor = htonl(bnx_TPAT_b06FwReleaseMinor);
	bf->bnx_TPAT_FwReleaseFix = htonl(bnx_TPAT_b06FwReleaseFix);
	bf->bnx_TPAT_FwStartAddr = htonl(bnx_TPAT_b06FwStartAddr);
	bf->bnx_TPAT_FwTextAddr = htonl(bnx_TPAT_b06FwTextAddr);
	bf->bnx_TPAT_FwTextLen = htonl(bnx_TPAT_b06FwTextLen);
	bf->bnx_TPAT_FwDataAddr = htonl(bnx_TPAT_b06FwDataAddr);
	bf->bnx_TPAT_FwDataLen = htonl(bnx_TPAT_b06FwDataLen);
	bf->bnx_TPAT_FwRodataAddr = htonl(bnx_TPAT_b06FwRodataAddr);
	bf->bnx_TPAT_FwRodataLen = htonl(bnx_TPAT_b06FwRodataLen);
	bf->bnx_TPAT_FwBssAddr = htonl(bnx_TPAT_b06FwBssAddr);
	bf->bnx_TPAT_FwBssLen = htonl(bnx_TPAT_b06FwBssLen);
	bf->bnx_TPAT_FwSbssAddr = htonl(bnx_TPAT_b06FwSbssAddr);
	bf->bnx_TPAT_FwSbssLen = htonl(bnx_TPAT_b06FwSbssLen);

	bf->bnx_TXP_FwReleaseMajor = htonl(bnx_TXP_b06FwReleaseMajor);
	bf->bnx_TXP_FwReleaseMinor = htonl(bnx_TXP_b06FwReleaseMinor);
	bf->bnx_TXP_FwReleaseFix = htonl(bnx_TXP_b06FwReleaseFix);
	bf->bnx_TXP_FwStartAddr = htonl(bnx_TXP_b06FwStartAddr);
	bf->bnx_TXP_FwTextAddr = htonl(bnx_TXP_b06FwTextAddr);
	bf->bnx_TXP_FwTextLen = htonl(bnx_TXP_b06FwTextLen);
	bf->bnx_TXP_FwDataAddr = htonl(bnx_TXP_b06FwDataAddr);
	bf->bnx_TXP_FwDataLen = htonl(bnx_TXP_b06FwDataLen);
	bf->bnx_TXP_FwRodataAddr = htonl(bnx_TXP_b06FwRodataAddr);
	bf->bnx_TXP_FwRodataLen = htonl(bnx_TXP_b06FwRodataLen);
	bf->bnx_TXP_FwBssAddr = htonl(bnx_TXP_b06FwBssAddr);
	bf->bnx_TXP_FwBssLen = htonl(bnx_TXP_b06FwBssLen);
	bf->bnx_TXP_FwSbssAddr = htonl(bnx_TXP_b06FwSbssAddr);
	bf->bnx_TXP_FwSbssLen = htonl(bnx_TXP_b06FwSbssLen);

	write_firmware(FILENAME_B06, bf, sizeof(*bf), chunks_b06,
	    nitems(chunks_b06));

	bzero(bf, sizeof *bf);

	bf->bnx_COM_FwReleaseMajor = htonl(bnx_COM_b09FwReleaseMajor);
	bf->bnx_COM_FwReleaseMinor = htonl(bnx_COM_b09FwReleaseMinor);
	bf->bnx_COM_FwReleaseFix = htonl(bnx_COM_b09FwReleaseFix);
	bf->bnx_COM_FwStartAddr = htonl(bnx_COM_b09FwStartAddr);
	bf->bnx_COM_FwTextAddr = htonl(bnx_COM_b09FwTextAddr);
	bf->bnx_COM_FwTextLen = htonl(bnx_COM_b09FwTextLen);
	bf->bnx_COM_FwDataAddr = htonl(bnx_COM_b09FwDataAddr);
	bf->bnx_COM_FwDataLen = htonl(bnx_COM_b09FwDataLen);
	bf->bnx_COM_FwRodataAddr = htonl(bnx_COM_b09FwRodataAddr);
	bf->bnx_COM_FwRodataLen = htonl(bnx_COM_b09FwRodataLen);
	bf->bnx_COM_FwBssAddr = htonl(bnx_COM_b09FwBssAddr);
	bf->bnx_COM_FwBssLen = htonl(bnx_COM_b09FwBssLen);
	bf->bnx_COM_FwSbssAddr = htonl(bnx_COM_b09FwSbssAddr);
	bf->bnx_COM_FwSbssLen = htonl(bnx_COM_b09FwSbssLen);

	bf->bnx_RXP_FwReleaseMajor = htonl(bnx_RXP_b09FwReleaseMajor);
	bf->bnx_RXP_FwReleaseMinor = htonl(bnx_RXP_b09FwReleaseMinor);
	bf->bnx_RXP_FwReleaseFix = htonl(bnx_RXP_b09FwReleaseFix);
	bf->bnx_RXP_FwStartAddr = htonl(bnx_RXP_b09FwStartAddr);
	bf->bnx_RXP_FwTextAddr = htonl(bnx_RXP_b09FwTextAddr);
	bf->bnx_RXP_FwTextLen = htonl(bnx_RXP_b09FwTextLen);
	bf->bnx_RXP_FwDataAddr = htonl(bnx_RXP_b09FwDataAddr);
	bf->bnx_RXP_FwDataLen = htonl(bnx_RXP_b09FwDataLen);
	bf->bnx_RXP_FwRodataAddr = htonl(bnx_RXP_b09FwRodataAddr);
	bf->bnx_RXP_FwRodataLen = htonl(bnx_RXP_b09FwRodataLen);
	bf->bnx_RXP_FwBssAddr = htonl(bnx_RXP_b09FwBssAddr);
	bf->bnx_RXP_FwBssLen = htonl(bnx_RXP_b09FwBssLen);
	bf->bnx_RXP_FwSbssAddr = htonl(bnx_RXP_b09FwSbssAddr);
	bf->bnx_RXP_FwSbssLen = htonl(bnx_RXP_b09FwSbssLen);

	bf->bnx_TPAT_FwReleaseMajor = htonl(bnx_TPAT_b09FwReleaseMajor);
	bf->bnx_TPAT_FwReleaseMinor = htonl(bnx_TPAT_b09FwReleaseMinor);
	bf->bnx_TPAT_FwReleaseFix = htonl(bnx_TPAT_b09FwReleaseFix);
	bf->bnx_TPAT_FwStartAddr = htonl(bnx_TPAT_b09FwStartAddr);
	bf->bnx_TPAT_FwTextAddr = htonl(bnx_TPAT_b09FwTextAddr);
	bf->bnx_TPAT_FwTextLen = htonl(bnx_TPAT_b09FwTextLen);
	bf->bnx_TPAT_FwDataAddr = htonl(bnx_TPAT_b09FwDataAddr);
	bf->bnx_TPAT_FwDataLen = htonl(bnx_TPAT_b09FwDataLen);
	bf->bnx_TPAT_FwRodataAddr = htonl(bnx_TPAT_b09FwRodataAddr);
	bf->bnx_TPAT_FwRodataLen = htonl(bnx_TPAT_b09FwRodataLen);
	bf->bnx_TPAT_FwBssAddr = htonl(bnx_TPAT_b09FwBssAddr);
	bf->bnx_TPAT_FwBssLen = htonl(bnx_TPAT_b09FwBssLen);
	bf->bnx_TPAT_FwSbssAddr = htonl(bnx_TPAT_b09FwSbssAddr);
	bf->bnx_TPAT_FwSbssLen = htonl(bnx_TPAT_b09FwSbssLen);

	bf->bnx_TXP_FwReleaseMajor = htonl(bnx_TXP_b09FwReleaseMajor);
	bf->bnx_TXP_FwReleaseMinor = htonl(bnx_TXP_b09FwReleaseMinor);
	bf->bnx_TXP_FwReleaseFix = htonl(bnx_TXP_b09FwReleaseFix);
	bf->bnx_TXP_FwStartAddr = htonl(bnx_TXP_b09FwStartAddr);
	bf->bnx_TXP_FwTextAddr = htonl(bnx_TXP_b09FwTextAddr);
	bf->bnx_TXP_FwTextLen = htonl(bnx_TXP_b09FwTextLen);
	bf->bnx_TXP_FwDataAddr = htonl(bnx_TXP_b09FwDataAddr);
	bf->bnx_TXP_FwDataLen = htonl(bnx_TXP_b09FwDataLen);
	bf->bnx_TXP_FwRodataAddr = htonl(bnx_TXP_b09FwRodataAddr);
	bf->bnx_TXP_FwRodataLen = htonl(bnx_TXP_b09FwRodataLen);
	bf->bnx_TXP_FwBssAddr = htonl(bnx_TXP_b09FwBssAddr);
	bf->bnx_TXP_FwBssLen = htonl(bnx_TXP_b09FwBssLen);
	bf->bnx_TXP_FwSbssAddr = htonl(bnx_TXP_b09FwSbssAddr);
	bf->bnx_TXP_FwSbssLen = htonl(bnx_TXP_b09FwSbssLen);

	write_firmware(FILENAME_B09, bf, sizeof(*bf), chunks_b09,
	    nitems(chunks_b09));

	free(bf);

	rh = (struct bnx_rv2p_header *)malloc(sizeof *rh);

	bzero(rh, sizeof *rh);
	bnx_rv2p_proc1len = sizeof bnx_rv2p_proc1;
	bnx_rv2p_proc2len = sizeof bnx_rv2p_proc2;
	rh->bnx_rv2p_proc1len = htonl(bnx_rv2p_proc1len);
	rh->bnx_rv2p_proc2len = htonl(bnx_rv2p_proc2len);

	write_firmware(FILENAME_RV2P, rh, sizeof(*rh), chunks_rv2p,
	    nitems(chunks_rv2p));

	bzero(rh, sizeof *rh);
	bnx_rv2p_proc1len = sizeof bnx_xi_rv2p_proc1;
	bnx_rv2p_proc2len = sizeof bnx_xi_rv2p_proc2;
	rh->bnx_rv2p_proc1len = htonl(bnx_rv2p_proc1len);
	rh->bnx_rv2p_proc2len = htonl(bnx_rv2p_proc2len);

	write_firmware(FILENAME_XI_RV2P, rh, sizeof(*rh), chunks_xi_rv2p,
	    nitems(chunks_xi_rv2p));

	bzero(rh, sizeof *rh);
	bnx_rv2p_proc1len = sizeof bnx_xi90_rv2p_proc1;
	bnx_rv2p_proc2len = sizeof bnx_xi90_rv2p_proc2;
	rh->bnx_rv2p_proc1len = htonl(bnx_rv2p_proc1len);
	rh->bnx_rv2p_proc2len = htonl(bnx_rv2p_proc2len);

	write_firmware(FILENAME_XI90_RV2P, rh, sizeof(*rh), chunks_xi90_rv2p,
	    nitems(chunks_xi90_rv2p));

	free(rh);

	return 0;
}
