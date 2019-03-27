/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Jake Burkholder.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kbd.h"
#include "opt_sunkbd.h"

#if (defined(SUNKBD_EMULATE_ATKBD) && defined(SUNKBD_DFLT_KEYMAP)) ||	\
    !defined(SUNKBD_EMULATE_ATKBD)
#define	KBD_DFLT_KEYMAP
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kbio.h>
#include <sys/kernel.h>
#include <sys/limits.h>

#include <machine/bus.h>

#include <dev/kbd/kbdreg.h>
#include <dev/kbd/kbdtables.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>

#include <dev/uart/uart_kbd_sun.h>
#if !defined(SUNKBD_EMULATE_ATKBD)
#include <dev/uart/uart_kbd_sun_tables.h>
#endif

#if defined(SUNKBD_EMULATE_ATKBD) && defined(SUNKBD_DFLT_KEYMAP)
#include "sunkbdmap.h"
#endif
#include "uart_if.h"

#define	SUNKBD_DRIVER_NAME	"sunkbd"

#define	TODO	printf("%s: unimplemented", __func__)

struct sunkbd_softc {
	keyboard_t		sc_kbd;
	struct uart_softc	*sc_uart;
	struct uart_devinfo	*sc_sysdev;

	struct callout		sc_repeat_callout;
	int			sc_repeat_key;

	int			sc_accents;
	int			sc_composed_char;
	int			sc_flags;
#define	KPCOMPOSE			(1 << 0)
	int			sc_mode;
	int			sc_polling;
	int			sc_repeating;
	int			sc_state;

#if defined(SUNKBD_EMULATE_ATKBD)
	int			sc_buffered_char[2];
#endif
};

static int sunkbd_configure(int flags);
static int sunkbd_probe_keyboard(struct uart_devinfo *di);

static int sunkbd_probe(int unit, void *arg, int flags);
static int sunkbd_init(int unit, keyboard_t **kbdp, void *arg, int flags);
static int sunkbd_term(keyboard_t *kbd);
static int sunkbd_intr(keyboard_t *kbd, void *arg);
static int sunkbd_test_if(keyboard_t *kbd);
static int sunkbd_enable(keyboard_t *kbd);
static int sunkbd_disable(keyboard_t *kbd);
static int sunkbd_read(keyboard_t *kbd, int wait);
static int sunkbd_check(keyboard_t *kbd);
static u_int sunkbd_read_char(keyboard_t *kbd, int wait);
static int sunkbd_check_char(keyboard_t *kbd);
static int sunkbd_ioctl(keyboard_t *kbd, u_long cmd, caddr_t data);
static int sunkbd_lock(keyboard_t *kbd, int lock);
static void sunkbd_clear_state(keyboard_t *kbd);
static int sunkbd_get_state(keyboard_t *kbd, void *buf, size_t len);
static int sunkbd_set_state(keyboard_t *kbd, void *buf, size_t len);
static int sunkbd_poll_mode(keyboard_t *kbd, int on);
static void sunkbd_diag(keyboard_t *kbd, int level);

static void sunkbd_repeat(void *v);
#if defined(SUNKBD_EMULATE_ATKBD)
static int keycode2scancode(int keycode, int shift, int up);
#endif

static keyboard_switch_t sunkbdsw = {
	sunkbd_probe,
	sunkbd_init,
	sunkbd_term,
	sunkbd_intr,
	sunkbd_test_if,
	sunkbd_enable,
	sunkbd_disable,
	sunkbd_read,
	sunkbd_check,
	sunkbd_read_char,
	sunkbd_check_char,
	sunkbd_ioctl,
	sunkbd_lock,
	sunkbd_clear_state,
	sunkbd_get_state,
	sunkbd_set_state,
	genkbd_get_fkeystr,
	sunkbd_poll_mode,
	sunkbd_diag
};

KEYBOARD_DRIVER(sunkbd, sunkbdsw, sunkbd_configure);

static struct sunkbd_softc sunkbd_softc;
static struct uart_devinfo uart_keyboard;

