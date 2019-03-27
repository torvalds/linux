/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef RTWN_USB_REG_H
#define RTWN_USB_REG_H

static __inline uint16_t
rtwn_usb_calc_tx_checksum(void *buf)
{
	uint16_t sum = 0;
	int i;

	/* NB: checksum calculation takes into account only first 32 bytes. */
	for (i = 0; i < 32 / 2; i++)
		sum ^= ((uint16_t *)buf)[i];

	return (sum);		/* NB: already little endian. */
}

int		rtwn_usb_write_region_1(struct rtwn_softc *, uint16_t,
		    uint8_t *, int);
int		rtwn_usb_write_1(struct rtwn_softc *, uint16_t, uint8_t);
int		rtwn_usb_write_2(struct rtwn_softc *, uint16_t, uint16_t);
int		rtwn_usb_write_4(struct rtwn_softc *, uint16_t, uint32_t);
uint8_t		rtwn_usb_read_1(struct rtwn_softc *, uint16_t);
uint16_t	rtwn_usb_read_2(struct rtwn_softc *, uint16_t);
uint32_t	rtwn_usb_read_4(struct rtwn_softc *, uint16_t);
void		rtwn_usb_delay(struct rtwn_softc *, int);

#endif	/* RTWN_USB_REG_H */
