/*	$OpenBSD: ipmi_i2c.c,v 1.6 2024/10/09 00:38:26 jsg Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>

#include <dev/i2c/i2cvar.h>
#include <dev/ipmivar.h>

#define BMC_SA			0x20	/* BMC/ESM3 */
#define BMC_LUN			0

struct ipmi_i2c_softc {
	struct ipmi_softc sc;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;
	uint8_t		sc_rev;
};

void	cmn_buildmsg(struct ipmi_cmd *);
int	ssif_sendmsg(struct ipmi_cmd *);
int	ssif_recvmsg(struct ipmi_cmd *);
int	ssif_reset(struct ipmi_softc *);
int	ssif_probe(struct ipmi_softc *);

struct ipmi_if ssif_if = {
	"SSIF",
	0,
	cmn_buildmsg,
	ssif_sendmsg,
	ssif_recvmsg,
	ssif_reset,
	ssif_probe,
	IPMI_MSG_DATASND,
	IPMI_MSG_DATARCV
};

int	ipmi_i2c_match(struct device *, void *, void *);
void	ipmi_i2c_attach(struct device *, struct device *, void *);

const struct cfattach ipmi_i2c_ca = {
	sizeof(struct ipmi_i2c_softc), ipmi_i2c_match, ipmi_i2c_attach,
	NULL, ipmi_activate
};

int	ipmi_i2c_get_interface_caps(struct ipmi_i2c_softc *);
int	ipmi_i2c_get_device_id(struct ipmi_i2c_softc *);

int
ipmi_i2c_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return (strcmp(ia->ia_name, "IPI0001") == 0 ||
	    strcmp(ia->ia_name, "APMC0D8A") == 0);
}

void
ipmi_i2c_attach(struct device *parent, struct device *self, void *aux)
{
	struct ipmi_i2c_softc *sc = (struct ipmi_i2c_softc *)self;
	struct i2c_attach_args *ia = aux;
	struct ipmi_attach_args iaa;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc.sc_if = &ssif_if;

	if (ipmi_i2c_get_interface_caps(sc)) {
		printf(": can't get system interface capabilities\n");
		return;
	}

	if (ipmi_i2c_get_device_id(sc)) {
		printf(": can't get system interface capabilities\n");
		return;
	}

	memset(&iaa, 0, sizeof(iaa));
	iaa.iaa_if_type = IPMI_IF_SSIF;
	iaa.iaa_if_rev = (sc->sc_rev >> 4 | sc->sc_rev << 4);
	iaa.iaa_if_irq = -1;
	ipmi_attach_common(&sc->sc, &iaa);
}

int
ipmi_i2c_get_interface_caps(struct ipmi_i2c_softc *sc)
{
	struct ipmi_cmd c;
	uint8_t data[5];

	data[0] = 0;		/* SSIF */

	c.c_sc = &sc->sc;
	c.c_rssa = BMC_SA;
	c.c_rslun = BMC_LUN;
	c.c_netfn = APP_NETFN;
	c.c_cmd = APP_GET_SYSTEM_INTERFACE_CAPS;
	c.c_txlen = 1;
	c.c_rxlen = 0;
	c.c_maxrxlen = sizeof(data);
	c.c_data = data;
	if (ipmi_sendcmd(&c) || ipmi_recvcmd(&c))
		return EIO;

	/* Check SSIF version number. */
	if ((data[1] & 0x7) != 0)
		return EINVAL;
	/* Check input and output message sizes. */
	if (data[2] < 32 || data[3] < 32)
		return EINVAL;

	return 0;
}