#if defined(SUNKBD_EMULATE_ATKBD)

#define	SCAN_PRESS		0x000
#define	SCAN_RELEASE		0x080
#define	SCAN_PREFIX_E0		0x100
#define	SCAN_PREFIX_E1		0x200
#define	SCAN_PREFIX_CTL		0x400
#define	SCAN_PREFIX_SHIFT	0x800
#define	SCAN_PREFIX		(SCAN_PREFIX_E0 | SCAN_PREFIX_E1 |	\
				SCAN_PREFIX_CTL | SCAN_PREFIX_SHIFT)

#define	NOTR	0x0	/* no translation */

static const uint8_t sunkbd_trtab[] = {
	NOTR, 0x6d, 0x78, 0x6e, 0x79, 0x3b, 0x3c, 0x44, /* 0x00 - 0x07 */
	0x3d, 0x57, 0x3e, 0x58, 0x3f, 0x5d, 0x40, NOTR, /* 0x08 - 0x0f */
	0x41, 0x42, 0x43, 0x38, 0x5f, 0x68, 0x5c, 0x46, /* 0x10 - 0x17 */
	0x61, 0x6f, 0x70, 0x64, 0x62, 0x01, 0x02, 0x03, /* 0x18 - 0x1f */
	0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, /* 0x20 - 0x27 */
	0x0c, 0x0d, 0x29, 0x0e, 0x66, 0x77, 0x5b, 0x37, /* 0x28 - 0x2f */
	0x7a, 0x71, 0x53, 0x74, 0x5e, 0x0f, 0x10, 0x11, /* 0x30 - 0x37 */
	0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, /* 0x38 - 0x3f */
	0x1a, 0x1b, 0x67, 0x6b, 0x47, 0x48, 0x49, 0x4a, /* 0x40 - 0x47 */
	0x73, 0x72, 0x63, NOTR, 0x1d, 0x1e, 0x1f, 0x20, /* 0x48 - 0x4f */
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, /* 0x50 - 0x57 */
	0x2b, 0x1c, 0x59, 0x4b, 0x4c, 0x4d, 0x52, 0x75, /* 0x58 - 0x5f */
	0x60, 0x76, 0x45, 0x2a, 0x2c, 0x2d, 0x2e, 0x2f, /* 0x60 - 0x67 */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, NOTR, /* 0x68 - 0x6f */
	0x4f, 0x50, 0x51, NOTR, NOTR, NOTR, 0x6c, 0x3a, /* 0x70 - 0x77 */
	0x69, 0x39, 0x6a, 0x65, 0x56, 0x4e, NOTR, NOTR  /* 0x78 - 0x7f */
};

#endif

static int
sunkbd_probe_keyboard(struct uart_devinfo *di)
{
	int c, id, ltries, tries;

	for (tries = 5; tries != 0; tries--) {
		uart_putc(di, SKBD_CMD_RESET);
		for (ltries = 1000; ltries != 0; ltries--) {
			if (uart_poll(di) == SKBD_RSP_RESET)
				break;
			DELAY(1000);
		}
		if (ltries == 0)
			continue;
		id = -1;
		for (ltries = 1000; ltries != 0; ltries--) {
			switch (c = uart_poll(di)) {
			case -1:
				break;
			case SKBD_RSP_IDLE:
				return (id);
			default:
				id = c;
			}
			DELAY(1000);
		}
	}
	return (-1);
}

static int sunkbd_attach(struct uart_softc *sc);
static void sunkbd_uart_intr(void *arg);

