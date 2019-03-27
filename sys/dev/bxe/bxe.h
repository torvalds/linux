/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2007-2014 QLogic Corporation. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __BXE_H__
#define __BXE_H__

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/kobj.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/smp.h>
#include <sys/bitstring.h>
#include <sys/limits.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/zlib.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_vlan_var.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/netdump/netdump.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/atomic.h>
#include <machine/resource.h>
#include <machine/endian.h>
#include <machine/bus.h>
#include <machine/in_cksum.h>

#include "device_if.h"
#include "bus_if.h"
#include "pci_if.h"

#if _BYTE_ORDER == _LITTLE_ENDIAN
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN
#endif
#undef BIG_ENDIAN
#undef __BIG_ENDIAN
#else /* _BIG_ENDIAN */
#ifndef BIG_ENDIAN
#define BIG_ENDIAN
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN
#endif
#undef LITTLE_ENDIAN
#undef __LITTLE_ENDIAN
#endif

#include "ecore_mfw_req.h"
#include "ecore_fw_defs.h"
#include "ecore_hsi.h"
#include "ecore_reg.h"
#include "bxe_dcb.h"
#include "bxe_stats.h"

#include "bxe_elink.h"

#define VF_MAC_CREDIT_CNT 0
#define VF_VLAN_CREDIT_CNT (0)

#if __FreeBSD_version < 800054
#if defined(__i386__) || defined(__amd64__)
#define mb()  __asm volatile("mfence;" : : : "memory")
#define wmb() __asm volatile("sfence;" : : : "memory")
#define rmb() __asm volatile("lfence;" : : : "memory")
static __inline void prefetch(void *x)
{
    __asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}
#else
#define mb()
#define rmb()
#define wmb()
#define prefetch(x)
#endif
#endif

#if __FreeBSD_version >= 1000000
#define PCIR_EXPRESS_DEVICE_STA        PCIER_DEVICE_STA
#define PCIM_EXP_STA_TRANSACTION_PND   PCIEM_STA_TRANSACTION_PND
#define PCIR_EXPRESS_LINK_STA          PCIER_LINK_STA
#define PCIM_LINK_STA_WIDTH            PCIEM_LINK_STA_WIDTH
#define PCIM_LINK_STA_SPEED            PCIEM_LINK_STA_SPEED
#define PCIR_EXPRESS_DEVICE_CTL        PCIER_DEVICE_CTL
#define PCIM_EXP_CTL_MAX_PAYLOAD       PCIEM_CTL_MAX_PAYLOAD
#define PCIM_EXP_CTL_MAX_READ_REQUEST  PCIEM_CTL_MAX_READ_REQUEST
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
#ifndef ARRSIZE
#define ARRSIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef ilog2
static inline
int bxe_ilog2(int x)
{
    int log = 0;
    while (x >>= 1) log++;
    return (log);
}
#define ilog2(x) bxe_ilog2(x)
#endif

#include "ecore_sp.h"

#define BRCM_VENDORID 0x14e4
#define	QLOGIC_VENDORID	0x1077
#define PCI_ANY_ID    (uint16_t)(~0U)

struct bxe_device_type
{
    uint16_t bxe_vid;
    uint16_t bxe_did;
    uint16_t bxe_svid;
    uint16_t bxe_sdid;
    char     *bxe_name;
};

#define BCM_PAGE_SHIFT       12
#define BCM_PAGE_SIZE        (1 << BCM_PAGE_SHIFT)
#define BCM_PAGE_MASK        (~(BCM_PAGE_SIZE - 1))
#define BCM_PAGE_ALIGN(addr) ((addr + BCM_PAGE_SIZE - 1) & BCM_PAGE_MASK)

#if BCM_PAGE_SIZE != 4096
#error Page sizes other than 4KB are unsupported!
#endif

#if (BUS_SPACE_MAXADDR > 0xFFFFFFFF)
#define U64_LO(addr) ((uint32_t)(((uint64_t)(addr)) & 0xFFFFFFFF))
#define U64_HI(addr) ((uint32_t)(((uint64_t)(addr)) >> 32))
#else
#define U64_LO(addr) ((uint32_t)(addr))
#define U64_HI(addr) (0)
#endif
#define HILO_U64(hi, lo) ((((uint64_t)(hi)) << 32) + (lo))

