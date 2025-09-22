/*	$OpenBSD: qwzvar.h,v 1.12 2025/07/07 00:55:15 jsg Exp $	*/

/*
 * Copyright (c) 2018-2019 The Linux Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *  * Neither the name of [Owner Organization] nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef QWZ_DEBUG
#define DPRINTF(x...)		do { if (qwz_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (qwz_debug & n) printf(x); } while(0)
#define	QWZ_D_MISC		0x00000001
#define	QWZ_D_MHI		0x00000002
#define	QWZ_D_QMI		0x00000004
#define	QWZ_D_WMI		0x00000008
#define	QWZ_D_HTC		0x00000010
#define	QWZ_D_HTT		0x00000020
#define	QWZ_D_MAC		0x00000040
#define	QWZ_D_MGMT		0x00000080
#define	QWZ_D_CE		0x00000100
extern uint32_t	qwz_debug;
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

struct qwz_softc;

#define ATH12K_EXT_IRQ_GRP_NUM_MAX 11

struct ath12k_hw_ring_mask {
	uint8_t tx[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	uint8_t rx_mon_dest[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	uint8_t rx[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	uint8_t rx_err[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	uint8_t rx_wbm_rel[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	uint8_t reo_status[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	uint8_t host2rxdma[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	uint8_t tx_mon_dest[ATH12K_EXT_IRQ_GRP_NUM_MAX];
};

#define ATH12K_FW_DIR			"qwz"

#define ATH12K_BOARD_MAGIC		"QCA-ATH12K-BOARD"
#define ATH12K_BOARD_API2_FILE		"board-2"
#define ATH12K_DEFAULT_BOARD_FILE	"board"
#define ATH12K_DEFAULT_CAL_FILE		"caldata"
#define ATH12K_AMSS_FILE		"amss"
#define ATH12K_M3_FILE			"m3"

#define QWZ_FW_BUILD_ID_MASK "QC_IMAGE_VERSION_STRING="

struct ath12k_hal_tcl_to_wbm_rbm_map {
	uint8_t wbm_ring_num;
	uint8_t rbm_id;
};

/**
 * enum hal_rx_buf_return_buf_manager
 *
 * @HAL_RX_BUF_RBM_WBM_IDLE_BUF_LIST: Buffer returned to WBM idle buffer list
 * @HAL_RX_BUF_RBM_WBM_CHIP0_IDLE_DESC_LIST: Descriptor returned to WBM idle
 * 	descriptor list, where the chip 0 WBM is chosen in case of a
 *  	multi-chip config
 * @HAL_RX_BUF_RBM_FW_BM: Buffer returned to FW
 * @HAL_RX_BUF_RBM_SW0_BM: For Tx completion -- returned to host
 * @HAL_RX_BUF_RBM_SW1_BM: For Tx completion -- returned to host
 * @HAL_RX_BUF_RBM_SW2_BM: For Tx completion -- returned to host
 * @HAL_RX_BUF_RBM_SW3_BM: For Rx release -- returned to host
 * @HAL_RX_BUF_RBM_SW4_BM: For Tx completion -- returned to host
 * @HAL_RX_BUF_RBM_SW5_BM: For ring 5 -- returned to host
 * @HAL_RX_BUF_RBM_SW6_BM: For ring 6 -- returned to host
 */

enum hal_rx_buf_return_buf_manager {
	HAL_RX_BUF_RBM_WBM_IDLE_BUF_LIST,
	HAL_RX_BUF_RBM_WBM_DEV0_IDLE_DESC_LIST,
	HAL_RX_BUF_RBM_WBM_DEV1_IDLE_DESC_LIST,
	HAL_RX_BUF_RBM_WBM_DEV2_IDLE_DESC_LIST,
	HAL_RX_BUF_RBM_FW_BM,
	HAL_RX_BUF_RBM_SW0_BM,
	HAL_RX_BUF_RBM_SW1_BM,
	HAL_RX_BUF_RBM_SW2_BM,
	HAL_RX_BUF_RBM_SW3_BM,
	HAL_RX_BUF_RBM_SW4_BM,
	HAL_RX_BUF_RBM_SW5_BM,
	HAL_RX_BUF_RBM_SW6_BM,
};

struct ath12k_hw_hal_params {
	enum hal_rx_buf_return_buf_manager rx_buf_rbm;
	const struct ath12k_hw_tcl2wbm_rbm_map *tcl2wbm_rbm_map;
	uint32_t wbm2sw_cc_enable;
};

struct hal_tx_info {
	uint16_t meta_data_flags; /* %HAL_TCL_DATA_CMD_INFO0_META_ */
	uint8_t ring_id;
	uint32_t desc_id;
	enum hal_tcl_desc_type type;
	enum hal_tcl_encap_type encap_type;
	uint64_t paddr;
	uint32_t data_len;
	uint32_t pkt_offset;
	enum hal_encrypt_type encrypt_type;
	uint32_t flags0; /* %HAL_TCL_DATA_CMD_INFO1_ */
	uint32_t flags1; /* %HAL_TCL_DATA_CMD_INFO2_ */
	uint16_t addr_search_flags; /* %HAL_TCL_DATA_CMD_INFO0_ADDR(X/Y)_ */
	uint16_t bss_ast_hash;
	uint16_t bss_ast_idx;
	uint8_t tid;
	uint8_t search_type; /* %HAL_TX_ADDR_SEARCH_ */
	uint8_t lmac_id;
	uint8_t dscp_tid_tbl_idx;
	bool enable_mesh;
	uint8_t rbm_id;
};

/* TODO: Check if the actual desc macros can be used instead */
#define HAL_TX_STATUS_FLAGS_FIRST_MSDU		BIT(0)
#define HAL_TX_STATUS_FLAGS_LAST_MSDU		BIT(1)
#define HAL_TX_STATUS_FLAGS_MSDU_IN_AMSDU	BIT(2)
#define HAL_TX_STATUS_FLAGS_RATE_STATS_VALID	BIT(3)
#define HAL_TX_STATUS_FLAGS_RATE_LDPC		BIT(4)
#define HAL_TX_STATUS_FLAGS_RATE_STBC		BIT(5)
#define HAL_TX_STATUS_FLAGS_OFDMA		BIT(6)

#define HAL_TX_STATUS_DESC_LEN		sizeof(struct hal_wbm_release_ring)

/* Tx status parsed from srng desc */
struct hal_tx_status {
	enum hal_wbm_rel_src_module buf_rel_source;
	enum hal_wbm_tqm_rel_reason status;
	uint8_t ack_rssi;
	uint32_t flags; /* %HAL_TX_STATUS_FLAGS_ */
	uint32_t ppdu_id;
	uint8_t try_cnt;
	uint8_t tid;
	uint16_t peer_id;
	uint32_t rate_stats;
};

struct hal_ops {
	int (*create_srng_config)(struct qwz_softc *);
	uint16_t (*rxdma_ring_wmask_rx_mpdu_start)(void);
	uint32_t (*rxdma_ring_wmask_rx_msdu_end)(void);
	const struct hal_rx_ops *(*get_hal_rx_compact_ops)(void);
	const struct ath12k_hal_tcl_to_wbm_rbm_map *tcl_to_wbm_rbm_map;
};

struct ath12k_hw_params {
	const char *name;
	uint16_t hw_rev;
	uint8_t max_radios;
	uint32_t bdf_addr;

	struct {
		const char *dir;
		size_t board_size;
		size_t cal_offset;
	} fw;

	const struct ath12k_hw_ops *hw_ops;
	const struct ath12k_hw_ring_mask *ring_mask;

	bool internal_sleep_clock;

	const struct ath12k_hw_regs *regs;
	uint32_t qmi_service_ins_id;
	const struct ce_attr *host_ce_config;
	uint32_t ce_count;
	const struct ce_pipe_config *target_ce_config;
	uint32_t target_ce_count;
	const struct service_to_pipe *svc_to_ce_map;
	uint32_t svc_to_ce_map_len;

	bool single_pdev_only;

	bool rxdma1_enable;
	int num_rxmda_per_pdev;
	int num_rxdma_dst_ring;
	bool rx_mac_buf_ring;
	bool vdev_start_delay;
	bool htt_peer_map_v2;
#if notyet
	struct {
		uint8_t fft_sz;
		uint8_t fft_pad_sz;
		uint8_t summary_pad_sz;
		uint8_t fft_hdr_len;
		uint16_t max_fft_bins;
		bool fragment_160mhz;
	} spectral;

	uint16_t interface_modes;
	bool supports_monitor;
	bool full_monitor_mode;
#endif
	bool reoq_lut_support;
	bool supports_shadow_regs;
	bool idle_ps;
	bool supports_sta_ps;
	bool supports_suspend;
	uint32_t hal_desc_sz;
	bool fix_l1ss;
	uint32_t num_tcl_banks;
	uint32_t max_tx_ring;
	const struct ath12k_hw_hal_params *hal_params;
	void (*wmi_init)(struct qwz_softc *sc,
	    struct wmi_resource_config_arg *config);
	const struct hal_ops *hal_ops;
	uint64_t qmi_cnss_feature_bitmap;
#if notyet
	bool supports_dynamic_smps_6ghz;
	bool alloc_cacheable_memory;
	bool supports_rssi_stats;
#endif
	bool current_cc_support;
	bool dbr_debug_support;
#ifdef notyet
	const struct cfg80211_sar_capa *bios_sar_capa;
	bool support_off_channel_tx;
	bool supports_multi_bssid;

	struct {
		uint32_t start;
		uint32_t end;
	} sram_dump;

	bool tcl_ring_retry;
#endif
	uint32_t tx_ring_size;
	bool smp2p_wow_exit;
};

struct ath12k_hw_ops {
	uint8_t (*get_hw_mac_from_pdev_id)(int pdev_id);
	int (*mac_id_to_pdev_id)(struct ath12k_hw_params *hw, int mac_id);
	int (*mac_id_to_srng_id)(struct ath12k_hw_params *hw, int mac_id);
	bool (*dp_srng_is_tx_comp_ring)(int ring_num);
};

