/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kbd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <sys/kbio.h>

#include <dev/evdev/input-event-codes.h>
#include <dev/kbd/kbdreg.h>

#define KBD_INDEX(dev)	dev2unit(dev)

#define KB_QSIZE	512
#define KB_BUFSIZE	64

typedef struct genkbd_softc {
	int		gkb_flags;	/* flag/status bits */
#define KB_ASLEEP	(1 << 0)
	struct selinfo	gkb_rsel;
	char		gkb_q[KB_QSIZE];		/* input queue */
	unsigned int	gkb_q_start;
	unsigned int	gkb_q_length;
} genkbd_softc_t;

static	SLIST_HEAD(, keyboard_driver) keyboard_drivers =
	SLIST_HEAD_INITIALIZER(keyboard_drivers);

SET_DECLARE(kbddriver_set, const keyboard_driver_t);

/* local arrays */

/*
 * We need at least one entry each in order to initialize a keyboard
 * for the kernel console.  The arrays will be increased dynamically
 * when necessary.
 */

static int		keyboards = 1;
static keyboard_t	*kbd_ini;
static keyboard_t	**keyboard = &kbd_ini;
static keyboard_switch_t *kbdsw_ini;
       keyboard_switch_t **kbdsw = &kbdsw_ini;

static int keymap_restrict_change;
static SYSCTL_NODE(_hw, OID_AUTO, kbd, CTLFLAG_RD, 0, "kbd");
SYSCTL_INT(_hw_kbd, OID_AUTO, keymap_restrict_change, CTLFLAG_RW,
    &keymap_restrict_change, 0, "restrict ability to change keymap");

#define ARRAY_DELTA	4