#define SET_FLAG(value, mask, flag)            \
    do {                                       \
        (value) &= ~(mask);                    \
        (value) |= ((flag) << (mask##_SHIFT)); \
    } while (0)

#define GET_FLAG(value, mask)              \
    (((value) & (mask)) >> (mask##_SHIFT))

#define GET_FIELD(value, fname)                     \
    (((value) & (fname##_MASK)) >> (fname##_SHIFT))

#define BXE_MAX_SEGMENTS     12 /* 13-1 for parsing buffer */
#define BXE_TSO_MAX_SEGMENTS 32
#define BXE_TSO_MAX_SIZE     (65535 + sizeof(struct ether_vlan_header))
#define BXE_TSO_MAX_SEG_SIZE 4096

/* dropless fc FW/HW related params */
#define BRB_SIZE(sc)         (CHIP_IS_E3(sc) ? 1024 : 512)
#define MAX_AGG_QS(sc)       (CHIP_IS_E1(sc) ?                       \
                                  ETH_MAX_AGGREGATION_QUEUES_E1 :    \
                                  ETH_MAX_AGGREGATION_QUEUES_E1H_E2)
#define FW_DROP_LEVEL(sc)    (3 + MAX_SPQ_PENDING + MAX_AGG_QS(sc))
#define FW_PREFETCH_CNT      16
#define DROPLESS_FC_HEADROOM 100

/******************/
/* RX SGE defines */
/******************/

#define RX_SGE_NUM_PAGES       2 /* must be a power of 2 */
#define RX_SGE_TOTAL_PER_PAGE  (BCM_PAGE_SIZE / sizeof(struct eth_rx_sge))
#define RX_SGE_NEXT_PAGE_DESC_CNT 2
#define RX_SGE_USABLE_PER_PAGE (RX_SGE_TOTAL_PER_PAGE - RX_SGE_NEXT_PAGE_DESC_CNT)
#define RX_SGE_PER_PAGE_MASK   (RX_SGE_TOTAL_PER_PAGE - 1)
#define RX_SGE_TOTAL           (RX_SGE_TOTAL_PER_PAGE * RX_SGE_NUM_PAGES)
#define RX_SGE_USABLE          (RX_SGE_USABLE_PER_PAGE * RX_SGE_NUM_PAGES)
#define RX_SGE_MAX             (RX_SGE_TOTAL - 1)
#define RX_SGE(x)              ((x) & RX_SGE_MAX)

#define RX_SGE_NEXT(x)                                              \
    ((((x) & RX_SGE_PER_PAGE_MASK) == (RX_SGE_USABLE_PER_PAGE - 1)) \
     ? (x) + 1 + RX_SGE_NEXT_PAGE_DESC_CNT : (x) + 1)

#define RX_SGE_MASK_ELEM_SZ    64
#define RX_SGE_MASK_ELEM_SHIFT 6
#define RX_SGE_MASK_ELEM_MASK  ((uint64_t)RX_SGE_MASK_ELEM_SZ - 1)

/*
 * Creates a bitmask of all ones in less significant bits.
 * idx - index of the most significant bit in the created mask.
 */
#define RX_SGE_ONES_MASK(idx)                                      \
    (((uint64_t)0x1 << (((idx) & RX_SGE_MASK_ELEM_MASK) + 1)) - 1)
#define RX_SGE_MASK_ELEM_ONE_MASK ((uint64_t)(~0))

/* Number of uint64_t elements in SGE mask array. */
#define RX_SGE_MASK_LEN                                                \
    ((RX_SGE_NUM_PAGES * RX_SGE_TOTAL_PER_PAGE) / RX_SGE_MASK_ELEM_SZ)
#define RX_SGE_MASK_LEN_MASK      (RX_SGE_MASK_LEN - 1)
#define RX_SGE_NEXT_MASK_ELEM(el) (((el) + 1) & RX_SGE_MASK_LEN_MASK)

/*
 * dropless fc calculations for SGEs
 * Number of required SGEs is the sum of two:
 * 1. Number of possible opened aggregations (next packet for
 *    these aggregations will probably consume SGE immidiatelly)
 * 2. Rest of BRB blocks divided by 2 (block will consume new SGE only
 *    after placement on BD for new TPA aggregation)
 * Takes into account RX_SGE_NEXT_PAGE_DESC_CNT "next" elements on each page
 */
#define NUM_SGE_REQ(sc)                                    \
    (MAX_AGG_QS(sc) + (BRB_SIZE(sc) - MAX_AGG_QS(sc)) / 2)
#define NUM_SGE_PG_REQ(sc)                                                    \
    ((NUM_SGE_REQ(sc) + RX_SGE_USABLE_PER_PAGE - 1) / RX_SGE_USABLE_PER_PAGE)
#define SGE_TH_LO(sc)                                                  \
    (NUM_SGE_REQ(sc) + NUM_SGE_PG_REQ(sc) * RX_SGE_NEXT_PAGE_DESC_CNT)
#define SGE_TH_HI(sc)                      \
    (SGE_TH_LO(sc) + DROPLESS_FC_HEADROOM)

#define PAGES_PER_SGE_SHIFT  0
#define PAGES_PER_SGE        (1 << PAGES_PER_SGE_SHIFT)
#define SGE_PAGE_SIZE        BCM_PAGE_SIZE
#define SGE_PAGE_SHIFT       BCM_PAGE_SHIFT
#define SGE_PAGE_ALIGN(addr) BCM_PAGE_ALIGN(addr)
#define SGE_PAGES            (SGE_PAGE_SIZE * PAGES_PER_SGE)
#define TPA_AGG_SIZE         min((8 * SGE_PAGES), 0xffff)

/*****************/
/* TX BD defines */
/*****************/

#define TX_BD_NUM_PAGES       16 /* must be a power of 2 */
#define TX_BD_TOTAL_PER_PAGE  (BCM_PAGE_SIZE / sizeof(union eth_tx_bd_types))
#define TX_BD_USABLE_PER_PAGE (TX_BD_TOTAL_PER_PAGE - 1)
#define TX_BD_TOTAL           (TX_BD_TOTAL_PER_PAGE * TX_BD_NUM_PAGES)
#define TX_BD_USABLE          (TX_BD_USABLE_PER_PAGE * TX_BD_NUM_PAGES)
#define TX_BD_MAX             (TX_BD_TOTAL - 1)

#define TX_BD_NEXT(x)                                                 \
    ((((x) & TX_BD_USABLE_PER_PAGE) == (TX_BD_USABLE_PER_PAGE - 1)) ? \
     ((x) + 2) : ((x) + 1))
#define TX_BD(x)      ((x) & TX_BD_MAX)
#define TX_BD_PAGE(x) (((x) & ~TX_BD_USABLE_PER_PAGE) >> 8)
#define TX_BD_IDX(x)  ((x) & TX_BD_USABLE_PER_PAGE)

/*
 * Trigger pending transmits when the number of available BDs is greater
 * than 1/8 of the total number of usable BDs.
 */
#define BXE_TX_CLEANUP_THRESHOLD (TX_BD_USABLE / 8)
#define BXE_TX_TIMEOUT 5

/*****************/
/* RX BD defines */
/*****************/

#define RX_BD_NUM_PAGES       8 /* power of 2 */
#define RX_BD_TOTAL_PER_PAGE  (BCM_PAGE_SIZE / sizeof(struct eth_rx_bd))
#define RX_BD_NEXT_PAGE_DESC_CNT 2
#define RX_BD_USABLE_PER_PAGE (RX_BD_TOTAL_PER_PAGE - RX_BD_NEXT_PAGE_DESC_CNT)
#define RX_BD_PER_PAGE_MASK   (RX_BD_TOTAL_PER_PAGE - 1)
#define RX_BD_TOTAL           (RX_BD_TOTAL_PER_PAGE * RX_BD_NUM_PAGES)
#define RX_BD_USABLE          (RX_BD_USABLE_PER_PAGE * RX_BD_NUM_PAGES)
#define RX_BD_MAX             (RX_BD_TOTAL - 1)

#define RX_BD_NEXT(x)                                               \
    ((((x) & RX_BD_PER_PAGE_MASK) == (RX_BD_USABLE_PER_PAGE - 1)) ? \
     ((x) + 3) : ((x) + 1))
#define RX_BD(x)      ((x) & RX_BD_MAX)
#define RX_BD_PAGE(x) (((x) & ~RX_BD_PER_PAGE_MASK) >> 9)
#define RX_BD_IDX(x)  ((x) & RX_BD_PER_PAGE_MASK)

/*
 * dropless fc calculations for BDs
 * Number of BDs should be as number of buffers in BRB:
 * Low threshold takes into account RX_BD_NEXT_PAGE_DESC_CNT
 * "next" elements on each page
 */
#define NUM_BD_REQ(sc) \
    BRB_SIZE(sc)
#define NUM_BD_PG_REQ(sc)                                                  \
    ((NUM_BD_REQ(sc) + RX_BD_USABLE_PER_PAGE - 1) / RX_BD_USABLE_PER_PAGE)
#define BD_TH_LO(sc)                                \
    (NUM_BD_REQ(sc) +                               \
     NUM_BD_PG_REQ(sc) * RX_BD_NEXT_PAGE_DESC_CNT + \
     FW_DROP_LEVEL(sc))
#define BD_TH_HI(sc)                      \
    (BD_TH_LO(sc) + DROPLESS_FC_HEADROOM)
#define MIN_RX_AVAIL(sc)                           \
    ((sc)->dropless_fc ? BD_TH_HI(sc) + 128 : 128)
#define MIN_RX_SIZE_TPA_HW(sc)                         \
    (CHIP_IS_E1(sc) ? ETH_MIN_RX_CQES_WITH_TPA_E1 :    \
                      ETH_MIN_RX_CQES_WITH_TPA_E1H_E2)
#define MIN_RX_SIZE_NONTPA_HW ETH_MIN_RX_CQES_WITHOUT_TPA
#define MIN_RX_SIZE_TPA(sc)                         \
    (max(MIN_RX_SIZE_TPA_HW(sc), MIN_RX_AVAIL(sc)))
#define MIN_RX_SIZE_NONTPA(sc)                     \
    (max(MIN_RX_SIZE_NONTPA_HW, MIN_RX_AVAIL(sc)))

/***************/
/* RCQ defines */
/***************/

/*
 * As long as CQE is X times bigger than BD entry we have to allocate X times
 * more pages for CQ ring in order to keep it balanced with BD ring
 */
#define CQE_BD_REL          (sizeof(union eth_rx_cqe) / \
                             sizeof(struct eth_rx_bd))
#define RCQ_NUM_PAGES       (RX_BD_NUM_PAGES * CQE_BD_REL) /* power of 2 */
#define RCQ_TOTAL_PER_PAGE  (BCM_PAGE_SIZE / sizeof(union eth_rx_cqe))
#define RCQ_NEXT_PAGE_DESC_CNT 1
#define RCQ_USABLE_PER_PAGE (RCQ_TOTAL_PER_PAGE - RCQ_NEXT_PAGE_DESC_CNT)
#define RCQ_TOTAL           (RCQ_TOTAL_PER_PAGE * RCQ_NUM_PAGES)
#define RCQ_USABLE          (RCQ_USABLE_PER_PAGE * RCQ_NUM_PAGES)
#define RCQ_MAX             (RCQ_TOTAL - 1)

#define RCQ_NEXT(x)                                               \
    ((((x) & RCQ_USABLE_PER_PAGE) == (RCQ_USABLE_PER_PAGE - 1)) ? \
     ((x) + 1 + RCQ_NEXT_PAGE_DESC_CNT) : ((x) + 1))
#define RCQ(x)      ((x) & RCQ_MAX)
#define RCQ_PAGE(x) (((x) & ~RCQ_USABLE_PER_PAGE) >> 7)
#define RCQ_IDX(x)  ((x) & RCQ_USABLE_PER_PAGE)

/*
 * dropless fc calculations for RCQs
 * Number of RCQs should be as number of buffers in BRB:
 * Low threshold takes into account RCQ_NEXT_PAGE_DESC_CNT
 * "next" elements on each page
 */
#define NUM_RCQ_REQ(sc) \
    BRB_SIZE(sc)
#define NUM_RCQ_PG_REQ(sc)                                              \
    ((NUM_RCQ_REQ(sc) + RCQ_USABLE_PER_PAGE - 1) / RCQ_USABLE_PER_PAGE)
#define RCQ_TH_LO(sc)                              \
    (NUM_RCQ_REQ(sc) +                             \
     NUM_RCQ_PG_REQ(sc) * RCQ_NEXT_PAGE_DESC_CNT + \
     FW_DROP_LEVEL(sc))
#define RCQ_TH_HI(sc)                      \
    (RCQ_TH_LO(sc) + DROPLESS_FC_HEADROOM)

/* This is needed for determening of last_max */
#define SUB_S16(a, b) (int16_t)((int16_t)(a) - (int16_t)(b))

#define __SGE_MASK_SET_BIT(el, bit)               \
    do {                                          \
        (el) = ((el) | ((uint64_t)0x1 << (bit))); \
    } while (0)

#define __SGE_MASK_CLEAR_BIT(el, bit)                \
    do {                                             \
        (el) = ((el) & (~((uint64_t)0x1 << (bit)))); \
    } while (0)

#define SGE_MASK_SET_BIT(fp, idx)                                       \
    __SGE_MASK_SET_BIT((fp)->sge_mask[(idx) >> RX_SGE_MASK_ELEM_SHIFT], \
                       ((idx) & RX_SGE_MASK_ELEM_MASK))

#define SGE_MASK_CLEAR_BIT(fp, idx)                                       \
    __SGE_MASK_CLEAR_BIT((fp)->sge_mask[(idx) >> RX_SGE_MASK_ELEM_SHIFT], \
                         ((idx) & RX_SGE_MASK_ELEM_MASK))

/* Load / Unload modes */
#define LOAD_NORMAL       0
#define LOAD_OPEN         1
#define LOAD_DIAG         2
#define LOAD_LOOPBACK_EXT 3
#define UNLOAD_NORMAL     0
#define UNLOAD_CLOSE      1
#define UNLOAD_RECOVERY   2

/* Some constants... */
//#define MAX_PATH_NUM       2
//#define E2_MAX_NUM_OF_VFS  64
//#define E1H_FUNC_MAX       8
//#define E2_FUNC_MAX        4   /* per path */
#define MAX_VNIC_NUM       4
#define MAX_FUNC_NUM       8   /* common to all chips */
//#define MAX_NDSB           HC_SB_MAX_SB_E2 /* max non-default status block */
#define MAX_RSS_CHAINS     16 /* a constant for HW limit */
#define MAX_MSI_VECTOR     8  /* a constant for HW limit */

#define ILT_NUM_PAGE_ENTRIES 3072
/*
 * 57710/11 we use whole table since we have 8 functions.
 * 57712 we have only 4 functions, but use same size per func, so only half
 * of the table is used.
 */
#define ILT_PER_FUNC        (ILT_NUM_PAGE_ENTRIES / 8)
#define FUNC_ILT_BASE(func) (func * ILT_PER_FUNC)
/*
 * the phys address is shifted right 12 bits and has an added
 * 1=valid bit added to the 53rd bit
 * then since this is a wide register(TM)
 * we split it into two 32 bit writes
 */
#define ONCHIP_ADDR1(x) ((uint32_t)(((uint64_t)x >> 12) & 0xFFFFFFFF))
#define ONCHIP_ADDR2(x) ((uint32_t)((1 << 20) | ((uint64_t)x >> 44)))

/* L2 header size + 2*VLANs (8 bytes) + LLC SNAP (8 bytes) */
#define ETH_HLEN                  14
#define ETH_OVERHEAD              (ETH_HLEN + 8 + 8)
#define ETH_MIN_PACKET_SIZE       60
#define ETH_MAX_PACKET_SIZE       ETHERMTU /* 1500 */
#define ETH_MAX_JUMBO_PACKET_SIZE 9600
/* TCP with Timestamp Option (32) + IPv6 (40) */
#define ETH_MAX_TPA_HEADER_SIZE   72

/* max supported alignment is 256 (8 shift) */
//#define BXE_RX_ALIGN_SHIFT ((CACHE_LINE_SHIFT < 8) ? CACHE_LINE_SHIFT : 8)
#define BXE_RX_ALIGN_SHIFT 8
/* FW uses 2 cache lines alignment for start packet and size  */
#define BXE_FW_RX_ALIGN_START (1 << BXE_RX_ALIGN_SHIFT)
#define BXE_FW_RX_ALIGN_END   (1 << BXE_RX_ALIGN_SHIFT)

#define BXE_PXP_DRAM_ALIGN (BXE_RX_ALIGN_SHIFT - 5) /* XXX ??? */
#define BXE_SET_ERROR_BIT(sc, error) \
{ \
                (sc)->error_status |= (error); \
}

struct bxe_bar {
    struct resource    *resource;
    int                rid;
    bus_space_tag_t    tag;
    bus_space_handle_t handle;
    vm_offset_t        kva;
};

struct bxe_intr {
    struct resource *resource;
    int             rid;
    void            *tag;
};

/* Used to manage DMA allocations. */
struct bxe_dma {
    struct bxe_softc  *sc;
    bus_addr_t        paddr;
    void              *vaddr;
    bus_dma_tag_t     tag;
    bus_dmamap_t      map;
    bus_dma_segment_t seg;
    bus_size_t        size;
    int               nseg;
    char              msg[32];
};

/* attn group wiring */
#define MAX_DYNAMIC_ATTN_GRPS 8

struct attn_route {
    uint32_t sig[5];
};

struct iro {
    uint32_t base;
    uint16_t m1;
    uint16_t m2;
    uint16_t m3;
    uint16_t size;
};

union bxe_host_hc_status_block {
    /* pointer to fp status block e2 */
    struct host_hc_status_block_e2  *e2_sb;
    /* pointer to fp status block e1x */
    struct host_hc_status_block_e1x *e1x_sb;
};

union bxe_db_prod {
    struct doorbell_set_prod data;
    uint32_t                 raw;
};

struct bxe_sw_tx_bd {
    struct mbuf  *m;
    bus_dmamap_t m_map;
    uint16_t     first_bd;
    uint8_t      flags;
/* set on the first BD descriptor when there is a split BD */
#define BXE_TSO_SPLIT_BD (1 << 0)
};

struct bxe_sw_rx_bd {
    struct mbuf  *m;
    bus_dmamap_t m_map;
};

struct bxe_sw_tpa_info {
    struct bxe_sw_rx_bd bd;
    bus_dma_segment_t   seg;
    uint8_t             state;
#define BXE_TPA_STATE_START 1
#define BXE_TPA_STATE_STOP  2
    uint8_t             placement_offset;
    uint16_t            parsing_flags;
    uint16_t            vlan_tag;
    uint16_t            len_on_bd;
};

/*
 * This is the HSI fastpath data structure. There can be up to MAX_RSS_CHAIN
 * instances of the fastpath structure when using multiple queues.
 */
struct bxe_fastpath {
    /* pointer back to parent structure */
    struct bxe_softc *sc;

    struct mtx tx_mtx;
    char       tx_mtx_name[32];
    struct mtx rx_mtx;
    char       rx_mtx_name[32];

#define BXE_FP_TX_LOCK(fp)        mtx_lock(&fp->tx_mtx)
#define BXE_FP_TX_UNLOCK(fp)      mtx_unlock(&fp->tx_mtx)
#define BXE_FP_TX_LOCK_ASSERT(fp) mtx_assert(&fp->tx_mtx, MA_OWNED)
#define BXE_FP_TX_TRYLOCK(fp)     mtx_trylock(&fp->tx_mtx)

#define BXE_FP_RX_LOCK(fp)        mtx_lock(&fp->rx_mtx)
#define BXE_FP_RX_UNLOCK(fp)      mtx_unlock(&fp->rx_mtx)
#define BXE_FP_RX_LOCK_ASSERT(fp) mtx_assert(&fp->rx_mtx, MA_OWNED)

    /* status block */
    struct bxe_dma                 sb_dma;
    union bxe_host_hc_status_block status_block;

    /* transmit chain (tx bds) */
    struct bxe_dma        tx_dma;
    union eth_tx_bd_types *tx_chain;

    /* receive chain (rx bds) */
    struct bxe_dma   rx_dma;
    struct eth_rx_bd *rx_chain;

    /* receive completion queue chain (rcq bds) */
    struct bxe_dma   rcq_dma;
    union eth_rx_cqe *rcq_chain;

    /* receive scatter/gather entry chain (for TPA) */
    struct bxe_dma    rx_sge_dma;
    struct eth_rx_sge *rx_sge_chain;

    /* tx mbufs */
    bus_dma_tag_t       tx_mbuf_tag;
    struct bxe_sw_tx_bd tx_mbuf_chain[TX_BD_TOTAL];

    /* rx mbufs */
    bus_dma_tag_t       rx_mbuf_tag;
    struct bxe_sw_rx_bd rx_mbuf_chain[RX_BD_TOTAL];
    bus_dmamap_t        rx_mbuf_spare_map;

    /* rx sge mbufs */
    bus_dma_tag_t       rx_sge_mbuf_tag;
    struct bxe_sw_rx_bd rx_sge_mbuf_chain[RX_SGE_TOTAL];
    bus_dmamap_t        rx_sge_mbuf_spare_map;

    /* rx tpa mbufs (use the larger size for TPA queue length) */
    int                    tpa_enable; /* disabled per fastpath upon error */
    struct bxe_sw_tpa_info rx_tpa_info[ETH_MAX_AGGREGATION_QUEUES_E1H_E2];
    bus_dmamap_t           rx_tpa_info_mbuf_spare_map;
    uint64_t               rx_tpa_queue_used;

    uint16_t *sb_index_values;
    uint16_t *sb_running_index;
    uint32_t ustorm_rx_prods_offset;

    uint8_t igu_sb_id; /* status block number in HW */
    uint8_t fw_sb_id;  /* status block number in FW */

    uint32_t rx_buf_size;
    int mbuf_alloc_size;

    int state;
#define BXE_FP_STATE_CLOSED  0x01
#define BXE_FP_STATE_IRQ     0x02
#define BXE_FP_STATE_OPENING 0x04
#define BXE_FP_STATE_OPEN    0x08
#define BXE_FP_STATE_HALTING 0x10
#define BXE_FP_STATE_HALTED  0x20

    /* reference back to this fastpath queue number */
    uint8_t index; /* this is also the 'cid' */
#define FP_IDX(fp) (fp->index)

    /* interrupt taskqueue (fast) */
    struct task      tq_task;
    struct taskqueue *tq;
    char             tq_name[32];

    struct task tx_task;
    struct timeout_task tx_timeout_task;

    /* ethernet client ID (each fastpath set of RX/TX/CQE is a client) */
    uint8_t cl_id;
#define FP_CL_ID(fp) (fp->cl_id)
    uint8_t cl_qzone_id;

    uint16_t fp_hc_idx;

    /* driver copy of the receive buffer descriptor prod/cons indices */
    uint16_t rx_bd_prod;
    uint16_t rx_bd_cons;

    /* driver copy of the receive completion queue prod/cons indices */
    uint16_t rx_cq_prod;
    uint16_t rx_cq_cons;

    union bxe_db_prod tx_db;

    /* Transmit packet producer index (used in eth_tx_bd). */
    uint16_t tx_pkt_prod;
    uint16_t tx_pkt_cons;

    /* Transmit buffer descriptor producer index. */
    uint16_t tx_bd_prod;
    uint16_t tx_bd_cons;

    uint64_t sge_mask[RX_SGE_MASK_LEN];
    uint16_t rx_sge_prod;

    struct tstorm_per_queue_stats old_tclient;
    struct ustorm_per_queue_stats old_uclient;
    struct xstorm_per_queue_stats old_xclient;
    struct bxe_eth_q_stats        eth_q_stats;
    struct bxe_eth_q_stats_old    eth_q_stats_old;

    /* Pointer to the receive consumer in the status block */
    uint16_t *rx_cq_cons_sb;

    /* Pointer to the transmit consumer in the status block */
    uint16_t *tx_cons_sb;

    /* transmit timeout until chip reset */
    int watchdog_timer;

    /* Free/used buffer descriptor counters. */
    //uint16_t used_tx_bd;

    /* Last maximal completed SGE */
    uint16_t last_max_sge;

    //uint16_t rx_sge_free_idx;

    //uint8_t segs;

#if __FreeBSD_version >= 800000
#define BXE_BR_SIZE 4096
    struct buf_ring *tx_br;
#endif
}; /* struct bxe_fastpath */

/* sriov XXX */
#define BXE_MAX_NUM_OF_VFS 64
#define BXE_VF_CID_WND     0
#define BXE_CIDS_PER_VF    (1 << BXE_VF_CID_WND)
#define BXE_CLIENTS_PER_VF 1
#define BXE_FIRST_VF_CID   256
#define BXE_VF_CIDS        (BXE_MAX_NUM_OF_VFS * BXE_CIDS_PER_VF)
#define BXE_VF_ID_INVALID  0xFF
#define IS_SRIOV(sc) 0

#define GET_NUM_VFS_PER_PATH(sc) 0
#define GET_NUM_VFS_PER_PF(sc)   0

/* maximum number of fast-path interrupt contexts */
#define FP_SB_MAX_E1x 16
#define FP_SB_MAX_E2  HC_SB_MAX_SB_E2

union cdu_context {
    struct eth_context eth;
    char pad[1024];
};

/* CDU host DB constants */
#define CDU_ILT_PAGE_SZ_HW 2
#define CDU_ILT_PAGE_SZ    (8192 << CDU_ILT_PAGE_SZ_HW) /* 32K */
#define ILT_PAGE_CIDS      (CDU_ILT_PAGE_SZ / sizeof(union cdu_context))

#define CNIC_ISCSI_CID_MAX 256
#define CNIC_FCOE_CID_MAX  2048
#define CNIC_CID_MAX       (CNIC_ISCSI_CID_MAX + CNIC_FCOE_CID_MAX)
#define CNIC_ILT_LINES     DIV_ROUND_UP(CNIC_CID_MAX, ILT_PAGE_CIDS)

#define QM_ILT_PAGE_SZ_HW  0
#define QM_ILT_PAGE_SZ     (4096 << QM_ILT_PAGE_SZ_HW) /* 4K */
#define QM_CID_ROUND       1024

/* TM (timers) host DB constants */
#define TM_ILT_PAGE_SZ_HW  0
#define TM_ILT_PAGE_SZ     (4096 << TM_ILT_PAGE_SZ_HW) /* 4K */
/*#define TM_CONN_NUM        (CNIC_STARTING_CID+CNIC_ISCSI_CXT_MAX) */
#define TM_CONN_NUM        1024
#define TM_ILT_SZ          (8 * TM_CONN_NUM)
#define TM_ILT_LINES       DIV_ROUND_UP(TM_ILT_SZ, TM_ILT_PAGE_SZ)

/* SRC (Searcher) host DB constants */
#define SRC_ILT_PAGE_SZ_HW 0
#define SRC_ILT_PAGE_SZ    (4096 << SRC_ILT_PAGE_SZ_HW) /* 4K */
#define SRC_HASH_BITS      10
#define SRC_CONN_NUM       (1 << SRC_HASH_BITS) /* 1024 */
#define SRC_ILT_SZ         (sizeof(struct src_ent) * SRC_CONN_NUM)
#define SRC_T2_SZ          SRC_ILT_SZ
#define SRC_ILT_LINES      DIV_ROUND_UP(SRC_ILT_SZ, SRC_ILT_PAGE_SZ)

struct hw_context {
    struct bxe_dma    vcxt_dma;
    union cdu_context *vcxt;
    //bus_addr_t        cxt_mapping;
    size_t            size;
};

#define SM_RX_ID 0
#define SM_TX_ID 1

/* defines for multiple tx priority indices */
#define FIRST_TX_ONLY_COS_INDEX 1
#define FIRST_TX_COS_INDEX      0

#define CID_TO_FP(cid, sc) ((cid) % BXE_NUM_NON_CNIC_QUEUES(sc))

#define HC_INDEX_ETH_RX_CQ_CONS       1
#define HC_INDEX_OOO_TX_CQ_CONS       4
#define HC_INDEX_ETH_TX_CQ_CONS_COS0  5
#define HC_INDEX_ETH_TX_CQ_CONS_COS1  6
#define HC_INDEX_ETH_TX_CQ_CONS_COS2  7
#define HC_INDEX_ETH_FIRST_TX_CQ_CONS HC_INDEX_ETH_TX_CQ_CONS_COS0

/* congestion management fairness mode */
#define CMNG_FNS_NONE   0
#define CMNG_FNS_MINMAX 1

/* CMNG constants, as derived from system spec calculations */
/* default MIN rate in case VNIC min rate is configured to zero - 100Mbps */
#define DEF_MIN_RATE 100
/* resolution of the rate shaping timer - 400 usec */
#define RS_PERIODIC_TIMEOUT_USEC 400
/* number of bytes in single QM arbitration cycle -
 * coefficient for calculating the fairness timer */
#define QM_ARB_BYTES 160000
/* resolution of Min algorithm 1:100 */
#define MIN_RES 100
/* how many bytes above threshold for the minimal credit of Min algorithm*/
#define MIN_ABOVE_THRESH 32768
/* fairness algorithm integration time coefficient -
 * for calculating the actual Tfair */
#define T_FAIR_COEF ((MIN_ABOVE_THRESH + QM_ARB_BYTES) * 8 * MIN_RES)
/* memory of fairness algorithm - 2 cycles */
#define FAIR_MEM 2

#define HC_SEG_ACCESS_DEF   0 /* Driver decision 0-3 */
#define HC_SEG_ACCESS_ATTN  4
#define HC_SEG_ACCESS_NORM  0 /* Driver decision 0-1 */

/*
 * The total number of L2 queues, MSIX vectors and HW contexts (CIDs) is
 * control by the number of fast-path status blocks supported by the
 * device (HW/FW). Each fast-path status block (FP-SB) aka non-default
 * status block represents an independent interrupts context that can
 * serve a regular L2 networking queue. However special L2 queues such
 * as the FCoE queue do not require a FP-SB and other components like
 * the CNIC may consume FP-SB reducing the number of possible L2 queues
 *
 * If the maximum number of FP-SB available is X then:
 * a. If CNIC is supported it consumes 1 FP-SB thus the max number of
 *    regular L2 queues is Y=X-1
 * b. in MF mode the actual number of L2 queues is Y= (X-1/MF_factor)
 * c. If the FCoE L2 queue is supported the actual number of L2 queues
 *    is Y+1
 * d. The number of irqs (MSIX vectors) is either Y+1 (one extra for
 *    slow-path interrupts) or Y+2 if CNIC is supported (one additional
 *    FP interrupt context for the CNIC).
 * e. The number of HW context (CID count) is always X or X+1 if FCoE
 *    L2 queue is supported. the cid for the FCoE L2 queue is always X.
 *
 * So this is quite simple for now as no ULPs are supported yet. :-)
 */
#define BXE_NUM_QUEUES(sc)          ((sc)->num_queues)
#define BXE_NUM_ETH_QUEUES(sc)      BXE_NUM_QUEUES(sc)
#define BXE_NUM_NON_CNIC_QUEUES(sc) BXE_NUM_QUEUES(sc)
#define BXE_NUM_RX_QUEUES(sc)       BXE_NUM_QUEUES(sc)

#define FOR_EACH_QUEUE(sc, var)                          \
    for ((var) = 0; (var) < BXE_NUM_QUEUES(sc); (var)++)

#define FOR_EACH_NONDEFAULT_QUEUE(sc, var)               \
    for ((var) = 1; (var) < BXE_NUM_QUEUES(sc); (var)++)

#define FOR_EACH_ETH_QUEUE(sc, var)                          \
    for ((var) = 0; (var) < BXE_NUM_ETH_QUEUES(sc); (var)++)

#define FOR_EACH_NONDEFAULT_ETH_QUEUE(sc, var)               \
    for ((var) = 1; (var) < BXE_NUM_ETH_QUEUES(sc); (var)++)

#define FOR_EACH_COS_IN_TX_QUEUE(sc, var)           \
    for ((var) = 0; (var) < (sc)->max_cos; (var)++)

#define FOR_EACH_CNIC_QUEUE(sc, var)     \
    for ((var) = BXE_NUM_ETH_QUEUES(sc); \
         (var) < BXE_NUM_QUEUES(sc);     \
         (var)++)

enum {
    OOO_IDX_OFFSET,
    FCOE_IDX_OFFSET,
    FWD_IDX_OFFSET,
};

#define FCOE_IDX(sc)              (BXE_NUM_NON_CNIC_QUEUES(sc) + FCOE_IDX_OFFSET)
#define bxe_fcoe_fp(sc)           (&sc->fp[FCOE_IDX(sc)])
#define bxe_fcoe(sc, var)         (bxe_fcoe_fp(sc)->var)
#define bxe_fcoe_inner_sp_obj(sc) (&sc->sp_objs[FCOE_IDX(sc)])
#define bxe_fcoe_sp_obj(sc, var)  (bxe_fcoe_inner_sp_obj(sc)->var)
#define bxe_fcoe_tx(sc, var)      (bxe_fcoe_fp(sc)->txdata_ptr[FIRST_TX_COS_INDEX]->var)

#define OOO_IDX(sc)               (BXE_NUM_NON_CNIC_QUEUES(sc) + OOO_IDX_OFFSET)
#define bxe_ooo_fp(sc)            (&sc->fp[OOO_IDX(sc)])
#define bxe_ooo(sc, var)          (bxe_ooo_fp(sc)->var)
#define bxe_ooo_inner_sp_obj(sc)  (&sc->sp_objs[OOO_IDX(sc)])
#define bxe_ooo_sp_obj(sc, var)   (bxe_ooo_inner_sp_obj(sc)->var)

#define FWD_IDX(sc)               (BXE_NUM_NON_CNIC_QUEUES(sc) + FWD_IDX_OFFSET)
#define bxe_fwd_fp(sc)            (&sc->fp[FWD_IDX(sc)])
#define bxe_fwd(sc, var)          (bxe_fwd_fp(sc)->var)
#define bxe_fwd_inner_sp_obj(sc)  (&sc->sp_objs[FWD_IDX(sc)])
#define bxe_fwd_sp_obj(sc, var)   (bxe_fwd_inner_sp_obj(sc)->var)
#define bxe_fwd_txdata(fp)        (fp->txdata_ptr[FIRST_TX_COS_INDEX])

#define IS_ETH_FP(fp)    ((fp)->index < BXE_NUM_ETH_QUEUES((fp)->sc))
#define IS_FCOE_FP(fp)   ((fp)->index == FCOE_IDX((fp)->sc))
#define IS_FCOE_IDX(idx) ((idx) == FCOE_IDX(sc))
#define IS_FWD_FP(fp)    ((fp)->index == FWD_IDX((fp)->sc))
#define IS_FWD_IDX(idx)  ((idx) == FWD_IDX(sc))
#define IS_OOO_FP(fp)    ((fp)->index == OOO_IDX((fp)->sc))
#define IS_OOO_IDX(idx)  ((idx) == OOO_IDX(sc))

enum {
    BXE_PORT_QUERY_IDX,
    BXE_PF_QUERY_IDX,
    BXE_FCOE_QUERY_IDX,
    BXE_FIRST_QUEUE_QUERY_IDX,
};

struct bxe_fw_stats_req {
    struct stats_query_header hdr;
    struct stats_query_entry  query[FP_SB_MAX_E1x +
                                    BXE_FIRST_QUEUE_QUERY_IDX];
};

struct bxe_fw_stats_data {
    struct stats_counter          storm_counters;
    struct per_port_stats         port;
    struct per_pf_stats           pf;
    //struct fcoe_statistics_params fcoe;
    struct per_queue_stats        queue_stats[1];
};

/* IGU MSIX STATISTICS on 57712: 64 for VFs; 4 for PFs; 4 for Attentions */
#define BXE_IGU_STAS_MSG_VF_CNT 64
#define BXE_IGU_STAS_MSG_PF_CNT 4

#define MAX_DMAE_C 8

/*
 * For the main interface up/down code paths, a not-so-fine-grained CORE
 * mutex lock is used. Inside this code are various calls to kernel routines
 * that can cause a sleep to occur. Namely memory allocations and taskqueue
 * handling. If using an MTX lock we are *not* allowed to sleep but we can
 * with an SX lock. This define forces the CORE lock to use and SX lock.
 * Undefine this and an MTX lock will be used instead. Note that the IOCTL
 * path can cause problems since it's called by a non-sleepable thread. To
 * alleviate a potential sleep, any IOCTL processing that results in the
 * chip/interface being started/stopped/reinitialized, the actual work is
 * offloaded to a taskqueue.
 */
#define BXE_CORE_LOCK_SX

/*
 * This is the slowpath data structure. It is mapped into non-paged memory
 * so that the hardware can access it's contents directly and must be page
 * aligned.
 */
struct bxe_slowpath {

    /* used by the DMAE command executer */
    struct dmae_cmd dmae[MAX_DMAE_C];

    /* statistics completion */
    uint32_t stats_comp;

    /* firmware defined statistics blocks */
    union mac_stats        mac_stats;
    struct nig_stats       nig_stats;
    struct host_port_stats port_stats;
    struct host_func_stats func_stats;
    //struct host_func_stats func_stats_base;

    /* DMAE completion value and data source/sink */
    uint32_t wb_comp;
    uint32_t wb_data[4];

    union {
        struct mac_configuration_cmd          e1x;
        struct eth_classify_rules_ramrod_data e2;
    } mac_rdata;

    union {
        struct tstorm_eth_mac_filter_config e1x;
        struct eth_filter_rules_ramrod_data e2;
    } rx_mode_rdata;

    struct eth_rss_update_ramrod_data rss_rdata;

    union {
        struct mac_configuration_cmd           e1;
        struct eth_multicast_rules_ramrod_data e2;
    } mcast_rdata;

    union {
        struct function_start_data        func_start;
        struct flow_control_configuration pfc_config; /* for DCBX ramrod */
    } func_rdata;

    /* Queue State related ramrods */
    union {
        struct client_init_ramrod_data   init_data;
        struct client_update_ramrod_data update_data;
    } q_rdata;

    /*
     * AFEX ramrod can not be a part of func_rdata union because these
     * events might arrive in parallel to other events from func_rdata.
     * If they were defined in the same union the data can get corrupted.
     */
    struct afex_vif_list_ramrod_data func_afex_rdata;

    union drv_info_to_mcp drv_info_to_mcp;
}; /* struct bxe_slowpath */

/*
 * Port specifc data structure.
 */
struct bxe_port {
    /*
     * Port Management Function (for 57711E only).
     * When this field is set the driver instance is
     * responsible for managing port specifc
     * configurations such as handling link attentions.
     */
    uint32_t pmf;

    /* Ethernet maximum transmission unit. */
    uint16_t ether_mtu;

    uint32_t link_config[ELINK_LINK_CONFIG_SIZE];

    uint32_t ext_phy_config;

    /* Port feature config.*/
    uint32_t config;

    /* Defines the features supported by the PHY. */
    uint32_t supported[ELINK_LINK_CONFIG_SIZE];

    /* Defines the features advertised by the PHY. */
    uint32_t advertising[ELINK_LINK_CONFIG_SIZE];
#define ADVERTISED_10baseT_Half    (1 << 1)
#define ADVERTISED_10baseT_Full    (1 << 2)
#define ADVERTISED_100baseT_Half   (1 << 3)
#define ADVERTISED_100baseT_Full   (1 << 4)
#define ADVERTISED_1000baseT_Half  (1 << 5)
#define ADVERTISED_1000baseT_Full  (1 << 6)
#define ADVERTISED_TP              (1 << 7)
#define ADVERTISED_FIBRE           (1 << 8)
#define ADVERTISED_Autoneg         (1 << 9)
#define ADVERTISED_Asym_Pause      (1 << 10)
#define ADVERTISED_Pause           (1 << 11)
#define ADVERTISED_2500baseX_Full  (1 << 15)
#define ADVERTISED_10000baseT_Full (1 << 16)

    uint32_t    phy_addr;

    /* Used to synchronize phy accesses. */
    struct mtx  phy_mtx;
    char        phy_mtx_name[32];

#define BXE_PHY_LOCK(sc)          mtx_lock(&sc->port.phy_mtx)
#define BXE_PHY_UNLOCK(sc)        mtx_unlock(&sc->port.phy_mtx)
#define BXE_PHY_LOCK_ASSERT(sc)   mtx_assert(&sc->port.phy_mtx, MA_OWNED)

    /*
     * MCP scratchpad address for port specific statistics.
     * The device is responsible for writing statistcss
     * back to the MCP for use with management firmware such
     * as UMP/NC-SI.
     */
    uint32_t port_stx;

    struct nig_stats old_nig_stats;
}; /* struct bxe_port */

struct bxe_mf_info {
    uint32_t mf_config[E1HVN_MAX];

    uint32_t vnics_per_port;   /* 1, 2 or 4 */
    uint32_t multi_vnics_mode; /* can be set even if vnics_per_port = 1 */
    uint32_t path_has_ovlan;   /* MF mode in the path (can be different than the MF mode of the function */

#define IS_MULTI_VNIC(sc)  ((sc)->devinfo.mf_info.multi_vnics_mode)
#define VNICS_PER_PORT(sc) ((sc)->devinfo.mf_info.vnics_per_port)
#define VNICS_PER_PATH(sc)                                  \
    ((sc)->devinfo.mf_info.vnics_per_port *                 \
     ((CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) ? 2 : 1 ))

    uint8_t min_bw[MAX_VNIC_NUM];
    uint8_t max_bw[MAX_VNIC_NUM];

    uint16_t ext_id; /* vnic outer vlan or VIF ID */
#define VALID_OVLAN(ovlan) ((ovlan) <= 4096)
#define INVALID_VIF_ID 0xFFFF
#define OVLAN(sc) ((sc)->devinfo.mf_info.ext_id)
#define VIF_ID(sc) ((sc)->devinfo.mf_info.ext_id)

    uint16_t default_vlan;
#define NIV_DEFAULT_VLAN(sc) ((sc)->devinfo.mf_info.default_vlan)

    uint8_t niv_allowed_priorities;
#define NIV_ALLOWED_PRIORITIES(sc) ((sc)->devinfo.mf_info.niv_allowed_priorities)

    uint8_t niv_default_cos;
#define NIV_DEFAULT_COS(sc) ((sc)->devinfo.mf_info.niv_default_cos)

    uint8_t niv_mba_enabled;

    enum mf_cfg_afex_vlan_mode afex_vlan_mode;
#define AFEX_VLAN_MODE(sc) ((sc)->devinfo.mf_info.afex_vlan_mode)
    int                        afex_def_vlan_tag;
    uint32_t                   pending_max;

    uint16_t flags;
#define MF_INFO_VALID_MAC       0x0001

    uint8_t mf_mode; /* Switch-Dependent or Switch-Independent */
#define IS_MF(sc)                        \
    (IS_MULTI_VNIC(sc) &&                \
     ((sc)->devinfo.mf_info.mf_mode != 0))
#define IS_MF_SD(sc)                                     \
    (IS_MULTI_VNIC(sc) &&                                \
     ((sc)->devinfo.mf_info.mf_mode == MULTI_FUNCTION_SD))
#define IS_MF_SI(sc)                                     \
    (IS_MULTI_VNIC(sc) &&                                \
     ((sc)->devinfo.mf_info.mf_mode == MULTI_FUNCTION_SI))
#define IS_MF_AFEX(sc)                              \
    (IS_MULTI_VNIC(sc) &&                           \
     ((sc)->devinfo.mf_info.mf_mode == MULTI_FUNCTION_AFEX))
#define IS_MF_SD_MODE(sc)   IS_MF_SD(sc)
#define IS_MF_SI_MODE(sc)   IS_MF_SI(sc)
#define IS_MF_AFEX_MODE(sc) IS_MF_AFEX(sc)

    uint32_t mf_protos_supported;
    #define MF_PROTO_SUPPORT_ETHERNET 0x1
    #define MF_PROTO_SUPPORT_ISCSI    0x2
    #define MF_PROTO_SUPPORT_FCOE     0x4
}; /* struct bxe_mf_info */

/* Device information data structure. */
struct bxe_devinfo {
    /* PCIe info */
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subvendor_id;
    uint16_t subdevice_id;

    /*
     * chip_id = 0b'CCCCCCCCCCCCCCCCRRRRMMMMMMMMBBBB'
     *   C = Chip Number   (bits 16-31)
     *   R = Chip Revision (bits 12-15)
     *   M = Chip Metal    (bits 4-11)
     *   B = Chip Bond ID  (bits 0-3)
     */
    uint32_t chip_id;
#define CHIP_ID(sc)           ((sc)->devinfo.chip_id & 0xffff0000)
#define CHIP_NUM(sc)          ((sc)->devinfo.chip_id >> 16)
/* device ids */
#define CHIP_NUM_57710        0x164e
#define CHIP_NUM_57711        0x164f
#define CHIP_NUM_57711E       0x1650
#define CHIP_NUM_57712        0x1662
#define CHIP_NUM_57712_MF     0x1663
#define CHIP_NUM_57712_VF     0x166f
#define CHIP_NUM_57800        0x168a
#define CHIP_NUM_57800_MF     0x16a5
#define CHIP_NUM_57800_VF     0x16a9
#define CHIP_NUM_57810        0x168e
#define CHIP_NUM_57810_MF     0x16ae
#define CHIP_NUM_57810_VF     0x16af
#define CHIP_NUM_57811        0x163d
#define CHIP_NUM_57811_MF     0x163e
#define CHIP_NUM_57811_VF     0x163f
#define CHIP_NUM_57840_OBS    0x168d
#define CHIP_NUM_57840_OBS_MF 0x16ab
#define CHIP_NUM_57840_4_10   0x16a1
#define CHIP_NUM_57840_2_20   0x16a2
#define CHIP_NUM_57840_MF     0x16a4
#define CHIP_NUM_57840_VF     0x16ad

#define CHIP_REV_SHIFT      12
#define CHIP_REV_MASK       (0xF << CHIP_REV_SHIFT)
#define CHIP_REV(sc)        ((sc)->devinfo.chip_id & CHIP_REV_MASK)

#define CHIP_REV_Ax         (0x0 << CHIP_REV_SHIFT)
#define CHIP_REV_Bx         (0x1 << CHIP_REV_SHIFT)
#define CHIP_REV_Cx         (0x2 << CHIP_REV_SHIFT)

#define CHIP_REV_IS_SLOW(sc)    \
    (CHIP_REV(sc) > 0x00005000)
#define CHIP_REV_IS_FPGA(sc)                              \
    (CHIP_REV_IS_SLOW(sc) && (CHIP_REV(sc) & 0x00001000))
#define CHIP_REV_IS_EMUL(sc)                               \
    (CHIP_REV_IS_SLOW(sc) && !(CHIP_REV(sc) & 0x00001000))
#define CHIP_REV_IS_ASIC(sc) \
    (!CHIP_REV_IS_SLOW(sc))

#define CHIP_METAL(sc)      ((sc->devinfo.chip_id) & 0x00000ff0)
#define CHIP_BOND_ID(sc)    ((sc->devinfo.chip_id) & 0x0000000f)

#define CHIP_IS_E1(sc)      (CHIP_NUM(sc) == CHIP_NUM_57710)
#define CHIP_IS_57710(sc)   (CHIP_NUM(sc) == CHIP_NUM_57710)
#define CHIP_IS_57711(sc)   (CHIP_NUM(sc) == CHIP_NUM_57711)
#define CHIP_IS_57711E(sc)  (CHIP_NUM(sc) == CHIP_NUM_57711E)
#define CHIP_IS_E1H(sc)     ((CHIP_IS_57711(sc)) || \
                             (CHIP_IS_57711E(sc)))
#define CHIP_IS_E1x(sc)     (CHIP_IS_E1((sc)) || \
                             CHIP_IS_E1H((sc)))

#define CHIP_IS_57712(sc)    (CHIP_NUM(sc) == CHIP_NUM_57712)
#define CHIP_IS_57712_MF(sc) (CHIP_NUM(sc) == CHIP_NUM_57712_MF)
#define CHIP_IS_57712_VF(sc) (CHIP_NUM(sc) == CHIP_NUM_57712_VF)
#define CHIP_IS_E2(sc)       (CHIP_IS_57712(sc) ||  \
                              CHIP_IS_57712_MF(sc))

#define CHIP_IS_57800(sc)    (CHIP_NUM(sc) == CHIP_NUM_57800)
#define CHIP_IS_57800_MF(sc) (CHIP_NUM(sc) == CHIP_NUM_57800_MF)
#define CHIP_IS_57800_VF(sc) (CHIP_NUM(sc) == CHIP_NUM_57800_VF)
#define CHIP_IS_57810(sc)    (CHIP_NUM(sc) == CHIP_NUM_57810)
#define CHIP_IS_57810_MF(sc) (CHIP_NUM(sc) == CHIP_NUM_57810_MF)
#define CHIP_IS_57810_VF(sc) (CHIP_NUM(sc) == CHIP_NUM_57810_VF)
#define CHIP_IS_57811(sc)    (CHIP_NUM(sc) == CHIP_NUM_57811)
#define CHIP_IS_57811_MF(sc) (CHIP_NUM(sc) == CHIP_NUM_57811_MF)
#define CHIP_IS_57811_VF(sc) (CHIP_NUM(sc) == CHIP_NUM_57811_VF)
#define CHIP_IS_57840(sc)    ((CHIP_NUM(sc) == CHIP_NUM_57840_OBS)  || \
                              (CHIP_NUM(sc) == CHIP_NUM_57840_4_10) || \
                              (CHIP_NUM(sc) == CHIP_NUM_57840_2_20))
#define CHIP_IS_57840_MF(sc) ((CHIP_NUM(sc) == CHIP_NUM_57840_OBS_MF) || \
                              (CHIP_NUM(sc) == CHIP_NUM_57840_MF))
#define CHIP_IS_57840_VF(sc) (CHIP_NUM(sc) == CHIP_NUM_57840_VF)

#define CHIP_IS_E3(sc)      (CHIP_IS_57800(sc)    || \
                             CHIP_IS_57800_MF(sc) || \
                             CHIP_IS_57800_VF(sc) || \
                             CHIP_IS_57810(sc)    || \
                             CHIP_IS_57810_MF(sc) || \
                             CHIP_IS_57810_VF(sc) || \
                             CHIP_IS_57811(sc)    || \
                             CHIP_IS_57811_MF(sc) || \
                             CHIP_IS_57811_VF(sc) || \
                             CHIP_IS_57840(sc)    || \
                             CHIP_IS_57840_MF(sc) || \
                             CHIP_IS_57840_VF(sc))
