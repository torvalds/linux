/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * All rights reserved.
 *
 * Based on dev/usb/input/ukbd.c  
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

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <sys/ioccom.h>
#include <sys/filio.h>
#include <sys/tty.h>
#include <sys/kbio.h>

#include <dev/kbd/kbdreg.h>

#include <machine/bus.h>

#include <dev/kbd/kbdtables.h>

#define	KMI_LOCK()	mtx_lock(&Giant)
#define	KMI_UNLOCK()	mtx_unlock(&Giant)

#ifdef	INVARIANTS
/*
 * Assert that the lock is held in all contexts
 * where the code can be executed.
 */
#define	KMI_LOCK_ASSERT()	mtx_assert(&Giant, MA_OWNED)
/*
 * Assert that the lock is held in the contexts
 * where it really has to be so.
 */
#define	KMI_CTX_LOCK_ASSERT()			 	\
	do {						\
		if (!kdb_active && panicstr == NULL)	\
			mtx_assert(&Giant, MA_OWNED);	\
	} while (0)
#else
#define KMI_LOCK_ASSERT()	(void)0
#define KMI_CTX_LOCK_ASSERT()	(void)0
#endif

#define	KMICR		0x00
#define		KMICR_TYPE_NONPS2	(1 << 5)
#define		KMICR_RXINTREN		(1 << 4)
#define		KMICR_TXINTREN		(1 << 3)
#define		KMICR_EN		(1 << 2)
#define		KMICR_FKMID		(1 << 1)
#define		KMICR_FKMIC		(1 << 0)
#define	KMISTAT		0x04
#define		KMISTAT_TXEMPTY		(1 << 6)
#define		KMISTAT_TXBUSY		(1 << 5)
#define		KMISTAT_RXFULL		(1 << 4)
#define		KMISTAT_RXBUSY		(1 << 3)
#define		KMISTAT_RXPARITY	(1 << 2)
#define		KMISTAT_KMIC		(1 << 1)
#define		KMISTAT_KMID		(1 << 0)
#define	KMIDATA		0x08
#define	KMICLKDIV	0x0C
#define	KMIIR		0x10
#define		KMIIR_TXINTR		(1 << 1)
#define		KMIIR_RXINTR		(1 << 0)

#define	KMI_DRIVER_NAME          "kmi"
#define	KMI_NFKEY        (sizeof(fkey_tab)/sizeof(fkey_tab[0]))	/* units */

#define	SET_SCANCODE_SET	0xf0

struct kmi_softc {
	device_t sc_dev;
	keyboard_t sc_kbd;
	keymap_t sc_keymap;
	accentmap_t sc_accmap;
	fkeytab_t sc_fkeymap[KMI_NFKEY];

	struct resource*	sc_mem_res;
	struct resource*	sc_irq_res;
	void*			sc_intr_hl;

	int			sc_mode;		/* input mode (K_XLATE,K_RAW,K_CODE) */
	int			sc_state;		/* shift/lock key state */
	int			sc_accents;		/* accent key index (> 0) */
	uint32_t		sc_flags;		/* flags */
#define	KMI_FLAG_COMPOSE	0x00000001
#define	KMI_FLAG_POLLING	0x00000002

	struct			thread *sc_poll_thread;
};

/* Read/Write macros for Timer used as timecounter */
#define pl050_kmi_read_4(sc, reg)		\
	bus_read_4((sc)->sc_mem_res, (reg))

#define pl050_kmi_write_4(sc, reg, val)	\
	bus_write_4((sc)->sc_mem_res, (reg), (val))

/* prototypes */
static void	kmi_set_leds(struct kmi_softc *, uint8_t);
static int	kmi_set_typematic(keyboard_t *, int);
static uint32_t	kmi_read_char(keyboard_t *, int);
static void	kmi_clear_state(keyboard_t *);
static int	kmi_ioctl(keyboard_t *, u_long, caddr_t);
static int	kmi_enable(keyboard_t *);
static int	kmi_disable(keyboard_t *);

static int	kmi_attached = 0;

/* early keyboard probe, not supported */
static int
kmi_configure(int flags)
{
	return (0);
}

/* detect a keyboard, not used */
static int
kmi_probe(int unit, void *arg, int flags)
{
	return (ENXIO);
}

/* reset and initialize the device, not used */
static int
kmi_init(int unit, keyboard_t **kbdp, void *arg, int flags)
{
	return (ENXIO);
}

/* test the interface to the device, not used */
static int
kmi_test_if(keyboard_t *kbd)
{
	return (0);
}

/* finish using this keyboard, not used */
static int
kmi_term(keyboard_t *kbd)
{
	return (ENXIO);
}

/* keyboard interrupt routine, not used */
static int
kmi_intr(keyboard_t *kbd, void *arg)
{

	return (0);
}

/* lock the access to the keyboard, not used */
static int
kmi_lock(keyboard_t *kbd, int lock)
{
	return (1);
}

