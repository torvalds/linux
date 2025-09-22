/* $OpenBSD: pms.c,v 1.102 2025/07/15 13:40:02 jsg Exp $ */
/* $NetBSD: psm.c,v 1.11 2000/06/05 22:20:57 sommerfeld Exp $ */

/*-
 * Copyright (c) 1994 Charles M. Hannum.
 * Copyright (c) 1992, 1993 Erik Forsberg.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL I BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/task.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/ic/pckbcvar.h>

#include <dev/pckbc/pmsreg.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#if defined(__i386__) || defined(__amd64__)
#include "acpi.h"
#endif

#if !defined(SMALL_KERNEL) && NACPI > 0
extern int mouse_has_softbtn;
#else
int mouse_has_softbtn;
#endif

#ifdef DEBUG
#define DPRINTF(x...)	do { printf(x); } while (0);
#else
#define DPRINTF(x...)
#endif

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

#define WSMOUSE_BUTTON(x)	(1 << ((x) - 1))

struct pms_softc;

struct pms_protocol {
	int type;
#define PMS_STANDARD		0
#define PMS_INTELLI		1
#define PMS_SYNAPTICS		2
#define PMS_ALPS		3
#define PMS_ELANTECH_V1		4
#define PMS_ELANTECH_V2		5
#define PMS_ELANTECH_V3		6
#define PMS_ELANTECH_V4		7
	u_int packetsize;
	int (*enable)(struct pms_softc *);
	int (*ioctl)(struct pms_softc *, u_long, caddr_t, int, struct proc *);
	int (*sync)(struct pms_softc *, int);
	void (*proc)(struct pms_softc *);
	void (*disable)(struct pms_softc *);
};

struct synaptics_softc {
	int identify;
	int capabilities, ext_capabilities, ext2_capabilities;
	int model, ext_model;
	int modes;

	int mode;

	int mask;
#define SYNAPTICS_MASK_NEWABS_STRICT	0xc8
#define SYNAPTICS_MASK_NEWABS_RELAXED	0xc0
#define SYNAPTICS_VALID_NEWABS_FIRST	0x80
#define SYNAPTICS_VALID_NEWABS_NEXT	0xc0

	u_int sec_buttons;

#define SYNAPTICS_PRESSURE_HI		30
#define SYNAPTICS_PRESSURE_LO		25
#define SYNAPTICS_PRESSURE		SYNAPTICS_PRESSURE_HI
#define SYNAPTICS_SCALE			4
#define SYNAPTICS_MAX_FINGERS		3
};

struct alps_softc {
	int model;
#define ALPS_GLIDEPOINT		(1 << 1)
#define ALPS_DUALPOINT		(1 << 2)
#define ALPS_PASSTHROUGH	(1 << 3)
#define ALPS_INTERLEAVED	(1 << 4)

	int mask;
	int version;

	u_int gesture;

	u_int sec_buttons;	/* trackpoint */

	int old_x, old_y;
#define ALPS_PRESSURE		40
};

struct elantech_softc {
	int flags;
#define ELANTECH_F_REPORTS_PRESSURE	0x01
#define ELANTECH_F_HAS_ROCKER		0x02
#define ELANTECH_F_2FINGER_PACKET	0x04
#define ELANTECH_F_HW_V1_OLD		0x08
#define ELANTECH_F_CRC_ENABLED		0x10
#define ELANTECH_F_TRACKPOINT		0x20
	int fw_version;

	u_int mt_slots;

	int width;

	u_char parity[256];
	u_char p1, p2, p3;

	int max_x, max_y;
	int old_x, old_y;
	int initial_pkt;
};
#define ELANTECH_IS_CLICKPAD(sc) (((sc)->elantech->fw_version & 0x1000) != 0)

struct pms_softc {		/* driver status information */
	struct device sc_dev;

	pckbc_tag_t sc_kbctag;

	int sc_state;
#define PMS_STATE_DISABLED	0
#define PMS_STATE_ENABLED	1
#define PMS_STATE_SUSPENDED	2

	struct rwlock sc_state_lock;

	int sc_dev_enable;
#define PMS_DEV_IGNORE		0x00
#define PMS_DEV_PRIMARY		0x01
#define PMS_DEV_SECONDARY	0x02

	struct task sc_rsttask;
	struct timeout sc_rsttimo;
	int sc_rststate;
#define PMS_RST_COMMENCE	0x01
#define PMS_RST_ANNOUNCED	0x02

	int poll;
	int inputstate;

	const struct pms_protocol *protocol;
	struct synaptics_softc *synaptics;
	struct alps_softc *alps;
	struct elantech_softc *elantech;

	u_char packet[8];

	struct device *sc_wsmousedev;
	struct device *sc_sec_wsmousedev;
};

static const u_int butmap[8] = {
	0,
	WSMOUSE_BUTTON(1),
	WSMOUSE_BUTTON(3),
	WSMOUSE_BUTTON(1) | WSMOUSE_BUTTON(3),
	WSMOUSE_BUTTON(2),
	WSMOUSE_BUTTON(1) | WSMOUSE_BUTTON(2),
	WSMOUSE_BUTTON(2) | WSMOUSE_BUTTON(3),
	WSMOUSE_BUTTON(1) | WSMOUSE_BUTTON(2) | WSMOUSE_BUTTON(3)
};

static const struct alps_model {
	int version;
	int mask;
	int model;
} alps_models[] = {
	{ 0x2021, 0xf8, ALPS_DUALPOINT | ALPS_PASSTHROUGH },
	{ 0x2221, 0xf8, ALPS_DUALPOINT | ALPS_PASSTHROUGH },
	{ 0x2222, 0xff, ALPS_DUALPOINT | ALPS_PASSTHROUGH },
	{ 0x3222, 0xf8, ALPS_DUALPOINT | ALPS_PASSTHROUGH },
	{ 0x5212, 0xff, ALPS_DUALPOINT | ALPS_PASSTHROUGH | ALPS_INTERLEAVED },
	{ 0x5321, 0xf8, ALPS_GLIDEPOINT },
	{ 0x5322, 0xf8, ALPS_GLIDEPOINT },
	{ 0x603b, 0xf8, ALPS_GLIDEPOINT },
	{ 0x6222, 0xcf, ALPS_DUALPOINT | ALPS_PASSTHROUGH | ALPS_INTERLEAVED },
	{ 0x6321, 0xf8, ALPS_GLIDEPOINT },
	{ 0x6322, 0xf8, ALPS_GLIDEPOINT },
	{ 0x6323, 0xf8, ALPS_GLIDEPOINT },
	{ 0x6324, 0x8f, ALPS_GLIDEPOINT },
	{ 0x6325, 0xef, ALPS_GLIDEPOINT },
	{ 0x6326, 0xf8, ALPS_GLIDEPOINT },
	{ 0x7301, 0xf8, ALPS_DUALPOINT },
	{ 0x7321, 0xf8, ALPS_GLIDEPOINT },
	{ 0x7322, 0xf8, ALPS_GLIDEPOINT },
	{ 0x7325, 0xcf, ALPS_GLIDEPOINT },
#if 0
	/*
	 * This model has a clitpad sending almost compatible PS2
	 * packets but not compatible enough to be used with the
	 * ALPS protocol.
	 */
	{ 0x633b, 0xf8, ALPS_DUALPOINT | ALPS_PASSTHROUGH },

	{ 0x7326, 0, 0 },	/* XXX Uses unknown v3 protocol */

	{ 0x7331, 0x8f, ALPS_DUALPOINT },	/* not supported */
#endif
};

static struct wsmouse_param synaptics_params[] = {
	{ WSMOUSECFG_PRESSURE_LO, SYNAPTICS_PRESSURE_LO },
	{ WSMOUSECFG_PRESSURE_HI, SYNAPTICS_PRESSURE_HI }
};

static struct wsmouse_param alps_params[] = {
	{ WSMOUSECFG_SMOOTHING, 3 }
};

int	pmsprobe(struct device *, void *, void *);
void	pmsattach(struct device *, struct device *, void *);
int	pmsactivate(struct device *, int);

void	pmsinput(void *, int);

int	pms_change_state(struct pms_softc *, int, int);

int	pms_ioctl(void *, u_long, caddr_t, int, struct proc *);
int	pms_enable(void *);
void	pms_disable(void *);

int	pms_sec_ioctl(void *, u_long, caddr_t, int, struct proc *);
int	pms_sec_enable(void *);
void	pms_sec_disable(void *);

int	pms_cmd(struct pms_softc *, u_char *, int, u_char *, int);
int	pms_spec_cmd(struct pms_softc *, int);
int	pms_get_devid(struct pms_softc *, u_char *);
int	pms_get_status(struct pms_softc *, u_char *);
int	pms_set_rate(struct pms_softc *, int);
int	pms_set_resolution(struct pms_softc *, int);
int	pms_set_scaling(struct pms_softc *, int);
int	pms_reset(struct pms_softc *);
int	pms_dev_enable(struct pms_softc *);
int	pms_dev_disable(struct pms_softc *);
void	pms_protocol_lookup(struct pms_softc *);
void	pms_reset_detect(struct pms_softc *, int);
void	pms_reset_task(void *);
void	pms_reset_timo(void *);

int	pms_enable_intelli(struct pms_softc *);

int	pms_ioctl_mouse(struct pms_softc *, u_long, caddr_t, int, struct proc *);
int	pms_sync_mouse(struct pms_softc *, int);
void	pms_proc_mouse(struct pms_softc *);

int	pms_enable_synaptics(struct pms_softc *);
int	pms_ioctl_synaptics(struct pms_softc *, u_long, caddr_t, int, struct proc *);
int	pms_sync_synaptics(struct pms_softc *, int);
void	pms_proc_synaptics(struct pms_softc *);
void	pms_disable_synaptics(struct pms_softc *);

int	pms_enable_alps(struct pms_softc *);
int	pms_ioctl_alps(struct pms_softc *, u_long, caddr_t, int, struct proc *);
int	pms_sync_alps(struct pms_softc *, int);
void	pms_proc_alps(struct pms_softc *);

int	pms_enable_elantech_v1(struct pms_softc *);
int	pms_enable_elantech_v2(struct pms_softc *);
int	pms_enable_elantech_v3(struct pms_softc *);
int	pms_enable_elantech_v4(struct pms_softc *);
int	pms_ioctl_elantech(struct pms_softc *, u_long, caddr_t, int,
    struct proc *);
int	pms_sync_elantech_v1(struct pms_softc *, int);
int	pms_sync_elantech_v2(struct pms_softc *, int);
int	pms_sync_elantech_v3(struct pms_softc *, int);
int	pms_sync_elantech_v4(struct pms_softc *, int);
void	pms_proc_elantech_v1(struct pms_softc *);
void	pms_proc_elantech_v2(struct pms_softc *);
void	pms_proc_elantech_v3(struct pms_softc *);
void	pms_proc_elantech_v4(struct pms_softc *);

