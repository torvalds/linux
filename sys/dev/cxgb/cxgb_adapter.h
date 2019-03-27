/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007-2009, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

$FreeBSD$

***************************************************************************/


#ifndef _CXGB_ADAPTER_H_
#define _CXGB_ADAPTER_H_

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/condvar.h>
#include <sys/buf_ring.h>
#include <sys/taskqueue.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/tcp_lro.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <cxgb_osdep.h>

struct adapter;
struct sge_qset;
extern int cxgb_debug;

#ifdef DEBUG_LOCKING
#define MTX_INIT(lock, lockname, class, flags) \
	do { \
		printf("initializing %s at %s:%d\n", lockname, __FILE__, __LINE__); \
		mtx_init((lock), lockname, class, flags);		\
	} while (0)

#define MTX_DESTROY(lock) \
	do { \
		printf("destroying %s at %s:%d\n", (lock)->lock_object.lo_name, __FILE__, __LINE__); \
		mtx_destroy((lock));					\
	} while (0)

#else
#define MTX_INIT mtx_init
#define MTX_DESTROY mtx_destroy
#endif

enum {
	LF_NO = 0,
	LF_MAYBE,
	LF_YES
};

struct port_info {
	struct adapter	*adapter;
	struct ifnet	*ifp;
	int		if_flags;
	int		flags;
	const struct port_type_info *port_type;
	struct cphy	phy;
	struct cmac	mac;
	struct timeval	last_refreshed;
	struct link_config link_config;
	struct ifmedia	media;
	struct mtx	lock;
	uint32_t	port_id;
	uint32_t	tx_chan;
	uint32_t	txpkt_intf;
	uint32_t        first_qset;
	uint32_t	nqsets;
	int		link_fault;

	uint8_t		hw_addr[ETHER_ADDR_LEN];
	struct callout	link_check_ch;
	struct task	link_check_task;
	struct task	timer_reclaim_task;
	struct cdev     *port_cdev;

#define PORT_LOCK_NAME_LEN 32
#define PORT_NAME_LEN 32
	char            lockbuf[PORT_LOCK_NAME_LEN];
	char            namebuf[PORT_NAME_LEN];
} __aligned(L1_CACHE_BYTES);

enum {
	/* adapter flags */
	FULL_INIT_DONE	= (1 << 0),
	USING_MSI	= (1 << 1),
	USING_MSIX	= (1 << 2),
	QUEUES_BOUND	= (1 << 3),
	FW_UPTODATE	= (1 << 4),
	TPS_UPTODATE	= (1 << 5),
	CXGB_SHUTDOWN	= (1 << 6),
	CXGB_OFLD_INIT	= (1 << 7),
	TP_PARITY_INIT	= (1 << 8),
	CXGB_BUSY	= (1 << 9),
	TOM_INIT_DONE	= (1 << 10),

	/* port flags */
	DOOMED		= (1 << 0),
};
#define IS_DOOMED(p)	(p->flags & DOOMED)
#define SET_DOOMED(p)	do {p->flags |= DOOMED;} while (0)
#define IS_BUSY(sc)	(sc->flags & CXGB_BUSY)
#define SET_BUSY(sc)	do {sc->flags |= CXGB_BUSY;} while (0)
#define CLR_BUSY(sc)	do {sc->flags &= ~CXGB_BUSY;} while (0)

#define FL_Q_SIZE	4096
#define JUMBO_Q_SIZE	1024
#define RSPQ_Q_SIZE	2048
#define TX_ETH_Q_SIZE	1024
#define TX_OFLD_Q_SIZE	1024
#define TX_CTRL_Q_SIZE	256

enum { TXQ_ETH = 0,
       TXQ_OFLD = 1,
       TXQ_CTRL = 2, };


/* 
 * work request size in bytes
 */
#define WR_LEN (WR_FLITS * 8)
#define PIO_LEN (WR_LEN - sizeof(struct cpl_tx_pkt_lso))

struct lro_state {
	unsigned short enabled;
	struct lro_ctrl ctrl;
};

#define RX_BUNDLE_SIZE 8

struct rsp_desc;

struct sge_rspq {
	uint32_t	credits;
	uint32_t	size;
	uint32_t	cidx;
	uint32_t	gen;
	uint32_t	polling;
	uint32_t	holdoff_tmr;
	uint32_t	next_holdoff;
	uint32_t        imm_data;
	uint32_t        async_notif;
	uint32_t	cntxt_id;
	uint32_t        offload_pkts;
	uint32_t        pure_rsps;
	uint32_t        unhandled_irqs;
	uint32_t        starved;

	bus_addr_t	phys_addr;
	bus_dma_tag_t	desc_tag;
	bus_dmamap_t	desc_map;

