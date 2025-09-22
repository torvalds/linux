/*	$OpenBSD: pcons.c,v 1.30 2025/06/28 11:33:12 miod Exp $	*/
/*	$NetBSD: pcons.c,v 1.7 2001/05/02 10:32:20 scw Exp $	*/

/*-
 * Copyright (c) 2000 Eduardo E. Horvath
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Default console driver.  Uses the PROM or whatever
 * driver(s) are appropriate.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/syslog.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>
#include <machine/conf.h>
#include <machine/cpu.h>
#include <machine/psl.h>

#include <dev/cons.h>

#include "wsdisplay.h"

#if NWSDISPLAY > 0
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#endif

struct pconssoftc {
	struct device of_dev;

#if NWSDISPLAY > 0
	int	sc_wsdisplay;
	u_int	sc_nscreens;
#endif

	struct tty *of_tty;
	struct timeout sc_poll_to;
	int of_flags;
};
/* flags: */
#define	OFPOLL		1

#define	OFBURSTLEN	128	/* max number of bytes to write in one chunk */

/* XXXXXXXX - this is in MI code in NetBSD */
/*
 * Stuff to handle debugger magic key sequences.
 */
#define CNS_LEN                 128
#define CNS_MAGIC_VAL(x)        ((x)&0x1ff)
#define CNS_MAGIC_NEXT(x)       (((x)>>9)&0x7f)
#define CNS_TERM                0x7f    /* End of sequence */

typedef struct cnm_state {
	int	cnm_state;
	u_short	*cnm_magic;
} cnm_state_t;
#ifdef DDB
#include <ddb/db_var.h>
#define cn_trap()	do { if (db_console) db_enter(); } while (0)
#else
#define cn_trap()
#endif
#define cn_isconsole(d)	((d) == cn_tab->cn_dev)
void cn_init_magic(cnm_state_t *cnm);
int cn_set_magic(char *magic);
/* This should be called for each byte read */
#ifndef cn_check_magic
#define cn_check_magic(d, k, s)                                         \
        do {                                                            \
		if (cn_isconsole(d)) {                                  \
			int v = (s).cnm_magic[(s).cnm_state];           \
			if ((k) == CNS_MAGIC_VAL(v)) {                  \
				(s).cnm_state = CNS_MAGIC_NEXT(v);      \
				if ((s).cnm_state == CNS_TERM) {        \
					cn_trap();                      \
                                        (s).cnm_state = 0;              \
				}                                       \
			} else {                                        \
				(s).cnm_state = 0;                      \
			}                                               \
		}                                                       \
	} while (/* CONSTCOND */ 0)
#endif

/* Encode out-of-band events this way when passing to cn_check_magic() */
#define CNC_BREAK               0x100

/* XXXXXXXXXX - end of this part of cnmagic, more at the end of this file. */

#include <sparc64/dev/cons.h>

int pconsmatch(struct device *, void *, void *);
void pconsattach(struct device *, struct device *, void *);

const struct cfattach pcons_ca = {
	sizeof(struct pconssoftc), pconsmatch, pconsattach
};

struct cfdriver pcons_cd = {
	NULL, "pcons", DV_TTY
};

extern struct cfdriver pcons_cd;
static struct cnm_state pcons_cnm_state;

static int pconsprobe(void);
static void pcons_wsdisplay_init(struct pconssoftc *);
extern struct consdev *cn_tab;

cons_decl(prom_);

int
pconsmatch(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	/* Only attach if no other console has attached. */
	return (strcmp("pcons", ma->ma_name) == 0 &&
	    cn_tab->cn_getc == prom_cngetc);
}

void pcons_poll(void *);

