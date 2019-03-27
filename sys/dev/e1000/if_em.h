/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Nicole Graziano <nicole@nextbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

/*$FreeBSD$*/
#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#ifdef DDB
#include <sys/types.h>
#include <ddb/ddb.h>
#endif
#if __FreeBSD_version >= 800000
#include <sys/buf_ring.h>
#endif
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/eventhandler.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/iflib.h>

#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <machine/in_cksum.h>
#include <dev/led/led.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "e1000_api.h"
#include "e1000_82571.h"
#include "ifdi_if.h"


#ifndef _EM_H_DEFINED_
#define _EM_H_DEFINED_


/* Tunables */

/*
 * EM_MAX_TXD: Maximum number of Transmit Descriptors
 * Valid Range: 80-256 for 82542 and 82543-based adapters
 *              80-4096 for others
 * Default Value: 1024
 *   This value is the number of transmit descriptors allocated by the driver.
 *   Increasing this value allows the driver to queue more transmits. Each
 *   descriptor is 16 bytes.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   desscriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct e1000_tx_desc)) % 128 == 0
 */
#define EM_MIN_TXD		128
#define EM_MAX_TXD		4096
#define EM_DEFAULT_TXD          1024
#define EM_DEFAULT_MULTI_TXD	4096
#define IGB_MAX_TXD		4096

/*
 * EM_MAX_RXD - Maximum number of receive Descriptors
 * Valid Range: 80-256 for 82542 and 82543-based adapters
 *              80-4096 for others
 * Default Value: 1024
 *   This value is the number of receive descriptors allocated by the driver.
 *   Increasing this value allows the driver to buffer more incoming packets.
 *   Each descriptor is 16 bytes.  A receive buffer is also allocated for each
 *   descriptor. The maximum MTU size is 16110.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   desscriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct e1000_tx_desc)) % 128 == 0
 */
#define EM_MIN_RXD		128
#define EM_MAX_RXD		4096
#define EM_DEFAULT_RXD          1024
#define EM_DEFAULT_MULTI_RXD	4096
#define IGB_MAX_RXD		4096

/*
 * EM_TIDV - Transmit Interrupt Delay Value
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value delays the generation of transmit interrupts in units of
 *   1.024 microseconds. Transmit interrupt reduction can improve CPU
 *   efficiency if properly tuned for specific network traffic. If the
 *   system is reporting dropped transmits, this value may be set too high
 *   causing the driver to run out of available transmit descriptors.
 */
#define EM_TIDV                         64

/*
 * EM_TADV - Transmit Absolute Interrupt Delay Value
 * (Not valid for 82542/82543/82544)
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value, in units of 1.024 microseconds, limits the delay in which a
 *   transmit interrupt is generated. Useful only if EM_TIDV is non-zero,
 *   this value ensures that an interrupt is generated after the initial
 *   packet is sent on the wire within the set amount of time.  Proper tuning,
 *   along with EM_TIDV, may improve traffic throughput in specific
 *   network conditions.
 */
#define EM_TADV                         64

/*
 * EM_RDTR - Receive Interrupt Delay Timer (Packet Timer)
 * Valid Range: 0-65535 (0=off)
 * Default Value: 0
 *   This value delays the generation of receive interrupts in units of 1.024
 *   microseconds.  Receive interrupt reduction can improve CPU efficiency if
 *   properly tuned for specific network traffic. Increasing this value adds
 *   extra latency to frame reception and can end up decreasing the throughput
 *   of TCP traffic. If the system is reporting dropped receives, this value
 *   may be set too high, causing the driver to run out of available receive
 *   descriptors.
 *
 *   CAUTION: When setting EM_RDTR to a value other than 0, adapters
 *            may hang (stop transmitting) under certain network conditions.
 *            If this occurs a WATCHDOG message is logged in the system
 *            event log. In addition, the controller is automatically reset,
 *            restoring the network connection. To eliminate the potential
 *            for the hang ensure that EM_RDTR is set to 0.
 */
#define EM_RDTR                         0

/*
 * Receive Interrupt Absolute Delay Timer (Not valid for 82542/82543/82544)
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value, in units of 1.024 microseconds, limits the delay in which a
 *   receive interrupt is generated. Useful only if EM_RDTR is non-zero,
 *   this value ensures that an interrupt is generated after the initial
 *   packet is received within the set amount of time.  Proper tuning,
 *   along with EM_RDTR, may improve traffic throughput in specific network
 *   conditions.
 */