	struct t3_mbuf_hdr rspq_mh;
	struct rsp_desc	*desc;
	struct mtx      lock;
#define RSPQ_NAME_LEN  32
	char            lockbuf[RSPQ_NAME_LEN];
	uint32_t	rspq_dump_start;
	uint32_t	rspq_dump_count;
};

struct rx_desc;
struct rx_sw_desc;

struct sge_fl {
	uint32_t	buf_size;
	uint32_t	credits;
	uint32_t	size;
	uint32_t	cidx;
	uint32_t	pidx;
	uint32_t	gen;
	uint32_t	db_pending;
	bus_addr_t	phys_addr;
	uint32_t	cntxt_id;
	uint32_t	empty;
	bus_dma_tag_t	desc_tag;
	bus_dmamap_t	desc_map;
	bus_dma_tag_t   entry_tag;
	uma_zone_t      zone;
	struct rx_desc	*desc;
	struct rx_sw_desc *sdesc;
	int             type;
};

struct tx_desc;
struct tx_sw_desc;

#define TXQ_TRANSMITTING    0x1

struct sge_txq {
	uint64_t	flags;
	uint32_t	in_use;
	uint32_t	size;
	uint32_t	processed;
	uint32_t	cleaned;
	uint32_t	stop_thres;
	uint32_t	cidx;
	uint32_t	pidx;
	uint32_t	gen;
	uint32_t	unacked;
	uint32_t	db_pending;
	struct tx_desc	*desc;
	struct tx_sw_desc *sdesc;
	uint32_t	token;
	bus_addr_t	phys_addr;
	struct task     qresume_task;
	struct task     qreclaim_task;
	uint32_t	cntxt_id;
	uint64_t	stops;
	uint64_t	restarts;
	bus_dma_tag_t	desc_tag;
	bus_dmamap_t	desc_map;
	bus_dma_tag_t   entry_tag;
	struct mbufq	sendq;

	struct buf_ring *txq_mr;
	struct ifaltq	*txq_ifq;
	struct callout	txq_timer;
	struct callout	txq_watchdog;
	uint64_t        txq_coalesced;
	uint32_t        txq_skipped;
	uint32_t        txq_enqueued;
	uint32_t	txq_dump_start;
	uint32_t	txq_dump_count;
	uint64_t	txq_direct_packets;
	uint64_t	txq_direct_bytes;	
	uint64_t	txq_frees;
	struct sg_ent  txq_sgl[TX_MAX_SEGS / 2 + 1];
};
     	
#define SGE_PSTAT_MAX (SGE_PSTAT_VLANINS+1)

#define QS_EXITING              0x1
#define QS_RUNNING              0x2
#define QS_BOUND                0x4
#define	QS_FLUSHING		0x8
#define	QS_TIMEOUT		0x10

struct sge_qset {
	struct sge_rspq		rspq;
	struct sge_fl		fl[SGE_RXQ_PER_SET];
	struct lro_state        lro;
	struct sge_txq		txq[SGE_TXQ_PER_SET];
	uint32_t                txq_stopped;       /* which Tx queues are stopped */
	struct port_info        *port;
	struct adapter          *adap;
	int                     idx; /* qset # */
	int                     qs_flags;
	int			coalescing;
	struct cv		qs_cv;
	struct mtx		lock;
#define QS_NAME_LEN 32
	char                    namebuf[QS_NAME_LEN];
};

struct sge {
	struct sge_qset	        qs[SGE_QSETS];
	struct mtx              reg_lock;
};

struct filter_info;

typedef int (*cpl_handler_t)(struct sge_qset *, struct rsp_desc *,
    struct mbuf *);

struct adapter {
	SLIST_ENTRY(adapter)	link;
	device_t		dev;
	int			flags;

	/* PCI register resources */
	int			regs_rid;
	struct resource		*regs_res;
	int			udbs_rid;
	struct resource		*udbs_res;
	bus_space_handle_t	bh;
	bus_space_tag_t		bt;
	bus_size_t              mmio_len;
	uint32_t                link_width;
	
	/* DMA resources */
	bus_dma_tag_t		parent_dmat;
	bus_dma_tag_t		rx_dmat;
	bus_dma_tag_t		rx_jumbo_dmat;
	bus_dma_tag_t		tx_dmat;

	/* Interrupt resources */
	struct resource		*irq_res;
	int			irq_rid;
	void			*intr_tag;

	uint32_t		msix_regs_rid;
	struct resource		*msix_regs_res;

	struct resource		*msix_irq_res[SGE_QSETS];
	int			msix_irq_rid[SGE_QSETS];
	void			*msix_intr_tag[SGE_QSETS];
	uint8_t                 rxpkt_map[8]; /* maps RX_PKT interface values to port ids */
	uint8_t                 rrss_map[SGE_QSETS]; /* revers RSS map table */
	uint16_t                rspq_map[RSS_TABLE_SIZE];     /* maps 7-bit cookie to qidx */
	union {
		uint8_t                 fill[SGE_QSETS];
		uint64_t                coalesce;
	} u;

#define tunq_fill u.fill
#define tunq_coalesce u.coalesce
	
