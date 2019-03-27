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
#include "opt_atkbd.h"
#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>
#include <dev/atkbdc/atkbdreg.h>
#include <dev/atkbdc/atkbdcreg.h>

#ifdef EVDEV_SUPPORT
#include <dev/evdev/evdev.h>
#include <dev/evdev/input.h>
#endif

typedef struct atkbd_state {
	KBDC		kbdc;		/* keyboard controller */
	int		ks_mode;	/* input mode (K_XLATE,K_RAW,K_CODE) */
	int		ks_flags;	/* flags */
#define COMPOSE		(1 << 0)
	int		ks_polling;
	int		ks_state;	/* shift/lock key state */
	int		ks_accents;	/* accent key index (> 0) */
	u_int		ks_composed_char; /* composed char code (> 0) */
	u_char		ks_prefix;	/* AT scan code prefix */
	struct callout	ks_timer;
#ifdef EVDEV_SUPPORT
	struct evdev_dev *ks_evdev;
	int		ks_evdev_state;
#endif
} atkbd_state_t;

static void		atkbd_timeout(void *arg);
static void		atkbd_shutdown_final(void *v);
static int		atkbd_reset(KBDC kbdc, int flags, int c);

#define HAS_QUIRK(p, q)		(((atkbdc_softc_t *)(p))->quirks & q)
#define ALLOW_DISABLE_KBD(kbdc)	!HAS_QUIRK(kbdc, KBDC_QUIRK_KEEP_ACTIVATED)

#define DEFAULT_DELAY		0x1  /* 500ms */
#define DEFAULT_RATE		0x10 /* 14Hz */

#ifdef EVDEV_SUPPORT
#define PS2_KEYBOARD_VENDOR	1
#define PS2_KEYBOARD_PRODUCT	1
#endif

int
atkbd_probe_unit(device_t dev, int irq, int flags)
{
	keyboard_switch_t *sw;
	int args[2];
	int error;

	sw = kbd_get_switch(ATKBD_DRIVER_NAME);
	if (sw == NULL)
		return ENXIO;

	args[0] = device_get_unit(device_get_parent(dev));
	args[1] = irq;
	error = (*sw->probe)(device_get_unit(dev), args, flags);
	if (error)
		return error;
	return 0;
}

int
atkbd_attach_unit(device_t dev, keyboard_t **kbd, int irq, int flags)
{
	keyboard_switch_t *sw;
	atkbd_state_t *state;
	int args[2];
	int error;
	int unit;

	sw = kbd_get_switch(ATKBD_DRIVER_NAME);
	if (sw == NULL)
		return ENXIO;

	/* reset, initialize and enable the device */
	unit = device_get_unit(dev);
	args[0] = device_get_unit(device_get_parent(dev));
	args[1] = irq;
	*kbd = NULL;
	error = (*sw->probe)(unit, args, flags);
	if (error)
		return error;
	error = (*sw->init)(unit, kbd, args, flags);
	if (error)
		return error;
	(*sw->enable)(*kbd);

#ifdef KBD_INSTALL_CDEV
	/* attach a virtual keyboard cdev */
	error = kbd_attach(*kbd);
	if (error)
		return error;
#endif

	/*
	 * This is a kludge to compensate for lost keyboard interrupts.
	 * A similar code used to be in syscons. See below. XXX
	 */
	state = (atkbd_state_t *)(*kbd)->kb_data;
	callout_init(&state->ks_timer, 0);
	atkbd_timeout(*kbd);

	if (bootverbose)
		(*sw->diag)(*kbd, bootverbose);

	EVENTHANDLER_REGISTER(shutdown_final, atkbd_shutdown_final, *kbd,
	    SHUTDOWN_PRI_DEFAULT);

	return 0;
}

static void
atkbd_timeout(void *arg)
{
	atkbd_state_t *state;
	keyboard_t *kbd;
	int s;

	/*
	 * The original text of the following comments are extracted 
	 * from syscons.c (1.287)
	 * 
	 * With release 2.1 of the Xaccel server, the keyboard is left
	 * hanging pretty often. Apparently an interrupt from the
	 * keyboard is lost, and I don't know why (yet).
	 * This ugly hack calls the low-level interrupt routine if input
	 * is ready for the keyboard and conveniently hides the problem. XXX
	 *
	 * Try removing anything stuck in the keyboard controller; whether
	 * it's a keyboard scan code or mouse data. The low-level
	 * interrupt routine doesn't read the mouse data directly, 
	 * but the keyboard controller driver will, as a side effect.
	 */
	/*
	 * And here is bde's original comment about this:
	 *
	 * This is necessary to handle edge triggered interrupts - if we
	 * returned when our IRQ is high due to unserviced input, then there
	 * would be no more keyboard IRQs until the keyboard is reset by
	 * external powers.
	 *
	 * The keyboard apparently unwedges the irq in most cases.
	 */
	s = spltty();
	kbd = (keyboard_t *)arg;
	if (kbdd_lock(kbd, TRUE)) {
		/*
		 * We have seen the lock flag is not set. Let's reset
		 * the flag early, otherwise the LED update routine fails
		 * which may want the lock during the interrupt routine.
		 */
		kbdd_lock(kbd, FALSE);
		if (kbdd_check_char(kbd))
			kbdd_intr(kbd, NULL);
	}
	splx(s);
	state = (atkbd_state_t *)kbd->kb_data;
	callout_reset(&state->ks_timer, hz / 10, atkbd_timeout, arg);
}

/* LOW-LEVEL */

#define ATKBD_DEFAULT	0

