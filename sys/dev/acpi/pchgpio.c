/*	$OpenBSD: pchgpio.c,v 1.19 2025/08/21 03:12:11 jsg Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis
 * Copyright (c) 2020 James Hastings
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

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#define PCHGPIO_MAXCOM		5

#define PCHGPIO_CONF_TXSTATE		0x00000001
#define PCHGPIO_CONF_RXSTATE		0x00000002
#define PCHGPIO_CONF_RXINV		0x00800000
#define PCHGPIO_CONF_RXEV_EDGE		0x02000000
#define PCHGPIO_CONF_RXEV_ZERO		0x04000000
#define PCHGPIO_CONF_RXEV_MASK		0x06000000
#define PCHGPIO_CONF_PADRSTCFG_MASK	0xc0000000

#define PCHGPIO_PADBAR		0x00c

struct pchgpio_group {
	uint8_t		bar;
	uint8_t		bank;
	uint16_t	base;
	uint16_t	limit;
	int16_t		gpiobase;
};

struct pchgpio_device {
	uint16_t	pad_size;
	uint16_t	gpi_is;
	uint16_t	gpi_ie;
	const struct pchgpio_group *groups;
	int		ngroups;
	int		npins;
};

struct pchgpio_match {
	const char	*hid;
	const struct pchgpio_device *device;
};

struct pchgpio_pincfg {
	uint32_t	pad_cfg_dw0;
	uint32_t	pad_cfg_dw1;
	int		gpi_ie;
};

struct pchgpio_intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
};

struct pchgpio_softc {
	struct device sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;

	bus_space_tag_t sc_memt[PCHGPIO_MAXCOM];
	bus_space_handle_t sc_memh[PCHGPIO_MAXCOM];
	void *sc_ih;
	int sc_naddr;

	const struct pchgpio_device *sc_device;
	uint16_t sc_padbar[PCHGPIO_MAXCOM];
	uint16_t sc_padbase[PCHGPIO_MAXCOM];
	int sc_padsize;

	int sc_npins;
	struct pchgpio_pincfg *sc_pin_cfg;
	struct pchgpio_intrhand *sc_pin_ih;

	struct acpi_gpio sc_gpio;
};

int	pchgpio_match(struct device *, void *, void *);
void	pchgpio_attach(struct device *, struct device *, void *);
int	pchgpio_activate(struct device *, int);

const struct cfattach pchgpio_ca = {
	sizeof(struct pchgpio_softc), pchgpio_match, pchgpio_attach,
	NULL, pchgpio_activate
};

struct cfdriver pchgpio_cd = {
	NULL, "pchgpio", DV_DULL
};

const char *pchgpio_hids[] = {
	"INT344B",
	"INT3450",
	"INT3451",
	"INT345D",
	"INT34BB",
	"INT34C5",
	"INT34C6",
	"INTC1055",
	"INTC1056",
	"INTC1057",
	"INTC105E",
	"INTC1082",
	"INTC1083",
	"INTC1085",
	NULL
};

/* Sunrisepoint-LP */

const struct pchgpio_group spt_lp_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 23, 0 },		/* GPP_A */
	{ 0, 1, 24, 47, 24 },		/* GPP_B */

	/* Community 1 */
	{ 1, 0, 48, 71, 48 },		/* GPP_C */
	{ 1, 1, 72, 95, 72 },		/* GPP_D */
	{ 1, 2, 96, 119, 96 },		/* GPP_E */
	
	/* Community 3 */
	{ 2, 0, 120, 143, 120 },	/* GPP_F */
	{ 2, 1, 144, 151, 144 },	/* GPP_G */
};

const struct pchgpio_device spt_lp_device =
{
	.pad_size = 8,
	.gpi_is = 0x100,
	.gpi_ie = 0x120,
	.groups = spt_lp_groups,
	.ngroups = nitems(spt_lp_groups),
	.npins = 176,
};

/* Sunrisepoint-H */

