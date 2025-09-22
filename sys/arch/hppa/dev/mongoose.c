/*	$OpenBSD: mongoose.c,v 1.24 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/reboot.h>

#include <machine/bus.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <hppa/dev/cpudevs.h>
#include <hppa/dev/viper.h>

#include <dev/eisa/eisareg.h>
#include <dev/eisa/eisavar.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <hppa/dev/mongoosereg.h>
#include <hppa/dev/mongoosevar.h>

void	mgattach_gedoens(struct device *, struct device *, void *);
int	mgmatch_gedoens(struct device *, void *, void *);

const struct cfattach mg_gedoens_ca = {
	sizeof(struct mongoose_softc), mgmatch_gedoens, mgattach_gedoens
};

struct cfdriver mongoose_cd = {
	NULL, "mongoose", DV_DULL
};

void		 mg_eisa_attach_hook(struct device *parent,
		    struct device *self,
		    struct eisabus_attach_args *mg);
int		 mg_intr_map(void *v, u_int irq, eisa_intr_handle_t *ehp);
const char	*mg_intr_string(void *v, int irq);
void		 mg_isa_attach_hook(struct device *parent,
		    struct device *self,
		    struct isabus_attach_args *iba);
void		*mg_intr_establish(void *v, int irq, int type, int pri,
		    int (*handler)(void *), void *arg, const char *name);
void		 mg_intr_disestablish(void *v, void *cookie);
int		 mg_intr_check(void *v, int irq, int type);
int		 mg_eisa_iomap(void *v, bus_addr_t addr, bus_size_t size,
		    int flags, bus_space_handle_t *bshp);
int		 mg_eisa_memmap(void *v, bus_addr_t addr, bus_size_t size,
		    int flags, bus_space_handle_t *bshp);
void		 mg_eisa_memunmap(void *v, bus_space_handle_t bsh,
		    bus_size_t size);
void		 mg_isa_barrier(void *v, bus_space_handle_t h, bus_size_t o,
		    bus_size_t l, int op);
u_int16_t	 mg_isa_r2(void *v, bus_space_handle_t h, bus_size_t o);
u_int32_t	 mg_isa_r4(void *v, bus_space_handle_t h, bus_size_t o);
void		 mg_isa_w2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv);
void		 mg_isa_w4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv);
void		 mg_isa_rm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t *a, bus_size_t c);
void		 mg_isa_rm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t *a, bus_size_t c);
void		 mg_isa_wm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int16_t *a, bus_size_t c);
void		 mg_isa_wm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int32_t *a, bus_size_t c);
void		 mg_isa_sm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv, bus_size_t c);
void		 mg_isa_sm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv, bus_size_t c);
void		 mg_isa_rr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t *a, bus_size_t c);
void		 mg_isa_rr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t *a, bus_size_t c);
void		 mg_isa_wr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int16_t *a, bus_size_t c);
void		 mg_isa_wr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int32_t *a, bus_size_t c);
void		 mg_isa_sr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv, bus_size_t c);
void		 mg_isa_sr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv, bus_size_t c);

/* TODO: DMA guts */

void
mg_eisa_attach_hook(struct device *parent, struct device *self,
	struct eisabus_attach_args *mg)
{
}

int
mg_intr_map(void *v, u_int irq, eisa_intr_handle_t *ehp)
{
	*ehp = irq;
	return 0;
}

const char *
mg_intr_string(void *v, int irq)
{
	static char buf[16];

	snprintf(buf, sizeof buf, "isa irq %d", irq);
	return buf;
}

void
mg_isa_attach_hook(struct device *parent, struct device *self,
	struct isabus_attach_args *iba)
{

}

