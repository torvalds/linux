/*
 * ng_bt3c_pccard.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_bt3c_pccard.c,v 1.5 2003/04/01 18:15:21 max Exp $
 * $FreeBSD$
 *
 * XXX XXX XX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX 
 *
 * Based on information obrained from: Jose Orlando Pereira <jop@di.uminho.pt>
 * and disassembled w2k driver.
 *
 * XXX XXX XX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX 
 *
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <machine/bus.h>

#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>

#include <machine/resource.h>
#include <sys/rman.h>

#include <sys/socket.h>
#include <net/if.h>
#include <net/if_var.h>

#include <dev/pccard/pccardreg.h>
#include <dev/pccard/pccardvar.h>
#include "pccarddevs.h"

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_bt3c.h>
#include <netgraph/bluetooth/drivers/bt3c/ng_bt3c_var.h>

/* Netgraph methods */
static ng_constructor_t	ng_bt3c_constructor;
static ng_shutdown_t	ng_bt3c_shutdown;
static ng_newhook_t	ng_bt3c_newhook;
static ng_connect_t	ng_bt3c_connect;
static ng_disconnect_t	ng_bt3c_disconnect;
static ng_rcvmsg_t	ng_bt3c_rcvmsg;
static ng_rcvdata_t	ng_bt3c_rcvdata;

/* PCMCIA driver methods */
static int	bt3c_pccard_probe	(device_t);
static int	bt3c_pccard_attach	(device_t);
static int	bt3c_pccard_detach	(device_t);

static void	bt3c_intr		(void *);
static void	bt3c_receive		(bt3c_softc_p);

static void	bt3c_swi_intr		(void *);
static void	bt3c_forward		(node_p, hook_p, void *, int);
static void	bt3c_send		(node_p, hook_p, void *, int);

static void	bt3c_download_firmware	(bt3c_softc_p, char const *, int);

#define	bt3c_set_address(sc, address) \
do { \
	bus_space_write_1((sc)->iot, (sc)->ioh, BT3C_ADDR_L, ((address) & 0xff)); \
	bus_space_write_1((sc)->iot, (sc)->ioh, BT3C_ADDR_H, (((address) >> 8) & 0xff)); \
} while (0)

#define	bt3c_read_data(sc, data) \
do { \
	(data)  = bus_space_read_1((sc)->iot, (sc)->ioh, BT3C_DATA_L); \
	(data) |= ((bus_space_read_1((sc)->iot, (sc)->ioh, BT3C_DATA_H) & 0xff) << 8); \
} while (0)

#define	bt3c_write_data(sc, data) \
do { \
	bus_space_write_1((sc)->iot, (sc)->ioh, BT3C_DATA_L, ((data) & 0xff)); \
	bus_space_write_1((sc)->iot, (sc)->ioh, BT3C_DATA_H, (((data) >> 8) & 0xff)); \
} while (0)

#define	bt3c_read_control(sc, data) \
do { \
	(data) = bus_space_read_1((sc)->iot, (sc)->ioh, BT3C_CONTROL); \
} while (0)

#define	bt3c_write_control(sc, data) \
do { \
	bus_space_write_1((sc)->iot, (sc)->ioh, BT3C_CONTROL, (data)); \
} while (0)

#define bt3c_read(sc, address, data) \
do { \
	bt3c_set_address((sc), (address)); \
	bt3c_read_data((sc), (data)); \
} while(0)

#define bt3c_write(sc, address, data) \
do { \
	bt3c_set_address((sc), (address)); \
	bt3c_write_data((sc), (data)); \
} while(0)

static MALLOC_DEFINE(M_BT3C, "bt3c", "bt3c data structures");
	
/****************************************************************************
 ****************************************************************************
 **                           Netgraph specific
 ****************************************************************************
 ****************************************************************************/

/*
 * Netgraph node type
 */

/* Queue length */
static const struct ng_parse_struct_field	ng_bt3c_node_qlen_type_fields[] =
{
	{ "queue", &ng_parse_int32_type, },
	{ "qlen",  &ng_parse_int32_type, },
	{ NULL, }
};
static const struct ng_parse_type		ng_bt3c_node_qlen_type = {
	&ng_parse_struct_type,
	&ng_bt3c_node_qlen_type_fields
};