int	synaptics_knock(struct pms_softc *);
int	synaptics_set_mode(struct pms_softc *, int, int);
int	synaptics_query(struct pms_softc *, int, int *);
int	synaptics_get_hwinfo(struct pms_softc *);
void	synaptics_sec_proc(struct pms_softc *);

int	alps_sec_proc(struct pms_softc *);
int	alps_get_hwinfo(struct pms_softc *);

int	elantech_knock(struct pms_softc *);
int	elantech_get_hwinfo_v1(struct pms_softc *);
int	elantech_get_hwinfo_v2(struct pms_softc *);
int	elantech_get_hwinfo_v3(struct pms_softc *);
int	elantech_get_hwinfo_v4(struct pms_softc *);
int	elantech_ps2_cmd(struct pms_softc *, u_char);
int	elantech_set_absolute_mode_v1(struct pms_softc *);
int	elantech_set_absolute_mode_v2(struct pms_softc *);
int	elantech_set_absolute_mode_v3(struct pms_softc *);
int	elantech_set_absolute_mode_v4(struct pms_softc *);

const struct cfattach pms_ca = {
	sizeof(struct pms_softc), pmsprobe, pmsattach, NULL,
	pmsactivate
};

struct cfdriver pms_cd = {
	NULL, "pms", DV_DULL
};

const struct wsmouse_accessops pms_accessops = {
	pms_enable,
	pms_ioctl,
	pms_disable,
};

const struct wsmouse_accessops pms_sec_accessops = {
	pms_sec_enable,
	pms_sec_ioctl,
	pms_sec_disable,
};

const struct pms_protocol pms_protocols[] = {
	/* Generic PS/2 mouse */
	{
		PMS_STANDARD, 3,
		NULL,
		pms_ioctl_mouse,
		pms_sync_mouse,
		pms_proc_mouse,
		NULL
	},
	/* Synaptics touchpad */
	{
		PMS_SYNAPTICS, 6,
		pms_enable_synaptics,
		pms_ioctl_synaptics,
		pms_sync_synaptics,
		pms_proc_synaptics,
		pms_disable_synaptics
	},
	/* ALPS touchpad */
	{
		PMS_ALPS, 6,
		pms_enable_alps,
		pms_ioctl_alps,
		pms_sync_alps,
		pms_proc_alps,
		NULL
	},
	/* Elantech touchpad (hardware version 1) */
	{
		PMS_ELANTECH_V1, 4,
		pms_enable_elantech_v1,
		pms_ioctl_elantech,
		pms_sync_elantech_v1,
		pms_proc_elantech_v1,
		NULL
	},
	/* Elantech touchpad (hardware version 2) */
	{
		PMS_ELANTECH_V2, 6,
		pms_enable_elantech_v2,
		pms_ioctl_elantech,
		pms_sync_elantech_v2,
		pms_proc_elantech_v2,
		NULL
	},
	/* Elantech touchpad (hardware version 3) */
	{
		PMS_ELANTECH_V3, 6,
		pms_enable_elantech_v3,
		pms_ioctl_elantech,
		pms_sync_elantech_v3,
		pms_proc_elantech_v3,
		NULL
	},
	/* Elantech touchpad (hardware version 4) */
	{
		PMS_ELANTECH_V4, 6,
		pms_enable_elantech_v4,
		pms_ioctl_elantech,
		pms_sync_elantech_v4,
		pms_proc_elantech_v4,
		NULL
	},
	/* Microsoft IntelliMouse */
	{
		PMS_INTELLI, 4,
		pms_enable_intelli,
		pms_ioctl_mouse,
		pms_sync_mouse,
		pms_proc_mouse,
		NULL
	},
};

int
pms_cmd(struct pms_softc *sc, u_char *cmd, int len, u_char *resp, int resplen)
{
	if (sc->poll) {
		return pckbc_poll_cmd(sc->sc_kbctag, PCKBC_AUX_SLOT,
		    cmd, len, resplen, resp, 1);
	} else {
		return pckbc_enqueue_cmd(sc->sc_kbctag, PCKBC_AUX_SLOT,
		    cmd, len, resplen, 1, resp);
	}
}

int
pms_spec_cmd(struct pms_softc *sc, int cmd)
{
	if (pms_set_scaling(sc, 1) ||
	    pms_set_resolution(sc, (cmd >> 6) & 0x03) ||
	    pms_set_resolution(sc, (cmd >> 4) & 0x03) ||
	    pms_set_resolution(sc, (cmd >> 2) & 0x03) ||
	    pms_set_resolution(sc, (cmd >> 0) & 0x03))
		return (-1);
	return (0);
}

int
pms_get_devid(struct pms_softc *sc, u_char *resp)
{
	u_char cmd[1];

	cmd[0] = PMS_SEND_DEV_ID;
	return (pms_cmd(sc, cmd, 1, resp, 1));
}

int
pms_get_status(struct pms_softc *sc, u_char *resp)
{
	u_char cmd[1];

	cmd[0] = PMS_SEND_DEV_STATUS;
	return (pms_cmd(sc, cmd, 1, resp, 3));
}

int
pms_set_rate(struct pms_softc *sc, int value)
{
	u_char cmd[2];

	cmd[0] = PMS_SET_SAMPLE;
	cmd[1] = value;
	return (pms_cmd(sc, cmd, 2, NULL, 0));
}

int
pms_set_resolution(struct pms_softc *sc, int value)
{
	u_char cmd[2];

	cmd[0] = PMS_SET_RES;
	cmd[1] = value;
	return (pms_cmd(sc, cmd, 2, NULL, 0));
}

int
pms_set_scaling(struct pms_softc *sc, int scale)
{
	u_char cmd[1];

	switch (scale) {
	case 1:
	default:
		cmd[0] = PMS_SET_SCALE11;
		break;
	case 2:
		cmd[0] = PMS_SET_SCALE21;
		break;
	}
	return (pms_cmd(sc, cmd, 1, NULL, 0));
}

int
pms_reset(struct pms_softc *sc)
{
	u_char cmd[1], resp[2];
	int res;

	cmd[0] = PMS_RESET;
	res = pms_cmd(sc, cmd, 1, resp, 2);
#ifdef DEBUG
	if (res || resp[0] != PMS_RSTDONE || resp[1] != 0)
		printf("%s: reset error %d (response 0x%02x, type 0x%02x)\n",
		    DEVNAME(sc), res, resp[0], resp[1]);
#endif
	return (res);
}

int
pms_dev_enable(struct pms_softc *sc)
{
	u_char cmd[1];
	int res;

	cmd[0] = PMS_DEV_ENABLE;
	res = pms_cmd(sc, cmd, 1, NULL, 0);
	if (res)
		printf("%s: enable error\n", DEVNAME(sc));
	return (res);
}

int
pms_dev_disable(struct pms_softc *sc)
{
	u_char cmd[1];
	int res;

	cmd[0] = PMS_DEV_DISABLE;
	res = pms_cmd(sc, cmd, 1, NULL, 0);
	if (res)
		printf("%s: disable error\n", DEVNAME(sc));
	return (res);
}

void
pms_protocol_lookup(struct pms_softc *sc)
{
	int i;

	sc->protocol = &pms_protocols[0];
	for (i = 1; i < nitems(pms_protocols); i++) {
		pms_reset(sc);
		if (pms_protocols[i].enable(sc)) {
			sc->protocol = &pms_protocols[i];
			break;
		}
	}

	DPRINTF("%s: protocol type %d\n", DEVNAME(sc), sc->protocol->type);
}

/*
 * Detect reset announcement ([0xaa, 0x0]).
 * The sequence will be sent as input on rare occasions when the touchpad was
 * reset due to a power failure.
 */
void
pms_reset_detect(struct pms_softc *sc, int data)
{
	switch (sc->sc_rststate) {
	case PMS_RST_COMMENCE:
		if (data == 0x0) {
			sc->sc_rststate = PMS_RST_ANNOUNCED;
			timeout_add_msec(&sc->sc_rsttimo, 100);
		} else if (data != PMS_RSTDONE) {
			sc->sc_rststate = 0;
		}
		break;
	default:
		if (data == PMS_RSTDONE)
			sc->sc_rststate = PMS_RST_COMMENCE;
		else
			sc->sc_rststate = 0;
	}
}

void
pms_reset_timo(void *v)
{
	struct pms_softc *sc = v;
	int s = spltty();

	/*
	 * Do nothing if the reset was a false positive or if the device already
	 * is disabled.
	 */
	if (sc->sc_rststate == PMS_RST_ANNOUNCED &&
	    sc->sc_state != PMS_STATE_DISABLED)
		task_add(systq, &sc->sc_rsttask);

	splx(s);
}

void
pms_reset_task(void *v)
{
	struct pms_softc *sc = v;
	int s = spltty();

#ifdef DIAGNOSTIC
	printf("%s: device reset (state = %d)\n", DEVNAME(sc), sc->sc_rststate);
#endif

	rw_enter_write(&sc->sc_state_lock);

	if (sc->sc_sec_wsmousedev != NULL)
		pms_change_state(sc, PMS_STATE_DISABLED, PMS_DEV_SECONDARY);
	pms_change_state(sc, PMS_STATE_DISABLED, PMS_DEV_PRIMARY);

	pms_change_state(sc, PMS_STATE_ENABLED, PMS_DEV_PRIMARY);
	if (sc->sc_sec_wsmousedev != NULL)
		pms_change_state(sc, PMS_STATE_ENABLED, PMS_DEV_SECONDARY);

	rw_exit_write(&sc->sc_state_lock);
	splx(s);
}

int
pms_enable_intelli(struct pms_softc *sc)
{
	u_char resp;

	/* the special sequence to enable the third button and the roller */
	if (pms_set_rate(sc, PMS_INTELLI_MAGIC1) ||
	    pms_set_rate(sc, PMS_INTELLI_MAGIC2) ||
	    pms_set_rate(sc, PMS_INTELLI_MAGIC3) ||
	    pms_get_devid(sc, &resp) ||
	    resp != PMS_INTELLI_ID)
		return (0);

	return (1);
}

int
pms_ioctl_mouse(struct pms_softc *sc, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	int i;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_PS2;
		break;
	case WSMOUSEIO_SRES:
		i = ((int) *(u_int *)data - 12) / 25;
		/* valid values are {0,1,2,3} */
		if (i < 0)
			i = 0;
		if (i > 3)
			i = 3;

		if (pms_set_resolution(sc, i))
			printf("%s: SET_RES command error\n", DEVNAME(sc));
		break;
	default:
		return (-1);
	}
	return (0);
}

int
pms_sync_mouse(struct pms_softc *sc, int data)
{
	if (sc->inputstate != 0)
		return (0);

	switch (sc->protocol->type) {
	case PMS_STANDARD:
		if ((data & 0xc0) != 0)
			return (-1);
		break;
	case PMS_INTELLI:
		if ((data & 0x08) != 0x08)
			return (-1);
		break;
	}

	return (0);
}

