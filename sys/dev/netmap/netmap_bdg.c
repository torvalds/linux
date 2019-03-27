/*
 * Copyright (C) 2013-2016 Universita` di Pisa
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


/*
 * This module implements the VALE switch for netmap

--- VALE SWITCH ---

NMG_LOCK() serializes all modifications to switches and ports.
A switch cannot be deleted until all ports are gone.

For each switch, an SX lock (RWlock on linux) protects
deletion of ports. When configuring or deleting a new port, the
lock is acquired in exclusive mode (after holding NMG_LOCK).
When forwarding, the lock is acquired in shared mode (without NMG_LOCK).
The lock is held throughout the entire forwarding cycle,
during which the thread may incur in a page fault.
Hence it is important that sleepable shared locks are used.

On the rx ring, the per-port lock is grabbed initially to reserve
a number of slot in the ring, then the lock is released,
packets are copied from source to destination, and then
the lock is acquired again and the receive ring is updated.
(A similar thing is done on the tx ring for NIC and host stack
ports attached to the switch)

 */

/*
 * OS-specific code that is used only within this file.
 * Other OS-specific code that must be accessed by drivers
 * is present in netmap_kern.h
 */

#if defined(__FreeBSD__)
#include <sys/cdefs.h> /* prerequisite */
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>	/* defines used in kernel.h */
#include <sys/kernel.h>	/* types used in module initialization */
#include <sys/conf.h>	/* cdevsw struct, UID, GID */
#include <sys/sockio.h>
#include <sys/socketvar.h>	/* struct socket */
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/rwlock.h>
#include <sys/socket.h> /* sockaddrs */
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/bpf.h>		/* BIOCIMMEDIATE */
#include <machine/bus.h>	/* bus_dmamap_* */
#include <sys/endian.h>
#include <sys/refcount.h>
#include <sys/smp.h>


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

#include <dev/netmap/netmap_bdg.h>

const char*
netmap_bdg_name(struct netmap_vp_adapter *vp)
{
	struct nm_bridge *b = vp->na_bdg;
	if (b == NULL)
		return NULL;
	return b->bdg_basename;
}


#ifndef CONFIG_NET_NS
/*
 * XXX in principle nm_bridges could be created dynamically
 * Right now we have a static array and deletions are protected
 * by an exclusive lock.
 */
struct nm_bridge *nm_bridges;
#endif /* !CONFIG_NET_NS */


