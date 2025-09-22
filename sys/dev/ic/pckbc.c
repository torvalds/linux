/* $OpenBSD: pckbc.c,v 1.55 2023/08/26 15:01:00 jmc Exp $ */
/* $NetBSD: pckbc.c,v 1.5 2000/06/09 04:58:35 soda Exp $ */

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/queue.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>

#include "pckbd.h"

#if NPCKBD > 0
#include <dev/pckbc/pckbdvar.h>
#endif

#ifdef PCKBCDEBUG
#define DPRINTF(x...)	do { printf(x); } while (0);
#else
#define DPRINTF(x...)
#endif

/* descriptor for one device command */
struct pckbc_devcmd {
	TAILQ_ENTRY(pckbc_devcmd) next;
	int flags;
#define KBC_CMDFLAG_SYNC 1 /* give descriptor back to caller */
#define KBC_CMDFLAG_SLOW 2
#define KBC_CMDFLAG_QUEUED 4 /* descriptor on cmdqueue */
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

#define CMD_IN_QUEUE(q) (!TAILQ_EMPTY(&(q)->cmdqueue))

void pckbc_init_slotdata(struct pckbc_slotdata *);
int pckbc_attach_slot(struct pckbc_softc *, pckbc_slot_t, int);
int pckbc_submatch_locators(struct device *, void *, void *);
int pckbc_submatch(struct device *, void *, void *);
int pckbcprint(void *, const char *);

struct pckbc_internal pckbc_consdata;
int pckbc_console_attached;

int pckbc_console;
static struct pckbc_slotdata pckbc_cons_slotdata;

static int pckbc_wait_output(bus_space_tag_t, bus_space_handle_t);

static int pckbc_get8042cmd(struct pckbc_internal *);
static int pckbc_put8042cmd(struct pckbc_internal *);
static int pckbc_send_devcmd(struct pckbc_internal *, pckbc_slot_t,
				  u_char);
static void pckbc_poll_cmd1(struct pckbc_internal *, pckbc_slot_t,
				 struct pckbc_devcmd *);

void pckbc_cleanqueues(struct pckbc_internal *);
void pckbc_cleanqueue(struct pckbc_slotdata *);
void pckbc_cleanup(void *);
void pckbc_poll(void *);
int pckbc_cmdresponse(struct pckbc_internal *, pckbc_slot_t, u_char);
void pckbc_start(struct pckbc_internal *, pckbc_slot_t);
int pckbcintr_internal(struct pckbc_internal *, struct pckbc_softc *);

const char *pckbc_slot_names[] = { "kbd", "aux" };

#define KBC_DEVCMD_ACK		0xfa
#define KBC_DEVCMD_RESEND	0xfe
#define KBC_DEVCMD_BAT_DONE	0xaa
#define KBC_DEVCMD_BAT_FAIL	0xfc

#define	KBD_DELAY	DELAY(8)

static inline int
pckbc_wait_output(bus_space_tag_t iot, bus_space_handle_t ioh_c)
{
	u_int i;

	for (i = 100000; i; i--)
		if (!(bus_space_read_1(iot, ioh_c, 0) & KBS_IBF)) {
			KBD_DELAY;
			return (1);
		}
	return (0);
}

int
pckbc_send_cmd(bus_space_tag_t iot, bus_space_handle_t ioh_c, u_char val)
{
	if (!pckbc_wait_output(iot, ioh_c))
		return (0);
	bus_space_write_1(iot, ioh_c, 0, val);
	return (1);
}

int
pckbc_poll_data1(bus_space_tag_t iot, bus_space_handle_t ioh_d,
    bus_space_handle_t ioh_c, pckbc_slot_t slot, int checkaux)
{
	int i;
	u_char stat;

	/* polls for ~100ms */
	for (i = 100; i; i--, delay(1000)) {
		stat = bus_space_read_1(iot, ioh_c, 0);
		if (stat & KBS_DIB) {
			register u_char c;

			KBD_DELAY;
			CPU_BUSY_CYCLE();
			c = bus_space_read_1(iot, ioh_d, 0);
			if (checkaux && (stat & KBS_AUXDATA)) {
				if (slot != PCKBC_AUX_SLOT) {
					DPRINTF("lost aux 0x%x\n", c);
					continue;
				}
			} else {
				if (slot == PCKBC_AUX_SLOT) {
					DPRINTF("lost kbd 0x%x\n", c);
					continue;
				} else if (stat & KBS_AUXDATA) {
					DPRINTF("discard aux data 0x%x\n", c);
					continue;
				}
			}
			return (c);
		}
	}
	return (-1);
}

/*
 * Get the current command byte.
 */
static int
pckbc_get8042cmd(struct pckbc_internal *t)
{
	bus_space_tag_t iot = t->t_iot;
	bus_space_handle_t ioh_d = t->t_ioh_d;
	bus_space_handle_t ioh_c = t->t_ioh_c;
	int data;

	if (!pckbc_send_cmd(iot, ioh_c, K_RDCMDBYTE))
		return (0);
	data = pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT,
				t->t_haveaux);
	if (data == -1)
		return (0);
	t->t_cmdbyte = data;
	return (1);
}

