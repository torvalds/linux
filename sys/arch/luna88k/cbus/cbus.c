/*	$OpenBSD: cbus.c,v 1.9 2021/03/11 11:16:58 jsg Exp $	*/

/*
 * Copyright (c) 2014 Kenji Aoyama.
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
 * PC-9801 extension board slot bus ('C-bus') driver for LUNA-88K2.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/asm_macro.h>	/* ff1() */
#include <machine/autoconf.h>
#include <machine/board.h>	/* PC_BASE */

#include <luna88k/cbus/cbusvar.h>
#include <luna88k/luna88k/isr.h>	/* isrlink_autovec() */

#if 0
#define CBUS_DEBUG
#endif

#include "ne.h"
#include "necsb.h"
#include "pcic.h"

static struct cbus_attach_args cbus_devs[] = {
#if NNE > 0
	/* NE-2000 compatible ethernet */
	{ "ne",    -1, -1, -1, -1, -1 },
#endif
#if NNECSB > 0
	/* PC-9801-86 sound board */
	{ "necsb", -1, -1, -1, -1, -1 },
#endif
#if NPCIC > 0
	/* PC-9801-102 & PC-9821X[AE]-01 PCMCIA board */
	{ "pcic",  -1, -1, -1, -1, -1 },
#endif
	/* C-bus "generic" driver */
	{ "pcex",  -1, -1, -1, -1, -1 }
};

/*
 * C-bus interrupt status register
 */
#define CBUS_INTR_STAT_REG	(PC_BASE + 0x1100000)
volatile u_int8_t *cbus_isreg = (u_int8_t *)CBUS_INTR_STAT_REG;

/* autoconf stuff */
int cbus_match(struct device *, void *, void *);
void cbus_attach(struct device *, struct device *, void *);
int cbus_print(void *, const char *);

struct cbus_softc {
	struct device sc_dev;
	struct cbus_isr_t cbus_isr[NCBUSISR];
	u_int8_t registered;
};

const struct cfattach cbus_ca = {
	sizeof(struct cbus_softc), cbus_match, cbus_attach
};

struct cfdriver cbus_cd = {
	NULL, "cbus", DV_DULL
};

/* prototypes */
void cbus_isrdispatch(int);
int cbus_intr(void *);

int
cbus_match(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, cbus_cd.cd_name))
		return 0;
#if 0
	if (badaddr((vaddr_t)ma->ma_addr, 4))
		return 0;
#endif
	return 1;
}

void
cbus_attach(struct device *parent, struct device *self, void *args)
{
	struct cbus_softc *sc = (struct cbus_softc *)self;
	struct mainbus_attach_args *ma = args;
	int i;

	for (i = 0; i < NCBUSISR; i++) {
		struct cbus_isr_t *ci = &sc->cbus_isr[i];
		ci->isr_func = NULL;
		ci->isr_arg = NULL;
		ci->isr_intlevel = ci->isr_ipl = -1;
		/* clearing interrupt flags (INT0-INT6) */
		*cbus_isreg = (u_int8_t)(6 - i);
	}
	sc->registered = 0x00;

	/* register C-bus interrupt service routine on mainbus */
	isrlink_autovec(cbus_intr, (void *)self, ma->ma_ilvl,
	    ISRPRI_TTY, self->dv_xname);

	printf("\n");

	for (i = 0; i < sizeof(cbus_devs)/sizeof(cbus_devs[0]); i++)
		config_found(self, &cbus_devs[i], cbus_print);

	return;
}

int
cbus_print(void *aux, const char *pnp)
{
	struct cbus_attach_args *caa = aux;

	if (pnp)
		printf("%s at %s", caa->ca_name, pnp);	/* not configured */
	if (caa->ca_iobase != -1)
		printf(" port 0x%x", caa->ca_iobase);
	if (caa->ca_maddr != -1)
		printf(" addr 0x%x", caa->ca_maddr);
	if (caa->ca_int != -1)
		printf(" int %d", caa->ca_int);

	return UNCONF;
}

/*
 * Register a C-bus interrupt service routine.
 */
int
cbus_isrlink(int (*func)(void *), void *arg, int intlevel, int ipl,
    const char *name)
{
	struct cbus_softc *sc = NULL;
	struct cbus_isr_t *ci;

	if (cbus_cd.cd_ndevs != 0)
		sc = cbus_cd.cd_devs[0];
	if (sc == NULL)
		panic("cbus_isrlink: can't find cbus_softc");

#ifdef DIAGNOSTIC
	if (intlevel < 0 || intlevel >= NCBUSISR) {
		printf("cbus_isrlink: bad INT level %d\n", intlevel);
		return -1;
	}
#endif

	ci = &sc->cbus_isr[intlevel];

	if (ci->isr_func != NULL) {
		printf("cbus_isrlink: isr already assigned on INT%d\n",
		    intlevel);
		return -1;
	}

	/* set the entry */
	ci->isr_func = func;
	ci->isr_arg = arg;
	ci->isr_intlevel = intlevel;
	ci->isr_ipl = ipl;
	evcount_attach(&ci->isr_count, name, &ci->isr_intlevel);
	sc->registered |= (1 << (6 - intlevel));
#ifdef CBUS_DEBUG
	printf("cbus_isrlink: sc->registered = 0x%02x\n", sc->registered);
#endif

	return 0;
}

/*
 * Unregister a C-bus interrupt service routine.
 */