static int
sunkbd_configure(int flags)
{
	struct sunkbd_softc *sc;

	/*
	 * We are only prepared to be used for the high-level console
	 * when the keyboard is both configured and attached.
	 */
	if (!(flags & KB_CONF_PROBE_ONLY)) {
		if (KBD_IS_INITIALIZED(&sunkbd_softc.sc_kbd))
			goto found;
		else
			return (0);
	}

	if (uart_cpu_getdev(UART_DEV_KEYBOARD, &uart_keyboard))
		return (0);
	if (uart_probe(&uart_keyboard))
		return (0);
	uart_init(&uart_keyboard);

	uart_keyboard.type = UART_DEV_KEYBOARD;
	uart_keyboard.attach = sunkbd_attach;
	uart_add_sysdev(&uart_keyboard);

	if (sunkbd_probe_keyboard(&uart_keyboard) != KB_SUN4)
		return (0);

	sc = &sunkbd_softc;
	callout_init(&sc->sc_repeat_callout, 0);
	sunkbd_clear_state(&sc->sc_kbd);

#if defined(SUNKBD_EMULATE_ATKBD)
	kbd_init_struct(&sc->sc_kbd, SUNKBD_DRIVER_NAME, KB_101, 0, 0, 0, 0);
	kbd_set_maps(&sc->sc_kbd, &key_map, &accent_map, fkey_tab,
	    sizeof(fkey_tab) / sizeof(fkey_tab[0]));
#else
	kbd_init_struct(&sc->sc_kbd, SUNKBD_DRIVER_NAME, KB_OTHER, 0, 0, 0, 0);
	kbd_set_maps(&sc->sc_kbd, &keymap_sun_us_unix_kbd,
	    &accentmap_sun_us_unix_kbd, fkey_tab,
	    sizeof(fkey_tab) / sizeof(fkey_tab[0]));
#endif
	sc->sc_mode = K_XLATE;
	kbd_register(&sc->sc_kbd);

	sc->sc_sysdev = &uart_keyboard;

 found:
	/* Return number of found keyboards. */
	return (1);
}

static int
sunkbd_attach(struct uart_softc *sc)
{

	/*
	 * Don't attach if we didn't probe the keyboard. Note that
	 * the UART is still marked as a system device in that case.
	 */
	if (sunkbd_softc.sc_sysdev == NULL) {
		device_printf(sc->sc_dev, "keyboard not present\n");
		return (0);
	}

	if (sc->sc_sysdev != NULL) {
		sunkbd_softc.sc_uart = sc;

#ifdef KBD_INSTALL_CDEV
		kbd_attach(&sunkbd_softc.sc_kbd);
#endif
		sunkbd_enable(&sunkbd_softc.sc_kbd);

		swi_add(&tty_intr_event, uart_driver_name, sunkbd_uart_intr,
		    &sunkbd_softc, SWI_TTY, INTR_TYPE_TTY, &sc->sc_softih);

		sc->sc_opened = 1;
		KBD_INIT_DONE(&sunkbd_softc.sc_kbd);
	}

	return (0);
}

static void
sunkbd_uart_intr(void *arg)
{
	struct sunkbd_softc *sc = arg;
	int pend;

	if (sc->sc_uart->sc_leaving)
		return;

	pend = atomic_readandclear_32(&sc->sc_uart->sc_ttypend);
	if (!(pend & SER_INT_MASK))
		return;

	if (pend & SER_INT_RXREADY) {
		if (KBD_IS_ACTIVE(&sc->sc_kbd) && KBD_IS_BUSY(&sc->sc_kbd)) {
			sc->sc_kbd.kb_callback.kc_func(&sc->sc_kbd,
			    KBDIO_KEYINPUT, sc->sc_kbd.kb_callback.kc_arg);
		}
	}
}

static int
sunkbd_probe(int unit, void *arg, int flags)
{

	TODO;
	return (0);
}

static int
sunkbd_init(int unit, keyboard_t **kbdp, void *arg, int flags)
{

	TODO;
	return (0);
}

static int
sunkbd_term(keyboard_t *kbd)
{

	TODO;
	return (0);
}

static int
sunkbd_intr(keyboard_t *kbd, void *arg)
{

	TODO;
	return (0);
}

static int
sunkbd_test_if(keyboard_t *kbd)
{

	TODO;
	return (0);
}

static int
sunkbd_enable(keyboard_t *kbd)
{

	KBD_ACTIVATE(kbd);
	return (0);
}

