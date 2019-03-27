/*-
 * Copyright (c) 2015-2016 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include "opt_platform.h"
#include "opt_kbd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/kdb.h>

#include <sys/ioccom.h>
#include <sys/filio.h>
#include <sys/tty.h>
#include <sys/kbio.h>

#include <dev/kbd/kbdreg.h>
#include <dev/kbd/kbdtables.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/gpio/gpiokeys.h>

#define	KBD_DRIVER_NAME	"gpiokeys"

#define	GPIOKEYS_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	GPIOKEYS_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	GPIOKEYS_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
	    "gpiokeys", MTX_DEF)
#define	GPIOKEYS_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx);
#define	GPIOKEYS_ASSERT_LOCKED(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#define	GPIOKEY_LOCK(_key)		mtx_lock(&(_key)->mtx)
#define	GPIOKEY_UNLOCK(_key)		mtx_unlock(&(_key)->mtx)
#define	GPIOKEY_LOCK_INIT(_key) \
	mtx_init(&(_key)->mtx, "gpiokey", "gpiokey", MTX_DEF)
#define	GPIOKEY_LOCK_DESTROY(_key)	mtx_destroy(&(_key)->mtx);

#define	KEY_PRESS	  0
#define	KEY_RELEASE	  0x80

#define	SCAN_PRESS	  0
#define	SCAN_RELEASE	  0x80
#define	SCAN_CHAR(c)	((c) & 0x7f)

#define	GPIOKEYS_GLOBAL_NMOD                     8	/* units */
#define	GPIOKEYS_GLOBAL_NKEYCODE                 6	/* units */
#define	GPIOKEYS_GLOBAL_IN_BUF_SIZE  (2*(GPIOKEYS_GLOBAL_NMOD + (2*GPIOKEYS_GLOBAL_NKEYCODE)))	/* bytes */
#define	GPIOKEYS_GLOBAL_IN_BUF_FULL  (GPIOKEYS_GLOBAL_IN_BUF_SIZE / 2)	/* bytes */
#define	GPIOKEYS_GLOBAL_NFKEY        (sizeof(fkey_tab)/sizeof(fkey_tab[0]))	/* units */
#define	GPIOKEYS_GLOBAL_BUFFER_SIZE	      64	/* bytes */

#define	AUTOREPEAT_DELAY	250
#define	AUTOREPEAT_REPEAT	34

struct gpiokeys_softc;

struct gpiokey
{
	struct gpiokeys_softc	*parent_sc;
	gpio_pin_t		pin;
	int			irq_rid;
	struct resource		*irq_res;
	void			*intr_hl;
	struct mtx		mtx;
	uint32_t		keycode;
	int			autorepeat;
	struct callout		debounce_callout;
	struct callout		repeat_callout;
	int			repeat_delay;
	int			repeat;
	int			debounce_interval;
};

struct gpiokeys_softc
{
	device_t	sc_dev;
	struct mtx	sc_mtx;
	struct gpiokey	*sc_keys;
	int		sc_total_keys;

	keyboard_t	sc_kbd;
	keymap_t	sc_keymap;
	accentmap_t	sc_accmap;
	fkeytab_t	sc_fkeymap[GPIOKEYS_GLOBAL_NFKEY];

	uint32_t	sc_input[GPIOKEYS_GLOBAL_IN_BUF_SIZE];	/* input buffer */
	uint32_t	sc_time_ms;
#define	GPIOKEYS_GLOBAL_FLAG_POLLING	0x00000002

	uint32_t	sc_flags;		/* flags */

	int		sc_mode;		/* input mode (K_XLATE,K_RAW,K_CODE) */
	int		sc_state;		/* shift/lock key state */
	int		sc_accents;		/* accent key index (> 0) */
	int		sc_kbd_size;

	uint16_t	sc_inputs;
	uint16_t	sc_inputhead;
	uint16_t	sc_inputtail;

	uint8_t		sc_kbd_id;
};

