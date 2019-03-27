/*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 * @defgroup group_udma_config UDMA Config
 * @ingroup group_udma_api
 *  UDMA Config API
 *  @{
 * @file   al_hal_udma_config.h
 *
 * @brief C Header file for the Universal DMA HAL driver for configuration APIs
 *
 */

#ifndef __AL_HAL_UDMA_CONFIG_H__
#define __AL_HAL_UDMA_CONFIG_H__

#include <al_hal_udma.h>


/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/** Scheduling mode */
enum al_udma_sch_mode {
	STRICT,			/* Strict */
	SRR,			/* Simple Sound Rubin */
	DWRR			/* Deficit Weighted Round Rubin */
};

/** AXI configuration */
struct al_udma_axi_conf {
	uint32_t axi_timeout;	/* Timeout for AXI transactions  */
	uint8_t arb_promotion;	/* arbitration promotion */
	al_bool swap_8_bytes;	/* enable 8 bytes swap instead of 4 bytes */
	al_bool swap_s2m_data;
	al_bool swap_s2m_desc;
	al_bool swap_m2s_data;
	al_bool swap_m2s_desc;
};

/** UDMA AXI M2S configuration */
struct al_udma_axi_submaster {
	uint8_t id; /* AXI ID */
	uint8_t cache_type;
	uint8_t burst;
	uint16_t used_ext;
	uint8_t bus_size;
	uint8_t qos;
	uint8_t prot;
	uint8_t max_beats;
};

/** UDMA AXI M2S configuration */
struct al_udma_m2s_axi_conf {
	struct al_udma_axi_submaster comp_write;
	struct al_udma_axi_submaster data_read;
	struct al_udma_axi_submaster desc_read;
	al_bool break_on_max_boundary; /* Data read break on max boundary */
	uint8_t min_axi_beats; /* Minimum burst for writing completion desc. */
	uint8_t ostand_max_data_read;
	uint8_t ostand_max_desc_read;
	uint8_t ostand_max_comp_req;
	uint8_t ostand_max_comp_write;
};

/** UDMA AXI S2M configuration */
struct al_udma_s2m_axi_conf {
	struct al_udma_axi_submaster data_write;
	struct al_udma_axi_submaster desc_read;
	struct al_udma_axi_submaster comp_write;
	al_bool break_on_max_boundary; /* Data read break on max boundary */
	uint8_t min_axi_beats; /* Minimum burst for writing completion desc. */
	uint8_t ostand_max_data_req;
	uint8_t ostand_max_data_write;
	uint8_t ostand_max_comp_req;
	uint8_t ostand_max_comp_write;
	uint8_t ostand_max_desc_read;
	uint8_t ack_fifo_depth;	/* size of the stream application ack fifo */
};

/** M2S error logging */
struct al_udma_err_log {
	uint32_t error_status;
	uint32_t header[4];
};

/** M2S max packet size configuration */
struct al_udma_m2s_pkt_len_conf {
	uint32_t max_pkt_size;
	al_bool encode_64k_as_zero;
};

/** M2S Descriptor Prefetch configuration */
struct al_udma_m2s_desc_pref_conf {
	uint8_t desc_fifo_depth;
	enum al_udma_sch_mode sch_mode;	/* Scheduling mode
					* (either strict or RR) */

	uint8_t max_desc_per_packet;	/* max number of descriptors to
					 * prefetch */
	/* in one burst (5b) */
	uint8_t pref_thr;
	uint8_t min_burst_above_thr;	/* min burst size when fifo above
					* pref_thr (4b)
					*/
	uint8_t min_burst_below_thr;	/* min burst size when fifo below
					* pref_thr (4b)
					*/
	uint8_t max_pkt_limit;		/* maximum number of packets in the data
					* read FIFO, defined based on header
					* FIFO size
					*/
	uint16_t data_fifo_depth;	/* maximum number of data beats in the
					* data read FIFO,
					* defined based on header FIFO size
					*/
};