struct hal_rx_ops {
	int (*rx_desc_get_first_msdu)(struct hal_rx_desc *desc);
#if notyet
	bool (*rx_desc_get_last_msdu)(struct hal_rx_desc *desc);
#endif
	uint8_t (*rx_desc_get_l3_pad_bytes)(struct hal_rx_desc *desc);
	uint8_t *(*rx_desc_get_hdr_status)(struct hal_rx_desc *desc);
	int (*rx_desc_encrypt_valid)(struct hal_rx_desc *desc);
	uint32_t (*rx_desc_get_encrypt_type)(struct hal_rx_desc *desc);
	uint8_t (*rx_desc_get_decap_type)(struct hal_rx_desc *desc);
#ifdef notyet
	uint8_t (*rx_desc_get_mesh_ctl)(struct hal_rx_desc *desc);
	bool (*rx_desc_get_mpdu_seq_ctl_vld)(struct hal_rx_desc *desc);
	bool (*rx_desc_get_mpdu_fc_valid)(struct hal_rx_desc *desc);
	uint16_t (*rx_desc_get_mpdu_start_seq_no)(struct hal_rx_desc *desc);
#endif
	uint16_t (*rx_desc_get_msdu_len)(struct hal_rx_desc *desc);
#ifdef notyet
	uint8_t (*rx_desc_get_msdu_sgi)(struct hal_rx_desc *desc);
	uint8_t (*rx_desc_get_msdu_rate_mcs)(struct hal_rx_desc *desc);
	uint8_t (*rx_desc_get_msdu_rx_bw)(struct hal_rx_desc *desc);
#endif
	uint32_t (*rx_desc_get_msdu_freq)(struct hal_rx_desc *desc);
	uint8_t (*rx_desc_get_msdu_pkt_type)(struct hal_rx_desc *desc);
	uint8_t (*rx_desc_get_msdu_nss)(struct hal_rx_desc *desc);
	uint8_t (*rx_desc_get_mpdu_tid)(struct hal_rx_desc *desc);
	uint16_t (*rx_desc_get_mpdu_peer_id)(struct hal_rx_desc *desc);
	void (*rx_desc_copy_end_tlv)(struct hal_rx_desc *fdesc,
				     struct hal_rx_desc *ldesc);
	uint32_t (*rx_desc_get_mpdu_start_tag)(struct hal_rx_desc *desc);
	uint32_t (*rx_desc_get_mpdu_ppdu_id)(struct hal_rx_desc *desc);
	void (*rx_desc_set_msdu_len)(struct hal_rx_desc *desc, uint16_t len);
#ifdef notyet
	uint8_t *(*rx_desc_get_msdu_payload)(struct hal_rx_desc *desc);
	uint32_t (*rx_desc_get_mpdu_start_offset)(void);
	uint32_t (*rx_desc_get_msdu_end_offset)(void);
	bool (*rx_desc_mac_addr2_valid)(struct hal_rx_desc *desc);
	uint8_t* (*rx_desc_mpdu_start_addr2)(struct hal_rx_desc *desc);
#endif
	int (*rx_desc_is_da_mcbc)(struct hal_rx_desc *desc);
#ifdef notyet
	void (*rx_desc_get_dot11_hdr)(struct hal_rx_desc *desc,
				      struct ieee80211_hdr *hdr);
	uint16_t (*rx_desc_get_mpdu_frame_ctl)(struct hal_rx_desc *desc);
	void (*rx_desc_get_crypto_header)(struct hal_rx_desc *desc,
					  uint8_t *crypto_hdr,
					  enum hal_encrypt_type enctype);
#endif
	bool (*dp_rx_h_msdu_done)(struct hal_rx_desc *desc);
#ifdef notyet
	bool (*dp_rx_h_l4_cksum_fail)(struct hal_rx_desc *desc);
	bool (*dp_rx_h_ip_cksum_fail)(struct hal_rx_desc *desc);
#endif
	int (*dp_rx_h_is_decrypted)(struct hal_rx_desc *desc);
	uint32_t (*dp_rx_h_mpdu_err)(struct hal_rx_desc *desc);
	uint32_t (*rx_desc_get_desc_size)(void);
#ifdef notyet
	uint8_t (*rx_desc_get_msdu_src_link_id)(struct hal_rx_desc *desc);
#endif
};

extern const struct hal_rx_ops hal_rx_wcn7850_ops;

extern const struct ath12k_hw_ring_mask ath12k_hw_ring_mask_wcn7850;

struct ath12k_hw_regs {
	uint32_t hal_tcl1_ring_id;
	uint32_t hal_tcl1_ring_misc;
	uint32_t hal_tcl1_ring_tp_addr_lsb;
	uint32_t hal_tcl1_ring_tp_addr_msb;
	uint32_t hal_tcl1_ring_consumer_int_setup_ix0;
	uint32_t hal_tcl1_ring_consumer_int_setup_ix1;
	uint32_t hal_tcl1_ring_msi1_base_lsb;
	uint32_t hal_tcl1_ring_msi1_base_msb;
	uint32_t hal_tcl1_ring_msi1_data;
	uint32_t hal_tcl_ring_base_lsb;

	uint32_t hal_tcl_status_ring_base_lsb;

	uint32_t hal_wbm_idle_ring_base_lsb;
	uint32_t hal_wbm_idle_ring_misc_addr;
	uint32_t hal_wbm_r0_idle_list_cntl_addr;
	uint32_t hal_wbm_r0_idle_list_size_addr;
	uint32_t hal_wbm_scattered_ring_base_lsb;
	uint32_t hal_wbm_scattered_ring_base_msb;
	uint32_t hal_wbm_scattered_desc_head_info_ix0;
	uint32_t hal_wbm_scattered_desc_head_info_ix1;
	uint32_t hal_wbm_scattered_desc_tail_info_ix0;
	uint32_t hal_wbm_scattered_desc_tail_info_ix1;
	uint32_t hal_wbm_scattered_desc_ptr_hp_addr;

	uint32_t hal_wbm_sw_release_ring_base_lsb;
	uint32_t hal_wbm_sw1_release_ring_base_lsb;
	uint32_t hal_wbm0_release_ring_base_lsb;
	uint32_t hal_wbm1_release_ring_base_lsb;

	uint32_t pcie_qserdes_sysclk_en_sel;
	uint32_t pcie_pcs_osc_dtct_config_base;

	uint32_t hal_ppe_rel_ring_base;

	uint32_t hal_reo2_ring_base;
	uint32_t hal_reo1_misc_ctrl_addr;
	uint32_t hal_reo1_sw_cookie_cfg0;
	uint32_t hal_reo1_sw_cookie_cfg1;
	uint32_t hal_reo1_qdesc_lut_base0;
	uint32_t hal_reo1_qdesc_lut_base1;
	uint32_t hal_reo1_ring_base_lsb;
	uint32_t hal_reo1_ring_base_msb;
	uint32_t hal_reo1_ring_id;
	uint32_t hal_reo1_ring_misc;
	uint32_t hal_reo1_ring_hp_addr_lsb;
	uint32_t hal_reo1_ring_hp_addr_msb;
	uint32_t hal_reo1_ring_producer_int_setup;
	uint32_t hal_reo1_ring_msi1_base_lsb;
	uint32_t hal_reo1_ring_msi1_base_msb;
	uint32_t hal_reo1_ring_msi1_data;
	uint32_t hal_reo1_aging_thres_ix0;
	uint32_t hal_reo1_aging_thres_ix1;
	uint32_t hal_reo1_aging_thres_ix2;
	uint32_t hal_reo1_aging_thres_ix3;

	uint32_t hal_reo2_sw0_ring_base;

	uint32_t hal_sw2reo_ring_base;
	uint32_t hal_sw2reo1_ring_base;

	uint32_t hal_reo_cmd_ring_base;

	uint32_t hal_reo_status_ring_base;
};

extern const struct ath12k_hw_regs wcn7850_regs;

enum ath12k_dev_flags {
	ATH12K_CAC_RUNNING,
	ATH12K_FLAG_CORE_REGISTERED,
	ATH12K_FLAG_CRASH_FLUSH,
	ATH12K_FLAG_RAW_MODE,
	ATH12K_FLAG_HW_CRYPTO_DISABLED,
	ATH12K_FLAG_BTCOEX,
	ATH12K_FLAG_RECOVERY,
	ATH12K_FLAG_UNREGISTERING,
	ATH12K_FLAG_REGISTERED,
	ATH12K_FLAG_QMI_FAIL,
	ATH12K_FLAG_HTC_SUSPEND_COMPLETE,
	ATH12K_FLAG_CE_IRQ_ENABLED,
	ATH12K_FLAG_EXT_IRQ_ENABLED,
	ATH12K_FLAG_FIXED_MEM_RGN,
	ATH12K_FLAG_DEVICE_INIT_DONE,
	ATH12K_FLAG_MULTI_MSI_VECTORS,
};

enum ath12k_scan_state {
	ATH12K_SCAN_IDLE,
	ATH12K_SCAN_STARTING,
	ATH12K_SCAN_RUNNING,
	ATH12K_SCAN_ABORTING,
};

enum ath12k_11d_state {
	ATH12K_11D_IDLE,
	ATH12K_11D_PREPARING,
	ATH12K_11D_RUNNING,
};

/* enum ath12k_spectral_mode:
 *
 * @SPECTRAL_DISABLED: spectral mode is disabled
 * @SPECTRAL_BACKGROUND: hardware sends samples when it is not busy with
 *	something else.
 * @SPECTRAL_MANUAL: spectral scan is enabled, triggering for samples
 *	is performed manually.
 */
enum ath12k_spectral_mode {
	ATH12K_SPECTRAL_DISABLED = 0,
	ATH12K_SPECTRAL_BACKGROUND,
	ATH12K_SPECTRAL_MANUAL,
};

#define QWZ_SCAN_11D_INTERVAL		600000
#define QWZ_11D_INVALID_VDEV_ID		0xFFFF

