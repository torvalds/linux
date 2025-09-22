/*	$OpenBSD: sti_pci.c,v 1.14 2024/08/17 08:45:22 miod Exp $	*/

/*
 * Copyright (c) 2006, 2007, 2023 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
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
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsdisplayvar.h>

#include <dev/ic/stireg.h>
#include <dev/ic/stivar.h>

int	sti_pci_match(struct device *, void *, void *);
void	sti_pci_attach(struct device *, struct device *, void *);

struct	sti_pci_softc {
	struct sti_softc	sc_base;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;

	bus_space_handle_t	sc_romh;
};

const struct cfattach sti_pci_ca = {
	sizeof(struct sti_pci_softc), sti_pci_match, sti_pci_attach
};

const struct pci_matchid sti_pci_devices[] = {
	{ PCI_VENDOR_HP, PCI_PRODUCT_HP_VISUALIZE_EG },
	{ PCI_VENDOR_HP, PCI_PRODUCT_HP_VISUALIZE_FX2 },
	{ PCI_VENDOR_HP, PCI_PRODUCT_HP_VISUALIZE_FX4 },
	{ PCI_VENDOR_HP, PCI_PRODUCT_HP_VISUALIZE_FX6 },
	{ PCI_VENDOR_HP, PCI_PRODUCT_HP_VISUALIZE_FXE },
#ifdef notyet
	{ PCI_VENDOR_IBM, PCI_PRODUCT_IBM_FIREGL2 }
#endif
};

int	sti_readbar(struct sti_softc *, struct pci_attach_args *, u_int, int);
int	sti_check_rom(struct sti_pci_softc *, struct pci_attach_args *);
void	sti_pci_enable_rom(struct sti_softc *);
void	sti_pci_disable_rom(struct sti_softc *);

int	sti_pci_is_console(struct pci_attach_args *, bus_addr_t *);

int
sti_pci_match(struct device *parent, void *cf, void *aux)
{
	struct pci_attach_args *paa = aux;

	return pci_matchbyid(paa, sti_pci_devices,
	    sizeof(sti_pci_devices) / sizeof(sti_pci_devices[0]));
}

void
sti_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct sti_pci_softc *spc = (void *)self;
	struct pci_attach_args *paa = aux;

	spc->sc_pc = paa->pa_pc;
	spc->sc_tag = paa->pa_tag;
	spc->sc_base.sc_enable_rom = sti_pci_enable_rom;
	spc->sc_base.sc_disable_rom = sti_pci_disable_rom;

	printf("\n");

	if (sti_check_rom(spc, paa) != 0)
		return;

	printf("%s", self->dv_xname);
	if (sti_pci_is_console(paa, spc->sc_base.bases) != 0)
		spc->sc_base.sc_flags |= STI_CONSOLE;
	if (sti_attach_common(&spc->sc_base, paa->pa_iot, paa->pa_memt,
	    spc->sc_romh, STI_CODEBASE_MAIN) == 0)
		startuphook_establish(sti_end_attach, spc);
}

/*
 * Enable PCI ROM.
 */
void
sti_pci_enable_rom(struct sti_softc *sc)
{
	struct sti_pci_softc *spc = (struct sti_pci_softc *)sc;
	pcireg_t address;

	if (!ISSET(sc->sc_flags, STI_ROM_ENABLED)) {
		address = pci_conf_read(spc->sc_pc, spc->sc_tag, PCI_ROM_REG);
		address |= PCI_ROM_ENABLE;
		pci_conf_write(spc->sc_pc, spc->sc_tag, PCI_ROM_REG, address);
		SET(sc->sc_flags, STI_ROM_ENABLED);
	}
}

/*
 * Disable PCI ROM.
 */
void
sti_pci_disable_rom(struct sti_softc *sc)
{
	struct sti_pci_softc *spc = (struct sti_pci_softc *)sc;
	pcireg_t address;

	if (ISSET(sc->sc_flags, STI_ROM_ENABLED)) {
		address = pci_conf_read(spc->sc_pc, spc->sc_tag, PCI_ROM_REG);
		address &= ~PCI_ROM_ENABLE;
		pci_conf_write(spc->sc_pc, spc->sc_tag, PCI_ROM_REG, address);

		CLR(sc->sc_flags, STI_ROM_ENABLED);
	}
}

/*
 * We have to be extremely careful with output in this file, as the
 * device we are trying to attach might be the console, and we are
 * still using the PDC routines for output at this point.
 *
 * On some devices, if not all, PDC routines assume the STI ROM is *NOT*
 * mapped when they are invoked, and they will cause the system to freeze
 * if it is mapped.
 *
 * As a result, we need to make sure the ROM is not mapped when invoking
 * printf(). The following wrapper takes care of this to reduce the risk
 * of making a mistake.
 */

