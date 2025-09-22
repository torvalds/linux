/* $OpenBSD: pckbcvar.h,v 1.17 2023/07/25 10:00:44 miod Exp $ */
/* $NetBSD: pckbcvar.h,v 1.4 2000/06/09 04:58:35 soda Exp $ */

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
 *
 */

#ifndef _DEV_IC_PCKBCVAR_H_
#define _DEV_IC_PCKBCVAR_H_

#include <sys/timeout.h>

#define PCKBCCF_SLOT            0
#define PCKBCCF_SLOT_DEFAULT    -1

typedef void *pckbc_tag_t;
typedef int pckbc_slot_t;
#define	PCKBC_KBD_SLOT	0
#define	PCKBC_AUX_SLOT	1
#define	PCKBC_NSLOTS	2

/*
 * external representation (pckbc_tag_t),
 * needed early for console operation
 */
struct pckbc_internal { 
	bus_space_tag_t t_iot;
	bus_space_handle_t t_ioh_d, t_ioh_c; /* data port, cmd port */
	bus_addr_t t_addr;
	u_char t_cmdbyte; /* shadow */

	int t_flags;
	/* need auxwrite command to find aux */
#define	PCKBC_NEED_AUXWRITE	0x0001
	/* can't translate to XT scancodes, stuck to set #2 */
#define	PCKBC_FIXED_SET2	0x0002
	/* can't translate to XT scancodes, stuck to set #3 */
#define	PCKBC_FIXED_SET3	0x0004
#define	PCKBC_CANT_TRANSLATE	(PCKBC_FIXED_SET2 | PCKBC_FIXED_SET3)
	int t_haveaux;	/* controller has an aux port */

	struct pckbc_slotdata *t_slotdata[PCKBC_NSLOTS];

	struct pckbc_softc *t_sc; /* back pointer */

	struct timeout t_cleanup;
	struct timeout t_poll;
};

typedef void (*pckbc_inputfcn)(void *, int);

/*
 * State per device.
 */
struct pckbc_softc {
	struct device sc_dv;
	struct pckbc_internal *id;

	pckbc_inputfcn inputhandler[PCKBC_NSLOTS];
	void *inputarg[PCKBC_NSLOTS];
	char *subname[PCKBC_NSLOTS];
};

struct pckbc_attach_args {
	pckbc_tag_t pa_tag;
	pckbc_slot_t pa_slot;
};

extern const char *pckbc_slot_names[];
extern struct pckbc_internal pckbc_consdata;
extern int pckbc_console_attached;

void pckbc_set_inputhandler(pckbc_tag_t, pckbc_slot_t,
				 pckbc_inputfcn, void *, char *);

void pckbc_flush(pckbc_tag_t, pckbc_slot_t);
int pckbc_poll_cmd(pckbc_tag_t, pckbc_slot_t, u_char *, int,
			int, u_char *, int);
int pckbc_enqueue_cmd(pckbc_tag_t, pckbc_slot_t, u_char *, int,
			   int, int, u_char *);
int pckbc_send_cmd(bus_space_tag_t, bus_space_handle_t, u_char);
int pckbc_poll_data(pckbc_tag_t, pckbc_slot_t);
int pckbc_poll_data1(bus_space_tag_t, bus_space_handle_t,
			  bus_space_handle_t, pckbc_slot_t, int);
void pckbc_set_poll(pckbc_tag_t, pckbc_slot_t, int);
int pckbc_xt_translation(pckbc_tag_t, int *);
void pckbc_slot_enable(pckbc_tag_t, pckbc_slot_t, int);

void pckbc_attach(struct pckbc_softc *, int);
int pckbc_cnattach(bus_space_tag_t, bus_addr_t, bus_size_t, int);
int pckbc_is_console(bus_space_tag_t, bus_addr_t);
void pckbc_reset(struct pckbc_softc *);
void pckbc_stop(struct pckbc_softc *);
int pckbcintr(void *);

void pckbc_release_console(void);

/*
 * Device configuration flags (cf_flags).
 */

#define	PCKBCF_FORCE_KEYBOARD_PRESENT	0x0001

#endif /* _DEV_IC_PCKBCVAR_H_ */