struct qwz_ops {
	uint32_t	(*read32)(struct qwz_softc *, uint32_t);
	void		(*write32)(struct qwz_softc *, uint32_t, uint32_t);
	int		(*start)(struct qwz_softc *);
	void		(*stop)(struct qwz_softc *);
	int		(*power_up)(struct qwz_softc *);
	void		(*power_down)(struct qwz_softc *);
	int		(*submit_xfer)(struct qwz_softc *, struct mbuf *);
	void		(*irq_enable)(struct qwz_softc *sc);
	void		(*irq_disable)(struct qwz_softc *sc);
	int		(*map_service_to_pipe)(struct qwz_softc *, uint16_t,
			    uint8_t *, uint8_t *);
	int		(*get_user_msi_vector)(struct qwz_softc *, char *,
			    int *, uint32_t *, uint32_t *);
};

struct qwz_dmamem {
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	size_t			size;
	caddr_t			kva;
};

struct qwz_dmamem *qwz_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t);
void qwz_dmamem_free(bus_dma_tag_t, struct qwz_dmamem *);

#define QWZ_DMA_MAP(_adm)	((_adm)->map)
#define QWZ_DMA_LEN(_adm)	((_adm)->size)
#define QWZ_DMA_DVA(_adm)	((_adm)->map->dm_segs[0].ds_addr)
#define QWZ_DMA_KVA(_adm)	((void *)(_adm)->kva)

struct hal_srng_params {
	bus_addr_t ring_base_paddr;
	uint32_t *ring_base_vaddr;
	int num_entries;
	uint32_t intr_batch_cntr_thres_entries;
	uint32_t intr_timer_thres_us;
	uint32_t flags;
	uint32_t max_buffer_len;
	uint32_t low_threshold;
	uint64_t msi_addr;
	uint32_t msi_data;

	/* Add more params as needed */
};

enum hal_srng_dir {
	HAL_SRNG_DIR_SRC,
	HAL_SRNG_DIR_DST
};

/* srng flags */
#define HAL_SRNG_FLAGS_MSI_SWAP			0x00000008
#define HAL_SRNG_FLAGS_RING_PTR_SWAP		0x00000010
#define HAL_SRNG_FLAGS_DATA_TLV_SWAP		0x00000020
#define HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN	0x00010000
#define HAL_SRNG_FLAGS_MSI_INTR			0x00020000
#define HAL_SRNG_FLAGS_HIGH_THRESH_INTR_EN	0x00080000
#define HAL_SRNG_FLAGS_LMAC_RING		0x80000000

#define HAL_SRNG_TLV_HDR_TAG		GENMASK(9, 1)
#define HAL_SRNG_TLV_HDR_LEN		GENMASK(25, 10)

/* Common SRNG ring structure for source and destination rings */
struct hal_srng {
	/* Unique SRNG ring ID */
	uint8_t ring_id;

	/* Ring initialization done */
	uint8_t initialized;

	/* Interrupt/MSI value assigned to this ring */
	int irq;

	/* Physical base address of the ring */
	bus_addr_t ring_base_paddr;

	/* Virtual base address of the ring */
	uint32_t *ring_base_vaddr;

	/* Number of entries in ring */
	uint32_t num_entries;

	/* Ring size */
	uint32_t ring_size;

	/* Ring size mask */
	uint32_t ring_size_mask;

	/* Size of ring entry */
	uint32_t entry_size;

	/* Interrupt timer threshold - in micro seconds */
	uint32_t intr_timer_thres_us;

	/* Interrupt batch counter threshold - in number of ring entries */
	uint32_t intr_batch_cntr_thres_entries;

	/* MSI Address */
	bus_addr_t msi_addr;

	/* MSI data */
	uint32_t msi_data;

	/* Misc flags */
	uint32_t flags;
#ifdef notyet
	/* Lock for serializing ring index updates */
	spinlock_t lock;
#endif
	/* Start offset of SRNG register groups for this ring
	 * TBD: See if this is required - register address can be derived
	 * from ring ID
	 */
	uint32_t hwreg_base[HAL_SRNG_NUM_REG_GRP];

	uint64_t timestamp;

	/* Source or Destination ring */
	enum hal_srng_dir ring_dir;

	union {
		struct {
			/* SW tail pointer */
			uint32_t tp;

			/* Shadow head pointer location to be updated by HW */
			volatile uint32_t *hp_addr;

			/* Cached head pointer */
			uint32_t cached_hp;

			/* Tail pointer location to be updated by SW - This
			 * will be a register address and need not be
			 * accessed through SW structure
			 */
			uint32_t *tp_addr;

			/* Current SW loop cnt */
			uint32_t loop_cnt;

			/* max transfer size */
			uint16_t max_buffer_length;

			/* head pointer at access end */
			uint32_t last_hp;
		} dst_ring;

		struct {
			/* SW head pointer */
			uint32_t hp;

			/* SW reap head pointer */
			uint32_t reap_hp;

			/* Shadow tail pointer location to be updated by HW */
			uint32_t *tp_addr;

			/* Cached tail pointer */
			uint32_t cached_tp;

			/* Head pointer location to be updated by SW - This
			 * will be a register address and need not be accessed
			 * through SW structure
			 */
			uint32_t *hp_addr;

			/* Low threshold - in number of ring entries */
			uint32_t low_threshold;

			/* tail pointer at access end */
			uint32_t last_tp;
		} src_ring;
	} u;
};

enum hal_ring_type {
	HAL_REO_DST,
	HAL_REO_EXCEPTION,
	HAL_REO_REINJECT,
	HAL_REO_CMD,
	HAL_REO_STATUS,
	HAL_TCL_DATA,
	HAL_TCL_CMD,
	HAL_TCL_STATUS,
	HAL_CE_SRC,
	HAL_CE_DST,
	HAL_CE_DST_STATUS,
	HAL_WBM_IDLE_LINK,
	HAL_SW2WBM_RELEASE,
	HAL_WBM2SW_RELEASE,
	HAL_RXDMA_BUF,
	HAL_RXDMA_DST,
	HAL_RXDMA_MONITOR_BUF,
	HAL_RXDMA_MONITOR_STATUS,
	HAL_RXDMA_MONITOR_DST,
	HAL_RXDMA_MONITOR_DESC,
	HAL_RXDMA_DIR_BUF,
	HAL_PPE2TCL,
	HAL_PPE_RELEASE,
	HAL_TX_MONITOR_BUF,
	HAL_TX_MONITOR_DST,
	HAL_MAX_RING_TYPES,
};

enum hal_srng_mac_type {
	ATH12K_HAL_SRNG_UMAC,
	ATH12K_HAL_SRNG_DMAC,
	ATH12K_HAL_SRNG_PMAC
};

/* HW SRNG configuration table */
struct hal_srng_config {
	int start_ring_id;
	uint16_t max_rings;
	uint16_t entry_size;
	uint32_t reg_start[HAL_SRNG_NUM_REG_GRP];
	uint16_t reg_size[HAL_SRNG_NUM_REG_GRP];
	enum hal_srng_mac_type mac_type;
	enum hal_srng_dir ring_dir;
	uint32_t max_size;
};

#define QWZ_NUM_SRNG_CFG	21

struct hal_reo_status_header {
	uint16_t cmd_num;
	enum hal_reo_cmd_status cmd_status;
	uint16_t cmd_exe_time;
	uint32_t timestamp;
};

struct hal_reo_status_queue_stats {
	uint16_t ssn;
	uint16_t curr_idx;
	uint32_t pn[4];
	uint32_t last_rx_queue_ts;
	uint32_t last_rx_dequeue_ts;
	uint32_t rx_bitmap[8]; /* Bitmap from 0-255 */
	uint32_t curr_mpdu_cnt;
	uint32_t curr_msdu_cnt;
	uint16_t fwd_due_to_bar_cnt;
	uint16_t dup_cnt;
	uint32_t frames_in_order_cnt;
	uint32_t num_mpdu_processed_cnt;
	uint32_t num_msdu_processed_cnt;
	uint32_t total_num_processed_byte_cnt;
	uint32_t late_rx_mpdu_cnt;
	uint32_t reorder_hole_cnt;
	uint8_t timeout_cnt;
	uint8_t bar_rx_cnt;
	uint8_t num_window_2k_jump_cnt;
};

struct hal_reo_status_flush_queue {
	bool err_detected;
};

enum hal_reo_status_flush_cache_err_code {
	HAL_REO_STATUS_FLUSH_CACHE_ERR_CODE_SUCCESS,
	HAL_REO_STATUS_FLUSH_CACHE_ERR_CODE_IN_USE,
	HAL_REO_STATUS_FLUSH_CACHE_ERR_CODE_NOT_FOUND,
};

struct hal_reo_status_flush_cache {
	bool err_detected;
	enum hal_reo_status_flush_cache_err_code err_code;
	bool cache_controller_flush_status_hit;
	uint8_t cache_controller_flush_status_desc_type;
	uint8_t cache_controller_flush_status_client_id;
	uint8_t cache_controller_flush_status_err;
	uint8_t cache_controller_flush_status_cnt;
};

enum hal_reo_status_unblock_cache_type {
	HAL_REO_STATUS_UNBLOCK_BLOCKING_RESOURCE,
	HAL_REO_STATUS_UNBLOCK_ENTIRE_CACHE_USAGE,
};

struct hal_reo_status_unblock_cache {
	bool err_detected;
	enum hal_reo_status_unblock_cache_type unblock_type;
};

struct hal_reo_status_flush_timeout_list {
	bool err_detected;
	bool list_empty;
	uint16_t release_desc_cnt;
	uint16_t fwd_buf_cnt;
};

enum hal_reo_threshold_idx {
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER0,
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER1,
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER2,
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER_SUM,
};

struct hal_reo_status_desc_thresh_reached {
	enum hal_reo_threshold_idx threshold_idx;
	uint32_t link_desc_counter0;
	uint32_t link_desc_counter1;
	uint32_t link_desc_counter2;
	uint32_t link_desc_counter_sum;
};