/* Stat info */
static const struct ng_parse_struct_field	ng_bt3c_node_stat_type_fields[] =
{
	{ "pckts_recv", &ng_parse_uint32_type, },
	{ "bytes_recv", &ng_parse_uint32_type, },
	{ "pckts_sent", &ng_parse_uint32_type, },
	{ "bytes_sent", &ng_parse_uint32_type, },
	{ "oerrors",    &ng_parse_uint32_type, },
	{ "ierrors",    &ng_parse_uint32_type, },
	{ NULL, }
};
static const struct ng_parse_type		ng_bt3c_node_stat_type = {
	&ng_parse_struct_type,
	&ng_bt3c_node_stat_type_fields
};

static const struct ng_cmdlist	ng_bt3c_cmdlist[] = {
{
	NGM_BT3C_COOKIE,
	NGM_BT3C_NODE_GET_STATE,
	"get_state",
	NULL,
	&ng_parse_uint16_type
},
{
	NGM_BT3C_COOKIE,
	NGM_BT3C_NODE_SET_DEBUG,
	"set_debug",
	&ng_parse_uint16_type,
	NULL
},
{
	NGM_BT3C_COOKIE,
	NGM_BT3C_NODE_GET_DEBUG,
	"get_debug",
	NULL,
	&ng_parse_uint16_type
},
{
	NGM_BT3C_COOKIE,
	NGM_BT3C_NODE_GET_QLEN,
	"get_qlen",
	NULL,
	&ng_bt3c_node_qlen_type
},
{
	NGM_BT3C_COOKIE,
	NGM_BT3C_NODE_SET_QLEN,
	"set_qlen",
	&ng_bt3c_node_qlen_type,
	NULL
},
{
	NGM_BT3C_COOKIE,
	NGM_BT3C_NODE_GET_STAT,
	"get_stat",
	NULL,
	&ng_bt3c_node_stat_type
},
{
	NGM_BT3C_COOKIE,
	NGM_BT3C_NODE_RESET_STAT,
	"reset_stat",
	NULL,
	NULL
},
{ 0, }
};

static struct ng_type	typestruct = {
	.version =	NG_ABI_VERSION,
	.name = 	NG_BT3C_NODE_TYPE,
	.constructor = 	ng_bt3c_constructor,
	.rcvmsg =	ng_bt3c_rcvmsg,
	.shutdown = 	ng_bt3c_shutdown,
	.newhook =	ng_bt3c_newhook,
	.connect =	ng_bt3c_connect,
	.rcvdata =	ng_bt3c_rcvdata,
	.disconnect =	ng_bt3c_disconnect,
        .cmdlist =	ng_bt3c_cmdlist	
};

/*
 * Netgraph node constructor. Do not allow to create node of this type.
 */

static int
ng_bt3c_constructor(node_p node)
{
	return (EINVAL);
} /* ng_bt3c_constructor */

/*
 * Netgraph node destructor. Destroy node only when device has been detached
 */

static int
ng_bt3c_shutdown(node_p node)
{
	bt3c_softc_p	sc = (bt3c_softc_p) NG_NODE_PRIVATE(node);

	/* Let old node go */
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);

	/* Create new fresh one if we are not going down */
	if (sc == NULL)
		goto out;

	/* Create new Netgraph node */
	if (ng_make_node_common(&typestruct, &sc->node) != 0) {
		device_printf(sc->dev, "Could not create Netgraph node\n");
		sc->node = NULL;
		goto out;
	}

	/* Name new Netgraph node */
	if (ng_name_node(sc->node,  device_get_nameunit(sc->dev)) != 0) {
		device_printf(sc->dev, "Could not name Netgraph node\n");
		NG_NODE_UNREF(sc->node);
		sc->node = NULL;
		goto out;
	}

	NG_NODE_SET_PRIVATE(sc->node, sc);
out:
	return (0);
} /* ng_bt3c_shutdown */

/*
 * Create new hook. There can only be one.
 */

static int
ng_bt3c_newhook(node_p node, hook_p hook, char const *name)
{
	bt3c_softc_p	sc = (bt3c_softc_p) NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_BT3C_HOOK) != 0)
		return (EINVAL);

	if (sc->hook != NULL)
		return (EISCONN);

	sc->hook = hook;

	return (0);
} /* ng_bt3c_newhook */

/*
 * Connect hook. Say YEP, that's OK with me.
 */

static int
ng_bt3c_connect(hook_p hook)
{
	bt3c_softc_p	sc = (bt3c_softc_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook != sc->hook) {
		sc->hook = NULL;
		return (EINVAL);
	}

	/* set the hook into queueing mode (for incoming (from wire) packets) */
	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));

	return (0);
} /* ng_bt3c_connect */

