/* $OpenBSD: arml2cc.c,v 1.8 2022/03/12 14:40:41 mpi Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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
#include <sys/systm.h>
#include <sys/device.h>
#include <machine/intr.h>
#include <machine/bus.h>
#include <arm/cpufunc.h>
#include <arm/cortex/cortex.h>
#include <arm/cortex/smc.h>

#define PL310_ERRATA_727915

/* offset from periphbase */
#define L2C_ADDR			0x2000
#define L2C_SIZE			0x1000

/* registers */
#define L2C_CACHE_ID			0x000
#define L2C_CACHE_TYPE			0x004
#define L2C_CTL				0x100
#define L2C_AUXCTL			0x104
#define L2C_TAG_RAM_CTL			0x108
#define L2C_DATA_RAM_CTL		0x10c
#define L2C_EVC_CTR_CTL			0x200
#define L2C_EVC_CTR0_CTL		0x204
#define L2C_EVC_CTR1_CTL		0x208
#define L2C_EVC_CTR0_VAL		0x20c
#define L2C_EVC_CTR1_VAL		0x210
#define L2C_INT_MASK			0x214
#define L2C_INT_MASK_STS		0x218
#define L2C_INT_RAW_STS			0x21c
#define L2C_INT_CLR			0x220
#define L2C_CACHE_SYNC			0x730
#define L2C_INV_PA			0x770
#define L2C_INV_WAY			0x77c
#define L2C_CLEAN_PA			0x7b0
#define L2C_CLEAN_INDEX			0x7b8
#define L2C_CLEAN_WAY			0x7bc
#define L2C_CLEAN_INV_PA		0x7f0
#define L2C_CLEAN_INV_INDEX		0x7f8
#define L2C_CLEAN_INV_WAY		0x7fc
#define L2C_D_LOCKDOWN0			0x900
#define L2C_I_LOCKDOWN0			0x904
#define L2C_D_LOCKDOWN1			0x908
#define L2C_I_LOCKDOWN1			0x90c
#define L2C_D_LOCKDOWN2			0x910
#define L2C_I_LOCKDOWN2			0x914
#define L2C_D_LOCKDOWN3			0x918
#define L2C_I_LOCKDOWN3			0x91c
#define L2C_D_LOCKDOWN4			0x920
#define L2C_I_LOCKDOWN4			0x924
#define L2C_D_LOCKDOWN5			0x928
#define L2C_I_LOCKDOWN5			0x92c
#define L2C_D_LOCKDOWN6			0x930
#define L2C_I_LOCKDOWN6			0x934
#define L2C_D_LOCKDOWN7			0x938
#define L2C_I_LOCKDOWN7			0x93c
#define L2C_LOCKDOWN_LINE_EN		0x950
#define L2C_UNLOCK_WAY			0x954
#define L2C_ADDR_FILTER_START		0xc00
#define L2C_ADDR_FILTER_END		0xc04
#define L2C_DEBUG_CTL			0xf40
#define L2C_PREFETCH_CTL		0xf60
#define L2C_POWER_CTL			0xf80

#define L2C_CACHE_ID_RELEASE_MASK	0x3f
#define L2C_CACHE_TYPE_LINESIZE		0x3
#define L2C_AUXCTL_ASSOC_SHIFT		16
#define L2C_AUXCTL_ASSOC_MASK		0x1

#define roundup2(size, unit) (((size) + (unit) - 1) & ~((unit) - 1))

struct arml2cc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	uint32_t		sc_enabled;
	uint32_t		sc_waymask;
	uint32_t		sc_dcache_line_size;
};

struct arml2cc_softc *arml2cc_sc;