/*
 * Pass command byte to keyboard controller (8042).
 */
static int
pckbc_put8042cmd(struct pckbc_internal *t)
{
	bus_space_tag_t iot = t->t_iot;
	bus_space_handle_t ioh_d = t->t_ioh_d;
	bus_space_handle_t ioh_c = t->t_ioh_c;

	if (!pckbc_send_cmd(iot, ioh_c, K_LDCMDBYTE))
		return (0);
	if (!pckbc_wait_output(iot, ioh_c))
		return (0);
	bus_space_write_1(iot, ioh_d, 0, t->t_cmdbyte);
	return (1);
}

static int
pckbc_send_devcmd(struct pckbc_internal *t, pckbc_slot_t slot, u_char val)
{
	bus_space_tag_t iot = t->t_iot;
	bus_space_handle_t ioh_d = t->t_ioh_d;
	bus_space_handle_t ioh_c = t->t_ioh_c;

	if (slot == PCKBC_AUX_SLOT) {
		if (!pckbc_send_cmd(iot, ioh_c, KBC_AUXWRITE))
			return (0);
	}
	if (!pckbc_wait_output(iot, ioh_c))
		return (0);
	bus_space_write_1(iot, ioh_d, 0, val);
	return (1);
}

int
pckbc_is_console(bus_space_tag_t iot, bus_addr_t addr)
{
	if (pckbc_console && !pckbc_console_attached &&
	    pckbc_consdata.t_iot == iot &&
	    pckbc_consdata.t_addr == addr)
		return (1);
	return (0);
}

int
pckbc_submatch_locators(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct pckbc_attach_args *pa = aux;

	if (cf->cf_loc[PCKBCCF_SLOT] != PCKBCCF_SLOT_DEFAULT &&
	    cf->cf_loc[PCKBCCF_SLOT] != pa->pa_slot)
		return (0);
	return (1);
}

int
pckbc_submatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;

	if (pckbc_submatch_locators(parent, match, aux) == 0)
		return (0);
	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

int
pckbc_attach_slot(struct pckbc_softc *sc, pckbc_slot_t slot, int force)
{
	struct pckbc_internal *t = sc->id;
	struct pckbc_attach_args pa;
	int found;

	pa.pa_tag = t;
	pa.pa_slot = slot;
	found = (config_found_sm((struct device *)sc, &pa, pckbcprint,
	    force ? pckbc_submatch_locators : pckbc_submatch) != NULL);

	if ((found || slot == PCKBC_AUX_SLOT) && !t->t_slotdata[slot]) {
		t->t_slotdata[slot] = malloc(sizeof(struct pckbc_slotdata),
					     M_DEVBUF, M_NOWAIT);
		if (t->t_slotdata[slot] == NULL)
			return 0;
		pckbc_init_slotdata(t->t_slotdata[slot]);

		if (!found && slot == PCKBC_AUX_SLOT) {
			/*
			 * Some machines don't handle disabling the aux slot
			 * completely and still generate data when the mouse is
			 * moved, so setup a dummy interrupt handler to discard
			 * this slot's data.
			 */
			pckbc_set_inputhandler(t, PCKBC_AUX_SLOT, NULL, sc,
			    NULL);
			found = 1;
		}
	}
	return (found);
}