static int
sti_local_printf(struct sti_softc *sc, const char *fmt, ...)
{
	va_list ap;
	int rc;
	int enabled = sc->sc_flags & STI_ROM_ENABLED;

	if (enabled)
		sti_pci_disable_rom(sc);
	va_start(ap, fmt);
	rc = vprintf(fmt, ap);
	if (enabled)
		sti_pci_enable_rom(sc);

	return rc;
}

#define printf(fmt, ...) sti_local_printf(sc, fmt, ## __VA_ARGS__)

/*
 * PCI ROM Data Structure (6.3.1.2)
 */

struct pcirom_ds {
	uint32_t signature;
	uint16_t vid;
	uint16_t pid;
	uint16_t reserved_8;
	uint16_t dslen;
	uint8_t rev;
	uint8_t class[3];
	uint16_t romlen;
	uint16_t level;
	uint8_t arch;
	uint8_t indicator;
	uint16_t reserved_16;
};

/*
 * Callback data used while walking PCI ROM images to pick the most
 * appropriate image.
 */

struct sti_pcirom_walk_ctx {
#ifdef STIDEBUG
	struct sti_softc *sc;
#endif
	bus_addr_t romoffs;
};

int	sti_pcirom_check(bus_space_tag_t, bus_space_handle_t, bus_addr_t,
	    const struct pcirom_ds *, void *);
int	sti_pcirom_walk(struct sti_softc *, bus_space_tag_t, bus_space_handle_t,
	    bus_size_t, int (*)(bus_space_tag_t, bus_space_handle_t, bus_addr_t,
	    const struct pcirom_ds *, void *), void *);

/*
 * Grovel the STI ROM image.
 */

int
sti_check_rom(struct sti_pci_softc *spc, struct pci_attach_args *pa)
{
	struct sti_softc *sc = &spc->sc_base;
	pcireg_t address, mask;
	bus_space_handle_t romh;
	bus_size_t romsize, stiromsize;
	bus_addr_t offs;
	uint8_t region_bars[STI_REGION_MAX];
	int i;
	int rc;

	struct sti_pcirom_walk_ctx ctx;

	/* sort of inline sti_pci_enable_rom(sc) */
	address = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, ~PCI_ROM_ENABLE);
	mask = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	address |= PCI_ROM_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, address);
	sc->sc_flags |= STI_ROM_ENABLED;

	/*
	 * Map the complete ROM for now.
	 */

	romsize = PCI_ROM_SIZE(mask);
	rc = bus_space_map(pa->pa_memt, PCI_ROM_ADDR(address), romsize,
	    0, &romh);
	if (rc != 0) {
		printf("%s: can't map PCI ROM (%d)\n",
		    sc->sc_dev.dv_xname, rc);
		goto disable_return;
	}

	/*
	 * Iterate over the ROM images, pick the best candidate.
	 */

#ifdef STIDEBUG
	ctx.sc = sc;
#endif
	ctx.romoffs = (bus_addr_t)-1;
	rc = sti_pcirom_walk(sc, pa->pa_memt, romh, romsize, sti_pcirom_check,
	    &ctx);

	if (ctx.romoffs == (bus_addr_t)-1) {
		if (rc == 0) {
			printf("%s: found no ROM with correct microcode"
			    " architecture\n", sc->sc_dev.dv_xname);
			rc = ENOEXEC;
		}
		goto unmap_disable_return;
	}

	/*
	 * Read the STI region BAR assignments.
	 */

	offs = ctx.romoffs +
	    (bus_addr_t)bus_space_read_2(pa->pa_memt, romh, ctx.romoffs + 0x0e);
	bus_space_read_region_1(pa->pa_memt, romh, offs, region_bars,
	    STI_REGION_MAX);
	for (i = 0; i < STI_REGION_MAX; i++) {
		/*
		 * Region 0 is supposed to always be the ROM. FireGL-UX
		 * ROM agrees so well that it will report the expansion
		 * ROM BAR rather than any regular BAR.
		 * We'll address this later after remapping the ROM.
		 */
		if (i == 0 && region_bars[i] == PCI_ROM_REG)
			continue;

		rc = sti_readbar(sc, pa, i, region_bars[i]);
		if (rc != 0)
			goto unmap_disable_return;
	}

	/*
	 * Find out where the STI ROM itself lies within the PCI ROM,
	 * and its size.
	 */

	offs = ctx.romoffs +
	    (bus_addr_t)bus_space_read_4(pa->pa_memt, romh, ctx.romoffs + 0x08);
	stiromsize = (bus_addr_t)bus_space_read_4(pa->pa_memt, romh,
	    offs + 0x18);
	stiromsize = letoh32(stiromsize);

	/*
	 * Replace our mapping with a smaller mapping of only the area
	 * we are interested in.
	 */

	bus_space_unmap(pa->pa_memt, romh, romsize);
	rc = bus_space_map(pa->pa_memt, PCI_ROM_ADDR(address) + offs,
	    stiromsize, 0, &spc->sc_romh);
	if (rc != 0) {
		printf("%s: can't map STI ROM (%d)\n",
		    sc->sc_dev.dv_xname, rc);
		goto disable_return;
	}

	/*
	 * Now set up region 0 if we had skipped it earlier.
	 */

	if (region_bars[0] == PCI_ROM_REG) {
		sc->bases[0] =
		    (bus_addr_t)bus_space_vaddr(pa->pa_memt, spc->sc_romh) -
		    (offs - ctx.romoffs);
	}

	sti_pci_disable_rom(sc);
	return 0;

