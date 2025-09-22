/*	$OpenBSD: tipd.c,v 1.4 2025/07/10 22:46:17 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <dev/i2c/i2cvar.h>

#define TPS_CMD1		0x08
#define TPS_DATA1		0x09
#define TPS_INT_EVENT_1		0x14
#define TPS_INT_EVENT_2		0x15
#define TPS_INT_MASK_1		0x16
#define TPS_INT_MASK_2		0x17
#define TPS_INT_CLEAR_1		0x18
#define TPS_INT_CLEAR_2		0x19
#define TPS_STATUS		0x1a
#define  TPS_STATUS_PLUG_PRESENT	(1 << 0)
#define TPS_SYSTEM_POWER_STATE	0x20
#define  TPS_SYSTEM_POWER_STATE_S0	0
#define  TPS_SYSTEM_POWER_STATE_S5	5
#define TPS_POWER_STATUS	0x3f

#define TPS_CMD(s)	((s[3] << 24) | (s[2] << 16) | (s[1] << 8) | s[0])

/*
 * Interrupt bits on the CD321x controllers used by Apple differ from
 * those used by the standard TPS6598x controllers.
 */
#define CD_INT_PLUG_EVENT		(1 << 1)

struct tipd_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;

	void			*sc_ih;

	struct device_ports	sc_ports;
	uint32_t		sc_status;
};

int	tipd_match(struct device *, void *, void *);
void	tipd_attach(struct device *, struct device *, void *);
int	tipd_activate(struct device *, int);

const struct cfattach tipd_ca = {
	sizeof(struct tipd_softc), tipd_match, tipd_attach, NULL,
	tipd_activate
};

struct cfdriver tipd_cd = {
	NULL, "tipd", DV_DULL
};

int	tipd_intr(void *);

int	tipd_read_4(struct tipd_softc *, uint8_t, uint32_t *);
int	tipd_read_8(struct tipd_softc *, uint8_t, uint64_t *);
int	tipd_write_4(struct tipd_softc *, uint8_t, uint32_t);
int	tipd_write_8(struct tipd_softc *, uint8_t, uint64_t);
int	tipd_exec(struct tipd_softc *, const char *,
	    const void *, size_t, void *, size_t);

int
tipd_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return iic_is_compatible(ia, "apple,cd321x");
}

void
tipd_attach(struct device *parent, struct device *self, void *aux)
{
	struct tipd_softc *sc = (struct tipd_softc *)self;
	struct i2c_attach_args *ia = aux;
	int node = *(int *)ia->ia_cookie;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	sc->sc_ih = fdt_intr_establish(node, IPL_BIO, tipd_intr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");

	tipd_read_4(sc, TPS_STATUS, &sc->sc_status);
	tipd_write_8(sc, TPS_INT_MASK_1, CD_INT_PLUG_EVENT);

	node = OF_getnodebyname(node, "connector");
	if (node) {
		sc->sc_ports.dp_node = node;
		device_ports_register(&sc->sc_ports, -1);
	}
}

int
tipd_activate(struct device *self, int act)
{
	struct tipd_softc *sc = (struct tipd_softc *)self;
	uint8_t state;
	int error;

	switch (act) {
	case DVACT_QUIESCE:
		tipd_write_8(sc, TPS_INT_MASK_1, 0);
		break;
	case DVACT_SUSPEND:
		state = TPS_SYSTEM_POWER_STATE_S5;
		error = tipd_exec(sc, "SSPS", &state, sizeof(state), NULL, 0);
		if (error)
			printf("%s: powerdown failed\n", sc->sc_dev.dv_xname);
		break;
	case DVACT_RESUME:
		state = TPS_SYSTEM_POWER_STATE_S0;
		error = tipd_exec(sc, "SSPS", &state, sizeof(state), NULL, 0);
		if (error)
			printf("%s: powerup failed\n", sc->sc_dev.dv_xname);
		break;
	case DVACT_WAKEUP:
		tipd_read_4(sc, TPS_STATUS, &sc->sc_status);
		tipd_write_8(sc, TPS_INT_MASK_1, CD_INT_PLUG_EVENT);
		break;
	}

	return 0;
}

void
tipd_connect(struct tipd_softc *sc)
{
	struct endpoint *ep, *rep;
	struct usb_controller_port *port;

	ep = endpoint_byreg(&sc->sc_ports, 0, -1);
	if (ep == NULL)
		return;
	rep = endpoint_remote(ep);
	if (rep == NULL || rep->ep_type != EP_USB_CONTROLLER_PORT)
		return;
	port = endpoint_get_cookie(rep);
	if (port && port->up_connect)
		port->up_connect(port->up_cookie);
}

void
tipd_disconnect(struct tipd_softc *sc)
{
	struct endpoint *ep, *rep;
	struct usb_controller_port *port;

	ep = endpoint_byreg(&sc->sc_ports, 0, -1);
	if (ep == NULL)
		return;
	rep = endpoint_remote(ep);
	if (rep == NULL || rep->ep_type != EP_USB_CONTROLLER_PORT)
		return;
	port = endpoint_get_cookie(rep);
	if (port && port->up_disconnect)
		port->up_disconnect(port->up_cookie);
}

int
tipd_intr(void *arg)
{
	struct tipd_softc *sc = arg;
	uint64_t event;
	uint32_t status;
	int error;

	error = tipd_read_8(sc, TPS_INT_EVENT_1, &event);
	if (error)
		return 0;

	if (event == 0)
		return 0;

	if (event & CD_INT_PLUG_EVENT) {
		error = tipd_read_4(sc, TPS_STATUS, &status);
		if (error)
			goto fail;

		/*
		 * We may get a spurious plug event upon resume.  Make
		 * sure we only signal a new connection when the plug
		 * present state really changed.
		 */
		if ((status ^ sc->sc_status) & TPS_STATUS_PLUG_PRESENT) {
			if (status & TPS_STATUS_PLUG_PRESENT)
				tipd_connect(sc);
			else
				tipd_disconnect(sc);
			sc->sc_status = status;
		}
	}

fail:
	tipd_write_8(sc, TPS_INT_CLEAR_1, event);
	return 1;
}

int
tipd_read_4(struct tipd_softc *sc, uint8_t reg, uint32_t *val)
{
	uint8_t buf[5];
	int error;

	iic_acquire_bus(sc->sc_tag, 0);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), buf, sizeof(buf), 0);
	iic_release_bus(sc->sc_tag, 0);

	if (error == 0)
		*val = lemtoh32(&buf[1]);

	return error;
}

