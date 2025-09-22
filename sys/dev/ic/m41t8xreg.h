/*	$OpenBSD: m41t8xreg.h,v 1.3 2020/10/23 20:55:15 patrick Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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

/*
 * ST M41T8x serial access real time clock registers
 *
 * http://www.st.com/stonline/products/literature/ds/9074/m41t80.pdf
 */

/*
 * Note that register update will stop while accessing registers 0x00 to 0x07
 * (without the clock stopping), so that the reader does not get false data.
 * Registers will be thawed after reading register 0x07, when the
 * autoincrementing read address reaches 0x08.
 *
 * Clock and alarm numerical values are stored in BCD. All other values are
 * stored in binary.
 */

#define	M41T8X_HSEC	0x00		/* 1/100th of second */
#define	M41T8X_SEC	0x01		/* second */
#define	M41T8X_STOP		0x80	/* stop clock, bit 7 of above */
#define	M41T8X_MIN	0x02		/* minute */
#define	M41T8X_HR	0x03		/* hour */
#define	M41T8X_CEB		0x80	/* century bit toggle enable */
#define	M41T8X_CB		0x40	/* century bit */
#define	M41T8X_DOW	0x04		/* day of week */
#define	M41T8X_DOW_MASK		0x07	/* day of week bits */
#define	M41T8X_DAY	0x05		/* day of month */
#define	M41T8X_MON	0x06		/* month */
#define	M41T8X_YEAR	0x07		/* year */

#define	M41T8X_TOD_START	0x00
#define	M41T8X_TOD_LENGTH	0x08

#define	M41T8X_CTRL	0x08		/* control */
#define	M41T8X_OUT		0x80	/* output level */
#define	M41T8X_32KHZ	0x09		/* 32KHz oscillator control */
#define	M41T8X_32KE		0x80	/* 32KHz enable */

#define	M41T8X_ALMON	0x0a		/* alarm month */
#define	M41T8X_AFE		0x80	/* alarm flag enable */
#define	M41T8X_SQWE		0x40	/* square wave enable */
#define	M41T8X_ALDAY	0x0b		/* alarm day */
#define	M41T8X_RPT4		0x80	/* alarm repeat mode bits */
#define	M41T8X_RPT5		0x40
#define	M41T8X_ALHOUR	0x0c		/* alarm hour */
#define	M41T8X_RPT3		0x80
#define	M41T8X_ALMIN	0x0d		/* alarm minute */
#define	M41T8X_RPT2		0x80
#define	M41T8X_ALSEC	0x0e		/* alarm second */
#define	M41T8X_RPT1		0x80

#define	M41T8X_FLAGS	0x0f		/* flags */
#define	M41T8X_AF		0x40	/* alarm flag */

#define	M41T8X_SQW	0x13		/* square wave control */
#define	M41T8X_RS3		0x80	/* square wave frequency */
#define	M41T8X_RS2		0x40
#define	M41T8X_RS1		0x20
#define	M41T8X_RS0		0x10

/* alarm repeat settings, RPT5..RPT1 */
#define	M41T8X_ALREP_SEC	0x1f	/* once per second */
#define	M41T8X_ALREP_MIN	0x1e	/* once per minute */
#define	M41T8X_ALREP_HOUR	0x1c	/* once per hour */
#define	M41T8X_ALREP_DAY	0x18	/* once per day */
#define	M41T8X_ALREP_MON	0x10	/* once per month */
#define	M41T8X_ALREP_YEAR	0x00	/* once per year */

/* square wave frequency, RS3..RS0 */
#define	M41T8X_SQW_32K		0x01
#define	M41T8X_SQW_8K		0x02
#define	M41T8X_SQW_4K		0x03
#define	M41T8X_SQW_2K		0x04
#define	M41T8X_SQW_1K		0x05
#define	M41T8X_SQW_512		0x06
#define	M41T8X_SQW_256		0x07
#define	M41T8X_SQW_128		0x08
#define	M41T8X_SQW_64		0x09
#define	M41T8X_SQW_32		0x0a
#define	M41T8X_SQW_16		0x0b
#define	M41T8X_SQW_8		0x0c
#define	M41T8X_SQW_4		0x0d
#define	M41T8X_SQW_2		0x0e
#define	M41T8X_SQW_1		0x0f