#define CHIP_IS_E3A0(sc)    (CHIP_IS_E3(sc) &&              \
                             (CHIP_REV(sc) == CHIP_REV_Ax))
#define CHIP_IS_E3B0(sc)    (CHIP_IS_E3(sc) &&              \
                             (CHIP_REV(sc) == CHIP_REV_Bx))

#define USES_WARPCORE(sc)   (CHIP_IS_E3(sc))
#define CHIP_IS_E2E3(sc)    (CHIP_IS_E2(sc) || \
                             CHIP_IS_E3(sc))

#define CHIP_IS_MF_CAP(sc)  (CHIP_IS_57711E(sc)  ||  \
                             CHIP_IS_57712_MF(sc) || \
                             CHIP_IS_E3(sc))

#define IS_VF(sc)           (CHIP_IS_57712_VF(sc) || \
                             CHIP_IS_57800_VF(sc) || \
                             CHIP_IS_57810_VF(sc) || \
                             CHIP_IS_57840_VF(sc))
#define IS_PF(sc)           (!IS_VF(sc))

/*
 * This define is used in two main places:
 * 1. In the early stages of nic_load, to know if to configure Parser/Searcher
 * to nic-only mode or to offload mode. Offload mode is configured if either
 * the chip is E1x (where NIC_MODE register is not applicable), or if cnic
 * already registered for this port (which means that the user wants storage
 * services).
 * 2. During cnic-related load, to know if offload mode is already configured
 * in the HW or needs to be configrued. Since the transition from nic-mode to
 * offload-mode in HW causes traffic coruption, nic-mode is configured only
 * in ports on which storage services where never requested.
 */