void
pms_proc_mouse(struct pms_softc *sc)
{
	u_int buttons;
	int  dx, dy, dz;

	buttons = butmap[sc->packet[0] & PMS_PS2_BUTTONSMASK];
	dx = (sc->packet[0] & PMS_PS2_XNEG) ?
	    (int)sc->packet[1] - 256 : sc->packet[1];
	dy = (sc->packet[0] & PMS_PS2_YNEG) ?
	    (int)sc->packet[2] - 256 : sc->packet[2];

	if (sc->protocol->type == PMS_INTELLI)
		dz = (signed char)sc->packet[3];
	else
		dz = 0;

	WSMOUSE_INPUT(sc->sc_wsmousedev, buttons, dx, dy, dz, 0);
}

int
pmsprobe(struct device *parent, void *match, void *aux)
{
	struct pckbc_attach_args *pa = aux;
	u_char cmd[1], resp[2];
	int res;

	if (pa->pa_slot != PCKBC_AUX_SLOT)
		return (0);

	/* Flush any garbage. */
	pckbc_flush(pa->pa_tag, pa->pa_slot);

	/* reset the device */
	cmd[0] = PMS_RESET;
	res = pckbc_poll_cmd(pa->pa_tag, pa->pa_slot, cmd, 1, 2, resp, 1);
	if (res || resp[0] != PMS_RSTDONE || resp[1] != 0) {
#ifdef DEBUG
		printf("pms: reset error %d (response 0x%02x, type 0x%02x)\n",
		    res, resp[0], resp[1]);
#endif
		return (0);
	}

	return (1);
}

void
pmsattach(struct device *parent, struct device *self, void *aux)
{
	struct pms_softc *sc = (void *)self;
	struct pckbc_attach_args *pa = aux;
	struct wsmousedev_attach_args a;

	sc->sc_kbctag = pa->pa_tag;

	pckbc_set_inputhandler(sc->sc_kbctag, PCKBC_AUX_SLOT,
	    pmsinput, sc, DEVNAME(sc));

	printf("\n");

	a.accessops = &pms_accessops;
	a.accesscookie = sc;

	rw_init(&sc->sc_state_lock, "pmsst");

	/*
	 * Attach the wsmouse, saving a handle to it.
	 * Note that we don't need to check this pointer against NULL
	 * here or in pmsintr, because if this fails pms_enable() will
	 * never be called, so pmsinput() will never be called.
	 */
	sc->sc_wsmousedev = config_found(self, &a, wsmousedevprint);

	task_set(&sc->sc_rsttask, pms_reset_task, sc);
	timeout_set(&sc->sc_rsttimo, pms_reset_timo, sc);

	sc->poll = 1;
	sc->sc_dev_enable = 0;

	/* See if the device understands an extended (touchpad) protocol. */
	pms_protocol_lookup(sc);

	/* no interrupts until enabled */
	pms_change_state(sc, PMS_STATE_DISABLED, PMS_DEV_IGNORE);
}

int
pmsactivate(struct device *self, int act)
{
	struct pms_softc *sc = (struct pms_softc *)self;
	int rv;

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		if (sc->sc_state == PMS_STATE_ENABLED)
			pms_change_state(sc, PMS_STATE_SUSPENDED,
			    PMS_DEV_IGNORE);
		break;
	case DVACT_WAKEUP:
		if (sc->sc_state == PMS_STATE_SUSPENDED)
			pms_change_state(sc, PMS_STATE_ENABLED,
			    PMS_DEV_IGNORE);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
pms_change_state(struct pms_softc *sc, int newstate, int dev)
{
	if (dev != PMS_DEV_IGNORE) {
		switch (newstate) {
		case PMS_STATE_ENABLED:
			if (sc->sc_dev_enable & dev)
				return (EBUSY);

			sc->sc_dev_enable |= dev;

			if (sc->sc_state == PMS_STATE_ENABLED)
				return (0);

			break;
		case PMS_STATE_DISABLED:
			sc->sc_dev_enable &= ~dev;

			if (sc->sc_dev_enable)
				return (0);

			break;
		}
	}

	switch (newstate) {
	case PMS_STATE_ENABLED:
		sc->inputstate = 0;
		sc->sc_rststate = 0;

		pckbc_slot_enable(sc->sc_kbctag, PCKBC_AUX_SLOT, 1);

		if (sc->poll)
			pckbc_flush(sc->sc_kbctag, PCKBC_AUX_SLOT);

		pms_reset(sc);
		if (sc->protocol->enable != NULL &&
		    sc->protocol->enable(sc) == 0)
			pms_protocol_lookup(sc);

		pms_dev_enable(sc);
		break;
	case PMS_STATE_DISABLED:
	case PMS_STATE_SUSPENDED:
		pms_dev_disable(sc);

		if (sc->protocol->disable)
			sc->protocol->disable(sc);

		pckbc_slot_enable(sc->sc_kbctag, PCKBC_AUX_SLOT, 0);
		break;
	}

	sc->sc_state = newstate;
	sc->poll = (newstate == PMS_STATE_SUSPENDED) ? 1 : 0;

	return (0);
}

int
pms_enable(void *v)
{
	struct pms_softc *sc = v;
	int rv;

	rw_enter_write(&sc->sc_state_lock);
	rv = pms_change_state(sc, PMS_STATE_ENABLED, PMS_DEV_PRIMARY);
	rw_exit_write(&sc->sc_state_lock);

	return (rv);
}

void
pms_disable(void *v)
{
	struct pms_softc *sc = v;

	rw_enter_write(&sc->sc_state_lock);
	pms_change_state(sc, PMS_STATE_DISABLED, PMS_DEV_PRIMARY);
	rw_exit_write(&sc->sc_state_lock);
}

int
pms_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct pms_softc *sc = v;

	if (sc->protocol->ioctl)
		return (sc->protocol->ioctl(sc, cmd, data, flag, p));
	else
		return (-1);
}

int
pms_sec_enable(void *v)
{
	struct pms_softc *sc = v;
	int rv;

	rw_enter_write(&sc->sc_state_lock);
	rv = pms_change_state(sc, PMS_STATE_ENABLED, PMS_DEV_SECONDARY);
	rw_exit_write(&sc->sc_state_lock);

	return (rv);
}

void
pms_sec_disable(void *v)
{
	struct pms_softc *sc = v;

	rw_enter_write(&sc->sc_state_lock);
	pms_change_state(sc, PMS_STATE_DISABLED, PMS_DEV_SECONDARY);
	rw_exit_write(&sc->sc_state_lock);
}

int
pms_sec_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_PS2;
		break;
	default:
		return (-1);
	}
	return (0);
}

#ifdef DIAGNOSTIC
static inline void
pms_print_packet(struct pms_softc *sc)
{
	int i, state, size;

	state = sc->inputstate;
	size = sc->protocol->packetsize;
	for (i = 0; i < size; i++)
		printf(i == state ? " %02x |" : " %02x", sc->packet[i]);
}
#endif

void
pmsinput(void *vsc, int data)
{
	struct pms_softc *sc = vsc;

	if (sc->sc_state != PMS_STATE_ENABLED) {
		/* Interrupts are not expected.  Discard the byte. */
		return;
	}

	sc->packet[sc->inputstate] = data;
	pms_reset_detect(sc, data);
	if (sc->protocol->sync(sc, data)) {
#ifdef DIAGNOSTIC
		printf("%s: not in sync yet, discard input "
		    "(state = %d,",
		    DEVNAME(sc), sc->inputstate);
		pms_print_packet(sc);
		printf(")\n");
#endif

		sc->inputstate = 0;
		return;
	}

	sc->inputstate++;

	if (sc->inputstate != sc->protocol->packetsize)
		return;

	sc->inputstate = 0;
	sc->protocol->proc(sc);
}

int
synaptics_set_mode(struct pms_softc *sc, int mode, int rate)
{
	struct synaptics_softc *syn = sc->synaptics;

	if (pms_spec_cmd(sc, mode) ||
	    pms_set_rate(sc, rate == 0 ? SYNAPTICS_CMD_SET_MODE : rate))
		return (-1);

	/*
	 * Make sure that the set mode command has finished.
	 * Otherwise enabling the device before that will make it fail.
	 */
	delay(10000);

	if (rate == 0)
		syn->mode = mode;

	return (0);
}

int
synaptics_query(struct pms_softc *sc, int query, int *val)
{
	u_char resp[3];

	if (pms_spec_cmd(sc, query) ||
	    pms_get_status(sc, resp))
		return (-1);

	if (val)
		*val = (resp[0] << 16) | (resp[1] << 8) | resp[2];

	return (0);
}