void
pconsattach(struct device *parent, struct device *self, void *aux)
{
	struct pconssoftc *sc = (struct pconssoftc *) self;
#if NWSDISPLAY > 0
	char buffer[128];
	extern struct consdev wsdisplay_cons;
	extern int wsdisplay_getc_dummy(dev_t);
#endif

	printf("\n");
	if (!pconsprobe())
		return;

#if NWSDISPLAY > 0
	/*
	 * Attach a dumb wsdisplay device if a wscons input driver has
	 * registered as the console, or is about to do so (usb keyboards).
	 */
	if (wsdisplay_cons.cn_getc != wsdisplay_getc_dummy)
		sc->sc_wsdisplay = 1;
	else {
		if (OF_getprop(OF_instance_to_package(stdin), "compatible",
		    buffer, sizeof(buffer)) != -1 &&
		    strncmp("usb", buffer, 3) == 0)
			sc->sc_wsdisplay = 1;
	}

	if (sc->sc_wsdisplay != 0) {
		pcons_wsdisplay_init(sc);
		return;
	}
#endif
	cn_init_magic(&pcons_cnm_state);
	cn_set_magic("+++++");
	timeout_set(&sc->sc_poll_to, pcons_poll, sc);
}

void pconsstart(struct tty *);
int pconsparam(struct tty *, struct termios *);

int
pconsopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct pconssoftc *sc;
	int unit = minor(dev);
	struct tty *tp;
	
	if (unit >= pcons_cd.cd_ndevs)
		return ENXIO;
	sc = pcons_cd.cd_devs[unit];
	if (!sc)
		return ENXIO;
#if NWSDISPLAY > 0
	if (sc->sc_wsdisplay != 0)
		return ENXIO;
#endif
	if (!(tp = sc->of_tty)) {
		sc->of_tty = tp = ttymalloc(0);
	}
	tp->t_oproc = pconsstart;
	tp->t_param = pconsparam;
	tp->t_dev = dev;
	cn_tab->cn_dev = dev;
	if (!(tp->t_state & TS_ISOPEN)) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		pconsparam(tp, &tp->t_termios);
		ttsetwater(tp);
	} else if ((tp->t_state & TS_XCLUDE) && suser(p))
		return EBUSY;
	tp->t_state |= TS_CARR_ON;
	
	if (!(sc->of_flags & OFPOLL)) {
		sc->of_flags |= OFPOLL;
		timeout_add(&sc->sc_poll_to, 1);
	}

	return (*linesw[tp->t_line].l_open)(dev, tp, p);
}

int
pconsclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct pconssoftc *sc = pcons_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->of_tty;

	timeout_del(&sc->sc_poll_to);
	sc->of_flags &= ~OFPOLL;
	(*linesw[tp->t_line].l_close)(tp, flag, p);
	ttyclose(tp);
	return 0;
}

int
pconsread(dev_t dev, struct uio *uio, int flag)
{
	struct pconssoftc *sc = pcons_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->of_tty;

	return (*linesw[tp->t_line].l_read)(tp, uio, flag);
}

int
pconswrite(dev_t dev, struct uio *uio, int flag)
{
	struct pconssoftc *sc = pcons_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->of_tty;

	return (*linesw[tp->t_line].l_write)(tp, uio, flag);
}

int
pconsioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct pconssoftc *sc = pcons_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->of_tty;
	int error;

	if ((error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p)) >= 0)
		return error;
	if ((error = ttioctl(tp, cmd, data, flag, p)) >= 0)
		return error;
	return ENOTTY;
}

struct tty *
pconstty(dev_t dev)
{
	struct pconssoftc *sc = pcons_cd.cd_devs[minor(dev)];

	return sc->of_tty;
}

int
pconsstop(struct tty *tp, int flag)
{
	return 0;
}

