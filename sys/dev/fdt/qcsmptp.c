/*	$OpenBSD: qcsmptp.c,v 1.2 2023/07/04 14:32:21 patrick Exp $	*/
/*
 * Copyright (c) 2023 Patrick Wildt <patrick@blueri.se>
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
#include <sys/atomic.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define SMP2P_MAX_ENTRY		16
#define SMP2P_MAX_ENTRY_NAME	16

struct qcsmptp_smem_item {
	uint32_t magic;
#define SMP2P_MAGIC			0x504d5324
	uint8_t version;
#define SMP2P_VERSION			1
	unsigned features:24;
#define SMP2P_FEATURE_SSR_ACK		(1 << 0)
	uint16_t local_pid;
	uint16_t remote_pid;
	uint16_t total_entries;
	uint16_t valid_entries;
	uint32_t flags;
#define SMP2P_FLAGS_RESTART_DONE	(1 << 0)
#define SMP2P_FLAGS_RESTART_ACK		(1 << 1)

	struct {
		uint8_t name[SMP2P_MAX_ENTRY_NAME];
		uint32_t value;
	} entries[SMP2P_MAX_ENTRY];
} __packed;

struct qcsmptp_intrhand {
	TAILQ_ENTRY(qcsmptp_intrhand) ih_q;
	int (*ih_func)(void *);
	void *ih_arg;
	void *ih_ic;
	int ih_pin;
	int ih_enabled;
};

struct qcsmptp_interrupt_controller {
	TAILQ_HEAD(,qcsmptp_intrhand) ic_intrq;
	struct qcsmptp_softc *ic_sc;
	struct interrupt_controller ic_ic;
};

struct qcsmptp_entry {
	TAILQ_ENTRY(qcsmptp_entry) e_q;
	char *e_name;
	int e_namelen;
	uint32_t *e_value;
	uint32_t e_last_value;
	struct qcsmptp_interrupt_controller *e_ic;
};

struct qcsmptp_softc {
	struct device		sc_dev;
	int			sc_node;
	void			*sc_ih;

	uint16_t		sc_local_pid;
	uint16_t		sc_remote_pid;
	uint32_t		sc_smem_id[2];

	struct qcsmptp_smem_item *sc_in;
	struct qcsmptp_smem_item *sc_out;

	TAILQ_HEAD(,qcsmptp_entry) sc_inboundq;
	TAILQ_HEAD(,qcsmptp_entry) sc_outboundq;

	int			sc_negotiated;
	int			sc_ssr_ack_enabled;
	int			sc_ssr_ack;

	uint16_t		sc_valid_entries;

	struct mbox_channel	*sc_mc;
};

int	qcsmptp_match(struct device *, void *, void *);
void	qcsmptp_attach(struct device *, struct device *, void *);
void	qcsmptp_deferred(struct device *);

const struct cfattach qcsmptp_ca = {
	sizeof (struct qcsmptp_softc), qcsmptp_match, qcsmptp_attach
};

struct cfdriver qcsmptp_cd = {
	NULL, "qcsmptp", DV_DULL
};

int	qcsmptp_intr(void *);
void	*qcsmptp_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	qcsmptp_intr_disestablish(void *);
void	qcsmptp_intr_enable(void *);
void	qcsmptp_intr_disable(void *);
void	qcsmptp_intr_barrier(void *);

extern int qcsmem_alloc(int, int, int);
extern void *qcsmem_get(int, int, int *);

int
qcsmptp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,smp2p");
}

void
qcsmptp_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcsmptp_softc *sc = (struct qcsmptp_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_node = faa->fa_node;

	TAILQ_INIT(&sc->sc_inboundq);
	TAILQ_INIT(&sc->sc_outboundq);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    qcsmptp_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	sc->sc_local_pid = OF_getpropint(faa->fa_node,
	    "qcom,local-pid", 0xffff);
	sc->sc_remote_pid = OF_getpropint(faa->fa_node,
	    "qcom,remote-pid", 0xffff);
	if (sc->sc_local_pid == 0xffff || sc->sc_remote_pid == 0xffff) {
		printf(": can't get pids\n");
		return;
	}

	if (OF_getpropintarray(faa->fa_node, "qcom,smem", sc->sc_smem_id,
	    sizeof(sc->sc_smem_id)) != sizeof(sc->sc_smem_id)) {
		printf(": can't get smem property \n");
		return;
	}

	printf("\n");

	config_defer(self, qcsmptp_deferred);
}

void
qcsmptp_deferred(struct device *self)
{
	struct qcsmptp_softc *sc = (struct qcsmptp_softc *)self;
	struct qcsmptp_entry *e;
	int node, size;

	sc->sc_mc = mbox_channel_idx(sc->sc_node, 0, NULL);
	if (sc->sc_mc == NULL) {
		printf(": can't get mailbox\n");
		return;
	}

	if (qcsmem_alloc(sc->sc_remote_pid, sc->sc_smem_id[0],
	    sizeof(*sc->sc_in)) != 0) {
		printf(": can't alloc smp2p item\n");
		return;
	}

	sc->sc_in = qcsmem_get(sc->sc_remote_pid, sc->sc_smem_id[0], NULL);
	if (sc->sc_in == NULL) {
		printf(": can't get smp2p item\n");
		return;
	}

	if (qcsmem_alloc(sc->sc_remote_pid, sc->sc_smem_id[1],
	    sizeof(*sc->sc_out)) != 0) {
		printf(": can't alloc smp2p item\n");
		return;
	}

	sc->sc_out = qcsmem_get(sc->sc_remote_pid, sc->sc_smem_id[1], NULL);
	if (sc->sc_out == NULL) {
		printf(": can't get smp2p item\n");
		return;
	}

	memset(sc->sc_out, 0, sizeof(*sc->sc_out));
	sc->sc_out->magic = SMP2P_MAGIC;
	sc->sc_out->local_pid = sc->sc_local_pid;
	sc->sc_out->remote_pid = sc->sc_remote_pid;
	sc->sc_out->total_entries = SMP2P_MAX_ENTRY;
	sc->sc_out->features = SMP2P_FEATURE_SSR_ACK;
	membar_sync();
	sc->sc_out->version = SMP2P_VERSION;
	mbox_send(sc->sc_mc, NULL, 0);

	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		size = OF_getproplen(node, "qcom,entry-name");
		if (size <= 0)
			continue;
		e = malloc(sizeof(*e), M_DEVBUF, M_WAITOK | M_ZERO);
		e->e_namelen = size;
		e->e_name = malloc(e->e_namelen, M_DEVBUF, M_WAITOK);
		OF_getprop(node, "qcom,entry-name", e->e_name,
		    e->e_namelen);
		e->e_name[e->e_namelen - 1] = '\0';

		if (OF_getproplen(node, "interrupt-controller") == 0) {
			struct qcsmptp_interrupt_controller *ic;
			ic = malloc(sizeof(*ic), M_DEVBUF, M_WAITOK | M_ZERO);
			TAILQ_INIT(&ic->ic_intrq);
			ic->ic_sc = sc;
			ic->ic_ic.ic_node = node;
			ic->ic_ic.ic_cookie = ic;
			ic->ic_ic.ic_establish = qcsmptp_intr_establish;
			ic->ic_ic.ic_disestablish = qcsmptp_intr_disestablish;
			ic->ic_ic.ic_enable = qcsmptp_intr_enable;
			ic->ic_ic.ic_disable = qcsmptp_intr_disable;
			ic->ic_ic.ic_barrier = qcsmptp_intr_barrier;
			fdt_intr_register(&ic->ic_ic);
			e->e_ic = ic;
			TAILQ_INSERT_TAIL(&sc->sc_inboundq, e, e_q);
		} else {
			if (sc->sc_out->valid_entries >= SMP2P_MAX_ENTRY)
				continue;

			strlcpy(sc->sc_out->entries[sc->sc_out->valid_entries].name,
			    e->e_name, SMP2P_MAX_ENTRY_NAME);
			e->e_value = &sc->sc_out->entries[sc->sc_out->valid_entries].value;
			sc->sc_out->valid_entries++;
			TAILQ_INSERT_TAIL(&sc->sc_outboundq, e, e_q);

			/* TODO: provide as smem state */
		}
	}
}

