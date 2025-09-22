/*	$OpenBSD: aplcpu.c,v 1.9 2024/09/29 09:25:37 jsg Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define DVFS_CMD			0x0020
#define DVFS_CMD_BUSY			(1U << 31)
#define DVFS_CMD_SET			(1 << 25)
#define DVFS_CMD_PS2_MASK		(0x1f << 12)
#define DVFS_CMD_PS2_SHIFT		12
#define DVFS_CMD_PS1_MASK		(0x1f << 0)
#define DVFS_CMD_PS1_SHIFT		0

#define DVFS_STATUS			0x50
#define DVFS_T8103_STATUS_CUR_PS_MASK	(0xf << 4)
#define DVFS_T8103_STATUS_CUR_PS_SHIFT	4
#define DVFS_T8112_STATUS_CUR_PS_MASK	(0x1f << 5)
#define DVFS_T8112_STATUS_CUR_PS_SHIFT	5

#define APLCPU_DEEP_WFI_LATENCY		10 /* microseconds */

struct opp {
	uint64_t opp_hz;
	uint32_t opp_level;
};

struct opp_table {
	LIST_ENTRY(opp_table) ot_list;
	uint32_t ot_phandle;

	struct opp *ot_opp;
	u_int ot_nopp;
	uint64_t ot_opp_hz_min;
	uint64_t ot_opp_hz_max;
};

#define APLCPU_MAX_CLUSTERS	8

struct aplcpu_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh[APLCPU_MAX_CLUSTERS];
	bus_size_t		sc_ios[APLCPU_MAX_CLUSTERS];

	int			sc_node;
	u_int			sc_nclusters;
	int			sc_perflevel;

	uint32_t		sc_cur_ps_mask;
	u_int			sc_cur_ps_shift;

	LIST_HEAD(, opp_table)	sc_opp_tables;
	struct opp_table	*sc_opp_table[APLCPU_MAX_CLUSTERS];
	uint64_t		sc_opp_hz_min;
	uint64_t		sc_opp_hz_max;

	struct ksensordev	sc_sensordev;
	struct ksensor		sc_sensor[APLCPU_MAX_CLUSTERS];
};

int	aplcpu_match(struct device *, void *, void *);
void	aplcpu_attach(struct device *, struct device *, void *);

const struct cfattach aplcpu_ca = {
	sizeof (struct aplcpu_softc), aplcpu_match, aplcpu_attach
};

struct cfdriver aplcpu_cd = {
	NULL, "aplcpu", DV_DULL
};

void	aplcpu_opp_init(struct aplcpu_softc *, int);
uint32_t aplcpu_opp_level(struct aplcpu_softc *, int);
int	aplcpu_clockspeed(int *);
void	aplcpu_setperf(int level);
void	aplcpu_refresh_sensors(void *);
void	aplcpu_idle_cycle(void);
void	aplcpu_deep_wfi(void);

int
aplcpu_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,soc-cpufreq") ||
	    OF_is_compatible(faa->fa_node, "apple,cluster-cpufreq");
}

void
aplcpu_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplcpu_softc *sc = (struct aplcpu_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	int i;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	if (faa->fa_nreg > APLCPU_MAX_CLUSTERS) {
		printf(": too many registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	for (i = 0; i < faa->fa_nreg; i++) {
		if (bus_space_map(sc->sc_iot, faa->fa_reg[i].addr,
		    faa->fa_reg[i].size, 0, &sc->sc_ioh[i])) {
			printf(": can't map registers\n");
			goto unmap;
		}
		sc->sc_ios[i] = faa->fa_reg[i].size;
	}

	printf("\n");

	sc->sc_node = faa->fa_node;
	sc->sc_nclusters = faa->fa_nreg;

	if (OF_is_compatible(sc->sc_node, "apple,t8103-soc-cpufreq") ||
	    OF_is_compatible(sc->sc_node, "apple,t8103-cluster-cpufreq")) {
		sc->sc_cur_ps_mask = DVFS_T8103_STATUS_CUR_PS_MASK;
		sc->sc_cur_ps_shift = DVFS_T8103_STATUS_CUR_PS_SHIFT;
	} else if (OF_is_compatible(sc->sc_node, "apple,t8112-soc-cpufreq") ||
	    OF_is_compatible(sc->sc_node, "apple,t8112-cluster-cpufreq")) {
		sc->sc_cur_ps_mask = DVFS_T8112_STATUS_CUR_PS_MASK;
		sc->sc_cur_ps_shift = DVFS_T8112_STATUS_CUR_PS_SHIFT;
	}

	sc->sc_opp_hz_min = UINT64_MAX;
	sc->sc_opp_hz_max = 0;

	LIST_INIT(&sc->sc_opp_tables);
	CPU_INFO_FOREACH(cii, ci) {
		aplcpu_opp_init(sc, ci->ci_node);
	}

	for (i = 0; i < sc->sc_nclusters; i++) {
		sc->sc_sensor[i].type = SENSOR_FREQ;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	}

	aplcpu_refresh_sensors(sc);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, aplcpu_refresh_sensors, 1);

	cpu_idle_cycle_fcn = aplcpu_idle_cycle;
	cpu_suspend_cycle_fcn = aplcpu_deep_wfi;
	cpu_cpuspeed = aplcpu_clockspeed;
	cpu_setperf = aplcpu_setperf;
	return;

unmap:
	for (i = 0; i < faa->fa_nreg; i++) {
		if (sc->sc_ios[i] == 0)
			continue;
		bus_space_unmap(sc->sc_iot, sc->sc_ioh[i], sc->sc_ios[i]);
	}
}