void *
mg_intr_establish(void *v, int irq, int type, int pri,
	int (*handler)(void *), void *arg, const char *name)
{
	struct hppa_isa_iv *iv;
	struct mongoose_softc *sc = v;
	volatile u_int8_t *imr, *pic;

	if (!sc || irq < 0 || irq >= MONGOOSE_NINTS ||
	    (0 <= irq && irq < MONGOOSE_NINTS && sc->sc_iv[irq].iv_handler))
		return NULL;

	if (type != IST_LEVEL && type != IST_EDGE) {
#ifdef DEBUG
		printf("%s: bad interrupt level (%d)\n", sc->sc_dev.dv_xname,
		    type);
#endif
		return NULL;
	}

	iv = &sc->sc_iv[irq];
	if (iv->iv_handler) {
#ifdef DEBUG
		printf("%s: irq %d already established\n", sc->sc_dev.dv_xname,
		    irq);
#endif
		return NULL;
	}

	iv->iv_name = name;
	iv->iv_pri = pri;
	iv->iv_handler = handler;
	iv->iv_arg = arg;
	
	if (irq < 8) {
		imr = &sc->sc_ctrl->imr0;
		pic = &sc->sc_ctrl->pic0;
	} else {
		imr = &sc->sc_ctrl->imr1;
		pic = &sc->sc_ctrl->pic1;
		irq -= 8;
	}

	*imr |= 1 << irq;
	*pic |= (type == IST_LEVEL) << irq;

	/* TODO: ack it? */

	return iv;
}

void
mg_intr_disestablish(void *v, void *cookie)
{
	struct hppa_isa_iv *iv = cookie;
	struct mongoose_softc *sc = v;
 	int irq;
 	volatile u_int8_t *imr;

	if (!sc || !cookie)
		return;

 	irq = iv - sc->sc_iv;
	if (irq < 8)
		imr = &sc->sc_ctrl->imr0;
	else
		imr = &sc->sc_ctrl->imr1;
	*imr &= ~(1 << irq);
	/* TODO: ack it? */

	iv->iv_handler = NULL;
}

int
mg_intr_check(void *v, int irq, int type)
{
	return 0;
}

int
mg_intr(void *v)
{
	struct mongoose_softc *sc = v;
	struct hppa_isa_iv *iv;
	int s, irq = 0;

	iv = &sc->sc_iv[irq];
	s = splraise(iv->iv_pri);
	(iv->iv_handler)(iv->iv_arg);
	splx(s);

	return 0;
}

int
mg_eisa_iomap(void *v, bus_addr_t addr, bus_size_t size, int flags,
	bus_space_handle_t *bshp)
{
	struct mongoose_softc *sc = v;

	/* see if it's ISA space we are mapping */
	if (0x100 <= addr && addr < 0x400) {
#define	TOISA(a) ((((a) & 0x3f8) << 9) + ((a) & 7))
		size = TOISA(addr + size) - TOISA(addr);
		addr = TOISA(addr);
	}

	return (sc->sc_bt->hbt_map)(NULL, sc->sc_iomap + addr, size,
				    flags, bshp);
}

int
mg_eisa_memmap(void *v, bus_addr_t addr, bus_size_t size, int flags,
	bus_space_handle_t *bshp)
{
	/* TODO: eisa memory map */
	return -1;
}

void
mg_eisa_memunmap(void *v, bus_space_handle_t bsh, bus_size_t size)
{
	/* TODO: eisa memory unmap */
}

void
mg_isa_barrier(void *v, bus_space_handle_t h, bus_size_t o, bus_size_t l, int op)
{
	sync_caches();
}

u_int16_t
mg_isa_r2(void *v, bus_space_handle_t h, bus_size_t o)
{
	register u_int16_t r = *((volatile u_int16_t *)(h + o));
	return letoh16(r);
}

u_int32_t
mg_isa_r4(void *v, bus_space_handle_t h, bus_size_t o)
{
	register u_int32_t r = *((volatile u_int32_t *)(h + o));
	return letoh32(r);
}

void
mg_isa_w2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv)
{
	*((volatile u_int16_t *)(h + o)) = htole16(vv);
}

void
mg_isa_w4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv)
{
	*((volatile u_int32_t *)(h + o)) = htole32(vv);
}

void
mg_isa_rm_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(a++) = letoh16(*(volatile u_int16_t *)h);
}

void
mg_isa_rm_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(a++) = letoh32(*(volatile u_int32_t *)h);
}

void
mg_isa_wm_2(void *v, bus_space_handle_t h, bus_size_t o, const u_int16_t *a, bus_size_t c)
{
	register u_int16_t r;
	h += o;
	while (c--) {
		r = *(a++);
		*(volatile u_int16_t *)h = htole16(r);
	}
}

void
mg_isa_wm_4(void *v, bus_space_handle_t h, bus_size_t o, const u_int32_t *a, bus_size_t c)
{
	register u_int32_t r;
	h += o;
	while (c--) {
		r = *(a++);
		*(volatile u_int32_t *)h = htole32(r);
	}
}