/* keyboard driver declaration */
static int		atkbd_configure(int flags);
static kbd_probe_t	atkbd_probe;
static kbd_init_t	atkbd_init;
static kbd_term_t	atkbd_term;
static kbd_intr_t	atkbd_intr;
static kbd_test_if_t	atkbd_test_if;
static kbd_enable_t	atkbd_enable;
static kbd_disable_t	atkbd_disable;
static kbd_read_t	atkbd_read;
static kbd_check_t	atkbd_check;
static kbd_read_char_t	atkbd_read_char;
static kbd_check_char_t	atkbd_check_char;
static kbd_ioctl_t	atkbd_ioctl;
static kbd_lock_t	atkbd_lock;
static kbd_clear_state_t atkbd_clear_state;
static kbd_get_state_t	atkbd_get_state;
static kbd_set_state_t	atkbd_set_state;
static kbd_poll_mode_t	atkbd_poll;

static keyboard_switch_t atkbdsw = {
	atkbd_probe,
	atkbd_init,
	atkbd_term,
	atkbd_intr,
	atkbd_test_if,
	atkbd_enable,
	atkbd_disable,
	atkbd_read,
	atkbd_check,
	atkbd_read_char,
	atkbd_check_char,
	atkbd_ioctl,
	atkbd_lock,
	atkbd_clear_state,
	atkbd_get_state,
	atkbd_set_state,
	genkbd_get_fkeystr,
	atkbd_poll,
	genkbd_diag,
};

KEYBOARD_DRIVER(atkbd, atkbdsw, atkbd_configure);

/* local functions */
static int		set_typematic(keyboard_t *kbd);
static int		setup_kbd_port(KBDC kbdc, int port, int intr);
static int		get_kbd_echo(KBDC kbdc);
static int		probe_keyboard(KBDC kbdc, int flags);
static int		init_keyboard(KBDC kbdc, int *type, int flags);
static int		write_kbd(KBDC kbdc, int command, int data);
static int		get_kbd_id(KBDC kbdc);
static int		typematic(int delay, int rate);
static int		typematic_delay(int delay);
static int		typematic_rate(int rate);

#ifdef EVDEV_SUPPORT
static evdev_event_t atkbd_ev_event;

static const struct evdev_methods atkbd_evdev_methods = {
	.ev_event = atkbd_ev_event,
};
#endif

/* local variables */

/* the initial key map, accent map and fkey strings */
#ifdef ATKBD_DFLT_KEYMAP
#define KBD_DFLT_KEYMAP
#include "atkbdmap.h"
#endif
#include <dev/kbd/kbdtables.h>

/* structures for the default keyboard */
static keyboard_t	default_kbd;
static atkbd_state_t	default_kbd_state;
static keymap_t		default_keymap;
static accentmap_t	default_accentmap;
static fkeytab_t	default_fkeytab[NUM_FKEYS];

/* 
 * The back door to the keyboard driver!
 * This function is called by the console driver, via the kbdio module,
 * to tickle keyboard drivers when the low-level console is being initialized.
 * Almost nothing in the kernel has been initialied yet.  Try to probe
 * keyboards if possible.
 * NOTE: because of the way the low-level console is initialized, this routine
 * may be called more than once!!
 */
static int
atkbd_configure(int flags)
{
	keyboard_t *kbd;
	int arg[2];
	int i;

	/*
	 * Probe the keyboard controller, if not present or if the driver
	 * is disabled, unregister the keyboard if any.
	 */
	if (atkbdc_configure() != 0 ||
	    resource_disabled("atkbd", ATKBD_DEFAULT)) {
		i = kbd_find_keyboard(ATKBD_DRIVER_NAME, ATKBD_DEFAULT);
		if (i >= 0) {
			kbd = kbd_get_keyboard(i);
			kbd_unregister(kbd);
			kbd->kb_flags &= ~KB_REGISTERED;
		}
		return 0;
	}
	
	/* XXX: a kludge to obtain the device configuration flags */
	if (resource_int_value("atkbd", ATKBD_DEFAULT, "flags", &i) == 0)
		flags |= i;

	/* probe the default keyboard */
	arg[0] = -1;
	arg[1] = -1;
	kbd = NULL;
	if (atkbd_probe(ATKBD_DEFAULT, arg, flags))
		return 0;
	if (atkbd_init(ATKBD_DEFAULT, &kbd, arg, flags))
		return 0;

	/* return the number of found keyboards */
	return 1;
}

/* low-level functions */

/* detect a keyboard */
static int
atkbd_probe(int unit, void *arg, int flags)
{
	KBDC kbdc;
	int *data = (int *)arg;	/* data[0]: controller, data[1]: irq */

	/* XXX */
	if (unit == ATKBD_DEFAULT) {
		if (KBD_IS_PROBED(&default_kbd))
			return 0;
	}

	kbdc = atkbdc_open(data[0]);
	if (kbdc == NULL)
		return ENXIO;
	if (probe_keyboard(kbdc, flags)) {
		if (flags & KB_CONF_FAIL_IF_NO_KBD)
			return ENXIO;
	}
	return 0;
}