int arml2cc_match(struct device *, void *, void *);
void arml2cc_attach(struct device *parent, struct device *self, void *args);
void arml2cc_enable(struct arml2cc_softc *);
void arml2cc_disable(struct arml2cc_softc *);
void arml2cc_sdcache_wbinv_all(void);
void arml2cc_sdcache_wbinv_range(vaddr_t, paddr_t, psize_t);
void arml2cc_sdcache_inv_range(vaddr_t, paddr_t, psize_t);
void arml2cc_sdcache_wb_range(vaddr_t, paddr_t, psize_t);
void arml2cc_cache_range_op(paddr_t, psize_t, bus_size_t);
void arml2cc_cache_way_op(struct arml2cc_softc *, bus_size_t, uint32_t);
void arml2cc_cache_op(struct arml2cc_softc *, bus_size_t, uint32_t);
void arml2cc_cache_sync(struct arml2cc_softc *);
void arml2cc_sdcache_drain_writebuf(void);

const struct cfattach armliicc_ca = {
	sizeof (struct arml2cc_softc), arml2cc_match, arml2cc_attach
};

struct cfdriver armliicc_cd = {
	NULL, "armliicc", DV_DULL
};

int
arml2cc_match(struct device *parent, void *cfdata, void *aux)
{
	if ((cpufunc_id() & CPU_ID_CORTEX_A9_MASK) == CPU_ID_CORTEX_A9)
		return (1);

	return (0);
}

void
arml2cc_attach(struct device *parent, struct device *self, void *args)
{
	struct cortex_attach_args *ia = args;
	struct arml2cc_softc *sc = (struct arml2cc_softc *) self;

	sc->sc_iot = ia->ca_iot;
	if (bus_space_map(sc->sc_iot, ia->ca_periphbase + L2C_ADDR,
	    L2C_SIZE, 0, &sc->sc_ioh))
		panic("arml2cc_attach: bus_space_map failed!");

	printf(": rtl %d", bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    L2C_CACHE_ID) & 0x3f);

	arml2cc_sc = sc;

	if (bus_space_read_4(sc->sc_iot, sc->sc_ioh, L2C_CTL))
		panic("L2 Cache controller was already enabled");

	sc->sc_dcache_line_size = 32 << (bus_space_read_4(sc->sc_iot, sc->sc_ioh, L2C_CACHE_TYPE) & L2C_CACHE_TYPE_LINESIZE);
	sc->sc_waymask = (8 << ((bus_space_read_4(sc->sc_iot, sc->sc_ioh, L2C_AUXCTL)
			    >> L2C_AUXCTL_ASSOC_SHIFT) & L2C_AUXCTL_ASSOC_MASK)) - 1;
	printf(" waymask: 0x%08x\n", sc->sc_waymask);

	arml2cc_enable(sc);
	sc->sc_enabled = 1;

	arml2cc_sdcache_wbinv_all();

	cpufuncs.cf_sdcache_wbinv_all = arml2cc_sdcache_wbinv_all;
	cpufuncs.cf_sdcache_wbinv_range = arml2cc_sdcache_wbinv_range;
	cpufuncs.cf_sdcache_inv_range = arml2cc_sdcache_inv_range;
	cpufuncs.cf_sdcache_wb_range = arml2cc_sdcache_wb_range;
	cpufuncs.cf_sdcache_drain_writebuf = arml2cc_sdcache_drain_writebuf;
}

void
arml2cc_enable(struct arml2cc_softc *sc)
{
	int s;
	s = splhigh();

	platform_smc_write(sc->sc_iot, sc->sc_ioh, L2C_CTL, SMC_L2_CTL,
	    1);

	arml2cc_cache_way_op(sc, L2C_INV_WAY, sc->sc_waymask);
	arml2cc_cache_sync(sc);

	splx(s);
}

void
arml2cc_disable(struct arml2cc_softc *sc)
{
	int s;
	s = splhigh();

	arml2cc_cache_way_op(sc, L2C_CLEAN_INV_WAY, sc->sc_waymask);
	arml2cc_cache_sync(sc);

	platform_smc_write(sc->sc_iot, sc->sc_ioh, L2C_CTL, SMC_L2_CTL, 0);

	splx(s);
}