static int
kbd_realloc_array(void)
{
	keyboard_t **new_kbd;
	keyboard_switch_t **new_kbdsw;
	int newsize;
	int s;

	s = spltty();
	newsize = rounddown(keyboards + ARRAY_DELTA, ARRAY_DELTA);
	new_kbd = malloc(sizeof(*new_kbd)*newsize, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (new_kbd == NULL) {
		splx(s);
		return (ENOMEM);
	}
	new_kbdsw = malloc(sizeof(*new_kbdsw)*newsize, M_DEVBUF,
			    M_NOWAIT|M_ZERO);
	if (new_kbdsw == NULL) {
		free(new_kbd, M_DEVBUF);
		splx(s);
		return (ENOMEM);
	}
	bcopy(keyboard, new_kbd, sizeof(*keyboard)*keyboards);
	bcopy(kbdsw, new_kbdsw, sizeof(*kbdsw)*keyboards);
	if (keyboards > 1) {
		free(keyboard, M_DEVBUF);
		free(kbdsw, M_DEVBUF);
	}
	keyboard = new_kbd;
	kbdsw = new_kbdsw;
	keyboards = newsize;
	splx(s);

	if (bootverbose)
		printf("kbd: new array size %d\n", keyboards);

	return (0);
}

/*
 * Low-level keyboard driver functions
 * Keyboard subdrivers, such as the AT keyboard driver and the USB keyboard
 * driver, call these functions to initialize the keyboard_t structure
 * and register it to the virtual keyboard driver `kbd'.
 */

/* initialize the keyboard_t structure */
void
kbd_init_struct(keyboard_t *kbd, char *name, int type, int unit, int config,
		int port, int port_size)
{
	kbd->kb_flags = KB_NO_DEVICE;	/* device has not been found */
	kbd->kb_name = name;
	kbd->kb_type = type;
	kbd->kb_unit = unit;
	kbd->kb_config = config & ~KB_CONF_PROBE_ONLY;
	kbd->kb_led = 0;		/* unknown */
	kbd->kb_io_base = port;
	kbd->kb_io_size = port_size;
	kbd->kb_data = NULL;
	kbd->kb_keymap = NULL;
	kbd->kb_accentmap = NULL;
	kbd->kb_fkeytab = NULL;
	kbd->kb_fkeytab_size = 0;
	kbd->kb_delay1 = KB_DELAY1;	/* these values are advisory only */
	kbd->kb_delay2 = KB_DELAY2;
	kbd->kb_count = 0L;
	bzero(kbd->kb_lastact, sizeof(kbd->kb_lastact));
}

void
kbd_set_maps(keyboard_t *kbd, keymap_t *keymap, accentmap_t *accmap,
	     fkeytab_t *fkeymap, int fkeymap_size)
{
	kbd->kb_keymap = keymap;
	kbd->kb_accentmap = accmap;
	kbd->kb_fkeytab = fkeymap;
	kbd->kb_fkeytab_size = fkeymap_size;
}

/* declare a new keyboard driver */
int
kbd_add_driver(keyboard_driver_t *driver)
{
	if (SLIST_NEXT(driver, link))
		return (EINVAL);
	SLIST_INSERT_HEAD(&keyboard_drivers, driver, link);
	return (0);
}

int
kbd_delete_driver(keyboard_driver_t *driver)
{
	SLIST_REMOVE(&keyboard_drivers, driver, keyboard_driver, link);
	SLIST_NEXT(driver, link) = NULL;
	return (0);
}

/* register a keyboard and associate it with a function table */
int
kbd_register(keyboard_t *kbd)
{
	const keyboard_driver_t **list;
	const keyboard_driver_t *p;
	keyboard_t *mux;
	keyboard_info_t ki;
	int index;

	mux = kbd_get_keyboard(kbd_find_keyboard("kbdmux", -1));

	for (index = 0; index < keyboards; ++index) {
		if (keyboard[index] == NULL)
			break;
	}
	if (index >= keyboards) {
		if (kbd_realloc_array())
			return (-1);
	}

	kbd->kb_index = index;
	KBD_UNBUSY(kbd);
	KBD_VALID(kbd);
	kbd->kb_active = 0;	/* disabled until someone calls kbd_enable() */
	kbd->kb_token = NULL;
	kbd->kb_callback.kc_func = NULL;
	kbd->kb_callback.kc_arg = NULL;

	SLIST_FOREACH(p, &keyboard_drivers, link) {
		if (strcmp(p->name, kbd->kb_name) == 0) {
			keyboard[index] = kbd;
			kbdsw[index] = p->kbdsw;

			if (mux != NULL) {
				bzero(&ki, sizeof(ki));
				strcpy(ki.kb_name, kbd->kb_name);
				ki.kb_unit = kbd->kb_unit;

				(void)kbdd_ioctl(mux, KBADDKBD, (caddr_t) &ki);
			}

			return (index);
		}
	}
	SET_FOREACH(list, kbddriver_set) {
		p = *list;
		if (strcmp(p->name, kbd->kb_name) == 0) {
			keyboard[index] = kbd;
			kbdsw[index] = p->kbdsw;

			if (mux != NULL) {
				bzero(&ki, sizeof(ki));
				strcpy(ki.kb_name, kbd->kb_name);
				ki.kb_unit = kbd->kb_unit;

				(void)kbdd_ioctl(mux, KBADDKBD, (caddr_t) &ki);
			}

			return (index);
		}
	}

	return (-1);
}

int
kbd_unregister(keyboard_t *kbd)
{
	int error;
	int s;

	if ((kbd->kb_index < 0) || (kbd->kb_index >= keyboards))
		return (ENOENT);
	if (keyboard[kbd->kb_index] != kbd)
		return (ENOENT);

	s = spltty();
	if (KBD_IS_BUSY(kbd)) {
		error = (*kbd->kb_callback.kc_func)(kbd, KBDIO_UNLOADING,
		    kbd->kb_callback.kc_arg);
		if (error) {
			splx(s);
			return (error);
		}
		if (KBD_IS_BUSY(kbd)) {
			splx(s);
			return (EBUSY);
		}
	}
	KBD_INVALID(kbd);
	keyboard[kbd->kb_index] = NULL;
	kbdsw[kbd->kb_index] = NULL;

	splx(s);
	return (0);
}

/* find a function table by the driver name */
keyboard_switch_t *
kbd_get_switch(char *driver)
{
	const keyboard_driver_t **list;
	const keyboard_driver_t *p;

	SLIST_FOREACH(p, &keyboard_drivers, link) {
		if (strcmp(p->name, driver) == 0)
			return (p->kbdsw);
	}
	SET_FOREACH(list, kbddriver_set) {
		p = *list;
		if (strcmp(p->name, driver) == 0)
			return (p->kbdsw);
	}

	return (NULL);
}

/*
 * Keyboard client functions
 * Keyboard clients, such as the console driver `syscons' and the keyboard
 * cdev driver, use these functions to claim and release a keyboard for
 * exclusive use.
 */

/*
 * find the keyboard specified by a driver name and a unit number
 * starting at given index
 */
int
kbd_find_keyboard2(char *driver, int unit, int index)
{
	int i;

	if ((index < 0) || (index >= keyboards))
		return (-1);

	for (i = index; i < keyboards; ++i) {
		if (keyboard[i] == NULL)
			continue;
		if (!KBD_IS_VALID(keyboard[i]))
			continue;
		if (strcmp("*", driver) && strcmp(keyboard[i]->kb_name, driver))
			continue;
		if ((unit != -1) && (keyboard[i]->kb_unit != unit))
			continue;
		return (i);
	}

	return (-1);
}

/* find the keyboard specified by a driver name and a unit number */
int
kbd_find_keyboard(char *driver, int unit)
{
	return (kbd_find_keyboard2(driver, unit, 0));
}

/* allocate a keyboard */
int
kbd_allocate(char *driver, int unit, void *id, kbd_callback_func_t *func,
	     void *arg)
{
	int index;
	int s;

	if (func == NULL)
		return (-1);

	s = spltty();
	index = kbd_find_keyboard(driver, unit);
	if (index >= 0) {
		if (KBD_IS_BUSY(keyboard[index])) {
			splx(s);
			return (-1);
		}
		keyboard[index]->kb_token = id;
		KBD_BUSY(keyboard[index]);
		keyboard[index]->kb_callback.kc_func = func;
		keyboard[index]->kb_callback.kc_arg = arg;
		kbdd_clear_state(keyboard[index]);
	}
	splx(s);
	return (index);
}

int
kbd_release(keyboard_t *kbd, void *id)
{
	int error;
	int s;

	s = spltty();
	if (!KBD_IS_VALID(kbd) || !KBD_IS_BUSY(kbd)) {
		error = EINVAL;
	} else if (kbd->kb_token != id) {
		error = EPERM;
	} else {
		kbd->kb_token = NULL;
		KBD_UNBUSY(kbd);
		kbd->kb_callback.kc_func = NULL;
		kbd->kb_callback.kc_arg = NULL;
		kbdd_clear_state(kbd);
		error = 0;
	}
	splx(s);
	return (error);
}

int
kbd_change_callback(keyboard_t *kbd, void *id, kbd_callback_func_t *func,
		    void *arg)
{
	int error;
	int s;

	s = spltty();
	if (!KBD_IS_VALID(kbd) || !KBD_IS_BUSY(kbd)) {
		error = EINVAL;
	} else if (kbd->kb_token != id) {
		error = EPERM;
	} else if (func == NULL) {
		error = EINVAL;
	} else {
		kbd->kb_callback.kc_func = func;
		kbd->kb_callback.kc_arg = arg;
		error = 0;
	}
	splx(s);
	return (error);
}

/* get a keyboard structure */
keyboard_t *
kbd_get_keyboard(int index)
{
	if ((index < 0) || (index >= keyboards))
		return (NULL);
	if (keyboard[index] == NULL)
		return (NULL);
	if (!KBD_IS_VALID(keyboard[index]))
		return (NULL);
	return (keyboard[index]);
}

/*
 * The back door for the console driver; configure keyboards
 * This function is for the kernel console to initialize keyboards
 * at very early stage.
 */

int
kbd_configure(int flags)
{
	const keyboard_driver_t **list;
	const keyboard_driver_t *p;

	SLIST_FOREACH(p, &keyboard_drivers, link) {
		if (p->configure != NULL)
			(*p->configure)(flags);
	}
	SET_FOREACH(list, kbddriver_set) {
		p = *list;
		if (p->configure != NULL)
			(*p->configure)(flags);
	}

	return (0);
}

#ifdef KBD_INSTALL_CDEV

/*
 * Virtual keyboard cdev driver functions
 * The virtual keyboard driver dispatches driver functions to
 * appropriate subdrivers.
 */

#define KBD_UNIT(dev)	dev2unit(dev)

static d_open_t		genkbdopen;
static d_close_t	genkbdclose;
static d_read_t		genkbdread;
static d_write_t	genkbdwrite;
static d_ioctl_t	genkbdioctl;
static d_poll_t		genkbdpoll;


static struct cdevsw kbd_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	genkbdopen,
	.d_close =	genkbdclose,
	.d_read =	genkbdread,
	.d_write =	genkbdwrite,
	.d_ioctl =	genkbdioctl,
	.d_poll =	genkbdpoll,
	.d_name =	"kbd",
};