/*
 * Disconnect hook
 */

static int
ng_bt3c_disconnect(hook_p hook)
{
	bt3c_softc_p	sc = (bt3c_softc_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	/*
	 * We need to check for sc != NULL because we can be called from
	 * bt3c_pccard_detach() via ng_rmnode_self()
	 */

	if (sc != NULL) {
		if (hook != sc->hook)
			return (EINVAL);

		IF_DRAIN(&sc->inq);
		IF_DRAIN(&sc->outq);

		sc->hook = NULL;
	}

	return (0);
} /* ng_bt3c_disconnect */

/*
 * Process control message
 */

static int
ng_bt3c_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	bt3c_softc_p	 sc = (bt3c_softc_p) NG_NODE_PRIVATE(node);
	struct ng_mesg	*msg = NULL, *rsp = NULL;
	int		 error = 0;

	if (sc == NULL) {
		NG_FREE_ITEM(item);
		return (EHOSTDOWN);
	}

	NGI_GET_MSG(item, msg);

	switch (msg->header.typecookie) {
	case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TEXT_STATUS:
			NG_MKRESPONSE(rsp, msg, NG_TEXTRESPONSE, M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				snprintf(rsp->data, NG_TEXTRESPONSE,
					"Hook: %s\n" \
					"Flags: %#x\n" \
					"Debug: %d\n"  \
					"State: %d\n"  \
					"IncmQ: [len:%d,max:%d]\n" \
					"OutgQ: [len:%d,max:%d]\n",
					(sc->hook != NULL)? NG_BT3C_HOOK : "",
					sc->flags,
					sc->debug,
					sc->state,
					_IF_QLEN(&sc->inq), /* XXX */
					sc->inq.ifq_maxlen, /* XXX */
					_IF_QLEN(&sc->outq), /* XXX */
					sc->outq.ifq_maxlen /* XXX */
					);
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	case NGM_BT3C_COOKIE:
		switch (msg->header.cmd) {
		case NGM_BT3C_NODE_GET_STATE:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_bt3c_node_state_ep),
				M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				*((ng_bt3c_node_state_ep *)(rsp->data)) = 
					sc->state;
			break;

		case NGM_BT3C_NODE_SET_DEBUG:
			if (msg->header.arglen != sizeof(ng_bt3c_node_debug_ep))
				error = EMSGSIZE;
			else
				sc->debug =
					*((ng_bt3c_node_debug_ep *)(msg->data));
			break;

		case NGM_BT3C_NODE_GET_DEBUG:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_bt3c_node_debug_ep),
				M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				*((ng_bt3c_node_debug_ep *)(rsp->data)) = 
					sc->debug;
			break;

		case NGM_BT3C_NODE_GET_QLEN:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_bt3c_node_qlen_ep),
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			switch (((ng_bt3c_node_qlen_ep *)(msg->data))->queue) {
			case NGM_BT3C_NODE_IN_QUEUE:
				((ng_bt3c_node_qlen_ep *)(rsp->data))->queue =
					NGM_BT3C_NODE_IN_QUEUE;
				((ng_bt3c_node_qlen_ep *)(rsp->data))->qlen =
					sc->inq.ifq_maxlen;
				break;

			case NGM_BT3C_NODE_OUT_QUEUE:
				((ng_bt3c_node_qlen_ep *)(rsp->data))->queue =
					NGM_BT3C_NODE_OUT_QUEUE;
				((ng_bt3c_node_qlen_ep *)(rsp->data))->qlen =
					sc->outq.ifq_maxlen;
				break;

			default:
				NG_FREE_MSG(rsp);
				error = EINVAL;
				break;
			}
			break;

		case NGM_BT3C_NODE_SET_QLEN:
			if (msg->header.arglen != sizeof(ng_bt3c_node_qlen_ep)){
				error = EMSGSIZE;
				break;
			}

			if (((ng_bt3c_node_qlen_ep *)(msg->data))->qlen <= 0) {
				error = EINVAL;
				break;
			}

			switch (((ng_bt3c_node_qlen_ep *)(msg->data))->queue) {
			case NGM_BT3C_NODE_IN_QUEUE:
				sc->inq.ifq_maxlen = ((ng_bt3c_node_qlen_ep *)
					(msg->data))->qlen; /* XXX */
				break;

			case NGM_BT3C_NODE_OUT_QUEUE:
				sc->outq.ifq_maxlen = ((ng_bt3c_node_qlen_ep *)
					(msg->data))->qlen; /* XXX */
				break;

			default:
				error = EINVAL;
				break;
			}
			break;

		case NGM_BT3C_NODE_GET_STAT:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_bt3c_node_stat_ep),
				M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				bcopy(&sc->stat, rsp->data,
					sizeof(ng_bt3c_node_stat_ep));
			break;

		case NGM_BT3C_NODE_RESET_STAT:
			NG_BT3C_STAT_RESET(sc->stat);
			break;

		case NGM_BT3C_NODE_DOWNLOAD_FIRMWARE:
			if (msg->header.arglen < 
					sizeof(ng_bt3c_firmware_block_ep))
				error = EMSGSIZE;
			else	
				bt3c_download_firmware(sc, msg->data,
							msg->header.arglen);
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	default:
		error = EINVAL;
		break;
	}

	NG_RESPOND_MSG(error, node, item, rsp);
	NG_FREE_MSG(msg);

	return (error);
} /* ng_bt3c_rcvmsg */

