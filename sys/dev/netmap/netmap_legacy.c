/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2018 Vincenzo Maffione
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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

/* $FreeBSD$ */

#if defined(__FreeBSD__)
#include <sys/cdefs.h> /* prerequisite */
#include <sys/types.h>
#include <sys/param.h>	/* defines used in kernel.h */
#include <sys/filio.h>	/* FIONBIO */
#include <sys/malloc.h>
#include <sys/socketvar.h>	/* struct socket */
#include <sys/socket.h> /* sockaddrs */
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/bpf.h>		/* BIOCIMMEDIATE */
#include <machine/bus.h>	/* bus_dmamap_* */
#include <sys/endian.h>
#elif defined(linux)
#include "bsd_glue.h"
#elif defined(__APPLE__)
#warning OSX support is only partial
#include "osx_glue.h"
#elif defined (_WIN32)
#include "win_glue.h"
#endif

/*
 * common headers
 */
#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_bdg.h>

static int
nmreq_register_from_legacy(struct nmreq *nmr, struct nmreq_header *hdr,
				struct nmreq_register *req)
{
	req->nr_offset = nmr->nr_offset;
	req->nr_memsize = nmr->nr_memsize;
	req->nr_tx_slots = nmr->nr_tx_slots;
	req->nr_rx_slots = nmr->nr_rx_slots;
	req->nr_tx_rings = nmr->nr_tx_rings;
	req->nr_rx_rings = nmr->nr_rx_rings;
	req->nr_host_tx_rings = 0;
	req->nr_host_rx_rings = 0;
	req->nr_mem_id = nmr->nr_arg2;
	req->nr_ringid = nmr->nr_ringid & NETMAP_RING_MASK;
	if ((nmr->nr_flags & NR_REG_MASK) == NR_REG_DEFAULT) {
		/* Convert the older nmr->nr_ringid (original
		 * netmap control API) to nmr->nr_flags. */
		u_int regmode = NR_REG_DEFAULT;
		if (req->nr_ringid & NETMAP_SW_RING) {
			regmode = NR_REG_SW;
		} else if (req->nr_ringid & NETMAP_HW_RING) {
			regmode = NR_REG_ONE_NIC;
		} else {
			regmode = NR_REG_ALL_NIC;
		}
		req->nr_mode = regmode;
	} else {
		req->nr_mode = nmr->nr_flags & NR_REG_MASK;
	}

	/* Fix nr_name, nr_mode and nr_ringid to handle pipe requests. */
	if (req->nr_mode == NR_REG_PIPE_MASTER ||
			req->nr_mode == NR_REG_PIPE_SLAVE) {
		char suffix[10];
		snprintf(suffix, sizeof(suffix), "%c%d",
			(req->nr_mode == NR_REG_PIPE_MASTER ? '{' : '}'),
			req->nr_ringid);
		if (strlen(hdr->nr_name) + strlen(suffix)
					>= sizeof(hdr->nr_name)) {
			/* No space for the pipe suffix. */
			return ENOBUFS;
		}
		strncat(hdr->nr_name, suffix, strlen(suffix));
		req->nr_mode = NR_REG_ALL_NIC;
		req->nr_ringid = 0;
	}
	req->nr_flags = nmr->nr_flags & (~NR_REG_MASK);
	if (nmr->nr_ringid & NETMAP_NO_TX_POLL) {
		req->nr_flags |= NR_NO_TX_POLL;
	}
	if (nmr->nr_ringid & NETMAP_DO_RX_POLL) {
		req->nr_flags |= NR_DO_RX_POLL;
	}
	/* nmr->nr_arg1 (nr_pipes) ignored */
	req->nr_extra_bufs = nmr->nr_arg3;

	return 0;
}

/* Convert the legacy 'nmr' struct into one of the nmreq_xyz structs
 * (new API). The new struct is dynamically allocated. */