	struct filter_info      *filters;
	
	/* Tasks */
	struct task		slow_intr_task;
	struct task		tick_task;
	struct taskqueue	*tq;
	struct callout		cxgb_tick_ch;
	struct callout		sge_timer_ch;

	/* Register lock for use by the hardware layer */
	struct mtx		mdio_lock;
	struct mtx		elmer_lock;

	/* Bookkeeping for the hardware layer */
	struct adapter_params  params;
	unsigned int slow_intr_mask;
	unsigned long irq_stats[IRQ_NUM_STATS];

	unsigned		nqsets;
	struct sge              sge;
	struct mc7              pmrx;
	struct mc7              pmtx;
	struct mc7              cm;
	struct mc5              mc5;

	struct port_info	port[MAX_NPORTS];
	device_t		portdev[MAX_NPORTS];
#ifdef TCP_OFFLOAD
	void 			*tom_softc;
	void 			*iwarp_softc;
#endif
	char                    fw_version[64];
	char                    port_types[MAX_NPORTS + 1];
	uint32_t                open_device_map;
#ifdef TCP_OFFLOAD
	int			offload_map;
#endif
	struct mtx              lock;
	driver_intr_t           *cxgb_intr;
	int                     msi_count;

#define ADAPTER_LOCK_NAME_LEN	32
	char                    lockbuf[ADAPTER_LOCK_NAME_LEN];
	char                    reglockbuf[ADAPTER_LOCK_NAME_LEN];
	char                    mdiolockbuf[ADAPTER_LOCK_NAME_LEN];
	char                    elmerlockbuf[ADAPTER_LOCK_NAME_LEN];

	int			timestamp;

#ifdef TCP_OFFLOAD
#define NUM_CPL_HANDLERS	0xa7
	cpl_handler_t cpl_handler[NUM_CPL_HANDLERS] __aligned(CACHE_LINE_SIZE);
#endif
};

struct t3_rx_mode {
	
	uint32_t                idx;
	struct port_info        *port;
};

#define MDIO_LOCK(adapter)	mtx_lock(&(adapter)->mdio_lock)
#define MDIO_UNLOCK(adapter)	mtx_unlock(&(adapter)->mdio_lock)
#define ELMR_LOCK(adapter)	mtx_lock(&(adapter)->elmer_lock)
#define ELMR_UNLOCK(adapter)	mtx_unlock(&(adapter)->elmer_lock)


#define PORT_LOCK(port)		     mtx_lock(&(port)->lock);
#define PORT_UNLOCK(port)	     mtx_unlock(&(port)->lock);
#define PORT_LOCK_INIT(port, name)   mtx_init(&(port)->lock, name, 0, MTX_DEF)
#define PORT_LOCK_DEINIT(port)       mtx_destroy(&(port)->lock)
#define PORT_LOCK_ASSERT_NOTOWNED(port) mtx_assert(&(port)->lock, MA_NOTOWNED)
#define PORT_LOCK_ASSERT_OWNED(port) mtx_assert(&(port)->lock, MA_OWNED)

#define ADAPTER_LOCK(adap)	mtx_lock(&(adap)->lock);
#define ADAPTER_UNLOCK(adap)	mtx_unlock(&(adap)->lock);
#define ADAPTER_LOCK_INIT(adap, name) mtx_init(&(adap)->lock, name, 0, MTX_DEF)
#define ADAPTER_LOCK_DEINIT(adap) mtx_destroy(&(adap)->lock)
#define ADAPTER_LOCK_ASSERT_NOTOWNED(adap) mtx_assert(&(adap)->lock, MA_NOTOWNED)
#define ADAPTER_LOCK_ASSERT_OWNED(adap) mtx_assert(&(adap)->lock, MA_OWNED)


static __inline uint32_t
t3_read_reg(adapter_t *adapter, uint32_t reg_addr)
{
	return (bus_space_read_4(adapter->bt, adapter->bh, reg_addr));
}

static __inline void
t3_write_reg(adapter_t *adapter, uint32_t reg_addr, uint32_t val)
{
	bus_space_write_4(adapter->bt, adapter->bh, reg_addr, val);
}

static __inline void
t3_os_pci_read_config_4(adapter_t *adapter, int reg, uint32_t *val)
{
	*val = pci_read_config(adapter->dev, reg, 4);
}

static __inline void
t3_os_pci_write_config_4(adapter_t *adapter, int reg, uint32_t val)
{
	pci_write_config(adapter->dev, reg, val, 4);
}

