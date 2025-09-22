/* $OpenBSD: pckbd.c,v 1.51 2023/08/13 21:54:02 miod Exp $ */
/* $NetBSD: pckbd.c,v 1.24 2000/06/05 22:20:57 sommerfeld Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)pccons.c	5.11 (Berkeley) 5/21/91
 */

/*
 * code to work keyboard for PC-style console
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/ioctl.h>

#include <machine/bus.h>

#include <dev/ic/pckbcvar.h>
#include <dev/pckbc/pckbdreg.h>
#include <dev/pckbc/pmsreg.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdraw.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#include <dev/pckbc/wskbdmap_mfii.h>

struct pckbd_internal {
	int t_isconsole;
	pckbc_tag_t t_kbctag;
	pckbc_slot_t t_kbcslot;

	int t_translating;	/* nonzero if hardware performs translation */
	int t_table;		/* scan code set in use */

	int t_lastchar;
	int t_extended;
	int t_extended1;
	int t_releasing;

	struct pckbd_softc *t_sc; /* back pointer */
};

struct pckbd_softc {
        struct  device sc_dev;

	struct pckbd_internal *id;
	int sc_enabled;

	int sc_ledstate;

	struct device *sc_wskbddev;
#ifdef WSDISPLAY_COMPAT_RAWKBD
	int	rawkbd;
	u_int	sc_rawcnt;
	char	sc_rawbuf[3];
#endif
};

static int pckbd_is_console(pckbc_tag_t, pckbc_slot_t);

int pckbdprobe(struct device *, void *, void *);
void pckbdattach(struct device *, struct device *, void *);
int pckbdactivate(struct device *, int);

const struct cfattach pckbd_ca = {
	sizeof(struct pckbd_softc), 
	pckbdprobe, 
	pckbdattach, 
	NULL, 
	pckbdactivate
};

int	pckbd_enable(void *, int);
void	pckbd_set_leds(void *, int);
int	pckbd_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wskbd_accessops pckbd_accessops = {
	pckbd_enable,
	pckbd_set_leds,
	pckbd_ioctl,
};

void	pckbd_cngetc(void *, u_int *, int *);
void	pckbd_cnpollc(void *, int);
void	pckbd_cnbell(void *, u_int, u_int, u_int);

const struct wskbd_consops pckbd_consops = {
	pckbd_cngetc,
	pckbd_cnpollc,
	pckbd_cnbell,
};

const struct wskbd_mapdata pckbd_keymapdata = {
	pckbd_keydesctab,
#ifdef PCKBD_LAYOUT
	PCKBD_LAYOUT,
#else
	KB_US | KB_DEFAULT,
#endif
};

/*
 * Hackish support for a bell on the PC Keyboard; when a suitable beeper
 * is found, it attaches itself into the pckbd driver here.
 */
void	(*pckbd_bell_fn)(void *, u_int, u_int, u_int, int);
void	*pckbd_bell_fn_arg;

void	pckbd_bell(u_int, u_int, u_int, int);

int	pckbd_scancode_translate(struct pckbd_internal *, int);
int	pckbd_set_xtscancode(pckbc_tag_t, pckbc_slot_t,
	    struct pckbd_internal *);
void	pckbd_init(struct pckbd_internal *, pckbc_tag_t, pckbc_slot_t, int);
void	pckbd_input(void *, int);

static int	pckbd_decode(struct pckbd_internal *, int,
				  u_int *, int *);
static int	pckbd_led_encode(int);

struct pckbd_internal pckbd_consdata;

int
pckbdactivate(struct device *self, int act)
{
	struct pckbd_softc *sc = (struct pckbd_softc *)self;
	int rv = 0;
	u_char cmd[1];

	switch(act) {
	case DVACT_RESUME:
		if (sc->sc_enabled) {
			/*
			 * Some keyboards are not enabled after a reset,
			 * so make sure it is enabled now.
			 */
			cmd[0] = KBC_ENABLE;
			(void) pckbc_poll_cmd(sc->id->t_kbctag,
			    sc->id->t_kbcslot, cmd, 1, 0, NULL, 0);
			/* XXX - also invoke pckbd_set_xtscancode() too? */
		}
		break;
	}

	rv = config_activate_children(self, act);

	return (rv);
}

