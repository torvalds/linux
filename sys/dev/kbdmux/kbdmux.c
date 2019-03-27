/*
 * kbdmux.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: kbdmux.c,v 1.4 2005/07/14 17:38:35 max Exp $
 * $FreeBSD$
 */

#include "opt_evdev.h"
#include "opt_kbd.h"
#include "opt_kbdmux.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/consio.h>
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

/* the initial key map, accent map and fkey strings */
#ifdef KBDMUX_DFLT_KEYMAP
#define KBD_DFLT_KEYMAP
#include "kbdmuxmap.h"
#endif

#include <dev/kbd/kbdtables.h>

#ifdef EVDEV_SUPPORT
#include <dev/evdev/evdev.h>
#include <dev/evdev/input.h>
#endif

#define KEYBOARD_NAME	"kbdmux"

MALLOC_DECLARE(M_KBDMUX);
MALLOC_DEFINE(M_KBDMUX, KEYBOARD_NAME, "Keyboard multiplexor");

/*****************************************************************************
 *****************************************************************************
 **                             Keyboard state
 *****************************************************************************
 *****************************************************************************/

#define	KBDMUX_Q_SIZE	512	/* input queue size */

/*
 * XXX
 * For now rely on Giant mutex to protect our data structures.
 * Just like the rest of keyboard drivers and syscons(4) do.
 * Note that callout is initialized as not MP-safe to make sure
 * Giant is held.
 */

#if 0 /* not yet */
#define KBDMUX_LOCK_DECL_GLOBAL \
	struct mtx ks_lock
#define KBDMUX_LOCK_INIT(s) \
	mtx_init(&(s)->ks_lock, "kbdmux", NULL, MTX_DEF|MTX_RECURSE)
#define KBDMUX_LOCK_DESTROY(s) \
	mtx_destroy(&(s)->ks_lock)
#define KBDMUX_LOCK(s) \
	mtx_lock(&(s)->ks_lock)
#define KBDMUX_UNLOCK(s) \
	mtx_unlock(&(s)->ks_lock)
#define KBDMUX_LOCK_ASSERT(s, w) \
	mtx_assert(&(s)->ks_lock, (w))
#define KBDMUX_SLEEP(s, f, d, t) \
	msleep(&(s)->f, &(s)->ks_lock, PCATCH | (PZERO + 1), (d), (t))
#define KBDMUX_CALLOUT_INIT(s) \
	callout_init_mtx(&(s)->ks_timo, &(s)->ks_lock, 0)
#define KBDMUX_QUEUE_INTR(s) \
	taskqueue_enqueue(taskqueue_swi_giant, &(s)->ks_task)
#else
#define KBDMUX_LOCK_DECL_GLOBAL

#define KBDMUX_LOCK_INIT(s)

#define KBDMUX_LOCK_DESTROY(s)

#define KBDMUX_LOCK(s)

#define KBDMUX_UNLOCK(s)

#define KBDMUX_LOCK_ASSERT(s, w)

#define KBDMUX_SLEEP(s, f, d, t) \
	tsleep(&(s)->f, PCATCH | (PZERO + 1), (d), (t))
#define KBDMUX_CALLOUT_INIT(s) \
	callout_init(&(s)->ks_timo, 0)
#define KBDMUX_QUEUE_INTR(s) \
	taskqueue_enqueue(taskqueue_swi_giant, &(s)->ks_task)
#endif /* not yet */

/*
 * kbdmux keyboard
 */
struct kbdmux_kbd
{
	keyboard_t		*kbd;	/* keyboard */
	SLIST_ENTRY(kbdmux_kbd)	 next;	/* link to next */
};

typedef struct kbdmux_kbd	kbdmux_kbd_t;

/*
 * kbdmux state
 */
struct kbdmux_state
{
	char			 ks_inq[KBDMUX_Q_SIZE]; /* input chars queue */
	unsigned int		 ks_inq_start;
	unsigned int		 ks_inq_length;
	struct task		 ks_task;	/* interrupt task */
	struct callout		 ks_timo;	/* timeout handler */
#define TICKS			(hz)		/* rate */

	int			 ks_flags;	/* flags */
#define COMPOSE			(1 << 0)	/* compose char flag */ 
#define TASK			(1 << 2)	/* interrupt task queued */

	int			 ks_polling;	/* poll nesting count */
	int			 ks_mode;	/* K_XLATE, K_RAW, K_CODE */
	int			 ks_state;	/* state */
	int			 ks_accents;	/* accent key index (> 0) */
	u_int			 ks_composed_char; /* composed char code */
	u_char			 ks_prefix;	/* AT scan code prefix */

#ifdef EVDEV_SUPPORT
	struct evdev_dev *	 ks_evdev;
	int			 ks_evdev_state;
#endif

	SLIST_HEAD(, kbdmux_kbd) ks_kbds;	/* keyboards */

