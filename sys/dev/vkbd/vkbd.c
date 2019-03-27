/*
 * vkbd.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: vkbd.c,v 1.20 2004/11/15 23:53:30 max Exp $
 * $FreeBSD$
 */

#include "opt_kbd.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kbio.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/selinfo.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>
#include <dev/kbd/kbdreg.h>
#include <dev/kbd/kbdtables.h>
#include <dev/vkbd/vkbd_var.h>

#define DEVICE_NAME	"vkbdctl"
#define KEYBOARD_NAME	"vkbd"

MALLOC_DECLARE(M_VKBD);
MALLOC_DEFINE(M_VKBD, KEYBOARD_NAME, "Virtual AT keyboard");

/*****************************************************************************
 *****************************************************************************
 **                             Keyboard state
 *****************************************************************************
 *****************************************************************************/

/*
 * XXX
 * For now rely on Giant mutex to protect our data structures.
 * Just like the rest of keyboard drivers and syscons(4) do.
 */

#if 0 /* not yet */
#define VKBD_LOCK_DECL		struct mtx ks_lock
#define VKBD_LOCK_INIT(s)	mtx_init(&(s)->ks_lock, "vkbd_lock", NULL, MTX_DEF|MTX_RECURSE)
#define VKBD_LOCK_DESTROY(s)	mtx_destroy(&(s)->ks_lock)
#define VKBD_LOCK(s)		mtx_lock(&(s)->ks_lock)
#define VKBD_UNLOCK(s)		mtx_unlock(&(s)->ks_lock)
#define VKBD_LOCK_ASSERT(s, w)	mtx_assert(&(s)->ks_lock, w)
#define VKBD_SLEEP(s, f, d, t) \
	msleep(&(s)->f, &(s)->ks_lock, PCATCH | (PZERO + 1), d, t)
#else
#define VKBD_LOCK_DECL
#define VKBD_LOCK_INIT(s)
#define VKBD_LOCK_DESTROY(s)
#define VKBD_LOCK(s)
#define VKBD_UNLOCK(s)
#define VKBD_LOCK_ASSERT(s, w)
#define VKBD_SLEEP(s, f, d, t)	tsleep(&(s)->f, PCATCH | (PZERO + 1), d, t)
#endif

#define VKBD_KEYBOARD(d) \
	kbd_get_keyboard(kbd_find_keyboard(KEYBOARD_NAME, dev2unit(d)))

/* vkbd queue */
struct vkbd_queue
{
	int		q[VKBD_Q_SIZE]; /* queue */
	int		head;		/* index of the first code */
	int		tail;		/* index of the last code */
	int		cc;		/* number of codes in queue */
};

typedef struct vkbd_queue	vkbd_queue_t;

/* vkbd state */
struct vkbd_state
{
	struct cdev	*ks_dev;	/* control device */

	struct selinfo	 ks_rsel;	/* select(2) */
	struct selinfo	 ks_wsel;

	vkbd_queue_t	 ks_inq;	/* input key codes queue */
	struct task	 ks_task;	/* interrupt task */

	int		 ks_flags;	/* flags */
#define OPEN		(1 << 0)	/* control device is open */
#define COMPOSE		(1 << 1)	/* compose flag */
#define STATUS		(1 << 2)	/* status has changed */
#define TASK		(1 << 3)	/* interrupt task queued */
#define READ		(1 << 4)	/* read pending */
#define WRITE		(1 << 5)	/* write pending */

	int		 ks_mode;	/* K_XLATE, K_RAW, K_CODE */
	int		 ks_polling;	/* polling flag */
	int		 ks_state;	/* shift/lock key state */
	int		 ks_accents;	/* accent key index (> 0) */
	u_int		 ks_composed_char; /* composed char code */
	u_char		 ks_prefix;	/* AT scan code prefix */

	VKBD_LOCK_DECL;
};

typedef struct vkbd_state	vkbd_state_t;

/*****************************************************************************
 *****************************************************************************
 **                             Character device
 *****************************************************************************
 *****************************************************************************/

static void		vkbd_dev_clone(void *, struct ucred *, char *, int,
			    struct cdev **);
static d_open_t		vkbd_dev_open;
static d_close_t	vkbd_dev_close;
static d_read_t		vkbd_dev_read;
static d_write_t	vkbd_dev_write;
static d_ioctl_t	vkbd_dev_ioctl;
static d_poll_t		vkbd_dev_poll;
static void		vkbd_dev_intr(void *, int);
static void		vkbd_status_changed(vkbd_state_t *);
static int		vkbd_data_ready(vkbd_state_t *);
static int		vkbd_data_read(vkbd_state_t *, int);

static struct cdevsw	vkbd_dev_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT | D_NEEDMINOR,
	.d_open =	vkbd_dev_open,
	.d_close =	vkbd_dev_close,
	.d_read =	vkbd_dev_read,
	.d_write =	vkbd_dev_write,
	.d_ioctl =	vkbd_dev_ioctl,
	.d_poll =	vkbd_dev_poll,
	.d_name =	DEVICE_NAME,
};

static struct clonedevs	*vkbd_dev_clones = NULL;

