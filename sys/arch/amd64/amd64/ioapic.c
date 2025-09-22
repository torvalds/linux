/*	$OpenBSD: ioapic.c,v 1.34 2025/09/16 12:18:10 hshoexer Exp $	*/
/* 	$NetBSD: ioapic.c,v 1.6 2003/05/15 13:30:31 fvdl Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * Copyright (c) 1999 Stefan Grefen
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR AND CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <uvm/uvm_extern.h>
#include <machine/i82093reg.h>
#include <machine/i82093var.h>

#include <machine/mpbiosvar.h>

/*
 * XXX locking
 */

int     ioapic_match(struct device *, void *, void *);
void    ioapic_attach(struct device *, struct device *, void *);
int	ioapic_activate(struct device *, int);

void ioapic_hwmask(struct pic *, int);
void ioapic_hwunmask(struct pic *, int);
void ioapic_addroute(struct pic *, struct cpu_info *, int, int, int);
void ioapic_delroute(struct pic *, struct cpu_info *, int, int, int);
void apic_set_redir(struct ioapic_softc *, int, int, struct cpu_info *);

int ioapic_bsp_id = 0;
int ioapic_cold = 1;

struct ioapic_softc *ioapics;	 /* head of linked list */
int nioapics = 0;	   	 /* number attached */
static int ioapic_vecbase;

void ioapic_set_id(struct ioapic_softc *);

static __inline u_long
ioapic_lock(struct ioapic_softc *sc)
{
	u_long flags;

	flags = intr_disable();
#ifdef MULTIPROCESSOR
	mtx_enter(&sc->sc_pic.pic_mutex);
#endif
	return flags;
}

static __inline void
ioapic_unlock(struct ioapic_softc *sc, u_long flags)
{
#ifdef MULTIPROCESSOR
	mtx_leave(&sc->sc_pic.pic_mutex);
#endif
	intr_restore(flags);
}

/*
 * Register read/write routines.
 */
static __inline u_int32_t
ioapic_read_ul(struct ioapic_softc *sc,int regid)
{
	bus_space_write_4(sc->sc_memt, sc->sc_memh, IOAPIC_REG, regid);
	return bus_space_read_4(sc->sc_memt, sc->sc_memh, IOAPIC_DATA);
}

static __inline void
ioapic_write_ul(struct ioapic_softc *sc,int regid, u_int32_t val)
{
	bus_space_write_4(sc->sc_memt, sc->sc_memh, IOAPIC_REG, regid);
	bus_space_write_4(sc->sc_memt, sc->sc_memh, IOAPIC_DATA, val);
}

static __inline u_int32_t
ioapic_read(struct ioapic_softc *sc, int regid)
{
	u_int32_t val;
	u_long flags;

	flags = ioapic_lock(sc);
	val = ioapic_read_ul(sc, regid);
	ioapic_unlock(sc, flags);
	return val;
}

static __inline void
ioapic_write(struct ioapic_softc *sc,int regid, int val)
{
	u_long flags;

	flags = ioapic_lock(sc);
	ioapic_write_ul(sc, regid, val);
	ioapic_unlock(sc, flags);
}

struct ioapic_softc *
ioapic_find(int apicid)
{
	struct ioapic_softc *sc;

	if (apicid == MPS_ALL_APICS) {	/* XXX mpbios-specific */
		/*
		 * XXX kludge for all-ioapics interrupt support
		 * on single ioapic systems
		 */
		if (nioapics <= 1)
			return (ioapics);
		panic("unsupported: all-ioapics interrupt with >1 ioapic");
	}

	for (sc = ioapics; sc != NULL; sc = sc->sc_next)
		if (sc->sc_apicid == apicid)
			return (sc);

	return (NULL);
}

/*
 * For the case the I/O APICs were configured using ACPI, there must
 * be an option to match global ACPI interrupts with APICs.
 */
struct ioapic_softc *
ioapic_find_bybase(int vec)
{
	struct ioapic_softc *sc;

	for (sc = ioapics; sc != NULL; sc = sc->sc_next) {
		if (vec >= sc->sc_apic_vecbase &&
		    vec < (sc->sc_apic_vecbase + sc->sc_apic_sz))
			return sc;
	}

	return NULL;
}