#define CONFIGURE_NIC_MODE(sc) (!CHIP_IS_E1x(sc) && !CNIC_ENABLED(sc))

    uint8_t  chip_port_mode;
#define CHIP_4_PORT_MODE        0x0
#define CHIP_2_PORT_MODE        0x1
#define CHIP_PORT_MODE_NONE     0x2
#define CHIP_PORT_MODE(sc)      ((sc)->devinfo.chip_port_mode)
#define CHIP_IS_MODE_4_PORT(sc) (CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE)

    uint8_t int_block;
#define INT_BLOCK_HC            0
#define INT_BLOCK_IGU           1
#define INT_BLOCK_MODE_NORMAL   0
#define INT_BLOCK_MODE_BW_COMP  2
#define CHIP_INT_MODE_IS_NBC(sc)                          \
    (!CHIP_IS_E1x(sc) &&                                  \
     !((sc)->devinfo.int_block & INT_BLOCK_MODE_BW_COMP))
#define CHIP_INT_MODE_IS_BC(sc) (!CHIP_INT_MODE_IS_NBC(sc))

    uint32_t shmem_base;
    uint32_t shmem2_base;
    uint32_t bc_ver;
    char bc_ver_str[32];
    uint32_t mf_cfg_base; /* bootcode shmem address in BAR memory */
    struct bxe_mf_info mf_info;

    int flash_size;