/* reset and initialize the device */
static int
atkbd_init(int unit, keyboard_t **kbdp, void *arg, int flags)
{
	keyboard_t *kbd;
	atkbd_state_t *state;
	keymap_t *keymap;
	accentmap_t *accmap;
	fkeytab_t *fkeymap;
	int fkeymap_size;
	int delay[2];
	int *data = (int *)arg;	/* data[0]: controller, data[1]: irq */
	int error, needfree;
#ifdef EVDEV_SUPPORT
	struct evdev_dev *evdev;
	char phys_loc[8];
#endif

	/* XXX */
	if (unit == ATKBD_DEFAULT) {
		*kbdp = kbd = &default_kbd;
		if (KBD_IS_INITIALIZED(kbd) && KBD_IS_CONFIGURED(kbd))
			return 0;
		state = &default_kbd_state;
		keymap = &default_keymap;
		accmap = &default_accentmap;
		fkeymap = default_fkeytab;
		fkeymap_size = nitems(default_fkeytab);
		needfree = 0;
	} else if (*kbdp == NULL) {
		*kbdp = kbd = malloc(sizeof(*kbd), M_DEVBUF, M_NOWAIT | M_ZERO);
		state = malloc(sizeof(*state), M_DEVBUF, M_NOWAIT | M_ZERO);
		/* NB: these will always be initialized 'cuz !KBD_IS_PROBED */
		keymap = malloc(sizeof(key_map), M_DEVBUF, M_NOWAIT);
		accmap = malloc(sizeof(accent_map), M_DEVBUF, M_NOWAIT);
		fkeymap = malloc(sizeof(fkey_tab), M_DEVBUF, M_NOWAIT);
		fkeymap_size = sizeof(fkey_tab)/sizeof(fkey_tab[0]);
		needfree = 1;
		if ((kbd == NULL) || (state == NULL) || (keymap == NULL)
		     || (accmap == NULL) || (fkeymap == NULL)) {
			error = ENOMEM;
			goto bad;
		}
	} else if (KBD_IS_INITIALIZED(*kbdp) && KBD_IS_CONFIGURED(*kbdp)) {
		return 0;
	} else {
		kbd = *kbdp;
		state = (atkbd_state_t *)kbd->kb_data;
		bzero(state, sizeof(*state));
		keymap = kbd->kb_keymap;
		accmap = kbd->kb_accentmap;
		fkeymap = kbd->kb_fkeytab;
		fkeymap_size = kbd->kb_fkeytab_size;
		needfree = 0;
	}

	if (!KBD_IS_PROBED(kbd)) {
		state->kbdc = atkbdc_open(data[0]);
		if (state->kbdc == NULL) {
			error = ENXIO;
			goto bad;
		}
		kbd_init_struct(kbd, ATKBD_DRIVER_NAME, KB_OTHER, unit, flags,
				0, 0);
		bcopy(&key_map, keymap, sizeof(key_map));
		bcopy(&accent_map, accmap, sizeof(accent_map));
		bcopy(fkey_tab, fkeymap,
		    imin(fkeymap_size * sizeof(fkeymap[0]), sizeof(fkey_tab)));
		kbd_set_maps(kbd, keymap, accmap, fkeymap, fkeymap_size);
		kbd->kb_data = (void *)state;
	
		if (probe_keyboard(state->kbdc, flags)) { /* shouldn't happen */
			if (flags & KB_CONF_FAIL_IF_NO_KBD) {
				error = ENXIO;
				goto bad;
			}
		} else {
			KBD_FOUND_DEVICE(kbd);
		}
		atkbd_clear_state(kbd);
		state->ks_mode = K_XLATE;
		/* 
		 * FIXME: set the initial value for lock keys in ks_state
		 * according to the BIOS data?
		 */
		KBD_PROBE_DONE(kbd);
	}
	if (!KBD_IS_INITIALIZED(kbd) && !(flags & KB_CONF_PROBE_ONLY)) {
		kbd->kb_config = flags & ~KB_CONF_PROBE_ONLY;
		if (KBD_HAS_DEVICE(kbd)
		    && init_keyboard(state->kbdc, &kbd->kb_type, kbd->kb_config)
		    && (kbd->kb_config & KB_CONF_FAIL_IF_NO_KBD)) {
			kbd_unregister(kbd);
			error = ENXIO;
			goto bad;
		}
		atkbd_ioctl(kbd, KDSETLED, (caddr_t)&state->ks_state);
		set_typematic(kbd);
		delay[0] = kbd->kb_delay1;
		delay[1] = kbd->kb_delay2;
		atkbd_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);

#ifdef EVDEV_SUPPORT
		/* register as evdev provider on first init */
		if (state->ks_evdev == NULL) {
			snprintf(phys_loc, sizeof(phys_loc), "atkbd%d", unit);
			evdev = evdev_alloc();
			evdev_set_name(evdev, "AT keyboard");
			evdev_set_phys(evdev, phys_loc);
			evdev_set_id(evdev, BUS_I8042, PS2_KEYBOARD_VENDOR,
			    PS2_KEYBOARD_PRODUCT, 0);
			evdev_set_methods(evdev, kbd, &atkbd_evdev_methods);
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
		}
#endif

		KBD_INIT_DONE(kbd);
	}
	if (!KBD_IS_CONFIGURED(kbd)) {
		if (kbd_register(kbd) < 0) {
			error = ENXIO;
			goto bad;
		}
		KBD_CONFIG_DONE(kbd);
	}

	return 0;
bad:
	if (needfree) {
		if (state != NULL)
			free(state, M_DEVBUF);
		if (keymap != NULL)
			free(keymap, M_DEVBUF);
		if (accmap != NULL)
			free(accmap, M_DEVBUF);
		if (fkeymap != NULL)
			free(fkeymap, M_DEVBUF);
		if (kbd != NULL) {
			free(kbd, M_DEVBUF);
			*kbdp = NULL;	/* insure ref doesn't leak to caller */
		}
	}
	return error;
}

/* finish using this keyboard */
static int
atkbd_term(keyboard_t *kbd)
{
	atkbd_state_t *state = (atkbd_state_t *)kbd->kb_data;

	kbd_unregister(kbd);
	callout_drain(&state->ks_timer);
	return 0;
}