/* Clone device */
static void
vkbd_dev_clone(void *arg, struct ucred *cred, char *name, int namelen,
    struct cdev **dev)
{
	int	unit;

	if (*dev != NULL)
		return;

	if (strcmp(name, DEVICE_NAME) == 0)
		unit = -1;
	else if (dev_stdclone(name, NULL, DEVICE_NAME, &unit) != 1)
		return; /* don't recognize the name */

	/* find any existing device, or allocate new unit number */
	if (clone_create(&vkbd_dev_clones, &vkbd_dev_cdevsw, &unit, dev, 0))
		*dev = make_dev_credf(MAKEDEV_REF, &vkbd_dev_cdevsw, unit,
			cred, UID_ROOT, GID_WHEEL, 0600, DEVICE_NAME "%d",
			unit);
}

/* Open device */
static int
vkbd_dev_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	int			 unit = dev2unit(dev), error;
	keyboard_switch_t	*sw = NULL;
	keyboard_t		*kbd = NULL;
	vkbd_state_t		*state = (vkbd_state_t *) dev->si_drv1;

	/* XXX FIXME: dev->si_drv1 locking */
	if (state == NULL) {
		if ((sw = kbd_get_switch(KEYBOARD_NAME)) == NULL)
			return (ENXIO);

		if ((error = (*sw->probe)(unit, NULL, 0)) != 0 ||
		    (error = (*sw->init)(unit, &kbd, NULL, 0)) != 0)
			return (error);

		state = (vkbd_state_t *) kbd->kb_data;

		if ((error = (*sw->enable)(kbd)) != 0) {
			(*sw->term)(kbd);
			return (error);
		}

#ifdef KBD_INSTALL_CDEV
		if ((error = kbd_attach(kbd)) != 0) {
			(*sw->disable)(kbd);
			(*sw->term)(kbd);
			return (error);
		}
#endif /* def KBD_INSTALL_CDEV */

		dev->si_drv1 = kbd->kb_data;
	}

	VKBD_LOCK(state);

	if (state->ks_flags & OPEN) {
		VKBD_UNLOCK(state);
		return (EBUSY);
	}

	state->ks_flags |= OPEN;
	state->ks_dev = dev;

	VKBD_UNLOCK(state);

	return (0);
}

/* Close device */
static int
vkbd_dev_close(struct cdev *dev, int foo, int bar, struct thread *td)
{
	keyboard_t	*kbd = VKBD_KEYBOARD(dev);
	vkbd_state_t	*state = NULL;

	if (kbd == NULL)
		return (ENXIO);

	if (kbd->kb_data == NULL || kbd->kb_data != dev->si_drv1)
		panic("%s: kbd->kb_data != dev->si_drv1\n", __func__);

	state = (vkbd_state_t *) kbd->kb_data;

	VKBD_LOCK(state);

	/* wait for interrupt task */
	while (state->ks_flags & TASK)
		VKBD_SLEEP(state, ks_task, "vkbdc", 0);

	/* wakeup poll()ers */
	selwakeuppri(&state->ks_rsel, PZERO + 1);
	selwakeuppri(&state->ks_wsel, PZERO + 1);

	state->ks_flags &= ~OPEN;
	state->ks_dev = NULL;
	state->ks_inq.head = state->ks_inq.tail = state->ks_inq.cc = 0;

	VKBD_UNLOCK(state);

	kbdd_disable(kbd);
#ifdef KBD_INSTALL_CDEV
	kbd_detach(kbd);
#endif /* def KBD_INSTALL_CDEV */
	kbdd_term(kbd);

	/* XXX FIXME: dev->si_drv1 locking */
	dev->si_drv1 = NULL;

	return (0);
}

/* Read status */
static int
vkbd_dev_read(struct cdev *dev, struct uio *uio, int flag)
{
	keyboard_t	*kbd = VKBD_KEYBOARD(dev);
	vkbd_state_t	*state = NULL;
	vkbd_status_t	 status;
	int		 error;

	if (kbd == NULL)
		return (ENXIO);

	if (uio->uio_resid != sizeof(status))
		return (EINVAL);

	if (kbd->kb_data == NULL || kbd->kb_data != dev->si_drv1)
		panic("%s: kbd->kb_data != dev->si_drv1\n", __func__);

	state = (vkbd_state_t *) kbd->kb_data;

	VKBD_LOCK(state);

	if (state->ks_flags & READ) {
		VKBD_UNLOCK(state);
		return (EALREADY);
	}

	state->ks_flags |= READ;
again:
	if (state->ks_flags & STATUS) {
		state->ks_flags &= ~STATUS;

		status.mode = state->ks_mode;
		status.leds = KBD_LED_VAL(kbd);
		status.lock = state->ks_state & LOCK_MASK;
		status.delay = kbd->kb_delay1;
		status.rate = kbd->kb_delay2;
		bzero(status.reserved, sizeof(status.reserved));

		error = uiomove(&status, sizeof(status), uio);
	} else {
		if (flag & O_NONBLOCK) {
			error = EWOULDBLOCK;
			goto done;
		}

		error = VKBD_SLEEP(state, ks_flags, "vkbdr", 0);
		if (error != 0) 
			goto done;

		goto again;
	}
done:
	state->ks_flags &= ~READ;

	VKBD_UNLOCK(state);
	
	return (error);
}