const struct pchgpio_group spt_h_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 23, 0 },		/* GPP_A */
	{ 0, 1, 24, 47, 24 },		/* GPP_B */

	/* Community 1 */
	{ 1, 0, 48, 71, 48 },		/* GPP_C */
	{ 1, 1, 72, 95, 72 },		/* GPP_D */
	{ 1, 2, 96, 108, 96 },		/* GPP_E */
	{ 1, 3, 109, 132, 120 },	/* GPP_F */
	{ 1, 4, 133, 156, 144 },	/* GPP_G */
	{ 1, 5, 157, 180, 168 },	/* GPP_H */

	/* Community 3 */
	{ 2, 0, 181, 191, 192 },	/* GPP_I */
};

const struct pchgpio_device spt_h_device =
{
	.pad_size = 8,
	.gpi_is = 0x100,
	.gpi_ie = 0x120,
	.groups = spt_h_groups,
	.ngroups = nitems(spt_h_groups),
	.npins = 224,
};

/* Cannon Lake-H */

const struct pchgpio_group cnl_h_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 24, 0 },		/* GPP_A */
	{ 0, 1, 25, 50, 32 },		/* GPP_B */

	/* Community 1 */
	{ 1, 0, 51, 74, 64 },		/* GPP_C */
	{ 1, 1, 75, 98, 96 },		/* GPP_D */
	{ 1, 2, 99, 106, 128 },		/* GPP_G */

	/* Community 3 */
	{ 2, 0, 155, 178, 192 },	/* GPP_K */
	{ 2, 1, 179, 202, 224 },	/* GPP_H */
	{ 2, 2, 203, 215, 256 },	/* GPP_E */
	{ 2, 3, 216, 239, 288 },	/* GPP_F */

	/* Community 4 */
	{ 3, 2, 269, 286, 320 },	/* GPP_I */
	{ 3, 3, 287, 298, 352 },	/* GPP_J */
};

const struct pchgpio_device cnl_h_device =
{
	.pad_size = 16,
	.gpi_is = 0x100,
	.gpi_ie = 0x120,
	.groups = cnl_h_groups,
	.ngroups = nitems(cnl_h_groups),
	.npins = 384,
};

/* Cannon Lake-LP */

const struct pchgpio_group cnl_lp_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 24, 0 },		/* GPP_A */
	{ 0, 1, 25, 50, 32 },		/* GPP_B */
	{ 0, 2, 51, 58, 64 },		/* GPP_G */

	/* Community 1 */
	{ 1, 0, 68, 92, 96 },		/* GPP_D */
	{ 1, 1, 93, 116, 128 },		/* GPP_F */
	{ 1, 2, 117, 140, 160 },	/* GPP_H */

	/* Community 4 */
	{ 2, 0, 181, 204, 256 },	/* GPP_C */
	{ 2, 1, 205, 228, 288 },	/* GPP_E */
};

const struct pchgpio_device cnl_lp_device =
{
	.pad_size = 16,
	.gpi_is = 0x100,
	.gpi_ie = 0x120,
	.groups = cnl_lp_groups,
	.ngroups = nitems(cnl_lp_groups),
	.npins = 320,
};

/* Tiger Lake-LP */

const struct pchgpio_group tgl_lp_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 25, 0 },		/* GPP_B */
	{ 0, 1, 26, 41, 32 },		/* GPP_T */
	{ 0, 2, 42, 66, 64 },		/* GPP_A */

	/* Community 1 */
	{ 1, 0, 67, 74, 96 },		/* GPP_S */
	{ 1, 1, 75, 98, 128 },		/* GPP_H */
	{ 1, 2, 99, 119, 160 },		/* GPP_D */
	{ 1, 3, 120, 143, 192 },	/* GPP_U */

	/* Community 4 */
	{ 2, 0, 171, 194, 256 },	/* GPP_C */
	{ 2, 1, 195, 219, 288 },	/* GPP_F */
	{ 2, 3, 226, 250, 320 },	/* GPP_E */

	/* Community 5 */
	{ 3, 0, 260, 267, 352 },	/* GPP_R */
};

const struct pchgpio_device tgl_lp_device =
{
	.pad_size = 16,
	.gpi_is = 0x100,
	.gpi_ie = 0x120,
	.groups = tgl_lp_groups,
	.ngroups = nitems(tgl_lp_groups),
	.npins = 360,
};