/* gpio-keys device */
static int gpiokeys_probe(device_t);
static int gpiokeys_attach(device_t);
static int gpiokeys_detach(device_t);

/* kbd methods prototypes */
static int	gpiokeys_set_typematic(keyboard_t *, int);
static uint32_t	gpiokeys_read_char(keyboard_t *, int);
static void	gpiokeys_clear_state(keyboard_t *);
static int	gpiokeys_ioctl(keyboard_t *, u_long, caddr_t);
static int	gpiokeys_enable(keyboard_t *);
static int	gpiokeys_disable(keyboard_t *);
static void	gpiokeys_event_keyinput(struct gpiokeys_softc *);

static void
gpiokeys_put_key(struct gpiokeys_softc *sc, uint32_t key)
{

	GPIOKEYS_ASSERT_LOCKED(sc);

	if (sc->sc_inputs < GPIOKEYS_GLOBAL_IN_BUF_SIZE) {
		sc->sc_input[sc->sc_inputtail] = key;
		++(sc->sc_inputs);
		++(sc->sc_inputtail);
		if (sc->sc_inputtail >= GPIOKEYS_GLOBAL_IN_BUF_SIZE) {
			sc->sc_inputtail = 0;
		}
	} else {
		device_printf(sc->sc_dev, "input buffer is full\n");
	}
}

static void
gpiokeys_key_event(struct gpiokeys_softc *sc, uint16_t keycode, int pressed)
{
	uint32_t key;


	key = keycode & SCAN_KEYCODE_MASK;

	if (!pressed)
		key |= KEY_RELEASE;

	GPIOKEYS_LOCK(sc);
	if (keycode & SCAN_PREFIX_E0)
		gpiokeys_put_key(sc, 0xe0);
	else if (keycode & SCAN_PREFIX_E1)
		gpiokeys_put_key(sc, 0xe1);

	gpiokeys_put_key(sc, key);
	GPIOKEYS_UNLOCK(sc);

	gpiokeys_event_keyinput(sc);
}

static void
gpiokey_autorepeat(void *arg)
{
	struct gpiokey *key;

	key = arg;

	if (key->keycode == GPIOKEY_NONE)
		return;

	gpiokeys_key_event(key->parent_sc, key->keycode, 1);

	callout_reset(&key->repeat_callout, key->repeat,
		    gpiokey_autorepeat, key);
}

static void
gpiokey_debounced_intr(void *arg)
{
	struct gpiokey *key;
	bool active;

	key = arg;

	if (key->keycode == GPIOKEY_NONE)
		return;

	gpio_pin_is_active(key->pin, &active);
	if (active) {
		gpiokeys_key_event(key->parent_sc, key->keycode, 1);
		if (key->autorepeat) {
			callout_reset(&key->repeat_callout, key->repeat_delay,
			    gpiokey_autorepeat, key);
		}
	}
	else {
		if (key->autorepeat &&
		    callout_pending(&key->repeat_callout))
			callout_stop(&key->repeat_callout);
		gpiokeys_key_event(key->parent_sc, key->keycode, 0);
	}
}

static void
gpiokey_intr(void *arg)
{
	struct gpiokey *key;
	int debounce_ticks;

	key = arg;

	GPIOKEY_LOCK(key);
	debounce_ticks = (hz * key->debounce_interval) / 1000;
	if (debounce_ticks == 0)
		debounce_ticks = 1;
	if (!callout_pending(&key->debounce_callout))
		callout_reset(&key->debounce_callout, debounce_ticks,
		    gpiokey_debounced_intr, key);
	GPIOKEY_UNLOCK(key);
}

static void
gpiokeys_attach_key(struct gpiokeys_softc *sc, phandle_t node,
    struct gpiokey *key)
{
	pcell_t prop;
	char *name;
	uint32_t code;
	int err;
	const char *key_name;

	GPIOKEY_LOCK_INIT(key);
	key->parent_sc = sc;
	callout_init_mtx(&key->debounce_callout, &key->mtx, 0);
	callout_init_mtx(&key->repeat_callout, &key->mtx, 0);

