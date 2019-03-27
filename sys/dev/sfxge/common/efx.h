/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_EFX_H
#define	_SYS_EFX_H

#include "efx_annote.h"
#include "efsys.h"
#include "efx_check.h"
#include "efx_phy_ids.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	EFX_STATIC_ASSERT(_cond)		\
	((void)sizeof (char[(_cond) ? 1 : -1]))

#define	EFX_ARRAY_SIZE(_array)			\
	(sizeof (_array) / sizeof ((_array)[0]))

#define	EFX_FIELD_OFFSET(_type, _field)		\
	((size_t)&(((_type *)0)->_field))

/* The macro expands divider twice */
#define	EFX_DIV_ROUND_UP(_n, _d)		(((_n) + (_d) - 1) / (_d))

/* Return codes */

typedef __success(return == 0) int efx_rc_t;


/* Chip families */

typedef enum efx_family_e {
	EFX_FAMILY_INVALID,
	EFX_FAMILY_FALCON,	/* Obsolete and not supported */
	EFX_FAMILY_SIENA,
	EFX_FAMILY_HUNTINGTON,
	EFX_FAMILY_MEDFORD,
	EFX_FAMILY_MEDFORD2,
	EFX_FAMILY_NTYPES
} efx_family_t;

extern	__checkReturn	efx_rc_t
efx_family(
	__in		uint16_t venid,
	__in		uint16_t devid,
	__out		efx_family_t *efp,
	__out		unsigned int *membarp);


#define	EFX_PCI_VENID_SFC			0x1924

#define	EFX_PCI_DEVID_FALCON			0x0710	/* SFC4000 */

#define	EFX_PCI_DEVID_BETHPAGE			0x0803	/* SFC9020 */
#define	EFX_PCI_DEVID_SIENA			0x0813	/* SFL9021 */
#define	EFX_PCI_DEVID_SIENA_F1_UNINIT		0x0810

#define	EFX_PCI_DEVID_HUNTINGTON_PF_UNINIT	0x0901
#define	EFX_PCI_DEVID_FARMINGDALE		0x0903	/* SFC9120 PF */
#define	EFX_PCI_DEVID_GREENPORT			0x0923	/* SFC9140 PF */

#define	EFX_PCI_DEVID_FARMINGDALE_VF		0x1903	/* SFC9120 VF */
#define	EFX_PCI_DEVID_GREENPORT_VF		0x1923	/* SFC9140 VF */

#define	EFX_PCI_DEVID_MEDFORD_PF_UNINIT		0x0913
#define	EFX_PCI_DEVID_MEDFORD			0x0A03	/* SFC9240 PF */
#define	EFX_PCI_DEVID_MEDFORD_VF		0x1A03	/* SFC9240 VF */

#define	EFX_PCI_DEVID_MEDFORD2_PF_UNINIT	0x0B13
#define	EFX_PCI_DEVID_MEDFORD2			0x0B03	/* SFC9250 PF */
#define	EFX_PCI_DEVID_MEDFORD2_VF		0x1B03	/* SFC9250 VF */


#define	EFX_MEM_BAR_SIENA			2

#define	EFX_MEM_BAR_HUNTINGTON_PF		2
#define	EFX_MEM_BAR_HUNTINGTON_VF		0

#define	EFX_MEM_BAR_MEDFORD_PF			2
#define	EFX_MEM_BAR_MEDFORD_VF			0

#define	EFX_MEM_BAR_MEDFORD2			0


/* Error codes */

enum {
	EFX_ERR_INVALID,
	EFX_ERR_SRAM_OOB,
	EFX_ERR_BUFID_DC_OOB,
	EFX_ERR_MEM_PERR,
	EFX_ERR_RBUF_OWN,
	EFX_ERR_TBUF_OWN,
	EFX_ERR_RDESQ_OWN,
	EFX_ERR_TDESQ_OWN,
	EFX_ERR_EVQ_OWN,
	EFX_ERR_EVFF_OFLO,
	EFX_ERR_ILL_ADDR,
	EFX_ERR_SRAM_PERR,
	EFX_ERR_NCODES
};

/* Calculate the IEEE 802.3 CRC32 of a MAC addr */
extern	__checkReturn		uint32_t
efx_crc32_calculate(
	__in			uint32_t crc_init,
	__in_ecount(length)	uint8_t const *input,
	__in			int length);


/* Type prototypes */

typedef struct efx_rxq_s	efx_rxq_t;

/* NIC */

typedef struct efx_nic_s	efx_nic_t;

extern	__checkReturn	efx_rc_t
efx_nic_create(
	__in		efx_family_t family,
	__in		efsys_identifier_t *esip,
	__in		efsys_bar_t *esbp,
	__in		efsys_lock_t *eslp,
	__deref_out	efx_nic_t **enpp);

/* EFX_FW_VARIANT codes map one to one on MC_CMD_FW codes */
typedef enum efx_fw_variant_e {
	EFX_FW_VARIANT_FULL_FEATURED,
	EFX_FW_VARIANT_LOW_LATENCY,
	EFX_FW_VARIANT_PACKED_STREAM,
	EFX_FW_VARIANT_HIGH_TX_RATE,
	EFX_FW_VARIANT_PACKED_STREAM_HASH_MODE_1,
	EFX_FW_VARIANT_RULES_ENGINE,
	EFX_FW_VARIANT_DPDK,
	EFX_FW_VARIANT_DONT_CARE = 0xffffffff
} efx_fw_variant_t;

extern	__checkReturn	efx_rc_t
efx_nic_probe(
	__in		efx_nic_t *enp,
	__in		efx_fw_variant_t efv);

