/*	$OpenBSD: gsckbc.c,v 1.23 2025/06/28 13:24:21 miod Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF MIND,
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Derived from /sys/dev/ic/pckbd.c under the following terms:
 * OpenBSD: pckbc.c,v 1.5 2002/06/09 00:58:03 nordin Exp
 * NetBSD: pckbc.c,v 1.5 2000/06/09 04:58:35 soda Exp
 */
/*
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
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
 *	This product includes software developed for the NetBSD Project
 *	by Matthias Drochner.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for the PS/2-like keyboard and mouse ports found on 712 and 715
 * models, among others.
 *
 * Contrary to the ``pckbc'' port set found on other arches, the
 * keyboard and mouse port are two separate entities on the snakes, and
 * they are driven by a custom chip not 8042-compatible.
 */

#include "pckbd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/iomod.h>

#include <hppa/dev/cpudevs.h>
#include <hppa/gsc/gscbusvar.h>

#include <hppa/gsc/gsckbcreg.h>
#include <dev/ic/pckbcvar.h>

#include <dev/pckbc/pckbdreg.h>	/* constants for probe magic */
#include <dev/pckbc/pckbdvar.h>

int	gsckbc_match(struct device *, void *, void *);
void	gsckbc_attach(struct device *, struct device *, void *);

struct	gsckbc_softc {
	struct pckbc_softc sc_pckbc;

	void *sc_ih;
	int sc_type;
};

const struct cfattach gsckbc_ca = {
	sizeof(struct gsckbc_softc), gsckbc_match, gsckbc_attach
};

struct cfdriver gsckbc_cd = {
	NULL, "gsckbc", DV_DULL
};

/* descriptor for one device command */
struct pckbc_devcmd {
	TAILQ_ENTRY(pckbc_devcmd) next;
	int flags;
#define KBC_CMDFLAG_SYNC 1 /* give descriptor back to caller */
#define KBC_CMDFLAG_SLOW 2
	u_char cmd[4];
	int cmdlen, cmdidx, retries;
	u_char response[4];
	int status, responselen, responseidx;
};

/* data per slave device */
struct pckbc_slotdata {
	int polling; /* don't read data port in interrupt handler */
	TAILQ_HEAD(, pckbc_devcmd) cmdqueue; /* active commands */
	TAILQ_HEAD(, pckbc_devcmd) freequeue; /* free commands */
#define NCMD 5
	struct pckbc_devcmd cmds[NCMD];
};

#define CMD_IN_QUEUE(q) (TAILQ_FIRST(&(q)->cmdqueue) != NULL)
/* Force polling mode behaviour for boot -a XXX */
#define IS_POLLING(q)	((q)->polling || cold)

void pckbc_init_slotdata(struct pckbc_slotdata *);
int pckbc_attach_slot(struct pckbc_softc *, pckbc_slot_t);
int pckbc_submatch(struct device *, void *, void *);
int pckbcprint(void *, const char *);

int pckbc_wait_output(bus_space_tag_t, bus_space_handle_t);
int pckbc_send_devcmd(struct pckbc_internal *, pckbc_slot_t,
				  u_char);
void pckbc_poll_cmd1(struct pckbc_internal *, pckbc_slot_t,
				 struct pckbc_devcmd *);

void pckbc_cleanqueue(struct pckbc_slotdata *);
void pckbc_cleanup(void *);
int pckbc_cmdresponse(struct pckbc_internal *, pckbc_slot_t, u_char);
void pckbc_start(struct pckbc_internal *, pckbc_slot_t);
int gsckbcintr(void *);

const char *pckbc_slot_names[] = { "kbd", "mouse" };

#define KBC_DEVCMD_ACK 0xfa
#define KBC_DEVCMD_RESEND 0xfe

#define	KBD_DELAY	DELAY(8)

int
gsckbc_match(struct device *parent, void *match, void *aux)
{
	struct gsc_attach_args *ga = aux;
	bus_space_handle_t ioh;
	u_int8_t rv;

	if (ga->ga_type.iodc_type != HPPA_TYPE_FIO ||
	    ga->ga_type.iodc_sv_model != HPPA_FIO_GPCIO)
		return (0);

	/* Map the i/o space. */
	if (bus_space_map(ga->ga_ca.ca_iot, ga->ga_ca.ca_hpa,
	    KBMAPSIZE, 0, &ioh))
		return 0;

	rv = bus_space_read_1(ga->ga_ca.ca_iot, ioh, KBIDP);
	bus_space_unmap(ga->ga_ca.ca_iot, ioh, KBMAPSIZE);

	if (rv == PCKBC_KBD_SLOT || rv == PCKBC_AUX_SLOT)
		return (1);	/* keyboard or mouse port */

	return (0);
}