void
aplcpu_opp_init(struct aplcpu_softc *sc, int node)
{
	struct opp_table *ot;
	int count, child;
	uint32_t freq_domain[2], phandle;
	uint32_t opp_hz, opp_level;
	int i, j;

	freq_domain[0] = OF_getpropint(node, "performance-domains", 0);
	freq_domain[1] = 0;
	if (freq_domain[0] == 0) {
		if (OF_getpropintarray(node, "apple,freq-domain", freq_domain,
		    sizeof(freq_domain)) != sizeof(freq_domain))
			return;
		if (freq_domain[1] > APLCPU_MAX_CLUSTERS)
			return;
	}
	if (freq_domain[0] != OF_getpropint(sc->sc_node, "phandle", 0))
		return;
	
	phandle = OF_getpropint(node, "operating-points-v2", 0);
	if (phandle == 0)
		return;

	LIST_FOREACH(ot, &sc->sc_opp_tables, ot_list) {
		if (ot->ot_phandle == phandle) {
			sc->sc_opp_table[freq_domain[1]] = ot;
			return;
		}
	}

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "operating-points-v2"))
		return;

	count = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child))
		count++;
	if (count == 0)
		return;

	ot = malloc(sizeof(struct opp_table), M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_phandle = phandle;
	ot->ot_opp = mallocarray(count, sizeof(struct opp),
	    M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_nopp = count;

	count = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		opp_hz = OF_getpropint64(child, "opp-hz", 0);
		opp_level = OF_getpropint(child, "opp-level", 0);

		/* Insert into the array, keeping things sorted. */
		for (i = 0; i < count; i++) {
			if (opp_hz < ot->ot_opp[i].opp_hz)
				break;
		}
		for (j = count; j > i; j--)
			ot->ot_opp[j] = ot->ot_opp[j - 1];
		ot->ot_opp[i].opp_hz = opp_hz;
		ot->ot_opp[i].opp_level = opp_level;
		count++;
	}

	ot->ot_opp_hz_min = ot->ot_opp[0].opp_hz;
	ot->ot_opp_hz_max = ot->ot_opp[count - 1].opp_hz;

	LIST_INSERT_HEAD(&sc->sc_opp_tables, ot, ot_list);
	sc->sc_opp_table[freq_domain[1]] = ot;

	/* Keep track of overall min/max frequency. */
	if (sc->sc_opp_hz_min > ot->ot_opp_hz_min)
		sc->sc_opp_hz_min = ot->ot_opp_hz_min;
	if (sc->sc_opp_hz_max < ot->ot_opp_hz_max)
		sc->sc_opp_hz_max = ot->ot_opp_hz_max;
}

uint32_t
aplcpu_opp_level(struct aplcpu_softc *sc, int cluster)
{
	uint32_t opp_level;
	uint64_t pstate;

	if (sc->sc_cur_ps_mask) {
		pstate = bus_space_read_8(sc->sc_iot, sc->sc_ioh[cluster],
		    DVFS_STATUS);
		opp_level = (pstate & sc->sc_cur_ps_mask);
		opp_level >>= sc->sc_cur_ps_shift;
	} else {
		pstate = bus_space_read_8(sc->sc_iot, sc->sc_ioh[cluster],
		    DVFS_CMD);
		opp_level = (pstate & DVFS_CMD_PS1_MASK);
		opp_level >>= DVFS_CMD_PS1_SHIFT;
	}

	return opp_level;
}