extern	__checkReturn	efx_rc_t
efx_nic_init(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_nic_reset(
	__in		efx_nic_t *enp);

extern	__checkReturn	boolean_t
efx_nic_hw_unavailable(
	__in		efx_nic_t *enp);

extern			void
efx_nic_set_hw_unavailable(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_DIAG

extern	__checkReturn	efx_rc_t
efx_nic_register_test(
	__in		efx_nic_t *enp);

#endif	/* EFSYS_OPT_DIAG */

extern		void
efx_nic_fini(
	__in		efx_nic_t *enp);

extern		void
efx_nic_unprobe(
	__in		efx_nic_t *enp);

extern		void
efx_nic_destroy(
	__in	efx_nic_t *enp);

#define	EFX_PCIE_LINK_SPEED_GEN1		1
#define	EFX_PCIE_LINK_SPEED_GEN2		2
#define	EFX_PCIE_LINK_SPEED_GEN3		3

typedef enum efx_pcie_link_performance_e {
	EFX_PCIE_LINK_PERFORMANCE_UNKNOWN_BANDWIDTH,
	EFX_PCIE_LINK_PERFORMANCE_SUBOPTIMAL_BANDWIDTH,
	EFX_PCIE_LINK_PERFORMANCE_SUBOPTIMAL_LATENCY,
	EFX_PCIE_LINK_PERFORMANCE_OPTIMAL
} efx_pcie_link_performance_t;

extern	__checkReturn	efx_rc_t
efx_nic_calculate_pcie_link_bandwidth(
	__in		uint32_t pcie_link_width,
	__in		uint32_t pcie_link_gen,
	__out		uint32_t *bandwidth_mbpsp);

extern	__checkReturn	efx_rc_t
efx_nic_check_pcie_link_speed(
	__in		efx_nic_t *enp,
	__in		uint32_t pcie_link_width,
	__in		uint32_t pcie_link_gen,
	__out		efx_pcie_link_performance_t *resultp);

#if EFSYS_OPT_MCDI

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
/* Huntington and Medford require MCDIv2 commands */
#define	WITH_MCDI_V2 1
#endif

typedef struct efx_mcdi_req_s efx_mcdi_req_t;

typedef enum efx_mcdi_exception_e {
	EFX_MCDI_EXCEPTION_MC_REBOOT,
	EFX_MCDI_EXCEPTION_MC_BADASSERT,
} efx_mcdi_exception_t;

#if EFSYS_OPT_MCDI_LOGGING
typedef enum efx_log_msg_e {
	EFX_LOG_INVALID,
	EFX_LOG_MCDI_REQUEST,
	EFX_LOG_MCDI_RESPONSE,
} efx_log_msg_t;
#endif /* EFSYS_OPT_MCDI_LOGGING */

typedef struct efx_mcdi_transport_s {
	void		*emt_context;
	efsys_mem_t	*emt_dma_mem;
	void		(*emt_execute)(void *, efx_mcdi_req_t *);
	void		(*emt_ev_cpl)(void *);
	void		(*emt_exception)(void *, efx_mcdi_exception_t);
#if EFSYS_OPT_MCDI_LOGGING
	void		(*emt_logger)(void *, efx_log_msg_t,
					void *, size_t, void *, size_t);
#endif /* EFSYS_OPT_MCDI_LOGGING */
#if EFSYS_OPT_MCDI_PROXY_AUTH
	void		(*emt_ev_proxy_response)(void *, uint32_t, efx_rc_t);
#endif /* EFSYS_OPT_MCDI_PROXY_AUTH */
} efx_mcdi_transport_t;

extern	__checkReturn	efx_rc_t
efx_mcdi_init(
	__in		efx_nic_t *enp,
	__in		const efx_mcdi_transport_t *mtp);

extern	__checkReturn	efx_rc_t
efx_mcdi_reboot(
	__in		efx_nic_t *enp);

			void
efx_mcdi_new_epoch(
	__in		efx_nic_t *enp);

extern			void
efx_mcdi_get_timeout(
	__in		efx_nic_t *enp,
	__in		efx_mcdi_req_t *emrp,
	__out		uint32_t *usec_timeoutp);

extern			void
efx_mcdi_request_start(
	__in		efx_nic_t *enp,
	__in		efx_mcdi_req_t *emrp,
	__in		boolean_t ev_cpl);

extern	__checkReturn	boolean_t
efx_mcdi_request_poll(
	__in		efx_nic_t *enp);

extern	__checkReturn	boolean_t
efx_mcdi_request_abort(
	__in		efx_nic_t *enp);

extern			void
efx_mcdi_fini(
	__in		efx_nic_t *enp);

#endif	/* EFSYS_OPT_MCDI */

/* INTR */

#define	EFX_NINTR_SIENA 1024

typedef enum efx_intr_type_e {
	EFX_INTR_INVALID = 0,
	EFX_INTR_LINE,
	EFX_INTR_MESSAGE,
	EFX_INTR_NTYPES
} efx_intr_type_t;

#define	EFX_INTR_SIZE	(sizeof (efx_oword_t))

extern	__checkReturn	efx_rc_t
efx_intr_init(
	__in		efx_nic_t *enp,
	__in		efx_intr_type_t type,
	__in_opt	efsys_mem_t *esmp);

extern			void
efx_intr_enable(
	__in		efx_nic_t *enp);

extern			void
efx_intr_disable(
	__in		efx_nic_t *enp);

extern			void
efx_intr_disable_unlocked(
	__in		efx_nic_t *enp);

#define	EFX_INTR_NEVQS	32

extern	__checkReturn	efx_rc_t
efx_intr_trigger(
	__in		efx_nic_t *enp,
	__in		unsigned int level);

extern			void
efx_intr_status_line(
	__in		efx_nic_t *enp,
	__out		boolean_t *fatalp,
	__out		uint32_t *maskp);

extern			void
efx_intr_status_message(
	__in		efx_nic_t *enp,
	__in		unsigned int message,
	__out		boolean_t *fatalp);

extern			void
efx_intr_fatal(
	__in		efx_nic_t *enp);

extern			void
efx_intr_fini(
	__in		efx_nic_t *enp);

/* MAC */

#if EFSYS_OPT_MAC_STATS

/* START MKCONFIG GENERATED EfxHeaderMacBlock ea466a9bc8789994 */
typedef enum efx_mac_stat_e {
	EFX_MAC_RX_OCTETS,
	EFX_MAC_RX_PKTS,
	EFX_MAC_RX_UNICST_PKTS,
	EFX_MAC_RX_MULTICST_PKTS,
	EFX_MAC_RX_BRDCST_PKTS,
	EFX_MAC_RX_PAUSE_PKTS,
	EFX_MAC_RX_LE_64_PKTS,
	EFX_MAC_RX_65_TO_127_PKTS,
	EFX_MAC_RX_128_TO_255_PKTS,
	EFX_MAC_RX_256_TO_511_PKTS,
	EFX_MAC_RX_512_TO_1023_PKTS,
	EFX_MAC_RX_1024_TO_15XX_PKTS,
	EFX_MAC_RX_GE_15XX_PKTS,
	EFX_MAC_RX_ERRORS,
	EFX_MAC_RX_FCS_ERRORS,
	EFX_MAC_RX_DROP_EVENTS,
	EFX_MAC_RX_FALSE_CARRIER_ERRORS,
	EFX_MAC_RX_SYMBOL_ERRORS,
	EFX_MAC_RX_ALIGN_ERRORS,
	EFX_MAC_RX_INTERNAL_ERRORS,
	EFX_MAC_RX_JABBER_PKTS,
	EFX_MAC_RX_LANE0_CHAR_ERR,
	EFX_MAC_RX_LANE1_CHAR_ERR,
	EFX_MAC_RX_LANE2_CHAR_ERR,
	EFX_MAC_RX_LANE3_CHAR_ERR,
	EFX_MAC_RX_LANE0_DISP_ERR,
	EFX_MAC_RX_LANE1_DISP_ERR,
	EFX_MAC_RX_LANE2_DISP_ERR,
	EFX_MAC_RX_LANE3_DISP_ERR,
	EFX_MAC_RX_MATCH_FAULT,
	EFX_MAC_RX_NODESC_DROP_CNT,
	EFX_MAC_TX_OCTETS,
	EFX_MAC_TX_PKTS,
	EFX_MAC_TX_UNICST_PKTS,
	EFX_MAC_TX_MULTICST_PKTS,
	EFX_MAC_TX_BRDCST_PKTS,
	EFX_MAC_TX_PAUSE_PKTS,
	EFX_MAC_TX_LE_64_PKTS,
	EFX_MAC_TX_65_TO_127_PKTS,
	EFX_MAC_TX_128_TO_255_PKTS,
	EFX_MAC_TX_256_TO_511_PKTS,
	EFX_MAC_TX_512_TO_1023_PKTS,
	EFX_MAC_TX_1024_TO_15XX_PKTS,
	EFX_MAC_TX_GE_15XX_PKTS,
	EFX_MAC_TX_ERRORS,
	EFX_MAC_TX_SGL_COL_PKTS,
	EFX_MAC_TX_MULT_COL_PKTS,
	EFX_MAC_TX_EX_COL_PKTS,
	EFX_MAC_TX_LATE_COL_PKTS,
	EFX_MAC_TX_DEF_PKTS,
	EFX_MAC_TX_EX_DEF_PKTS,
	EFX_MAC_PM_TRUNC_BB_OVERFLOW,
	EFX_MAC_PM_DISCARD_BB_OVERFLOW,
	EFX_MAC_PM_TRUNC_VFIFO_FULL,
	EFX_MAC_PM_DISCARD_VFIFO_FULL,
	EFX_MAC_PM_TRUNC_QBB,
	EFX_MAC_PM_DISCARD_QBB,
	EFX_MAC_PM_DISCARD_MAPPING,
	EFX_MAC_RXDP_Q_DISABLED_PKTS,
	EFX_MAC_RXDP_DI_DROPPED_PKTS,
	EFX_MAC_RXDP_STREAMING_PKTS,
	EFX_MAC_RXDP_HLB_FETCH,
	EFX_MAC_RXDP_HLB_WAIT,
	EFX_MAC_VADAPTER_RX_UNICAST_PACKETS,
	EFX_MAC_VADAPTER_RX_UNICAST_BYTES,
	EFX_MAC_VADAPTER_RX_MULTICAST_PACKETS,
	EFX_MAC_VADAPTER_RX_MULTICAST_BYTES,
	EFX_MAC_VADAPTER_RX_BROADCAST_PACKETS,
	EFX_MAC_VADAPTER_RX_BROADCAST_BYTES,
	EFX_MAC_VADAPTER_RX_BAD_PACKETS,
	EFX_MAC_VADAPTER_RX_BAD_BYTES,
	EFX_MAC_VADAPTER_RX_OVERFLOW,
	EFX_MAC_VADAPTER_TX_UNICAST_PACKETS,
	EFX_MAC_VADAPTER_TX_UNICAST_BYTES,
	EFX_MAC_VADAPTER_TX_MULTICAST_PACKETS,
	EFX_MAC_VADAPTER_TX_MULTICAST_BYTES,
	EFX_MAC_VADAPTER_TX_BROADCAST_PACKETS,
	EFX_MAC_VADAPTER_TX_BROADCAST_BYTES,
	EFX_MAC_VADAPTER_TX_BAD_PACKETS,
	EFX_MAC_VADAPTER_TX_BAD_BYTES,
	EFX_MAC_VADAPTER_TX_OVERFLOW,
	EFX_MAC_FEC_UNCORRECTED_ERRORS,
	EFX_MAC_FEC_CORRECTED_ERRORS,
	EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE0,
	EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE1,
	EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE2,
	EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE3,
	EFX_MAC_CTPIO_VI_BUSY_FALLBACK,
	EFX_MAC_CTPIO_LONG_WRITE_SUCCESS,
	EFX_MAC_CTPIO_MISSING_DBELL_FAIL,
	EFX_MAC_CTPIO_OVERFLOW_FAIL,
	EFX_MAC_CTPIO_UNDERFLOW_FAIL,
	EFX_MAC_CTPIO_TIMEOUT_FAIL,
	EFX_MAC_CTPIO_NONCONTIG_WR_FAIL,
	EFX_MAC_CTPIO_FRM_CLOBBER_FAIL,
	EFX_MAC_CTPIO_INVALID_WR_FAIL,
	EFX_MAC_CTPIO_VI_CLOBBER_FALLBACK,
	EFX_MAC_CTPIO_UNQUALIFIED_FALLBACK,
	EFX_MAC_CTPIO_RUNT_FALLBACK,
	EFX_MAC_CTPIO_SUCCESS,
	EFX_MAC_CTPIO_FALLBACK,
	EFX_MAC_CTPIO_POISON,
	EFX_MAC_CTPIO_ERASE,
	EFX_MAC_RXDP_SCATTER_DISABLED_TRUNC,
	EFX_MAC_RXDP_HLB_IDLE,
	EFX_MAC_RXDP_HLB_TIMEOUT,
	EFX_MAC_NSTATS
} efx_mac_stat_t;

/* END MKCONFIG GENERATED EfxHeaderMacBlock */

#endif	/* EFSYS_OPT_MAC_STATS */

typedef enum efx_link_mode_e {
	EFX_LINK_UNKNOWN = 0,
	EFX_LINK_DOWN,
	EFX_LINK_10HDX,
	EFX_LINK_10FDX,
	EFX_LINK_100HDX,
	EFX_LINK_100FDX,
	EFX_LINK_1000HDX,
	EFX_LINK_1000FDX,
	EFX_LINK_10000FDX,
	EFX_LINK_40000FDX,
	EFX_LINK_25000FDX,
	EFX_LINK_50000FDX,
	EFX_LINK_100000FDX,
	EFX_LINK_NMODES
} efx_link_mode_t;

#define	EFX_MAC_ADDR_LEN 6

#define	EFX_VNI_OR_VSID_LEN 3

#define	EFX_MAC_ADDR_IS_MULTICAST(_address) (((uint8_t *)_address)[0] & 0x01)

#define	EFX_MAC_MULTICAST_LIST_MAX	256

#define	EFX_MAC_SDU_MAX	9202

#define	EFX_MAC_PDU_ADJUSTMENT					\
	(/* EtherII */ 14					\
	    + /* VLAN */ 4					\
	    + /* CRC */ 4					\
	    + /* bug16011 */ 16)				\

#define	EFX_MAC_PDU(_sdu)					\
	P2ROUNDUP((_sdu) + EFX_MAC_PDU_ADJUSTMENT, 8)

/*
 * Due to the P2ROUNDUP in EFX_MAC_PDU(), EFX_MAC_SDU_FROM_PDU() may give
 * the SDU rounded up slightly.
 */
#define	EFX_MAC_SDU_FROM_PDU(_pdu)	((_pdu) - EFX_MAC_PDU_ADJUSTMENT)

#define	EFX_MAC_PDU_MIN	60
#define	EFX_MAC_PDU_MAX	EFX_MAC_PDU(EFX_MAC_SDU_MAX)

extern	__checkReturn	efx_rc_t
efx_mac_pdu_get(
	__in		efx_nic_t *enp,
	__out		size_t *pdu);

extern	__checkReturn	efx_rc_t
efx_mac_pdu_set(
	__in		efx_nic_t *enp,
	__in		size_t pdu);

extern	__checkReturn	efx_rc_t
efx_mac_addr_set(
	__in		efx_nic_t *enp,
	__in		uint8_t *addr);

extern	__checkReturn			efx_rc_t
efx_mac_filter_set(
	__in				efx_nic_t *enp,
	__in				boolean_t all_unicst,
	__in				boolean_t mulcst,
	__in				boolean_t all_mulcst,
	__in				boolean_t brdcst);

extern	__checkReturn	efx_rc_t
efx_mac_multicast_list_set(
	__in				efx_nic_t *enp,
	__in_ecount(6*count)		uint8_t const *addrs,
	__in				int count);

extern	__checkReturn	efx_rc_t
efx_mac_filter_default_rxq_set(
	__in		efx_nic_t *enp,
	__in		efx_rxq_t *erp,
	__in		boolean_t using_rss);

extern			void
efx_mac_filter_default_rxq_clear(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_mac_drain(
	__in		efx_nic_t *enp,
	__in		boolean_t enabled);

extern	__checkReturn	efx_rc_t
efx_mac_up(
	__in		efx_nic_t *enp,
	__out		boolean_t *mac_upp);

#define	EFX_FCNTL_RESPOND	0x00000001
#define	EFX_FCNTL_GENERATE	0x00000002

extern	__checkReturn	efx_rc_t
efx_mac_fcntl_set(
	__in		efx_nic_t *enp,
	__in		unsigned int fcntl,
	__in		boolean_t autoneg);

extern			void
efx_mac_fcntl_get(
	__in		efx_nic_t *enp,
	__out		unsigned int *fcntl_wantedp,
	__out		unsigned int *fcntl_linkp);


#if EFSYS_OPT_MAC_STATS

#if EFSYS_OPT_NAMES

extern	__checkReturn			const char *
efx_mac_stat_name(
	__in				efx_nic_t *enp,
	__in				unsigned int id);

#endif	/* EFSYS_OPT_NAMES */

#define	EFX_MAC_STATS_MASK_BITS_PER_PAGE	(8 * sizeof (uint32_t))

#define	EFX_MAC_STATS_MASK_NPAGES	\
	(P2ROUNDUP(EFX_MAC_NSTATS, EFX_MAC_STATS_MASK_BITS_PER_PAGE) / \
	    EFX_MAC_STATS_MASK_BITS_PER_PAGE)

/*
 * Get mask of MAC statistics supported by the hardware.
 *
 * If mask_size is insufficient to return the mask, EINVAL error is
 * returned. EFX_MAC_STATS_MASK_NPAGES multiplied by size of the page
 * (which is sizeof (uint32_t)) is sufficient.
 */
extern	__checkReturn			efx_rc_t
efx_mac_stats_get_mask(
	__in				efx_nic_t *enp,
	__out_bcount(mask_size)		uint32_t *maskp,
	__in				size_t mask_size);

#define	EFX_MAC_STAT_SUPPORTED(_mask, _stat)	\
	((_mask)[(_stat) / EFX_MAC_STATS_MASK_BITS_PER_PAGE] &	\
	    (1ULL << ((_stat) & (EFX_MAC_STATS_MASK_BITS_PER_PAGE - 1))))


extern	__checkReturn			efx_rc_t
efx_mac_stats_clear(
	__in				efx_nic_t *enp);

/*
 * Upload mac statistics supported by the hardware into the given buffer.
 *
 * The DMA buffer must be 4Kbyte aligned and sized to hold at least
 * efx_nic_cfg_t::enc_mac_stats_nstats 64bit counters.
 *
 * The hardware will only DMA statistics that it understands (of course).
 * Drivers should not make any assumptions about which statistics are
 * supported, especially when the statistics are generated by firmware.
 *
 * Thus, drivers should zero this buffer before use, so that not-understood
 * statistics read back as zero.
 */
extern	__checkReturn			efx_rc_t
efx_mac_stats_upload(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp);

extern	__checkReturn			efx_rc_t
efx_mac_stats_periodic(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__in				uint16_t period_ms,
	__in				boolean_t events);

extern	__checkReturn			efx_rc_t
efx_mac_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_MAC_NSTATS)	efsys_stat_t *stat,
	__inout_opt			uint32_t *generationp);

#endif	/* EFSYS_OPT_MAC_STATS */

/* MON */

typedef enum efx_mon_type_e {
	EFX_MON_INVALID = 0,
	EFX_MON_SFC90X0,
	EFX_MON_SFC91X0,
	EFX_MON_SFC92X0,
	EFX_MON_NTYPES
} efx_mon_type_t;

#if EFSYS_OPT_NAMES

extern		const char *
efx_mon_name(
	__in	efx_nic_t *enp);

#endif	/* EFSYS_OPT_NAMES */

extern	__checkReturn	efx_rc_t
efx_mon_init(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_MON_STATS

#define	EFX_MON_STATS_PAGE_SIZE 0x100
#define	EFX_MON_MASK_ELEMENT_SIZE 32

/* START MKCONFIG GENERATED MonitorHeaderStatsBlock 78b65c8d5af9747b */
typedef enum efx_mon_stat_e {
	EFX_MON_STAT_CONTROLLER_TEMP,
	EFX_MON_STAT_PHY_COMMON_TEMP,
	EFX_MON_STAT_CONTROLLER_COOLING,
	EFX_MON_STAT_PHY0_TEMP,
	EFX_MON_STAT_PHY0_COOLING,
	EFX_MON_STAT_PHY1_TEMP,
	EFX_MON_STAT_PHY1_COOLING,
	EFX_MON_STAT_IN_1V0,
	EFX_MON_STAT_IN_1V2,
	EFX_MON_STAT_IN_1V8,
	EFX_MON_STAT_IN_2V5,
	EFX_MON_STAT_IN_3V3,
	EFX_MON_STAT_IN_12V0,
	EFX_MON_STAT_IN_1V2A,
	EFX_MON_STAT_IN_VREF,
	EFX_MON_STAT_OUT_VAOE,
	EFX_MON_STAT_AOE_TEMP,
	EFX_MON_STAT_PSU_AOE_TEMP,
	EFX_MON_STAT_PSU_TEMP,
	EFX_MON_STAT_FAN_0,
	EFX_MON_STAT_FAN_1,
	EFX_MON_STAT_FAN_2,
	EFX_MON_STAT_FAN_3,
	EFX_MON_STAT_FAN_4,
	EFX_MON_STAT_IN_VAOE,
	EFX_MON_STAT_OUT_IAOE,
	EFX_MON_STAT_IN_IAOE,
	EFX_MON_STAT_NIC_POWER,
	EFX_MON_STAT_IN_0V9,
	EFX_MON_STAT_IN_I0V9,
	EFX_MON_STAT_IN_I1V2,
	EFX_MON_STAT_IN_0V9_ADC,
	EFX_MON_STAT_CONTROLLER_2_TEMP,
	EFX_MON_STAT_VREG_INTERNAL_TEMP,
	EFX_MON_STAT_VREG_0V9_TEMP,
	EFX_MON_STAT_VREG_1V2_TEMP,
	EFX_MON_STAT_CONTROLLER_VPTAT,
	EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP,
	EFX_MON_STAT_CONTROLLER_VPTAT_EXTADC,
	EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP_EXTADC,
	EFX_MON_STAT_AMBIENT_TEMP,
	EFX_MON_STAT_AIRFLOW,
	EFX_MON_STAT_VDD08D_VSS08D_CSR,
	EFX_MON_STAT_VDD08D_VSS08D_CSR_EXTADC,
	EFX_MON_STAT_HOTPOINT_TEMP,
	EFX_MON_STAT_PHY_POWER_PORT0,
	EFX_MON_STAT_PHY_POWER_PORT1,
	EFX_MON_STAT_MUM_VCC,
	EFX_MON_STAT_IN_0V9_A,
	EFX_MON_STAT_IN_I0V9_A,
	EFX_MON_STAT_VREG_0V9_A_TEMP,
	EFX_MON_STAT_IN_0V9_B,
	EFX_MON_STAT_IN_I0V9_B,
	EFX_MON_STAT_VREG_0V9_B_TEMP,
	EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY,
	EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY_EXTADC,
	EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY,
	EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY_EXTADC,
	EFX_MON_STAT_CONTROLLER_MASTER_VPTAT,
	EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP,
	EFX_MON_STAT_CONTROLLER_MASTER_VPTAT_EXTADC,
	EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP_EXTADC,
	EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT,
	EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP,
	EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT_EXTADC,
	EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP_EXTADC,
	EFX_MON_STAT_SODIMM_VOUT,
	EFX_MON_STAT_SODIMM_0_TEMP,
	EFX_MON_STAT_SODIMM_1_TEMP,
	EFX_MON_STAT_PHY0_VCC,
	EFX_MON_STAT_PHY1_VCC,
	EFX_MON_STAT_CONTROLLER_TDIODE_TEMP,
	EFX_MON_STAT_BOARD_FRONT_TEMP,
	EFX_MON_STAT_BOARD_BACK_TEMP,
	EFX_MON_STAT_IN_I1V8,
	EFX_MON_STAT_IN_I2V5,
	EFX_MON_STAT_IN_I3V3,
	EFX_MON_STAT_IN_I12V0,
	EFX_MON_STAT_IN_1V3,
	EFX_MON_STAT_IN_I1V3,
	EFX_MON_NSTATS
} efx_mon_stat_t;

/* END MKCONFIG GENERATED MonitorHeaderStatsBlock */

typedef enum efx_mon_stat_state_e {
	EFX_MON_STAT_STATE_OK = 0,
	EFX_MON_STAT_STATE_WARNING = 1,
	EFX_MON_STAT_STATE_FATAL = 2,
	EFX_MON_STAT_STATE_BROKEN = 3,
	EFX_MON_STAT_STATE_NO_READING = 4,
} efx_mon_stat_state_t;

typedef enum efx_mon_stat_unit_e {
	EFX_MON_STAT_UNIT_UNKNOWN = 0,
	EFX_MON_STAT_UNIT_BOOL,
	EFX_MON_STAT_UNIT_TEMP_C,
	EFX_MON_STAT_UNIT_VOLTAGE_MV,
	EFX_MON_STAT_UNIT_CURRENT_MA,
	EFX_MON_STAT_UNIT_POWER_W,
	EFX_MON_STAT_UNIT_RPM,
	EFX_MON_NUNITS
} efx_mon_stat_unit_t;

typedef struct efx_mon_stat_value_s {
	uint16_t		emsv_value;
	efx_mon_stat_state_t	emsv_state;
	efx_mon_stat_unit_t	emsv_unit;
} efx_mon_stat_value_t;

typedef struct efx_mon_limit_value_s {
	uint16_t			emlv_warning_min;
	uint16_t			emlv_warning_max;
	uint16_t			emlv_fatal_min;
	uint16_t			emlv_fatal_max;
} efx_mon_stat_limits_t;

typedef enum efx_mon_stat_portmask_e {
	EFX_MON_STAT_PORTMAP_NONE = 0,
	EFX_MON_STAT_PORTMAP_PORT0 = 1,
	EFX_MON_STAT_PORTMAP_PORT1 = 2,
	EFX_MON_STAT_PORTMAP_PORT2 = 3,
	EFX_MON_STAT_PORTMAP_PORT3 = 4,
	EFX_MON_STAT_PORTMAP_ALL = (-1),
	EFX_MON_STAT_PORTMAP_UNKNOWN = (-2)
} efx_mon_stat_portmask_t;

#if EFSYS_OPT_NAMES

extern					const char *
efx_mon_stat_name(
	__in				efx_nic_t *enp,
	__in				efx_mon_stat_t id);

extern					const char *
efx_mon_stat_description(
	__in				efx_nic_t *enp,
	__in				efx_mon_stat_t id);

#endif	/* EFSYS_OPT_NAMES */

extern	__checkReturn			boolean_t
efx_mon_mcdi_to_efx_stat(
	__in				int mcdi_index,
	__out				efx_mon_stat_t *statp);

extern	__checkReturn			boolean_t
efx_mon_get_stat_unit(
	__in				efx_mon_stat_t stat,
	__out				efx_mon_stat_unit_t *unitp);

extern	__checkReturn			boolean_t
efx_mon_get_stat_portmap(
	__in				efx_mon_stat_t stat,
	__out				efx_mon_stat_portmask_t *maskp);

extern	__checkReturn			efx_rc_t
efx_mon_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_MON_NSTATS)	efx_mon_stat_value_t *values);

