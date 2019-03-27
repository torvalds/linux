/*-
 * Copyright (c) 2014, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/hash.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/refcount.h>
#include <sys/rmlock.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vxlan.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>

struct vxlan_softc;
LIST_HEAD(vxlan_softc_head, vxlan_softc);

struct vxlan_socket_mc_info {
	union vxlan_sockaddr		 vxlsomc_saddr;
	union vxlan_sockaddr		 vxlsomc_gaddr;
	int				 vxlsomc_ifidx;
	int				 vxlsomc_users;
};

#define VXLAN_SO_MC_MAX_GROUPS		32

#define VXLAN_SO_VNI_HASH_SHIFT		6
#define VXLAN_SO_VNI_HASH_SIZE		(1 << VXLAN_SO_VNI_HASH_SHIFT)
#define VXLAN_SO_VNI_HASH(_vni)		((_vni) % VXLAN_SO_VNI_HASH_SIZE)

struct vxlan_socket {
	struct socket			*vxlso_sock;
	struct rmlock			 vxlso_lock;
	u_int				 vxlso_refcnt;
	union vxlan_sockaddr		 vxlso_laddr;
	LIST_ENTRY(vxlan_socket)	 vxlso_entry;
	struct vxlan_softc_head		 vxlso_vni_hash[VXLAN_SO_VNI_HASH_SIZE];
	struct vxlan_socket_mc_info	 vxlso_mc[VXLAN_SO_MC_MAX_GROUPS];
};

#define VXLAN_SO_RLOCK(_vso, _p)	rm_rlock(&(_vso)->vxlso_lock, (_p))
#define VXLAN_SO_RUNLOCK(_vso, _p)	rm_runlock(&(_vso)->vxlso_lock, (_p))
#define VXLAN_SO_WLOCK(_vso)		rm_wlock(&(_vso)->vxlso_lock)
#define VXLAN_SO_WUNLOCK(_vso)		rm_wunlock(&(_vso)->vxlso_lock)
#define VXLAN_SO_LOCK_ASSERT(_vso) \
    rm_assert(&(_vso)->vxlso_lock, RA_LOCKED)
#define VXLAN_SO_LOCK_WASSERT(_vso) \
    rm_assert(&(_vso)->vxlso_lock, RA_WLOCKED)

#define VXLAN_SO_ACQUIRE(_vso)		refcount_acquire(&(_vso)->vxlso_refcnt)
#define VXLAN_SO_RELEASE(_vso)		refcount_release(&(_vso)->vxlso_refcnt)

struct vxlan_ftable_entry {
	LIST_ENTRY(vxlan_ftable_entry)	 vxlfe_hash;
	uint16_t			 vxlfe_flags;
	uint8_t				 vxlfe_mac[ETHER_ADDR_LEN];
	union vxlan_sockaddr		 vxlfe_raddr;
	time_t				 vxlfe_expire;
};

#define VXLAN_FE_FLAG_DYNAMIC		0x01
#define VXLAN_FE_FLAG_STATIC		0x02

#define VXLAN_FE_IS_DYNAMIC(_fe) \
    ((_fe)->vxlfe_flags & VXLAN_FE_FLAG_DYNAMIC)

#define VXLAN_SC_FTABLE_SHIFT		9
#define VXLAN_SC_FTABLE_SIZE		(1 << VXLAN_SC_FTABLE_SHIFT)
#define VXLAN_SC_FTABLE_MASK		(VXLAN_SC_FTABLE_SIZE - 1)
#define VXLAN_SC_FTABLE_HASH(_sc, _mac)	\
    (vxlan_mac_hash(_sc, _mac) % VXLAN_SC_FTABLE_SIZE)

LIST_HEAD(vxlan_ftable_head, vxlan_ftable_entry);

struct vxlan_statistics {
	uint32_t	ftable_nospace;
	uint32_t	ftable_lock_upgrade_failed;
};

struct vxlan_softc {
	struct ifnet			*vxl_ifp;
	struct vxlan_socket		*vxl_sock;
	uint32_t			 vxl_vni;
	union vxlan_sockaddr		 vxl_src_addr;
	union vxlan_sockaddr		 vxl_dst_addr;
	uint32_t			 vxl_flags;
#define VXLAN_FLAG_INIT		0x0001
#define VXLAN_FLAG_TEARDOWN	0x0002
#define VXLAN_FLAG_LEARN	0x0004

	uint32_t			 vxl_port_hash_key;
	uint16_t			 vxl_min_port;
	uint16_t			 vxl_max_port;
	uint8_t				 vxl_ttl;

	/* Lookup table from MAC address to forwarding entry. */
	uint32_t			 vxl_ftable_cnt;
	uint32_t			 vxl_ftable_max;
	uint32_t			 vxl_ftable_timeout;
	uint32_t			 vxl_ftable_hash_key;
	struct vxlan_ftable_head	*vxl_ftable;

	/* Derived from vxl_dst_addr. */
	struct vxlan_ftable_entry	 vxl_default_fe;

	struct ip_moptions		*vxl_im4o;
	struct ip6_moptions		*vxl_im6o;

	struct rmlock			 vxl_lock;
	volatile u_int			 vxl_refcnt;

	int				 vxl_unit;
	int				 vxl_vso_mc_index;
	struct vxlan_statistics		 vxl_stats;
	struct sysctl_oid		*vxl_sysctl_node;
	struct sysctl_ctx_list		 vxl_sysctl_ctx;
	struct callout			 vxl_callout;
	struct ether_addr		 vxl_hwaddr;
	int				 vxl_mc_ifindex;
	struct ifnet			*vxl_mc_ifp;
	struct ifmedia 			 vxl_media;
	char				 vxl_mc_ifname[IFNAMSIZ];
	LIST_ENTRY(vxlan_softc)		 vxl_entry;
	LIST_ENTRY(vxlan_softc)		 vxl_ifdetach_list;
};

#define VXLAN_RLOCK(_sc, _p)	rm_rlock(&(_sc)->vxl_lock, (_p))
#define VXLAN_RUNLOCK(_sc, _p)	rm_runlock(&(_sc)->vxl_lock, (_p))
#define VXLAN_WLOCK(_sc)	rm_wlock(&(_sc)->vxl_lock)
#define VXLAN_WUNLOCK(_sc)	rm_wunlock(&(_sc)->vxl_lock)
#define VXLAN_LOCK_WOWNED(_sc)	rm_wowned(&(_sc)->vxl_lock)
#define VXLAN_LOCK_ASSERT(_sc)	rm_assert(&(_sc)->vxl_lock, RA_LOCKED)
#define VXLAN_LOCK_WASSERT(_sc) rm_assert(&(_sc)->vxl_lock, RA_WLOCKED)
#define VXLAN_UNLOCK(_sc, _p) do {		\
    if (VXLAN_LOCK_WOWNED(_sc))			\
	VXLAN_WUNLOCK(_sc);			\
    else					\
	VXLAN_RUNLOCK(_sc, _p);			\
} while (0)

#define VXLAN_ACQUIRE(_sc)	refcount_acquire(&(_sc)->vxl_refcnt)
#define VXLAN_RELEASE(_sc)	refcount_release(&(_sc)->vxl_refcnt)

#define	satoconstsin(sa)	((const struct sockaddr_in *)(sa))
#define	satoconstsin6(sa)	((const struct sockaddr_in6 *)(sa))

struct vxlanudphdr {
	struct udphdr		vxlh_udp;
	struct vxlan_header	vxlh_hdr;
} __packed;

static int	vxlan_ftable_addr_cmp(const uint8_t *, const uint8_t *);
static void	vxlan_ftable_init(struct vxlan_softc *);
static void	vxlan_ftable_fini(struct vxlan_softc *);
static void	vxlan_ftable_flush(struct vxlan_softc *, int);
static void	vxlan_ftable_expire(struct vxlan_softc *);
static int	vxlan_ftable_update_locked(struct vxlan_softc *,
		    const union vxlan_sockaddr *, const uint8_t *,
		    struct rm_priotracker *);
static int	vxlan_ftable_learn(struct vxlan_softc *,
		    const struct sockaddr *, const uint8_t *);
static int	vxlan_ftable_sysctl_dump(SYSCTL_HANDLER_ARGS);

static struct vxlan_ftable_entry *
		vxlan_ftable_entry_alloc(void);
static void	vxlan_ftable_entry_free(struct vxlan_ftable_entry *);
static void	vxlan_ftable_entry_init(struct vxlan_softc *,
		    struct vxlan_ftable_entry *, const uint8_t *,
		    const struct sockaddr *, uint32_t);
static void	vxlan_ftable_entry_destroy(struct vxlan_softc *,
		    struct vxlan_ftable_entry *);
static int	vxlan_ftable_entry_insert(struct vxlan_softc *,
		    struct vxlan_ftable_entry *);
static struct vxlan_ftable_entry *
		vxlan_ftable_entry_lookup(struct vxlan_softc *,
		    const uint8_t *);
static void	vxlan_ftable_entry_dump(struct vxlan_ftable_entry *,
		    struct sbuf *);

static struct vxlan_socket *
		vxlan_socket_alloc(const union vxlan_sockaddr *);
static void	vxlan_socket_destroy(struct vxlan_socket *);
static void	vxlan_socket_release(struct vxlan_socket *);
static struct vxlan_socket *
		vxlan_socket_lookup(union vxlan_sockaddr *vxlsa);
static void	vxlan_socket_insert(struct vxlan_socket *);
static int	vxlan_socket_init(struct vxlan_socket *, struct ifnet *);
static int	vxlan_socket_bind(struct vxlan_socket *, struct ifnet *);
static int	vxlan_socket_create(struct ifnet *, int,
		    const union vxlan_sockaddr *, struct vxlan_socket **);
static void	vxlan_socket_ifdetach(struct vxlan_socket *,
		    struct ifnet *, struct vxlan_softc_head *);

static struct vxlan_socket *
		vxlan_socket_mc_lookup(const union vxlan_sockaddr *);
static int	vxlan_sockaddr_mc_info_match(
		    const struct vxlan_socket_mc_info *,
		    const union vxlan_sockaddr *,
		    const union vxlan_sockaddr *, int);
static int	vxlan_socket_mc_join_group(struct vxlan_socket *,
		    const union vxlan_sockaddr *, const union vxlan_sockaddr *,
		    int *, union vxlan_sockaddr *);
static int	vxlan_socket_mc_leave_group(struct vxlan_socket *,
		    const union vxlan_sockaddr *,
		    const union vxlan_sockaddr *, int);
static int	vxlan_socket_mc_add_group(struct vxlan_socket *,
		    const union vxlan_sockaddr *, const union vxlan_sockaddr *,
		    int, int *);
static void	vxlan_socket_mc_release_group_by_idx(struct vxlan_socket *,
		    int);

static struct vxlan_softc *
		vxlan_socket_lookup_softc_locked(struct vxlan_socket *,
		    uint32_t);
static struct vxlan_softc *
		vxlan_socket_lookup_softc(struct vxlan_socket *, uint32_t);
static int	vxlan_socket_insert_softc(struct vxlan_socket *,
		    struct vxlan_softc *);
static void	vxlan_socket_remove_softc(struct vxlan_socket *,
		    struct vxlan_softc *);

static struct ifnet *
		vxlan_multicast_if_ref(struct vxlan_softc *, int);
static void	vxlan_free_multicast(struct vxlan_softc *);
static int	vxlan_setup_multicast_interface(struct vxlan_softc *);

static int	vxlan_setup_multicast(struct vxlan_softc *);
static int	vxlan_setup_socket(struct vxlan_softc *);
static void	vxlan_setup_interface(struct vxlan_softc *);
static int	vxlan_valid_init_config(struct vxlan_softc *);
static void	vxlan_init_wait(struct vxlan_softc *);
static void	vxlan_init_complete(struct vxlan_softc *);
static void	vxlan_init(void *);
static void	vxlan_release(struct vxlan_softc *);
static void	vxlan_teardown_wait(struct vxlan_softc *);
static void	vxlan_teardown_complete(struct vxlan_softc *);
static void	vxlan_teardown_locked(struct vxlan_softc *);
static void	vxlan_teardown(struct vxlan_softc *);
static void	vxlan_ifdetach(struct vxlan_softc *, struct ifnet *,
		    struct vxlan_softc_head *);
static void	vxlan_timer(void *);

