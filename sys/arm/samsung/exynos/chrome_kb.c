/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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

/*
 * Samsung Chromebook Keyboard
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/kdb.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/mutex.h>
#include <sys/gpio.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <sys/ioccom.h>
#include <sys/filio.h>
#include <sys/tty.h>
#include <sys/kbio.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include "gpio_if.h"

#include <arm/samsung/exynos/chrome_ec.h>
#include <arm/samsung/exynos/chrome_kb.h>

#include <arm/samsung/exynos/exynos5_combiner.h>
#include <arm/samsung/exynos/exynos5_pad.h>

#define	CKB_LOCK()	mtx_lock(&Giant)
#define	CKB_UNLOCK()	mtx_unlock(&Giant)

#ifdef	INVARIANTS
/*
 * Assert that the lock is held in all contexts
 * where the code can be executed.
 */
#define	CKB_LOCK_ASSERT()	mtx_assert(&Giant, MA_OWNED)
/*
 * Assert that the lock is held in the contexts
 * where it really has to be so.
 */
#define	CKB_CTX_LOCK_ASSERT()			 	\
	do {						\
		if (!kdb_active && panicstr == NULL)	\
			mtx_assert(&Giant, MA_OWNED);	\
	} while (0)
#else
#define CKB_LOCK_ASSERT()	(void)0
#define CKB_CTX_LOCK_ASSERT()	(void)0
#endif

/*
 * Define a stub keyboard driver in case one hasn't been
 * compiled into the kernel
 */
#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>
#include <dev/kbd/kbdtables.h>

#define	CKB_NFKEY		12
#define	CKB_FLAG_COMPOSE	0x1
#define	CKB_FLAG_POLLING	0x2
#define	KBD_DRIVER_NAME		"ckbd"

struct ckb_softc {
	keyboard_t sc_kbd;
	keymap_t sc_keymap;
	accentmap_t sc_accmap;
	fkeytab_t sc_fkeymap[CKB_NFKEY];

	struct resource*	sc_mem_res;
	struct resource*	sc_irq_res;
	void*			sc_intr_hl;

	int	sc_mode;	/* input mode (K_XLATE,K_RAW,K_CODE) */
	int	sc_state;	/* shift/lock key state */
	int	sc_accents;	/* accent key index (> 0) */
	int	sc_flags;	/* flags */

	struct callout		sc_repeat_callout;
	int			sc_repeat_key;
	int			sc_repeating;

	int			flag;
	int			rows;
	int			cols;
	int			gpio;
	device_t		dev;
	device_t		gpio_dev;
	struct thread		*sc_poll_thread;
	uint16_t		*keymap;

	uint8_t			*scan_local;
	uint8_t			*scan;
};

/* prototypes */
static int	ckb_set_typematic(keyboard_t *, int);
static uint32_t	ckb_read_char(keyboard_t *, int);
static void	ckb_clear_state(keyboard_t *);
static int	ckb_ioctl(keyboard_t *, u_long, caddr_t);
static int	ckb_enable(keyboard_t *);
static int	ckb_disable(keyboard_t *);

static void
ckb_repeat(void *arg)
{
	struct ckb_softc *sc;

	sc = arg;

	if (KBD_IS_ACTIVE(&sc->sc_kbd) && KBD_IS_BUSY(&sc->sc_kbd)) {
		if (sc->sc_repeat_key != -1) {
			sc->sc_repeating = 1;
			sc->sc_kbd.kb_callback.kc_func(&sc->sc_kbd,
			    KBDIO_KEYINPUT, sc->sc_kbd.kb_callback.kc_arg);
		}
	}
}

/* detect a keyboard, not used */
static int
ckb__probe(int unit, void *arg, int flags)
{

	return (ENXIO);
}

/* reset and initialize the device, not used */
static int
ckb_init(int unit, keyboard_t **kbdp, void *arg, int flags)
{

	return (ENXIO);
}