struct hal_reo_status {
	struct hal_reo_status_header uniform_hdr;
	uint8_t loop_cnt;
	union {
		struct hal_reo_status_queue_stats queue_stats;
		struct hal_reo_status_flush_queue flush_queue;
		struct hal_reo_status_flush_cache flush_cache;
		struct hal_reo_status_unblock_cache unblock_cache;
		struct hal_reo_status_flush_timeout_list timeout_list;
		struct hal_reo_status_desc_thresh_reached desc_thresh_reached;
	} u;
};

/* HAL context to be used to access SRNG APIs (currently used by data path
 * and transport (CE) modules)
 */
struct ath12k_hal {
	/* HAL internal state for all SRNG rings.
	 */
	struct hal_srng srng_list[HAL_SRNG_RING_ID_MAX];

	/* SRNG configuration table */
	struct hal_srng_config *srng_config;

	/* Remote pointer memory for HW/FW updates */
	struct qwz_dmamem *rdpmem;
	struct {
		uint32_t *vaddr;
		bus_addr_t paddr;
	} rdp;

	/* Shared memory for ring pointer updates from host to FW */
	struct qwz_dmamem *wrpmem;
	struct {
		uint32_t *vaddr;
		bus_addr_t paddr;
	} wrp;

	/* Available REO blocking resources bitmap */
	uint8_t avail_blk_resource;

	uint8_t current_blk_index;

	/* shadow register configuration */
	uint32_t shadow_reg_addr[HAL_SHADOW_NUM_REGS];
	int num_shadow_reg_configured;
#ifdef notyet
	struct lock_class_key srng_key[HAL_SRNG_RING_ID_MAX];
#endif

	uint32_t hal_desc_sz;
};

enum hal_pn_type {
	HAL_PN_TYPE_NONE,
	HAL_PN_TYPE_WPA,
	HAL_PN_TYPE_WAPI_EVEN,
	HAL_PN_TYPE_WAPI_UNEVEN,
};

enum hal_ce_desc {
	HAL_CE_DESC_SRC,
	HAL_CE_DESC_DST,
	HAL_CE_DESC_DST_STATUS,
};

struct ce_ie_addr {
	uint32_t ie1_reg_addr;
	uint32_t ie2_reg_addr;
	uint32_t ie3_reg_addr;
};

struct ce_remap {
	uint32_t base;
	uint32_t size;
};

struct ce_attr {
	/* CE_ATTR_* values */
	unsigned int flags;

	/* #entries in source ring - Must be a power of 2 */
	unsigned int src_nentries;

	/*
	 * Max source send size for this CE.
	 * This is also the minimum size of a destination buffer.
	 */
	unsigned int src_sz_max;

	/* #entries in destination ring - Must be a power of 2 */
	unsigned int dest_nentries;

	void (*recv_cb)(struct qwz_softc *, struct mbuf *);
};

#define CE_DESC_RING_ALIGN 8

struct qwz_rx_msdu {
	TAILQ_ENTRY(qwz_rx_msdu) entry;
	struct mbuf *m;
	struct ieee80211_rxinfo rxi;
	int is_first_msdu;
	int is_last_msdu;
	int is_continuation;
	int is_mcbc;
	int is_eapol;
	struct hal_rx_desc *rx_desc;
	uint8_t err_rel_src;
	uint8_t err_code;
	uint8_t mac_id;
	uint8_t unmapped;
	uint8_t is_frag;
	uint8_t tid;
	uint16_t peer_id;
	uint16_t seq_no;
};

TAILQ_HEAD(qwz_rx_msdu_list, qwz_rx_msdu);

struct qwz_rx_data {
	struct mbuf	*m;
	bus_dmamap_t	map;
	struct qwz_rx_msdu rx_msdu;
};

struct qwz_tx_data {
	struct ieee80211_node *ni;
	struct mbuf	*m;
	bus_dmamap_t	map;
	uint8_t eid;
	uint8_t flags;
	uint32_t cipher;
};

struct qwz_ce_ring {
	/* Number of entries in this ring; must be power of 2 */
	unsigned int nentries;
	unsigned int nentries_mask;

	/* For dest ring, this is the next index to be processed
	 * by software after it was/is received into.
	 *
	 * For src ring, this is the last descriptor that was sent
	 * and completion processed by software.
	 *
	 * Regardless of src or dest ring, this is an invariant
	 * (modulo ring size):
	 *     write index >= read index >= sw_index
	 */
	unsigned int sw_index;
	/* cached copy */
	unsigned int write_index;

	/* Start of DMA-coherent area reserved for descriptors */
	/* Host address space */
	caddr_t base_addr;

	/* DMA map for Tx/Rx descriptors. */
	bus_dmamap_t		dmap;
	bus_dma_segment_t	dsegs;
	int			nsegs;
	size_t			desc_sz;

	/* HAL ring id */
	uint32_t hal_ring_id;

	/*
	 * Per-transfer data.
	 * Size and type of this data depends on how the ring is used.
	 *
	 * For transfers using DMA, the context contains pointers to
	 * struct qwz_rx_data if this ring is a dest ring, or struct
	 * qwz_tx_data if this ring is a src ring. DMA maps are allocated
	 * when the device is started via sc->ops.start, and will be used
	 * to load mbufs for DMA transfers.
	 * In this case, the pointers MUST NOT be cleared until the device
	 * is stopped. Otherwise we'd lose track of our DMA mappings!
	 * The Linux ath12k driver works differently because it can store
	 * DMA mapping information in a Linux socket buffer structure, which
	 * is not possible with mbufs.
	 *
	 * Keep last.
	 */
	void *per_transfer_context[0];
};

void qwz_htc_rx_completion_handler(struct qwz_softc *, struct mbuf *);
void qwz_dp_htt_htc_t2h_msg_handler(struct qwz_softc *, struct mbuf *);

struct qwz_dp;

struct qwz_dp_htt_wbm_tx_status {
	uint32_t msdu_id;
	int acked;
	int ack_rssi;
	uint16_t peer_id;
};

#define DP_NUM_CLIENTS_MAX 64
#define DP_AVG_TIDS_PER_CLIENT 2
#define DP_NUM_TIDS_MAX (DP_NUM_CLIENTS_MAX * DP_AVG_TIDS_PER_CLIENT)
#define DP_AVG_MSDUS_PER_FLOW 128
#define DP_AVG_FLOWS_PER_TID 2
#define DP_AVG_MPDUS_PER_TID_MAX 128
#define DP_AVG_MSDUS_PER_MPDU 4

#define DP_RX_HASH_ENABLE	1 /* Enable hash based Rx steering */

#define DP_BA_WIN_SZ_MAX	256

#define DP_TCL_NUM_RING_MAX	4

#define DP_IDLE_SCATTER_BUFS_MAX 16

#define DP_WBM_RELEASE_RING_SIZE	64
#define DP_TCL_DATA_RING_SIZE		512
#define DP_TCL_DATA_RING_SIZE_WCN6750	2048
#define DP_TX_COMP_RING_SIZE		32768
#define DP_TX_IDR_SIZE			DP_TX_COMP_RING_SIZE
#define DP_TCL_CMD_RING_SIZE		32
#define DP_TCL_STATUS_RING_SIZE		32
#define DP_REO_DST_RING_MAX		4
#define DP_REO_DST_RING_SIZE		2048
#define DP_REO_REINJECT_RING_SIZE	32
#define DP_RX_RELEASE_RING_SIZE		1024
#define DP_REO_EXCEPTION_RING_SIZE	128
#define DP_REO_CMD_RING_SIZE		256
#define DP_REO_STATUS_RING_SIZE		2048
#define DP_RXDMA_BUF_RING_SIZE		4096
#define DP_RXDMA_REFILL_RING_SIZE	2048
#define DP_RXDMA_ERR_DST_RING_SIZE	1024
#define DP_RXDMA_MON_STATUS_RING_SIZE	1024
#define DP_RXDMA_MONITOR_BUF_RING_SIZE	4096
#define DP_RXDMA_MONITOR_DST_RING_SIZE	2048
#define DP_RXDMA_MONITOR_DESC_RING_SIZE	4096
#define DP_TX_MONITOR_BUF_RING_SIZE	4096
#define DP_TX_MONITOR_DEST_RING_SIZE	2048

#define DP_TX_MONITOR_BUF_SIZE		2048
#define DP_TX_MONITOR_BUF_SIZE_MIN	48
#define DP_TX_MONITOR_BUF_SIZE_MAX	8192

#define DP_RX_RELEASE_RING_NUM	3

#define DP_RX_BUFFER_SIZE	2048
#define	DP_RX_BUFFER_SIZE_LITE  1024
#define DP_RX_BUFFER_ALIGN_SIZE	128

#define DP_RXDMA_BUF_COOKIE_BUF_ID	GENMASK(17, 0)
#define DP_RXDMA_BUF_COOKIE_PDEV_ID	GENMASK(20, 18)

#define DP_HW2SW_MACID(mac_id) ((mac_id) ? ((mac_id) - 1) : 0)
#define DP_SW2HW_MACID(mac_id) ((mac_id) + 1)

#define DP_TX_DESC_ID_MAC_ID  GENMASK(1, 0)
#define DP_TX_DESC_ID_MSDU_ID GENMASK(18, 2)
#define DP_TX_DESC_ID_POOL_ID GENMASK(20, 19)

#define ATH12K_NUM_POOL_TX_DESC	32768

/* TODO: revisit this count during testing */
#define ATH12K_RX_DESC_COUNT	(12288)

#define ATH12K_PAGE_SIZE	PAGE_SIZE

/* Total 1024 entries in PPT, i.e 4K/4 considering 4K aligned
 * SPT pages which makes lower 12bits 0
 */
#define ATH12K_MAX_PPT_ENTRIES	1024

/* Total 512 entries in a SPT, i.e 4K Page/8 */
#define ATH12K_MAX_SPT_ENTRIES	512

#define ATH12K_NUM_RX_SPT_PAGES	((ATH12K_RX_DESC_COUNT) / ATH12K_MAX_SPT_ENTRIES)

#define ATH12K_TX_SPT_PAGES_PER_POOL (ATH12K_NUM_POOL_TX_DESC / \
					  ATH12K_MAX_SPT_ENTRIES)