/*
 * Attachment helper functions
 */

/* state machine values */
#define	PROBE_SUCCESS	0
#define	PROBE_TIMEOUT	1
#define	PROBE_RETRANS	2
#define	PROBE_NOACK	3

int probe_readtmo(bus_space_tag_t iot, bus_space_handle_t ioh, int *reply);
int probe_readretry(bus_space_tag_t iot, bus_space_handle_t ioh, int *reply);
int probe_sendtmo(bus_space_tag_t iot, bus_space_handle_t ioh, int cmdbyte);
int probe_sendack(bus_space_tag_t iot, bus_space_handle_t ioh, int cmdbyte);
int probe_ident(bus_space_tag_t iot, bus_space_handle_t ioh);

#define	PROBE_TRIES	1000

int
probe_readtmo(bus_space_tag_t iot, bus_space_handle_t ioh, int *reply)
{
	int numtries = PROBE_TRIES;

	while (numtries--) {
		if (bus_space_read_1(iot, ioh, KBSTATP) &
		    (KBS_DIB | KBS_TERR | KBS_PERR))
			break;
		DELAY(500);
	}

	if (numtries <= 0)
		return (PROBE_TIMEOUT);

	if (bus_space_read_1(iot, ioh, KBSTATP) & (KBS_PERR | KBS_TERR)) {
		if (!(bus_space_read_1(iot, ioh, KBSTATP) & KBS_DIB)) {
			bus_space_write_1(iot, ioh, KBRESETP, 0xff);
			bus_space_write_1(iot, ioh, KBRESETP, 0x00);
			bus_space_write_1(iot, ioh, KBCMDP,
			    bus_space_read_1(iot, ioh, KBCMDP) | KBCP_ENABLE);
			return (PROBE_TIMEOUT);
		}

		*reply = bus_space_read_1(iot, ioh, KBDATAP);
		if (!(bus_space_read_1(iot, ioh, KBSTATP) & KBS_DIB)) {
			bus_space_write_1(iot, ioh, KBRESETP, 0xff);
			bus_space_write_1(iot, ioh, KBRESETP, 0x00);
			bus_space_write_1(iot, ioh, KBCMDP,
			    bus_space_read_1(iot, ioh, KBCMDP) | KBCP_ENABLE);
			if (probe_sendtmo(iot, ioh, KBR_RESEND))
				return (PROBE_TIMEOUT);
			else
				return (PROBE_RETRANS);
		} else
			return (PROBE_SUCCESS);
	} else {
		*reply = bus_space_read_1(iot, ioh, KBDATAP);
		return (PROBE_SUCCESS);
	}
}

int
probe_readretry(bus_space_tag_t iot, bus_space_handle_t ioh, int *reply)
{
	int read_status;
	int retrans = KB_MAX_RETRANS;

	do {
		read_status = probe_readtmo(iot, ioh, reply);
	} while ((read_status == PROBE_RETRANS) && retrans--);

	return (read_status);
}

int
probe_sendtmo(bus_space_tag_t iot, bus_space_handle_t ioh, int cmdbyte)
{
	int numtries = PROBE_TRIES;

	while (numtries--) {
		if ((bus_space_read_1(iot, ioh, KBSTATP) & KBS_OCMD) == 0)
			break;
		DELAY(500);
	}

	if (numtries <= 0)
		return (1);

	bus_space_write_1(iot, ioh, KBDATAP, cmdbyte);
	bus_space_write_1(iot, ioh, KBCMDP, KBCP_ENABLE);
	return (0);
}