int
pckbd_set_xtscancode(pckbc_tag_t kbctag, pckbc_slot_t kbcslot,
    struct pckbd_internal *id)
{
	int table = 0;

	if (pckbc_xt_translation(kbctag, &table)) {
#ifdef DEBUG
		printf("pckbd: enabling of translation failed\n");
#endif
#ifdef __sparc64__ /* only pckbc@ebus on sparc64 uses this */
		/*
		 * If hardware lacks translation capability, stick to the
		 * table it is using.
		 */
		if (table != 0) {
			id->t_translating = 0;
			id->t_table = table;
			return 0;
		}
#endif
		/*
		 * Since the keyboard controller can not translate scan
		 * codes to the XT set (#1), we would like to request
		 * this exact set. However it is likely that the
		 * controller does not support it either.
		 *
		 * So try scan code set #2 as well, which this driver
		 * knows how to translate.
		 */
		table = 2;
		if (id != NULL)
			id->t_translating = 0;
	} else {
		table = 3;
		if (id != NULL) {
			id->t_translating = 1;
			if (id->t_table == 0) {
				/*
				 * Don't bother explicitly setting into set 2,
				 * it's the default.
				 */
				id->t_table = 2;
				return (0);
			}
		}
	}

	/* keep falling back until we hit a table that looks usable. */
	for (; table >= 1; table--) {
		u_char cmd[2];
#ifdef DEBUG
		printf("pckbd: trying table %d\n", table);
#endif
		cmd[0] = KBC_SETTABLE;
		cmd[1] = table;
		if (pckbc_poll_cmd(kbctag, kbcslot, cmd, 2, 0, NULL, 0)) {
#ifdef DEBUG
			printf("pckbd: table set of %d failed\n", table);
#endif
			if (table > 1) {
				cmd[0] = KBC_RESET;
				(void)pckbc_poll_cmd(kbctag, kbcslot, cmd,
				    1, 1, NULL, 1);
				pckbc_flush(kbctag, kbcslot);

				continue;
			}
		}

		/*
		 * the 8042 took the table set request, however, not all that
		 * report they can work with table 3 actually work, so ask what
		 * table it reports it's in.
		 */
		if (table == 3) {
			u_char resp[1];

			cmd[0] = KBC_SETTABLE;
			cmd[1] = 0;
			if (pckbc_poll_cmd(kbctag, kbcslot, cmd, 2, 1, resp, 0)) {
				/*
				 * query failed, step down to table 2 to be
				 * safe.
				 */
#ifdef DEBUG
				printf("pckbd: table 3 verification failed\n");
#endif
				continue;
			} else if (resp[0] == 3) {
#ifdef DEBUG
				printf("pckbd: settling on table 3\n");
#endif
				break;
			}
#ifdef DEBUG
			else
				printf("pckbd: table \"%x\" != 3, trying 2\n",
					resp[0]);
#endif
		} else {
#ifdef DEBUG
			printf("pckbd: settling on table %d\n", table);
#endif
			break;
		}
	}

	if (table == 0)
		return (1);

	if (id != NULL)
		id->t_table = table;

	return (0);
}

static int
pckbd_is_console(pckbc_tag_t tag, pckbc_slot_t slot)
{
	return (pckbd_consdata.t_isconsole &&
		(tag == pckbd_consdata.t_kbctag) &&
		(slot == pckbd_consdata.t_kbcslot));
}

/*
 * these are both bad jokes
 */