void
pckbc_attach(struct pckbc_softc *sc, int flags)
{
	struct pckbc_internal *t;
	bus_space_tag_t iot;
	bus_space_handle_t ioh_d, ioh_c;
	int haskbd = 0, res;
	u_char cmdbits = 0;

	t = sc->id;
	iot = t->t_iot;
	ioh_d = t->t_ioh_d;
	ioh_c = t->t_ioh_c;

	if (pckbc_console == 0) {
		timeout_set(&t->t_cleanup, pckbc_cleanup, t);
		timeout_set(&t->t_poll, pckbc_poll, t);
	}

	/* flush */
	(void) pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT, 0);

	/* set initial cmd byte */
	if (!pckbc_put8042cmd(t)) {
#if defined(__i386__) || defined(__amd64__)
		if (!ISSET(flags, PCKBCF_FORCE_KEYBOARD_PRESENT)) {
			pckbc_release_console();
			return;
		}
#endif
		printf("kbc: cmd word write error\n");
		return;
	}

/*
 * XXX Don't check the keyboard port. There are broken keyboard controllers
 * which don't pass the test but work normally otherwise.
 */
#if 0
	/*
	 * check kbd port ok
	 */
	if (!pckbc_send_cmd(iot, ioh_c, KBC_KBDTEST))
		return;
	res = pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT, 0);

	/*
	 * Normally, we should get a "0" here.
	 * But there are keyboard controllers behaving differently.
	 */
	if (res == 0 || res == 0xfa || res == 0x01 || res == 0xab) {
#ifdef PCKBCDEBUG
		if (res != 0)
			printf("kbc: returned %x on kbd slot test\n", res);
#endif
		if (pckbc_attach_slot(sc, PCKBC_KBD_SLOT, 0)) {
			cmdbits |= KC8_KENABLE;
			haskbd = 1;
		}
	} else {
		printf("kbc: kbd port test: %x\n", res);
		return;
	}
#else
	if (pckbc_attach_slot(sc, PCKBC_KBD_SLOT, 0)) {
		cmdbits |= KC8_KENABLE;
		haskbd = 1;
	}
#endif /* 0 */

	/*
	 * Check aux port ok.
	 * Avoid KBC_AUXTEST because it hangs some older controllers
	 * (eg UMC880?).
	 */
	if (!pckbc_send_cmd(iot, ioh_c, KBC_AUXECHO)) {
		printf("kbc: aux echo error 1\n");
		goto nomouse;
	}
	if (!pckbc_wait_output(iot, ioh_c)) {
		printf("kbc: aux echo error 2\n");
		goto nomouse;
	}
	bus_space_write_1(iot, ioh_d, 0, 0x5a);	/* a random value */
	res = pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_AUX_SLOT, 1);

	if (ISSET(t->t_flags, PCKBC_NEED_AUXWRITE)) {
		/*
		 * The following code is necessary to find the aux port on the
		 * oqo-1 machine, among others.  However if confuses old
		 * (non-ps/2) keyboard controllers (at least UMC880x again).
		 */
		if (res == -1) {
			/* Read of aux echo timed out, try again */
			if (!pckbc_send_cmd(iot, ioh_c, KBC_AUXWRITE))
				goto nomouse;
			if (!pckbc_wait_output(iot, ioh_c))
				goto nomouse;
			bus_space_write_1(iot, ioh_d, 0, 0x5a);
			res = pckbc_poll_data1(iot, ioh_d, ioh_c,
			    PCKBC_AUX_SLOT, 1);
			DPRINTF("kbc: aux echo: %x\n", res);
		}
	}

	if (res != -1) {
		/*
		 * In most cases, the 0x5a gets echoed.
		 * Some old controllers (Gateway 2000 circa 1993)
		 * return 0xfe here.
		 * We are satisfied if there is anything in the
		 * aux output buffer.
		 */
		DPRINTF("kbc: aux echo: %x\n", res);
		t->t_haveaux = 1;
		if (pckbc_attach_slot(sc, PCKBC_AUX_SLOT, 0))
			cmdbits |= KC8_MENABLE;
	}