/* keyboard interrupt routine */
static int
atkbd_intr(keyboard_t *kbd, void *arg)
{
	atkbd_state_t *state = (atkbd_state_t *)kbd->kb_data;
	int delay[2];
	int c;

	if (!KBD_HAS_DEVICE(kbd)) {
		/*
		 * The keyboard was not detected before;
		 * it must have been reconnected!
		 */
		init_keyboard(state->kbdc, &kbd->kb_type, kbd->kb_config);
		KBD_FOUND_DEVICE(kbd);
		atkbd_ioctl(kbd, KDSETLED, (caddr_t)&state->ks_state);
		set_typematic(kbd);
		delay[0] = kbd->kb_delay1;
		delay[1] = kbd->kb_delay2;
		atkbd_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);
	}

	if (state->ks_polling)
		return 0;

	if (KBD_IS_ACTIVE(kbd) && KBD_IS_BUSY(kbd)) {
		/* let the callback function to process the input */
		(*kbd->kb_callback.kc_func)(kbd, KBDIO_KEYINPUT,
					    kbd->kb_callback.kc_arg);
	} else {
		/* read and discard the input; no one is waiting for input */
		do {
			c = atkbd_read_char(kbd, FALSE);
		} while (c != NOKEY);
	}
	return 0;
}

/* test the interface to the device */
static int
atkbd_test_if(keyboard_t *kbd)
{
	int error;
	int s;

	error = 0;
	empty_both_buffers(((atkbd_state_t *)kbd->kb_data)->kbdc, 10);
	s = spltty();
	if (!test_controller(((atkbd_state_t *)kbd->kb_data)->kbdc))
		error = EIO;
	else if (test_kbd_port(((atkbd_state_t *)kbd->kb_data)->kbdc) != 0)
		error = EIO;
	splx(s);

	return error;
}

/* 
 * Enable the access to the device; until this function is called,
 * the client cannot read from the keyboard.
 */
static int
atkbd_enable(keyboard_t *kbd)
{
	int s;

	s = spltty();
	KBD_ACTIVATE(kbd);
	splx(s);
	return 0;
}

/* disallow the access to the device */
static int
atkbd_disable(keyboard_t *kbd)
{
	int s;

	s = spltty();
	KBD_DEACTIVATE(kbd);
	splx(s);
	return 0;
}

/* read one byte from the keyboard if it's allowed */
static int
atkbd_read(keyboard_t *kbd, int wait)
{
	int c;

	if (wait)
		c = read_kbd_data(((atkbd_state_t *)kbd->kb_data)->kbdc);
	else
		c = read_kbd_data_no_wait(((atkbd_state_t *)kbd->kb_data)->kbdc);
	if (c != -1)
		++kbd->kb_count;
	return (KBD_IS_ACTIVE(kbd) ? c : -1);
}

/* check if data is waiting */
static int
atkbd_check(keyboard_t *kbd)
{
	if (!KBD_IS_ACTIVE(kbd))
		return FALSE;
	return kbdc_data_ready(((atkbd_state_t *)kbd->kb_data)->kbdc);
}

/* read char from the keyboard */
static u_int
atkbd_read_char(keyboard_t *kbd, int wait)
{
	atkbd_state_t *state;
	u_int action;
	int scancode;
	int keycode;

	state = (atkbd_state_t *)kbd->kb_data;
next_code:
	/* do we have a composed char to return? */
	if (!(state->ks_flags & COMPOSE) && (state->ks_composed_char > 0)) {
		action = state->ks_composed_char;
		state->ks_composed_char = 0;
		if (action > UCHAR_MAX)
			return ERRKEY;
		return action;
	}

	/* see if there is something in the keyboard port */
	if (wait) {
		do {
			scancode = read_kbd_data(state->kbdc);
		} while (scancode == -1);
	} else {
		scancode = read_kbd_data_no_wait(state->kbdc);
		if (scancode == -1)
			return NOKEY;
	}
	++kbd->kb_count;

#if KBDIO_DEBUG >= 10
	printf("atkbd_read_char(): scancode:0x%x\n", scancode);
#endif

#ifdef EVDEV_SUPPORT
	/* push evdev event */
	if (evdev_rcpt_mask & EVDEV_RCPT_HW_KBD && state->ks_evdev != NULL) {
		keycode = evdev_scancode2key(&state->ks_evdev_state,
		    scancode);

		if (keycode != KEY_RESERVED) {
			evdev_push_event(state->ks_evdev, EV_KEY,
			    (uint16_t)keycode, scancode & 0x80 ? 0 : 1);
			evdev_sync(state->ks_evdev);
		}
	}
#endif

	/* return the byte as is for the K_RAW mode */
	if (state->ks_mode == K_RAW)
		return scancode;

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
	case 0xE0:		/* 0xE0 prefix */
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
	if (state->ks_mode == K_CODE)
		return (keycode | (scancode & 0x80));

	/* compose a character code */
	if (state->ks_flags & COMPOSE) {
		switch (keycode | (scancode & 0x80)) {
		/* key pressed, process it */
		case 0x47: case 0x48: case 0x49:	/* keypad 7,8,9 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x40;
			if (state->ks_composed_char > UCHAR_MAX)
				return ERRKEY;
			goto next_code;
		case 0x4B: case 0x4C: case 0x4D:	/* keypad 4,5,6 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x47;
			if (state->ks_composed_char > UCHAR_MAX)
				return ERRKEY;
			goto next_code;
		case 0x4F: case 0x50: case 0x51:	/* keypad 1,2,3 */
			state->ks_composed_char *= 10;
			state->ks_composed_char += keycode - 0x4E;
			if (state->ks_composed_char > UCHAR_MAX)
				return ERRKEY;
			goto next_code;
		case 0x52:				/* keypad 0 */
			state->ks_composed_char *= 10;
			if (state->ks_composed_char > UCHAR_MAX)
				return ERRKEY;
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
				return ERRKEY;
			}
			break;
		}
	}

	/* keycode to key action */
	action = genkbd_keyaction(kbd, keycode, scancode & 0x80,
				  &state->ks_state, &state->ks_accents);
	if (action == NOKEY)
		goto next_code;
	else
		return action;
}

