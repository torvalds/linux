/*	$OpenBSD: elroy.c,v 1.13 2024/05/22 14:25:47 jsg Exp $	*/

/*
 * Copyright (c) 2005 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "cardbus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/reboot.h>
#include <sys/malloc.h>
#include <sys/extent.h>

#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <arch/hppa/dev/cpudevs.h>

#if NCARDBUS > 0
#include <dev/cardbus/rbus.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <hppa/dev/elroyreg.h>
#include <hppa/dev/elroyvar.h>

#define	ELROY_MEM_CHUNK		0x800000
#define	ELROY_MEM_WINDOW	(2 * ELROY_MEM_CHUNK)

int	elroy_match(struct device *, void *, void *);
void	elroy_attach(struct device *, struct device *, void *);

const struct cfattach elroy_ca = {
	sizeof(struct elroy_softc), elroy_match, elroy_attach
};

struct cfdriver elroy_cd = {
	NULL, "elroy", DV_DULL
};

void		 elroy_attach_hook(struct device *parent, struct device *self,
		    struct pcibus_attach_args *pba);
int		 elroy_maxdevs(void *v, int bus);
pcitag_t	 elroy_make_tag(void *v, int bus, int dev, int func);
void		 elroy_decompose_tag(void *v, pcitag_t tag, int *bus,
		    int *dev, int *func);
int		 elroy_conf_size(void *v, pcitag_t tag);
pcireg_t	 elroy_conf_read(void *v, pcitag_t tag, int reg);
void		 elroy_conf_write(void *v, pcitag_t tag, int reg,
		    pcireg_t data);
int		 elroy_iomap(void *v, bus_addr_t bpa, bus_size_t size,
		    int flags, bus_space_handle_t *bshp);
int		 elroy_memmap(void *v, bus_addr_t bpa, bus_size_t size,
		    int flags, bus_space_handle_t *bshp);
int		 elroy_subregion(void *v, bus_space_handle_t bsh,
		    bus_size_t offset, bus_size_t size,
		    bus_space_handle_t *nbshp);
int		 elroy_ioalloc(void *v, bus_addr_t rstart, bus_addr_t rend,
		    bus_size_t size, bus_size_t align, bus_size_t boundary,
		    int flags, bus_addr_t *addrp, bus_space_handle_t *bshp);
int		 elroy_memalloc(void *v, bus_addr_t rstart, bus_addr_t rend,
		    bus_size_t size, bus_size_t align, bus_size_t boundary,
		    int flags, bus_addr_t *addrp, bus_space_handle_t *bshp);
void		 elroy_unmap(void *v, bus_space_handle_t bsh,
		    bus_size_t size);
void		 elroy_free(void *v, bus_space_handle_t bh, bus_size_t size);
void		 elroy_barrier(void *v, bus_space_handle_t h, bus_size_t o,
		    bus_size_t l, int op);
void *		 elroy_alloc_parent(struct device *self,
		    struct pci_attach_args *pa, int io);
void *		 elroy_vaddr(void *v, bus_space_handle_t h);
u_int8_t	 elroy_r1(void *v, bus_space_handle_t h, bus_size_t o);
u_int16_t	 elroy_r2(void *v, bus_space_handle_t h, bus_size_t o);
u_int32_t	 elroy_r4(void *v, bus_space_handle_t h, bus_size_t o);
u_int64_t	 elroy_r8(void *v, bus_space_handle_t h, bus_size_t o);
void		 elroy_w1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t vv);
void		 elroy_w2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv);
void		 elroy_w4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv);
void		 elroy_w8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t vv);
void		 elroy_rm_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_rm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t *a, bus_size_t c);
void		 elroy_rm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t *a, bus_size_t c);
void		 elroy_rm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t *a, bus_size_t c);
void		 elroy_wm_1(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_wm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int16_t *a, bus_size_t c);
void		 elroy_wm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int32_t *a, bus_size_t c);
void		 elroy_wm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int64_t *a, bus_size_t c);
void		 elroy_sm_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t vv, bus_size_t c);
void		 elroy_sm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv, bus_size_t c);
void		 elroy_sm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv, bus_size_t c);
void		 elroy_sm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t vv, bus_size_t c);
void		 elroy_rrm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_rrm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_rrm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_wrm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_wrm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_wrm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_rr_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_rr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t *a, bus_size_t c);
void		 elroy_rr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t *a, bus_size_t c);
void		 elroy_rr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t *a, bus_size_t c);
void		 elroy_wr_1(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_wr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int16_t *a, bus_size_t c);
void		 elroy_wr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int32_t *a, bus_size_t c);
void		 elroy_wr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int64_t *a, bus_size_t c);
void		 elroy_rrr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_rrr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_rrr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 elroy_wrr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_wrr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_wrr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 elroy_sr_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t vv, bus_size_t c);
void		 elroy_sr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv, bus_size_t c);
void		 elroy_sr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv, bus_size_t c);
void		 elroy_sr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t vv, bus_size_t c);
void		 elroy_cp_1(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
void		 elroy_cp_2(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
void		 elroy_cp_4(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
void		 elroy_cp_8(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);

int
elroy_match(struct device *parent, void *cfdata, void *aux)
{
	struct confargs *ca = aux;
	/* struct cfdata *cf = cfdata; */

	if ((ca->ca_name && !strcmp(ca->ca_name, "lba")) ||
	    (ca->ca_type.iodc_type == HPPA_TYPE_BRIDGE &&
	     ca->ca_type.iodc_sv_model == HPPA_BRIDGE_DINO &&
	     ca->ca_type.iodc_model == 0x78))
		return (1);

	return (0);
}