#define EM_RADV                         64

/*
 * This parameter controls whether or not autonegotation is enabled.
 *              0 - Disable autonegotiation
 *              1 - Enable  autonegotiation
 */
#define DO_AUTO_NEG                     1

/*
 * This parameter control whether or not the driver will wait for
 * autonegotiation to complete.
 *              1 - Wait for autonegotiation to complete
 *              0 - Don't wait for autonegotiation to complete
 */
#define WAIT_FOR_AUTO_NEG_DEFAULT       0

/* Tunables -- End */

#define AUTONEG_ADV_DEFAULT	(ADVERTISE_10_HALF | ADVERTISE_10_FULL | \
				ADVERTISE_100_HALF | ADVERTISE_100_FULL | \
				ADVERTISE_1000_FULL)

#define AUTO_ALL_MODES		0

/* PHY master/slave setting */
#define EM_MASTER_SLAVE		e1000_ms_hw_default

/*
 * Micellaneous constants
 */
#define EM_VENDOR_ID                    0x8086
#define EM_FLASH                        0x0014 

#define EM_JUMBO_PBA                    0x00000028
#define EM_DEFAULT_PBA                  0x00000030
#define EM_SMARTSPEED_DOWNSHIFT         3
#define EM_SMARTSPEED_MAX               15
#define EM_MAX_LOOP			10

#define MAX_NUM_MULTICAST_ADDRESSES     128
#define PCI_ANY_ID                      (~0U)
#define ETHER_ALIGN                     2
#define EM_FC_PAUSE_TIME		0x0680
#define EM_EEPROM_APME			0x400;
#define EM_82544_APME			0x0004;


/* Support AutoMediaDetect for Marvell M88 PHY in i354 */
#define IGB_MEDIA_RESET			(1 << 0)

/* Define the starting Interrupt rate per Queue */
#define IGB_INTS_PER_SEC        8000
#define IGB_DEFAULT_ITR         ((1000000/IGB_INTS_PER_SEC) << 2)

#define IGB_LINK_ITR            2000
#define I210_LINK_DELAY		1000

#define IGB_TXPBSIZE		20408
#define IGB_HDR_BUF		128
#define IGB_PKTTYPE_MASK	0x0000FFF0
#define IGB_DMCTLX_DCFLUSH_DIS	0x80000000  /* Disable DMA Coalesce Flush */

/*
 * Driver state logic for the detection of a hung state
 * in hardware.  Set TX_HUNG whenever a TX packet is used
 * (data is sent) and clear it when txeof() is invoked if
 * any descriptors from the ring are cleaned/reclaimed.
 * Increment internal counter if no descriptors are cleaned
 * and compare to TX_MAXTRIES.  When counter > TX_MAXTRIES,
 * reset adapter.
 */
#define EM_TX_IDLE			0x00000000
#define EM_TX_BUSY			0x00000001
#define EM_TX_HUNG			0x80000000
#define EM_TX_MAXTRIES			10

#define PCICFG_DESC_RING_STATUS		0xe4
#define FLUSH_DESC_REQUIRED		0x100


#define IGB_RX_PTHRESH			((hw->mac.type == e1000_i354) ? 12 : \
					  ((hw->mac.type <= e1000_82576) ? 16 : 8))
#define IGB_RX_HTHRESH			8
#define IGB_RX_WTHRESH			((hw->mac.type == e1000_82576 && \
					  (adapter->intr_type == IFLIB_INTR_MSIX)) ? 1 : 4)

#define IGB_TX_PTHRESH			((hw->mac.type == e1000_i354) ? 20 : 8)
#define IGB_TX_HTHRESH			1
#define IGB_TX_WTHRESH			((hw->mac.type != e1000_82575 && \
                                          (adapter->intr_type == IFLIB_INTR_MSIX) ? 1 : 16)

/*
 * TDBA/RDBA should be aligned on 16 byte boundary. But TDLEN/RDLEN should be
 * multiple of 128 bytes. So we align TDBA/RDBA on 128 byte boundary. This will
 * also optimize cache line size effect. H/W supports up to cache line size 128.
 */