int
kbd_attach(keyboard_t *kbd)
{

	if (kbd->kb_index >= keyboards)
		return (EINVAL);
	if (keyboard[kbd->kb_index] != kbd)
		return (EINVAL);

	kbd->kb_dev = make_dev(&kbd_cdevsw, kbd->kb_index, UID_ROOT, GID_WHEEL,
	    0600, "%s%r", kbd->kb_name, kbd->kb_unit);
	make_dev_alias(kbd->kb_dev, "kbd%r", kbd->kb_index);
	kbd->kb_dev->si_drv1 = malloc(sizeof(genkbd_softc_t), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	printf("kbd%d at %s%d\n", kbd->kb_index, kbd->kb_name, kbd->kb_unit);
	return (0);
}

int
kbd_detach(keyboard_t *kbd)
{

	if (kbd->kb_index >= keyboards)
		return (EINVAL);
	if (keyboard[kbd->kb_index] != kbd)
		return (EINVAL);

	free(kbd->kb_dev->si_drv1, M_DEVBUF);
	destroy_dev(kbd->kb_dev);

	return (0);
}

/*
 * Generic keyboard cdev driver functions
 * Keyboard subdrivers may call these functions to implement common
 * driver functions.
 */

static void
genkbd_putc(genkbd_softc_t *sc, char c)
{
	unsigned int p;

	if (sc->gkb_q_length == KB_QSIZE)
		return;

	p = (sc->gkb_q_start + sc->gkb_q_length) % KB_QSIZE;
	sc->gkb_q[p] = c;
	sc->gkb_q_length++;
}

static size_t
genkbd_getc(genkbd_softc_t *sc, char *buf, size_t len)
{

	/* Determine copy size. */
	if (sc->gkb_q_length == 0)
		return (0);
	if (len >= sc->gkb_q_length)
		len = sc->gkb_q_length;
	if (len >= KB_QSIZE - sc->gkb_q_start)
		len = KB_QSIZE - sc->gkb_q_start;

	/* Copy out data and progress offset. */
	memcpy(buf, sc->gkb_q + sc->gkb_q_start, len);
	sc->gkb_q_start = (sc->gkb_q_start + len) % KB_QSIZE;
	sc->gkb_q_length -= len;

	return (len);
}

static kbd_callback_func_t genkbd_event;

static int
genkbdopen(struct cdev *dev, int mode, int flag, struct thread *td)
{
	keyboard_t *kbd;
	genkbd_softc_t *sc;
	int s;
	int i;

	s = spltty();
	sc = dev->si_drv1;
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		splx(s);
		return (ENXIO);
	}
	i = kbd_allocate(kbd->kb_name, kbd->kb_unit, sc,
	    genkbd_event, (void *)sc);
	if (i < 0) {
		splx(s);
		return (EBUSY);
	}
	/* assert(i == kbd->kb_index) */
	/* assert(kbd == kbd_get_keyboard(i)) */

	/*
	 * NOTE: even when we have successfully claimed a keyboard,
	 * the device may still be missing (!KBD_HAS_DEVICE(kbd)).
	 */

	sc->gkb_q_length = 0;
	splx(s);

	return (0);
}