/* Tiger Lake-H */

const struct pchgpio_group tgl_h_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 24, 0 },		/* GPP_A */
	{ 0, 1, 25, 44, 32 },		/* GPP_R */
	{ 0, 2, 45, 70, 64 },		/* GPP_B */

	/* Community 1 */
	{ 1, 0, 79, 104, 128 },		/* GPP_D */
	{ 1, 1, 105, 128, 160 },	/* GPP_C */
	{ 1, 2, 129, 136, 192 },	/* GPP_S */
	{ 1, 3, 137, 153, 224 },	/* GPP_G */

	/* Community 3 */
	{ 2, 0, 181, 193, 288 },	/* GPP_E */
	{ 2, 1, 194, 217, 320 },	/* GPP_F */

	/* Community 4 */
	{ 2, 0, 218, 241, 352 },	/* GPP_H */
	{ 2, 1, 242, 251, 384 },	/* GPP_J */
	{ 2, 2, 252, 266, 416 },	/* GPP_K */

	/* Community 5 */
	{ 3, 0, 267, 281, 448 },	/* GPP_I */
};

const struct pchgpio_device tgl_h_device =
{
	.pad_size = 16,
	.gpi_is = 0x100,
	.gpi_ie = 0x120,
	.groups = tgl_h_groups,
	.ngroups = nitems(tgl_h_groups),
	.npins = 480,
};

/* Alder Lake-S */

const struct pchgpio_group adl_s_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 24, 0 },		/* GPP_I */
	{ 0, 1, 25, 47, 32 },		/* GPP_R */
	{ 0, 2, 48, 59, 64 },		/* GPP_J */

	/* Community 1 */
	{ 1, 0, 95, 118, 160 },		/* GPP_B */
	{ 1, 1, 119, 126, 192 },	/* GPP_G */
	{ 1, 2, 127, 150, 224 },	/* GPP_H */

	/* Community 3 */
	{ 2, 1, 160, 175, 256 },	/* GPP_A */
	{ 2, 2, 176, 199, 288 },	/* GPP_C */

	/* Community 4 */
	{ 3, 0, 200, 207, 320 },	/* GPP_S */
	{ 3, 1, 208, 230, 352 },	/* GPP_E */
	{ 3, 2, 231, 245, 384 },	/* GPP_K */
	{ 3, 3, 246, 269, 416 },	/* GPP_F */

	/* Community 5 */
	{ 4, 0, 270, 294, 448 },	/* GPP_D */
};

const struct pchgpio_device adl_s_device =
{
	.pad_size = 16,
	.gpi_is = 0x200,
	.gpi_ie = 0x220,
	.groups = adl_s_groups,
	.ngroups = nitems(adl_s_groups),
	.npins = 480,
};

/* Alder Lake-N */

const struct pchgpio_group adl_n_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 25, 0 },		/* GPP_B */
	{ 0, 1, 26, 41, 32 },		/* GPP_T */
	{ 0, 2, 42, 66, 64 },		/* GPP_A */

	/* Community 1 */
	{ 1, 0, 67, 74, 96 },		/* GPP_S */
	{ 1, 1, 75, 94, 128 },		/* GPP_I */
	{ 1, 2, 95, 118, 160 },		/* GPP_H */
	{ 1, 3, 119, 139, 192 },	/* GPP_D */

	/* Community 4 */
	{ 2, 0, 169, 192, 256 },	/* GPP_C */
	{ 2, 1, 193, 217, 288 },	/* GPP_F */
	{ 2, 3, 224, 248, 320 },	/* GPP_E */

	/* Community 5 */
	{ 3, 0, 249, 256, 352 },	/* GPP_R */
};

const struct pchgpio_device adl_n_device =
{
	.pad_size = 16,
	.gpi_is = 0x100,
	.gpi_ie = 0x120,
	.groups = adl_n_groups,
	.ngroups = nitems(adl_n_groups),
	.npins = 384,
};

/* Meteor Lake-P */