void
elroy_write32(volatile u_int32_t *p, u_int32_t v)
{
	*p = v;
}

u_int32_t
elroy_read32(volatile u_int32_t *p)
{
	return *p;
}

void
elroy_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{

}

int
elroy_maxdevs(void *v, int bus)
{
	return (32);
}

pcitag_t
elroy_make_tag(void *v, int bus, int dev, int func)
{
	if (bus > 255 || dev > 31 || func > 7)
		panic("elroy_make_tag: bad request");

	return ((bus << 16) | (dev << 11) | (func << 8));
}

void
elroy_decompose_tag(void *v, pcitag_t tag, int *bus, int *dev, int *func)
{
	if (bus)
		*bus = (tag >> 16) & 0xff;
	if (dev)
		*dev = (tag >> 11) & 0x1f;
	if (func)
		*func= (tag >>  8) & 0x07;
}

int
elroy_conf_size(void *v, pcitag_t tag)
{
	return PCI_CONFIG_SPACE_SIZE;
}

pcireg_t
elroy_conf_read(void *v, pcitag_t tag, int reg)
{
	struct elroy_softc *sc = v;
	volatile struct elroy_regs *r = sc->sc_regs;
	u_int32_t arb_mask, err_cfg, control;
	pcireg_t data, data1;

/* printf("elroy_conf_read(%p, 0x%08x, 0x%x)", v, tag, reg); */
	arb_mask = elroy_read32(&r->arb_mask);
	err_cfg = elroy_read32(&r->err_cfg);
	control = elroy_read32(&r->control);
	if (!arb_mask)
		elroy_write32(&r->arb_mask, htole32(ELROY_ARB_ENABLE));
	elroy_write32(&r->err_cfg, err_cfg |
	    htole32(ELROY_ERRCFG_SMART | ELROY_ERRCFG_CM));
	elroy_write32(&r->control, (control | htole32(ELROY_CONTROL_CE)) &
	    ~htole32(ELROY_CONTROL_HF));

	elroy_write32(&r->pci_conf_addr, htole32(tag | reg));
	data1 = elroy_read32(&r->pci_conf_addr);
	data = elroy_read32(&r->pci_conf_data);

	elroy_write32(&r->control, control |
	    htole32(ELROY_CONTROL_CE|ELROY_CONTROL_CL));
	elroy_write32(&r->control, control);
	elroy_write32(&r->err_cfg, err_cfg);
	if (!arb_mask)
		elroy_write32(&r->arb_mask, arb_mask);

	data = letoh32(data);
/* printf("=0x%08x (@ 0x%08x)\n", data, letoh32(data1)); */
	return (data);
}

void
elroy_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct elroy_softc *sc = v;
	volatile struct elroy_regs *r = sc->sc_regs;
	u_int32_t arb_mask, err_cfg, control;
	pcireg_t data1;

/* printf("elroy_conf_write(%p, 0x%08x, 0x%x, 0x%x)\n", v, tag, reg, data); */
	arb_mask = elroy_read32(&r->arb_mask);
	err_cfg = elroy_read32(&r->err_cfg);
	control = elroy_read32(&r->control);
	if (!arb_mask)
		elroy_write32(&r->arb_mask, htole32(ELROY_ARB_ENABLE));
	elroy_write32(&r->err_cfg, err_cfg |
	    htole32(ELROY_ERRCFG_SMART | ELROY_ERRCFG_CM));
	elroy_write32(&r->control, (control | htole32(ELROY_CONTROL_CE)) &
	    ~htole32(ELROY_CONTROL_HF));

	/* fix coalescing config writes errata by interleaving w/ a read */
	elroy_write32(&r->pci_conf_addr, htole32(tag | PCI_ID_REG));
	data1 = elroy_read32(&r->pci_conf_addr);
	data1 = elroy_read32(&r->pci_conf_data);

	elroy_write32(&r->pci_conf_addr, htole32(tag | reg));
	data1 = elroy_read32(&r->pci_conf_addr);
	elroy_write32(&r->pci_conf_data, htole32(data));
	data1 = elroy_read32(&r->pci_conf_addr);

	elroy_write32(&r->control, control |
	    htole32(ELROY_CONTROL_CE|ELROY_CONTROL_CL));
	elroy_write32(&r->control, control);
	elroy_write32(&r->err_cfg, err_cfg);
	if (!arb_mask)
		elroy_write32(&r->arb_mask, arb_mask);
}