void
mg_isa_sm_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv, bus_size_t c)
{
	vv = htole16(vv);
	h += o;
	while (c--)
		*(volatile u_int16_t *)h = vv;
}

void
mg_isa_sm_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv, bus_size_t c)
{
	vv = htole32(vv);
	h += o;
	while (c--)
		*(volatile u_int32_t *)h = vv;
}

void
mg_isa_rr_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p = (u_int16_t *)(h + o);
	u_int32_t r;

	while (c--) {
		r = *p++;
		*a++ = letoh16(r);
	}
}

void
mg_isa_rr_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p = (u_int32_t *)(h + o);
	u_int32_t r;

	while (c--) {
		r = *p++;
		*a++ = letoh32(r);
	}
}

void
mg_isa_wr_2(void *v, bus_space_handle_t h, bus_size_t o, const u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p = (u_int16_t *)(h + o);
	u_int32_t r;

	while (c--) {
		r = *a++;
		*p++ = htole16(r);
	}
}

void
mg_isa_wr_4(void *v, bus_space_handle_t h, bus_size_t o, const u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p = (u_int32_t *)(h + o);
	u_int32_t r;

	while (c--) {
		r = *a++;
		*p++ = htole32(r);
	}
}

void
mg_isa_sr_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv, bus_size_t c)
{
	volatile u_int16_t *p = (u_int16_t *)(h + o);

	vv = htole16(vv);
	while (c--)
		*p++ = vv;
}

void
mg_isa_sr_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv, bus_size_t c)
{
	volatile u_int32_t *p = (u_int32_t *)(h + o);

	vv = htole32(vv);
	while (c--)
		*p++ = vv;
}

int
mgattach_common(struct mongoose_softc *sc)
{
	struct hppa_bus_space_tag *bt;
	union mongoose_attach_args ea;
	char brid[EISA_IDSTRINGLEN];

	viper_eisa_en();

	/* BUS RESET */
	sc->sc_ctrl->nmi_ext = MONGOOSE_NMI_BUSRESET;
	DELAY(1);
	sc->sc_ctrl->nmi_ext = 0;
	DELAY(100);

	/* determine eisa board id */
	{
		u_int8_t id[4], *p;
		p = (u_int8_t *)(sc->sc_iomap + EISA_SLOTOFF_VID);
		id[0] = *p++;
		id[1] = *p++;
		id[2] = *p++;
		id[3] = *p++;

		brid[0] = EISA_VENDID_0(id);
		brid[1] = EISA_VENDID_1(id);
		brid[2] = EISA_VENDID_2(id);
		brid[3] = EISA_PRODID_0(id + 2);
		brid[4] = EISA_PRODID_1(id + 2);
		brid[5] = EISA_PRODID_2(id + 2);
		brid[6] = EISA_PRODID_3(id + 2);
		brid[7] = '\0';
	}

	printf (": %s rev %d, %d MHz\n", brid, sc->sc_regs->version,
		(sc->sc_regs->clock? 33 : 25));
	sc->sc_regs->liowait = 1;	/* disable isa wait states */
	sc->sc_regs->lock    = 1;	/* bus unlock */

	/* attach EISA */
	sc->sc_ec.ec_v = sc;
	sc->sc_ec.ec_attach_hook = mg_eisa_attach_hook;
	sc->sc_ec.ec_intr_establish = mg_intr_establish;
	sc->sc_ec.ec_intr_disestablish = mg_intr_disestablish;
	sc->sc_ec.ec_intr_string = mg_intr_string;
	sc->sc_ec.ec_intr_map = mg_intr_map;
	/* inherit the bus tags for eisa from the mainbus */
	bt = &sc->sc_eiot;
	bcopy(sc->sc_bt, bt, sizeof(*bt));
	bt->hbt_cookie = sc;
	bt->hbt_map = mg_eisa_iomap;
#define	R(n)	bt->__CONCAT(hbt_,n) = &__CONCAT(mg_isa_,n)
	/* R(barrier); */
	R(r2); R(r4); R(w2); R(w4);
	R(rm_2);R(rm_4);R(wm_2);R(wm_4);R(sm_2);R(sm_4);
	R(rr_2);R(rr_4);R(wr_2);R(wr_4);R(sr_2);R(sr_4);

	bt = &sc->sc_ememt;
	bcopy(sc->sc_bt, bt, sizeof(*bt));
	bt->hbt_cookie = sc;
	bt->hbt_map = mg_eisa_memmap;
	bt->hbt_unmap = mg_eisa_memunmap;
	/* attachment guts */
	ea.mongoose_eisa.eba_busname = "eisa";
	ea.mongoose_eisa.eba_iot = &sc->sc_eiot;
	ea.mongoose_eisa.eba_memt = &sc->sc_ememt;
	ea.mongoose_eisa.eba_dmat = NULL /* &sc->sc_edmat */;
	ea.mongoose_eisa.eba_ec = &sc->sc_ec;
	config_found((struct device *)sc, &ea.mongoose_eisa, mgprint);

	sc->sc_ic.ic_v = sc;
	sc->sc_ic.ic_attach_hook = mg_isa_attach_hook;
	sc->sc_ic.ic_intr_establish = mg_intr_establish;
	sc->sc_ic.ic_intr_disestablish = mg_intr_disestablish;
	sc->sc_ic.ic_intr_check = mg_intr_check;
	/* inherit the bus tags for isa from the eisa */
	bt = &sc->sc_imemt;
	bcopy(&sc->sc_ememt, bt, sizeof(*bt));
	bt = &sc->sc_iiot;
	bcopy(&sc->sc_eiot, bt, sizeof(*bt));
	/* TODO: DMA tags */
	/* attachment guts */
	ea.mongoose_isa.iba_busname = "isa";
	ea.mongoose_isa.iba_iot = &sc->sc_iiot;
	ea.mongoose_isa.iba_memt = &sc->sc_imemt;
#if NISADMA > 0
	ea.mongoose_isa.iba_dmat = &sc->sc_idmat;
#endif
	ea.mongoose_isa.iba_ic = &sc->sc_ic;
	config_found((struct device *)sc, &ea.mongoose_isa, mgprint);
#undef	R

	return (0);
}