int
pckbdprobe(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct pckbc_attach_args *pa = aux;
	u_char cmd[1], resp[2];
	int res;

	/*
	 * XXX There are rumours that a keyboard can be connected
	 * to the aux port as well. For me, this didn't work.
	 * For further experiments, allow it if explicitly
	 * wired in the config file.
	 */
	if ((pa->pa_slot != PCKBC_KBD_SLOT) &&
	    (cf->cf_loc[PCKBCCF_SLOT] == PCKBCCF_SLOT_DEFAULT))
		return (0);

	/* Flush any garbage. */
	pckbc_flush(pa->pa_tag, pa->pa_slot);

	/* Reset the keyboard. */
	cmd[0] = KBC_RESET;
	res = pckbc_poll_cmd(pa->pa_tag, pa->pa_slot, cmd, 1, 1, resp, 1);
	if (res != 0) {
#ifdef DEBUG
		printf("pckbdprobe: reset error %d\n", res);
#endif
	} else if (resp[0] != KBR_RSTDONE) {
#ifdef DEBUG
		printf("pckbdprobe: reset response 0x%x\n", resp[0]);
#endif
		res = EINVAL;
	}
#if defined(__i386__) || defined(__amd64__)
	if (res) {
		/*
		 * The 8042 emulation on Chromebooks fails the reset
		 * command but otherwise appears to work correctly.
		 * Try a "get ID" command to give it a second chance.
		 */
		cmd[0] = KBC_GETID;
		res = pckbc_poll_cmd(pa->pa_tag, pa->pa_slot,
		    cmd, 1, 2, resp, 0);
		if (res != 0) {
#ifdef DEBUG
			printf("pckbdprobe: getid error %d\n", res);
#endif
		} else if (resp[0] != 0xab || resp[1] != 0x83) {
#ifdef DEBUG
			printf("pckbdprobe: unexpected id 0x%x/0x%x\n",
			    resp[0], resp[1]);
#endif
			res = EINVAL;
		}
	}
#endif
	if (res) {
		/*
		 * There is probably no keyboard connected.
		 * Let the probe succeed if the keyboard is used
		 * as console input - it can be connected later.
		 */
#if defined(__i386__) || defined(__amd64__)
		/*
		 * However, on legacy-free PCs, there might really
		 * be no PS/2 connector at all; in that case, do not
		 * even try to attach; ukbd will take over as console.
		 */
		if (res == ENXIO) {
			/* check cf_flags from parent */
			struct cfdata *cf = parent->dv_cfdata;
			if (!ISSET(cf->cf_flags, PCKBCF_FORCE_KEYBOARD_PRESENT))
				return 0;
		}
#endif
		return (pckbd_is_console(pa->pa_tag, pa->pa_slot) ? 1 : 0);
	}

	/*
	 * Some keyboards seem to leave a second ack byte after the reset.
	 * This is kind of stupid, but we account for them anyway by just
	 * flushing the buffer.
	 */
	pckbc_flush(pa->pa_tag, pa->pa_slot);

	return (2);
}

void
pckbdattach(struct device *parent, struct device *self, void *aux)
{
	struct pckbd_softc *sc = (void *)self;
	struct pckbc_attach_args *pa = aux;
	int isconsole;
	struct wskbddev_attach_args a;
	u_char cmd[1];

	isconsole = pckbd_is_console(pa->pa_tag, pa->pa_slot);

	if (isconsole) {
		sc->id = &pckbd_consdata;
		if (sc->id->t_table == 0)
			pckbd_set_xtscancode(pa->pa_tag, pa->pa_slot, sc->id);

		/*
		 * Some keyboards are not enabled after a reset,
		 * so make sure it is enabled now.
		 */
		cmd[0] = KBC_ENABLE;
		(void) pckbc_poll_cmd(sc->id->t_kbctag, sc->id->t_kbcslot,
		    cmd, 1, 0, NULL, 0);
		sc->sc_enabled = 1;
	} else {
		sc->id = malloc(sizeof(struct pckbd_internal),
				M_DEVBUF, M_WAITOK);
		pckbd_init(sc->id, pa->pa_tag, pa->pa_slot, 0);
		pckbd_set_xtscancode(pa->pa_tag, pa->pa_slot, sc->id);

		/* no interrupts until enabled */
		cmd[0] = KBC_DISABLE;
		(void) pckbc_poll_cmd(sc->id->t_kbctag, sc->id->t_kbcslot,
				      cmd, 1, 0, NULL, 0);
		sc->sc_enabled = 0;
	}

	sc->id->t_sc = sc;

	pckbc_set_inputhandler(sc->id->t_kbctag, sc->id->t_kbcslot,
			       pckbd_input, sc, sc->sc_dev.dv_xname);

	a.console = isconsole;

	a.keymap = &pckbd_keymapdata;

	a.accessops = &pckbd_accessops;
	a.accesscookie = sc;

	printf("\n");

	/*
	 * Attach the wskbd, saving a handle to it.
	 */
	sc->sc_wskbddev = config_found(self, &a, wskbddevprint);
}