int
elroy_iomap(void *v, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct elroy_softc *sc = v;
	/* volatile struct elroy_regs *r = sc->sc_regs; */
	int error;

	if ((error = bus_space_map(sc->sc_bt, bpa + sc->sc_iobase, size,
	    flags, bshp)))
		return (error);

	return (0);
}

int
elroy_memmap(void *v, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct elroy_softc *sc = v;
	/* volatile struct elroy_regs *r = sc->sc_regs; */
	int error;

	if ((error = bus_space_map(sc->sc_bt, bpa, size, flags, bshp)))
		return (error);

	return (0);
}

int
elroy_subregion(void *v, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;
	return (0);
}

int
elroy_ioalloc(void *v, bus_addr_t rstart, bus_addr_t rend, bus_size_t size,
    bus_size_t align, bus_size_t boundary, int flags, bus_addr_t *addrp,
    bus_space_handle_t *bshp)
{
	struct elroy_softc *sc = v;
	volatile struct elroy_regs *r = sc->sc_regs;
	bus_addr_t iostart, ioend;

	iostart = r->io_base & ~htole32(ELROY_BASE_RE);
	ioend = iostart + ~htole32(r->io_mask) + 1;
	if (rstart < iostart || rend > ioend)
		panic("elroy_ioalloc: bad region start/end");

	rstart += sc->sc_iobase;
	rend += sc->sc_iobase;
	if (bus_space_alloc(sc->sc_bt, rstart, rend, size,
	    align, boundary, flags, addrp, bshp))
		return (ENOMEM);

	return (0);
}

int
elroy_memalloc(void *v, bus_addr_t rstart, bus_addr_t rend, bus_size_t size,
    bus_size_t align, bus_size_t boundary, int flags, bus_addr_t *addrp,
    bus_space_handle_t *bshp)
{
	struct elroy_softc *sc = v;
	/* volatile struct elroy_regs *r = sc->sc_regs; */

	if (bus_space_alloc(sc->sc_bt, rstart, rend, size,
	    align, boundary, flags, addrp, bshp))
		return (ENOMEM);

	return (0);
}

void
elroy_unmap(void *v, bus_space_handle_t bsh, bus_size_t size)
{
	struct elroy_softc *sc = v;

	bus_space_free(sc->sc_bt, bsh, size);
}

void
elroy_free(void *v, bus_space_handle_t bh, bus_size_t size)
{
	/* should be enough */
	elroy_unmap(v, bh, size);
}

void
elroy_barrier(void *v, bus_space_handle_t h, bus_size_t o, bus_size_t l, int op)
{
	struct elroy_softc *sc = v;
	volatile struct elroy_regs *r = sc->sc_regs;
	u_int32_t data;

	sync_caches();
	if (op & BUS_SPACE_BARRIER_WRITE) {
		data = r->pci_id;	/* flush write fifo */
		sync_caches();
	}
}

#if NCARDBUS > 0
void *
elroy_alloc_parent(struct device *self, struct pci_attach_args *pa, int io)
{
#if 0	/* TODO */

	struct elroy_softc *sc = pa->pa_pc->_cookie;
	struct extent *ex;
	bus_space_tag_t tag;
	bus_addr_t start;
	bus_size_t size;

	if (io) {
		ex = sc->sc_ioex;
		tag = pa->pa_iot;
		start = 0xa000;
		size = 0x1000;
	} else {
		if (!sc->sc_memex) {
			bus_space_handle_t memh;
			bus_addr_t mem_start;

			if (elroy_memalloc(sc, 0xf0800000, 0xff7fffff,
			    ELROY_MEM_WINDOW, ELROY_MEM_WINDOW, EX_NOBOUNDARY,
			    0, &mem_start, &memh))
				return (NULL);

			snprintf(sc->sc_memexname, sizeof(sc->sc_memexname),
			    "%s_mem", sc->sc_dv.dv_xname);
			if ((sc->sc_memex = extent_create(sc->sc_memexname,
			    mem_start, mem_start + ELROY_MEM_WINDOW, M_DEVBUF,
			    NULL, 0, EX_NOWAIT | EX_MALLOCOK)) == NULL) {
				extent_destroy(sc->sc_ioex);
				bus_space_free(sc->sc_bt, memh,
				    ELROY_MEM_WINDOW);
				return (NULL);
			}
		}
		ex = sc->sc_memex;
		tag = pa->pa_memt;
		start = ex->ex_start;
		size = ELROY_MEM_CHUNK;
	}

	if (extent_alloc_subregion(ex, start, ex->ex_end, size, size, 0,
	    EX_NOBOUNDARY, EX_NOWAIT, &start))
		return (NULL);

	extent_free(ex, start, size, EX_NOWAIT);
	return rbus_new_root_share(tag, ex, start, size);
#else
	return (NULL);
#endif
}
#endif

