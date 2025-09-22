/* $OpenBSD: crosec_kbd.c,v 1.5 2023/01/23 09:36:39 nicm Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <armv7/exynos/crosecvar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

void cros_ec_add_task(void *);
void cros_ec_poll_keystate(void *);
int cros_ec_get_keystate(struct cros_ec_softc *);

int cros_ec_keyboard_enable(void *, int);
void cros_ec_keyboard_set_leds(void *, int);
int cros_ec_keyboard_ioctl(void *, u_long, caddr_t, int, struct proc *);

struct wskbd_accessops cros_ec_keyboard_accessops = {
	cros_ec_keyboard_enable,
	cros_ec_keyboard_set_leds,
	cros_ec_keyboard_ioctl,
};

void cros_ec_keyboard_cngetc(void *, u_int *, int *);
void cros_ec_keyboard_cnpollc(void *, int);

struct wskbd_consops cros_ec_keyboard_consops = {
	cros_ec_keyboard_cngetc,
	cros_ec_keyboard_cnpollc,
};


/* XXX: assumes 8 rows, 13 cols, FDT */
#define KC(n) KS_KEYCODE(n)
static const keysym_t cros_ec_keyboard_keydesc_us[] = {
	KC(1),	KS_Caps_Lock,
	KC(3),	KS_b,
	KC(6),	KS_n,
	KC(10),	KS_Alt_R,
	KC(14),	KS_Escape,
	KC(16),	KS_g,
	KC(19),	KS_h,
	KC(24),	KS_BackSpace,
	KC(26),	KS_Control_L,
	KC(27),	KS_Tab,		KS_Backtab,
	KC(29),	KS_t,
	KC(32),	KS_y,
	KC(42),	KS_5,
	KC(45),	KS_6,
	KC(52),	KS_Control_R,
	KC(53),	KS_a,
	KC(54),	KS_d,
	KC(55),	KS_f,
	KC(56),	KS_s,
	KC(57),	KS_k,
	KC(58),	KS_j,
	KC(61),	KS_l,
	KC(63),	KS_Return,
	KC(66),	KS_z,
	KC(67),	KS_c,
	KC(68),	KS_v,
	KC(69),	KS_x,
	KC(70),	KS_comma,
	KC(71),	KS_m,
	KC(72),	KS_Shift_L,
	KC(74),	KS_period,
	KC(76),	KS_space,
	KC(79),	KS_1,
	KC(80),	KS_3,
	KC(81),	KS_4,
	KC(82),	KS_2,
	KC(83),	KS_8,
	KC(84),	KS_7,
	KC(86),	KS_0,
	KC(87),	KS_9,
	KC(88),	KS_Alt_L,
	KC(92),	KS_q,
	KC(93),	KS_e,
	KC(94),	KS_r,
	KC(95),	KS_w,
	KC(96),	KS_i,
	KC(97),	KS_u,
	KC(98),	KS_Shift_R,
	KC(99),	KS_p,
	KC(100),	KS_o,
};

#define KBD_MAP(name, base, map) \
	{ name, base, sizeof(map)/sizeof(keysym_t), map }
static const struct wscons_keydesc cros_ec_keyboard_keydesctab[] = {
	KBD_MAP(KB_US,			0,	cros_ec_keyboard_keydesc_us),
	{0, 0, 0, 0}
};
struct wskbd_mapdata cros_ec_keyboard_keymapdata = {
	cros_ec_keyboard_keydesctab,
	KB_US,
};