extern	__checkReturn			efx_rc_t
efx_mon_limits_update(
	__in				efx_nic_t *enp,
	__inout_ecount(EFX_MON_NSTATS)	efx_mon_stat_limits_t *values);

#endif	/* EFSYS_OPT_MON_STATS */

extern		void
efx_mon_fini(
	__in	efx_nic_t *enp);

/* PHY */

extern	__checkReturn	efx_rc_t
efx_phy_verify(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_PHY_LED_CONTROL

typedef enum efx_phy_led_mode_e {
	EFX_PHY_LED_DEFAULT = 0,
	EFX_PHY_LED_OFF,
	EFX_PHY_LED_ON,
	EFX_PHY_LED_FLASH,
	EFX_PHY_LED_NMODES
} efx_phy_led_mode_t;

extern	__checkReturn	efx_rc_t
efx_phy_led_set(
	__in	efx_nic_t *enp,
	__in	efx_phy_led_mode_t mode);

#endif	/* EFSYS_OPT_PHY_LED_CONTROL */

extern	__checkReturn	efx_rc_t
efx_port_init(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_LOOPBACK

typedef enum efx_loopback_type_e {
	EFX_LOOPBACK_OFF = 0,
	EFX_LOOPBACK_DATA = 1,
	EFX_LOOPBACK_GMAC = 2,
	EFX_LOOPBACK_XGMII = 3,
	EFX_LOOPBACK_XGXS = 4,
	EFX_LOOPBACK_XAUI = 5,
	EFX_LOOPBACK_GMII = 6,
	EFX_LOOPBACK_SGMII = 7,
	EFX_LOOPBACK_XGBR = 8,
	EFX_LOOPBACK_XFI = 9,
	EFX_LOOPBACK_XAUI_FAR = 10,
	EFX_LOOPBACK_GMII_FAR = 11,
	EFX_LOOPBACK_SGMII_FAR = 12,
	EFX_LOOPBACK_XFI_FAR = 13,
	EFX_LOOPBACK_GPHY = 14,
	EFX_LOOPBACK_PHY_XS = 15,
	EFX_LOOPBACK_PCS = 16,
	EFX_LOOPBACK_PMA_PMD = 17,
	EFX_LOOPBACK_XPORT = 18,
	EFX_LOOPBACK_XGMII_WS = 19,
	EFX_LOOPBACK_XAUI_WS = 20,
	EFX_LOOPBACK_XAUI_WS_FAR = 21,
	EFX_LOOPBACK_XAUI_WS_NEAR = 22,
	EFX_LOOPBACK_GMII_WS = 23,
	EFX_LOOPBACK_XFI_WS = 24,
	EFX_LOOPBACK_XFI_WS_FAR = 25,
	EFX_LOOPBACK_PHYXS_WS = 26,
	EFX_LOOPBACK_PMA_INT = 27,
	EFX_LOOPBACK_SD_NEAR = 28,
	EFX_LOOPBACK_SD_FAR = 29,
	EFX_LOOPBACK_PMA_INT_WS = 30,
	EFX_LOOPBACK_SD_FEP2_WS = 31,
	EFX_LOOPBACK_SD_FEP1_5_WS = 32,
	EFX_LOOPBACK_SD_FEP_WS = 33,
	EFX_LOOPBACK_SD_FES_WS = 34,
	EFX_LOOPBACK_AOE_INT_NEAR = 35,
	EFX_LOOPBACK_DATA_WS = 36,
	EFX_LOOPBACK_FORCE_EXT_LINK = 37,
	EFX_LOOPBACK_NTYPES
} efx_loopback_type_t;

typedef enum efx_loopback_kind_e {
	EFX_LOOPBACK_KIND_OFF = 0,
	EFX_LOOPBACK_KIND_ALL,
	EFX_LOOPBACK_KIND_MAC,
	EFX_LOOPBACK_KIND_PHY,
	EFX_LOOPBACK_NKINDS
} efx_loopback_kind_t;

extern			void
efx_loopback_mask(
	__in	efx_loopback_kind_t loopback_kind,
	__out	efx_qword_t *maskp);

extern	__checkReturn	efx_rc_t
efx_port_loopback_set(
	__in	efx_nic_t *enp,
	__in	efx_link_mode_t link_mode,
	__in	efx_loopback_type_t type);

#if EFSYS_OPT_NAMES

extern	__checkReturn	const char *
efx_loopback_type_name(
	__in		efx_nic_t *enp,
	__in		efx_loopback_type_t type);

#endif	/* EFSYS_OPT_NAMES */

#endif	/* EFSYS_OPT_LOOPBACK */

extern	__checkReturn	efx_rc_t
efx_port_poll(
	__in		efx_nic_t *enp,
	__out_opt	efx_link_mode_t	*link_modep);

extern		void
efx_port_fini(
	__in	efx_nic_t *enp);

typedef enum efx_phy_cap_type_e {
	EFX_PHY_CAP_INVALID = 0,
	EFX_PHY_CAP_10HDX,
	EFX_PHY_CAP_10FDX,
	EFX_PHY_CAP_100HDX,
	EFX_PHY_CAP_100FDX,
	EFX_PHY_CAP_1000HDX,
	EFX_PHY_CAP_1000FDX,
	EFX_PHY_CAP_10000FDX,
	EFX_PHY_CAP_PAUSE,
	EFX_PHY_CAP_ASYM,
	EFX_PHY_CAP_AN,
	EFX_PHY_CAP_40000FDX,
	EFX_PHY_CAP_DDM,
	EFX_PHY_CAP_100000FDX,
	EFX_PHY_CAP_25000FDX,
	EFX_PHY_CAP_50000FDX,
	EFX_PHY_CAP_BASER_FEC,
	EFX_PHY_CAP_BASER_FEC_REQUESTED,
	EFX_PHY_CAP_RS_FEC,
	EFX_PHY_CAP_RS_FEC_REQUESTED,
	EFX_PHY_CAP_25G_BASER_FEC,
	EFX_PHY_CAP_25G_BASER_FEC_REQUESTED,
	EFX_PHY_CAP_NTYPES
} efx_phy_cap_type_t;


#define	EFX_PHY_CAP_CURRENT	0x00000000
#define	EFX_PHY_CAP_DEFAULT	0x00000001
#define	EFX_PHY_CAP_PERM	0x00000002

extern		void
efx_phy_adv_cap_get(
	__in		efx_nic_t *enp,
	__in		uint32_t flag,
	__out		uint32_t *maskp);

extern	__checkReturn	efx_rc_t
efx_phy_adv_cap_set(
	__in		efx_nic_t *enp,
	__in		uint32_t mask);

extern			void
efx_phy_lp_cap_get(
	__in		efx_nic_t *enp,
	__out		uint32_t *maskp);

extern	__checkReturn	efx_rc_t
efx_phy_oui_get(
	__in		efx_nic_t *enp,
	__out		uint32_t *ouip);

typedef enum efx_phy_media_type_e {
	EFX_PHY_MEDIA_INVALID = 0,
	EFX_PHY_MEDIA_XAUI,
	EFX_PHY_MEDIA_CX4,
	EFX_PHY_MEDIA_KX4,
	EFX_PHY_MEDIA_XFP,
	EFX_PHY_MEDIA_SFP_PLUS,
	EFX_PHY_MEDIA_BASE_T,
	EFX_PHY_MEDIA_QSFP_PLUS,
	EFX_PHY_MEDIA_NTYPES
} efx_phy_media_type_t;

/*
 * Get the type of medium currently used.  If the board has ports for
 * modules, a module is present, and we recognise the media type of
 * the module, then this will be the media type of the module.
 * Otherwise it will be the media type of the port.
 */
extern			void
efx_phy_media_type_get(
	__in		efx_nic_t *enp,
	__out		efx_phy_media_type_t *typep);

/*
 * 2-wire device address of the base information in accordance with SFF-8472
 * Diagnostic Monitoring Interface for Optical Transceivers section
 * 4 Memory Organization.
 */
#define	EFX_PHY_MEDIA_INFO_DEV_ADDR_SFP_BASE	0xA0

/*
 * 2-wire device address of the digital diagnostics monitoring interface
 * in accordance with SFF-8472 Diagnostic Monitoring Interface for Optical
 * Transceivers section 4 Memory Organization.
 */
#define	EFX_PHY_MEDIA_INFO_DEV_ADDR_SFP_DDM	0xA2

/*
 * Hard wired 2-wire device address for QSFP+ in accordance with SFF-8436
 * QSFP+ 10 Gbs 4X PLUGGABLE TRANSCEIVER section 7.4 Device Addressing and
 * Operation.
 */
#define	EFX_PHY_MEDIA_INFO_DEV_ADDR_QSFP	0xA0

/*
 * Maximum accessible data offset for PHY module information.
 */
#define	EFX_PHY_MEDIA_INFO_MAX_OFFSET		0x100


extern	__checkReturn		efx_rc_t
efx_phy_module_get_info(
	__in			efx_nic_t *enp,
	__in			uint8_t dev_addr,
	__in			size_t offset,
	__in			size_t len,
	__out_bcount(len)	uint8_t *data);

#if EFSYS_OPT_PHY_STATS

/* START MKCONFIG GENERATED PhyHeaderStatsBlock 30ed56ad501f8e36 */
typedef enum efx_phy_stat_e {
	EFX_PHY_STAT_OUI,
	EFX_PHY_STAT_PMA_PMD_LINK_UP,
	EFX_PHY_STAT_PMA_PMD_RX_FAULT,
	EFX_PHY_STAT_PMA_PMD_TX_FAULT,
	EFX_PHY_STAT_PMA_PMD_REV_A,
	EFX_PHY_STAT_PMA_PMD_REV_B,
	EFX_PHY_STAT_PMA_PMD_REV_C,
	EFX_PHY_STAT_PMA_PMD_REV_D,
	EFX_PHY_STAT_PCS_LINK_UP,
	EFX_PHY_STAT_PCS_RX_FAULT,
	EFX_PHY_STAT_PCS_TX_FAULT,
	EFX_PHY_STAT_PCS_BER,
	EFX_PHY_STAT_PCS_BLOCK_ERRORS,
	EFX_PHY_STAT_PHY_XS_LINK_UP,
	EFX_PHY_STAT_PHY_XS_RX_FAULT,
	EFX_PHY_STAT_PHY_XS_TX_FAULT,
	EFX_PHY_STAT_PHY_XS_ALIGN,
	EFX_PHY_STAT_PHY_XS_SYNC_A,
	EFX_PHY_STAT_PHY_XS_SYNC_B,
	EFX_PHY_STAT_PHY_XS_SYNC_C,
	EFX_PHY_STAT_PHY_XS_SYNC_D,
	EFX_PHY_STAT_AN_LINK_UP,
	EFX_PHY_STAT_AN_MASTER,
	EFX_PHY_STAT_AN_LOCAL_RX_OK,
	EFX_PHY_STAT_AN_REMOTE_RX_OK,
	EFX_PHY_STAT_CL22EXT_LINK_UP,
	EFX_PHY_STAT_SNR_A,
	EFX_PHY_STAT_SNR_B,
	EFX_PHY_STAT_SNR_C,
	EFX_PHY_STAT_SNR_D,
	EFX_PHY_STAT_PMA_PMD_SIGNAL_A,
	EFX_PHY_STAT_PMA_PMD_SIGNAL_B,
	EFX_PHY_STAT_PMA_PMD_SIGNAL_C,
	EFX_PHY_STAT_PMA_PMD_SIGNAL_D,
	EFX_PHY_STAT_AN_COMPLETE,
	EFX_PHY_STAT_PMA_PMD_REV_MAJOR,
	EFX_PHY_STAT_PMA_PMD_REV_MINOR,
	EFX_PHY_STAT_PMA_PMD_REV_MICRO,
	EFX_PHY_STAT_PCS_FW_VERSION_0,
	EFX_PHY_STAT_PCS_FW_VERSION_1,
	EFX_PHY_STAT_PCS_FW_VERSION_2,
	EFX_PHY_STAT_PCS_FW_VERSION_3,
	EFX_PHY_STAT_PCS_FW_BUILD_YY,
	EFX_PHY_STAT_PCS_FW_BUILD_MM,
	EFX_PHY_STAT_PCS_FW_BUILD_DD,
	EFX_PHY_STAT_PCS_OP_MODE,
	EFX_PHY_NSTATS
} efx_phy_stat_t;

/* END MKCONFIG GENERATED PhyHeaderStatsBlock */

#if EFSYS_OPT_NAMES

extern					const char *
efx_phy_stat_name(
	__in				efx_nic_t *enp,
	__in				efx_phy_stat_t stat);

#endif	/* EFSYS_OPT_NAMES */

#define	EFX_PHY_STATS_SIZE 0x100

extern	__checkReturn			efx_rc_t
efx_phy_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_PHY_NSTATS)	uint32_t *stat);

