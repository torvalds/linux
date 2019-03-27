/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
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

******************************************************************************/
/*$FreeBSD$*/


#ifndef _IXGBE_H_
#define _IXGBE_H_


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf_ring.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sockio.h>
#include <sys/eventhandler.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net/if_types.h>
#include <net/if_vlan_var.h>
#include <net/iflib.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/clock.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/gtaskqueue.h>
#include <sys/pcpu.h>
#include <sys/smp.h>
#include <machine/smp.h>
#include <sys/sbuf.h>

#include "ixgbe_api.h"
#include "ixgbe_common.h"
#include "ixgbe_phy.h"
#include "ixgbe_vf.h"
#include "ixgbe_features.h"

/* Tunables */

/*
 * TxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of transmit descriptors allocated by the driver. Increasing this
 * value allows the driver to queue more transmits. Each descriptor is 16
 * bytes. Performance tests have show the 2K value to be optimal for top
 * performance.
 */
#define DEFAULT_TXD     2048
#define PERFORM_TXD     2048
#define MAX_TXD         4096
#define MIN_TXD         64

/*
 * RxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of receive descriptors allocated for each RX queue. Increasing this
 * value allows the driver to buffer more incoming packets. Each descriptor
 * is 16 bytes.  A receive buffer is also allocated for each descriptor.
 *
 * Note: with 8 rings and a dual port card, it is possible to bump up
 *       against the system mbuf pool limit, you can tune nmbclusters
 *       to adjust for this.
 */
#define DEFAULT_RXD     2048
#define PERFORM_RXD     2048
#define MAX_RXD         4096
#define MIN_RXD         64

/* Alignment for rings */
#define DBA_ALIGN       128

/*
 * This is the max watchdog interval, ie. the time that can
 * pass between any two TX clean operations, such only happening
 * when the TX hardware is functioning.
 */
#define IXGBE_WATCHDOG  (10 * hz)

/*
 * This parameters control when the driver calls the routine to reclaim
 * transmit descriptors.
 */
#define IXGBE_TX_CLEANUP_THRESHOLD(_a)  ((_a)->num_tx_desc / 8)
#define IXGBE_TX_OP_THRESHOLD(_a)       ((_a)->num_tx_desc / 32)

/* These defines are used in MTU calculations */
#define IXGBE_MAX_FRAME_SIZE  9728
#define IXGBE_MTU_HDR         (ETHER_HDR_LEN + ETHER_CRC_LEN)
#define IXGBE_MTU_HDR_VLAN    (ETHER_HDR_LEN + ETHER_CRC_LEN + \
                               ETHER_VLAN_ENCAP_LEN)
#define IXGBE_MAX_MTU         (IXGBE_MAX_FRAME_SIZE - IXGBE_MTU_HDR)
#define IXGBE_MAX_MTU_VLAN    (IXGBE_MAX_FRAME_SIZE - IXGBE_MTU_HDR_VLAN)

/* Flow control constants */
#define IXGBE_FC_PAUSE        0xFFFF
#define IXGBE_FC_HI           0x20000
#define IXGBE_FC_LO           0x10000

/*
 * Used for optimizing small rx mbufs.  Effort is made to keep the copy
 * small and aligned for the CPU L1 cache.
 *
 * MHLEN is typically 168 bytes, giving us 8-byte alignment.  Getting
 * 32 byte alignment needed for the fast bcopy results in 8 bytes being
 * wasted.  Getting 64 byte alignment, which _should_ be ideal for
 * modern Intel CPUs, results in 40 bytes wasted and a significant drop
 * in observed efficiency of the optimization, 97.9% -> 81.8%.
 */
