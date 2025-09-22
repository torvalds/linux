/*	$OpenBSD: ipmi_opal.c,v 1.4 2024/10/09 00:38:26 jsg Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/fdt.h>
#include <machine/opal.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/ipmivar.h>

struct ipmi_opal_softc {
	struct ipmi_softc sc;
	int		sc_id;
};

void	ipmi_opal_buildmsg(struct ipmi_cmd *);
int	ipmi_opal_sendmsg(struct ipmi_cmd *);
int	ipmi_opal_recvmsg(struct ipmi_cmd *);
int	ipmi_opal_reset(struct ipmi_softc *);
int	ipmi_opal_probe(struct ipmi_softc *);

#define IPMI_OPALMSG_VERSION		0
#define IPMI_OPALMSG_NFLN		1
#define IPMI_OPALMSG_CMD		2
#define IPMI_OPALMSG_CCODE		3
#define IPMI_OPALMSG_DATASND		3
#define IPMI_OPALMSG_DATARCV		4

struct ipmi_if opal_if = {
	"OPAL",
	0,
	ipmi_opal_buildmsg,
	ipmi_opal_sendmsg,
	ipmi_opal_recvmsg,
	ipmi_opal_reset,
	ipmi_opal_probe,
	IPMI_OPALMSG_DATASND,
	IPMI_OPALMSG_DATARCV
};

int	ipmi_opal_match(struct device *, void *, void *);
void	ipmi_opal_attach(struct device *, struct device *, void *);

const struct cfattach ipmi_opal_ca = {
	sizeof (struct ipmi_opal_softc), ipmi_opal_match, ipmi_opal_attach,
	NULL, ipmi_activate
};

int
ipmi_opal_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ibm,opal-ipmi");
}

void
ipmi_opal_attach(struct device *parent, struct device *self, void *aux)
{
	struct ipmi_opal_softc *sc = (struct ipmi_opal_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct ipmi_attach_args iaa;
	uint64_t size;
	int64_t error;

	sc->sc.sc_if = &opal_if;
	sc->sc_id = OF_getpropint(faa->fa_node, "ibm,ipmi-interface-id", 0);

	/* Clear IPMI message queue. */
	do {
		size = sizeof(sc->sc.sc_buf);
		error = opal_ipmi_recv(sc->sc_id,
		    opal_phys(sc->sc.sc_buf), opal_phys(&size));
	} while (error == OPAL_SUCCESS);

	memset(&iaa, 0, sizeof(iaa));
	iaa.iaa_if_type = IPMI_IF_SSIF;
	iaa.iaa_if_rev = 0x20;
	iaa.iaa_if_irq = -1;
	ipmi_attach_common(&sc->sc, &iaa);
}

#define RSSA_MASK 0xff
#define LUN_MASK 0x3
#define NETFN_LUN(nf,ln) (((nf) << 2) | ((ln) & LUN_MASK))

void
ipmi_opal_buildmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	struct opal_ipmi_msg *msg = (struct opal_ipmi_msg *)sc->sc_buf;

	msg->version = OPAL_IPMI_MSG_FORMAT_VERSION_1;
	msg->netfn = NETFN_LUN(c->c_netfn, c->c_rslun);
	msg->cmd = c->c_cmd;
	if (c->c_txlen && c->c_data)
		memcpy(msg->data, c->c_data, c->c_txlen);
}

int
ipmi_opal_sendmsg(struct ipmi_cmd *c)
{
	struct ipmi_opal_softc *sc = (struct ipmi_opal_softc *)c->c_sc;
	int64_t error;

	error = opal_ipmi_send(sc->sc_id,
	    opal_phys(sc->sc.sc_buf), c->c_txlen);

	return (error == OPAL_SUCCESS ? 0 : -1);
}

int
ipmi_opal_recvmsg(struct ipmi_cmd *c)
{
	struct ipmi_opal_softc *sc = (struct ipmi_opal_softc *)c->c_sc;
	struct opal_ipmi_msg *msg = (struct opal_ipmi_msg *)sc->sc.sc_buf;
	uint64_t size = sizeof(sc->sc.sc_buf);
	int64_t error;
	int timo;

	msg->version = OPAL_IPMI_MSG_FORMAT_VERSION_1;
	for (timo = 1000; timo > 0; timo--) {
		error = opal_ipmi_recv(sc->sc_id,
		    opal_phys(sc->sc.sc_buf), opal_phys(&size));
		if (error != OPAL_EMPTY)
			break;

		tsleep_nsec(sc, PWAIT, "ipmi", MSEC_TO_NSEC(1));
		opal_poll_events(NULL);
	}

	if (error == OPAL_SUCCESS) {
		if (msg->version != OPAL_IPMI_MSG_FORMAT_VERSION_1)
			return -1;
		
		sc->sc.sc_buf[IPMI_MSG_NFLN] = msg->netfn;
		sc->sc.sc_buf[IPMI_MSG_CMD] = msg->cmd;
		sc->sc.sc_buf[IPMI_MSG_CCODE] = msg->data[0];
		c->c_rxlen = MIN(size, c->c_maxrxlen);
	}

	return (error == OPAL_SUCCESS ? 0 : -1);
}

int
ipmi_opal_reset(struct ipmi_softc *sc)
{
	return -1;
}

int
ipmi_opal_probe(struct ipmi_softc *sc)
{
	return 0;
}
