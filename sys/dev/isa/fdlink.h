/*	$OpenBSD: fdlink.h,v 1.9 2007/04/10 17:47:55 miod Exp $	*/

/*-
 * Copyright (c) 1993, 1994, 1995 Charles Hannum.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
 *
 * Portions Copyright (c) 1993, 1994 by
 *  jc@irbs.UUCP (John Capo)
 *  vak@zebub.msk.su (Serge Vakulenko)
 *  ache@astral.msk.su (Andrew A. Chernov)
 *  joerg_wunsch@uriah.sax.de (Joerg Wunsch)
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
 */

/*
 * The goo that binds the floppy controller to its devices.
 */

enum fdc_state {
	DEVIDLE = 0,
	MOTORWAIT,
	DOSEEK,
	SEEKWAIT,
	SEEKTIMEDOUT,
	SEEKCOMPLETE,
	DOIO,
	IOCOMPLETE,
	IOTIMEDOUT,
	DORESET,
	RESETCOMPLETE,
	RESETTIMEDOUT,
	DORECAL,
	RECALWAIT,
	RECALTIMEDOUT,
	RECALCOMPLETE
};

enum fdc_type {
	FDC_TYPE_TAPE,
	FDC_TYPE_DISK
};


/* software state, per controller */
struct fd_softc;
struct fdc_fdlink {
	struct fd_softc *sc_fd[4];	/* pointers to children */
	TAILQ_HEAD(drivehead, fd_softc) sc_drives;
};

struct ft_softc;
struct fdc_ftlink {
	struct ft_softc *sc_ft[4];	/* pointers to children */
};

struct fdc_softc {
	struct device sc_dev;		/* boilerplate */
	struct isadev sc_id;
	void *sc_ih;

	bus_space_tag_t sc_iot;		/* ISA chipset identifier */
	bus_space_handle_t sc_ioh;	/* ISA io handle */
	bus_space_handle_t sc_ioh_ctl;	/* ISA io handle */

	int sc_drq;

	enum fdc_type sc_type[4];	/* type of device */
	union {
		struct fdc_fdlink fdlink;
		struct fdc_ftlink ftlink;
	} sc_link;
	enum fdc_state sc_state;
	int sc_errors;			/* number of retries so far */
	struct timeout fdcpseudointr_to;
	u_char sc_status[7];		/* copy of registers */
};

/*
 * Arguments passed between fdcattach and f[dt]probe.
 */
struct fdc_attach_args {
	int fa_drive;
	int fa_flags;
	int fa_type;			/* tape drive type */
	struct fd_type *fa_deftype;
};

/* Functions from fdc.c. */
int fdcresult(struct fdc_softc *);
int out_fdc(bus_space_tag_t, bus_space_handle_t, u_char);
void fdcstart(struct fdc_softc *);
void fdcstatus(struct device *, int, char *);
void fdcpseudointr(void *);

/* Functions from fd.c. */
struct fd_type *fd_nvtotype(char *, int, int);
