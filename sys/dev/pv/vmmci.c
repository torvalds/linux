/*	$OpenBSD: vmmci.c,v 1.14 2025/09/16 12:18:10 hshoexer Exp $	*/

/*
 * Copyright (c) 2017 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/timeout.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <machine/bus.h>

#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>
#include <dev/pv/pvvar.h>

enum vmmci_cmd {
	VMMCI_NONE = 0,
	VMMCI_SHUTDOWN,
	VMMCI_REBOOT,
	VMMCI_SYNCRTC,
};

struct vmmci_softc {
	struct device		 sc_dev;
	struct virtio_softc	*sc_virtio;
	enum vmmci_cmd		 sc_cmd;
	unsigned int		 sc_interval;
	struct ksensordev	 sc_sensordev;
	struct ksensor		 sc_sensor;
	struct timeout		 sc_tick;
};

int	vmmci_match(struct device *, void *, void *);
void	vmmci_attach(struct device *, struct device *, void *);
int	vmmci_activate(struct device *, int);

int	vmmci_config_change(struct virtio_softc *);
void	vmmci_tick(void *);
void	vmmci_tick_hook(struct device *);

const struct cfattach vmmci_ca = {
	sizeof(struct vmmci_softc),
	vmmci_match,
	vmmci_attach,
	NULL,
	vmmci_activate
};

/* Configuration registers */
#define VMMCI_CONFIG_COMMAND	0
#define VMMCI_CONFIG_TIME_SEC	4
#define VMMCI_CONFIG_TIME_USEC	12

/* Feature bits */
#define VMMCI_F_TIMESYNC	(1ULL<<0)
#define VMMCI_F_ACK		(1ULL<<1)
#define VMMCI_F_SYNCRTC		(1ULL<<2)

struct cfdriver vmmci_cd = {
	NULL, "vmmci", DV_DULL, CD_COCOVM
};

int
vmmci_match(struct device *parent, void *match, void *aux)
{
	struct virtio_attach_args *va = aux;
	if (va->va_devid == PCI_PRODUCT_VIRTIO_VMMCI)
		return (1);
	return (0);
}

void
vmmci_attach(struct device *parent, struct device *self, void *aux)
{
	struct vmmci_softc *sc = (struct vmmci_softc *)self;
	struct virtio_softc *vsc = (struct virtio_softc *)parent;
	struct virtio_attach_args *va = aux;

	if (vsc->sc_child != NULL)
		panic("already attached to something else");

	vsc->sc_child = self;
	vsc->sc_nvqs = 0;
	vsc->sc_config_change = vmmci_config_change;
	vsc->sc_ipl = IPL_NET;
	sc->sc_virtio = vsc;

	vsc->sc_driver_features = VMMCI_F_TIMESYNC | VMMCI_F_ACK |
	    VMMCI_F_SYNCRTC;
	if (virtio_negotiate_features(vsc, NULL) != 0)
		goto err;

	if (virtio_has_feature(vsc, VMMCI_F_TIMESYNC)) {
		strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
		    sizeof(sc->sc_sensordev.xname));
		sc->sc_sensor.type = SENSOR_TIMEDELTA;
		sc->sc_sensor.status = SENSOR_S_UNKNOWN;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
		sensordev_install(&sc->sc_sensordev);

		config_mountroot(self, vmmci_tick_hook);
	}

	printf("\n");
	if (virtio_attach_finish(vsc, va) != 0)
		goto err;
	return;

err:
	vsc->sc_child = VIRTIO_CHILD_ERROR;
}

int
vmmci_activate(struct device *self, int act)
{
	struct vmmci_softc	*sc = (struct vmmci_softc *)self;
	struct virtio_softc	*vsc = sc->sc_virtio;

	if (virtio_has_feature(vsc, VMMCI_F_ACK) == 0)
		return (0);

	switch (act) {
	case DVACT_POWERDOWN:
		printf("%s: powerdown\n", sc->sc_dev.dv_xname);

		/*
		 * Tell the host that we are shutting down.  The host will
		 * start a timer and kill our VM if we didn't reboot before
		 * expiration.  This avoids being stuck in the
		 * "Please press any key to reboot" handler on RB_HALT;
		 * without hooking into the MD code directly.
		 */
		virtio_write_device_config_4(vsc, VMMCI_CONFIG_COMMAND,
		    VMMCI_SHUTDOWN);
		break;
	default:
		break;
	}
	return (0);
}

int
vmmci_config_change(struct virtio_softc *vsc)
{
	struct vmmci_softc	*sc = (struct vmmci_softc *)vsc->sc_child;
	uint32_t		 cmd;

	/* Check for command */
	cmd = virtio_read_device_config_4(vsc, VMMCI_CONFIG_COMMAND);
	if (cmd == sc->sc_cmd)
		return (0);
	sc->sc_cmd = cmd;

	switch (cmd) {
	case VMMCI_NONE:
		/* no action */
		break;
	case VMMCI_SHUTDOWN:
		pvbus_shutdown(&sc->sc_dev);
		break;
	case VMMCI_REBOOT:
		pvbus_reboot(&sc->sc_dev);
		break;
	case VMMCI_SYNCRTC:
		inittodr(gettime());
		sc->sc_cmd = VMMCI_NONE;
		break;	
	default:
		printf("%s: invalid command %d\n", sc->sc_dev.dv_xname, cmd);
		cmd = VMMCI_NONE;		
		break;
	}

	if ((cmd != VMMCI_NONE) && virtio_has_feature(vsc, VMMCI_F_ACK))
		virtio_write_device_config_4(vsc, VMMCI_CONFIG_COMMAND, cmd);

	return (1);
}

void
vmmci_tick(void *arg)
{
	struct vmmci_softc	*sc = arg;
	struct virtio_softc	*vsc = sc->sc_virtio;
	struct timeval		*guest = &sc->sc_sensor.tv;
	struct timeval		 host, diff;

	microtime(guest);

	/* Update time delta sensor */
	host.tv_sec = virtio_read_device_config_8(vsc, VMMCI_CONFIG_TIME_SEC);
	host.tv_usec = virtio_read_device_config_8(vsc, VMMCI_CONFIG_TIME_USEC);

	if (host.tv_usec > 0) {
		timersub(guest, &host, &diff);

		sc->sc_sensor.value = (uint64_t)diff.tv_sec * 1000000000LL +
		    (uint64_t)diff.tv_usec * 1000LL;
		sc->sc_sensor.status = SENSOR_S_OK;
	} else
		sc->sc_sensor.status = SENSOR_S_UNKNOWN;

	timeout_add_sec(&sc->sc_tick, 15);
}

void
vmmci_tick_hook(struct device *self)
{
	struct vmmci_softc	*sc = (struct vmmci_softc *)self;

	timeout_set(&sc->sc_tick, vmmci_tick, sc);
	vmmci_tick(sc);
}
