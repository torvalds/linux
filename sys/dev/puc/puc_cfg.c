/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <dev/puc/puc_bus.h>
#include <dev/puc/puc_cfg.h>
#include <dev/puc/puc_bfe.h>

int
puc_config(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port, intptr_t *r)
{
	const struct puc_cfg *cfg = sc->sc_cfg;
	int error;

	if (cfg->config_function != NULL) {
		error = cfg->config_function(sc, cmd, port, r);
		if (!error)
			return (0);
	} else
		error = EDOOFUS;

	switch (cmd) {
	case PUC_CFG_GET_CLOCK:
		if (cfg->clock < 0)
			return (error);
		*r = cfg->clock;
		return (0);
	case PUC_CFG_GET_DESC:
		if (cfg->desc == NULL)
			return (error);
		*r = (intptr_t)cfg->desc;
		return (0);
	case PUC_CFG_GET_ILR:
		*r = PUC_ILR_NONE;
		return (0);
	case PUC_CFG_GET_LEN:
		/* The length of bus space needed by the port. */
		*r = 8;
		return (0);
	case PUC_CFG_GET_NPORTS:
		/* The number of ports on this card. */
		switch (cfg->ports) {
		case PUC_PORT_NONSTANDARD:
			return (error);
		case PUC_PORT_1P:
		case PUC_PORT_1S:
			*r = 1;
			return (0);
		case PUC_PORT_1S1P:
		case PUC_PORT_2P:
		case PUC_PORT_2S:
			*r = 2;
			return (0);
		case PUC_PORT_1S2P:
		case PUC_PORT_2S1P:
		case PUC_PORT_3S:
			*r = 3;
			return (0);
		case PUC_PORT_4S:
			*r = 4;
			return (0);
		case PUC_PORT_4S1P:
			*r = 5;
			return (0);
		case PUC_PORT_6S:
			*r = 6;
			return (0);
		case PUC_PORT_8S:
			*r = 8;
			return (0);
		case PUC_PORT_12S:
			*r = 12;
			return (0);
		case PUC_PORT_16S:
			*r = 16;
			return (0);
		}
		break;
	case PUC_CFG_GET_OFS:
		/* The offset relative to the RID. */
		if (cfg->d_ofs < 0)
			return (error);
		*r = port * cfg->d_ofs;
		return (0);
	case PUC_CFG_GET_RID:
		/* The RID for this port. */
		if (port == 0) {
			if (cfg->rid < 0)
				return (error);
			*r = cfg->rid;
			return (0);
		}
		if (cfg->d_rid < 0)
			return (error);
		if (cfg->rid < 0) {
			error = puc_config(sc, PUC_CFG_GET_RID, 0, r);
			if (error)
				return (error);
		} else
			*r = cfg->rid;
		*r += port * cfg->d_rid;
		return (0);
	case PUC_CFG_GET_TYPE:
		/* The type of this port. */
		if (cfg->ports == PUC_PORT_NONSTANDARD)
			return (error);
		switch (port) {
		case 0:
			if (cfg->ports == PUC_PORT_1P ||
			    cfg->ports == PUC_PORT_2P)
				*r = PUC_TYPE_PARALLEL;
			else
				*r = PUC_TYPE_SERIAL;
			return (0);
		case 1:
			if (cfg->ports == PUC_PORT_1S1P ||
			    cfg->ports == PUC_PORT_1S2P ||
			    cfg->ports == PUC_PORT_2P)
				*r = PUC_TYPE_PARALLEL;
			else
				*r = PUC_TYPE_SERIAL;
			return (0);
		case 2:
			if (cfg->ports == PUC_PORT_1S2P ||
			    cfg->ports == PUC_PORT_2S1P)
				*r = PUC_TYPE_PARALLEL;
			else
				*r = PUC_TYPE_SERIAL;
			return (0);
		case 4:
			if (cfg->ports == PUC_PORT_4S1P)
				*r = PUC_TYPE_PARALLEL;
			else
				*r = PUC_TYPE_SERIAL;
			return (0);
		}
		*r = PUC_TYPE_SERIAL;
		return (0);
	case PUC_CFG_SETUP:
		*r = ENXIO;
		return (0);
	}

	return (ENXIO);
}