static int
genkbdclose(struct cdev *dev, int mode, int flag, struct thread *td)
{
	keyboard_t *kbd;
	genkbd_softc_t *sc;
	int s;

	/*
	 * NOTE: the device may have already become invalid.
	 * kbd == NULL || !KBD_IS_VALID(kbd)
	 */
	s = spltty();
	sc = dev->si_drv1;
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		/* XXX: we shall be forgiving and don't report error... */
	} else {
		kbd_release(kbd, (void *)sc);
	}
	splx(s);
	return (0);
}

static int
genkbdread(struct cdev *dev, struct uio *uio, int flag)
{
	keyboard_t *kbd;
	genkbd_softc_t *sc;
	u_char buffer[KB_BUFSIZE];
	int len;
	int error;
	int s;

	/* wait for input */
	s = spltty();
	sc = dev->si_drv1;
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		splx(s);
		return (ENXIO);
	}
	while (sc->gkb_q_length == 0) {
		if (flag & O_NONBLOCK) {
			splx(s);
			return (EWOULDBLOCK);
		}
		sc->gkb_flags |= KB_ASLEEP;
		error = tsleep(sc, PZERO | PCATCH, "kbdrea", 0);
		kbd = kbd_get_keyboard(KBD_INDEX(dev));
		if ((kbd == NULL) || !KBD_IS_VALID(kbd)) {
			splx(s);
			return (ENXIO);	/* our keyboard has gone... */
		}
		if (error) {
			sc->gkb_flags &= ~KB_ASLEEP;
			splx(s);
			return (error);
		}
	}
	splx(s);

	/* copy as much input as possible */
	error = 0;
	while (uio->uio_resid > 0) {
		len = imin(uio->uio_resid, sizeof(buffer));
		len = genkbd_getc(sc, buffer, len);
		if (len <= 0)
			break;
		error = uiomove(buffer, len, uio);
		if (error)
			break;
	}

	return (error);
}

static int
genkbdwrite(struct cdev *dev, struct uio *uio, int flag)
{
	keyboard_t *kbd;

	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((kbd == NULL) || !KBD_IS_VALID(kbd))
		return (ENXIO);
	return (ENODEV);
}

static int
genkbdioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct thread *td)
{
	keyboard_t *kbd;
	int error;

	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((kbd == NULL) || !KBD_IS_VALID(kbd))
		return (ENXIO);
	error = kbdd_ioctl(kbd, cmd, arg);
	if (error == ENOIOCTL)
		error = ENODEV;
	return (error);
}

static int
genkbdpoll(struct cdev *dev, int events, struct thread *td)
{
	keyboard_t *kbd;
	genkbd_softc_t *sc;
	int revents;
	int s;

	revents = 0;
	s = spltty();
	sc = dev->si_drv1;
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		revents =  POLLHUP;	/* the keyboard has gone */
	} else if (events & (POLLIN | POLLRDNORM)) {
		if (sc->gkb_q_length > 0)
			revents = events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &sc->gkb_rsel);
	}
	splx(s);
	return (revents);
}

static int
genkbd_event(keyboard_t *kbd, int event, void *arg)
{
	genkbd_softc_t *sc;
	size_t len;
	u_char *cp;
	int mode;
	u_int c;

	/* assert(KBD_IS_VALID(kbd)) */
	sc = (genkbd_softc_t *)arg;

	switch (event) {
	case KBDIO_KEYINPUT:
		break;
	case KBDIO_UNLOADING:
		/* the keyboard is going... */
		kbd_release(kbd, (void *)sc);
		if (sc->gkb_flags & KB_ASLEEP) {
			sc->gkb_flags &= ~KB_ASLEEP;
			wakeup(sc);
		}
		selwakeuppri(&sc->gkb_rsel, PZERO);
		return (0);
	default:
		return (EINVAL);
	}

	/* obtain the current key input mode */
	if (kbdd_ioctl(kbd, KDGKBMODE, (caddr_t)&mode))
		mode = K_XLATE;

	/* read all pending input */
	while (kbdd_check_char(kbd)) {
		c = kbdd_read_char(kbd, FALSE);
		if (c == NOKEY)
			continue;
		if (c == ERRKEY)	/* XXX: ring bell? */
			continue;
		if (!KBD_IS_BUSY(kbd))
			/* the device is not open, discard the input */
			continue;

		/* store the byte as is for K_RAW and K_CODE modes */
		if (mode != K_XLATE) {
			genkbd_putc(sc, KEYCHAR(c));
			continue;
		}

		/* K_XLATE */
		if (c & RELKEY)	/* key release is ignored */
			continue;

		/* process special keys; most of them are just ignored... */
		if (c & SPCLKEY) {
			switch (KEYCHAR(c)) {
			default:
				/* ignore them... */
				continue;
			case BTAB:	/* a backtab: ESC [ Z */
				genkbd_putc(sc, 0x1b);
				genkbd_putc(sc, '[');
				genkbd_putc(sc, 'Z');
				continue;
			}
		}

		/* normal chars, normal chars with the META, function keys */
		switch (KEYFLAGS(c)) {
		case 0:			/* a normal char */
			genkbd_putc(sc, KEYCHAR(c));
			break;
		case MKEY:		/* the META flag: prepend ESC */
			genkbd_putc(sc, 0x1b);
			genkbd_putc(sc, KEYCHAR(c));
			break;
		case FKEY | SPCLKEY:	/* a function key, return string */
			cp = kbdd_get_fkeystr(kbd, KEYCHAR(c), &len);
			if (cp != NULL) {
				while (len-- >  0)
					genkbd_putc(sc, *cp++);
			}
			break;
		}
	}

	/* wake up sleeping/polling processes */
	if (sc->gkb_q_length > 0) {
		if (sc->gkb_flags & KB_ASLEEP) {
			sc->gkb_flags &= ~KB_ASLEEP;
			wakeup(sc);
		}
		selwakeuppri(&sc->gkb_rsel, PZERO);
	}

	return (0);
}