	name = NULL;
	if (OF_getprop_alloc(node, "label", (void **)&name) == -1)
		OF_getprop_alloc(node, "name", (void **)&name);

	if (name != NULL)
		key_name = name;
	else
		key_name = "unknown";

	key->autorepeat = OF_hasprop(node, "autorepeat");

	key->repeat_delay = (hz * AUTOREPEAT_DELAY) / 1000;
	if (key->repeat_delay == 0)
		key->repeat_delay = 1;

	key->repeat = (hz * AUTOREPEAT_REPEAT) / 1000;
	if (key->repeat == 0)
		key->repeat = 1;

	if ((OF_getprop(node, "debounce-interval", &prop, sizeof(prop))) > 0)
		key->debounce_interval = fdt32_to_cpu(prop);
	else
		key->debounce_interval = 5;

	if ((OF_getprop(node, "freebsd,code", &prop, sizeof(prop))) > 0)
		key->keycode = fdt32_to_cpu(prop);
	else if ((OF_getprop(node, "linux,code", &prop, sizeof(prop))) > 0) {
		code = fdt32_to_cpu(prop);
		key->keycode = gpiokey_map_linux_code(code);
		if (key->keycode == GPIOKEY_NONE)
			device_printf(sc->sc_dev, "<%s> failed to map linux,code value 0x%x\n",
			    key_name, code);
	}
	else
		device_printf(sc->sc_dev, "<%s> no linux,code or freebsd,code property\n",
		    key_name);

	err = gpio_pin_get_by_ofw_idx(sc->sc_dev, node, 0, &key->pin);
	if (err) {
		device_printf(sc->sc_dev, "<%s> failed to map pin\n", key_name);
		if (name)
			OF_prop_free(name);
		return;
	}

	key->irq_res = gpio_alloc_intr_resource(sc->sc_dev, &key->irq_rid,
	    RF_ACTIVE, key->pin, GPIO_INTR_EDGE_BOTH);
	if (!key->irq_res) {
		device_printf(sc->sc_dev, "<%s> cannot allocate interrupt\n", key_name);
		gpio_pin_release(key->pin);
		key->pin = NULL;
		if (name)
			OF_prop_free(name);
		return;
	}

	if (bus_setup_intr(sc->sc_dev, key->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
			NULL, gpiokey_intr, key,
			&key->intr_hl) != 0) {
		device_printf(sc->sc_dev, "<%s> unable to setup the irq handler\n", key_name);
		bus_release_resource(sc->sc_dev, SYS_RES_IRQ, key->irq_rid,
		    key->irq_res);
		gpio_pin_release(key->pin);
		key->pin = NULL;
		key->irq_res = NULL;
		if (name)
			OF_prop_free(name);
		return;
	}

	if (bootverbose)
		device_printf(sc->sc_dev, "<%s> code=%08x, autorepeat=%d, "\
		    "repeat=%d, repeat_delay=%d\n", key_name, key->keycode,
		    key->autorepeat, key->repeat, key->repeat_delay);

	if (name)
		OF_prop_free(name);
}

static void
gpiokeys_detach_key(struct gpiokeys_softc *sc, struct gpiokey *key)
{

	GPIOKEY_LOCK(key);
	if (key->intr_hl)
		bus_teardown_intr(sc->sc_dev, key->irq_res, key->intr_hl);
	if (key->irq_res)
		bus_release_resource(sc->sc_dev, SYS_RES_IRQ,
		    key->irq_rid, key->irq_res);
	if (callout_pending(&key->repeat_callout))
		callout_drain(&key->repeat_callout);
	if (callout_pending(&key->debounce_callout))
		callout_drain(&key->debounce_callout);
	if (key->pin)
		gpio_pin_release(key->pin);
	GPIOKEY_UNLOCK(key);

	GPIOKEY_LOCK_DESTROY(key);
}

static int
gpiokeys_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "gpio-keys"))
		return (ENXIO);

	device_set_desc(dev, "GPIO keyboard");

	return (0);
}