static int
sunkbd_disable(keyboard_t *kbd)
{

	KBD_DEACTIVATE(kbd);
	return (0);
}

static int
sunkbd_read(keyboard_t *kbd, int wait)
{

	TODO;
	return (0);
}

static int
sunkbd_check(keyboard_t *kbd)
{
	struct sunkbd_softc *sc;

	if (!KBD_IS_ACTIVE(kbd))
		return (FALSE);

	sc = (struct sunkbd_softc *)kbd;

#if defined(SUNKBD_EMULATE_ATKBD)
	if (sc->sc_buffered_char[0])
		return (TRUE);
#endif

	if (sc->sc_repeating)
		return (TRUE);

	if (sc->sc_uart != NULL && !uart_rx_empty(sc->sc_uart))
		return (TRUE);

	if (sc->sc_polling != 0 && sc->sc_sysdev != NULL &&
	    uart_rxready(sc->sc_sysdev))
		return (TRUE);

	return (FALSE);
}

static u_int
sunkbd_read_char(keyboard_t *kbd, int wait)
{
	struct sunkbd_softc *sc;
	int key, release, repeated, suncode;

	sc = (struct sunkbd_softc *)kbd;

#if defined(SUNKBD_EMULATE_ATKBD)
	if (sc->sc_mode == K_RAW && sc->sc_buffered_char[0]) {
		key = sc->sc_buffered_char[0];
		if (key & SCAN_PREFIX) {
			sc->sc_buffered_char[0] = key & ~SCAN_PREFIX;
			return ((key & SCAN_PREFIX_E0) ? 0xe0 : 0xe1);
		} else {
			sc->sc_buffered_char[0] = sc->sc_buffered_char[1];
			sc->sc_buffered_char[1] = 0;
			return (key);
		}
	}
#endif

	repeated = 0;
	if (sc->sc_repeating) {
		repeated = 1;
		sc->sc_repeating = 0;
		callout_reset(&sc->sc_repeat_callout, hz / 10,
		    sunkbd_repeat, sc);
		suncode = sc->sc_repeat_key;
		goto process_code;
	}

	for (;;) {
 next_code:
		if (!(sc->sc_flags & KPCOMPOSE) && (sc->sc_composed_char > 0)) {
			key = sc->sc_composed_char;
			sc->sc_composed_char = 0;
			if (key > UCHAR_MAX)
				return (ERRKEY);
			return (key);
		}

		if (sc->sc_uart != NULL && !uart_rx_empty(sc->sc_uart)) {
			suncode = uart_rx_get(sc->sc_uart);
		} else if (sc->sc_polling != 0 && sc->sc_sysdev != NULL) {
			if (wait)
				suncode = uart_getc(sc->sc_sysdev);
			else if ((suncode = uart_poll(sc->sc_sysdev)) == -1)
				return (NOKEY);
		} else {
			return (NOKEY);
		}

		switch (suncode) {
		case SKBD_RSP_IDLE:
			break;
		default:
 process_code:
			++kbd->kb_count;
			key = SKBD_KEY_CHAR(suncode);
			release = suncode & SKBD_KEY_RELEASE;
			if (!repeated) {
				if (release == 0) {
					callout_reset(&sc->sc_repeat_callout,
					    hz / 2, sunkbd_repeat, sc);
					sc->sc_repeat_key = suncode;
				} else if (sc->sc_repeat_key == key) {
					callout_stop(&sc->sc_repeat_callout);
					sc->sc_repeat_key = -1;
				}
			}

#if defined(SUNKBD_EMULATE_ATKBD)
			key = sunkbd_trtab[key];
			if (key == NOTR)
				return (NOKEY);

			if (!repeated) {
				switch (key) {
				case 0x1d:	/* ctrl */
					if (release != 0)
						sc->sc_flags &= ~CTLS;
					else
						sc->sc_flags |= CTLS;
					break;
				case 0x2a:	/* left shift */
				case 0x36:	/* right shift */
					if (release != 0)
						sc->sc_flags &= ~SHIFTS;
					else
						sc->sc_flags |= SHIFTS;
					break;
				case 0x38:	/* alt */
				case 0x5d:	/* altgr */
					if (release != 0)
						sc->sc_flags &= ~ALTS;
					else
						sc->sc_flags |= ALTS;
					break;
				}
			}
			if (sc->sc_mode == K_RAW) {
				key = keycode2scancode(key, sc->sc_flags,
				    release);
				if (key & SCAN_PREFIX) {
					if (key & SCAN_PREFIX_CTL) {
						sc->sc_buffered_char[0] =
						    0x1d | (key & SCAN_RELEASE);
						sc->sc_buffered_char[1] =
						    key & ~SCAN_PREFIX;
					} else if (key & SCAN_PREFIX_SHIFT) {
						sc->sc_buffered_char[0] =
						    0x2a | (key & SCAN_RELEASE);
						sc->sc_buffered_char[1] =
						    key & ~SCAN_PREFIX_SHIFT;
					} else {
						sc->sc_buffered_char[0] =
						    key & ~SCAN_PREFIX;
						sc->sc_buffered_char[1] = 0;
					}
					return ((key & SCAN_PREFIX_E0) ?
					    0xe0 : 0xe1);
				}
				return (key);
			}
			switch (key) {
			case 0x5c:	/* print screen */
				if (sc->sc_flags & ALTS)
					key = 0x54;	/* sysrq */
				break;
			case 0x68:	/* pause/break */
				if (sc->sc_flags & CTLS)
					key = 0x6c;	/* break */
				break;
			}

			if (sc->sc_mode == K_CODE)
				return (key | release);
#else
			if (sc->sc_mode == K_RAW || sc->sc_mode == K_CODE)
				return (suncode);
#endif

#if defined(SUNKBD_EMULATE_ATKBD)
			if (key == 0x38) {	/* left alt (KP compose key) */
#else
			if (key == 0x13) {	/* left alt (KP compose key) */
#endif
				if (release != 0) {
					if (sc->sc_flags & KPCOMPOSE) {
						sc->sc_flags &= ~KPCOMPOSE;
						if (sc->sc_composed_char >
						    UCHAR_MAX)
							sc->sc_composed_char =
							    0;
					}
				} else {
					if (!(sc->sc_flags & KPCOMPOSE)) {
						sc->sc_flags |= KPCOMPOSE;
						sc->sc_composed_char = 0;
					}
				}
			}
			if (sc->sc_flags & KPCOMPOSE) {
				switch (suncode) {
				case 0x44:			/* KP 7 */
				case 0x45:			/* KP 8 */
				case 0x46:			/* KP 9 */
					sc->sc_composed_char *= 10;
					sc->sc_composed_char += suncode - 0x3d;
					if (sc->sc_composed_char > UCHAR_MAX)
						return (ERRKEY);
					goto next_code;
				case 0x5b:			/* KP 4 */
				case 0x5c:			/* KP 5 */
				case 0x5d:			/* KP 6 */
					sc->sc_composed_char *= 10;
					sc->sc_composed_char += suncode - 0x58;
					if (sc->sc_composed_char > UCHAR_MAX)
						return (ERRKEY);
					goto next_code;
				case 0x70:			/* KP 1 */
				case 0x71:			/* KP 2 */
				case 0x72:			/* KP 3 */
					sc->sc_composed_char *= 10;
					sc->sc_composed_char += suncode - 0x6f;
					if (sc->sc_composed_char > UCHAR_MAX)
						return (ERRKEY);
					goto next_code;
				case 0x5e:			/* KP 0 */
					sc->sc_composed_char *= 10;
					if (sc->sc_composed_char > UCHAR_MAX)
						return (ERRKEY);
					goto next_code;

				case 0x44 | SKBD_KEY_RELEASE:	/* KP 7 */
				case 0x45 | SKBD_KEY_RELEASE:	/* KP 8 */
				case 0x46 | SKBD_KEY_RELEASE:	/* KP 9 */
				case 0x5b | SKBD_KEY_RELEASE:	/* KP 4 */
				case 0x5c | SKBD_KEY_RELEASE:	/* KP 5 */
				case 0x5d | SKBD_KEY_RELEASE:	/* KP 6 */
				case 0x70 | SKBD_KEY_RELEASE:	/* KP 1 */
				case 0x71 | SKBD_KEY_RELEASE:	/* KP 2 */
				case 0x72 | SKBD_KEY_RELEASE:	/* KP 3 */
				case 0x5e | SKBD_KEY_RELEASE:	/* KP 0 */
					goto next_code;
				default:
					if (sc->sc_composed_char > 0) {
						sc->sc_flags &= ~KPCOMPOSE;
						sc->sc_composed_char = 0;
						return (ERRKEY);
					}
				}
			}

			key = genkbd_keyaction(kbd, key, release,
			    &sc->sc_state, &sc->sc_accents);
			if (key != NOKEY || repeated)
				return (key);
		}
	}
	return (0);
}

static int
sunkbd_check_char(keyboard_t *kbd)
{
	struct sunkbd_softc *sc;

	if (!KBD_IS_ACTIVE(kbd))
		return (FALSE);

	sc = (struct sunkbd_softc *)kbd;
	if (!(sc->sc_flags & KPCOMPOSE) && (sc->sc_composed_char > 0))
		return (TRUE);

	return (sunkbd_check(kbd));
}

static int
sunkbd_ioctl(keyboard_t *kbd, u_long cmd, caddr_t data)
{
	struct sunkbd_softc *sc;
	int c, error;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5)
	int ival;
#endif

	sc = (struct sunkbd_softc *)kbd;
	error = 0;
	switch (cmd) {
	case KDGKBMODE:
		*(int *)data = sc->sc_mode;
		break;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5)
	case _IO('K', 7):
		ival = IOCPARM_IVAL(data);
		data = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBMODE:
		switch (*(int *)data) {
		case K_XLATE:
			if (sc->sc_mode != K_XLATE) {
				/* make lock key state and LED state match */
				sc->sc_state &= ~LOCK_MASK;
				sc->sc_state |= KBD_LED_VAL(kbd);
			}
			/* FALLTHROUGH */
		case K_RAW:
		case K_CODE:
			if (sc->sc_mode != *(int *)data) {
				sunkbd_clear_state(kbd);
				sc->sc_mode = *(int *)data;
			}
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	case KDGETLED:
		*(int *)data = KBD_LED_VAL(kbd);
		break;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5)
	case _IO('K', 66):
		ival = IOCPARM_IVAL(data);
		data = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETLED:
		if (*(int *)data & ~LOCK_MASK) {
			error = EINVAL;
			break;
		}
		if (sc->sc_sysdev == NULL)
			break;
		c = 0;
		if (*(int *)data & CLKED)
			c |= SKBD_LED_CAPSLOCK;
		if (*(int *)data & NLKED)
			c |= SKBD_LED_NUMLOCK;
		if (*(int *)data & SLKED)
			c |= SKBD_LED_SCROLLLOCK;
		uart_lock(sc->sc_sysdev->hwmtx);
		sc->sc_sysdev->ops->putc(&sc->sc_sysdev->bas, SKBD_CMD_SETLED);
		sc->sc_sysdev->ops->putc(&sc->sc_sysdev->bas, c);
		uart_unlock(sc->sc_sysdev->hwmtx);
		KBD_LED_VAL(kbd) = *(int *)data;
		break;
	case KDGKBSTATE:
		*(int *)data = sc->sc_state & LOCK_MASK;
		break;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5)
	case _IO('K', 20):
		ival = IOCPARM_IVAL(data);
		data = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBSTATE:
		if (*(int *)data & ~LOCK_MASK) {
			error = EINVAL;
			break;
		}
		sc->sc_state &= ~LOCK_MASK;
		sc->sc_state |= *(int *)data;
		/* set LEDs and quit */
		return (sunkbd_ioctl(kbd, KDSETLED, data));
	case KDSETREPEAT:
	case KDSETRAD:
		break;
	case PIO_KEYMAP:
	case OPIO_KEYMAP:
	case PIO_KEYMAPENT:
	case PIO_DEADKEYMAP:
	default:
		return (genkbd_commonioctl(kbd, cmd, data));
	}
	return (error);
}