int
probe_sendack(bus_space_tag_t iot, bus_space_handle_t ioh, int cmdbyte)
{
	int retranscount;
	int reply;

	for (retranscount = 0; retranscount < KB_MAX_RETRANS; retranscount++) {
		if (probe_sendtmo(iot, ioh, cmdbyte))
			return (PROBE_TIMEOUT);
		if (probe_readretry(iot, ioh, &reply))
			return (PROBE_TIMEOUT);

		switch (reply) {
		case KBR_ACK:
			return (PROBE_SUCCESS);
		case KBR_RESEND:
			break;
		default:
			return (PROBE_NOACK);
		}
	}
	return (PROBE_TIMEOUT);
	
}

int
probe_ident(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int status;

	bus_space_write_1(iot, ioh, KBRESETP, 0);
	bus_space_write_1(iot, ioh, KBCMDP, KBCP_ENABLE);
	DELAY(0x20000);	/* XXX why 0x? */
	bus_space_write_1(iot, ioh, KBCMDP, 0);
	DELAY(20000);
	bus_space_write_1(iot, ioh, KBRESETP, 0);
	bus_space_write_1(iot, ioh, KBCMDP, KBCP_DIAG);
	DELAY(20000);

	status = probe_sendack(iot, ioh, KBC_DISABLE);
	switch (status) {
	case PROBE_TIMEOUT:
		if (bus_space_read_1(iot, ioh, KBSTATP) & KBS_OCMD) {
			bus_space_write_1(iot, ioh, KBRESETP, 0);
			bus_space_write_1(iot, ioh, KBCMDP, KBCP_ENABLE);
		}
		return (-1);
	case PROBE_NOACK:
		return (-1);
	}

	if (probe_sendack(iot, ioh, KBC_ID) != PROBE_SUCCESS)
		return (-1);

	if (probe_readretry(iot, ioh, &status))
		return (-1);

	switch (status) {
	case KBR_MOUSE_ID:
		return PCKBC_AUX_SLOT;
	case KBR_KBD_ID1:
		if (probe_readretry(iot, ioh, &status))
			return (-1);
		if (status == KBR_KBD_ID2) {
			if (probe_sendack(iot, ioh, KBC_ENABLE) ==
			    PROBE_TIMEOUT) {
				bus_space_write_1(iot, ioh, KBRESETP, 0);
				bus_space_write_1(iot, ioh, KBCMDP,
				    KBCP_ENABLE);
			}
			return PCKBC_KBD_SLOT;
		}
	}
	return (-1);
}

void
gsckbc_attach(struct device *parent, struct device *self, void *aux)
{
	struct gsc_attach_args *ga = aux;
	struct gsckbc_softc *gsc = (void *)self;
	struct pckbc_softc *sc = &gsc->sc_pckbc;
	struct pckbc_internal *t;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	int ident;

	iot = ga->ga_ca.ca_iot;

	if (bus_space_map(iot, ga->ga_ca.ca_hpa, KBMAPSIZE, 0, &ioh))
		panic("gsckbc_attach: couldn't map port");

	gsc->sc_type = bus_space_read_1(iot, ioh, KBIDP);

	switch (gsc->sc_type) {
	case PCKBC_KBD_SLOT:
	case PCKBC_AUX_SLOT:
		break;
	default:
		printf(": unknown port type %x\n", gsc->sc_type);
		/* play nice and don't really attach. */
		bus_space_unmap(iot, ioh, KBMAPSIZE);
		return;
	}

	gsc->sc_ih = gsc_intr_establish((struct gsc_softc *)parent,
	    ga->ga_ca.ca_irq, IPL_TTY, gsckbcintr, sc, sc->sc_dv.dv_xname);
	if (gsc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		bus_space_unmap(iot, ioh, KBMAPSIZE);
		return;
	}

	printf("\n");

	t = malloc(sizeof(*t), M_DEVBUF, M_WAITOK | M_ZERO);
	t->t_iot = iot;
	/* XXX it does not make sense to only map two ports here */
	t->t_ioh_d = t->t_ioh_c = ioh;
	t->t_addr = ga->ga_ca.ca_hpa;
	t->t_sc = sc;
	timeout_set(&t->t_cleanup, pckbc_cleanup, t);
	sc->id = t;

	/*
	 * Reset port and probe device, if plugged
	 */
	ident = probe_ident(iot, ioh);
	if (ident != gsc->sc_type) {
		/* don't whine for unplugged ports */
		if (ident != -1)
			printf("%s: expecting device type %d, got %d\n",
			    sc->sc_dv.dv_xname, gsc->sc_type, ident);
	} else {
#if (NPCKBD > 0)
		if (gsc->sc_type == PCKBC_KBD_SLOT &&
		    ga->ga_dp.dp_mod == PAGE0->mem_kbd.pz_dp.dp_mod &&
		    bcmp(ga->ga_dp.dp_bc, PAGE0->mem_kbd.pz_dp.dp_bc, 6) == 0)
			pckbd_cnattach(t);
#endif
		pckbc_attach_slot(sc, gsc->sc_type);
	}
}