int
cbus_isrunlink(int (*func)(void *), int intlevel)
{
	struct cbus_softc *sc = NULL;
	struct cbus_isr_t *ci;

	if (cbus_cd.cd_ndevs != 0)
		sc = cbus_cd.cd_devs[0];
	if (sc == NULL)
		panic("cbus_isrunlink: can't find cbus_softc");

#ifdef DIAGNOSTIC
	if (intlevel < 0 || intlevel >= NCBUSISR) {
		printf("cbus_isrunlink: bad INT level %d\n", intlevel);
		return -1;
	}
#endif

	ci = &sc->cbus_isr[intlevel];

	if (ci->isr_func == NULL) {
		printf("cbus_isrunlink: isr not assigned on INT%d\n", intlevel);
		return -1;
	}

	/* reset the entry */
	ci->isr_func = NULL;
	ci->isr_arg = NULL;
	ci->isr_intlevel = ci->isr_ipl = -1;
	evcount_detach(&ci->isr_count);
	sc->registered &= ~(1 << (6 - intlevel));

	/* clear interrupt flags */
	*cbus_isreg = (u_int8_t)(6 - intlevel);
#ifdef CBUS_DEBUG
	printf("cbus_isrunlink: sc->registered = 0x%02x\n", sc->registered);
#endif

	return 0;
}

/*
 * Dispatch C-bus interrupt service routines.
 */
void
cbus_isrdispatch(int intlevel)
{
	int rc, s;
	static int straycount, unexpected;
	struct cbus_softc *sc = NULL;
	struct cbus_isr_t *ci;

	if (cbus_cd.cd_ndevs != 0)
		sc = cbus_cd.cd_devs[0];
	if (sc == NULL)
		panic("cbus_isrdispatch: can't find cbus_softc");

#ifdef DIAGNOSTIC
	if (intlevel < 0 || intlevel >= NCBUSISR)
		panic("cbus_isrdispatch: bad INT level 0x%d", intlevel);
#endif

	ci = &sc->cbus_isr[intlevel];

	if (ci->isr_func == NULL) {
		printf("cbus_isrdispatch: INT%d unexpected\n", intlevel);
		if (++unexpected > 10)
			panic("too many unexpected interrupts");
		return;
	}

	s = splraise(ci->isr_ipl);
	rc = ci->isr_func(ci->isr_arg);
	splx(s);

	if (rc != 0)
		ci->isr_count.ec_count++;

	if (rc)
		straycount = 0;
	else if (++straycount > 50)
		panic("cbus_isrdispatch: too many stray interrupts");
	else
		printf("cbus_isrdispatch: stray INT%d, IPL=%d\n", intlevel,
		    ci->isr_ipl);
}

/*
 * Return registered status of interrupt service routines.
 */
u_int8_t
cbus_intr_registered(void)
{
	struct cbus_softc *sc = NULL;

	if (cbus_cd.cd_ndevs != 0)
		sc = cbus_cd.cd_devs[0];
	if (sc == NULL)
		panic("cbus_intr_used: can't find cbus_softc");

	return sc->registered;
}

/*
 * Note about interrupt on PC-9801 extension board slot
 *
 * PC-9801 extension board slot bus (so-called 'C-bus' in Japan) use 8 own
 * interrupt levels, INT0-INT6, and NMI.  On LUNA-88K2, they all trigger
 * level 4 interrupt on mainbus, so we need to check the dedicated interrupt
 * status register to know which C-bus interrupt is occurred.
 *
 * The interrupt status register for C-bus is located at
 * (u_int8_t *)CBUS_INTR_STAT. Each bit of the register becomes 0 when
 * corresponding C-bus interrupt has occurred, otherwise 1.
 *
 * bit 7 = NMI(?)
 * bit 6 = INT0
 * bit 5 = INT1
 *  :
 * bit 0 = INT6
 *
 * To clear the C-bus interrupt flag, write the corresponding 'bit' number
 * (as u_int_8) to the register.  For example, if you want to clear INT1,
 * you should write '5' like:
 *   *(u_int8_t *)CBUS_INTR_STAT = 5;
 */

/*
 * Interrupt handler on mainbus.
 */
int
cbus_intr(void *arg)
{
	struct cbus_softc *sc = (struct cbus_softc *)arg;
	u_int8_t intr_status;
	int n;

	/*
	 * LUNA-88K2's interrupt level 4 is shared with other devices,
	 * such as le(4), for example.  So we check:
	 * - the value of our C-bus interrupt status register, and
	 * - if the INT level is what we are looking for.
	 */
	intr_status = *cbus_isreg & sc->registered;
	if (intr_status == sc->registered) return 0;	/* Not for me */

#ifdef CBUS_DEBUG
	printf("cbus_intr: called, *cbus_isreg=0x%02x, registered = 0x%02x\n",
	    *cbus_isreg, sc->registered);
#endif
	/* Make the bit pattern that we should process */
	intr_status = intr_status ^ sc->registered;
#ifdef CBUS_DEBUG
	printf("cbus_intr: processing 0x%02x\n", intr_status);
#endif

	/* Process, and clear each interrupt flag */
	while ((n = ff1(intr_status)) != 32) {
		cbus_isrdispatch(6 - n);
		*cbus_isreg = (u_int8_t)n;
		intr_status &= ~(1 << n); 
	}

	return 1;
}
