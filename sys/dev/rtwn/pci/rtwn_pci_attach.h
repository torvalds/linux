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

void	r92ce_attach(struct rtwn_pci_softc *);
void	r88ee_attach(struct rtwn_pci_softc *);

enum {
	RTWN_CHIP_RTL8192CE,
	RTWN_CHIP_RTL8188EE,
	RTWN_CHIP_MAX_PCI
};

struct rtwn_pci_ident {
	uint16_t	vendor;
	uint16_t	device;
	const char	*name;
	int		chip;
};

static const struct rtwn_pci_ident rtwn_pci_ident_table[] = {
	{ 0x10ec, 0x8176, "Realtek RTL8188CE", RTWN_CHIP_RTL8192CE },
	{ 0x10ec, 0x8179, "Realtek RTL8188EE", RTWN_CHIP_RTL8188EE },
	{ 0, 0, NULL, RTWN_CHIP_MAX_PCI }
};

typedef void	(*chip_pci_attach)(struct rtwn_pci_softc *);

static const chip_pci_attach rtwn_chip_pci_attach[RTWN_CHIP_MAX_PCI] = {
	[RTWN_CHIP_RTL8192CE] = r92ce_attach,
	[RTWN_CHIP_RTL8188EE] = r88ee_attach
};

static __inline void
rtwn_pci_attach_private(struct rtwn_pci_softc *pc, int chip)
{
	rtwn_chip_pci_attach[chip](pc);
}