/*
 * pckbc-like interfaces
 */

int
pckbc_wait_output(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int i;

	for (i = 100000; i; i--) {
		if ((bus_space_read_1(iot, ioh, KBSTATP) & KBS_OCMD)) {
			KBD_DELAY;
		} else
			return (1);
	}
	return (0);
}

int
pckbc_send_cmd(bus_space_tag_t iot, bus_space_handle_t ioh, u_char val)
{
	if (!pckbc_wait_output(iot, ioh))
		return (0);
	bus_space_write_1(iot, ioh, KBOUTP, val);
	bus_space_write_1(iot, ioh, KBCMDP, KBCP_ENABLE);
	return (1);
}

/* XXX logic */
int
pckbc_poll_data1(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_space_handle_t ioh_c, pckbc_slot_t slot, int checkaux)
    /* checkaux ignored on hppa */
{
	int i;
	u_char stat;

	/* if 1 port read takes 1us (?), this polls for 100ms */
	for (i = 100000; i; i--) {
		stat = bus_space_read_1(iot, ioh, KBSTATP);
		if (stat & KBS_DIB) {
			KBD_DELAY;
			return bus_space_read_1(iot, ioh, KBDATAP);
		}
	}
	return (-1);
}

int
pckbc_send_devcmd(struct pckbc_internal *t, pckbc_slot_t slot, u_char val)
{
	return pckbc_send_cmd(t->t_iot, t->t_ioh_d, val);
}

int
pckbc_submatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct pckbc_attach_args *pa = aux;

	if (cf->cf_loc[PCKBCCF_SLOT] != PCKBCCF_SLOT_DEFAULT &&
	    cf->cf_loc[PCKBCCF_SLOT] != pa->pa_slot)
		return (0);
	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

int
pckbc_attach_slot(struct pckbc_softc *sc, pckbc_slot_t slot)
{
	struct pckbc_internal *t = sc->id;
	struct pckbc_attach_args pa;
	int found;

	pa.pa_tag = t;
	pa.pa_slot = slot;
	found = (config_found_sm((struct device *)sc, &pa,
				 pckbcprint, pckbc_submatch) != NULL);

	if (found && !t->t_slotdata[slot]) {
		t->t_slotdata[slot] = malloc(sizeof(struct pckbc_slotdata),
					     M_DEVBUF, M_NOWAIT);
		if (t->t_slotdata[slot] == NULL)
			return 0;
		pckbc_init_slotdata(t->t_slotdata[slot]);
	}
	return (found);
}

int
pckbcprint(void *aux, const char *pnp)
{
#if 0	/* hppa having devices for each slot, this is barely useful */
	struct pckbc_attach_args *pa = aux;

	if (!pnp)
		printf(" (%s slot)", pckbc_slot_names[pa->pa_slot]);
#endif
	return (QUIET);
}

void
pckbc_init_slotdata(struct pckbc_slotdata *q)
{
	int i;
	TAILQ_INIT(&q->cmdqueue);
	TAILQ_INIT(&q->freequeue);

	for (i = 0; i < NCMD; i++) {
		TAILQ_INSERT_TAIL(&q->freequeue, &(q->cmds[i]), next);
	}
	q->polling = 0;
}

void
pckbc_flush(pckbc_tag_t self, pckbc_slot_t slot)
{
	struct pckbc_internal *t = self;

	pckbc_poll_data1(t->t_iot, t->t_ioh_d, t->t_ioh_d, slot, 0);
}

int
pckbc_poll_data(pckbc_tag_t self, pckbc_slot_t slot)
{
	struct pckbc_internal *t = self;
	struct pckbc_slotdata *q = t->t_slotdata[slot];
	int c;

	c = pckbc_poll_data1(t->t_iot, t->t_ioh_d, t->t_ioh_d, slot, 0);
	if (c != -1 && q && CMD_IN_QUEUE(q)) {
		/* we jumped into a running command - try to
		 deliver the response */
		if (pckbc_cmdresponse(t, slot, c))
			return (-1);
	}
	return (c);
}

