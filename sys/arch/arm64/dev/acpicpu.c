/*	$OpenBSD: acpicpu.c,v 1.1 2025/06/07 15:11:12 kettenis Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis
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

#include "kstat.h"

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kstat.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/task.h>

#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

struct acpicpu_softc {
	struct device	sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node	*sc_node;
	uint64_t	sc_mpidr;

	uint64_t	sc_highest_perf;
	uint64_t	sc_nominal_perf;
	uint64_t	sc_lowest_perf;
	uint64_t	sc_nominal_freq;
	uint64_t	sc_lowest_freq;
	struct acpi_gas	sc_desired_perf;

	uint64_t	sc_perf;
};

int	acpicpu_match(struct device *, void *, void *);
void	acpicpu_attach(struct device *, struct device *, void *);

const struct cfattach acpicpu_ca = {
	sizeof(struct acpicpu_softc), acpicpu_match, acpicpu_attach
};

struct cfdriver acpicpu_cd = {
	NULL, "acpicpu", DV_DULL
};

const char *acpicpu_hids[] = {
	"ACPI0007",
	NULL
};

uint64_t acpicpu_lowest_perf = UINT64_MAX;
uint64_t acpicpu_highest_perf = 0;
struct task acpicpu_setperf_task;

void	acpicpu_do_setperf(void *);
void	acpicpu_setperf(int);
int	acpicpu_cpuspeed(int *);
void	acpicpu_kstat_attach(struct acpicpu_softc *);

int
acpicpu_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, acpicpu_hids, cf->cf_driver->cd_name);
}

void
acpicpu_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpicpu_softc *sc = (struct acpicpu_softc *)self;
	struct acpi_table_header *hdr;
	struct acpi_madt *madt = NULL;
	struct acpi_q *entry;
	struct aml_value res;
	caddr_t addr;
	uint64_t uid = 0;
	int found = 0;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	aml_evalinteger(sc->sc_acpi, sc->sc_node, "_UID", 0, NULL, &uid);

	printf("\n");

	/* Look for MADT table. */
	SIMPLEQ_FOREACH(entry, &acpi_softc->sc_tables, q_next) {
		hdr = entry->q_table;
		if (strncmp(hdr->signature, MADT_SIG,
		    sizeof(hdr->signature)) == 0) {
			madt = entry->q_table;
			break;
		}
	}
	if (madt == NULL)
		return;

	/* Locate the CPU in the MADT table. */
	addr = (caddr_t)(madt + 1);
	while (addr < (caddr_t)madt + madt->hdr.length) {
		struct acpi_madt_gicc *gicc = (struct acpi_madt_gicc *)addr;

		if (gicc->apic_type == ACPI_MADT_GICC) {
			if (gicc->acpi_proc_uid == uid) {
				sc->sc_mpidr = gicc->mpidr;
				found = 1;
				break;
			}
		}

		addr += gicc->length;
	}
	if (!found)
		return;

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_CPC", 0, NULL, &res))
		return;

	if (res.type != AML_OBJTYPE_PACKAGE || res.length < 23)
		return;

	/* Check number of entries in the _CPC package. */
	if (res.v_package[0]->type != AML_OBJTYPE_INTEGER ||
	    res.v_package[0]->v_integer < 23)
		return;

	/* Check revision number of the _CPC package format. */
	if (res.v_package[1]->type != AML_OBJTYPE_INTEGER ||
	    res.v_package[1]->v_integer != 3)
		return;

	if (res.v_package[2]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[3]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[4]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[5]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[6]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[7]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[8]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[9]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[10]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[11]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[12]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[13]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[14]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[15]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[16]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[17]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[18]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[19]->type != AML_OBJTYPE_BUFFER ||
	    res.v_package[20]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[21]->type != AML_OBJTYPE_INTEGER || /* or Buffer */
	    res.v_package[22]->type != AML_OBJTYPE_INTEGER) /* or Buffer */
		return;

	memcpy(&sc->sc_desired_perf, &res.v_package[7]->v_buffer[3],
	    sizeof(struct acpi_gas));

	sc->sc_highest_perf = res.v_package[2]->v_integer;
	sc->sc_nominal_perf = res.v_package[3]->v_integer;
	sc->sc_lowest_perf = res.v_package[5]->v_integer;
	sc->sc_lowest_freq = res.v_package[21]->v_integer;
	sc->sc_nominal_freq = res.v_package[22]->v_integer;

	if (acpicpu_lowest_perf > sc->sc_lowest_perf)
		acpicpu_lowest_perf = sc->sc_lowest_perf;
	if (acpicpu_highest_perf < sc->sc_highest_perf)
		acpicpu_highest_perf = sc->sc_highest_perf;

	task_set(&acpicpu_setperf_task, acpicpu_do_setperf, NULL);
	cpu_setperf = acpicpu_setperf;

	cpu_cpuspeed = acpicpu_cpuspeed;
	acpicpu_kstat_attach(sc);
}