static int	vxlan_ctrl_get_config(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_vni(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_local_addr(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_remote_addr(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_local_port(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_remote_port(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_port_range(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_ftable_timeout(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_ftable_max(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_multicast_if(struct vxlan_softc * , void *);
static int	vxlan_ctrl_set_ttl(struct vxlan_softc *, void *);
static int	vxlan_ctrl_set_learn(struct vxlan_softc *, void *);
static int	vxlan_ctrl_ftable_entry_add(struct vxlan_softc *, void *);
static int	vxlan_ctrl_ftable_entry_rem(struct vxlan_softc *, void *);
static int	vxlan_ctrl_flush(struct vxlan_softc *, void *);
static int	vxlan_ioctl_drvspec(struct vxlan_softc *,
		    struct ifdrv *, int);
static int	vxlan_ioctl_ifflags(struct vxlan_softc *);
static int	vxlan_ioctl(struct ifnet *, u_long, caddr_t);

#if defined(INET) || defined(INET6)
static uint16_t vxlan_pick_source_port(struct vxlan_softc *, struct mbuf *);
static void	vxlan_encap_header(struct vxlan_softc *, struct mbuf *,
		    int, uint16_t, uint16_t);
#endif
static int	vxlan_encap4(struct vxlan_softc *,
		    const union vxlan_sockaddr *, struct mbuf *);
static int	vxlan_encap6(struct vxlan_softc *,
		    const union vxlan_sockaddr *, struct mbuf *);
static int	vxlan_transmit(struct ifnet *, struct mbuf *);
static void	vxlan_qflush(struct ifnet *);
static void	vxlan_rcv_udp_packet(struct mbuf *, int, struct inpcb *,
		    const struct sockaddr *, void *);
static int	vxlan_input(struct vxlan_socket *, uint32_t, struct mbuf **,
		    const struct sockaddr *);

static void	vxlan_set_default_config(struct vxlan_softc *);
static int	vxlan_set_user_config(struct vxlan_softc *,
		     struct ifvxlanparam *);
static int	vxlan_clone_create(struct if_clone *, int, caddr_t);
static void	vxlan_clone_destroy(struct ifnet *);

static uint32_t vxlan_mac_hash(struct vxlan_softc *, const uint8_t *);
static int	vxlan_media_change(struct ifnet *);
static void	vxlan_media_status(struct ifnet *, struct ifmediareq *);

static int	vxlan_sockaddr_cmp(const union vxlan_sockaddr *,
		    const struct sockaddr *);
static void	vxlan_sockaddr_copy(union vxlan_sockaddr *,
		    const struct sockaddr *);
static int	vxlan_sockaddr_in_equal(const union vxlan_sockaddr *,
		    const struct sockaddr *);
static void	vxlan_sockaddr_in_copy(union vxlan_sockaddr *,
		    const struct sockaddr *);
static int	vxlan_sockaddr_supported(const union vxlan_sockaddr *, int);
static int	vxlan_sockaddr_in_any(const union vxlan_sockaddr *);
static int	vxlan_sockaddr_in_multicast(const union vxlan_sockaddr *);
static int	vxlan_sockaddr_in6_embedscope(union vxlan_sockaddr *);

static int	vxlan_can_change_config(struct vxlan_softc *);
static int	vxlan_check_vni(uint32_t);
static int	vxlan_check_ttl(int);
static int	vxlan_check_ftable_timeout(uint32_t);
static int	vxlan_check_ftable_max(uint32_t);

static void	vxlan_sysctl_setup(struct vxlan_softc *);
static void	vxlan_sysctl_destroy(struct vxlan_softc *);
static int	vxlan_tunable_int(struct vxlan_softc *, const char *, int);

static void	vxlan_ifdetach_event(void *, struct ifnet *);
static void	vxlan_load(void);
static void	vxlan_unload(void);
static int	vxlan_modevent(module_t, int, void *);

static const char vxlan_name[] = "vxlan";
static MALLOC_DEFINE(M_VXLAN, vxlan_name,
    "Virtual eXtensible LAN Interface");
static struct if_clone *vxlan_cloner;

static struct mtx vxlan_list_mtx;
#define VXLAN_LIST_LOCK()	mtx_lock(&vxlan_list_mtx)
#define VXLAN_LIST_UNLOCK()	mtx_unlock(&vxlan_list_mtx)

static LIST_HEAD(, vxlan_socket) vxlan_socket_list;

static eventhandler_tag vxlan_ifdetach_event_tag;

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, OID_AUTO, vxlan, CTLFLAG_RW, 0,
    "Virtual eXtensible Local Area Network");

static int vxlan_legacy_port = 0;
TUNABLE_INT("net.link.vxlan.legacy_port", &vxlan_legacy_port);
static int vxlan_reuse_port = 0;
TUNABLE_INT("net.link.vxlan.reuse_port", &vxlan_reuse_port);

/* Default maximum number of addresses in the forwarding table. */
#ifndef VXLAN_FTABLE_MAX
#define VXLAN_FTABLE_MAX	2000
#endif

/* Timeout (in seconds) of addresses learned in the forwarding table. */
#ifndef VXLAN_FTABLE_TIMEOUT
#define VXLAN_FTABLE_TIMEOUT	(20 * 60)
#endif

/*
 * Maximum timeout (in seconds) of addresses learned in the forwarding
 * table.
 */
#ifndef VXLAN_FTABLE_MAX_TIMEOUT
#define VXLAN_FTABLE_MAX_TIMEOUT	(60 * 60 * 24)
#endif

/* Number of seconds between pruning attempts of the forwarding table. */
#ifndef VXLAN_FTABLE_PRUNE
#define VXLAN_FTABLE_PRUNE	(5 * 60)
#endif

static int vxlan_ftable_prune_period = VXLAN_FTABLE_PRUNE;

struct vxlan_control {
	int	(*vxlc_func)(struct vxlan_softc *, void *);
	int	vxlc_argsize;
	int	vxlc_flags;
#define VXLAN_CTRL_FLAG_COPYIN	0x01
#define VXLAN_CTRL_FLAG_COPYOUT	0x02
#define VXLAN_CTRL_FLAG_SUSER	0x04
};

static const struct vxlan_control vxlan_control_table[] = {
	[VXLAN_CMD_GET_CONFIG] =
	    {	vxlan_ctrl_get_config, sizeof(struct ifvxlancfg),
		VXLAN_CTRL_FLAG_COPYOUT
	    },

	[VXLAN_CMD_SET_VNI] =
	    {   vxlan_ctrl_set_vni, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_LOCAL_ADDR] =
	    {   vxlan_ctrl_set_local_addr, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_REMOTE_ADDR] =
	    {   vxlan_ctrl_set_remote_addr, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_LOCAL_PORT] =
	    {   vxlan_ctrl_set_local_port, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_REMOTE_PORT] =
	    {   vxlan_ctrl_set_remote_port, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_PORT_RANGE] =
	    {   vxlan_ctrl_set_port_range, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_FTABLE_TIMEOUT] =
	    {	vxlan_ctrl_set_ftable_timeout, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_FTABLE_MAX] =
	    {	vxlan_ctrl_set_ftable_max, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_MULTICAST_IF] =
	    {	vxlan_ctrl_set_multicast_if, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_TTL] =
	    {	vxlan_ctrl_set_ttl, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_SET_LEARN] =
	    {	vxlan_ctrl_set_learn, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_FTABLE_ENTRY_ADD] =
	    {	vxlan_ctrl_ftable_entry_add, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_FTABLE_ENTRY_REM] =
	    {	vxlan_ctrl_ftable_entry_rem, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },

	[VXLAN_CMD_FLUSH] =
	    {   vxlan_ctrl_flush, sizeof(struct ifvxlancmd),
		VXLAN_CTRL_FLAG_COPYIN | VXLAN_CTRL_FLAG_SUSER,
	    },
};

static const int vxlan_control_table_size = nitems(vxlan_control_table);

static int
vxlan_ftable_addr_cmp(const uint8_t *a, const uint8_t *b)
{
	int i, d;

	for (i = 0, d = 0; i < ETHER_ADDR_LEN && d == 0; i++)
		d = ((int)a[i]) - ((int)b[i]);

	return (d);
}

static void
vxlan_ftable_init(struct vxlan_softc *sc)
{
	int i;

	sc->vxl_ftable = malloc(sizeof(struct vxlan_ftable_head) *
	    VXLAN_SC_FTABLE_SIZE, M_VXLAN, M_ZERO | M_WAITOK);

	for (i = 0; i < VXLAN_SC_FTABLE_SIZE; i++)
		LIST_INIT(&sc->vxl_ftable[i]);
	sc->vxl_ftable_hash_key = arc4random();
}

static void
vxlan_ftable_fini(struct vxlan_softc *sc)
{
	int i;

	for (i = 0; i < VXLAN_SC_FTABLE_SIZE; i++) {
		KASSERT(LIST_EMPTY(&sc->vxl_ftable[i]),
		    ("%s: vxlan %p ftable[%d] not empty", __func__, sc, i));
	}
	MPASS(sc->vxl_ftable_cnt == 0);

	free(sc->vxl_ftable, M_VXLAN);
	sc->vxl_ftable = NULL;
}

static void
vxlan_ftable_flush(struct vxlan_softc *sc, int all)
{
	struct vxlan_ftable_entry *fe, *tfe;
	int i;

	for (i = 0; i < VXLAN_SC_FTABLE_SIZE; i++) {
		LIST_FOREACH_SAFE(fe, &sc->vxl_ftable[i], vxlfe_hash, tfe) {
			if (all || VXLAN_FE_IS_DYNAMIC(fe))
				vxlan_ftable_entry_destroy(sc, fe);
		}
	}
}

static void
vxlan_ftable_expire(struct vxlan_softc *sc)
{
	struct vxlan_ftable_entry *fe, *tfe;
	int i;

	VXLAN_LOCK_WASSERT(sc);

	for (i = 0; i < VXLAN_SC_FTABLE_SIZE; i++) {
		LIST_FOREACH_SAFE(fe, &sc->vxl_ftable[i], vxlfe_hash, tfe) {
			if (VXLAN_FE_IS_DYNAMIC(fe) &&
			    time_uptime >= fe->vxlfe_expire)
				vxlan_ftable_entry_destroy(sc, fe);
		}
	}
}

static int
vxlan_ftable_update_locked(struct vxlan_softc *sc,
    const union vxlan_sockaddr *vxlsa, const uint8_t *mac,
    struct rm_priotracker *tracker)
{
	struct vxlan_ftable_entry *fe;
	int error __unused;

	VXLAN_LOCK_ASSERT(sc);

again:
	/*
	 * A forwarding entry for this MAC address might already exist. If
	 * so, update it, otherwise create a new one. We may have to upgrade
	 * the lock if we have to change or create an entry.
	 */
	fe = vxlan_ftable_entry_lookup(sc, mac);
	if (fe != NULL) {
		fe->vxlfe_expire = time_uptime + sc->vxl_ftable_timeout;

		if (!VXLAN_FE_IS_DYNAMIC(fe) ||
		    vxlan_sockaddr_in_equal(&fe->vxlfe_raddr, &vxlsa->sa))
			return (0);
		if (!VXLAN_LOCK_WOWNED(sc)) {
			VXLAN_RUNLOCK(sc, tracker);
			VXLAN_WLOCK(sc);
			sc->vxl_stats.ftable_lock_upgrade_failed++;
			goto again;
		}
		vxlan_sockaddr_in_copy(&fe->vxlfe_raddr, &vxlsa->sa);
		return (0);
	}

	if (!VXLAN_LOCK_WOWNED(sc)) {
		VXLAN_RUNLOCK(sc, tracker);
		VXLAN_WLOCK(sc);
		sc->vxl_stats.ftable_lock_upgrade_failed++;
		goto again;
	}

	if (sc->vxl_ftable_cnt >= sc->vxl_ftable_max) {
		sc->vxl_stats.ftable_nospace++;
		return (ENOSPC);
	}

	fe = vxlan_ftable_entry_alloc();
	if (fe == NULL)
		return (ENOMEM);

	vxlan_ftable_entry_init(sc, fe, mac, &vxlsa->sa, VXLAN_FE_FLAG_DYNAMIC);

	/* The prior lookup failed, so the insert should not. */
	error = vxlan_ftable_entry_insert(sc, fe);
	MPASS(error == 0);

	return (0);
}

static int
vxlan_ftable_learn(struct vxlan_softc *sc, const struct sockaddr *sa,
    const uint8_t *mac)
{
	struct rm_priotracker tracker;
	union vxlan_sockaddr vxlsa;
	int error;

	/*
	 * The source port may be randomly selected by the remote host, so
	 * use the port of the default destination address.
	 */
	vxlan_sockaddr_copy(&vxlsa, sa);
	vxlsa.in4.sin_port = sc->vxl_dst_addr.in4.sin_port;

	if (VXLAN_SOCKADDR_IS_IPV6(&vxlsa)) {
		error = vxlan_sockaddr_in6_embedscope(&vxlsa);
		if (error)
			return (error);
	}

	VXLAN_RLOCK(sc, &tracker);
	error = vxlan_ftable_update_locked(sc, &vxlsa, mac, &tracker);
	VXLAN_UNLOCK(sc, &tracker);

	return (error);
}

static int
vxlan_ftable_sysctl_dump(SYSCTL_HANDLER_ARGS)
{
	struct rm_priotracker tracker;
	struct sbuf sb;
	struct vxlan_softc *sc;
	struct vxlan_ftable_entry *fe;
	size_t size;
	int i, error;

	/*
	 * This is mostly intended for debugging during development. It is
	 * not practical to dump an entire large table this way.
	 */

	sc = arg1;
	size = PAGE_SIZE;	/* Calculate later. */

	sbuf_new(&sb, NULL, size, SBUF_FIXEDLEN);
	sbuf_putc(&sb, '\n');

	VXLAN_RLOCK(sc, &tracker);
	for (i = 0; i < VXLAN_SC_FTABLE_SIZE; i++) {
		LIST_FOREACH(fe, &sc->vxl_ftable[i], vxlfe_hash) {
			if (sbuf_error(&sb) != 0)
				break;
			vxlan_ftable_entry_dump(fe, &sb);
		}
	}
	VXLAN_RUNLOCK(sc, &tracker);

	if (sbuf_len(&sb) == 1)
		sbuf_setpos(&sb, 0);

	sbuf_finish(&sb);
	error = sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
	sbuf_delete(&sb);

	return (error);
}

static struct vxlan_ftable_entry *
vxlan_ftable_entry_alloc(void)
{
	struct vxlan_ftable_entry *fe;

	fe = malloc(sizeof(*fe), M_VXLAN, M_ZERO | M_NOWAIT);

	return (fe);
}

static void
vxlan_ftable_entry_free(struct vxlan_ftable_entry *fe)
{

	free(fe, M_VXLAN);
}

static void
vxlan_ftable_entry_init(struct vxlan_softc *sc, struct vxlan_ftable_entry *fe,
    const uint8_t *mac, const struct sockaddr *sa, uint32_t flags)
{

	fe->vxlfe_flags = flags;
	fe->vxlfe_expire = time_uptime + sc->vxl_ftable_timeout;
	memcpy(fe->vxlfe_mac, mac, ETHER_ADDR_LEN);
	vxlan_sockaddr_copy(&fe->vxlfe_raddr, sa);
}

static void
vxlan_ftable_entry_destroy(struct vxlan_softc *sc,
    struct vxlan_ftable_entry *fe)
{

	sc->vxl_ftable_cnt--;
	LIST_REMOVE(fe, vxlfe_hash);
	vxlan_ftable_entry_free(fe);
}

static int
vxlan_ftable_entry_insert(struct vxlan_softc *sc,
    struct vxlan_ftable_entry *fe)
{
	struct vxlan_ftable_entry *lfe;
	uint32_t hash;
	int dir;

	VXLAN_LOCK_WASSERT(sc);
	hash = VXLAN_SC_FTABLE_HASH(sc, fe->vxlfe_mac);

	lfe = LIST_FIRST(&sc->vxl_ftable[hash]);
	if (lfe == NULL) {
		LIST_INSERT_HEAD(&sc->vxl_ftable[hash], fe, vxlfe_hash);
		goto out;
	}

	do {
		dir = vxlan_ftable_addr_cmp(fe->vxlfe_mac, lfe->vxlfe_mac);
		if (dir == 0)
			return (EEXIST);
		if (dir > 0) {
			LIST_INSERT_BEFORE(lfe, fe, vxlfe_hash);
			goto out;
		} else if (LIST_NEXT(lfe, vxlfe_hash) == NULL) {
			LIST_INSERT_AFTER(lfe, fe, vxlfe_hash);
			goto out;
		} else
			lfe = LIST_NEXT(lfe, vxlfe_hash);
	} while (lfe != NULL);

out:
	sc->vxl_ftable_cnt++;

	return (0);
}

static struct vxlan_ftable_entry *
vxlan_ftable_entry_lookup(struct vxlan_softc *sc, const uint8_t *mac)
{
	struct vxlan_ftable_entry *fe;
	uint32_t hash;
	int dir;

	VXLAN_LOCK_ASSERT(sc);
	hash = VXLAN_SC_FTABLE_HASH(sc, mac);

	LIST_FOREACH(fe, &sc->vxl_ftable[hash], vxlfe_hash) {
		dir = vxlan_ftable_addr_cmp(mac, fe->vxlfe_mac);
		if (dir == 0)
			return (fe);
		if (dir > 0)
			break;
	}

	return (NULL);
}

static void
vxlan_ftable_entry_dump(struct vxlan_ftable_entry *fe, struct sbuf *sb)
{
	char buf[64];
	const union vxlan_sockaddr *sa;
	const void *addr;
	int i, len, af, width;

	sa = &fe->vxlfe_raddr;
	af = sa->sa.sa_family;
	len = sbuf_len(sb);

	sbuf_printf(sb, "%c 0x%02X ", VXLAN_FE_IS_DYNAMIC(fe) ? 'D' : 'S',
	    fe->vxlfe_flags);

	for (i = 0; i < ETHER_ADDR_LEN - 1; i++)
		sbuf_printf(sb, "%02X:", fe->vxlfe_mac[i]);
	sbuf_printf(sb, "%02X ", fe->vxlfe_mac[i]);

	if (af == AF_INET) {
		addr = &sa->in4.sin_addr;
		width = INET_ADDRSTRLEN - 1;
	} else {
		addr = &sa->in6.sin6_addr;
		width = INET6_ADDRSTRLEN - 1;
	}
	inet_ntop(af, addr, buf, sizeof(buf));
	sbuf_printf(sb, "%*s ", width, buf);

	sbuf_printf(sb, "%08jd", (intmax_t)fe->vxlfe_expire);

	sbuf_putc(sb, '\n');

	/* Truncate a partial line. */
	if (sbuf_error(sb) != 0)
		sbuf_setpos(sb, len);
}

static struct vxlan_socket *
vxlan_socket_alloc(const union vxlan_sockaddr *sa)
{
	struct vxlan_socket *vso;
	int i;

	vso = malloc(sizeof(*vso), M_VXLAN, M_WAITOK | M_ZERO);
	rm_init(&vso->vxlso_lock, "vxlansorm");
	refcount_init(&vso->vxlso_refcnt, 0);
	for (i = 0; i < VXLAN_SO_VNI_HASH_SIZE; i++)
		LIST_INIT(&vso->vxlso_vni_hash[i]);
	vso->vxlso_laddr = *sa;

	return (vso);
}

static void
vxlan_socket_destroy(struct vxlan_socket *vso)
{
	struct socket *so;
#ifdef INVARIANTS
	int i;
	struct vxlan_socket_mc_info *mc;

	for (i = 0; i < VXLAN_SO_MC_MAX_GROUPS; i++) {
		mc = &vso->vxlso_mc[i];
		KASSERT(mc->vxlsomc_gaddr.sa.sa_family == AF_UNSPEC,
		    ("%s: socket %p mc[%d] still has address",
		     __func__, vso, i));
	}

	for (i = 0; i < VXLAN_SO_VNI_HASH_SIZE; i++) {
		KASSERT(LIST_EMPTY(&vso->vxlso_vni_hash[i]),
		    ("%s: socket %p vni_hash[%d] not empty",
		     __func__, vso, i));
	}
#endif
	so = vso->vxlso_sock;
	if (so != NULL) {
		vso->vxlso_sock = NULL;
		soclose(so);
	}

	rm_destroy(&vso->vxlso_lock);
	free(vso, M_VXLAN);
}

static void
vxlan_socket_release(struct vxlan_socket *vso)
{
	int destroy;

	VXLAN_LIST_LOCK();
	destroy = VXLAN_SO_RELEASE(vso);
	if (destroy != 0)
		LIST_REMOVE(vso, vxlso_entry);
	VXLAN_LIST_UNLOCK();

	if (destroy != 0)
		vxlan_socket_destroy(vso);
}

static struct vxlan_socket *
vxlan_socket_lookup(union vxlan_sockaddr *vxlsa)
{
	struct vxlan_socket *vso;

	VXLAN_LIST_LOCK();
	LIST_FOREACH(vso, &vxlan_socket_list, vxlso_entry) {
		if (vxlan_sockaddr_cmp(&vso->vxlso_laddr, &vxlsa->sa) == 0) {
			VXLAN_SO_ACQUIRE(vso);
			break;
		}
	}
	VXLAN_LIST_UNLOCK();

	return (vso);
}

static void
vxlan_socket_insert(struct vxlan_socket *vso)
{

	VXLAN_LIST_LOCK();
	VXLAN_SO_ACQUIRE(vso);
	LIST_INSERT_HEAD(&vxlan_socket_list, vso, vxlso_entry);
	VXLAN_LIST_UNLOCK();
}

static int
vxlan_socket_init(struct vxlan_socket *vso, struct ifnet *ifp)
{
	struct thread *td;
	int error;

	td = curthread;

	error = socreate(vso->vxlso_laddr.sa.sa_family, &vso->vxlso_sock,
	    SOCK_DGRAM, IPPROTO_UDP, td->td_ucred, td);
	if (error) {
		if_printf(ifp, "cannot create socket: %d\n", error);
		return (error);
	}

	error = udp_set_kernel_tunneling(vso->vxlso_sock,
	    vxlan_rcv_udp_packet, NULL, vso);
	if (error) {
		if_printf(ifp, "cannot set tunneling function: %d\n", error);
		return (error);
	}

	if (vxlan_reuse_port != 0) {
		struct sockopt sopt;
		int val = 1;

		bzero(&sopt, sizeof(sopt));
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = IPPROTO_IP;
		sopt.sopt_name = SO_REUSEPORT;
		sopt.sopt_val = &val;
		sopt.sopt_valsize = sizeof(val);
		error = sosetopt(vso->vxlso_sock, &sopt);
		if (error) {
			if_printf(ifp,
			    "cannot set REUSEADDR socket opt: %d\n", error);
			return (error);
		}
	}

	return (0);
}

static int
vxlan_socket_bind(struct vxlan_socket *vso, struct ifnet *ifp)
{
	union vxlan_sockaddr laddr;
	struct thread *td;
	int error;

	td = curthread;
	laddr = vso->vxlso_laddr;

	error = sobind(vso->vxlso_sock, &laddr.sa, td);
	if (error) {
		if (error != EADDRINUSE)
			if_printf(ifp, "cannot bind socket: %d\n", error);
		return (error);
	}

	return (0);
}

static int
vxlan_socket_create(struct ifnet *ifp, int multicast,
    const union vxlan_sockaddr *saddr, struct vxlan_socket **vsop)
{
	union vxlan_sockaddr laddr;
	struct vxlan_socket *vso;
	int error;

	laddr = *saddr;

	/*
	 * If this socket will be multicast, then only the local port
	 * must be specified when binding.
	 */
	if (multicast != 0) {
		if (VXLAN_SOCKADDR_IS_IPV4(&laddr))
			laddr.in4.sin_addr.s_addr = INADDR_ANY;
#ifdef INET6
		else
			laddr.in6.sin6_addr = in6addr_any;
#endif
	}

	vso = vxlan_socket_alloc(&laddr);
	if (vso == NULL)
		return (ENOMEM);

	error = vxlan_socket_init(vso, ifp);
	if (error)
		goto fail;

	error = vxlan_socket_bind(vso, ifp);
	if (error)
		goto fail;

	/*
	 * There is a small window between the bind completing and
	 * inserting the socket, so that a concurrent create may fail.
	 * Let's not worry about that for now.
	 */
	vxlan_socket_insert(vso);
	*vsop = vso;

	return (0);

fail:
	vxlan_socket_destroy(vso);

	return (error);
}

static void
vxlan_socket_ifdetach(struct vxlan_socket *vso, struct ifnet *ifp,
    struct vxlan_softc_head *list)
{
	struct rm_priotracker tracker;
	struct vxlan_softc *sc;
	int i;

	VXLAN_SO_RLOCK(vso, &tracker);
	for (i = 0; i < VXLAN_SO_VNI_HASH_SIZE; i++) {
		LIST_FOREACH(sc, &vso->vxlso_vni_hash[i], vxl_entry)
			vxlan_ifdetach(sc, ifp, list);
	}
	VXLAN_SO_RUNLOCK(vso, &tracker);
}

static struct vxlan_socket *
vxlan_socket_mc_lookup(const union vxlan_sockaddr *vxlsa)
{
	union vxlan_sockaddr laddr;
	struct vxlan_socket *vso;

	laddr = *vxlsa;

	if (VXLAN_SOCKADDR_IS_IPV4(&laddr))
		laddr.in4.sin_addr.s_addr = INADDR_ANY;
#ifdef INET6
	else
		laddr.in6.sin6_addr = in6addr_any;
#endif

	vso = vxlan_socket_lookup(&laddr);

	return (vso);
}

static int
vxlan_sockaddr_mc_info_match(const struct vxlan_socket_mc_info *mc,
    const union vxlan_sockaddr *group, const union vxlan_sockaddr *local,
    int ifidx)
{

	if (!vxlan_sockaddr_in_any(local) &&
	    !vxlan_sockaddr_in_equal(&mc->vxlsomc_saddr, &local->sa))
		return (0);
	if (!vxlan_sockaddr_in_equal(&mc->vxlsomc_gaddr, &group->sa))
		return (0);
	if (ifidx != 0 && ifidx != mc->vxlsomc_ifidx)
		return (0);

	return (1);
}

static int
vxlan_socket_mc_join_group(struct vxlan_socket *vso,
    const union vxlan_sockaddr *group, const union vxlan_sockaddr *local,
    int *ifidx, union vxlan_sockaddr *source)
{
	struct sockopt sopt;
	int error;

	*source = *local;

	if (VXLAN_SOCKADDR_IS_IPV4(group)) {
		struct ip_mreq mreq;

		mreq.imr_multiaddr = group->in4.sin_addr;
		mreq.imr_interface = local->in4.sin_addr;

		bzero(&sopt, sizeof(sopt));
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = IPPROTO_IP;
		sopt.sopt_name = IP_ADD_MEMBERSHIP;
		sopt.sopt_val = &mreq;
		sopt.sopt_valsize = sizeof(mreq);
		error = sosetopt(vso->vxlso_sock, &sopt);
		if (error)
			return (error);

		/*
		 * BMV: Ideally, there would be a formal way for us to get
		 * the local interface that was selected based on the
		 * imr_interface address. We could then update *ifidx so
		 * vxlan_sockaddr_mc_info_match() would return a match for
		 * later creates that explicitly set the multicast interface.
		 *
		 * If we really need to, we can of course look in the INP's
		 * membership list:
		 *     sotoinpcb(vso->vxlso_sock)->inp_moptions->
		 *         imo_membership[]->inm_ifp
		 * similarly to imo_match_group().
		 */
		source->in4.sin_addr = local->in4.sin_addr;

	} else if (VXLAN_SOCKADDR_IS_IPV6(group)) {
		struct ipv6_mreq mreq;

		mreq.ipv6mr_multiaddr = group->in6.sin6_addr;
		mreq.ipv6mr_interface = *ifidx;

		bzero(&sopt, sizeof(sopt));
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = IPPROTO_IPV6;
		sopt.sopt_name = IPV6_JOIN_GROUP;
		sopt.sopt_val = &mreq;
		sopt.sopt_valsize = sizeof(mreq);
		error = sosetopt(vso->vxlso_sock, &sopt);
		if (error)
			return (error);

		/*
		 * BMV: As with IPv4, we would really like to know what
		 * interface in6p_lookup_mcast_ifp() selected.
		 */
	} else
		error = EAFNOSUPPORT;

	return (error);
}

static int
vxlan_socket_mc_leave_group(struct vxlan_socket *vso,
    const union vxlan_sockaddr *group, const union vxlan_sockaddr *source,
    int ifidx)
{
	struct sockopt sopt;
	int error;

	bzero(&sopt, sizeof(sopt));
	sopt.sopt_dir = SOPT_SET;

	if (VXLAN_SOCKADDR_IS_IPV4(group)) {
		struct ip_mreq mreq;

		mreq.imr_multiaddr = group->in4.sin_addr;
		mreq.imr_interface = source->in4.sin_addr;

		sopt.sopt_level = IPPROTO_IP;
		sopt.sopt_name = IP_DROP_MEMBERSHIP;
		sopt.sopt_val = &mreq;
		sopt.sopt_valsize = sizeof(mreq);
		error = sosetopt(vso->vxlso_sock, &sopt);

	} else if (VXLAN_SOCKADDR_IS_IPV6(group)) {
		struct ipv6_mreq mreq;

		mreq.ipv6mr_multiaddr = group->in6.sin6_addr;
		mreq.ipv6mr_interface = ifidx;

		sopt.sopt_level = IPPROTO_IPV6;
		sopt.sopt_name = IPV6_LEAVE_GROUP;
		sopt.sopt_val = &mreq;
		sopt.sopt_valsize = sizeof(mreq);
		error = sosetopt(vso->vxlso_sock, &sopt);

	} else
		error = EAFNOSUPPORT;

	return (error);
}

static int
vxlan_socket_mc_add_group(struct vxlan_socket *vso,
    const union vxlan_sockaddr *group, const union vxlan_sockaddr *local,
    int ifidx, int *idx)
{
	union vxlan_sockaddr source;
	struct vxlan_socket_mc_info *mc;
	int i, empty, error;

	/*
	 * Within a socket, the same multicast group may be used by multiple
	 * interfaces, each with a different network identifier. But a socket
	 * may only join a multicast group once, so keep track of the users
	 * here.
	 */

	VXLAN_SO_WLOCK(vso);
	for (empty = 0, i = 0; i < VXLAN_SO_MC_MAX_GROUPS; i++) {
		mc = &vso->vxlso_mc[i];

		if (mc->vxlsomc_gaddr.sa.sa_family == AF_UNSPEC) {
			empty++;
			continue;
		}

		if (vxlan_sockaddr_mc_info_match(mc, group, local, ifidx))
			goto out;
	}
	VXLAN_SO_WUNLOCK(vso);

	if (empty == 0)
		return (ENOSPC);

	error = vxlan_socket_mc_join_group(vso, group, local, &ifidx, &source);
	if (error)
		return (error);

	VXLAN_SO_WLOCK(vso);
	for (i = 0; i < VXLAN_SO_MC_MAX_GROUPS; i++) {
		mc = &vso->vxlso_mc[i];

		if (mc->vxlsomc_gaddr.sa.sa_family == AF_UNSPEC) {
			vxlan_sockaddr_copy(&mc->vxlsomc_gaddr, &group->sa);
			vxlan_sockaddr_copy(&mc->vxlsomc_saddr, &source.sa);
			mc->vxlsomc_ifidx = ifidx;
			goto out;
		}
	}
	VXLAN_SO_WUNLOCK(vso);

	error = vxlan_socket_mc_leave_group(vso, group, &source, ifidx);
	MPASS(error == 0);

	return (ENOSPC);

out:
	mc->vxlsomc_users++;
	VXLAN_SO_WUNLOCK(vso);

	*idx = i;

	return (0);
}

static void
vxlan_socket_mc_release_group_by_idx(struct vxlan_socket *vso, int idx)
{
	union vxlan_sockaddr group, source;
	struct vxlan_socket_mc_info *mc;
	int ifidx, leave;

	KASSERT(idx >= 0 && idx < VXLAN_SO_MC_MAX_GROUPS,
	    ("%s: vso %p idx %d out of bounds", __func__, vso, idx));

	leave = 0;
	mc = &vso->vxlso_mc[idx];

	VXLAN_SO_WLOCK(vso);
	mc->vxlsomc_users--;
	if (mc->vxlsomc_users == 0) {
		group = mc->vxlsomc_gaddr;
		source = mc->vxlsomc_saddr;
		ifidx = mc->vxlsomc_ifidx;
		bzero(mc, sizeof(*mc));
		leave = 1;
	}
	VXLAN_SO_WUNLOCK(vso);

	if (leave != 0) {
		/*
		 * Our socket's membership in this group may have already
		 * been removed if we joined through an interface that's
		 * been detached.
		 */
		vxlan_socket_mc_leave_group(vso, &group, &source, ifidx);
	}
}

static struct vxlan_softc *
vxlan_socket_lookup_softc_locked(struct vxlan_socket *vso, uint32_t vni)
{
	struct vxlan_softc *sc;
	uint32_t hash;

	VXLAN_SO_LOCK_ASSERT(vso);
	hash = VXLAN_SO_VNI_HASH(vni);

	LIST_FOREACH(sc, &vso->vxlso_vni_hash[hash], vxl_entry) {
		if (sc->vxl_vni == vni) {
			VXLAN_ACQUIRE(sc);
			break;
		}
	}

	return (sc);
}

static struct vxlan_softc *
vxlan_socket_lookup_softc(struct vxlan_socket *vso, uint32_t vni)
{
	struct rm_priotracker tracker;
	struct vxlan_softc *sc;

	VXLAN_SO_RLOCK(vso, &tracker);
	sc = vxlan_socket_lookup_softc_locked(vso, vni);
	VXLAN_SO_RUNLOCK(vso, &tracker);

	return (sc);
}

static int
vxlan_socket_insert_softc(struct vxlan_socket *vso, struct vxlan_softc *sc)
{
	struct vxlan_softc *tsc;
	uint32_t vni, hash;

	vni = sc->vxl_vni;
	hash = VXLAN_SO_VNI_HASH(vni);

	VXLAN_SO_WLOCK(vso);
	tsc = vxlan_socket_lookup_softc_locked(vso, vni);
	if (tsc != NULL) {
		VXLAN_SO_WUNLOCK(vso);
		vxlan_release(tsc);
		return (EEXIST);
	}

	VXLAN_ACQUIRE(sc);
	LIST_INSERT_HEAD(&vso->vxlso_vni_hash[hash], sc, vxl_entry);
	VXLAN_SO_WUNLOCK(vso);

	return (0);
}

static void
vxlan_socket_remove_softc(struct vxlan_socket *vso, struct vxlan_softc *sc)
{

	VXLAN_SO_WLOCK(vso);
	LIST_REMOVE(sc, vxl_entry);
	VXLAN_SO_WUNLOCK(vso);

	vxlan_release(sc);
}

static struct ifnet *
vxlan_multicast_if_ref(struct vxlan_softc *sc, int ipv4)
{
	struct ifnet *ifp;

	VXLAN_LOCK_ASSERT(sc);

	if (ipv4 && sc->vxl_im4o != NULL)
		ifp = sc->vxl_im4o->imo_multicast_ifp;
	else if (!ipv4 && sc->vxl_im6o != NULL)
		ifp = sc->vxl_im6o->im6o_multicast_ifp;
	else
		ifp = NULL;

	if (ifp != NULL)
		if_ref(ifp);

	return (ifp);
}

static void
vxlan_free_multicast(struct vxlan_softc *sc)
{

	if (sc->vxl_mc_ifp != NULL) {
		if_rele(sc->vxl_mc_ifp);
		sc->vxl_mc_ifp = NULL;
		sc->vxl_mc_ifindex = 0;
	}

	if (sc->vxl_im4o != NULL) {
		free(sc->vxl_im4o, M_VXLAN);
		sc->vxl_im4o = NULL;
	}

	if (sc->vxl_im6o != NULL) {
		free(sc->vxl_im6o, M_VXLAN);
		sc->vxl_im6o = NULL;
	}
}

static int
vxlan_setup_multicast_interface(struct vxlan_softc *sc)
{
	struct ifnet *ifp;

	ifp = ifunit_ref(sc->vxl_mc_ifname);
	if (ifp == NULL) {
		if_printf(sc->vxl_ifp, "multicast interface %s does "
		    "not exist\n", sc->vxl_mc_ifname);
		return (ENOENT);
	}

	if ((ifp->if_flags & IFF_MULTICAST) == 0) {
		if_printf(sc->vxl_ifp, "interface %s does not support "
		     "multicast\n", sc->vxl_mc_ifname);
		if_rele(ifp);
		return (ENOTSUP);
	}

	sc->vxl_mc_ifp = ifp;
	sc->vxl_mc_ifindex = ifp->if_index;

	return (0);
}

static int
vxlan_setup_multicast(struct vxlan_softc *sc)
{
	const union vxlan_sockaddr *group;
	int error;

	group = &sc->vxl_dst_addr;
	error = 0;

	if (sc->vxl_mc_ifname[0] != '\0') {
		error = vxlan_setup_multicast_interface(sc);
		if (error)
			return (error);
	}

	/*
	 * Initialize an multicast options structure that is sufficiently
	 * populated for use in the respective IP output routine. This
	 * structure is typically stored in the socket, but our sockets
	 * may be shared among multiple interfaces.
	 */
	if (VXLAN_SOCKADDR_IS_IPV4(group)) {
		sc->vxl_im4o = malloc(sizeof(struct ip_moptions), M_VXLAN,
		    M_ZERO | M_WAITOK);
		sc->vxl_im4o->imo_multicast_ifp = sc->vxl_mc_ifp;
		sc->vxl_im4o->imo_multicast_ttl = sc->vxl_ttl;
		sc->vxl_im4o->imo_multicast_vif = -1;
	} else if (VXLAN_SOCKADDR_IS_IPV6(group)) {
		sc->vxl_im6o = malloc(sizeof(struct ip6_moptions), M_VXLAN,
		    M_ZERO | M_WAITOK);
		sc->vxl_im6o->im6o_multicast_ifp = sc->vxl_mc_ifp;
		sc->vxl_im6o->im6o_multicast_hlim = sc->vxl_ttl;
	}

	return (error);
}

static int
vxlan_setup_socket(struct vxlan_softc *sc)
{
	struct vxlan_socket *vso;
	struct ifnet *ifp;
	union vxlan_sockaddr *saddr, *daddr;
	int multicast, error;

	vso = NULL;
	ifp = sc->vxl_ifp;
	saddr = &sc->vxl_src_addr;
	daddr = &sc->vxl_dst_addr;

	multicast = vxlan_sockaddr_in_multicast(daddr);
	MPASS(multicast != -1);
	sc->vxl_vso_mc_index = -1;

	/*
	 * Try to create the socket. If that fails, attempt to use an
	 * existing socket.
	 */
	error = vxlan_socket_create(ifp, multicast, saddr, &vso);
	if (error) {
		if (multicast != 0)
			vso = vxlan_socket_mc_lookup(saddr);
		else
			vso = vxlan_socket_lookup(saddr);

		if (vso == NULL) {
			if_printf(ifp, "cannot create socket (error: %d), "
			    "and no existing socket found\n", error);
			goto out;
		}
	}

	if (multicast != 0) {
		error = vxlan_setup_multicast(sc);
		if (error)
			goto out;

		error = vxlan_socket_mc_add_group(vso, daddr, saddr,
		    sc->vxl_mc_ifindex, &sc->vxl_vso_mc_index);
		if (error)
			goto out;
	}

	sc->vxl_sock = vso;
	error = vxlan_socket_insert_softc(vso, sc);
	if (error) {
		sc->vxl_sock = NULL;
		if_printf(ifp, "network identifier %d already exists in "
		    "this socket\n", sc->vxl_vni);
		goto out;
	}

	return (0);

out:
	if (vso != NULL) {
		if (sc->vxl_vso_mc_index != -1) {
			vxlan_socket_mc_release_group_by_idx(vso,
			    sc->vxl_vso_mc_index);
			sc->vxl_vso_mc_index = -1;
		}
		if (multicast != 0)
			vxlan_free_multicast(sc);
		vxlan_socket_release(vso);
	}

	return (error);
}

static void
vxlan_setup_interface(struct vxlan_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vxl_ifp;
	ifp->if_hdrlen = ETHER_HDR_LEN + sizeof(struct vxlanudphdr);

	if (VXLAN_SOCKADDR_IS_IPV4(&sc->vxl_dst_addr) != 0)
		ifp->if_hdrlen += sizeof(struct ip);
	else if (VXLAN_SOCKADDR_IS_IPV6(&sc->vxl_dst_addr) != 0)
		ifp->if_hdrlen += sizeof(struct ip6_hdr);
}

static int
vxlan_valid_init_config(struct vxlan_softc *sc)
{
	const char *reason;

	if (vxlan_check_vni(sc->vxl_vni) != 0) {
		reason = "invalid virtual network identifier specified";
		goto fail;
	}

	if (vxlan_sockaddr_supported(&sc->vxl_src_addr, 1) == 0) {
		reason = "source address type is not supported";
		goto fail;
	}

	if (vxlan_sockaddr_supported(&sc->vxl_dst_addr, 0) == 0) {
		reason = "destination address type is not supported";
		goto fail;
	}

	if (vxlan_sockaddr_in_any(&sc->vxl_dst_addr) != 0) {
		reason = "no valid destination address specified";
		goto fail;
	}

	if (vxlan_sockaddr_in_multicast(&sc->vxl_dst_addr) == 0 &&
	    sc->vxl_mc_ifname[0] != '\0') {
		reason = "can only specify interface with a group address";
		goto fail;
	}

	if (vxlan_sockaddr_in_any(&sc->vxl_src_addr) == 0) {
		if (VXLAN_SOCKADDR_IS_IPV4(&sc->vxl_src_addr) ^
		    VXLAN_SOCKADDR_IS_IPV4(&sc->vxl_dst_addr)) {
			reason = "source and destination address must both "
			    "be either IPv4 or IPv6";
			goto fail;
		}
	}

	if (sc->vxl_src_addr.in4.sin_port == 0) {
		reason = "local port not specified";
		goto fail;
	}

	if (sc->vxl_dst_addr.in4.sin_port == 0) {
		reason = "remote port not specified";
		goto fail;
	}

	return (0);

fail:
	if_printf(sc->vxl_ifp, "cannot initialize interface: %s\n", reason);
	return (EINVAL);
}

static void
vxlan_init_wait(struct vxlan_softc *sc)
{

	VXLAN_LOCK_WASSERT(sc);
	while (sc->vxl_flags & VXLAN_FLAG_INIT)
		rm_sleep(sc, &sc->vxl_lock, 0, "vxlint", hz);
}

static void
vxlan_init_complete(struct vxlan_softc *sc)
{

	VXLAN_WLOCK(sc);
	sc->vxl_flags &= ~VXLAN_FLAG_INIT;
	wakeup(sc);
	VXLAN_WUNLOCK(sc);
}

static void
vxlan_init(void *xsc)
{
	static const uint8_t empty_mac[ETHER_ADDR_LEN];
	struct vxlan_softc *sc;
	struct ifnet *ifp;

	sc = xsc;
	ifp = sc->vxl_ifp;

	VXLAN_WLOCK(sc);
	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		VXLAN_WUNLOCK(sc);
		return;
	}
	sc->vxl_flags |= VXLAN_FLAG_INIT;
	VXLAN_WUNLOCK(sc);

	if (vxlan_valid_init_config(sc) != 0)
		goto out;

	vxlan_setup_interface(sc);

	if (vxlan_setup_socket(sc) != 0)
		goto out;

	/* Initialize the default forwarding entry. */
	vxlan_ftable_entry_init(sc, &sc->vxl_default_fe, empty_mac,
	    &sc->vxl_dst_addr.sa, VXLAN_FE_FLAG_STATIC);

	VXLAN_WLOCK(sc);
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	callout_reset(&sc->vxl_callout, vxlan_ftable_prune_period * hz,
	    vxlan_timer, sc);
	VXLAN_WUNLOCK(sc);

	if_link_state_change(ifp, LINK_STATE_UP);
out:
	vxlan_init_complete(sc);
}

static void
vxlan_release(struct vxlan_softc *sc)
{

	/*
	 * The softc may be destroyed as soon as we release our reference,
	 * so we cannot serialize the wakeup with the softc lock. We use a
	 * timeout in our sleeps so a missed wakeup is unfortunate but not
	 * fatal.
	 */
	if (VXLAN_RELEASE(sc) != 0)
		wakeup(sc);
}

static void
vxlan_teardown_wait(struct vxlan_softc *sc)
{

	VXLAN_LOCK_WASSERT(sc);
	while (sc->vxl_flags & VXLAN_FLAG_TEARDOWN)
		rm_sleep(sc, &sc->vxl_lock, 0, "vxltrn", hz);
}

static void
vxlan_teardown_complete(struct vxlan_softc *sc)
{

	VXLAN_WLOCK(sc);
	sc->vxl_flags &= ~VXLAN_FLAG_TEARDOWN;
	wakeup(sc);
	VXLAN_WUNLOCK(sc);
}

static void
vxlan_teardown_locked(struct vxlan_softc *sc)
{
	struct ifnet *ifp;
	struct vxlan_socket *vso;

	ifp = sc->vxl_ifp;

	VXLAN_LOCK_WASSERT(sc);
	MPASS(sc->vxl_flags & VXLAN_FLAG_TEARDOWN);

	ifp->if_flags &= ~IFF_UP;
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	callout_stop(&sc->vxl_callout);
	vso = sc->vxl_sock;
	sc->vxl_sock = NULL;

	VXLAN_WUNLOCK(sc);
	if_link_state_change(ifp, LINK_STATE_DOWN);

	if (vso != NULL) {
		vxlan_socket_remove_softc(vso, sc);

		if (sc->vxl_vso_mc_index != -1) {
			vxlan_socket_mc_release_group_by_idx(vso,
			    sc->vxl_vso_mc_index);
			sc->vxl_vso_mc_index = -1;
		}
	}

	VXLAN_WLOCK(sc);
	while (sc->vxl_refcnt != 0)
		rm_sleep(sc, &sc->vxl_lock, 0, "vxldrn", hz);
	VXLAN_WUNLOCK(sc);

	callout_drain(&sc->vxl_callout);

	vxlan_free_multicast(sc);
	if (vso != NULL)
		vxlan_socket_release(vso);

	vxlan_teardown_complete(sc);
}

static void
vxlan_teardown(struct vxlan_softc *sc)
{

	VXLAN_WLOCK(sc);
	if (sc->vxl_flags & VXLAN_FLAG_TEARDOWN) {
		vxlan_teardown_wait(sc);
		VXLAN_WUNLOCK(sc);
		return;
	}

	sc->vxl_flags |= VXLAN_FLAG_TEARDOWN;
	vxlan_teardown_locked(sc);
}

static void
vxlan_ifdetach(struct vxlan_softc *sc, struct ifnet *ifp,
    struct vxlan_softc_head *list)
{

	VXLAN_WLOCK(sc);

	if (sc->vxl_mc_ifp != ifp)
		goto out;
	if (sc->vxl_flags & VXLAN_FLAG_TEARDOWN)
		goto out;

	sc->vxl_flags |= VXLAN_FLAG_TEARDOWN;
	LIST_INSERT_HEAD(list, sc, vxl_ifdetach_list);

out:
	VXLAN_WUNLOCK(sc);
}

static void
vxlan_timer(void *xsc)
{
	struct vxlan_softc *sc;

	sc = xsc;
	VXLAN_LOCK_WASSERT(sc);

	vxlan_ftable_expire(sc);
	callout_schedule(&sc->vxl_callout, vxlan_ftable_prune_period * hz);
}

static int
vxlan_ioctl_ifflags(struct vxlan_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vxl_ifp;

	if (ifp->if_flags & IFF_UP) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			vxlan_init(sc);
	} else {
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			vxlan_teardown(sc);
	}

	return (0);
}

static int
vxlan_ctrl_get_config(struct vxlan_softc *sc, void *arg)
{
	struct rm_priotracker tracker;
	struct ifvxlancfg *cfg;

	cfg = arg;
	bzero(cfg, sizeof(*cfg));

	VXLAN_RLOCK(sc, &tracker);
	cfg->vxlc_vni = sc->vxl_vni;
	memcpy(&cfg->vxlc_local_sa, &sc->vxl_src_addr,
	    sizeof(union vxlan_sockaddr));
	memcpy(&cfg->vxlc_remote_sa, &sc->vxl_dst_addr,
	    sizeof(union vxlan_sockaddr));
	cfg->vxlc_mc_ifindex = sc->vxl_mc_ifindex;
	cfg->vxlc_ftable_cnt = sc->vxl_ftable_cnt;
	cfg->vxlc_ftable_max = sc->vxl_ftable_max;
	cfg->vxlc_ftable_timeout = sc->vxl_ftable_timeout;
	cfg->vxlc_port_min = sc->vxl_min_port;
	cfg->vxlc_port_max = sc->vxl_max_port;
	cfg->vxlc_learn = (sc->vxl_flags & VXLAN_FLAG_LEARN) != 0;
	cfg->vxlc_ttl = sc->vxl_ttl;
	VXLAN_RUNLOCK(sc, &tracker);

#ifdef INET6
	if (VXLAN_SOCKADDR_IS_IPV6(&cfg->vxlc_local_sa))
		sa6_recoverscope(&cfg->vxlc_local_sa.in6);
	if (VXLAN_SOCKADDR_IS_IPV6(&cfg->vxlc_remote_sa))
		sa6_recoverscope(&cfg->vxlc_remote_sa.in6);
#endif

	return (0);
}

static int
vxlan_ctrl_set_vni(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int error;

	cmd = arg;

	if (vxlan_check_vni(cmd->vxlcmd_vni) != 0)
		return (EINVAL);

	VXLAN_WLOCK(sc);
	if (vxlan_can_change_config(sc)) {
		sc->vxl_vni = cmd->vxlcmd_vni;
		error = 0;
	} else
		error = EBUSY;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_local_addr(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	union vxlan_sockaddr *vxlsa;
	int error;

	cmd = arg;
	vxlsa = &cmd->vxlcmd_sa;

	if (!VXLAN_SOCKADDR_IS_IPV46(vxlsa))
		return (EINVAL);
	if (vxlan_sockaddr_in_multicast(vxlsa) != 0)
		return (EINVAL);
	if (VXLAN_SOCKADDR_IS_IPV6(vxlsa)) {
		error = vxlan_sockaddr_in6_embedscope(vxlsa);
		if (error)
			return (error);
	}

	VXLAN_WLOCK(sc);
	if (vxlan_can_change_config(sc)) {
		vxlan_sockaddr_in_copy(&sc->vxl_src_addr, &vxlsa->sa);
		error = 0;
	} else
		error = EBUSY;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_remote_addr(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	union vxlan_sockaddr *vxlsa;
	int error;

	cmd = arg;
	vxlsa = &cmd->vxlcmd_sa;

	if (!VXLAN_SOCKADDR_IS_IPV46(vxlsa))
		return (EINVAL);
	if (VXLAN_SOCKADDR_IS_IPV6(vxlsa)) {
		error = vxlan_sockaddr_in6_embedscope(vxlsa);
		if (error)
			return (error);
	}

	VXLAN_WLOCK(sc);
	if (vxlan_can_change_config(sc)) {
		vxlan_sockaddr_in_copy(&sc->vxl_dst_addr, &vxlsa->sa);
		error = 0;
	} else
		error = EBUSY;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_local_port(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int error;

	cmd = arg;

	if (cmd->vxlcmd_port == 0)
		return (EINVAL);

	VXLAN_WLOCK(sc);
	if (vxlan_can_change_config(sc)) {
		sc->vxl_src_addr.in4.sin_port = htons(cmd->vxlcmd_port);
		error = 0;
	} else
		error = EBUSY;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_remote_port(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int error;

	cmd = arg;

	if (cmd->vxlcmd_port == 0)
		return (EINVAL);

	VXLAN_WLOCK(sc);
	if (vxlan_can_change_config(sc)) {
		sc->vxl_dst_addr.in4.sin_port = htons(cmd->vxlcmd_port);
		error = 0;
	} else
		error = EBUSY;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_port_range(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	uint16_t min, max;
	int error;

	cmd = arg;
	min = cmd->vxlcmd_port_min;
	max = cmd->vxlcmd_port_max;

	if (max < min)
		return (EINVAL);

	VXLAN_WLOCK(sc);
	if (vxlan_can_change_config(sc)) {
		sc->vxl_min_port = min;
		sc->vxl_max_port = max;
		error = 0;
	} else
		error = EBUSY;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_ftable_timeout(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int error;

	cmd = arg;

	VXLAN_WLOCK(sc);
	if (vxlan_check_ftable_timeout(cmd->vxlcmd_ftable_timeout) == 0) {
		sc->vxl_ftable_timeout = cmd->vxlcmd_ftable_timeout;
		error = 0;
	} else
		error = EINVAL;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_ftable_max(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int error;

	cmd = arg;

	VXLAN_WLOCK(sc);
	if (vxlan_check_ftable_max(cmd->vxlcmd_ftable_max) == 0) {
		sc->vxl_ftable_max = cmd->vxlcmd_ftable_max;
		error = 0;
	} else
		error = EINVAL;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_multicast_if(struct vxlan_softc * sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int error;

	cmd = arg;

	VXLAN_WLOCK(sc);
	if (vxlan_can_change_config(sc)) {
		strlcpy(sc->vxl_mc_ifname, cmd->vxlcmd_ifname, IFNAMSIZ);
		error = 0;
	} else
		error = EBUSY;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_ttl(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int error;

	cmd = arg;

	VXLAN_WLOCK(sc);
	if (vxlan_check_ttl(cmd->vxlcmd_ttl) == 0) {
		sc->vxl_ttl = cmd->vxlcmd_ttl;
		if (sc->vxl_im4o != NULL)
			sc->vxl_im4o->imo_multicast_ttl = sc->vxl_ttl;
		if (sc->vxl_im6o != NULL)
			sc->vxl_im6o->im6o_multicast_hlim = sc->vxl_ttl;
		error = 0;
	} else
		error = EINVAL;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_set_learn(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;

	cmd = arg;

	VXLAN_WLOCK(sc);
	if (cmd->vxlcmd_flags & VXLAN_CMD_FLAG_LEARN)
		sc->vxl_flags |= VXLAN_FLAG_LEARN;
	else
		sc->vxl_flags &= ~VXLAN_FLAG_LEARN;
	VXLAN_WUNLOCK(sc);

	return (0);
}

static int
vxlan_ctrl_ftable_entry_add(struct vxlan_softc *sc, void *arg)
{
	union vxlan_sockaddr vxlsa;
	struct ifvxlancmd *cmd;
	struct vxlan_ftable_entry *fe;
	int error;

	cmd = arg;
	vxlsa = cmd->vxlcmd_sa;

	if (!VXLAN_SOCKADDR_IS_IPV46(&vxlsa))
		return (EINVAL);
	if (vxlan_sockaddr_in_any(&vxlsa) != 0)
		return (EINVAL);
	if (vxlan_sockaddr_in_multicast(&vxlsa) != 0)
		return (EINVAL);
	/* BMV: We could support both IPv4 and IPv6 later. */
	if (vxlsa.sa.sa_family != sc->vxl_dst_addr.sa.sa_family)
		return (EAFNOSUPPORT);

	if (VXLAN_SOCKADDR_IS_IPV6(&vxlsa)) {
		error = vxlan_sockaddr_in6_embedscope(&vxlsa);
		if (error)
			return (error);
	}

	fe = vxlan_ftable_entry_alloc();
	if (fe == NULL)
		return (ENOMEM);

	if (vxlsa.in4.sin_port == 0)
		vxlsa.in4.sin_port = sc->vxl_dst_addr.in4.sin_port;

	vxlan_ftable_entry_init(sc, fe, cmd->vxlcmd_mac, &vxlsa.sa,
	    VXLAN_FE_FLAG_STATIC);

	VXLAN_WLOCK(sc);
	error = vxlan_ftable_entry_insert(sc, fe);
	VXLAN_WUNLOCK(sc);

	if (error)
		vxlan_ftable_entry_free(fe);

	return (error);
}

static int
vxlan_ctrl_ftable_entry_rem(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	struct vxlan_ftable_entry *fe;
	int error;

	cmd = arg;

	VXLAN_WLOCK(sc);
	fe = vxlan_ftable_entry_lookup(sc, cmd->vxlcmd_mac);
	if (fe != NULL) {
		vxlan_ftable_entry_destroy(sc, fe);
		error = 0;
	} else
		error = ENOENT;
	VXLAN_WUNLOCK(sc);

	return (error);
}

static int
vxlan_ctrl_flush(struct vxlan_softc *sc, void *arg)
{
	struct ifvxlancmd *cmd;
	int all;

	cmd = arg;
	all = cmd->vxlcmd_flags & VXLAN_CMD_FLAG_FLUSH_ALL;

	VXLAN_WLOCK(sc);
	vxlan_ftable_flush(sc, all);
	VXLAN_WUNLOCK(sc);

	return (0);
}

static int
vxlan_ioctl_drvspec(struct vxlan_softc *sc, struct ifdrv *ifd, int get)
{
	const struct vxlan_control *vc;
	union {
		struct ifvxlancfg	cfg;
		struct ifvxlancmd	cmd;
	} args;
	int out, error;

	if (ifd->ifd_cmd >= vxlan_control_table_size)
		return (EINVAL);

	bzero(&args, sizeof(args));
	vc = &vxlan_control_table[ifd->ifd_cmd];
	out = (vc->vxlc_flags & VXLAN_CTRL_FLAG_COPYOUT) != 0;

	if ((get != 0 && out == 0) || (get == 0 && out != 0))
		return (EINVAL);

	if (vc->vxlc_flags & VXLAN_CTRL_FLAG_SUSER) {
		error = priv_check(curthread, PRIV_NET_VXLAN);
		if (error)
			return (error);
	}

	if (ifd->ifd_len != vc->vxlc_argsize ||
	    ifd->ifd_len > sizeof(args))
		return (EINVAL);

	if (vc->vxlc_flags & VXLAN_CTRL_FLAG_COPYIN) {
		error = copyin(ifd->ifd_data, &args, ifd->ifd_len);
		if (error)
			return (error);
	}

	error = vc->vxlc_func(sc, &args);
	if (error)
		return (error);

	if (vc->vxlc_flags & VXLAN_CTRL_FLAG_COPYOUT) {
		error = copyout(&args, ifd->ifd_data, ifd->ifd_len);
		if (error)
			return (error);
	}

	return (0);
}

static int
vxlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vxlan_softc *sc;
	struct ifreq *ifr;
	struct ifdrv *ifd;
	int error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *) data;
	ifd = (struct ifdrv *) data;

	switch (cmd) {
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = 0;
		break;

	case SIOCGDRVSPEC:
	case SIOCSDRVSPEC:
		error = vxlan_ioctl_drvspec(sc, ifd, cmd == SIOCGDRVSPEC);
		break;

	case SIOCSIFFLAGS:
		error = vxlan_ioctl_ifflags(sc);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->vxl_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

#if defined(INET) || defined(INET6)
static uint16_t
vxlan_pick_source_port(struct vxlan_softc *sc, struct mbuf *m)
{
	int range;
	uint32_t hash;

	range = sc->vxl_max_port - sc->vxl_min_port + 1;

	if (M_HASHTYPE_ISHASH(m))
		hash = m->m_pkthdr.flowid;
	else
		hash = jenkins_hash(m->m_data, ETHER_HDR_LEN,
		    sc->vxl_port_hash_key);

	return (sc->vxl_min_port + (hash % range));
}

static void
vxlan_encap_header(struct vxlan_softc *sc, struct mbuf *m, int ipoff,
    uint16_t srcport, uint16_t dstport)
{
	struct vxlanudphdr *hdr;
	struct udphdr *udph;
	struct vxlan_header *vxh;
	int len;

	len = m->m_pkthdr.len - ipoff;
	MPASS(len >= sizeof(struct vxlanudphdr));
	hdr = mtodo(m, ipoff);

	udph = &hdr->vxlh_udp;
	udph->uh_sport = srcport;
	udph->uh_dport = dstport;
	udph->uh_ulen = htons(len);
	udph->uh_sum = 0;

	vxh = &hdr->vxlh_hdr;
	vxh->vxlh_flags = htonl(VXLAN_HDR_FLAGS_VALID_VNI);
	vxh->vxlh_vni = htonl(sc->vxl_vni << VXLAN_HDR_VNI_SHIFT);
}
#endif

static int
vxlan_encap4(struct vxlan_softc *sc, const union vxlan_sockaddr *fvxlsa,
    struct mbuf *m)
{
#ifdef INET
	struct ifnet *ifp;
	struct ip *ip;
	struct in_addr srcaddr, dstaddr;
	uint16_t srcport, dstport;
	int len, mcast, error;

	ifp = sc->vxl_ifp;
	srcaddr = sc->vxl_src_addr.in4.sin_addr;
	srcport = vxlan_pick_source_port(sc, m);
	dstaddr = fvxlsa->in4.sin_addr;
	dstport = fvxlsa->in4.sin_port;

	M_PREPEND(m, sizeof(struct ip) + sizeof(struct vxlanudphdr),
	    M_NOWAIT);
	if (m == NULL) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return (ENOBUFS);
	}

	len = m->m_pkthdr.len;

	ip = mtod(m, struct ip *);
	ip->ip_tos = 0;
	ip->ip_len = htons(len);
	ip->ip_off = 0;
	ip->ip_ttl = sc->vxl_ttl;
	ip->ip_p = IPPROTO_UDP;
	ip->ip_sum = 0;
	ip->ip_src = srcaddr;
	ip->ip_dst = dstaddr;

	vxlan_encap_header(sc, m, sizeof(struct ip), srcport, dstport);

	mcast = (m->m_flags & (M_MCAST | M_BCAST)) ? 1 : 0;
	m->m_flags &= ~(M_MCAST | M_BCAST);

	error = ip_output(m, NULL, NULL, 0, sc->vxl_im4o, NULL);
	if (error == 0) {
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
		if (mcast != 0)
			if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
	} else
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	return (error);
#else
	m_freem(m);
	return (ENOTSUP);
#endif
}

static int
vxlan_encap6(struct vxlan_softc *sc, const union vxlan_sockaddr *fvxlsa,
    struct mbuf *m)
{
#ifdef INET6
	struct ifnet *ifp;
	struct ip6_hdr *ip6;
	const struct in6_addr *srcaddr, *dstaddr;
	uint16_t srcport, dstport;
	int len, mcast, error;

	ifp = sc->vxl_ifp;
	srcaddr = &sc->vxl_src_addr.in6.sin6_addr;
	srcport = vxlan_pick_source_port(sc, m);
	dstaddr = &fvxlsa->in6.sin6_addr;
	dstport = fvxlsa->in6.sin6_port;

	M_PREPEND(m, sizeof(struct ip6_hdr) + sizeof(struct vxlanudphdr),
	    M_NOWAIT);
	if (m == NULL) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return (ENOBUFS);
	}

	len = m->m_pkthdr.len;

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = 0;		/* BMV: Keep in forwarding entry? */
	ip6->ip6_vfc = IPV6_VERSION;
	ip6->ip6_plen = 0;
	ip6->ip6_nxt = IPPROTO_UDP;
	ip6->ip6_hlim = sc->vxl_ttl;
	ip6->ip6_src = *srcaddr;
	ip6->ip6_dst = *dstaddr;

	vxlan_encap_header(sc, m, sizeof(struct ip6_hdr), srcport, dstport);

	/*
	 * XXX BMV We need support for RFC6935 before we can send and
	 * receive IPv6 UDP packets with a zero checksum.
	 */
	{
		struct udphdr *hdr = mtodo(m, sizeof(struct ip6_hdr));
		hdr->uh_sum = in6_cksum_pseudo(ip6,
		    m->m_pkthdr.len - sizeof(struct ip6_hdr), IPPROTO_UDP, 0);
		m->m_pkthdr.csum_flags = CSUM_UDP_IPV6;
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
	}

	mcast = (m->m_flags & (M_MCAST | M_BCAST)) ? 1 : 0;
	m->m_flags &= ~(M_MCAST | M_BCAST);

	error = ip6_output(m, NULL, NULL, 0, sc->vxl_im6o, NULL, NULL);
	if (error == 0) {
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
		if (mcast != 0)
			if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
	} else
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	return (error);
#else
	m_freem(m);
	return (ENOTSUP);
#endif
}

static int
vxlan_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct rm_priotracker tracker;
	union vxlan_sockaddr vxlsa;
	struct vxlan_softc *sc;
	struct vxlan_ftable_entry *fe;
	struct ifnet *mcifp;
	struct ether_header *eh;
	int ipv4, error;

	sc = ifp->if_softc;
	eh = mtod(m, struct ether_header *);
	fe = NULL;
	mcifp = NULL;

	ETHER_BPF_MTAP(ifp, m);

	VXLAN_RLOCK(sc, &tracker);
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		VXLAN_RUNLOCK(sc, &tracker);
		m_freem(m);
		return (ENETDOWN);
	}

	if ((m->m_flags & (M_BCAST | M_MCAST)) == 0)
		fe = vxlan_ftable_entry_lookup(sc, eh->ether_dhost);
	if (fe == NULL)
		fe = &sc->vxl_default_fe;
	vxlan_sockaddr_copy(&vxlsa, &fe->vxlfe_raddr.sa);

	ipv4 = VXLAN_SOCKADDR_IS_IPV4(&vxlsa) != 0;
	if (vxlan_sockaddr_in_multicast(&vxlsa) != 0)
		mcifp = vxlan_multicast_if_ref(sc, ipv4);

	VXLAN_ACQUIRE(sc);
	VXLAN_RUNLOCK(sc, &tracker);

	if (ipv4 != 0)
		error = vxlan_encap4(sc, &vxlsa, m);
	else
		error = vxlan_encap6(sc, &vxlsa, m);

	vxlan_release(sc);
	if (mcifp != NULL)
		if_rele(mcifp);

	return (error);
}

static void
vxlan_qflush(struct ifnet *ifp __unused)
{
}

static void
vxlan_rcv_udp_packet(struct mbuf *m, int offset, struct inpcb *inpcb,
    const struct sockaddr *srcsa, void *xvso)
{
	struct vxlan_socket *vso;
	struct vxlan_header *vxh, vxlanhdr;
	uint32_t vni;
	int error __unused;

	M_ASSERTPKTHDR(m);
	vso = xvso;
	offset += sizeof(struct udphdr);

	if (m->m_pkthdr.len < offset + sizeof(struct vxlan_header))
		goto out;

	if (__predict_false(m->m_len < offset + sizeof(struct vxlan_header))) {
		m_copydata(m, offset, sizeof(struct vxlan_header),
		    (caddr_t) &vxlanhdr);
		vxh = &vxlanhdr;
	} else
		vxh = mtodo(m, offset);

	/*
	 * Drop if there is a reserved bit set in either the flags or VNI
	 * fields of the header. This goes against the specification, but
	 * a bit set may indicate an unsupported new feature. This matches
	 * the behavior of the Linux implementation.
	 */
	if (vxh->vxlh_flags != htonl(VXLAN_HDR_FLAGS_VALID_VNI) ||
	    vxh->vxlh_vni & ~htonl(VXLAN_VNI_MASK))
		goto out;

	vni = ntohl(vxh->vxlh_vni) >> VXLAN_HDR_VNI_SHIFT;
	/* Adjust to the start of the inner Ethernet frame. */
	m_adj(m, offset + sizeof(struct vxlan_header));

	error = vxlan_input(vso, vni, &m, srcsa);
	MPASS(error != 0 || m == NULL);

out:
	if (m != NULL)
		m_freem(m);
}

static int
vxlan_input(struct vxlan_socket *vso, uint32_t vni, struct mbuf **m0,
    const struct sockaddr *sa)
{
	struct vxlan_softc *sc;
	struct ifnet *ifp;
	struct mbuf *m;
	struct ether_header *eh;
	int error;

	sc = vxlan_socket_lookup_softc(vso, vni);
	if (sc == NULL)
		return (ENOENT);

	ifp = sc->vxl_ifp;
	m = *m0;
	eh = mtod(m, struct ether_header *);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		error = ENETDOWN;
		goto out;
	} else if (ifp == m->m_pkthdr.rcvif) {
		/* XXX Does not catch more complex loops. */
		error = EDEADLK;
		goto out;
	}

	if (sc->vxl_flags & VXLAN_FLAG_LEARN)
		vxlan_ftable_learn(sc, sa, eh->ether_shost);

	m_clrprotoflags(m);
	m->m_pkthdr.rcvif = ifp;
	M_SETFIB(m, ifp->if_fib);

	error = netisr_queue_src(NETISR_ETHER, 0, m);
	*m0 = NULL;

out:
	vxlan_release(sc);
	return (error);
}

static void
vxlan_set_default_config(struct vxlan_softc *sc)
{

	sc->vxl_flags |= VXLAN_FLAG_LEARN;

	sc->vxl_vni = VXLAN_VNI_MAX;
	sc->vxl_ttl = IPDEFTTL;

	if (!vxlan_tunable_int(sc, "legacy_port", vxlan_legacy_port)) {
		sc->vxl_src_addr.in4.sin_port = htons(VXLAN_PORT);
		sc->vxl_dst_addr.in4.sin_port = htons(VXLAN_PORT);
	} else {
		sc->vxl_src_addr.in4.sin_port = htons(VXLAN_LEGACY_PORT);
		sc->vxl_dst_addr.in4.sin_port = htons(VXLAN_LEGACY_PORT);
	}

	sc->vxl_min_port = V_ipport_firstauto;
	sc->vxl_max_port = V_ipport_lastauto;

	sc->vxl_ftable_max = VXLAN_FTABLE_MAX;
	sc->vxl_ftable_timeout = VXLAN_FTABLE_TIMEOUT;
}

static int
vxlan_set_user_config(struct vxlan_softc *sc, struct ifvxlanparam *vxlp)
{

#ifndef INET
	if (vxlp->vxlp_with & (VXLAN_PARAM_WITH_LOCAL_ADDR4 |
	    VXLAN_PARAM_WITH_REMOTE_ADDR4))
		return (EAFNOSUPPORT);
#endif

#ifndef INET6
	if (vxlp->vxlp_with & (VXLAN_PARAM_WITH_LOCAL_ADDR6 |
	    VXLAN_PARAM_WITH_REMOTE_ADDR6))
		return (EAFNOSUPPORT);
#else
	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_LOCAL_ADDR6) {
		int error = vxlan_sockaddr_in6_embedscope(&vxlp->vxlp_local_sa);
		if (error)
			return (error);
	}
	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_REMOTE_ADDR6) {
		int error = vxlan_sockaddr_in6_embedscope(
		   &vxlp->vxlp_remote_sa);
		if (error)
			return (error);
	}
#endif

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_VNI) {
		if (vxlan_check_vni(vxlp->vxlp_vni) == 0)
			sc->vxl_vni = vxlp->vxlp_vni;
	}

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_LOCAL_ADDR4) {
		sc->vxl_src_addr.in4.sin_len = sizeof(struct sockaddr_in);
		sc->vxl_src_addr.in4.sin_family = AF_INET;
		sc->vxl_src_addr.in4.sin_addr =
		    vxlp->vxlp_local_sa.in4.sin_addr;
	} else if (vxlp->vxlp_with & VXLAN_PARAM_WITH_LOCAL_ADDR6) {
		sc->vxl_src_addr.in6.sin6_len = sizeof(struct sockaddr_in6);
		sc->vxl_src_addr.in6.sin6_family = AF_INET6;
		sc->vxl_src_addr.in6.sin6_addr =
		    vxlp->vxlp_local_sa.in6.sin6_addr;
	}

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_REMOTE_ADDR4) {
		sc->vxl_dst_addr.in4.sin_len = sizeof(struct sockaddr_in);
		sc->vxl_dst_addr.in4.sin_family = AF_INET;
		sc->vxl_dst_addr.in4.sin_addr =
		    vxlp->vxlp_remote_sa.in4.sin_addr;
	} else if (vxlp->vxlp_with & VXLAN_PARAM_WITH_REMOTE_ADDR6) {
		sc->vxl_dst_addr.in6.sin6_len = sizeof(struct sockaddr_in6);
		sc->vxl_dst_addr.in6.sin6_family = AF_INET6;
		sc->vxl_dst_addr.in6.sin6_addr =
		    vxlp->vxlp_remote_sa.in6.sin6_addr;
	}

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_LOCAL_PORT)
		sc->vxl_src_addr.in4.sin_port = htons(vxlp->vxlp_local_port);
	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_REMOTE_PORT)
		sc->vxl_dst_addr.in4.sin_port = htons(vxlp->vxlp_remote_port);

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_PORT_RANGE) {
		if (vxlp->vxlp_min_port <= vxlp->vxlp_max_port) {
			sc->vxl_min_port = vxlp->vxlp_min_port;
			sc->vxl_max_port = vxlp->vxlp_max_port;
		}
	}

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_MULTICAST_IF)
		strlcpy(sc->vxl_mc_ifname, vxlp->vxlp_mc_ifname, IFNAMSIZ);

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_FTABLE_TIMEOUT) {
		if (vxlan_check_ftable_timeout(vxlp->vxlp_ftable_timeout) == 0)
			sc->vxl_ftable_timeout = vxlp->vxlp_ftable_timeout;
	}

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_FTABLE_MAX) {
		if (vxlan_check_ftable_max(vxlp->vxlp_ftable_max) == 0)
			sc->vxl_ftable_max = vxlp->vxlp_ftable_max;
	}

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_TTL) {
		if (vxlan_check_ttl(vxlp->vxlp_ttl) == 0)
			sc->vxl_ttl = vxlp->vxlp_ttl;
	}

	if (vxlp->vxlp_with & VXLAN_PARAM_WITH_LEARN) {
		if (vxlp->vxlp_learn == 0)
			sc->vxl_flags &= ~VXLAN_FLAG_LEARN;
	}

	return (0);
}