/* check if char is waiting */
static int
atkbd_check_char(keyboard_t *kbd)
{
	atkbd_state_t *state;

	if (!KBD_IS_ACTIVE(kbd))
		return FALSE;
	state = (atkbd_state_t *)kbd->kb_data;
	if (!(state->ks_flags & COMPOSE) && (state->ks_composed_char > 0))
		return TRUE;
	return kbdc_data_ready(state->kbdc);
}

/* some useful control functions */
static int
atkbd_ioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	/* translate LED_XXX bits into the device specific bits */
	static u_char ledmap[8] = {
		0, 4, 2, 6, 1, 5, 3, 7,
	};
	atkbd_state_t *state = kbd->kb_data;
	int error;
	int s;
	int i;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	int ival;
#endif

	s = spltty();
	switch (cmd) {

	case KDGKBMODE:		/* get keyboard mode */
		*(int *)arg = state->ks_mode;
		break;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
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
			}
			/* FALLTHROUGH */
		case K_RAW:
		case K_CODE:
			if (state->ks_mode != *(int *)arg) {
				atkbd_clear_state(kbd);
				state->ks_mode = *(int *)arg;
			}
			break;
		default:
			splx(s);
			return EINVAL;
		}
		break;

	case KDGETLED:		/* get keyboard LED */
		*(int *)arg = KBD_LED_VAL(kbd);
		break;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IO('K', 66):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETLED:		/* set keyboard LED */
		/* NOTE: lock key state in ks_state won't be changed */
		if (*(int *)arg & ~LOCK_MASK) {
			splx(s);
			return EINVAL;
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
		if (KBD_HAS_DEVICE(kbd)) {
			error = write_kbd(state->kbdc, KBDC_SET_LEDS,
					  ledmap[i & LED_MASK]);
			if (error) {
				splx(s);
				return error;
			}
		}
#ifdef EVDEV_SUPPORT
		/* push LED states to evdev */
		if (state->ks_evdev != NULL &&
		    evdev_rcpt_mask & EVDEV_RCPT_HW_KBD)
			evdev_push_leds(state->ks_evdev, *(int *)arg);
#endif
		KBD_LED_VAL(kbd) = *(int *)arg;
		break;

	case KDGKBSTATE:	/* get lock key state */
		*(int *)arg = state->ks_state & LOCK_MASK;
		break;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IO('K', 20):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBSTATE:	/* set lock key state */
		if (*(int *)arg & ~LOCK_MASK) {
			splx(s);
			return EINVAL;
		}
		state->ks_state &= ~LOCK_MASK;
		state->ks_state |= *(int *)arg;
		splx(s);
		/* set LEDs and quit */
		return atkbd_ioctl(kbd, KDSETLED, arg);

	case KDSETREPEAT:	/* set keyboard repeat rate (new interface) */
		splx(s);
		if (!KBD_HAS_DEVICE(kbd))
			return 0;
		i = typematic(((int *)arg)[0], ((int *)arg)[1]);
		error = write_kbd(state->kbdc, KBDC_SET_TYPEMATIC, i);
		if (error == 0) {
			kbd->kb_delay1 = typematic_delay(i);
			kbd->kb_delay2 = typematic_rate(i);
#ifdef EVDEV_SUPPORT
			if (state->ks_evdev != NULL &&
			    evdev_rcpt_mask & EVDEV_RCPT_HW_KBD)
				evdev_push_repeats(state->ks_evdev, kbd);
#endif
		}
		return error;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IO('K', 67):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETRAD:		/* set keyboard repeat rate (old interface) */
		splx(s);
		if (!KBD_HAS_DEVICE(kbd))
			return 0;
		error = write_kbd(state->kbdc, KBDC_SET_TYPEMATIC, *(int *)arg);
		if (error == 0) {
			kbd->kb_delay1 = typematic_delay(*(int *)arg);
			kbd->kb_delay2 = typematic_rate(*(int *)arg);
#ifdef EVDEV_SUPPORT
			if (state->ks_evdev != NULL &&
			    evdev_rcpt_mask & EVDEV_RCPT_HW_KBD)
				evdev_push_repeats(state->ks_evdev, kbd);
#endif
		}
		return error;

	case PIO_KEYMAP:	/* set keyboard translation table */
	case OPIO_KEYMAP:	/* set keyboard translation table (compat) */
	case PIO_KEYMAPENT:	/* set keyboard translation table entry */
	case PIO_DEADKEYMAP:	/* set accent key translation table */
		state->ks_accents = 0;
		/* FALLTHROUGH */
	default:
		splx(s);
		return genkbd_commonioctl(kbd, cmd, arg);
	}

	splx(s);
	return 0;
}

/* lock the access to the keyboard */
static int
atkbd_lock(keyboard_t *kbd, int lock)
{
	return kbdc_lock(((atkbd_state_t *)kbd->kb_data)->kbdc, lock);
}

/* clear the internal state of the keyboard */
static void
atkbd_clear_state(keyboard_t *kbd)
{
	atkbd_state_t *state;

	state = (atkbd_state_t *)kbd->kb_data;
	state->ks_flags = 0;
	state->ks_polling = 0;
	state->ks_state &= LOCK_MASK;	/* preserve locking key state */
	state->ks_accents = 0;
	state->ks_composed_char = 0;
#if 0
	state->ks_prefix = 0; /* XXX */
#endif
}

/* save the internal state */
static int
atkbd_get_state(keyboard_t *kbd, void *buf, size_t len)
{
	if (len == 0)
		return sizeof(atkbd_state_t);
	if (len < sizeof(atkbd_state_t))
		return -1;
	bcopy(kbd->kb_data, buf, sizeof(atkbd_state_t));
	return 0;
}