unmap_disable_return:
	bus_space_unmap(pa->pa_memt, romh, romsize);
disable_return:
	sti_pci_disable_rom(sc);
	return rc;
}

/*
 * Decode a BAR register.
 */
int
sti_readbar(struct sti_softc *sc, struct pci_attach_args *pa, u_int region,
    int bar)
{
	bus_addr_t addr;
	bus_size_t size;
	pcireg_t type;
	int rc;

	if (bar == 0) {
		sc->bases[region] = 0;
		return 0;
	}

	if (bar < PCI_MAPREG_START || bar > PCI_MAPREG_PPB_END) {
#ifdef DIAGNOSTIC
		printf("%s: unexpected bar %02x for region %d\n",
		    sc->sc_dev.dv_xname, bar, region);
#endif
		return EINVAL;
	}

	type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, bar);
	rc = pci_mapreg_info(pa->pa_pc, pa->pa_tag, bar, type, &addr, &size,
	    NULL);
	if (rc != 0) {
		printf("%s: invalid bar %02x for region %d\n",
		    sc->sc_dev.dv_xname, bar, region);
		return rc;
	}

	sc->bases[region] = addr;
	return 0;
}

/*
 * Check a PCI ROM image for an STI ROM.
 */

int
sti_pcirom_check(bus_space_tag_t romt, bus_space_handle_t romh, bus_addr_t offs,
    const struct pcirom_ds *ds, void *v)
{
	struct sti_pcirom_walk_ctx *ctx = v;
#ifdef STIDEBUG
	struct sti_softc *sc = ctx->sc;
#endif
	uint32_t tmp32;

	/*
	 * Check for a valid STI ROM header.
	 */

	tmp32 = bus_space_read_4(romt, romh, offs + 0);
	tmp32 = letoh32(tmp32);
	if (tmp32 != 0x55aa0000) {
		/* Not an STI ROM image, onto the next */
#ifdef STIDEBUG
		printf("Invalid HP ROM signature (%08x)\n", tmp32);
#endif
		return 0;
	}

	/*
	 * Check ROM type.
	 */

	tmp32 = bus_space_read_4(romt, romh, offs + 4);
	tmp32 = letoh32(tmp32);
	if (tmp32 != 0x00000001) {	/* 1 == STI ROM */
#ifdef STIDEBUG
		printf("Unknown HP ROM type (%08x)\n", tmp32);
#endif
		return 0;
	}

#ifdef STIDEBUG
	printf("ROM architecture code %02x", ds->arch);
#endif
	switch (ds->arch) {
#ifdef __hppa__
	/*
	 * The PCI specification assigns value 0x02 to PA-RISC, but
	 * according to the STI specification (and to hardware),
	 * the correct value to check for is 0x10.
	 */
	case 0x10:
		if (ctx->romoffs == (bus_addr_t)-1)
			ctx->romoffs = offs;
		break;
#endif
#ifdef __i386__
	case 0x00:
		if (ctx->romoffs == (bus_addr_t)-1)
			ctx->romoffs = offs;
		break;
#endif
	default:
#ifdef STIDEBUG
		printf(" (wrong architecture)");
#endif
		break;
	}

#ifdef STIDEBUG
	if (ctx->romoffs == offs)
		printf(" -> SELECTED");
	printf("\n");
#endif

	return 0;
}