/* Write scancodes */
static int
vkbd_dev_write(struct cdev *dev, struct uio *uio, int flag)
{
	keyboard_t	*kbd = VKBD_KEYBOARD(dev);
	vkbd_state_t	*state = NULL;
	vkbd_queue_t	*q = NULL;
	int		 error, avail, bytes;

	if (kbd == NULL)
		return (ENXIO);

	if (uio->uio_resid <= 0)
		return (EINVAL);

	if (kbd->kb_data == NULL || kbd->kb_data != dev->si_drv1)
		panic("%s: kbd->kb_data != dev->si_drv1\n", __func__);

	state = (vkbd_state_t *) kbd->kb_data;

	VKBD_LOCK(state);

	if (state->ks_flags & WRITE) {
		VKBD_UNLOCK(state);
		return (EALREADY);
	}

	state->ks_flags |= WRITE;
	error = 0;
	q = &state->ks_inq;

	while (uio->uio_resid >= sizeof(q->q[0])) {
		if (q->head == q->tail) {
			if (q->cc == 0)
				avail = nitems(q->q) - q->head;
			else
				avail = 0; /* queue must be full */
		} else if (q->head < q->tail)
			avail = nitems(q->q) - q->tail;
		else
			avail = q->head - q->tail;

		if (avail == 0) {
			if (flag & O_NONBLOCK) {
				error = EWOULDBLOCK;
				break;
			}

			error = VKBD_SLEEP(state, ks_inq, "vkbdw", 0);
			if (error != 0)
				break;
		} else {
			bytes = avail * sizeof(q->q[0]);
			if (bytes > uio->uio_resid) {
				avail = uio->uio_resid / sizeof(q->q[0]);
				bytes = avail * sizeof(q->q[0]);
			}

			error = uiomove((void *) &q->q[q->tail], bytes, uio);
			if (error != 0)
				break;

			q->cc += avail;
			q->tail += avail;
			if (q->tail == nitems(q->q))
				q->tail = 0;

			/* queue interrupt task if needed */
			if (!(state->ks_flags & TASK) &&
			    taskqueue_enqueue(taskqueue_swi_giant, &state->ks_task) == 0)
				state->ks_flags |= TASK;
		}
	}

	state->ks_flags &= ~WRITE;

	VKBD_UNLOCK(state);

	return (error);
}

/* Process ioctl */
static int
vkbd_dev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	keyboard_t	*kbd = VKBD_KEYBOARD(dev);

	return ((kbd == NULL)? ENXIO : kbdd_ioctl(kbd, cmd, data));
}

/* Poll device */
static int
vkbd_dev_poll(struct cdev *dev, int events, struct thread *td)
{
	vkbd_state_t	*state = (vkbd_state_t *) dev->si_drv1;
	vkbd_queue_t	*q = NULL;
	int		 revents = 0;

	if (state == NULL)
		return (ENXIO);

	VKBD_LOCK(state);

	q = &state->ks_inq;

	if (events & (POLLIN | POLLRDNORM)) {
		if (state->ks_flags & STATUS)
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &state->ks_rsel);
	}

	if (events & (POLLOUT | POLLWRNORM)) {
		if (q->cc < nitems(q->q))
			revents |= events & (POLLOUT | POLLWRNORM);
		else
			selrecord(td, &state->ks_wsel);
	}

	VKBD_UNLOCK(state);

	return (revents);
}

/* Interrupt handler */
void
vkbd_dev_intr(void *xkbd, int pending)
{
	keyboard_t	*kbd = (keyboard_t *) xkbd;
	vkbd_state_t	*state = (vkbd_state_t *) kbd->kb_data;

	kbdd_intr(kbd, NULL);

	VKBD_LOCK(state);

	state->ks_flags &= ~TASK;
	wakeup(&state->ks_task);

	VKBD_UNLOCK(state);
}

/* Set status change flags */
static void
vkbd_status_changed(vkbd_state_t *state)
{
	VKBD_LOCK_ASSERT(state, MA_OWNED);

	if (!(state->ks_flags & STATUS)) {
		state->ks_flags |= STATUS;
		selwakeuppri(&state->ks_rsel, PZERO + 1);
		wakeup(&state->ks_flags);
	}
}

/* Check if we have data in the input queue */
static int
vkbd_data_ready(vkbd_state_t *state)
{
	VKBD_LOCK_ASSERT(state, MA_OWNED);

	return (state->ks_inq.cc > 0);
}

/* Read one code from the input queue */
static int
vkbd_data_read(vkbd_state_t *state, int wait)
{
	vkbd_queue_t	*q = &state->ks_inq;
	int		 c;

	VKBD_LOCK_ASSERT(state, MA_OWNED);

	if (q->cc == 0)
		return (-1);

	/* get first code from the queue */
	q->cc --;
	c = q->q[q->head ++];
	if (q->head == nitems(q->q))
		q->head = 0;

	/* wakeup ks_inq writers/poll()ers */
	selwakeuppri(&state->ks_wsel, PZERO + 1);
	wakeup(q);

	return (c);
}