	KBDMUX_LOCK_DECL_GLOBAL;
};

typedef struct kbdmux_state	kbdmux_state_t;

/*****************************************************************************
 *****************************************************************************
 **                             Helper functions
 *****************************************************************************
 *****************************************************************************/

static task_fn_t		kbdmux_kbd_intr;
static timeout_t		kbdmux_kbd_intr_timo;
static kbd_callback_func_t	kbdmux_kbd_event;

static void
kbdmux_kbd_putc(kbdmux_state_t *state, char c)
{
	unsigned int p;

	if (state->ks_inq_length == KBDMUX_Q_SIZE)
		return;

	p = (state->ks_inq_start + state->ks_inq_length) % KBDMUX_Q_SIZE;
	state->ks_inq[p] = c;
	state->ks_inq_length++;
}

static int
kbdmux_kbd_getc(kbdmux_state_t *state)
{
	unsigned char c;

	if (state->ks_inq_length == 0)
		return (-1);

	c = state->ks_inq[state->ks_inq_start];
	state->ks_inq_start = (state->ks_inq_start + 1) % KBDMUX_Q_SIZE;
	state->ks_inq_length--;

	return (c);
}

/*
 * Interrupt handler task
 */
void
kbdmux_kbd_intr(void *xkbd, int pending)
{
	keyboard_t	*kbd = (keyboard_t *) xkbd;
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;

	kbdd_intr(kbd, NULL);

	KBDMUX_LOCK(state);

	state->ks_flags &= ~TASK;
	wakeup(&state->ks_task);

	KBDMUX_UNLOCK(state);
}

/*
 * Schedule interrupt handler on timeout. Called with locked state.
 */
void
kbdmux_kbd_intr_timo(void *xstate)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) xstate;

	KBDMUX_LOCK_ASSERT(state, MA_OWNED);

	if (callout_pending(&state->ks_timo))
		return; /* callout was reset */

	if (!callout_active(&state->ks_timo))
		return; /* callout was stopped */

	callout_deactivate(&state->ks_timo);

	/* queue interrupt task if needed */
	if (state->ks_inq_length > 0 && !(state->ks_flags & TASK) &&
	    KBDMUX_QUEUE_INTR(state) == 0)
		state->ks_flags |= TASK;

	/* re-schedule timeout */
	callout_reset(&state->ks_timo, TICKS, kbdmux_kbd_intr_timo, state);
}

/*
 * Process event from one of our keyboards
 */
static int
kbdmux_kbd_event(keyboard_t *kbd, int event, void *arg)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) arg;

	switch (event) {
	case KBDIO_KEYINPUT: {
		int	c;

		KBDMUX_LOCK(state);

		/*
		 * Read all chars from the keyboard
		 *
		 * Turns out that atkbd(4) check_char() method may return
		 * "true" while read_char() method returns NOKEY. If this
		 * happens we could stuck in the loop below. Avoid this
		 * by breaking out of the loop if read_char() method returns
		 * NOKEY.
		 */

		while (kbdd_check_char(kbd)) {
			c = kbdd_read_char(kbd, 0);
			if (c == NOKEY)
				break;
			if (c == ERRKEY)
				continue; /* XXX ring bell */
			if (!KBD_IS_BUSY(kbd))
				continue; /* not open - discard the input */

			kbdmux_kbd_putc(state, c);
		}

		/* queue interrupt task if needed */
		if (state->ks_inq_length > 0 && !(state->ks_flags & TASK) &&
		    KBDMUX_QUEUE_INTR(state) == 0)
			state->ks_flags |= TASK;

		KBDMUX_UNLOCK(state);
		} break;

	case KBDIO_UNLOADING: {
		kbdmux_kbd_t	*k;

		KBDMUX_LOCK(state);

		SLIST_FOREACH(k, &state->ks_kbds, next)
			if (k->kbd == kbd)
				break;

		if (k != NULL) {
			kbd_release(k->kbd, &k->kbd);
			SLIST_REMOVE(&state->ks_kbds, k, kbdmux_kbd, next);

			k->kbd = NULL;

			free(k, M_KBDMUX);
		}

		KBDMUX_UNLOCK(state);
		} break;

	default:
		return (EINVAL);
		/* NOT REACHED */
	}

	return (0);
}

/****************************************************************************
 ****************************************************************************
 **                              Keyboard driver
 ****************************************************************************
 ****************************************************************************/