/* test the interface to the device, not used */
static int
ckb_test_if(keyboard_t *kbd)
{

	return (0);
}

/* finish using this keyboard, not used */
static int
ckb_term(keyboard_t *kbd)
{

	return (ENXIO);
}

/* keyboard interrupt routine, not used */
static int
ckb_intr(keyboard_t *kbd, void *arg)
{

	return (0);
}

/* lock the access to the keyboard, not used */
static int
ckb_lock(keyboard_t *kbd, int lock)
{

	return (1);
}

/* clear the internal state of the keyboard */
static void
ckb_clear_state(keyboard_t *kbd)
{
	struct ckb_softc *sc;

	sc = kbd->kb_data;

	CKB_CTX_LOCK_ASSERT();

	sc->sc_flags &= ~(CKB_FLAG_COMPOSE | CKB_FLAG_POLLING);
	sc->sc_state &= LOCK_MASK; /* preserve locking key state */
	sc->sc_accents = 0;
}

/* save the internal state, not used */
static int
ckb_get_state(keyboard_t *kbd, void *buf, size_t len)
{

	return (len == 0) ? 1 : -1;
}

/* set the internal state, not used */
static int
ckb_set_state(keyboard_t *kbd, void *buf, size_t len)
{

	return (EINVAL);
}


/* check if data is waiting */
static int
ckb_check(keyboard_t *kbd)
{
	struct ckb_softc *sc;
	int i;

	sc = kbd->kb_data;

	CKB_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (0);

	if (sc->sc_flags & CKB_FLAG_POLLING) {
		return (1);
	}

	for (i = 0; i < sc->cols; i++)
		if (sc->scan_local[i] != sc->scan[i]) {
			return (1);
		}

	if (sc->sc_repeating)
		return (1);

	return (0);
}

/* check if char is waiting */
static int
ckb_check_char_locked(keyboard_t *kbd)
{
	CKB_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (0);

	return (ckb_check(kbd));
}

static int
ckb_check_char(keyboard_t *kbd)
{
	int result;

	CKB_LOCK();
	result = ckb_check_char_locked(kbd);
	CKB_UNLOCK();

	return (result);
}

/* read one byte from the keyboard if it's allowed */
/* Currently unused. */
static int
ckb_read(keyboard_t *kbd, int wait)
{
	CKB_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (-1);

	printf("Implement ME: %s\n", __func__);
	return (0);
}

static uint16_t
keymap_read(struct ckb_softc *sc, int col, int row)
{

	KASSERT(sc->keymap != NULL, ("keymap_read: no keymap"));
	if (col >= 0 && col < sc->cols &&
	    row >= 0 && row < sc->rows) {
		return sc->keymap[row * sc->cols + col];
	}

	return (0);
}

static int
keymap_write(struct ckb_softc *sc, int col, int row, uint16_t key)
{

	KASSERT(sc->keymap != NULL, ("keymap_write: no keymap"));
	if (col >= 0 && col < sc->cols &&
	    row >= 0 && row < sc->rows) {
		sc->keymap[row * sc->cols + col] = key;
		return (0);
	}

	return (-1);
}