#define NVRAM_1MB_SIZE      0x20000
#define NVRAM_TIMEOUT_COUNT 30000
#define NVRAM_PAGE_SIZE     256

    /* PCIe capability information */
    uint32_t pcie_cap_flags;
#define BXE_PM_CAPABLE_FLAG     0x00000001
#define BXE_PCIE_CAPABLE_FLAG   0x00000002
#define BXE_MSI_CAPABLE_FLAG    0x00000004
#define BXE_MSIX_CAPABLE_FLAG   0x00000008
    uint16_t pcie_pm_cap_reg;
    uint16_t pcie_pcie_cap_reg;
    //uint16_t pcie_devctl;
    uint16_t pcie_link_width;
    uint16_t pcie_link_speed;
    uint16_t pcie_msi_cap_reg;
    uint16_t pcie_msix_cap_reg;

    /* device configuration read from bootcode shared memory */
    uint32_t hw_config;
    uint32_t hw_config2;
}; /* struct bxe_devinfo */

struct bxe_sp_objs {
    struct ecore_vlan_mac_obj mac_obj; /* MACs object */
    struct ecore_queue_sp_obj q_obj; /* Queue State object */
}; /* struct bxe_sp_objs */

/*
 * Data that will be used to create a link report message. We will keep the
 * data used for the last link report in order to prevent reporting the same
 * link parameters twice.
 */
struct bxe_link_report_data {
    uint16_t      line_speed;        /* Effective line speed */
    unsigned long link_report_flags; /* BXE_LINK_REPORT_XXX flags */
};
enum {
    BXE_LINK_REPORT_FULL_DUPLEX,
    BXE_LINK_REPORT_LINK_DOWN,
    BXE_LINK_REPORT_RX_FC_ON,
    BXE_LINK_REPORT_TX_FC_ON
};

/* Top level device private data structure. */
struct bxe_softc {
    /*
     * First entry must be a pointer to the BSD ifnet struct which
     * has a first element of 'void *if_softc' (which is us). XXX
     */
    if_t 	    ifp;
    struct ifmedia  ifmedia; /* network interface media structure */
    int             media;

    volatile int    state; /* device state */
#define BXE_STATE_CLOSED                 0x0000
#define BXE_STATE_OPENING_WAITING_LOAD   0x1000
#define BXE_STATE_OPENING_WAITING_PORT   0x2000
#define BXE_STATE_OPEN                   0x3000
#define BXE_STATE_CLOSING_WAITING_HALT   0x4000
#define BXE_STATE_CLOSING_WAITING_DELETE 0x5000
#define BXE_STATE_CLOSING_WAITING_UNLOAD 0x6000
#define BXE_STATE_DISABLED               0xD000
#define BXE_STATE_DIAG                   0xE000
#define BXE_STATE_ERROR                  0xF000

    int flags;
#define BXE_ONE_PORT_FLAG    0x00000001
#define BXE_NO_ISCSI         0x00000002
#define BXE_NO_FCOE          0x00000004
#define BXE_ONE_PORT(sc)     (sc->flags & BXE_ONE_PORT_FLAG)
//#define BXE_NO_WOL_FLAG      0x00000008
//#define BXE_USING_DAC_FLAG   0x00000010
//#define BXE_USING_MSIX_FLAG  0x00000020
//#define BXE_USING_MSI_FLAG   0x00000040
//#define BXE_DISABLE_MSI_FLAG 0x00000080
#define BXE_NO_MCP_FLAG      0x00000200
#define BXE_NOMCP(sc)        (sc->flags & BXE_NO_MCP_FLAG)
//#define BXE_SAFC_TX_FLAG     0x00000400
#define BXE_MF_FUNC_DIS      0x00000800
#define BXE_TX_SWITCHING     0x00001000
#define BXE_NO_PULSE	     0x00002000

    unsigned long debug; /* per-instance debug logging config */

#define MAX_BARS 5
    struct bxe_bar bar[MAX_BARS]; /* map BARs 0, 2, 4 */

    uint16_t doorbell_size;

    /* periodic timer callout */
#define PERIODIC_STOP 0
#define PERIODIC_GO   1
    volatile unsigned long periodic_flags;
    struct callout         periodic_callout;

    /* chip start/stop/reset taskqueue */
#define CHIP_TQ_NONE   0
#define CHIP_TQ_START  1
#define CHIP_TQ_STOP   2
#define CHIP_TQ_REINIT 3
    volatile unsigned long chip_tq_flags;
    struct task            chip_tq_task;
    struct taskqueue       *chip_tq;
    char                   chip_tq_name[32];

    struct timeout_task        sp_err_timeout_task;

    /* slowpath interrupt taskqueue */
    struct task      sp_tq_task;
    struct taskqueue *sp_tq;
    char             sp_tq_name[32];

    struct bxe_fastpath fp[MAX_RSS_CHAINS];
    struct bxe_sp_objs  sp_objs[MAX_RSS_CHAINS];

    device_t dev;  /* parent device handle */
    uint8_t  unit; /* driver instance number */

    int pcie_bus;    /* PCIe bus number */
    int pcie_device; /* PCIe device/slot number */
    int pcie_func;   /* PCIe function number */

    uint8_t pfunc_rel; /* function relative */
    uint8_t pfunc_abs; /* function absolute */
    uint8_t path_id;   /* function absolute */
#define SC_PATH(sc)     (sc->path_id)
#define SC_PORT(sc)     (sc->pfunc_rel & 1)
#define SC_FUNC(sc)     (sc->pfunc_rel)
#define SC_ABS_FUNC(sc) (sc->pfunc_abs)
#define SC_VN(sc)       (sc->pfunc_rel >> 1)
#define SC_L_ID(sc)     (SC_VN(sc) << 2)
#define PORT_ID(sc)     SC_PORT(sc)
#define PATH_ID(sc)     SC_PATH(sc)
#define VNIC_ID(sc)     SC_VN(sc)
#define FUNC_ID(sc)     SC_FUNC(sc)
#define ABS_FUNC_ID(sc) SC_ABS_FUNC(sc)
#define SC_FW_MB_IDX_VN(sc, vn)                                \
    (SC_PORT(sc) + (vn) *                                      \
     ((CHIP_IS_E1x(sc) || (CHIP_IS_MODE_4_PORT(sc))) ? 2 : 1))
#define SC_FW_MB_IDX(sc) SC_FW_MB_IDX_VN(sc, SC_VN(sc))

    int if_capen; /* enabled interface capabilities */

    struct bxe_devinfo devinfo;
    char fw_ver_str[32];
    char mf_mode_str[32];
    char pci_link_str[32];

    const struct iro *iro_array;

#ifdef BXE_CORE_LOCK_SX
    struct sx      core_sx;
    char           core_sx_name[32];
#else
    struct mtx     core_mtx;
    char           core_mtx_name[32];
#endif
    struct mtx     sp_mtx;
    char           sp_mtx_name[32];
    struct mtx     dmae_mtx;
    char           dmae_mtx_name[32];
    struct mtx     fwmb_mtx;
    char           fwmb_mtx_name[32];
    struct mtx     print_mtx;
    char           print_mtx_name[32];
    struct mtx     stats_mtx;
    char           stats_mtx_name[32];
    struct mtx     mcast_mtx;
    char           mcast_mtx_name[32];

#ifdef BXE_CORE_LOCK_SX
#define BXE_CORE_TRYLOCK(sc)      sx_try_xlock(&sc->core_sx)
#define BXE_CORE_LOCK(sc)         sx_xlock(&sc->core_sx)
#define BXE_CORE_UNLOCK(sc)       sx_xunlock(&sc->core_sx)
#define BXE_CORE_LOCK_ASSERT(sc)  sx_assert(&sc->core_sx, SA_XLOCKED)
#else
#define BXE_CORE_TRYLOCK(sc)      mtx_trylock(&sc->core_mtx)
#define BXE_CORE_LOCK(sc)         mtx_lock(&sc->core_mtx)
#define BXE_CORE_UNLOCK(sc)       mtx_unlock(&sc->core_mtx)
#define BXE_CORE_LOCK_ASSERT(sc)  mtx_assert(&sc->core_mtx, MA_OWNED)
#endif

#define BXE_SP_LOCK(sc)           mtx_lock(&sc->sp_mtx)
#define BXE_SP_UNLOCK(sc)         mtx_unlock(&sc->sp_mtx)
#define BXE_SP_LOCK_ASSERT(sc)    mtx_assert(&sc->sp_mtx, MA_OWNED)

#define BXE_DMAE_LOCK(sc)         mtx_lock(&sc->dmae_mtx)
#define BXE_DMAE_UNLOCK(sc)       mtx_unlock(&sc->dmae_mtx)
#define BXE_DMAE_LOCK_ASSERT(sc)  mtx_assert(&sc->dmae_mtx, MA_OWNED)

#define BXE_FWMB_LOCK(sc)         mtx_lock(&sc->fwmb_mtx)
#define BXE_FWMB_UNLOCK(sc)       mtx_unlock(&sc->fwmb_mtx)
#define BXE_FWMB_LOCK_ASSERT(sc)  mtx_assert(&sc->fwmb_mtx, MA_OWNED)

#define BXE_PRINT_LOCK(sc)        mtx_lock(&sc->print_mtx)
#define BXE_PRINT_UNLOCK(sc)      mtx_unlock(&sc->print_mtx)
#define BXE_PRINT_LOCK_ASSERT(sc) mtx_assert(&sc->print_mtx, MA_OWNED)

#define BXE_STATS_LOCK(sc)        mtx_lock(&sc->stats_mtx)
#define BXE_STATS_UNLOCK(sc)      mtx_unlock(&sc->stats_mtx)
#define BXE_STATS_LOCK_ASSERT(sc) mtx_assert(&sc->stats_mtx, MA_OWNED)

#if __FreeBSD_version < 800000
#define BXE_MCAST_LOCK(sc)        \
    do {                          \
        mtx_lock(&sc->mcast_mtx); \
        IF_ADDR_LOCK(sc->ifp);  \
    } while (0)
#define BXE_MCAST_UNLOCK(sc)        \
    do {                            \
        IF_ADDR_UNLOCK(sc->ifp);  \
        mtx_unlock(&sc->mcast_mtx); \
    } while (0)
#else
#define BXE_MCAST_LOCK(sc)         \
    do {                           \
        mtx_lock(&sc->mcast_mtx);  \
        if_maddr_rlock(sc->ifp); \
    } while (0)
#define BXE_MCAST_UNLOCK(sc)         \
    do {                             \
        if_maddr_runlock(sc->ifp); \
        mtx_unlock(&sc->mcast_mtx);  \
    } while (0)
#endif
#define BXE_MCAST_LOCK_ASSERT(sc) mtx_assert(&sc->mcast_mtx, MA_OWNED)

    int dmae_ready;
#define DMAE_READY(sc) (sc->dmae_ready)

    struct ecore_credit_pool_obj vlans_pool;
    struct ecore_credit_pool_obj macs_pool;
    struct ecore_rx_mode_obj     rx_mode_obj;
    struct ecore_mcast_obj       mcast_obj;
    struct ecore_rss_config_obj  rss_conf_obj;
    struct ecore_func_sp_obj     func_obj;

    uint16_t fw_seq;
    uint16_t fw_drv_pulse_wr_seq;
    uint32_t func_stx;

    struct elink_params         link_params;
    struct elink_vars           link_vars;
    uint32_t                    link_cnt;
    struct bxe_link_report_data last_reported_link;
    char mac_addr_str[32];