static __inline void
ioapic_add(struct ioapic_softc *sc)
{
	sc->sc_next = ioapics;
	ioapics = sc;
	nioapics++;
}

void
ioapic_print_redir(struct ioapic_softc *sc, char *why, int pin)
{
	u_int32_t redirlo = ioapic_read(sc, IOAPIC_REDLO(pin));
	u_int32_t redirhi = ioapic_read(sc, IOAPIC_REDHI(pin));

	apic_format_redir(sc->sc_pic.pic_name, why, pin, redirhi, redirlo);
}

const struct cfattach ioapic_ca = {
	sizeof(struct ioapic_softc), ioapic_match, ioapic_attach, NULL,
	ioapic_activate
};

struct cfdriver ioapic_cd = {
	NULL, "ioapic", DV_DULL, CD_COCOVM
};

int
ioapic_match(struct device *parent, void *v, void *aux)
{
	struct apic_attach_args *aaa = (struct apic_attach_args *)aux;
	struct cfdata *match = v;

	if (strcmp(aaa->aaa_name, match->cf_driver->cd_name) == 0)
		return (1);
	return (0);
}

/* Reprogram the APIC ID, and check that it actually got set. */
void
ioapic_set_id(struct ioapic_softc *sc)
{
	u_int8_t apic_id;

	ioapic_write(sc, IOAPIC_ID,
	    (ioapic_read(sc, IOAPIC_ID) & ~IOAPIC_ID_MASK) |
	    (sc->sc_apicid << IOAPIC_ID_SHIFT));

	apic_id = (ioapic_read(sc, IOAPIC_ID) & IOAPIC_ID_MASK) >>
	    IOAPIC_ID_SHIFT;

	if (apic_id != sc->sc_apicid)
		printf(", can't remap");
	else
		printf(", remapped");
}

void
ioapic_attach(struct device *parent, struct device *self, void *aux)
{
	struct ioapic_softc *sc = (struct ioapic_softc *)self;
	struct apic_attach_args  *aaa = (struct apic_attach_args *)aux;
	int apic_id;
	u_int32_t ver_sz;
	int i;

	sc->sc_flags = aaa->flags;
	sc->sc_apicid = aaa->apic_id;

	printf(": apid %d", aaa->apic_id);

	if (ioapic_find(aaa->apic_id) != NULL) {
		printf(", duplicate apic id (ignored)\n");
		return;
	}

	ioapic_add(sc);

	printf(" pa 0x%lx", aaa->apic_address);

	sc->sc_memt = aaa->apic_memt;
	if (bus_space_map(sc->sc_memt, aaa->apic_address, PAGE_SIZE, 0,
	    &sc->sc_memh)) {
		printf(", map failed\n");
		return;
	}

	sc->sc_pic.pic_type = PIC_IOAPIC;
#ifdef MULTIPROCESSOR
	mtx_init(&sc->sc_pic.pic_mutex, IPL_NONE);
#endif
	sc->sc_pic.pic_hwmask = ioapic_hwmask;
	sc->sc_pic.pic_hwunmask = ioapic_hwunmask;
	sc->sc_pic.pic_addroute = ioapic_addroute;
	sc->sc_pic.pic_delroute = ioapic_delroute;
	sc->sc_pic.pic_edge_stubs = ioapic_edge_stubs;
	sc->sc_pic.pic_level_stubs = ioapic_level_stubs;

	ver_sz = ioapic_read(sc, IOAPIC_VER);
	sc->sc_apic_vers = (ver_sz & IOAPIC_VER_MASK) >> IOAPIC_VER_SHIFT;
	sc->sc_apic_sz = (ver_sz & IOAPIC_MAX_MASK) >> IOAPIC_MAX_SHIFT;
	sc->sc_apic_sz++;

	if (aaa->apic_vecbase != -1)
		sc->sc_apic_vecbase = aaa->apic_vecbase;
	else {
		/*
		 * XXX this assumes ordering of ioapics in the table.
		 * Only needed for broken BIOS workaround (see mpbios.c)
		 */
		sc->sc_apic_vecbase = ioapic_vecbase;
		ioapic_vecbase += sc->sc_apic_sz;
	}

	if (mp_verbose) {
		printf(", %s mode",
		    aaa->flags & IOAPIC_PICMODE ? "PIC" : "virtual wire");
	}

	printf(", version %x, %d pins", sc->sc_apic_vers, sc->sc_apic_sz);

	apic_id = (ioapic_read(sc, IOAPIC_ID) & IOAPIC_ID_MASK) >>
	    IOAPIC_ID_SHIFT;

	sc->sc_pins = mallocarray(sc->sc_apic_sz, sizeof(struct ioapic_pin),
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < sc->sc_apic_sz; i++) {
		sc->sc_pins[i].ip_next = NULL;
		sc->sc_pins[i].ip_map = NULL;
		sc->sc_pins[i].ip_vector = 0;
		sc->sc_pins[i].ip_type = IST_NONE;
	}

	/*
	 * In case the APIC is not initialized to the correct ID
	 * do it now.
	 * Maybe we should record the original ID for interrupt
	 * mapping later ...
	 */
	if (apic_id != sc->sc_apicid) {
		if (mp_verbose)
			printf("\n%s: misconfigured as apic %d",
			    sc->sc_pic.pic_name, apic_id);
		ioapic_set_id(sc);
	}

	printf("\n");
}