#if __FreeBSD_version < 1002000
#define MPKTHSIZE                 (sizeof(struct m_hdr) + sizeof(struct pkthdr))
#endif
#define IXGBE_RX_COPY_HDR_PADDED  ((((MPKTHSIZE - 1) / 32) + 1) * 32)
#define IXGBE_RX_COPY_LEN         (MSIZE - IXGBE_RX_COPY_HDR_PADDED)
#define IXGBE_RX_COPY_ALIGN       (IXGBE_RX_COPY_HDR_PADDED - MPKTHSIZE)

/* Keep older OS drivers building... */
#if !defined(SYSCTL_ADD_UQUAD)
#define SYSCTL_ADD_UQUAD SYSCTL_ADD_QUAD
#endif

/* Defines for printing debug information */
#define DEBUG_INIT  0
#define DEBUG_IOCTL 0
#define DEBUG_HW    0

#define INIT_DEBUGOUT(S)            if (DEBUG_INIT)  printf(S "\n")
#define INIT_DEBUGOUT1(S, A)        if (DEBUG_INIT)  printf(S "\n", A)
#define INIT_DEBUGOUT2(S, A, B)     if (DEBUG_INIT)  printf(S "\n", A, B)
#define IOCTL_DEBUGOUT(S)           if (DEBUG_IOCTL) printf(S "\n")
#define IOCTL_DEBUGOUT1(S, A)       if (DEBUG_IOCTL) printf(S "\n", A)
#define IOCTL_DEBUGOUT2(S, A, B)    if (DEBUG_IOCTL) printf(S "\n", A, B)
#define HW_DEBUGOUT(S)              if (DEBUG_HW) printf(S "\n")
#define HW_DEBUGOUT1(S, A)          if (DEBUG_HW) printf(S "\n", A)
#define HW_DEBUGOUT2(S, A, B)       if (DEBUG_HW) printf(S "\n", A, B)

#define MAX_NUM_MULTICAST_ADDRESSES     128
#define IXGBE_82598_SCATTER             100
#define IXGBE_82599_SCATTER             32
#define MSIX_82598_BAR                  3
#define MSIX_82599_BAR                  4
#define IXGBE_TSO_SIZE                  262140
#define IXGBE_RX_HDR                    128
#define IXGBE_VFTA_SIZE                 128
#define IXGBE_BR_SIZE                   4096
#define IXGBE_QUEUE_MIN_FREE            32
#define IXGBE_MAX_TX_BUSY               10
#define IXGBE_QUEUE_HUNG                0x80000000

#define IXGBE_EITR_DEFAULT              128

/* Supported offload bits in mbuf flag */
#if __FreeBSD_version >= 1000000
#define CSUM_OFFLOAD  (CSUM_IP_TSO|CSUM_IP6_TSO|CSUM_IP| \
                       CSUM_IP_UDP|CSUM_IP_TCP|CSUM_IP_SCTP| \
                       CSUM_IP6_UDP|CSUM_IP6_TCP|CSUM_IP6_SCTP)
#elif __FreeBSD_version >= 800000
#define CSUM_OFFLOAD  (CSUM_IP|CSUM_TCP|CSUM_UDP|CSUM_SCTP)
#else
#define CSUM_OFFLOAD  (CSUM_IP|CSUM_TCP|CSUM_UDP)
#endif

#define IXGBE_CAPS (IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6 | IFCAP_TSO | \
		IFCAP_LRO | IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWTSO | \
		IFCAP_VLAN_HWCSUM | IFCAP_JUMBO_MTU | IFCAP_VLAN_MTU | \
		IFCAP_VLAN_HWFILTER | IFCAP_WOL)

/* Backward compatibility items for very old versions */
#ifndef pci_find_cap
#define pci_find_cap pci_find_extcap
#endif

#ifndef DEVMETHOD_END
#define DEVMETHOD_END { NULL, NULL }
#endif

/*
 * Interrupt Moderation parameters
 */
#define IXGBE_LOW_LATENCY   128
#define IXGBE_AVE_LATENCY   400
#define IXGBE_BULK_LATENCY  1200