int
pckbc_xt_translation(pckbc_tag_t self, int *table)
{
	/* Translation isn't supported... */
	return (-1);
}

void
pckbc_slot_enable(pckbc_tag_t self, pckbc_slot_t slot, int on)
{
	/* can't enable slots here as they are different devices */
}

void
pckbc_set_poll(pckbc_tag_t self, pckbc_slot_t slot, int on)
{
	struct pckbc_internal *t = (struct pckbc_internal *)self;

	t->t_slotdata[slot]->polling = on;

	if (!on) {
                int s;

                /*
                 * If disabling polling on a device that's been configured,
                 * make sure there are no bytes left in the FIFO, holding up
                 * the interrupt line.  Otherwise we won't get any further
                 * interrupts.
                 */
		if (t->t_sc) {
			s = spltty();
			gsckbcintr(t->t_sc);
			splx(s);
		}
	}
}

/*
 * Pass command to device, poll for ACK and data.
 * to be called at spltty()
 */
void
pckbc_poll_cmd1(struct pckbc_internal *t, pckbc_slot_t slot,
    struct pckbc_devcmd *cmd)
{
	bus_space_tag_t iot = t->t_iot;
	bus_space_handle_t ioh = t->t_ioh_d;
	int i, c = 0;

	while (cmd->cmdidx < cmd->cmdlen) {
		if (!pckbc_send_devcmd(t, slot, cmd->cmd[cmd->cmdidx])) {
			printf("pckbc_cmd: send error\n");
			cmd->status = EIO;
			return;
		}
		for (i = 10; i; i--) { /* 1s ??? */
			c = pckbc_poll_data1(iot, ioh, ioh, slot, 0);
			if (c != -1)
				break;
		}

		if (c == KBC_DEVCMD_ACK) {
			cmd->cmdidx++;
			continue;
		}
		if (c == KBC_DEVCMD_RESEND) {
#ifdef PCKBCDEBUG
			printf("pckbc_cmd: RESEND\n");
#endif
			if (cmd->retries++ < KB_MAX_RETRANS)
				continue;
			else {
#ifdef PCKBCDEBUG
				printf("pckbc: cmd failed\n");
#endif
				cmd->status = EIO;
				return;
			}
		}
		if (c == -1) {
#ifdef PCKBCDEBUG
			printf("pckbc_cmd: timeout\n");
#endif
			cmd->status = EIO;
			return;
		}
#ifdef PCKBCDEBUG
		printf("pckbc_cmd: lost 0x%x\n", c);
#endif
	}

	while (cmd->responseidx < cmd->responselen) {
		if (cmd->flags & KBC_CMDFLAG_SLOW)
			i = 100; /* 10s ??? */
		else
			i = 10; /* 1s ??? */
		while (i--) {
			c = pckbc_poll_data1(iot, ioh, ioh, slot, 0);
			if (c != -1)
				break;
		}
		if (c == -1) {
#ifdef PCKBCDEBUG
			printf("pckbc_cmd: no data\n");
#endif
			cmd->status = ETIMEDOUT;
			return;
		} else
			cmd->response[cmd->responseidx++] = c;
	}
}

/* for use in autoconfiguration */
int
pckbc_poll_cmd(pckbc_tag_t self, pckbc_slot_t slot, u_char *cmd, int len,
    int responselen, u_char *respbuf, int slow)
{
	struct pckbc_devcmd nc;

	if ((len > 4) || (responselen > 4))
		return (EINVAL);

	bzero(&nc, sizeof(nc));
	bcopy(cmd, nc.cmd, len);
	nc.cmdlen = len;
	nc.responselen = responselen;
	nc.flags = (slow ? KBC_CMDFLAG_SLOW : 0);

	pckbc_poll_cmd1(self, slot, &nc);

	if (nc.status == 0 && respbuf)
		bcopy(nc.response, respbuf, responselen);

	return (nc.status);
}

/*
 * Clean up a command queue, throw away everything.
 */
