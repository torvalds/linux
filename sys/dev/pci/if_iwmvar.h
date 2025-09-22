/*	$OpenBSD: if_iwmvar.h,v 1.78 2022/05/14 05:47:04 stsp Exp $	*/

/*
 * Copyright (c) 2014 genua mbh <info@genua.de>
 * Copyright (c) 2014 Fixup Software Ltd.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 * Driver version we are currently based off of is
 * Linux 3.14.3 (tag id a2df521e42b1d9a23f620ac79dbfe8655a8391dd)
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

struct iwm_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsft;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_dbm_antsignal;
	int8_t		wr_dbm_antnoise;
} __packed;

#define IWM_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_TSFT) |				\
	 (1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE))

struct iwm_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define IWM_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

#define IWM_UCODE_SECT_MAX 16
#define IWM_FWDMASEGSZ (192*1024)
#define IWM_FWDMASEGSZ_8000 (320*1024)
/* sanity check value */
#define IWM_FWMAXSIZE (2*1024*1024)

/*
 * fw_status is used to determine if we've already parsed the firmware file
 *
 * In addition to the following, status < 0 ==> -error
 */
#define IWM_FW_STATUS_NONE		0
#define IWM_FW_STATUS_INPROGRESS	1
#define IWM_FW_STATUS_DONE		2

enum iwm_ucode_type {
	IWM_UCODE_TYPE_REGULAR,
	IWM_UCODE_TYPE_INIT,
	IWM_UCODE_TYPE_WOW,
	IWM_UCODE_TYPE_REGULAR_USNIFFER,
	IWM_UCODE_TYPE_MAX
};

struct iwm_fw_info {
	void *fw_rawdata;
	size_t fw_rawsize;
	int fw_status;

	struct iwm_fw_sects {
		struct iwm_fw_onesect {
			void *fws_data;
			uint32_t fws_len;
			uint32_t fws_devoff;
		} fw_sect[IWM_UCODE_SECT_MAX];
		size_t fw_totlen;
		int fw_count;
		uint32_t paging_mem_size;
	} fw_sects[IWM_UCODE_TYPE_MAX];
};

struct iwm_nvm_data {
	int n_hw_addrs;
	uint8_t hw_addr[ETHER_ADDR_LEN];

	uint8_t calib_version;
	uint16_t calib_voltage;

	uint16_t raw_temperature;
	uint16_t kelvin_temperature;
	uint16_t kelvin_voltage;
	uint16_t xtal_calib[2];

	int sku_cap_band_24GHz_enable;
	int sku_cap_band_52GHz_enable;
	int sku_cap_11n_enable;
	int sku_cap_11ac_enable;
	int sku_cap_amt_enable;
	int sku_cap_ipan_enable;
	int sku_cap_mimo_disable;

	uint8_t radio_cfg_type;
	uint8_t radio_cfg_step;
	uint8_t radio_cfg_dash;
	uint8_t radio_cfg_pnum;
	uint8_t valid_tx_ant, valid_rx_ant;

	uint16_t nvm_version;
	uint8_t max_tx_pwr_half_dbm;

	int lar_enabled;
};

/* max bufs per tfd the driver will use */
#define IWM_MAX_CMD_TBS_PER_TFD 2

struct iwm_host_cmd {
	const void *data[IWM_MAX_CMD_TBS_PER_TFD];
	struct iwm_rx_packet *resp_pkt;
	size_t resp_pkt_len;
	unsigned long _rx_page_addr;
	uint32_t _rx_page_order;
	int handler_status;

	uint32_t flags;
	uint16_t len[IWM_MAX_CMD_TBS_PER_TFD];
	uint8_t dataflags[IWM_MAX_CMD_TBS_PER_TFD];
	uint32_t id;
};

/*
 * DMA glue is from iwn
 */

struct iwm_dma_info {
	bus_dma_tag_t		tag;
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	bus_addr_t		paddr;
	void 			*vaddr;
	bus_size_t		size;
};

/**
 * struct iwm_fw_paging
 * @fw_paging_block: dma memory info
 * @fw_paging_size: page size
 */