static int
gpiokeys_attach(device_t dev)
{
	int unit;
	struct gpiokeys_softc *sc;
	keyboard_t *kbd;
	phandle_t keys, child;
	int total_keys;

	if ((keys = ofw_bus_get_node(dev)) == -1)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	kbd = &sc->sc_kbd;

	GPIOKEYS_LOCK_INIT(sc);
	unit = device_get_unit(dev);
	kbd_init_struct(kbd, KBD_DRIVER_NAME, KB_OTHER, unit, 0, 0, 0);

	kbd->kb_data = (void *)sc;
	sc->sc_mode = K_XLATE;

	sc->sc_keymap = key_map;
	sc->sc_accmap = accent_map;

	kbd_set_maps(kbd, &sc->sc_keymap, &sc->sc_accmap,
	    sc->sc_fkeymap, GPIOKEYS_GLOBAL_NFKEY);

	KBD_FOUND_DEVICE(kbd);

	gpiokeys_clear_state(kbd);

	KBD_PROBE_DONE(kbd);

	KBD_INIT_DONE(kbd);

	if (kbd_register(kbd) < 0) {
		goto detach;
	}

	KBD_CONFIG_DONE(kbd);

	gpiokeys_enable(kbd);

#ifdef KBD_INSTALL_CDEV
	if (kbd_attach(kbd)) {
		goto detach;
	}
#endif

	if (bootverbose) {
		genkbd_diag(kbd, 1);
	}

	total_keys = 0;

	/* Traverse the 'gpio-keys' node and count keys */
	for (child = OF_child(keys); child != 0; child = OF_peer(child)) {
		if (!OF_hasprop(child, "gpios"))
			continue;
		total_keys++;
	}

	if (total_keys) {
		sc->sc_keys =  malloc(sizeof(struct gpiokey) * total_keys,
		    M_DEVBUF, M_WAITOK | M_ZERO);

		sc->sc_total_keys = 0;
		/* Traverse the 'gpio-keys' node and count keys */
		for (child = OF_child(keys); child != 0; child = OF_peer(child)) {
			if (!OF_hasprop(child, "gpios"))
				continue;
			gpiokeys_attach_key(sc, child ,&sc->sc_keys[sc->sc_total_keys]);
			sc->sc_total_keys++;
		}
	}

	return (0);

detach:
	gpiokeys_detach(dev);
	return (ENXIO);
}

static int
gpiokeys_detach(device_t dev)
{
	struct gpiokeys_softc *sc;
	keyboard_t *kbd;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < sc->sc_total_keys; i++)
		gpiokeys_detach_key(sc, &sc->sc_keys[i]);

	kbd = kbd_get_keyboard(kbd_find_keyboard(KBD_DRIVER_NAME,
	    device_get_unit(dev)));

#ifdef KBD_INSTALL_CDEV
	kbd_detach(kbd);
#endif
	kbd_unregister(kbd);

	GPIOKEYS_LOCK_DESTROY(sc);
	if (sc->sc_keys)
		free(sc->sc_keys, M_DEVBUF);

	return (0);
}

/* early keyboard probe, not supported */
static int
gpiokeys_configure(int flags)
{
	return (0);
}

/* detect a keyboard, not used */
static int
gpiokeys__probe(int unit, void *arg, int flags)
{
	return (ENXIO);
}

/* reset and initialize the device, not used */
static int
gpiokeys_init(int unit, keyboard_t **kbdp, void *arg, int flags)
{
	return (ENXIO);
}

/* test the interface to the device, not used */
static int
gpiokeys_test_if(keyboard_t *kbd)
{
	return (0);
}

/* finish using this keyboard, not used */
static int
gpiokeys_term(keyboard_t *kbd)
{
	return (ENXIO);
}

/* keyboard interrupt routine, not used */
static int
gpiokeys_intr(keyboard_t *kbd, void *arg)
{
	return (0);
}