/*
 * Enable the access to the device; until this function is called,
 * the client cannot read from the keyboard.
 */
static int
kmi_enable(keyboard_t *kbd)
{

	KMI_LOCK();
	KBD_ACTIVATE(kbd);
	KMI_UNLOCK();

	return (0);
}

/* disallow the access to the device */
static int
kmi_disable(keyboard_t *kbd)
{

	KMI_LOCK();
	KBD_DEACTIVATE(kbd);
	KMI_UNLOCK();

	return (0);
}

/* check if data is waiting */
static int
kmi_check(keyboard_t *kbd)
{
	struct kmi_softc *sc = kbd->kb_data;
	uint32_t reg;

	KMI_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (0);

	reg = pl050_kmi_read_4(sc, KMIIR);
	return (reg & KMIIR_RXINTR);
}

/* check if char is waiting */
static int
kmi_check_char_locked(keyboard_t *kbd)
{
	KMI_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (0);

	return (kmi_check(kbd));
}

static int
kmi_check_char(keyboard_t *kbd)
{
	int result;

	KMI_LOCK();
	result = kmi_check_char_locked(kbd);
	KMI_UNLOCK();

	return (result);
}

/* read one byte from the keyboard if it's allowed */
/* Currently unused. */
static int
kmi_read(keyboard_t *kbd, int wait)
{
	KMI_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (-1);

	++(kbd->kb_count);
	printf("Implement ME: %s\n", __func__);
	return (0);
}

/* read char from the keyboard */
static uint32_t
kmi_read_char_locked(keyboard_t *kbd, int wait)
{
	struct kmi_softc *sc = kbd->kb_data;
	uint32_t reg, data;

	KMI_CTX_LOCK_ASSERT();

	if (!KBD_IS_ACTIVE(kbd))
		return (NOKEY);

	reg = pl050_kmi_read_4(sc, KMIIR);
	if (reg & KMIIR_RXINTR) {
		data = pl050_kmi_read_4(sc, KMIDATA);
		return (data);
	}

	++kbd->kb_count;
	return (NOKEY);
}

/* Currently wait is always false. */
static uint32_t
kmi_read_char(keyboard_t *kbd, int wait)
{
	uint32_t keycode;

	KMI_LOCK();
	keycode = kmi_read_char_locked(kbd, wait);
	KMI_UNLOCK();

	return (keycode);
}

/* some useful control functions */
static int
kmi_ioctl_locked(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	struct kmi_softc *sc = kbd->kb_data;
	int i;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	int ival;

#endif

	KMI_LOCK_ASSERT();

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
				if ((sc->sc_flags & KMI_FLAG_POLLING) == 0)
					kmi_clear_state(kbd);
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
		if (KBD_HAS_DEVICE(kbd))
			kmi_set_leds(sc, i);

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

		/* set LEDs and quit */
		return (kmi_ioctl(kbd, KDSETLED, arg));

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
		return (kmi_set_typematic(kbd, *(int *)arg));

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
kmi_ioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
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
		KMI_LOCK();
		result = kmi_ioctl_locked(kbd, cmd, arg);
		KMI_UNLOCK();
		return (result);
	}
}

/* clear the internal state of the keyboard */
static void
kmi_clear_state(keyboard_t *kbd)
{
	struct kmi_softc *sc = kbd->kb_data;

	KMI_CTX_LOCK_ASSERT();

	sc->sc_flags &= ~(KMI_FLAG_COMPOSE | KMI_FLAG_POLLING);
	sc->sc_state &= LOCK_MASK;	/* preserve locking key state */
	sc->sc_accents = 0;
}

/* save the internal state, not used */
static int
kmi_get_state(keyboard_t *kbd, void *buf, size_t len)
{
	return (len == 0) ? 1 : -1;
}

/* set the internal state, not used */
static int
kmi_set_state(keyboard_t *kbd, void *buf, size_t len)
{
	return (EINVAL);
}

static int
kmi_poll(keyboard_t *kbd, int on)
{
	struct kmi_softc *sc = kbd->kb_data;

	KMI_LOCK();
	if (on) {
		sc->sc_flags |= KMI_FLAG_POLLING;
		sc->sc_poll_thread = curthread;
	} else {
		sc->sc_flags &= ~KMI_FLAG_POLLING;
	}
	KMI_UNLOCK();

	return (0);
}

/* local functions */

static void
kmi_set_leds(struct kmi_softc *sc, uint8_t leds)
{

	KMI_LOCK_ASSERT();

	/* start transfer, if not already started */
	printf("Implement me: %s\n", __func__);
}

static int
kmi_set_typematic(keyboard_t *kbd, int code)
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