/*
 * Process data
 */

static int
ng_bt3c_rcvdata(hook_p hook, item_p item)
{
	bt3c_softc_p	 sc = (bt3c_softc_p)NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf	*m = NULL;
	int		 error = 0;

	if (sc == NULL) {
		error = EHOSTDOWN;
		goto out;
	}

	if (hook != sc->hook) {
		error = EINVAL;
		goto out;
	}

	NGI_GET_M(item, m);

	IF_LOCK(&sc->outq);
	if (_IF_QFULL(&sc->outq)) {
		NG_BT3C_ERR(sc->dev,
"Outgoing queue is full. Dropping mbuf, len=%d\n", m->m_pkthdr.len);

		NG_BT3C_STAT_OERROR(sc->stat);

		NG_FREE_M(m);
	} else 
		_IF_ENQUEUE(&sc->outq, m);
	IF_UNLOCK(&sc->outq);

	error = ng_send_fn(sc->node, NULL, bt3c_send, NULL, 0 /* new send */);
out:
        NG_FREE_ITEM(item);

	return (error);
} /* ng_bt3c_rcvdata */

/****************************************************************************
 ****************************************************************************
 **                         PCMCIA driver specific
 ****************************************************************************
 ****************************************************************************/

/*
 * PC Card (PCMCIA) probe routine
 */

static struct pccard_product const	bt3c_pccard_products[] = {
	PCMCIA_CARD(3COM, 3CRWB609),
	{ NULL, }
};

static int
bt3c_pccard_probe(device_t dev)
{
	struct pccard_product const	*pp = NULL;

	pp = pccard_product_lookup(dev, bt3c_pccard_products,
			sizeof(bt3c_pccard_products[0]), NULL);
	if (pp == NULL)
		return (ENXIO);

	device_set_desc(dev, pp->pp_name);

	return (0);
} /* bt3c_pccard_probe */

/*
 * PC Card (PCMCIA) attach routine
 */

static int
bt3c_pccard_attach(device_t dev)
{
	bt3c_softc_p	sc = (bt3c_softc_p) device_get_softc(dev);

	/* Allocate I/O ports */
	sc->iobase_rid = 0;
	sc->iobase = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
			&sc->iobase_rid, 8, RF_ACTIVE);
	if (sc->iobase == NULL) {
		device_printf(dev, "Could not allocate I/O ports\n");
		goto bad;
	}
	sc->iot = rman_get_bustag(sc->iobase);
	sc->ioh = rman_get_bushandle(sc->iobase);

	/* Allocate IRQ */
	sc->irq_rid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
			RF_ACTIVE);
	if (sc->irq == NULL) {
		device_printf(dev, "Could not allocate IRQ\n");
		goto bad;
	}

	sc->irq_cookie = NULL;
	if (bus_setup_intr(dev, sc->irq, INTR_TYPE_TTY, NULL, bt3c_intr, sc,
			&sc->irq_cookie) != 0) {
		device_printf(dev, "Could not setup ISR\n");
		goto bad;
	}

	/* Attach handler to TTY SWI thread */
	sc->ith = NULL;
	if (swi_add(&tty_intr_event, device_get_nameunit(dev),
			bt3c_swi_intr, sc, SWI_TTY, 0, &sc->ith) < 0) {
		device_printf(dev, "Could not setup SWI ISR\n");
		goto bad;
	}

	/* Create Netgraph node */
	if (ng_make_node_common(&typestruct, &sc->node) != 0) {
		device_printf(dev, "Could not create Netgraph node\n");
		sc->node = NULL;
		goto bad;
	}

	/* Name Netgraph node */
	if (ng_name_node(sc->node, device_get_nameunit(dev)) != 0) {
		device_printf(dev, "Could not name Netgraph node\n");
		NG_NODE_UNREF(sc->node);
		sc->node = NULL;
		goto bad;
	}

	sc->dev = dev;
	sc->debug = NG_BT3C_WARN_LEVEL;

	sc->inq.ifq_maxlen = sc->outq.ifq_maxlen = BT3C_DEFAULTQLEN;
	mtx_init(&sc->inq.ifq_mtx, "BT3C inq", NULL, MTX_DEF);
	mtx_init(&sc->outq.ifq_mtx, "BT3C outq", NULL, MTX_DEF);

	sc->state = NG_BT3C_W4_PKT_IND;
	sc->want = 1;

	NG_NODE_SET_PRIVATE(sc->node, sc);

	return (0);