static int
vxlan_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct vxlan_softc *sc;
	struct ifnet *ifp;
	struct ifvxlanparam vxlp;
	int error;

	sc = malloc(sizeof(struct vxlan_softc), M_VXLAN, M_WAITOK | M_ZERO);
	sc->vxl_unit = unit;
	vxlan_set_default_config(sc);

	if (params != 0) {
		error = copyin(params, &vxlp, sizeof(vxlp));
		if (error)
			goto fail;

		error = vxlan_set_user_config(sc, &vxlp);
		if (error)
			goto fail;
	}

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		error = ENOSPC;
		goto fail;
	}

	sc->vxl_ifp = ifp;
	rm_init(&sc->vxl_lock, "vxlanrm");
	callout_init_rw(&sc->vxl_callout, &sc->vxl_lock, 0);
	sc->vxl_port_hash_key = arc4random();
	vxlan_ftable_init(sc);

	vxlan_sysctl_setup(sc);

	ifp->if_softc = sc;
	if_initname(ifp, vxlan_name, unit);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = vxlan_init;
	ifp->if_ioctl = vxlan_ioctl;
	ifp->if_transmit = vxlan_transmit;
	ifp->if_qflush = vxlan_qflush;
	ifp->if_capabilities |= IFCAP_LINKSTATE;
	ifp->if_capenable |= IFCAP_LINKSTATE;

	ifmedia_init(&sc->vxl_media, 0, vxlan_media_change, vxlan_media_status);
	ifmedia_add(&sc->vxl_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->vxl_media, IFM_ETHER | IFM_AUTO);

	ether_fakeaddr(&sc->vxl_hwaddr);
	ether_ifattach(ifp, sc->vxl_hwaddr.octet);

	ifp->if_baudrate = 0;
	ifp->if_hdrlen = 0;

	return (0);