#define ATH12K_NUM_TX_SPT_PAGES	(ATH12K_TX_SPT_PAGES_PER_POOL * ATH12K_HW_MAX_QUEUES)
#define ATH12K_NUM_SPT_PAGES	(ATH12K_NUM_RX_SPT_PAGES + ATH12K_NUM_TX_SPT_PAGES)

#define ATH12K_TX_SPT_PAGE_OFFSET 0
#define ATH12K_RX_SPT_PAGE_OFFSET ATH12K_NUM_TX_SPT_PAGES

/* The SPT pages are divided for RX and TX, first block for RX
 * and remaining for TX
 */
#define ATH12K_NUM_TX_SPT_PAGE_START ATH12K_NUM_RX_SPT_PAGES

#define ATH12K_DP_RX_DESC_MAGIC	0xBABABABA

/* 4K aligned address have last 12 bits set to 0, this check is done
 * so that two spt pages address can be stored per 8bytes
 * of CMEM (PPT)
 */
#define ATH12K_SPT_4K_ALIGN_CHECK 0xFFF
#define ATH12K_SPT_4K_ALIGN_OFFSET 12
#define ATH12K_PPT_ADDR_OFFSET(ppt_index) (4 * (ppt_index))

/* To indicate HW of CMEM address, b0-31 are cmem base received via QMI */
#define ATH12K_CMEM_ADDR_MSB 0x10

/* Of 20 bits cookie, b0-b8 is to indicate SPT offset and b9-19 for PPT */
#define ATH12K_CC_SPT_MSB 8
#define ATH12K_CC_PPT_MSB 19
#define ATH12K_CC_PPT_SHIFT 9
#define ATH12K_DP_CC_COOKIE_SPT	GENMASK(8, 0)
#define ATH12K_DP_CC_COOKIE_PPT	GENMASK(19, 9)

#define DP_REO_QREF_NUM		GENMASK(31, 16)
#define DP_MAX_PEER_ID		2047

/* Total size of the LUT is based on 2K peers, each having reference
 * for 17tids, note each entry is of type ath12k_reo_queue_ref
 * hence total size is 2048 * 17 * 8 = 278528
 */
#define DP_REOQ_LUT_SIZE	278528

/* Invalid TX Bank ID value */
#define DP_INVALID_BANK_ID -1

struct ath12k_dp_tx_bank_profile {
	uint8_t is_configured;
	uint32_t num_users;
	uint32_t bank_config;
};

struct qwz_hp_update_timer {
	struct timeout timer;
	int started;
	int init;
	uint32_t tx_num;
	uint32_t timer_tx_num;
	uint32_t ring_id;
	uint32_t interval;
	struct qwz_softc *sc;
};

struct ath12k_rx_desc_info {
	TAILQ_ENTRY(ath12k_rx_desc_info) entry;
	struct mbuf	*m;
	bus_dmamap_t	map;
	uint32_t cookie;
	uint32_t magic;
	uint8_t in_use		: 1,
	        reserved	: 7;
};

struct ath12k_tx_desc_info {
	TAILQ_ENTRY(ath12k_tx_desc_info) entry;
	struct mbuf	*m;
	bus_dmamap_t	map;
	uint32_t desc_id; /* Cookie */
	uint8_t mac_id;
	uint8_t pool_id;
};

struct ath12k_spt_info {
	struct qwz_dmamem *mem;
	struct ath12k_rx_desc_info *rxbaddr[ATH12K_NUM_RX_SPT_PAGES];
	struct ath12k_tx_desc_info *txbaddr[ATH12K_NUM_TX_SPT_PAGES];
};

struct dp_rx_tid {
	uint8_t tid;
	struct qwz_dmamem *mem;
	uint32_t *vaddr;
	uint64_t paddr;
	uint32_t size;
	uint32_t ba_win_sz;
	int active;

	/* Info related to rx fragments */
	uint32_t cur_sn;
	uint16_t last_frag_no;
	uint16_t rx_frag_bitmap;
#if 0
	struct sk_buff_head rx_frags;
	struct hal_reo_dest_ring *dst_ring_desc;

	/* Timer info related to fragments */
	struct timer_list frag_timer;
	struct ath12k_base *ab;
#endif
};

#define DP_REO_DESC_FREE_THRESHOLD  64
#define DP_REO_DESC_FREE_TIMEOUT_MS 1000
#define DP_MON_PURGE_TIMEOUT_MS     100
#define DP_MON_SERVICE_BUDGET       128

struct dp_reo_cache_flush_elem {
	TAILQ_ENTRY(dp_reo_cache_flush_elem) entry;
	struct dp_rx_tid data;
	uint64_t ts;
};

TAILQ_HEAD(dp_reo_cmd_cache_flush_head, dp_reo_cache_flush_elem);

struct dp_reo_cmd {
	TAILQ_ENTRY(dp_reo_cmd) entry;
	struct dp_rx_tid data;
	int cmd_num;
	void (*handler)(struct qwz_dp *, void *,
	    enum hal_reo_cmd_status status);
};

TAILQ_HEAD(dp_reo_cmd_head, dp_reo_cmd);

struct dp_srng {
	struct qwz_dmamem *mem;
	uint32_t *vaddr;
	bus_addr_t paddr;
	int size;
	uint32_t ring_id;
};

struct dp_tx_ring {
	uint8_t tcl_data_ring_id;
	struct dp_srng tcl_data_ring;
	struct dp_srng tcl_comp_ring;
	int cur;
	int queued;
	struct qwz_tx_data *data;
	struct hal_wbm_release_ring *tx_status;
	int tx_status_head;
	int tx_status_tail;
};


struct dp_link_desc_bank {
	struct qwz_dmamem *mem;
	caddr_t *vaddr;
	bus_addr_t paddr;
	uint32_t size;
};

/* Size to enforce scatter idle list mode */
#define DP_LINK_DESC_ALLOC_SIZE_THRESH 0x200000
#define DP_LINK_DESC_BANKS_MAX 8

#define DP_LINK_DESC_START	0x4000
#define DP_LINK_DESC_SHIFT	3

#define DP_LINK_DESC_COOKIE_SET(id, page) \
	((((id) + DP_LINK_DESC_START) << DP_LINK_DESC_SHIFT) | (page))

#define DP_LINK_DESC_BANK_MASK	GENMASK(2, 0)

struct hal_wbm_idle_scatter_list {
	struct qwz_dmamem *mem;
	bus_addr_t paddr;
	struct hal_wbm_link_desc *vaddr;
};

struct dp_rxdma_mon_ring {
	struct dp_srng refill_buf_ring;
#if 0
	struct idr bufs_idr;
	/* Protects bufs_idr */
	spinlock_t idr_lock;
#else
	struct qwz_rx_data *rx_data;
#endif
	int bufs_max;
	uint8_t freemap[howmany(DP_RXDMA_BUF_RING_SIZE, 8)];
};

struct dp_rxdma_ring {
	struct dp_srng refill_buf_ring;
	struct qwz_rx_data *rx_data;
	int bufs_max;
};

#define MAX_RXDMA_PER_PDEV     2

struct qwz_dp {
	struct qwz_softc *sc;
	uint8_t num_bank_profiles;
	struct ath12k_dp_tx_bank_profile *bank_profiles;
	enum ath12k_htc_ep_id eid;
	int htt_tgt_version_received;
	uint8_t htt_tgt_ver_major;
	uint8_t htt_tgt_ver_minor;
	struct dp_link_desc_bank link_desc_banks[DP_LINK_DESC_BANKS_MAX];
	struct dp_srng wbm_idle_ring;
	struct dp_srng wbm_desc_rel_ring;
	struct dp_srng reo_reinject_ring;
	struct dp_srng rx_rel_ring;
	struct dp_srng reo_except_ring;
	struct dp_srng reo_cmd_ring;
	struct dp_srng reo_status_ring;
	enum peer_metadata_version peer_metadata_ver;
	struct dp_srng reo_dst_ring[DP_REO_DST_RING_MAX];
	struct dp_tx_ring tx_ring[DP_TCL_NUM_RING_MAX];
	struct hal_wbm_idle_scatter_list scatter_list[DP_IDLE_SCATTER_BUFS_MAX];
	struct dp_reo_cmd_head reo_cmd_list;
	struct dp_reo_cmd_cache_flush_head reo_cmd_cache_flush_list;
#if 0
	struct list_head dp_full_mon_mpdu_list;
#endif
	uint32_t reo_cmd_cache_flush_count;
	enum hal_rx_buf_return_buf_manager idle_link_rbm;
#if 0
	/**
	 * protects access to below fields,
	 * - reo_cmd_list
	 * - reo_cmd_cache_flush_list
	 * - reo_cmd_cache_flush_count
	 */
	spinlock_t reo_cmd_lock;
#endif
	struct qwz_hp_update_timer reo_cmd_timer;
	struct qwz_hp_update_timer tx_ring_timer[DP_TCL_NUM_RING_MAX];
	struct ath12k_spt_info *spt_info;
	uint32_t num_spt_pages;
	TAILQ_HEAD(,ath12k_rx_desc_info) rx_desc_free_list;
#ifdef notyet
	/* protects the free desc list */
	spinlock_t rx_desc_lock;
#endif
	TAILQ_HEAD(,ath12k_tx_desc_info) tx_desc_free_list[ATH12K_HW_MAX_QUEUES];
	TAILQ_HEAD(,ath12k_tx_desc_info) tx_desc_used_list[ATH12K_HW_MAX_QUEUES];
#ifdef notyet
	/* protects the free and used desc lists */
	spinlock_t tx_desc_lock[ATH12K_HW_MAX_QUEUES];
#endif
	struct dp_rxdma_ring rx_refill_buf_ring;
	struct dp_srng rx_mac_buf_ring[MAX_RXDMA_PER_PDEV];
	struct dp_srng rxdma_err_dst_ring[MAX_RXDMA_PER_PDEV];
	struct dp_rxdma_mon_ring rxdma_mon_buf_ring;
};

