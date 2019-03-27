/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 *
 */

#if !defined(__OCS_XPORT_H__)
#define __OCS_XPORT_H__

/**
 * @brief FCFI lookup/pending frames
 */
typedef struct ocs_xport_fcfi_s {
	ocs_lock_t	pend_frames_lock;
	ocs_list_t	pend_frames;
	uint32_t	hold_frames:1;		/*<< hold pending frames */
	uint32_t	pend_frames_processed;	/*<< count of pending frames that were processed */
} ocs_xport_fcfi_t;

/**
 * @brief Structure to hold the information related to an RQ processing thread used
 *        to increase 40G performance.
 */
typedef struct ocs_xport_rq_thread_info_s {
	ocs_t *ocs;
	uint8_t thread_started;
	ocs_thread_t thread;
	ocs_cbuf_t * seq_cbuf;
	char thread_name[64];
} ocs_xport_rq_thread_info_t;

typedef enum {
	OCS_XPORT_PORT_ONLINE=1,
	OCS_XPORT_PORT_OFFLINE,
	OCS_XPORT_SHUTDOWN,
	OCS_XPORT_POST_NODE_EVENT,
	OCS_XPORT_WWNN_SET,
	OCS_XPORT_WWPN_SET,
} ocs_xport_ctrl_e;

typedef enum {
	OCS_XPORT_PORT_STATUS,
	OCS_XPORT_CONFIG_PORT_STATUS,
	OCS_XPORT_LINK_SPEED,
	OCS_XPORT_IS_SUPPORTED_LINK_SPEED,
	OCS_XPORT_LINK_STATISTICS,
	OCS_XPORT_LINK_STAT_RESET,
	OCS_XPORT_IS_QUIESCED
} ocs_xport_status_e;

typedef struct ocs_xport_link_stats_s {
	uint32_t	rec:1,
				gec:1,
				w02of:1,
				w03of:1,
				w04of:1,
				w05of:1,
				w06of:1,
				w07of:1,
				w08of:1,
				w09of:1,
				w10of:1,
				w11of:1,
				w12of:1,
				w13of:1,
				w14of:1,
				w15of:1,
				w16of:1,
				w17of:1,
				w18of:1,
				w19of:1,
				w20of:1,
				w21of:1,
				resv0:8,
				clrc:1,
				clof:1;
	uint32_t	link_failure_error_count;
	uint32_t	loss_of_sync_error_count;
	uint32_t	loss_of_signal_error_count;
	uint32_t	primitive_sequence_error_count;
	uint32_t	invalid_transmission_word_error_count;
	uint32_t	crc_error_count;
	uint32_t	primitive_sequence_event_timeout_count;
	uint32_t	elastic_buffer_overrun_error_count;
	uint32_t	arbitration_fc_al_timout_count;
	uint32_t	advertised_receive_bufftor_to_buffer_credit;
	uint32_t	current_receive_buffer_to_buffer_credit;
	uint32_t	advertised_transmit_buffer_to_buffer_credit;
	uint32_t	current_transmit_buffer_to_buffer_credit;
	uint32_t	received_eofa_count;
	uint32_t	received_eofdti_count;
	uint32_t	received_eofni_count;
	uint32_t	received_soff_count;
	uint32_t	received_dropped_no_aer_count;
	uint32_t	received_dropped_no_available_rpi_resources_count;
	uint32_t	received_dropped_no_available_xri_resources_count;
} ocs_xport_link_stats_t;

typedef struct ocs_xport_host_stats_s {
	uint32_t	cc:1,
				  :31;
	uint32_t	transmit_kbyte_count;
	uint32_t	receive_kbyte_count;
	uint32_t	transmit_frame_count;
	uint32_t	receive_frame_count;
	uint32_t	transmit_sequence_count;
	uint32_t	receive_sequence_count;
	uint32_t	total_exchanges_originator;
	uint32_t	total_exchanges_responder;
	uint32_t	receive_p_bsy_count;
	uint32_t	receive_f_bsy_count;
	uint32_t	dropped_frames_due_to_no_rq_buffer_count;
	uint32_t	empty_rq_timeout_count;
	uint32_t	dropped_frames_due_to_no_xri_count;
	uint32_t	empty_xri_pool_count;
} ocs_xport_host_stats_t;

typedef struct ocs_xport_host_statistics_s {
	ocs_sem_t semaphore;
	ocs_xport_link_stats_t link_stats;
	ocs_xport_host_stats_t host_stats;
} ocs_xport_host_statistics_t;

typedef union ocs_xport {
	uint32_t value;
	ocs_xport_host_statistics_t stats;
} ocs_xport_stats_t;
/**
 * @brief Transport private values
 */
struct ocs_xport_s {
	ocs_t *ocs;
	uint64_t req_wwpn;			/*<< wwpn requested by user for primary sport */
	uint64_t req_wwnn;			/*<< wwnn requested by user for primary sport */

	ocs_xport_fcfi_t fcfi[SLI4_MAX_FCFI];

	/* Nodes */
	uint32_t nodes_count;			/**< number of allocated nodes */
	ocs_node_t **nodes;			/**< array of pointers to nodes */
	ocs_list_t nodes_free_list;		/**< linked list of free nodes */

	/* Io pool and counts */
	ocs_io_pool_t *io_pool;			/**< pointer to IO pool */
	ocs_atomic_t io_alloc_failed_count;	/**< used to track how often IO pool is empty */
	ocs_lock_t io_pending_lock;		/**< lock for io_pending_list */
	ocs_list_t io_pending_list;		/**< list of IOs waiting for HW resources
						 **  lock: xport->io_pending_lock
						 **  link: ocs_io_t->io_pending_link
						 */
	ocs_atomic_t io_total_alloc;		/**< count of totals IOS allocated */
	ocs_atomic_t io_total_free;		/**< count of totals IOS free'd */
	ocs_atomic_t io_total_pending;		/**< count of totals IOS that were pended */
	ocs_atomic_t io_active_count;		/**< count of active IOS */
	ocs_atomic_t io_pending_count;		/**< count of pending IOS */
	ocs_atomic_t io_pending_recursing;	/**< non-zero if ocs_scsi_check_pending is executing */

	/* vport */
	ocs_list_t vport_list;			/**< list of VPORTS (NPIV) */

	/* Port */
	uint32_t configured_link_state;		/**< requested link state */

	/* RQ processing threads */
	uint32_t num_rq_threads;
	ocs_xport_rq_thread_info_t *rq_thread_info;

	ocs_timer_t     stats_timer;            /**< Timer for Statistics */
	ocs_xport_stats_t fc_xport_stats;
};


extern ocs_xport_t *ocs_xport_alloc(ocs_t *ocs);
extern int32_t ocs_xport_attach(ocs_xport_t *xport);
extern int32_t ocs_xport_initialize(ocs_xport_t *xport);
extern int32_t ocs_xport_detach(ocs_xport_t *xport);
extern int32_t ocs_xport_control(ocs_xport_t *xport, ocs_xport_ctrl_e cmd, ...);
extern int32_t ocs_xport_status(ocs_xport_t *xport, ocs_xport_status_e cmd, ocs_xport_stats_t *result);
extern void ocs_xport_free(ocs_xport_t *xport);

#endif 