struct iwm_fw_paging {
	struct iwm_dma_info fw_paging_block;
	uint32_t fw_paging_size;
};

#define IWM_TX_RING_COUNT	256
#define IWM_TX_RING_LOMARK	192
#define IWM_TX_RING_HIMARK	224

/* For aggregation queues, index must be aligned to frame sequence number. */
#define IWM_AGG_SSN_TO_TXQ_IDX(x)	((x) & (IWM_TX_RING_COUNT - 1))

struct iwm_tx_data {
	bus_dmamap_t	map;
	bus_addr_t	cmd_paddr;
	bus_addr_t	scratch_paddr;
	struct mbuf	*m;
	struct iwm_node *in;
	int txmcs;
	int txrate;

	/* A-MPDU subframes */
	int ampdu_txmcs;
	int ampdu_txnss;
	int ampdu_nframes;
};

struct iwm_tx_ring {
	struct iwm_dma_info	desc_dma;
	struct iwm_dma_info	cmd_dma;
	struct iwm_tfd		*desc;
	struct iwm_device_cmd	*cmd;
	struct iwm_tx_data	data[IWM_TX_RING_COUNT];
	int			qid;
	int			queued;
	int			cur;
	int			tail;
};

#define IWM_RX_MQ_RING_COUNT	512
#define IWM_RX_RING_COUNT	256
/* Linux driver optionally uses 8k buffer */
#define IWM_RBUF_SIZE		4096

struct iwm_rx_data {
	struct mbuf	*m;
	bus_dmamap_t	map;
};

struct iwm_rx_ring {
	struct iwm_dma_info	free_desc_dma;
	struct iwm_dma_info	stat_dma;
	struct iwm_dma_info	used_desc_dma;
	void			*desc;
	struct iwm_rb_status	*stat;
	struct iwm_rx_data	data[IWM_RX_MQ_RING_COUNT];
	int			cur;
};

#define IWM_FLAG_USE_ICT	0x01	/* using Interrupt Cause Table */
#define IWM_FLAG_RFKILL		0x02	/* radio kill switch is set */
#define IWM_FLAG_SCANNING	0x04	/* scan in progress */
#define IWM_FLAG_MAC_ACTIVE	0x08	/* MAC context added to firmware */
#define IWM_FLAG_BINDING_ACTIVE	0x10	/* MAC->PHY binding added to firmware */
#define IWM_FLAG_STA_ACTIVE	0x20	/* AP added to firmware station table */
#define IWM_FLAG_TE_ACTIVE	0x40	/* time event is scheduled */
#define IWM_FLAG_HW_ERR		0x80	/* hardware error occurred */
#define IWM_FLAG_SHUTDOWN	0x100	/* shutting down; new tasks forbidden */
#define IWM_FLAG_BGSCAN		0x200	/* background scan in progress */
#define IWM_FLAG_TXFLUSH	0x400	/* Tx queue flushing in progress */

struct iwm_ucode_status {
	uint32_t uc_error_event_table;
	uint32_t uc_umac_error_event_table;
	uint32_t uc_log_event_table;

	int uc_ok;
	int uc_intr;
};

#define IWM_CMD_RESP_MAX PAGE_SIZE

/* lower blocks contain EEPROM image and calibration data */
#define IWM_OTP_LOW_IMAGE_SIZE_FAMILY_7000 	16384
#define IWM_OTP_LOW_IMAGE_SIZE_FAMILY_8000	32768

#define IWM_TE_SESSION_PROTECTION_MAX_TIME_MS 1000
#define IWM_TE_SESSION_PROTECTION_MIN_TIME_MS 400

enum IWM_CMD_MODE {
	IWM_CMD_ASYNC		= (1 << 0),
	IWM_CMD_WANT_RESP	= (1 << 1),
	IWM_CMD_SEND_IN_RFKILL	= (1 << 2),
};
enum iwm_hcmd_dataflag {
	IWM_HCMD_DFL_NOCOPY     = (1 << 0),
	IWM_HCMD_DFL_DUP        = (1 << 1),
};