#define ATH12K_SHADOW_DP_TIMER_INTERVAL 20
#define ATH12K_SHADOW_CTRL_TIMER_INTERVAL 10

struct qwz_ce_pipe {
	struct qwz_softc *sc;
	uint16_t pipe_num;
	unsigned int attr_flags;
	unsigned int buf_sz;
	unsigned int rx_buf_needed;

	int (*send_cb)(struct qwz_ce_pipe *pipe);
	void (*recv_cb)(struct qwz_softc *, struct mbuf *);

#ifdef notyet
	struct tasklet_struct intr_tq;
#endif
	struct qwz_ce_ring *src_ring;
	struct qwz_ce_ring *dest_ring;
	struct qwz_ce_ring *status_ring;
	uint64_t timestamp;
};

struct qwz_ce {
	struct qwz_ce_pipe ce_pipe[CE_COUNT_MAX];
#ifdef notyet
	/* Protects rings of all ce pipes */
	spinlock_t ce_lock;
#endif
};


/* XXX This may be non-zero on AHB but is always zero on PCI. */
#define ATH12K_CE_OFFSET(sc)	(0)

struct qwz_qmi_ce_cfg {
	const uint8_t *shadow_reg;
	int shadow_reg_len;
	uint32_t *shadow_reg_v3;
	uint32_t shadow_reg_v3_len;
};

struct qwz_qmi_target_info {
	uint32_t chip_id;
	uint32_t chip_family;
	uint32_t board_id;
	uint32_t soc_id;
	uint32_t fw_version;
	uint32_t eeprom_caldata;
	char fw_build_timestamp[ATH12K_QMI_WLANFW_MAX_TIMESTAMP_LEN_V01 + 1];
	char fw_build_id[ATH12K_QMI_WLANFW_MAX_BUILD_ID_LEN_V01 + 1];
	char bdf_ext[ATH12K_QMI_BDF_EXT_STR_LENGTH];
};

struct qwz_qmi_dev_mem_info {
	uint64_t start;
	uint64_t size;
};

enum ath12k_bdf_search {
	ATH12K_BDF_SEARCH_DEFAULT,
	ATH12K_BDF_SEARCH_BUS_AND_BOARD,
};

struct qwz_device_id {
	enum ath12k_bdf_search bdf_search;
	uint32_t vendor;
	uint32_t device;
	uint32_t subsystem_vendor;
	uint32_t subsystem_device;
};

struct qwz_wmi_base;

struct qwz_pdev_wmi {
	struct qwz_wmi_base *wmi;
	enum ath12k_htc_ep_id eid;
	const struct wmi_peer_flags_map *peer_flags;
	uint32_t rx_decap_mode;
};

#define QWZ_MAX_RADIOS 3

struct qwz_wmi_base {
	struct qwz_softc *sc;
	struct qwz_pdev_wmi wmi[QWZ_MAX_RADIOS];
	enum ath12k_htc_ep_id wmi_endpoint_id[QWZ_MAX_RADIOS];
	uint32_t max_msg_len[QWZ_MAX_RADIOS];
	int service_ready;
	int unified_ready;
	uint8_t svc_map[howmany(WMI_MAX_EXT2_SERVICE, 8)];
	int tx_credits;
	const struct wmi_peer_flags_map *peer_flags;
	uint32_t num_mem_chunks;
	uint32_t rx_decap_mode;
	struct wmi_host_mem_chunk mem_chunks[WMI_MAX_MEM_REQS];
	enum wmi_host_hw_mode_config_type preferred_hw_mode;
	struct target_resource_config  wlan_resource_config;
	struct ath12k_targ_cap *targ_cap;
};

struct wmi_tlv_policy {
	size_t min_len;
};

struct wmi_tlv_svc_ready_parse {
	int wmi_svc_bitmap_done;
};

struct wmi_tlv_dma_ring_caps_parse {
	struct wmi_dma_ring_capabilities *dma_ring_caps;
	uint32_t n_dma_ring_caps;
};

struct wmi_tlv_svc_rdy_ext_parse {
	struct ath12k_service_ext_param param;
	struct wmi_soc_mac_phy_hw_mode_caps *hw_caps;
	struct wmi_hw_mode_capabilities *hw_mode_caps;
	uint32_t n_hw_mode_caps;
	uint32_t tot_phy_id;
	struct wmi_hw_mode_capabilities pref_hw_mode_caps;
	struct wmi_mac_phy_capabilities *mac_phy_caps;
	size_t mac_phy_caps_size;
	uint32_t n_mac_phy_caps;
	struct wmi_soc_hal_reg_capabilities *soc_hal_reg_caps;
	struct wmi_hal_reg_capabilities_ext *ext_hal_reg_caps;
	uint32_t n_ext_hal_reg_caps;
	struct wmi_tlv_dma_ring_caps_parse dma_caps_parse;
	int hw_mode_done;
	int mac_phy_done;
	int ext_hal_reg_done;
	int mac_phy_chainmask_combo_done;
	int mac_phy_chainmask_cap_done;
	int oem_dma_ring_cap_done;
	int dma_ring_cap_done;
};

struct wmi_tlv_svc_rdy_ext2_parse {
	struct wmi_tlv_dma_ring_caps_parse dma_caps_parse;
	bool dma_ring_cap_done;
};

struct wmi_tlv_rdy_parse {
	uint32_t num_extra_mac_addr;
};

struct wmi_tlv_dma_buf_release_parse {
	struct ath12k_wmi_dma_buf_release_fixed_param fixed;
	struct wmi_dma_buf_release_entry *buf_entry;
	struct wmi_dma_buf_release_meta_data *meta_data;
	uint32_t num_buf_entry;
	uint32_t num_meta;
	bool buf_entry_done;
	bool meta_data_done;
};

struct wmi_tlv_fw_stats_parse {
	const struct wmi_stats_event *ev;
	const struct wmi_per_chain_rssi_stats *rssi;
	struct ath12k_fw_stats *stats;
	int rssi_num;
	bool chain_rssi_done;
};

struct wmi_tlv_mgmt_rx_parse {
	const struct wmi_mgmt_rx_hdr *fixed;
	const uint8_t *frame_buf;
	bool frame_buf_done;
};

struct qwz_htc;

struct qwz_htc_ep_ops {
	void (*ep_tx_complete)(struct qwz_softc *, struct mbuf *);
	void (*ep_rx_complete)(struct qwz_softc *, struct mbuf *);
	void (*ep_tx_credits)(struct qwz_softc *);
};

/* service connection information */
struct qwz_htc_svc_conn_req {
	uint16_t service_id;
	struct qwz_htc_ep_ops ep_ops;
	int max_send_queue_depth;
};

/* service connection response information */
struct qwz_htc_svc_conn_resp {
	uint8_t buffer_len;
	uint8_t actual_len;
	enum ath12k_htc_ep_id eid;
	unsigned int max_msg_len;
	uint8_t connect_resp_code;
};

#define ATH12K_NUM_CONTROL_TX_BUFFERS 2
#define ATH12K_HTC_MAX_LEN 4096
#define ATH12K_HTC_MAX_CTRL_MSG_LEN 256
#define ATH12K_HTC_WAIT_TIMEOUT_HZ (1 * HZ)
#define ATH12K_HTC_CONTROL_BUFFER_SIZE (ATH12K_HTC_MAX_CTRL_MSG_LEN + \
					sizeof(struct ath12k_htc_hdr))
#define ATH12K_HTC_CONN_SVC_TIMEOUT_HZ (1 * HZ)
#define ATH12K_HTC_MAX_SERVICE_ALLOC_ENTRIES 8

struct qwz_htc_ep {
	struct qwz_htc *htc;
	enum ath12k_htc_ep_id eid;
	enum ath12k_htc_svc_id service_id;
	struct qwz_htc_ep_ops ep_ops;

	int max_tx_queue_depth;
	int max_ep_message_len;
	uint8_t ul_pipe_id;
	uint8_t dl_pipe_id;

	uint8_t seq_no; /* for debugging */
	int tx_credits;
	bool tx_credit_flow_enabled;
};

struct qwz_htc_svc_tx_credits {
	uint16_t service_id;
	uint8_t  credit_allocation;
};

struct qwz_htc {
	struct qwz_softc *sc;
	struct qwz_htc_ep endpoint[ATH12K_HTC_EP_COUNT];
#ifdef notyet
	/* protects endpoints */
	spinlock_t tx_lock;
#endif
	uint8_t control_resp_buffer[ATH12K_HTC_MAX_CTRL_MSG_LEN];
	int control_resp_len;

	int ctl_resp;

	int total_transmit_credits;
	struct qwz_htc_svc_tx_credits
		service_alloc_table[ATH12K_HTC_MAX_SERVICE_ALLOC_ENTRIES];
	int target_credit_size;
	uint8_t wmi_ep_count;
};

struct qwz_msi_user {
	char *name;
	int num_vectors;
	uint32_t base_vector;
};

struct qwz_msi_config {
	int total_vectors;
	int total_users;
	struct qwz_msi_user *users;
	uint16_t hw_rev;
};

struct ath12k_band_cap {
	uint32_t phy_id;
	uint32_t max_bw_supported;
	uint32_t ht_cap_info;
	uint32_t he_cap_info[2];
	uint32_t he_mcs;
	uint32_t he_cap_phy_info[PSOC_HOST_MAX_PHY_SIZE];
	struct ath12k_ppe_threshold he_ppet;
	uint16_t he_6ghz_capa;
};

struct ath12k_pdev_cap {
	uint32_t supported_bands;
	uint32_t ampdu_density;
	uint32_t vht_cap;
	uint32_t vht_mcs;
	uint32_t he_mcs;
	uint32_t tx_chain_mask;
	uint32_t rx_chain_mask;
	uint32_t tx_chain_mask_shift;
	uint32_t rx_chain_mask_shift;
	struct ath12k_band_cap band[WMI_NUM_SUPPORTED_BAND_MAX];
	int nss_ratio_enabled;
	uint8_t nss_ratio_info;
};

struct qwz_pdev {
	struct qwz_softc *sc;
	uint32_t pdev_id;
	struct ath12k_pdev_cap cap;
	uint8_t mac_addr[IEEE80211_ADDR_LEN];
};