const struct pchgpio_group mtl_p_groups[] =
{
	/* Community 0 */
	{ 0, 1, 5, 28, 32 },		/* GPP_V */
	{ 0, 2, 29, 52, 64 },		/* GPP_C */

	/* Community 1 */
	{ 1, 0, 53, 77, 96 },		/* GPP_A */
	{ 1, 1, 78, 102, 128 },		/* GPP_E */

	/* Community 3 */
	{ 2, 0, 103, 128, 160 },	/* GPP_H */
	{ 2, 1, 129, 154, 192 },	/* GPP_F */

	/* Community 4 */
	{ 3, 0, 184, 191, 288 },	/* GPP_S */

	/* Community 5 */
	{ 4, 0, 204, 228, 352 },	/* GPP_B */
	{ 4, 1, 229, 253, 384 },	/* GPP_D */
};

const struct pchgpio_device mtl_p_device =
{
	.pad_size = 16,
	.gpi_is = 0x200,
	.gpi_ie = 0x210,
	.groups = mtl_p_groups,
	.ngroups = nitems(mtl_p_groups),
	.npins = 416,
};

/* Meteor Lake-S */

const struct pchgpio_group mtl_s_groups[] =
{
	/* Community 0 */
	{ 0, 0, 0, 27, 0 },		/* GPP_A */
	{ 0, 2, 47, 73, 64 },		/* GPP_C */

	/* Community 1 */
	{ 1, 0, 74, 93, 96 },		/* GPP_B */
	{ 1, 2, 96, 119, 160 },		/* GPP_D */
};

const struct pchgpio_device mtl_s_device =
{
	.pad_size = 16,
	.gpi_is = 0x200,
	.gpi_ie = 0x210,
	.groups = mtl_s_groups,
	.ngroups = nitems(mtl_s_groups),
	.npins = 192,
};

struct pchgpio_match pchgpio_devices[] = {
	{ "INT344B", &spt_lp_device },
	{ "INT3450", &cnl_h_device },
	{ "INT3451", &spt_h_device },
	{ "INT345D", &spt_h_device },
	{ "INT34BB", &cnl_lp_device },
	{ "INT34C5", &tgl_lp_device },
	{ "INT34C6", &tgl_h_device },
	{ "INTC1055", &tgl_lp_device },
	{ "INTC1056", &adl_s_device },
	{ "INTC1057", &adl_n_device },
	{ "INTC1085", &adl_s_device },
	{ "INTC1082", &mtl_s_device },
	{ "INTC1083", &mtl_p_device },
	{ "INTC105E", &mtl_p_device },
};

int	pchgpio_read_pin(void *, int);
void	pchgpio_write_pin(void *, int, int);
void	pchgpio_intr_establish(void *, int, int, int, int (*)(void *), void *);
void	pchgpio_intr_enable(void *, int);
void	pchgpio_intr_disable(void *, int);
int	pchgpio_intr(void *);
void	pchgpio_save(struct pchgpio_softc *);
void	pchgpio_restore(struct pchgpio_softc *);

int
pchgpio_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, pchgpio_hids, cf->cf_driver->cd_name);
}