fail:
	free(sc, M_VXLAN);
	return (error);
}

static void
vxlan_clone_destroy(struct ifnet *ifp)
{
	struct vxlan_softc *sc;

	sc = ifp->if_softc;

	vxlan_teardown(sc);

	vxlan_ftable_flush(sc, 1);

	ether_ifdetach(ifp);
	if_free(ifp);
	ifmedia_removeall(&sc->vxl_media);

	vxlan_ftable_fini(sc);

	vxlan_sysctl_destroy(sc);
	rm_destroy(&sc->vxl_lock);
	free(sc, M_VXLAN);
}

/* BMV: Taken from if_bridge. */
static uint32_t
vxlan_mac_hash(struct vxlan_softc *sc, const uint8_t *addr)
{
	uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = sc->vxl_ftable_hash_key;

	b += addr[5] << 8;
	b += addr[4];
	a += addr[3] << 24;
	a += addr[2] << 16;
	a += addr[1] << 8;
	a += addr[0];

/*
 * The following hash function is adapted from "Hash Functions" by Bob Jenkins
 * ("Algorithm Alley", Dr. Dobbs Journal, September 1997).
 */
#define	mix(a, b, c)							\
do {									\
	a -= b; a -= c; a ^= (c >> 13);					\
	b -= c; b -= a; b ^= (a << 8);					\
	c -= a; c -= b; c ^= (b >> 13);					\
	a -= b; a -= c; a ^= (c >> 12);					\
	b -= c; b -= a; b ^= (a << 16);					\
	c -= a; c -= b; c ^= (b >> 5);					\
	a -= b; a -= c; a ^= (c >> 3);					\
	b -= c; b -= a; b ^= (a << 10);					\
	c -= a; c -= b; c ^= (b >> 15);					\
} while (0)

	mix(a, b, c);

#undef mix

	return (c);
}