static int		kbdmux_configure(int flags);
static kbd_probe_t	kbdmux_probe;
static kbd_init_t	kbdmux_init;
static kbd_term_t	kbdmux_term;
static kbd_intr_t	kbdmux_intr;
static kbd_test_if_t	kbdmux_test_if;
static kbd_enable_t	kbdmux_enable;
static kbd_disable_t	kbdmux_disable;
static kbd_read_t	kbdmux_read;
static kbd_check_t	kbdmux_check;
static kbd_read_char_t	kbdmux_read_char;
static kbd_check_char_t	kbdmux_check_char;
static kbd_ioctl_t	kbdmux_ioctl;
static kbd_lock_t	kbdmux_lock;
static void		kbdmux_clear_state_locked(kbdmux_state_t *state);
static kbd_clear_state_t kbdmux_clear_state;
static kbd_get_state_t	kbdmux_get_state;
static kbd_set_state_t	kbdmux_set_state;
static kbd_poll_mode_t	kbdmux_poll;

static keyboard_switch_t kbdmuxsw = {
	.probe =	kbdmux_probe,
	.init =		kbdmux_init,
	.term =		kbdmux_term,
	.intr =		kbdmux_intr,
	.test_if =	kbdmux_test_if,
	.enable =	kbdmux_enable,
	.disable =	kbdmux_disable,
	.read =		kbdmux_read,
	.check =	kbdmux_check,
	.read_char =	kbdmux_read_char,
	.check_char =	kbdmux_check_char,
	.ioctl =	kbdmux_ioctl,
	.lock =		kbdmux_lock,
	.clear_state =	kbdmux_clear_state,
	.get_state =	kbdmux_get_state,
	.set_state =	kbdmux_set_state,
	.get_fkeystr =	genkbd_get_fkeystr,
	.poll =		kbdmux_poll,
	.diag =		genkbd_diag,
};

#ifdef EVDEV_SUPPORT
static evdev_event_t kbdmux_ev_event;

static const struct evdev_methods kbdmux_evdev_methods = {
	.ev_event = kbdmux_ev_event,
};
#endif

/*
 * Return the number of found keyboards
 */
static int
kbdmux_configure(int flags)
{
	return (1);
}

/*
 * Detect a keyboard
 */
static int
kbdmux_probe(int unit, void *arg, int flags)
{
	if (resource_disabled(KEYBOARD_NAME, unit))
		return (ENXIO);

	return (0);
}

/*
 * Reset and initialize the keyboard (stolen from atkbd.c)
 */
static int
kbdmux_init(int unit, keyboard_t **kbdp, void *arg, int flags)
{
	keyboard_t	*kbd = NULL;
	kbdmux_state_t	*state = NULL;
	keymap_t	*keymap = NULL;
        accentmap_t	*accmap = NULL;
        fkeytab_t	*fkeymap = NULL;
	int		 error, needfree, fkeymap_size, delay[2];
#ifdef EVDEV_SUPPORT
	struct evdev_dev *evdev;
	char		 phys_loc[NAMELEN];
#endif

	if (*kbdp == NULL) {
		*kbdp = kbd = malloc(sizeof(*kbd), M_KBDMUX, M_NOWAIT | M_ZERO);
		state = malloc(sizeof(*state), M_KBDMUX, M_NOWAIT | M_ZERO);
		keymap = malloc(sizeof(key_map), M_KBDMUX, M_NOWAIT);
		accmap = malloc(sizeof(accent_map), M_KBDMUX, M_NOWAIT);
		fkeymap = malloc(sizeof(fkey_tab), M_KBDMUX, M_NOWAIT);
		fkeymap_size = sizeof(fkey_tab)/sizeof(fkey_tab[0]);
		needfree = 1;

		if ((kbd == NULL) || (state == NULL) || (keymap == NULL) ||
		    (accmap == NULL) || (fkeymap == NULL)) {
			error = ENOMEM;
			goto bad;
		}

		KBDMUX_LOCK_INIT(state);
		TASK_INIT(&state->ks_task, 0, kbdmux_kbd_intr, (void *) kbd);
		KBDMUX_CALLOUT_INIT(state);
		SLIST_INIT(&state->ks_kbds);
	} else if (KBD_IS_INITIALIZED(*kbdp) && KBD_IS_CONFIGURED(*kbdp)) {
		return (0);
	} else {
		kbd = *kbdp;
		state = (kbdmux_state_t *) kbd->kb_data;
		keymap = kbd->kb_keymap;
		accmap = kbd->kb_accentmap;
		fkeymap = kbd->kb_fkeytab;
		fkeymap_size = kbd->kb_fkeytab_size;
		needfree = 0;
	}

	if (!KBD_IS_PROBED(kbd)) {
		/* XXX assume 101/102 keys keyboard */
		kbd_init_struct(kbd, KEYBOARD_NAME, KB_101, unit, flags, 0, 0);
		bcopy(&key_map, keymap, sizeof(key_map));
		bcopy(&accent_map, accmap, sizeof(accent_map));
		bcopy(fkey_tab, fkeymap,
			imin(fkeymap_size*sizeof(fkeymap[0]), sizeof(fkey_tab)));
		kbd_set_maps(kbd, keymap, accmap, fkeymap, fkeymap_size);
		kbd->kb_data = (void *)state;
	
		KBD_FOUND_DEVICE(kbd);
		KBD_PROBE_DONE(kbd);

		KBDMUX_LOCK(state);
		kbdmux_clear_state_locked(state);
		state->ks_mode = K_XLATE;
		KBDMUX_UNLOCK(state);
	}

	if (!KBD_IS_INITIALIZED(kbd) && !(flags & KB_CONF_PROBE_ONLY)) {
		kbd->kb_config = flags & ~KB_CONF_PROBE_ONLY;

		kbdmux_ioctl(kbd, KDSETLED, (caddr_t)&state->ks_state);

		delay[0] = kbd->kb_delay1;
		delay[1] = kbd->kb_delay2;
		kbdmux_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);

#ifdef EVDEV_SUPPORT
		/* register as evdev provider */
		evdev = evdev_alloc();
		evdev_set_name(evdev, "System keyboard multiplexer");
		snprintf(phys_loc, NAMELEN, KEYBOARD_NAME"%d", unit);
		evdev_set_phys(evdev, phys_loc);
		evdev_set_id(evdev, BUS_VIRTUAL, 0, 0, 0);
		evdev_set_methods(evdev, kbd, &kbdmux_evdev_methods);
		evdev_support_event(evdev, EV_SYN);
		evdev_support_event(evdev, EV_KEY);
		evdev_support_event(evdev, EV_LED);
		evdev_support_event(evdev, EV_REP);
		evdev_support_all_known_keys(evdev);
		evdev_support_led(evdev, LED_NUML);
		evdev_support_led(evdev, LED_CAPSL);
		evdev_support_led(evdev, LED_SCROLLL);

		if (evdev_register_mtx(evdev, &Giant))
			evdev_free(evdev);
		else
			state->ks_evdev = evdev;
		state->ks_evdev_state = 0;
#endif

		KBD_INIT_DONE(kbd);
	}

	if (!KBD_IS_CONFIGURED(kbd)) {
		if (kbd_register(kbd) < 0) {
			error = ENXIO;
			goto bad;
		}

		KBD_CONFIG_DONE(kbd);

		KBDMUX_LOCK(state);
		callout_reset(&state->ks_timo, TICKS, kbdmux_kbd_intr_timo, state);
		KBDMUX_UNLOCK(state);
	}

	return (0);