void
arml2cc_cache_op(struct arml2cc_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, off, val);
	while (bus_space_read_4(sc->sc_iot, sc->sc_ioh, off) & 1) {
		/* spin */
	}
}

void
arml2cc_cache_way_op(struct arml2cc_softc *sc, bus_size_t off, uint32_t way_mask)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, off, way_mask);
	while (bus_space_read_4(sc->sc_iot, sc->sc_ioh, off) & way_mask) {
		/* spin */
	}
}

void
arml2cc_cache_sync(struct arml2cc_softc *sc)
{
	/* ARM Errata 753970 */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, 0x740, 0xffffffff);
}

void
arml2cc_sdcache_drain_writebuf(void)
{
	struct arml2cc_softc * const sc = arml2cc_sc;
	if (sc == NULL || !sc->sc_enabled)
		return;

	arml2cc_cache_sync(sc);
}

void
arml2cc_cache_range_op(paddr_t pa, psize_t len, bus_size_t cache_op)
{
	struct arml2cc_softc * const sc = arml2cc_sc;
	size_t line_size = sc->sc_dcache_line_size;
	size_t line_mask = line_size - 1;
	paddr_t endpa;

	endpa = pa + len;
	pa = pa & ~line_mask;

	// printf("l2inv op %x %08x %08x incr %d %d\n", cache_op, pa, endpa, line_size, len);
	while (endpa > pa) {
		arml2cc_cache_op(sc, cache_op, pa);
		pa += line_size;
	}
}

void
arml2cc_sdcache_wbinv_all(void)
{
	struct arml2cc_softc *sc = arml2cc_sc;
	if (sc == NULL || !sc->sc_enabled)
		return;

#ifdef PL310_ERRATA_727915
	platform_smc_write(sc->sc_iot, sc->sc_ioh, L2C_DEBUG_CTL, SMC_L2_DBG, 3);
#endif

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, L2C_CLEAN_INV_WAY, sc->sc_waymask);
	while(bus_space_read_4(sc->sc_iot, sc->sc_ioh, L2C_CLEAN_INV_WAY) & sc->sc_waymask);

#ifdef PL310_ERRATA_727915
	platform_smc_write(sc->sc_iot, sc->sc_ioh, L2C_DEBUG_CTL, SMC_L2_DBG, 0);
#endif

	arml2cc_cache_sync(sc);
}
void
arml2cc_sdcache_wbinv_range(vaddr_t va, paddr_t pa, psize_t len)
{
	struct arml2cc_softc *sc = arml2cc_sc;
	if (sc == NULL || !sc->sc_enabled)
		return;

#ifdef PL310_ERRATA_727915
	platform_smc_write(sc->sc_iot, sc->sc_ioh, L2C_DEBUG_CTL, SMC_L2_DBG, 3);
#endif

	arml2cc_cache_range_op(pa, len, L2C_CLEAN_INV_PA);
	arml2cc_cache_sync(sc);

#ifdef PL310_ERRATA_727915
	platform_smc_write(sc->sc_iot, sc->sc_ioh, L2C_DEBUG_CTL, SMC_L2_DBG, 0);
#endif
}

void
arml2cc_sdcache_inv_range(vaddr_t va, paddr_t pa, psize_t len)
{
	struct arml2cc_softc *sc = arml2cc_sc;
	if (sc == NULL || !sc->sc_enabled)
		return;
	arml2cc_cache_range_op(pa, len, L2C_INV_PA);
	arml2cc_cache_sync(sc);
}

void
arml2cc_sdcache_wb_range(vaddr_t va, paddr_t pa, psize_t len)
{
	struct arml2cc_softc *sc = arml2cc_sc;
	if (sc == NULL || !sc->sc_enabled)
		return;
	arml2cc_cache_range_op(pa, len, L2C_CLEAN_PA);
	arml2cc_cache_sync(sc);
}