static int
vxlan_media_change(struct ifnet *ifp)
{

	/* Ignore. */
	return (0);
}

static void
vxlan_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{

	ifmr->ifm_status = IFM_ACTIVE | IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER | IFM_FDX;
}

static int
vxlan_sockaddr_cmp(const union vxlan_sockaddr *vxladdr,
    const struct sockaddr *sa)
{

	return (bcmp(&vxladdr->sa, sa, vxladdr->sa.sa_len));
}

static void
vxlan_sockaddr_copy(union vxlan_sockaddr *vxladdr,
    const struct sockaddr *sa)
{

	MPASS(sa->sa_family == AF_INET || sa->sa_family == AF_INET6);
	bzero(vxladdr, sizeof(*vxladdr));

	if (sa->sa_family == AF_INET) {
		vxladdr->in4 = *satoconstsin(sa);
		vxladdr->in4.sin_len = sizeof(struct sockaddr_in);
	} else if (sa->sa_family == AF_INET6) {
		vxladdr->in6 = *satoconstsin6(sa);
		vxladdr->in6.sin6_len = sizeof(struct sockaddr_in6);
	}
}

static int
vxlan_sockaddr_in_equal(const union vxlan_sockaddr *vxladdr,
    const struct sockaddr *sa)
{
	int equal;

	if (sa->sa_family == AF_INET) {
		const struct in_addr *in4 = &satoconstsin(sa)->sin_addr;
		equal = in4->s_addr == vxladdr->in4.sin_addr.s_addr;
	} else if (sa->sa_family == AF_INET6) {
		const struct in6_addr *in6 = &satoconstsin6(sa)->sin6_addr;
		equal = IN6_ARE_ADDR_EQUAL(in6, &vxladdr->in6.sin6_addr);
	} else
		equal = 0;

	return (equal);
}