struct qwz_dbring_cap {
	uint32_t pdev_id;
	enum wmi_direct_buffer_module id;
	uint32_t min_elem;
	uint32_t min_buf_sz;
	uint32_t min_buf_align;
};

enum hal_rx_mon_status {
	HAL_RX_MON_STATUS_PPDU_NOT_DONE,
	HAL_RX_MON_STATUS_PPDU_DONE,
	HAL_RX_MON_STATUS_BUF_DONE,
};

struct hal_rx_user_status {
	uint32_t mcs:4,
	nss:3,
	ofdma_info_valid:1,
	dl_ofdma_ru_start_index:7,
	dl_ofdma_ru_width:7,
	dl_ofdma_ru_size:8;
	uint32_t ul_ofdma_user_v0_word0;
	uint32_t ul_ofdma_user_v0_word1;
	uint32_t ast_index;
	uint32_t tid;
	uint16_t tcp_msdu_count;
	uint16_t udp_msdu_count;
	uint16_t other_msdu_count;
	uint16_t frame_control;
	uint8_t frame_control_info_valid;
	uint8_t data_sequence_control_info_valid;
	uint16_t first_data_seq_ctrl;
	uint32_t preamble_type;
	uint16_t ht_flags;
	uint16_t vht_flags;
	uint16_t he_flags;
	uint8_t rs_flags;
	uint32_t mpdu_cnt_fcs_ok;
	uint32_t mpdu_cnt_fcs_err;
	uint32_t mpdu_fcs_ok_bitmap[8];
	uint32_t mpdu_ok_byte_count;
	uint32_t mpdu_err_byte_count;
};

struct hal_rx_wbm_rel_info {
	uint32_t cookie;
	enum hal_wbm_rel_src_module err_rel_src;
	enum hal_reo_dest_ring_push_reason push_reason;
	uint32_t err_code;
	int first_msdu;
	int last_msdu;
};

#define HAL_INVALID_PEERID 0xffff
#define VHT_SIG_SU_NSS_MASK 0x7

#define HAL_RX_MAX_MCS 12
#define HAL_RX_MAX_NSS 8

#define HAL_TLV_STATUS_PPDU_NOT_DONE    HAL_RX_MON_STATUS_PPDU_NOT_DONE
#define HAL_TLV_STATUS_PPDU_DONE        HAL_RX_MON_STATUS_PPDU_DONE
#define HAL_TLV_STATUS_BUF_DONE         HAL_RX_MON_STATUS_BUF_DONE

struct hal_rx_mon_ppdu_info {
	uint32_t ppdu_id;
	uint32_t ppdu_ts;
	uint32_t num_mpdu_fcs_ok;
	uint32_t num_mpdu_fcs_err;
	uint32_t preamble_type;
	uint16_t chan_num;
	uint16_t tcp_msdu_count;
	uint16_t tcp_ack_msdu_count;
	uint16_t udp_msdu_count;
	uint16_t other_msdu_count;
	uint16_t peer_id;
	uint8_t rate;
	uint8_t mcs;
	uint8_t nss;
	uint8_t bw;
	uint8_t vht_flag_values1;
	uint8_t vht_flag_values2;
	uint8_t vht_flag_values3[4];
	uint8_t vht_flag_values4;
	uint8_t vht_flag_values5;
	uint16_t vht_flag_values6;
	uint8_t is_stbc;
	uint8_t gi;
	uint8_t ldpc;
	uint8_t beamformed;
	uint8_t rssi_comb;
	uint8_t rssi_chain_pri20[HAL_RX_MAX_NSS];
	uint8_t tid;
	uint16_t ht_flags;
	uint16_t vht_flags;
	uint16_t he_flags;
	uint16_t he_mu_flags;
	uint8_t dcm;
	uint8_t ru_alloc;
	uint8_t reception_type;
	uint64_t tsft;
	uint64_t rx_duration;
	uint16_t frame_control;
	uint32_t ast_index;
	uint8_t rs_fcs_err;
	uint8_t rs_flags;
	uint8_t cck_flag;
	uint8_t ofdm_flag;
	uint8_t ulofdma_flag;
	uint8_t frame_control_info_valid;
	uint16_t he_per_user_1;
	uint16_t he_per_user_2;
	uint8_t he_per_user_position;
	uint8_t he_per_user_known;
	uint16_t he_flags1;
	uint16_t he_flags2;
	uint8_t he_RU[4];
	uint16_t he_data1;
	uint16_t he_data2;
	uint16_t he_data3;
	uint16_t he_data4;
	uint16_t he_data5;
	uint16_t he_data6;
	uint32_t ppdu_len;
	uint32_t prev_ppdu_id;
	uint32_t device_id;
	uint16_t first_data_seq_ctrl;
	uint8_t monitor_direct_used;
	uint8_t data_sequence_control_info_valid;
	uint8_t ltf_size;
	uint8_t rxpcu_filter_pass;
	char rssi_chain[8][8];
	struct hal_rx_user_status userstats;
};

enum dp_mon_status_buf_state {
	/* PPDU id matches in dst ring and status ring */
	DP_MON_STATUS_MATCH,
	/* status ring dma is not done */
	DP_MON_STATUS_NO_DMA,
	/* status ring is lagging, reap status ring */
	DP_MON_STATUS_LAG,
	/* status ring is leading, reap dst ring and drop */
	DP_MON_STATUS_LEAD,
	/* replinish monitor status ring */
	DP_MON_STATUS_REPLINISH,
};

struct qwz_pdev_mon_stats {
	uint32_t status_ppdu_state;
	uint32_t status_ppdu_start;
	uint32_t status_ppdu_end;
	uint32_t status_ppdu_compl;
	uint32_t status_ppdu_start_mis;
	uint32_t status_ppdu_end_mis;
	uint32_t status_ppdu_done;
	uint32_t dest_ppdu_done;
	uint32_t dest_mpdu_done;
	uint32_t dest_mpdu_drop;
	uint32_t dup_mon_linkdesc_cnt;
	uint32_t dup_mon_buf_cnt;
	uint32_t dest_mon_stuck;
	uint32_t dest_mon_not_reaped;
};

struct qwz_mon_data {
	struct dp_link_desc_bank link_desc_banks[DP_LINK_DESC_BANKS_MAX];
	struct hal_rx_mon_ppdu_info mon_ppdu_info;

	uint32_t mon_ppdu_status;
	uint32_t mon_last_buf_cookie;
	uint64_t mon_last_linkdesc_paddr;
	uint16_t chan_noise_floor;
	bool hold_mon_dst_ring;
	enum dp_mon_status_buf_state buf_state;
	bus_addr_t mon_status_paddr;
	struct dp_full_mon_mpdu *mon_mpdu;
#ifdef notyet
	struct hal_sw_mon_ring_entries sw_mon_entries;
#endif
	struct qwz_pdev_mon_stats rx_mon_stats;
#ifdef notyet
	/* lock for monitor data */
	spinlock_t mon_lock;
	struct sk_buff_head rx_status_q;
#endif
};


struct qwz_pdev_dp {
	uint32_t mac_id;
#if 0
	atomic_t num_tx_pending;
	wait_queue_head_t tx_empty_waitq;
#endif
	struct dp_srng rxdma_mon_dst_ring[MAX_RXDMA_PER_PDEV];
	struct dp_srng tx_mon_dst_ring[MAX_RXDMA_PER_PDEV];
#if 0
	struct ieee80211_rx_status rx_status;
#endif
	struct qwz_mon_data mon_data;
};

struct qwz_txmgmt_queue {
	struct qwz_tx_data data[8];
	int cur;
	int queued;
};

struct qwz_vif {
	uint32_t vdev_id;
	enum wmi_vdev_type vdev_type;
	enum wmi_vdev_subtype vdev_subtype;
	uint32_t beacon_interval;
	uint32_t dtim_period;
	uint16_t ast_hash;
	uint16_t ast_idx;
	uint16_t tcl_metadata;
	uint8_t hal_addr_search_flags;
	uint8_t search_type;

	struct qwz_softc *sc;

	uint16_t tx_seq_no;
	struct wmi_wmm_params_all_arg wmm_params;
	TAILQ_ENTRY(qwz_vif) entry;
	union {
		struct {
			uint32_t uapsd;
		} sta;
		struct {
			/* 127 stations; wmi limit */
			uint8_t tim_bitmap[16];
			uint8_t tim_len;
			uint32_t ssid_len;
			uint8_t ssid[IEEE80211_NWID_LEN];
			bool hidden_ssid;
			/* P2P_IE with NoA attribute for P2P_GO case */
			uint32_t noa_len;
			uint8_t *noa_data;
		} ap;
	} u;

	bool is_started;
	bool is_up;
	bool ftm_responder;
	bool spectral_enabled;
	bool ps;
	uint32_t aid;
	uint8_t bssid[IEEE80211_ADDR_LEN];
#if 0
	struct cfg80211_bitrate_mask bitrate_mask;
	struct delayed_work connection_loss_work;
#endif
	int num_legacy_stations;
	int rtscts_prot_mode;
	int txpower;
	bool rsnie_present;
	bool wpaie_present;
	bool bcca_zero_sent;
	bool do_not_send_tmpl;
	struct ieee80211_channel *chan;
#if 0
	struct ath12k_arp_ns_offload arp_ns_offload;
	struct ath12k_rekey_data rekey_data;
#endif
#ifdef CONFIG_ATH12K_DEBUGFS
	struct dentry *debugfs_twt;
#endif /* CONFIG_ATH12K_DEBUGFS */

	struct qwz_txmgmt_queue txmgmt;
};

TAILQ_HEAD(qwz_vif_list, qwz_vif);

struct qwz_survey_info {
	int8_t noise;
	uint64_t time;
	uint64_t time_busy;
};

#define ATH12K_IRQ_NUM_MAX 52
#define ATH12K_EXT_IRQ_NUM_MAX	16