#endif /* KBD_INSTALL_CDEV */

/*
 * Generic low-level keyboard functions
 * The low-level functions in the keyboard subdriver may use these
 * functions.
 */

#ifndef KBD_DISABLE_KEYMAP_LOAD
static int key_change_ok(struct keyent_t *, struct keyent_t *, struct thread *);
static int keymap_change_ok(keymap_t *, keymap_t *, struct thread *);
static int accent_change_ok(accentmap_t *, accentmap_t *, struct thread *);
static int fkey_change_ok(fkeytab_t *, fkeyarg_t *, struct thread *);
#endif

int
genkbd_commonioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	keymap_t *mapp;
	okeymap_t *omapp;
	keyarg_t *keyp;
	fkeyarg_t *fkeyp;
	int s;
	int i, j;
	int error;

	s = spltty();
	switch (cmd) {

	case KDGKBINFO:		/* get keyboard information */
		((keyboard_info_t *)arg)->kb_index = kbd->kb_index;
		i = imin(strlen(kbd->kb_name) + 1,
		    sizeof(((keyboard_info_t *)arg)->kb_name));
		bcopy(kbd->kb_name, ((keyboard_info_t *)arg)->kb_name, i);
		((keyboard_info_t *)arg)->kb_unit = kbd->kb_unit;
		((keyboard_info_t *)arg)->kb_type = kbd->kb_type;
		((keyboard_info_t *)arg)->kb_config = kbd->kb_config;
		((keyboard_info_t *)arg)->kb_flags = kbd->kb_flags;
		break;

	case KDGKBTYPE:		/* get keyboard type */
		*(int *)arg = kbd->kb_type;
		break;

	case KDGETREPEAT:	/* get keyboard repeat rate */
		((int *)arg)[0] = kbd->kb_delay1;
		((int *)arg)[1] = kbd->kb_delay2;
		break;

	case GIO_KEYMAP:	/* get keyboard translation table */
		error = copyout(kbd->kb_keymap, *(void **)arg,
		    sizeof(keymap_t));
		splx(s);
		return (error);
	case OGIO_KEYMAP:	/* get keyboard translation table (compat) */
		mapp = kbd->kb_keymap;
		omapp = (okeymap_t *)arg;
		omapp->n_keys = mapp->n_keys;
		for (i = 0; i < NUM_KEYS; i++) {
			for (j = 0; j < NUM_STATES; j++)
				omapp->key[i].map[j] =
				    mapp->key[i].map[j];
			omapp->key[i].spcl = mapp->key[i].spcl;
			omapp->key[i].flgs = mapp->key[i].flgs;
		}
		break;
	case PIO_KEYMAP:	/* set keyboard translation table */
	case OPIO_KEYMAP:	/* set keyboard translation table (compat) */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		mapp = malloc(sizeof *mapp, M_TEMP, M_WAITOK);
		if (cmd == OPIO_KEYMAP) {
			omapp = (okeymap_t *)arg;
			mapp->n_keys = omapp->n_keys;
			for (i = 0; i < NUM_KEYS; i++) {
				for (j = 0; j < NUM_STATES; j++)
					mapp->key[i].map[j] =
					    omapp->key[i].map[j];
				mapp->key[i].spcl = omapp->key[i].spcl;
				mapp->key[i].flgs = omapp->key[i].flgs;
			}
		} else {
			error = copyin(*(void **)arg, mapp, sizeof *mapp);
			if (error != 0) {
				splx(s);
				free(mapp, M_TEMP);
				return (error);
			}
		}

		error = keymap_change_ok(kbd->kb_keymap, mapp, curthread);
		if (error != 0) {
			splx(s);
			free(mapp, M_TEMP);
			return (error);
		}
		bzero(kbd->kb_accentmap, sizeof(*kbd->kb_accentmap));
		bcopy(mapp, kbd->kb_keymap, sizeof(*kbd->kb_keymap));
		free(mapp, M_TEMP);
		break;
#else
		splx(s);
		return (ENODEV);
#endif

	case GIO_KEYMAPENT:	/* get keyboard translation table entry */
		keyp = (keyarg_t *)arg;
		if (keyp->keynum >= sizeof(kbd->kb_keymap->key) /
		    sizeof(kbd->kb_keymap->key[0])) {
			splx(s);
			return (EINVAL);
		}
		bcopy(&kbd->kb_keymap->key[keyp->keynum], &keyp->key,
		    sizeof(keyp->key));
		break;
	case PIO_KEYMAPENT:	/* set keyboard translation table entry */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		keyp = (keyarg_t *)arg;
		if (keyp->keynum >= sizeof(kbd->kb_keymap->key) /
		    sizeof(kbd->kb_keymap->key[0])) {
			splx(s);
			return (EINVAL);
		}
		error = key_change_ok(&kbd->kb_keymap->key[keyp->keynum],
		    &keyp->key, curthread);
		if (error != 0) {
			splx(s);
			return (error);
		}
		bcopy(&keyp->key, &kbd->kb_keymap->key[keyp->keynum],
		    sizeof(keyp->key));
		break;
#else
		splx(s);
		return (ENODEV);
#endif

	case GIO_DEADKEYMAP:	/* get accent key translation table */
		bcopy(kbd->kb_accentmap, arg, sizeof(*kbd->kb_accentmap));
		break;
	case PIO_DEADKEYMAP:	/* set accent key translation table */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		error = accent_change_ok(kbd->kb_accentmap,
		    (accentmap_t *)arg, curthread);
		if (error != 0) {
			splx(s);
			return (error);
		}
		bcopy(arg, kbd->kb_accentmap, sizeof(*kbd->kb_accentmap));
		break;
#else
		splx(s);
		return (ENODEV);
#endif

	case GETFKEY:		/* get functionkey string */
		fkeyp = (fkeyarg_t *)arg;
		if (fkeyp->keynum >= kbd->kb_fkeytab_size) {
			splx(s);
			return (EINVAL);
		}
		bcopy(kbd->kb_fkeytab[fkeyp->keynum].str, fkeyp->keydef,
		    kbd->kb_fkeytab[fkeyp->keynum].len);
		fkeyp->flen = kbd->kb_fkeytab[fkeyp->keynum].len;
		break;
	case SETFKEY:		/* set functionkey string */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		fkeyp = (fkeyarg_t *)arg;
		if (fkeyp->keynum >= kbd->kb_fkeytab_size) {
			splx(s);
			return (EINVAL);
		}
		error = fkey_change_ok(&kbd->kb_fkeytab[fkeyp->keynum],
		    fkeyp, curthread);
		if (error != 0) {
			splx(s);
			return (error);
		}
		kbd->kb_fkeytab[fkeyp->keynum].len = min(fkeyp->flen, MAXFK);
		bcopy(fkeyp->keydef, kbd->kb_fkeytab[fkeyp->keynum].str,
		    kbd->kb_fkeytab[fkeyp->keynum].len);
		break;
#else
		splx(s);
		return (ENODEV);
#endif

	default:
		splx(s);
		return (ENOIOCTL);
	}

	splx(s);
	return (0);
}