int
qcsmptp_intr(void *arg)
{
	struct qcsmptp_softc *sc = arg;
	struct qcsmptp_entry *e;
	struct qcsmptp_intrhand *ih;
	uint32_t changed, val;
	int do_ack = 0, i;

	/* Do initial feature negotiation if inbound is new. */
	if (!sc->sc_negotiated) {
		if (sc->sc_in->version != sc->sc_out->version)
			return 1;
		sc->sc_out->features &= sc->sc_in->features;
		if (sc->sc_out->features & SMP2P_FEATURE_SSR_ACK)
			sc->sc_ssr_ack_enabled = 1;
		sc->sc_negotiated = 1;
	}
	if (!sc->sc_negotiated)
		return 1;

	/* Use ACK mechanism if negotiated. */
	if (sc->sc_ssr_ack_enabled &&
	    !!(sc->sc_in->flags & SMP2P_FLAGS_RESTART_DONE) != sc->sc_ssr_ack)
		do_ack = 1;

	/* Catch up on new inbound entries that got added in the meantime. */
	for (i = sc->sc_valid_entries; i < sc->sc_in->valid_entries; i++) {
		TAILQ_FOREACH(e, &sc->sc_inboundq, e_q) {
			if (strncmp(sc->sc_in->entries[i].name, e->e_name,
			    SMP2P_MAX_ENTRY_NAME) != 0)
				continue;
			e->e_value = &sc->sc_in->entries[i].value;
		}
	}
	sc->sc_valid_entries = i;

	/* For each inbound "interrupt controller". */
	TAILQ_FOREACH(e, &sc->sc_inboundq, e_q) {
		if (e->e_value == NULL)
			continue;
		val = *e->e_value;
		if (val == e->e_last_value)
			continue;
		changed = val ^ e->e_last_value;
		e->e_last_value = val;
		TAILQ_FOREACH(ih, &e->e_ic->ic_intrq, ih_q) {
			if (!ih->ih_enabled)
				continue;
			if ((changed & (1 << ih->ih_pin)) == 0)
				continue;
			ih->ih_func(ih->ih_arg);
		}
	}

	if (do_ack) {
		sc->sc_ssr_ack = !sc->sc_ssr_ack;
		if (sc->sc_ssr_ack)
			sc->sc_out->flags |= SMP2P_FLAGS_RESTART_ACK;
		else
			sc->sc_out->flags &= ~SMP2P_FLAGS_RESTART_ACK;
		membar_sync();
		mbox_send(sc->sc_mc, NULL, 0);
	}

	return 1;
}