void *
elroy_vaddr(void *v, bus_space_handle_t h)
{
	return ((void *)h);
}

u_int8_t
elroy_r1(void *v, bus_space_handle_t h, bus_size_t o)
{
	h += o;
	return *(volatile u_int8_t *)h;
}

u_int16_t
elroy_r2(void *v, bus_space_handle_t h, bus_size_t o)
{
	volatile u_int16_t *p;

	h += o;
	p = (volatile u_int16_t *)h;
	return (letoh16(*p));
}

u_int32_t
elroy_r4(void *v, bus_space_handle_t h, bus_size_t o)
{
	u_int32_t data;

	h += o;
	data = *(volatile u_int32_t *)h;
	return (letoh32(data));
}

u_int64_t
elroy_r8(void *v, bus_space_handle_t h, bus_size_t o)
{
	u_int64_t data;

	h += o;
	data = *(volatile u_int64_t *)h;
	return (letoh64(data));
}

void
elroy_w1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t vv)
{
	h += o;
	*(volatile u_int8_t *)h = vv;
}

void
elroy_w2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv)
{
	volatile u_int16_t *p;

	h += o;
	p = (volatile u_int16_t *)h;
	*p = htole16(vv);
}

void
elroy_w4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv)
{
	h += o;
	vv = htole32(vv);
	*(volatile u_int32_t *)h = vv;
}

void
elroy_w8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t vv)
{
	h += o;
	*(volatile u_int64_t *)h = htole64(vv);
}


void
elroy_rm_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t *a, bus_size_t c)
{
	volatile u_int8_t *p;

	h += o;
	p = (volatile u_int8_t *)h;
	while (c--)
		*a++ = *p;
}

void
elroy_rm_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p;

	h += o;
	p = (volatile u_int16_t *)h;
	while (c--)
		*a++ = letoh16(*p);
}

void
elroy_rm_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p;

	h += o;
	p = (volatile u_int32_t *)h;
	while (c--)
		*a++ = letoh32(*p);
}

void
elroy_rm_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t *a, bus_size_t c)
{
	volatile u_int64_t *p;

	h += o;
	p = (volatile u_int64_t *)h;
	while (c--)
		*a++ = letoh64(*p);
}

void
elroy_wm_1(void *v, bus_space_handle_t h, bus_size_t o, const u_int8_t *a, bus_size_t c)
{
	volatile u_int8_t *p;

	h += o;
	p = (volatile u_int8_t *)h;
	while (c--)
		*p = *a++;
}

void
elroy_wm_2(void *v, bus_space_handle_t h, bus_size_t o, const u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p;

	h += o;
	p = (volatile u_int16_t *)h;
	while (c--)
		*p = htole16(*a++);
}

void
elroy_wm_4(void *v, bus_space_handle_t h, bus_size_t o, const u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p;

	h += o;
	p = (volatile u_int32_t *)h;
	while (c--)
		*p = htole32(*a++);
}

void
elroy_wm_8(void *v, bus_space_handle_t h, bus_size_t o, const u_int64_t *a, bus_size_t c)
{
	volatile u_int64_t *p;

	h += o;
	p = (volatile u_int64_t *)h;
	while (c--)
		*p = htole64(*a++);
}

void
elroy_sm_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t vv, bus_size_t c)
{
	volatile u_int8_t *p;

	h += o;
	p = (volatile u_int8_t *)h;
	while (c--)
		*p = vv;
}

void
elroy_sm_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv, bus_size_t c)
{
	volatile u_int16_t *p;

	h += o;
	p = (volatile u_int16_t *)h;
	vv = htole16(vv);
	while (c--)
		*p = vv;
}

void
elroy_sm_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv, bus_size_t c)
{
	volatile u_int32_t *p;

	h += o;
	p = (volatile u_int32_t *)h;
	vv = htole32(vv);
	while (c--)
		*p = vv;
}

void
elroy_sm_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t vv, bus_size_t c)
{
	volatile u_int64_t *p;

	h += o;
	p = (volatile u_int64_t *)h;
	vv = htole64(vv);
	while (c--)
		*p = vv;
}

void
elroy_rrm_2(void *v, bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t c)
{
	volatile u_int16_t *p, *q = (u_int16_t *)a;

	h += o;
	p = (volatile u_int16_t *)h;
	c /= 2;
	while (c--)
		*q++ = *p;
}

