/* $OpenBSD: acpihpet.c,v 1.32 2025/09/16 12:18:10 hshoexer Exp $ */
/*
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
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
#include <sys/stdint.h>
#include <sys/timetc.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>

int acpihpet_attached;

int acpihpet_match(struct device *, void *, void *);
void acpihpet_attach(struct device *, struct device *, void *);
int acpihpet_activate(struct device *, int);
void acpihpet_delay(int);
u_int acpihpet_gettime(struct timecounter *tc);

uint64_t	acpihpet_r(bus_space_tag_t _iot, bus_space_handle_t _ioh,
		    bus_size_t _ioa);
void		acpihpet_w(bus_space_tag_t _iot, bus_space_handle_t _ioh,
		    bus_size_t _ioa, uint64_t _val);

static struct timecounter hpet_timecounter = {
	.tc_get_timecount = acpihpet_gettime,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = 0,
	.tc_quality = 1000,
	.tc_priv = NULL,
	.tc_user = 0,
};

#define HPET_TIMERS	3
struct hpet_regs {
	uint64_t	configuration;
	uint64_t	interrupt_status;
	uint64_t	main_counter;
	struct {	/* timers */
		uint64_t config;
		uint64_t compare;
		uint64_t interrupt;
	} timers[HPET_TIMERS];
};

struct acpihpet_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_conf;
	struct hpet_regs	sc_save;
};

const struct cfattach acpihpet_ca = {
	sizeof(struct acpihpet_softc), acpihpet_match, acpihpet_attach,
	NULL, acpihpet_activate
};

struct cfdriver acpihpet_cd = {
	NULL, "acpihpet", DV_DULL, CD_COCOVM
};

uint64_t
acpihpet_r(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t ioa)
{
	uint64_t val;

	val = bus_space_read_4(iot, ioh, ioa + 4);
	val = val << 32;
	val |= bus_space_read_4(iot, ioh, ioa);
	return (val);
}

void
acpihpet_w(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t ioa,
    uint64_t val)
{
	bus_space_write_4(iot, ioh, ioa + 4, val >> 32);
	bus_space_write_4(iot, ioh, ioa, val & 0xffffffff);
}

int
acpihpet_activate(struct device *self, int act)
{
	struct acpihpet_softc *sc = (struct acpihpet_softc *) self;

	switch (act) {
	case DVACT_SUSPEND:
		delay_fini(acpihpet_delay);

		/* stop, then save */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    HPET_CONFIGURATION, sc->sc_conf);

		sc->sc_save.configuration = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_CONFIGURATION);
		sc->sc_save.interrupt_status = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_INTERRUPT_STATUS);
		sc->sc_save.main_counter = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_MAIN_COUNTER);
		sc->sc_save.timers[0].config = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER0_CONFIG);
		sc->sc_save.timers[0].interrupt = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER0_INTERRUPT);
		sc->sc_save.timers[0].compare = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER0_COMPARE);
		sc->sc_save.timers[1].config = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER1_CONFIG);
		sc->sc_save.timers[1].interrupt = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER1_INTERRUPT);
		sc->sc_save.timers[1].compare = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER1_COMPARE);
		sc->sc_save.timers[2].config = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER2_CONFIG);
		sc->sc_save.timers[2].interrupt = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER2_INTERRUPT);
		sc->sc_save.timers[2].compare = acpihpet_r(sc->sc_iot,
		    sc->sc_ioh, HPET_TIMER2_COMPARE);
		break;
	case DVACT_RESUME:
		/* stop, restore, then restart */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    HPET_CONFIGURATION, sc->sc_conf);

		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_CONFIGURATION, sc->sc_save.configuration);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_INTERRUPT_STATUS, sc->sc_save.interrupt_status);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_MAIN_COUNTER, sc->sc_save.main_counter);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER0_CONFIG, sc->sc_save.timers[0].config);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER0_INTERRUPT, sc->sc_save.timers[0].interrupt);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER0_COMPARE, sc->sc_save.timers[0].compare);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER1_CONFIG, sc->sc_save.timers[1].config);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER1_INTERRUPT, sc->sc_save.timers[1].interrupt);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER1_COMPARE, sc->sc_save.timers[1].compare);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER2_CONFIG, sc->sc_save.timers[2].config);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER2_INTERRUPT, sc->sc_save.timers[2].interrupt);
		acpihpet_w(sc->sc_iot, sc->sc_ioh,
		    HPET_TIMER2_COMPARE, sc->sc_save.timers[2].compare);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    HPET_CONFIGURATION, sc->sc_conf | 1);

		delay_init(acpihpet_delay, 2000);
		break;
	}

	return 0;
}