struct qwz_ext_irq_grp {
	struct qwz_softc *sc;
	uint32_t irqs[ATH12K_EXT_IRQ_NUM_MAX];
	uint32_t num_irq;
	uint32_t grp_id;
	uint64_t timestamp;
#if 0
	bool napi_enabled;
	struct napi_struct napi;
	struct net_device napi_ndev;
#endif
};

struct qwz_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
} __packed;

#define IWX_RX_RADIOTAP_PRESENT	0 /* TODO add more information */

struct qwz_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
} __packed;

#define IWX_TX_RADIOTAP_PRESENT	0 /* TODO add more information */

struct qwz_setkey_task_arg {
	struct ieee80211_node *ni;
	struct ieee80211_key *k;
	int cmd;
#define QWZ_ADD_KEY	1
#define QWZ_DEL_KEY	2
};

struct qwz_softc {
	struct device			sc_dev;
	struct ieee80211com		sc_ic;
	uint32_t			sc_flags;
	int				sc_node;

	int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);

	struct rwlock ioctl_rwl;

	struct task		init_task; /* NB: not reference-counted */
	struct refcnt		task_refs;
	struct taskq		*sc_nswq;
	struct task		newstate_task;
	enum ieee80211_state	ns_nstate;
	int			ns_arg;

	/* Task for setting encryption keys and its arguments. */
	struct task		setkey_task;
	/*
	 * At present we need to process at most two keys at once:
	 * Our pairwise key and a group key.
	 * When hostap mode is implemented this array needs to grow or
	 * it might become a bottleneck for associations that occur at
	 * roughly the same time.
	 */
	struct qwz_setkey_task_arg setkey_arg[2];
	int setkey_cur;
	int setkey_tail;
	int setkey_nkeys;

	int install_key_done;
	int install_key_status;

	enum ath12k_11d_state	state_11d;
	int			completed_11d_scan;
	uint32_t		vdev_id_11d_scan;
	struct {
		int started;
		int completed;
		int on_channel;
		struct timeout timeout;
		enum ath12k_scan_state state;
		int vdev_id;
		int is_roc;
		int roc_freq;
		int roc_notify;
	} scan;
	u_int			scan_channel;
	struct qwz_survey_info	survey[IEEE80211_CHAN_MAX];

	int			attached;
	struct {
		u_char *data;
		size_t size;
	} fw_img[3];
#define QWZ_FW_AMSS	0
#define QWZ_FW_BOARD	1
#define QWZ_FW_M3	2

	int			sc_tx_timer;
	uint32_t		qfullmsk;
#define	QWZ_MGMT_QUEUE_ID	31

	bus_addr_t			mem;
	struct ath12k_hw_params		hw_params;
	struct ath12k_hal		hal;
	struct qwz_ce			ce;
	struct qwz_dp			dp;
	struct qwz_pdev_dp		pdev_dp;
	struct qwz_wmi_base		wmi;
	struct qwz_htc			htc;
	const struct hal_rx_ops		*hal_rx_ops;
	uint32_t			wmi_conf_rx_decap_mode;

	enum ath12k_firmware_mode	fw_mode;
	enum ath12k_crypt_mode		crypto_mode;
	enum ath12k_hw_txrx_mode	frame_mode;

	struct qwz_ext_irq_grp		ext_irq_grp[ATH12K_EXT_IRQ_GRP_NUM_MAX];

	uint16_t			qmi_txn_id;
	int				qmi_cal_done;
	struct qwz_qmi_ce_cfg		qmi_ce_cfg;
	struct qwz_qmi_target_info	qmi_target;
	struct qwz_qmi_dev_mem_info	qmi_dev_mem[ATH12K_QMI_WLFW_MAX_DEV_MEM_NUM_V01];
	struct ath12k_targ_cap		target_caps;
	int				num_radios;
	uint8_t				device_id;
	uint32_t			cc_freq_hz;
	uint32_t			cfg_tx_chainmask;
	uint32_t			cfg_rx_chainmask;
	int				num_tx_chains;
	int				num_rx_chains;
	int				num_created_vdevs;
	int				num_started_vdevs;
	uint32_t			allocated_vdev_map;
	uint32_t			free_vdev_map;
	int				num_peers;
	int				peer_mapped;
	int				peer_delete_done;
	int				vdev_setup_done;
	int				peer_assoc_done;

	struct qwz_dbring_cap	*db_caps;
	uint32_t		 num_db_cap;

	uint8_t		mac_addr[IEEE80211_ADDR_LEN];
	int		wmi_ready;
	uint32_t	wlan_init_status;

	uint32_t pktlog_defs_checksum;

	struct qwz_vif_list vif_list;
	struct qwz_pdev pdevs[MAX_RADIOS];
	struct {
		enum WMI_HOST_WLAN_BAND supported_bands;
		uint32_t pdev_id;
	} target_pdev_ids[MAX_RADIOS];
	uint8_t target_pdev_count;
	uint32_t pdevs_active;
	int pdevs_macaddr_valid;
	struct ath12k_hal_reg_capabilities_ext hal_reg_cap[MAX_RADIOS];

	struct {
		uint32_t service;
		uint32_t instance;
		uint32_t node;
		uint32_t port;
	} qrtr_server;

	struct qmi_response_type_v01	qmi_resp;

	struct qwz_dmamem		*fwmem;
	int				 expect_fwmem_req;
	int				 fwmem_ready;
	int				 fw_ready;

	int				 ctl_resp;

	struct qwz_dmamem		*m3_mem;

	struct timeout			 mon_reap_timer;
#define ATH12K_MON_TIMER_INTERVAL	10

	/* Provided by attachment driver: */
	struct qwz_ops			ops;
	bus_dma_tag_t			sc_dmat;
	enum ath12k_hw_rev		sc_hw_rev;
	int				static_window_map;
	struct qwz_device_id		id;
	char				sc_bus_str[4]; /* "pci" or "ahb" */
	int				num_msivec;
	uint32_t			msi_addr_lo;
	uint32_t			msi_addr_hi;
	uint32_t			msi_data_start;
	const struct qwz_msi_config	*msi_cfg;
	uint32_t			msi_ce_irqmask;

	struct qmi_wlanfw_request_mem_ind_msg_v01 *sc_req_mem_ind;

	caddr_t			sc_drvbpf;

	union {
		struct qwz_rx_radiotap_header th;
		uint8_t	pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct qwz_tx_radiotap_header th;
		uint8_t	pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int			sc_txtap_len;
};

int	qwz_ce_intr(void *);
int	qwz_ext_intr(void *);
int	qwz_dp_service_srng(struct qwz_softc *, int);

int	qwz_init_hw_params(struct qwz_softc *);
int	qwz_attach(struct qwz_softc *);
void	qwz_detach(struct qwz_softc *);
int	qwz_activate(struct device *, int);

void	qwz_core_deinit(struct qwz_softc *);
void	qwz_ce_cleanup_pipes(struct qwz_softc *);

int	qwz_ioctl(struct ifnet *, u_long, caddr_t);
void	qwz_start(struct ifnet *);
void	qwz_watchdog(struct ifnet *);
int	qwz_media_change(struct ifnet *);
void	qwz_init_task(void *);
int	qwz_newstate(struct ieee80211com *, enum ieee80211_state, int);
void	qwz_newstate_task(void *);

struct ath12k_peer {
#if 0
	struct list_head list;
	struct ieee80211_sta *sta;
#endif
	int vdev_id;
#if 0
	u8 addr[ETH_ALEN];
#endif
	int peer_id;
	uint16_t ast_hash;
	uint8_t pdev_id;
	uint16_t hw_peer_id;
#if 0
	/* protected by ab->data_lock */
	struct ieee80211_key_conf *keys[WMI_MAX_KEY_INDEX + 1];
#endif
	struct dp_rx_tid rx_tid[IEEE80211_NUM_TID + 1];
#if 0
	/* peer id based rhashtable list pointer */
	struct rhash_head rhash_id;
	/* peer addr based rhashtable list pointer */
	struct rhash_head rhash_addr;

	/* Info used in MMIC verification of
	 * RX fragments
	 */
	struct crypto_shash *tfm_mmic;
	u8 mcast_keyidx;
	u8 ucast_keyidx;
	u16 sec_type;
	u16 sec_type_grp;
	bool is_authorized;
	bool dp_setup_done;
#endif
};

struct qwz_node {
	struct ieee80211_node ni;
	struct ath12k_peer peer;
	unsigned int flags;
#define QWZ_NODE_FLAG_HAVE_PAIRWISE_KEY	0x01
#define QWZ_NODE_FLAG_HAVE_GROUP_KEY	0x02
};

struct ieee80211_node *qwz_node_alloc(struct ieee80211com *);
int	qwz_set_key(struct ieee80211com *, struct ieee80211_node *,
    struct ieee80211_key *);
void	qwz_delete_key(struct ieee80211com *, struct ieee80211_node *,
    struct ieee80211_key *);

void	qwz_qrtr_recv_msg(struct qwz_softc *, struct mbuf *);

int	qwz_hal_srng_init(struct qwz_softc *);

int	qwz_ce_alloc_pipes(struct qwz_softc *);
void	qwz_ce_free_pipes(struct qwz_softc *);
void	qwz_ce_rx_post_buf(struct qwz_softc *);
void	qwz_ce_get_shadow_config(struct qwz_softc *, uint32_t **, uint32_t *);

static inline unsigned int
qwz_roundup_pow_of_two(unsigned int i)
{
	return (powerof2(i) ? i : (1 << (fls(i) - 1)));
}

static inline unsigned int
qwz_ce_get_attr_flags(struct qwz_softc *sc, int ce_id)
{
	KASSERT(ce_id < sc->hw_params.ce_count);
	return sc->hw_params.host_ce_config[ce_id].flags;
}

static inline enum ieee80211_edca_ac qwz_tid_to_ac(uint32_t tid)
{
	return (((tid == 0) || (tid == 3)) ? EDCA_AC_BE :
		((tid == 1) || (tid == 2)) ? EDCA_AC_BK :
		((tid == 4) || (tid == 5)) ? EDCA_AC_VI :
		EDCA_AC_VO);
}