void
pchgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct pchgpio_softc *sc = (struct pchgpio_softc *)self;
	struct acpi_attach_args *aaa = aux;
	uint16_t bar;
	int i;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr");

	for (i = 0; i < aaa->aaa_naddr; i++) {
		printf(" 0x%llx/0x%llx", aaa->aaa_addr[i], aaa->aaa_size[i]);

		sc->sc_memt[i] = aaa->aaa_bst[i];
		if (bus_space_map(sc->sc_memt[i], aaa->aaa_addr[i],
		    aaa->aaa_size[i], 0, &sc->sc_memh[i])) {
			printf(": can't map registers\n");
			goto unmap;
		}

		sc->sc_padbar[i] = bus_space_read_4(sc->sc_memt[i],
		    sc->sc_memh[i], PCHGPIO_PADBAR);
		sc->sc_naddr++;
	}

	printf(" irq %d", aaa->aaa_irq[0]);

	for (i = 0; i < nitems(pchgpio_devices); i++) {
		if (strcmp(pchgpio_devices[i].hid, aaa->aaa_dev) == 0) {
			sc->sc_device = pchgpio_devices[i].device;
			break;
		}
	}
	KASSERT(sc->sc_device);

	/* Figure out the first pin for each community. */
	bar = -1;
	for (i = 0; i < sc->sc_device->ngroups; i++) {
		if (sc->sc_device->groups[i].bar != bar) {
			bar = sc->sc_device->groups[i].bar;
			sc->sc_padbase[bar] = sc->sc_device->groups[i].base;
		}
	}

	sc->sc_padsize = sc->sc_device->pad_size;
	sc->sc_npins = sc->sc_device->npins;
	sc->sc_pin_cfg = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_cfg),
	    M_DEVBUF, M_WAITOK);
	sc->sc_pin_ih = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_ih),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_BIO, pchgpio_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = pchgpio_read_pin;
	sc->sc_gpio.write_pin = pchgpio_write_pin;
	sc->sc_gpio.intr_establish = pchgpio_intr_establish;
	sc->sc_gpio.intr_enable = pchgpio_intr_enable;
	sc->sc_gpio.intr_disable = pchgpio_intr_disable;
	sc->sc_node->gpio = &sc->sc_gpio;

	printf(", %d pins\n", sc->sc_npins);

	acpi_register_gpio(sc->sc_acpi, sc->sc_node);
	return;

unmap:
	free(sc->sc_pin_ih, M_DEVBUF, sc->sc_npins * sizeof(*sc->sc_pin_ih));
	free(sc->sc_pin_cfg, M_DEVBUF, sc->sc_npins * sizeof(*sc->sc_pin_cfg));
	for (i = 0; i < sc->sc_naddr; i++)
		bus_space_unmap(sc->sc_memt[i], sc->sc_memh[i],
		    aaa->aaa_size[i]);
}

int
pchgpio_activate(struct device *self, int act)
{
	struct pchgpio_softc *sc = (struct pchgpio_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		pchgpio_save(sc);
		break;
	case DVACT_RESUME:
		pchgpio_restore(sc);
		break;
	}

	return 0;
}

const struct pchgpio_group *
pchgpio_find_group(struct pchgpio_softc *sc, int pin)
{
	int i, npads;

	for (i = 0; i < sc->sc_device->ngroups; i++) {
		npads = 1 + sc->sc_device->groups[i].limit -
		    sc->sc_device->groups[i].base;

		if (pin >= sc->sc_device->groups[i].gpiobase &&
		    pin < sc->sc_device->groups[i].gpiobase + npads)
			return &sc->sc_device->groups[i];
	}
	return NULL;
}

int
pchgpio_read_pin(void *cookie, int pin)
{
	struct pchgpio_softc *sc = cookie;
	const struct pchgpio_group *group;
	uint32_t reg;
	uint16_t pad;
	uint8_t bar;

	group = pchgpio_find_group(sc, pin);
	bar = group->bar;
	pad = group->base + (pin - group->gpiobase) - sc->sc_padbase[bar];

	reg = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_padbar[bar] + pad * sc->sc_padsize);

	return !!(reg & PCHGPIO_CONF_RXSTATE);
}

void
pchgpio_write_pin(void *cookie, int pin, int value)
{
	struct pchgpio_softc *sc = cookie;
	const struct pchgpio_group *group;
	uint32_t reg;
	uint16_t pad;
	uint8_t bar;

	group = pchgpio_find_group(sc, pin);
	bar = group->bar;
	pad = group->base + (pin - group->gpiobase) - sc->sc_padbase[bar];

	reg = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_padbar[bar] + pad * sc->sc_padsize);
	if (value)
		reg |= PCHGPIO_CONF_TXSTATE;
	else
		reg &= ~PCHGPIO_CONF_TXSTATE;
	bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_padbar[bar] + pad * sc->sc_padsize, reg);
}