int
pckbd_enable(void *v, int on)
{
	struct pckbd_softc *sc = v;
	u_char cmd[1];
	int res;

	if (on) {
		if (sc->sc_enabled)
			return (EBUSY);

		pckbc_slot_enable(sc->id->t_kbctag, sc->id->t_kbcslot, 1);

		cmd[0] = KBC_ENABLE;
		res = pckbc_poll_cmd(sc->id->t_kbctag, sc->id->t_kbcslot,
					cmd, 1, 0, NULL, 0);
		if (res) {
			printf("pckbd_enable: command error\n");
			return (res);
		}

		res = pckbd_set_xtscancode(sc->id->t_kbctag,
					   sc->id->t_kbcslot, sc->id);
		if (res)
			return (res);

		sc->sc_enabled = 1;
	} else {
		if (sc->id->t_isconsole)
			return (EBUSY);

		cmd[0] = KBC_DISABLE;
		res = pckbc_enqueue_cmd(sc->id->t_kbctag, sc->id->t_kbcslot,
					cmd, 1, 0, 1, 0);
		if (res) {
			printf("pckbd_disable: command error\n");
			return (res);
		}

		pckbc_slot_enable(sc->id->t_kbctag, sc->id->t_kbcslot, 0);

		sc->sc_enabled = 0;
	}

	return (0);
}

/*
 * Scan code set #2 translation tables
 */

const u_int8_t pckbd_xtbl2[] = {
/* 0x00 */
	0,
	RAWKEY_f9,
	0,
	RAWKEY_f5,
	RAWKEY_f3,
	RAWKEY_f1,
	RAWKEY_f2,
	RAWKEY_f12,
	RAWKEY_f6,	/* F6 according to documentation */
	RAWKEY_f10,
	RAWKEY_f8,
	RAWKEY_f6,	/* F6 according to experimentation */
	RAWKEY_f4,
	RAWKEY_Tab,
	RAWKEY_grave,
	0,
/* 0x10 */
	0,
	RAWKEY_Alt_L,
	RAWKEY_Shift_L,
	0,
	RAWKEY_Control_L,
	RAWKEY_q,
	RAWKEY_1,
	0,
	0,
	0,
	RAWKEY_z,
	RAWKEY_s,
	RAWKEY_a,
	RAWKEY_w,
	RAWKEY_2,
	RAWKEY_Meta_L,
/* 0x20 */	
	0,
	RAWKEY_c,
	RAWKEY_x,
	RAWKEY_d,
	RAWKEY_e,
	RAWKEY_4,
	RAWKEY_3,
	0,
	RAWKEY_Meta_R,
	RAWKEY_space,
	RAWKEY_v,
	RAWKEY_f,
	RAWKEY_t,
	RAWKEY_r,
	RAWKEY_5,
	0,
/* 0x30 */
	0,
	RAWKEY_n,
	RAWKEY_b,
	RAWKEY_h,
	RAWKEY_g,
	RAWKEY_y,
	RAWKEY_6,
	0,
	0,
	0,
	RAWKEY_m,
	RAWKEY_j,
	RAWKEY_u,
	RAWKEY_7,
	RAWKEY_8,
	0,
/* 0x40 */
	0,
	RAWKEY_comma,
	RAWKEY_k,
	RAWKEY_i,
	RAWKEY_o,
	RAWKEY_0,
	RAWKEY_9,
	0,
	0,
	RAWKEY_period,
	RAWKEY_slash,
	RAWKEY_l,
	RAWKEY_semicolon,
	RAWKEY_p,
	RAWKEY_minus,
	0,
/* 0x50 */
	0,
	0,
	RAWKEY_apostrophe,
	0,
	RAWKEY_bracketleft,
	RAWKEY_equal,
	0,
	0,
	RAWKEY_Caps_Lock,
	RAWKEY_Shift_R,
	RAWKEY_Return,
	RAWKEY_bracketright,
	0,
	RAWKEY_backslash,
	0,
	0,
/* 0x60 */
	0,
	0,
	0,
	0,
	0,
	0,
	RAWKEY_BackSpace,
	0,
	0,
	RAWKEY_KP_End,
	0,
	RAWKEY_KP_Left,
	RAWKEY_KP_Home,
	0,
	0,
	0,
/* 0x70 */
	RAWKEY_KP_Insert,
	RAWKEY_KP_Delete,
	RAWKEY_KP_Down,
	RAWKEY_KP_Begin,
	RAWKEY_KP_Right,
	RAWKEY_KP_Up,
	RAWKEY_Escape,
	RAWKEY_Num_Lock,
	RAWKEY_f11,
	RAWKEY_KP_Add,
	RAWKEY_KP_Next,
	RAWKEY_KP_Subtract,
	RAWKEY_KP_Multiply,
	RAWKEY_KP_Prior,
	RAWKEY_Hold_Screen,
	0,
/* 0x80 */
	0,
	0,
	0,
	RAWKEY_f7,
	0		/* Alt-Print Screen */
};