bad:
	if (needfree) {
		if (state != NULL)
			free(state, M_KBDMUX);
		if (keymap != NULL)
			free(keymap, M_KBDMUX);
		if (accmap != NULL)
			free(accmap, M_KBDMUX);
		if (fkeymap != NULL)
			free(fkeymap, M_KBDMUX);
		if (kbd != NULL) {
			free(kbd, M_KBDMUX);
			*kbdp = NULL;	/* insure ref doesn't leak to caller */
		}
	}

	return (error);
}

/*
 * Finish using this keyboard
 */
static int
kbdmux_term(keyboard_t *kbd)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;
	kbdmux_kbd_t	*k;

	KBDMUX_LOCK(state);

	/* kill callout */
	callout_stop(&state->ks_timo);

	/* wait for interrupt task */
	while (state->ks_flags & TASK)
		KBDMUX_SLEEP(state, ks_task, "kbdmuxc", 0);

	/* release all keyboards from the mux */
	while ((k = SLIST_FIRST(&state->ks_kbds)) != NULL) {
		kbd_release(k->kbd, &k->kbd);
		SLIST_REMOVE_HEAD(&state->ks_kbds, next);

		k->kbd = NULL;

		free(k, M_KBDMUX);
	}

	KBDMUX_UNLOCK(state);

	kbd_unregister(kbd);

#ifdef EVDEV_SUPPORT
	evdev_free(state->ks_evdev);
#endif

	KBDMUX_LOCK_DESTROY(state);
	bzero(state, sizeof(*state));
	free(state, M_KBDMUX);

	free(kbd->kb_keymap, M_KBDMUX);
	free(kbd->kb_accentmap, M_KBDMUX);
	free(kbd->kb_fkeytab, M_KBDMUX);
	free(kbd, M_KBDMUX);

	return (0);
}

/*
 * Keyboard interrupt routine
 */
static int
kbdmux_intr(keyboard_t *kbd, void *arg)
{
	int	c;

	if (KBD_IS_ACTIVE(kbd) && KBD_IS_BUSY(kbd)) {
		/* let the callback function to process the input */
		(*kbd->kb_callback.kc_func)(kbd, KBDIO_KEYINPUT,
					    kbd->kb_callback.kc_arg);
	} else {
		/* read and discard the input; no one is waiting for input */
		do {
			c = kbdmux_read_char(kbd, FALSE);
		} while (c != NOKEY);
	}

	return (0);
}

/*
 * Test the interface to the device
 */
static int
kbdmux_test_if(keyboard_t *kbd)
{
	return (0);
}

/* 
 * Enable the access to the device; until this function is called,
 * the client cannot read from the keyboard.
 */