bad:
	if (sc->ith != NULL) {
		swi_remove(sc->ith);
		sc->ith = NULL;
	}

	if (sc->irq != NULL) {
		if (sc->irq_cookie != NULL)
			bus_teardown_intr(dev, sc->irq, sc->irq_cookie);

		bus_release_resource(dev, SYS_RES_IRQ,
			sc->irq_rid, sc->irq);

		sc->irq = NULL;
		sc->irq_rid = 0;
	}

	if (sc->iobase != NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT,
			sc->iobase_rid, sc->iobase);

		sc->iobase = NULL;
		sc->iobase_rid = 0;
	}

	return (ENXIO);
} /* bt3c_pccacd_attach */

/*
 * PC Card (PCMCIA) detach routine
 */

static int
bt3c_pccard_detach(device_t dev)
{
	bt3c_softc_p	sc = (bt3c_softc_p) device_get_softc(dev);

	if (sc == NULL)
		return (0);

	swi_remove(sc->ith);
	sc->ith = NULL;

	bus_teardown_intr(dev, sc->irq, sc->irq_cookie);
	bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq);
	sc->irq_cookie = NULL;
	sc->irq = NULL;
	sc->irq_rid = 0;

	bus_release_resource(dev, SYS_RES_IOPORT, sc->iobase_rid, sc->iobase);
	sc->iobase = NULL;
	sc->iobase_rid = 0;

	if (sc->node != NULL) {
		NG_NODE_SET_PRIVATE(sc->node, NULL);
		ng_rmnode_self(sc->node);
		sc->node = NULL;
	}

	NG_FREE_M(sc->m);
	IF_DRAIN(&sc->inq);
	IF_DRAIN(&sc->outq);

	mtx_destroy(&sc->inq.ifq_mtx);
	mtx_destroy(&sc->outq.ifq_mtx);

	return (0);
} /* bt3c_pccacd_detach */

/*
 * Interrupt service routine's
 */

static void
bt3c_intr(void *context)
{
	bt3c_softc_p	sc = (bt3c_softc_p) context;
	u_int16_t	control, status;

	if (sc == NULL || sc->ith == NULL) {
		printf("%s: bogus interrupt\n", NG_BT3C_NODE_TYPE);
		return;
	}

	bt3c_read_control(sc, control);
	if ((control & 0x80) == 0)
		return;

	bt3c_read(sc, 0x7001, status);
	NG_BT3C_INFO(sc->dev, "control=%#x, status=%#x\n", control, status);

	if ((status & 0xff) == 0x7f || (status & 0xff) == 0xff) {
		NG_BT3C_WARN(sc->dev, "Strange status=%#x\n", status);
		return;
	}

	/* Receive complete */
	if (status & 0x0001)
		bt3c_receive(sc);

	/* Record status and schedule SWI */
	sc->status |= status;
	swi_sched(sc->ith, 0);

	/* Complete interrupt */
	bt3c_write(sc, 0x7001, 0x0000);
	bt3c_write_control(sc, control);
} /* bt3c_intr */

/*
 * Receive data
 */