static struct nmreq_header *
nmreq_from_legacy(struct nmreq *nmr, u_long ioctl_cmd)
{
	struct nmreq_header *hdr = nm_os_malloc(sizeof(*hdr));

	if (hdr == NULL) {
		goto oom;
	}

	/* Sanitize nmr->nr_name by adding the string terminator. */
	if (ioctl_cmd == NIOCGINFO || ioctl_cmd == NIOCREGIF) {
		nmr->nr_name[sizeof(nmr->nr_name) - 1] = '\0';
	}

	/* First prepare the request header. */
	hdr->nr_version = NETMAP_API; /* new API */
	strlcpy(hdr->nr_name, nmr->nr_name, sizeof(nmr->nr_name));
	hdr->nr_options = (uintptr_t)NULL;
	hdr->nr_body = (uintptr_t)NULL;

	switch (ioctl_cmd) {
	case NIOCREGIF: {
		switch (nmr->nr_cmd) {
		case 0: {
			/* Regular NIOCREGIF operation. */
			struct nmreq_register *req = nm_os_malloc(sizeof(*req));
			if (!req) { goto oom; }
			hdr->nr_body = (uintptr_t)req;
			hdr->nr_reqtype = NETMAP_REQ_REGISTER;
			if (nmreq_register_from_legacy(nmr, hdr, req)) {
				goto oom;
			}
			break;
		}
		case NETMAP_BDG_ATTACH: {
			struct nmreq_vale_attach *req = nm_os_malloc(sizeof(*req));
			if (!req) { goto oom; }
			hdr->nr_body = (uintptr_t)req;
			hdr->nr_reqtype = NETMAP_REQ_VALE_ATTACH;
			if (nmreq_register_from_legacy(nmr, hdr, &req->reg)) {
				goto oom;
			}
			/* Fix nr_mode, starting from nr_arg1. */
			if (nmr->nr_arg1 & NETMAP_BDG_HOST) {
				req->reg.nr_mode = NR_REG_NIC_SW;
			} else {
				req->reg.nr_mode = NR_REG_ALL_NIC;
			}
			break;
		}
		case NETMAP_BDG_DETACH: {
			hdr->nr_reqtype = NETMAP_REQ_VALE_DETACH;
			hdr->nr_body = (uintptr_t)nm_os_malloc(sizeof(struct nmreq_vale_detach));
			break;
		}
		case NETMAP_BDG_VNET_HDR:
		case NETMAP_VNET_HDR_GET: {
			struct nmreq_port_hdr *req = nm_os_malloc(sizeof(*req));
			if (!req) { goto oom; }
			hdr->nr_body = (uintptr_t)req;
			hdr->nr_reqtype = (nmr->nr_cmd == NETMAP_BDG_VNET_HDR) ?
				NETMAP_REQ_PORT_HDR_SET : NETMAP_REQ_PORT_HDR_GET;
			req->nr_hdr_len = nmr->nr_arg1;
			break;
		}
		case NETMAP_BDG_NEWIF : {
			struct nmreq_vale_newif *req = nm_os_malloc(sizeof(*req));
			if (!req) { goto oom; }
			hdr->nr_body = (uintptr_t)req;
			hdr->nr_reqtype = NETMAP_REQ_VALE_NEWIF;
			req->nr_tx_slots = nmr->nr_tx_slots;
			req->nr_rx_slots = nmr->nr_rx_slots;
			req->nr_tx_rings = nmr->nr_tx_rings;
			req->nr_rx_rings = nmr->nr_rx_rings;
			req->nr_mem_id = nmr->nr_arg2;
			break;
		}
		case NETMAP_BDG_DELIF: {
			hdr->nr_reqtype = NETMAP_REQ_VALE_DELIF;
			break;
		}
		case NETMAP_BDG_POLLING_ON:
		case NETMAP_BDG_POLLING_OFF: {
			struct nmreq_vale_polling *req = nm_os_malloc(sizeof(*req));
			if (!req) { goto oom; }
			hdr->nr_body = (uintptr_t)req;
			hdr->nr_reqtype = (nmr->nr_cmd == NETMAP_BDG_POLLING_ON) ?
				NETMAP_REQ_VALE_POLLING_ENABLE :
				NETMAP_REQ_VALE_POLLING_DISABLE;
			switch (nmr->nr_flags & NR_REG_MASK) {
			default:
				req->nr_mode = 0; /* invalid */
				break;
			case NR_REG_ONE_NIC:
				req->nr_mode = NETMAP_POLLING_MODE_MULTI_CPU;
				break;
			case NR_REG_ALL_NIC:
				req->nr_mode = NETMAP_POLLING_MODE_SINGLE_CPU;
				break;
			}
			req->nr_first_cpu_id = nmr->nr_ringid & NETMAP_RING_MASK;
			req->nr_num_polling_cpus = nmr->nr_arg1;
			break;
		}
		case NETMAP_PT_HOST_CREATE:
		case NETMAP_PT_HOST_DELETE: {
			nm_prerr("Netmap passthrough not supported yet");
			return NULL;
			break;
		}
		}
		break;
	}
	case NIOCGINFO: {
		if (nmr->nr_cmd == NETMAP_BDG_LIST) {
			struct nmreq_vale_list *req = nm_os_malloc(sizeof(*req));
			if (!req) { goto oom; }
			hdr->nr_body = (uintptr_t)req;
			hdr->nr_reqtype = NETMAP_REQ_VALE_LIST;
			req->nr_bridge_idx = nmr->nr_arg1;
			req->nr_port_idx = nmr->nr_arg2;
		} else {
			/* Regular NIOCGINFO. */
			struct nmreq_port_info_get *req = nm_os_malloc(sizeof(*req));
			if (!req) { goto oom; }
			hdr->nr_body = (uintptr_t)req;
			hdr->nr_reqtype = NETMAP_REQ_PORT_INFO_GET;
			req->nr_memsize = nmr->nr_memsize;
			req->nr_tx_slots = nmr->nr_tx_slots;
			req->nr_rx_slots = nmr->nr_rx_slots;
			req->nr_tx_rings = nmr->nr_tx_rings;
			req->nr_rx_rings = nmr->nr_rx_rings;
			req->nr_host_tx_rings = 0;
			req->nr_host_rx_rings = 0;
			req->nr_mem_id = nmr->nr_arg2;
		}
		break;
	}
	}

	return hdr;
oom:
	if (hdr) {
		if (hdr->nr_body) {
			nm_os_free((void *)(uintptr_t)hdr->nr_body);
		}
		nm_os_free(hdr);
	}
	nm_prerr("Failed to allocate memory for nmreq_xyz struct");

	return NULL;
}