#endif	/* EFSYS_OPT_PHY_STATS */


#if EFSYS_OPT_BIST

typedef enum efx_bist_type_e {
	EFX_BIST_TYPE_UNKNOWN,
	EFX_BIST_TYPE_PHY_NORMAL,
	EFX_BIST_TYPE_PHY_CABLE_SHORT,
	EFX_BIST_TYPE_PHY_CABLE_LONG,
	EFX_BIST_TYPE_MC_MEM,	/* Test the MC DMEM and IMEM */
	EFX_BIST_TYPE_SAT_MEM,	/* Test the DMEM and IMEM of satellite cpus */
	EFX_BIST_TYPE_REG,	/* Test the register memories */
	EFX_BIST_TYPE_NTYPES,
} efx_bist_type_t;

typedef enum efx_bist_result_e {
	EFX_BIST_RESULT_UNKNOWN,
	EFX_BIST_RESULT_RUNNING,
	EFX_BIST_RESULT_PASSED,
	EFX_BIST_RESULT_FAILED,
} efx_bist_result_t;

typedef enum efx_phy_cable_status_e {
	EFX_PHY_CABLE_STATUS_OK,
	EFX_PHY_CABLE_STATUS_INVALID,
	EFX_PHY_CABLE_STATUS_OPEN,
	EFX_PHY_CABLE_STATUS_INTRAPAIRSHORT,
	EFX_PHY_CABLE_STATUS_INTERPAIRSHORT,
	EFX_PHY_CABLE_STATUS_BUSY,
} efx_phy_cable_status_t;

typedef enum efx_bist_value_e {
	EFX_BIST_PHY_CABLE_LENGTH_A,
	EFX_BIST_PHY_CABLE_LENGTH_B,
	EFX_BIST_PHY_CABLE_LENGTH_C,
	EFX_BIST_PHY_CABLE_LENGTH_D,
	EFX_BIST_PHY_CABLE_STATUS_A,
	EFX_BIST_PHY_CABLE_STATUS_B,
	EFX_BIST_PHY_CABLE_STATUS_C,
	EFX_BIST_PHY_CABLE_STATUS_D,
	EFX_BIST_FAULT_CODE,
	/*
	 * Memory BIST specific values. These match to the MC_CMD_BIST_POLL
	 * response.
	 */
	EFX_BIST_MEM_TEST,
	EFX_BIST_MEM_ADDR,
	EFX_BIST_MEM_BUS,
	EFX_BIST_MEM_EXPECT,
	EFX_BIST_MEM_ACTUAL,
	EFX_BIST_MEM_ECC,
	EFX_BIST_MEM_ECC_PARITY,
	EFX_BIST_MEM_ECC_FATAL,
	EFX_BIST_NVALUES,
} efx_bist_value_t;

extern	__checkReturn		efx_rc_t
efx_bist_enable_offline(
	__in			efx_nic_t *enp);

extern	__checkReturn		efx_rc_t
efx_bist_start(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type);

extern	__checkReturn		efx_rc_t
efx_bist_poll(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type,
	__out			efx_bist_result_t *resultp,
	__out_opt		uint32_t *value_maskp,
	__out_ecount_opt(count)	unsigned long *valuesp,
	__in			size_t count);

extern				void
efx_bist_stop(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type);

#endif	/* EFSYS_OPT_BIST */

#define	EFX_FEATURE_IPV6		0x00000001
#define	EFX_FEATURE_LFSR_HASH_INSERT	0x00000002
#define	EFX_FEATURE_LINK_EVENTS		0x00000004
#define	EFX_FEATURE_PERIODIC_MAC_STATS	0x00000008
#define	EFX_FEATURE_MCDI		0x00000020
#define	EFX_FEATURE_LOOKAHEAD_SPLIT	0x00000040
#define	EFX_FEATURE_MAC_HEADER_FILTERS	0x00000080
#define	EFX_FEATURE_TURBO		0x00000100
#define	EFX_FEATURE_MCDI_DMA		0x00000200
#define	EFX_FEATURE_TX_SRC_FILTERS	0x00000400
#define	EFX_FEATURE_PIO_BUFFERS		0x00000800
#define	EFX_FEATURE_FW_ASSISTED_TSO	0x00001000
#define	EFX_FEATURE_FW_ASSISTED_TSO_V2	0x00002000
#define	EFX_FEATURE_PACKED_STREAM	0x00004000
#define	EFX_FEATURE_TXQ_CKSUM_OP_DESC	0x00008000

typedef enum efx_tunnel_protocol_e {
	EFX_TUNNEL_PROTOCOL_NONE = 0,
	EFX_TUNNEL_PROTOCOL_VXLAN,
	EFX_TUNNEL_PROTOCOL_GENEVE,
	EFX_TUNNEL_PROTOCOL_NVGRE,
	EFX_TUNNEL_NPROTOS
} efx_tunnel_protocol_t;

typedef enum efx_vi_window_shift_e {
	EFX_VI_WINDOW_SHIFT_INVALID = 0,
	EFX_VI_WINDOW_SHIFT_8K = 13,
	EFX_VI_WINDOW_SHIFT_16K = 14,
	EFX_VI_WINDOW_SHIFT_64K = 16,
} efx_vi_window_shift_t;

typedef struct efx_nic_cfg_s {
	uint32_t		enc_board_type;
	uint32_t		enc_phy_type;
#if EFSYS_OPT_NAMES
	char			enc_phy_name[21];
#endif
	char			enc_phy_revision[21];
	efx_mon_type_t		enc_mon_type;
#if EFSYS_OPT_MON_STATS
	uint32_t		enc_mon_stat_dma_buf_size;
	uint32_t		enc_mon_stat_mask[(EFX_MON_NSTATS + 31) / 32];
#endif
	unsigned int		enc_features;
	efx_vi_window_shift_t	enc_vi_window_shift;
	uint8_t			enc_mac_addr[6];
	uint8_t			enc_port;	/* PHY port number */
	uint32_t		enc_intr_vec_base;
	uint32_t		enc_intr_limit;
	uint32_t		enc_evq_limit;
	uint32_t		enc_txq_limit;
	uint32_t		enc_rxq_limit;
	uint32_t		enc_txq_max_ndescs;
	uint32_t		enc_buftbl_limit;
	uint32_t		enc_piobuf_limit;
	uint32_t		enc_piobuf_size;
	uint32_t		enc_piobuf_min_alloc_size;
	uint32_t		enc_evq_timer_quantum_ns;
	uint32_t		enc_evq_timer_max_us;
	uint32_t		enc_clk_mult;
	uint32_t		enc_rx_prefix_size;
	uint32_t		enc_rx_buf_align_start;
	uint32_t		enc_rx_buf_align_end;
#if EFSYS_OPT_RX_SCALE
	uint32_t		enc_rx_scale_max_exclusive_contexts;
	/*
	 * Mask of supported hash algorithms.
	 * Hash algorithm types are used as the bit indices.
	 */
	uint32_t		enc_rx_scale_hash_alg_mask;
	/*
	 * Indicates whether port numbers can be included to the
	 * input data for hash computation.
	 */
	boolean_t		enc_rx_scale_l4_hash_supported;
	boolean_t		enc_rx_scale_additional_modes_supported;
#endif /* EFSYS_OPT_RX_SCALE */
#if EFSYS_OPT_LOOPBACK
	efx_qword_t		enc_loopback_types[EFX_LINK_NMODES];
#endif	/* EFSYS_OPT_LOOPBACK */
#if EFSYS_OPT_PHY_FLAGS
	uint32_t		enc_phy_flags_mask;
#endif	/* EFSYS_OPT_PHY_FLAGS */
#if EFSYS_OPT_PHY_LED_CONTROL
	uint32_t		enc_led_mask;
#endif	/* EFSYS_OPT_PHY_LED_CONTROL */
#if EFSYS_OPT_PHY_STATS
	uint64_t		enc_phy_stat_mask;
#endif	/* EFSYS_OPT_PHY_STATS */
#if EFSYS_OPT_MCDI
	uint8_t			enc_mcdi_mdio_channel;
#if EFSYS_OPT_PHY_STATS
	uint32_t		enc_mcdi_phy_stat_mask;
#endif	/* EFSYS_OPT_PHY_STATS */
#if EFSYS_OPT_MON_STATS
	uint32_t		*enc_mcdi_sensor_maskp;
	uint32_t		enc_mcdi_sensor_mask_size;
#endif	/* EFSYS_OPT_MON_STATS */
#endif	/* EFSYS_OPT_MCDI */
#if EFSYS_OPT_BIST
	uint32_t		enc_bist_mask;
#endif	/* EFSYS_OPT_BIST */
#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
	uint32_t		enc_pf;
	uint32_t		enc_vf;
	uint32_t		enc_privilege_mask;
#endif /* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
	boolean_t		enc_bug26807_workaround;
	boolean_t		enc_bug35388_workaround;
	boolean_t		enc_bug41750_workaround;
	boolean_t		enc_bug61265_workaround;
	boolean_t		enc_bug61297_workaround;
	boolean_t		enc_rx_batching_enabled;
	/* Maximum number of descriptors completed in an rx event. */
	uint32_t		enc_rx_batch_max;
	/* Number of rx descriptors the hardware requires for a push. */
	uint32_t		enc_rx_push_align;
	/* Maximum amount of data in DMA descriptor */
	uint32_t		enc_tx_dma_desc_size_max;
	/*
	 * Boundary which DMA descriptor data must not cross or 0 if no
	 * limitation.
	 */
	uint32_t		enc_tx_dma_desc_boundary;
	/*
	 * Maximum number of bytes into the packet the TCP header can start for
	 * the hardware to apply TSO packet edits.
	 */
	uint32_t		enc_tx_tso_tcp_header_offset_limit;
	boolean_t		enc_fw_assisted_tso_enabled;
	boolean_t		enc_fw_assisted_tso_v2_enabled;
	boolean_t		enc_fw_assisted_tso_v2_encap_enabled;
	/* Number of TSO contexts on the NIC (FATSOv2) */
	uint32_t		enc_fw_assisted_tso_v2_n_contexts;
	boolean_t		enc_hw_tx_insert_vlan_enabled;
	/* Number of PFs on the NIC */
	uint32_t		enc_hw_pf_count;
	/* Datapath firmware vadapter/vport/vswitch support */
	boolean_t		enc_datapath_cap_evb;
	boolean_t		enc_rx_disable_scatter_supported;
	boolean_t		enc_allow_set_mac_with_installed_filters;
	boolean_t		enc_enhanced_set_mac_supported;
	boolean_t		enc_init_evq_v2_supported;
	boolean_t		enc_rx_packed_stream_supported;
	boolean_t		enc_rx_var_packed_stream_supported;
	boolean_t		enc_rx_es_super_buffer_supported;
	boolean_t		enc_fw_subvariant_no_tx_csum_supported;
	boolean_t		enc_pm_and_rxdp_counters;
	boolean_t		enc_mac_stats_40g_tx_size_bins;
	uint32_t		enc_tunnel_encapsulations_supported;
	/*
	 * NIC global maximum for unique UDP tunnel ports shared by all
	 * functions.
	 */
	uint32_t		enc_tunnel_config_udp_entries_max;
	/* External port identifier */
	uint8_t			enc_external_port;
	uint32_t		enc_mcdi_max_payload_length;
	/* VPD may be per-PF or global */
	boolean_t		enc_vpd_is_global;
	/* Minimum unidirectional bandwidth in Mb/s to max out all ports */
	uint32_t		enc_required_pcie_bandwidth_mbps;
	uint32_t		enc_max_pcie_link_gen;
	/* Firmware verifies integrity of NVRAM updates */
	uint32_t		enc_nvram_update_verify_result_supported;
	/* Firmware support for extended MAC_STATS buffer */
	uint32_t		enc_mac_stats_nstats;
	boolean_t		enc_fec_counters;
	boolean_t		enc_hlb_counters;
	/* Firmware support for "FLAG" and "MARK" filter actions */
	boolean_t		enc_filter_action_flag_supported;
	boolean_t		enc_filter_action_mark_supported;
	uint32_t		enc_filter_action_mark_max;
} efx_nic_cfg_t;

#define	EFX_PCI_FUNCTION_IS_PF(_encp)	((_encp)->enc_vf == 0xffff)
#define	EFX_PCI_FUNCTION_IS_VF(_encp)	((_encp)->enc_vf != 0xffff)

#define	EFX_PCI_FUNCTION(_encp)	\
	(EFX_PCI_FUNCTION_IS_PF(_encp) ? (_encp)->enc_pf : (_encp)->enc_vf)

#define	EFX_PCI_VF_PARENT(_encp)	((_encp)->enc_pf)

extern			const efx_nic_cfg_t *
efx_nic_cfg_get(
	__in		efx_nic_t *enp);

/* RxDPCPU firmware id values by which FW variant can be identified */
#define	EFX_RXDP_FULL_FEATURED_FW_ID	0x0
#define	EFX_RXDP_LOW_LATENCY_FW_ID	0x1
#define	EFX_RXDP_PACKED_STREAM_FW_ID	0x2
#define	EFX_RXDP_RULES_ENGINE_FW_ID	0x5
#define	EFX_RXDP_DPDK_FW_ID		0x6

typedef struct efx_nic_fw_info_s {
	/* Basic FW version information */
	uint16_t	enfi_mc_fw_version[4];
	/*
	 * If datapath capabilities can be detected,
	 * additional FW information is to be shown
	 */
	boolean_t	enfi_dpcpu_fw_ids_valid;
	/* Rx and Tx datapath CPU FW IDs */
	uint16_t	enfi_rx_dpcpu_fw_id;
	uint16_t	enfi_tx_dpcpu_fw_id;
} efx_nic_fw_info_t;

extern	__checkReturn		efx_rc_t
efx_nic_get_fw_version(
	__in			efx_nic_t *enp,
	__out			efx_nic_fw_info_t *enfip);