static void
bt3c_receive(bt3c_softc_p sc)
{
	u_int16_t	i, count, c;

	/* Receive data from the card */
	bt3c_read(sc, 0x7006, count);
	NG_BT3C_INFO(sc->dev, "The card has %d characters\n", count);

	bt3c_set_address(sc, 0x7480);

	for (i = 0; i < count; i++) {
		/* Allocate new mbuf if needed */
		if (sc->m == NULL) {
			sc->state = NG_BT3C_W4_PKT_IND;
			sc->want = 1;

			MGETHDR(sc->m, M_NOWAIT, MT_DATA);
			if (sc->m == NULL) {
				NG_BT3C_ERR(sc->dev, "Could not get mbuf\n");
				NG_BT3C_STAT_IERROR(sc->stat);

				break; /* XXX lost of sync */
			}

			if (!(MCLGET(sc->m, M_NOWAIT))) {
				NG_FREE_M(sc->m);

				NG_BT3C_ERR(sc->dev, "Could not get cluster\n");
				NG_BT3C_STAT_IERROR(sc->stat);

				break; /* XXX lost of sync */
			}

			sc->m->m_len = sc->m->m_pkthdr.len = 0;
		}

		/* Read and append character to mbuf */
		bt3c_read_data(sc, c);
		if (sc->m->m_pkthdr.len >= MCLBYTES) {
			NG_BT3C_ERR(sc->dev, "Oversized frame\n");
	
			NG_FREE_M(sc->m);
			sc->state = NG_BT3C_W4_PKT_IND;
			sc->want = 1;

			break; /* XXX lost of sync */
		}

		mtod(sc->m, u_int8_t *)[sc->m->m_len ++] = (u_int8_t) c;
		sc->m->m_pkthdr.len ++;

		NG_BT3C_INFO(sc->dev,
"Got char %#x, want=%d, got=%d\n", c, sc->want, sc->m->m_pkthdr.len);

		if (sc->m->m_pkthdr.len < sc->want)
			continue; /* wait for more */

		switch (sc->state) {
		/* Got packet indicator */
		case NG_BT3C_W4_PKT_IND:
			NG_BT3C_INFO(sc->dev,
"Got packet indicator %#x\n", *mtod(sc->m, u_int8_t *));

			sc->state = NG_BT3C_W4_PKT_HDR;

			/*
			 * Since packet indicator included in the packet 
			 * header just set sc->want to sizeof(packet header).
			 */

			switch (*mtod(sc->m, u_int8_t *)) {
			case NG_HCI_ACL_DATA_PKT:
				sc->want = sizeof(ng_hci_acldata_pkt_t);
				break;

			case NG_HCI_SCO_DATA_PKT:
				sc->want = sizeof(ng_hci_scodata_pkt_t);
				break;

			case NG_HCI_EVENT_PKT:
				sc->want = sizeof(ng_hci_event_pkt_t);
				break;

			default:
       	                	NG_BT3C_ERR(sc->dev,
"Ignoring unknown packet type=%#x\n", *mtod(sc->m, u_int8_t *));

				NG_BT3C_STAT_IERROR(sc->stat);

				NG_FREE_M(sc->m);
				sc->state = NG_BT3C_W4_PKT_IND;
				sc->want = 1;
				break;
			}
			break;

		/* Got packet header */
		case NG_BT3C_W4_PKT_HDR:
			sc->state = NG_BT3C_W4_PKT_DATA;

			switch (*mtod(sc->m, u_int8_t *)) {
			case NG_HCI_ACL_DATA_PKT:
				c = le16toh(mtod(sc->m,
					ng_hci_acldata_pkt_t *)->length);
				break;

			case NG_HCI_SCO_DATA_PKT:
				c = mtod(sc->m, ng_hci_scodata_pkt_t*)->length;
				break;

			case NG_HCI_EVENT_PKT:
				c = mtod(sc->m, ng_hci_event_pkt_t *)->length;
				break;

			default:
				KASSERT(0,
("Invalid packet type=%#x\n", *mtod(sc->m, u_int8_t *)));
				break;
       	        	 }

			NG_BT3C_INFO(sc->dev,
"Got packet header, packet type=%#x, got so far %d, payload size=%d\n",
				*mtod(sc->m, u_int8_t *), sc->m->m_pkthdr.len,
				c);

			if (c > 0) {
				sc->want += c;
				break;
			}

			/* else FALLTHROUGH and deliver frame */
			/* XXX is this true? should we deliver empty frame? */

		/* Got packet data */
		case NG_BT3C_W4_PKT_DATA:
			NG_BT3C_INFO(sc->dev,
"Got full packet, packet type=%#x, packet size=%d\n",
				*mtod(sc->m, u_int8_t *), sc->m->m_pkthdr.len);

			NG_BT3C_STAT_BYTES_RECV(sc->stat, sc->m->m_pkthdr.len);
			NG_BT3C_STAT_PCKTS_RECV(sc->stat);

			IF_LOCK(&sc->inq);
			if (_IF_QFULL(&sc->inq)) {
				NG_BT3C_ERR(sc->dev,
"Incoming queue is full. Dropping mbuf, len=%d\n", sc->m->m_pkthdr.len);

				NG_BT3C_STAT_IERROR(sc->stat);

				NG_FREE_M(sc->m);
			} else {
				_IF_ENQUEUE(&sc->inq, sc->m);
				sc->m = NULL;
			}
			IF_UNLOCK(&sc->inq);

			sc->state = NG_BT3C_W4_PKT_IND;
			sc->want = 1;
			break;

		default:
			KASSERT(0,
("Invalid node state=%d", sc->state));
			break;
		}
	}

	bt3c_write(sc, 0x7006, 0x0000);
} /* bt3c_receive */