/****************************************************************************
 ****************************************************************************
 **                              Keyboard driver
 ****************************************************************************
 ****************************************************************************/

static int		vkbd_configure(int flags);
static kbd_probe_t	vkbd_probe;
static kbd_init_t	vkbd_init;
static kbd_term_t	vkbd_term;
static kbd_intr_t	vkbd_intr;
static kbd_test_if_t	vkbd_test_if;
static kbd_enable_t	vkbd_enable;
static kbd_disable_t	vkbd_disable;
static kbd_read_t	vkbd_read;
static kbd_check_t	vkbd_check;
static kbd_read_char_t	vkbd_read_char;
static kbd_check_char_t	vkbd_check_char;
static kbd_ioctl_t	vkbd_ioctl;
static kbd_lock_t	vkbd_lock;
static void		vkbd_clear_state_locked(vkbd_state_t *state);
static kbd_clear_state_t vkbd_clear_state;
static kbd_get_state_t	vkbd_get_state;
static kbd_set_state_t	vkbd_set_state;
static kbd_poll_mode_t	vkbd_poll;

static keyboard_switch_t vkbdsw = {
	.probe =	vkbd_probe,
	.init =		vkbd_init,
	.term =		vkbd_term,
	.intr =		vkbd_intr,
	.test_if =	vkbd_test_if,
	.enable =	vkbd_enable,
	.disable =	vkbd_disable,
	.read =		vkbd_read,
	.check =	vkbd_check,
	.read_char =	vkbd_read_char,
	.check_char =	vkbd_check_char,
	.ioctl =	vkbd_ioctl,
	.lock =		vkbd_lock,
	.clear_state =	vkbd_clear_state,
	.get_state =	vkbd_get_state,
	.set_state =	vkbd_set_state,
	.get_fkeystr =	genkbd_get_fkeystr,
	.poll =		vkbd_poll,
	.diag =		genkbd_diag,
};

static int	typematic(int delay, int rate);
static int	typematic_delay(int delay);
static int	typematic_rate(int rate);

/* Return the number of found keyboards */
static int
vkbd_configure(int flags)
{
	return (1);
}

/* Detect a keyboard */
static int
vkbd_probe(int unit, void *arg, int flags)
{
	return (0);
}

/* Reset and initialize the keyboard (stolen from atkbd.c) */
static int
vkbd_init(int unit, keyboard_t **kbdp, void *arg, int flags)
{
	keyboard_t	*kbd = NULL;
	vkbd_state_t	*state = NULL;
	keymap_t	*keymap = NULL;
	accentmap_t	*accmap = NULL;
	fkeytab_t	*fkeymap = NULL;
	int		 fkeymap_size, delay[2];
	int		 error, needfree;

	if (*kbdp == NULL) {
		*kbdp = kbd = malloc(sizeof(*kbd), M_VKBD, M_NOWAIT | M_ZERO);
		state = malloc(sizeof(*state), M_VKBD, M_NOWAIT | M_ZERO);
		keymap = malloc(sizeof(key_map), M_VKBD, M_NOWAIT);
		accmap = malloc(sizeof(accent_map), M_VKBD, M_NOWAIT);
		fkeymap = malloc(sizeof(fkey_tab), M_VKBD, M_NOWAIT);
		fkeymap_size = sizeof(fkey_tab)/sizeof(fkey_tab[0]);
		needfree = 1;
		if ((kbd == NULL) || (state == NULL) || (keymap == NULL) ||
		    (accmap == NULL) || (fkeymap == NULL)) {
			error = ENOMEM;
			goto bad;
		}

		VKBD_LOCK_INIT(state);
		state->ks_inq.head = state->ks_inq.tail = state->ks_inq.cc = 0;
		TASK_INIT(&state->ks_task, 0, vkbd_dev_intr, (void *) kbd);
	} else if (KBD_IS_INITIALIZED(*kbdp) && KBD_IS_CONFIGURED(*kbdp)) {
		return (0);
	} else {
		kbd = *kbdp;
		state = (vkbd_state_t *) kbd->kb_data;
		keymap = kbd->kb_keymap;
		accmap = kbd->kb_accentmap;
		fkeymap = kbd->kb_fkeytab;
		fkeymap_size = kbd->kb_fkeytab_size;
		needfree = 0;
	}

	if (!KBD_IS_PROBED(kbd)) {
		kbd_init_struct(kbd, KEYBOARD_NAME, KB_OTHER, unit, flags, 0, 0);
		bcopy(&key_map, keymap, sizeof(key_map));
		bcopy(&accent_map, accmap, sizeof(accent_map));
		bcopy(fkey_tab, fkeymap,
			imin(fkeymap_size*sizeof(fkeymap[0]), sizeof(fkey_tab)));
		kbd_set_maps(kbd, keymap, accmap, fkeymap, fkeymap_size);
		kbd->kb_data = (void *)state;
	
		KBD_FOUND_DEVICE(kbd);
		KBD_PROBE_DONE(kbd);

		VKBD_LOCK(state);
		vkbd_clear_state_locked(state);
		state->ks_mode = K_XLATE;
		/* FIXME: set the initial value for lock keys in ks_state */
		VKBD_UNLOCK(state);
	}
	if (!KBD_IS_INITIALIZED(kbd) && !(flags & KB_CONF_PROBE_ONLY)) {
		kbd->kb_config = flags & ~KB_CONF_PROBE_ONLY;

		vkbd_ioctl(kbd, KDSETLED, (caddr_t)&state->ks_state);
		delay[0] = kbd->kb_delay1;
		delay[1] = kbd->kb_delay2;
		vkbd_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);

		KBD_INIT_DONE(kbd);
	}
	if (!KBD_IS_CONFIGURED(kbd)) {
		if (kbd_register(kbd) < 0) {
			error = ENXIO;
			goto bad;
		}
		KBD_CONFIG_DONE(kbd);
	}

	return (0);