const u_int8_t pckbd_xtbl2_ext[] = {
/* 0x00 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
/* 0x10 */
	0,
	RAWKEY_Alt_R,
	0,		/* E0 12, to be ignored */
	0,
	RAWKEY_Control_R,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
/* 0x20 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
/* 0x30 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
/* 0x40 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	RAWKEY_KP_Divide,
	0,
	0,
	0,
	0,
	0,
/* 0x50 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	RAWKEY_KP_Enter,
	0,
	0,
	0,
	0,
	0,
/* 0x60 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	RAWKEY_End,
	0,
	RAWKEY_Left,
	RAWKEY_Home,
	0,
	0,
	0,
/* 0x70 */
	RAWKEY_Insert,
	RAWKEY_Delete,
	RAWKEY_Down,
	0,
	RAWKEY_Right,
	RAWKEY_Up,
	0,
	0,
	0,
	0,
	RAWKEY_Next,
	0,
	RAWKEY_Print_Screen,
	RAWKEY_Prior,
	0xc6,		/* Ctrl-Break */
	0
};

#ifdef __sparc64__ /* only pckbc@ebus on sparc64 uses this */

/*
 * Scan code set #3 translation table
 */

const u_int8_t pckbd_xtbl3[] = {
/* 0x00 */
	0,
	RAWKEY_L5,	/* Front */
	RAWKEY_L1,	/* Stop */
	RAWKEY_L3,	/* Props */
	0,
	RAWKEY_L7,	/* Open */
	RAWKEY_L9,	/* Find */
	RAWKEY_f1,
	RAWKEY_Escape,
	RAWKEY_L10,	/* Cut */
	0,
	0,
	0,
	RAWKEY_Tab,
	RAWKEY_grave,
	RAWKEY_f2,
/* 0x10 */
	RAWKEY_Help,
	RAWKEY_Control_L,
	RAWKEY_Shift_L,
	0,
	RAWKEY_Caps_Lock,
	RAWKEY_q,
	RAWKEY_1,
	RAWKEY_f3,
	0,
	RAWKEY_Alt_L,
	RAWKEY_z,
	RAWKEY_s,
	RAWKEY_a,
	RAWKEY_w,
	RAWKEY_2,
	RAWKEY_f4,
/* 0x20 */	
	0,
	RAWKEY_c,
	RAWKEY_x,
	RAWKEY_d,
	RAWKEY_e,
	RAWKEY_4,
	RAWKEY_3,
	RAWKEY_f5,
	RAWKEY_L4,	/* Undo */
	RAWKEY_space,
	RAWKEY_v,
	RAWKEY_f,
	RAWKEY_t,
	RAWKEY_r,
	RAWKEY_5,
	RAWKEY_f6,
/* 0x30 */
	RAWKEY_L2,	/* Again */
	RAWKEY_n,
	RAWKEY_b,
	RAWKEY_h,
	RAWKEY_g,
	RAWKEY_y,
	RAWKEY_6,
	RAWKEY_f7,
	0,
	RAWKEY_Alt_R,
	RAWKEY_m,
	RAWKEY_j,
	RAWKEY_u,
	RAWKEY_7,
	RAWKEY_8,
	RAWKEY_f8,
/* 0x40 */
	0,
	RAWKEY_comma,
	RAWKEY_k,
	RAWKEY_i,
	RAWKEY_o,
	RAWKEY_0,
	RAWKEY_9,
	RAWKEY_f9,
	RAWKEY_L6,	/* Copy */
	RAWKEY_period,
	RAWKEY_slash,
	RAWKEY_l,
	RAWKEY_semicolon,
	RAWKEY_p,
	RAWKEY_minus,
	RAWKEY_f10,
/* 0x50 */
	0,
	0,
	RAWKEY_apostrophe,
	0,
	RAWKEY_bracketleft,
	RAWKEY_equal,
	RAWKEY_f11,
	RAWKEY_Print_Screen,
	RAWKEY_Control_R,
	RAWKEY_Shift_R,
	RAWKEY_Return,
	RAWKEY_bracketright,
	RAWKEY_backslash,
	0,
	RAWKEY_f12,
	RAWKEY_Hold_Screen,
/* 0x60 */
	RAWKEY_Down,
	RAWKEY_Left,
	RAWKEY_Pause,
	RAWKEY_Up,
	RAWKEY_Delete,
	RAWKEY_End,
	RAWKEY_BackSpace,
	RAWKEY_Insert,
	RAWKEY_L8,	/* Paste */
	RAWKEY_KP_End,
	RAWKEY_Right,
	RAWKEY_KP_Left,
	RAWKEY_KP_Home,
	RAWKEY_Next,
	RAWKEY_Home,
	RAWKEY_Prior,
/* 0x70 */
	RAWKEY_KP_Insert,
	RAWKEY_KP_Delete,
	RAWKEY_KP_Down,
	RAWKEY_KP_Begin,
	RAWKEY_KP_Right,
	RAWKEY_KP_Up,
	RAWKEY_Num_Lock,
	RAWKEY_KP_Divide,
	0,
	RAWKEY_KP_Enter,
	RAWKEY_KP_Next,
	0,
	RAWKEY_KP_Add,
	RAWKEY_KP_Prior,
	RAWKEY_KP_Multiply,
	0,
/* 0x80 */
	0,
	0,
	0,
	0,
	RAWKEY_KP_Subtract,
	0,
	0,
	0,
	0,
	0,
	0,
	RAWKEY_Meta_L,
	RAWKEY_Meta_R
};