/*
 * SWI interrupt handler
 * Netgraph part is handled via ng_send_fn() to avoid race with hook
 * connection/disconnection
 */

static void
bt3c_swi_intr(void *context)
{
	bt3c_softc_p	sc = (bt3c_softc_p) context;
	u_int16_t	data;

	/* Receive complete */
	if (sc->status & 0x0001) {
		sc->status &= ~0x0001; /* XXX is it safe? */

		if (ng_send_fn(sc->node, NULL, &bt3c_forward, NULL, 0) != 0)
			NG_BT3C_ALERT(sc->dev, "Could not forward frames!\n");
	}

	/* Send complete */
	if (sc->status & 0x0002) {
		sc->status &= ~0x0002; /* XXX is it safe */

		if (ng_send_fn(sc->node, NULL, &bt3c_send, NULL, 1) != 0)
			NG_BT3C_ALERT(sc->dev, "Could not send frames!\n");
	}

	/* Antenna position */
	if (sc->status & 0x0020) { 
		sc->status &= ~0x0020; /* XXX is it safe */

		bt3c_read(sc, 0x7002, data);
		data &= 0x10;

		if (data)
			sc->flags |= BT3C_ANTENNA_OUT;
		else
			sc->flags &= ~BT3C_ANTENNA_OUT;

		NG_BT3C_INFO(sc->dev, "Antenna %s\n", data? "OUT" : "IN");
	}
} /* bt3c_swi_intr */

/*
 * Send all incoming frames to the upper layer
 */

static void
bt3c_forward(node_p node, hook_p hook, void *arg1, int arg2)
{
	bt3c_softc_p	 sc = (bt3c_softc_p) NG_NODE_PRIVATE(node);
	struct mbuf	*m = NULL;
	int		 error;

	if (sc == NULL)
		return;

	if (sc->hook != NULL && NG_HOOK_IS_VALID(sc->hook)) {
		for (;;) {
			IF_DEQUEUE(&sc->inq, m);
			if (m == NULL)
				break;

			NG_SEND_DATA_ONLY(error, sc->hook, m);
			if (error != 0)
				NG_BT3C_STAT_IERROR(sc->stat);
		}
	} else {
		IF_LOCK(&sc->inq);
		for (;;) {
			_IF_DEQUEUE(&sc->inq, m);
			if (m == NULL)
				break;

			NG_BT3C_STAT_IERROR(sc->stat);
			NG_FREE_M(m);
		}
		IF_UNLOCK(&sc->inq);
	}
} /* bt3c_forward */

/*
 * Send more data to the device. Must be called when node is locked
 */