/* Driver resource limits (minimum required/maximum usable). */
typedef struct efx_drv_limits_s {
	uint32_t	edl_min_evq_count;
	uint32_t	edl_max_evq_count;

	uint32_t	edl_min_rxq_count;
	uint32_t	edl_max_rxq_count;

	uint32_t	edl_min_txq_count;
	uint32_t	edl_max_txq_count;

	/* PIO blocks (sub-allocated from piobuf) */
	uint32_t	edl_min_pio_alloc_size;
	uint32_t	edl_max_pio_alloc_count;
} efx_drv_limits_t;

extern	__checkReturn	efx_rc_t
efx_nic_set_drv_limits(
	__inout		efx_nic_t *enp,
	__in		efx_drv_limits_t *edlp);

typedef enum efx_nic_region_e {
	EFX_REGION_VI,			/* Memory BAR UC mapping */
	EFX_REGION_PIO_WRITE_VI,	/* Memory BAR WC mapping */
} efx_nic_region_t;

extern	__checkReturn	efx_rc_t
efx_nic_get_bar_region(
	__in		efx_nic_t *enp,
	__in		efx_nic_region_t region,
	__out		uint32_t *offsetp,
	__out		size_t *sizep);

extern	__checkReturn	efx_rc_t
efx_nic_get_vi_pool(
	__in		efx_nic_t *enp,
	__out		uint32_t *evq_countp,
	__out		uint32_t *rxq_countp,
	__out		uint32_t *txq_countp);


#if EFSYS_OPT_VPD

typedef enum efx_vpd_tag_e {
	EFX_VPD_ID = 0x02,
	EFX_VPD_END = 0x0f,
	EFX_VPD_RO = 0x10,
	EFX_VPD_RW = 0x11,
} efx_vpd_tag_t;

typedef uint16_t efx_vpd_keyword_t;

typedef struct efx_vpd_value_s {
	efx_vpd_tag_t		evv_tag;
	efx_vpd_keyword_t	evv_keyword;
	uint8_t			evv_length;
	uint8_t			evv_value[0x100];
} efx_vpd_value_t;


#define	EFX_VPD_KEYWORD(x, y) ((x) | ((y) << 8))

extern	__checkReturn		efx_rc_t
efx_vpd_init(
	__in			efx_nic_t *enp);

extern	__checkReturn		efx_rc_t
efx_vpd_size(
	__in			efx_nic_t *enp,
	__out			size_t *sizep);