int
synaptics_get_hwinfo(struct pms_softc *sc)
{
	struct synaptics_softc *syn = sc->synaptics;
	struct wsmousehw *hw;
	int resolution = 0, max_coords = 0, min_coords = 0;

	hw = wsmouse_get_hw(sc->sc_wsmousedev);

	if (synaptics_query(sc, SYNAPTICS_QUE_IDENTIFY, &syn->identify))
		return (-1);
	if (synaptics_query(sc, SYNAPTICS_QUE_CAPABILITIES,
	    &syn->capabilities))
		return (-1);
	if (synaptics_query(sc, SYNAPTICS_QUE_MODEL, &syn->model))
		return (-1);
	if ((SYNAPTICS_CAP_EXTENDED_QUERIES(syn->capabilities) >= 1) &&
	    synaptics_query(sc, SYNAPTICS_QUE_EXT_MODEL, &syn->ext_model))
		return (-1);
	if ((SYNAPTICS_CAP_EXTENDED_QUERIES(syn->capabilities) >= 4) &&
	    synaptics_query(sc, SYNAPTICS_QUE_EXT_CAPABILITIES,
		&syn->ext_capabilities))
		return (-1);
	if ((SYNAPTICS_ID_MAJOR(syn->identify) >= 4) &&
	    synaptics_query(sc, SYNAPTICS_QUE_RESOLUTION, &resolution))
		return (-1);
	if ((SYNAPTICS_CAP_EXTENDED_QUERIES(syn->capabilities) >= 5) &&
	    (syn->ext_capabilities & SYNAPTICS_EXT_CAP_MAX_COORDS) &&
	    synaptics_query(sc, SYNAPTICS_QUE_EXT_MAX_COORDS, &max_coords))
		return (-1);
	if ((SYNAPTICS_CAP_EXTENDED_QUERIES(syn->capabilities) >= 7 ||
	    SYNAPTICS_ID_FULL(syn->identify) == 0x801) &&
	    (syn->ext_capabilities & SYNAPTICS_EXT_CAP_MIN_COORDS) &&
	    synaptics_query(sc, SYNAPTICS_QUE_EXT_MIN_COORDS, &min_coords))
		return (-1);

	if (SYNAPTICS_ID_FULL(syn->identify) >= 0x705) {
		if (synaptics_query(sc, SYNAPTICS_QUE_MODES, &syn->modes))
			return (-1);
		if ((syn->modes & SYNAPTICS_EXT2_CAP) &&
		    synaptics_query(sc, SYNAPTICS_QUE_EXT2_CAPABILITIES,
		    &syn->ext2_capabilities))
			return (-1);
	}

	if ((syn->ext_capabilities & SYNAPTICS_EXT_CAP_CLICKPAD) &&
	    !(syn->ext2_capabilities & SYNAPTICS_EXT2_CAP_BUTTONS_STICK)
	    && mouse_has_softbtn)
		hw->type = WSMOUSE_TYPE_SYNAP_SBTN;
	else
		hw->type = WSMOUSE_TYPE_SYNAPTICS;

	hw->hw_type = (syn->ext_capabilities & SYNAPTICS_EXT_CAP_CLICKPAD)
	    ? WSMOUSEHW_CLICKPAD : WSMOUSEHW_TOUCHPAD;

	if (resolution & SYNAPTICS_RESOLUTION_VALID) {
		hw->h_res = SYNAPTICS_RESOLUTION_X(resolution);
		hw->v_res = SYNAPTICS_RESOLUTION_Y(resolution);
	}

	hw->x_min = (min_coords ?
	    SYNAPTICS_X_LIMIT(min_coords) : SYNAPTICS_XMIN_BEZEL);
	hw->y_min = (min_coords ?
	    SYNAPTICS_Y_LIMIT(min_coords) : SYNAPTICS_YMIN_BEZEL);
	hw->x_max = (max_coords ?
	    SYNAPTICS_X_LIMIT(max_coords) : SYNAPTICS_XMAX_BEZEL);
	hw->y_max = (max_coords ?
	    SYNAPTICS_Y_LIMIT(max_coords) : SYNAPTICS_YMAX_BEZEL);

	if ((syn->capabilities & SYNAPTICS_CAP_MULTIFINGER) ||
	    SYNAPTICS_SUPPORTS_AGM(syn->ext_capabilities))
		hw->contacts_max = SYNAPTICS_MAX_FINGERS;
	else
		hw->contacts_max = 1;

	syn->sec_buttons = 0;

	if (SYNAPTICS_EXT_MODEL_BUTTONS(syn->ext_model) > 8)
		syn->ext_model &= ~0xf000;

	if ((syn->model & SYNAPTICS_MODEL_NEWABS) == 0) {
		printf("%s: don't support Synaptics OLDABS\n", DEVNAME(sc));
		return (-1);
	}

	if ((SYNAPTICS_ID_MAJOR(syn->identify) == 5) &&
	    (SYNAPTICS_ID_MINOR(syn->identify) == 9))
		syn->mask = SYNAPTICS_MASK_NEWABS_RELAXED;
	else
		syn->mask = SYNAPTICS_MASK_NEWABS_STRICT;

	return (0);
}

void
synaptics_sec_proc(struct pms_softc *sc)
{
	struct synaptics_softc *syn = sc->synaptics;
	u_int buttons;
	int dx, dy;

	if ((sc->sc_dev_enable & PMS_DEV_SECONDARY) == 0)
		return;

	buttons = butmap[sc->packet[1] & PMS_PS2_BUTTONSMASK];
	buttons |= syn->sec_buttons;
	dx = (sc->packet[1] & PMS_PS2_XNEG) ?
	    (int)sc->packet[4] - 256 : sc->packet[4];
	dy = (sc->packet[1] & PMS_PS2_YNEG) ?
	    (int)sc->packet[5] - 256 : sc->packet[5];

	WSMOUSE_INPUT(sc->sc_sec_wsmousedev, buttons, dx, dy, 0, 0);
}

int
synaptics_knock(struct pms_softc *sc)
{
	u_char resp[3];

	if (pms_set_resolution(sc, 0) ||
	    pms_set_resolution(sc, 0) ||
	    pms_set_resolution(sc, 0) ||
	    pms_set_resolution(sc, 0) ||
	    pms_get_status(sc, resp) ||
	    resp[1] != SYNAPTICS_ID_MAGIC)
		return (-1);

	return (0);
}

int
pms_enable_synaptics(struct pms_softc *sc)
{
	struct synaptics_softc *syn = sc->synaptics;
	struct wsmousedev_attach_args a;
	int mode, i;

	if (synaptics_knock(sc)) {
		if (sc->synaptics == NULL)
			goto err;
		/*
		 * Some synaptics touchpads don't resume quickly.
		 * Retry a few times.
		 */
		for (i = 10; i > 0; --i) {
			printf("%s: device not resuming, retrying\n",
			    DEVNAME(sc));
			pms_reset(sc);
			if (synaptics_knock(sc) == 0)
				break;
			delay(100000);
		}
		if (i == 0) {
			printf("%s: lost device\n", DEVNAME(sc));
			goto err;
		}
	}

	if (sc->synaptics == NULL) {
		sc->synaptics = syn = malloc(sizeof(struct synaptics_softc),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		if (syn == NULL) {
			printf("%s: synaptics: not enough memory\n",
			    DEVNAME(sc));
			goto err;
		}

		if (synaptics_get_hwinfo(sc)) {
			free(sc->synaptics, M_DEVBUF,
			    sizeof(struct synaptics_softc));
			sc->synaptics = NULL;
			goto err;
		}

		/* enable pass-through PS/2 port if supported */
		if (syn->capabilities & SYNAPTICS_CAP_PASSTHROUGH) {
			a.accessops = &pms_sec_accessops;
			a.accesscookie = sc;
			sc->sc_sec_wsmousedev = config_found((void *)sc, &a,
			    wsmousedevprint);
		}

		if (wsmouse_configure(sc->sc_wsmousedev, synaptics_params,
		    nitems(synaptics_params)))
			goto err;

		printf("%s: Synaptics %s, firmware %d.%d, "
		    "0x%x 0x%x 0x%x 0x%x 0x%x\n",
		    DEVNAME(sc),
		    (syn->ext_capabilities & SYNAPTICS_EXT_CAP_CLICKPAD ?
			"clickpad" : "touchpad"),
		    SYNAPTICS_ID_MAJOR(syn->identify),
		    SYNAPTICS_ID_MINOR(syn->identify),
		    syn->model, syn->ext_model, syn->modes,
		    syn->capabilities, syn->ext_capabilities);
	}

	/*
	 * Enable absolute mode, plain W-mode and "advanced gesture mode"
	 * (AGM), if possible.  AGM, which seems to be a prerequisite for the
	 * extended W-mode, might not always be necessary here, but at least
	 * some older Synaptics models do not report finger counts without it.
	 */
	mode = SYNAPTICS_ABSOLUTE_MODE | SYNAPTICS_HIGH_RATE;
	if (syn->capabilities & SYNAPTICS_CAP_EXTENDED)
		mode |= SYNAPTICS_W_MODE;
	else if (SYNAPTICS_ID_MAJOR(syn->identify) >= 4)
		mode |= SYNAPTICS_DISABLE_GESTURE;
	if (synaptics_set_mode(sc, mode, 0))
		goto err;

	if (SYNAPTICS_SUPPORTS_AGM(syn->ext_capabilities) &&
	    synaptics_set_mode(sc, SYNAPTICS_QUE_MODEL,
	        SYNAPTICS_CMD_SET_ADV_GESTURE_MODE))
		goto err;

	return (1);

err:
	pms_reset(sc);

	return (0);
}

int
pms_ioctl_synaptics(struct pms_softc *sc, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;
	struct wsmousehw *hw;
	int wsmode;

	hw = wsmouse_get_hw(sc->sc_wsmousedev);
	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = hw->type;
		break;
	case WSMOUSEIO_GCALIBCOORDS:
		wsmc->minx = hw->x_min;
		wsmc->maxx = hw->x_max;
		wsmc->miny = hw->y_min;
		wsmc->maxy = hw->y_max;
		wsmc->swapxy = 0;
		wsmc->resx = hw->h_res;
		wsmc->resy = hw->v_res;
		break;
	case WSMOUSEIO_SETMODE:
		wsmode = *(u_int *)data;
		if (wsmode != WSMOUSE_COMPAT && wsmode != WSMOUSE_NATIVE)
			return (EINVAL);
		wsmouse_set_mode(sc->sc_wsmousedev, wsmode);
		break;
	default:
		return (-1);
	}
	return (0);
}

int
pms_sync_synaptics(struct pms_softc *sc, int data)
{
	struct synaptics_softc *syn = sc->synaptics;

	switch (sc->inputstate) {
	case 0:
		if ((data & syn->mask) != SYNAPTICS_VALID_NEWABS_FIRST)
			return (-1);
		break;
	case 3:
		if ((data & syn->mask) != SYNAPTICS_VALID_NEWABS_NEXT)
			return (-1);
		break;
	}

	return (0);
}