static int
kbdmux_enable(keyboard_t *kbd)
{
	KBD_ACTIVATE(kbd);
	return (0);
}

/*
 * Disallow the access to the device
 */
static int
kbdmux_disable(keyboard_t *kbd)
{
	KBD_DEACTIVATE(kbd);
	return (0);
}

/*
 * Read one byte from the keyboard if it's allowed
 */
static int
kbdmux_read(keyboard_t *kbd, int wait)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;
	int		 c;

	KBDMUX_LOCK(state);
	c = kbdmux_kbd_getc(state);
	KBDMUX_UNLOCK(state);

	if (c != -1)
		kbd->kb_count ++;

	return (KBD_IS_ACTIVE(kbd)? c : -1);
}

/*
 * Check if data is waiting
 */
static int
kbdmux_check(keyboard_t *kbd)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;
	int		 ready;

	if (!KBD_IS_ACTIVE(kbd))
		return (FALSE);

	KBDMUX_LOCK(state);
	ready = (state->ks_inq_length > 0) ? TRUE : FALSE;
	KBDMUX_UNLOCK(state);

	return (ready);
}

/*
 * Read char from the keyboard (stolen from atkbd.c)
 */
static u_int
kbdmux_read_char(keyboard_t *kbd, int wait)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;
	u_int		 action;
	int		 scancode, keycode;

	KBDMUX_LOCK(state);

next_code:

	/* do we have a composed char to return? */
	if (!(state->ks_flags & COMPOSE) && (state->ks_composed_char > 0)) {
		action = state->ks_composed_char;
		state->ks_composed_char = 0;
		if (action > UCHAR_MAX) {
			KBDMUX_UNLOCK(state);

			return (ERRKEY);
		}

		KBDMUX_UNLOCK(state);

		return (action);
	}

	/* see if there is something in the keyboard queue */
	scancode = kbdmux_kbd_getc(state);
	if (scancode == -1) {
		if (state->ks_polling != 0) {
			kbdmux_kbd_t	*k;

			SLIST_FOREACH(k, &state->ks_kbds, next) {
				while (kbdd_check_char(k->kbd)) {
					scancode = kbdd_read_char(k->kbd, 0);
					if (scancode == NOKEY)
						break;
					if (scancode == ERRKEY)
						continue;
					if (!KBD_IS_BUSY(k->kbd))
						continue; 

					kbdmux_kbd_putc(state, scancode);
				}
			}

			if (state->ks_inq_length > 0)
				goto next_code;
		}

		KBDMUX_UNLOCK(state);
		return (NOKEY);
	}
	/* XXX FIXME: check for -1 if wait == 1! */

	kbd->kb_count ++;

#ifdef EVDEV_SUPPORT
	/* push evdev event */
	if (evdev_rcpt_mask & EVDEV_RCPT_KBDMUX && state->ks_evdev != NULL) {
		uint16_t key = evdev_scancode2key(&state->ks_evdev_state,
		    scancode);

		if (key != KEY_RESERVED) {
			evdev_push_event(state->ks_evdev, EV_KEY,
			    key, scancode & 0x80 ? 0 : 1);
			evdev_sync(state->ks_evdev);
		}
	}
#endif

	/* return the byte as is for the K_RAW mode */
	if (state->ks_mode == K_RAW) {
		KBDMUX_UNLOCK(state);
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
		case 0x64:	/* [JP106USB] backslash, underscore */
			keycode = 0x73;
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

	/* XXX assume 101/102 keys AT keyboard */
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

	/* return the key code in the K_CODE mode */
	if (state->ks_mode == K_CODE) {
		KBDMUX_UNLOCK(state);
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
				KBDMUX_UNLOCK(state);
				return (ERRKEY);
			}
			goto next_code;
		case 0x4B: case 0x4C: case 0x4D:	/* keypad 4,5,6 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x47;
			if (state->ks_composed_char > UCHAR_MAX) {
				KBDMUX_UNLOCK(state);
				return (ERRKEY);
			}
			goto next_code;
		case 0x4F: case 0x50: case 0x51:	/* keypad 1,2,3 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x4E;
			if (state->ks_composed_char > UCHAR_MAX) {
				KBDMUX_UNLOCK(state);
				return (ERRKEY);
			}
			goto next_code;
		case 0x52:	/* keypad 0 */
			state->ks_composed_char *= 10;
			if (state->ks_composed_char > UCHAR_MAX) {
				KBDMUX_UNLOCK(state);
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
				KBDMUX_UNLOCK(state);
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

	KBDMUX_UNLOCK(state);

	return (action);
}

/*
 * Check if char is waiting
 */
static int
kbdmux_check_char(keyboard_t *kbd)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;
	int		 ready;

	if (!KBD_IS_ACTIVE(kbd))
		return (FALSE);

	KBDMUX_LOCK(state);

	if (!(state->ks_flags & COMPOSE) && (state->ks_composed_char != 0))
		ready = TRUE;
	else
		ready = (state->ks_inq_length > 0) ? TRUE : FALSE;

	KBDMUX_UNLOCK(state);

	return (ready);
}

