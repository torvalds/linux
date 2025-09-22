/*	$OpenBSD: glxclk.c,v 1.11 2023/09/17 14:50:51 cheloha Exp $	*/

/*
 * Copyright (c) 2013 Paul Irofti.
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
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/stdint.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <dev/isa/isavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/glxreg.h>
#include <dev/pci/glxvar.h>

struct glxclk_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct intrclock	sc_intrclock;
};

struct cfdriver glxclk_cd = {
	NULL, "glxclk", DV_DULL
};

int	glxclk_match(struct device *, void *, void *);
void	glxclk_attach(struct device *, struct device *, void *);
int	glxclk_intr(void *);
void	glxclk_initclock(void);
void	glxclk_startclock(struct cpu_info *);
void	glxclk_rearm(void *, uint64_t);
void	glxclk_trigger(void *);

const struct cfattach glxclk_ca = {
	sizeof(struct glxclk_softc), glxclk_match, glxclk_attach,
};

#define	MSR_LBAR_ENABLE		0x100000000ULL
#define	MSR_LBAR_MFGPT		DIVIL_LBAR_MFGPT
#define	MSR_MFGPT_SIZE		0x40
#define	MSR_MFGPT_ADDR_MASK	0xffc0

/*
 * Experience shows that the clock source goes crazy on scale factors
 * lower than 8, so keep it at 8.
 */

#define	AMD5536_MFGPT1_CMP2	0x0000000a	/* Compare value for CMP2 */
#define	AMD5536_MFGPT1_CNT	0x0000000c	/* Up counter */
#define	AMD5536_MFGPT1_SETUP	0x0000000e	/* Setup register */
#define	AMD5536_MFGPT1_SCALE	0x3		/* Set divider to 8 */
#define	AMD5536_MFGPT1_CLOCK	(1 << 15)	/* Clock frequency */
#define	AMD5536_MFGPT1_C2_IRQM	0x00000200

#define	AMD5536_MFGPT_CNT_EN	(1 << 15)	/* Enable counting */
#define	AMD5536_MFGPT_CMP2	(1 << 14)	/* Compare 2 output */
#define	AMD5536_MFGPT_CMP1	(1 << 13)	/* Compare 1 output */
#define AMD5536_MFGPT_SETUP	(1 << 12)	/* Set to 1 after 1st write */
#define	AMD5536_MFGPT_STOP_EN	(1 << 11)	/* Stop enable */
#define	AMD5536_MFGPT_CMP2MODE	(1 << 9)|(1 << 8)/* Set to GE + activate IRQ */
#define AMD5536_MFGPT_CLKSEL	(1 << 4)	/* Clock select 14MHz */


struct glxclk_softc *glxclk_sc;

int
glxclk_match(struct device *parent, void *match, void *aux)
{
	struct glxpcib_attach_args *gaa = aux;
	struct cfdata *cf = match;

	if (strcmp(gaa->gaa_name, cf->cf_driver->cd_name) != 0)
		return 0;

	return 1;
}

