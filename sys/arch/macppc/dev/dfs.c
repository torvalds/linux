/*	$OpenBSD: dfs.c,v 1.4 2022/03/13 12:33:01 mpi Exp $	*/
/*
 * Copyright (c) 2011 Martin Pieuchot <mpi@openbsd.org>
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
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <dev/ofw/openfirm.h>

#include <machine/cpu.h>
#include <machine/autoconf.h>
#include <macppc/pci/macobio.h>
#include <powerpc/hid.h>

extern int perflevel;

struct dfs_softc {
	struct device	sc_dev;
	int		sc_voltage;
};

int	dfs_match(struct device *, void *, void *);
void	dfs_attach(struct device *, struct device *, void *);
void	dfs_setperf(int);
void	dfs_scale_frequency(u_int);

const struct cfattach dfs_ca = {
	sizeof(struct dfs_softc), dfs_match, dfs_attach
};

struct cfdriver dfs_cd = {
	NULL, "dfs", DV_DULL
};

int
dfs_match(struct device *parent, void *arg, void *aux)
{
	struct confargs *ca = aux;
	uint16_t cpu;

	if (strcmp(ca->ca_name, "cpu-vcore-select") != 0)
		return (0);

	cpu = ppc_mfpvr() >> 16;
	if (cpu == PPC_CPU_MPC7447A || cpu == PPC_CPU_MPC7448)
			return (1);

	return (0);
}

void
dfs_attach(struct device *parent, struct device *self, void *aux)
{
	struct dfs_softc *sc = (struct dfs_softc *)self;
	struct confargs *ca = aux;
	uint32_t hid1, reg;
	uint16_t cpu;

	/*
	 * On some models the vcore-select offset is relative to
	 * its parent offset and not to the bus base address.
	 */
	OF_getprop(OF_parent(ca->ca_node), "reg", &reg, sizeof(reg));
	if (reg > ca->ca_reg[0])
		sc->sc_voltage = reg + ca->ca_reg[0];
	else
		sc->sc_voltage = ca->ca_reg[0];

	hid1 = ppc_mfhid1();

	if (hid1 & HID1_DFS4) {
		ppc_curfreq = ppc_maxfreq / 4;
		perflevel = 25;
	} else if (hid1 & HID1_DFS2) {
		ppc_curfreq = ppc_maxfreq / 2;
		perflevel = 50;
	}

	cpu_setperf = dfs_setperf;

	printf(": speeds: %d, %d", ppc_maxfreq, ppc_maxfreq / 2);

	cpu = ppc_mfpvr() >> 16;
	if (cpu == PPC_CPU_MPC7448)
		printf(", %d", ppc_maxfreq / 4);
	printf(" MHz\n");
}

void
dfs_setperf(int perflevel)
{
	struct dfs_softc *sc = dfs_cd.cd_devs[0];

	if (perflevel > 50) {
		if (ppc_curfreq != ppc_maxfreq) {
			macobio_write(sc->sc_voltage, GPIO_DDR_OUTPUT | 1);
			delay(1000);
			dfs_scale_frequency(FREQ_FULL);
		}
	} else {
		uint16_t cpu;

		cpu = ppc_mfpvr() >> 16;
		if (cpu == PPC_CPU_MPC7448 && perflevel <= 25)  {
			if (ppc_curfreq != ppc_maxfreq / 4) {
				dfs_scale_frequency(FREQ_QUARTER);
				macobio_write(sc->sc_voltage,
				    GPIO_DDR_OUTPUT | 0);
				delay(1000);
			}
		} else {
			if (ppc_curfreq != ppc_maxfreq / 2) {
				dfs_scale_frequency(FREQ_HALF);
				macobio_write(sc->sc_voltage,
				    GPIO_DDR_OUTPUT | 0);
				delay(1000);
			}
		}
	}
}

void
dfs_scale_frequency(u_int freq_scale)
{
	uint32_t hid1;
	int s;

	s = splhigh();
	hid1 = ppc_mfhid1();

	hid1 &= ~(HID1_DFS2 | HID1_DFS4);
	switch (freq_scale) {
	case FREQ_QUARTER:
		hid1 |= HID1_DFS4;
		ppc_curfreq = ppc_maxfreq / 4;
		break;
	case FREQ_HALF:
		hid1 |= HID1_DFS2;
		ppc_curfreq = ppc_maxfreq / 2;
		break;
	case FREQ_FULL: /* FALLTHROUGH */
	default:
		ppc_curfreq = ppc_maxfreq;
	}

	asm volatile ("sync");
	ppc_mthid1(hid1);
	asm volatile ("sync; isync");

	splx(s);
}