void
pms_proc_synaptics(struct pms_softc *sc)
{
	struct synaptics_softc *syn = sc->synaptics;
	u_int buttons;
	int x, y, z, w, fingerwidth;

	w = ((sc->packet[0] & 0x30) >> 2) | ((sc->packet[0] & 0x04) >> 1) |
	    ((sc->packet[3] & 0x04) >> 2);
	z = sc->packet[2];

	if ((syn->capabilities & SYNAPTICS_CAP_EXTENDED) == 0) {
		/*
		 * Emulate W mode for models that don't provide it. Bit 3
		 * of the w-input signals a touch ("finger"), Bit 2 and
		 * the "gesture" bits 1-0 can be ignored.
		 */
		if (w & 8)
			w = 4;
		else
			z = w = 0;
	}


	if (w == 3) {
		if (syn->capabilities & SYNAPTICS_CAP_PASSTHROUGH)
			synaptics_sec_proc(sc);
		return;
	}

	if ((sc->sc_dev_enable & PMS_DEV_PRIMARY) == 0)
		return;

	if (w == 2)
		return;	/* EW-mode packets are not expected here. */

	x = ((sc->packet[3] & 0x10) << 8) | ((sc->packet[1] & 0x0f) << 8) |
	    sc->packet[4];
	y = ((sc->packet[3] & 0x20) << 7) | ((sc->packet[1] & 0xf0) << 4) |
	    sc->packet[5];

	buttons = ((sc->packet[0] & sc->packet[3]) & 0x01) ?
	    WSMOUSE_BUTTON(1) : 0;
	buttons |= ((sc->packet[0] & sc->packet[3]) & 0x02) ?
	    WSMOUSE_BUTTON(3) : 0;

	if (syn->ext_capabilities & SYNAPTICS_EXT_CAP_CLICKPAD) {
		buttons |= ((sc->packet[0] ^ sc->packet[3]) & 0x01) ?
		    WSMOUSE_BUTTON(1) : 0;
	} else if (syn->capabilities & SYNAPTICS_CAP_MIDDLE_BUTTON) {
		buttons |= ((sc->packet[0] ^ sc->packet[3]) & 0x01) ?
		    WSMOUSE_BUTTON(2) : 0;
	}

	if (syn->capabilities & SYNAPTICS_CAP_FOUR_BUTTON) {
		buttons |= ((sc->packet[0] ^ sc->packet[3]) & 0x01) ?
		    WSMOUSE_BUTTON(4) : 0;
		buttons |= ((sc->packet[0] ^ sc->packet[3]) & 0x02) ?
		    WSMOUSE_BUTTON(5) : 0;
	} else if (SYNAPTICS_EXT_MODEL_BUTTONS(syn->ext_model) &&
	    ((sc->packet[0] ^ sc->packet[3]) & 0x02)) {
		if (syn->ext2_capabilities & SYNAPTICS_EXT2_CAP_BUTTONS_STICK) {
			/*
			 * Trackstick buttons on this machine are wired to the
			 * trackpad as extra buttons, so route the event
			 * through the trackstick interface as normal buttons
			 */
			syn->sec_buttons =
			    (sc->packet[4] & 0x01) ? WSMOUSE_BUTTON(1) : 0;
			syn->sec_buttons |=
			    (sc->packet[5] & 0x01) ? WSMOUSE_BUTTON(3) : 0;
			syn->sec_buttons |=
			    (sc->packet[4] & 0x02) ? WSMOUSE_BUTTON(2) : 0;
			wsmouse_buttons(
			    sc->sc_sec_wsmousedev, syn->sec_buttons);
			wsmouse_input_sync(sc->sc_sec_wsmousedev);
			return;
		}

		buttons |= (sc->packet[4] & 0x01) ? WSMOUSE_BUTTON(6) : 0;
		buttons |= (sc->packet[5] & 0x01) ? WSMOUSE_BUTTON(7) : 0;
		buttons |= (sc->packet[4] & 0x02) ? WSMOUSE_BUTTON(8) : 0;
		buttons |= (sc->packet[5] & 0x02) ? WSMOUSE_BUTTON(9) : 0;
		buttons |= (sc->packet[4] & 0x04) ? WSMOUSE_BUTTON(10) : 0;
		buttons |= (sc->packet[5] & 0x04) ? WSMOUSE_BUTTON(11) : 0;
		buttons |= (sc->packet[4] & 0x08) ? WSMOUSE_BUTTON(12) : 0;
		buttons |= (sc->packet[5] & 0x08) ? WSMOUSE_BUTTON(13) : 0;
		x &= ~0x0f;
		y &= ~0x0f;
	}

	if (z) {
		fingerwidth = max(w, 4);
		w = (w < 2 ? w + 2 : 1);
	} else {
		fingerwidth = 0;
		w = 0;
	}
	wsmouse_set(sc->sc_wsmousedev, WSMOUSE_TOUCH_WIDTH, fingerwidth, 0);
	WSMOUSE_TOUCH(sc->sc_wsmousedev, buttons, x, y, z, w);
}

void
pms_disable_synaptics(struct pms_softc *sc)
{
	struct synaptics_softc *syn = sc->synaptics;

	if (syn->capabilities & SYNAPTICS_CAP_SLEEP)
		synaptics_set_mode(sc, SYNAPTICS_SLEEP_MODE |
		    SYNAPTICS_DISABLE_GESTURE, 0);
}

int
alps_sec_proc(struct pms_softc *sc)
{
	struct alps_softc *alps = sc->alps;
	int dx, dy, pos = 0;

	if ((sc->packet[0] & PMS_ALPS_PS2_MASK) == PMS_ALPS_PS2_VALID) {
		/*
		 * We need to keep buttons states because interleaved
		 * packets only signalize x/y movements.
		 */
		alps->sec_buttons = butmap[sc->packet[0] & PMS_PS2_BUTTONSMASK];
	} else if ((sc->packet[3] & PMS_ALPS_INTERLEAVED_MASK) ==
	    PMS_ALPS_INTERLEAVED_VALID) {
		sc->inputstate = 3;
		pos = 3;
	} else {
		return (0);
	}

	if ((sc->sc_dev_enable & PMS_DEV_SECONDARY) == 0)
		return (1);

	dx = (sc->packet[pos] & PMS_PS2_XNEG) ?
	    (int)sc->packet[pos + 1] - 256 : sc->packet[pos + 1];
	dy = (sc->packet[pos] & PMS_PS2_YNEG) ?
	    (int)sc->packet[pos + 2] - 256 : sc->packet[pos + 2];

	WSMOUSE_INPUT(sc->sc_sec_wsmousedev, alps->sec_buttons, dx, dy, 0, 0);

	return (1);
}

int
alps_get_hwinfo(struct pms_softc *sc)
{
	struct alps_softc *alps = sc->alps;
	u_char resp[3];
	int i;
	struct wsmousehw *hw;

	if (pms_set_resolution(sc, 0) ||
	    pms_set_scaling(sc, 2) ||
	    pms_set_scaling(sc, 2) ||
	    pms_set_scaling(sc, 2) ||
	    pms_get_status(sc, resp)) {
		DPRINTF("%s: alps: model query error\n", DEVNAME(sc));
		return (-1);
	}

	alps->version = (resp[0] << 8) | (resp[1] << 4) | (resp[2] / 20 + 1);

	for (i = 0; i < nitems(alps_models); i++)
		if (alps->version == alps_models[i].version) {
			alps->model = alps_models[i].model;
			alps->mask = alps_models[i].mask;

			hw = wsmouse_get_hw(sc->sc_wsmousedev);
			hw->type = WSMOUSE_TYPE_ALPS;
			hw->hw_type = WSMOUSEHW_TOUCHPAD;
			hw->x_min = ALPS_XMIN_BEZEL;
			hw->y_min = ALPS_YMIN_BEZEL;
			hw->x_max = ALPS_XMAX_BEZEL;
			hw->y_max = ALPS_YMAX_BEZEL;
			hw->contacts_max = 1;

			return (0);
		}

	return (-1);
}

int
pms_enable_alps(struct pms_softc *sc)
{
	struct alps_softc *alps = sc->alps;
	struct wsmousedev_attach_args a;
	u_char resp[3];

	if (pms_set_resolution(sc, 0) ||
	    pms_set_scaling(sc, 1) ||
	    pms_set_scaling(sc, 1) ||
	    pms_set_scaling(sc, 1) ||
	    pms_get_status(sc, resp) ||
	    resp[0] != PMS_ALPS_MAGIC1 ||
	    resp[1] != PMS_ALPS_MAGIC2 ||
	    (resp[2] != PMS_ALPS_MAGIC3_1 && resp[2] != PMS_ALPS_MAGIC3_2 &&
	    resp[2] != PMS_ALPS_MAGIC3_3))
		goto err;

	if (sc->alps == NULL) {
		sc->alps = alps = malloc(sizeof(struct alps_softc),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		if (alps == NULL) {
			printf("%s: alps: not enough memory\n", DEVNAME(sc));
			goto err;
		}

		if (alps_get_hwinfo(sc)) {
			free(sc->alps, M_DEVBUF, sizeof(struct alps_softc));
			sc->alps = NULL;
			goto err;
		}

		if (wsmouse_configure(sc->sc_wsmousedev, alps_params,
		    nitems(alps_params))) {
			free(sc->alps, M_DEVBUF, sizeof(struct alps_softc));
			sc->alps = NULL;
			printf("%s: setup failed\n", DEVNAME(sc));
			goto err;
		}

		printf("%s: ALPS %s, version 0x%04x\n", DEVNAME(sc),
		    (alps->model & ALPS_DUALPOINT ? "Dualpoint" : "Glidepoint"),
		    alps->version);


		if (alps->model & ALPS_DUALPOINT) {
			a.accessops = &pms_sec_accessops;
			a.accesscookie = sc;
			sc->sc_sec_wsmousedev = config_found((void *)sc, &a,
			    wsmousedevprint);
		}
	}

	if (alps->model == 0)
		goto err;

	if ((alps->model & ALPS_PASSTHROUGH) &&
	   (pms_set_scaling(sc, 2) ||
	    pms_set_scaling(sc, 2) ||
	    pms_set_scaling(sc, 2) ||
	    pms_dev_disable(sc))) {
		DPRINTF("%s: alps: passthrough on error\n", DEVNAME(sc));
		goto err;
	}

	if (pms_dev_disable(sc) ||
	    pms_dev_disable(sc) ||
	    pms_set_rate(sc, 0x0a)) {
		DPRINTF("%s: alps: tapping error\n", DEVNAME(sc));
		goto err;
	}

	if (pms_dev_disable(sc) ||
	    pms_dev_disable(sc) ||
	    pms_dev_disable(sc) ||
	    pms_dev_disable(sc) ||
	    pms_dev_enable(sc)) {
		DPRINTF("%s: alps: absolute mode error\n", DEVNAME(sc));
		goto err;
	}

	if ((alps->model & ALPS_PASSTHROUGH) &&
	   (pms_set_scaling(sc, 1) ||
	    pms_set_scaling(sc, 1) ||
	    pms_set_scaling(sc, 1) ||
	    pms_dev_disable(sc))) {
		DPRINTF("%s: alps: passthrough off error\n", DEVNAME(sc));
		goto err;
	}

	alps->sec_buttons = 0;

	return (1);

err:
	pms_reset(sc);

	return (0);
}

int
pms_ioctl_alps(struct pms_softc *sc, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;
	int wsmode;
	struct wsmousehw *hw;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_ALPS;
		break;
	case WSMOUSEIO_GCALIBCOORDS:
		hw = wsmouse_get_hw(sc->sc_wsmousedev);
		wsmc->minx = hw->x_min;
		wsmc->maxx = hw->x_max;
		wsmc->miny = hw->y_min;
		wsmc->maxy = hw->y_max;
		wsmc->swapxy = 0;
		break;
	case WSMOUSEIO_SETMODE:
		wsmode = *(u_int *)data;
		if (wsmode != WSMOUSE_COMPAT && wsmode != WSMOUSE_NATIVE)
			return (EINVAL);
		wsmouse_set_mode(sc->sc_wsmousedev, wsmode);
		break;
	default:
		return (-1);
	}
	return (0);
}

int
pms_sync_alps(struct pms_softc *sc, int data)
{
	struct alps_softc *alps = sc->alps;

	if ((alps->model & ALPS_DUALPOINT) &&
	    (sc->packet[0] & PMS_ALPS_PS2_MASK) == PMS_ALPS_PS2_VALID) {
		if (sc->inputstate == 2)
			sc->inputstate += 3;
		return (0);
	}

	switch (sc->inputstate) {
	case 0:
		if ((data & alps->mask) != alps->mask)
			return (-1);
		break;
	case 1:
	case 2:
	case 3:
		if ((data & PMS_ALPS_MASK) != PMS_ALPS_VALID)
			return (-1);
		break;
	case 4:
	case 5:
		if ((alps->model & ALPS_INTERLEAVED) == 0 &&
		    (data & PMS_ALPS_MASK) != PMS_ALPS_VALID)
			return (-1);
		break;
	}

	return (0);
}