static void
vxlan_sockaddr_in_copy(union vxlan_sockaddr *vxladdr,
    const struct sockaddr *sa)
{

	MPASS(sa->sa_family == AF_INET || sa->sa_family == AF_INET6);

	if (sa->sa_family == AF_INET) {
		const struct in_addr *in4 = &satoconstsin(sa)->sin_addr;
		vxladdr->in4.sin_family = AF_INET;
		vxladdr->in4.sin_len = sizeof(struct sockaddr_in);
		vxladdr->in4.sin_addr = *in4;
	} else if (sa->sa_family == AF_INET6) {
		const struct in6_addr *in6 = &satoconstsin6(sa)->sin6_addr;
		vxladdr->in6.sin6_family = AF_INET6;
		vxladdr->in6.sin6_len = sizeof(struct sockaddr_in6);
		vxladdr->in6.sin6_addr = *in6;
	}
}

static int
vxlan_sockaddr_supported(const union vxlan_sockaddr *vxladdr, int unspec)
{
	const struct sockaddr *sa;
	int supported;

	sa = &vxladdr->sa;
	supported = 0;

	if (sa->sa_family == AF_UNSPEC && unspec != 0) {
		supported = 1;
	} else if (sa->sa_family == AF_INET) {
#ifdef INET
		supported = 1;
#endif
	} else if (sa->sa_family == AF_INET6) {
#ifdef INET6
		supported = 1;
#endif
	}

	return (supported);
}