#define EM_DBA_ALIGN			128

/*
 * See Intel 82574 Driver Programming Interface Manual, Section 10.2.6.9
 */
#define TARC_COMPENSATION_MODE	(1 << 7)	/* Compensation Mode */
#define TARC_SPEED_MODE_BIT 	(1 << 21)	/* On PCI-E MACs only */
#define TARC_MQ_FIX		(1 << 23) | \
				(1 << 24) | \
				(1 << 25)	/* Handle errata in MQ mode */
#define TARC_ERRATA_BIT 	(1 << 26)	/* Note from errata on 82574 */

/* PCI Config defines */
#define EM_BAR_TYPE(v)		((v) & EM_BAR_TYPE_MASK)
#define EM_BAR_TYPE_MASK	0x00000001
#define EM_BAR_TYPE_MMEM	0x00000000
#define EM_BAR_TYPE_IO		0x00000001
#define EM_BAR_TYPE_FLASH	0x0014 
#define EM_BAR_MEM_TYPE(v)	((v) & EM_BAR_MEM_TYPE_MASK)
#define EM_BAR_MEM_TYPE_MASK	0x00000006
#define EM_BAR_MEM_TYPE_32BIT	0x00000000
#define EM_BAR_MEM_TYPE_64BIT	0x00000004
#define EM_MSIX_BAR		3	/* On 82575 */

/* More backward compatibility */
#if __FreeBSD_version < 900000
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

#define EM_MAX_SCATTER		40
#define EM_VFTA_SIZE		128
#define EM_TSO_SIZE		65535
#define EM_TSO_SEG_SIZE		4096	/* Max dma segment size */
#define EM_MSIX_MASK		0x01F00000 /* For 82574 use */
#define EM_MSIX_LINK		0x01000000 /* For 82574 use */
#define ETH_ZLEN		60
#define ETH_ADDR_LEN		6
#define EM_CSUM_OFFLOAD		(CSUM_IP | CSUM_IP_UDP | CSUM_IP_TCP) /* Offload bits in mbuf flag */
#define IGB_CSUM_OFFLOAD	(CSUM_IP | CSUM_IP_UDP | CSUM_IP_TCP | \
				 CSUM_IP_SCTP | CSUM_IP6_UDP | CSUM_IP6_TCP | \
				 CSUM_IP6_SCTP)	/* Offload bits in mbuf flag */


#define IGB_PKTTYPE_MASK	0x0000FFF0
#define IGB_DMCTLX_DCFLUSH_DIS	0x80000000  /* Disable DMA Coalesce Flush */

/*
 * 82574 has a nonstandard address for EIAC
 * and since its only used in MSI-X, and in
 * the em driver only 82574 uses MSI-X we can
 * solve it just using this define.
 */
#define EM_EIAC 0x000DC
/*
 * 82574 only reports 3 MSI-X vectors by default;
 * defines assisting with making it report 5 are
 * located here.
 */
#define EM_NVM_PCIE_CTRL	0x1B
#define EM_NVM_MSIX_N_MASK	(0x7 << EM_NVM_MSIX_N_SHIFT)
#define EM_NVM_MSIX_N_SHIFT	7

struct adapter;

struct em_int_delay_info {
	struct adapter *adapter;	/* Back-pointer to the adapter struct */
	int offset;			/* Register offset to read/write */
	int value;			/* Current value in usecs */
};

/*
 * The transmit ring, one per tx queue
 */
struct tx_ring {
        struct adapter          *adapter;
	struct e1000_tx_desc	*tx_base;
	uint64_t                tx_paddr; 
	qidx_t			*tx_rsq;
	bool			tx_tso;		/* last tx was tso */
	uint8_t			me;
	qidx_t			tx_rs_cidx;
	qidx_t			tx_rs_pidx;
	qidx_t			tx_cidx_processed;
	/* Interrupt resources */
	void                    *tag;
	struct resource         *res;
        unsigned long		tx_irq;

	/* Saved csum offloading context information */
	int			csum_flags;
	int			csum_lhlen;
	int			csum_iphlen;

	int			csum_thlen;
	int			csum_mss;
	int			csum_pktlen;

	uint32_t		csum_txd_upper;
	uint32_t		csum_txd_lower; /* last field */
};