static void
bt3c_send(node_p node, hook_p hook, void *arg, int completed)
{
	bt3c_softc_p	 sc = (bt3c_softc_p) NG_NODE_PRIVATE(node);
	struct mbuf	*m = NULL;
	int		 i, wrote, len;

	if (sc == NULL)
		return;

	if (completed)
		sc->flags &= ~BT3C_XMIT;

	if (sc->flags & BT3C_XMIT)
		return;

	bt3c_set_address(sc, 0x7080);

	for (wrote = 0; wrote < BT3C_FIFO_SIZE; ) {
		IF_DEQUEUE(&sc->outq, m);
		if (m == NULL)
			break;

		while (m != NULL) {
			len = min((BT3C_FIFO_SIZE - wrote), m->m_len);

			for (i = 0; i < len; i++)
				bt3c_write_data(sc, m->m_data[i]);

			wrote += len;
			m->m_data += len;
			m->m_len -= len;

			if (m->m_len > 0)
				break;

			m = m_free(m);
		}

		if (m != NULL) {
			IF_PREPEND(&sc->outq, m);
			break;
		}

		NG_BT3C_STAT_PCKTS_SENT(sc->stat);
	}

	if (wrote > 0) {
		NG_BT3C_INFO(sc->dev, "Wrote %d bytes\n", wrote);
		NG_BT3C_STAT_BYTES_SENT(sc->stat, wrote);

		bt3c_write(sc, 0x7005, wrote);
		sc->flags |= BT3C_XMIT;
	}
} /* bt3c_send */

/*
 * Download chip firmware
 */

static void
bt3c_download_firmware(bt3c_softc_p sc, char const *firmware, int firmware_size)
{
	ng_bt3c_firmware_block_ep const	*block = NULL;
	u_int16_t const			*data = NULL;
	int				 i, size;
	u_int8_t			 c;

	/* Reset */
	device_printf(sc->dev, "Reseting the card...\n");
	bt3c_write(sc, 0x8040, 0x0404);
	bt3c_write(sc, 0x8040, 0x0400);
	DELAY(1);

	bt3c_write(sc, 0x8040, 0x0404);
	DELAY(17);

	/* Download firmware */
	device_printf(sc->dev, "Starting firmware download process...\n");

	for (size = 0; size < firmware_size; ) {
		block = (ng_bt3c_firmware_block_ep const *)(firmware + size);
		data = (u_int16_t const *)(block + 1);

		if (bootverbose)
			device_printf(sc->dev, "Download firmware block, " \
				"address=%#08x, size=%d words, aligment=%d\n",
				block->block_address, block->block_size,
				block->block_alignment);

		bt3c_set_address(sc, block->block_address);
		for (i = 0; i < block->block_size; i++)
			bt3c_write_data(sc, data[i]);

		size += (sizeof(*block) + (block->block_size * 2) + 
				block->block_alignment);
	}

	DELAY(17);
	device_printf(sc->dev, "Firmware download process complete\n");

	/* Boot */
	device_printf(sc->dev, "Starting the card...\n");
	bt3c_set_address(sc, 0x3000);
	bt3c_read_control(sc, c);
	bt3c_write_control(sc, (c | 0x40));
	DELAY(17);

	/* Clear registers */
	device_printf(sc->dev, "Clearing card registers...\n");
	bt3c_write(sc, 0x7006, 0x0000);
	bt3c_write(sc, 0x7005, 0x0000);
	bt3c_write(sc, 0x7001, 0x0000);
	DELAY(1000);
} /* bt3c_download_firmware */

/****************************************************************************
 ****************************************************************************
 **                           Driver module
 ****************************************************************************
 ****************************************************************************/

/*
 * PC Card (PCMCIA) driver
 */

static device_method_t	bt3c_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bt3c_pccard_probe),
	DEVMETHOD(device_attach,	bt3c_pccard_attach),
	DEVMETHOD(device_detach,	bt3c_pccard_detach),

	{ 0, 0 }
};

static driver_t		bt3c_pccard_driver = {
	NG_BT3C_NODE_TYPE,
	bt3c_pccard_methods,
	sizeof(bt3c_softc_t)
};

static devclass_t	bt3c_devclass;

 
/*
 * Load/Unload the driver module
 */
 
static int
bt3c_modevent(module_t mod, int event, void *data)
{
	int	error;
 
	switch (event) {
	case MOD_LOAD:
		error = ng_newtype(&typestruct);
		if (error != 0)
			printf("%s: Could not register Netgraph node type, " \
				"error=%d\n", NG_BT3C_NODE_TYPE, error);
		break;

	case MOD_UNLOAD:
		error = ng_rmtype(&typestruct);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
} /* bt3c_modevent */

DRIVER_MODULE(bt3c, pccard, bt3c_pccard_driver, bt3c_devclass, bt3c_modevent,0);
MODULE_VERSION(ng_bt3c, NG_BLUETOOTH_VERSION);
MODULE_DEPEND(ng_bt3c, netgraph, NG_ABI_VERSION, NG_ABI_VERSION,NG_ABI_VERSION);
PCCARD_PNP_INFO(bt3c_pccard_products);