void
pms_proc_alps(struct pms_softc *sc)
{
	struct alps_softc *alps = sc->alps;
	int x, y, z, dx, dy;
	u_int buttons, gesture;

	if ((alps->model & ALPS_DUALPOINT) && alps_sec_proc(sc))
		return;

	x = sc->packet[1] | ((sc->packet[2] & 0x78) << 4);
	y = sc->packet[4] | ((sc->packet[3] & 0x70) << 3);
	z = sc->packet[5];

	buttons = ((sc->packet[3] & 1) ? WSMOUSE_BUTTON(1) : 0) |
	    ((sc->packet[3] & 2) ? WSMOUSE_BUTTON(3) : 0) |
	    ((sc->packet[3] & 4) ? WSMOUSE_BUTTON(2) : 0);

	if ((sc->sc_dev_enable & PMS_DEV_SECONDARY) && z == ALPS_Z_MAGIC) {
		dx = (x > ALPS_XSEC_BEZEL / 2) ? (x - ALPS_XSEC_BEZEL) : x;
		dy = (y > ALPS_YSEC_BEZEL / 2) ? (y - ALPS_YSEC_BEZEL) : y;

		WSMOUSE_INPUT(sc->sc_sec_wsmousedev, buttons, dx, dy, 0, 0);

		return;
	}

	if ((sc->sc_dev_enable & PMS_DEV_PRIMARY) == 0)
		return;

	/*
	 * XXX The Y-axis is in the opposite direction compared to
	 * Synaptics touchpads and PS/2 mouses.
	 * It's why we need to translate the y value here for both
	 * NATIVE and COMPAT modes.
	 */
	y = ALPS_YMAX_BEZEL - y + ALPS_YMIN_BEZEL;

	if (alps->gesture == ALPS_TAP) {
		/* Report a touch with the tap coordinates. */
		WSMOUSE_TOUCH(sc->sc_wsmousedev, buttons,
		    alps->old_x, alps->old_y, ALPS_PRESSURE, 0);
		if (z > 0) {
			/*
			 * The hardware doesn't send a null pressure
			 * event when dragging starts.
			 */
			WSMOUSE_TOUCH(sc->sc_wsmousedev, buttons,
			    alps->old_x, alps->old_y, 0, 0);
		}
	}

	gesture = sc->packet[2] & 0x03;
	if (gesture != ALPS_TAP)
		WSMOUSE_TOUCH(sc->sc_wsmousedev, buttons, x, y, z, 0);

	if (alps->gesture != ALPS_DRAG || gesture != ALPS_TAP)
		alps->gesture = gesture;

	alps->old_x = x;
	alps->old_y = y;
}

int
elantech_set_absolute_mode_v1(struct pms_softc *sc)
{
	int i;
	u_char resp[3];

	/* Enable absolute mode. Magic numbers from Linux driver. */
	if (pms_spec_cmd(sc, ELANTECH_CMD_WRITE_REG) ||
	    pms_spec_cmd(sc, 0x10) ||
	    pms_spec_cmd(sc, 0x16) ||
	    pms_set_scaling(sc, 1) ||
	    pms_spec_cmd(sc, ELANTECH_CMD_WRITE_REG) ||
	    pms_spec_cmd(sc, 0x11) ||
	    pms_spec_cmd(sc, 0x8f) ||
	    pms_set_scaling(sc, 1))
		return (-1);

	/* Read back reg 0x10 to ensure hardware is ready. */
	for (i = 0; i < 5; i++) {
		if (pms_spec_cmd(sc, ELANTECH_CMD_READ_REG) ||
		    pms_spec_cmd(sc, 0x10) ||
		    pms_get_status(sc, resp) == 0)
			break;
		delay(2000);
	}
	if (i == 5)
		return (-1);

	if ((resp[0] & ELANTECH_ABSOLUTE_MODE) == 0)
		return (-1);

	return (0);
}

int
elantech_set_absolute_mode_v2(struct pms_softc *sc)
{
	int i;
	u_char resp[3];
	u_char reg10 = (sc->elantech->fw_version == 0x20030 ? 0x54 : 0xc4);

	/* Enable absolute mode. Magic numbers from Linux driver. */
	if (elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, ELANTECH_CMD_WRITE_REG) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, 0x10) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, reg10) ||
	    pms_set_scaling(sc, 1) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, ELANTECH_CMD_WRITE_REG) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, 0x11) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, 0x88) ||
	    pms_set_scaling(sc, 1))
		return (-1);

	/* Read back reg 0x10 to ensure hardware is ready. */
	for (i = 0; i < 5; i++) {
		if (elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_cmd(sc, ELANTECH_CMD_READ_REG) ||
		    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_cmd(sc, 0x10) ||
		    pms_get_status(sc, resp) == 0)
			break;
		delay(2000);
	}
	if (i == 5)
		return (-1);

	return (0);
}

int
elantech_set_absolute_mode_v3(struct pms_softc *sc)
{
	int i;
	u_char resp[3];

	/* Enable absolute mode. Magic numbers from Linux driver. */
	if (elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, ELANTECH_CMD_READ_WRITE_REG) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, 0x10) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, 0x0b) ||
	    pms_set_scaling(sc, 1))
		return (-1);

	/* Read back reg 0x10 to ensure hardware is ready. */
	for (i = 0; i < 5; i++) {
		if (elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_cmd(sc, ELANTECH_CMD_READ_WRITE_REG) ||
		    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_cmd(sc, 0x10) ||
		    pms_get_status(sc, resp) == 0)
			break;
		delay(2000);
	}
	if (i == 5)
		return (-1);

	return (0);
}

int
elantech_set_absolute_mode_v4(struct pms_softc *sc)
{
	/* Enable absolute mode. Magic numbers from Linux driver. */
	if (elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, ELANTECH_CMD_READ_WRITE_REG) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, 0x07) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, ELANTECH_CMD_READ_WRITE_REG) ||
	    elantech_ps2_cmd(sc, ELANTECH_PS2_CUSTOM_COMMAND) ||
	    elantech_ps2_cmd(sc, 0x01) ||
	    pms_set_scaling(sc, 1))
		return (-1);

	/* v4 has no register 0x10 to read response from */

	return (0);
}

int
elantech_get_hwinfo_v1(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	struct wsmousehw *hw;
	int fw_version;
	u_char capabilities[3];

	if (synaptics_query(sc, ELANTECH_QUE_FW_VER, &fw_version))
		return (-1);

	if (fw_version < 0x20030 || fw_version == 0x20600) {
		if (fw_version < 0x20000)
			elantech->flags |= ELANTECH_F_HW_V1_OLD;
	} else
		return (-1);

	elantech->fw_version = fw_version;

	if (pms_spec_cmd(sc, ELANTECH_QUE_CAPABILITIES) ||
	    pms_get_status(sc, capabilities))
		return (-1);

	if (capabilities[0] & ELANTECH_CAP_HAS_ROCKER)
		elantech->flags |= ELANTECH_F_HAS_ROCKER;

	if (elantech_set_absolute_mode_v1(sc))
		return (-1);

	hw = wsmouse_get_hw(sc->sc_wsmousedev);
	hw->type = WSMOUSE_TYPE_ELANTECH;
	hw->hw_type = WSMOUSEHW_TOUCHPAD;
	hw->x_min = ELANTECH_V1_X_MIN;
	hw->x_max = ELANTECH_V1_X_MAX;
	hw->y_min = ELANTECH_V1_Y_MIN;
	hw->y_max = ELANTECH_V1_Y_MAX;

	return (0);
}

int
elantech_get_hwinfo_v2(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	struct wsmousehw *hw;
	int fw_version, ic_ver;
	u_char capabilities[3];
	int i, fixed_dpi;
	u_char resp[3];

	if (synaptics_query(sc, ELANTECH_QUE_FW_VER, &fw_version))
		return (-1);

	ic_ver = (fw_version & 0x0f0000) >> 16;
	if (ic_ver != 2 && ic_ver != 4)
		return (-1);

	elantech->fw_version = fw_version;
	if (fw_version >= 0x20800)
		elantech->flags |= ELANTECH_F_REPORTS_PRESSURE;

	if (pms_spec_cmd(sc, ELANTECH_QUE_CAPABILITIES) ||
	    pms_get_status(sc, capabilities))
		return (-1);

	if (elantech_set_absolute_mode_v2(sc))
		return (-1);

	hw = wsmouse_get_hw(sc->sc_wsmousedev);
	hw->type = WSMOUSE_TYPE_ELANTECH;
	hw->hw_type = WSMOUSEHW_TOUCHPAD;

	if (fw_version == 0x20800 || fw_version == 0x20b00 ||
	    fw_version == 0x20030) {
		hw->x_max = ELANTECH_V2_X_MAX;
		hw->y_max = ELANTECH_V2_Y_MAX;
	} else {
		if (pms_spec_cmd(sc, ELANTECH_QUE_FW_ID) ||
		    pms_get_status(sc, resp))
			return (-1);
		fixed_dpi = resp[1] & 0x10;
		i = (fw_version > 0x20800 && fw_version < 0x20900) ? 1 : 2;
		if ((fw_version >> 16) == 0x14 && fixed_dpi) {
			if (pms_spec_cmd(sc, ELANTECH_QUE_SAMPLE) ||
			    pms_get_status(sc, resp))
				return (-1);
			hw->x_max = (capabilities[1] - i) * resp[1] / 2;
			hw->y_max = (capabilities[2] - i) * resp[2] / 2;
		} else if (fw_version == 0x040216) {
			hw->x_max = 819;
			hw->y_max = 405;
		} else if (fw_version == 0x040219 || fw_version == 0x040215) {
			hw->x_max = 900;
			hw->y_max = 500;
		} else {
			hw->x_max = (capabilities[1] - i) * 64;
			hw->y_max = (capabilities[2] - i) * 64;
		}
	}

	return (0);
}

int
elantech_get_hwinfo_v3(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	struct wsmousehw *hw;
	int fw_version;
	u_char resp[3];

	if (synaptics_query(sc, ELANTECH_QUE_FW_VER, &fw_version))
		return (-1);

	if (((fw_version & 0x0f0000) >> 16) != 5)
		return (-1);

	elantech->fw_version = fw_version;
	elantech->flags |= ELANTECH_F_REPORTS_PRESSURE;

	if ((fw_version & 0x4000) == 0x4000)
		elantech->flags |= ELANTECH_F_CRC_ENABLED;

	if (elantech_set_absolute_mode_v3(sc))
		return (-1);

	if (pms_spec_cmd(sc, ELANTECH_QUE_FW_ID) ||
	    pms_get_status(sc, resp))
		return (-1);

	hw = wsmouse_get_hw(sc->sc_wsmousedev);
	hw->x_max = elantech->max_x = (resp[0] & 0x0f) << 8 | resp[1];
	hw->y_max = elantech->max_y = (resp[0] & 0xf0) << 4 | resp[2];

	hw->type = WSMOUSE_TYPE_ELANTECH;
	hw->hw_type = WSMOUSEHW_TOUCHPAD;

	return (0);
}