/* Using 1FF (the max value), the interval is ~1.05ms */
#define IXGBE_LINK_ITR_QUANTA  0x1FF
#define IXGBE_LINK_ITR         ((IXGBE_LINK_ITR_QUANTA << 3) & \
                                IXGBE_EITR_ITR_INT_MASK)


/************************************************************************
 * vendor_info_array
 *
 *   Contains the list of Subvendor/Subdevice IDs on
 *   which the driver should load.
 ************************************************************************/
typedef struct _ixgbe_vendor_info_t {
	unsigned int vendor_id;
	unsigned int device_id;
	unsigned int subvendor_id;
	unsigned int subdevice_id;
	unsigned int index;
} ixgbe_vendor_info_t;

struct ixgbe_bp_data {
	u32 low;
	u32 high;
	u32 log;
};


/*
 */
struct ixgbe_dma_alloc {
	bus_addr_t        dma_paddr;
	caddr_t           dma_vaddr;
	bus_dma_tag_t     dma_tag;
	bus_dmamap_t      dma_map;
	bus_dma_segment_t dma_seg;
	bus_size_t        dma_size;
	int               dma_nseg;
};

struct ixgbe_mc_addr {
	u8  addr[IXGBE_ETH_LENGTH_OF_ADDRESS];
	u32 vmdq;
};

/*
 * The transmit ring, one per queue
 */
struct tx_ring {
	struct adapter          *adapter;
	union ixgbe_adv_tx_desc *tx_base;
	uint64_t                tx_paddr;
	u32                     tail;
	qidx_t                  *tx_rsq;
	qidx_t                  tx_rs_cidx;
	qidx_t                  tx_rs_pidx;
	qidx_t                  tx_cidx_processed;
	uint8_t                 me;

	/* Flow Director */
	u16                     atr_sample;
	u16                     atr_count;

	u32                     bytes;  /* used for AIM */
	u32                     packets;
	/* Soft Stats */
	u64                     tso_tx;
	u64                     total_packets;
};


/*
 * The Receive ring, one per rx queue
 */
struct rx_ring {
	struct ix_rx_queue      *que;
	struct adapter          *adapter;
	u32                     me;
	u32                     tail;
	union ixgbe_adv_rx_desc *rx_base;
	bool                    hw_rsc;
	bool                    vtag_strip;
	uint64_t rx_paddr;
	bus_dma_tag_t           ptag;

	u32                     bytes; /* Used for AIM calc */
	u32                     packets;

	/* Soft stats */
	u64                     rx_irq;
	u64                     rx_copies;
	u64                     rx_packets;
	u64                     rx_bytes;
	u64                     rx_discarded;
	u64                     rsc_num;

	/* Flow Director */
	u64                     flm;
};

/*
 * Driver queue struct: this is the interrupt container
 *  for the associated tx and rx ring.
 */
struct ix_rx_queue {
	struct adapter		*adapter;
	u32			msix;           /* This queue's MSIX vector */
	u32			eims;           /* This queue's EIMS bit */
	u32			eitr_setting;
	struct resource		*res;
	void			*tag;
	int			busy;
	struct rx_ring		rxr;
	struct if_irq           que_irq;
	u64			irqs;
};

struct ix_tx_queue {
	struct adapter		*adapter;
	u32			msix;           /* This queue's MSIX vector */
	struct tx_ring		txr;
};

#define IXGBE_MAX_VF_MC 30  /* Max number of multicast entries */

struct ixgbe_vf {
	u_int    pool;
	u_int    rar_index;
	u_int    maximum_frame_size;
	uint32_t flags;
	uint8_t  ether_addr[ETHER_ADDR_LEN];
	uint16_t mc_hash[IXGBE_MAX_VF_MC];
	uint16_t num_mc_hashes;
	uint16_t default_vlan;
	uint16_t vlan_tag;
	uint16_t api_ver;
};