/* lock the access to the keyboard, not used */
static int
gpiokeys_lock(keyboard_t *kbd, int lock)
{
	return (1);
}

/*
 * Enable the access to the device; until this function is called,
 * the client cannot read from the keyboard.
 */
static int
gpiokeys_enable(keyboard_t *kbd)
{
	struct gpiokeys_softc *sc;

	sc = kbd->kb_data;
	GPIOKEYS_LOCK(sc);
	KBD_ACTIVATE(kbd);
	GPIOKEYS_UNLOCK(sc);

	return (0);
}

/* disallow the access to the device */
static int
gpiokeys_disable(keyboard_t *kbd)
{
	struct gpiokeys_softc *sc;

	sc = kbd->kb_data;
	GPIOKEYS_LOCK(sc);
	KBD_DEACTIVATE(kbd);
	GPIOKEYS_UNLOCK(sc);

	return (0);
}

static void
gpiokeys_do_poll(struct gpiokeys_softc *sc, uint8_t wait)
{

	KASSERT((sc->sc_flags & GPIOKEYS_GLOBAL_FLAG_POLLING) != 0,
	    ("gpiokeys_do_poll called when not polling\n"));

	GPIOKEYS_ASSERT_LOCKED(sc);

	if (!kdb_active && !SCHEDULER_STOPPED()) {
		while (sc->sc_inputs == 0) {
			kern_yield(PRI_UNCHANGED);
			if (!wait)
				break;
		}
		return;
	}

	while ((sc->sc_inputs == 0) && wait) {
		printf("POLL!\n");
	}
}

/* check if data is waiting */
static int
gpiokeys_check(keyboard_t *kbd)
{
	struct gpiokeys_softc *sc = kbd->kb_data;

	GPIOKEYS_ASSERT_LOCKED(sc);

	if (!KBD_IS_ACTIVE(kbd))
		return (0);

	if (sc->sc_flags & GPIOKEYS_GLOBAL_FLAG_POLLING)
		gpiokeys_do_poll(sc, 0);

	if (sc->sc_inputs > 0) {
		return (1);
	}
	return (0);
}

/* check if char is waiting */
static int
gpiokeys_check_char_locked(keyboard_t *kbd)
{
	if (!KBD_IS_ACTIVE(kbd))
		return (0);

	return (gpiokeys_check(kbd));
}

static int
gpiokeys_check_char(keyboard_t *kbd)
{
	int result;
	struct gpiokeys_softc *sc = kbd->kb_data;

	GPIOKEYS_LOCK(sc);
	result = gpiokeys_check_char_locked(kbd);
	GPIOKEYS_UNLOCK(sc);

	return (result);
}

static int32_t
gpiokeys_get_key(struct gpiokeys_softc *sc, uint8_t wait)
{
	int32_t c;

	KASSERT((!kdb_active && !SCHEDULER_STOPPED())
	    || (sc->sc_flags & GPIOKEYS_GLOBAL_FLAG_POLLING) != 0,
	    ("not polling in kdb or panic\n"));

	GPIOKEYS_ASSERT_LOCKED(sc);

	if (sc->sc_flags & GPIOKEYS_GLOBAL_FLAG_POLLING)
		gpiokeys_do_poll(sc, wait);

	if (sc->sc_inputs == 0) {
		c = -1;
	} else {
		c = sc->sc_input[sc->sc_inputhead];
		--(sc->sc_inputs);
		++(sc->sc_inputhead);
		if (sc->sc_inputhead >= GPIOKEYS_GLOBAL_IN_BUF_SIZE) {
			sc->sc_inputhead = 0;
		}
	}

	return (c);
}

/* read one byte from the keyboard if it's allowed */
static int
gpiokeys_read(keyboard_t *kbd, int wait)
{
	struct gpiokeys_softc *sc = kbd->kb_data;
	int32_t keycode;

	if (!KBD_IS_ACTIVE(kbd))
		return (-1);

	/* XXX */
	keycode = gpiokeys_get_key(sc, (wait == FALSE) ? 0 : 1);
	if (!KBD_IS_ACTIVE(kbd) || (keycode == -1))
		return (-1);

	++(kbd->kb_count);

	return (keycode);
}