#ifndef KBD_DISABLE_KEYMAP_LOAD
#define RESTRICTED_KEY(key, i) \
	((key->spcl & (0x80 >> i)) && \
		(key->map[i] == RBT || key->map[i] == SUSP || \
		 key->map[i] == STBY || key->map[i] == DBG || \
		 key->map[i] == PNC || key->map[i] == HALT || \
		 key->map[i] == PDWN))

static int
key_change_ok(struct keyent_t *oldkey, struct keyent_t *newkey, struct thread *td)
{
	int i;

	/* Low keymap_restrict_change means any changes are OK. */
	if (keymap_restrict_change <= 0)
		return (0);

	/* High keymap_restrict_change means only root can change the keymap. */
	if (keymap_restrict_change >= 2) {
		for (i = 0; i < NUM_STATES; i++)
			if (oldkey->map[i] != newkey->map[i])
				return priv_check(td, PRIV_KEYBOARD);
		if (oldkey->spcl != newkey->spcl)
			return priv_check(td, PRIV_KEYBOARD);
		if (oldkey->flgs != newkey->flgs)
			return priv_check(td, PRIV_KEYBOARD);
		return (0);
	}

	/* Otherwise we have to see if any special keys are being changed. */
	for (i = 0; i < NUM_STATES; i++) {
		/*
		 * If either the oldkey or the newkey action is restricted
		 * then we must make sure that the action doesn't change.
		 */
		if (!RESTRICTED_KEY(oldkey, i) && !RESTRICTED_KEY(newkey, i))
			continue;
		if ((oldkey->spcl & (0x80 >> i)) == (newkey->spcl & (0x80 >> i))
		    && oldkey->map[i] == newkey->map[i])
			continue;
		return priv_check(td, PRIV_KEYBOARD);
	}

	return (0);
}

static int
keymap_change_ok(keymap_t *oldmap, keymap_t *newmap, struct thread *td)
{
	int keycode, error;

	for (keycode = 0; keycode < NUM_KEYS; keycode++) {
		if ((error = key_change_ok(&oldmap->key[keycode],
		    &newmap->key[keycode], td)) != 0)
			return (error);
	}
	return (0);
}

static int
accent_change_ok(accentmap_t *oldmap, accentmap_t *newmap, struct thread *td)
{
	struct acc_t *oldacc, *newacc;
	int accent, i;

	if (keymap_restrict_change <= 2)
		return (0);

	if (oldmap->n_accs != newmap->n_accs)
		return priv_check(td, PRIV_KEYBOARD);

	for (accent = 0; accent < oldmap->n_accs; accent++) {
		oldacc = &oldmap->acc[accent];
		newacc = &newmap->acc[accent];
		if (oldacc->accchar != newacc->accchar)
			return priv_check(td, PRIV_KEYBOARD);
		for (i = 0; i < NUM_ACCENTCHARS; ++i) {
			if (oldacc->map[i][0] != newacc->map[i][0])
				return priv_check(td, PRIV_KEYBOARD);
			if (oldacc->map[i][0] == 0)	/* end of table */
				break;
			if (oldacc->map[i][1] != newacc->map[i][1])
				return priv_check(td, PRIV_KEYBOARD);
		}
	}

	return (0);
}

