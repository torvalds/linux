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
 * OCS linux driver IO declarations
 */

#if !defined(__OCS_IO_H__)
#define __OCS_IO_H__

#define io_error_log(io, fmt, ...)  \
	do { \
		if (OCS_LOG_ENABLE_IO_ERRORS(io->ocs)) \
			ocs_log_warn(io->ocs, fmt, ##__VA_ARGS__); \
	} while (0)

/**
 * @brief FCP IO context
 *
 * This structure is used for transport and backend IO requests and responses.
 */

#define SCSI_CMD_BUF_LENGTH		48
#define SCSI_RSP_BUF_LENGTH		sizeof(fcp_rsp_iu_t)

/**
 * @brief OCS IO types
 */
typedef enum {
	OCS_IO_TYPE_IO = 0,
	OCS_IO_TYPE_ELS,
	OCS_IO_TYPE_CT,
	OCS_IO_TYPE_CT_RESP,
	OCS_IO_TYPE_BLS_RESP,
	OCS_IO_TYPE_ABORT,

	OCS_IO_TYPE_MAX,		/**< must be last */
} ocs_io_type_e;

struct ocs_io_s {

	ocs_t *ocs;			/**< pointer back to ocs */
	uint32_t instance_index;	/**< unique instance index value */
	const char *display_name;	/**< display name */
	ocs_node_t *node;		/**< pointer to node */
	ocs_list_link_t io_alloc_link;	/**< (io_pool->io_free_list) free list link */
	uint32_t init_task_tag;		/**< initiator task tag (OX_ID) for back-end and SCSI logging */
	uint32_t tgt_task_tag;		/**< target task tag (RX_ID) - for back-end and SCSI logging */
	uint32_t hw_tag;		/**< HW layer unique IO id - for back-end and SCSI logging */
	uint32_t tag;			/**< unique IO identifier */
	ocs_scsi_sgl_t *sgl;		/**< SGL */
	uint32_t sgl_allocated;		/**< Number of allocated SGEs */
	uint32_t sgl_count;		/**< Number of SGEs in this SGL */
	ocs_scsi_ini_io_t ini_io;	/**< backend initiator private IO data */
	ocs_scsi_tgt_io_t tgt_io;	/**< backend target private IO data */
	uint32_t exp_xfer_len;		/**< expected data transfer length, based on FC or iSCSI header */
	ocs_mgmt_functions_t *mgmt_functions;

	/* Declarations private to HW/SLI */
	void *hw_priv;			/**< HW private context */

	/* Declarations private to FC Transport */
	ocs_io_type_e io_type;		/**< indicates what this ocs_io_t structure is used for */
	ocs_ref_t ref;			/**< refcount object */
	void *dslab_item;		/**< pointer back to dslab allocation object */
	ocs_hw_io_t *hio;		/**< HW IO context */
	size_t transferred;		/**< Number of bytes transferred so far */
	uint32_t auto_resp:1,		/**< set if auto_trsp was set */
		 low_latency:1,		/**< set if low latency request */
		 wq_steering:4,		/**< selected WQ steering request */
		 wq_class:4;		/**< selected WQ class if steering is class */
	uint32_t xfer_req;		/**< transfer size for current request */
	ocs_scsi_rsp_io_cb_t scsi_ini_cb; /**< initiator callback function */
	void *scsi_ini_cb_arg;		/**< initiator callback function argument */
	ocs_scsi_io_cb_t scsi_tgt_cb;	/**< target callback function */
	void *scsi_tgt_cb_arg;		/**< target callback function argument */
	ocs_scsi_io_cb_t abort_cb;	/**< abort callback function */
	void *abort_cb_arg;		/**< abort callback function argument */
	ocs_scsi_io_cb_t bls_cb;	/**< BLS callback function */
	void *bls_cb_arg;		/**< BLS callback function argument */
	ocs_scsi_tmf_cmd_e tmf_cmd;	/**< TMF command being processed */
	uint16_t abort_rx_id;		/**< rx_id from the ABTS that initiated the command abort */

	uint32_t cmd_tgt:1,		/**< True if this is a Target command */
		 send_abts:1,		/**< when aborting, indicates ABTS is to be sent */
		 cmd_ini:1,		/**< True if this is an Initiator command */
		 seq_init:1;		/**< True if local node has sequence initiative */
	ocs_hw_io_param_t iparam;	/**< iparams for hw io send call */
	ocs_hw_dif_info_t hw_dif;	/**< HW formatted DIF parameters */
	ocs_scsi_dif_info_t scsi_dif_info;	/**< DIF info saved for DIF error recovery */
	ocs_hw_io_type_e hio_type;	/**< HW IO type */
	uint32_t wire_len;		/**< wire length */
	void *hw_cb;			/**< saved HW callback */
	ocs_list_link_t io_pending_link;/**< link list link pending */

	ocs_dma_t ovfl_sgl;		/**< Overflow SGL */

	/* for ELS requests/responses */
	uint32_t els_pend:1,		/**< True if ELS is pending */
		els_active:1;		/**< True if ELS is active */
	ocs_dma_t els_req;		/**< ELS request payload buffer */
	ocs_dma_t els_rsp;		/**< ELS response payload buffer */
	ocs_sm_ctx_t els_sm;		/**< EIO IO state machine context */
	uint32_t els_evtdepth;		/**< current event posting nesting depth */
	uint32_t els_req_free:1;	/**< this els is to be free'd */
	uint32_t els_retries_remaining;	/*<< Retries remaining */
	void (*els_callback)(ocs_node_t *node, ocs_node_cb_t *cbdata, void *cbarg);
	void *els_callback_arg;
	uint32_t els_timeout_sec;	/**< timeout */

	ocs_timer_t delay_timer;	/**< delay timer */

	/* for abort handling */
	ocs_io_t *io_to_abort;		/**< pointer to IO to abort */

	ocs_list_link_t link;		/**< linked list link */
	ocs_dma_t cmdbuf;		/**< SCSI Command buffer, used for CDB (initiator) */
	ocs_dma_t rspbuf;		/**< SCSI Response buffer (i+t) */
	uint32_t  timeout;		/**< Timeout value in seconds for this IO */
	uint8_t   cs_ctl;		/**< CS_CTL priority for this IO */
	uint8_t	  io_free;		/**< Is io object in freelist > */
	uint32_t  app_id;		
};

/**
 * @brief common IO callback argument
 *
 * Callback argument used as common I/O callback argument
 */

typedef struct {
	int32_t status;				/**< completion status */
	int32_t ext_status;			/**< extended completion status */
	void *app;				/**< application argument */
} ocs_io_cb_arg_t;

/**
 * @brief Test if IO object is busy
 *
 * Return True if IO object is busy.   Busy is defined as the IO object not being on
 * the free list 
 *
 * @param io Pointer to IO object
 *
 * @return returns True if IO is busy
 */

static inline int32_t
ocs_io_busy(ocs_io_t *io)
{
	return !(io->io_free);
}

typedef struct ocs_io_pool_s ocs_io_pool_t;

extern ocs_io_pool_t *ocs_io_pool_create(ocs_t *ocs, uint32_t num_io, uint32_t num_sgl);
extern int32_t ocs_io_pool_free(ocs_io_pool_t *io_pool);
extern uint32_t ocs_io_pool_allocated(ocs_io_pool_t *io_pool);

extern ocs_io_t *ocs_io_pool_io_alloc(ocs_io_pool_t *io_pool);
extern void ocs_io_pool_io_free(ocs_io_pool_t *io_pool, ocs_io_t *io);
extern ocs_io_t *ocs_io_find_tgt_io(ocs_t *ocs, ocs_node_t *node, uint16_t ox_id, uint16_t rx_id);
extern void ocs_ddump_io(ocs_textbuf_t *textbuf, ocs_io_t *io);

#endif 