/*
 * Iterate over all PCI ROM images.
 * This code is absolutely not related to sti(4) and could be moved
 * elsewhere in case other drivers have a need for it, but is kept
 * here for now due to the printf() wrapper requirement (which is why
 * there is an otherwise unnecessary softc argument).
 */

int
sti_pcirom_walk(struct sti_softc *sc, bus_space_tag_t romt,
    bus_space_handle_t romh, bus_size_t romsize, int (*cb)(bus_space_tag_t,
    bus_space_handle_t, bus_addr_t, const struct pcirom_ds *, void *),
    void *cbarg)
{
	bus_addr_t offs;
	bus_size_t subsize;
	int rc = 0;

	for (offs = 0; offs < romsize; offs += subsize) {
		struct pcirom_ds ds;
		bus_addr_t dsoffs;
		uint16_t tmp16;

#ifdef STIDEBUG
		printf("Checking for ROM image at offset %08lx/%08lx\n",
		    offs, romsize);
#endif

		/*
		 * Check for a valid ROM header (6.3.1.1).
		 */

		tmp16 = bus_space_read_2(romt, romh, offs + 0);
		tmp16 = letoh16(tmp16);
		if (tmp16 != 0x55aa) {
			if (offs == 0) {
				printf("%s: invalid PCI ROM header signature"
				    " (%04x)\n",
				    sc->sc_dev.dv_xname, tmp16);
				rc = EINVAL;
			}
			break;
		}

		/*
		 * Check for a valid ROM data structure (6.3.1.2).
		 */

		dsoffs = (bus_addr_t)bus_space_read_2(romt, romh, offs + 0x18);
#ifdef STIDEBUG
		printf("PCI DS offset %04lx\n", dsoffs);
#endif
		if ((dsoffs & 0x03) != 0 || dsoffs < 0x1a ||
		    offs + dsoffs + sizeof(struct pcirom_ds) > romsize) {
			if (offs == 0) {
				printf("%s: ill-formed PCI Data Structure"
				    " (offset %04lx)\n",
				    sc->sc_dev.dv_xname, dsoffs);
				rc = EINVAL;
			} else {
#ifdef STIDEBUG
				printf("Ill-formed PCI Data Structure"
				    " (offset %04lx)\n", dsoffs);
#endif
			}
			break;
		}

		bus_space_read_region_1(romt, romh, offs + dsoffs,
		    (uint8_t *)&ds, sizeof ds);
		/* convert sizes to host endianness */
		ds.dslen = letoh16(ds.dslen);
		ds.romlen = letoh16(ds.romlen);
#if 0 /* not used in this code */
		ds.vid = letoh16(ds.vid);
		ds.pid = letoh16(ds.pid);
		ds.level = letoh16(ds.level);
#endif
		if (ds.signature != 0x50434952) {	/* PCIR */
			if (offs == 0) {
				printf("%s: invalid PCI data signature"
				    " (%08x)\n",
				    sc->sc_dev.dv_xname, ds.signature);
				rc = EINVAL;
			} else {
#ifdef STIDEBUG
				printf(" invalid PCI data signature %08x\n",
				    ds.signature);
#endif
			}
			break;
		}

#ifdef STIDEBUG
		printf("PCI DS length %04lx\n", ds.dslen);
#endif
		if (ds.dslen < sizeof ds || dsoffs + ds.dslen > romsize) {
			if (offs == 0) {
				printf("%s: ill-formed PCI Data Structure"
				    " (size %04lx)\n",
				    sc->sc_dev.dv_xname, ds.dslen);
				rc = EINVAL;
			} else {
#ifdef STIDEBUG
				printf("Ill-formed PCI Data Structure"
				    " (size %04lx)\n", ds.dslen);
#endif
			}
			break;
		}

		subsize = ((bus_size_t)ds.romlen) << 9;
#ifdef STIDEBUG
		printf("ROM image size %08lx\n", subsize);
#endif
		if (subsize == 0 || offs + subsize > romsize) {
			if (offs == 0) {
				printf("%s: invalid ROM image size"
				    " (%04lx)\n",
				    sc->sc_dev.dv_xname, subsize);
				rc = EINVAL;
			} else {
#ifdef STIDEBUG
				printf("Invalid ROM image size"
				    " (%04lx)\n", subsize);
#endif
			}
			break;
		}

		rc = (*cb)(romt, romh, offs, &ds, cbarg);
		if (rc != 0)
			break;

		if ((ds.indicator & 0x80) != 0) {
			/* no more ROM images */
#ifdef STIDEBUG
			printf("no more ROM images\n");
#endif
			break;
		}
	}

	return rc;
}