void
acpicpu_do_setperf(void *arg)
{
	struct acpicpu_softc *sc;
	int i;

	for (i = 0; i < acpicpu_cd.cd_ndevs; i++) {
		sc = acpicpu_cd.cd_devs[i];
		if (sc == NULL || sc->sc_nominal_perf == 0)
			continue;

		acpi_gasio(sc->sc_acpi, ACPI_IOWRITE,
		    sc->sc_desired_perf.address_space_id,
		    sc->sc_desired_perf.address,
		    (1 << (sc->sc_desired_perf.access_size - 1)),
		    sc->sc_desired_perf.register_bit_width / 8, &sc->sc_perf);
	}
}

void
acpicpu_setperf(int level)
{
	struct acpicpu_softc *sc;
	uint64_t min, max, perf;
	int i;

	/*
	 * Map the performance level onto the CPPC performance scale.
	 */
	min = acpicpu_lowest_perf;
	max = acpicpu_highest_perf;
	perf = min + (level * (max - min)) / 100;

	for (i = 0; i < acpicpu_cd.cd_ndevs; i++) {
		sc = acpicpu_cd.cd_devs[i];
		if (sc == NULL || sc->sc_nominal_perf == 0)
			continue;

		/*
		 * Clamp performance based on the limits for this CPU.
		 * For now use the nominal performance as the maximum
		 * as setting all CPUs to the highest performance at
		 * the same time probably isn't going to work well.
		 */
		if (perf < sc->sc_lowest_perf)
			sc->sc_perf = sc->sc_lowest_perf;
		else if (perf > sc->sc_nominal_perf)
			sc->sc_perf = sc->sc_nominal_perf;
		else
			sc->sc_perf = perf;
	}

	task_add(systq, &acpicpu_setperf_task);
}

int
acpicpu_cpuspeed(int *freq)
{
	struct acpicpu_softc *sc = NULL;
	uint64_t delta_perf, perf = 0;
	uint64_t delta_freq;
	int i;

	for (i = 0; i < acpicpu_cd.cd_ndevs; i++) {
		sc = acpicpu_cd.cd_devs[i];
		if (sc == NULL || sc->sc_nominal_perf == 0)
			continue;
	}
	KASSERT(sc != NULL);

	acpi_gasio(sc->sc_acpi, ACPI_IOREAD,
	    sc->sc_desired_perf.address_space_id, sc->sc_desired_perf.address,
	    (1 << (sc->sc_desired_perf.access_size - 1)),
	    sc->sc_desired_perf.register_bit_width / 8, &perf);

	delta_freq = sc->sc_nominal_freq - sc->sc_lowest_freq;
	delta_perf = sc->sc_nominal_perf - sc->sc_lowest_perf;
	*freq = sc->sc_lowest_freq +
	    (perf - sc->sc_lowest_perf) * delta_freq / delta_perf;
	return 0;
}

struct acpicpu_kstats {
	struct kstat_kv		ak_perf;
	struct kstat_kv		ak_freq;
};

int
acpicpu_kstat_read(struct kstat *ks)
{
	struct acpicpu_softc *sc = ks->ks_softc;
	struct acpicpu_kstats *ak = ks->ks_data;
	struct timespec now, diff;
	uint64_t delta_perf, perf;
	uint64_t delta_freq, freq;

	/* rate limit */
	getnanouptime(&now);
	timespecsub(&now, &ks->ks_updated, &diff);
	if (diff.tv_sec < 1)
		return 0;

	perf = 0;
	acpi_gasio(sc->sc_acpi, ACPI_IOREAD,
	    sc->sc_desired_perf.address_space_id, sc->sc_desired_perf.address,
	    (1 << (sc->sc_desired_perf.access_size - 1)),
	    sc->sc_desired_perf.register_bit_width / 8, &perf);
	if (perf == 0)
		return 0;
	kstat_kv_u64(&ak->ak_perf) = perf;

	delta_freq = sc->sc_nominal_freq - sc->sc_lowest_freq;
	delta_perf = sc->sc_nominal_perf - sc->sc_lowest_perf;
	freq = sc->sc_lowest_freq +
	    (perf - sc->sc_lowest_perf) * delta_freq / delta_perf;
	kstat_kv_freq(&ak->ak_freq) = freq * 1000000;

	ks->ks_updated = now;

	return 0;
}

void
acpicpu_kstat_attach(struct acpicpu_softc *sc)
{
	struct kstat *ks;
	struct acpicpu_kstats *ak;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		if (sc->sc_mpidr == ci->ci_mpidr)
			break;
	}
	if (ci == NULL)
		return;

	ks = kstat_create(ci->ci_dev->dv_xname, 0, "cppc", 0, KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("%s: unable to create cppc kstats\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	ak = malloc(sizeof(*ak), M_DEVBUF, M_WAITOK);

	kstat_kv_init(&ak->ak_perf, "perf", KSTAT_KV_T_UINT64);
	kstat_kv_init(&ak->ak_freq, "freq", KSTAT_KV_T_FREQ);

	ks->ks_softc = sc;
	ks->ks_data = ak;
	ks->ks_datalen = sizeof(*ak);
	ks->ks_read = acpicpu_kstat_read;

	kstat_install(ks);
}
