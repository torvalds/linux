/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * Copyright (c) 2014-2017 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_frontend.c#4 $
 */
/*
 * CAM Target Layer front end interface code
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/endian.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/nv.h>
#include <sys/dnv.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_backend.h>
/* XXX KDM move defines from ctl_ioctl.h to somewhere else */
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_private.h>
#include <cam/ctl/ctl_debug.h>

extern struct ctl_softc *control_softc;

int
ctl_frontend_register(struct ctl_frontend *fe)
{
	struct ctl_softc *softc = control_softc;
	struct ctl_frontend *fe_tmp;
	int error;

	KASSERT(softc != NULL, ("CTL is not initialized"));

	/* Sanity check, make sure this isn't a duplicate registration. */
	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(fe_tmp, &softc->fe_list, links) {
		if (strcmp(fe_tmp->name, fe->name) == 0) {
			mtx_unlock(&softc->ctl_lock);
			return (-1);
		}
	}
	mtx_unlock(&softc->ctl_lock);
	STAILQ_INIT(&fe->port_list);

	/* Call the frontend's initialization routine. */
	if (fe->init != NULL) {
		if ((error = fe->init()) != 0) {
			printf("%s frontend init error: %d\n",
			    fe->name, error);
			return (error);
		}
	}

	mtx_lock(&softc->ctl_lock);
	softc->num_frontends++;
	STAILQ_INSERT_TAIL(&softc->fe_list, fe, links);
	mtx_unlock(&softc->ctl_lock);
	return (0);
}

int
ctl_frontend_deregister(struct ctl_frontend *fe)
{
	struct ctl_softc *softc = control_softc;
	int error;

	/* Call the frontend's shutdown routine.*/
	if (fe->shutdown != NULL) {
		if ((error = fe->shutdown()) != 0) {
			printf("%s frontend shutdown error: %d\n",
			    fe->name, error);
			return (error);
		}
	}

	mtx_lock(&softc->ctl_lock);
	STAILQ_REMOVE(&softc->fe_list, fe, ctl_frontend, links);
	softc->num_frontends--;
	mtx_unlock(&softc->ctl_lock);
	return (0);
}

struct ctl_frontend *
ctl_frontend_find(char *frontend_name)
{
	struct ctl_softc *softc = control_softc;
	struct ctl_frontend *fe;

	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(fe, &softc->fe_list, links) {
		if (strcmp(fe->name, frontend_name) == 0) {
			mtx_unlock(&softc->ctl_lock);
			return (fe);
		}
	}
	mtx_unlock(&softc->ctl_lock);
	return (NULL);
}