static int
vxlan_sockaddr_in_any(const union vxlan_sockaddr *vxladdr)
{
	const struct sockaddr *sa;
	int any;

	sa = &vxladdr->sa;

	if (sa->sa_family == AF_INET) {
		const struct in_addr *in4 = &satoconstsin(sa)->sin_addr;
		any = in4->s_addr == INADDR_ANY;
	} else if (sa->sa_family == AF_INET6) {
		const struct in6_addr *in6 = &satoconstsin6(sa)->sin6_addr;
		any = IN6_IS_ADDR_UNSPECIFIED(in6);
	} else
		any = -1;

	return (any);
}

static int
vxlan_sockaddr_in_multicast(const union vxlan_sockaddr *vxladdr)
{
	const struct sockaddr *sa;
	int mc;

	sa = &vxladdr->sa;

	if (sa->sa_family == AF_INET) {
		const struct in_addr *in4 = &satoconstsin(sa)->sin_addr;
		mc = IN_MULTICAST(ntohl(in4->s_addr));
	} else if (sa->sa_family == AF_INET6) {
		const struct in6_addr *in6 = &satoconstsin6(sa)->sin6_addr;
		mc = IN6_IS_ADDR_MULTICAST(in6);
	} else
		mc = -1;

	return (mc);
}

