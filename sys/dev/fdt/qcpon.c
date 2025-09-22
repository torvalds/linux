/*	$OpenBSD: qcpon.c,v 1.7 2025/06/16 20:21:33 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/task.h>
#include <sys/proc.h>
#include <sys/signalvar.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/spmivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define PON_RT_STS		0x10
#define  PON_PMK8350_KPDPWR_N_SET	(1U << 7)

struct qcpon_softc {
	struct device		sc_dev;
	int			sc_node;

	spmi_tag_t		sc_tag;
	int8_t			sc_sid;
	uint16_t		sc_addr;

	void			*sc_pwrkey_ih;
	uint32_t		sc_last_sts;
};

int	qcpon_match(struct device *, void *, void *);
void	qcpon_attach(struct device *, struct device *, void *);

int	qcpon_pwrkey_intr(void *);

const struct cfattach qcpon_ca = {
	sizeof(struct qcpon_softc), qcpon_match, qcpon_attach
};

struct cfdriver qcpon_cd = {
	NULL, "qcpon", DV_DULL
};

int
qcpon_match(struct device *parent, void *match, void *aux)
{
	struct spmi_attach_args *saa = aux;

	return (OF_is_compatible(saa->sa_node, "qcom,pm8998-pon") ||
	    OF_is_compatible(saa->sa_node, "qcom,pmk8350-pon"));
}

void
qcpon_attach(struct device *parent, struct device *self, void *aux)
{
	struct spmi_attach_args *saa = aux;
	struct qcpon_softc *sc = (struct qcpon_softc *)self;
	uint32_t reg[2];
	int node;

	if (OF_getpropintarray(saa->sa_node, "reg",
	    reg, sizeof(reg)) != sizeof(reg)) {
		printf(": can't find registers\n");
		return;
	}

	sc->sc_node = saa->sa_node;
	sc->sc_tag = saa->sa_tag;
	sc->sc_sid = saa->sa_sid;
	sc->sc_addr = reg[0];

	printf("\n");

	for (node = OF_child(saa->sa_node); node; node = OF_peer(node)) {
		if (OF_is_compatible(node, "qcom,pmk8350-pwrkey")) {
			sc->sc_pwrkey_ih = fdt_intr_establish(node,
			    IPL_BIO | IPL_WAKEUP, qcpon_pwrkey_intr, sc,
			    sc->sc_dev.dv_xname);
			if (sc->sc_pwrkey_ih == NULL) {
				printf("%s: can't establish interrupt\n",
				    sc->sc_dev.dv_xname);
				continue;
			}
#ifdef SUSPEND
			device_register_wakeup(&sc->sc_dev);
#endif
		}
	}
}

int
qcpon_pwrkey_intr(void *arg)
{
	struct qcpon_softc *sc = arg;
#ifdef SUSPEND
	extern int cpu_suspended;
#endif
	uint32_t sts;
	int error;

#ifdef SUSPEND
	if (cpu_suspended) {
		cpu_suspended = 0;
		return 1;
	}
#endif

	error = spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    sc->sc_addr + PON_RT_STS, &sts, sizeof(sts));
	if (error)
		return 0;

	/* Ignore presses, handle releases. */
	if ((sc->sc_last_sts & PON_PMK8350_KPDPWR_N_SET) &&
	    (sts & PON_PMK8350_KPDPWR_N_SET) == 0)
		powerbutton_event();

	sc->sc_last_sts = sts;
	return 1;
}