/*
 * Keyboard ioctl's
 */
static int
kbdmux_ioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	static int	 delays[] = {
		250, 500, 750, 1000
	};

	static int	 rates[]  =  {
		34,  38,  42,  46,  50,   55,  59,  63,
		68,  76,  84,  92,  100, 110, 118, 126,
		136, 152, 168, 184, 200, 220, 236, 252,
		272, 304, 336, 368, 400, 440, 472, 504
	};

	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;
	kbdmux_kbd_t	*k;
	keyboard_info_t	*ki;
	int		 error = 0, mode;
#ifdef COMPAT_FREEBSD6
	int		 ival;
#endif

	if (state == NULL)
		return (ENXIO);

	switch (cmd) {
	case KBADDKBD: /* add keyboard to the mux */
		ki = (keyboard_info_t *) arg;

		if (ki == NULL || ki->kb_unit < 0 || ki->kb_name[0] == '\0' ||
		    strcmp(ki->kb_name, "*") == 0)
			return (EINVAL); /* bad input */

		KBDMUX_LOCK(state);

		SLIST_FOREACH(k, &state->ks_kbds, next)
			if (k->kbd->kb_unit == ki->kb_unit &&
			    strcmp(k->kbd->kb_name, ki->kb_name) == 0)
				break;

		if (k != NULL) {
			KBDMUX_UNLOCK(state);

			return (0); /* keyboard already in the mux */
		}

		k = malloc(sizeof(*k), M_KBDMUX, M_NOWAIT | M_ZERO);
		if (k == NULL) {
			KBDMUX_UNLOCK(state);

			return (ENOMEM); /* out of memory */
		}

		k->kbd = kbd_get_keyboard(
				kbd_allocate(
					ki->kb_name,
					ki->kb_unit,
					(void *) &k->kbd,
					kbdmux_kbd_event, (void *) state));
		if (k->kbd == NULL) {
			KBDMUX_UNLOCK(state);
			free(k, M_KBDMUX);

			return (EINVAL); /* bad keyboard */
		}

		kbdd_enable(k->kbd);
		kbdd_clear_state(k->kbd);

		/* set K_RAW mode on slave keyboard */
		mode = K_RAW;
		error = kbdd_ioctl(k->kbd, KDSKBMODE, (caddr_t)&mode);
		if (error == 0) {
			/* set lock keys state on slave keyboard */
			mode = state->ks_state & LOCK_MASK;
			error = kbdd_ioctl(k->kbd, KDSKBSTATE, (caddr_t)&mode);
		}

		if (error != 0) {
			KBDMUX_UNLOCK(state);

			kbd_release(k->kbd, &k->kbd);
			k->kbd = NULL;

			free(k, M_KBDMUX);

			return (error); /* could not set mode */
		}

		SLIST_INSERT_HEAD(&state->ks_kbds, k, next);

		KBDMUX_UNLOCK(state);
		break;

	case KBRELKBD: /* release keyboard from the mux */
		ki = (keyboard_info_t *) arg;

		if (ki == NULL || ki->kb_unit < 0 || ki->kb_name[0] == '\0' ||
		    strcmp(ki->kb_name, "*") == 0)
			return (EINVAL); /* bad input */

		KBDMUX_LOCK(state);

		SLIST_FOREACH(k, &state->ks_kbds, next)
			if (k->kbd->kb_unit == ki->kb_unit &&
			    strcmp(k->kbd->kb_name, ki->kb_name) == 0)
				break;

		if (k != NULL) {
			error = kbd_release(k->kbd, &k->kbd);
			if (error == 0) {
				SLIST_REMOVE(&state->ks_kbds, k, kbdmux_kbd, next);

				k->kbd = NULL;

				free(k, M_KBDMUX);
			}
		} else
			error = ENXIO; /* keyboard is not in the mux */

		KBDMUX_UNLOCK(state);
		break;

	case KDGKBMODE: /* get kyboard mode */
		KBDMUX_LOCK(state);
		*(int *)arg = state->ks_mode;
		KBDMUX_UNLOCK(state);
		break;

#ifdef COMPAT_FREEBSD6
	case _IO('K', 7):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBMODE: /* set keyboard mode */
		KBDMUX_LOCK(state);

		switch (*(int *)arg) {
		case K_XLATE:
			if (state->ks_mode != K_XLATE) {
				/* make lock key state and LED state match */
				state->ks_state &= ~LOCK_MASK;
				state->ks_state |= KBD_LED_VAL(kbd);
                        }
                        /* FALLTHROUGH */

		case K_RAW:
		case K_CODE:
			if (state->ks_mode != *(int *)arg) {
				kbdmux_clear_state_locked(state);
				state->ks_mode = *(int *)arg;
			}
			break;

                default:
			error = EINVAL;
			break;
		}

		KBDMUX_UNLOCK(state);
		break;

	case KDGETLED: /* get keyboard LED */
		KBDMUX_LOCK(state);
		*(int *)arg = KBD_LED_VAL(kbd);
		KBDMUX_UNLOCK(state);
		break;

#ifdef COMPAT_FREEBSD6
	case _IO('K', 66):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETLED: /* set keyboard LED */
		KBDMUX_LOCK(state);

		/* NOTE: lock key state in ks_state won't be changed */
		if (*(int *)arg & ~LOCK_MASK) {
			KBDMUX_UNLOCK(state);

			return (EINVAL);
		}

		KBD_LED_VAL(kbd) = *(int *)arg;
#ifdef EVDEV_SUPPORT
		if (state->ks_evdev != NULL &&
		    evdev_rcpt_mask & EVDEV_RCPT_KBDMUX)
			evdev_push_leds(state->ks_evdev, *(int *)arg);
#endif
		/* KDSETLED on all slave keyboards */
		SLIST_FOREACH(k, &state->ks_kbds, next)
			(void)kbdd_ioctl(k->kbd, KDSETLED, arg);

		KBDMUX_UNLOCK(state);
		break;

	case KDGKBSTATE: /* get lock key state */
		KBDMUX_LOCK(state);
		*(int *)arg = state->ks_state & LOCK_MASK;
		KBDMUX_UNLOCK(state);
		break;

#ifdef COMPAT_FREEBSD6
	case _IO('K', 20):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBSTATE: /* set lock key state */
		KBDMUX_LOCK(state);

		if (*(int *)arg & ~LOCK_MASK) {
			KBDMUX_UNLOCK(state);

			return (EINVAL);
		}

		state->ks_state &= ~LOCK_MASK;
		state->ks_state |= *(int *)arg;

		/* KDSKBSTATE on all slave keyboards */
		SLIST_FOREACH(k, &state->ks_kbds, next)
			(void)kbdd_ioctl(k->kbd, KDSKBSTATE, arg);

		KBDMUX_UNLOCK(state);

		return (kbdmux_ioctl(kbd, KDSETLED, arg));
		/* NOT REACHED */

#ifdef COMPAT_FREEBSD6
	case _IO('K', 67):
		cmd = KDSETRAD;
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETREPEAT: /* set keyboard repeat rate (new interface) */
	case KDSETRAD: /* set keyboard repeat rate (old interface) */
		KBDMUX_LOCK(state);

		if (cmd == KDSETREPEAT) {
			int	i;

			/* lookup delay */
			for (i = sizeof(delays)/sizeof(delays[0]) - 1; i > 0; i --)
				if (((int *)arg)[0] >= delays[i])
					break;
			mode = i << 5;

			/* lookup rate */
			for (i = sizeof(rates)/sizeof(rates[0]) - 1; i > 0; i --)
				if (((int *)arg)[1] >= rates[i])
					break;
			mode |= i;
		} else
			mode = *(int *)arg;

		if (mode & ~0x7f) {
			KBDMUX_UNLOCK(state);

			return (EINVAL);
		}

		kbd->kb_delay1 = delays[(mode >> 5) & 3];
		kbd->kb_delay2 = rates[mode & 0x1f];
#ifdef EVDEV_SUPPORT
		if (state->ks_evdev != NULL &&
		    evdev_rcpt_mask & EVDEV_RCPT_KBDMUX)
			evdev_push_repeats(state->ks_evdev, kbd);
#endif
		/* perform command on all slave keyboards */
		SLIST_FOREACH(k, &state->ks_kbds, next)
			(void)kbdd_ioctl(k->kbd, cmd, arg);

		KBDMUX_UNLOCK(state);
		break;

	case PIO_KEYMAP:	/* set keyboard translation table */
	case OPIO_KEYMAP:	/* set keyboard translation table (compat) */
	case PIO_KEYMAPENT:	/* set keyboard translation table entry */
	case PIO_DEADKEYMAP:	/* set accent key translation table */
		KBDMUX_LOCK(state);
                state->ks_accents = 0;

		/* perform command on all slave keyboards */
		SLIST_FOREACH(k, &state->ks_kbds, next)
			(void)kbdd_ioctl(k->kbd, cmd, arg);

		KBDMUX_UNLOCK(state);
                /* FALLTHROUGH */

	default:
		error = genkbd_commonioctl(kbd, cmd, arg);
		break;
	}

	return (error);
}

