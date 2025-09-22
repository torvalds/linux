/*	$OpenBSD: abl.c,v 1.6 2025/01/23 11:24:34 kettenis Exp $ */

/*
 * Copyright (c) 2020 Marcus Glocker <mglocker@openbsd.org>
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
 * Driver for controlling the screen backlight brightness on Apple machines.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#ifdef ABL_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define ABL_IO_BASE_INTEL	0xb2
#define ABL_IO_BASE_NVIDIA	0x52e
#define ABL_IO_SIZE		2
#define ABL_IO_LO		0x00
#define ABL_IO_HI		0x01
#define ABL_BRIGHTNESS_MIN	0
#define ABL_BRIGHTNESS_MAX	15

struct abl_softc {
	struct device		 sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	bus_space_tag_t		 sc_bt;
	bus_space_handle_t	 sc_bh;

	bus_addr_t		 sc_io_base;
	uint8_t			 sc_brightness;
};

int	abl_match(struct device *, void *, void *);
void	abl_attach(struct device *, struct device *, void *);

int	abl_get_brightness(struct abl_softc *);
int	abl_set_brightness(struct abl_softc *, uint8_t);

/* Hooks for wsconsole brightness control. */
int	abl_get_param(struct wsdisplay_param *);
int	abl_set_param(struct wsdisplay_param *);

const struct cfattach abl_ca = {
	sizeof(struct abl_softc), abl_match, abl_attach, NULL, NULL
};

struct cfdriver abl_cd = {
	NULL, "abl", DV_DULL
};

const char *abl_hids[] = {
	"APP0002", NULL
};

int
abl_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata *cf = match;
	
	return acpi_matchhids(aa, abl_hids, cf->cf_driver->cd_name);
}

void
abl_attach(struct device *parent, struct device *self, void *aux)
{
	struct abl_softc *sc = (struct abl_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct aml_value res;
	int64_t sta;
	int reg;
	pci_chipset_tag_t pc;
	pcitag_t tag;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aaa->aaa_node;

	printf(": %s", sc->sc_devnode->name);

        sta = acpi_getsta(sc->sc_acpi, sc->sc_devnode);
	if ((sta & (STA_PRESENT | STA_ENABLED | STA_DEV_OK)) !=
	    (STA_PRESENT | STA_ENABLED | STA_DEV_OK)) {
		printf(": not enabled\n");
		return;
	}

	if (!(aml_evalname(sc->sc_acpi, sc->sc_devnode, "_CID", 0, NULL, &res)))
		printf(" (%s)", res.v_string);

	/* Backlight on non-iMacs is already handled differently. */
	if (strncmp(hw_prod, "iMac", 4)) {
		printf("\n");
		return;
	}

	/*
	 * We need to check on what type of PCI controller we're running on to
	 * access the right I/O space.
	 */
	pc = pci_lookup_segment(0, 0);
	tag = pci_make_tag(pc, 0, 0, 0);
	reg = pci_conf_read(pc, tag, PCI_ID_REG);

	switch (PCI_VENDOR(reg)) {
	case PCI_VENDOR_INTEL:
		sc->sc_io_base = ABL_IO_BASE_INTEL;
		break;
	case PCI_VENDOR_NVIDIA:
		sc->sc_io_base = ABL_IO_BASE_NVIDIA;
		break;
	default:
		printf(": pci controller not supported\n");
		return;
	}

	/*
	 * Map I/O space.  This driver uses the ACPI SMI command port.
	 * This port has already been claimed by the generic ACPI code
	 * so we need to work around that here by calling _bus_space_map().
	 */
	sc->sc_bt = aaa->aaa_iot;
	if (_bus_space_map(sc->sc_bt, sc->sc_io_base, ABL_IO_SIZE, 0,
	    &sc->sc_bh)) {
		printf(": can't map register\n");
		return;
	}

	/* Read the current brightness value initially. */
	sc->sc_brightness = abl_get_brightness(sc);

	/* Map wsconsole hook functions. */
	ws_get_param = abl_get_param;
	ws_set_param = abl_set_param;

	printf("\n");
}

int
abl_get_brightness(struct abl_softc *sc)
{
	uint8_t val;

	bus_space_write_1(sc->sc_bt, sc->sc_bh, ABL_IO_HI, 0x03);
	bus_space_write_1(sc->sc_bt, sc->sc_bh, ABL_IO_LO, 0xbf);
	val = bus_space_read_1(sc->sc_bt, sc->sc_bh, ABL_IO_HI);

	return (val >> 4);
}

int
abl_set_brightness(struct abl_softc *sc, uint8_t val)
{
	bus_space_write_1(sc->sc_bt, sc->sc_bh, ABL_IO_HI, 0x04 | (val << 4));
	bus_space_write_1(sc->sc_bt, sc->sc_bh, ABL_IO_LO, 0xbf);

	return 0;
}

int
abl_get_param(struct wsdisplay_param *dp)
{
	struct abl_softc *sc = abl_cd.cd_devs[0];

	if (sc == NULL)
		return -1;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		DPRINTF(("abl_get_param: sc->sc_brightness = %d\n",
		    sc->sc_brightness));
		dp->min = ABL_BRIGHTNESS_MIN;
		dp->max = ABL_BRIGHTNESS_MAX;
		dp->curval = sc->sc_brightness;
		return 0;
	default:
		return -1;
	}
}

int
abl_set_param(struct wsdisplay_param *dp)
{
	struct abl_softc *sc = abl_cd.cd_devs[0];

	if (sc == NULL)
		return -1;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		DPRINTF(("abl_set_param: curval = %d\n", dp->curval));
		if (dp->curval < ABL_BRIGHTNESS_MIN)
			dp->curval = 0;
		if (dp->curval > ABL_BRIGHTNESS_MAX)
			dp->curval = ABL_BRIGHTNESS_MAX;
		abl_set_brightness(sc, dp->curval);
		sc->sc_brightness = dp->curval;
		return 0;
	default:
		return -1;
	}
}
