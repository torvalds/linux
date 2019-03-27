/*-
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/consio.h>

#if defined(__arm__) || defined(__mips__) || \
	defined(__powerpc__) || defined(__sparc64__)
#include <machine/sc_machdep.h>
#else
#include <machine/pc/display.h>
#endif

#include <dev/syscons/syscons.h>
#include <dev/syscons/sctermvar.h>

/* dumb terminal emulator */

static sc_term_init_t	dumb_init;
static sc_term_term_t	dumb_term;
static sc_term_puts_t	dumb_puts;
static sc_term_ioctl_t	dumb_ioctl;
static sc_term_clear_t	dumb_clear;
static sc_term_input_t	dumb_input;
static void		dumb_nop(void);
static sc_term_fkeystr_t	dumb_fkeystr;
static sc_term_sync_t	dumb_sync;

static sc_term_sw_t sc_term_dumb = {
	{ NULL, NULL },
	"dumb",				/* emulator name */
	"dumb terminal",		/* description */
	"*",				/* matching renderer */
	0,				/* softc size */
	0,
	dumb_init,
	dumb_term,
	dumb_puts,
	dumb_ioctl,
	(sc_term_reset_t *)dumb_nop,
	(sc_term_default_attr_t *)dumb_nop,
	dumb_clear,
	(sc_term_notify_t *)dumb_nop,
	dumb_input,
	dumb_fkeystr,
	dumb_sync,
};

SCTERM_MODULE(dumb, sc_term_dumb);

static int
dumb_init(scr_stat *scp, void **softc, int code)
{
	switch (code) {
	case SC_TE_COLD_INIT:
		++sc_term_dumb.te_refcount;
		break;
	case SC_TE_WARM_INIT:
		break;
	}
	return 0;
}

static int
dumb_term(scr_stat *scp, void **softc)
{
	--sc_term_dumb.te_refcount;
	return 0;
}

static void
dumb_puts(scr_stat *scp, u_char *buf, int len)
{
	while (len > 0) {
		++scp->sc->write_in_progress;
		sc_term_gen_print(scp, &buf, &len, SC_NORM_ATTR << 8);
    		sc_term_gen_scroll(scp, scp->sc->scr_map[0x20],
				   SC_NORM_ATTR << 8);
		--scp->sc->write_in_progress;
	}
}

static int
dumb_ioctl(scr_stat *scp, struct tty *tp, u_long cmd, caddr_t data,
	   struct thread *td)
{
	vid_info_t *vi;

	switch (cmd) {
	case GIO_ATTR:      	/* get current attributes */
		*(int*)data = SC_NORM_ATTR;
		return 0;
	case CONS_GETINFO:  	/* get current (virtual) console info */
		vi = (vid_info_t *)data;
		if (vi->size != sizeof(struct vid_info))
			return EINVAL;
		vi->mv_norm.fore = SC_NORM_ATTR & 0x0f;
		vi->mv_norm.back = (SC_NORM_ATTR >> 4) & 0x0f;
		vi->mv_rev.fore = SC_NORM_ATTR & 0x0f;
		vi->mv_rev.back = (SC_NORM_ATTR >> 4) & 0x0f;
		/*
		 * The other fields are filled by the upper routine. XXX
		 */
		return ENOIOCTL;
	}
	return ENOIOCTL;
}

static void
dumb_clear(scr_stat *scp)
{
	sc_move_cursor(scp, 0, 0);
	sc_vtb_clear(&scp->vtb, scp->sc->scr_map[0x20], SC_NORM_ATTR << 8);
	mark_all(scp);
}

static int
dumb_input(scr_stat *scp, int c, struct tty *tp)
{
	return FALSE;
}

static const char *
dumb_fkeystr(scr_stat *scp, int c)
{
	return (NULL);
}

static void
dumb_sync(scr_stat *scp)
{
}

static void
dumb_nop(void)
{
	/* nothing */
}