void
elroy_rrm_4(void *v, bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t c)
{
	volatile u_int32_t *p, *q = (u_int32_t *)a;

	h += o;
	p = (volatile u_int32_t *)h;
	c /= 4;
	while (c--)
		*q++ = *p;
}

void
elroy_rrm_8(void *v, bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t c)
{
	volatile u_int64_t *p, *q = (u_int64_t *)a;

	h += o;
	p = (volatile u_int64_t *)h;
	c /= 8;
	while (c--)
		*q++ = *p;
}

void
elroy_wrm_2(void *v, bus_space_handle_t h, bus_size_t o,
    const u_int8_t *a, bus_size_t c)
{
	volatile u_int16_t *p;
	const u_int16_t *q = (const u_int16_t *)a;

	h += o;
	p = (volatile u_int16_t *)h;
	c /= 2;
	while (c--)
		*p = *q++;
}

void
elroy_wrm_4(void *v, bus_space_handle_t h, bus_size_t o,
    const u_int8_t *a, bus_size_t c)
{
	volatile u_int32_t *p;
	const u_int32_t *q = (const u_int32_t *)a;

	h += o;
	p = (volatile u_int32_t *)h;
	c /= 4;
	while (c--)
		*p = *q++;
}

void
elroy_wrm_8(void *v, bus_space_handle_t h, bus_size_t o,
    const u_int8_t *a, bus_size_t c)
{
	volatile u_int64_t *p;
	const u_int64_t *q = (const u_int64_t *)a;

	h += o;
	p = (volatile u_int64_t *)h;
	c /= 8;
	while (c--)
		*p = *q++;
}

void
elroy_rr_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t *a, bus_size_t c)
{
	volatile u_int8_t *p;

	h += o;
	p = (volatile u_int8_t *)h;
	while (c--)
		*a++ = *p++;
}

void
elroy_rr_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p, data;

	h += o;
	p = (volatile u_int16_t *)h;
	while (c--) {
		data = *p++;
		*a++ = letoh16(data);
	}
}

void
elroy_rr_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p, data;

	h += o;
	p = (volatile u_int32_t *)h;
	while (c--) {
		data = *p++;
		*a++ = letoh32(data);
	}
}

void
elroy_rr_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t *a, bus_size_t c)
{
	volatile u_int64_t *p, data;

	h += o;
	p = (volatile u_int64_t *)h;
	while (c--) {
		data = *p++;
		*a++ = letoh64(data);
	}
}

void
elroy_wr_1(void *v, bus_space_handle_t h, bus_size_t o, const u_int8_t *a, bus_size_t c)
{
	volatile u_int8_t *p;

	h += o;
	p = (volatile u_int8_t *)h;
	while (c--)
		*p++ = *a++;
}

void
elroy_wr_2(void *v, bus_space_handle_t h, bus_size_t o, const u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p, data;

	h += o;
	p = (volatile u_int16_t *)h;
	while (c--) {
		data = *a++;
		*p++ = htole16(data);
	}
}

void
elroy_wr_4(void *v, bus_space_handle_t h, bus_size_t o, const u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p, data;

	h += o;
	p = (volatile u_int32_t *)h;
	while (c--) {
		data = *a++;
		*p++ = htole32(data);
	}
}

void
elroy_wr_8(void *v, bus_space_handle_t h, bus_size_t o, const u_int64_t *a, bus_size_t c)
{
	volatile u_int64_t *p, data;

	h += o;
	p = (volatile u_int64_t *)h;
	while (c--) {
		data = *a++;
		*p++ = htole64(data);
	}
}

void
elroy_rrr_2(void *v, bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t c)
{
	volatile u_int16_t *p, *q = (u_int16_t *)a;

	c /= 2;
	h += o;
	p = (volatile u_int16_t *)h;
	while (c--)
		*q++ = *p++;
}

void
elroy_rrr_4(void *v, bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t c)
{
	volatile u_int32_t *p, *q = (u_int32_t *)a;

	c /= 4;
	h += o;
	p = (volatile u_int32_t *)h;
	while (c--)
		*q++ = *p++;
}

void
elroy_rrr_8(void *v, bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t c)
{
	volatile u_int64_t *p, *q = (u_int64_t *)a;

	c /= 8;
	h += o;
	p = (volatile u_int64_t *)h;
	while (c--)
		*q++ = *p++;
}

void
elroy_wrr_2(void *v, bus_space_handle_t h, bus_size_t o,
    const u_int8_t *a, bus_size_t c)
{
	volatile u_int16_t *p;
	const u_int16_t *q = (u_int16_t *)a;

	c /= 2;
	h += o;
	p = (volatile u_int16_t *)h;
	while (c--)
		*p++ = *q++;
}

