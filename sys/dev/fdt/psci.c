/*	$OpenBSD: psci.c,v 1.17 2024/07/10 11:01:24 kettenis Exp $	*/

/*
 * Copyright (c) 2016 Jonathan Gray <jsg@openbsd.org>
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
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/fdt/pscivar.h>

extern void (*cpuresetfn)(void);
extern void (*powerdownfn)(void);

#define SMCCC_VERSION		0x80000000
#define SMCCC_ARCH_FEATURES	0x80000001
#define SMCCC_ARCH_WORKAROUND_1	0x80008000
#define SMCCC_ARCH_WORKAROUND_2	0x80007fff
#define SMCCC_ARCH_WORKAROUND_3	0x80003fff

struct psci_softc {
	struct device	 sc_dev;
	register_t	 (*sc_callfn)(register_t, register_t, register_t,
			     register_t);
	uint32_t	 sc_psci_version;
	uint32_t	 sc_system_off;
	uint32_t	 sc_system_reset;
	uint32_t	 sc_system_suspend;
	uint32_t	 sc_cpu_on;
	uint32_t	 sc_cpu_off;
	uint32_t	 sc_cpu_suspend;

	uint32_t	 sc_smccc_version;
	uint32_t	 sc_method;
};

struct psci_softc *psci_sc;

int	psci_match(struct device *, void *, void *);
void	psci_attach(struct device *, struct device *, void *);
void	psci_reset(void);
void	psci_powerdown(void);

extern register_t hvc_call(register_t, register_t, register_t, register_t);
extern register_t smc_call(register_t, register_t, register_t, register_t);

int32_t smccc_version(void);
int32_t smccc_arch_features(uint32_t);

uint32_t psci_version(void);
int32_t psci_features(uint32_t);

const struct cfattach psci_ca = {
	sizeof(struct psci_softc), psci_match, psci_attach
};

struct cfdriver psci_cd = {
	NULL, "psci", DV_DULL
};

int
psci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "arm,psci") ||
	    OF_is_compatible(faa->fa_node, "arm,psci-0.2") ||
	    OF_is_compatible(faa->fa_node, "arm,psci-1.0");
}

void
psci_attach(struct device *parent, struct device *self, void *aux)
{
	struct psci_softc *sc = (struct psci_softc *)self;
	struct fdt_attach_args *faa = aux;
	char method[128];
	uint32_t version;

	if (OF_getprop(faa->fa_node, "method", method, sizeof(method))) {
		if (strcmp(method, "hvc") == 0) {
			sc->sc_callfn = hvc_call;
			sc->sc_method = PSCI_METHOD_HVC;
		} else if (strcmp(method, "smc") == 0) {
			sc->sc_callfn = smc_call;
			sc->sc_method = PSCI_METHOD_SMC;
		}
	}

	/*
	 * The function IDs are only to be parsed for the old specification
	 * (as in version 0.1).  All newer implementations are supposed to
	 * use the specified values.
	 */
	if (OF_is_compatible(faa->fa_node, "arm,psci-0.2") ||
	    OF_is_compatible(faa->fa_node, "arm,psci-1.0")) {
		sc->sc_psci_version = PSCI_VERSION;
		sc->sc_system_off = SYSTEM_OFF;
		sc->sc_system_reset = SYSTEM_RESET;
		sc->sc_cpu_on = CPU_ON;
		sc->sc_cpu_off = CPU_OFF;
		sc->sc_cpu_suspend = CPU_SUSPEND;
	} else if (OF_is_compatible(faa->fa_node, "arm,psci")) {
		sc->sc_system_off = OF_getpropint(faa->fa_node,
		    "system_off", 0);
		sc->sc_system_reset = OF_getpropint(faa->fa_node,
		    "system_reset", 0);
		sc->sc_cpu_on = OF_getpropint(faa->fa_node, "cpu_on", 0);
		sc->sc_cpu_off = OF_getpropint(faa->fa_node, "cpu_off", 0);
		sc->sc_cpu_suspend = OF_getpropint(faa->fa_node,
		    "cpu_suspend", 0);
	}

	psci_sc = sc;

	version = psci_version();
	printf(": PSCI %d.%d", version >> 16, version & 0xffff);

	if (version >= 0x10000) {
		if (psci_features(SMCCC_VERSION) == PSCI_SUCCESS) {
			sc->sc_smccc_version = smccc_version();
			printf(", SMCCC %d.%d", sc->sc_smccc_version >> 16,
			    sc->sc_smccc_version & 0xffff);
		}
		if (psci_features(SYSTEM_SUSPEND) == PSCI_SUCCESS) {
			sc->sc_system_suspend = SYSTEM_SUSPEND;
			printf(", SYSTEM_SUSPEND");
		}
	}

	printf("\n");

	if (sc->sc_system_off != 0)
		powerdownfn = psci_powerdown;
	if (sc->sc_system_reset != 0)
		cpuresetfn = psci_reset;
}

void
psci_reset(void)
{
	struct psci_softc *sc = psci_sc;

	if (sc->sc_callfn)
		(*sc->sc_callfn)(sc->sc_system_reset, 0, 0, 0);
}

void
psci_powerdown(void)
{
	struct psci_softc *sc = psci_sc;

	if (sc->sc_callfn)
		(*sc->sc_callfn)(sc->sc_system_off, 0, 0, 0);
}

/*
 * Firmware-based workaround for CVE-2017-5715.  We determine whether
 * the workaround is actually implemented and needed the first time we
 * are invoked such that we only make the firmware call when appropriate.
 */