#endif

/*
 * Translate scan codes from set 2 or 3 to set 1
 */
int
pckbd_scancode_translate(struct pckbd_internal *id, int datain)
{
	if (id->t_translating != 0 || id->t_table == 1)
		return datain;

	if (datain == KBR_BREAK) {
		id->t_releasing = 0x80;	/* next keycode is a release */
		return 0;	/* consume scancode */
	}

	switch (id->t_table) {
	case 2:
		/*
	 	* Convert BREAK sequence (14 77 -> 1D 45)
	 	*/
		if (id->t_extended1 == 2 && datain == 0x14)
			return 0x1d | id->t_releasing;
		else if (id->t_extended1 == 1 && datain == 0x77)
			return 0x45 | id->t_releasing;

		if (id->t_extended != 0) {
			if (datain >= sizeof pckbd_xtbl2_ext)
				datain = 0;
			else
				datain = pckbd_xtbl2_ext[datain];
			/* xtbl2_ext already has the upper bit set */
			id->t_extended = 0;
		} else {
			if (datain >= sizeof pckbd_xtbl2)
				datain = 0;
			else
				datain = pckbd_xtbl2[datain] & ~0x80;
		}
		break;
#ifdef __sparc64__ /* only pckbc@ebus on sparc64 uses this */
	case 3:
		if (datain >= sizeof pckbd_xtbl3)
			datain = 0;
		else
			datain = pckbd_xtbl3[datain] & ~0x80;
		break;
#endif
	}

	if (datain == 0) {
		/*
		 * We don't know how to translate this scan code, but
		 * we can't silently eat it either (because there might
		 * have been an extended byte transmitted already).
		 * Hopefully this value will be harmless to the upper
		 * layers.
		 */
		return 0xff;
	}

	return datain | id->t_releasing;
}