void
elroy_wrr_4(void *v, bus_space_handle_t h, bus_size_t o,
    const u_int8_t *a, bus_size_t c)
{
	volatile u_int32_t *p;
	const u_int32_t *q = (u_int32_t *)a;

	c /= 4;
	h += o;
	p = (volatile u_int32_t *)h;
	while (c--)
		*p++ = *q++;
}

void
elroy_wrr_8(void *v, bus_space_handle_t h, bus_size_t o,
    const u_int8_t *a, bus_size_t c)
{
	volatile u_int64_t *p;
	const u_int64_t *q = (u_int64_t *)a;

	c /= 8;
	h += o;
	p = (volatile u_int64_t *)h;
	while (c--)
		*p++ = *q++;
}

void
elroy_sr_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t vv, bus_size_t c)
{
	volatile u_int8_t *p;

	h += o;
	p = (volatile u_int8_t *)h;
	while (c--)
		*p++ = vv;
}

void
elroy_sr_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv, bus_size_t c)
{
	volatile u_int16_t *p;

	h += o;
	vv = htole16(vv);
	p = (volatile u_int16_t *)h;
	while (c--)
		*p++ = vv;
}

void
elroy_sr_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv, bus_size_t c)
{
	volatile u_int32_t *p;

	h += o;
	vv = htole32(vv);
	p = (volatile u_int32_t *)h;
	while (c--)
		*p++ = vv;
}

void
elroy_sr_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t vv, bus_size_t c)
{
	volatile u_int64_t *p;

	h += o;
	vv = htole64(vv);
	p = (volatile u_int64_t *)h;
	while (c--)
		*p++ = vv;
}

void
elroy_cp_1(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	while (c--)
		elroy_w1(v, h1, o1++, elroy_r1(v, h2, o2++));
}

void
elroy_cp_2(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	while (c--) {
		elroy_w2(v, h1, o1, elroy_r2(v, h2, o2));
		o1 += 2;
		o2 += 2;
	}
}

void
elroy_cp_4(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	while (c--) {
		elroy_w4(v, h1, o1, elroy_r4(v, h2, o2));
		o1 += 4;
		o2 += 4;
	}
}

void
elroy_cp_8(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	while (c--) {
		elroy_w8(v, h1, o1, elroy_r8(v, h2, o2));
		o1 += 8;
		o2 += 8;
	}
}

const struct hppa_bus_space_tag elroy_iomemt = {
	NULL,

	NULL, elroy_unmap, elroy_subregion, NULL, elroy_free,
	elroy_barrier, elroy_vaddr,
	elroy_r1,    elroy_r2,    elroy_r4,    elroy_r8,
	elroy_w1,    elroy_w2,    elroy_w4,    elroy_w8,  
	elroy_rm_1,  elroy_rm_2,  elroy_rm_4,  elroy_rm_8,
	elroy_wm_1,  elroy_wm_2,  elroy_wm_4,  elroy_wm_8,
	elroy_sm_1,  elroy_sm_2,  elroy_sm_4,  elroy_sm_8,
		     elroy_rrm_2, elroy_rrm_4, elroy_rrm_8,
		     elroy_wrm_2, elroy_wrm_4, elroy_wrm_8,
	elroy_rr_1,  elroy_rr_2,  elroy_rr_4,  elroy_rr_8,
	elroy_wr_1,  elroy_wr_2,  elroy_wr_4,  elroy_wr_8,
		     elroy_rrr_2, elroy_rrr_4, elroy_rrr_8,
		     elroy_wrr_2, elroy_wrr_4, elroy_wrr_8,
	elroy_sr_1,  elroy_sr_2,  elroy_sr_4,  elroy_sr_8,
	elroy_cp_1,  elroy_cp_2,  elroy_cp_4,  elroy_cp_8
};

int		 elroy_dmamap_create(void *v, bus_size_t size,
		    int nsegments, bus_size_t maxsegsz,
		    bus_size_t boundary, int flags,
		    bus_dmamap_t *dmamp);
void		 elroy_dmamap_destroy(void *v, bus_dmamap_t map);
int		 elroy_dmamap_load(void *v, bus_dmamap_t map,
		    void *addr, bus_size_t size,
		    struct proc *p, int flags);
int		 elroy_dmamap_load_mbuf(void *v, bus_dmamap_t map,
		    struct mbuf *m, int flags);
int		 elroy_dmamap_load_uio(void *v, bus_dmamap_t map,
		    struct uio *uio, int flags);
int		 elroy_dmamap_load_raw(void *v, bus_dmamap_t map,
		    bus_dma_segment_t *segs,
		    int nsegs, bus_size_t size, int flags);
void		 elroy_dmamap_unload(void *v, bus_dmamap_t map);
void		 elroy_dmamap_sync(void *v, bus_dmamap_t map, bus_addr_t off,
		    bus_size_t len, int ops);