int
elantech_get_hwinfo_v4(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	struct wsmousehw *hw;
	int fw_version;
	u_char capabilities[3];
	u_char resp[3];

	if (synaptics_query(sc, ELANTECH_QUE_FW_VER, &fw_version))
		return (-1);

	if ((fw_version & 0x0f0000) >> 16 < 6)
		return (-1);

	elantech->fw_version = fw_version;
	elantech->flags |= ELANTECH_F_REPORTS_PRESSURE;

	if ((fw_version & 0x4000) == 0x4000)
		elantech->flags |= ELANTECH_F_CRC_ENABLED;

	if (elantech_set_absolute_mode_v4(sc))
		return (-1);

	if (pms_spec_cmd(sc, ELANTECH_QUE_CAPABILITIES) ||
	    pms_get_status(sc, capabilities))
		return (-1);

	if (pms_spec_cmd(sc, ELANTECH_QUE_FW_ID) ||
	    pms_get_status(sc, resp))
		return (-1);

	hw = wsmouse_get_hw(sc->sc_wsmousedev);
	hw->x_max = (resp[0] & 0x0f) << 8 | resp[1];
	hw->y_max = (resp[0] & 0xf0) << 4 | resp[2];

	if ((capabilities[1] < 2) || (capabilities[1] > hw->x_max))
		return (-1);

	if (capabilities[0] & ELANTECH_CAP_TRACKPOINT)
		elantech->flags |= ELANTECH_F_TRACKPOINT;

	hw->type = WSMOUSE_TYPE_ELANTECH;
	hw->hw_type = (ELANTECH_IS_CLICKPAD(sc)
	    ? WSMOUSEHW_CLICKPAD : WSMOUSEHW_TOUCHPAD);
	hw->mt_slots = ELANTECH_MAX_FINGERS;

	elantech->width = hw->x_max / (capabilities[1] - 1);

	return (0);
}

int
elantech_ps2_cmd(struct pms_softc *sc, u_char command)
{
	u_char cmd[1];

	cmd[0] = command;
	return (pms_cmd(sc, cmd, 1, NULL, 0));
}

int
elantech_knock(struct pms_softc *sc)
{
	u_char resp[3];

	if (pms_dev_disable(sc) ||
	    pms_set_scaling(sc, 1) ||
	    pms_set_scaling(sc, 1) ||
	    pms_set_scaling(sc, 1) ||
	    pms_get_status(sc, resp) ||
	    resp[0] != PMS_ELANTECH_MAGIC1 ||
	    resp[1] != PMS_ELANTECH_MAGIC2 ||
	    (resp[2] != PMS_ELANTECH_MAGIC3_1 &&
	    resp[2] != PMS_ELANTECH_MAGIC3_2))
		return (-1);

	return (0);
}