static int
vxlan_sockaddr_in6_embedscope(union vxlan_sockaddr *vxladdr)
{
	int error;

	MPASS(VXLAN_SOCKADDR_IS_IPV6(vxladdr));
#ifdef INET6
	error = sa6_embedscope(&vxladdr->in6, V_ip6_use_defzone);
#else
	error = EAFNOSUPPORT;
#endif

	return (error);
}

static int
vxlan_can_change_config(struct vxlan_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vxl_ifp;
	VXLAN_LOCK_ASSERT(sc);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return (0);
	if (sc->vxl_flags & (VXLAN_FLAG_INIT | VXLAN_FLAG_TEARDOWN))
		return (0);

	return (1);
}

static int
vxlan_check_vni(uint32_t vni)
{

	return (vni >= VXLAN_VNI_MAX);
}

static int
vxlan_check_ttl(int ttl)
{

	return (ttl > MAXTTL);
}

static int
vxlan_check_ftable_timeout(uint32_t timeout)
{

	return (timeout > VXLAN_FTABLE_MAX_TIMEOUT);
}

static int
vxlan_check_ftable_max(uint32_t max)
{

	return (max > VXLAN_FTABLE_MAX);
}

static void
vxlan_sysctl_setup(struct vxlan_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *node;
	struct vxlan_statistics *stats;
	char namebuf[8];

	ctx = &sc->vxl_sysctl_ctx;
	stats = &sc->vxl_stats;
	snprintf(namebuf, sizeof(namebuf), "%d", sc->vxl_unit);

	sysctl_ctx_init(ctx);
	sc->vxl_sysctl_node = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_STATIC_CHILDREN(_net_link_vxlan), OID_AUTO, namebuf,
	    CTLFLAG_RD, NULL, "");

	node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(sc->vxl_sysctl_node),
	    OID_AUTO, "ftable", CTLFLAG_RD, NULL, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "count",
	    CTLFLAG_RD, &sc->vxl_ftable_cnt, 0,
	    "Number of entries in fowarding table");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "max",
	     CTLFLAG_RD, &sc->vxl_ftable_max, 0,
	    "Maximum number of entries allowed in fowarding table");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "timeout",
	    CTLFLAG_RD, &sc->vxl_ftable_timeout, 0,
	    "Number of seconds between prunes of the forwarding table");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "dump",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE | CTLFLAG_SKIP,
	    sc, 0, vxlan_ftable_sysctl_dump, "A",
	    "Dump the forwarding table entries");

	node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(sc->vxl_sysctl_node),
	    OID_AUTO, "stats", CTLFLAG_RD, NULL, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO,
	    "ftable_nospace", CTLFLAG_RD, &stats->ftable_nospace, 0,
	    "Fowarding table reached maximum entries");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO,
	    "ftable_lock_upgrade_failed", CTLFLAG_RD,
	    &stats->ftable_lock_upgrade_failed, 0,
	    "Forwarding table update required lock upgrade");
}

static void
vxlan_sysctl_destroy(struct vxlan_softc *sc)
{

	sysctl_ctx_free(&sc->vxl_sysctl_ctx);
	sc->vxl_sysctl_node = NULL;
}

static int
vxlan_tunable_int(struct vxlan_softc *sc, const char *knob, int def)
{
	char path[64];

	snprintf(path, sizeof(path), "net.link.vxlan.%d.%s",
	    sc->vxl_unit, knob);
	TUNABLE_INT_FETCH(path, &def);

	return (def);
}

static void
vxlan_ifdetach_event(void *arg __unused, struct ifnet *ifp)
{
	struct vxlan_softc_head list;
	struct vxlan_socket *vso;
	struct vxlan_softc *sc, *tsc;

	LIST_INIT(&list);

	if (ifp->if_flags & IFF_RENAMING)
		return;
	if ((ifp->if_flags & IFF_MULTICAST) == 0)
		return;

	VXLAN_LIST_LOCK();
	LIST_FOREACH(vso, &vxlan_socket_list, vxlso_entry)
		vxlan_socket_ifdetach(vso, ifp, &list);
	VXLAN_LIST_UNLOCK();

	LIST_FOREACH_SAFE(sc, &list, vxl_ifdetach_list, tsc) {
		LIST_REMOVE(sc, vxl_ifdetach_list);

		VXLAN_WLOCK(sc);
		if (sc->vxl_flags & VXLAN_FLAG_INIT)
			vxlan_init_wait(sc);
		vxlan_teardown_locked(sc);
	}
}

static void
vxlan_load(void)
{

	mtx_init(&vxlan_list_mtx, "vxlan list", NULL, MTX_DEF);
	LIST_INIT(&vxlan_socket_list);
	vxlan_ifdetach_event_tag = EVENTHANDLER_REGISTER(ifnet_departure_event,
	    vxlan_ifdetach_event, NULL, EVENTHANDLER_PRI_ANY);
	vxlan_cloner = if_clone_simple(vxlan_name, vxlan_clone_create,
	    vxlan_clone_destroy, 0);
}

static void
vxlan_unload(void)
{

	EVENTHANDLER_DEREGISTER(ifnet_departure_event,
	    vxlan_ifdetach_event_tag);
	if_clone_detach(vxlan_cloner);
	mtx_destroy(&vxlan_list_mtx);
	MPASS(LIST_EMPTY(&vxlan_socket_list));
}

static int
vxlan_modevent(module_t mod, int type, void *unused)
{
	int error;

	error = 0;

	switch (type) {
	case MOD_LOAD:
		vxlan_load();
		break;
	case MOD_UNLOAD:
		vxlan_unload();
		break;
	default:
		error = ENOTSUP;
		break;
	}

	return (error);
}

static moduledata_t vxlan_mod = {
	"if_vxlan",
	vxlan_modevent,
	0
};

DECLARE_MODULE(if_vxlan, vxlan_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_vxlan, 1);