static int
sunkbd_lock(keyboard_t *kbd, int lock)
{

	TODO;
	return (0);
}

static void
sunkbd_clear_state(keyboard_t *kbd)
{
	struct sunkbd_softc *sc;

	sc = (struct sunkbd_softc *)kbd;
	sc->sc_repeat_key = -1;
	sc->sc_accents = 0;
	sc->sc_composed_char = 0;
	sc->sc_flags = 0;
	sc->sc_polling = 0;
	sc->sc_repeating = 0;
	sc->sc_state &= LOCK_MASK;	/* Preserve locking key state. */

#if defined(SUNKBD_EMULATE_ATKBD)
	sc->sc_buffered_char[0] = 0;
	sc->sc_buffered_char[1] = 0;
#endif
}

static int
sunkbd_get_state(keyboard_t *kbd, void *buf, size_t len)
{

	TODO;
	return (0);
}

static int
sunkbd_set_state(keyboard_t *kbd, void *buf, size_t len)
{

	TODO;
	return (0);
}

static int
sunkbd_poll_mode(keyboard_t *kbd, int on)
{
	struct sunkbd_softc *sc;

	sc = (struct sunkbd_softc *)kbd;
	if (on)
		sc->sc_polling++;
	else
		sc->sc_polling--;
	return (0);
}