#ifdef PCKBCDEBUG
	else
		printf("kbc: aux echo test failed\n");
#endif

#if defined(__i386__) || defined(__amd64__)
	if (haskbd == 0 && !ISSET(flags, PCKBCF_FORCE_KEYBOARD_PRESENT)) {
		if (t->t_haveaux) {
			if (pckbc_attach_slot(sc, PCKBC_KBD_SLOT, 1))
				cmdbits |= KC8_KENABLE;
		} else {
			pckbc_release_console();
		}
	}
#endif

nomouse:
	/* enable needed interrupts */
	t->t_cmdbyte |= cmdbits;
	if (!pckbc_put8042cmd(t))
		printf("kbc: cmd word write error\n");
}

int
pckbcprint(void *aux, const char *pnp)
{
	struct pckbc_attach_args *pa = aux;

	if (!pnp)
		printf(" (%s slot)", pckbc_slot_names[pa->pa_slot]);
	return (QUIET);
}

void
pckbc_release_console(void)
{
#if defined(__i386__) || defined(__amd64__)
	/*
	 * If there is no keyboard present, yet we are the console,
	 * we might be on a legacy-free PC where the PS/2 emulated
	 * keyboard was elected as console, but went away as soon
	 * as the USB controller drivers attached.
	 *
	 * In that case, we want to release ourselves from console
	 * duties, unless we have been able to attach a mouse,
	 * which would mean this is a real PS/2 controller
	 * after all.
	 */
	if (pckbc_console != 0) {
		extern void wscn_input_init(int);

		pckbc_console = 0;
		wscn_input_init(1);
	}
#endif
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

	(void) pckbc_poll_data1(t->t_iot, t->t_ioh_d, t->t_ioh_c,
	    slot, t->t_haveaux);
}

int
pckbc_poll_data(pckbc_tag_t self, pckbc_slot_t slot)
{
	struct pckbc_internal *t = self;
	struct pckbc_slotdata *q = t->t_slotdata[slot];
	int c;

	c = pckbc_poll_data1(t->t_iot, t->t_ioh_d, t->t_ioh_c,
			     slot, t->t_haveaux);
	if (c != -1 && q && CMD_IN_QUEUE(q)) {
		/* we jumped into a running command - try to
		 deliver the response */
		if (pckbc_cmdresponse(t, slot, c))
			return (-1);
	}
	return (c);
}

/*
 * set scancode translation on
 */
int
pckbc_xt_translation(pckbc_tag_t self, int *table)
{
	struct pckbc_internal *t = self;

#ifdef __sparc64__ /* only pckbc@ebus on sparc64 uses this */
	if ((t->t_flags & PCKBC_CANT_TRANSLATE) != 0) {
		/* Hardware lacks translation capability. Nothing to do! */
		if (t->t_flags & PCKBC_FIXED_SET2)
			*table = 2;
		else /* PCKBC_FIXED_SET3 */
			*table = 3;
		return (-1);
	}
#endif

	if (t->t_cmdbyte & KC8_TRANS)
		return (0);

	t->t_cmdbyte |= KC8_TRANS;
	if (!pckbc_put8042cmd(t))
		return (-1);

	/* read back to be sure */
	if (!pckbc_get8042cmd(t))
		return (-1);

	return (t->t_cmdbyte & KC8_TRANS) ? (0) : (-1);
}

static struct pckbc_portcmd {
	u_char cmd_en, cmd_dis;
} pckbc_portcmd[2] = {
	{
		KBC_KBDENABLE, KBC_KBDDISABLE,
	}, {
		KBC_AUXENABLE, KBC_AUXDISABLE,
	}
};

void
pckbc_slot_enable(pckbc_tag_t self, pckbc_slot_t slot, int on)
{
	struct pckbc_internal *t = (struct pckbc_internal *)self;
	struct pckbc_portcmd *cmd;

	cmd = &pckbc_portcmd[slot];

	if (!pckbc_send_cmd(t->t_iot, t->t_ioh_c,
			    on ? cmd->cmd_en : cmd->cmd_dis))
		printf("pckbc_slot_enable(%d) failed\n", on);

	if (slot == PCKBC_KBD_SLOT) {
		if (on)
			timeout_add_sec(&t->t_poll, 1);
		else
			timeout_del(&t->t_poll);
	}
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
			pckbcintr_internal(t, t->t_sc);
			splx(s);
		}
	}
}