    int last_reported_link_state;

    int tx_ring_size;
    int rx_ring_size;
    int wol;

    int is_leader;
    int recovery_state;
#define BXE_RECOVERY_DONE        1
#define BXE_RECOVERY_INIT        2
#define BXE_RECOVERY_WAIT        3
#define BXE_RECOVERY_FAILED      4
#define BXE_RECOVERY_NIC_LOADING 5

#define BXE_ERR_TXQ_STUCK       0x1  /* Tx queue stuck detected by driver. */
#define BXE_ERR_MISC            0x2  /* MISC ERR */
#define BXE_ERR_PARITY          0x4  /* Parity error detected. */
#define BXE_ERR_STATS_TO        0x8  /* Statistics timeout detected. */
#define BXE_ERR_MC_ASSERT       0x10 /* MC assert attention received. */
#define BXE_ERR_PANIC           0x20 /* Driver asserted. */
#define BXE_ERR_MCP_ASSERT      0x40 /* MCP assert attention received. No Recovery*/
#define BXE_ERR_GLOBAL          0x80 /* PCIe/PXP/IGU/MISC/NIG device blocks error- needs PCIe/Fundamental reset */
        uint32_t error_status;

    uint32_t rx_mode;
#define BXE_RX_MODE_NONE     0
#define BXE_RX_MODE_NORMAL   1
#define BXE_RX_MODE_ALLMULTI 2
#define BXE_RX_MODE_PROMISC  3
#define BXE_MAX_MULTICAST    64

    struct bxe_port port;

    struct cmng_init cmng;

    /* user configs */
    int      num_queues;
    int      max_rx_bufs;
    int      hc_rx_ticks;
    int      hc_tx_ticks;
    int      rx_budget;
    int      max_aggregation_size;
    int      mrrs;
    int      autogreeen;
#define AUTO_GREEN_HW_DEFAULT 0
#define AUTO_GREEN_FORCE_ON   1
#define AUTO_GREEN_FORCE_OFF  2
    int      interrupt_mode;
#define INTR_MODE_INTX 0
#define INTR_MODE_MSI  1
#define INTR_MODE_MSIX 2
    int      udp_rss;

    /* interrupt allocations */
    struct bxe_intr intr[MAX_RSS_CHAINS+1];
    int             intr_count;
    uint8_t         igu_dsb_id;
    uint8_t         igu_base_sb;
    uint8_t         igu_sb_cnt;
    //uint8_t         min_msix_vec_cnt;
    uint32_t        igu_base_addr;
    //bus_addr_t      def_status_blk_mapping;
    uint8_t         base_fw_ndsb;
#define DEF_SB_IGU_ID 16
#define DEF_SB_ID     HC_SP_SB_ID

    /* parent bus DMA tag  */
    bus_dma_tag_t parent_dma_tag;

    /* default status block */
    struct bxe_dma              def_sb_dma;
    struct host_sp_status_block *def_sb;
    uint16_t                    def_idx;
    uint16_t                    def_att_idx;
    uint32_t                    attn_state;
    struct attn_route           attn_group[MAX_DYNAMIC_ATTN_GRPS];

/* general SP events - stats query, cfc delete, etc */
#define HC_SP_INDEX_ETH_DEF_CONS         3
/* EQ completions */
#define HC_SP_INDEX_EQ_CONS              7
/* FCoE L2 connection completions */
#define HC_SP_INDEX_ETH_FCOE_TX_CQ_CONS  6
#define HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS  4
/* iSCSI L2 */
#define HC_SP_INDEX_ETH_ISCSI_CQ_CONS    5
#define HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS 1

    /* event queue */
    struct bxe_dma        eq_dma;
    union event_ring_elem *eq;
    uint16_t              eq_prod;
    uint16_t              eq_cons;
    uint16_t              *eq_cons_sb;
#define NUM_EQ_PAGES     1 /* must be a power of 2 */
#define EQ_DESC_CNT_PAGE (BCM_PAGE_SIZE / sizeof(union event_ring_elem))
#define EQ_DESC_MAX_PAGE (EQ_DESC_CNT_PAGE - 1)
#define NUM_EQ_DESC      (EQ_DESC_CNT_PAGE * NUM_EQ_PAGES)
#define EQ_DESC_MASK     (NUM_EQ_DESC - 1)
#define MAX_EQ_AVAIL     (EQ_DESC_MAX_PAGE * NUM_EQ_PAGES - 2)
/* depends on EQ_DESC_CNT_PAGE being a power of 2 */
#define NEXT_EQ_IDX(x)                                      \
    ((((x) & EQ_DESC_MAX_PAGE) == (EQ_DESC_MAX_PAGE - 1)) ? \
         ((x) + 2) : ((x) + 1))
/* depends on the above and on NUM_EQ_PAGES being a power of 2 */
#define EQ_DESC(x) ((x) & EQ_DESC_MASK)

    /* slow path */
    struct bxe_dma      sp_dma;
    struct bxe_slowpath *sp;
    unsigned long       sp_state;

    /* slow path queue */
    struct bxe_dma spq_dma;
    struct eth_spe *spq;
#define SP_DESC_CNT     (BCM_PAGE_SIZE / sizeof(struct eth_spe))
#define MAX_SP_DESC_CNT (SP_DESC_CNT - 1)
#define MAX_SPQ_PENDING 8

    uint16_t       spq_prod_idx;
    struct eth_spe *spq_prod_bd;
    struct eth_spe *spq_last_bd;
    uint16_t       *dsb_sp_prod;
    //uint16_t       *spq_hw_con;
    //uint16_t       spq_left;

    volatile unsigned long eq_spq_left; /* COMMON_xxx ramrod credit */
    volatile unsigned long cq_spq_left; /* ETH_xxx ramrod credit */

    /* fw decompression buffer */
    struct bxe_dma gz_buf_dma;
    void           *gz_buf;
    z_streamp      gz_strm;
    uint32_t       gz_outlen;
#define GUNZIP_BUF(sc)    (sc->gz_buf)
#define GUNZIP_OUTLEN(sc) (sc->gz_outlen)
#define GUNZIP_PHYS(sc)   (sc->gz_buf_dma.paddr)
#define FW_BUF_SIZE       0x40000

    const struct raw_op *init_ops;
    const uint16_t *init_ops_offsets; /* init block offsets inside init_ops */
    const uint32_t *init_data;        /* data blob, 32 bit granularity */
    uint32_t       init_mode_flags;
#define INIT_MODE_FLAGS(sc) (sc->init_mode_flags)
    /* PRAM blobs - raw data */
    const uint8_t *tsem_int_table_data;
    const uint8_t *tsem_pram_data;
    const uint8_t *usem_int_table_data;
    const uint8_t *usem_pram_data;
    const uint8_t *xsem_int_table_data;
    const uint8_t *xsem_pram_data;
    const uint8_t *csem_int_table_data;
    const uint8_t *csem_pram_data;
#define INIT_OPS(sc)                 (sc->init_ops)
#define INIT_OPS_OFFSETS(sc)         (sc->init_ops_offsets)
#define INIT_DATA(sc)                (sc->init_data)
#define INIT_TSEM_INT_TABLE_DATA(sc) (sc->tsem_int_table_data)
#define INIT_TSEM_PRAM_DATA(sc)      (sc->tsem_pram_data)
#define INIT_USEM_INT_TABLE_DATA(sc) (sc->usem_int_table_data)
#define INIT_USEM_PRAM_DATA(sc)      (sc->usem_pram_data)
#define INIT_XSEM_INT_TABLE_DATA(sc) (sc->xsem_int_table_data)
#define INIT_XSEM_PRAM_DATA(sc)      (sc->xsem_pram_data)
#define INIT_CSEM_INT_TABLE_DATA(sc) (sc->csem_int_table_data)
#define INIT_CSEM_PRAM_DATA(sc)      (sc->csem_pram_data)

    /* ILT
     * For max 196 cids (64*3 + non-eth), 32KB ILT page size and 1KB
     * context size we need 8 ILT entries.
     */
#define ILT_MAX_L2_LINES 8
    struct hw_context context[ILT_MAX_L2_LINES];
    struct ecore_ilt *ilt;
#define ILT_MAX_LINES 256

/* max supported number of RSS queues: IGU SBs minus one for CNIC */
#define BXE_MAX_RSS_COUNT(sc) ((sc)->igu_sb_cnt - CNIC_SUPPORT(sc))
/* max CID count: Max RSS * Max_Tx_Multi_Cos + FCoE + iSCSI */
#if 1
#define BXE_L2_MAX_CID(sc)                                              \
    (BXE_MAX_RSS_COUNT(sc) * ECORE_MULTI_TX_COS + 2 * CNIC_SUPPORT(sc))
#else
#define BXE_L2_MAX_CID(sc) /* OOO + FWD */                              \
    (BXE_MAX_RSS_COUNT(sc) * ECORE_MULTI_TX_COS + 4 * CNIC_SUPPORT(sc))
#endif
#if 1
#define BXE_L2_CID_COUNT(sc)                                             \
    (BXE_NUM_ETH_QUEUES(sc) * ECORE_MULTI_TX_COS + 2 * CNIC_SUPPORT(sc))
#else
#define BXE_L2_CID_COUNT(sc) /* OOO + FWD */                             \
    (BXE_NUM_ETH_QUEUES(sc) * ECORE_MULTI_TX_COS + 4 * CNIC_SUPPORT(sc))
#endif
#define L2_ILT_LINES(sc)                                \
    (DIV_ROUND_UP(BXE_L2_CID_COUNT(sc), ILT_PAGE_CIDS))

    int qm_cid_count;

    uint8_t dropless_fc;

    /* total number of FW statistics requests */
    uint8_t fw_stats_num;
    /*
     * This is a memory buffer that will contain both statistics ramrod
     * request and data.
     */
    struct bxe_dma fw_stats_dma;
    /*
     * FW statistics request shortcut (points at the beginning of fw_stats
     * buffer).
     */
    int                     fw_stats_req_size;
    struct bxe_fw_stats_req *fw_stats_req;
    bus_addr_t              fw_stats_req_mapping;
    /*
     * FW statistics data shortcut (points at the beginning of fw_stats
     * buffer + fw_stats_req_size).
     */
    int                      fw_stats_data_size;
    struct bxe_fw_stats_data *fw_stats_data;
    bus_addr_t               fw_stats_data_mapping;

    /* tracking a pending STAT_QUERY ramrod */
    uint16_t stats_pending;
    /* number of completed statistics ramrods */
    uint16_t stats_comp;
    uint16_t stats_counter;
    uint8_t  stats_init;
    int      stats_state;

    struct bxe_eth_stats         eth_stats;
    struct host_func_stats       func_stats;
    struct bxe_eth_stats_old     eth_stats_old;
    struct bxe_net_stats_old     net_stats_old;
    struct bxe_fw_port_stats_old fw_stats_old;

    struct dmae_cmd stats_dmae; /* used by dmae command loader */
    int                 executer_idx;

    int mtu;

    /* LLDP params */
    struct bxe_config_lldp_params lldp_config_params;
    /* DCB support on/off */
    int dcb_state;
#define BXE_DCB_STATE_OFF 0
#define BXE_DCB_STATE_ON  1
    /* DCBX engine mode */
    int dcbx_enabled;
#define BXE_DCBX_ENABLED_OFF        0
#define BXE_DCBX_ENABLED_ON_NEG_OFF 1
#define BXE_DCBX_ENABLED_ON_NEG_ON  2
#define BXE_DCBX_ENABLED_INVALID    -1
    uint8_t dcbx_mode_uset;
    struct bxe_config_dcbx_params dcbx_config_params;
    struct bxe_dcbx_port_params   dcbx_port_params;
    int dcb_version;

    uint8_t cnic_support;
    uint8_t cnic_enabled;
    uint8_t cnic_loaded;
#define CNIC_SUPPORT(sc) 0 /* ((sc)->cnic_support) */
#define CNIC_ENABLED(sc) 0 /* ((sc)->cnic_enabled) */
#define CNIC_LOADED(sc)  0 /* ((sc)->cnic_loaded) */

    /* multiple tx classes of service */
    uint8_t max_cos;
#define BXE_MAX_PRIORITY 8
    /* priority to cos mapping */
    uint8_t prio_to_cos[BXE_MAX_PRIORITY];

    int panic;

    struct cdev *ioctl_dev;

    void *grc_dump;
    unsigned int trigger_grcdump;
    unsigned int  grcdump_done;
    unsigned int grcdump_started;
    int bxe_pause_param;
    void *eeprom;
}; /* struct bxe_softc */

/* IOCTL sub-commands for edebug and firmware upgrade */
#define BXE_IOC_RD_NVRAM        1
#define BXE_IOC_WR_NVRAM        2
#define BXE_IOC_STATS_SHOW_NUM  3
#define BXE_IOC_STATS_SHOW_STR  4
#define BXE_IOC_STATS_SHOW_CNT  5

struct bxe_nvram_data {
    uint32_t op; /* ioctl sub-command */
    uint32_t offset;
    uint32_t len;
    uint32_t value[1]; /* variable */
};

union bxe_stats_show_data {
    uint32_t op; /* ioctl sub-command */

    struct {
        uint32_t num; /* return number of stats */
        uint32_t len; /* length of each string item */
    } desc;

    /* variable length... */
    char str[1]; /* holds names of desc.num stats, each desc.len in length */

    /* variable length... */
    uint64_t stats[1]; /* holds all stats */
};

/* function init flags */
#define FUNC_FLG_RSS     0x0001
#define FUNC_FLG_STATS   0x0002
/* FUNC_FLG_UNMATCHED       0x0004 */
#define FUNC_FLG_TPA     0x0008
#define FUNC_FLG_SPQ     0x0010
#define FUNC_FLG_LEADING 0x0020 /* PF only */

struct bxe_func_init_params {
    bus_addr_t fw_stat_map; /* (dma) valid if FUNC_FLG_STATS */
    bus_addr_t spq_map;     /* (dma) valid if FUNC_FLG_SPQ */
    uint16_t   func_flgs;
    uint16_t   func_id;     /* abs function id */
    uint16_t   pf_id;
    uint16_t   spq_prod;    /* valid if FUNC_FLG_SPQ */
};