void
pchgpio_intr_establish(void *cookie, int pin, int flags, int level,
    int (*func)(void *), void *arg)
{
	struct pchgpio_softc *sc = cookie;
	const struct pchgpio_group *group;
	uint32_t reg;
	uint16_t pad;
	uint8_t bank, bar;

	KASSERT(pin >= 0);

	group = pchgpio_find_group(sc, pin);
	if (group == NULL)
		return;

	bar = group->bar;
	bank = group->bank;
	pad = group->base + (pin - group->gpiobase) - sc->sc_padbase[bar];

	sc->sc_pin_ih[pin].ih_func = func;
	sc->sc_pin_ih[pin].ih_arg = arg;
	sc->sc_pin_ih[pin].ih_ipl = level & ~IPL_WAKEUP;

	reg = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_padbar[bar] + pad * sc->sc_padsize);
	reg &= ~(PCHGPIO_CONF_RXEV_MASK | PCHGPIO_CONF_RXINV);
	if ((flags & LR_GPIO_MODE) == 1)
		reg |= PCHGPIO_CONF_RXEV_EDGE;
	if ((flags & LR_GPIO_POLARITY) == LR_GPIO_ACTLO)
		reg |= PCHGPIO_CONF_RXINV;
	if ((flags & LR_GPIO_POLARITY) == LR_GPIO_ACTBOTH)
		reg |= PCHGPIO_CONF_RXEV_EDGE | PCHGPIO_CONF_RXEV_ZERO;
	bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_padbar[bar] + pad * sc->sc_padsize, reg);

	reg = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_device->gpi_ie + bank * 4);
	reg |= (1 << (pin - group->gpiobase));
	bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_device->gpi_ie + bank * 4, reg);
}

void
pchgpio_intr_enable(void *cookie, int pin)
{
	struct pchgpio_softc *sc = cookie;
	const struct pchgpio_group *group;
	uint32_t reg;
	uint16_t pad;
	uint8_t bank, bar;

	KASSERT(pin >= 0);

	group = pchgpio_find_group(sc, pin);
	if (group == NULL)
		return;

	bar = group->bar;
	bank = group->bank;
	pad = group->base + (pin - group->gpiobase) - sc->sc_padbase[bar];

	reg = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_device->gpi_ie + bank * 4);
	reg |= (1 << (pin - group->gpiobase));
	bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_device->gpi_ie + bank * 4, reg);
}

void
pchgpio_intr_disable(void *cookie, int pin)
{
	struct pchgpio_softc *sc = cookie;
	const struct pchgpio_group *group;
	uint32_t reg;
	uint16_t pad;
	uint8_t bank, bar;

	KASSERT(pin >= 0);

	group = pchgpio_find_group(sc, pin);
	if (group == NULL)
		return;

	bar = group->bar;
	bank = group->bank;
	pad = group->base + (pin - group->gpiobase) - sc->sc_padbase[bar];

	reg = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_device->gpi_ie + bank * 4);
	reg &= ~(1 << (pin - group->gpiobase));
	bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_device->gpi_ie + bank * 4, reg);
}

int
pchgpio_intr_handle(struct pchgpio_softc *sc, int group, int bit)
{
	uint32_t enable;
	int gpiobase, pin, s, handled = 0;
	uint8_t bank, bar;

	bar = sc->sc_device->groups[group].bar;
	bank = sc->sc_device->groups[group].bank;
	gpiobase = sc->sc_device->groups[group].gpiobase;

	pin = gpiobase + bit;
	if (sc->sc_pin_ih[pin].ih_func) {
		s = splraise(sc->sc_pin_ih[pin].ih_ipl);
		sc->sc_pin_ih[pin].ih_func(sc->sc_pin_ih[pin].ih_arg);
		splx(s);
		handled = 1;
	} else {
		/* Mask unhandled interrupt */
		enable = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_device->gpi_ie + bank * 4);
		enable &= ~(1 << bit);
		bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_device->gpi_ie + bank * 4, enable);
	}

	return handled;
}