/** S2M Descriptor Prefetch configuration */
struct al_udma_s2m_desc_pref_conf {
	uint8_t desc_fifo_depth;
	enum al_udma_sch_mode sch_mode;	/* Scheduling mode *
					* (either strict or RR)
					*/

	al_bool q_promotion;		/* enable promotion */
	al_bool force_promotion;	/* force promotion  */
	al_bool en_pref_prediction;	/* enable prefetch prediction */
	uint8_t promotion_th;		/* Threshold for queue promotion */

	uint8_t pref_thr;
	uint8_t min_burst_above_thr;	/* min burst size when fifo above
	 	 	 	 	 * pref_thr (4b)
	 	 	 	 	 */
	uint8_t min_burst_below_thr;	/* min burst size when fifo below
	 	 	 	 	 * pref_thr (4b)
	 	 	 	 	 */
	uint8_t a_full_thr;		/* almost full threshold */
};

/** S2M Data write configuration */
struct al_udma_s2m_data_write_conf {
	uint16_t data_fifo_depth;	/* maximum number of data beats in the
					 * data write FIFO, defined based on
					 * header FIFO size
					 */
	uint8_t max_pkt_limit;		/* maximum number of packets in the
					* data write FIFO,defined based on
					* header FIFO size
					*/
	uint8_t fifo_margin;
	uint32_t desc_wait_timer;	/* waiting time for the host to write
					* new descriptor to the queue
					* (for the current packet in process)
					*/
	uint32_t flags;			/* bitwise of flags of s2m
					 * data_cfg_2 register
					 */
};

/** S2M Completion configuration */
struct al_udma_s2m_completion_conf {
	uint8_t desc_size;		/* Size of completion descriptor
					 * in words
					 */
	al_bool cnt_words;		/* Completion fifo in use counter:
	 	 	 	 	 * AL_TRUE words, AL_FALS descriptors
	 	 	 	 	 */
	al_bool q_promotion;		/* Enable promotion of the current
					 * unack in progress */
					/* in the completion write scheduler */
	al_bool force_rr;		/* force RR arbitration in the
					*  scheduler
					*/
  //	uint8_t ack_fifo_depth;		/* size of the stream application ack fifo */
	uint8_t q_free_min;		/* minimum number of free completion
					 * entries
					 */
					/* to qualify for promotion */

	uint16_t comp_fifo_depth;	/* Size of completion fifo in words */
	uint16_t unack_fifo_depth;	/* Size of unacked fifo in descs */
	uint32_t timeout;		/* Ack timout from stream interface */
};

/** M2S UDMA DWRR configuration */
struct al_udma_m2s_dwrr_conf {
	al_bool enable_dwrr;
	uint8_t inc_factor;
	uint8_t weight;
	al_bool pkt_mode;
	uint32_t deficit_init_val;
};

/** M2S DMA Rate Limitation mode */
struct al_udma_m2s_rlimit_mode {
	al_bool pkt_mode_en;
	uint16_t short_cycle_sz;
	uint32_t token_init_val;
};

/** M2S Stream/Q Rate Limitation */
struct al_udma_m2s_rlimit_cfg {
	uint32_t max_burst_sz;	/* maximum number of accumulated bytes in the
				 * token counter
				 */
	uint16_t long_cycle_sz;	/* number of short cycles between token fill */
	uint32_t long_cycle;	/* number of bits to add in each long cycle */
	uint32_t short_cycle;	/* number of bits to add in each cycle */
	uint32_t mask;		/* mask the different types of rate limiters */
};

enum al_udma_m2s_rlimit_action {
	AL_UDMA_STRM_RLIMIT_ENABLE,
	AL_UDMA_STRM_RLIMIT_PAUSE,
	AL_UDMA_STRM_RLIMIT_RESET
};

/** M2S UDMA Q scheduling configuration */
struct al_udma_m2s_q_dwrr_conf {
	uint32_t max_deficit_cnt_sz;	/*maximum number of accumulated bytes
					* in the deficit counter
					*/
	al_bool strict;		/* bypass DWRR */
	uint8_t axi_qos;
	uint16_t q_qos;
	uint8_t weight;
};

