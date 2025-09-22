/*	$OpenBSD: gscpmreg.h,v 1.1 2004/09/15 20:28:53 grange Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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

#ifndef _DEV_PCI_GSCPMREG_H_
#define _DEV_PCI_GSCPMREG_H_

/*
 * National Semiconductor Geode SC1100 SMI/ACPI module register definitions.
 */

/* PCI configuration space */
#define GSCPM_ACPIBASE		0x40	/* ACPI I/O space base address */
#define GSCPM_ACPISIZE		256	/* ACPI I/O space size */

/* ACPI I/O space */
#define GSCPM_P_CNT		0x00	/* processor control */
#define		GSCPM_P_CNT_CLK(val)	((val) & 0x7)
#define		GSCPM_P_CNT_THTEN	(1 << 4)  /* throttle enable */
#define GSCPM_PM_TMR		0x1c	/* PM timer */

/* CPU throttling values */
static const struct {
	int level;
	int value;
} gscpm_tht[] = {
	{ 88, 1 },
	{ 75, 2 },
	{ 63, 3 },
	{ 50, 4 },
	{ 38, 5 },
	{ 25, 6 },
	{ 13, 7 }
};

#define GSCPM_THT_LEVELS (sizeof(gscpm_tht) / sizeof(gscpm_tht[0]))

#endif	/* !_DEV_PCI_GSCPMREG_H_ */
