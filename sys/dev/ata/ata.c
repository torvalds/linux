/*      $OpenBSD: ata.c,v 1.37 2024/05/26 10:01:01 jsg Exp $      */
/*      $NetBSD: ata.c,v 1.9 1999/04/15 09:41:09 bouyer Exp $      */
/*
 * Copyright (c) 1998, 2001 Manuel Bouyer.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/pool.h>

#include <dev/ata/atareg.h>
#include <dev/ata/atavar.h>
#include <dev/ic/wdcreg.h>

#define DEBUG_FUNCS  0x08
#define DEBUG_PROBE  0x10
#ifdef WDCDEBUG
extern int wdcdebug_mask; /* init'ed in wdc.c */
#define WDCDEBUG_PRINT(args, level) do {	\
	if ((wdcdebug_mask & (level)) != 0)	\
		printf args;			\
} while (0)
#else
#define WDCDEBUG_PRINT(args, level)
#endif

#define ATAPARAMS_SIZE 512

/* Get the disk's parameters */
int
ata_get_params(struct ata_drive_datas *drvp, u_int8_t flags,
    struct ataparams *prms)
{
	char *tb;
	struct wdc_command wdc_c;
	int i;
	u_int16_t *p;
	int ret;

	WDCDEBUG_PRINT(("ata_get_params\n"), DEBUG_FUNCS);

	bzero(&wdc_c, sizeof(struct wdc_command));

	if (drvp->drive_flags & DRIVE_ATA) {
		wdc_c.r_command = WDCC_IDENTIFY;
		wdc_c.r_st_bmask = WDCS_DRDY;
		wdc_c.r_st_pmask = 0;
		wdc_c.timeout = 3000; /* 3s */
	} else if (drvp->drive_flags & DRIVE_ATAPI) {
		wdc_c.r_command = ATAPI_IDENTIFY_DEVICE;
		wdc_c.r_st_bmask = 0;
		wdc_c.r_st_pmask = 0;
		wdc_c.timeout = 10000; /* 10s */
	} else {
		WDCDEBUG_PRINT(("ata_get_params: no disks\n"),
		    DEBUG_FUNCS|DEBUG_PROBE);
		return CMD_ERR;
	}

	tb = dma_alloc(ATAPARAMS_SIZE, PR_NOWAIT | PR_ZERO);
	if (tb == NULL)
		return CMD_AGAIN;
	wdc_c.flags = AT_READ | flags;
	wdc_c.data = tb;
	wdc_c.bcount = ATAPARAMS_SIZE;

	if ((ret = wdc_exec_command(drvp, &wdc_c)) != WDC_COMPLETE) {
		WDCDEBUG_PRINT(("%s: wdc_exec_command failed: %d\n",
		    __func__, ret), DEBUG_PROBE);
		dma_free(tb, ATAPARAMS_SIZE);
		return CMD_AGAIN;
	}

	if (wdc_c.flags & (AT_ERROR | AT_TIMEOU | AT_DF)) {
		WDCDEBUG_PRINT(("%s: IDENTIFY failed: 0x%x\n", __func__,
		    wdc_c.flags), DEBUG_PROBE);

		dma_free(tb, ATAPARAMS_SIZE);
		return CMD_ERR;
	} else {
#if BYTE_ORDER == BIG_ENDIAN
		/* All the fields in the params structure are 16-bit
		   integers except for the ID strings which are char
		   strings.  The 16-bit integers are currently in
		   memory in little-endian, regardless of architecture.
		   So, they need to be swapped on big-endian architectures
		   before they are accessed through the ataparams structure.

		   The swaps below avoid touching the char strings.
		*/

		swap16_multi((u_int16_t *)tb, 10);
		swap16_multi((u_int16_t *)tb + 20, 3);
		swap16_multi((u_int16_t *)tb + 47, ATAPARAMS_SIZE / 2 - 47);
#endif
		/* Read in parameter block. */
		bcopy(tb, prms, sizeof(struct ataparams));

		/*
		 * Shuffle string byte order.
		 * ATAPI Mitsumi and NEC drives don't need this.
		 */
		if ((prms->atap_config & WDC_CFG_ATAPI_MASK) ==
		    WDC_CFG_ATAPI &&
		    ((prms->atap_model[0] == 'N' &&
			prms->atap_model[1] == 'E') ||
		     (prms->atap_model[0] == 'F' &&
			 prms->atap_model[1] == 'X'))) {
			dma_free(tb, ATAPARAMS_SIZE);
			return CMD_OK;
		}
		for (i = 0; i < sizeof(prms->atap_model); i += 2) {
			p = (u_short *)(prms->atap_model + i);
			*p = swap16(*p);
		}
		for (i = 0; i < sizeof(prms->atap_serial); i += 2) {
			p = (u_short *)(prms->atap_serial + i);
			*p = swap16(*p);
		}
		for (i = 0; i < sizeof(prms->atap_revision); i += 2) {
			p = (u_short *)(prms->atap_revision + i);
			*p = swap16(*p);
		}

		dma_free(tb, ATAPARAMS_SIZE);
		return CMD_OK;
	}
}

