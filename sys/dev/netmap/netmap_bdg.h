/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2013-2018 Universita` di Pisa
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
 *
 * $FreeBSD$
 */
#ifndef _NET_NETMAP_BDG_H_
#define _NET_NETMAP_BDG_H_

#if defined(__FreeBSD__)
#define BDG_RWLOCK_T		struct rwlock // struct rwlock

#define	BDG_RWINIT(b)		\
	rw_init_flags(&(b)->bdg_lock, "bdg lock", RW_NOWITNESS)
#define BDG_WLOCK(b)		rw_wlock(&(b)->bdg_lock)
#define BDG_WUNLOCK(b)		rw_wunlock(&(b)->bdg_lock)
#define BDG_RLOCK(b)		rw_rlock(&(b)->bdg_lock)
#define BDG_RTRYLOCK(b)		rw_try_rlock(&(b)->bdg_lock)
#define BDG_RUNLOCK(b)		rw_runlock(&(b)->bdg_lock)
#define BDG_RWDESTROY(b)	rw_destroy(&(b)->bdg_lock)

#endif /* __FreeBSD__ */

/*
 * The following bridge-related functions are used by other
 * kernel modules.
 *
 * VALE only supports unicast or broadcast. The lookup
 * function can return 0 .. NM_BDG_MAXPORTS-1 for regular ports,
 * NM_BDG_MAXPORTS for broadcast, NM_BDG_MAXPORTS+1 to indicate
 * drop.
 */
typedef uint32_t (*bdg_lookup_fn_t)(struct nm_bdg_fwd *ft, uint8_t *ring_nr,
		struct netmap_vp_adapter *, void *private_data);
typedef int (*bdg_config_fn_t)(struct nm_ifreq *);
typedef void (*bdg_dtor_fn_t)(const struct netmap_vp_adapter *);
typedef void *(*bdg_update_private_data_fn_t)(void *private_data, void *callback_data, int *error);
typedef int (*bdg_vp_create_fn_t)(struct nmreq_header *hdr,
		struct ifnet *ifp, struct netmap_mem_d *nmd,
		struct netmap_vp_adapter **ret);
typedef int (*bdg_bwrap_attach_fn_t)(const char *nr_name, struct netmap_adapter *hwna);
struct netmap_bdg_ops {
	bdg_lookup_fn_t lookup;
	bdg_config_fn_t config;
	bdg_dtor_fn_t	dtor;
	bdg_vp_create_fn_t	vp_create;
	bdg_bwrap_attach_fn_t	bwrap_attach;
	char name[IFNAMSIZ];
};
int netmap_bwrap_attach(const char *name, struct netmap_adapter *, struct netmap_bdg_ops *);
int netmap_bdg_regops(const char *name, struct netmap_bdg_ops *bdg_ops, void *private_data, void *auth_token);

#define	NM_BRIDGES		8	/* number of bridges */
#define	NM_BDG_MAXPORTS		254	/* up to 254 */
#define	NM_BDG_BROADCAST	NM_BDG_MAXPORTS
#define	NM_BDG_NOPORT		(NM_BDG_MAXPORTS+1)

/* XXX Should go away after fixing find_bridge() - Michio */
#define NM_BDG_HASH		1024	/* forwarding table entries */

/* XXX revise this */
struct nm_hash_ent {
	uint64_t	mac;	/* the top 2 bytes are the epoch */
	uint64_t	ports;
};

/* Default size for the Maximum Frame Size. */
#define NM_BDG_MFS_DEFAULT	1514

/*
 * nm_bridge is a descriptor for a VALE switch.
 * Interfaces for a bridge are all in bdg_ports[].
 * The array has fixed size, an empty entry does not terminate
 * the search, but lookups only occur on attach/detach so we
 * don't mind if they are slow.
 *
 * The bridge is non blocking on the transmit ports: excess
 * packets are dropped if there is no room on the output port.
 *
 * bdg_lock protects accesses to the bdg_ports array.
 * This is a rw lock (or equivalent).
 */
#define NM_BDG_IFNAMSIZ IFNAMSIZ
struct nm_bridge {
	/* XXX what is the proper alignment/layout ? */
	BDG_RWLOCK_T	bdg_lock;	/* protects bdg_ports */
	int		bdg_namelen;
	uint32_t	bdg_active_ports;
	char		bdg_basename[NM_BDG_IFNAMSIZ];

	/* Indexes of active ports (up to active_ports)
	 * and all other remaining ports.
	 */
	uint32_t	bdg_port_index[NM_BDG_MAXPORTS];
	/* used by netmap_bdg_detach_common() */
	uint32_t	tmp_bdg_port_index[NM_BDG_MAXPORTS];

	struct netmap_vp_adapter *bdg_ports[NM_BDG_MAXPORTS];

	/*
	 * Programmable lookup functions to figure out the destination port.
	 * It returns either of an index of the destination port,
	 * NM_BDG_BROADCAST to broadcast this packet, or NM_BDG_NOPORT not to
	 * forward this packet.  ring_nr is the source ring index, and the
	 * function may overwrite this value to forward this packet to a
	 * different ring index.
	 * The function is set by netmap_bdg_regops().
	 */
	struct netmap_bdg_ops bdg_ops;
	struct netmap_bdg_ops bdg_saved_ops;

	/*
	 * Contains the data structure used by the bdg_ops.lookup function.
	 * By default points to *ht which is allocated on attach and used by the default lookup
	 * otherwise will point to the data structure received by netmap_bdg_regops().
	 */
	void *private_data;
	struct nm_hash_ent *ht;

	/* Currently used to specify if the bridge is still in use while empty and
	 * if it has been put in exclusive mode by an external module, see netmap_bdg_regops()
	 * and netmap_bdg_create().
	 */
#define NM_BDG_ACTIVE		1
#define NM_BDG_EXCLUSIVE	2
#define NM_BDG_NEED_BWRAP	4
	uint8_t			bdg_flags;


#ifdef CONFIG_NET_NS
	struct net *ns;
#endif /* CONFIG_NET_NS */
};

static inline void *
nm_bdg_get_auth_token(struct nm_bridge *b)
{
	return b->ht;
}

/* bridge not in exclusive mode ==> always valid
 * bridge in exclusive mode (created through netmap_bdg_create()) ==> check authentication token
 */
static inline int
nm_bdg_valid_auth_token(struct nm_bridge *b, void *auth_token)
{
	return !(b->bdg_flags & NM_BDG_EXCLUSIVE) || b->ht == auth_token;
}

int netmap_get_bdg_na(struct nmreq_header *hdr, struct netmap_adapter **na,
	struct netmap_mem_d *nmd, int create, struct netmap_bdg_ops *ops);

struct nm_bridge *nm_find_bridge(const char *name, int create, struct netmap_bdg_ops *ops);
int netmap_bdg_free(struct nm_bridge *b);
void netmap_bdg_detach_common(struct nm_bridge *b, int hw, int sw);
int netmap_vp_bdg_ctl(struct nmreq_header *hdr, struct netmap_adapter *na);
int netmap_bwrap_reg(struct netmap_adapter *, int onoff);
int netmap_vp_reg(struct netmap_adapter *na, int onoff);
int netmap_vp_rxsync(struct netmap_kring *kring, int flags);
int netmap_bwrap_notify(struct netmap_kring *kring, int flags);
int netmap_bwrap_attach_common(struct netmap_adapter *na,
		struct netmap_adapter *hwna);
int netmap_bwrap_krings_create_common(struct netmap_adapter *na);
void netmap_bwrap_krings_delete_common(struct netmap_adapter *na);
struct nm_bridge *netmap_init_bridges2(u_int);
void netmap_uninit_bridges2(struct nm_bridge *, u_int);
int netmap_bdg_update_private_data(const char *name, bdg_update_private_data_fn_t callback,
	void *callback_data, void *auth_token);
int netmap_bdg_config(struct nm_ifreq *nifr);
int nm_is_bwrap(struct netmap_adapter *);

#define NM_NEED_BWRAP (-2)
#endif /* _NET_NETMAP_BDG_H_ */