int
cros_ec_init_keyboard(struct cros_ec_softc *sc)
{
	struct ec_response_cros_ec_info info;
	struct wskbddev_attach_args a;

	if (cros_ec_info(sc, &info)) {
		printf("%s: could not read KBC info\n", __func__);
		return (-1);
	}

	sc->keyboard.rows = info.rows;
	sc->keyboard.cols = info.cols;
	sc->keyboard.switches = info.switches;
	sc->keyboard.state = (uint8_t *)malloc(info.rows*info.cols,
			M_DEVBUF, M_WAITOK|M_ZERO);
	if (sc->keyboard.state == NULL)
		panic("%s: no memory available for keyboard states", __func__);

	/* FIXME: interrupt driven, please. */
	sc->keyboard.taskq = taskq_create("crosec-keyb", 1, IPL_TTY, 0);
	task_set(&sc->keyboard.task, cros_ec_poll_keystate, sc);
	timeout_set(&sc->keyboard.timeout, cros_ec_add_task, sc);

	/* XXX: ghosting */

	wskbd_cnattach(&cros_ec_keyboard_consops, sc, &cros_ec_keyboard_keymapdata);
	a.console = 1;

	a.keymap = &cros_ec_keyboard_keymapdata;
	a.accessops = &cros_ec_keyboard_accessops;
	a.accesscookie = sc;

	sc->keyboard.wskbddev = config_found((void *)sc, &a, wskbddevprint);

	timeout_add_sec(&sc->keyboard.timeout, 10);

	return 0;
}

void
cros_ec_add_task(void *arg)
{
	struct cros_ec_softc *sc = (struct cros_ec_softc *)arg;
	task_add(sc->keyboard.taskq, &sc->keyboard.task);
	timeout_add_msec(&sc->keyboard.timeout, 100);
}

void
cros_ec_poll_keystate(void *arg)
{
	struct cros_ec_softc *sc = (struct cros_ec_softc *)arg;
	cros_ec_get_keystate(sc);
}

int
cros_ec_get_keystate(struct cros_ec_softc *sc)
{
	int col, row, s;
	uint8_t state[sc->keyboard.cols];
	cros_ec_scan_keyboard(sc, state, sc->keyboard.cols);
	for (col = 0; col < sc->keyboard.cols; col++) {
		for (row = 0; row < sc->keyboard.rows; row++) {
			int off = row*sc->keyboard.cols;
			int pressed = !!(state[col] & (1 << row));
			if (pressed && !sc->keyboard.state[off+col]) {
				//printf("row %d col %d id %d pressed\n", row, col, off+col);
				sc->keyboard.state[off+col] = 1;
				if (sc->keyboard.polling)
					return off+col;
				s = spltty();
				wskbd_input(sc->keyboard.wskbddev, WSCONS_EVENT_KEY_DOWN, off+col);
				splx(s);
			} else if (!pressed && sc->keyboard.state[off+col]) {
				//printf("row %d col %d id %d released\n", row, col, off+col);
				sc->keyboard.state[off+col] = 0;
				if (sc->keyboard.polling)
					return off+col;
				s = spltty();
				wskbd_input(sc->keyboard.wskbddev, WSCONS_EVENT_KEY_UP, off+col);
				splx(s);
			} else if (sc->keyboard.state[off+col]) {
				//printf("row %d col %d id %d repeated\n", row, col, off+col);
			}
		}
	}
	return (-1);
}

void
cros_ec_keyboard_cngetc(void *v, u_int *type, int *data)
{
	struct cros_ec_softc *sc = v;
	int key;

	sc->keyboard.polling = 1;
	while ((key = cros_ec_get_keystate(sc)) == -1) {
		delay(10000);
	}
	sc->keyboard.polling = 0;

	*data = key;
	*type = sc->keyboard.state[key] ? WSCONS_EVENT_KEY_UP : WSCONS_EVENT_KEY_DOWN;
}

void
cros_ec_keyboard_cnpollc(void *v, int on)
{
}

int
cros_ec_keyboard_enable(void *v, int on)
{
	return 0;
}

void
cros_ec_keyboard_set_leds(void *v, int on)
{
}

int
cros_ec_keyboard_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
#ifdef WSDISPLAY_COMPAT_RAWKBD
	struct cros_ec_softc *sc = v;
#endif

	switch (cmd) {

	case WSKBDIO_GTYPE:
		*(int *)data = WSKBD_TYPE_ZAURUS;
		return 0;
	case WSKBDIO_SETLEDS:
		return 0;
	case WSKBDIO_GETLEDS:
		*(int *)data = 0;
		return 0;
#ifdef WSDISPLAY_COMPAT_RAWKBD
	case WSKBDIO_SETMODE:
		sc->keyboard.rawkbd = *(int *)data == WSKBD_RAW;
		return (0);
#endif

	}
	/* kbdioctl(...); */

	return -1;
}