int
ata_set_mode(struct ata_drive_datas *drvp, u_int8_t mode, u_int8_t flags)
{
	struct wdc_command wdc_c;

	WDCDEBUG_PRINT(("%s: mode=0x%x, flags=0x%x\n", __func__,
	    mode, flags), DEBUG_PROBE);

	bzero(&wdc_c, sizeof(struct wdc_command));

	wdc_c.r_command = SET_FEATURES;
	wdc_c.r_st_bmask = 0;
	wdc_c.r_st_pmask = 0;
	wdc_c.r_features = WDSF_SET_MODE;
	wdc_c.r_count = mode;
	wdc_c.flags = flags;
	wdc_c.timeout = 1000; /* 1s */
	if (wdc_exec_command(drvp, &wdc_c) != WDC_COMPLETE)
		return CMD_AGAIN;

	WDCDEBUG_PRINT(("%s: after wdc_exec_command() wdc_c.flags=0x%x\n",
	    __func__, wdc_c.flags), DEBUG_PROBE);

	if (wdc_c.flags & (AT_ERROR | AT_TIMEOU | AT_DF)) {
		return CMD_ERR;
	}
	return CMD_OK;
}

void
ata_dmaerr(struct ata_drive_datas *drvp)
{
	/*
	 * Downgrade decision: if we get NERRS_MAX in NXFER.
	 * We start with n_dmaerrs set to NERRS_MAX-1 so that the
	 * first error within the first NXFER ops will immediately trigger
	 * a downgrade.
	 * If we got an error and n_xfers is bigger than NXFER reset counters.
	 */
	drvp->n_dmaerrs++;
	if (drvp->n_dmaerrs >= NERRS_MAX && drvp->n_xfers <= NXFER) {
		wdc_downgrade_mode(drvp);
		drvp->n_dmaerrs = NERRS_MAX-1;
		drvp->n_xfers = 0;
		return;
	}
	if (drvp->n_xfers > NXFER) {
		drvp->n_dmaerrs = 1; /* just got an error */
		drvp->n_xfers = 1; /* restart counting from this error */
	}
}

void
ata_perror(struct ata_drive_datas *drvp, int errno, char *buf, size_t buf_len)
{
	static char *errstr0_3[] = {"address mark not found",
	    "track 0 not found", "aborted command", "media change requested",
	    "id not found", "media changed", "uncorrectable data error",
	    "bad block detected"};
	static char *errstr4_5[] = {"",
	    "no media/write protected", "aborted command",
	    "media change requested", "id not found", "media changed",
	    "uncorrectable data error", "interface CRC error"};
	char **errstr;
	int i;
	char *sep = "";
	size_t len = 0;

	if (drvp->ata_vers >= 4)
		errstr = errstr4_5;
	else
		errstr = errstr0_3;

	if (errno == 0) {
		snprintf(buf, buf_len, "error not notified");
	}

	for (i = 0; i < 8; i++) {
		if (errno & (1 << i)) {
			snprintf(buf + len, buf_len - len, "%s%s", sep,
			    errstr[i]);
			len = strlen(buf);
			sep = ", ";
		}
	}
}
