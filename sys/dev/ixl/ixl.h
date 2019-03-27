/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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

#ifndef _IXL_H_
#define _IXL_H_

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_rss.h"
#include "opt_ixl.h"

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
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/iflib.h>

#include <net/bpf.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/tcp_lro.h>
#include <netinet/udp.h>
#include <netinet/sctp.h>

#include <machine/in_cksum.h>

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
#include <sys/taskqueue.h>
#include <sys/pcpu.h>
#include <sys/smp.h>
#include <sys/sbuf.h>
#include <machine/smp.h>
#include <machine/stdarg.h>

#ifdef RSS
#include <net/rss_config.h>
#include <netinet/in_rss.h>
#endif

#include "ifdi_if.h"
#include "i40e_type.h"
#include "i40e_prototype.h"
#include "ixl_debug.h"

#define PVIDV(vendor, devid, name) \
    PVID(vendor, devid, name " - " IXL_DRIVER_VERSION_STRING)

/* Tunables */

/*
 * Ring Descriptors Valid Range: 32-4096 Default Value: 1024 This value is the
 * number of tx/rx descriptors allocated by the driver. Increasing this
 * value allows the driver to queue more operations.
 *
 * Tx descriptors are always 16 bytes, but Rx descriptors can be 32 bytes.
 * The driver currently always uses 32 byte Rx descriptors.
 */
#define IXL_DEFAULT_RING	1024
#define IXL_MAX_RING		4096
#define IXL_MIN_RING		64
#define IXL_RING_INCREMENT	32

#define IXL_AQ_LEN		256
#define IXL_AQ_LEN_MAX		1024

/* Alignment for rings */
#define DBA_ALIGN		128

#define MAX_MULTICAST_ADDR	128

#define IXL_MSIX_BAR		3
#define IXL_ADM_LIMIT		2
#define IXL_TSO_SIZE		((255*1024)-1)
#define IXL_TX_BUF_SZ		((u32) 1514)
#define IXL_AQ_BUF_SZ		((u32) 4096)
#define IXL_RX_ITR		0
#define IXL_TX_ITR		1
#define IXL_ITR_NONE		3
#define IXL_QUEUE_EOL		0x7FF
#define IXL_MIN_FRAME		17
#define IXL_MAX_FRAME		9728
#define IXL_MAX_TX_SEGS		8
#define IXL_MAX_RX_SEGS		5
#define IXL_MAX_TSO_SEGS	128
#define IXL_SPARSE_CHAIN	7
#define IXL_MIN_TSO_MSS		64
#define IXL_MAX_TSO_MSS		9668
#define IXL_MAX_DMA_SEG_SIZE	((16 * 1024) - 1)

#define IXL_RSS_KEY_SIZE_REG		13
#define IXL_RSS_KEY_SIZE		(IXL_RSS_KEY_SIZE_REG * 4)
#define IXL_RSS_VSI_LUT_SIZE		64	/* X722 -> VSI, X710 -> VF */
#define IXL_RSS_VSI_LUT_ENTRY_MASK	0x3F
#define IXL_RSS_VF_LUT_ENTRY_MASK	0xF

#define IXL_VF_MAX_BUFFER	0x3F80
#define IXL_VF_MAX_HDR_BUFFER	0x840
#define IXL_VF_MAX_FRAME	0x3FFF

/* ERJ: hardware can support ~2k (SW5+) filters between all functions */
#define IXL_MAX_FILTERS		256

#define IXL_NVM_VERSION_LO_SHIFT	0
#define IXL_NVM_VERSION_LO_MASK		(0xff << IXL_NVM_VERSION_LO_SHIFT)
#define IXL_NVM_VERSION_HI_SHIFT	12
#define IXL_NVM_VERSION_HI_MASK		(0xf << IXL_NVM_VERSION_HI_SHIFT)

/*
 * Interrupt Moderation parameters
 * Multiply ITR values by 2 for real ITR value
 */
#define IXL_MAX_ITR		0x0FF0
#define IXL_ITR_100K		0x0005
#define IXL_ITR_20K		0x0019
#define IXL_ITR_8K		0x003E
#define IXL_ITR_4K		0x007A
#define IXL_ITR_1K		0x01F4
#define IXL_ITR_DYNAMIC		0x8000
#define IXL_LOW_LATENCY		0
#define IXL_AVE_LATENCY		1
#define IXL_BULK_LATENCY	2