/** M2S UDMA / UDMA Q scheduling configuration */
struct al_udma_m2s_sc {
	enum al_udma_sch_mode sch_mode;	/* Scheduling Mode */
	struct al_udma_m2s_dwrr_conf dwrr;	/* DWRR configuration */
};

/** UDMA / UDMA Q rate limitation configuration */
struct al_udma_m2s_rlimit {
	struct al_udma_m2s_rlimit_mode rlimit_mode;
						/* rate limitation enablers */
#if 0
	struct al_udma_tkn_bkt_conf token_bkt;        /* Token Bucket configuration */
#endif
};

/** UDMA Data read configuration */
struct al_udma_m2s_data_rd_conf {
	uint8_t max_rd_d_beats;		/* max burst size for reading data
					 * (in AXI beats-128b) (5b)
					 */
	uint8_t max_rd_d_out_req;	/* max number of outstanding data
					 * read requests (6b)
					 */
	uint16_t max_rd_d_out_beats;	/* max num. of data read beats (10b) */
};

/** M2S UDMA completion and application timeouts */
struct al_udma_m2s_comp_timeouts {
	enum al_udma_sch_mode sch_mode;	/* Scheduling mode
					 * (either strict or RR)
					 */
	al_bool enable_q_promotion;
	uint8_t unack_fifo_depth;	/* unacked desc fifo size */
	uint8_t comp_fifo_depth;	/* desc fifo size */
	uint32_t coal_timeout;	/* (24b) */
	uint32_t app_timeout;	/* (24b) */
};

/** S2M UDMA per queue completion configuration */
struct al_udma_s2m_q_comp_conf {
	al_bool dis_comp_coal;		/* disable completion coalescing */
	al_bool en_comp_ring_update;	/* enable writing completion descs */
	uint32_t comp_timer;		/* completion coalescing timer */
	al_bool en_hdr_split;		/* enable header split */
	al_bool force_hdr_split;	/* force header split */
	uint16_t hdr_split_size;	/* size used for the header split */
	uint8_t q_qos;			/* queue QoS */
};

/** UDMA per queue Target-ID control configuration */
struct al_udma_gen_tgtid_q_conf {
	/* Enable usage of the Target-ID per queue according to 'tgtid' */
	al_bool queue_en;

	/* Enable usage of the Target-ID from the descriptor buffer address 63:48 */
	al_bool desc_en;

	/* Target-ID to be applied when 'queue_en' is asserted */
	uint16_t tgtid;

	/* TGTADDR to be applied to msbs when 'desc_en' is asserted.
	 * Relevant for revisions >= AL_UDMA_REV_ID_REV2 */
	uint16_t tgtaddr;
};

/** UDMA Target-ID control configuration */
struct al_udma_gen_tgtid_conf {
	/* TX queue configuration */
	struct al_udma_gen_tgtid_q_conf tx_q_conf[DMA_MAX_Q];

	/* RX queue configuration */
	struct al_udma_gen_tgtid_q_conf rx_q_conf[DMA_MAX_Q];
};

/** UDMA Target-ID MSIX control configuration */
struct al_udma_gen_tgtid_msix_conf {
	/* Enable write to all TGTID_n registers in the MSI-X Controller */
	al_bool access_en;

	/* use TGTID_n [7:0] from MSI-X Controller for MSI-X message */
	al_bool sel;
};

/* Report Error - to be used for abort */
void al_udma_err_report(struct al_udma *udma);

/* Statistics - TBD */
void al_udma_stats_get(struct al_udma *udma);

/* Misc configurations */
/* Configure AXI configuration */
int al_udma_axi_set(struct udma_gen_axi *axi_regs,
		    struct al_udma_axi_conf *axi);

/* Configure UDMA AXI M2S configuration */
int al_udma_m2s_axi_set(struct al_udma *udma,
			struct al_udma_m2s_axi_conf *axi_m2s);