/* read char from the keyboard */
static uint32_t
gpiokeys_read_char_locked(keyboard_t *kbd, int wait)
{
	struct gpiokeys_softc *sc = kbd->kb_data;
	uint32_t action;
	uint32_t keycode;

	if (!KBD_IS_ACTIVE(kbd))
		return (NOKEY);

next_code:

	/* see if there is something in the keyboard port */
	/* XXX */
	keycode = gpiokeys_get_key(sc, (wait == FALSE) ? 0 : 1);
	++kbd->kb_count;

	/* return the byte as is for the K_RAW mode */
	if (sc->sc_mode == K_RAW) {
		return (keycode);
	}

	/* return the key code in the K_CODE mode */
	/* XXX: keycode |= SCAN_RELEASE; */

	if (sc->sc_mode == K_CODE) {
		return (keycode);
	}

	/* keycode to key action */
	action = genkbd_keyaction(kbd, SCAN_CHAR(keycode),
	    (keycode & SCAN_RELEASE),
	    &sc->sc_state, &sc->sc_accents);
	if (action == NOKEY) {
		goto next_code;
	}

	return (action);
}

/* Currently wait is always false. */
static uint32_t
gpiokeys_read_char(keyboard_t *kbd, int wait)
{
	uint32_t keycode;
	struct gpiokeys_softc *sc = kbd->kb_data;

	GPIOKEYS_LOCK(sc);
	keycode = gpiokeys_read_char_locked(kbd, wait);
	GPIOKEYS_UNLOCK(sc);

	return (keycode);
}