int
acpihpet_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpi_table_header *hdr;

	/*
	 * If we do not have a table, it is not us; attach only once
	 */
	if (acpihpet_attached || aaa->aaa_table == NULL)
		return (0);

	/*
	 * If it is an HPET table, we can attach
	 */
	hdr = (struct acpi_table_header *)aaa->aaa_table;
	if (memcmp(hdr->signature, HPET_SIG, sizeof(HPET_SIG) - 1) != 0)
		return (0);

	return (1);
}

void
acpihpet_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpihpet_softc *sc = (struct acpihpet_softc *) self;
	struct acpi_softc *psc = (struct acpi_softc *)parent;
	struct acpi_attach_args *aaa = aux;
	struct acpi_hpet *hpet = (struct acpi_hpet *)aaa->aaa_table;
	uint64_t period, freq;	/* timer period in femtoseconds (10^-15) */
	uint32_t v1, v2;
	int timeout;

	if (acpi_map_address(psc, &hpet->base_address, 0, HPET_REG_SIZE,
	    &sc->sc_ioh, &sc->sc_iot))	{
		printf(": can't map i/o space\n");
		return;
	}

	/*
	 * Revisions 0x30 through 0x3a of the AMD SB700, with spread
	 * spectrum enabled, have an SMM based HPET emulation that's
	 * subtly broken.  The hardware is initialized upon first
	 * access of the configuration register.  Initialization takes
	 * some time during which the configuration register returns
	 * 0xffffffff.
	 */
	timeout = 1000;
	do {
		if (bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    HPET_CONFIGURATION) != 0xffffffff)
			break;
	} while(--timeout > 0);

	if (timeout == 0) {
		printf(": disabled\n");
		return;
	}

	/* enable hpet */
	sc->sc_conf = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    HPET_CONFIGURATION) & ~1;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, HPET_CONFIGURATION,
	    sc->sc_conf | 1);

	/* make sure hpet is working */
	v1 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, HPET_MAIN_COUNTER);
	delay(1);
	v2 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, HPET_MAIN_COUNTER);
	if (v1 == v2) {
		printf(": counter not incrementing\n");
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    HPET_CONFIGURATION, sc->sc_conf);
		return;
	}

	period = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    HPET_CAPABILITIES + sizeof(uint32_t));

	/* Period must be > 0 and less than 100ns (10^8 fs) */
	if (period == 0 || period > HPET_MAX_PERIOD) {
		printf(": invalid period\n");
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    HPET_CONFIGURATION, sc->sc_conf);
		return;
	}
	freq = 1000000000000000ull / period;
	printf(": %lld Hz\n", freq);

	hpet_timecounter.tc_frequency = freq;
	hpet_timecounter.tc_priv = sc;
	hpet_timecounter.tc_name = sc->sc_dev.dv_xname;
	tc_init(&hpet_timecounter);

	delay_init(acpihpet_delay, 2000);

#if defined(__amd64__)
	extern void cpu_recalibrate_tsc(struct timecounter *);
	cpu_recalibrate_tsc(&hpet_timecounter);
#endif
	acpihpet_attached++;
}

void
acpihpet_delay(int usecs)
{
	uint64_t count = 0, cycles;
	struct acpihpet_softc *sc = hpet_timecounter.tc_priv;
	uint32_t val1, val2;

	val2 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, HPET_MAIN_COUNTER);
	cycles = usecs * hpet_timecounter.tc_frequency / 1000000;
	while (count < cycles) {
		CPU_BUSY_CYCLE();
		val1 = val2;
		val2 = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    HPET_MAIN_COUNTER);
		count += val2 - val1;
	}
}

u_int
acpihpet_gettime(struct timecounter *tc)
{
	struct acpihpet_softc *sc = tc->tc_priv;

	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, HPET_MAIN_COUNTER));
}