int
pchgpio_intr(void *arg)
{
	struct pchgpio_softc *sc = arg;
	uint32_t status, enable;
	int group, bit, handled = 0;
	uint16_t base, limit;
	uint8_t bank, bar;

	for (group = 0; group < sc->sc_device->ngroups; group++) {
		bar = sc->sc_device->groups[group].bar;
		bank = sc->sc_device->groups[group].bank;
		base = sc->sc_device->groups[group].base;
		limit = sc->sc_device->groups[group].limit;

		status = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_device->gpi_is + bank * 4);
		bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_device->gpi_is + bank * 4, status);
		enable = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_device->gpi_ie + bank * 4);
		status &= enable;
		if (status == 0)
			continue;

		for (bit = 0; bit <= (limit - base); bit++) {
			if (status & (1 << bit))
				handled |= pchgpio_intr_handle(sc, group, bit);
		}
	}

	return handled;
}

void
pchgpio_save_pin(struct pchgpio_softc *sc, int pin)
{
	const struct pchgpio_group *group;
	uint32_t gpi_ie;
	uint16_t pad;
	uint8_t bank, bar;

	group = pchgpio_find_group(sc, pin);
	if (group == NULL)
		return;

	bar = group->bar;
	bank = group->bank;
	pad = group->base + (pin - group->gpiobase) - sc->sc_padbase[bar];

	sc->sc_pin_cfg[pin].pad_cfg_dw0 =
	    bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
		sc->sc_padbar[bar] + pad * sc->sc_padsize);
	sc->sc_pin_cfg[pin].pad_cfg_dw1 =
	    bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
		sc->sc_padbar[bar] + pad * sc->sc_padsize + 4);

	gpi_ie = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_device->gpi_ie + bank * 4);
	sc->sc_pin_cfg[pin].gpi_ie = (gpi_ie & (1 << (pin - group->gpiobase)));
}

void
pchgpio_save(struct pchgpio_softc *sc)
{
	int pin;

	for (pin = 0; pin < sc->sc_npins; pin++)
		pchgpio_save_pin(sc, pin);
}

void
pchgpio_restore_pin(struct pchgpio_softc *sc, int pin)
{
	const struct pchgpio_group *group;
	int restore = 0;
	uint32_t pad_cfg_dw0, gpi_ie;
	uint16_t pad;
	uint8_t bank, bar;

	group = pchgpio_find_group(sc, pin);
	if (group == NULL)
		return;

	bar = group->bar;
	bank = group->bank;
	pad = group->base + (pin - group->gpiobase) - sc->sc_padbase[bar];

	pad_cfg_dw0 = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
	    sc->sc_padbar[bar] + pad * sc->sc_padsize);

	if (sc->sc_pin_ih[pin].ih_func)
		restore = 1;

	/*
	 * The BIOS on Lenovo Thinkpads based on Intel's Tiger Lake
	 * platform have a bug where the GPIO pin that is used for the
	 * touchpad interrupt gets reset when entering S3 and isn't
	 * properly restored upon resume.  We detect this issue by
	 * comparing the bits in the PAD_CFG_DW0 register PADRSTCFG
	 * field before suspend and after resume and restore the pin
	 * configuration if the bits don't match.
	 */
	if ((sc->sc_pin_cfg[pin].pad_cfg_dw0 & PCHGPIO_CONF_PADRSTCFG_MASK) !=
	    (pad_cfg_dw0 & PCHGPIO_CONF_PADRSTCFG_MASK))
		restore = 1;

	if (restore) {
		bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_padbar[bar] + pad * sc->sc_padsize,
		    sc->sc_pin_cfg[pin].pad_cfg_dw0);
		bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_padbar[bar] + pad * sc->sc_padsize + 4,
		    sc->sc_pin_cfg[pin].pad_cfg_dw1);

		gpi_ie = bus_space_read_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_device->gpi_ie + bank * 4);
		if (sc->sc_pin_cfg[pin].gpi_ie)
			gpi_ie |= (1 << (pin - group->gpiobase));
		else
			gpi_ie &= ~(1 << (pin - group->gpiobase));
		bus_space_write_4(sc->sc_memt[bar], sc->sc_memh[bar],
		    sc->sc_device->gpi_ie + bank * 4, gpi_ie);
	}
}

void
pchgpio_restore(struct pchgpio_softc *sc)
{
	int pin;

	for (pin = 0; pin < sc->sc_npins; pin++)
		pchgpio_restore_pin(sc, pin);
}