void
pconsstart(struct tty *tp)
{
	struct clist *cl;
	int s, len;
	u_char buf[OFBURSTLEN];
	
	s = spltty();
	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
		splx(s);
		return;
	}
	tp->t_state |= TS_BUSY;
	splx(s);
	cl = &tp->t_outq;
	len = q_to_b(cl, buf, OFBURSTLEN);
	OF_write(stdout, buf, len);
	s = spltty();
	tp->t_state &= ~TS_BUSY;
	if (cl->c_cc) {
		tp->t_state |= TS_TIMEOUT;
		timeout_add(&tp->t_rstrt_to, 1);
	}
	if (cl->c_cc <= tp->t_lowat) {
		if (tp->t_state & TS_ASLEEP) {
			tp->t_state &= ~TS_ASLEEP;
			wakeup(cl);
		}
		selwakeup(&tp->t_wsel);
	}
	splx(s);
}

int
pconsparam(struct tty *tp, struct termios *t)
{
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return 0;
}

void
pcons_poll(void *aux)
{
	struct pconssoftc *sc = aux;
	struct tty *tp = sc->of_tty;
	char ch;
	
	while (OF_read(stdin, &ch, 1) > 0) {
		cn_check_magic(tp->t_dev, ch, pcons_cnm_state);
		if (tp && (tp->t_state & TS_ISOPEN)) {
			if (ch == '\b')
				ch = '\177';
			(*linesw[tp->t_line].l_rint)(ch, tp);
		}
	}
	timeout_add(&sc->sc_poll_to, 1);
}

int
pconsprobe(void)
{
	if (!stdin) stdin = OF_stdin();
	if (!stdout) stdout = OF_stdout();

	return (stdin && stdout);
}

void
pcons_cnpollc(dev_t dev, int on)
{
	struct pconssoftc *sc = NULL;

	if (pcons_cd.cd_devs) 
		sc = pcons_cd.cd_devs[minor(dev)];

	if (sc == NULL)
		return;

	if (on) {
		if (sc->of_flags & OFPOLL)
			timeout_del(&sc->sc_poll_to);
		sc->of_flags &= ~OFPOLL;
	} else {
                /* Resuming kernel. */
		if (!(sc->of_flags & OFPOLL)) {
			sc->of_flags |= OFPOLL;
			timeout_add(&sc->sc_poll_to, 1);
		}
	}
}

/* XXXXXXXX --- more cnmagic stuff. */
#define ENCODE_STATE(c, n) (short)(((c)&0x1ff)|(((n)&0x7f)<<9))

static unsigned short cn_magic[CNS_LEN];

/*
 * Initialize a cnm_state_t.
 */
void
cn_init_magic(cnm_state_t *cnm)
{
	cnm->cnm_state = 0;
	cnm->cnm_magic = cn_magic;
}

/*
 * Translate a magic string to a state
 * machine table.
 */
int
cn_set_magic(char *magic)
{
	unsigned int i, c, n;
	unsigned short m[CNS_LEN];

	for (i=0; i<CNS_LEN; i++) {
		c = (*magic++)&0xff;
		n = *magic ? i+1 : CNS_TERM;
		switch (c) {
		case 0:
			/* End of string */
			if (i == 0) {
				/* empty string? */
				cn_magic[0] = 0;
#ifdef DEBUG
				printf("cn_set_magic(): empty!\n");
#endif
				return (0);
			}
			do {
				cn_magic[i] = m[i];
			} while (i--);
			return(0);
		case 0x27:
			/* Escape sequence */
			c = (*magic++)&0xff;
			n = *magic ? i+1 : CNS_TERM;
			switch (c) {
			case 0x27:
				break;
			case 0x01:
				/* BREAK */
				c = CNC_BREAK;
				break;
			case 0x02:
				/* NUL */
				c = 0;
				break;
			}
			/* FALLTHROUGH */
		default:
			/* Transition to the next state. */
#ifdef DEBUG
			if (!cold)
				printf("mag %d %x:%x\n", i, c, n);
#endif
			m[i] = ENCODE_STATE(c, n);
			break;
		}
	} 
	return (EINVAL);
}

#if NWSDISPLAY > 0