static int
pckbd_decode(struct pckbd_internal *id, int datain, u_int *type, int *dataout)
{
	int key;
	int releasing;

	if (datain == KBR_EXTENDED0) {
		id->t_extended = 0x80;
		return 0;
	} else if (datain == KBR_EXTENDED1) {
		id->t_extended1 = 2;
		return 0;
	}

	releasing = datain & 0x80;
	datain &= 0x7f;

	/*
	 * process BREAK key sequence (EXT1 1D 45 / EXT1 9D C5):
	 * map to (unused) code 7F
	 */
	if (id->t_extended1 == 2 && datain == 0x1d) {
		id->t_extended1 = 1;
		return 0;
	} else if (id->t_extended1 == 1 && datain == 0x45) {
		id->t_extended1 = 0;
		datain = 0x7f;
	} else
		id->t_extended1 = 0;

	if (id->t_translating != 0 || id->t_table == 1) {
		id->t_releasing = releasing;
	} else {
		/* id->t_releasing computed in pckbd_scancode_translate() */
	}

	/* map extended keys to (unused) codes 128-254 */
	key = datain | id->t_extended;
	id->t_extended = 0;

	if (id->t_releasing) {
		id->t_releasing = 0;
		id->t_lastchar = 0;
		*type = WSCONS_EVENT_KEY_UP;
	} else {
		/* Always ignore typematic keys */
		if (key == id->t_lastchar)
			return 0;
		id->t_lastchar = key;
		*type = WSCONS_EVENT_KEY_DOWN;
	}

	*dataout = key;
	return 1;
}

void
pckbd_init(struct pckbd_internal *t, pckbc_tag_t kbctag, pckbc_slot_t kbcslot,
    int console)
{
	bzero(t, sizeof(struct pckbd_internal));

	t->t_isconsole = console;
	t->t_kbctag = kbctag;
	t->t_kbcslot = kbcslot;
}

static int
pckbd_led_encode(int led)
{
	int res;

	res = 0;

	if (led & WSKBD_LED_SCROLL)
		res |= 0x01;
	if (led & WSKBD_LED_NUM)
		res |= 0x02;
	if (led & WSKBD_LED_CAPS)
		res |= 0x04;
	return(res);
}

void
pckbd_set_leds(void *v, int leds)
{
	struct pckbd_softc *sc = v;
	u_char cmd[2];

	cmd[0] = KBC_MODEIND;
	cmd[1] = pckbd_led_encode(leds);
	sc->sc_ledstate = leds;

	(void) pckbc_enqueue_cmd(sc->id->t_kbctag, sc->id->t_kbcslot,
				 cmd, 2, 0, 0, 0);
}

/*
 * Got a console receive interrupt -
 * the console processor wants to give us a character.
 */
void
pckbd_input(void *vsc, int data)
{
	struct pckbd_softc *sc = vsc;
	int rc, type, key;

	data = pckbd_scancode_translate(sc->id, data);
	if (data == 0)
		return;

	rc = pckbd_decode(sc->id, data, &type, &key);

#ifdef WSDISPLAY_COMPAT_RAWKBD
	if (sc->rawkbd) {
		sc->sc_rawbuf[sc->sc_rawcnt++] = (char)data;

		if (rc != 0 || sc->sc_rawcnt == sizeof(sc->sc_rawbuf)) {
			wskbd_rawinput(sc->sc_wskbddev, sc->sc_rawbuf,
			    sc->sc_rawcnt);
			sc->sc_rawcnt = 0;
		}

		/*
		 * Pass audio keys to wskbd_input anyway.
		 */
		if (rc == 0 || (key != 160 && key != 174 && key != 176))
			return;
	}
#endif
	if (rc != 0)
		wskbd_input(sc->sc_wskbddev, type, key);
}