/*
 * The Receive ring, one per rx queue
 */
struct rx_ring {
        struct adapter          *adapter;
        struct em_rx_queue      *que;
        u32                     me;
        u32                     payload;
        union e1000_rx_desc_extended	*rx_base;
        uint64_t                rx_paddr; 

        /* Interrupt resources */
        void                    *tag;
        struct resource         *res;
	bool			discard;

        /* Soft stats */
        unsigned long		rx_irq;
        unsigned long		rx_discarded;
        unsigned long		rx_packets;
        unsigned long		rx_bytes;
};

struct em_tx_queue {
	struct adapter         *adapter;
        u32                     msix;
	u32			eims;		/* This queue's EIMS bit */
	u32                    me;
	struct tx_ring         txr;
};

struct em_rx_queue {
	struct adapter         *adapter;
	u32                    me;
	u32                    msix;
	u32                    eims;
	struct rx_ring         rxr;
	u64                    irqs;
	struct if_irq          que_irq; 
};  

/* Our adapter structure */
struct adapter {
	struct ifnet 	*ifp;
	struct e1000_hw	hw;

        if_softc_ctx_t shared;
        if_ctx_t ctx;
#define tx_num_queues shared->isc_ntxqsets
#define rx_num_queues shared->isc_nrxqsets
#define intr_type shared->isc_intr
	/* FreeBSD operating-system-specific structures. */
	struct e1000_osdep osdep;
	device_t	dev;
	struct cdev	*led_dev;

        struct em_tx_queue *tx_queues;
        struct em_rx_queue *rx_queues; 
        struct if_irq   irq;

	struct resource *memory;
	struct resource *flash;
	struct resource	*ioport;

	struct resource	*res;
	void		*tag;
	u32		linkvec;
	u32		ivars;

	struct ifmedia	*media;
	int		msix;
	int		if_flags;
	int		em_insert_vlan_header;
	u32		ims;
	bool		in_detach;

	u32		flags;
	/* Task for FAST handling */
	struct grouptask link_task;

	u16	        num_vlans;
        u32		txd_cmd;

        u32             tx_process_limit; 
        u32             rx_process_limit;
	u32		rx_mbuf_sz;

	/* Management and WOL features */
	u32		wol;
	bool		has_manage;
	bool		has_amt;

	/* Multicast array memory */
	u8		*mta;

	/*
	** Shadow VFTA table, this is needed because
	** the real vlan filter table gets cleared during
	** a soft reset and the driver needs to be able
	** to repopulate it.
	*/
	u32		shadow_vfta[EM_VFTA_SIZE];

	/* Info about the interface */
	u16		link_active;
	u16		fc;
	u16		link_speed;
	u16		link_duplex;
	u32		smartspeed;
	u32		dmac;
	int		link_mask;

	u64		que_mask;

	struct em_int_delay_info tx_int_delay;
	struct em_int_delay_info tx_abs_int_delay;
	struct em_int_delay_info rx_int_delay;
	struct em_int_delay_info rx_abs_int_delay;
	struct em_int_delay_info tx_itr;

	/* Misc stats maintained by the driver */
	unsigned long	dropped_pkts;
	unsigned long	link_irq;
	unsigned long	rx_overruns;
	unsigned long	watchdog_events;

	struct e1000_hw_stats stats;
	u16		vf_ifp;
};

/********************************************************************************
 * vendor_info_array
 *
 * This array contains the list of Subvendor/Subdevice IDs on which the driver
 * should load.
 *
 ********************************************************************************/
typedef struct _em_vendor_info_t {
	unsigned int vendor_id;
	unsigned int device_id;
	unsigned int subvendor_id;
	unsigned int subdevice_id;
	unsigned int index;
} em_vendor_info_t;

void em_dump_rs(struct adapter *);

#define EM_RSSRK_SIZE	4
#define EM_RSSRK_VAL(key, i)		(key[(i) * EM_RSSRK_SIZE] | \
					 key[(i) * EM_RSSRK_SIZE + 1] << 8 | \
					 key[(i) * EM_RSSRK_SIZE + 2] << 16 | \
					 key[(i) * EM_RSSRK_SIZE + 3] << 24)
#endif /* _EM_H_DEFINED_ */