/* memory resources reside at BARs 0, 2, 4 */
/* Run `pciconf -lb` to see mappings */
#define BAR0 0
#define BAR1 2
#define BAR2 4

#ifdef BXE_REG_NO_INLINE

uint8_t bxe_reg_read8(struct bxe_softc *sc, bus_size_t offset);
uint16_t bxe_reg_read16(struct bxe_softc *sc, bus_size_t offset);
uint32_t bxe_reg_read32(struct bxe_softc *sc, bus_size_t offset);

void bxe_reg_write8(struct bxe_softc *sc, bus_size_t offset, uint8_t val);
void bxe_reg_write16(struct bxe_softc *sc, bus_size_t offset, uint16_t val);
void bxe_reg_write32(struct bxe_softc *sc, bus_size_t offset, uint32_t val);

#define REG_RD8(sc, offset)  bxe_reg_read8(sc, offset)
#define REG_RD16(sc, offset) bxe_reg_read16(sc, offset)
#define REG_RD32(sc, offset) bxe_reg_read32(sc, offset)

#define REG_WR8(sc, offset, val)  bxe_reg_write8(sc, offset, val)
#define REG_WR16(sc, offset, val) bxe_reg_write16(sc, offset, val)
#define REG_WR32(sc, offset, val) bxe_reg_write32(sc, offset, val)

#else /* not BXE_REG_NO_INLINE */

#define REG_WR8(sc, offset, val)            \
    bus_space_write_1(sc->bar[BAR0].tag,    \
                      sc->bar[BAR0].handle, \
                      offset, val)

#define REG_WR16(sc, offset, val)           \
    bus_space_write_2(sc->bar[BAR0].tag,    \
                      sc->bar[BAR0].handle, \
                      offset, val)

#define REG_WR32(sc, offset, val)           \
    bus_space_write_4(sc->bar[BAR0].tag,    \
                      sc->bar[BAR0].handle, \
                      offset, val)

#define REG_RD8(sc, offset)                \
    bus_space_read_1(sc->bar[BAR0].tag,    \
                     sc->bar[BAR0].handle, \
                     offset)

#define REG_RD16(sc, offset)               \
    bus_space_read_2(sc->bar[BAR0].tag,    \
                     sc->bar[BAR0].handle, \
                     offset)

#define REG_RD32(sc, offset)               \
    bus_space_read_4(sc->bar[BAR0].tag,    \
                     sc->bar[BAR0].handle, \
                     offset)

#endif /* BXE_REG_NO_INLINE */

#define REG_RD(sc, offset)      REG_RD32(sc, offset)
#define REG_WR(sc, offset, val) REG_WR32(sc, offset, val)

#define REG_RD_IND(sc, offset)      bxe_reg_rd_ind(sc, offset)
#define REG_WR_IND(sc, offset, val) bxe_reg_wr_ind(sc, offset, val)

#define BXE_SP(sc, var) (&(sc)->sp->var)
#define BXE_SP_MAPPING(sc, var) \
    (sc->sp_dma.paddr + offsetof(struct bxe_slowpath, var))

#define BXE_FP(sc, nr, var) ((sc)->fp[(nr)].var)
#define BXE_SP_OBJ(sc, fp) ((sc)->sp_objs[(fp)->index])

#define REG_RD_DMAE(sc, offset, valp, len32)               \
    do {                                                   \
        bxe_read_dmae(sc, offset, len32);                  \
        memcpy(valp, BXE_SP(sc, wb_data[0]), (len32) * 4); \
    } while (0)

#define REG_WR_DMAE(sc, offset, valp, len32)                            \
    do {                                                                \
        memcpy(BXE_SP(sc, wb_data[0]), valp, (len32) * 4);              \
        bxe_write_dmae(sc, BXE_SP_MAPPING(sc, wb_data), offset, len32); \
    } while (0)

#define REG_WR_DMAE_LEN(sc, offset, valp, len32) \
    REG_WR_DMAE(sc, offset, valp, len32)

#define REG_RD_DMAE_LEN(sc, offset, valp, len32) \
    REG_RD_DMAE(sc, offset, valp, len32)

#define VIRT_WR_DMAE_LEN(sc, data, addr, len32, le32_swap)         \
    do {                                                           \
        /* if (le32_swap) {                                     */ \
        /*    BLOGW(sc, "VIRT_WR_DMAE_LEN with le32_swap=1\n"); */ \
        /* }                                                    */ \
        memcpy(GUNZIP_BUF(sc), data, len32 * 4);                   \
        ecore_write_big_buf_wb(sc, addr, len32);                   \
    } while (0)

#define BXE_DB_MIN_SHIFT 3   /* 8 bytes */
#define BXE_DB_SHIFT     7   /* 128 bytes */
#if (BXE_DB_SHIFT < BXE_DB_MIN_SHIFT)
#error "Minimum DB doorbell stride is 8"
#endif
#define DPM_TRIGGER_TYPE 0x40
#define DOORBELL(sc, cid, val)                                              \
    do {                                                                    \
        bus_space_write_4(sc->bar[BAR1].tag, sc->bar[BAR1].handle,          \
                          ((sc->doorbell_size * (cid)) + DPM_TRIGGER_TYPE), \
                          (uint32_t)val);                                   \
    } while(0)

#define SHMEM_ADDR(sc, field)                                       \
    (sc->devinfo.shmem_base + offsetof(struct shmem_region, field))
#define SHMEM_RD(sc, field)      REG_RD(sc, SHMEM_ADDR(sc, field))
#define SHMEM_RD16(sc, field)    REG_RD16(sc, SHMEM_ADDR(sc, field))
#define SHMEM_WR(sc, field, val) REG_WR(sc, SHMEM_ADDR(sc, field), val)

#define SHMEM2_ADDR(sc, field)                                        \
    (sc->devinfo.shmem2_base + offsetof(struct shmem2_region, field))
#define SHMEM2_HAS(sc, field)                                            \
    (sc->devinfo.shmem2_base && (REG_RD(sc, SHMEM2_ADDR(sc, size)) >     \
                                 offsetof(struct shmem2_region, field)))
#define SHMEM2_RD(sc, field)      REG_RD(sc, SHMEM2_ADDR(sc, field))
#define SHMEM2_WR(sc, field, val) REG_WR(sc, SHMEM2_ADDR(sc, field), val)

#define MFCFG_ADDR(sc, field)                                  \
    (sc->devinfo.mf_cfg_base + offsetof(struct mf_cfg, field))
#define MFCFG_RD(sc, field)      REG_RD(sc, MFCFG_ADDR(sc, field))
#define MFCFG_RD16(sc, field)    REG_RD16(sc, MFCFG_ADDR(sc, field))
#define MFCFG_WR(sc, field, val) REG_WR(sc, MFCFG_ADDR(sc, field), val)

/* DMAE command defines */

#define DMAE_TIMEOUT      -1
#define DMAE_PCI_ERROR    -2 /* E2 and onward */
#define DMAE_NOT_RDY      -3
#define DMAE_PCI_ERR_FLAG 0x80000000

#define DMAE_SRC_PCI      0
#define DMAE_SRC_GRC      1

#define DMAE_DST_NONE     0
#define DMAE_DST_PCI      1
#define DMAE_DST_GRC      2

#define DMAE_COMP_PCI     0
#define DMAE_COMP_GRC     1

#define DMAE_COMP_REGULAR 0
#define DMAE_COM_SET_ERR  1

#define DMAE_CMD_SRC_PCI (DMAE_SRC_PCI << DMAE_CMD_SRC_SHIFT)
#define DMAE_CMD_SRC_GRC (DMAE_SRC_GRC << DMAE_CMD_SRC_SHIFT)
#define DMAE_CMD_DST_PCI (DMAE_DST_PCI << DMAE_CMD_DST_SHIFT)
#define DMAE_CMD_DST_GRC (DMAE_DST_GRC << DMAE_CMD_DST_SHIFT)

#define DMAE_CMD_C_DST_PCI (DMAE_COMP_PCI << DMAE_CMD_C_DST_SHIFT)
#define DMAE_CMD_C_DST_GRC (DMAE_COMP_GRC << DMAE_CMD_C_DST_SHIFT)

#define DMAE_CMD_ENDIANITY_NO_SWAP   (0 << DMAE_CMD_ENDIANITY_SHIFT)
#define DMAE_CMD_ENDIANITY_B_SWAP    (1 << DMAE_CMD_ENDIANITY_SHIFT)
#define DMAE_CMD_ENDIANITY_DW_SWAP   (2 << DMAE_CMD_ENDIANITY_SHIFT)
#define DMAE_CMD_ENDIANITY_B_DW_SWAP (3 << DMAE_CMD_ENDIANITY_SHIFT)

#define DMAE_CMD_PORT_0 0
#define DMAE_CMD_PORT_1 DMAE_CMD_PORT

#define DMAE_SRC_PF 0
#define DMAE_SRC_VF 1

#define DMAE_DST_PF 0
#define DMAE_DST_VF 1

#define DMAE_C_SRC 0
#define DMAE_C_DST 1

#define DMAE_LEN32_RD_MAX     0x80
#define DMAE_LEN32_WR_MAX(sc) (CHIP_IS_E1(sc) ? 0x400 : 0x2000)

#define DMAE_COMP_VAL 0x60d0d0ae /* E2 and beyond, upper bit indicates error */

#define MAX_DMAE_C_PER_PORT 8
#define INIT_DMAE_C(sc)     ((SC_PORT(sc) * MAX_DMAE_C_PER_PORT) + SC_VN(sc))
#define PMF_DMAE_C(sc)      ((SC_PORT(sc) * MAX_DMAE_C_PER_PORT) + E1HVN_MAX)

static const uint32_t dmae_reg_go_c[] = {
    DMAE_REG_GO_C0,  DMAE_REG_GO_C1,  DMAE_REG_GO_C2,  DMAE_REG_GO_C3,
    DMAE_REG_GO_C4,  DMAE_REG_GO_C5,  DMAE_REG_GO_C6,  DMAE_REG_GO_C7,
    DMAE_REG_GO_C8,  DMAE_REG_GO_C9,  DMAE_REG_GO_C10, DMAE_REG_GO_C11,
    DMAE_REG_GO_C12, DMAE_REG_GO_C13, DMAE_REG_GO_C14, DMAE_REG_GO_C15
};

#define ATTN_NIG_FOR_FUNC     (1L << 8)
#define ATTN_SW_TIMER_4_FUNC  (1L << 9)
#define GPIO_2_FUNC           (1L << 10)
#define GPIO_3_FUNC           (1L << 11)
#define GPIO_4_FUNC           (1L << 12)
#define ATTN_GENERAL_ATTN_1   (1L << 13)
#define ATTN_GENERAL_ATTN_2   (1L << 14)
#define ATTN_GENERAL_ATTN_3   (1L << 15)
#define ATTN_GENERAL_ATTN_4   (1L << 13)
#define ATTN_GENERAL_ATTN_5   (1L << 14)
#define ATTN_GENERAL_ATTN_6   (1L << 15)
#define ATTN_HARD_WIRED_MASK  0xff00
#define ATTENTION_ID          4

#define AEU_IN_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR \
    AEU_INPUTS_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR

#define MAX_IGU_ATTN_ACK_TO 100

#define STORM_ASSERT_ARRAY_SIZE 50

#define BXE_PMF_LINK_ASSERT(sc) \
    GENERAL_ATTEN_OFFSET(LINK_SYNC_ATTENTION_BIT_FUNC_0 + SC_FUNC(sc))

#define BXE_MC_ASSERT_BITS \
    (GENERAL_ATTEN_OFFSET(TSTORM_FATAL_ASSERT_ATTENTION_BIT) | \
     GENERAL_ATTEN_OFFSET(USTORM_FATAL_ASSERT_ATTENTION_BIT) | \
     GENERAL_ATTEN_OFFSET(CSTORM_FATAL_ASSERT_ATTENTION_BIT) | \
     GENERAL_ATTEN_OFFSET(XSTORM_FATAL_ASSERT_ATTENTION_BIT))

#define BXE_MCP_ASSERT \
    GENERAL_ATTEN_OFFSET(MCP_FATAL_ASSERT_ATTENTION_BIT)

#define BXE_GRC_TIMEOUT GENERAL_ATTEN_OFFSET(LATCHED_ATTN_TIMEOUT_GRC)
#define BXE_GRC_RSV     (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCR) | \
                         GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCT) | \
                         GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCN) | \
                         GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCU) | \
                         GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCP) | \
                         GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RSVD_GRC))

#define MULTI_MASK 0x7f

#define PFS_PER_PORT(sc)                               \
    ((CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) ? 2 : 4)
#define SC_MAX_VN_NUM(sc) PFS_PER_PORT(sc)

#define FIRST_ABS_FUNC_IN_PORT(sc)                    \
    ((CHIP_PORT_MODE(sc) == CHIP_PORT_MODE_NONE) ?    \
     PORT_ID(sc) : (PATH_ID(sc) + (2 * PORT_ID(sc))))

#define FOREACH_ABS_FUNC_IN_PORT(sc, i)            \
    for ((i) = FIRST_ABS_FUNC_IN_PORT(sc);         \
         (i) < MAX_FUNC_NUM;                       \
         (i) += (MAX_FUNC_NUM / PFS_PER_PORT(sc)))

#define BXE_SWCID_SHIFT 17
#define BXE_SWCID_MASK  ((0x1 << BXE_SWCID_SHIFT) - 1)

#define SW_CID(x)  (le32toh(x) & BXE_SWCID_MASK)
#define CQE_CMD(x) (le32toh(x) >> COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT)