void
pckbc_cleanqueue(struct pckbc_slotdata *q)
{
	struct pckbc_devcmd *cmd;
#ifdef PCKBCDEBUG
	int i;
#endif

	while ((cmd = TAILQ_FIRST(&q->cmdqueue))) {
		TAILQ_REMOVE(&q->cmdqueue, cmd, next);
#ifdef PCKBCDEBUG
		printf("pckbc_cleanqueue: removing");
		for (i = 0; i < cmd->cmdlen; i++)
			printf(" %02x", cmd->cmd[i]);
		printf("\n");
#endif
		TAILQ_INSERT_TAIL(&q->freequeue, cmd, next);
	}
}

/*
 * Timeout error handler: clean queues and data port.
 * XXX could be less invasive.
 */
void
pckbc_cleanup(void *self)
{
	struct pckbc_internal *t = self;
	int s;

	printf("pckbc: command timeout\n");

	s = spltty();

	if (t->t_slotdata[PCKBC_KBD_SLOT])
		pckbc_cleanqueue(t->t_slotdata[PCKBC_KBD_SLOT]);
	if (t->t_slotdata[PCKBC_AUX_SLOT])
		pckbc_cleanqueue(t->t_slotdata[PCKBC_AUX_SLOT]);

	while (bus_space_read_1(t->t_iot, t->t_ioh_d, KBSTATP) & KBS_DIB) {
		KBD_DELAY;
		(void) bus_space_read_1(t->t_iot, t->t_ioh_d, KBDATAP);
	}

	/* reset KBC? */

	splx(s);
}

/*
 * Pass command to device during normal operation.
 * to be called at spltty()
 */
void
pckbc_start(struct pckbc_internal *t, pckbc_slot_t slot)
{
	struct pckbc_slotdata *q = t->t_slotdata[slot];
	struct pckbc_devcmd *cmd = TAILQ_FIRST(&q->cmdqueue);

	if (IS_POLLING(q)) {
		do {
			pckbc_poll_cmd1(t, slot, cmd);
			if (cmd->status)
				printf("pckbc_start: command error\n");

			TAILQ_REMOVE(&q->cmdqueue, cmd, next);
			if (cmd->flags & KBC_CMDFLAG_SYNC)
				wakeup(cmd);
			else {
				timeout_del(&t->t_cleanup);
				TAILQ_INSERT_TAIL(&q->freequeue, cmd, next);
			}
			cmd = TAILQ_FIRST(&q->cmdqueue);
		} while (cmd);
		return;
	}

	if (!pckbc_send_devcmd(t, slot, cmd->cmd[cmd->cmdidx])) {
		printf("pckbc_start: send error\n");
		/* XXX what now? */
		return;
	}
}

/*
 * Handle command responses coming in asynchronously,
 * return nonzero if valid response.
 * to be called at spltty()
 */
int
pckbc_cmdresponse(struct pckbc_internal *t, pckbc_slot_t slot, u_char data)
{
	struct pckbc_slotdata *q = t->t_slotdata[slot];
	struct pckbc_devcmd *cmd = TAILQ_FIRST(&q->cmdqueue);

#ifdef DIAGNOSTIC
	if (!cmd)
		panic("pckbc_cmdresponse: no active command");
#endif
	if (cmd->cmdidx < cmd->cmdlen) {
		if (data != KBC_DEVCMD_ACK && data != KBC_DEVCMD_RESEND)
			return (0);

		if (data == KBC_DEVCMD_RESEND) {
			if (cmd->retries++ < KB_MAX_RETRANS) {
				/* try again last command */
				goto restart;
			} else {
#ifdef PCKBCDEBUG
				printf("pckbc: cmd failed\n");
#endif
				cmd->status = EIO;
				/* dequeue */
			}
		} else {
			if (++cmd->cmdidx < cmd->cmdlen)
				goto restart;
			if (cmd->responselen)
				return (1);
			/* else dequeue */
		}
	} else if (cmd->responseidx < cmd->responselen) {
		cmd->response[cmd->responseidx++] = data;
		if (cmd->responseidx < cmd->responselen)
			return (1);
		/* else dequeue */
	} else
		return (0);

	/* dequeue: */
	TAILQ_REMOVE(&q->cmdqueue, cmd, next);
	if (cmd->flags & KBC_CMDFLAG_SYNC)
		wakeup(cmd);
	else {
		timeout_del(&t->t_cleanup);
		TAILQ_INSERT_TAIL(&q->freequeue, cmd, next);
	}
	if (!CMD_IN_QUEUE(q))
		return (1);
restart:
	pckbc_start(t, slot);
	return (1);
}