/*
 * Pass command to device, poll for ACK and data.
 * to be called at spltty()
 */
static void
pckbc_poll_cmd1(struct pckbc_internal *t, pckbc_slot_t slot,
    struct pckbc_devcmd *cmd)
{
	bus_space_tag_t iot = t->t_iot;
	bus_space_handle_t ioh_d = t->t_ioh_d;
	bus_space_handle_t ioh_c = t->t_ioh_c;
	int i, c = 0;

	while (cmd->cmdidx < cmd->cmdlen) {
		if (!pckbc_send_devcmd(t, slot, cmd->cmd[cmd->cmdidx])) {
			printf("pckbc_cmd: send error\n");
			cmd->status = EIO;
			return;
		}
		for (i = 10; i; i--) { /* 1s ??? */
			c = pckbc_poll_data1(iot, ioh_d, ioh_c, slot,
					     t->t_haveaux);
			if (c != -1)
				break;
		}

		switch (c) {
		case KBC_DEVCMD_ACK:
			cmd->cmdidx++;
			continue;
		/*
		 * Some legacy free PCs keep returning Basic Assurance Test
		 * (BAT) instead of something usable, so fail gracefully.
		 */
		case KBC_DEVCMD_RESEND:
		case KBC_DEVCMD_BAT_DONE:
		case KBC_DEVCMD_BAT_FAIL:
			DPRINTF("pckbc_cmd: %s\n",
			    c == KBC_DEVCMD_RESEND ? "RESEND": "BAT");
			if (cmd->retries++ < 5)
				continue;

			DPRINTF("pckbc_cmd: cmd failed\n");
			cmd->status = ENXIO;
			return;
		case -1:
			DPRINTF("pckbc_cmd: timeout\n");
			cmd->status = EIO;
			return;
		default:
			DPRINTF("pckbc_cmd: lost 0x%x\n", c);
		}
	}