int
tipd_read_8(struct tipd_softc *sc, uint8_t reg, uint64_t *val)
{
	uint8_t buf[9];
	int error;

	iic_acquire_bus(sc->sc_tag, 0);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), buf, sizeof(buf), 0);
	iic_release_bus(sc->sc_tag, 0);

	if (error == 0)
		*val = lemtoh64(&buf[1]);

	return error;
}

int
tipd_write_4(struct tipd_softc *sc, uint8_t reg, uint32_t val)
{
	uint8_t buf[5];
	int error;

	buf[0] = 4;
	htolem32(&buf[1], val);

	iic_acquire_bus(sc->sc_tag, 0);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), buf, sizeof(buf), 0);
	iic_release_bus(sc->sc_tag, 0);

	return error;
}

int
tipd_write_8(struct tipd_softc *sc, uint8_t reg, uint64_t val)
{
	uint8_t buf[9];
	int error;

	buf[0] = 8;
	htolem64(&buf[1], val);

	iic_acquire_bus(sc->sc_tag, 0);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), buf, sizeof(buf), 0);
	iic_release_bus(sc->sc_tag, 0);

	return error;
}

int
tipd_exec(struct tipd_softc *sc, const char *cmd, const void *wbuf,
    size_t wlen, void *rbuf, size_t rlen)
{
	char buf[65];
	uint32_t val;
	int timo, error;
	uint8_t reg = TPS_DATA1;
	int s;

	if (wlen >= sizeof(buf) - 1)
		return EINVAL;

	s = splbio();

	error = tipd_read_4(sc, TPS_CMD1, &val);
	if (error == 0 && val == TPS_CMD("!CMD"))
		error = EBUSY;
	if (error)
		goto fail;

	if (wlen > 0) {
		buf[0] = wlen;
		memcpy(&buf[1], wbuf, wlen);
		iic_acquire_bus(sc->sc_tag, 0);
		error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
		    sc->sc_addr, &reg, sizeof(reg), buf, sizeof(buf), 0);
		iic_release_bus(sc->sc_tag, 0);
		if (error)
			goto fail;
	}

	error = tipd_write_4(sc, TPS_CMD1, TPS_CMD(cmd));
	if (error)
		goto fail;

	for (timo = 1000; timo > 0; timo--) {
		error = tipd_read_4(sc, TPS_CMD1, &val);
		if (error == 0 && val == TPS_CMD("!CMD"))
			error = EBUSY;
		if (error)
			goto fail;
		if (val == 0)
			break;
		delay(10);
	}

	if (timo == 0) {
		error = ETIMEDOUT;
		goto fail;
	}

	if (rlen > 0) {
		memset(buf, 0, sizeof(buf));
		iic_acquire_bus(sc->sc_tag, 0);
		error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
		    sc->sc_addr, &reg, sizeof(reg), buf, sizeof(buf), 0);
		iic_release_bus(sc->sc_tag, 0);
		if (error == 0 && buf[0] < rlen)
			error = EIO;
		if (error)
			goto fail;
		memcpy(rbuf, &buf[1], rlen);
	}

fail:
	splx(s);
	return error;
}