int
aplcpu_clockspeed(int *freq)
{
	struct aplcpu_softc *sc;
	struct opp_table *ot;
	uint32_t opp_hz = 0, opp_level;
	int i, j, k;

	/*
	 * Clusters can run at different frequencies.  We report the
	 * highest frequency among all clusters.
	 */

	for (i = 0; i < aplcpu_cd.cd_ndevs; i++) {
		sc = aplcpu_cd.cd_devs[i];
		if (sc == NULL)
			continue;

		for (j = 0; j < sc->sc_nclusters; j++) {
			if (sc->sc_opp_table[j] == NULL)
				continue;

			opp_level = aplcpu_opp_level(sc, j);

			/* Translate P-state to frequency. */
			ot = sc->sc_opp_table[j];
			for (k = 0; k < ot->ot_nopp; k++) {
				if (ot->ot_opp[k].opp_level != opp_level)
					continue;
				opp_hz = MAX(opp_hz, ot->ot_opp[k].opp_hz);
			}
		}
	}

	if (opp_hz == 0)
		return EINVAL;

	*freq = opp_hz / 1000000;
	return 0;
}

void
aplcpu_setperf(int level)
{
	struct aplcpu_softc *sc;
	struct opp_table *ot;
	uint64_t min, max;
	uint64_t level_hz;
	uint32_t opp_level;
	uint64_t reg;
	int i, j, k, timo;

	/*
	 * We let the CPU performance level span the entire range
	 * between the lowest frequency on any of the clusters and the
	 * highest frequency on any of the clusters.  We pick a
	 * frequency within that range based on the performance level
	 * and set all the clusters to the frequency that is closest
	 * to but less than that frequency.  This isn't a particularly
	 * sensible method but it is easy to implement and it is hard
	 * to come up with something more sensible given the
	 * constraints of the hw.setperf sysctl interface.
	 */
	for (i = 0; i < aplcpu_cd.cd_ndevs; i++) {
		sc = aplcpu_cd.cd_devs[i];
		if (sc == NULL)
			continue;

		min = sc->sc_opp_hz_min;
		max = sc->sc_opp_hz_max;
		level_hz = min + (level * (max - min)) / 100;
	}

	for (i = 0; i < aplcpu_cd.cd_ndevs; i++) {
		sc = aplcpu_cd.cd_devs[i];
		if (sc == NULL)
			continue;
		if (sc->sc_perflevel == level)
			continue;

		for (j = 0; j < sc->sc_nclusters; j++) {
			if (sc->sc_opp_table[j] == NULL)
				continue;

			/* Translate performance level to a P-state. */
			ot = sc->sc_opp_table[j];
			opp_level = ot->ot_opp[0].opp_level;
			for (k = 0; k < ot->ot_nopp; k++) {
				if (ot->ot_opp[k].opp_hz <= level_hz &&
				    ot->ot_opp[k].opp_level >= opp_level)
					opp_level = ot->ot_opp[k].opp_level;
			}

			/* Wait until P-state logic isn't busy. */
			for (timo = 100; timo > 0; timo--) {
				reg = bus_space_read_8(sc->sc_iot,
				    sc->sc_ioh[j], DVFS_CMD);
				if ((reg & DVFS_CMD_BUSY) == 0)
					break;
				delay(1);
			}
			if (reg & DVFS_CMD_BUSY)
				continue;

			/* Set desired P-state. */
			reg &= ~DVFS_CMD_PS1_MASK;
			reg |= (opp_level << DVFS_CMD_PS1_SHIFT);
			reg |= DVFS_CMD_SET;
			bus_space_write_8(sc->sc_iot, sc->sc_ioh[j],
			    DVFS_CMD, reg);
		}

		sc->sc_perflevel = level;
	}
}

void
aplcpu_refresh_sensors(void *arg)
{
	struct aplcpu_softc *sc = arg;
	struct opp_table *ot;
	uint32_t opp_level;
	int i, j;

	for (i = 0; i < sc->sc_nclusters; i++) {
		if (sc->sc_opp_table[i] == NULL)
			continue;

		opp_level = aplcpu_opp_level(sc, i);

		/* Translate P-state to frequency. */
		ot = sc->sc_opp_table[i];
		for (j = 0; j < ot->ot_nopp; j++) {
			if (ot->ot_opp[j].opp_level == opp_level) {
				sc->sc_sensor[i].value = ot->ot_opp[j].opp_hz;
				break;
			}
		}
	}
}

void
aplcpu_idle_cycle(void)
{
	struct cpu_info *ci = curcpu();
	struct timeval start, stop;
	u_long itime;

	microuptime(&start);

	if (ci->ci_prev_sleep > 3 * APLCPU_DEEP_WFI_LATENCY)
		aplcpu_deep_wfi();
	else
		cpu_wfi();

	microuptime(&stop);
	timersub(&stop, &start, &stop);
	itime = stop.tv_sec * 1000000 + stop.tv_usec;

	ci->ci_last_itime = itime;
	itime >>= 1;
	ci->ci_prev_sleep = (ci->ci_prev_sleep + (ci->ci_prev_sleep >> 1)
	    + itime) >> 1;
}
