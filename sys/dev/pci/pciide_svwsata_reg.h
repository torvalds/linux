/*	$OpenBSD: pciide_svwsata_reg.h,v 1.4 2006/02/10 21:45:41 kettenis Exp $	*/

/*
 * Copyright (c) 2005 Mark Kettenis
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

#ifndef _DEV_PCI_PCIIDE_SVWSATA_REG_H_
#define _DEV_PCI_PCIIDE_SVWSATA_REG_H_

struct pciide_svwsata {
	bus_space_tag_t		ba5_st;
	bus_space_handle_t	ba5_sh;
};

#define SVWSATA_TF0		0x00
#define SVWSATA_TF8		0x20

#define SVWSATA_DMA		0x30

#define SVWSATA_SSTATUS		0x40
#define SVWSATA_SERROR		0x44
#define SVWSATA_SCONTROL	0x48

#define SVWSATA_SICR1		0x80
#define SVWSATA_SICR2		0x84
#define SVWSATA_SIM		0x88

u_int8_t svwsata_read_reg(struct channel_softc *, enum wdc_regs);
void     svwsata_write_reg(struct channel_softc *, enum wdc_regs, u_int8_t);
void     svwsata_lba48_write_reg(struct channel_softc *, enum wdc_regs, u_int16_t);

struct channel_softc_vtbl wdc_svwsata_vtbl = {
	svwsata_read_reg,
	svwsata_write_reg,
	svwsata_lba48_write_reg,
	wdc_default_read_raw_multi_2,
	wdc_default_write_raw_multi_2,
	wdc_default_read_raw_multi_4,
	wdc_default_write_raw_multi_4
};

#endif	/* !_DEV_PCI_PCIIDE_SVWSATA_REG_H_ */