static int
fkey_change_ok(fkeytab_t *oldkey, fkeyarg_t *newkey, struct thread *td)
{
	if (keymap_restrict_change <= 3)
		return (0);

	if (oldkey->len != newkey->flen ||
	    bcmp(oldkey->str, newkey->keydef, oldkey->len) != 0)
		return priv_check(td, PRIV_KEYBOARD);

	return (0);
}
#endif

/* get a pointer to the string associated with the given function key */
u_char *
genkbd_get_fkeystr(keyboard_t *kbd, int fkey, size_t *len)
{
	if (kbd == NULL)
		return (NULL);
	fkey -= F_FN;
	if (fkey > kbd->kb_fkeytab_size)
		return (NULL);
	*len = kbd->kb_fkeytab[fkey].len;
	return (kbd->kb_fkeytab[fkey].str);
}

/* diagnostic dump */
static char *
get_kbd_type_name(int type)
{
	static struct {
		int type;
		char *name;
	} name_table[] = {
		{ KB_84,	"AT 84" },
		{ KB_101,	"AT 101/102" },
		{ KB_OTHER,	"generic" },
	};
	int i;

	for (i = 0; i < nitems(name_table); ++i) {
		if (type == name_table[i].type)
			return (name_table[i].name);
	}
	return ("unknown");
}

void
genkbd_diag(keyboard_t *kbd, int level)
{
	if (level > 0) {
		printf("kbd%d: %s%d, %s (%d), config:0x%x, flags:0x%x",
		    kbd->kb_index, kbd->kb_name, kbd->kb_unit,
		    get_kbd_type_name(kbd->kb_type), kbd->kb_type,
		    kbd->kb_config, kbd->kb_flags);
		if (kbd->kb_io_base > 0)
			printf(", port:0x%x-0x%x", kbd->kb_io_base,
			    kbd->kb_io_base + kbd->kb_io_size - 1);
		printf("\n");
	}
}