int
pms_enable_elantech_v1(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	int i;

	if (elantech_knock(sc))
		goto err;

	if (sc->elantech == NULL) {
		sc->elantech = elantech = malloc(sizeof(struct elantech_softc),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		if (elantech == NULL) {
			printf("%s: elantech: not enough memory\n",
			    DEVNAME(sc));
			goto err;
		}

		if (elantech_get_hwinfo_v1(sc)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			goto err;
		}
		if (wsmouse_configure(sc->sc_wsmousedev, NULL, 0)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			printf("%s: elantech: setup failed\n", DEVNAME(sc));
			goto err;
		}

		printf("%s: Elantech Touchpad, version %d, firmware 0x%x\n",
		    DEVNAME(sc), 1, sc->elantech->fw_version);
	} else if (elantech_set_absolute_mode_v1(sc))
		goto err;

	for (i = 0; i < nitems(sc->elantech->parity); i++)
		sc->elantech->parity[i] = sc->elantech->parity[i & (i - 1)] ^ 1;

	return (1);

err:
	pms_reset(sc);

	return (0);
}

int
pms_enable_elantech_v2(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;

	if (elantech_knock(sc))
		goto err;

	if (sc->elantech == NULL) {
		sc->elantech = elantech = malloc(sizeof(struct elantech_softc),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		if (elantech == NULL) {
			printf("%s: elantech: not enough memory\n",
			    DEVNAME(sc));
			goto err;
		}

		if (elantech_get_hwinfo_v2(sc)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			goto err;
		}
		if (wsmouse_configure(sc->sc_wsmousedev, NULL, 0)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			printf("%s: elantech: setup failed\n", DEVNAME(sc));
			goto err;
		}

		printf("%s: Elantech Touchpad, version %d, firmware 0x%x\n",
		    DEVNAME(sc), 2, sc->elantech->fw_version);
	} else if (elantech_set_absolute_mode_v2(sc))
		goto err;

	return (1);

err:
	pms_reset(sc);

	return (0);
}

int
pms_enable_elantech_v3(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;

	if (elantech_knock(sc))
		goto err;

	if (sc->elantech == NULL) {
		sc->elantech = elantech = malloc(sizeof(struct elantech_softc),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		if (elantech == NULL) {
			printf("%s: elantech: not enough memory\n",
			    DEVNAME(sc));
			goto err;
		}

		if (elantech_get_hwinfo_v3(sc)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			goto err;
		}
		if (wsmouse_configure(sc->sc_wsmousedev, NULL, 0)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			printf("%s: elantech: setup failed\n", DEVNAME(sc));
			goto err;
		}

		printf("%s: Elantech Touchpad, version %d, firmware 0x%x\n",
		    DEVNAME(sc), 3, sc->elantech->fw_version);
	} else if (elantech_set_absolute_mode_v3(sc))
		goto err;

	return (1);

err:
	pms_reset(sc);

	return (0);
}

int
pms_enable_elantech_v4(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	struct wsmousedev_attach_args a;

	if (elantech_knock(sc))
		goto err;

	if (sc->elantech == NULL) {
		sc->elantech = elantech = malloc(sizeof(struct elantech_softc),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		if (elantech == NULL) {
			printf("%s: elantech: not enough memory\n",
			    DEVNAME(sc));
			goto err;
		}

		if (elantech_get_hwinfo_v4(sc)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			goto err;
		}
		if (wsmouse_configure(sc->sc_wsmousedev, NULL, 0)) {
			free(sc->elantech, M_DEVBUF,
			    sizeof(struct elantech_softc));
			sc->elantech = NULL;
			printf("%s: elantech: setup failed\n", DEVNAME(sc));
			goto err;
		}

		printf("%s: Elantech %s, version 4, firmware 0x%x\n",
		    DEVNAME(sc), (ELANTECH_IS_CLICKPAD(sc) ?  "Clickpad"
		    : "Touchpad"), sc->elantech->fw_version);

		if (sc->elantech->flags & ELANTECH_F_TRACKPOINT) {
			a.accessops = &pms_sec_accessops;
			a.accesscookie = sc;
			sc->sc_sec_wsmousedev = config_found((void *) sc, &a,
			    wsmousedevprint);
		}

	} else if (elantech_set_absolute_mode_v4(sc))
		goto err;

	return (1);

err:
	pms_reset(sc);

	return (0);
}

int
pms_ioctl_elantech(struct pms_softc *sc, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;
	struct wsmousehw *hw;
	int wsmode;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_ELANTECH;
		break;
	case WSMOUSEIO_GCALIBCOORDS:
		hw = wsmouse_get_hw(sc->sc_wsmousedev);
		wsmc->minx = hw->x_min;
		wsmc->maxx = hw->x_max;
		wsmc->miny = hw->y_min;
		wsmc->maxy = hw->y_max;
		wsmc->swapxy = 0;
		wsmc->resx = hw->h_res;
		wsmc->resy = hw->v_res;
		break;
	case WSMOUSEIO_SETMODE:
		wsmode = *(u_int *)data;
		if (wsmode != WSMOUSE_COMPAT && wsmode != WSMOUSE_NATIVE)
			return (EINVAL);
		wsmouse_set_mode(sc->sc_wsmousedev, wsmode);
		break;
	default:
		return (-1);
	}
	return (0);
}

int
pms_sync_elantech_v1(struct pms_softc *sc, int data)
{
	struct elantech_softc *elantech = sc->elantech;
	u_char p;

	switch (sc->inputstate) {
	case 0:
		if (elantech->flags & ELANTECH_F_HW_V1_OLD) {
			elantech->p1 = (data & 0x20) >> 5;
			elantech->p2 = (data & 0x10) >> 4;
		} else {
			elantech->p1 = (data & 0x10) >> 4;
			elantech->p2 = (data & 0x20) >> 5;
		}
		elantech->p3 = (data & 0x04) >> 2;
		return (0);
	case 1:
		p = elantech->p1;
		break;
	case 2:
		p = elantech->p2;
		break;
	case 3:
		p = elantech->p3;
		break;
	default:
		return (-1);
	}

	if (data < 0 || data >= nitems(elantech->parity) ||
	/*
	 * FW 0x20022 sends inverted parity bits on cold boot, returning
	 * to normal after suspend & resume, so the parity check is
	 * disabled for this one.
	 */
	    (elantech->fw_version != 0x20022 && elantech->parity[data] != p))
		return (-1);

	return (0);
}

int
pms_sync_elantech_v2(struct pms_softc *sc, int data)
{
	struct elantech_softc *elantech = sc->elantech;

	/* Variants reporting pressure always have the same constant bits. */
	if (elantech->flags & ELANTECH_F_REPORTS_PRESSURE) {
		if (sc->inputstate == 0 && (data & 0x0c) != 0x04)
			return (-1);
		if (sc->inputstate == 3 && (data & 0x0f) != 0x02)
			return (-1);
		return (0);
	}

	/* For variants not reporting pressure, 1 and 3 finger touch packets
	 * have different constant bits than 2 finger touch packets. */
	switch (sc->inputstate) {
	case 0:
		if ((data & 0xc0) == 0x80) {
			if ((data & 0x0c) != 0x0c)
				return (-1);
			elantech->flags |= ELANTECH_F_2FINGER_PACKET;
		} else {
			if ((data & 0x3c) != 0x3c)
				return (-1);
			elantech->flags &= ~ELANTECH_F_2FINGER_PACKET;
		}
		break;
	case 1:
	case 4:
		if (elantech->flags & ELANTECH_F_2FINGER_PACKET)
			break;
		if ((data & 0xf0) != 0x00)
			return (-1);
		break;
	case 3:
		if (elantech->flags & ELANTECH_F_2FINGER_PACKET) {
			if ((data & 0x0e) != 0x08)
				return (-1);
		} else {
			if ((data & 0x3e) != 0x38)
				return (-1);
		}
		break;
	default:
		break;
	}

	return (0);
}

int
pms_sync_elantech_v3(struct pms_softc *sc, int data)
{
	struct elantech_softc *elantech = sc->elantech;

	switch (sc->inputstate) {
	case 0:
		if (elantech->flags & ELANTECH_F_CRC_ENABLED)
			break;
		if ((data & 0x0c) != 0x04 && (data & 0x0c) != 0x0c)
			return (-1);
		break;
	case 3:
		if (elantech->flags & ELANTECH_F_CRC_ENABLED) {
			if ((data & 0x09) != 0x08 && (data & 0x09) != 0x09)
				return (-1);
		} else {
			if ((data & 0xcf) != 0x02 && (data & 0xce) != 0x0c)
				return (-1);
		}
		break;
	}

	return (0);
}

/* Extract the type bits from packet[3]. */
static inline int
elantech_packet_type(struct elantech_softc *elantech, u_char b)
{
	/*
	 * This looks dubious, but in the "crc-enabled" format bit 2 may
	 * be set even in MOTION packets.
	 */
	if ((elantech->flags & ELANTECH_F_TRACKPOINT) && ((b & 0x0f) == 0x06))
		return (ELANTECH_PKT_TRACKPOINT);
	else
		return (b & 0x03);
}

int
pms_sync_elantech_v4(struct pms_softc *sc, int data)
{
	if (sc->inputstate == 0)
		return ((data & 0x08) == 0 ? 0 : -1);

	if (sc->inputstate == 3) {
		switch (elantech_packet_type(sc->elantech, data)) {
		case ELANTECH_V4_PKT_STATUS:
		case ELANTECH_V4_PKT_HEAD:
		case ELANTECH_V4_PKT_MOTION:
			if (sc->elantech->flags & ELANTECH_F_CRC_ENABLED)
				return ((data & 0x08) == 0 ? 0 : -1);
			else
				return ((data & 0x1c) == 0x10 ? 0 : -1);
		case ELANTECH_PKT_TRACKPOINT:
			return ((sc->packet[0] & 0xc8) == 0
			    && sc->packet[1] == ((data & 0x10) << 3)
			    && sc->packet[2] == ((data & 0x20) << 2)
			    && (data ^ (sc->packet[0] & 0x30)) == 0x36
			    ? 0 : -1);
		}
		return (-1);
	}
	return (0);
}

void
pms_proc_elantech_v1(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	int x, y, w, z;
	u_int buttons;

	buttons = butmap[sc->packet[0] & 3];

	if (elantech->flags & ELANTECH_F_HAS_ROCKER) {
		if (sc->packet[0] & 0x40) /* up */
			buttons |= WSMOUSE_BUTTON(4);
		if (sc->packet[0] & 0x80) /* down */
			buttons |= WSMOUSE_BUTTON(5);
	}

	if (elantech->flags & ELANTECH_F_HW_V1_OLD)
		w = ((sc->packet[1] & 0x80) >> 7) +
		    ((sc->packet[1] & 0x30) >> 4);
	else
		w = (sc->packet[0] & 0xc0) >> 6;

	/*
	 * Firmwares 0x20022 and 0x20600 have a bug, position data in the
	 * first two reports for single-touch contacts may be corrupt.
	 */
	if (elantech->fw_version == 0x20022 ||
	    elantech->fw_version == 0x20600) {
		if (w == 1) {
			if (elantech->initial_pkt < 2) {
				elantech->initial_pkt++;
				return;
			}
		} else if (elantech->initial_pkt) {
			elantech->initial_pkt = 0;
		}
	}

	/* Hardware version 1 doesn't report pressure. */
	if (w) {
		x = ((sc->packet[1] & 0x0c) << 6) | sc->packet[2];
		y = ((sc->packet[1] & 0x03) << 8) | sc->packet[3];
		z = SYNAPTICS_PRESSURE;
	} else {
		x = y = z = 0;
	}

	WSMOUSE_TOUCH(sc->sc_wsmousedev, buttons, x, y, z, w);
}

void
pms_proc_elantech_v2(struct pms_softc *sc)
{
	const u_char debounce_pkt[] = { 0x84, 0xff, 0xff, 0x02, 0xff, 0xff };
	struct elantech_softc *elantech = sc->elantech;
	int x, y, w, z;
	u_int buttons;

	/*
	 * The hardware sends this packet when in debounce state.
	 * The packet should be ignored.
	 */
	if (!memcmp(sc->packet, debounce_pkt, sizeof(debounce_pkt)))
		return;

	buttons = butmap[sc->packet[0] & 3];

	w = (sc->packet[0] & 0xc0) >> 6;
	if (w == 1 || w == 3) {
		x = ((sc->packet[1] & 0x0f) << 8) | sc->packet[2];
		y = ((sc->packet[4] & 0x0f) << 8) | sc->packet[5];
		if (elantech->flags & ELANTECH_F_REPORTS_PRESSURE)
			z = ((sc->packet[1] & 0xf0) |
			    (sc->packet[4] & 0xf0) >> 4);
		else
			z = SYNAPTICS_PRESSURE;
	} else if (w == 2) {
		x = (((sc->packet[0] & 0x10) << 4) | sc->packet[1]) << 2;
		y = (((sc->packet[0] & 0x20) << 3) | sc->packet[2]) << 2;
		z = SYNAPTICS_PRESSURE;
	} else {
		x = y = z = 0;
	}

	WSMOUSE_TOUCH(sc->sc_wsmousedev, buttons, x, y, z, w);
}

void
pms_proc_elantech_v3(struct pms_softc *sc)
{
	const u_char debounce_pkt[] = { 0xc4, 0xff, 0xff, 0x02, 0xff, 0xff };
	struct elantech_softc *elantech = sc->elantech;
	int x, y, w, z;
	u_int buttons;

	buttons = butmap[sc->packet[0] & 3];

	x = ((sc->packet[1] & 0x0f) << 8 | sc->packet[2]);
	y = ((sc->packet[4] & 0x0f) << 8 | sc->packet[5]);
	z = 0;
	w = (sc->packet[0] & 0xc0) >> 6;
	if (w == 2) {
		/*
		 * Two-finger touch causes two packets -- a head packet
		 * and a tail packet. We report a single event and ignore
		 * the tail packet.
		 */
		if (elantech->flags & ELANTECH_F_CRC_ENABLED) {
			if ((sc->packet[3] & 0x09) != 0x08)
				return;
		} else {
			/* The hardware sends this packet when in debounce state.
	 		 * The packet should be ignored. */
			if (!memcmp(sc->packet, debounce_pkt, sizeof(debounce_pkt)))
				return;
			if ((sc->packet[0] & 0x0c) != 0x04 &&
	    		(sc->packet[3] & 0xcf) != 0x02) {
				/* not the head packet -- ignore */
				return;
			}
		}
	}

	/* Prevent jumping cursor if pad isn't touched or reports garbage. */
	if (w == 0 ||
	    ((x == 0 || y == 0 || x == elantech->max_x || y == elantech->max_y)
	    && (x != elantech->old_x || y != elantech->old_y))) {
		x = elantech->old_x;
		y = elantech->old_y;
	}

	if (elantech->flags & ELANTECH_F_REPORTS_PRESSURE)
		z = (sc->packet[1] & 0xf0) | ((sc->packet[4] & 0xf0) >> 4);
	else if (w)
		z = SYNAPTICS_PRESSURE;

	WSMOUSE_TOUCH(sc->sc_wsmousedev, buttons, x, y, z, w);
	elantech->old_x = x;
	elantech->old_y = y;
}

void
pms_proc_elantech_v4(struct pms_softc *sc)
{
	struct elantech_softc *elantech = sc->elantech;
	struct device *sc_wsmousedev = sc->sc_wsmousedev;
	int id, weight, n, x, y, z;
	u_int buttons, slots;

	switch (elantech_packet_type(elantech, sc->packet[3])) {
	case ELANTECH_V4_PKT_STATUS:
		slots = elantech->mt_slots;
		elantech->mt_slots = sc->packet[1] & 0x1f;
		slots &= ~elantech->mt_slots;
		for (id = 0; slots; id++, slots >>= 1) {
			if (slots & 1)
				wsmouse_mtstate(sc_wsmousedev, id, 0, 0, 0);
		}
		break;

	case ELANTECH_V4_PKT_HEAD:
		id = ((sc->packet[3] & 0xe0) >> 5) - 1;
		if (id > -1 && id < ELANTECH_MAX_FINGERS) {
			x = ((sc->packet[1] & 0x0f) << 8) | sc->packet[2];
			y = ((sc->packet[4] & 0x0f) << 8) | sc->packet[5];
			z = (sc->packet[1] & 0xf0)
			    | ((sc->packet[4] & 0xf0) >> 4);
			wsmouse_mtstate(sc_wsmousedev, id, x, y, z);
		}
		break;

	case ELANTECH_V4_PKT_MOTION:
		weight = (sc->packet[0] & 0x10) ? ELANTECH_V4_WEIGHT_VALUE : 1;
		for (n = 0; n < 6; n += 3) {
			id = ((sc->packet[n] & 0xe0) >> 5) - 1;
			if (id < 0 || id >= ELANTECH_MAX_FINGERS)
				continue;
			x = weight * (signed char)sc->packet[n + 1];
			y = weight * (signed char)sc->packet[n + 2];
			z = WSMOUSE_DEFAULT_PRESSURE;
			wsmouse_set(sc_wsmousedev, WSMOUSE_MT_REL_X, x, id);
			wsmouse_set(sc_wsmousedev, WSMOUSE_MT_REL_Y, y, id);
			wsmouse_set(sc_wsmousedev, WSMOUSE_MT_PRESSURE, z, id);
		}
		break;

	case ELANTECH_PKT_TRACKPOINT:
		if (sc->sc_dev_enable & PMS_DEV_SECONDARY) {
			/*
			* This firmware misreport coordinates for trackpoint
			* occasionally. Discard packets outside of [-127, 127] range
			* to prevent cursor jumps.
			*/
			if (sc->packet[4] == 0x80 || sc->packet[5] == 0x80 ||
			    sc->packet[1] >> 7 == sc->packet[4] >> 7 ||
			    sc->packet[2] >> 7 == sc->packet[5] >> 7)
				return;

			x = sc->packet[4] - 0x100 + (sc->packet[1] << 1);
			y = sc->packet[5] - 0x100 + (sc->packet[2] << 1);
			buttons = butmap[sc->packet[0] & 7];
			WSMOUSE_INPUT(sc->sc_sec_wsmousedev,
			    buttons, x, y, 0, 0);
		}
		return;

	default:
		printf("%s: unknown packet type 0x%x\n", DEVNAME(sc),
		    sc->packet[3] & 0x1f);
		return;
	}

	buttons = butmap[sc->packet[0] & 3];
	wsmouse_buttons(sc_wsmousedev, buttons);

	wsmouse_input_sync(sc_wsmousedev);
}