int
mgprint(void *aux, const char *pnp)
{
	union mongoose_attach_args *ea = aux;

	if (pnp)
		printf ("%s at %s", ea->mongoose_name, pnp);

	return (UNCONF);
}

int
mgmatch_gedoens(struct device *parent, void *cfdata, void *aux)
{
	register struct confargs *ca = aux;
	/* struct cfdata *cf = cfdata; */
	bus_space_handle_t ioh;

	if (ca->ca_type.iodc_type != HPPA_TYPE_BHA ||
	    (ca->ca_type.iodc_sv_model != HPPA_BHA_EISA &&
	     ca->ca_type.iodc_sv_model != HPPA_BHA_WEISA))
		return 0;

	if (bus_space_map(ca->ca_iot, ca->ca_hpa + MONGOOSE_MONGOOSE,
	    IOMOD_HPASIZE, 0, &ioh))
		return 0;

	/* XXX check EISA signature */

	bus_space_unmap(ca->ca_iot, ioh, IOMOD_HPASIZE);

	return 1;
}

void
mgattach_gedoens(struct device *parent, struct device *self, void *aux)
{
	register struct confargs *ca = aux;
	register struct mongoose_softc *sc = (struct mongoose_softc *)self;
	bus_space_handle_t ioh;

	sc->sc_bt = ca->ca_iot;
	sc->sc_iomap = ca->ca_hpa;

	if (bus_space_map(ca->ca_iot, ca->ca_hpa + MONGOOSE_MONGOOSE,
	    sizeof(struct mongoose_regs), 0, &ioh) != 0) {
		printf(": can't map IO space\n");
		return;
	}
	sc->sc_regs = (struct mongoose_regs *)ioh;

	if (bus_space_map(ca->ca_iot, ca->ca_hpa + MONGOOSE_CTRL,
	    sizeof(struct mongoose_ctrl), 0, &ioh) != 0) {
		printf(": can't map control registers\n");
		bus_space_unmap(ca->ca_iot, (bus_space_handle_t)sc->sc_regs,
		    sizeof(struct mongoose_regs));
		return;
	}
	sc->sc_ctrl = (struct mongoose_ctrl *)ioh;

	if (mgattach_common(sc) != 0)
		return;

	/* attach interrupt */
	sc->sc_ih = cpu_intr_establish(IPL_HIGH, ca->ca_irq,
				       mg_intr, sc, sc->sc_dev.dv_xname);
}