#define CQE_TYPE(cqe_fp_flags)   ((cqe_fp_flags) & ETH_FAST_PATH_RX_CQE_TYPE)
#define CQE_TYPE_START(cqe_type) ((cqe_type) == RX_ETH_CQE_TYPE_ETH_START_AGG)
#define CQE_TYPE_STOP(cqe_type)  ((cqe_type) == RX_ETH_CQE_TYPE_ETH_STOP_AGG)
#define CQE_TYPE_SLOW(cqe_type)  ((cqe_type) == RX_ETH_CQE_TYPE_ETH_RAMROD)
#define CQE_TYPE_FAST(cqe_type)  ((cqe_type) == RX_ETH_CQE_TYPE_ETH_FASTPATH)

/* must be used on a CID before placing it on a HW ring */
#define HW_CID(sc, x) \
    ((SC_PORT(sc) << 23) | (SC_VN(sc) << BXE_SWCID_SHIFT) | (x))

#define SPEED_10    10
#define SPEED_100   100
#define SPEED_1000  1000
#define SPEED_2500  2500
#define SPEED_10000 10000

#define PCI_PM_D0    1
#define PCI_PM_D3hot 2

#ifndef DUPLEX_UNKNOWN
#define DUPLEX_UNKNOWN (0xff)
#endif

#ifndef SPEED_UNKNOWN
#define SPEED_UNKNOWN (-1)
#endif

/* Enable or disable autonegotiation. */
#define AUTONEG_DISABLE         0x00
#define AUTONEG_ENABLE          0x01

/* Which connector port. */
#define PORT_TP                 0x00
#define PORT_AUI                0x01
#define PORT_MII                0x02
#define PORT_FIBRE              0x03
#define PORT_BNC                0x04
#define PORT_DA                 0x05
#define PORT_NONE               0xef
#define PORT_OTHER              0xff

int  bxe_test_bit(int nr, volatile unsigned long * addr);
void bxe_set_bit(unsigned int nr, volatile unsigned long * addr);
void bxe_clear_bit(int nr, volatile unsigned long * addr);
int  bxe_test_and_set_bit(int nr, volatile unsigned long * addr);
int  bxe_test_and_clear_bit(int nr, volatile unsigned long * addr);
int  bxe_cmpxchg(volatile int *addr, int old, int new);

void bxe_reg_wr_ind(struct bxe_softc *sc, uint32_t addr,
                    uint32_t val);
uint32_t bxe_reg_rd_ind(struct bxe_softc *sc, uint32_t addr);


int bxe_dma_alloc(struct bxe_softc *sc, bus_size_t size,
                  struct bxe_dma *dma, const char *msg);
void bxe_dma_free(struct bxe_softc *sc, struct bxe_dma *dma);

uint32_t bxe_dmae_opcode_add_comp(uint32_t opcode, uint8_t comp_type);
uint32_t bxe_dmae_opcode_clr_src_reset(uint32_t opcode);
uint32_t bxe_dmae_opcode(struct bxe_softc *sc, uint8_t src_type,
                         uint8_t dst_type, uint8_t with_comp,
                         uint8_t comp_type);
void bxe_post_dmae(struct bxe_softc *sc, struct dmae_cmd *dmae, int idx);
void bxe_read_dmae(struct bxe_softc *sc, uint32_t src_addr, uint32_t len32);
void bxe_write_dmae(struct bxe_softc *sc, bus_addr_t dma_addr,
                    uint32_t dst_addr, uint32_t len32);
void bxe_write_dmae_phys_len(struct bxe_softc *sc, bus_addr_t phys_addr,
                             uint32_t addr, uint32_t len);

void bxe_set_ctx_validation(struct bxe_softc *sc, struct eth_context *cxt,
                            uint32_t cid);
void bxe_update_coalesce_sb_index(struct bxe_softc *sc, uint8_t fw_sb_id,
                                  uint8_t sb_index, uint8_t disable,
                                  uint16_t usec);

int bxe_sp_post(struct bxe_softc *sc, int command, int cid,
                uint32_t data_hi, uint32_t data_lo, int cmd_type);

void bxe_igu_ack_sb(struct bxe_softc *sc, uint8_t igu_sb_id,
                    uint8_t segment, uint16_t index, uint8_t op,
                    uint8_t update);

void ecore_init_e1_firmware(struct bxe_softc *sc);
void ecore_init_e1h_firmware(struct bxe_softc *sc);
void ecore_init_e2_firmware(struct bxe_softc *sc);

void ecore_storm_memset_struct(struct bxe_softc *sc, uint32_t addr,
                               size_t size, uint32_t *data);

/*********************/
/* LOGGING AND DEBUG */
/*********************/

/* debug logging codepaths */
#define DBG_LOAD   0x00000001 /* load and unload    */
#define DBG_INTR   0x00000002 /* interrupt handling */
#define DBG_SP     0x00000004 /* slowpath handling  */
#define DBG_STATS  0x00000008 /* stats updates      */
#define DBG_TX     0x00000010 /* packet transmit    */
#define DBG_RX     0x00000020 /* packet receive     */
#define DBG_PHY    0x00000040 /* phy/link handling  */
#define DBG_IOCTL  0x00000080 /* ioctl handling     */
#define DBG_MBUF   0x00000100 /* dumping mbuf info  */
#define DBG_REGS   0x00000200 /* register access    */
#define DBG_LRO    0x00000400 /* lro processing     */
#define DBG_ASSERT 0x80000000 /* debug assert       */
#define DBG_ALL    0xFFFFFFFF /* flying monkeys     */

#define DBASSERT(sc, exp, msg)                         \
    do {                                               \
        if (__predict_false(sc->debug & DBG_ASSERT)) { \
            if (__predict_false(!(exp))) {             \
                panic msg;                             \
            }                                          \
        }                                              \
    } while (0)

/* log a debug message */
#define BLOGD(sc, codepath, format, args...)           \
    do {                                               \
        if (__predict_false(sc->debug & (codepath))) { \
            device_printf((sc)->dev,                   \
                          "%s(%s:%d) " format,         \
                          __FUNCTION__,                \
                          __FILE__,                    \
                          __LINE__,                    \
                          ## args);                    \
        }                                              \
    } while(0)

/* log a info message */
#define BLOGI(sc, format, args...)             \
    do {                                       \
        if (__predict_false(sc->debug)) {      \
            device_printf((sc)->dev,           \
                          "%s(%s:%d) " format, \
                          __FUNCTION__,        \
                          __FILE__,            \
                          __LINE__,            \
                          ## args);            \
        } else {                               \
            device_printf((sc)->dev,           \
                          format,              \
                          ## args);            \
        }                                      \
    } while(0)

/* log a warning message */
#define BLOGW(sc, format, args...)                      \
    do {                                                \
        if (__predict_false(sc->debug)) {               \
            device_printf((sc)->dev,                    \
                          "%s(%s:%d) WARNING: " format, \
                          __FUNCTION__,                 \
                          __FILE__,                     \
                          __LINE__,                     \
                          ## args);                     \
        } else {                                        \
            device_printf((sc)->dev,                    \
                          "WARNING: " format,           \
                          ## args);                     \
        }                                               \
    } while(0)

/* log a error message */
#define BLOGE(sc, format, args...)                    \
    do {                                              \
        if (__predict_false(sc->debug)) {             \
            device_printf((sc)->dev,                  \
                          "%s(%s:%d) ERROR: " format, \
                          __FUNCTION__,               \
                          __FILE__,                   \
                          __LINE__,                   \
                          ## args);                   \
        } else {                                      \
            device_printf((sc)->dev,                  \
                          "ERROR: " format,           \
                          ## args);                   \
        }                                             \
    } while(0)

#ifdef ECORE_STOP_ON_ERROR

#define bxe_panic(sc, msg) \
    do {                   \
        panic msg;         \
    } while (0)

#else

#define bxe_panic(sc, msg) \
    device_printf((sc)->dev, "%s (%s,%d)\n", __FUNCTION__, __FILE__, __LINE__);

#endif

#define CATC_TRIGGER(sc, data) REG_WR((sc), 0x2000, (data));
#define CATC_TRIGGER_START(sc) CATC_TRIGGER((sc), 0xcafecafe)

void bxe_dump_mem(struct bxe_softc *sc, char *tag,
                  uint8_t *mem, uint32_t len);
void bxe_dump_mbuf_data(struct bxe_softc *sc, char *pTag,
                        struct mbuf *m, uint8_t contents);

#if __FreeBSD_version >= 800000
#if (__FreeBSD_version >= 1001513 && __FreeBSD_version < 1100000) ||\
    __FreeBSD_version >= 1100048
#define BXE_SET_FLOWID(m) M_HASHTYPE_SET(m, M_HASHTYPE_OPAQUE)
#define BXE_VALID_FLOWID(m) (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
#else
#define BXE_VALID_FLOWID(m) ((m->m_flags & M_FLOWID) != 0)
#define BXE_SET_FLOWID(m) m->m_flags |= M_FLOWID
#endif
#endif /* #if __FreeBSD_version >= 800000 */

/***********/
/* INLINES */
/***********/

static inline uint32_t
reg_poll(struct bxe_softc *sc,
         uint32_t         reg,
         uint32_t         expected,
         int              ms,
         int              wait)
{
    uint32_t val;

    do {
        val = REG_RD(sc, reg);
        if (val == expected) {
            break;
        }
        ms -= wait;
        DELAY(wait * 1000);
    } while (ms > 0);

    return (val);
}

static inline void
bxe_update_fp_sb_idx(struct bxe_fastpath *fp)
{
    mb(); /* status block is written to by the chip */
    fp->fp_hc_idx = fp->sb_running_index[SM_RX_ID];
}

static inline void
bxe_igu_ack_sb_gen(struct bxe_softc *sc,
                   uint8_t          igu_sb_id,
                   uint8_t          segment,
                   uint16_t         index,
                   uint8_t          op,
                   uint8_t          update,
                   uint32_t         igu_addr)
{
    struct igu_regular cmd_data = {0};

    cmd_data.sb_id_and_flags =
        ((index << IGU_REGULAR_SB_INDEX_SHIFT) |
         (segment << IGU_REGULAR_SEGMENT_ACCESS_SHIFT) |
         (update << IGU_REGULAR_BUPDATE_SHIFT) |
         (op << IGU_REGULAR_ENABLE_INT_SHIFT));

    BLOGD(sc, DBG_INTR, "write 0x%08x to IGU addr 0x%x\n",
            cmd_data.sb_id_and_flags, igu_addr);
    REG_WR(sc, igu_addr, cmd_data.sb_id_and_flags);

    /* Make sure that ACK is written */
    bus_space_barrier(sc->bar[0].tag, sc->bar[0].handle, 0, 0,
                      BUS_SPACE_BARRIER_WRITE);
    mb();
}

static inline void
bxe_hc_ack_sb(struct bxe_softc *sc,
              uint8_t          sb_id,
              uint8_t          storm,
              uint16_t         index,
              uint8_t          op,
              uint8_t          update)
{
    uint32_t hc_addr = (HC_REG_COMMAND_REG + SC_PORT(sc)*32 +
                        COMMAND_REG_INT_ACK);
    struct igu_ack_register igu_ack;

    igu_ack.status_block_index = index;
    igu_ack.sb_id_and_flags =
        ((sb_id << IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT) |
         (storm << IGU_ACK_REGISTER_STORM_ID_SHIFT) |
         (update << IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT) |
         (op << IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT));

    REG_WR(sc, hc_addr, (*(uint32_t *)&igu_ack));

    /* Make sure that ACK is written */
    bus_space_barrier(sc->bar[0].tag, sc->bar[0].handle, 0, 0,
                      BUS_SPACE_BARRIER_WRITE);
    mb();
}

static inline void
bxe_ack_sb(struct bxe_softc *sc,
           uint8_t          igu_sb_id,
           uint8_t          storm,
           uint16_t         index,
           uint8_t          op,
           uint8_t          update)
{
    if (sc->devinfo.int_block == INT_BLOCK_HC)
        bxe_hc_ack_sb(sc, igu_sb_id, storm, index, op, update);
    else {
        uint8_t segment;
        if (CHIP_INT_MODE_IS_BC(sc)) {
            segment = storm;
        } else if (igu_sb_id != sc->igu_dsb_id) {
            segment = IGU_SEG_ACCESS_DEF;
        } else if (storm == ATTENTION_ID) {
            segment = IGU_SEG_ACCESS_ATTN;
        } else {
            segment = IGU_SEG_ACCESS_DEF;
        }
        bxe_igu_ack_sb(sc, igu_sb_id, segment, index, op, update);
    }
}

static inline uint16_t
bxe_hc_ack_int(struct bxe_softc *sc)
{
    uint32_t hc_addr = (HC_REG_COMMAND_REG + SC_PORT(sc)*32 +
                        COMMAND_REG_SIMD_MASK);
    uint32_t result = REG_RD(sc, hc_addr);

    mb();
    return (result);
}

static inline uint16_t
bxe_igu_ack_int(struct bxe_softc *sc)
{
    uint32_t igu_addr = (BAR_IGU_INTMEM + IGU_REG_SISR_MDPC_WMASK_LSB_UPPER*8);
    uint32_t result = REG_RD(sc, igu_addr);

    BLOGD(sc, DBG_INTR, "read 0x%08x from IGU addr 0x%x\n",
          result, igu_addr);

    mb();
    return (result);
}

static inline uint16_t
bxe_ack_int(struct bxe_softc *sc)
{
    mb();
    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        return (bxe_hc_ack_int(sc));
    } else {
        return (bxe_igu_ack_int(sc));
    }
}

static inline int
func_by_vn(struct bxe_softc *sc,
           int              vn)
{
    return (2 * vn + SC_PORT(sc));
}

/*
 * Statistics ID are global per chip/path, while Client IDs for E1x
 * are per port.
 */
static inline uint8_t
bxe_stats_id(struct bxe_fastpath *fp)
{
    struct bxe_softc *sc = fp->sc;

    if (!CHIP_IS_E1x(sc)) {
        return (fp->cl_id);
    }

    return (fp->cl_id + SC_PORT(sc) * FP_SB_MAX_E1x);
}

#endif /* __BXE_H__ */