static keyboard_switch_t kmisw = {
	.probe = &kmi_probe,
	.init = &kmi_init,
	.term = &kmi_term,
	.intr = &kmi_intr,
	.test_if = &kmi_test_if,
	.enable = &kmi_enable,
	.disable = &kmi_disable,
	.read = &kmi_read,
	.check = &kmi_check,
	.read_char = &kmi_read_char,
	.check_char = &kmi_check_char,
	.ioctl = &kmi_ioctl,
	.lock = &kmi_lock,
	.clear_state = &kmi_clear_state,
	.get_state = &kmi_get_state,
	.set_state = &kmi_set_state,
	.get_fkeystr = &genkbd_get_fkeystr,
	.poll = &kmi_poll,
	.diag = &genkbd_diag,
};

KEYBOARD_DRIVER(kmi, kmisw, kmi_configure);

static void
pl050_kmi_intr(void *arg)
{
	struct kmi_softc *sc = arg;
	uint32_t c;

	KMI_CTX_LOCK_ASSERT();

	if ((sc->sc_flags & KMI_FLAG_POLLING) != 0)
		return;

	if (KBD_IS_ACTIVE(&sc->sc_kbd) &&
	    KBD_IS_BUSY(&sc->sc_kbd)) {
		/* let the callback function process the input */
		(sc->sc_kbd.kb_callback.kc_func) (&sc->sc_kbd, KBDIO_KEYINPUT,
		    sc->sc_kbd.kb_callback.kc_arg);
	} else {
		/* read and discard the input, no one is waiting for it */
		do {
			c = kmi_read_char_locked(&sc->sc_kbd, 0);
		} while (c != NOKEY);
	}

}

static int
pl050_kmi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	/*
	 * PL050 is plain PS2 port that pushes bytes to/from computer
	 * VersatilePB has two such ports and QEMU simulates keyboard
	 * connected to port #0 and mouse connected to port #1. This
	 * information can't be obtained from device tree so we just
	 * hardcode this knowledge here. We attach keyboard driver to
	 * port #0 and ignore port #1
	 */
	if (kmi_attached)
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "arm,pl050")) {
		device_set_desc(dev, "PL050 Keyboard/Mouse Interface");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
pl050_kmi_attach(device_t dev)
{
	struct kmi_softc *sc = device_get_softc(dev);
	keyboard_t *kbd;
	int rid;
	int i;
	uint32_t ack;

	sc->sc_dev = dev;
	kbd = &sc->sc_kbd;
	rid = 0;

	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	/* Request the IRQ resources */
	sc->sc_irq_res =  bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "Error: could not allocate irq resources\n");
		return (ENXIO);
	}

	/* Setup and enable the timer */
	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_CLK,
			NULL, pl050_kmi_intr, sc,
			&sc->sc_intr_hl) != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, rid,
			sc->sc_irq_res);
		device_printf(dev, "Unable to setup the clock irq handler.\n");
		return (ENXIO);
	}

	/* TODO: clock & divisor */

	pl050_kmi_write_4(sc, KMICR, KMICR_EN);

	pl050_kmi_write_4(sc, KMIDATA, SET_SCANCODE_SET);
	/* read out ACK */
	ack = pl050_kmi_read_4(sc, KMIDATA);
	/* Set Scan Code set 1 (XT) */
	pl050_kmi_write_4(sc, KMIDATA, 1);
	/* read out ACK */
	ack = pl050_kmi_read_4(sc, KMIDATA);

	pl050_kmi_write_4(sc, KMICR, KMICR_EN | KMICR_RXINTREN);

	kbd_init_struct(kbd, KMI_DRIVER_NAME, KB_OTHER, 
			device_get_unit(dev), 0, 0, 0);
	kbd->kb_data = (void *)sc;

	sc->sc_keymap = key_map;
	sc->sc_accmap = accent_map;
	for (i = 0; i < KMI_NFKEY; i++) {
		sc->sc_fkeymap[i] = fkey_tab[i];
	}

	kbd_set_maps(kbd, &sc->sc_keymap, &sc->sc_accmap,
	    sc->sc_fkeymap, KMI_NFKEY);

	KBD_FOUND_DEVICE(kbd);
	kmi_clear_state(kbd);
	KBD_PROBE_DONE(kbd);

	KBD_INIT_DONE(kbd);

	if (kbd_register(kbd) < 0) {
		goto detach;
	}
	KBD_CONFIG_DONE(kbd);

#ifdef KBD_INSTALL_CDEV
	if (kbd_attach(kbd)) {
		goto detach;
	}
#endif

	if (bootverbose) {
		genkbd_diag(kbd, bootverbose);
	}
	kmi_attached = 1;
	return (0);

detach:
	return (ENXIO);

}

static device_method_t pl050_kmi_methods[] = {
	DEVMETHOD(device_probe,		pl050_kmi_probe),
	DEVMETHOD(device_attach,	pl050_kmi_attach),
	{ 0, 0 }
};

static driver_t pl050_kmi_driver = {
	"kmi",
	pl050_kmi_methods,
	sizeof(struct kmi_softc),
};

static devclass_t pl050_kmi_devclass;

DRIVER_MODULE(pl050_kmi, simplebus, pl050_kmi_driver, pl050_kmi_devclass, 0, 0);