/* Configure UDMA AXI S2M configuration */
int al_udma_s2m_axi_set(struct al_udma *udma,
			struct al_udma_s2m_axi_conf *axi_s2m);

/* Configure M2S packet len */
int al_udma_m2s_packet_size_cfg_set(struct al_udma *udma,
				    struct al_udma_m2s_pkt_len_conf *conf);

/* Configure M2S UDMA descriptor prefetch */
int al_udma_m2s_pref_set(struct al_udma *udma,
			 struct al_udma_m2s_desc_pref_conf *conf);
int al_udma_m2s_pref_get(struct al_udma *udma,
			 struct al_udma_m2s_desc_pref_conf *conf);

/* set m2s packet's max descriptors (including meta descriptors) */
#define AL_UDMA_M2S_MAX_ALLOWED_DESCS_PER_PACKET	31
int al_udma_m2s_max_descs_set(struct al_udma *udma, uint8_t max_descs);

/* set s2m packets' max descriptors */
#define AL_UDMA_S2M_MAX_ALLOWED_DESCS_PER_PACKET	31
int al_udma_s2m_max_descs_set(struct al_udma *udma, uint8_t max_descs);


/* Configure S2M UDMA descriptor prefetch */
int al_udma_s2m_pref_set(struct al_udma *udma,
			 struct al_udma_s2m_desc_pref_conf *conf);
int al_udma_m2s_pref_get(struct al_udma *udma,
			 struct al_udma_m2s_desc_pref_conf *conf);

/* Configure S2M UDMA data write */
int al_udma_s2m_data_write_set(struct al_udma *udma,
			       struct al_udma_s2m_data_write_conf *conf);

/* Configure the s2m full line write feature */
int al_udma_s2m_full_line_write_set(struct al_udma *umda, al_bool enable);

/* Configure S2M UDMA completion */
int al_udma_s2m_completion_set(struct al_udma *udma,
			       struct al_udma_s2m_completion_conf *conf);

/* Configure the M2S UDMA scheduling mode */
int al_udma_m2s_sc_set(struct al_udma *udma,
		       struct al_udma_m2s_dwrr_conf *sched);

/* Configure the M2S UDMA rate limitation */
int al_udma_m2s_rlimit_set(struct al_udma *udma,
			   struct al_udma_m2s_rlimit_mode *mode);
int al_udma_m2s_rlimit_reset(struct al_udma *udma);

/* Configure the M2S Stream rate limitation */
int al_udma_m2s_strm_rlimit_set(struct al_udma *udma,
				struct al_udma_m2s_rlimit_cfg *conf);
int al_udma_m2s_strm_rlimit_act(struct al_udma *udma,
				enum al_udma_m2s_rlimit_action act);

/* Configure the M2S UDMA Q rate limitation */
int al_udma_m2s_q_rlimit_set(struct al_udma_q *udma_q,
			     struct al_udma_m2s_rlimit_cfg *conf);
int al_udma_m2s_q_rlimit_act(struct al_udma_q *udma_q,
			     enum al_udma_m2s_rlimit_action act);

/* Configure the M2S UDMA Q scheduling mode */
int al_udma_m2s_q_sc_set(struct al_udma_q *udma_q,
			 struct al_udma_m2s_q_dwrr_conf *conf);
int al_udma_m2s_q_sc_pause(struct al_udma_q *udma_q, al_bool set);
int al_udma_m2s_q_sc_reset(struct al_udma_q *udma_q);

/* M2S UDMA completion and application timeouts */
int al_udma_m2s_comp_timeouts_set(struct al_udma *udma,
				  struct al_udma_m2s_comp_timeouts *conf);
int al_udma_m2s_comp_timeouts_get(struct al_udma *udma,
				  struct al_udma_m2s_comp_timeouts *conf);

/* UDMA get revision */
static INLINE unsigned int al_udma_get_revision(struct unit_regs __iomem *unit_regs)
{
	return (al_reg_read32(&unit_regs->gen.dma_misc.revision)
			& UDMA_GEN_DMA_MISC_REVISION_PROGRAMMING_ID_MASK) >>
			UDMA_GEN_DMA_MISC_REVISION_PROGRAMMING_ID_SHIFT;
}