#define IWM_NUM_PAPD_CH_GROUPS	9
#define IWM_NUM_TXP_CH_GROUPS	9

struct iwm_phy_db_entry {
	uint16_t size;
	uint8_t *data;
};

struct iwm_phy_db {
	struct iwm_phy_db_entry	cfg;
	struct iwm_phy_db_entry	calib_nch;
	struct iwm_phy_db_entry	calib_ch_group_papd[IWM_NUM_PAPD_CH_GROUPS];
	struct iwm_phy_db_entry	calib_ch_group_txp[IWM_NUM_TXP_CH_GROUPS];
};

struct iwm_phy_ctxt {
	uint16_t id;
	uint16_t color;
	uint32_t ref;
	struct ieee80211_channel *channel;
	uint8_t sco; /* 40 MHz secondary channel offset */
	uint8_t vht_chan_width;
};

struct iwm_bf_data {
	int bf_enabled;		/* filtering	*/
	int ba_enabled;		/* abort	*/
	int ave_beacon_signal;
	int last_cqm_event;
};

/**
 * struct iwm_reorder_buffer - per ra/tid/queue reorder buffer
 * @head_sn: reorder window head sn
 * @num_stored: number of mpdus stored in the buffer
 * @buf_size: the reorder buffer size as set by the last addba request
 * @queue: queue of this reorder buffer
 * @last_amsdu: track last ASMDU SN for duplication detection
 * @last_sub_index: track ASMDU sub frame index for duplication detection
 * @reorder_timer: timer for frames are in the reorder buffer. For AMSDU
 *	it is the time of last received sub-frame
 * @removed: prevent timer re-arming
 * @valid: reordering is valid for this queue
 * @consec_oldsn_drops: consecutive drops due to old SN
 * @consec_oldsn_ampdu_gp2: A-MPDU GP2 timestamp to track
 *	when to apply old SN consecutive drop workaround
 * @consec_oldsn_prev_drop: track whether or not an MPDU
 *	that was single/part of the previous A-MPDU was
 *	dropped due to old SN
 */
struct iwm_reorder_buffer {
	uint16_t head_sn;
	uint16_t num_stored;
	uint16_t buf_size;
	uint16_t last_amsdu;
	uint8_t last_sub_index;
	struct timeout reorder_timer;
	int removed;
	int valid;
	unsigned int consec_oldsn_drops;
	uint32_t consec_oldsn_ampdu_gp2;
	unsigned int consec_oldsn_prev_drop;
#define IWM_AMPDU_CONSEC_DROPS_DELBA	10
};

/**
 * struct iwm_reorder_buf_entry - reorder buffer entry per frame sequence number
 * @frames: list of mbufs stored (A-MSDU subframes share a sequence number)
 * @reorder_time: time the packet was stored in the reorder buffer
 */
struct iwm_reorder_buf_entry {
	struct mbuf_list frames;
	struct timeval reorder_time;
	uint32_t rx_pkt_status;
	int chanidx;
	int is_shortpre;
	uint32_t rate_n_flags;
	uint32_t device_timestamp;
	struct ieee80211_rxinfo rxi;
};

/**
 * struct iwm_rxba_data - BA session data
 * @sta_id: station id
 * @tid: tid of the session
 * @baid: baid of the session
 * @timeout: the timeout set in the addba request
 * @entries_per_queue: # of buffers per queue
 * @last_rx: last rx timestamp, updated only if timeout passed from last update
 * @session_timer: timer to check if BA session expired, runs at 2 * timeout
 * @sc: softc pointer, needed for timer context
 * @reorder_buf: reorder buffer
 * @reorder_buf_data: buffered frames, one entry per sequence number
 */
struct iwm_rxba_data {
	uint8_t sta_id;
	uint8_t tid;
	uint8_t baid;
	uint16_t timeout;
	uint16_t entries_per_queue;
	struct timeval last_rx;
	struct timeout session_timer;
	struct iwm_softc *sc;
	struct iwm_reorder_buffer reorder_buf;
	struct iwm_reorder_buf_entry entries[IEEE80211_BA_MAX_WINSZ];
};