/* Our adapter structure */
struct adapter {
	struct ixgbe_hw         hw;
	struct ixgbe_osdep      osdep;
	if_ctx_t                ctx;
	if_softc_ctx_t          shared;
#define num_tx_queues shared->isc_ntxqsets
#define num_rx_queues shared->isc_nrxqsets
#define max_frame_size shared->isc_max_frame_size
#define intr_type shared->isc_intr

	device_t                dev;
	struct ifnet            *ifp;

	struct resource         *pci_mem;

	/*
	 * Interrupt resources: this set is
	 * either used for legacy, or for Link
	 * when doing MSI-X
	 */
	struct if_irq           irq;
	void                    *tag;
	struct resource         *res;

	struct ifmedia          *media;
	int                     if_flags;
	int                     msix;

	u16                     num_vlans;

	/*
	 * Shadow VFTA table, this is needed because
	 * the real vlan filter table gets cleared during
	 * a soft reset and the driver needs to be able
	 * to repopulate it.
	 */
	u32                     shadow_vfta[IXGBE_VFTA_SIZE];

	/* Info about the interface */
	int                     advertise;  /* link speeds */
	bool                    link_active;
	u16                     num_segs;
	u32                     link_speed;
	bool                    link_up;
	u32                     vector;
	u16                     dmac;
	u32                     phy_layer;

	/* Power management-related */
	bool                    wol_support;
	u32                     wufc;

	/* Mbuf cluster size */
	u32                     rx_mbuf_sz;

	/* Support for pluggable optics */
	bool                    sfp_probe;

	/* Flow Director */
	int                     fdir_reinit;

	u32			task_requests;

	/*
	 * Queues:
	 *   This is the irq holder, it has
	 *   and RX/TX pair or rings associated
	 *   with it.
	 */
	struct ix_tx_queue	*tx_queues;
	struct ix_rx_queue	*rx_queues;
	u64			active_queues;

	/* Multicast array memory */
	struct ixgbe_mc_addr    *mta;

	/* SR-IOV */
	int                     iov_mode;
	int                     num_vfs;
	int                     pool;
	struct ixgbe_vf         *vfs;

	/* Bypass */
	struct ixgbe_bp_data    bypass;

	/* Misc stats maintained by the driver */
	unsigned long           dropped_pkts;
	unsigned long           mbuf_header_failed;
	unsigned long           mbuf_packet_failed;
	unsigned long           watchdog_events;
	unsigned long           link_irq;
	union {
		struct ixgbe_hw_stats pf;
		struct ixgbevf_hw_stats vf;
	} stats;
#if __FreeBSD_version >= 1100036
	/* counter(9) stats */
	u64                     ipackets;
	u64                     ierrors;
	u64                     opackets;
	u64                     oerrors;
	u64                     ibytes;
	u64                     obytes;
	u64                     imcasts;
	u64                     omcasts;
	u64                     iqdrops;
	u64                     noproto;
#endif
	/* Feature capable/enabled flags.  See ixgbe_features.h */
	u32                     feat_cap;
	u32                     feat_en;
};

/* Precision Time Sync (IEEE 1588) defines */
#define ETHERTYPE_IEEE1588      0x88F7
#define PICOSECS_PER_TICK       20833
#define TSYNC_UDP_PORT          319 /* UDP port for the protocol */
#define IXGBE_ADVTXD_TSTAMP     0x00080000

/* For backward compatibility */
#if !defined(PCIER_LINK_STA)
#define PCIER_LINK_STA PCIR_EXPRESS_LINK_STA
#endif