int		 elroy_dmamem_alloc(void *v, bus_size_t size,
		    bus_size_t alignment, bus_size_t boundary,
		    bus_dma_segment_t *segs, int nsegs, int *rsegs, int flags);
void		 elroy_dmamem_free(void *v, bus_dma_segment_t *segs,
		    int nsegs);
int		 elroy_dmamem_map(void *v, bus_dma_segment_t *segs, int nsegs,
		    size_t size, caddr_t *kvap, int flags);
void		 elroy_dmamem_unmap(void *v, caddr_t kva, size_t size);
paddr_t		 elroy_dmamem_mmap(void *v, bus_dma_segment_t *segs,
		    int nsegs, off_t off, int prot, int flags);

int
elroy_dmamap_create(void *v, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	struct elroy_softc *sc = v;

	/* TODO check the addresses, boundary, enable dma */

	return (bus_dmamap_create(sc->sc_dmat, size, nsegments,
	    maxsegsz, boundary, flags, dmamp));
}

void
elroy_dmamap_destroy(void *v, bus_dmamap_t map)
{
	struct elroy_softc *sc = v;

	bus_dmamap_destroy(sc->sc_dmat, map);
}

int
elroy_dmamap_load(void *v, bus_dmamap_t map, void *addr, bus_size_t size,
    struct proc *p, int flags)
{
	struct elroy_softc *sc = v;

	return (bus_dmamap_load(sc->sc_dmat, map, addr, size, p, flags));
}

int
elroy_dmamap_load_mbuf(void *v, bus_dmamap_t map, struct mbuf *m, int flags)
{
	struct elroy_softc *sc = v;

	return (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, flags));
}

int
elroy_dmamap_load_uio(void *v, bus_dmamap_t map, struct uio *uio, int flags)
{
	struct elroy_softc *sc = v;

	return (bus_dmamap_load_uio(sc->sc_dmat, map, uio, flags));
}

int
elroy_dmamap_load_raw(void *v, bus_dmamap_t map, bus_dma_segment_t *segs,
    int nsegs, bus_size_t size, int flags)
{
	struct elroy_softc *sc = v;

	return (bus_dmamap_load_raw(sc->sc_dmat, map, segs, nsegs, size, flags));
}

void
elroy_dmamap_unload(void *v, bus_dmamap_t map)
{
	struct elroy_softc *sc = v;

	bus_dmamap_unload(sc->sc_dmat, map);
}

void
elroy_dmamap_sync(void *v, bus_dmamap_t map, bus_addr_t off,
    bus_size_t len, int ops)
{
	struct elroy_softc *sc = v;

	bus_dmamap_sync(sc->sc_dmat, map, off, len, ops);
}

int
elroy_dmamem_alloc(void *v, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs,
    int nsegs, int *rsegs, int flags)
{
	struct elroy_softc *sc = v;

	return (bus_dmamem_alloc(sc->sc_dmat, size, alignment, boundary,
	    segs, nsegs, rsegs, flags));
}

void
elroy_dmamem_free(void *v, bus_dma_segment_t *segs, int nsegs)
{
	struct elroy_softc *sc = v;

	bus_dmamem_free(sc->sc_dmat, segs, nsegs);
}

int
elroy_dmamem_map(void *v, bus_dma_segment_t *segs, int nsegs, size_t size,
    caddr_t *kvap, int flags)
{
	struct elroy_softc *sc = v;

	return (bus_dmamem_map(sc->sc_dmat, segs, nsegs, size, kvap, flags));
}

void
elroy_dmamem_unmap(void *v, caddr_t kva, size_t size)
{
	struct elroy_softc *sc = v;

	bus_dmamem_unmap(sc->sc_dmat, kva, size);
}

paddr_t
elroy_dmamem_mmap(void *v, bus_dma_segment_t *segs, int nsegs, off_t off,
    int prot, int flags)
{
	struct elroy_softc *sc = v;

	return (bus_dmamem_mmap(sc->sc_dmat, segs, nsegs, off, prot, flags));
}

const struct hppa_bus_dma_tag elroy_dmat = {
	NULL,
	elroy_dmamap_create, elroy_dmamap_destroy,
	elroy_dmamap_load, elroy_dmamap_load_mbuf,
	elroy_dmamap_load_uio, elroy_dmamap_load_raw,
	elroy_dmamap_unload, elroy_dmamap_sync,

	elroy_dmamem_alloc, elroy_dmamem_free, elroy_dmamem_map,
	elroy_dmamem_unmap, elroy_dmamem_mmap
};