int
ioapic_activate(struct device *self, int act)
{
	struct ioapic_softc *sc = (struct ioapic_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		/* On resume, reset the APIC id, like we do on boot */
		ioapic_write(sc, IOAPIC_ID,
		    (ioapic_read(sc, IOAPIC_ID) & ~IOAPIC_ID_MASK) |
		    (sc->sc_apicid << IOAPIC_ID_SHIFT));
	}

	return (0);
}

void
apic_set_redir(struct ioapic_softc *sc, int pin, int idt_vec,
    struct cpu_info *ci)
{
	u_int32_t redlo;
	u_int32_t redhi = 0;
	int delmode;

	struct ioapic_pin *pp;
	struct mp_intr_map *map;

	pp = &sc->sc_pins[pin];
	map = pp->ip_map;
	redlo = (map == NULL) ? IOAPIC_REDLO_MASK : map->redir;
	delmode = (redlo & IOAPIC_REDLO_DEL_MASK) >> IOAPIC_REDLO_DEL_SHIFT;

	/* XXX magic numbers */
	if ((delmode != 0) && (delmode != 1))
		;
	else if (pp->ip_type == IST_NONE) {
		redlo |= IOAPIC_REDLO_MASK;
	} else {
		redlo |= (idt_vec & 0xff);
		redlo &= ~IOAPIC_REDLO_DEL_MASK;
		redlo |= (IOAPIC_REDLO_DEL_FIXED << IOAPIC_REDLO_DEL_SHIFT);
		redlo &= ~IOAPIC_REDLO_DSTMOD;

		/*
		 * Destination: BSP CPU
		 *
		 * XXX will want to distribute interrupts across cpu's
		 * eventually.  most likely, we'll want to vector each
		 * interrupt to a specific CPU and load-balance across
		 * cpu's.  but there's no point in doing that until after
		 * most interrupts run without the kernel lock.
		 */
		redhi |= (ci->ci_apicid << IOAPIC_REDHI_DEST_SHIFT);

		/* XXX derive this bit from BIOS info */
		if (pp->ip_type == IST_LEVEL)
			redlo |= IOAPIC_REDLO_LEVEL;
		else
			redlo &= ~IOAPIC_REDLO_LEVEL;
		if (map != NULL && ((map->flags & 3) == MPS_INTPO_DEF)) {
			if (pp->ip_type == IST_LEVEL)
				redlo |= IOAPIC_REDLO_ACTLO;
			else
				redlo &= ~IOAPIC_REDLO_ACTLO;
		}
	}
	/* Do atomic write */
	ioapic_write(sc, IOAPIC_REDLO(pin), IOAPIC_REDLO_MASK);
	ioapic_write(sc, IOAPIC_REDHI(pin), redhi);
	ioapic_write(sc, IOAPIC_REDLO(pin), redlo);
	if (mp_verbose)
		ioapic_print_redir(sc, "int", pin);
}