extern	__checkReturn		efx_rc_t
efx_vpd_read(
	__in			efx_nic_t *enp,
	__out_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
efx_vpd_verify(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
efx_vpd_reinit(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
efx_vpd_get(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__inout			efx_vpd_value_t *evvp);

extern	__checkReturn		efx_rc_t
efx_vpd_set(
	__in			efx_nic_t *enp,
	__inout_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_value_t *evvp);

extern	__checkReturn		efx_rc_t
efx_vpd_next(
	__in			efx_nic_t *enp,
	__inout_bcount(size)	caddr_t data,
	__in			size_t size,
	__out			efx_vpd_value_t *evvp,
	__inout			unsigned int *contp);

extern	__checkReturn		efx_rc_t
efx_vpd_write(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

extern				void
efx_vpd_fini(
	__in			efx_nic_t *enp);

#endif	/* EFSYS_OPT_VPD */

/* NVRAM */

#if EFSYS_OPT_NVRAM

typedef enum efx_nvram_type_e {
	EFX_NVRAM_INVALID = 0,
	EFX_NVRAM_BOOTROM,
	EFX_NVRAM_BOOTROM_CFG,
	EFX_NVRAM_MC_FIRMWARE,
	EFX_NVRAM_MC_GOLDEN,
	EFX_NVRAM_PHY,
	EFX_NVRAM_NULLPHY,
	EFX_NVRAM_FPGA,
	EFX_NVRAM_FCFW,
	EFX_NVRAM_CPLD,
	EFX_NVRAM_FPGA_BACKUP,
	EFX_NVRAM_DYNAMIC_CFG,
	EFX_NVRAM_LICENSE,
	EFX_NVRAM_UEFIROM,
	EFX_NVRAM_MUM_FIRMWARE,
	EFX_NVRAM_DYNCONFIG_DEFAULTS,
	EFX_NVRAM_ROMCONFIG_DEFAULTS,
	EFX_NVRAM_NTYPES,
} efx_nvram_type_t;

extern	__checkReturn		efx_rc_t
efx_nvram_init(
	__in			efx_nic_t *enp);

#if EFSYS_OPT_DIAG

extern	__checkReturn		efx_rc_t
efx_nvram_test(
	__in			efx_nic_t *enp);

#endif	/* EFSYS_OPT_DIAG */

extern	__checkReturn		efx_rc_t
efx_nvram_size(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__out			size_t *sizep);

extern	__checkReturn		efx_rc_t
efx_nvram_rw_start(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__out_opt		size_t *pref_chunkp);

extern	__checkReturn		efx_rc_t
efx_nvram_rw_finish(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__out_opt		uint32_t *verify_resultp);

extern	__checkReturn		efx_rc_t
efx_nvram_get_version(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__out			uint32_t *subtypep,
	__out_ecount(4)		uint16_t version[4]);

extern	__checkReturn		efx_rc_t
efx_nvram_read_chunk(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__in			unsigned int offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
efx_nvram_read_backup(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__in			unsigned int offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
efx_nvram_set_version(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__in_ecount(4)		uint16_t version[4]);

extern	__checkReturn		efx_rc_t
efx_nvram_validate(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__in_bcount(partn_size)	caddr_t partn_data,
	__in			size_t partn_size);

extern	 __checkReturn		efx_rc_t
efx_nvram_erase(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type);

extern	__checkReturn		efx_rc_t
efx_nvram_write_chunk(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__in			unsigned int offset,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

extern				void
efx_nvram_fini(
	__in			efx_nic_t *enp);

#endif	/* EFSYS_OPT_NVRAM */

#if EFSYS_OPT_BOOTCFG

/* Report size and offset of bootcfg sector in NVRAM partition. */
extern	__checkReturn		efx_rc_t
efx_bootcfg_sector_info(
	__in			efx_nic_t *enp,
	__in			uint32_t pf,
	__out_opt		uint32_t *sector_countp,
	__out			size_t *offsetp,
	__out			size_t *max_sizep);

/*
 * Copy bootcfg sector data to a target buffer which may differ in size.
 * Optionally corrects format errors in source buffer.
 */
extern				efx_rc_t
efx_bootcfg_copy_sector(
	__in			efx_nic_t *enp,
	__inout_bcount(sector_length)
				uint8_t *sector,
	__in			size_t sector_length,
	__out_bcount(data_size)	uint8_t *data,
	__in			size_t data_size,
	__in			boolean_t handle_format_errors);

extern				efx_rc_t
efx_bootcfg_read(
	__in			efx_nic_t *enp,
	__out_bcount(size)	uint8_t *data,
	__in			size_t size);

extern				efx_rc_t
efx_bootcfg_write(
	__in			efx_nic_t *enp,
	__in_bcount(size)	uint8_t *data,
	__in			size_t size);


/*
 * Processing routines for buffers arranged in the DHCP/BOOTP option format
 * (see https://tools.ietf.org/html/rfc1533)
 *
 * Summarising the format: the buffer is a sequence of options. All options
 * begin with a tag octet, which uniquely identifies the option.  Fixed-
 * length options without data consist of only a tag octet.  Only options PAD
 * (0) and END (255) are fixed length.  All other options are variable-length
 * with a length octet following the tag octet.  The value of the length
 * octet does not include the two octets specifying the tag and length.  The
 * length octet is followed by "length" octets of data.
 *
 * Option data may be a sequence of sub-options in the same format. The data
 * content of the encapsulating option is one or more encapsulated sub-options,
 * with no terminating END tag is required.
 *
 * To be valid, the top-level sequence of options should be terminated by an
 * END tag. The buffer should be padded with the PAD byte.
 *
 * When stored to NVRAM, the DHCP option format buffer is preceded by a
 * checksum octet. The full buffer (including after the END tag) contributes
 * to the checksum, hence the need to fill the buffer to the end with PAD.
 */

#define	EFX_DHCP_END ((uint8_t)0xff)
#define	EFX_DHCP_PAD ((uint8_t)0)

#define	EFX_DHCP_ENCAP_OPT(encapsulator, encapsulated) \
  (uint16_t)(((encapsulator) << 8) | (encapsulated))

extern	__checkReturn		uint8_t
efx_dhcp_csum(
	__in_bcount(size)	uint8_t const *data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
efx_dhcp_verify(
	__in_bcount(size)	uint8_t const *data,
	__in			size_t size,
	__out_opt		size_t *usedp);

extern	__checkReturn	efx_rc_t
efx_dhcp_find_tag(
	__in_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt,
	__deref_out			uint8_t **valuepp,
	__out				size_t *value_lengthp);

extern	__checkReturn	efx_rc_t
efx_dhcp_find_end(
	__in_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__deref_out			uint8_t **endpp);


extern	__checkReturn	efx_rc_t
efx_dhcp_delete_tag(
	__inout_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt);

extern	__checkReturn	efx_rc_t
efx_dhcp_add_tag(
	__inout_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt,
	__in_bcount_opt(value_length)	uint8_t *valuep,
	__in				size_t value_length);

extern	__checkReturn	efx_rc_t
efx_dhcp_update_tag(
	__inout_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt,
	__in				uint8_t *value_locationp,
	__in_bcount_opt(value_length)	uint8_t *valuep,
	__in				size_t value_length);


#endif	/* EFSYS_OPT_BOOTCFG */

#if EFSYS_OPT_IMAGE_LAYOUT

#include "ef10_signed_image_layout.h"

/*
 * Image header used in unsigned and signed image layouts (see SF-102785-PS).
 *
 * NOTE:
 * The image header format is extensible. However, older drivers require an
 * exact match of image header version and header length when validating and
 * writing firmware images.
 *
 * To avoid breaking backward compatibility, we use the upper bits of the
 * controller version fields to contain an extra version number used for
 * combined bootROM and UEFI ROM images on EF10 and later (to hold the UEFI ROM
 * version). See bug39254 and SF-102785-PS for details.
 */
typedef struct efx_image_header_s {
	uint32_t	eih_magic;
	uint32_t	eih_version;
	uint32_t	eih_type;
	uint32_t	eih_subtype;
	uint32_t	eih_code_size;
	uint32_t	eih_size;
	union {
		uint32_t	eih_controller_version_min;
		struct {
			uint16_t	eih_controller_version_min_short;
			uint8_t		eih_extra_version_a;
			uint8_t		eih_extra_version_b;
		};
	};
	union {
		uint32_t	eih_controller_version_max;
		struct {
			uint16_t	eih_controller_version_max_short;
			uint8_t		eih_extra_version_c;
			uint8_t		eih_extra_version_d;
		};
	};
	uint16_t	eih_code_version_a;
	uint16_t	eih_code_version_b;
	uint16_t	eih_code_version_c;
	uint16_t	eih_code_version_d;
} efx_image_header_t;

#define	EFX_IMAGE_HEADER_SIZE		(40)
#define	EFX_IMAGE_HEADER_VERSION	(4)
#define	EFX_IMAGE_HEADER_MAGIC		(0x106F1A5)


typedef struct efx_image_trailer_s {
	uint32_t	eit_crc;
} efx_image_trailer_t;

#define	EFX_IMAGE_TRAILER_SIZE		(4)

typedef enum efx_image_format_e {
	EFX_IMAGE_FORMAT_NO_IMAGE,
	EFX_IMAGE_FORMAT_INVALID,
	EFX_IMAGE_FORMAT_UNSIGNED,
	EFX_IMAGE_FORMAT_SIGNED,
} efx_image_format_t;

typedef struct efx_image_info_s {
	efx_image_format_t	eii_format;
	uint8_t *		eii_imagep;
	size_t			eii_image_size;
	efx_image_header_t *	eii_headerp;
} efx_image_info_t;

extern	__checkReturn	efx_rc_t
efx_check_reflash_image(
	__in		void			*bufferp,
	__in		uint32_t		buffer_size,
	__out		efx_image_info_t	*infop);

extern	__checkReturn	efx_rc_t
efx_build_signed_image_write_buffer(
	__out_bcount(buffer_size)
			uint8_t			*bufferp,
	__in		uint32_t		buffer_size,
	__in		efx_image_info_t	*infop,
	__out		efx_image_header_t	**headerpp);

#endif	/* EFSYS_OPT_IMAGE_LAYOUT */

#if EFSYS_OPT_DIAG

typedef enum efx_pattern_type_t {
	EFX_PATTERN_BYTE_INCREMENT = 0,
	EFX_PATTERN_ALL_THE_SAME,
	EFX_PATTERN_BIT_ALTERNATE,
	EFX_PATTERN_BYTE_ALTERNATE,
	EFX_PATTERN_BYTE_CHANGING,
	EFX_PATTERN_BIT_SWEEP,
	EFX_PATTERN_NTYPES
} efx_pattern_type_t;

typedef			void
(*efx_sram_pattern_fn_t)(
	__in		size_t row,
	__in		boolean_t negate,
	__out		efx_qword_t *eqp);

extern	__checkReturn	efx_rc_t
efx_sram_test(
	__in		efx_nic_t *enp,
	__in		efx_pattern_type_t type);

#endif	/* EFSYS_OPT_DIAG */

extern	__checkReturn	efx_rc_t
efx_sram_buf_tbl_set(
	__in		efx_nic_t *enp,
	__in		uint32_t id,
	__in		efsys_mem_t *esmp,
	__in		size_t n);

extern		void
efx_sram_buf_tbl_clear(
	__in	efx_nic_t *enp,
	__in	uint32_t id,
	__in	size_t n);

#define	EFX_BUF_TBL_SIZE	0x20000

#define	EFX_BUF_SIZE		4096

/* EV */

typedef struct efx_evq_s	efx_evq_t;

#if EFSYS_OPT_QSTATS

/* START MKCONFIG GENERATED EfxHeaderEventQueueBlock 6f3843f5fe7cc843 */
typedef enum efx_ev_qstat_e {
	EV_ALL,
	EV_RX,
	EV_RX_OK,
	EV_RX_FRM_TRUNC,
	EV_RX_TOBE_DISC,
	EV_RX_PAUSE_FRM_ERR,
	EV_RX_BUF_OWNER_ID_ERR,
	EV_RX_IPV4_HDR_CHKSUM_ERR,
	EV_RX_TCP_UDP_CHKSUM_ERR,
	EV_RX_ETH_CRC_ERR,
	EV_RX_IP_FRAG_ERR,
	EV_RX_MCAST_PKT,
	EV_RX_MCAST_HASH_MATCH,
	EV_RX_TCP_IPV4,
	EV_RX_TCP_IPV6,
	EV_RX_UDP_IPV4,
	EV_RX_UDP_IPV6,
	EV_RX_OTHER_IPV4,
	EV_RX_OTHER_IPV6,
	EV_RX_NON_IP,
	EV_RX_BATCH,
	EV_TX,
	EV_TX_WQ_FF_FULL,
	EV_TX_PKT_ERR,
	EV_TX_PKT_TOO_BIG,
	EV_TX_UNEXPECTED,
	EV_GLOBAL,
	EV_GLOBAL_MNT,
	EV_DRIVER,
	EV_DRIVER_SRM_UPD_DONE,
	EV_DRIVER_TX_DESCQ_FLS_DONE,
	EV_DRIVER_RX_DESCQ_FLS_DONE,
	EV_DRIVER_RX_DESCQ_FLS_FAILED,
	EV_DRIVER_RX_DSC_ERROR,
	EV_DRIVER_TX_DSC_ERROR,
	EV_DRV_GEN,
	EV_MCDI_RESPONSE,
	EV_NQSTATS
} efx_ev_qstat_t;

/* END MKCONFIG GENERATED EfxHeaderEventQueueBlock */

#endif	/* EFSYS_OPT_QSTATS */

extern	__checkReturn	efx_rc_t
efx_ev_init(
	__in		efx_nic_t *enp);

extern		void
efx_ev_fini(
	__in		efx_nic_t *enp);

#define	EFX_EVQ_MAXNEVS		32768
#define	EFX_EVQ_MINNEVS		512

#define	EFX_EVQ_SIZE(_nevs)	((_nevs) * sizeof (efx_qword_t))
#define	EFX_EVQ_NBUFS(_nevs)	(EFX_EVQ_SIZE(_nevs) / EFX_BUF_SIZE)

#define	EFX_EVQ_FLAGS_TYPE_MASK		(0x3)
#define	EFX_EVQ_FLAGS_TYPE_AUTO		(0x0)
#define	EFX_EVQ_FLAGS_TYPE_THROUGHPUT	(0x1)
#define	EFX_EVQ_FLAGS_TYPE_LOW_LATENCY	(0x2)

#define	EFX_EVQ_FLAGS_NOTIFY_MASK	(0xC)
#define	EFX_EVQ_FLAGS_NOTIFY_INTERRUPT	(0x0)	/* Interrupting (default) */
#define	EFX_EVQ_FLAGS_NOTIFY_DISABLED	(0x4)	/* Non-interrupting */

extern	__checkReturn	efx_rc_t
efx_ev_qcreate(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		efsys_mem_t *esmp,
	__in		size_t ndescs,
	__in		uint32_t id,
	__in		uint32_t us,
	__in		uint32_t flags,
	__deref_out	efx_evq_t **eepp);

extern		void
efx_ev_qpost(
	__in		efx_evq_t *eep,
	__in		uint16_t data);

typedef __checkReturn	boolean_t
(*efx_initialized_ev_t)(
	__in_opt	void *arg);

#define	EFX_PKT_UNICAST		0x0004
#define	EFX_PKT_START		0x0008

#define	EFX_PKT_VLAN_TAGGED	0x0010
#define	EFX_CKSUM_TCPUDP	0x0020
#define	EFX_CKSUM_IPV4		0x0040
#define	EFX_PKT_CONT		0x0080

#define	EFX_CHECK_VLAN		0x0100
#define	EFX_PKT_TCP		0x0200
#define	EFX_PKT_UDP		0x0400
#define	EFX_PKT_IPV4		0x0800

#define	EFX_PKT_IPV6		0x1000
#define	EFX_PKT_PREFIX_LEN	0x2000
#define	EFX_ADDR_MISMATCH	0x4000
#define	EFX_DISCARD		0x8000

/*
 * The following flags are used only for packed stream
 * mode. The values for the flags are reused to fit into 16 bit,
 * since EFX_PKT_START and EFX_PKT_CONT are never used in
 * packed stream mode
 */
#define	EFX_PKT_PACKED_STREAM_NEW_BUFFER	EFX_PKT_START
#define	EFX_PKT_PACKED_STREAM_PARSE_INCOMPLETE	EFX_PKT_CONT


#define	EFX_EV_RX_NLABELS	32
#define	EFX_EV_TX_NLABELS	32

typedef	__checkReturn	boolean_t
(*efx_rx_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t label,
	__in		uint32_t id,
	__in		uint32_t size,
	__in		uint16_t flags);

#if EFSYS_OPT_RX_PACKED_STREAM || EFSYS_OPT_RX_ES_SUPER_BUFFER

/*
 * Packed stream mode is documented in SF-112241-TC.
 * The general idea is that, instead of putting each incoming
 * packet into a separate buffer which is specified in a RX
 * descriptor, a large buffer is provided to the hardware and
 * packets are put there in a continuous stream.
 * The main advantage of such an approach is that RX queue refilling
 * happens much less frequently.
 *
 * Equal stride packed stream mode is documented in SF-119419-TC.
 * The general idea is to utilize advantages of the packed stream,
 * but avoid indirection in packets representation.
 * The main advantage of such an approach is that RX queue refilling
 * happens much less frequently and packets buffers are independent
 * from upper layers point of view.
 */

typedef	__checkReturn	boolean_t
(*efx_rx_ps_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t label,
	__in		uint32_t id,
	__in		uint32_t pkt_count,
	__in		uint16_t flags);

#endif

typedef	__checkReturn	boolean_t
(*efx_tx_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t label,
	__in		uint32_t id);

#define	EFX_EXCEPTION_RX_RECOVERY	0x00000001
#define	EFX_EXCEPTION_RX_DSC_ERROR	0x00000002
#define	EFX_EXCEPTION_TX_DSC_ERROR	0x00000003
#define	EFX_EXCEPTION_UNKNOWN_SENSOREVT	0x00000004
#define	EFX_EXCEPTION_FWALERT_SRAM	0x00000005
#define	EFX_EXCEPTION_UNKNOWN_FWALERT	0x00000006
#define	EFX_EXCEPTION_RX_ERROR		0x00000007
#define	EFX_EXCEPTION_TX_ERROR		0x00000008
#define	EFX_EXCEPTION_EV_ERROR		0x00000009

typedef	__checkReturn	boolean_t
(*efx_exception_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t label,
	__in		uint32_t data);

typedef	__checkReturn	boolean_t
(*efx_rxq_flush_done_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t rxq_index);

typedef	__checkReturn	boolean_t
(*efx_rxq_flush_failed_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t rxq_index);

typedef	__checkReturn	boolean_t
(*efx_txq_flush_done_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t txq_index);

typedef	__checkReturn	boolean_t
(*efx_software_ev_t)(
	__in_opt	void *arg,
	__in		uint16_t magic);

typedef	__checkReturn	boolean_t
(*efx_sram_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t code);

#define	EFX_SRAM_CLEAR		0
#define	EFX_SRAM_UPDATE		1
#define	EFX_SRAM_ILLEGAL_CLEAR	2

typedef	__checkReturn	boolean_t
(*efx_wake_up_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t label);

typedef	__checkReturn	boolean_t
(*efx_timer_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t label);

typedef __checkReturn	boolean_t
(*efx_link_change_ev_t)(
	__in_opt	void *arg,
	__in		efx_link_mode_t	link_mode);

#if EFSYS_OPT_MON_STATS

typedef __checkReturn	boolean_t
(*efx_monitor_ev_t)(
	__in_opt	void *arg,
	__in		efx_mon_stat_t id,
	__in		efx_mon_stat_value_t value);

#endif	/* EFSYS_OPT_MON_STATS */

#if EFSYS_OPT_MAC_STATS

typedef __checkReturn	boolean_t
(*efx_mac_stats_ev_t)(
	__in_opt	void *arg,
	__in		uint32_t generation);

#endif	/* EFSYS_OPT_MAC_STATS */

typedef struct efx_ev_callbacks_s {
	efx_initialized_ev_t		eec_initialized;
	efx_rx_ev_t			eec_rx;
#if EFSYS_OPT_RX_PACKED_STREAM || EFSYS_OPT_RX_ES_SUPER_BUFFER
	efx_rx_ps_ev_t			eec_rx_ps;
#endif
	efx_tx_ev_t			eec_tx;
	efx_exception_ev_t		eec_exception;
	efx_rxq_flush_done_ev_t		eec_rxq_flush_done;
	efx_rxq_flush_failed_ev_t	eec_rxq_flush_failed;
	efx_txq_flush_done_ev_t		eec_txq_flush_done;
	efx_software_ev_t		eec_software;
	efx_sram_ev_t			eec_sram;
	efx_wake_up_ev_t		eec_wake_up;
	efx_timer_ev_t			eec_timer;
	efx_link_change_ev_t		eec_link_change;
#if EFSYS_OPT_MON_STATS
	efx_monitor_ev_t		eec_monitor;
#endif	/* EFSYS_OPT_MON_STATS */
#if EFSYS_OPT_MAC_STATS
	efx_mac_stats_ev_t		eec_mac_stats;
#endif	/* EFSYS_OPT_MAC_STATS */
} efx_ev_callbacks_t;

extern	__checkReturn	boolean_t
efx_ev_qpending(
	__in		efx_evq_t *eep,
	__in		unsigned int count);

#if EFSYS_OPT_EV_PREFETCH

extern			void
efx_ev_qprefetch(
	__in		efx_evq_t *eep,
	__in		unsigned int count);

#endif	/* EFSYS_OPT_EV_PREFETCH */

extern			void
efx_ev_qpoll(
	__in		efx_evq_t *eep,
	__inout		unsigned int *countp,
	__in		const efx_ev_callbacks_t *eecp,
	__in_opt	void *arg);

extern	__checkReturn	efx_rc_t
efx_ev_usecs_to_ticks(
	__in		efx_nic_t *enp,
	__in		unsigned int usecs,
	__out		unsigned int *ticksp);

extern	__checkReturn	efx_rc_t
efx_ev_qmoderate(
	__in		efx_evq_t *eep,
	__in		unsigned int us);

extern	__checkReturn	efx_rc_t
efx_ev_qprime(
	__in		efx_evq_t *eep,
	__in		unsigned int count);

#if EFSYS_OPT_QSTATS

#if EFSYS_OPT_NAMES

extern		const char *
efx_ev_qstat_name(
	__in	efx_nic_t *enp,
	__in	unsigned int id);

#endif	/* EFSYS_OPT_NAMES */

extern					void
efx_ev_qstats_update(
	__in				efx_evq_t *eep,
	__inout_ecount(EV_NQSTATS)	efsys_stat_t *stat);

#endif	/* EFSYS_OPT_QSTATS */

extern		void
efx_ev_qdestroy(
	__in	efx_evq_t *eep);

/* RX */

extern	__checkReturn	efx_rc_t
efx_rx_init(
	__inout		efx_nic_t *enp);

extern		void
efx_rx_fini(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_RX_SCATTER
	__checkReturn	efx_rc_t
efx_rx_scatter_enable(
	__in		efx_nic_t *enp,
	__in		unsigned int buf_size);
#endif	/* EFSYS_OPT_RX_SCATTER */

/* Handle to represent use of the default RSS context. */
#define	EFX_RSS_CONTEXT_DEFAULT	0xffffffff

#if EFSYS_OPT_RX_SCALE

typedef enum efx_rx_hash_alg_e {
	EFX_RX_HASHALG_LFSR = 0,
	EFX_RX_HASHALG_TOEPLITZ,
	EFX_RX_HASHALG_PACKED_STREAM,
	EFX_RX_NHASHALGS
} efx_rx_hash_alg_t;

/*
 * Legacy hash type flags.
 *
 * They represent standard tuples for distinct traffic classes.
 */
#define	EFX_RX_HASH_IPV4	(1U << 0)
#define	EFX_RX_HASH_TCPIPV4	(1U << 1)
#define	EFX_RX_HASH_IPV6	(1U << 2)
#define	EFX_RX_HASH_TCPIPV6	(1U << 3)

#define	EFX_RX_HASH_LEGACY_MASK		\
	(EFX_RX_HASH_IPV4	|	\
	EFX_RX_HASH_TCPIPV4	|	\
	EFX_RX_HASH_IPV6	|	\
	EFX_RX_HASH_TCPIPV6)

/*
 * The type of the argument used by efx_rx_scale_mode_set() to
 * provide a means for the client drivers to configure hashing.
 *
 * A properly constructed value can either be:
 *  - a combination of legacy flags
 *  - a combination of EFX_RX_HASH() flags
 */
typedef uint32_t efx_rx_hash_type_t;

typedef enum efx_rx_hash_support_e {
	EFX_RX_HASH_UNAVAILABLE = 0,	/* Hardware hash not inserted */
	EFX_RX_HASH_AVAILABLE		/* Insert hash with/without RSS */
} efx_rx_hash_support_t;

#define	EFX_RSS_KEY_SIZE	40	/* RSS key size (bytes) */
#define	EFX_RSS_TBL_SIZE	128	/* Rows in RX indirection table */
#define	EFX_MAXRSS		64	/* RX indirection entry range */
#define	EFX_MAXRSS_LEGACY	16	/* See bug16611 and bug17213 */

typedef enum efx_rx_scale_context_type_e {
	EFX_RX_SCALE_UNAVAILABLE = 0,	/* No RX scale context */
	EFX_RX_SCALE_EXCLUSIVE,		/* Writable key/indirection table */
	EFX_RX_SCALE_SHARED		/* Read-only key/indirection table */
} efx_rx_scale_context_type_t;

/*
 * Traffic classes eligible for hash computation.
 *
 * Select packet headers used in computing the receive hash.
 * This uses the same encoding as the RSS_MODES field of
 * MC_CMD_RSS_CONTEXT_SET_FLAGS.
 */
#define	EFX_RX_CLASS_IPV4_TCP_LBN	8
#define	EFX_RX_CLASS_IPV4_TCP_WIDTH	4
#define	EFX_RX_CLASS_IPV4_UDP_LBN	12
#define	EFX_RX_CLASS_IPV4_UDP_WIDTH	4
#define	EFX_RX_CLASS_IPV4_LBN		16
#define	EFX_RX_CLASS_IPV4_WIDTH		4
#define	EFX_RX_CLASS_IPV6_TCP_LBN	20
#define	EFX_RX_CLASS_IPV6_TCP_WIDTH	4
#define	EFX_RX_CLASS_IPV6_UDP_LBN	24
#define	EFX_RX_CLASS_IPV6_UDP_WIDTH	4
#define	EFX_RX_CLASS_IPV6_LBN		28
#define	EFX_RX_CLASS_IPV6_WIDTH		4

#define	EFX_RX_NCLASSES			6

/*
 * Ancillary flags used to construct generic hash tuples.
 * This uses the same encoding as RSS_MODE_HASH_SELECTOR.
 */
#define	EFX_RX_CLASS_HASH_SRC_ADDR	(1U << 0)
#define	EFX_RX_CLASS_HASH_DST_ADDR	(1U << 1)
#define	EFX_RX_CLASS_HASH_SRC_PORT	(1U << 2)
#define	EFX_RX_CLASS_HASH_DST_PORT	(1U << 3)

/*
 * Generic hash tuples.
 *
 * They express combinations of packet fields
 * which can contribute to the hash value for
 * a particular traffic class.
 */
#define	EFX_RX_CLASS_HASH_DISABLE	0

#define	EFX_RX_CLASS_HASH_1TUPLE_SRC	EFX_RX_CLASS_HASH_SRC_ADDR
#define	EFX_RX_CLASS_HASH_1TUPLE_DST	EFX_RX_CLASS_HASH_DST_ADDR

#define	EFX_RX_CLASS_HASH_2TUPLE		\
	(EFX_RX_CLASS_HASH_SRC_ADDR	|	\
	EFX_RX_CLASS_HASH_DST_ADDR)

#define	EFX_RX_CLASS_HASH_2TUPLE_SRC		\
	(EFX_RX_CLASS_HASH_SRC_ADDR	|	\
	EFX_RX_CLASS_HASH_SRC_PORT)

#define	EFX_RX_CLASS_HASH_2TUPLE_DST		\
	(EFX_RX_CLASS_HASH_DST_ADDR	|	\
	EFX_RX_CLASS_HASH_DST_PORT)

#define	EFX_RX_CLASS_HASH_4TUPLE		\
	(EFX_RX_CLASS_HASH_SRC_ADDR	|	\
	EFX_RX_CLASS_HASH_DST_ADDR	|	\
	EFX_RX_CLASS_HASH_SRC_PORT	|	\
	EFX_RX_CLASS_HASH_DST_PORT)

#define EFX_RX_CLASS_HASH_NTUPLES	7

/*
 * Hash flag constructor.
 *
 * Resulting flags encode hash tuples for specific traffic classes.
 * The client drivers are encouraged to use these flags to form
 * a hash type value.
 */
#define	EFX_RX_HASH(_class, _tuple)				\
	EFX_INSERT_FIELD_NATIVE32(0, 31,			\
	EFX_RX_CLASS_##_class, EFX_RX_CLASS_HASH_##_tuple)

/*
 * The maximum number of EFX_RX_HASH() flags.
 */
#define	EFX_RX_HASH_NFLAGS	(EFX_RX_NCLASSES * EFX_RX_CLASS_HASH_NTUPLES)

extern	__checkReturn				efx_rc_t
efx_rx_scale_hash_flags_get(
	__in					efx_nic_t *enp,
	__in					efx_rx_hash_alg_t hash_alg,
	__out_ecount_part(max_nflags, *nflagsp)	unsigned int *flagsp,
	__in					unsigned int max_nflags,
	__out					unsigned int *nflagsp);

extern	__checkReturn	efx_rc_t
efx_rx_hash_default_support_get(
	__in		efx_nic_t *enp,
	__out		efx_rx_hash_support_t *supportp);


extern	__checkReturn	efx_rc_t
efx_rx_scale_default_support_get(
	__in		efx_nic_t *enp,
	__out		efx_rx_scale_context_type_t *typep);

extern	__checkReturn	efx_rc_t
efx_rx_scale_context_alloc(
	__in		efx_nic_t *enp,
	__in		efx_rx_scale_context_type_t type,
	__in		uint32_t num_queues,
	__out		uint32_t *rss_contextp);

extern	__checkReturn	efx_rc_t
efx_rx_scale_context_free(
	__in		efx_nic_t *enp,
	__in		uint32_t rss_context);

extern	__checkReturn	efx_rc_t
efx_rx_scale_mode_set(
	__in	efx_nic_t *enp,
	__in	uint32_t rss_context,
	__in	efx_rx_hash_alg_t alg,
	__in	efx_rx_hash_type_t type,
	__in	boolean_t insert);

extern	__checkReturn	efx_rc_t
efx_rx_scale_tbl_set(
	__in		efx_nic_t *enp,
	__in		uint32_t rss_context,
	__in_ecount(n)	unsigned int *table,
	__in		size_t n);

extern	__checkReturn	efx_rc_t
efx_rx_scale_key_set(
	__in		efx_nic_t *enp,
	__in		uint32_t rss_context,
	__in_ecount(n)	uint8_t *key,
	__in		size_t n);

extern	__checkReturn	uint32_t
efx_pseudo_hdr_hash_get(
	__in		efx_rxq_t *erp,
	__in		efx_rx_hash_alg_t func,
	__in		uint8_t *buffer);

#endif	/* EFSYS_OPT_RX_SCALE */

extern	__checkReturn	efx_rc_t
efx_pseudo_hdr_pkt_length_get(
	__in		efx_rxq_t *erp,
	__in		uint8_t *buffer,
	__out		uint16_t *pkt_lengthp);

#define	EFX_RXQ_MAXNDESCS		4096
#define	EFX_RXQ_MINNDESCS		512

#define	EFX_RXQ_SIZE(_ndescs)		((_ndescs) * sizeof (efx_qword_t))
#define	EFX_RXQ_NBUFS(_ndescs)		(EFX_RXQ_SIZE(_ndescs) / EFX_BUF_SIZE)
#define	EFX_RXQ_LIMIT(_ndescs)		((_ndescs) - 16)
#define	EFX_RXQ_DC_NDESCS(_dcsize)	(8 << _dcsize)

typedef enum efx_rxq_type_e {
	EFX_RXQ_TYPE_DEFAULT,
	EFX_RXQ_TYPE_PACKED_STREAM,
	EFX_RXQ_TYPE_ES_SUPER_BUFFER,
	EFX_RXQ_NTYPES
} efx_rxq_type_t;

/*
 * Dummy flag to be used instead of 0 to make it clear that the argument
 * is receive queue flags.
 */
#define	EFX_RXQ_FLAG_NONE		0x0
#define	EFX_RXQ_FLAG_SCATTER		0x1
/*
 * If tunnels are supported and Rx event can provide information about
 * either outer or inner packet classes (e.g. SFN8xxx adapters with
 * full-feature firmware variant running), outer classes are requested by
 * default. However, if the driver supports tunnels, the flag allows to
 * request inner classes which are required to be able to interpret inner
 * Rx checksum offload results.
 */
#define	EFX_RXQ_FLAG_INNER_CLASSES	0x2

extern	__checkReturn	efx_rc_t
efx_rx_qcreate(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		unsigned int label,
	__in		efx_rxq_type_t type,
	__in		efsys_mem_t *esmp,
	__in		size_t ndescs,
	__in		uint32_t id,
	__in		unsigned int flags,
	__in		efx_evq_t *eep,
	__deref_out	efx_rxq_t **erpp);

#if EFSYS_OPT_RX_PACKED_STREAM

#define	EFX_RXQ_PACKED_STREAM_BUF_SIZE_1M	(1U * 1024 * 1024)
#define	EFX_RXQ_PACKED_STREAM_BUF_SIZE_512K	(512U * 1024)
#define	EFX_RXQ_PACKED_STREAM_BUF_SIZE_256K	(256U * 1024)
#define	EFX_RXQ_PACKED_STREAM_BUF_SIZE_128K	(128U * 1024)
#define	EFX_RXQ_PACKED_STREAM_BUF_SIZE_64K	(64U * 1024)

extern	__checkReturn	efx_rc_t
efx_rx_qcreate_packed_stream(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		unsigned int label,
	__in		uint32_t ps_buf_size,
	__in		efsys_mem_t *esmp,
	__in		size_t ndescs,
	__in		efx_evq_t *eep,
	__deref_out	efx_rxq_t **erpp);

#endif

#if EFSYS_OPT_RX_ES_SUPER_BUFFER

/* Maximum head-of-line block timeout in nanoseconds */
#define	EFX_RXQ_ES_SUPER_BUFFER_HOL_BLOCK_MAX	(400U * 1000 * 1000)

extern	__checkReturn	efx_rc_t
efx_rx_qcreate_es_super_buffer(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		unsigned int label,
	__in		uint32_t n_bufs_per_desc,
	__in		uint32_t max_dma_len,
	__in		uint32_t buf_stride,
	__in		uint32_t hol_block_timeout,
	__in		efsys_mem_t *esmp,
	__in		size_t ndescs,
	__in		unsigned int flags,
	__in		efx_evq_t *eep,
	__deref_out	efx_rxq_t **erpp);

#endif

typedef struct efx_buffer_s {
	efsys_dma_addr_t	eb_addr;
	size_t			eb_size;
	boolean_t		eb_eop;
} efx_buffer_t;

typedef struct efx_desc_s {
	efx_qword_t ed_eq;
} efx_desc_t;

extern				void
efx_rx_qpost(
	__in			efx_rxq_t *erp,
	__in_ecount(ndescs)	efsys_dma_addr_t *addrp,
	__in			size_t size,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__in			unsigned int added);

extern		void
efx_rx_qpush(
	__in	efx_rxq_t *erp,
	__in	unsigned int added,
	__inout	unsigned int *pushedp);

#if EFSYS_OPT_RX_PACKED_STREAM

extern			void
efx_rx_qpush_ps_credits(
	__in		efx_rxq_t *erp);

extern	__checkReturn	uint8_t *
efx_rx_qps_packet_info(
	__in		efx_rxq_t *erp,
	__in		uint8_t *buffer,
	__in		uint32_t buffer_length,
	__in		uint32_t current_offset,
	__out		uint16_t *lengthp,
	__out		uint32_t *next_offsetp,
	__out		uint32_t *timestamp);
#endif

extern	__checkReturn	efx_rc_t
efx_rx_qflush(
	__in	efx_rxq_t *erp);

extern		void
efx_rx_qenable(
	__in	efx_rxq_t *erp);

extern		void
efx_rx_qdestroy(
	__in	efx_rxq_t *erp);

/* TX */

typedef struct efx_txq_s	efx_txq_t;

#if EFSYS_OPT_QSTATS

/* START MKCONFIG GENERATED EfxHeaderTransmitQueueBlock 12dff8778598b2db */
typedef enum efx_tx_qstat_e {
	TX_POST,
	TX_POST_PIO,
	TX_NQSTATS
} efx_tx_qstat_t;

/* END MKCONFIG GENERATED EfxHeaderTransmitQueueBlock */

#endif	/* EFSYS_OPT_QSTATS */

extern	__checkReturn	efx_rc_t
efx_tx_init(
	__in		efx_nic_t *enp);

extern		void
efx_tx_fini(
	__in	efx_nic_t *enp);

#define	EFX_TXQ_MINNDESCS		512

#define	EFX_TXQ_SIZE(_ndescs)		((_ndescs) * sizeof (efx_qword_t))
#define	EFX_TXQ_NBUFS(_ndescs)		(EFX_TXQ_SIZE(_ndescs) / EFX_BUF_SIZE)
#define	EFX_TXQ_LIMIT(_ndescs)		((_ndescs) - 16)

#define	EFX_TXQ_MAX_BUFS 8 /* Maximum independent of EFX_BUG35388_WORKAROUND. */

#define	EFX_TXQ_CKSUM_IPV4		0x0001
#define	EFX_TXQ_CKSUM_TCPUDP		0x0002
#define	EFX_TXQ_FATSOV2			0x0004
#define	EFX_TXQ_CKSUM_INNER_IPV4	0x0008
#define	EFX_TXQ_CKSUM_INNER_TCPUDP	0x0010

extern	__checkReturn	efx_rc_t
efx_tx_qcreate(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		unsigned int label,
	__in		efsys_mem_t *esmp,
	__in		size_t n,
	__in		uint32_t id,
	__in		uint16_t flags,
	__in		efx_evq_t *eep,
	__deref_out	efx_txq_t **etpp,
	__out		unsigned int *addedp);

extern	__checkReturn		efx_rc_t
efx_tx_qpost(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_buffer_t *eb,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp);

extern	__checkReturn	efx_rc_t
efx_tx_qpace(
	__in		efx_txq_t *etp,
	__in		unsigned int ns);

extern			void
efx_tx_qpush(
	__in		efx_txq_t *etp,
	__in		unsigned int added,
	__in		unsigned int pushed);

extern	__checkReturn	efx_rc_t
efx_tx_qflush(
	__in		efx_txq_t *etp);

extern			void
efx_tx_qenable(
	__in		efx_txq_t *etp);

extern	__checkReturn	efx_rc_t
efx_tx_qpio_enable(
	__in		efx_txq_t *etp);

extern			void
efx_tx_qpio_disable(
	__in		efx_txq_t *etp);

extern	__checkReturn	efx_rc_t
efx_tx_qpio_write(
	__in			efx_txq_t *etp,
	__in_ecount(buf_length)	uint8_t *buffer,
	__in			size_t buf_length,
	__in			size_t pio_buf_offset);

extern	__checkReturn	efx_rc_t
efx_tx_qpio_post(
	__in			efx_txq_t *etp,
	__in			size_t pkt_length,
	__in			unsigned int completed,
	__inout			unsigned int *addedp);

extern	__checkReturn	efx_rc_t
efx_tx_qdesc_post(
	__in		efx_txq_t *etp,
	__in_ecount(n)	efx_desc_t *ed,
	__in		unsigned int n,
	__in		unsigned int completed,
	__inout		unsigned int *addedp);

extern	void
efx_tx_qdesc_dma_create(
	__in	efx_txq_t *etp,
	__in	efsys_dma_addr_t addr,
	__in	size_t size,
	__in	boolean_t eop,
	__out	efx_desc_t *edp);

extern	void
efx_tx_qdesc_tso_create(
	__in	efx_txq_t *etp,
	__in	uint16_t ipv4_id,
	__in	uint32_t tcp_seq,
	__in	uint8_t  tcp_flags,
	__out	efx_desc_t *edp);

/* Number of FATSOv2 option descriptors */
#define	EFX_TX_FATSOV2_OPT_NDESCS		2

/* Maximum number of DMA segments per TSO packet (not superframe) */
#define	EFX_TX_FATSOV2_DMA_SEGS_PER_PKT_MAX	24

extern	void
efx_tx_qdesc_tso2_create(
	__in			efx_txq_t *etp,
	__in			uint16_t ipv4_id,
	__in			uint16_t outer_ipv4_id,
	__in			uint32_t tcp_seq,
	__in			uint16_t tcp_mss,
	__out_ecount(count)	efx_desc_t *edp,
	__in			int count);

extern	void
efx_tx_qdesc_vlantci_create(
	__in	efx_txq_t *etp,
	__in	uint16_t tci,
	__out	efx_desc_t *edp);

extern	void
efx_tx_qdesc_checksum_create(
	__in	efx_txq_t *etp,
	__in	uint16_t flags,
	__out	efx_desc_t *edp);

#if EFSYS_OPT_QSTATS

#if EFSYS_OPT_NAMES

extern		const char *
efx_tx_qstat_name(
	__in	efx_nic_t *etp,
	__in	unsigned int id);

#endif	/* EFSYS_OPT_NAMES */

extern					void
efx_tx_qstats_update(
	__in				efx_txq_t *etp,
	__inout_ecount(TX_NQSTATS)	efsys_stat_t *stat);

#endif	/* EFSYS_OPT_QSTATS */

extern		void
efx_tx_qdestroy(
	__in	efx_txq_t *etp);


/* FILTER */

#if EFSYS_OPT_FILTER

#define	EFX_ETHER_TYPE_IPV4 0x0800
#define	EFX_ETHER_TYPE_IPV6 0x86DD

#define	EFX_IPPROTO_TCP 6
#define	EFX_IPPROTO_UDP 17
#define	EFX_IPPROTO_GRE	47

/* Use RSS to spread across multiple queues */
#define	EFX_FILTER_FLAG_RX_RSS		0x01
/* Enable RX scatter */
#define	EFX_FILTER_FLAG_RX_SCATTER	0x02
/*
 * Override an automatic filter (priority EFX_FILTER_PRI_AUTO).
 * May only be set by the filter implementation for each type.
 * A removal request will restore the automatic filter in its place.
 */
#define	EFX_FILTER_FLAG_RX_OVER_AUTO	0x04
/* Filter is for RX */
#define	EFX_FILTER_FLAG_RX		0x08
/* Filter is for TX */
#define	EFX_FILTER_FLAG_TX		0x10
/* Set match flag on the received packet */
#define	EFX_FILTER_FLAG_ACTION_FLAG	0x20
/* Set match mark on the received packet */
#define	EFX_FILTER_FLAG_ACTION_MARK	0x40

typedef uint8_t efx_filter_flags_t;

/*
 * Flags which specify the fields to match on. The values are the same as in the
 * MC_CMD_FILTER_OP/MC_CMD_FILTER_OP_EXT commands.
 */

/* Match by remote IP host address */
#define	EFX_FILTER_MATCH_REM_HOST		0x00000001
/* Match by local IP host address */
#define	EFX_FILTER_MATCH_LOC_HOST		0x00000002
/* Match by remote MAC address */
#define	EFX_FILTER_MATCH_REM_MAC		0x00000004
/* Match by remote TCP/UDP port */
#define	EFX_FILTER_MATCH_REM_PORT		0x00000008
/* Match by remote TCP/UDP port */
#define	EFX_FILTER_MATCH_LOC_MAC		0x00000010
/* Match by local TCP/UDP port */
#define	EFX_FILTER_MATCH_LOC_PORT		0x00000020
/* Match by Ether-type */
#define	EFX_FILTER_MATCH_ETHER_TYPE		0x00000040
/* Match by inner VLAN ID */
#define	EFX_FILTER_MATCH_INNER_VID		0x00000080
/* Match by outer VLAN ID */
#define	EFX_FILTER_MATCH_OUTER_VID		0x00000100
/* Match by IP transport protocol */
#define	EFX_FILTER_MATCH_IP_PROTO		0x00000200
/* Match by VNI or VSID */
#define	EFX_FILTER_MATCH_VNI_OR_VSID		0x00000800
/* For encapsulated packets, match by inner frame local MAC address */
#define	EFX_FILTER_MATCH_IFRM_LOC_MAC		0x00010000
/* For encapsulated packets, match all multicast inner frames */
#define	EFX_FILTER_MATCH_IFRM_UNKNOWN_MCAST_DST	0x01000000
/* For encapsulated packets, match all unicast inner frames */
#define	EFX_FILTER_MATCH_IFRM_UNKNOWN_UCAST_DST	0x02000000
/*
 * Match by encap type, this flag does not correspond to
 * the MCDI match flags and any unoccupied value may be used
 */
#define	EFX_FILTER_MATCH_ENCAP_TYPE		0x20000000
/* Match otherwise-unmatched multicast and broadcast packets */
#define	EFX_FILTER_MATCH_UNKNOWN_MCAST_DST	0x40000000
/* Match otherwise-unmatched unicast packets */
#define	EFX_FILTER_MATCH_UNKNOWN_UCAST_DST	0x80000000

typedef uint32_t efx_filter_match_flags_t;

typedef enum efx_filter_priority_s {
	EFX_FILTER_PRI_HINT = 0,	/* Performance hint */
	EFX_FILTER_PRI_AUTO,		/* Automatic filter based on device
					 * address list or hardware
					 * requirements. This may only be used
					 * by the filter implementation for
					 * each NIC type. */
	EFX_FILTER_PRI_MANUAL,		/* Manually configured filter */
	EFX_FILTER_PRI_REQUIRED,	/* Required for correct behaviour of the
					 * client (e.g. SR-IOV, HyperV VMQ etc.)
					 */
} efx_filter_priority_t;

/*
 * FIXME: All these fields are assumed to be in little-endian byte order.
 * It may be better for some to be big-endian. See bug42804.
 */

typedef struct efx_filter_spec_s {
	efx_filter_match_flags_t	efs_match_flags;
	uint8_t				efs_priority;
	efx_filter_flags_t		efs_flags;
	uint16_t			efs_dmaq_id;
	uint32_t			efs_rss_context;
	uint32_t			efs_mark;
	/* Fields below here are hashed for software filter lookup */
	uint16_t			efs_outer_vid;
	uint16_t			efs_inner_vid;
	uint8_t				efs_loc_mac[EFX_MAC_ADDR_LEN];
	uint8_t				efs_rem_mac[EFX_MAC_ADDR_LEN];
	uint16_t			efs_ether_type;
	uint8_t				efs_ip_proto;
	efx_tunnel_protocol_t		efs_encap_type;
	uint16_t			efs_loc_port;
	uint16_t			efs_rem_port;
	efx_oword_t			efs_rem_host;
	efx_oword_t			efs_loc_host;
	uint8_t				efs_vni_or_vsid[EFX_VNI_OR_VSID_LEN];
	uint8_t				efs_ifrm_loc_mac[EFX_MAC_ADDR_LEN];
} efx_filter_spec_t;


/* Default values for use in filter specifications */
#define	EFX_FILTER_SPEC_RX_DMAQ_ID_DROP		0xfff
#define	EFX_FILTER_SPEC_VID_UNSPEC		0xffff

extern	__checkReturn	efx_rc_t
efx_filter_init(
	__in		efx_nic_t *enp);

extern			void
efx_filter_fini(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_filter_insert(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec);

extern	__checkReturn	efx_rc_t
efx_filter_remove(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec);

extern	__checkReturn	efx_rc_t
efx_filter_restore(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_filter_supported_filters(
	__in				efx_nic_t *enp,
	__out_ecount(buffer_length)	uint32_t *buffer,
	__in				size_t buffer_length,
	__out				size_t *list_lengthp);

extern			void
efx_filter_spec_init_rx(
	__out		efx_filter_spec_t *spec,
	__in		efx_filter_priority_t priority,
	__in		efx_filter_flags_t flags,
	__in		efx_rxq_t *erp);

extern			void
efx_filter_spec_init_tx(
	__out		efx_filter_spec_t *spec,
	__in		efx_txq_t *etp);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_ipv4_local(
	__inout		efx_filter_spec_t *spec,
	__in		uint8_t proto,
	__in		uint32_t host,
	__in		uint16_t port);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_ipv4_full(
	__inout		efx_filter_spec_t *spec,
	__in		uint8_t proto,
	__in		uint32_t lhost,
	__in		uint16_t lport,
	__in		uint32_t rhost,
	__in		uint16_t rport);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_eth_local(
	__inout		efx_filter_spec_t *spec,
	__in		uint16_t vid,
	__in		const uint8_t *addr);

extern			void
efx_filter_spec_set_ether_type(
	__inout		efx_filter_spec_t *spec,
	__in		uint16_t ether_type);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_uc_def(
	__inout		efx_filter_spec_t *spec);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_mc_def(
	__inout		efx_filter_spec_t *spec);

typedef enum efx_filter_inner_frame_match_e {
	EFX_FILTER_INNER_FRAME_MATCH_OTHER = 0,
	EFX_FILTER_INNER_FRAME_MATCH_UNKNOWN_MCAST_DST,
	EFX_FILTER_INNER_FRAME_MATCH_UNKNOWN_UCAST_DST
} efx_filter_inner_frame_match_t;

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_encap_type(
	__inout		efx_filter_spec_t *spec,
	__in		efx_tunnel_protocol_t encap_type,
	__in		efx_filter_inner_frame_match_t inner_frame_match);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_vxlan(
	__inout		efx_filter_spec_t *spec,
	__in		const uint8_t *vni,
	__in		const uint8_t *inner_addr,
	__in		const uint8_t *outer_addr);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_geneve(
	__inout		efx_filter_spec_t *spec,
	__in		const uint8_t *vni,
	__in		const uint8_t *inner_addr,
	__in		const uint8_t *outer_addr);

extern	__checkReturn	efx_rc_t
efx_filter_spec_set_nvgre(
	__inout		efx_filter_spec_t *spec,
	__in		const uint8_t *vsid,
	__in		const uint8_t *inner_addr,
	__in		const uint8_t *outer_addr);

#if EFSYS_OPT_RX_SCALE
extern	__checkReturn	efx_rc_t
efx_filter_spec_set_rss_context(
	__inout		efx_filter_spec_t *spec,
	__in		uint32_t rss_context);
#endif
#endif	/* EFSYS_OPT_FILTER */

/* HASH */

extern	__checkReturn		uint32_t
efx_hash_dwords(
	__in_ecount(count)	uint32_t const *input,
	__in			size_t count,
	__in			uint32_t init);

extern	__checkReturn		uint32_t
efx_hash_bytes(
	__in_ecount(length)	uint8_t const *input,
	__in			size_t length,
	__in			uint32_t init);

#if EFSYS_OPT_LICENSING

/* LICENSING */

typedef struct efx_key_stats_s {
	uint32_t	eks_valid;
	uint32_t	eks_invalid;
	uint32_t	eks_blacklisted;
	uint32_t	eks_unverifiable;
	uint32_t	eks_wrong_node;
	uint32_t	eks_licensed_apps_lo;
	uint32_t	eks_licensed_apps_hi;
	uint32_t	eks_licensed_features_lo;
	uint32_t	eks_licensed_features_hi;
} efx_key_stats_t;

extern	__checkReturn		efx_rc_t
efx_lic_init(
	__in			efx_nic_t *enp);

extern				void
efx_lic_fini(
	__in			efx_nic_t *enp);

extern	__checkReturn	boolean_t
efx_lic_check_support(
	__in			efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_lic_update_licenses(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_lic_get_key_stats(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *ksp);

extern	__checkReturn	efx_rc_t
efx_lic_app_state(
	__in		efx_nic_t *enp,
	__in		uint64_t app_id,
	__out		boolean_t *licensedp);

extern	__checkReturn	efx_rc_t
efx_lic_get_id(
	__in		efx_nic_t *enp,
	__in		size_t buffer_size,
	__out		uint32_t *typep,
	__out		size_t *lengthp,
	__out_opt	uint8_t *bufferp);


extern	__checkReturn		efx_rc_t
efx_lic_find_start(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__out			uint32_t *startp);

extern	__checkReturn		efx_rc_t
efx_lic_find_end(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *endp);

extern	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_find_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *startp,
	__out			uint32_t *lengthp);

extern	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_validate_key(
	__in			efx_nic_t *enp,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length);

extern	__checkReturn		efx_rc_t
efx_lic_read_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__out_bcount_part(key_max_size, *lengthp)
				caddr_t keyp,
	__in			size_t key_max_size,
	__out			uint32_t *lengthp);

extern	__checkReturn		efx_rc_t
efx_lic_write_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length,
	__out			uint32_t *lengthp);

	__checkReturn		efx_rc_t
efx_lic_delete_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__in			uint32_t end,
	__out			uint32_t *deltap);

extern	__checkReturn		efx_rc_t
efx_lic_create_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size);

extern	__checkReturn		efx_rc_t
efx_lic_finish_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size);

#endif	/* EFSYS_OPT_LICENSING */

/* TUNNEL */

#if EFSYS_OPT_TUNNEL

extern	__checkReturn	efx_rc_t
efx_tunnel_init(
	__in		efx_nic_t *enp);

extern			void
efx_tunnel_fini(
	__in		efx_nic_t *enp);

/*
 * For overlay network encapsulation using UDP, the firmware needs to know
 * the configured UDP port for the overlay so it can decode encapsulated
 * frames correctly.
 * The UDP port/protocol list is global.
 */

extern	__checkReturn	efx_rc_t
efx_tunnel_config_udp_add(
	__in		efx_nic_t *enp,
	__in		uint16_t port /* host/cpu-endian */,
	__in		efx_tunnel_protocol_t protocol);

extern	__checkReturn	efx_rc_t
efx_tunnel_config_udp_remove(
	__in		efx_nic_t *enp,
	__in		uint16_t port /* host/cpu-endian */,
	__in		efx_tunnel_protocol_t protocol);

extern			void
efx_tunnel_config_clear(
	__in		efx_nic_t *enp);

/**
 * Apply tunnel UDP ports configuration to hardware.
 *
 * EAGAIN is returned if hardware will be reset (datapath and management CPU
 * reboot).
 */
extern	__checkReturn	efx_rc_t
efx_tunnel_reconfigure(
	__in		efx_nic_t *enp);

#endif /* EFSYS_OPT_TUNNEL */

#if EFSYS_OPT_FW_SUBVARIANT_AWARE

/**
 * Firmware subvariant choice options.
 *
 * It may be switched to no Tx checksum if attached drivers are either
 * preboot or firmware subvariant aware and no VIS are allocated.
 * If may be always switched to default explicitly using set request or
 * implicitly if unaware driver is attaching. If switching is done when
 * a driver is attached, it gets MC_REBOOT event and should recreate its
 * datapath.
 *
 * See SF-119419-TC DPDK Firmware Driver Interface and
 * SF-109306-TC EF10 for Driver Writers for details.
 */
typedef enum efx_nic_fw_subvariant_e {
	EFX_NIC_FW_SUBVARIANT_DEFAULT = 0,
	EFX_NIC_FW_SUBVARIANT_NO_TX_CSUM = 1,
	EFX_NIC_FW_SUBVARIANT_NTYPES
} efx_nic_fw_subvariant_t;

extern	__checkReturn	efx_rc_t
efx_nic_get_fw_subvariant(
	__in		efx_nic_t *enp,
	__out		efx_nic_fw_subvariant_t *subvariantp);

extern	__checkReturn	efx_rc_t
efx_nic_set_fw_subvariant(
	__in		efx_nic_t *enp,
	__in		efx_nic_fw_subvariant_t subvariant);

#endif	/* EFSYS_OPT_FW_SUBVARIANT_AWARE */

typedef enum efx_phy_fec_type_e {
	EFX_PHY_FEC_NONE = 0,
	EFX_PHY_FEC_BASER,
	EFX_PHY_FEC_RS
} efx_phy_fec_type_t;

extern	__checkReturn	efx_rc_t
efx_phy_fec_type_get(
	__in		efx_nic_t *enp,
	__out		efx_phy_fec_type_t *typep);

typedef struct efx_phy_link_state_s {
	uint32_t		epls_adv_cap_mask;
	uint32_t		epls_lp_cap_mask;
	uint32_t		epls_ld_cap_mask;
	unsigned int		epls_fcntl;
	efx_phy_fec_type_t	epls_fec;
	efx_link_mode_t		epls_link_mode;
} efx_phy_link_state_t;

extern	__checkReturn	efx_rc_t
efx_phy_link_state_get(
	__in		efx_nic_t *enp,
	__out		efx_phy_link_state_t  *eplsp);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EFX_H */
