/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2016 Solarflare Communications Inc.
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

#ifndef	_SYS_EFX_IMPL_H
#define	_SYS_EFX_IMPL_H

#include "efx.h"
#include "efx_regs.h"
#include "efx_regs_ef10.h"

/* FIXME: Add definition for driver generated software events */
#ifndef	ESE_DZ_EV_CODE_DRV_GEN_EV
#define	ESE_DZ_EV_CODE_DRV_GEN_EV FSE_AZ_EV_CODE_DRV_GEN_EV
#endif


#if EFSYS_OPT_SIENA
#include "siena_impl.h"
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
#include "hunt_impl.h"
#endif	/* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
#include "medford_impl.h"
#endif	/* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
#include "medford2_impl.h"
#endif	/* EFSYS_OPT_MEDFORD2 */

#if (EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2)
#include "ef10_impl.h"
#endif	/* (EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2) */

#ifdef	__cplusplus
extern "C" {
#endif

#define	EFX_MOD_MCDI		0x00000001
#define	EFX_MOD_PROBE		0x00000002
#define	EFX_MOD_NVRAM		0x00000004
#define	EFX_MOD_VPD		0x00000008
#define	EFX_MOD_NIC		0x00000010
#define	EFX_MOD_INTR		0x00000020
#define	EFX_MOD_EV		0x00000040
#define	EFX_MOD_RX		0x00000080
#define	EFX_MOD_TX		0x00000100
#define	EFX_MOD_PORT		0x00000200
#define	EFX_MOD_MON		0x00000400
#define	EFX_MOD_FILTER		0x00001000
#define	EFX_MOD_LIC		0x00002000
#define	EFX_MOD_TUNNEL		0x00004000

#define	EFX_RESET_PHY		0x00000001
#define	EFX_RESET_RXQ_ERR	0x00000002
#define	EFX_RESET_TXQ_ERR	0x00000004
#define	EFX_RESET_HW_UNAVAIL	0x00000008

typedef enum efx_mac_type_e {
	EFX_MAC_INVALID = 0,
	EFX_MAC_SIENA,
	EFX_MAC_HUNTINGTON,
	EFX_MAC_MEDFORD,
	EFX_MAC_MEDFORD2,
	EFX_MAC_NTYPES
} efx_mac_type_t;

typedef struct efx_ev_ops_s {
	efx_rc_t	(*eevo_init)(efx_nic_t *);
	void		(*eevo_fini)(efx_nic_t *);
	efx_rc_t	(*eevo_qcreate)(efx_nic_t *, unsigned int,
					  efsys_mem_t *, size_t, uint32_t,
					  uint32_t, uint32_t, efx_evq_t *);
	void		(*eevo_qdestroy)(efx_evq_t *);
	efx_rc_t	(*eevo_qprime)(efx_evq_t *, unsigned int);
	void		(*eevo_qpost)(efx_evq_t *, uint16_t);
	efx_rc_t	(*eevo_qmoderate)(efx_evq_t *, unsigned int);
#if EFSYS_OPT_QSTATS
	void		(*eevo_qstats_update)(efx_evq_t *, efsys_stat_t *);
#endif
} efx_ev_ops_t;

typedef struct efx_tx_ops_s {
	efx_rc_t	(*etxo_init)(efx_nic_t *);
	void		(*etxo_fini)(efx_nic_t *);
	efx_rc_t	(*etxo_qcreate)(efx_nic_t *,
					unsigned int, unsigned int,
					efsys_mem_t *, size_t,
					uint32_t, uint16_t,
					efx_evq_t *, efx_txq_t *,
					unsigned int *);
	void		(*etxo_qdestroy)(efx_txq_t *);
	efx_rc_t	(*etxo_qpost)(efx_txq_t *, efx_buffer_t *,
				      unsigned int, unsigned int,
				      unsigned int *);
	void		(*etxo_qpush)(efx_txq_t *, unsigned int, unsigned int);
	efx_rc_t	(*etxo_qpace)(efx_txq_t *, unsigned int);
	efx_rc_t	(*etxo_qflush)(efx_txq_t *);
	void		(*etxo_qenable)(efx_txq_t *);
	efx_rc_t	(*etxo_qpio_enable)(efx_txq_t *);
	void		(*etxo_qpio_disable)(efx_txq_t *);
	efx_rc_t	(*etxo_qpio_write)(efx_txq_t *, uint8_t *, size_t,
					   size_t);
	efx_rc_t	(*etxo_qpio_post)(efx_txq_t *, size_t, unsigned int,
					   unsigned int *);
	efx_rc_t	(*etxo_qdesc_post)(efx_txq_t *, efx_desc_t *,
				      unsigned int, unsigned int,
				      unsigned int *);
	void		(*etxo_qdesc_dma_create)(efx_txq_t *, efsys_dma_addr_t,
						size_t, boolean_t,
						efx_desc_t *);
	void		(*etxo_qdesc_tso_create)(efx_txq_t *, uint16_t,
						uint32_t, uint8_t,
						efx_desc_t *);
	void		(*etxo_qdesc_tso2_create)(efx_txq_t *, uint16_t,
						uint16_t, uint32_t, uint16_t,
						efx_desc_t *, int);
	void		(*etxo_qdesc_vlantci_create)(efx_txq_t *, uint16_t,
						efx_desc_t *);
	void		(*etxo_qdesc_checksum_create)(efx_txq_t *, uint16_t,
						efx_desc_t *);
#if EFSYS_OPT_QSTATS
	void		(*etxo_qstats_update)(efx_txq_t *,
					      efsys_stat_t *);
#endif
} efx_tx_ops_t;

typedef union efx_rxq_type_data_u {
	/* Dummy member to have non-empty union if no options are enabled */
	uint32_t	ertd_dummy;
#if EFSYS_OPT_RX_PACKED_STREAM
	struct {
		uint32_t	eps_buf_size;
	} ertd_packed_stream;
#endif
#if EFSYS_OPT_RX_ES_SUPER_BUFFER
	struct {
		uint32_t	eessb_bufs_per_desc;
		uint32_t	eessb_max_dma_len;
		uint32_t	eessb_buf_stride;
		uint32_t	eessb_hol_block_timeout;
	} ertd_es_super_buffer;
#endif
} efx_rxq_type_data_t;

typedef struct efx_rx_ops_s {
	efx_rc_t	(*erxo_init)(efx_nic_t *);
	void		(*erxo_fini)(efx_nic_t *);
#if EFSYS_OPT_RX_SCATTER
	efx_rc_t	(*erxo_scatter_enable)(efx_nic_t *, unsigned int);
#endif
#if EFSYS_OPT_RX_SCALE
	efx_rc_t	(*erxo_scale_context_alloc)(efx_nic_t *,
						    efx_rx_scale_context_type_t,
						    uint32_t, uint32_t *);
	efx_rc_t	(*erxo_scale_context_free)(efx_nic_t *, uint32_t);
	efx_rc_t	(*erxo_scale_mode_set)(efx_nic_t *, uint32_t,
					       efx_rx_hash_alg_t,
					       efx_rx_hash_type_t, boolean_t);
	efx_rc_t	(*erxo_scale_key_set)(efx_nic_t *, uint32_t,
					      uint8_t *, size_t);
	efx_rc_t	(*erxo_scale_tbl_set)(efx_nic_t *, uint32_t,
					      unsigned int *, size_t);
	uint32_t	(*erxo_prefix_hash)(efx_nic_t *, efx_rx_hash_alg_t,
					    uint8_t *);
#endif /* EFSYS_OPT_RX_SCALE */
	efx_rc_t	(*erxo_prefix_pktlen)(efx_nic_t *, uint8_t *,
					      uint16_t *);
	void		(*erxo_qpost)(efx_rxq_t *, efsys_dma_addr_t *, size_t,
				      unsigned int, unsigned int,
				      unsigned int);
	void		(*erxo_qpush)(efx_rxq_t *, unsigned int, unsigned int *);
#if EFSYS_OPT_RX_PACKED_STREAM
	void		(*erxo_qpush_ps_credits)(efx_rxq_t *);
	uint8_t *	(*erxo_qps_packet_info)(efx_rxq_t *, uint8_t *,
						uint32_t, uint32_t,
						uint16_t *, uint32_t *, uint32_t *);
#endif
	efx_rc_t	(*erxo_qflush)(efx_rxq_t *);
	void		(*erxo_qenable)(efx_rxq_t *);
	efx_rc_t	(*erxo_qcreate)(efx_nic_t *enp, unsigned int,
					unsigned int, efx_rxq_type_t,
					const efx_rxq_type_data_t *,
					efsys_mem_t *, size_t, uint32_t,
					unsigned int,
					efx_evq_t *, efx_rxq_t *);
	void		(*erxo_qdestroy)(efx_rxq_t *);
} efx_rx_ops_t;

typedef struct efx_mac_ops_s {
	efx_rc_t	(*emo_poll)(efx_nic_t *, efx_link_mode_t *);
	efx_rc_t	(*emo_up)(efx_nic_t *, boolean_t *);
	efx_rc_t	(*emo_addr_set)(efx_nic_t *);
	efx_rc_t	(*emo_pdu_set)(efx_nic_t *);
	efx_rc_t	(*emo_pdu_get)(efx_nic_t *, size_t *);
	efx_rc_t	(*emo_reconfigure)(efx_nic_t *);
	efx_rc_t	(*emo_multicast_list_set)(efx_nic_t *);
	efx_rc_t	(*emo_filter_default_rxq_set)(efx_nic_t *,
						      efx_rxq_t *, boolean_t);
	void		(*emo_filter_default_rxq_clear)(efx_nic_t *);
#if EFSYS_OPT_LOOPBACK
	efx_rc_t	(*emo_loopback_set)(efx_nic_t *, efx_link_mode_t,
					    efx_loopback_type_t);
#endif	/* EFSYS_OPT_LOOPBACK */
#if EFSYS_OPT_MAC_STATS
	efx_rc_t	(*emo_stats_get_mask)(efx_nic_t *, uint32_t *, size_t);
	efx_rc_t	(*emo_stats_clear)(efx_nic_t *);
	efx_rc_t	(*emo_stats_upload)(efx_nic_t *, efsys_mem_t *);
	efx_rc_t	(*emo_stats_periodic)(efx_nic_t *, efsys_mem_t *,
					      uint16_t, boolean_t);
	efx_rc_t	(*emo_stats_update)(efx_nic_t *, efsys_mem_t *,
					    efsys_stat_t *, uint32_t *);
#endif	/* EFSYS_OPT_MAC_STATS */
} efx_mac_ops_t;

typedef struct efx_phy_ops_s {
	efx_rc_t	(*epo_power)(efx_nic_t *, boolean_t); /* optional */
	efx_rc_t	(*epo_reset)(efx_nic_t *);
	efx_rc_t	(*epo_reconfigure)(efx_nic_t *);
	efx_rc_t	(*epo_verify)(efx_nic_t *);
	efx_rc_t	(*epo_oui_get)(efx_nic_t *, uint32_t *);
	efx_rc_t	(*epo_link_state_get)(efx_nic_t *, efx_phy_link_state_t *);
#if EFSYS_OPT_PHY_STATS
	efx_rc_t	(*epo_stats_update)(efx_nic_t *, efsys_mem_t *,
					    uint32_t *);
#endif	/* EFSYS_OPT_PHY_STATS */
#if EFSYS_OPT_BIST
	efx_rc_t	(*epo_bist_enable_offline)(efx_nic_t *);
	efx_rc_t	(*epo_bist_start)(efx_nic_t *, efx_bist_type_t);
	efx_rc_t	(*epo_bist_poll)(efx_nic_t *, efx_bist_type_t,
					 efx_bist_result_t *, uint32_t *,
					 unsigned long *, size_t);
	void		(*epo_bist_stop)(efx_nic_t *, efx_bist_type_t);
#endif	/* EFSYS_OPT_BIST */
} efx_phy_ops_t;

#if EFSYS_OPT_FILTER
typedef struct efx_filter_ops_s {
	efx_rc_t	(*efo_init)(efx_nic_t *);
	void		(*efo_fini)(efx_nic_t *);
	efx_rc_t	(*efo_restore)(efx_nic_t *);
	efx_rc_t	(*efo_add)(efx_nic_t *, efx_filter_spec_t *,
				   boolean_t may_replace);
	efx_rc_t	(*efo_delete)(efx_nic_t *, efx_filter_spec_t *);
	efx_rc_t	(*efo_supported_filters)(efx_nic_t *, uint32_t *,
				   size_t, size_t *);
	efx_rc_t	(*efo_reconfigure)(efx_nic_t *, uint8_t const *, boolean_t,
				   boolean_t, boolean_t, boolean_t,
				   uint8_t const *, uint32_t);
} efx_filter_ops_t;

extern	__checkReturn	efx_rc_t
efx_filter_reconfigure(
	__in				efx_nic_t *enp,
	__in_ecount(6)			uint8_t const *mac_addr,
	__in				boolean_t all_unicst,
	__in				boolean_t mulcst,
	__in				boolean_t all_mulcst,
	__in				boolean_t brdcst,
	__in_ecount(6*count)		uint8_t const *addrs,
	__in				uint32_t count);

#endif /* EFSYS_OPT_FILTER */

#if EFSYS_OPT_TUNNEL
typedef struct efx_tunnel_ops_s {
	boolean_t	(*eto_udp_encap_supported)(efx_nic_t *);
	efx_rc_t	(*eto_reconfigure)(efx_nic_t *);
} efx_tunnel_ops_t;
#endif /* EFSYS_OPT_TUNNEL */

typedef struct efx_port_s {
	efx_mac_type_t		ep_mac_type;
	uint32_t		ep_phy_type;
	uint8_t			ep_port;
	uint32_t		ep_mac_pdu;
	uint8_t			ep_mac_addr[6];
	efx_link_mode_t		ep_link_mode;
	boolean_t		ep_all_unicst;
	boolean_t		ep_mulcst;
	boolean_t		ep_all_mulcst;
	boolean_t		ep_brdcst;
	unsigned int		ep_fcntl;
	boolean_t		ep_fcntl_autoneg;
	efx_oword_t		ep_multicst_hash[2];
	uint8_t			ep_mulcst_addr_list[EFX_MAC_ADDR_LEN *
						    EFX_MAC_MULTICAST_LIST_MAX];
	uint32_t		ep_mulcst_addr_count;
#if EFSYS_OPT_LOOPBACK
	efx_loopback_type_t	ep_loopback_type;
	efx_link_mode_t		ep_loopback_link_mode;
#endif	/* EFSYS_OPT_LOOPBACK */
#if EFSYS_OPT_PHY_FLAGS
	uint32_t		ep_phy_flags;
#endif	/* EFSYS_OPT_PHY_FLAGS */
#if EFSYS_OPT_PHY_LED_CONTROL
	efx_phy_led_mode_t	ep_phy_led_mode;
#endif	/* EFSYS_OPT_PHY_LED_CONTROL */
	efx_phy_media_type_t	ep_fixed_port_type;
	efx_phy_media_type_t	ep_module_type;
	uint32_t		ep_adv_cap_mask;
	uint32_t		ep_lp_cap_mask;
	uint32_t		ep_default_adv_cap_mask;
	uint32_t		ep_phy_cap_mask;
	boolean_t		ep_mac_drain;
#if EFSYS_OPT_BIST
	efx_bist_type_t		ep_current_bist;
#endif
	const efx_mac_ops_t	*ep_emop;
	const efx_phy_ops_t	*ep_epop;
} efx_port_t;

typedef struct efx_mon_ops_s {
#if EFSYS_OPT_MON_STATS
	efx_rc_t	(*emo_stats_update)(efx_nic_t *, efsys_mem_t *,
					    efx_mon_stat_value_t *);
	efx_rc_t	(*emo_limits_update)(efx_nic_t *,
					     efx_mon_stat_limits_t *);
#endif	/* EFSYS_OPT_MON_STATS */
} efx_mon_ops_t;

typedef struct efx_mon_s {
	efx_mon_type_t		em_type;
	const efx_mon_ops_t	*em_emop;
} efx_mon_t;

typedef struct efx_intr_ops_s {
	efx_rc_t	(*eio_init)(efx_nic_t *, efx_intr_type_t, efsys_mem_t *);
	void		(*eio_enable)(efx_nic_t *);
	void		(*eio_disable)(efx_nic_t *);
	void		(*eio_disable_unlocked)(efx_nic_t *);
	efx_rc_t	(*eio_trigger)(efx_nic_t *, unsigned int);
	void		(*eio_status_line)(efx_nic_t *, boolean_t *, uint32_t *);
	void		(*eio_status_message)(efx_nic_t *, unsigned int,
				 boolean_t *);
	void		(*eio_fatal)(efx_nic_t *);
	void		(*eio_fini)(efx_nic_t *);
} efx_intr_ops_t;

typedef struct efx_intr_s {
	const efx_intr_ops_t	*ei_eiop;
	efsys_mem_t		*ei_esmp;
	efx_intr_type_t		ei_type;
	unsigned int		ei_level;
} efx_intr_t;

typedef struct efx_nic_ops_s {
	efx_rc_t	(*eno_probe)(efx_nic_t *);
	efx_rc_t	(*eno_board_cfg)(efx_nic_t *);
	efx_rc_t	(*eno_set_drv_limits)(efx_nic_t *, efx_drv_limits_t*);
	efx_rc_t	(*eno_reset)(efx_nic_t *);
	efx_rc_t	(*eno_init)(efx_nic_t *);
	efx_rc_t	(*eno_get_vi_pool)(efx_nic_t *, uint32_t *);
	efx_rc_t	(*eno_get_bar_region)(efx_nic_t *, efx_nic_region_t,
					uint32_t *, size_t *);
	boolean_t	(*eno_hw_unavailable)(efx_nic_t *);
	void		(*eno_set_hw_unavailable)(efx_nic_t *);
#if EFSYS_OPT_DIAG
	efx_rc_t	(*eno_register_test)(efx_nic_t *);
#endif	/* EFSYS_OPT_DIAG */
	void		(*eno_fini)(efx_nic_t *);
	void		(*eno_unprobe)(efx_nic_t *);
} efx_nic_ops_t;

#ifndef EFX_TXQ_LIMIT_TARGET
#define	EFX_TXQ_LIMIT_TARGET 259
#endif
#ifndef EFX_RXQ_LIMIT_TARGET
#define	EFX_RXQ_LIMIT_TARGET 512
#endif


#if EFSYS_OPT_FILTER

#if EFSYS_OPT_SIENA

typedef struct siena_filter_spec_s {
	uint8_t		sfs_type;
	uint32_t	sfs_flags;
	uint32_t	sfs_dmaq_id;
	uint32_t	sfs_dword[3];
} siena_filter_spec_t;

typedef enum siena_filter_type_e {
	EFX_SIENA_FILTER_RX_TCP_FULL,	/* TCP/IPv4 {dIP,dTCP,sIP,sTCP} */
	EFX_SIENA_FILTER_RX_TCP_WILD,	/* TCP/IPv4 {dIP,dTCP,  -,   -} */
	EFX_SIENA_FILTER_RX_UDP_FULL,	/* UDP/IPv4 {dIP,dUDP,sIP,sUDP} */
	EFX_SIENA_FILTER_RX_UDP_WILD,	/* UDP/IPv4 {dIP,dUDP,  -,   -} */
	EFX_SIENA_FILTER_RX_MAC_FULL,	/* Ethernet {dMAC,VLAN} */
	EFX_SIENA_FILTER_RX_MAC_WILD,	/* Ethernet {dMAC,   -} */

	EFX_SIENA_FILTER_TX_TCP_FULL,	/* TCP/IPv4 {dIP,dTCP,sIP,sTCP} */
	EFX_SIENA_FILTER_TX_TCP_WILD,	/* TCP/IPv4 {  -,   -,sIP,sTCP} */
	EFX_SIENA_FILTER_TX_UDP_FULL,	/* UDP/IPv4 {dIP,dTCP,sIP,sTCP} */
	EFX_SIENA_FILTER_TX_UDP_WILD,	/* UDP/IPv4 {  -,   -,sIP,sUDP} */
	EFX_SIENA_FILTER_TX_MAC_FULL,	/* Ethernet {sMAC,VLAN} */
	EFX_SIENA_FILTER_TX_MAC_WILD,	/* Ethernet {sMAC,   -} */

	EFX_SIENA_FILTER_NTYPES
} siena_filter_type_t;

typedef enum siena_filter_tbl_id_e {
	EFX_SIENA_FILTER_TBL_RX_IP = 0,
	EFX_SIENA_FILTER_TBL_RX_MAC,
	EFX_SIENA_FILTER_TBL_TX_IP,
	EFX_SIENA_FILTER_TBL_TX_MAC,
	EFX_SIENA_FILTER_NTBLS
} siena_filter_tbl_id_t;

typedef struct siena_filter_tbl_s {
	int			sft_size;	/* number of entries */
	int			sft_used;	/* active count */
	uint32_t		*sft_bitmap;	/* active bitmap */
	siena_filter_spec_t	*sft_spec;	/* array of saved specs */
} siena_filter_tbl_t;

typedef struct siena_filter_s {
	siena_filter_tbl_t	sf_tbl[EFX_SIENA_FILTER_NTBLS];
	unsigned int		sf_depth[EFX_SIENA_FILTER_NTYPES];
} siena_filter_t;

#endif	/* EFSYS_OPT_SIENA */

typedef struct efx_filter_s {
#if EFSYS_OPT_SIENA
	siena_filter_t		*ef_siena_filter;
#endif /* EFSYS_OPT_SIENA */
#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
	ef10_filter_table_t	*ef_ef10_filter_table;
#endif /* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
} efx_filter_t;

#if EFSYS_OPT_SIENA

extern			void
siena_filter_tbl_clear(
	__in		efx_nic_t *enp,
	__in		siena_filter_tbl_id_t tbl);

#endif	/* EFSYS_OPT_SIENA */

#endif	/* EFSYS_OPT_FILTER */

#if EFSYS_OPT_MCDI

#define	EFX_TUNNEL_MAXNENTRIES	(16)

#if EFSYS_OPT_TUNNEL

typedef struct efx_tunnel_udp_entry_s {
	uint16_t			etue_port; /* host/cpu-endian */
	uint16_t			etue_protocol;
} efx_tunnel_udp_entry_t;

typedef struct efx_tunnel_cfg_s {
	efx_tunnel_udp_entry_t	etc_udp_entries[EFX_TUNNEL_MAXNENTRIES];
	unsigned int		etc_udp_entries_num;
} efx_tunnel_cfg_t;

#endif /* EFSYS_OPT_TUNNEL */

typedef struct efx_mcdi_ops_s {
	efx_rc_t	(*emco_init)(efx_nic_t *, const efx_mcdi_transport_t *);
	void		(*emco_send_request)(efx_nic_t *, void *, size_t,
					void *, size_t);
	efx_rc_t	(*emco_poll_reboot)(efx_nic_t *);
	boolean_t	(*emco_poll_response)(efx_nic_t *);
	void		(*emco_read_response)(efx_nic_t *, void *, size_t, size_t);
	void		(*emco_fini)(efx_nic_t *);
	efx_rc_t	(*emco_feature_supported)(efx_nic_t *,
					    efx_mcdi_feature_id_t, boolean_t *);
	void		(*emco_get_timeout)(efx_nic_t *, efx_mcdi_req_t *,
					    uint32_t *);
} efx_mcdi_ops_t;

typedef struct efx_mcdi_s {
	const efx_mcdi_ops_t		*em_emcop;
	const efx_mcdi_transport_t	*em_emtp;
	efx_mcdi_iface_t		em_emip;
} efx_mcdi_t;

#endif /* EFSYS_OPT_MCDI */

#if EFSYS_OPT_NVRAM

/* Invalid partition ID for en_nvram_partn_locked field of efx_nc_t */
#define	EFX_NVRAM_PARTN_INVALID		(0xffffffffu)

typedef struct efx_nvram_ops_s {
#if EFSYS_OPT_DIAG
	efx_rc_t	(*envo_test)(efx_nic_t *);
#endif	/* EFSYS_OPT_DIAG */
	efx_rc_t	(*envo_type_to_partn)(efx_nic_t *, efx_nvram_type_t,
					    uint32_t *);
	efx_rc_t	(*envo_partn_size)(efx_nic_t *, uint32_t, size_t *);
	efx_rc_t	(*envo_partn_rw_start)(efx_nic_t *, uint32_t, size_t *);
	efx_rc_t	(*envo_partn_read)(efx_nic_t *, uint32_t,
					    unsigned int, caddr_t, size_t);
	efx_rc_t	(*envo_partn_read_backup)(efx_nic_t *, uint32_t,
					    unsigned int, caddr_t, size_t);
	efx_rc_t	(*envo_partn_erase)(efx_nic_t *, uint32_t,
					    unsigned int, size_t);
	efx_rc_t	(*envo_partn_write)(efx_nic_t *, uint32_t,
					    unsigned int, caddr_t, size_t);
	efx_rc_t	(*envo_partn_rw_finish)(efx_nic_t *, uint32_t,
					    uint32_t *);
	efx_rc_t	(*envo_partn_get_version)(efx_nic_t *, uint32_t,
					    uint32_t *, uint16_t *);
	efx_rc_t	(*envo_partn_set_version)(efx_nic_t *, uint32_t,
					    uint16_t *);
	efx_rc_t	(*envo_buffer_validate)(uint32_t,
					    caddr_t, size_t);
} efx_nvram_ops_t;
#endif /* EFSYS_OPT_NVRAM */

#if EFSYS_OPT_VPD
typedef struct efx_vpd_ops_s {
	efx_rc_t	(*evpdo_init)(efx_nic_t *);
	efx_rc_t	(*evpdo_size)(efx_nic_t *, size_t *);
	efx_rc_t	(*evpdo_read)(efx_nic_t *, caddr_t, size_t);
	efx_rc_t	(*evpdo_verify)(efx_nic_t *, caddr_t, size_t);
	efx_rc_t	(*evpdo_reinit)(efx_nic_t *, caddr_t, size_t);
	efx_rc_t	(*evpdo_get)(efx_nic_t *, caddr_t, size_t,
					efx_vpd_value_t *);
	efx_rc_t	(*evpdo_set)(efx_nic_t *, caddr_t, size_t,
					efx_vpd_value_t *);
	efx_rc_t	(*evpdo_next)(efx_nic_t *, caddr_t, size_t,
					efx_vpd_value_t *, unsigned int *);
	efx_rc_t	(*evpdo_write)(efx_nic_t *, caddr_t, size_t);
	void		(*evpdo_fini)(efx_nic_t *);
} efx_vpd_ops_t;
#endif	/* EFSYS_OPT_VPD */

#if EFSYS_OPT_VPD || EFSYS_OPT_NVRAM

	__checkReturn		efx_rc_t
efx_mcdi_nvram_partitions(
	__in			efx_nic_t *enp,
	__out_bcount(size)	caddr_t data,
	__in			size_t size,
	__out			unsigned int *npartnp);

	__checkReturn		efx_rc_t
efx_mcdi_nvram_metadata(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			uint32_t *subtypep,
	__out_ecount(4)		uint16_t version[4],
	__out_bcount_opt(size)	char *descp,
	__in			size_t size);

	__checkReturn		efx_rc_t
efx_mcdi_nvram_info(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out_opt		size_t *sizep,
	__out_opt		uint32_t *addressp,
	__out_opt		uint32_t *erase_sizep,
	__out_opt		uint32_t *write_sizep);

	__checkReturn		efx_rc_t
efx_mcdi_nvram_update_start(
	__in			efx_nic_t *enp,
	__in			uint32_t partn);

	__checkReturn		efx_rc_t
efx_mcdi_nvram_read(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			uint32_t offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			uint32_t mode);

	__checkReturn		efx_rc_t
efx_mcdi_nvram_erase(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			uint32_t offset,
	__in			size_t size);

	__checkReturn		efx_rc_t
efx_mcdi_nvram_write(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			uint32_t offset,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

	__checkReturn		efx_rc_t
efx_mcdi_nvram_update_finish(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			boolean_t reboot,
	__out_opt		uint32_t *verify_resultp);

#if EFSYS_OPT_DIAG

	__checkReturn		efx_rc_t
efx_mcdi_nvram_test(
	__in			efx_nic_t *enp,
	__in			uint32_t partn);

#endif	/* EFSYS_OPT_DIAG */

#endif /* EFSYS_OPT_VPD || EFSYS_OPT_NVRAM */

#if EFSYS_OPT_LICENSING

typedef struct efx_lic_ops_s {
	efx_rc_t	(*elo_update_licenses)(efx_nic_t *);
	efx_rc_t	(*elo_get_key_stats)(efx_nic_t *, efx_key_stats_t *);
	efx_rc_t	(*elo_app_state)(efx_nic_t *, uint64_t, boolean_t *);
	efx_rc_t	(*elo_get_id)(efx_nic_t *, size_t, uint32_t *,
				      size_t *, uint8_t *);
	efx_rc_t	(*elo_find_start)
				(efx_nic_t *, caddr_t, size_t, uint32_t *);
	efx_rc_t	(*elo_find_end)(efx_nic_t *, caddr_t, size_t,
				uint32_t, uint32_t *);
	boolean_t	(*elo_find_key)(efx_nic_t *, caddr_t, size_t,
				uint32_t, uint32_t *, uint32_t *);
	boolean_t	(*elo_validate_key)(efx_nic_t *,
				caddr_t, uint32_t);
	efx_rc_t	(*elo_read_key)(efx_nic_t *,
				caddr_t, size_t, uint32_t, uint32_t,
				caddr_t, size_t, uint32_t *);
	efx_rc_t	(*elo_write_key)(efx_nic_t *,
				caddr_t, size_t, uint32_t,
				caddr_t, uint32_t, uint32_t *);
	efx_rc_t	(*elo_delete_key)(efx_nic_t *,
				caddr_t, size_t, uint32_t,
				uint32_t, uint32_t, uint32_t *);
	efx_rc_t	(*elo_create_partition)(efx_nic_t *,
				caddr_t, size_t);
	efx_rc_t	(*elo_finish_partition)(efx_nic_t *,
				caddr_t, size_t);
} efx_lic_ops_t;

#endif

typedef struct efx_drv_cfg_s {
	uint32_t		edc_min_vi_count;
	uint32_t		edc_max_vi_count;

	uint32_t		edc_max_piobuf_count;
	uint32_t		edc_pio_alloc_size;
} efx_drv_cfg_t;

struct efx_nic_s {
	uint32_t		en_magic;
	efx_family_t		en_family;
	uint32_t		en_features;
	efsys_identifier_t	*en_esip;
	efsys_lock_t		*en_eslp;
	efsys_bar_t		*en_esbp;
	unsigned int		en_mod_flags;
	unsigned int		en_reset_flags;
	efx_nic_cfg_t		en_nic_cfg;
	efx_drv_cfg_t		en_drv_cfg;
	efx_port_t		en_port;
	efx_mon_t		en_mon;
	efx_intr_t		en_intr;
	uint32_t		en_ev_qcount;
	uint32_t		en_rx_qcount;
	uint32_t		en_tx_qcount;
	const efx_nic_ops_t	*en_enop;
	const efx_ev_ops_t	*en_eevop;
	const efx_tx_ops_t	*en_etxop;
	const efx_rx_ops_t	*en_erxop;
	efx_fw_variant_t	efv;
#if EFSYS_OPT_FILTER
	efx_filter_t		en_filter;
	const efx_filter_ops_t	*en_efop;
#endif	/* EFSYS_OPT_FILTER */
#if EFSYS_OPT_TUNNEL
	efx_tunnel_cfg_t	en_tunnel_cfg;
	const efx_tunnel_ops_t	*en_etop;
#endif /* EFSYS_OPT_TUNNEL */
#if EFSYS_OPT_MCDI
	efx_mcdi_t		en_mcdi;
#endif	/* EFSYS_OPT_MCDI */
#if EFSYS_OPT_NVRAM
	uint32_t		en_nvram_partn_locked;
	const efx_nvram_ops_t	*en_envop;
#endif	/* EFSYS_OPT_NVRAM */
#if EFSYS_OPT_VPD
	const efx_vpd_ops_t	*en_evpdop;
#endif	/* EFSYS_OPT_VPD */
#if EFSYS_OPT_RX_SCALE
	efx_rx_hash_support_t		en_hash_support;
	efx_rx_scale_context_type_t	en_rss_context_type;
	uint32_t			en_rss_context;
#endif	/* EFSYS_OPT_RX_SCALE */
	uint32_t		en_vport_id;
#if EFSYS_OPT_LICENSING
	const efx_lic_ops_t	*en_elop;
	boolean_t		en_licensing_supported;
#endif
	union {
#if EFSYS_OPT_SIENA
		struct {
#if EFSYS_OPT_NVRAM || EFSYS_OPT_VPD
			unsigned int		enu_partn_mask;
#endif	/* EFSYS_OPT_NVRAM || EFSYS_OPT_VPD */
#if EFSYS_OPT_VPD
			caddr_t			enu_svpd;
			size_t			enu_svpd_length;
#endif	/* EFSYS_OPT_VPD */
			int			enu_unused;
		} siena;
#endif	/* EFSYS_OPT_SIENA */
		int	enu_unused;
	} en_u;
#if (EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2)
	union en_arch {
		struct {
			int			ena_vi_base;
			int			ena_vi_count;
			int			ena_vi_shift;
#if EFSYS_OPT_VPD
			caddr_t			ena_svpd;
			size_t			ena_svpd_length;
#endif	/* EFSYS_OPT_VPD */
			efx_piobuf_handle_t	ena_piobuf_handle[EF10_MAX_PIOBUF_NBUFS];
			uint32_t		ena_piobuf_count;
			uint32_t		ena_pio_alloc_map[EF10_MAX_PIOBUF_NBUFS];
			uint32_t		ena_pio_write_vi_base;
			/* Memory BAR mapping regions */
			uint32_t		ena_uc_mem_map_offset;
			size_t			ena_uc_mem_map_size;
			uint32_t		ena_wc_mem_map_offset;
			size_t			ena_wc_mem_map_size;
		} ef10;
	} en_arch;
#endif	/* (EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2) */
};


#define	EFX_NIC_MAGIC	0x02121996

typedef	boolean_t (*efx_ev_handler_t)(efx_evq_t *, efx_qword_t *,
    const efx_ev_callbacks_t *, void *);

typedef struct efx_evq_rxq_state_s {
	unsigned int			eers_rx_read_ptr;
	unsigned int			eers_rx_mask;
#if EFSYS_OPT_RX_PACKED_STREAM || EFSYS_OPT_RX_ES_SUPER_BUFFER
	unsigned int			eers_rx_stream_npackets;
	boolean_t			eers_rx_packed_stream;
#endif
#if EFSYS_OPT_RX_PACKED_STREAM
	unsigned int			eers_rx_packed_stream_credits;
#endif
} efx_evq_rxq_state_t;

struct efx_evq_s {
	uint32_t			ee_magic;
	efx_nic_t			*ee_enp;
	unsigned int			ee_index;
	unsigned int			ee_mask;
	efsys_mem_t			*ee_esmp;
#if EFSYS_OPT_QSTATS
	uint32_t			ee_stat[EV_NQSTATS];
#endif	/* EFSYS_OPT_QSTATS */

	efx_ev_handler_t		ee_rx;
	efx_ev_handler_t		ee_tx;
	efx_ev_handler_t		ee_driver;
	efx_ev_handler_t		ee_global;
	efx_ev_handler_t		ee_drv_gen;
#if EFSYS_OPT_MCDI
	efx_ev_handler_t		ee_mcdi;
#endif	/* EFSYS_OPT_MCDI */

	efx_evq_rxq_state_t		ee_rxq_state[EFX_EV_RX_NLABELS];

	uint32_t			ee_flags;
};

#define	EFX_EVQ_MAGIC	0x08081997

#define	EFX_EVQ_SIENA_TIMER_QUANTUM_NS	6144 /* 768 cycles */

struct efx_rxq_s {
	uint32_t			er_magic;
	efx_nic_t			*er_enp;
	efx_evq_t			*er_eep;
	unsigned int			er_index;
	unsigned int			er_label;
	unsigned int			er_mask;
	efsys_mem_t			*er_esmp;
	efx_evq_rxq_state_t		*er_ev_qstate;
};

#define	EFX_RXQ_MAGIC	0x15022005

struct efx_txq_s {
	uint32_t			et_magic;
	efx_nic_t			*et_enp;
	unsigned int			et_index;
	unsigned int			et_mask;
	efsys_mem_t			*et_esmp;
#if EFSYS_OPT_HUNTINGTON
	uint32_t			et_pio_bufnum;
	uint32_t			et_pio_blknum;
	uint32_t			et_pio_write_offset;
	uint32_t			et_pio_offset;
	size_t				et_pio_size;
#endif
#if EFSYS_OPT_QSTATS
	uint32_t			et_stat[TX_NQSTATS];
#endif	/* EFSYS_OPT_QSTATS */
};

#define	EFX_TXQ_MAGIC	0x05092005

#define	EFX_MAC_ADDR_COPY(_dst, _src)					\
	do {								\
		(_dst)[0] = (_src)[0];					\
		(_dst)[1] = (_src)[1];					\
		(_dst)[2] = (_src)[2];					\
		(_dst)[3] = (_src)[3];					\
		(_dst)[4] = (_src)[4];					\
		(_dst)[5] = (_src)[5];					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_MAC_BROADCAST_ADDR_SET(_dst)				\
	do {								\
		uint16_t *_d = (uint16_t *)(_dst);			\
		_d[0] = 0xffff;						\
		_d[1] = 0xffff;						\
		_d[2] = 0xffff;						\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#if EFSYS_OPT_CHECK_REG
#define	EFX_CHECK_REG(_enp, _reg)					\
	do {								\
		const char *name = #_reg;				\
		char min = name[4];					\
		char max = name[5];					\
		char rev;						\
									\
		switch ((_enp)->en_family) {				\
		case EFX_FAMILY_SIENA:					\
			rev = 'C';					\
			break;						\
									\
		case EFX_FAMILY_HUNTINGTON:				\
			rev = 'D';					\
			break;						\
									\
		case EFX_FAMILY_MEDFORD:				\
			rev = 'E';					\
			break;						\
									\
		case EFX_FAMILY_MEDFORD2:				\
			rev = 'F';					\
			break;						\
									\
		default:						\
			rev = '?';					\
			break;						\
		}							\
									\
		EFSYS_ASSERT3S(rev, >=, min);				\
		EFSYS_ASSERT3S(rev, <=, max);				\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#else
#define	EFX_CHECK_REG(_enp, _reg) do {					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

#define	EFX_BAR_READD(_enp, _reg, _edp, _lock)				\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_BAR_READD((_enp)->en_esbp, _reg ## _OFST,		\
		    (_edp), (_lock));					\
		EFSYS_PROBE3(efx_bar_readd, const char *, #_reg,	\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_WRITED(_enp, _reg, _edp, _lock)				\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE3(efx_bar_writed, const char *, #_reg,	\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
		EFSYS_BAR_WRITED((_enp)->en_esbp, _reg ## _OFST,	\
		    (_edp), (_lock));					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_READQ(_enp, _reg, _eqp)					\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_BAR_READQ((_enp)->en_esbp, _reg ## _OFST,		\
		    (_eqp));						\
		EFSYS_PROBE4(efx_bar_readq, const char *, #_reg,	\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_WRITEQ(_enp, _reg, _eqp)				\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE4(efx_bar_writeq, const char *, #_reg,	\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
		EFSYS_BAR_WRITEQ((_enp)->en_esbp, _reg ## _OFST,	\
		    (_eqp));						\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_READO(_enp, _reg, _eop)					\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_BAR_READO((_enp)->en_esbp, _reg ## _OFST,		\
		    (_eop), B_TRUE);					\
		EFSYS_PROBE6(efx_bar_reado, const char *, #_reg,	\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_WRITEO(_enp, _reg, _eop)				\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE6(efx_bar_writeo, const char *, #_reg,	\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
		EFSYS_BAR_WRITEO((_enp)->en_esbp, _reg ## _OFST,	\
		    (_eop), B_TRUE);					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/*
 * Accessors for memory BAR non-VI tables.
 *
 * Code used on EF10 *must* use EFX_BAR_VI_*() macros for per-VI registers,
 * to ensure the correct runtime VI window size is used on Medford2.
 *
 * Siena-only code may continue using EFX_BAR_TBL_*() macros for VI registers.
 */

#define	EFX_BAR_TBL_READD(_enp, _reg, _index, _edp, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_BAR_READD((_enp)->en_esbp,			\
		    (_reg ## _OFST + ((_index) * _reg ## _STEP)),	\
		    (_edp), (_lock));					\
		EFSYS_PROBE4(efx_bar_tbl_readd, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_TBL_WRITED(_enp, _reg, _index, _edp, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE4(efx_bar_tbl_writed, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
		EFSYS_BAR_WRITED((_enp)->en_esbp,			\
		    (_reg ## _OFST + ((_index) * _reg ## _STEP)),	\
		    (_edp), (_lock));					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_TBL_WRITED3(_enp, _reg, _index, _edp, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE4(efx_bar_tbl_writed, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
		EFSYS_BAR_WRITED((_enp)->en_esbp,			\
		    (_reg ## _OFST +					\
		    (3 * sizeof (efx_dword_t)) +			\
		    ((_index) * _reg ## _STEP)),			\
		    (_edp), (_lock));					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_TBL_READQ(_enp, _reg, _index, _eqp)			\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_BAR_READQ((_enp)->en_esbp,			\
		    (_reg ## _OFST + ((_index) * _reg ## _STEP)),	\
		    (_eqp));						\
		EFSYS_PROBE5(efx_bar_tbl_readq, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_TBL_WRITEQ(_enp, _reg, _index, _eqp)			\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE5(efx_bar_tbl_writeq, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
		EFSYS_BAR_WRITEQ((_enp)->en_esbp,			\
		    (_reg ## _OFST + ((_index) * _reg ## _STEP)),	\
		    (_eqp));						\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_TBL_READO(_enp, _reg, _index, _eop, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_BAR_READO((_enp)->en_esbp,			\
		    (_reg ## _OFST + ((_index) * _reg ## _STEP)),	\
		    (_eop), (_lock));					\
		EFSYS_PROBE7(efx_bar_tbl_reado, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_TBL_WRITEO(_enp, _reg, _index, _eop, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE7(efx_bar_tbl_writeo, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
		EFSYS_BAR_WRITEO((_enp)->en_esbp,			\
		    (_reg ## _OFST + ((_index) * _reg ## _STEP)),	\
		    (_eop), (_lock));					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/*
 * Accessors for memory BAR per-VI registers.
 *
 * The VI window size is 8KB for Medford and all earlier controllers.
 * For Medford2, the VI window size can be 8KB, 16KB or 64KB.
 */

#define	EFX_BAR_VI_READD(_enp, _reg, _index, _edp, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_BAR_READD((_enp)->en_esbp,			\
		    ((_reg ## _OFST) +					\
		    ((_index) << (_enp)->en_nic_cfg.enc_vi_window_shift)), \
		    (_edp), (_lock));					\
		EFSYS_PROBE4(efx_bar_vi_readd, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_VI_WRITED(_enp, _reg, _index, _edp, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE4(efx_bar_vi_writed, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
		EFSYS_BAR_WRITED((_enp)->en_esbp,			\
		    ((_reg ## _OFST) +					\
		    ((_index) << (_enp)->en_nic_cfg.enc_vi_window_shift)), \
		    (_edp), (_lock));					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_BAR_VI_WRITED2(_enp, _reg, _index, _edp, _lock)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE4(efx_bar_vi_writed, const char *, #_reg,	\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_edp)->ed_u32[0]);			\
		EFSYS_BAR_WRITED((_enp)->en_esbp,			\
		    ((_reg ## _OFST) +					\
		    (2 * sizeof (efx_dword_t)) +			\
		    ((_index) << (_enp)->en_nic_cfg.enc_vi_window_shift)), \
		    (_edp), (_lock));					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/*
 * Allow drivers to perform optimised 128-bit VI doorbell writes.
 * The DMA descriptor pointers (RX_DESC_UPD and TX_DESC_UPD) are
 * special-cased in the BIU on the Falcon/Siena and EF10 architectures to avoid
 * the need for locking in the host, and are the only ones known to be safe to
 * use 128-bites write with.
 */
#define	EFX_BAR_VI_DOORBELL_WRITEO(_enp, _reg, _index, _eop)		\
	do {								\
		EFX_CHECK_REG((_enp), (_reg));				\
		EFSYS_PROBE7(efx_bar_vi_doorbell_writeo,		\
		    const char *, #_reg,				\
		    uint32_t, (_index),					\
		    uint32_t, _reg ## _OFST,				\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
		EFSYS_BAR_DOORBELL_WRITEO((_enp)->en_esbp,		\
		    (_reg ## _OFST +					\
		    ((_index) << (_enp)->en_nic_cfg.enc_vi_window_shift)), \
		    (_eop));						\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_DMA_SYNC_QUEUE_FOR_DEVICE(_esmp, _entries, _wptr, _owptr)	\
	do {								\
		unsigned int _new = (_wptr);				\
		unsigned int _old = (_owptr);				\
									\
		if ((_new) >= (_old))					\
			EFSYS_DMA_SYNC_FOR_DEVICE((_esmp),		\
			    (_old) * sizeof (efx_desc_t),		\
			    ((_new) - (_old)) * sizeof (efx_desc_t));	\
		else							\
			/*						\
			 * It is cheaper to sync entire map than sync	\
			 * two parts especially when offset/size are	\
			 * ignored and entire map is synced in any case.\
			 */						\
			EFSYS_DMA_SYNC_FOR_DEVICE((_esmp),		\
			    0,						\
			    (_entries) * sizeof (efx_desc_t));		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

extern	__checkReturn	efx_rc_t
efx_mac_select(
	__in		efx_nic_t *enp);

extern	void
efx_mac_multicast_hash_compute(
	__in_ecount(6*count)		uint8_t const *addrs,
	__in				int count,
	__out				efx_oword_t *hash_low,
	__out				efx_oword_t *hash_high);

extern	__checkReturn	efx_rc_t
efx_phy_probe(
	__in		efx_nic_t *enp);

extern			void
efx_phy_unprobe(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_VPD

/* VPD utility functions */

extern	__checkReturn		efx_rc_t
efx_vpd_hunk_length(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__out			size_t *lengthp);

extern	__checkReturn		efx_rc_t
efx_vpd_hunk_verify(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__out_opt		boolean_t *cksummedp);

extern	__checkReturn		efx_rc_t
efx_vpd_hunk_reinit(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			boolean_t wantpid);

extern	__checkReturn		efx_rc_t
efx_vpd_hunk_get(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_tag_t tag,
	__in			efx_vpd_keyword_t keyword,
	__out			unsigned int *payloadp,
	__out			uint8_t *paylenp);

extern	__checkReturn			efx_rc_t
efx_vpd_hunk_next(
	__in_bcount(size)		caddr_t data,
	__in				size_t size,
	__out				efx_vpd_tag_t *tagp,
	__out				efx_vpd_keyword_t *keyword,
	__out_opt			unsigned int *payloadp,
	__out_opt			uint8_t *paylenp,
	__inout				unsigned int *contp);

extern	__checkReturn		efx_rc_t
efx_vpd_hunk_set(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_value_t *evvp);

#endif	/* EFSYS_OPT_VPD */

#if EFSYS_OPT_MCDI

extern	__checkReturn		efx_rc_t
efx_mcdi_set_workaround(
	__in			efx_nic_t *enp,
	__in			uint32_t type,
	__in			boolean_t enabled,
	__out_opt		uint32_t *flagsp);

extern	__checkReturn		efx_rc_t
efx_mcdi_get_workarounds(
	__in			efx_nic_t *enp,
	__out_opt		uint32_t *implementedp,
	__out_opt		uint32_t *enabledp);

#endif /* EFSYS_OPT_MCDI */

#if EFSYS_OPT_MAC_STATS

/*
 * Closed range of stats (i.e. the first and the last are included).
 * The last must be greater or equal (if the range is one item only) to
 * the first.
 */
struct efx_mac_stats_range {
	efx_mac_stat_t		first;
	efx_mac_stat_t		last;
};

extern					efx_rc_t
efx_mac_stats_mask_add_ranges(
	__inout_bcount(mask_size)	uint32_t *maskp,
	__in				size_t mask_size,
	__in_ecount(rng_count)		const struct efx_mac_stats_range *rngp,
	__in				unsigned int rng_count);

#endif	/* EFSYS_OPT_MAC_STATS */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EFX_IMPL_H */