/* set the internal state */
static int
atkbd_set_state(keyboard_t *kbd, void *buf, size_t len)
{
	if (len < sizeof(atkbd_state_t))
		return ENOMEM;
	if (((atkbd_state_t *)kbd->kb_data)->kbdc
		!= ((atkbd_state_t *)buf)->kbdc)
		return ENOMEM;
	bcopy(buf, kbd->kb_data, sizeof(atkbd_state_t));
	return 0;
}

static int
atkbd_poll(keyboard_t *kbd, int on)
{
	atkbd_state_t *state;
	int s;

	state = (atkbd_state_t *)kbd->kb_data;
	s = spltty();
	if (on)
		++state->ks_polling;
	else
		--state->ks_polling;
	splx(s);
	return 0;
}

static void
atkbd_shutdown_final(void *v)
{
#ifdef __sparc64__
	keyboard_t *kbd = v;
	KBDC kbdc = ((atkbd_state_t *)kbd->kb_data)->kbdc;

	/*
	 * Turn off the translation in preparation for handing the keyboard
	 * over to the OFW as the OBP driver doesn't use translation and
	 * also doesn't disable it itself resulting in a broken keymap at
	 * the boot prompt. Also disable the aux port and the interrupts as
	 * the OBP driver doesn't use them, i.e. polls the keyboard. Not
	 * disabling the interrupts doesn't cause real problems but the
	 * responsiveness is a bit better when they are turned off.
	 */
	send_kbd_command(kbdc, KBDC_DISABLE_KBD);
	set_controller_command_byte(kbdc,
	    KBD_AUX_CONTROL_BITS | KBD_KBD_CONTROL_BITS | KBD_TRANSLATION,
	    KBD_DISABLE_AUX_PORT | KBD_DISABLE_KBD_INT | KBD_ENABLE_KBD_PORT);
	send_kbd_command(kbdc, KBDC_ENABLE_KBD);
#endif
}

static int
atkbd_reset(KBDC kbdc, int flags, int c)
{
	/* reset keyboard hardware */
	if (!(flags & KB_CONF_NO_RESET) && !reset_kbd(kbdc)) {
		/*
		 * KEYBOARD ERROR
		 * Keyboard reset may fail either because the keyboard
		 * doen't exist, or because the keyboard doesn't pass
		 * the self-test, or the keyboard controller on the
		 * motherboard and the keyboard somehow fail to shake hands.
		 * It is just possible, particularly in the last case,
		 * that the keyboard controller may be left in a hung state.
		 * test_controller() and test_kbd_port() appear to bring
		 * the keyboard controller back (I don't know why and how,
		 * though.)
		 */
		empty_both_buffers(kbdc, 10);
		test_controller(kbdc);
		test_kbd_port(kbdc);
		/*
		 * We could disable the keyboard port and interrupt... but, 
		 * the keyboard may still exist (see above). 
		 */
		set_controller_command_byte(kbdc,
		    ALLOW_DISABLE_KBD(kbdc) ? 0xff : KBD_KBD_CONTROL_BITS, c);
		if (bootverbose)
			printf("atkbd: failed to reset the keyboard.\n");
		return (EIO);
	}
	return (0);
}

#ifdef EVDEV_SUPPORT
static void
atkbd_ev_event(struct evdev_dev *evdev, uint16_t type, uint16_t code,
    int32_t value)
{
	keyboard_t *kbd = evdev_get_softc(evdev);

	if (evdev_rcpt_mask & EVDEV_RCPT_HW_KBD &&
	    (type == EV_LED || type == EV_REP)) {
		mtx_lock(&Giant);
		kbd_ev_event(kbd, type, code, value);
		mtx_unlock(&Giant);
	}
}
#endif

/* local functions */

static int
set_typematic(keyboard_t *kbd)
{
	int val, error;
	atkbd_state_t *state = kbd->kb_data;

	val = typematic(DEFAULT_DELAY, DEFAULT_RATE);
	error = write_kbd(state->kbdc, KBDC_SET_TYPEMATIC, val);
	if (error == 0) {
		kbd->kb_delay1 = typematic_delay(val);
		kbd->kb_delay2 = typematic_rate(val);
	}

	return (error);
}

static int
setup_kbd_port(KBDC kbdc, int port, int intr)
{
	if (!set_controller_command_byte(kbdc,
		KBD_KBD_CONTROL_BITS,
		((port) ? KBD_ENABLE_KBD_PORT : KBD_DISABLE_KBD_PORT)
		    | ((intr) ? KBD_ENABLE_KBD_INT : KBD_DISABLE_KBD_INT)))
		return 1;
	return 0;
}

static int
get_kbd_echo(KBDC kbdc)
{
	/* enable the keyboard port, but disable the keyboard intr. */
	if (setup_kbd_port(kbdc, TRUE, FALSE))
		/* CONTROLLER ERROR: there is very little we can do... */
		return ENXIO;

	/* see if something is present */
	write_kbd_command(kbdc, KBDC_ECHO);
	if (read_kbd_data(kbdc) != KBD_ECHO) {
		empty_both_buffers(kbdc, 10);
		test_controller(kbdc);
		test_kbd_port(kbdc);
		return ENXIO;
	}

	/* enable the keyboard port and intr. */
	if (setup_kbd_port(kbdc, TRUE, TRUE)) {
		/*
		 * CONTROLLER ERROR 
		 * This is serious; the keyboard intr is left disabled! 
		 */
		return ENXIO;
	}

	return 0;
}