static inline struct iwm_rxba_data *
iwm_rxba_data_from_reorder_buf(struct iwm_reorder_buffer *buf)
{
	return (void *)((uint8_t *)buf -
			offsetof(struct iwm_rxba_data, reorder_buf));
}

/**
 * struct iwm_rxq_dup_data - per station per rx queue data
 * @last_seq: last sequence per tid for duplicate packet detection
 * @last_sub_frame: last subframe packet
 */
struct iwm_rxq_dup_data {
	uint16_t last_seq[IWM_MAX_TID_COUNT + 1];
	uint8_t last_sub_frame[IWM_MAX_TID_COUNT + 1];
};

struct iwm_ba_task_data {
	uint32_t		start_tidmask;
	uint32_t		stop_tidmask;
};

struct iwm_softc {
	struct device sc_dev;
	struct ieee80211com sc_ic;
	int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
	int sc_newstate_pending;
	int attached;

	struct ieee80211_amrr sc_amrr;
	struct timeout sc_calib_to;
	struct timeout sc_led_blink_to;

	struct task		init_task; /* NB: not reference-counted */
	struct refcnt		task_refs;
	struct task		newstate_task;
	enum ieee80211_state	ns_nstate;
	int			ns_arg;

	/* Task for firmware BlockAck setup/teardown and its arguments. */
	struct task		ba_task;
	struct iwm_ba_task_data	ba_rx;
	struct iwm_ba_task_data	ba_tx;

	/* Task for ERP/HT prot/slot-time/EDCA updates. */
	struct task		mac_ctxt_task;

	/* Task for HT 20/40 MHz channel width updates. */
	struct task		phy_ctxt_task;

	bus_space_tag_t sc_st;
	bus_space_handle_t sc_sh;
	bus_size_t sc_sz;
	bus_dma_tag_t sc_dmat;
	pci_chipset_tag_t sc_pct;
	pcitag_t sc_pcitag;
	const void *sc_ih;
	int sc_msix;

	/* TX scheduler rings. */
	struct iwm_dma_info		sched_dma;
	uint32_t			sched_base;

	/* TX/RX rings. */
	struct iwm_tx_ring txq[IWM_MAX_QUEUES];
	struct iwm_rx_ring rxq;
	int qfullmsk;
	int qenablemsk;
	int cmdqid;

	int sc_sf_state;

	/* ICT table. */
	struct iwm_dma_info	ict_dma;
	int			ict_cur;

	int sc_hw_rev;
#define IWM_SILICON_A_STEP	0
#define IWM_SILICON_B_STEP	1
#define IWM_SILICON_C_STEP	2
#define IWM_SILICON_D_STEP	3
	int sc_hw_id;
	int sc_device_family;
#define IWM_DEVICE_FAMILY_7000	1
#define IWM_DEVICE_FAMILY_8000	2
#define IWM_DEVICE_FAMILY_9000	3

	struct iwm_dma_info kw_dma;
	struct iwm_dma_info fw_dma;

	int sc_fw_chunk_done;
	int sc_init_complete;
#define IWM_INIT_COMPLETE	0x01
#define IWM_CALIB_COMPLETE	0x02

	struct iwm_ucode_status sc_uc;
	enum iwm_ucode_type sc_uc_current;
	char sc_fwver[32];

	int sc_capaflags;
	int sc_capa_max_probe_len;
	int sc_capa_n_scan_channels;
	uint8_t sc_ucode_api[howmany(IWM_NUM_UCODE_TLV_API, NBBY)];
	uint8_t sc_enabled_capa[howmany(IWM_NUM_UCODE_TLV_CAPA, NBBY)];
#define IWM_MAX_FW_CMD_VERSIONS	64
	struct iwm_fw_cmd_version cmd_versions[IWM_MAX_FW_CMD_VERSIONS];
	int n_cmd_versions;

	int sc_intmask;
	int sc_flags;

	uint32_t sc_fh_init_mask;
	uint32_t sc_hw_init_mask;
	uint32_t sc_fh_mask;
	uint32_t sc_hw_mask;