/* Stats macros */
#if __FreeBSD_version >= 1100036
#define IXGBE_SET_IPACKETS(sc, count)    (sc)->ipackets = (count)
#define IXGBE_SET_IERRORS(sc, count)     (sc)->ierrors = (count)
#define IXGBE_SET_OPACKETS(sc, count)    (sc)->opackets = (count)
#define IXGBE_SET_OERRORS(sc, count)     (sc)->oerrors = (count)
#define IXGBE_SET_COLLISIONS(sc, count)
#define IXGBE_SET_IBYTES(sc, count)      (sc)->ibytes = (count)
#define IXGBE_SET_OBYTES(sc, count)      (sc)->obytes = (count)
#define IXGBE_SET_IMCASTS(sc, count)     (sc)->imcasts = (count)
#define IXGBE_SET_OMCASTS(sc, count)     (sc)->omcasts = (count)
#define IXGBE_SET_IQDROPS(sc, count)     (sc)->iqdrops = (count)
#else
#define IXGBE_SET_IPACKETS(sc, count)    (sc)->ifp->if_ipackets = (count)
#define IXGBE_SET_IERRORS(sc, count)     (sc)->ifp->if_ierrors = (count)
#define IXGBE_SET_OPACKETS(sc, count)    (sc)->ifp->if_opackets = (count)
#define IXGBE_SET_OERRORS(sc, count)     (sc)->ifp->if_oerrors = (count)
#define IXGBE_SET_COLLISIONS(sc, count)  (sc)->ifp->if_collisions = (count)
#define IXGBE_SET_IBYTES(sc, count)      (sc)->ifp->if_ibytes = (count)
#define IXGBE_SET_OBYTES(sc, count)      (sc)->ifp->if_obytes = (count)
#define IXGBE_SET_IMCASTS(sc, count)     (sc)->ifp->if_imcasts = (count)
#define IXGBE_SET_OMCASTS(sc, count)     (sc)->ifp->if_omcasts = (count)
#define IXGBE_SET_IQDROPS(sc, count)     (sc)->ifp->if_iqdrops = (count)
#endif

/* External PHY register addresses */
#define IXGBE_PHY_CURRENT_TEMP     0xC820
#define IXGBE_PHY_OVERTEMP_STATUS  0xC830

/* Sysctl help messages; displayed with sysctl -d */
#define IXGBE_SYSCTL_DESC_ADV_SPEED \
        "\nControl advertised link speed using these flags:\n" \
        "\t0x1 - advertise 100M\n" \
        "\t0x2 - advertise 1G\n" \
        "\t0x4 - advertise 10G\n" \
        "\t0x8 - advertise 10M\n\n" \
        "\t100M and 10M are only supported on certain adapters.\n"

#define IXGBE_SYSCTL_DESC_SET_FC \
        "\nSet flow control mode using these values:\n" \
        "\t0 - off\n" \
        "\t1 - rx pause\n" \
        "\t2 - tx pause\n" \
        "\t3 - tx and rx pause"

/* Workaround to make 8.0 buildable */
#if __FreeBSD_version >= 800000 && __FreeBSD_version < 800504
static __inline int
drbr_needs_enqueue(struct ifnet *ifp, struct buf_ring *br)
{
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd))
		return (1);
#endif
	return (!buf_ring_empty(br));
}
#endif

/*
 * This checks for a zero mac addr, something that will be likely
 * unless the Admin on the Host has created one.
 */
static inline bool
ixv_check_ether_addr(u8 *addr)
{
	bool status = TRUE;

	if ((addr[0] == 0 && addr[1]== 0 && addr[2] == 0 &&
	    addr[3] == 0 && addr[4]== 0 && addr[5] == 0))
		status = FALSE;

	return (status);
}

/* Shared Prototypes */

int  ixgbe_allocate_queues(struct adapter *);
int  ixgbe_setup_transmit_structures(struct adapter *);
void ixgbe_free_transmit_structures(struct adapter *);
int  ixgbe_setup_receive_structures(struct adapter *);
void ixgbe_free_receive_structures(struct adapter *);
int  ixgbe_get_regs(SYSCTL_HANDLER_ARGS);

#include "ixgbe_bypass.h"
#include "ixgbe_fdir.h"
#include "ixgbe_rss.h"

#endif /* _IXGBE_H_ */
