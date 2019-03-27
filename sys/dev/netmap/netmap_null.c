/*
 * Copyright (C) 2018 Giuseppe Lettieri
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
#include <sys/errno.h>
#include <sys/param.h>	/* defines used in kernel.h */
#include <sys/kernel.h>	/* types used in module initialization */
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/socket.h> /* sockaddrs */
#include <net/if.h>
#include <net/if_var.h>
#include <machine/bus.h>	/* bus_dmamap_* */
#include <sys/refcount.h>


#elif defined(linux)

#include "bsd_glue.h"

#elif defined(__APPLE__)

#warning OSX support is only partial
#include "osx_glue.h"

#elif defined(_WIN32)
#include "win_glue.h"

#else

#error	Unsupported platform

#endif /* unsupported */

/*
 * common headers
 */

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_mem2.h>

#ifdef WITH_NMNULL

static int
netmap_null_sync(struct netmap_kring *kring, int flags)
{
	(void)kring;
	(void)flags;
	return 0;
}

static int
netmap_null_krings_create(struct netmap_adapter *na)
{
	return netmap_krings_create(na, 0);
}

static int
netmap_null_reg(struct netmap_adapter *na, int onoff)
{
	if (na->active_fds == 0) {
		if (onoff)
			na->na_flags |= NAF_NETMAP_ON;
		else
			na->na_flags &= ~NAF_NETMAP_ON;
	}
	return 0;
}

static int
netmap_null_bdg_attach(const char *name, struct netmap_adapter *na,
		struct nm_bridge *b)
{
	(void)name;
	(void)na;
	(void)b;
	return EINVAL;
}

int
netmap_get_null_na(struct nmreq_header *hdr, struct netmap_adapter **na,
		struct netmap_mem_d *nmd, int create)
{
	struct nmreq_register *req = (struct nmreq_register *)(uintptr_t)hdr->nr_body;
	struct netmap_null_adapter *nna;
	int error;

	if (req->nr_mode != NR_REG_NULL) {
		nm_prdis("not a null port");
		return 0;
	}

	if (!create) {
		nm_prerr("null ports cannot be re-opened");
		return EINVAL;
	}

	if (nmd == NULL) {
		nm_prerr("null ports must use an existing allocator");
		return EINVAL;
	}

	nna = nm_os_malloc(sizeof(*nna));
	if (nna == NULL) {
		error = ENOMEM;
		goto err;
	}
	snprintf(nna->up.name, sizeof(nna->up.name), "null:%s", hdr->nr_name);

	nna->up.nm_txsync = netmap_null_sync;
	nna->up.nm_rxsync = netmap_null_sync;
	nna->up.nm_register = netmap_null_reg;
	nna->up.nm_krings_create = netmap_null_krings_create;
	nna->up.nm_krings_delete = netmap_krings_delete;
	nna->up.nm_bdg_attach = netmap_null_bdg_attach;
	nna->up.nm_mem = netmap_mem_get(nmd);

	nna->up.num_tx_rings = req->nr_tx_rings;
	nna->up.num_rx_rings = req->nr_rx_rings;
	nna->up.num_tx_desc = req->nr_tx_slots;
	nna->up.num_rx_desc = req->nr_rx_slots;
	error = netmap_attach_common(&nna->up);
	if (error)
		goto free_nna;
	*na = &nna->up;
	netmap_adapter_get(*na);
	nm_prdis("created null %s", nna->up.name);

	return 0;

free_nna:
	nm_os_free(nna);
err:
	return error;
}


#endif /* WITH_NMNULL */