static __inline void
t3_os_pci_read_config_2(adapter_t *adapter, int reg, uint16_t *val)
{
	*val = pci_read_config(adapter->dev, reg, 2);
}

static __inline void
t3_os_pci_write_config_2(adapter_t *adapter, int reg, uint16_t val)
{
	pci_write_config(adapter->dev, reg, val, 2);
}

static __inline uint8_t *
t3_get_next_mcaddr(struct t3_rx_mode *rm)
{
	uint8_t *macaddr = NULL;
	struct ifnet *ifp = rm->port->ifp;
	struct ifmultiaddr *ifma;
	int i = 0;

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		if (i == rm->idx) {
			macaddr = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
			break;
		}
		i++;
	}
	if_maddr_runlock(ifp);
	
	rm->idx++;
	return (macaddr);
}

static __inline void
t3_init_rx_mode(struct t3_rx_mode *rm, struct port_info *port)
{
	rm->idx = 0;
	rm->port = port;
}

static __inline struct port_info *
adap2pinfo(struct adapter *adap, int idx)
{
	return &adap->port[idx];
}

int t3_os_find_pci_capability(adapter_t *adapter, int cap);
int t3_os_pci_save_state(struct adapter *adapter);
int t3_os_pci_restore_state(struct adapter *adapter);
void t3_os_link_intr(struct port_info *);
void t3_os_link_changed(adapter_t *adapter, int port_id, int link_status,
			int speed, int duplex, int fc, int mac_was_reset);
void t3_os_phymod_changed(struct adapter *adap, int port_id);
void t3_sge_err_intr_handler(adapter_t *adapter);
#ifdef TCP_OFFLOAD
int t3_offload_tx(struct adapter *, struct mbuf *);
#endif
void t3_os_set_hw_addr(adapter_t *adapter, int port_idx, u8 hw_addr[]);
int t3_mgmt_tx(adapter_t *adap, struct mbuf *m);
int t3_register_cpl_handler(struct adapter *, int, cpl_handler_t);

int t3_sge_alloc(struct adapter *);
int t3_sge_free(struct adapter *);
int t3_sge_alloc_qset(adapter_t *, uint32_t, int, int, const struct qset_params *,
    int, struct port_info *);
void t3_free_sge_resources(adapter_t *, int);
void t3_sge_start(adapter_t *);
void t3_sge_stop(adapter_t *);
void t3b_intr(void *data);
void t3_intr_msi(void *data);
void t3_intr_msix(void *data);

int t3_sge_init_adapter(adapter_t *);
int t3_sge_reset_adapter(adapter_t *);
int t3_sge_init_port(struct port_info *);
void t3_free_tx_desc(struct sge_qset *qs, int n, int qid);

void t3_rx_eth(struct adapter *adap, struct mbuf *m, int ethpad);

void t3_add_attach_sysctls(adapter_t *sc);
void t3_add_configured_sysctls(adapter_t *sc);
int t3_get_desc(const struct sge_qset *qs, unsigned int qnum, unsigned int idx,
    unsigned char *data);
void t3_update_qset_coalesce(struct sge_qset *qs, const struct qset_params *p);

/*
 * XXX figure out how we can return this to being private to sge
 */
#define desc_reclaimable(q) ((int)((q)->processed - (q)->cleaned - TX_MAX_DESC))

#define container_of(p, stype, field) ((stype *)(((uint8_t *)(p)) - offsetof(stype, field)))

static __inline struct sge_qset *
fl_to_qset(struct sge_fl *q, int qidx)
{
	return container_of(q, struct sge_qset, fl[qidx]);
}

static __inline struct sge_qset *
rspq_to_qset(struct sge_rspq *q)
{
	return container_of(q, struct sge_qset, rspq);
}

static __inline struct sge_qset *
txq_to_qset(struct sge_txq *q, int qidx)
{
	return container_of(q, struct sge_qset, txq[qidx]);
}

#undef container_of

#define OFFLOAD_DEVMAP_BIT (1 << MAX_NPORTS)
static inline int offload_running(adapter_t *adapter)
{
        return isset(&adapter->open_device_map, OFFLOAD_DEVMAP_BIT);
}

void cxgb_tx_watchdog(void *arg);
int cxgb_transmit(struct ifnet *ifp, struct mbuf *m);
void cxgb_qflush(struct ifnet *ifp);
void t3_iterate(void (*)(struct adapter *, void *), void *);
void cxgb_refresh_stats(struct port_info *);

#ifdef NETDUMP
int cxgb_netdump_encap(struct sge_qset *qs, struct mbuf **m);
int cxgb_netdump_poll_rx(adapter_t *adap, struct sge_qset *qs);
int cxgb_netdump_poll_tx(struct sge_qset *qs);
#endif

#endif