/**
 * S2M UDMA Configure the expected behavior of Rx/S2M UDMA when there are no Rx Descriptors.
 *
 * @param udma
 * @param drop_packet when set to true, the UDMA will drop packet.
 * @param gen_interrupt when set to true, the UDMA will generate
 *        no_desc_hint interrupt when a packet received and the UDMA
 *	  doesn't find enough free descriptors for it.
 * @param wait_for_desc_timeout timeout in SB cycles to wait for new
 *	  descriptors before dropping the packets.
 *	  Notes:
 *		- The hint interrupt is raised immediately without waiting
 *		for new descs.
 *		- value 0 means wait for ever.
 *
 * Notes:
 * - When get_interrupt is set, the API won't program the iofic to unmask this
 * interrupt, in this case the callee should take care for doing that unmask
 * using the al_udma_iofic_config() API.
 *
 * - The hardware's default configuration is: no drop packet, generate hint
 * interrupt.
 * - This API must be called once and before enabling the UDMA
 *
 * @return 0 if no error found.
 */
int al_udma_s2m_no_desc_cfg_set(struct al_udma *udma, al_bool drop_packet, al_bool gen_interrupt, uint32_t wait_for_desc_timeout);

/**
 * S2M UDMA configure a queue's completion update
 *
 * @param q_udma
 * @param enable set to true to enable completion update
 *
 * completion update better be disabled for tx queues as those descriptors
 * doesn't carry useful information, thus disabling it saves DMA accesses.
 *
 * @return 0 if no error found.
 */
int al_udma_s2m_q_compl_updade_config(struct al_udma_q *udma_q, al_bool enable);

/**
 * S2M UDMA configure a queue's completion descriptors coalescing
 *
 * @param q_udma
 * @param enable set to true to enable completion coalescing
 * @param coal_timeout in South Bridge cycles.
 *
 * @return 0 if no error found.
 */
int al_udma_s2m_q_compl_coal_config(struct al_udma_q *udma_q, al_bool enable, uint32_t coal_timeout);

/**
 * S2M UDMA configure completion descriptors write burst parameters
 *
 * @param udma
 * @param burst_size completion descriptors write burst size in bytes.
 *
 * @return 0 if no error found.
 */int al_udma_s2m_compl_desc_burst_config(struct al_udma *udma, uint16_t
		 burst_size);

/**
 * S2M UDMA configure a queue's completion header split
 *
 * @param q_udma
 * @param enable set to true to enable completion header split
 * @param force_hdr_split the header split length will be taken from the queue configuration
 * @param hdr_len header split length.
 *
 * @return 0 if no error found.
 */
int al_udma_s2m_q_compl_hdr_split_config(struct al_udma_q *udma_q,
					 al_bool enable,
					 al_bool force_hdr_split,
					 uint32_t hdr_len);

/* S2M UDMA per queue completion configuration */
int al_udma_s2m_q_comp_set(struct al_udma_q *udma_q,
			   struct al_udma_s2m_q_comp_conf *conf);

/** UDMA Target-ID control configuration per queue */
void al_udma_gen_tgtid_conf_queue_set(
	struct unit_regs		*unit_regs,
	struct al_udma_gen_tgtid_conf	*conf,
	uint32_t qid);

/** UDMA Target-ID control configuration */
void al_udma_gen_tgtid_conf_set(
	struct unit_regs __iomem	*unit_regs,
	struct al_udma_gen_tgtid_conf	*conf);

/** UDMA Target-ID MSIX control configuration */
void al_udma_gen_tgtid_msix_conf_set(
	struct unit_regs __iomem		*unit_regs,
	struct al_udma_gen_tgtid_msix_conf	*conf);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
/** @} end of UDMA config group */
#endif /* __AL_HAL_UDMA_CONFIG_H__ */