const struct hppa_pci_chipset_tag elroy_pc = {
	NULL,
	elroy_attach_hook, elroy_maxdevs, elroy_make_tag, elroy_decompose_tag,
	elroy_conf_size, elroy_conf_read, elroy_conf_write,
	apic_intr_map, apic_intr_string,
	apic_intr_establish, apic_intr_disestablish,
#if NCARDBUS > 0
	elroy_alloc_parent
#else
	NULL
#endif
};

int		 elroy_print(void *aux, const char *pnp);

int
elroy_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s\n", pba->pba_busname, pnp);
	return (UNCONF);
}

void
elroy_attach(struct device *parent, struct device *self, void *aux)
{
	struct elroy_softc *sc = (struct elroy_softc *)self;
	struct confargs *ca = (struct confargs *)aux;
	struct pcibus_attach_args pba;
	volatile struct elroy_regs *r;
	const char *p = NULL, *q;
	int i;

	sc->sc_hpa = ca->ca_hpa;
	sc->sc_bt = ca->ca_iot;
	sc->sc_dmat = ca->ca_dmatag;
	if (bus_space_map(sc->sc_bt, ca->ca_hpa, ca->ca_hpasz, 0, &sc->sc_bh)) {
		printf(": can't map space\n");
		return;
	}

	sc->sc_regs = r = bus_space_vaddr(sc->sc_bt, sc->sc_bh);
	elroy_write32(&r->pci_cmdstat, htole32(PCI_COMMAND_IO_ENABLE |
	    PCI_COMMAND_MEM_ENABLE | PCI_COMMAND_MASTER_ENABLE));

	elroy_write32(&r->control, elroy_read32(&r->control) &
	    ~htole32(ELROY_CONTROL_RF));
	for (i = 5000; i-- &&
	    elroy_read32(&r->status) & htole32(ELROY_STATUS_RC); DELAY(10));
	if (i < 0) {
		printf(": reset failed; status %b\n",
		    htole32(r->status), ELROY_STATUS_BITS);
		return;
	}

	q = "";
	sc->sc_ver = PCI_REVISION(letoh32(elroy_read32(&r->pci_class)));
	switch ((ca->ca_type.iodc_model << 4) |
	    (ca->ca_type.iodc_revision >> 4)) {
	case 0x782:
		p = "Elroy";
		switch (sc->sc_ver) {
		default:
			q = "+";
		case 5:	sc->sc_ver = 0x40;	break;
		case 4:	sc->sc_ver = 0x30;	break;
		case 3:	sc->sc_ver = 0x22;	break;
		case 2:	sc->sc_ver = 0x21;	break;
		case 1:	sc->sc_ver = 0x20;	break;
		case 0:	sc->sc_ver = 0x10;	break;
		}
		break;

	case 0x783:
		p = "Mercury";
		break;

	case 0x784:
		p = "Quicksilver";
		break;

	default:
		p = "Mojo";
		break;
	}

	printf(": %s TR%d.%d%s", p, sc->sc_ver >> 4, sc->sc_ver & 0xf, q);
	apic_attach(sc);
	printf("\n");

	elroy_write32(&r->imask, htole32(0xffffffff << 30));
	elroy_write32(&r->ibase, htole32(ELROY_BASE_RE));

	/* TODO reserve elroy's pci space ? */

#if 0
printf("lmm %llx/%llx gmm %llx/%llx wlm %llx/%llx wgm %llx/%llx io %llx/%llx eio %llx/%llx\n",
letoh64(r->lmmio_base), letoh64(r->lmmio_mask),
letoh64(r->gmmio_base), letoh64(r->gmmio_mask),
letoh64(r->wlmmio_base), letoh64(r->wlmmio_mask),
letoh64(r->wgmmio_base), letoh64(r->wgmmio_mask),
letoh64(r->io_base), letoh64(r->io_mask),
letoh64(r->eio_base), letoh64(r->eio_mask));
#endif

	/* XXX evil hack! */
	sc->sc_iobase = 0xfee00000;

	sc->sc_iot = elroy_iomemt;
	sc->sc_iot.hbt_cookie = sc;
	sc->sc_iot.hbt_map = elroy_iomap;
	sc->sc_iot.hbt_alloc = elroy_ioalloc;
	sc->sc_memt = elroy_iomemt;
	sc->sc_memt.hbt_cookie = sc;
	sc->sc_memt.hbt_map = elroy_memmap;
	sc->sc_memt.hbt_alloc = elroy_memalloc;
	sc->sc_pc = elroy_pc;
	sc->sc_pc._cookie = sc;
	sc->sc_dmatag = elroy_dmat;
	sc->sc_dmatag._cookie = sc;

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_iot;
	pba.pba_memt = &sc->sc_memt;
	pba.pba_dmat = &sc->sc_dmatag;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0; /* (letoh32(elroy_read32(&r->busnum)) & 0xff) >> 4; */
	config_found(self, &pba, elroy_print);
}