#define set_lockkey_state(k, s, l)				\
	if (!((s) & l ## DOWN)) {				\
		int i;						\
		(s) |= l ## DOWN;				\
		(s) ^= l ## ED;					\
		i = (s) & LOCK_MASK;				\
		(void)kbdd_ioctl((k), KDSETLED, (caddr_t)&i);	\
	}

static u_int
save_accent_key(keyboard_t *kbd, u_int key, int *accents)
{
	int i;

	/* make an index into the accent map */
	i = key - F_ACC + 1;
	if ((i > kbd->kb_accentmap->n_accs)
	    || (kbd->kb_accentmap->acc[i - 1].accchar == 0)) {
		/* the index is out of range or pointing to an empty entry */
		*accents = 0;
		return (ERRKEY);
	}

	/*
	 * If the same accent key has been hit twice, produce the accent
	 * char itself.
	 */
	if (i == *accents) {
		key = kbd->kb_accentmap->acc[i - 1].accchar;
		*accents = 0;
		return (key);
	}

	/* remember the index and wait for the next key  */
	*accents = i;
	return (NOKEY);
}

static u_int
make_accent_char(keyboard_t *kbd, u_int ch, int *accents)
{
	struct acc_t *acc;
	int i;

	acc = &kbd->kb_accentmap->acc[*accents - 1];
	*accents = 0;

	/*
	 * If the accent key is followed by the space key,
	 * produce the accent char itself.
	 */
	if (ch == ' ')
		return (acc->accchar);

	/* scan the accent map */
	for (i = 0; i < NUM_ACCENTCHARS; ++i) {
		if (acc->map[i][0] == 0)	/* end of table */
			break;
		if (acc->map[i][0] == ch)
			return (acc->map[i][1]);
	}
	/* this char cannot be accented... */
	return (ERRKEY);
}

int
genkbd_keyaction(keyboard_t *kbd, int keycode, int up, int *shiftstate,
		 int *accents)
{
	struct keyent_t *key;
	int state = *shiftstate;
	int action;
	int f;
	int i;

	i = keycode;
	f = state & (AGRS | ALKED);
	if ((f == AGRS1) || (f == AGRS2) || (f == ALKED))
		i += ALTGR_OFFSET;
	key = &kbd->kb_keymap->key[i];
	i = ((state & SHIFTS) ? 1 : 0)
	    | ((state & CTLS) ? 2 : 0)
	    | ((state & ALTS) ? 4 : 0);
	if (((key->flgs & FLAG_LOCK_C) && (state & CLKED))
		|| ((key->flgs & FLAG_LOCK_N) && (state & NLKED)) )
		i ^= 1;

	if (up) {	/* break: key released */
		action = kbd->kb_lastact[keycode];
		kbd->kb_lastact[keycode] = NOP;
		switch (action) {
		case LSHA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = LSH;
			/* FALL THROUGH */
		case LSH:
			state &= ~SHIFTS1;
			break;
		case RSHA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = RSH;
			/* FALL THROUGH */
		case RSH:
			state &= ~SHIFTS2;
			break;
		case LCTRA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = LCTR;
			/* FALL THROUGH */
		case LCTR:
			state &= ~CTLS1;
			break;
		case RCTRA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = RCTR;
			/* FALL THROUGH */
		case RCTR:
			state &= ~CTLS2;
			break;
		case LALTA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = LALT;
			/* FALL THROUGH */
		case LALT:
			state &= ~ALTS1;
			break;
		case RALTA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = RALT;
			/* FALL THROUGH */
		case RALT:
			state &= ~ALTS2;
			break;
		case ASH:
			state &= ~AGRS1;
			break;
		case META:
			state &= ~METAS1;
			break;
		case NLK:
			state &= ~NLKDOWN;
			break;
		case CLK:
			state &= ~CLKDOWN;
			break;
		case SLK:
			state &= ~SLKDOWN;
			break;
		case ALK:
			state &= ~ALKDOWN;
			break;
		case NOP:
			/* release events of regular keys are not reported */
			*shiftstate &= ~SHIFTAON;
			return (NOKEY);
		}
		*shiftstate = state & ~SHIFTAON;
		return (SPCLKEY | RELKEY | action);
	} else {	/* make: key pressed */
		action = key->map[i];
		state &= ~SHIFTAON;
		if (key->spcl & (0x80 >> i)) {
			/* special keys */
			if (kbd->kb_lastact[keycode] == NOP)
				kbd->kb_lastact[keycode] = action;
			if (kbd->kb_lastact[keycode] != action)
				action = NOP;
			switch (action) {
			/* LOCKING KEYS */
			case NLK:
				set_lockkey_state(kbd, state, NLK);
				break;
			case CLK:
				set_lockkey_state(kbd, state, CLK);
				break;
			case SLK:
				set_lockkey_state(kbd, state, SLK);
				break;
			case ALK:
				set_lockkey_state(kbd, state, ALK);
				break;
			/* NON-LOCKING KEYS */
			case SPSC: case RBT:  case SUSP: case STBY:
			case DBG:  case NEXT: case PREV: case PNC:
			case HALT: case PDWN:
				*accents = 0;
				break;
			case BTAB:
				*accents = 0;
				action |= BKEY;
				break;
			case LSHA:
				state |= SHIFTAON;
				action = LSH;
				/* FALL THROUGH */
			case LSH:
				state |= SHIFTS1;
				break;
			case RSHA:
				state |= SHIFTAON;
				action = RSH;
				/* FALL THROUGH */
			case RSH:
				state |= SHIFTS2;
				break;
			case LCTRA:
				state |= SHIFTAON;
				action = LCTR;
				/* FALL THROUGH */
			case LCTR:
				state |= CTLS1;
				break;
			case RCTRA:
				state |= SHIFTAON;
				action = RCTR;
				/* FALL THROUGH */
			case RCTR:
				state |= CTLS2;
				break;
			case LALTA:
				state |= SHIFTAON;
				action = LALT;
				/* FALL THROUGH */
			case LALT:
				state |= ALTS1;
				break;
			case RALTA:
				state |= SHIFTAON;
				action = RALT;
				/* FALL THROUGH */
			case RALT:
				state |= ALTS2;
				break;
			case ASH:
				state |= AGRS1;
				break;
			case META:
				state |= METAS1;
				break;
			case NOP:
				*shiftstate = state;
				return (NOKEY);
			default:
				/* is this an accent (dead) key? */
				*shiftstate = state;
				if (action >= F_ACC && action <= L_ACC) {
					action = save_accent_key(kbd, action,
								 accents);
					switch (action) {
					case NOKEY:
					case ERRKEY:
						return (action);
					default:
						if (state & METAS)
							return (action | MKEY);
						else
							return (action);
					}
					/* NOT REACHED */
				}
				/* other special keys */
				if (*accents > 0) {
					*accents = 0;
					return (ERRKEY);
				}
				if (action >= F_FN && action <= L_FN)
					action |= FKEY;
				/* XXX: return fkey string for the FKEY? */
				return (SPCLKEY | action);
			}
			*shiftstate = state;
			return (SPCLKEY | action);
		} else {
			/* regular keys */
			kbd->kb_lastact[keycode] = NOP;
			*shiftstate = state;
			if (*accents > 0) {
				/* make an accented char */
				action = make_accent_char(kbd, action, accents);
				if (action == ERRKEY)
					return (action);
			}
			if (state & METAS)
				action |= MKEY;
			return (action);
		}
	}
	/* NOT REACHED */
}

void
kbd_ev_event(keyboard_t *kbd, uint16_t type, uint16_t code, int32_t value)
{
	int delay[2], led = 0, leds, oleds;

	if (type == EV_LED) {
		leds = oleds = KBD_LED_VAL(kbd);
		switch (code) {
		case LED_CAPSL:
			led = CLKED;
			break;
		case LED_NUML:
			led = NLKED;
			break;
		case LED_SCROLLL:
			led = SLKED;
			break;
		}

		if (value)
			leds |= led;
		else
			leds &= ~led;

		if (leds != oleds)
			kbdd_ioctl(kbd, KDSETLED, (caddr_t)&leds);

	} else if (type == EV_REP && code == REP_DELAY) {
		delay[0] = value;
		delay[1] = kbd->kb_delay2;
		kbdd_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);
	} else if (type == EV_REP && code == REP_PERIOD) {
		delay[0] = kbd->kb_delay1;
		delay[1] = value;
		kbdd_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);
	}
}