/* read char from the keyboard */
static uint32_t
ckb_read_char_locked(keyboard_t *kbd, int wait)
{
	struct ckb_softc *sc;
	int i,j;
	uint16_t key;
	int oldbit;
	int newbit;
	int status;

	sc = kbd->kb_data;

	CKB_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (NOKEY);

	if (sc->sc_repeating) {
		sc->sc_repeating = 0;
		callout_reset(&sc->sc_repeat_callout, hz / 10,
                    ckb_repeat, sc);
		return (sc->sc_repeat_key);
	}

	if (sc->sc_flags & CKB_FLAG_POLLING) {
		for (;;) {
			GPIO_PIN_GET(sc->gpio_dev, sc->gpio, &status);
			if (status == 0) {
				if (ec_command(EC_CMD_MKBP_STATE, sc->scan,
					sc->cols,
				    sc->scan, sc->cols)) {
					return (NOKEY);
				}
				break;
			}
			if (!wait) {
				return (NOKEY);
			}
			DELAY(1000);
		}
	}

	for (i = 0; i < sc->cols; i++) {
		for (j = 0; j < sc->rows; j++) {
			oldbit = (sc->scan_local[i] & (1 << j));
			newbit = (sc->scan[i] & (1 << j));

			if (oldbit == newbit)
				continue;

			key = keymap_read(sc, i, j);
			if (key == 0) {
				continue;
			}

			if (newbit > 0) {
				/* key pressed */
				sc->scan_local[i] |= (1 << j);

				/* setup repeating */
				sc->sc_repeat_key = key;
				callout_reset(&sc->sc_repeat_callout,
				    hz / 2, ckb_repeat, sc);

			} else {
				/* key released */
				sc->scan_local[i] &= ~(1 << j);

				/* release flag */
				key |= 0x80;

				/* unsetup repeating */
				sc->sc_repeat_key = -1;
				callout_stop(&sc->sc_repeat_callout);
			}

			return (key);
		}
	}

	return (NOKEY);
}

/* Currently wait is always false. */
static uint32_t
ckb_read_char(keyboard_t *kbd, int wait)
{
	uint32_t keycode;

	CKB_LOCK();
	keycode = ckb_read_char_locked(kbd, wait);
	CKB_UNLOCK();

	return (keycode);
}


/* some useful control functions */
static int
ckb_ioctl_locked(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	struct ckb_softc *sc;
	int i;

	sc = kbd->kb_data;

	CKB_LOCK_ASSERT();

	switch (cmd) {
	case KDGKBMODE:		/* get keyboard mode */
		*(int *)arg = sc->sc_mode;
		break;

	case KDSKBMODE:		/* set keyboard mode */
		switch (*(int *)arg) {
		case K_XLATE:
			if (sc->sc_mode != K_XLATE) {
				/* make lock key state and LED state match */
				sc->sc_state &= ~LOCK_MASK;
				sc->sc_state |= KBD_LED_VAL(kbd);
			}
			/* FALLTHROUGH */
		case K_RAW:
		case K_CODE:
			if (sc->sc_mode != *(int *)arg) {
				if ((sc->sc_flags & CKB_FLAG_POLLING) == 0)
					ckb_clear_state(kbd);
				sc->sc_mode = *(int *)arg;
			}
			break;
		default:
			return (EINVAL);
		}
		break;

	case KDGETLED:			/* get keyboard LED */
		*(int *)arg = KBD_LED_VAL(kbd);
		break;

	case KDSETLED:			/* set keyboard LED */
		/* NOTE: lock key state in "sc_state" won't be changed */
		if (*(int *)arg & ~LOCK_MASK)
			return (EINVAL);

		i = *(int *)arg;

		/* replace CAPS LED with ALTGR LED for ALTGR keyboards */
		if (sc->sc_mode == K_XLATE &&
		    kbd->kb_keymap->n_keys > ALTGR_OFFSET) {
			if (i & ALKED)
				i |= CLKED;
			else
				i &= ~CLKED;
		}
		if (KBD_HAS_DEVICE(kbd)) {
			/* Configure LED */
		}

		KBD_LED_VAL(kbd) = *(int *)arg;
		break;
	case KDGKBSTATE:		/* get lock key state */
		*(int *)arg = sc->sc_state & LOCK_MASK;
		break;

	case KDSKBSTATE:		/* set lock key state */
		if (*(int *)arg & ~LOCK_MASK) {
			return (EINVAL);
		}
		sc->sc_state &= ~LOCK_MASK;
		sc->sc_state |= *(int *)arg;

		/* set LEDs and quit */
		return (ckb_ioctl(kbd, KDSETLED, arg));

	case KDSETREPEAT:		/* set keyboard repeat rate (new
					 * interface) */

		if (!KBD_HAS_DEVICE(kbd)) {
			return (0);
		}
		if (((int *)arg)[1] < 0) {
			return (EINVAL);
		}
		if (((int *)arg)[0] < 0) {
			return (EINVAL);
		}
		if (((int *)arg)[0] < 200)	/* fastest possible value */
			kbd->kb_delay1 = 200;
		else
			kbd->kb_delay1 = ((int *)arg)[0];
		kbd->kb_delay2 = ((int *)arg)[1];
		return (0);

	case KDSETRAD:			/* set keyboard repeat rate (old
					 * interface) */
		return (ckb_set_typematic(kbd, *(int *)arg));

	case PIO_KEYMAP:		/* set keyboard translation table */
	case OPIO_KEYMAP:		/* set keyboard translation table
					 * (compat) */
	case PIO_KEYMAPENT:		/* set keyboard translation table
					 * entry */
	case PIO_DEADKEYMAP:		/* set accent key translation table */
		sc->sc_accents = 0;
		/* FALLTHROUGH */
	default:
		return (genkbd_commonioctl(kbd, cmd, arg));
	}

	return (0);
}