bad:
	if (needfree) {
		if (state != NULL)
			free(state, M_VKBD);
		if (keymap != NULL)
			free(keymap, M_VKBD);
		if (accmap != NULL)
			free(accmap, M_VKBD);
		if (fkeymap != NULL)
			free(fkeymap, M_VKBD);
		if (kbd != NULL) {
			free(kbd, M_VKBD);
			*kbdp = NULL;	/* insure ref doesn't leak to caller */
		}
	}
	return (error);
}

/* Finish using this keyboard */
static int
vkbd_term(keyboard_t *kbd)
{
	vkbd_state_t	*state = (vkbd_state_t *) kbd->kb_data;

	kbd_unregister(kbd);

	VKBD_LOCK_DESTROY(state);
	bzero(state, sizeof(*state));
	free(state, M_VKBD);

	free(kbd->kb_keymap, M_VKBD);
	free(kbd->kb_accentmap, M_VKBD);
	free(kbd->kb_fkeytab, M_VKBD);
	free(kbd, M_VKBD);

	return (0);
}

/* Keyboard interrupt routine */
static int
vkbd_intr(keyboard_t *kbd, void *arg)
{
	int	c;

	if (KBD_IS_ACTIVE(kbd) && KBD_IS_BUSY(kbd)) {
		/* let the callback function to process the input */
		(*kbd->kb_callback.kc_func)(kbd, KBDIO_KEYINPUT,
					    kbd->kb_callback.kc_arg);
	} else {
		/* read and discard the input; no one is waiting for input */
		do {
			c = vkbd_read_char(kbd, FALSE);
		} while (c != NOKEY);
	}

	return (0);
}

/* Test the interface to the device */
static int
vkbd_test_if(keyboard_t *kbd)
{
	return (0);
}

/* 
 * Enable the access to the device; until this function is called,
 * the client cannot read from the keyboard.
 */

static int
vkbd_enable(keyboard_t *kbd)
{
	KBD_ACTIVATE(kbd);
	return (0);
}

/* Disallow the access to the device */
static int
vkbd_disable(keyboard_t *kbd)
{
	KBD_DEACTIVATE(kbd);
	return (0);
}

/* Read one byte from the keyboard if it's allowed */
static int
vkbd_read(keyboard_t *kbd, int wait)
{
	vkbd_state_t	*state = (vkbd_state_t *) kbd->kb_data;
	int		 c;

	VKBD_LOCK(state);
	c = vkbd_data_read(state, wait);
	VKBD_UNLOCK(state);

	if (c != -1)
		kbd->kb_count ++;

	return (KBD_IS_ACTIVE(kbd)? c : -1);
}

/* Check if data is waiting */
static int
vkbd_check(keyboard_t *kbd)
{
	vkbd_state_t	*state = NULL;
	int		 ready;

	if (!KBD_IS_ACTIVE(kbd))
		return (FALSE);

	state = (vkbd_state_t *) kbd->kb_data;

	VKBD_LOCK(state);
	ready = vkbd_data_ready(state);
	VKBD_UNLOCK(state);

	return (ready);
}