	while (cmd->responseidx < cmd->responselen) {
		if (cmd->flags & KBC_CMDFLAG_SLOW)
			i = 100; /* 10s ??? */
		else
			i = 10; /* 1s ??? */
		while (i--) {
			c = pckbc_poll_data1(iot, ioh_d, ioh_c, slot,
					     t->t_haveaux);
			if (c != -1)
				break;
		}
		if (c == -1) {
			DPRINTF("pckbc_cmd: no data\n");
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
	memcpy(nc.cmd, cmd, len);
	nc.cmdlen = len;
	nc.responselen = responselen;
	nc.flags = (slow ? KBC_CMDFLAG_SLOW : 0);

	pckbc_poll_cmd1(self, slot, &nc);

	if (nc.status == 0 && respbuf)
		memcpy(respbuf, nc.response, responselen);

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
		cmd->flags &= ~KBC_CMDFLAG_QUEUED;
#ifdef PCKBCDEBUG
		printf("pckbc_cleanqueue: removing");
		for (i = 0; i < cmd->cmdlen; i++)
			printf(" %02x", cmd->cmd[i]);
		printf("\n");
#endif
		/*
		 * A synchronous command on the cmdqueue is currently owned by a
		 * sleeping proc. The same proc is responsible for putting it
		 * back on the freequeue once awake.
		 */
		if (cmd->flags & KBC_CMDFLAG_SYNC)
			continue;

		TAILQ_INSERT_TAIL(&q->freequeue, cmd, next);
	}
}

void
pckbc_cleanqueues(struct pckbc_internal *t)
{
	if (t->t_slotdata[PCKBC_KBD_SLOT])
		pckbc_cleanqueue(t->t_slotdata[PCKBC_KBD_SLOT]);
	if (t->t_slotdata[PCKBC_AUX_SLOT])
		pckbc_cleanqueue(t->t_slotdata[PCKBC_AUX_SLOT]);
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

	pckbc_cleanqueues(t);

	while (bus_space_read_1(t->t_iot, t->t_ioh_c, 0) & KBS_DIB) {
		KBD_DELAY;
		(void) bus_space_read_1(t->t_iot, t->t_ioh_d, 0);
	}

	/* reset KBC? */

	splx(s);
}

/*
 * Stop the keyboard controller when we are going to suspend
 */
void
pckbc_stop(struct pckbc_softc *sc)
{
	struct pckbc_internal *t = sc->id;

	timeout_del(&t->t_poll);
	pckbc_cleanqueues(t);
	timeout_del(&t->t_cleanup);
}

/*
 * Reset the keyboard controller in a violent fashion; normally done
 * after suspend/resume when we do not trust the machine.
 */
void
pckbc_reset(struct pckbc_softc *sc)
{
	struct pckbc_internal *t = sc->id;
	bus_space_tag_t iot = t->t_iot;
	bus_space_handle_t ioh_d = t->t_ioh_d, ioh_c = t->t_ioh_c;

	pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT, 0);
	/* KBC selftest */
	if (pckbc_send_cmd(iot, ioh_c, KBC_SELFTEST) == 0)
		return;
	pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT, 0);
	(void)pckbc_put8042cmd(t);
	pckbcintr_internal(t->t_sc->id, t->t_sc);
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

	if (q->polling) {
		do {
			pckbc_poll_cmd1(t, slot, cmd);
			if (cmd->status)
				printf("pckbc_start: command error\n");

			TAILQ_REMOVE(&q->cmdqueue, cmd, next);
			cmd->flags &= ~KBC_CMDFLAG_QUEUED;
			if (cmd->flags & KBC_CMDFLAG_SYNC) {
				wakeup(cmd);
			} else {
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
			if (cmd->retries++ < 5) {
				/* try again last command */
				goto restart;
			} else {
				DPRINTF("pckbc: cmd failed\n");
				cmd->status = ENXIO;
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
	cmd->flags &= ~KBC_CMDFLAG_QUEUED;
	if (cmd->flags & KBC_CMDFLAG_SYNC) {
		wakeup(cmd);
	} else {
		timeout_del(&t->t_cleanup);
		TAILQ_INSERT_TAIL(&q->freequeue, cmd, next);
	}
	cmd = TAILQ_FIRST(&q->cmdqueue);
	if (cmd == NULL)
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
	memcpy(nc->cmd, cmd, len);
	nc->cmdlen = len;
	nc->responselen = responselen;
	nc->flags = (sync ? KBC_CMDFLAG_SYNC : 0);

	s = spltty();

	if (q->polling && sync) {
		/*
		 * XXX We should poll until the queue is empty.
		 * But we don't come here normally, so make
		 * it simple and throw away everything.
		 */
		pckbc_cleanqueue(q);
	}

	isactive = CMD_IN_QUEUE(q);
	nc->flags |= KBC_CMDFLAG_QUEUED;
	TAILQ_INSERT_TAIL(&q->cmdqueue, nc, next);
	if (!isactive)
		pckbc_start(t, slot);

	if (q->polling)
		res = (sync ? nc->status : 0);
	else if (sync) {
		if ((res = tsleep_nsec(nc, 0, "kbccmd", SEC_TO_NSEC(1)))) {
			pckbc_cleanup(t);
		} else {
			/*
			 * Under certain circumstances, such as during suspend,
			 * tsleep() becomes a no-op and the command is left on
			 * the cmdqueue.
			 */
			if (nc->flags & KBC_CMDFLAG_QUEUED) {
				TAILQ_REMOVE(&q->cmdqueue, nc, next);
				nc->flags &= ~KBC_CMDFLAG_QUEUED;
			}
			res = nc->status;
		}
	} else
		timeout_add_sec(&t->t_cleanup, 1);

	if (sync) {
		if (respbuf)
			memcpy(respbuf, nc->response, responselen);
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

	if (pckbc_console && slot == PCKBC_KBD_SLOT)
		timeout_add_sec(&t->t_poll, 1);
}

void
pckbc_poll(void *v)
{
	struct pckbc_internal *t = v;
	int s;

	s = spltty();
	(void)pckbcintr_internal(t, t->t_sc);
	timeout_add_sec(&t->t_poll, 1);
	splx(s);
}

int
pckbcintr(void *vsc)
{
	struct pckbc_softc *sc = (struct pckbc_softc *)vsc;

	return (pckbcintr_internal(sc->id, sc));
}

int
pckbcintr_internal(struct pckbc_internal *t, struct pckbc_softc *sc)
{
	u_char stat;
	pckbc_slot_t slot;
	struct pckbc_slotdata *q;
	int served = 0, data;

	/* reschedule timeout further into the idle times */
	if (timeout_pending(&t->t_poll))
		timeout_add_sec(&t->t_poll, 1);

	for(;;) {
		stat = bus_space_read_1(t->t_iot, t->t_ioh_c, 0);
		if (!(stat & KBS_DIB))
			break;

		served = 1;

		slot = (t->t_haveaux && (stat & KBS_AUXDATA)) ?
		    PCKBC_AUX_SLOT : PCKBC_KBD_SLOT;
		q = t->t_slotdata[slot];

		if (!q) {
			/* XXX do something for live insertion? */
#ifdef PCKBCDEBUG
			printf("pckbcintr: no dev for slot %d\n", slot);
#endif
			KBD_DELAY;
			(void) bus_space_read_1(t->t_iot, t->t_ioh_d, 0);
			continue;
		}

		if (q->polling)
			break; /* pckbc_poll_data() will get it */

		KBD_DELAY;
		data = bus_space_read_1(t->t_iot, t->t_ioh_d, 0);

		if (CMD_IN_QUEUE(q) && pckbc_cmdresponse(t, slot, data))
			continue;

		if (sc != NULL) {
			if (sc->inputhandler[slot])
				(*sc->inputhandler[slot])(sc->inputarg[slot],
				    data);
#ifdef PCKBCDEBUG
			else
				printf("pckbcintr: slot %d lost %d\n",
				    slot, data);
#endif
		}
	}

	return (served);
}

int
pckbc_cnattach(bus_space_tag_t iot, bus_addr_t addr, bus_size_t cmd_offset,
    int flags)
{
	bus_space_handle_t ioh_d, ioh_c;
	int res = 0;

	if (bus_space_map(iot, addr + KBDATAP, 1, 0, &ioh_d))
                return (ENXIO);
	if (bus_space_map(iot, addr + cmd_offset, 1, 0, &ioh_c)) {
		bus_space_unmap(iot, ioh_d, 1);
                return (ENXIO);
	}

	pckbc_consdata.t_iot = iot;
	pckbc_consdata.t_ioh_d = ioh_d;
	pckbc_consdata.t_ioh_c = ioh_c;
	pckbc_consdata.t_addr = addr;
	pckbc_consdata.t_flags = flags;
	timeout_set(&pckbc_consdata.t_cleanup, pckbc_cleanup, &pckbc_consdata);
	timeout_set(&pckbc_consdata.t_poll, pckbc_poll, &pckbc_consdata);

	/* flush */
	(void) pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT, 0);

	/* selftest? */

	/* init cmd byte, enable ports */
	pckbc_consdata.t_cmdbyte = KC8_CPU;
	if (!pckbc_put8042cmd(&pckbc_consdata)) {
		printf("kbc: cmd word write error\n");
		res = EIO;
	}

	if (!res) {
#if (NPCKBD > 0)
		res = pckbd_cnattach(&pckbc_consdata);
#else
		res = ENXIO;
#endif /* NPCKBD > 0 */
	}

	if (res) {
		bus_space_unmap(iot, pckbc_consdata.t_ioh_d, 1);
		bus_space_unmap(iot, pckbc_consdata.t_ioh_c, 1);
	} else {
		pckbc_consdata.t_slotdata[PCKBC_KBD_SLOT] = &pckbc_cons_slotdata;
		pckbc_init_slotdata(&pckbc_cons_slotdata);
		pckbc_console = 1;
	}

	return (res);
}

struct cfdriver pckbc_cd = {
	NULL, "pckbc", DV_DULL
};