static int
nm_is_id_char(const char c)
{
	return (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') ||
	       (c == '_');
}

/* Validate the name of a bdg port and return the
 * position of the ":" character. */
static int
nm_bdg_name_validate(const char *name, size_t prefixlen)
{
	int colon_pos = -1;
	int i;

	if (!name || strlen(name) < prefixlen) {
		return -1;
	}

	for (i = 0; i < NM_BDG_IFNAMSIZ && name[i]; i++) {
		if (name[i] == ':') {
			colon_pos = i;
			break;
		} else if (!nm_is_id_char(name[i])) {
			return -1;
		}
	}

	if (strlen(name) - colon_pos > IFNAMSIZ) {
		/* interface name too long */
		return -1;
	}

	return colon_pos;
}

/*
 * locate a bridge among the existing ones.
 * MUST BE CALLED WITH NMG_LOCK()
 *
 * a ':' in the name terminates the bridge name. Otherwise, just NM_NAME.
 * We assume that this is called with a name of at least NM_NAME chars.
 */
struct nm_bridge *
nm_find_bridge(const char *name, int create, struct netmap_bdg_ops *ops)
{
	int i, namelen;
	struct nm_bridge *b = NULL, *bridges;
	u_int num_bridges;

	NMG_LOCK_ASSERT();

	netmap_bns_getbridges(&bridges, &num_bridges);

	namelen = nm_bdg_name_validate(name,
			(ops != NULL ? strlen(ops->name) : 0));
	if (namelen < 0) {
		nm_prerr("invalid bridge name %s", name ? name : NULL);
		return NULL;
	}

	/* lookup the name, remember empty slot if there is one */
	for (i = 0; i < num_bridges; i++) {
		struct nm_bridge *x = bridges + i;

		if ((x->bdg_flags & NM_BDG_ACTIVE) + x->bdg_active_ports == 0) {
			if (create && b == NULL)
				b = x;	/* record empty slot */
		} else if (x->bdg_namelen != namelen) {
			continue;
		} else if (strncmp(name, x->bdg_basename, namelen) == 0) {
			nm_prdis("found '%.*s' at %d", namelen, name, i);
			b = x;
			break;
		}
	}
	if (i == num_bridges && b) { /* name not found, can create entry */
		/* initialize the bridge */
		nm_prdis("create new bridge %s with ports %d", b->bdg_basename,
			b->bdg_active_ports);
		b->ht = nm_os_malloc(sizeof(struct nm_hash_ent) * NM_BDG_HASH);
		if (b->ht == NULL) {
			nm_prerr("failed to allocate hash table");
			return NULL;
		}
		strncpy(b->bdg_basename, name, namelen);
		b->bdg_namelen = namelen;
		b->bdg_active_ports = 0;
		for (i = 0; i < NM_BDG_MAXPORTS; i++)
			b->bdg_port_index[i] = i;
		/* set the default function */
		b->bdg_ops = b->bdg_saved_ops = *ops;
		b->private_data = b->ht;
		b->bdg_flags = 0;
		NM_BNS_GET(b);
	}
	return b;
}


int
netmap_bdg_free(struct nm_bridge *b)
{
	if ((b->bdg_flags & NM_BDG_ACTIVE) + b->bdg_active_ports != 0) {
		return EBUSY;
	}

	nm_prdis("marking bridge %s as free", b->bdg_basename);
	nm_os_free(b->ht);
	memset(&b->bdg_ops, 0, sizeof(b->bdg_ops));
	memset(&b->bdg_saved_ops, 0, sizeof(b->bdg_saved_ops));
	b->bdg_flags = 0;
	NM_BNS_PUT(b);
	return 0;
}

/* Called by external kernel modules (e.g., Openvswitch).
 * to modify the private data previously given to regops().
 * 'name' may be just bridge's name (including ':' if it
 * is not just NM_BDG_NAME).
 * Called without NMG_LOCK.
 */
int
netmap_bdg_update_private_data(const char *name, bdg_update_private_data_fn_t callback,
	void *callback_data, void *auth_token)
{
	void *private_data = NULL;
	struct nm_bridge *b;
	int error = 0;

	NMG_LOCK();
	b = nm_find_bridge(name, 0 /* don't create */, NULL);
	if (!b) {
		error = EINVAL;
		goto unlock_update_priv;
	}
	if (!nm_bdg_valid_auth_token(b, auth_token)) {
		error = EACCES;
		goto unlock_update_priv;
	}
	BDG_WLOCK(b);
	private_data = callback(b->private_data, callback_data, &error);
	b->private_data = private_data;
	BDG_WUNLOCK(b);

unlock_update_priv:
	NMG_UNLOCK();
	return error;
}



/* remove from bridge b the ports in slots hw and sw
 * (sw can be -1 if not needed)
 */
void
netmap_bdg_detach_common(struct nm_bridge *b, int hw, int sw)
{
	int s_hw = hw, s_sw = sw;
	int i, lim =b->bdg_active_ports;
	uint32_t *tmp = b->tmp_bdg_port_index;

	/*
	New algorithm:
	make a copy of bdg_port_index;
	lookup NA(ifp)->bdg_port and SWNA(ifp)->bdg_port
	in the array of bdg_port_index, replacing them with
	entries from the bottom of the array;
	decrement bdg_active_ports;
	acquire BDG_WLOCK() and copy back the array.
	 */

	if (netmap_debug & NM_DEBUG_BDG)
		nm_prinf("detach %d and %d (lim %d)", hw, sw, lim);
	/* make a copy of the list of active ports, update it,
	 * and then copy back within BDG_WLOCK().
	 */
	memcpy(b->tmp_bdg_port_index, b->bdg_port_index, sizeof(b->tmp_bdg_port_index));
	for (i = 0; (hw >= 0 || sw >= 0) && i < lim; ) {
		if (hw >= 0 && tmp[i] == hw) {
			nm_prdis("detach hw %d at %d", hw, i);
			lim--; /* point to last active port */
			tmp[i] = tmp[lim]; /* swap with i */
			tmp[lim] = hw;	/* now this is inactive */
			hw = -1;
		} else if (sw >= 0 && tmp[i] == sw) {
			nm_prdis("detach sw %d at %d", sw, i);
			lim--;
			tmp[i] = tmp[lim];
			tmp[lim] = sw;
			sw = -1;
		} else {
			i++;
		}
	}
	if (hw >= 0 || sw >= 0) {
		nm_prerr("delete failed hw %d sw %d, should panic...", hw, sw);
	}

	BDG_WLOCK(b);
	if (b->bdg_ops.dtor)
		b->bdg_ops.dtor(b->bdg_ports[s_hw]);
	b->bdg_ports[s_hw] = NULL;
	if (s_sw >= 0) {
		b->bdg_ports[s_sw] = NULL;
	}
	memcpy(b->bdg_port_index, b->tmp_bdg_port_index, sizeof(b->tmp_bdg_port_index));
	b->bdg_active_ports = lim;
	BDG_WUNLOCK(b);

	nm_prdis("now %d active ports", lim);
	netmap_bdg_free(b);
}


/* nm_bdg_ctl callback for VALE ports */
int
netmap_vp_bdg_ctl(struct nmreq_header *hdr, struct netmap_adapter *na)
{
	struct netmap_vp_adapter *vpna = (struct netmap_vp_adapter *)na;
	struct nm_bridge *b = vpna->na_bdg;

	if (hdr->nr_reqtype == NETMAP_REQ_VALE_ATTACH) {
		return 0; /* nothing to do */
	}
	if (b) {
		netmap_set_all_rings(na, 0 /* disable */);
		netmap_bdg_detach_common(b, vpna->bdg_port, -1);
		vpna->na_bdg = NULL;
		netmap_set_all_rings(na, 1 /* enable */);
	}
	/* I have took reference just for attach */
	netmap_adapter_put(na);
	return 0;
}

int
netmap_default_bdg_attach(const char *name, struct netmap_adapter *na,
		struct nm_bridge *b)
{
	return NM_NEED_BWRAP;
}

/* Try to get a reference to a netmap adapter attached to a VALE switch.
 * If the adapter is found (or is created), this function returns 0, a
 * non NULL pointer is returned into *na, and the caller holds a
 * reference to the adapter.
 * If an adapter is not found, then no reference is grabbed and the
 * function returns an error code, or 0 if there is just a VALE prefix
 * mismatch. Therefore the caller holds a reference when
 * (*na != NULL && return == 0).
 */
int
netmap_get_bdg_na(struct nmreq_header *hdr, struct netmap_adapter **na,
	struct netmap_mem_d *nmd, int create, struct netmap_bdg_ops *ops)
{
	char *nr_name = hdr->nr_name;
	const char *ifname;
	struct ifnet *ifp = NULL;
	int error = 0;
	struct netmap_vp_adapter *vpna, *hostna = NULL;
	struct nm_bridge *b;
	uint32_t i, j;
	uint32_t cand = NM_BDG_NOPORT, cand2 = NM_BDG_NOPORT;
	int needed;

	*na = NULL;     /* default return value */

	/* first try to see if this is a bridge port. */
	NMG_LOCK_ASSERT();
	if (strncmp(nr_name, ops->name, strlen(ops->name) - 1)) {
		return 0;  /* no error, but no VALE prefix */
	}

	b = nm_find_bridge(nr_name, create, ops);
	if (b == NULL) {
		nm_prdis("no bridges available for '%s'", nr_name);
		return (create ? ENOMEM : ENXIO);
	}
	if (strlen(nr_name) < b->bdg_namelen) /* impossible */
		panic("x");

	/* Now we are sure that name starts with the bridge's name,
	 * lookup the port in the bridge. We need to scan the entire
	 * list. It is not important to hold a WLOCK on the bridge
	 * during the search because NMG_LOCK already guarantees
	 * that there are no other possible writers.
	 */

	/* lookup in the local list of ports */
	for (j = 0; j < b->bdg_active_ports; j++) {
		i = b->bdg_port_index[j];
		vpna = b->bdg_ports[i];
		nm_prdis("checking %s", vpna->up.name);
		if (!strcmp(vpna->up.name, nr_name)) {
			netmap_adapter_get(&vpna->up);
			nm_prdis("found existing if %s refs %d", nr_name)
			*na = &vpna->up;
			return 0;
		}
	}
	/* not found, should we create it? */
	if (!create)
		return ENXIO;
	/* yes we should, see if we have space to attach entries */
	needed = 2; /* in some cases we only need 1 */
	if (b->bdg_active_ports + needed >= NM_BDG_MAXPORTS) {
		nm_prerr("bridge full %d, cannot create new port", b->bdg_active_ports);
		return ENOMEM;
	}
	/* record the next two ports available, but do not allocate yet */
	cand = b->bdg_port_index[b->bdg_active_ports];
	cand2 = b->bdg_port_index[b->bdg_active_ports + 1];
	nm_prdis("+++ bridge %s port %s used %d avail %d %d",
		b->bdg_basename, ifname, b->bdg_active_ports, cand, cand2);

	/*
	 * try see if there is a matching NIC with this name
	 * (after the bridge's name)
	 */
	ifname = nr_name + b->bdg_namelen + 1;
	ifp = ifunit_ref(ifname);
	if (!ifp) {
		/* Create an ephemeral virtual port.
		 * This block contains all the ephemeral-specific logic.
		 */

		if (hdr->nr_reqtype != NETMAP_REQ_REGISTER) {
			error = EINVAL;
			goto out;
		}

		/* bdg_netmap_attach creates a struct netmap_adapter */
		error = b->bdg_ops.vp_create(hdr, NULL, nmd, &vpna);
		if (error) {
			if (netmap_debug & NM_DEBUG_BDG)
				nm_prerr("error %d", error);
			goto out;
		}
		/* shortcut - we can skip get_hw_na(),
		 * ownership check and nm_bdg_attach()
		 */

	} else {
		struct netmap_adapter *hw;

		/* the vale:nic syntax is only valid for some commands */
		switch (hdr->nr_reqtype) {
		case NETMAP_REQ_VALE_ATTACH:
		case NETMAP_REQ_VALE_DETACH:
		case NETMAP_REQ_VALE_POLLING_ENABLE:
		case NETMAP_REQ_VALE_POLLING_DISABLE:
			break; /* ok */
		default:
			error = EINVAL;
			goto out;
		}

		error = netmap_get_hw_na(ifp, nmd, &hw);
		if (error || hw == NULL)
			goto out;

		/* host adapter might not be created */
		error = hw->nm_bdg_attach(nr_name, hw, b);
		if (error == NM_NEED_BWRAP) {
			error = b->bdg_ops.bwrap_attach(nr_name, hw);
		}
		if (error)
			goto out;
		vpna = hw->na_vp;
		hostna = hw->na_hostvp;
		if (hdr->nr_reqtype == NETMAP_REQ_VALE_ATTACH) {
			/* Check if we need to skip the host rings. */
			struct nmreq_vale_attach *areq =
				(struct nmreq_vale_attach *)(uintptr_t)hdr->nr_body;
			if (areq->reg.nr_mode != NR_REG_NIC_SW) {
				hostna = NULL;
			}
		}
	}

	BDG_WLOCK(b);
	vpna->bdg_port = cand;
	nm_prdis("NIC  %p to bridge port %d", vpna, cand);
	/* bind the port to the bridge (virtual ports are not active) */
	b->bdg_ports[cand] = vpna;
	vpna->na_bdg = b;
	b->bdg_active_ports++;
	if (hostna != NULL) {
		/* also bind the host stack to the bridge */
		b->bdg_ports[cand2] = hostna;
		hostna->bdg_port = cand2;
		hostna->na_bdg = b;
		b->bdg_active_ports++;
		nm_prdis("host %p to bridge port %d", hostna, cand2);
	}
	nm_prdis("if %s refs %d", ifname, vpna->up.na_refcount);
	BDG_WUNLOCK(b);
	*na = &vpna->up;
	netmap_adapter_get(*na);

out:
	if (ifp)
		if_rele(ifp);

	return error;
}


int
nm_is_bwrap(struct netmap_adapter *na)
{
	return na->nm_register == netmap_bwrap_reg;
}


struct nm_bdg_polling_state;
struct
nm_bdg_kthread {
	struct nm_kctx *nmk;
	u_int qfirst;
	u_int qlast;
	struct nm_bdg_polling_state *bps;
};

struct nm_bdg_polling_state {
	bool configured;
	bool stopped;
	struct netmap_bwrap_adapter *bna;
	uint32_t mode;
	u_int qfirst;
	u_int qlast;
	u_int cpu_from;
	u_int ncpus;
	struct nm_bdg_kthread *kthreads;
};

static void
netmap_bwrap_polling(void *data)
{
	struct nm_bdg_kthread *nbk = data;
	struct netmap_bwrap_adapter *bna;
	u_int qfirst, qlast, i;
	struct netmap_kring **kring0, *kring;

	if (!nbk)
		return;
	qfirst = nbk->qfirst;
	qlast = nbk->qlast;
	bna = nbk->bps->bna;
	kring0 = NMR(bna->hwna, NR_RX);

	for (i = qfirst; i < qlast; i++) {
		kring = kring0[i];
		kring->nm_notify(kring, 0);
	}
}

static int
nm_bdg_create_kthreads(struct nm_bdg_polling_state *bps)
{
	struct nm_kctx_cfg kcfg;
	int i, j;

	bps->kthreads = nm_os_malloc(sizeof(struct nm_bdg_kthread) * bps->ncpus);
	if (bps->kthreads == NULL)
		return ENOMEM;

	bzero(&kcfg, sizeof(kcfg));
	kcfg.worker_fn = netmap_bwrap_polling;
	for (i = 0; i < bps->ncpus; i++) {
		struct nm_bdg_kthread *t = bps->kthreads + i;
		int all = (bps->ncpus == 1 &&
			bps->mode == NETMAP_POLLING_MODE_SINGLE_CPU);
		int affinity = bps->cpu_from + i;

		t->bps = bps;
		t->qfirst = all ? bps->qfirst /* must be 0 */: affinity;
		t->qlast = all ? bps->qlast : t->qfirst + 1;
		if (netmap_verbose)
			nm_prinf("kthread %d a:%u qf:%u ql:%u", i, affinity, t->qfirst,
				t->qlast);

		kcfg.type = i;
		kcfg.worker_private = t;
		t->nmk = nm_os_kctx_create(&kcfg, NULL);
		if (t->nmk == NULL) {
			goto cleanup;
		}
		nm_os_kctx_worker_setaff(t->nmk, affinity);
	}
	return 0;

cleanup:
	for (j = 0; j < i; j++) {
		struct nm_bdg_kthread *t = bps->kthreads + i;
		nm_os_kctx_destroy(t->nmk);
	}
	nm_os_free(bps->kthreads);
	return EFAULT;
}

/* A variant of ptnetmap_start_kthreads() */
static int
nm_bdg_polling_start_kthreads(struct nm_bdg_polling_state *bps)
{
	int error, i, j;

	if (!bps) {
		nm_prerr("polling is not configured");
		return EFAULT;
	}
	bps->stopped = false;

	for (i = 0; i < bps->ncpus; i++) {
		struct nm_bdg_kthread *t = bps->kthreads + i;
		error = nm_os_kctx_worker_start(t->nmk);
		if (error) {
			nm_prerr("error in nm_kthread_start(): %d", error);
			goto cleanup;
		}
	}
	return 0;

cleanup:
	for (j = 0; j < i; j++) {
		struct nm_bdg_kthread *t = bps->kthreads + i;
		nm_os_kctx_worker_stop(t->nmk);
	}
	bps->stopped = true;
	return error;
}

static void
nm_bdg_polling_stop_delete_kthreads(struct nm_bdg_polling_state *bps)
{
	int i;

	if (!bps)
		return;

	for (i = 0; i < bps->ncpus; i++) {
		struct nm_bdg_kthread *t = bps->kthreads + i;
		nm_os_kctx_worker_stop(t->nmk);
		nm_os_kctx_destroy(t->nmk);
	}
	bps->stopped = true;
}

static int
get_polling_cfg(struct nmreq_vale_polling *req, struct netmap_adapter *na,
		struct nm_bdg_polling_state *bps)
{
	unsigned int avail_cpus, core_from;
	unsigned int qfirst, qlast;
	uint32_t i = req->nr_first_cpu_id;
	uint32_t req_cpus = req->nr_num_polling_cpus;

	avail_cpus = nm_os_ncpus();

	if (req_cpus == 0) {
		nm_prerr("req_cpus must be > 0");
		return EINVAL;
	} else if (req_cpus >= avail_cpus) {
		nm_prerr("Cannot use all the CPUs in the system");
		return EINVAL;
	}

	if (req->nr_mode == NETMAP_POLLING_MODE_MULTI_CPU) {
		/* Use a separate core for each ring. If nr_num_polling_cpus>1
		 * more consecutive rings are polled.
		 * For example, if nr_first_cpu_id=2 and nr_num_polling_cpus=2,
		 * ring 2 and 3 are polled by core 2 and 3, respectively. */
		if (i + req_cpus > nma_get_nrings(na, NR_RX)) {
			nm_prerr("Rings %u-%u not in range (have %d rings)",
				i, i + req_cpus, nma_get_nrings(na, NR_RX));
			return EINVAL;
		}
		qfirst = i;
		qlast = qfirst + req_cpus;
		core_from = qfirst;

	} else if (req->nr_mode == NETMAP_POLLING_MODE_SINGLE_CPU) {
		/* Poll all the rings using a core specified by nr_first_cpu_id.
		 * the number of cores must be 1. */
		if (req_cpus != 1) {
			nm_prerr("ncpus must be 1 for NETMAP_POLLING_MODE_SINGLE_CPU "
				"(was %d)", req_cpus);
			return EINVAL;
		}
		qfirst = 0;
		qlast = nma_get_nrings(na, NR_RX);
		core_from = i;
	} else {
		nm_prerr("Invalid polling mode");
		return EINVAL;
	}

	bps->mode = req->nr_mode;
	bps->qfirst = qfirst;
	bps->qlast = qlast;
	bps->cpu_from = core_from;
	bps->ncpus = req_cpus;
	nm_prinf("%s qfirst %u qlast %u cpu_from %u ncpus %u",
		req->nr_mode == NETMAP_POLLING_MODE_MULTI_CPU ?
		"MULTI" : "SINGLE",
		qfirst, qlast, core_from, req_cpus);
	return 0;
}

static int
nm_bdg_ctl_polling_start(struct nmreq_vale_polling *req, struct netmap_adapter *na)
{
	struct nm_bdg_polling_state *bps;
	struct netmap_bwrap_adapter *bna;
	int error;

	bna = (struct netmap_bwrap_adapter *)na;
	if (bna->na_polling_state) {
		nm_prerr("ERROR adapter already in polling mode");
		return EFAULT;
	}

	bps = nm_os_malloc(sizeof(*bps));
	if (!bps)
		return ENOMEM;
	bps->configured = false;
	bps->stopped = true;

	if (get_polling_cfg(req, na, bps)) {
		nm_os_free(bps);
		return EINVAL;
	}

	if (nm_bdg_create_kthreads(bps)) {
		nm_os_free(bps);
		return EFAULT;
	}

	bps->configured = true;
	bna->na_polling_state = bps;
	bps->bna = bna;

	/* disable interrupts if possible */
	nma_intr_enable(bna->hwna, 0);
	/* start kthread now */
	error = nm_bdg_polling_start_kthreads(bps);
	if (error) {
		nm_prerr("ERROR nm_bdg_polling_start_kthread()");
		nm_os_free(bps->kthreads);
		nm_os_free(bps);
		bna->na_polling_state = NULL;
		nma_intr_enable(bna->hwna, 1);
	}
	return error;
}

static int
nm_bdg_ctl_polling_stop(struct netmap_adapter *na)
{
	struct netmap_bwrap_adapter *bna = (struct netmap_bwrap_adapter *)na;
	struct nm_bdg_polling_state *bps;

	if (!bna->na_polling_state) {
		nm_prerr("ERROR adapter is not in polling mode");
		return EFAULT;
	}
	bps = bna->na_polling_state;
	nm_bdg_polling_stop_delete_kthreads(bna->na_polling_state);
	bps->configured = false;
	nm_os_free(bps);
	bna->na_polling_state = NULL;
	/* reenable interrupts */
	nma_intr_enable(bna->hwna, 1);
	return 0;
}

int
nm_bdg_polling(struct nmreq_header *hdr)
{
	struct nmreq_vale_polling *req =
		(struct nmreq_vale_polling *)(uintptr_t)hdr->nr_body;
	struct netmap_adapter *na = NULL;
	int error = 0;

	NMG_LOCK();
	error = netmap_get_vale_na(hdr, &na, NULL, /*create=*/0);
	if (na && !error) {
		if (!nm_is_bwrap(na)) {
			error = EOPNOTSUPP;
		} else if (hdr->nr_reqtype == NETMAP_BDG_POLLING_ON) {
			error = nm_bdg_ctl_polling_start(req, na);
			if (!error)
				netmap_adapter_get(na);
		} else {
			error = nm_bdg_ctl_polling_stop(na);
			if (!error)
				netmap_adapter_put(na);
		}
		netmap_adapter_put(na);
	} else if (!na && !error) {
		/* Not VALE port. */
		error = EINVAL;
	}
	NMG_UNLOCK();

	return error;
}

/* Called by external kernel modules (e.g., Openvswitch).
 * to set configure/lookup/dtor functions of a VALE instance.
 * Register callbacks to the given bridge. 'name' may be just
 * bridge's name (including ':' if it is not just NM_BDG_NAME).
 *
 * Called without NMG_LOCK.
 */

int
netmap_bdg_regops(const char *name, struct netmap_bdg_ops *bdg_ops, void *private_data, void *auth_token)
{
	struct nm_bridge *b;
	int error = 0;

	NMG_LOCK();
	b = nm_find_bridge(name, 0 /* don't create */, NULL);
	if (!b) {
		error = ENXIO;
		goto unlock_regops;
	}
	if (!nm_bdg_valid_auth_token(b, auth_token)) {
		error = EACCES;
		goto unlock_regops;
	}

	BDG_WLOCK(b);
	if (!bdg_ops) {
		/* resetting the bridge */
		bzero(b->ht, sizeof(struct nm_hash_ent) * NM_BDG_HASH);
		b->bdg_ops = b->bdg_saved_ops;
		b->private_data = b->ht;
	} else {
		/* modifying the bridge */
		b->private_data = private_data;
#define nm_bdg_override(m) if (bdg_ops->m) b->bdg_ops.m = bdg_ops->m
		nm_bdg_override(lookup);
		nm_bdg_override(config);
		nm_bdg_override(dtor);
		nm_bdg_override(vp_create);
		nm_bdg_override(bwrap_attach);
#undef nm_bdg_override

	}
	BDG_WUNLOCK(b);

unlock_regops:
	NMG_UNLOCK();
	return error;
}


int
netmap_bdg_config(struct nm_ifreq *nr)
{
	struct nm_bridge *b;
	int error = EINVAL;

	NMG_LOCK();
	b = nm_find_bridge(nr->nifr_name, 0, NULL);
	if (!b) {
		NMG_UNLOCK();
		return error;
	}
	NMG_UNLOCK();
	/* Don't call config() with NMG_LOCK() held */
	BDG_RLOCK(b);
	if (b->bdg_ops.config != NULL)
		error = b->bdg_ops.config(nr);
	BDG_RUNLOCK(b);
	return error;
}


/* nm_register callback for VALE ports */
int
netmap_vp_reg(struct netmap_adapter *na, int onoff)
{
	struct netmap_vp_adapter *vpna =
		(struct netmap_vp_adapter*)na;

	/* persistent ports may be put in netmap mode
	 * before being attached to a bridge
	 */
	if (vpna->na_bdg)
		BDG_WLOCK(vpna->na_bdg);
	if (onoff) {
		netmap_krings_mode_commit(na, onoff);
		if (na->active_fds == 0)
			na->na_flags |= NAF_NETMAP_ON;
		 /* XXX on FreeBSD, persistent VALE ports should also
		 * toggle IFCAP_NETMAP in na->ifp (2014-03-16)
		 */
	} else {
		if (na->active_fds == 0)
			na->na_flags &= ~NAF_NETMAP_ON;
		netmap_krings_mode_commit(na, onoff);
	}
	if (vpna->na_bdg)
		BDG_WUNLOCK(vpna->na_bdg);
	return 0;
}


/* rxsync code used by VALE ports nm_rxsync callback and also
 * internally by the brwap
 */
static int
netmap_vp_rxsync_locked(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_ring *ring = kring->ring;
	u_int nm_i, lim = kring->nkr_num_slots - 1;
	u_int head = kring->rhead;
	int n;

	if (head > lim) {
		nm_prerr("ouch dangerous reset!!!");
		n = netmap_ring_reinit(kring);
		goto done;
	}

	/* First part, import newly received packets. */
	/* actually nothing to do here, they are already in the kring */

	/* Second part, skip past packets that userspace has released. */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {
		/* consistency check, but nothing really important here */
		for (n = 0; likely(nm_i != head); n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			void *addr = NMB(na, slot);

			if (addr == NETMAP_BUF_BASE(kring->na)) { /* bad buf */
				nm_prerr("bad buffer index %d, ignore ?",
					slot->buf_idx);
			}
			slot->flags &= ~NS_BUF_CHANGED;
			nm_i = nm_next(nm_i, lim);
		}
		kring->nr_hwcur = head;
	}

	n = 0;
done:
	return n;
}

/*
 * nm_rxsync callback for VALE ports
 * user process reading from a VALE switch.
 * Already protected against concurrent calls from userspace,
 * but we must acquire the queue's lock to protect against
 * writers on the same queue.
 */
int
netmap_vp_rxsync(struct netmap_kring *kring, int flags)
{
	int n;

	mtx_lock(&kring->q_lock);
	n = netmap_vp_rxsync_locked(kring, flags);
	mtx_unlock(&kring->q_lock);
	return n;
}

int
netmap_bwrap_attach(const char *nr_name, struct netmap_adapter *hwna,
		struct netmap_bdg_ops *ops)
{
	return ops->bwrap_attach(nr_name, hwna);
}


/* Bridge wrapper code (bwrap).
 * This is used to connect a non-VALE-port netmap_adapter (hwna) to a
 * VALE switch.
 * The main task is to swap the meaning of tx and rx rings to match the
 * expectations of the VALE switch code (see nm_bdg_flush).
 *
 * The bwrap works by interposing a netmap_bwrap_adapter between the
 * rest of the system and the hwna. The netmap_bwrap_adapter looks like
 * a netmap_vp_adapter to the rest the system, but, internally, it
 * translates all callbacks to what the hwna expects.
 *
 * Note that we have to intercept callbacks coming from two sides:
 *
 *  - callbacks coming from the netmap module are intercepted by
 *    passing around the netmap_bwrap_adapter instead of the hwna
 *
 *  - callbacks coming from outside of the netmap module only know
 *    about the hwna. This, however, only happens in interrupt
 *    handlers, where only the hwna->nm_notify callback is called.
 *    What the bwrap does is to overwrite the hwna->nm_notify callback
 *    with its own netmap_bwrap_intr_notify.
 *    XXX This assumes that the hwna->nm_notify callback was the
 *    standard netmap_notify(), as it is the case for nic adapters.
 *    Any additional action performed by hwna->nm_notify will not be
 *    performed by netmap_bwrap_intr_notify.
 *
 * Additionally, the bwrap can optionally attach the host rings pair
 * of the wrapped adapter to a different port of the switch.
 */


static void
netmap_bwrap_dtor(struct netmap_adapter *na)
{
	struct netmap_bwrap_adapter *bna = (struct netmap_bwrap_adapter*)na;
	struct netmap_adapter *hwna = bna->hwna;
	struct nm_bridge *b = bna->up.na_bdg,
		*bh = bna->host.na_bdg;

	if (bna->host.up.nm_mem)
		netmap_mem_put(bna->host.up.nm_mem);

	if (b) {
		netmap_bdg_detach_common(b, bna->up.bdg_port,
			    (bh ? bna->host.bdg_port : -1));
	}

	nm_prdis("na %p", na);
	na->ifp = NULL;
	bna->host.up.ifp = NULL;
	hwna->na_vp = bna->saved_na_vp;
	hwna->na_hostvp = NULL;
	hwna->na_private = NULL;
	hwna->na_flags &= ~NAF_BUSY;
	netmap_adapter_put(hwna);

}


/*
 * Intr callback for NICs connected to a bridge.
 * Simply ignore tx interrupts (maybe we could try to recover space ?)
 * and pass received packets from nic to the bridge.
 *
 * XXX TODO check locking: this is called from the interrupt
 * handler so we should make sure that the interface is not
 * disconnected while passing down an interrupt.
 *
 * Note, no user process can access this NIC or the host stack.
 * The only part of the ring that is significant are the slots,
 * and head/cur/tail are set from the kring as needed
 * (part as a receive ring, part as a transmit ring).
 *
 * callback that overwrites the hwna notify callback.
 * Packets come from the outside or from the host stack and are put on an
 * hwna rx ring.
 * The bridge wrapper then sends the packets through the bridge.
 */
static int
netmap_bwrap_intr_notify(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_bwrap_adapter *bna = na->na_private;
	struct netmap_kring *bkring;
	struct netmap_vp_adapter *vpna = &bna->up;
	u_int ring_nr = kring->ring_id;
	int ret = NM_IRQ_COMPLETED;
	int error;

	if (netmap_debug & NM_DEBUG_RXINTR)
	    nm_prinf("%s %s 0x%x", na->name, kring->name, flags);

	bkring = vpna->up.tx_rings[ring_nr];

	/* make sure the ring is not disabled */
	if (nm_kr_tryget(kring, 0 /* can't sleep */, NULL)) {
		return EIO;
	}

	if (netmap_debug & NM_DEBUG_RXINTR)
	    nm_prinf("%s head %d cur %d tail %d",  na->name,
		kring->rhead, kring->rcur, kring->rtail);

	/* simulate a user wakeup on the rx ring
	 * fetch packets that have arrived.
	 */
	error = kring->nm_sync(kring, 0);
	if (error)
		goto put_out;
	if (kring->nr_hwcur == kring->nr_hwtail) {
		if (netmap_verbose)
			nm_prlim(1, "interrupt with no packets on %s",
				kring->name);
		goto put_out;
	}

	/* new packets are kring->rcur to kring->nr_hwtail, and the bkring
	 * had hwcur == bkring->rhead. So advance bkring->rhead to kring->nr_hwtail
	 * to push all packets out.
	 */
	bkring->rhead = bkring->rcur = kring->nr_hwtail;

	bkring->nm_sync(bkring, flags);

	/* mark all buffers as released on this ring */
	kring->rhead = kring->rcur = kring->rtail = kring->nr_hwtail;
	/* another call to actually release the buffers */
	error = kring->nm_sync(kring, 0);

	/* The second rxsync may have further advanced hwtail. If this happens,
	 *  return NM_IRQ_RESCHED, otherwise just return NM_IRQ_COMPLETED. */
	if (kring->rcur != kring->nr_hwtail) {
		ret = NM_IRQ_RESCHED;
	}
put_out:
	nm_kr_put(kring);

	return error ? error : ret;
}


/* nm_register callback for bwrap */
int
netmap_bwrap_reg(struct netmap_adapter *na, int onoff)
{
	struct netmap_bwrap_adapter *bna =
		(struct netmap_bwrap_adapter *)na;
	struct netmap_adapter *hwna = bna->hwna;
	struct netmap_vp_adapter *hostna = &bna->host;
	int error, i;
	enum txrx t;

	nm_prdis("%s %s", na->name, onoff ? "on" : "off");

	if (onoff) {
		/* netmap_do_regif has been called on the bwrap na.
		 * We need to pass the information about the
		 * memory allocator down to the hwna before
		 * putting it in netmap mode
		 */
		hwna->na_lut = na->na_lut;

		if (hostna->na_bdg) {
			/* if the host rings have been attached to switch,
			 * we need to copy the memory allocator information
			 * in the hostna also
			 */
			hostna->up.na_lut = na->na_lut;
		}

	}

	/* pass down the pending ring state information */
	for_rx_tx(t) {
		for (i = 0; i < netmap_all_rings(na, t); i++) {
			NMR(hwna, nm_txrx_swap(t))[i]->nr_pending_mode =
				NMR(na, t)[i]->nr_pending_mode;
		}
	}

	/* forward the request to the hwna */
	error = hwna->nm_register(hwna, onoff);
	if (error)
		return error;

	/* copy up the current ring state information */
	for_rx_tx(t) {
		for (i = 0; i < netmap_all_rings(na, t); i++) {
			struct netmap_kring *kring = NMR(hwna, nm_txrx_swap(t))[i];
			NMR(na, t)[i]->nr_mode = kring->nr_mode;
		}
	}

	/* impersonate a netmap_vp_adapter */
	netmap_vp_reg(na, onoff);
	if (hostna->na_bdg)
		netmap_vp_reg(&hostna->up, onoff);

	if (onoff) {
		u_int i;
		/* intercept the hwna nm_nofify callback on the hw rings */
		for (i = 0; i < hwna->num_rx_rings; i++) {
			hwna->rx_rings[i]->save_notify = hwna->rx_rings[i]->nm_notify;
			hwna->rx_rings[i]->nm_notify = netmap_bwrap_intr_notify;
		}
		i = hwna->num_rx_rings; /* for safety */
		/* save the host ring notify unconditionally */
		for (; i < netmap_real_rings(hwna, NR_RX); i++) {
			hwna->rx_rings[i]->save_notify =
				hwna->rx_rings[i]->nm_notify;
			if (hostna->na_bdg) {
				/* also intercept the host ring notify */
				hwna->rx_rings[i]->nm_notify =
					netmap_bwrap_intr_notify;
				na->tx_rings[i]->nm_sync = na->nm_txsync;
			}
		}
		if (na->active_fds == 0)
			na->na_flags |= NAF_NETMAP_ON;
	} else {
		u_int i;

		if (na->active_fds == 0)
			na->na_flags &= ~NAF_NETMAP_ON;

		/* reset all notify callbacks (including host ring) */
		for (i = 0; i < netmap_all_rings(hwna, NR_RX); i++) {
			hwna->rx_rings[i]->nm_notify =
				hwna->rx_rings[i]->save_notify;
			hwna->rx_rings[i]->save_notify = NULL;
		}
		hwna->na_lut.lut = NULL;
		hwna->na_lut.plut = NULL;
		hwna->na_lut.objtotal = 0;
		hwna->na_lut.objsize = 0;

		/* pass ownership of the netmap rings to the hwna */
		for_rx_tx(t) {
			for (i = 0; i < netmap_all_rings(na, t); i++) {
				NMR(na, t)[i]->ring = NULL;
			}
		}
		/* reset the number of host rings to default */
		for_rx_tx(t) {
			nma_set_host_nrings(hwna, t, 1);
		}

	}

	return 0;
}

/* nm_config callback for bwrap */
static int
netmap_bwrap_config(struct netmap_adapter *na, struct nm_config_info *info)
{
	struct netmap_bwrap_adapter *bna =
		(struct netmap_bwrap_adapter *)na;
	struct netmap_adapter *hwna = bna->hwna;
	int error;

	/* Forward the request to the hwna. It may happen that nobody
	 * registered hwna yet, so netmap_mem_get_lut() may have not
	 * been called yet. */
	error = netmap_mem_get_lut(hwna->nm_mem, &hwna->na_lut);
	if (error)
		return error;
	netmap_update_config(hwna);
	/* swap the results and propagate */
	info->num_tx_rings = hwna->num_rx_rings;
	info->num_tx_descs = hwna->num_rx_desc;
	info->num_rx_rings = hwna->num_tx_rings;
	info->num_rx_descs = hwna->num_tx_desc;
	info->rx_buf_maxsize = hwna->rx_buf_maxsize;

	return 0;
}


/* nm_krings_create callback for bwrap */
int
netmap_bwrap_krings_create_common(struct netmap_adapter *na)
{
	struct netmap_bwrap_adapter *bna =
		(struct netmap_bwrap_adapter *)na;
	struct netmap_adapter *hwna = bna->hwna;
	struct netmap_adapter *hostna = &bna->host.up;
	int i, error = 0;
	enum txrx t;

	/* also create the hwna krings */
	error = hwna->nm_krings_create(hwna);
	if (error) {
		return error;
	}

	/* increment the usage counter for all the hwna krings */
	for_rx_tx(t) {
		for (i = 0; i < netmap_all_rings(hwna, t); i++) {
			NMR(hwna, t)[i]->users++;
		}
	}

	/* now create the actual rings */
	error = netmap_mem_rings_create(hwna);
	if (error) {
		goto err_dec_users;
	}

	/* cross-link the netmap rings
	 * The original number of rings comes from hwna,
	 * rx rings on one side equals tx rings on the other.
	 */
	for_rx_tx(t) {
		enum txrx r = nm_txrx_swap(t); /* swap NR_TX <-> NR_RX */
		for (i = 0; i < netmap_all_rings(hwna, r); i++) {
			NMR(na, t)[i]->nkr_num_slots = NMR(hwna, r)[i]->nkr_num_slots;
			NMR(na, t)[i]->ring = NMR(hwna, r)[i]->ring;
		}
	}

	if (na->na_flags & NAF_HOST_RINGS) {
		/* the hostna rings are the host rings of the bwrap.
		 * The corresponding krings must point back to the
		 * hostna
		 */
		hostna->tx_rings = &na->tx_rings[na->num_tx_rings];
		hostna->rx_rings = &na->rx_rings[na->num_rx_rings];
		for_rx_tx(t) {
			for (i = 0; i < nma_get_nrings(hostna, t); i++) {
				NMR(hostna, t)[i]->na = hostna;
			}
		}
	}

	return 0;

err_dec_users:
	for_rx_tx(t) {
		for (i = 0; i < netmap_all_rings(hwna, t); i++) {
			NMR(hwna, t)[i]->users--;
		}
	}
	hwna->nm_krings_delete(hwna);
	return error;
}


void
netmap_bwrap_krings_delete_common(struct netmap_adapter *na)
{
	struct netmap_bwrap_adapter *bna =
		(struct netmap_bwrap_adapter *)na;
	struct netmap_adapter *hwna = bna->hwna;
	enum txrx t;
	int i;

	nm_prdis("%s", na->name);

	/* decrement the usage counter for all the hwna krings */
	for_rx_tx(t) {
		for (i = 0; i < netmap_all_rings(hwna, t); i++) {
			NMR(hwna, t)[i]->users--;
		}
	}

	/* delete any netmap rings that are no longer needed */
	netmap_mem_rings_delete(hwna);
	hwna->nm_krings_delete(hwna);
}


/* notify method for the bridge-->hwna direction */
int
netmap_bwrap_notify(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_bwrap_adapter *bna = na->na_private;
	struct netmap_adapter *hwna = bna->hwna;
	u_int ring_n = kring->ring_id;
	u_int lim = kring->nkr_num_slots - 1;
	struct netmap_kring *hw_kring;
	int error;

	nm_prdis("%s: na %s hwna %s",
			(kring ? kring->name : "NULL!"),
			(na ? na->name : "NULL!"),
			(hwna ? hwna->name : "NULL!"));
	hw_kring = hwna->tx_rings[ring_n];

	if (nm_kr_tryget(hw_kring, 0, NULL)) {
		return ENXIO;
	}

	/* first step: simulate a user wakeup on the rx ring */
	netmap_vp_rxsync(kring, flags);
	nm_prdis("%s[%d] PRE rx(c%3d t%3d l%3d) ring(h%3d c%3d t%3d) tx(c%3d ht%3d t%3d)",
		na->name, ring_n,
		kring->nr_hwcur, kring->nr_hwtail, kring->nkr_hwlease,
		kring->rhead, kring->rcur, kring->rtail,
		hw_kring->nr_hwcur, hw_kring->nr_hwtail, hw_kring->rtail);
	/* second step: the new packets are sent on the tx ring
	 * (which is actually the same ring)
	 */
	hw_kring->rhead = hw_kring->rcur = kring->nr_hwtail;
	error = hw_kring->nm_sync(hw_kring, flags);
	if (error)
		goto put_out;

	/* third step: now we are back the rx ring */
	/* claim ownership on all hw owned bufs */
	kring->rhead = kring->rcur = nm_next(hw_kring->nr_hwtail, lim); /* skip past reserved slot */

	/* fourth step: the user goes to sleep again, causing another rxsync */
	netmap_vp_rxsync(kring, flags);
	nm_prdis("%s[%d] PST rx(c%3d t%3d l%3d) ring(h%3d c%3d t%3d) tx(c%3d ht%3d t%3d)",
		na->name, ring_n,
		kring->nr_hwcur, kring->nr_hwtail, kring->nkr_hwlease,
		kring->rhead, kring->rcur, kring->rtail,
		hw_kring->nr_hwcur, hw_kring->nr_hwtail, hw_kring->rtail);
put_out:
	nm_kr_put(hw_kring);

	return error ? error : NM_IRQ_COMPLETED;
}


/* nm_bdg_ctl callback for the bwrap.
 * Called on bridge-attach and detach, as an effect of vale-ctl -[ahd].
 * On attach, it needs to provide a fake netmap_priv_d structure and
 * perform a netmap_do_regif() on the bwrap. This will put both the
 * bwrap and the hwna in netmap mode, with the netmap rings shared
 * and cross linked. Moroever, it will start intercepting interrupts
 * directed to hwna.
 */
static int
netmap_bwrap_bdg_ctl(struct nmreq_header *hdr, struct netmap_adapter *na)
{
	struct netmap_priv_d *npriv;
	struct netmap_bwrap_adapter *bna = (struct netmap_bwrap_adapter*)na;
	int error = 0;

	if (hdr->nr_reqtype == NETMAP_REQ_VALE_ATTACH) {
		struct nmreq_vale_attach *req =
			(struct nmreq_vale_attach *)(uintptr_t)hdr->nr_body;
		if (req->reg.nr_ringid != 0 ||
			(req->reg.nr_mode != NR_REG_ALL_NIC &&
				req->reg.nr_mode != NR_REG_NIC_SW)) {
			/* We only support attaching all the NIC rings
			 * and/or the host stack. */
			return EINVAL;
		}
		if (NETMAP_OWNED_BY_ANY(na)) {
			return EBUSY;
		}
		if (bna->na_kpriv) {
			/* nothing to do */
			return 0;
		}
		npriv = netmap_priv_new();
		if (npriv == NULL)
			return ENOMEM;
		npriv->np_ifp = na->ifp; /* let the priv destructor release the ref */
		error = netmap_do_regif(npriv, na, req->reg.nr_mode,
					req->reg.nr_ringid, req->reg.nr_flags);
		if (error) {
			netmap_priv_delete(npriv);
			return error;
		}
		bna->na_kpriv = npriv;
		na->na_flags |= NAF_BUSY;
	} else {
		if (na->active_fds == 0) /* not registered */
			return EINVAL;
		netmap_priv_delete(bna->na_kpriv);
		bna->na_kpriv = NULL;
		na->na_flags &= ~NAF_BUSY;
	}

	return error;
}

/* attach a bridge wrapper to the 'real' device */
int
netmap_bwrap_attach_common(struct netmap_adapter *na,
		struct netmap_adapter *hwna)
{
	struct netmap_bwrap_adapter *bna;
	struct netmap_adapter *hostna = NULL;
	int error = 0;
	enum txrx t;

	/* make sure the NIC is not already in use */
	if (NETMAP_OWNED_BY_ANY(hwna)) {
		nm_prerr("NIC %s busy, cannot attach to bridge", hwna->name);
		return EBUSY;
	}

	bna = (struct netmap_bwrap_adapter *)na;
	/* make bwrap ifp point to the real ifp */
	na->ifp = hwna->ifp;
	if_ref(na->ifp);
	na->na_private = bna;
	/* fill the ring data for the bwrap adapter with rx/tx meanings
	 * swapped. The real cross-linking will be done during register,
	 * when all the krings will have been created.
	 */
	for_rx_tx(t) {
		enum txrx r = nm_txrx_swap(t); /* swap NR_TX <-> NR_RX */
		nma_set_nrings(na, t, nma_get_nrings(hwna, r));
		nma_set_ndesc(na, t, nma_get_ndesc(hwna, r));
	}
	na->nm_dtor = netmap_bwrap_dtor;
	na->nm_config = netmap_bwrap_config;
	na->nm_bdg_ctl = netmap_bwrap_bdg_ctl;
	na->pdev = hwna->pdev;
	na->nm_mem = netmap_mem_get(hwna->nm_mem);
	na->virt_hdr_len = hwna->virt_hdr_len;
	na->rx_buf_maxsize = hwna->rx_buf_maxsize;

	bna->hwna = hwna;
	netmap_adapter_get(hwna);
	hwna->na_private = bna; /* weak reference */
	bna->saved_na_vp = hwna->na_vp;
	hwna->na_vp = &bna->up;
	bna->up.up.na_vp = &(bna->up);

	if (hwna->na_flags & NAF_HOST_RINGS) {
		if (hwna->na_flags & NAF_SW_ONLY)
			na->na_flags |= NAF_SW_ONLY;
		na->na_flags |= NAF_HOST_RINGS;
		hostna = &bna->host.up;

		/* limit the number of host rings to that of hw */
		nm_bound_var(&hostna->num_tx_rings, 1, 1,
				nma_get_nrings(hwna, NR_TX), NULL);
		nm_bound_var(&hostna->num_rx_rings, 1, 1,
				nma_get_nrings(hwna, NR_RX), NULL);

		snprintf(hostna->name, sizeof(hostna->name), "%s^", na->name);
		hostna->ifp = hwna->ifp;
		for_rx_tx(t) {
			enum txrx r = nm_txrx_swap(t);
			u_int nr = nma_get_nrings(hostna, t);

			nma_set_nrings(hostna, t, nr);
			nma_set_host_nrings(na, t, nr);
			if (nma_get_host_nrings(hwna, t) < nr) {
				nma_set_host_nrings(hwna, t, nr);
			}
			nma_set_ndesc(hostna, t, nma_get_ndesc(hwna, r));
		}
		// hostna->nm_txsync = netmap_bwrap_host_txsync;
		// hostna->nm_rxsync = netmap_bwrap_host_rxsync;
		hostna->nm_mem = netmap_mem_get(na->nm_mem);
		hostna->na_private = bna;
		hostna->na_vp = &bna->up;
		na->na_hostvp = hwna->na_hostvp =
			hostna->na_hostvp = &bna->host;
		hostna->na_flags = NAF_BUSY; /* prevent NIOCREGIF */
		hostna->rx_buf_maxsize = hwna->rx_buf_maxsize;
	}
	if (hwna->na_flags & NAF_MOREFRAG)
		na->na_flags |= NAF_MOREFRAG;

	nm_prdis("%s<->%s txr %d txd %d rxr %d rxd %d",
		na->name, ifp->if_xname,
		na->num_tx_rings, na->num_tx_desc,
		na->num_rx_rings, na->num_rx_desc);

	error = netmap_attach_common(na);
	if (error) {
		goto err_put;
	}
	hwna->na_flags |= NAF_BUSY;
	return 0;

err_put:
	hwna->na_vp = hwna->na_hostvp = NULL;
	netmap_adapter_put(hwna);
	return error;

}

struct nm_bridge *
netmap_init_bridges2(u_int n)
{
	int i;
	struct nm_bridge *b;

	b = nm_os_malloc(sizeof(struct nm_bridge) * n);
	if (b == NULL)
		return NULL;
	for (i = 0; i < n; i++)
		BDG_RWINIT(&b[i]);
	return b;
}

void
netmap_uninit_bridges2(struct nm_bridge *b, u_int n)
{
	int i;

	if (b == NULL)
		return;

	for (i = 0; i < n; i++)
		BDG_RWDESTROY(&b[i]);
	nm_os_free(b);
}

int
netmap_init_bridges(void)
{
#ifdef CONFIG_NET_NS
	return netmap_bns_register();
#else
	nm_bridges = netmap_init_bridges2(NM_BRIDGES);
	if (nm_bridges == NULL)
		return ENOMEM;
	return 0;
#endif
}

void
netmap_uninit_bridges(void)
{
#ifdef CONFIG_NET_NS
	netmap_bns_unregister();
#else
	netmap_uninit_bridges2(nm_bridges, NM_BRIDGES);
#endif
}