static void
sunkbd_diag(keyboard_t *kbd, int level)
{

	TODO;
}

static void
sunkbd_repeat(void *v)
{
	struct sunkbd_softc *sc = v;

	if (KBD_IS_ACTIVE(&sc->sc_kbd) && KBD_IS_BUSY(&sc->sc_kbd)) {
		if (sc->sc_repeat_key != -1) {
			sc->sc_repeating = 1;
			sc->sc_kbd.kb_callback.kc_func(&sc->sc_kbd,
			    KBDIO_KEYINPUT, sc->sc_kbd.kb_callback.kc_arg);
		}
	}
}

#if defined(SUNKBD_EMULATE_ATKBD)
static int
keycode2scancode(int keycode, int shift, int up)
{
	static const int scan[] = {
		/* KP enter, right ctrl, KP divide */
		0x1c , 0x1d , 0x35 ,
		/* print screen */
		0x37 | SCAN_PREFIX_SHIFT,
		/* right alt, home, up, page up, left, right, end */
		0x38, 0x47, 0x48, 0x49, 0x4b, 0x4d, 0x4f,
		/* down, page down, insert, delete */
		0x50, 0x51, 0x52, 0x53,
		/* pause/break (see also below) */
		0x46,
		/*
		 * MS: left window, right window, menu
		 * also Sun: left meta, right meta, compose
		 */
		0x5b, 0x5c, 0x5d,
		/* Sun type 6 USB */
		/* help, stop, again, props, undo, front, copy */
		0x68, 0x5e, 0x5f, 0x60,	0x61, 0x62, 0x63,
		/* open, paste, find, cut, audiomute, audiolower, audioraise */
		0x64, 0x65, 0x66, 0x67, 0x25, 0x1f, 0x1e,
		/* power */
		0x20
	};
	int scancode;

	scancode = keycode;
	if ((keycode >= 89) && (keycode < 89 + nitems(scan)))
	scancode = scan[keycode - 89] | SCAN_PREFIX_E0;
	/* pause/break */
	if ((keycode == 104) && !(shift & CTLS))
		scancode = 0x45 | SCAN_PREFIX_E1 | SCAN_PREFIX_CTL;
	if (shift & SHIFTS)
		scancode &= ~SCAN_PREFIX_SHIFT;
	return (scancode | (up ? SCAN_RELEASE : SCAN_PRESS));
}
#endif