void *
qcsmptp_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct qcsmptp_interrupt_controller *ic = cookie;
	struct qcsmptp_softc *sc = ic->ic_sc;
	struct qcsmptp_intrhand *ih;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK | M_ZERO);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ic = ic;
	ih->ih_pin = cells[0];
	TAILQ_INSERT_TAIL(&ic->ic_intrq, ih, ih_q);

	qcsmptp_intr_enable(ih);

	if (ipl & IPL_WAKEUP)
		intr_set_wakeup(sc->sc_ih);

	return ih;
}

void
qcsmptp_intr_disestablish(void *cookie)
{
	struct qcsmptp_intrhand *ih = cookie;
	struct qcsmptp_interrupt_controller *ic = ih->ih_ic;

	qcsmptp_intr_disable(ih);

	TAILQ_REMOVE(&ic->ic_intrq, ih, ih_q);
	free(ih, M_DEVBUF, sizeof(*ih));
}

void
qcsmptp_intr_enable(void *cookie)
{
	struct qcsmptp_intrhand *ih = cookie;

	ih->ih_enabled = 1;
}

void
qcsmptp_intr_disable(void *cookie)
{
	struct qcsmptp_intrhand *ih = cookie;

	ih->ih_enabled = 0;
}

void
qcsmptp_intr_barrier(void *cookie)
{
	struct qcsmptp_intrhand *ih = cookie;
	struct qcsmptp_interrupt_controller *ic = ih->ih_ic;
	struct qcsmptp_softc *sc = ic->ic_sc;

	intr_barrier(sc->sc_ih);
}