static int
ckb_ioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	int result;

	/*
	 * XXX KDGKBSTATE, KDSKBSTATE and KDSETLED can be called from any
	 * context where printf(9) can be called, which among other things
	 * includes interrupt filters and threads with any kinds of locks
	 * already held.  For this reason it would be dangerous to acquire
	 * the Giant here unconditionally.  On the other hand we have to
	 * have it to handle the ioctl.
	 * So we make our best effort to auto-detect whether we can grab
	 * the Giant or not.  Blame syscons(4) for this.
	 */
	switch (cmd) {
	case KDGKBSTATE:
	case KDSKBSTATE:
	case KDSETLED:
		if (!mtx_owned(&Giant) && !SCHEDULER_STOPPED())
			return (EDEADLK);	/* best I could come up with */
		/* FALLTHROUGH */
	default:
		CKB_LOCK();
		result = ckb_ioctl_locked(kbd, cmd, arg);
		CKB_UNLOCK();
		return (result);
	}
}


/*
 * Enable the access to the device; until this function is called,
 * the client cannot read from the keyboard.
 */
static int
ckb_enable(keyboard_t *kbd)
{

	CKB_LOCK();
	KBD_ACTIVATE(kbd);
	CKB_UNLOCK();

	return (0);
}

/* disallow the access to the device */
static int
ckb_disable(keyboard_t *kbd)
{

	CKB_LOCK();
	KBD_DEACTIVATE(kbd);
	CKB_UNLOCK();

	return (0);
}

/* local functions */

static int
ckb_set_typematic(keyboard_t *kbd, int code)
{
	static const int delays[] = {250, 500, 750, 1000};
	static const int rates[] = {34, 38, 42, 46, 50, 55, 59, 63,
		68, 76, 84, 92, 100, 110, 118, 126,
		136, 152, 168, 184, 200, 220, 236, 252,
	272, 304, 336, 368, 400, 440, 472, 504};

	if (code & ~0x7f) {
		return (EINVAL);
	}
	kbd->kb_delay1 = delays[(code >> 5) & 3];
	kbd->kb_delay2 = rates[code & 0x1f];
	return (0);
}

static int
ckb_poll(keyboard_t *kbd, int on)
{
	struct ckb_softc *sc;

	sc = kbd->kb_data;

	CKB_LOCK();
	if (on) {
		sc->sc_flags |= CKB_FLAG_POLLING;
		sc->sc_poll_thread = curthread;
	} else {
		sc->sc_flags &= ~CKB_FLAG_POLLING;
	}
	CKB_UNLOCK();

	return (0);
}

/* local functions */

static int dummy_kbd_configure(int flags);