/* Read char from the keyboard (stolen from atkbd.c) */
static u_int
vkbd_read_char(keyboard_t *kbd, int wait)
{
	vkbd_state_t	*state = (vkbd_state_t *) kbd->kb_data;
	u_int		 action;
	int		 scancode, keycode;

	VKBD_LOCK(state);

next_code:

	/* do we have a composed char to return? */
	if (!(state->ks_flags & COMPOSE) && (state->ks_composed_char > 0)) {
		action = state->ks_composed_char;
		state->ks_composed_char = 0;
		if (action > UCHAR_MAX) {
			VKBD_UNLOCK(state);
			return (ERRKEY);
		}

		VKBD_UNLOCK(state);
		return (action);
	}

	/* see if there is something in the keyboard port */
	scancode = vkbd_data_read(state, wait);
	if (scancode == -1) {
		VKBD_UNLOCK(state);
		return (NOKEY);
	}
	/* XXX FIXME: check for -1 if wait == 1! */

	kbd->kb_count ++;

	/* return the byte as is for the K_RAW mode */
	if (state->ks_mode == K_RAW) {
		VKBD_UNLOCK(state);
		return (scancode);
	}

	/* translate the scan code into a keycode */
	keycode = scancode & 0x7F;
	switch (state->ks_prefix) {
	case 0x00:	/* normal scancode */
		switch(scancode) {
		case 0xB8:	/* left alt (compose key) released */
			if (state->ks_flags & COMPOSE) {
				state->ks_flags &= ~COMPOSE;
				if (state->ks_composed_char > UCHAR_MAX)
					state->ks_composed_char = 0;
			}
			break;
		case 0x38:	/* left alt (compose key) pressed */
			if (!(state->ks_flags & COMPOSE)) {
				state->ks_flags |= COMPOSE;
				state->ks_composed_char = 0;
			}
			break;
		case 0xE0:
		case 0xE1:
			state->ks_prefix = scancode;
			goto next_code;
		}
		break;
	case 0xE0:      /* 0xE0 prefix */
		state->ks_prefix = 0;
		switch (keycode) {
		case 0x1C:	/* right enter key */
			keycode = 0x59;
			break;
		case 0x1D:	/* right ctrl key */
			keycode = 0x5A;
			break;
		case 0x35:	/* keypad divide key */
			keycode = 0x5B;
			break;
		case 0x37:	/* print scrn key */
			keycode = 0x5C;
			break;
		case 0x38:	/* right alt key (alt gr) */
			keycode = 0x5D;
			break;
		case 0x46:	/* ctrl-pause/break on AT 101 (see below) */
			keycode = 0x68;
			break;
		case 0x47:	/* grey home key */
			keycode = 0x5E;
			break;
		case 0x48:	/* grey up arrow key */
			keycode = 0x5F;
			break;
		case 0x49:	/* grey page up key */
			keycode = 0x60;
			break;
		case 0x4B:	/* grey left arrow key */
			keycode = 0x61;
			break;
		case 0x4D:	/* grey right arrow key */
			keycode = 0x62;
			break;
		case 0x4F:	/* grey end key */
			keycode = 0x63;
			break;
		case 0x50:	/* grey down arrow key */
			keycode = 0x64;
			break;
		case 0x51:	/* grey page down key */
			keycode = 0x65;
			break;
		case 0x52:	/* grey insert key */
			keycode = 0x66;
			break;
		case 0x53:	/* grey delete key */
			keycode = 0x67;
			break;
		/* the following 3 are only used on the MS "Natural" keyboard */
		case 0x5b:	/* left Window key */
			keycode = 0x69;
			break;
		case 0x5c:	/* right Window key */
			keycode = 0x6a;
			break;
		case 0x5d:	/* menu key */
			keycode = 0x6b;
			break;
		case 0x5e:	/* power key */
			keycode = 0x6d;
			break;
		case 0x5f:	/* sleep key */
			keycode = 0x6e;
			break;
		case 0x63:	/* wake key */
			keycode = 0x6f;
			break;
		default:	/* ignore everything else */
			goto next_code;
		}
		break;
	case 0xE1:	/* 0xE1 prefix */
		/* 
		 * The pause/break key on the 101 keyboard produces:
		 * E1-1D-45 E1-9D-C5
		 * Ctrl-pause/break produces:
		 * E0-46 E0-C6 (See above.)
		 */
		state->ks_prefix = 0;
		if (keycode == 0x1D)
			state->ks_prefix = 0x1D;
		goto next_code;
		/* NOT REACHED */
	case 0x1D:	/* pause / break */
		state->ks_prefix = 0;
		if (keycode != 0x45)
			goto next_code;
		keycode = 0x68;
		break;
	}

	if (kbd->kb_type == KB_84) {
		switch (keycode) {
		case 0x37:	/* *(numpad)/print screen */
			if (state->ks_flags & SHIFTS)
				keycode = 0x5c;	/* print screen */
			break;
		case 0x45:	/* num lock/pause */
			if (state->ks_flags & CTLS)
				keycode = 0x68;	/* pause */
			break;
		case 0x46:	/* scroll lock/break */
			if (state->ks_flags & CTLS)
				keycode = 0x6c;	/* break */
			break;
		}
	} else if (kbd->kb_type == KB_101) {
		switch (keycode) {
		case 0x5c:	/* print screen */
			if (state->ks_flags & ALTS)
				keycode = 0x54;	/* sysrq */
			break;
		case 0x68:	/* pause/break */
			if (state->ks_flags & CTLS)
				keycode = 0x6c;	/* break */
			break;
		}
	}

	/* return the key code in the K_CODE mode */
	if (state->ks_mode == K_CODE) {
		VKBD_UNLOCK(state);
		return (keycode | (scancode & 0x80));
	}

	/* compose a character code */
	if (state->ks_flags & COMPOSE) {
		switch (keycode | (scancode & 0x80)) {
		/* key pressed, process it */
		case 0x47: case 0x48: case 0x49:	/* keypad 7,8,9 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x40;
			if (state->ks_composed_char > UCHAR_MAX) {
				VKBD_UNLOCK(state);
				return (ERRKEY);
			}
			goto next_code;
		case 0x4B: case 0x4C: case 0x4D:	/* keypad 4,5,6 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x47;
			if (state->ks_composed_char > UCHAR_MAX) {
				VKBD_UNLOCK(state);
				return (ERRKEY);
			}
			goto next_code;
		case 0x4F: case 0x50: case 0x51:	/* keypad 1,2,3 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x4E;
			if (state->ks_composed_char > UCHAR_MAX) {
				VKBD_UNLOCK(state);
				return (ERRKEY);
			}
			goto next_code;
		case 0x52:	/* keypad 0 */
			state->ks_composed_char *= 10;
			if (state->ks_composed_char > UCHAR_MAX) {
				VKBD_UNLOCK(state);
				return (ERRKEY);
			}
			goto next_code;

		/* key released, no interest here */
		case 0xC7: case 0xC8: case 0xC9:	/* keypad 7,8,9 */
		case 0xCB: case 0xCC: case 0xCD:	/* keypad 4,5,6 */
		case 0xCF: case 0xD0: case 0xD1:	/* keypad 1,2,3 */
		case 0xD2:				/* keypad 0 */
			goto next_code;

		case 0x38:				/* left alt key */
			break;

		default:
			if (state->ks_composed_char > 0) {
				state->ks_flags &= ~COMPOSE;
				state->ks_composed_char = 0;
				VKBD_UNLOCK(state);
				return (ERRKEY);
			}
			break;
		}
	}

	/* keycode to key action */
	action = genkbd_keyaction(kbd, keycode, scancode & 0x80,
			&state->ks_state, &state->ks_accents);
	if (action == NOKEY)
		goto next_code;

	VKBD_UNLOCK(state);

	return (action);
}