/*
 * Put command into the device's command queue, return zero or errno.
 */
int
pckbc_enqueue_cmd(pckbc_tag_t self, pckbc_slot_t slot, u_char *cmd, int len,
    int responselen, int sync, u_char *respbuf)
{
	struct pckbc_internal *t = self;
	struct pckbc_slotdata *q = t->t_slotdata[slot];
	struct pckbc_devcmd *nc;
	int s, isactive, res = 0;

	if ((len > 4) || (responselen > 4))
		return (EINVAL);
	s = spltty();
	nc = TAILQ_FIRST(&q->freequeue);
	if (nc) {
		TAILQ_REMOVE(&q->freequeue, nc, next);
	}
	splx(s);
	if (!nc)
		return (ENOMEM);

	bzero(nc, sizeof(*nc));
	bcopy(cmd, nc->cmd, len);
	nc->cmdlen = len;
	nc->responselen = responselen;
	nc->flags = (sync ? KBC_CMDFLAG_SYNC : 0);

	s = spltty();

	if (IS_POLLING(q) && sync) {
		/*
		 * XXX We should poll until the queue is empty.
		 * But we don't come here normally, so make
		 * it simple and throw away everything.
		 */
		pckbc_cleanqueue(q);
	}

	isactive = CMD_IN_QUEUE(q);
	TAILQ_INSERT_TAIL(&q->cmdqueue, nc, next);
	if (!isactive)
		pckbc_start(t, slot);

	if (IS_POLLING(q))
		res = (sync ? nc->status : 0);
	else if (sync) {
		if ((res = tsleep_nsec(nc, 0, "kbccmd", SEC_TO_NSEC(1)))) {
			TAILQ_REMOVE(&q->cmdqueue, nc, next);
			pckbc_cleanup(t);
		} else
			res = nc->status;
	} else
		timeout_add_sec(&t->t_cleanup, 1);

	if (sync) {
		if (respbuf)
			bcopy(nc->response, respbuf, responselen);
		TAILQ_INSERT_TAIL(&q->freequeue, nc, next);
	}

	splx(s);

	return (res);
}

void
pckbc_set_inputhandler(pckbc_tag_t self, pckbc_slot_t slot, pckbc_inputfcn func,
    void *arg, char *name)
{
	struct pckbc_internal *t = (struct pckbc_internal *)self;
	struct pckbc_softc *sc = t->t_sc;

	if (slot >= PCKBC_NSLOTS)
		panic("pckbc_set_inputhandler: bad slot %d", slot);

	sc->inputhandler[slot] = func;
	sc->inputarg[slot] = arg;
	sc->subname[slot] = name;
}

int
gsckbcintr(void *v)
{
	struct gsckbc_softc *gsc = v;
	struct pckbc_softc *sc = (struct pckbc_softc *)gsc;
	struct pckbc_internal *t = sc->id;
	pckbc_slot_t slot;
	struct pckbc_slotdata *q;
	int served = 0, data;

	while (bus_space_read_1(t->t_iot, t->t_ioh_d, KBSTATP) & KBS_DIB) {
		served = 1;

		slot = gsc->sc_type;
		q = t->t_slotdata[slot];

		if (!q) {
			/* XXX do something for live insertion? */
#ifdef PCKBCDEBUG
			printf("gsckbcintr: no dev for slot %d\n", slot);
#endif
			KBD_DELAY;
			(void) bus_space_read_1(t->t_iot, t->t_ioh_d, KBDATAP);
			continue;
		}

		if (IS_POLLING(q))
			break; /* pckbc_poll_data() will get it */

		KBD_DELAY;
		data = bus_space_read_1(t->t_iot, t->t_ioh_d, KBDATAP);

		if (CMD_IN_QUEUE(q) && pckbc_cmdresponse(t, slot, data))
			continue;

		if (sc->inputhandler[slot])
			(*sc->inputhandler[slot])(sc->inputarg[slot], data);
#ifdef PCKBCDEBUG
		else
			printf("gsckbcintr: slot %d lost %d\n", slot, data);
#endif
	}

	return (served);
}
