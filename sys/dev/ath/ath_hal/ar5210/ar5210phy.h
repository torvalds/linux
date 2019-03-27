/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2004 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 *
 * $FreeBSD$
 */
#ifndef _DEV_ATH_AR5210PHY_H
#define _DEV_ATH_AR5210PHY_H

/*
 * Definitions for the PHY on the Atheros AR5210 parts.
 */

/* PHY Registers */
#define	AR_PHY_BASE		0x9800		/* PHY register base */
#define	AR_PHY(_n)		(AR_PHY_BASE + ((_n)<<2))

#define	AR_PHY_FRCTL		0x9804		/* PHY frame control */
#define	AR_PHY_TURBO_MODE	0x00000001	/* PHY turbo mode */
#define	AR_PHY_TURBO_SHORT	0x00000002	/* PHY turbo short symbol */
#define	AR_PHY_TIMING_ERR	0x01000000	/* Detect PHY timing error */
#define	AR_PHY_PARITY_ERR	0x02000000	/* Detect signal parity err */
#define	AR_PHY_ILLRATE_ERR	0x04000000	/* Detect PHY illegal rate */
#define	AR_PHY_ILLLEN_ERR	0x08000000	/* Detect PHY illegal length */
#define	AR_PHY_SERVICE_ERR	0x20000000	/* Detect PHY nonzero service */
#define	AR_PHY_TXURN_ERR	0x40000000	/* DetectPHY TX underrun */
#define	AR_PHY_FRCTL_BITS \
	"\20\1TURBO_MODE\2TURBO_SHORT\30TIMING_ERR\31PARITY_ERR\32ILLRATE_ERR"\
	"\33ILLEN_ERR\35SERVICE_ERR\36TXURN_ERR"

#define	AR_PHY_AGC		0x9808		/* PHY AGC command */
#define	AR_PHY_AGC_DISABLE	0x08000000	/* Disable PHY AGC */
#define	AR_PHY_AGC_BITS	"\20\33DISABLE"

#define	AR_PHY_CHIPID		0x9818		/* PHY chip revision */

#define	AR_PHY_ACTIVE		0x981c		/* PHY activation */
#define	AR_PHY_ENABLE		0x00000001	/* activate PHY */
#define	AR_PHY_DISABLE		0x00000002	/* deactivate PHY */
#define	AR_PHY_ACTIVE_BITS	"\20\1ENABLE\2DISABLE"

#define	AR_PHY_AGCCTL		0x9860		/* PHY calibration and noise floor */
#define	AR_PHY_AGC_CAL		0x00000001	/* PHY internal calibration */
#define	AR_PHY_AGC_NF		0x00000002	/* calc PHY noise-floor */
#define	AR_PHY_AGCCTL_BITS	"\20\1CAL\2NF"

#endif /* _DEV_ATH_AR5210PHY_H */