/* Check if char is waiting */
static int
vkbd_check_char(keyboard_t *kbd)
{
	vkbd_state_t	*state = NULL;
	int		 ready;

	if (!KBD_IS_ACTIVE(kbd))
		return (FALSE);

	state = (vkbd_state_t *) kbd->kb_data;
	
	VKBD_LOCK(state);
	if (!(state->ks_flags & COMPOSE) && (state->ks_composed_char > 0))
		ready = TRUE;
	else
		ready = vkbd_data_ready(state);
	VKBD_UNLOCK(state);

	return (ready);
}

/* Some useful control functions (stolen from atkbd.c) */
static int
vkbd_ioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	vkbd_state_t	*state = (vkbd_state_t *) kbd->kb_data;
	int		 i;
#ifdef COMPAT_FREEBSD6
	int		 ival;
#endif

	VKBD_LOCK(state);

	switch (cmd) {
	case KDGKBMODE:		/* get keyboard mode */
		*(int *)arg = state->ks_mode;
		break;

#ifdef COMPAT_FREEBSD6
	case _IO('K', 7):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBMODE:		/* set keyboard mode */
		switch (*(int *)arg) {
		case K_XLATE:
			if (state->ks_mode != K_XLATE) {
				/* make lock key state and LED state match */
				state->ks_state &= ~LOCK_MASK;
				state->ks_state |= KBD_LED_VAL(kbd);
				vkbd_status_changed(state);
			}
			/* FALLTHROUGH */

		case K_RAW:
		case K_CODE:
			if (state->ks_mode != *(int *)arg) {
				vkbd_clear_state_locked(state);
				state->ks_mode = *(int *)arg;
				vkbd_status_changed(state);
			}
			break;

		default:
			VKBD_UNLOCK(state);
			return (EINVAL);
		}
		break;

	case KDGETLED:		/* get keyboard LED */
		*(int *)arg = KBD_LED_VAL(kbd);
		break;

#ifdef COMPAT_FREEBSD6
	case _IO('K', 66):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETLED:		/* set keyboard LED */
		/* NOTE: lock key state in ks_state won't be changed */
		if (*(int *)arg & ~LOCK_MASK) {
			VKBD_UNLOCK(state);
			return (EINVAL);
		}

		i = *(int *)arg;
		/* replace CAPS LED with ALTGR LED for ALTGR keyboards */
		if (state->ks_mode == K_XLATE &&
		    kbd->kb_keymap->n_keys > ALTGR_OFFSET) {
			if (i & ALKED)
				i |= CLKED;
			else
				i &= ~CLKED;
		}

		KBD_LED_VAL(kbd) = *(int *)arg;
		vkbd_status_changed(state);
		break;

	case KDGKBSTATE:	/* get lock key state */
		*(int *)arg = state->ks_state & LOCK_MASK;
		break;

#ifdef COMPAT_FREEBSD6
	case _IO('K', 20):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBSTATE:	/* set lock key state */
		if (*(int *)arg & ~LOCK_MASK) {
			VKBD_UNLOCK(state);
			return (EINVAL);
		}
		state->ks_state &= ~LOCK_MASK;
		state->ks_state |= *(int *)arg;
		vkbd_status_changed(state);
		VKBD_UNLOCK(state);
		/* set LEDs and quit */
		return (vkbd_ioctl(kbd, KDSETLED, arg));

	case KDSETREPEAT:	/* set keyboard repeat rate (new interface) */
		i = typematic(((int *)arg)[0], ((int *)arg)[1]);
		kbd->kb_delay1 = typematic_delay(i);
		kbd->kb_delay2 = typematic_rate(i);
		vkbd_status_changed(state);
		break;

#ifdef COMPAT_FREEBSD6
	case _IO('K', 67):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETRAD:		/* set keyboard repeat rate (old interface) */
		kbd->kb_delay1 = typematic_delay(*(int *)arg);
		kbd->kb_delay2 = typematic_rate(*(int *)arg);
		vkbd_status_changed(state);
		break;

	case PIO_KEYMAP:	/* set keyboard translation table */
	case OPIO_KEYMAP:	/* set keyboard translation table (compat) */
	case PIO_KEYMAPENT:	/* set keyboard translation table entry */
	case PIO_DEADKEYMAP:	/* set accent key translation table */
		state->ks_accents = 0;
		/* FALLTHROUGH */

	default:
		VKBD_UNLOCK(state);
		return (genkbd_commonioctl(kbd, cmd, arg));
	}

	VKBD_UNLOCK(state);

	return (0);
}