void
glxclk_attach(struct device *parent, struct device *self, void *aux)
{
	glxclk_sc = (struct glxclk_softc *)self;
	struct glxpcib_attach_args *gaa = aux;
	u_int64_t wa;

	glxclk_sc->sc_iot = gaa->gaa_iot;
	glxclk_sc->sc_ioh = gaa->gaa_ioh;

	wa = rdmsr(MSR_LBAR_MFGPT);

	if ((wa & MSR_LBAR_ENABLE) == 0) {
		printf(" not configured\n");
		return;
	}

	if (bus_space_map(glxclk_sc->sc_iot, wa & MSR_MFGPT_ADDR_MASK,
	    MSR_MFGPT_SIZE, 0, &glxclk_sc->sc_ioh)) {
		printf(" not configured\n");
		return;
	}

	/* Set comparator 2 */
	bus_space_write_2(glxclk_sc->sc_iot, glxclk_sc->sc_ioh,
	    AMD5536_MFGPT1_CMP2, 1);

	/* Reset counter to 0 */
	bus_space_write_2(glxclk_sc->sc_iot, glxclk_sc->sc_ioh,
	    AMD5536_MFGPT1_CNT, 0);

	/*
	 * All the bits in the range 11:0 have to be written at once.
	 * After they're set the first time all further writes are
	 * ignored.
	 */
	uint16_t setup = (AMD5536_MFGPT1_SCALE | AMD5536_MFGPT_CMP2MODE);

	bus_space_write_2(glxclk_sc->sc_iot, glxclk_sc->sc_ioh,
	    AMD5536_MFGPT1_SETUP, setup);

	/* Check to see if the MFGPT_SETUP bit was set */
	setup = bus_space_read_2(glxclk_sc->sc_iot, glxclk_sc->sc_ioh,
	    AMD5536_MFGPT1_SETUP);
	if ((setup & AMD5536_MFGPT_SETUP) == 0) {
		printf(" not configured\n");
		return;
	}

	/* Enable MFGPT1 Comparator 2 Output to the Interrupt Mapper */
	wa = rdmsr(MFGPT_IRQ);
	wa |= AMD5536_MFGPT1_C2_IRQM;
	wrmsr(MFGPT_IRQ, wa);

	/*
	 * Tie PIC input 5 to IG7 for glxclk(4).
	 */
	wa = rdmsr(PIC_ZSEL_LOW);
	wa &= ~(0xfUL << 20);
	wa |= 7 << 20;
	wrmsr(PIC_ZSEL_LOW, wa);

	/*
	 * The interrupt argument is NULL in order to notify the dispatcher
	 * to pass the clock frame as argument. This trick also forces keeping
	 * the soft state global because during the interrupt we need to
	 * disable the counter in the MFGPT setup register.
	 */
	isa_intr_establish(sys_platform->isa_chipset, 7, IST_LEVEL, IPL_CLOCK,
	    glxclk_intr, NULL, "clock");

	glxclk_sc->sc_intrclock.ic_cookie = glxclk_sc;
	glxclk_sc->sc_intrclock.ic_rearm = glxclk_rearm;
	glxclk_sc->sc_intrclock.ic_trigger = glxclk_trigger;

	md_initclock = glxclk_initclock;
	md_startclock = glxclk_startclock;

	printf("\n");
}

void
glxclk_initclock(void)
{
	/*
	 * MFGPT runs on powers of two, adjust the hz value accordingly.
	 */
	stathz = hz = 128;
	profhz = hz * 10;
	statclock_is_randomized = 1;
	tick = 1000000 / hz;
	tick_nsec = 1000000000 / hz;
}

void
glxclk_startclock(struct cpu_info *ci)
{
	clockintr_cpu_init(&glxclk_sc->sc_intrclock);

	/* Start the clock. */
	int s = splclock();
	ci->ci_clock_started++;
	clockintr_trigger();
	splx(s);
}

int
glxclk_intr(void *arg)
{
	struct clockframe *frame = arg;
	struct cpu_info *ci = curcpu();
	struct glxclk_softc *sc = glxclk_sc;

	/* Clear the current event and disable the counter. */
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT1_SETUP,
	    AMD5536_MFGPT_CMP1 | AMD5536_MFGPT_CMP2);

	if (ci->ci_clock_started == 0)
		return 1;

	clockintr_dispatch(frame);

	return 1;
}

void
glxclk_rearm(void *cookie, uint64_t nsecs)
{
	const uint64_t freq = AMD5536_MFGPT1_CLOCK >> AMD5536_MFGPT1_SCALE;
	const uint64_t nsec_ratio = (freq << 32) / 1000000000;
	/* Subtract 1 to leave room for rounding. */
	const uint64_t nsec_max = UINT64_MAX / nsec_ratio - 1;
	struct glxclk_softc *sc = cookie;
	uint64_t count;
	register_t sr;

	nsecs = MIN(nsecs, nsec_max);
	count = MAX((nsecs * nsec_ratio + (1U << 31)) >> 32, 1);

	sr = disableintr();
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT1_CMP2, count);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT1_CNT, 0);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT1_SETUP,
	    AMD5536_MFGPT_CNT_EN | AMD5536_MFGPT_CMP2);
	setsr(sr);
}

void
glxclk_trigger(void *cookie)
{
	glxclk_rearm(cookie, 0);
}