/*
 * Throw the switch and enable interrupts..
 */

void
ioapic_enable(void)
{
	int p;
	struct ioapic_softc *sc;
	struct ioapic_pin *ip;

	ioapic_cold = 0;

	if (ioapics == NULL)
		return;

	if (ioapics->sc_flags & IOAPIC_PICMODE) {
		printf("%s: writing to IMCR to disable pics\n",
		    ioapics->sc_pic.pic_name);
		outb(IMCR_ADDR, IMCR_REGISTER);
		outb(IMCR_DATA, IMCR_APIC);
	}

	for (sc = ioapics; sc != NULL; sc = sc->sc_next) {
		if (mp_verbose)
			printf("%s: enabling\n", sc->sc_pic.pic_name);

		for (p = 0; p < sc->sc_apic_sz; p++) {
			ip = &sc->sc_pins[p];
			if (ip->ip_type != IST_NONE)
				apic_set_redir(sc, p, ip->ip_vector,
				    ip->ip_cpu);
		}
	}
}

void
ioapic_hwmask(struct pic *pic, int pin)
{
	u_int32_t redlo;
	struct ioapic_softc *sc = (struct ioapic_softc *)pic;
	u_long flags;

	if (ioapic_cold)
		return;
	flags = ioapic_lock(sc);
	redlo = ioapic_read_ul(sc, IOAPIC_REDLO(pin));
	redlo |= IOAPIC_REDLO_MASK;
	redlo &= ~IOAPIC_REDLO_RIRR;
	ioapic_write_ul(sc, IOAPIC_REDLO(pin), redlo);
	ioapic_unlock(sc, flags);
}

void
ioapic_hwunmask(struct pic *pic, int pin)
{
	u_int32_t redlo;
	struct ioapic_softc *sc = (struct ioapic_softc *)pic;
	u_long flags;

	if (ioapic_cold)
		return;
	flags = ioapic_lock(sc);
	redlo = ioapic_read_ul(sc, IOAPIC_REDLO(pin));
	redlo &= ~IOAPIC_REDLO_MASK;
	redlo &= ~IOAPIC_REDLO_RIRR;
	ioapic_write_ul(sc, IOAPIC_REDLO(pin), redlo);
	ioapic_unlock(sc, flags);
}

void
ioapic_addroute(struct pic *pic, struct cpu_info *ci, int pin,
    int idtvec, int type)
{
	struct ioapic_softc *sc = (struct ioapic_softc *)pic;
	struct ioapic_pin *pp;

	pp = &sc->sc_pins[pin];
	pp->ip_type = type;
	pp->ip_vector = idtvec;
	pp->ip_cpu = ci;
	if (ioapic_cold)
		return;
	apic_set_redir(sc, pin, idtvec, ci);
}

void
ioapic_delroute(struct pic *pic, struct cpu_info *ci, int pin,
    int idtvec, int type)
{
	struct ioapic_softc *sc = (struct ioapic_softc *)pic;
	struct ioapic_pin *pp;

	if (ioapic_cold) {
		pp = &sc->sc_pins[pin];
		pp->ip_type = IST_NONE;
		return;
	}
	ioapic_hwmask(pic, pin);
}

#ifdef DDB
void ioapic_dump(void);

void
ioapic_dump(void)
{
	struct ioapic_softc *sc;
	struct ioapic_pin *ip;
	int p;

	for (sc = ioapics; sc != NULL; sc = sc->sc_next) {
		for (p = 0; p < sc->sc_apic_sz; p++) {
			ip = &sc->sc_pins[p];
			if (ip->ip_type != IST_NONE)
				ioapic_print_redir(sc, "dump", p);
		}
	}
}
#endif