	/*
	 * So why do we need a separate stopped flag and a generation?
	 * the former protects the device from issuing commands when it's
	 * stopped (duh).  The latter protects against race from a very
	 * fast stop/unstop cycle where threads waiting for responses do
	 * not have a chance to run in between.  Notably: we want to stop
	 * the device from interrupt context when it craps out, so we
	 * don't have the luxury of waiting for quiescence.
	 */
	int sc_generation;

	struct rwlock ioctl_rwl;

	int sc_cap_off; /* PCIe caps */

	const char *sc_fwname;
	bus_size_t sc_fwdmasegsz;
	size_t sc_nvm_max_section_size;
	struct iwm_fw_info sc_fw;
	uint32_t sc_fw_phy_config;
	uint32_t sc_extra_phy_config;
	struct iwm_tlv_calib_ctrl sc_default_calib[IWM_UCODE_TYPE_MAX];

	struct iwm_nvm_data sc_nvm;
	struct iwm_phy_db sc_phy_db;

	struct iwm_bf_data sc_bf;

	int sc_tx_timer[IWM_MAX_QUEUES];
	int sc_rx_ba_sessions;
	int tx_ba_queue_mask;

	struct task bgscan_done_task;
	struct ieee80211_node_switch_bss_arg *bgscan_unref_arg;
	size_t	bgscan_unref_arg_size;

	int sc_scan_last_antenna;

	int sc_fixed_ridx;

	int sc_staid;
	int sc_nodecolor;

	uint8_t *sc_cmd_resp_pkt[IWM_TX_RING_COUNT];
	size_t sc_cmd_resp_len[IWM_TX_RING_COUNT];
	int sc_nic_locks;

	struct taskq *sc_nswq;

	struct iwm_rx_phy_info sc_last_phy_info;
	int sc_ampdu_ref;
#define IWM_MAX_BAID	32
	struct iwm_rxba_data sc_rxba_data[IWM_MAX_BAID];

	uint32_t sc_time_event_uid;

	/* phy contexts.  we only use the first one */
	struct iwm_phy_ctxt sc_phyctxt[IWM_NUM_PHY_CTX];

	struct iwm_notif_statistics sc_stats;
	int sc_noise;

	int host_interrupt_operation_mode;
	int sc_ltr_enabled;
	enum iwm_nvm_type nvm_type;

	int sc_mqrx_supported;
	int sc_integrated;
	int sc_ltr_delay;
	int sc_xtal_latency;
	int sc_low_latency_xtal;

	/*
	 * Paging parameters - All of the parameters should be set by the
	 * opmode when paging is enabled
	 */
	struct iwm_fw_paging fw_paging_db[IWM_NUM_OF_FW_PAGING_BLOCKS];
	uint16_t num_of_paging_blk;
	uint16_t num_of_pages_in_last_blk;

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;

	union {
		struct iwm_rx_radiotap_header th;
		uint8_t	pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct iwm_tx_radiotap_header th;
		uint8_t	pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int			sc_txtap_len;
#endif
};

struct iwm_node {
	struct ieee80211_node in_ni;
	struct iwm_phy_ctxt *in_phyctxt;
	uint8_t in_macaddr[ETHER_ADDR_LEN];

	uint16_t in_id;
	uint16_t in_color;

	struct ieee80211_amrr_node in_amn;
	struct ieee80211_ra_node in_rn;
	struct ieee80211_ra_vht_node in_rn_vht;
	int lq_rate_mismatch;

	struct iwm_rxq_dup_data dup_data;

	/* For use with the ADD_STA command. */
	uint32_t tfd_queue_msk;
	uint16_t tid_disable_ampdu;
};
#define IWM_STATION_ID 0
#define IWM_AUX_STA_ID 1
#define IWM_MONITOR_STA_ID 2

#define IWM_ICT_SIZE		4096
#define IWM_ICT_COUNT		(IWM_ICT_SIZE / sizeof (uint32_t))
#define IWM_ICT_PADDR_SHIFT	12