/* MacVlan Flags */
#define IXL_FILTER_USED		(u16)(1 << 0)
#define IXL_FILTER_VLAN		(u16)(1 << 1)
#define IXL_FILTER_ADD		(u16)(1 << 2)
#define IXL_FILTER_DEL		(u16)(1 << 3)
#define IXL_FILTER_MC		(u16)(1 << 4)

/* used in the vlan field of the filter when not a vlan */
#define IXL_VLAN_ANY		-1

#define CSUM_OFFLOAD_IPV4	(CSUM_IP|CSUM_TCP|CSUM_UDP|CSUM_SCTP)
#define CSUM_OFFLOAD_IPV6	(CSUM_TCP_IPV6|CSUM_UDP_IPV6|CSUM_SCTP_IPV6)
#define CSUM_OFFLOAD		(CSUM_OFFLOAD_IPV4|CSUM_OFFLOAD_IPV6|CSUM_TSO)

#define IXL_VF_RESET_TIMEOUT	100

#define IXL_VSI_DATA_PORT	0x01

#define IAVF_MAX_QUEUES		16
#define IXL_MAX_VSI_QUEUES	(2 * (I40E_VSILAN_QTABLE_MAX_INDEX + 1))

#define IXL_RX_CTX_BASE_UNITS	128
#define IXL_TX_CTX_BASE_UNITS	128

#define IXL_PF_PCI_CIAA_VF_DEVICE_STATUS	0xAA

#define IXL_PF_PCI_CIAD_VF_TRANS_PENDING_MASK	0x20

#define IXL_GLGEN_VFLRSTAT_INDEX(glb_vf)	((glb_vf) / 32)
#define IXL_GLGEN_VFLRSTAT_MASK(glb_vf)	(1 << ((glb_vf) % 32))

#define IXL_MAX_ITR_IDX		3

#define IXL_END_OF_INTR_LNKLST	0x7FF

#define IXL_DEFAULT_RSS_HENA_BASE (\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_UDP) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_SCTP) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_OTHER) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV4) |		\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_UDP) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_TCP) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_SCTP) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_OTHER) |	\
	BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV6) |		\
	BIT_ULL(I40E_FILTER_PCTYPE_L2_PAYLOAD))

#define IXL_DEFAULT_RSS_HENA_XL710	IXL_DEFAULT_RSS_HENA_BASE

#define IXL_DEFAULT_RSS_HENA_X722 (\
	IXL_DEFAULT_RSS_HENA_BASE |			\
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_TCP_SYN_NO_ACK))

#define IXL_CAPS \
	(IFCAP_TSO4 | IFCAP_TSO6 | \
	 IFCAP_TXCSUM | IFCAP_TXCSUM_IPV6 | \
	 IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6 | \
	 IFCAP_VLAN_HWFILTER | IFCAP_VLAN_HWTSO | \
	 IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWCSUM | \
	 IFCAP_VLAN_MTU | IFCAP_JUMBO_MTU | IFCAP_LRO)

#define IXL_CSUM_TCP \
	(CSUM_IP_TCP|CSUM_IP_TSO|CSUM_IP6_TSO|CSUM_IP6_TCP)
#define IXL_CSUM_UDP \
	(CSUM_IP_UDP|CSUM_IP6_UDP)
#define IXL_CSUM_SCTP \
	(CSUM_IP_SCTP|CSUM_IP6_SCTP)
#define IXL_CSUM_IPV4 \
	(CSUM_IP|CSUM_IP_TSO)