/*
 * Lock the access to the keyboard
 */
static int
kbdmux_lock(keyboard_t *kbd, int lock)
{
	return (1); /* XXX */
}

/*
 * Clear the internal state of the keyboard
 */
static void
kbdmux_clear_state_locked(kbdmux_state_t *state)
{
	KBDMUX_LOCK_ASSERT(state, MA_OWNED);

	state->ks_flags &= ~COMPOSE;
	state->ks_polling = 0;
	state->ks_state &= LOCK_MASK;	/* preserve locking key state */
	state->ks_accents = 0;
	state->ks_composed_char = 0;
/*	state->ks_prefix = 0;		XXX */
	state->ks_inq_length = 0;
}

static void
kbdmux_clear_state(keyboard_t *kbd)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;

	KBDMUX_LOCK(state);
	kbdmux_clear_state_locked(state);
	KBDMUX_UNLOCK(state);
}

/*
 * Save the internal state
 */
static int
kbdmux_get_state(keyboard_t *kbd, void *buf, size_t len)
{
	if (len == 0)
		return (sizeof(kbdmux_state_t));
	if (len < sizeof(kbdmux_state_t))
		return (-1);

	bcopy(kbd->kb_data, buf, sizeof(kbdmux_state_t)); /* XXX locking? */

	return (0);
}