/* some useful control functions */
static int
gpiokeys_ioctl_locked(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	struct gpiokeys_softc *sc = kbd->kb_data;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	int ival;

#endif

	switch (cmd) {
	case KDGKBMODE:		/* get keyboard mode */
		*(int *)arg = sc->sc_mode;
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
			if (sc->sc_mode != K_XLATE) {
				/* make lock key state and LED state match */
				sc->sc_state &= ~LOCK_MASK;
				sc->sc_state |= KBD_LED_VAL(kbd);
			}
			/* FALLTHROUGH */
		case K_RAW:
		case K_CODE:
			if (sc->sc_mode != *(int *)arg) {
				if ((sc->sc_flags & GPIOKEYS_GLOBAL_FLAG_POLLING) == 0)
					gpiokeys_clear_state(kbd);
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
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IO('K', 66):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETLED:			/* set keyboard LED */
		KBD_LED_VAL(kbd) = *(int *)arg;
		break;
	case KDGKBSTATE:		/* get lock key state */
		*(int *)arg = sc->sc_state & LOCK_MASK;
		break;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IO('K', 20):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSKBSTATE:		/* set lock key state */
		if (*(int *)arg & ~LOCK_MASK) {
			return (EINVAL);
		}
		sc->sc_state &= ~LOCK_MASK;
		sc->sc_state |= *(int *)arg;
		return (0);

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

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IO('K', 67):
		ival = IOCPARM_IVAL(arg);
		arg = (caddr_t)&ival;
		/* FALLTHROUGH */
#endif
	case KDSETRAD:			/* set keyboard repeat rate (old
					 * interface) */
		return (gpiokeys_set_typematic(kbd, *(int *)arg));

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
gpiokeys_ioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	int result;
	struct gpiokeys_softc *sc;

	sc = kbd->kb_data;
	/*
	 * XXX Check if someone is calling us from a critical section:
	 */
	if (curthread->td_critnest != 0)
		return (EDEADLK);

	GPIOKEYS_LOCK(sc);
	result = gpiokeys_ioctl_locked(kbd, cmd, arg);
	GPIOKEYS_UNLOCK(sc);

	return (result);
}

/* clear the internal state of the keyboard */
static void
gpiokeys_clear_state(keyboard_t *kbd)
{
	struct gpiokeys_softc *sc = kbd->kb_data;

	sc->sc_flags &= ~(GPIOKEYS_GLOBAL_FLAG_POLLING);
	sc->sc_state &= LOCK_MASK;	/* preserve locking key state */
	sc->sc_accents = 0;
}

/* get the internal state, not used */
static int
gpiokeys_get_state(keyboard_t *kbd, void *buf, size_t len)
{
	return (len == 0) ? 1 : -1;
}

/* set the internal state, not used */
static int
gpiokeys_set_state(keyboard_t *kbd, void *buf, size_t len)
{
	return (EINVAL);
}

static int
gpiokeys_poll(keyboard_t *kbd, int on)
{
	struct gpiokeys_softc *sc = kbd->kb_data;

	GPIOKEYS_LOCK(sc);
	if (on)
		sc->sc_flags |= GPIOKEYS_GLOBAL_FLAG_POLLING;
	else
		sc->sc_flags &= ~GPIOKEYS_GLOBAL_FLAG_POLLING;
	GPIOKEYS_UNLOCK(sc);

	return (0);
}

static int
gpiokeys_set_typematic(keyboard_t *kbd, int code)
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

static void
gpiokeys_event_keyinput(struct gpiokeys_softc *sc)
{
	int c;

	if ((sc->sc_flags & GPIOKEYS_GLOBAL_FLAG_POLLING) != 0)
		return;

	if (KBD_IS_ACTIVE(&sc->sc_kbd) &&
	    KBD_IS_BUSY(&sc->sc_kbd)) {
		/* let the callback function process the input */
		(sc->sc_kbd.kb_callback.kc_func) (&sc->sc_kbd, KBDIO_KEYINPUT,
		    sc->sc_kbd.kb_callback.kc_arg);
	} else {
		/* read and discard the input, no one is waiting for it */
		do {
			c = gpiokeys_read_char(&sc->sc_kbd, 0);
		} while (c != NOKEY);
	}
}

static keyboard_switch_t gpiokeyssw = {
	.probe = &gpiokeys__probe,
	.init = &gpiokeys_init,
	.term = &gpiokeys_term,
	.intr = &gpiokeys_intr,
	.test_if = &gpiokeys_test_if,
	.enable = &gpiokeys_enable,
	.disable = &gpiokeys_disable,
	.read = &gpiokeys_read,
	.check = &gpiokeys_check,
	.read_char = &gpiokeys_read_char,
	.check_char = &gpiokeys_check_char,
	.ioctl = &gpiokeys_ioctl,
	.lock = &gpiokeys_lock,
	.clear_state = &gpiokeys_clear_state,
	.get_state = &gpiokeys_get_state,
	.set_state = &gpiokeys_set_state,
	.get_fkeystr = &genkbd_get_fkeystr,
	.poll = &gpiokeys_poll,
	.diag = &genkbd_diag,
};

KEYBOARD_DRIVER(gpiokeys, gpiokeyssw, gpiokeys_configure);

static int
gpiokeys_driver_load(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		kbd_add_driver(&gpiokeys_kbd_driver);
		break;
	case MOD_UNLOAD:
		kbd_delete_driver(&gpiokeys_kbd_driver);
		break;
	}
	return (0);
}

static devclass_t gpiokeys_devclass;

static device_method_t gpiokeys_methods[] = {
	DEVMETHOD(device_probe,		gpiokeys_probe),
	DEVMETHOD(device_attach,	gpiokeys_attach),
	DEVMETHOD(device_detach,	gpiokeys_detach),

	DEVMETHOD_END
};

static driver_t gpiokeys_driver = {
	"gpiokeys",
	gpiokeys_methods,
	sizeof(struct gpiokeys_softc),
};

DRIVER_MODULE(gpiokeys, simplebus, gpiokeys_driver, gpiokeys_devclass, gpiokeys_driver_load, 0);
MODULE_VERSION(gpiokeys, 1);