/* Pre-11 counter(9) compatibility */
#if __FreeBSD_version >= 1100036
#define IXL_SET_IPACKETS(vsi, count)	(vsi)->ipackets = (count)
#define IXL_SET_IERRORS(vsi, count)	(vsi)->ierrors = (count)
#define IXL_SET_OPACKETS(vsi, count)	(vsi)->opackets = (count)
#define IXL_SET_OERRORS(vsi, count)	(vsi)->oerrors = (count)
#define IXL_SET_COLLISIONS(vsi, count)	/* Do nothing; collisions is always 0. */
#define IXL_SET_IBYTES(vsi, count)	(vsi)->ibytes = (count)
#define IXL_SET_OBYTES(vsi, count)	(vsi)->obytes = (count)
#define IXL_SET_IMCASTS(vsi, count)	(vsi)->imcasts = (count)
#define IXL_SET_OMCASTS(vsi, count)	(vsi)->omcasts = (count)
#define IXL_SET_IQDROPS(vsi, count)	(vsi)->iqdrops = (count)
#define IXL_SET_OQDROPS(vsi, count)	(vsi)->oqdrops = (count)
#define IXL_SET_NOPROTO(vsi, count)	(vsi)->noproto = (count)
#else
#define IXL_SET_IPACKETS(vsi, count)	(vsi)->ifp->if_ipackets = (count)
#define IXL_SET_IERRORS(vsi, count)	(vsi)->ifp->if_ierrors = (count)
#define IXL_SET_OPACKETS(vsi, count)	(vsi)->ifp->if_opackets = (count)
#define IXL_SET_OERRORS(vsi, count)	(vsi)->ifp->if_oerrors = (count)
#define IXL_SET_COLLISIONS(vsi, count)	(vsi)->ifp->if_collisions = (count)
#define IXL_SET_IBYTES(vsi, count)	(vsi)->ifp->if_ibytes = (count)
#define IXL_SET_OBYTES(vsi, count)	(vsi)->ifp->if_obytes = (count)
#define IXL_SET_IMCASTS(vsi, count)	(vsi)->ifp->if_imcasts = (count)
#define IXL_SET_OMCASTS(vsi, count)	(vsi)->ifp->if_omcasts = (count)
#define IXL_SET_IQDROPS(vsi, count)	(vsi)->ifp->if_iqdrops = (count)
#define IXL_SET_OQDROPS(vsi, odrops)	(vsi)->ifp->if_snd.ifq_drops = (odrops)
#define IXL_SET_NOPROTO(vsi, count)	(vsi)->noproto = (count)
#endif

/* For stats sysctl naming */
#define QUEUE_NAME_LEN 32