static void
nmreq_register_to_legacy(const struct nmreq_register *req, struct nmreq *nmr)
{
	nmr->nr_offset = req->nr_offset;
	nmr->nr_memsize = req->nr_memsize;
	nmr->nr_tx_slots = req->nr_tx_slots;
	nmr->nr_rx_slots = req->nr_rx_slots;
	nmr->nr_tx_rings = req->nr_tx_rings;
	nmr->nr_rx_rings = req->nr_rx_rings;
	nmr->nr_arg2 = req->nr_mem_id;
	nmr->nr_arg3 = req->nr_extra_bufs;
}

/* Convert a nmreq_xyz struct (new API) to the legacy 'nmr' struct.
 * It also frees the nmreq_xyz struct, as it was allocated by
 * nmreq_from_legacy(). */
static int
nmreq_to_legacy(struct nmreq_header *hdr, struct nmreq *nmr)
{
	int ret = 0;

	/* We only write-back the fields that the user expects to be
	 * written back. */
	switch (hdr->nr_reqtype) {
	case NETMAP_REQ_REGISTER: {
		struct nmreq_register *req =
			(struct nmreq_register *)(uintptr_t)hdr->nr_body;
		nmreq_register_to_legacy(req, nmr);
		break;
	}
	case NETMAP_REQ_PORT_INFO_GET: {
		struct nmreq_port_info_get *req =
			(struct nmreq_port_info_get *)(uintptr_t)hdr->nr_body;
		nmr->nr_memsize = req->nr_memsize;
		nmr->nr_tx_slots = req->nr_tx_slots;
		nmr->nr_rx_slots = req->nr_rx_slots;
		nmr->nr_tx_rings = req->nr_tx_rings;
		nmr->nr_rx_rings = req->nr_rx_rings;
		nmr->nr_arg2 = req->nr_mem_id;
		break;
	}
	case NETMAP_REQ_VALE_ATTACH: {
		struct nmreq_vale_attach *req =
			(struct nmreq_vale_attach *)(uintptr_t)hdr->nr_body;
		nmreq_register_to_legacy(&req->reg, nmr);
		break;
	}
	case NETMAP_REQ_VALE_DETACH: {
		break;
	}
	case NETMAP_REQ_VALE_LIST: {
		struct nmreq_vale_list *req =
			(struct nmreq_vale_list *)(uintptr_t)hdr->nr_body;
		strlcpy(nmr->nr_name, hdr->nr_name, sizeof(nmr->nr_name));
		nmr->nr_arg1 = req->nr_bridge_idx;
		nmr->nr_arg2 = req->nr_port_idx;
		break;
	}
	case NETMAP_REQ_PORT_HDR_SET:
	case NETMAP_REQ_PORT_HDR_GET: {
		struct nmreq_port_hdr *req =
			(struct nmreq_port_hdr *)(uintptr_t)hdr->nr_body;
		nmr->nr_arg1 = req->nr_hdr_len;
		break;
	}
	case NETMAP_REQ_VALE_NEWIF: {
		struct nmreq_vale_newif *req =
			(struct nmreq_vale_newif *)(uintptr_t)hdr->nr_body;
		nmr->nr_tx_slots = req->nr_tx_slots;
		nmr->nr_rx_slots = req->nr_rx_slots;
		nmr->nr_tx_rings = req->nr_tx_rings;
		nmr->nr_rx_rings = req->nr_rx_rings;
		nmr->nr_arg2 = req->nr_mem_id;
		break;
	}
	case NETMAP_REQ_VALE_DELIF:
	case NETMAP_REQ_VALE_POLLING_ENABLE:
	case NETMAP_REQ_VALE_POLLING_DISABLE: {
		break;
	}
	}

	return ret;
}