/* Lock the access to the keyboard */
static int
vkbd_lock(keyboard_t *kbd, int lock)
{
	return (1); /* XXX */
}

/* Clear the internal state of the keyboard */
static void
vkbd_clear_state_locked(vkbd_state_t *state)
{
	VKBD_LOCK_ASSERT(state, MA_OWNED);

	state->ks_flags &= ~COMPOSE;
	state->ks_polling = 0;
	state->ks_state &= LOCK_MASK;	/* preserve locking key state */
	state->ks_accents = 0;
	state->ks_composed_char = 0;
/*	state->ks_prefix = 0;		XXX */

	/* flush ks_inq and wakeup writers/poll()ers */
	state->ks_inq.head = state->ks_inq.tail = state->ks_inq.cc = 0;
	selwakeuppri(&state->ks_wsel, PZERO + 1);
	wakeup(&state->ks_inq);
}

static void
vkbd_clear_state(keyboard_t *kbd)
{
	vkbd_state_t	*state = (vkbd_state_t *) kbd->kb_data;

	VKBD_LOCK(state);
	vkbd_clear_state_locked(state);
	VKBD_UNLOCK(state);
}

/* Save the internal state */
static int
vkbd_get_state(keyboard_t *kbd, void *buf, size_t len)
{
	if (len == 0)
		return (sizeof(vkbd_state_t));
	if (len < sizeof(vkbd_state_t))
		return (-1);
	bcopy(kbd->kb_data, buf, sizeof(vkbd_state_t)); /* XXX locking? */
	return (0);
}

/* Set the internal state */
static int
vkbd_set_state(keyboard_t *kbd, void *buf, size_t len)
{
	if (len < sizeof(vkbd_state_t))
		return (ENOMEM);
	bcopy(buf, kbd->kb_data, sizeof(vkbd_state_t)); /* XXX locking? */
	return (0);
}

/* Set polling */
static int
vkbd_poll(keyboard_t *kbd, int on)
{
	vkbd_state_t	*state = NULL;

	state = (vkbd_state_t *) kbd->kb_data;

	VKBD_LOCK(state);

	if (on)
		state->ks_polling ++;
	else
		state->ks_polling --;

	VKBD_UNLOCK(state);

	return (0);
}

/*
 * Local functions
 */

static int delays[] = { 250, 500, 750, 1000 };
static int rates[] = {	34,  38,  42,  46,  50,  55,  59,  63,
			68,  76,  84,  92, 100, 110, 118, 126,
			136, 152, 168, 184, 200, 220, 236, 252,
			272, 304, 336, 368, 400, 440, 472, 504 };

static int
typematic_delay(int i)
{
	return (delays[(i >> 5) & 3]);
}

static int
typematic_rate(int i)
{
	return (rates[i & 0x1f]);
}

static int
typematic(int delay, int rate)
{
	int value;
	int i;

	for (i = nitems(delays) - 1; i > 0; i --) {
		if (delay >= delays[i])
			break;
	}
	value = i << 5;
	for (i = nitems(rates) - 1; i > 0; i --) {
		if (rate >= rates[i])
			break;
	}
	value |= i;
	return (value);
}

/*****************************************************************************
 *****************************************************************************
 **                                    Module 
 *****************************************************************************
 *****************************************************************************/

KEYBOARD_DRIVER(vkbd, vkbdsw, vkbd_configure);

static int
vkbd_modevent(module_t mod, int type, void *data)
{
	static eventhandler_tag	tag;

	switch (type) {
	case MOD_LOAD:
		clone_setup(&vkbd_dev_clones);
		tag = EVENTHANDLER_REGISTER(dev_clone, vkbd_dev_clone, 0, 1000);
		if (tag == NULL) {
			clone_cleanup(&vkbd_dev_clones);
			return (ENOMEM);
		}
		kbd_add_driver(&vkbd_kbd_driver);
		break;

	case MOD_UNLOAD:
		kbd_delete_driver(&vkbd_kbd_driver);
		EVENTHANDLER_DEREGISTER(dev_clone, tag);
		clone_cleanup(&vkbd_dev_clones);
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

DEV_MODULE(vkbd, vkbd_modevent, NULL);