int
ipmi_i2c_get_device_id(struct ipmi_i2c_softc *sc)
{
	struct ipmi_cmd c;
	uint8_t data[16];

	c.c_sc = &sc->sc;
	c.c_rssa = BMC_SA;
	c.c_rslun = BMC_LUN;
	c.c_netfn = APP_NETFN;
	c.c_cmd = APP_GET_DEVICE_ID;
	c.c_txlen = 0;
	c.c_rxlen = 0;
	c.c_maxrxlen = sizeof(data);
	c.c_data = data;
	if (ipmi_sendcmd(&c) || ipmi_recvcmd(&c))
		return EIO;

	sc->sc_rev = data[4];
	return 0;
}

int
ssif_sendmsg(struct ipmi_cmd *c)
{
	struct ipmi_i2c_softc *sc = (struct ipmi_i2c_softc *)c->c_sc;
	uint8_t *buf = sc->sc.sc_buf;
	uint8_t cmd[2];
	int error, retry;

	/* We only support single-part writes. */
	if (c->c_txlen > 32)
		return -1;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd[0] = 2;
	cmd[1] = c->c_txlen;
	for (retry = 0; retry < 5; retry++) {
		error = iic_exec(sc->sc_tag, I2C_OP_WRITE_BLOCK,
		    sc->sc_addr, cmd, sizeof(cmd), buf, c->c_txlen, 0);
		if (!error)
			break;
	}

	iic_release_bus(sc->sc_tag, 0);

	return (error ? -1 : 0);
}

int
ssif_recvmsg(struct ipmi_cmd *c)
{
	struct ipmi_i2c_softc *sc = (struct ipmi_i2c_softc *)c->c_sc;
	uint8_t buf[33];
	uint8_t cmd[1];
	uint8_t len;
	int error, retry;
	int blkno = 0;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd[0] = 3;
	for (retry = 0; retry < 250; retry++) {
		memset(buf, 0, sizeof(buf));
		error = iic_exec(sc->sc_tag, I2C_OP_READ_BLOCK,
		    sc->sc_addr, cmd, sizeof(cmd), buf, sizeof(buf), 0);
		if (error)
			continue;

		if (buf[0] < 1 || buf[0] > 32) {
			error = EIO;
			goto release;
		}

		if (buf[0] == 32 && buf[1] == 0x00 && buf[2] == 0x01) {
			/* Multi-part read start. */
			c->c_rxlen = MIN(30, c->c_maxrxlen);
			memcpy(sc->sc.sc_buf, &buf[3], c->c_rxlen);
			break;
		} else {
			/* Single-part read. */
			c->c_rxlen = MIN(buf[0], c->c_maxrxlen);
			memcpy(sc->sc.sc_buf, &buf[1], c->c_rxlen);
			goto release;
		}
	}
	if (retry == 250)
		goto release;

	cmd[0] = 9;
	while (buf[1] != 0xff && c->c_rxlen < c->c_maxrxlen) {
		memset(buf, 0, sizeof(buf));
		error = iic_exec(sc->sc_tag, I2C_OP_READ_BLOCK,
		    sc->sc_addr, cmd, sizeof(cmd), buf, sizeof(buf), 0);
		if (error)
			goto release;

		if (buf[0] < 1 || buf[0] > 32) {
			error = EIO;
			goto release;
		}

		if (buf[0] == 32 && buf[1] == blkno) {
			/* Multi-part read middle. */
			len = MIN(31, c->c_maxrxlen - c->c_rxlen);
			memcpy(&sc->sc.sc_buf[c->c_rxlen], &buf[2], len);
		} else if (buf[1] == 0xff) {
			/* Multi-part read end. */
			len = MIN(buf[0] - 1, c->c_maxrxlen - c->c_rxlen);
			memcpy(&sc->sc.sc_buf[c->c_rxlen], &buf[2], len);
		} else {
			error = EIO;
			goto release;
		}
		c->c_rxlen += len;
		blkno++;
	}

release:
	iic_release_bus(sc->sc_tag, 0);

	return (error ? -1 : 0);
}

int
ssif_reset(struct ipmi_softc *sc)
{
	return -1;
}

int
ssif_probe(struct ipmi_softc *sc)
{
	return 0;
}