static int
probe_keyboard(KBDC kbdc, int flags)
{
	/*
	 * Don't try to print anything in this function.  The low-level 
	 * console may not have been initialized yet...
	 */
	int err;
	int c;
	int m;

	if (!kbdc_lock(kbdc, TRUE)) {
		/* driver error? */
		return ENXIO;
	}

	/* temporarily block data transmission from the keyboard */
	write_controller_command(kbdc, KBDC_DISABLE_KBD_PORT);

	/* flush any noise in the buffer */
	empty_both_buffers(kbdc, 100);

	/* save the current keyboard controller command byte */
	m = kbdc_get_device_mask(kbdc) & ~KBD_KBD_CONTROL_BITS;
	c = get_controller_command_byte(kbdc);
	if (c == -1) {
		/* CONTROLLER ERROR */
		kbdc_set_device_mask(kbdc, m);
		kbdc_lock(kbdc, FALSE);
		return ENXIO;
	}

	/* 
	 * The keyboard may have been screwed up by the boot block.
	 * We may just be able to recover from error by testing the controller
	 * and the keyboard port. The controller command byte needs to be
	 * saved before this recovery operation, as some controllers seem 
	 * to set the command byte to particular values.
	 */
	test_controller(kbdc);
	if (!(flags & KB_CONF_NO_PROBE_TEST))
		test_kbd_port(kbdc);

	err = get_kbd_echo(kbdc);

	/*
	 * Even if the keyboard doesn't seem to be present (err != 0),
	 * we shall enable the keyboard port and interrupt so that
	 * the driver will be operable when the keyboard is attached
	 * to the system later.  It is NOT recommended to hot-plug
	 * the AT keyboard, but many people do so...
	 */
	kbdc_set_device_mask(kbdc, m | KBD_KBD_CONTROL_BITS);
	setup_kbd_port(kbdc, TRUE, TRUE);
#if 0
	if (err == 0) {
		kbdc_set_device_mask(kbdc, m | KBD_KBD_CONTROL_BITS);
	} else {
		/* try to restore the command byte as before */
		set_controller_command_byte(kbdc,
		    ALLOW_DISABLE_KBD(kbdc) ? 0xff : KBD_KBD_CONTROL_BITS, c);
		kbdc_set_device_mask(kbdc, m);
	}
#endif

	kbdc_lock(kbdc, FALSE);
	return (HAS_QUIRK(kbdc, KBDC_QUIRK_IGNORE_PROBE_RESULT) ? 0 : err);
}

static int
init_keyboard(KBDC kbdc, int *type, int flags)
{
	int codeset;
	int id;
	int c;

	if (!kbdc_lock(kbdc, TRUE)) {
		/* driver error? */
		return EIO;
	}

	/* temporarily block data transmission from the keyboard */
	write_controller_command(kbdc, KBDC_DISABLE_KBD_PORT);

	/* save the current controller command byte */
	empty_both_buffers(kbdc, 200);
	c = get_controller_command_byte(kbdc);
	if (c == -1) {
		/* CONTROLLER ERROR */
		kbdc_lock(kbdc, FALSE);
		printf("atkbd: unable to get the current command byte value.\n");
		return EIO;
	}
	if (bootverbose)
		printf("atkbd: the current kbd controller command byte %04x\n",
		   c);
#if 0
	/* override the keyboard lock switch */
	c |= KBD_OVERRIDE_KBD_LOCK;
#endif

	/* enable the keyboard port, but disable the keyboard intr. */
	if (setup_kbd_port(kbdc, TRUE, FALSE)) {
		/* CONTROLLER ERROR: there is very little we can do... */
		printf("atkbd: unable to set the command byte.\n");
		kbdc_lock(kbdc, FALSE);
		return EIO;
	}

	if (HAS_QUIRK(kbdc, KBDC_QUIRK_RESET_AFTER_PROBE) &&
	    atkbd_reset(kbdc, flags, c)) {
		kbdc_lock(kbdc, FALSE);
		return EIO;
	}

	/* 
	 * Check if we have an XT keyboard before we attempt to reset it. 
	 * The procedure assumes that the keyboard and the controller have 
	 * been set up properly by BIOS and have not been messed up 
	 * during the boot process.
	 */
	codeset = -1;
	if (flags & KB_CONF_ALT_SCANCODESET)
		/* the user says there is a XT keyboard */
		codeset = 1;
#ifdef KBD_DETECT_XT_KEYBOARD
	else if ((c & KBD_TRANSLATION) == 0) {
		/* SET_SCANCODE_SET is not always supported; ignore error */
		if (send_kbd_command_and_data(kbdc, KBDC_SET_SCANCODE_SET, 0)
			== KBD_ACK) 
			codeset = read_kbd_data(kbdc);
	}
	if (bootverbose)
		printf("atkbd: scancode set %d\n", codeset);
#endif /* KBD_DETECT_XT_KEYBOARD */
 
	*type = KB_OTHER;
	id = get_kbd_id(kbdc);
	switch(id) {
	case 0x41ab:	/* 101/102/... Enhanced */
	case 0x83ab:	/* ditto */
	case 0x54ab:	/* SpaceSaver */
	case 0x84ab:	/* ditto */
#if 0
	case 0x90ab:	/* 'G' */
	case 0x91ab:	/* 'P' */
	case 0x92ab:	/* 'A' */
#endif
		*type = KB_101;
		break;
	case -1:	/* AT 84 keyboard doesn't return ID */
		*type = KB_84;
		break;
	default:
		break;
	}
	if (bootverbose)
		printf("atkbd: keyboard ID 0x%x (%d)\n", id, *type);

	if (!HAS_QUIRK(kbdc, KBDC_QUIRK_RESET_AFTER_PROBE) &&
	    atkbd_reset(kbdc, flags, c)) {
		kbdc_lock(kbdc, FALSE);
		return EIO;
	}

	/*
	 * Allow us to set the XT_KEYBD flag so that keyboards
	 * such as those on the IBM ThinkPad laptop computers can be used
	 * with the standard console driver.
	 */
	if (codeset == 1) {
		if (send_kbd_command_and_data(kbdc,
			KBDC_SET_SCANCODE_SET, codeset) == KBD_ACK) {
			/* XT kbd doesn't need scan code translation */
			c &= ~KBD_TRANSLATION;
		} else {
			/*
			 * KEYBOARD ERROR 
			 * The XT kbd isn't usable unless the proper scan
			 * code set is selected. 
			 */
			set_controller_command_byte(kbdc, ALLOW_DISABLE_KBD(kbdc)
			    ? 0xff : KBD_KBD_CONTROL_BITS, c);
			kbdc_lock(kbdc, FALSE);
			printf("atkbd: unable to set the XT keyboard mode.\n");
			return EIO;
		}
	}

#if defined(__sparc64__)
	if (send_kbd_command_and_data(
		kbdc, KBDC_SET_SCANCODE_SET, 2) != KBD_ACK) {
		printf("atkbd: can't set translation.\n");
	}
	c |= KBD_TRANSLATION;
#endif

	/*
	 * Some keyboards require a SETLEDS command to be sent after
	 * the reset command before they will send keystrokes to us
	 */
	if (HAS_QUIRK(kbdc, KBDC_QUIRK_SETLEDS_ON_INIT) &&
	    send_kbd_command_and_data(kbdc, KBDC_SET_LEDS, 0) != KBD_ACK) {
		printf("atkbd: setleds failed\n");
	}
	if (!ALLOW_DISABLE_KBD(kbdc))
	    send_kbd_command(kbdc, KBDC_ENABLE_KBD);

	/* enable the keyboard port and intr. */
	if (!set_controller_command_byte(kbdc, 
		KBD_KBD_CONTROL_BITS | KBD_TRANSLATION | KBD_OVERRIDE_KBD_LOCK,
		(c & (KBD_TRANSLATION | KBD_OVERRIDE_KBD_LOCK))
		    | KBD_ENABLE_KBD_PORT | KBD_ENABLE_KBD_INT)) {
		/*
		 * CONTROLLER ERROR 
		 * This is serious; we are left with the disabled
		 * keyboard intr. 
		 */
		set_controller_command_byte(kbdc, ALLOW_DISABLE_KBD(kbdc)
		    ? 0xff : (KBD_KBD_CONTROL_BITS | KBD_TRANSLATION |
			KBD_OVERRIDE_KBD_LOCK), c);
		kbdc_lock(kbdc, FALSE);
		printf("atkbd: unable to enable the keyboard port and intr.\n");
		return EIO;
	}

	kbdc_lock(kbdc, FALSE);
	return 0;
}