int
ctl_port_register(struct ctl_port *port)
{
	struct ctl_softc *softc = control_softc;
	struct ctl_port *tport, *nport;
	void *pool;
	int port_num;
	int retval;

	KASSERT(softc != NULL, ("CTL is not initialized"));
	port->ctl_softc = softc;

	mtx_lock(&softc->ctl_lock);
	if (port->targ_port >= 0)
		port_num = port->targ_port;
	else
		port_num = ctl_ffz(softc->ctl_port_mask,
		    softc->port_min, softc->port_max);
	if ((port_num < 0) ||
	    (ctl_set_mask(softc->ctl_port_mask, port_num) < 0)) {
		mtx_unlock(&softc->ctl_lock);
		return (1);
	}
	softc->num_ports++;
	mtx_unlock(&softc->ctl_lock);

	/*
	 * Initialize the initiator and portname mappings
	 */
	port->max_initiators = CTL_MAX_INIT_PER_PORT;
	port->wwpn_iid = malloc(sizeof(*port->wwpn_iid) * port->max_initiators,
	    M_CTL, M_NOWAIT | M_ZERO);
	if (port->wwpn_iid == NULL) {
		retval = ENOMEM;
		goto error;
	}

	/*
	 * We add 20 to whatever the caller requests, so he doesn't get
	 * burned by queueing things back to the pending sense queue.  In
	 * theory, there should probably only be one outstanding item, at
	 * most, on the pending sense queue for a LUN.  We'll clear the
	 * pending sense queue on the next command, whether or not it is
	 * a REQUEST SENSE.
	 */
	retval = ctl_pool_create(softc, port->port_name,
				 port->num_requested_ctl_io + 20, &pool);
	if (retval != 0) {
		free(port->wwpn_iid, M_CTL);
error:
		port->targ_port = -1;
		mtx_lock(&softc->ctl_lock);
		ctl_clear_mask(softc->ctl_port_mask, port_num);
		mtx_unlock(&softc->ctl_lock);
		return (retval);
	}
	port->targ_port = port_num;
	port->ctl_pool_ref = pool;
	if (port->options == NULL)
		port->options = nvlist_create(0);
	port->stats.item = port_num;
	mtx_init(&port->port_lock, "CTL port", NULL, MTX_DEF);

	mtx_lock(&softc->ctl_lock);
	STAILQ_INSERT_TAIL(&port->frontend->port_list, port, fe_links);
	for (tport = NULL, nport = STAILQ_FIRST(&softc->port_list);
	    nport != NULL && nport->targ_port < port_num;
	    tport = nport, nport = STAILQ_NEXT(tport, links)) {
	}
	if (tport)
		STAILQ_INSERT_AFTER(&softc->port_list, tport, port, links);
	else
		STAILQ_INSERT_HEAD(&softc->port_list, port, links);
	softc->ctl_ports[port->targ_port] = port;
	mtx_unlock(&softc->ctl_lock);

	return (retval);
}

int
ctl_port_deregister(struct ctl_port *port)
{
	struct ctl_softc *softc = port->ctl_softc;
	struct ctl_io_pool *pool = (struct ctl_io_pool *)port->ctl_pool_ref;
	int i;

	if (port->targ_port == -1)
		return (1);

	mtx_lock(&softc->ctl_lock);
	STAILQ_REMOVE(&softc->port_list, port, ctl_port, links);
	STAILQ_REMOVE(&port->frontend->port_list, port, ctl_port, fe_links);
	softc->num_ports--;
	ctl_clear_mask(softc->ctl_port_mask, port->targ_port);
	softc->ctl_ports[port->targ_port] = NULL;
	mtx_unlock(&softc->ctl_lock);

	ctl_pool_free(pool);
	nvlist_destroy(port->options);

	ctl_lun_map_deinit(port);
	free(port->port_devid, M_CTL);
	port->port_devid = NULL;
	free(port->target_devid, M_CTL);
	port->target_devid = NULL;
	free(port->init_devid, M_CTL);
	port->init_devid = NULL;
	for (i = 0; i < port->max_initiators; i++)
		free(port->wwpn_iid[i].name, M_CTL);
	free(port->wwpn_iid, M_CTL);
	mtx_destroy(&port->port_lock);

	return (0);
}