void
psci_flush_bp_none(void)
{
}

void
psci_flush_bp_smccc_arch_workaround_1(void)
{
	struct psci_softc *sc = psci_sc;

	(*sc->sc_callfn)(SMCCC_ARCH_WORKAROUND_1, 0, 0, 0);
}

void
psci_flush_bp(void)
{
	struct psci_softc *sc = psci_sc;
	struct cpu_info *ci = curcpu();

	/*
	 * SMCCC 1.1 allows us to detect if the workaround is
	 * implemented and needed.
	 */
	if (sc && sc->sc_smccc_version >= 0x10001 &&
	    smccc_arch_features(SMCCC_ARCH_WORKAROUND_1) == 0) {
		/* Workaround implemented and needed. */
		ci->ci_flush_bp = psci_flush_bp_smccc_arch_workaround_1;
		ci->ci_flush_bp();
	} else {
		/* Workaround isn't implemented or isn't needed. */
		ci->ci_flush_bp = psci_flush_bp_none;
	}
}

void
smccc_enable_arch_workaround_2(void)
{
	struct psci_softc *sc = psci_sc;

	/*
	 * SMCCC 1.1 allows us to detect if the workaround is
	 * implemented and needed.
	 */
	if (sc && sc->sc_smccc_version >= 0x10001 &&
	    smccc_arch_features(SMCCC_ARCH_WORKAROUND_2) == 0) {
		/* Workaround implemented and needed. */
		(*sc->sc_callfn)(SMCCC_ARCH_WORKAROUND_2, 1, 0, 0);
	}
}

int
smccc_needs_arch_workaround_3(void)
{
	struct psci_softc *sc = psci_sc;

	/*
	 * SMCCC 1.1 allows us to detect if the workaround is
	 * implemented and needed.
	 */
	if (sc && sc->sc_smccc_version >= 0x10001 &&
	    smccc_arch_features(SMCCC_ARCH_WORKAROUND_3) == 0) {
		/* Workaround implemented and needed. */
		return 1;
	}

	return 0;
}

int32_t
smccc_version(void)
{
	struct psci_softc *sc = psci_sc;
	int32_t version;

	KASSERT(sc && sc->sc_callfn);
	version = (*sc->sc_callfn)(SMCCC_VERSION, 0, 0, 0);
	if (version != PSCI_NOT_SUPPORTED)
		return version;

	/* Treat NOT_SUPPORTED as 1.0 */
	return 0x10000;
}

int32_t
smccc(uint32_t func_id, register_t arg0, register_t arg1, register_t arg2)
{
	struct psci_softc *sc = psci_sc;

	if (sc && sc->sc_callfn)
		return (*sc->sc_callfn)(func_id, arg0, arg1, arg2);

	return PSCI_NOT_SUPPORTED;
}

int32_t
smccc_arch_features(uint32_t arch_func_id)
{
	struct psci_softc *sc = psci_sc;

	KASSERT(sc && sc->sc_callfn);
	return (*sc->sc_callfn)(SMCCC_ARCH_FEATURES, arch_func_id, 0, 0);
}

uint32_t
psci_version(void)
{
	struct psci_softc *sc = psci_sc;

	if (sc && sc->sc_callfn && sc->sc_psci_version != 0)
		return (*sc->sc_callfn)(sc->sc_psci_version, 0, 0, 0);

	/* No version support; return 0.0. */
	return 0;
}

int32_t
psci_system_suspend(register_t entry_point_address, register_t context_id)
{
	struct psci_softc *sc = psci_sc;

	if (sc && sc->sc_callfn && sc->sc_system_suspend != 0)
		return (*sc->sc_callfn)(sc->sc_system_suspend,
		    entry_point_address, context_id, 0);

	return PSCI_NOT_SUPPORTED;
}

int32_t
psci_cpu_off(void)
{
	struct psci_softc *sc = psci_sc;

	if (sc && sc->sc_callfn && sc->sc_cpu_off != 0)
		return (*sc->sc_callfn)(sc->sc_cpu_off, 0, 0, 0);

	return PSCI_NOT_SUPPORTED;
}

int32_t
psci_cpu_on(register_t target_cpu, register_t entry_point_address,
    register_t context_id)
{
	struct psci_softc *sc = psci_sc;

	if (sc && sc->sc_callfn && sc->sc_cpu_on != 0)
		return (*sc->sc_callfn)(sc->sc_cpu_on, target_cpu,
		    entry_point_address, context_id);

	return PSCI_NOT_SUPPORTED;
}

int32_t
psci_cpu_suspend(register_t power_state, register_t entry_point_address,
    register_t context_id)
{
	struct psci_softc *sc = psci_sc;

	if (sc && sc->sc_callfn && sc->sc_cpu_suspend != 0)
		return (*sc->sc_callfn)(sc->sc_cpu_suspend, power_state,
		    entry_point_address, context_id);

	return PSCI_NOT_SUPPORTED;
}

int32_t
psci_features(uint32_t psci_func_id)
{
	struct psci_softc *sc = psci_sc;

	if (sc && sc->sc_callfn)
		return (*sc->sc_callfn)(PSCI_FEATURES, psci_func_id, 0, 0);

	return PSCI_NOT_SUPPORTED;
}

int
psci_can_suspend(void)
{
	struct psci_softc *sc = psci_sc;

	return (sc && sc->sc_system_suspend != 0);
}

int
psci_method(void)
{
	struct psci_softc *sc = psci_sc;

	return sc ? sc->sc_method : PSCI_METHOD_NONE;
}