keyboard_switch_t ckbdsw = {
	.probe = &ckb__probe,
	.init = &ckb_init,
	.term = &ckb_term,
	.intr = &ckb_intr,
	.test_if = &ckb_test_if,
	.enable = &ckb_enable,
	.disable = &ckb_disable,
	.read = &ckb_read,
	.check = &ckb_check,
	.read_char = &ckb_read_char,
	.check_char = &ckb_check_char,
	.ioctl = &ckb_ioctl,
	.lock = &ckb_lock,
	.clear_state = &ckb_clear_state,
	.get_state = &ckb_get_state,
	.set_state = &ckb_set_state,
	.get_fkeystr = &genkbd_get_fkeystr,
	.poll = &ckb_poll,
	.diag = &genkbd_diag,
};

static int
dummy_kbd_configure(int flags)
{

	return (0);
}

KEYBOARD_DRIVER(ckbd, ckbdsw, dummy_kbd_configure);

/* 
 * Parses 'keymap' into sc->keymap.
 * Requires sc->cols and sc->rows to be set.
 */
static int
parse_keymap(struct ckb_softc *sc, pcell_t *keymap, size_t len)
{
	int i;

	sc->keymap = malloc(sc->cols * sc->rows * sizeof(sc->keymap[0]),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->keymap == NULL) {
		return (ENOMEM);
	}

	for (i = 0; i < len; i++) {
		/* 
		 * Return value is ignored, we just write whatever fits into
		 * specified number of rows and columns and silently ignore
		 * everything else.
		 * Keymap entries follow this format: 0xRRCCKKKK
		 * RR - row number, CC - column number, KKKK - key code
		 */
		keymap_write(sc, (keymap[i] >> 16) & 0xff,
		    (keymap[i] >> 24) & 0xff,
		    keymap[i] & 0xffff);
	}

	return (0);
}

/* Allocates a new array for keymap and returns it in 'keymap'. */
static int
read_keymap(phandle_t node, const char *prop, pcell_t **keymap, size_t *len)
{

	if ((*len = OF_getproplen(node, prop)) <= 0) {
		return (ENXIO);
	}
	if ((*keymap = malloc(*len, M_DEVBUF, M_NOWAIT)) == NULL) {
		return (ENOMEM);
	}
	if (OF_getencprop(node, prop, *keymap, *len) != *len) {
		return (ENXIO);
	}
	return (0);
}