int
netmap_ioctl_legacy(struct netmap_priv_d *priv, u_long cmd, caddr_t data,
			struct thread *td)
{
	int error = 0;

	switch (cmd) {
	case NIOCGINFO:
	case NIOCREGIF: {
		/* Request for the legacy control API. Convert it to a
		 * NIOCCTRL request. */
		struct nmreq *nmr = (struct nmreq *) data;
		struct nmreq_header *hdr;

		if (nmr->nr_version < 14) {
			nm_prerr("Minimum supported API is 14 (requested %u)",
			    nmr->nr_version);
			return EINVAL;
		}
		hdr = nmreq_from_legacy(nmr, cmd);
		if (hdr == NULL) { /* out of memory */
			return ENOMEM;
		}
		error = netmap_ioctl(priv, NIOCCTRL, (caddr_t)hdr, td,
					/*nr_body_is_user=*/0);
		if (error == 0) {
			nmreq_to_legacy(hdr, nmr);
		}
		if (hdr->nr_body) {
			nm_os_free((void *)(uintptr_t)hdr->nr_body);
		}
		nm_os_free(hdr);
		break;
	}
#ifdef WITH_VALE
	case NIOCCONFIG: {
		struct nm_ifreq *nr = (struct nm_ifreq *)data;
		error = netmap_bdg_config(nr);
		break;
	}
#endif
#ifdef __FreeBSD__
	case FIONBIO:
	case FIOASYNC:
		/* FIONBIO/FIOASYNC are no-ops. */
		break;

	case BIOCIMMEDIATE:
	case BIOCGHDRCMPLT:
	case BIOCSHDRCMPLT:
	case BIOCSSEESENT:
		/* Ignore these commands. */
		break;

	default:	/* allow device-specific ioctls */
	    {
		struct nmreq *nmr = (struct nmreq *)data;
		struct ifnet *ifp = ifunit_ref(nmr->nr_name);
		if (ifp == NULL) {
			error = ENXIO;
		} else {
			struct socket so;

			bzero(&so, sizeof(so));
			so.so_vnet = ifp->if_vnet;
			// so->so_proto not null.
			error = ifioctl(&so, cmd, data, td);
			if_rele(ifp);
		}
		break;
	    }

#else /* linux */
	default:
		error = EOPNOTSUPP;
#endif /* linux */
	}

	return error;
}