int	pcons_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, uint32_t *);
void	pcons_free_screen(void *, void *);
int	pcons_ioctl(void *, u_long, caddr_t, int, struct proc *);
int	pcons_mapchar(void *, int, unsigned int *);
paddr_t	pcons_mmap(void *, off_t, int);
int	pcons_putchar(void *, int, int, u_int, uint32_t);
int	pcons_show_screen(void *, void *, int, void (*)(void *, int, int),
	    void *);

struct wsdisplay_emulops pcons_emulops = {
	NULL,
	pcons_mapchar,
	pcons_putchar
};

struct wsscreen_descr pcons_stdscreen = {
	"dumb", 80, 34, &pcons_emulops, 12, 22, 0
};

const struct wsscreen_descr *pcons_scrlist[] = {
	&pcons_stdscreen
};

struct wsscreen_list pcons_screenlist = {
	1, pcons_scrlist
};

struct wsdisplay_accessops pcons_accessops = {
	.ioctl = pcons_ioctl,
	.mmap = pcons_mmap,
	.alloc_screen = pcons_alloc_screen,
	.free_screen = pcons_free_screen,
	.show_screen = pcons_show_screen
};

int
pcons_alloc_screen(void *v, const struct wsscreen_descr *typ, void **cookiep,
    int *curxp, int *curyp, uint32_t *attrp)
{
	struct pconssoftc *sc = v;
	int *rowp, *colp;
	int row, col;

	if (sc->sc_nscreens > 0)
		return (ENOMEM);

	row = col = 0;
	if (romgetcursoraddr(&rowp, &colp) == 0) {
		if (rowp != NULL)
			row = *rowp;
		if (colp != NULL)
			col = *colp;
	}

	*cookiep = v;
	*attrp = 0;
	*curxp = col;
	*curyp = row;

	sc->sc_nscreens++;
	return (0);
}

void
pcons_free_screen(void *v, void *cookie)
{
	struct pconssoftc *sc = v;

	sc->sc_nscreens--;
}

int
pcons_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
		break;
	default:
		return (-1);
	}

	return (0);
}

paddr_t
pcons_mmap(void *v, off_t off, int prot)
{
	return ((paddr_t)-1);
}

int
pcons_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *arg)
{
	return (0);
}

int
pcons_mapchar(void *v, int uc, unsigned int *idx)
{
	if ((uc & 0xff) == uc) {
		*idx = uc;
		return (1);
	} else {
		*idx = '?';
		return (0);
	}
}

int
pcons_putchar(void *v, int row, int col, u_int uc, uint32_t attr)
{
	u_char buf[1];
	int s;

	buf[0] = (u_char)uc;
	s = splhigh();
	OF_write(stdout, &buf, 1);
	splx(s);

	return 0;
}

void
pcons_wsdisplay_init(struct pconssoftc *sc)
{
	struct wsemuldisplaydev_attach_args waa;
	int *rowp, *colp;
	int options, row, col;

	row = col = 0;
	if (romgetcursoraddr(&rowp, &colp) == 0) {
		if (rowp != NULL)
			row = *rowp;
		if (colp != NULL)
			col = *colp;
	}

	options = OF_finddevice("/options");
	pcons_stdscreen.nrows = getpropint(options, "screen-#rows", 34);
	pcons_stdscreen.ncols = getpropint(options, "screen-#columns", 80);

	/*
	 * We claim console here, because we can only get there if stdin
	 * is a keyboard. However, the PROM could have been configured with
	 * stdin being a keyboard and stdout being a serial sink.
	 * But since this combination is not supported under OpenBSD at the
	 * moment, it is reasonably safe to attach a dumb display as console
	 * here.
	 */
	wsdisplay_cnattach(&pcons_stdscreen, sc, col, row, 0);

	waa.console = 1;
	waa.scrdata = &pcons_screenlist;
	waa.accessops = &pcons_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 1;

	config_found((struct device *)sc, &waa, wsemuldisplaydevprint);
}
#endif