/*
 * Set the internal state
 */
static int
kbdmux_set_state(keyboard_t *kbd, void *buf, size_t len)
{
	if (len < sizeof(kbdmux_state_t))
		return (ENOMEM);

	bcopy(buf, kbd->kb_data, sizeof(kbdmux_state_t)); /* XXX locking? */

	return (0);
}

/*
 * Set polling
 */
static int
kbdmux_poll(keyboard_t *kbd, int on)
{
	kbdmux_state_t	*state = (kbdmux_state_t *) kbd->kb_data;
	kbdmux_kbd_t	*k;

	KBDMUX_LOCK(state);

	if (on)
		state->ks_polling++;
	else
		state->ks_polling--;

	/* set poll on slave keyboards */
	SLIST_FOREACH(k, &state->ks_kbds, next)
		kbdd_poll(k->kbd, on);

	KBDMUX_UNLOCK(state);

	return (0);
}

#ifdef EVDEV_SUPPORT
static void
kbdmux_ev_event(struct evdev_dev *evdev, uint16_t type, uint16_t code,
    int32_t value)
{
	keyboard_t *kbd = evdev_get_softc(evdev);

	if (evdev_rcpt_mask & EVDEV_RCPT_KBDMUX &&
	    (type == EV_LED || type == EV_REP)) {
		mtx_lock(&Giant);
		kbd_ev_event(kbd, type, code, value);
		mtx_unlock(&Giant);
	}
}
#endif

/*****************************************************************************
 *****************************************************************************
 **                                    Module 
 *****************************************************************************
 *****************************************************************************/

KEYBOARD_DRIVER(kbdmux, kbdmuxsw, kbdmux_configure);

static int
kbdmux_modevent(module_t mod, int type, void *data)
{
	keyboard_switch_t	*sw;
	keyboard_t		*kbd;
	int			 error;

	switch (type) {
	case MOD_LOAD:
		if ((error = kbd_add_driver(&kbdmux_kbd_driver)) != 0)
			break;

		if ((sw = kbd_get_switch(KEYBOARD_NAME)) == NULL) {
			kbd_delete_driver(&kbdmux_kbd_driver);
			error = ENXIO;
			break;
		}

		kbd = NULL;

		if ((error = (*sw->probe)(0, NULL, 0)) != 0 ||
		    (error = (*sw->init)(0, &kbd, NULL, 0)) != 0) {
			kbd_delete_driver(&kbdmux_kbd_driver);
			break;
		}

#ifdef KBD_INSTALL_CDEV
		if ((error = kbd_attach(kbd)) != 0) {
			(*sw->term)(kbd);
			kbd_delete_driver(&kbdmux_kbd_driver);
			break;
		}
#endif

		if ((error = (*sw->enable)(kbd)) != 0) {
			(*sw->disable)(kbd);
#ifdef KBD_INSTALL_CDEV
			kbd_detach(kbd);
#endif
			(*sw->term)(kbd);
			kbd_delete_driver(&kbdmux_kbd_driver);
			break;
		}
		break;

	case MOD_UNLOAD:
		if ((sw = kbd_get_switch(KEYBOARD_NAME)) == NULL)
			panic("kbd_get_switch(" KEYBOARD_NAME ") == NULL");

		kbd = kbd_get_keyboard(kbd_find_keyboard(KEYBOARD_NAME, 0));
		if (kbd != NULL) {
			(*sw->disable)(kbd);
#ifdef KBD_INSTALL_CDEV
			kbd_detach(kbd);
#endif
			(*sw->term)(kbd);
			kbd_delete_driver(&kbdmux_kbd_driver);
		}
		error = 0;
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

DEV_MODULE(kbdmux, kbdmux_modevent, NULL);
#ifdef EVDEV_SUPPORT
MODULE_DEPEND(kbdmux, evdev, 1, 1, 1);
#endif