int
pckbd_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct pckbd_softc *sc = v;

	switch (cmd) {
	    case WSKBDIO_GTYPE:
		*(int *)data = WSKBD_TYPE_PC_XT;
		return 0;
	    case WSKBDIO_SETLEDS: {
		char cmd[2];
		int res;
		cmd[0] = KBC_MODEIND;
		cmd[1] = pckbd_led_encode(*(int *)data);
		sc->sc_ledstate = *(int *)data & (WSKBD_LED_SCROLL |
		    WSKBD_LED_NUM | WSKBD_LED_CAPS);
		res = pckbc_enqueue_cmd(sc->id->t_kbctag, sc->id->t_kbcslot,
					cmd, 2, 0, 1, 0);
		return (res);
		}
	    case WSKBDIO_GETLEDS:
		*(int *)data = sc->sc_ledstate;
		return (0);
	    case WSKBDIO_COMPLEXBELL:
#define d ((struct wskbd_bell_data *)data)
		/*
		 * Keyboard can't beep directly; we have an
		 * externally-provided global hook to do this.
		 */
		pckbd_bell(d->pitch, d->period, d->volume, 0);
#undef d
		return (0);
#ifdef WSDISPLAY_COMPAT_RAWKBD
	    case WSKBDIO_SETMODE:
		sc->rawkbd = (*(int *)data == WSKBD_RAW);
		return (0);
#endif
	}
	return -1;
}

void
pckbd_bell(u_int pitch, u_int period, u_int volume, int poll)
{

	if (pckbd_bell_fn != NULL)
		(*pckbd_bell_fn)(pckbd_bell_fn_arg, pitch, period,
		    volume, poll);
}

void
pckbd_hookup_bell(void (*fn)(void *, u_int, u_int, u_int, int), void *arg)
{

	if (pckbd_bell_fn == NULL) {
		pckbd_bell_fn = fn;
		pckbd_bell_fn_arg = arg;
	}
}

int
pckbd_cnattach(pckbc_tag_t kbctag)
{
	pckbd_init(&pckbd_consdata, kbctag, PCKBC_KBD_SLOT, 1);
	wskbd_cnattach(&pckbd_consops, &pckbd_consdata, &pckbd_keymapdata);
	return (0);
}

void
pckbd_cngetc(void *v, u_int *type, int *data)
{
        struct pckbd_internal *t = v;
	int val;

	for (;;) {
		val = pckbc_poll_data(t->t_kbctag, t->t_kbcslot);
		if (val == -1)
			continue;

		val = pckbd_scancode_translate(t, val);
		if (val == 0)
			continue;

		if (pckbd_decode(t, val, type, data))
			return;
	}
}

void
pckbd_cnpollc(void *v, int on)
{
	struct pckbd_internal *t = v;

	pckbc_set_poll(t->t_kbctag, t->t_kbcslot, on);

	/*
	 * If we enter ukc or ddb before having attached the console
	 * keyboard we need to probe its scan code set.
	 */
	if (t->t_table == 0) {
		char cmd[1];

		pckbc_flush(t->t_kbctag, t->t_kbcslot);
		pckbd_set_xtscancode(t->t_kbctag, t->t_kbcslot, t);

		/* Just to be sure. */
		cmd[0] = KBC_ENABLE;
		pckbc_poll_cmd(t->t_kbctag, PCKBC_KBD_SLOT, cmd, 1, 0, NULL, 0);
	}
}

void
pckbd_cnbell(void *v, u_int pitch, u_int period, u_int volume)
{

	pckbd_bell(pitch, period, volume, 1);
}

struct cfdriver pckbd_cd = {
	NULL, "pckbd", DV_DULL
};