static int
parse_dts(struct ckb_softc *sc)
{
	phandle_t node;
	pcell_t dts_value;
	pcell_t *keymap;
	int len, ret;
	const char *keymap_prop = NULL;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	if ((len = OF_getproplen(node, "google,key-rows")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "google,key-rows", &dts_value, len);
	sc->rows = dts_value;

	if ((len = OF_getproplen(node, "google,key-columns")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "google,key-columns", &dts_value, len);
	sc->cols = dts_value;

	if ((len = OF_getproplen(node, "freebsd,intr-gpio")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "freebsd,intr-gpio", &dts_value, len);
	sc->gpio = dts_value;

	if (OF_hasprop(node, "freebsd,keymap")) {
		keymap_prop = "freebsd,keymap";
		device_printf(sc->dev, "using FreeBSD-specific keymap from FDT\n");
	} else if (OF_hasprop(node, "linux,keymap")) {
		keymap_prop = "linux,keymap";
		device_printf(sc->dev, "using Linux keymap from FDT\n");
	} else {
		device_printf(sc->dev, "using built-in keymap\n");
	}

	if (keymap_prop != NULL) {
		if ((ret = read_keymap(node, keymap_prop, &keymap, &len))) {
			device_printf(sc->dev,
			     "failed to read keymap from FDT: %d\n", ret);
			return (ret);
		}
		ret = parse_keymap(sc, keymap, len);
		free(keymap, M_DEVBUF);
		if (ret) {
			return (ret);
		}
	} else {
		if ((ret = parse_keymap(sc, default_keymap, KEYMAP_LEN))) {
			return (ret);
		}
	}

	if ((sc->rows == 0) || (sc->cols == 0) || (sc->gpio == 0))
		return (ENXIO);

	return (0);
}

void
ckb_ec_intr(void *arg)
{
	struct ckb_softc *sc;

	sc = arg;

	if (sc->sc_flags & CKB_FLAG_POLLING)
		return;

	ec_command(EC_CMD_MKBP_STATE, sc->scan, sc->cols,
	    sc->scan, sc->cols);

	(sc->sc_kbd.kb_callback.kc_func) (&sc->sc_kbd, KBDIO_KEYINPUT,
	    sc->sc_kbd.kb_callback.kc_arg);
};

static int
chrome_kb_attach(device_t dev)
{
	struct ckb_softc *sc;
	keyboard_t *kbd;
	int error;
	int rid;
	int i;

	sc = device_get_softc(dev);

	sc->dev = dev;
	sc->keymap = NULL;

	if ((error = parse_dts(sc)) != 0)
		return error;

	sc->gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (sc->gpio_dev == NULL) {
		device_printf(sc->dev, "Can't find gpio device.\n");
		return (ENXIO);
	}

#if 0
	device_printf(sc->dev, "Keyboard matrix [%dx%d]\n",
	    sc->cols, sc->rows);
#endif

	pad_setup_intr(sc->gpio, ckb_ec_intr, sc);

	kbd = &sc->sc_kbd;
	rid = 0;

	sc->scan_local = malloc(sc->cols, M_DEVBUF, M_NOWAIT);
	sc->scan = malloc(sc->cols, M_DEVBUF, M_NOWAIT);

	for (i = 0; i < sc->cols; i++) {
		sc->scan_local[i] = 0;
		sc->scan[i] = 0;
	}

	kbd_init_struct(kbd, KBD_DRIVER_NAME, KB_OTHER,
	    device_get_unit(dev), 0, 0, 0);
	kbd->kb_data = (void *)sc;

	sc->sc_keymap = key_map;
        sc->sc_accmap = accent_map;
	for (i = 0; i < CKB_NFKEY; i++) {
		sc->sc_fkeymap[i] = fkey_tab[i];
        }

	kbd_set_maps(kbd, &sc->sc_keymap, &sc->sc_accmap,
	    sc->sc_fkeymap, CKB_NFKEY);

	KBD_FOUND_DEVICE(kbd);
	ckb_clear_state(kbd);
	KBD_PROBE_DONE(kbd);

	callout_init(&sc->sc_repeat_callout, 0);

	KBD_INIT_DONE(kbd);

	if (kbd_register(kbd) < 0) {
		return (ENXIO);
	}
	KBD_CONFIG_DONE(kbd);

	return (0);
}

static int
chrome_kb_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "google,cros-ec-keyb") ||
	    ofw_bus_is_compatible(dev, "google,mkbp-keyb")) {
		device_set_desc(dev, "Chrome EC Keyboard");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
chrome_kb_detach(device_t dev)
{
	struct ckb_softc *sc;

	sc = device_get_softc(dev);

	if (sc->keymap != NULL) {
		free(sc->keymap, M_DEVBUF);
	}

	return 0;
}

static device_method_t chrome_kb_methods[] = {
	DEVMETHOD(device_probe,		chrome_kb_probe),
	DEVMETHOD(device_attach,	chrome_kb_attach),
	DEVMETHOD(device_detach,	chrome_kb_detach),
	{ 0, 0 }
};

static driver_t chrome_kb_driver = {
	"chrome_kb",
	chrome_kb_methods,
	sizeof(struct ckb_softc),
};

static devclass_t chrome_kb_devclass;

DRIVER_MODULE(chrome_kb, simplebus, chrome_kb_driver,
    chrome_kb_devclass, 0, 0);