#define IXL_DEV_ERR(_dev, _format, ...) \
	device_printf(_dev, "%s: " _format " (%s:%d)\n", __func__, ##__VA_ARGS__, __FILE__, __LINE__)

/*
 *****************************************************************************
 * vendor_info_array
 * 
 * This array contains the list of Subvendor/Subdevice IDs on which the driver
 * should load.
 * 
 *****************************************************************************
 */
typedef struct _ixl_vendor_info_t {
	unsigned int    vendor_id;
	unsigned int    device_id;
	unsigned int    subvendor_id;
	unsigned int    subdevice_id;
	unsigned int    index;
} ixl_vendor_info_t;

/*
** This struct has multiple uses, multicast
** addresses, vlans, and mac filters all use it.
*/
struct ixl_mac_filter {
	SLIST_ENTRY(ixl_mac_filter) next;
	u8	macaddr[ETHER_ADDR_LEN];
	s16	vlan;
	u16	flags;
};

/*
 * The Transmit ring control struct
 */
struct tx_ring {
        struct ixl_tx_queue	*que;
	u32			tail;
	struct i40e_tx_desc	*tx_base;
	u64			tx_paddr;
	u32			latency;
	u32			packets;
	u32			me;
	/*
	 * For reporting completed packet status
	 * in descriptor writeback mode
	 */
	qidx_t			*tx_rsq;
	qidx_t			tx_rs_cidx;
	qidx_t			tx_rs_pidx;
	qidx_t			tx_cidx_processed;

	/* Used for Dynamic ITR calculation */
	u32			itr;
	u32 			bytes;

	/* Soft Stats */
	u64			tx_bytes;
	u64			tx_packets;
	u64			mss_too_small;
};


/*
 * The Receive ring control struct
 */
struct rx_ring {
        struct ixl_rx_queue	*que;
	union i40e_rx_desc	*rx_base;
	uint64_t		rx_paddr;
	bool			discard;
	u32			itr;
	u32			latency;
	u32			mbuf_sz;
	u32			tail;
	u32			me;

	/* Used for Dynamic ITR calculation */
	u32			packets;
	u32 			bytes;

	/* Soft stats */
	u64			rx_packets;
	u64 			rx_bytes;
	u64 			desc_errs;
};

/*
** Driver queue structs
*/
struct ixl_tx_queue {
	struct ixl_vsi		*vsi;
	struct tx_ring		txr;
	struct if_irq		que_irq;
	u32			msix;
	/* Stats */
	u64			irqs;
	u64			tso;
};

struct ixl_rx_queue {
	struct ixl_vsi		*vsi;
	struct rx_ring		rxr;
	struct if_irq		que_irq;
	u32			msix;           /* This queue's MSIX vector */
	/* Stats */
	u64			irqs;
};

/*
** Virtual Station Interface
*/
SLIST_HEAD(ixl_ftl_head, ixl_mac_filter);
struct ixl_vsi {
	if_ctx_t		ctx;
	if_softc_ctx_t		shared;
	struct ifnet		*ifp;
	device_t		dev;
	struct i40e_hw		*hw;
	struct ifmedia		*media;

	int			num_rx_queues;
	int			num_tx_queues;

	void 			*back;
	enum i40e_vsi_type	type;
	int			id;
	u32			rx_itr_setting;
	u32			tx_itr_setting;
	bool			enable_head_writeback;

	u16			vsi_num;
	bool			link_active;
	u16			seid;
	u16			uplink_seid;
	u16			downlink_seid;

	struct ixl_tx_queue	*tx_queues;	/* TX queue array */
	struct ixl_rx_queue	*rx_queues;	/* RX queue array */
	struct if_irq		irq;
	u32			link_speed;

	/* MAC/VLAN Filter list */
	struct ixl_ftl_head	ftl;
	u16			num_macs;

	/* Contains readylist & stat counter id */
	struct i40e_aqc_vsi_properties_data info;

	u16			num_vlans;

	/* Per-VSI stats from hardware */
	struct i40e_eth_stats	eth_stats;
	struct i40e_eth_stats	eth_stats_offsets;
	bool 			stat_offsets_loaded;
	/* VSI stat counters */
	u64			ipackets;
	u64			ierrors;
	u64			opackets;
	u64			oerrors;
	u64			ibytes;
	u64			obytes;
	u64			imcasts;
	u64			omcasts;
	u64			iqdrops;
	u64			oqdrops;
	u64			noproto;

	/* Driver statistics */
	u64			hw_filters_del;
	u64			hw_filters_add;

	/* Misc. */
	u64 			flags;
	/* Stats sysctls for this VSI */
	struct sysctl_oid	*vsi_node;
};

/*
** Creates new filter with given MAC address and VLAN ID
*/
static inline struct ixl_mac_filter *
ixl_new_filter(struct ixl_vsi *vsi, const u8 *macaddr, s16 vlan)
{
	struct ixl_mac_filter  *f;

	/* create a new empty filter */
	f = malloc(sizeof(struct ixl_mac_filter),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (f) {
		SLIST_INSERT_HEAD(&vsi->ftl, f, next);
		bcopy(macaddr, f->macaddr, ETHER_ADDR_LEN);
		f->vlan = vlan;
		f->flags |= (IXL_FILTER_ADD | IXL_FILTER_USED);
	}

	return (f);
}

/*
** Compare two ethernet addresses
*/
static inline bool
cmp_etheraddr(const u8 *ea1, const u8 *ea2)
{       
	return (bcmp(ea1, ea2, 6) == 0);
}       

/*
 * Return next largest power of 2, unsigned
 *
 * Public domain, from Bit Twiddling Hacks
 */
static inline u32
next_power_of_two(u32 n)
{
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;

	/* Next power of two > 0 is 1 */
	n += (n == 0);

	return (n);
}

/*
 * Info for stats sysctls
 */
struct ixl_sysctl_info {
	u64	*stat;
	char	*name;
	char	*description;
};

extern const uint8_t ixl_bcast_addr[ETHER_ADDR_LEN];

/* Common function prototypes between PF/VF driver */
void		ixl_debug_core(device_t dev, u32 enabled_mask, u32 mask, char *fmt, ...);
void		 ixl_init_tx_ring(struct ixl_vsi *vsi, struct ixl_tx_queue *que);
void		 ixl_get_default_rss_key(u32 *);
const char *	i40e_vc_stat_str(struct i40e_hw *hw,
    enum virtchnl_status_code stat_err);
void		ixl_init_tx_rsqs(struct ixl_vsi *vsi);
void		ixl_init_tx_cidx(struct ixl_vsi *vsi);
u64		ixl_max_vc_speed_to_value(u8 link_speeds);
void		ixl_add_vsi_sysctls(device_t dev, struct ixl_vsi *vsi,
		    struct sysctl_ctx_list *ctx, const char *sysctl_name);
void		ixl_add_sysctls_eth_stats(struct sysctl_ctx_list *ctx,
		    struct sysctl_oid_list *child,
		    struct i40e_eth_stats *eth_stats);
void		ixl_add_queues_sysctls(device_t dev, struct ixl_vsi *vsi);
#endif /* _IXL_H_ */