void
ctl_port_set_wwns(struct ctl_port *port, int wwnn_valid, uint64_t wwnn,
		      int wwpn_valid, uint64_t wwpn)
{
	struct scsi_vpd_id_descriptor *desc;
	int len, proto;

	if (port->port_type == CTL_PORT_FC)
		proto = SCSI_PROTO_FC << 4;
	else if (port->port_type == CTL_PORT_SAS)
		proto = SCSI_PROTO_SAS << 4;
	else if (port->port_type == CTL_PORT_ISCSI)
		proto = SCSI_PROTO_ISCSI << 4;
	else
		proto = SCSI_PROTO_SPI << 4;

	if (wwnn_valid) {
		port->wwnn = wwnn;

		free(port->target_devid, M_CTL);

		len = sizeof(struct scsi_vpd_device_id) + CTL_WWPN_LEN;
		port->target_devid = malloc(sizeof(struct ctl_devid) + len,
		    M_CTL, M_WAITOK | M_ZERO);
		port->target_devid->len = len;
		desc = (struct scsi_vpd_id_descriptor *)port->target_devid->data;
		desc->proto_codeset = proto | SVPD_ID_CODESET_BINARY;
		desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_TARGET |
		    SVPD_ID_TYPE_NAA;
		desc->length = CTL_WWPN_LEN;
		scsi_u64to8b(port->wwnn, desc->identifier);
	}

	if (wwpn_valid) {
		port->wwpn = wwpn;

		free(port->port_devid, M_CTL);

		len = sizeof(struct scsi_vpd_device_id) + CTL_WWPN_LEN;
		port->port_devid = malloc(sizeof(struct ctl_devid) + len,
		    M_CTL, M_WAITOK | M_ZERO);
		port->port_devid->len = len;
		desc = (struct scsi_vpd_id_descriptor *)port->port_devid->data;
		desc->proto_codeset = proto | SVPD_ID_CODESET_BINARY;
		desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_PORT |
		    SVPD_ID_TYPE_NAA;
		desc->length = CTL_WWPN_LEN;
		scsi_u64to8b(port->wwpn, desc->identifier);
	}
}

void
ctl_port_online(struct ctl_port *port)
{
	struct ctl_softc *softc = port->ctl_softc;
	struct ctl_lun *lun;
	const char *value;
	uint32_t l;

	if (port->lun_enable != NULL) {
		if (port->lun_map) {
			for (l = 0; l < port->lun_map_size; l++) {
				if (ctl_lun_map_from_port(port, l) ==
				    UINT32_MAX)
					continue;
				port->lun_enable(port->targ_lun_arg, l);
			}
		} else {
			STAILQ_FOREACH(lun, &softc->lun_list, links)
				port->lun_enable(port->targ_lun_arg, lun->lun);
		}
	}
	if (port->port_online != NULL)
		port->port_online(port->onoff_arg);
	mtx_lock(&softc->ctl_lock);
	if (softc->is_single == 0) {
		value = dnvlist_get_string(port->options, "ha_shared", NULL);
		if (value != NULL && strcmp(value, "on") == 0)
			port->status |= CTL_PORT_STATUS_HA_SHARED;
		else
			port->status &= ~CTL_PORT_STATUS_HA_SHARED;
	}
	port->status |= CTL_PORT_STATUS_ONLINE;
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		if (ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		mtx_lock(&lun->lun_lock);
		ctl_est_ua_all(lun, -1, CTL_UA_INQ_CHANGE);
		mtx_unlock(&lun->lun_lock);
	}
	mtx_unlock(&softc->ctl_lock);
	ctl_isc_announce_port(port);
}

void
ctl_port_offline(struct ctl_port *port)
{
	struct ctl_softc *softc = port->ctl_softc;
	struct ctl_lun *lun;
	uint32_t l;

	if (port->port_offline != NULL)
		port->port_offline(port->onoff_arg);
	if (port->lun_disable != NULL) {
		if (port->lun_map) {
			for (l = 0; l < port->lun_map_size; l++) {
				if (ctl_lun_map_from_port(port, l) ==
				    UINT32_MAX)
					continue;
				port->lun_disable(port->targ_lun_arg, l);
			}
		} else {
			STAILQ_FOREACH(lun, &softc->lun_list, links)
				port->lun_disable(port->targ_lun_arg, lun->lun);
		}
	}
	mtx_lock(&softc->ctl_lock);
	port->status &= ~CTL_PORT_STATUS_ONLINE;
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		if (ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		mtx_lock(&lun->lun_lock);
		ctl_est_ua_all(lun, -1, CTL_UA_INQ_CHANGE);
		mtx_unlock(&lun->lun_lock);
	}
	mtx_unlock(&softc->ctl_lock);
	ctl_isc_announce_port(port);
}

/*
 * vim: ts=8
 */