static int
write_kbd(KBDC kbdc, int command, int data)
{
	int s;

	/* prevent the timeout routine from polling the keyboard */
	if (!kbdc_lock(kbdc, TRUE)) 
		return EBUSY;

	/* disable the keyboard and mouse interrupt */
	s = spltty();
#if 0
	c = get_controller_command_byte(kbdc);
	if ((c == -1) 
	    || !set_controller_command_byte(kbdc, 
		kbdc_get_device_mask(kbdc),
		KBD_DISABLE_KBD_PORT | KBD_DISABLE_KBD_INT
		| KBD_DISABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/* CONTROLLER ERROR */
		kbdc_lock(kbdc, FALSE);
		splx(s);
		return EIO;
	}
	/* 
	 * Now that the keyboard controller is told not to generate 
	 * the keyboard and mouse interrupts, call `splx()' to allow 
	 * the other tty interrupts. The clock interrupt may also occur, 
	 * but the timeout routine (`scrn_timer()') will be blocked 
	 * by the lock flag set via `kbdc_lock()'
	 */
	splx(s);
#endif
	if (send_kbd_command_and_data(kbdc, command, data) != KBD_ACK)
		send_kbd_command(kbdc, KBDC_ENABLE_KBD);
#if 0
	/* restore the interrupts */
	if (!set_controller_command_byte(kbdc, kbdc_get_device_mask(kbdc),
	    c & (KBD_KBD_CONTROL_BITS | KBD_AUX_CONTROL_BITS))) { 
		/* CONTROLLER ERROR */
	}
#else
	splx(s);
#endif
	kbdc_lock(kbdc, FALSE);

	return 0;
}

static int
get_kbd_id(KBDC kbdc)
{
	int id1, id2;

	empty_both_buffers(kbdc, 10);
	id1 = id2 = -1;
	if (send_kbd_command(kbdc, KBDC_SEND_DEV_ID) != KBD_ACK)
		return -1;

	DELAY(10000); 	/* 10 msec delay */
	id1 = read_kbd_data(kbdc);
	if (id1 != -1)
		id2 = read_kbd_data(kbdc);

	if ((id1 == -1) || (id2 == -1)) {
		empty_both_buffers(kbdc, 10);
		test_controller(kbdc);
		test_kbd_port(kbdc);
		return -1;
	}
	return ((id2 << 8) | id1);
}

static int delays[] = { 250, 500, 750, 1000 };
static int rates[] = {  34,  38,  42,  46,  50,  55,  59,  63,
			68,  76,  84,  92, 100, 110, 118, 126,
		       136, 152, 168, 184, 200, 220, 236, 252,
		       272, 304, 336, 368, 400, 440, 472, 504 };

static int
typematic_delay(int i)
{
	return delays[(i >> 5) & 3];
}

static int
typematic_rate(int i)
{
	return rates[i & 0x1f];
}

static int
typematic(int delay, int rate)
{
	int value;
	int i;

	for (i = nitems(delays) - 1; i > 0; --i) {
		if (delay >= delays[i])
			break;
	}
	value = i << 5;
	for (i = nitems(rates) - 1; i > 0; --i) {
		if (rate >= rates[i])
			break;
	}
	value |= i;
	return value;
}
