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
 * Defines the Hardware Abstraction Layer (HW) interface functions.
 */

#ifndef _OCS_HW_H
#define _OCS_HW_H

#include "sli4.h"
#include "ocs_hw.h"
#include "ocs_stats.h"
#include "ocs_utils.h"

typedef struct ocs_hw_io_s ocs_hw_io_t;


#if defined(OCS_INCLUDE_DEBUG)
#else
#define ocs_queue_history_wq(...)
#define ocs_queue_history_cqe(...)
#define ocs_queue_history_init(...)
#define ocs_queue_history_free(...)
#endif

/**
 * @brief HW queue forward declarations
 */
typedef struct hw_eq_s hw_eq_t;
typedef struct hw_cq_s hw_cq_t;
typedef struct hw_mq_s hw_mq_t;
typedef struct hw_wq_s hw_wq_t;
typedef struct hw_rq_s hw_rq_t;
typedef struct hw_rq_grp_s hw_rq_grp_t;

/* HW asserts/verify
 *
 */

extern void _ocs_hw_assert(const char *cond, const char *filename, int linenum);
extern void _ocs_hw_verify(const char *cond, const char *filename, int linenum);

#if defined(HW_NDEBUG)
#define ocs_hw_assert(cond)
#define ocs_hw_verify(cond, ...)
#else
#define ocs_hw_assert(cond) \
	do { \
		if ((!(cond))) { \
			_ocs_hw_assert(#cond, __FILE__, __LINE__); \
		} \
	} while (0)

#define ocs_hw_verify(cond, ...) \
	do { \
		if ((!(cond))) { \
			_ocs_hw_verify(#cond, __FILE__, __LINE__); \
			return __VA_ARGS__; \
		} \
	} while (0)
#endif
#define ocs_hw_verify_arg(cond)	ocs_hw_verify(cond, OCS_HW_RTN_INVALID_ARG)

/*
 * HW completion loop control parameters.
 *
 * The HW completion loop must terminate periodically to keep the OS happy.  The
 * loop terminates when a predefined time has elapsed, but to keep the overhead of
 * computing time down, the time is only checked after a number of loop iterations
 * has completed.
 *
 * OCS_HW_TIMECHECK_ITERATIONS		number of loop iterations between time checks
 *
 */

#define OCS_HW_TIMECHECK_ITERATIONS	100
#define OCS_HW_MAX_NUM_MQ 1
#define OCS_HW_MAX_NUM_RQ 32
#define OCS_HW_MAX_NUM_EQ 16
#define OCS_HW_MAX_NUM_WQ 32

#define OCE_HW_MAX_NUM_MRQ_PAIRS 16

#define OCS_HW_MAX_WQ_CLASS	4
#define OCS_HW_MAX_WQ_CPU	128

/*
 * A CQ will be assinged to each WQ (CQ must have 2X entries of the WQ for abort
 * processing), plus a separate one for each RQ PAIR and one for MQ
 */
#define OCS_HW_MAX_NUM_CQ ((OCS_HW_MAX_NUM_WQ*2) + 1 + (OCE_HW_MAX_NUM_MRQ_PAIRS * 2))

/*
 * Q hash - size is the maximum of all the queue sizes, rounded up to the next
 * power of 2
 */
#define OCS_HW_Q_HASH_SIZE	B32_NEXT_POWER_OF_2(OCS_MAX(OCS_HW_MAX_NUM_MQ, OCS_MAX(OCS_HW_MAX_NUM_RQ, \
				OCS_MAX(OCS_HW_MAX_NUM_EQ, OCS_MAX(OCS_HW_MAX_NUM_WQ, \
				OCS_HW_MAX_NUM_CQ)))))

#define OCS_HW_RQ_HEADER_SIZE	128
#define OCS_HW_RQ_HEADER_INDEX	0

/**
 * @brief Options for ocs_hw_command().
 */
enum {
	OCS_CMD_POLL,	/**< command executes synchronously and busy-waits for completion */
	OCS_CMD_NOWAIT,	/**< command executes asynchronously. Uses callback */
};

typedef enum {
	OCS_HW_RTN_SUCCESS = 0,
	OCS_HW_RTN_SUCCESS_SYNC = 1,
	OCS_HW_RTN_ERROR = -1,
	OCS_HW_RTN_NO_RESOURCES = -2,
	OCS_HW_RTN_NO_MEMORY = -3,
	OCS_HW_RTN_IO_NOT_ACTIVE = -4,
	OCS_HW_RTN_IO_ABORT_IN_PROGRESS = -5,
	OCS_HW_RTN_IO_PORT_OWNED_ALREADY_ABORTED = -6,
	OCS_HW_RTN_INVALID_ARG = -7,
} ocs_hw_rtn_e;
#define OCS_HW_RTN_IS_ERROR(e)	((e) < 0)

typedef enum {
	OCS_HW_RESET_FUNCTION,
	OCS_HW_RESET_FIRMWARE,
	OCS_HW_RESET_MAX
} ocs_hw_reset_e;

typedef enum {
	OCS_HW_N_IO,
	OCS_HW_N_SGL,
	OCS_HW_MAX_IO,
	OCS_HW_MAX_SGE,
	OCS_HW_MAX_SGL,
	OCS_HW_MAX_NODES,
	OCS_HW_MAX_RQ_ENTRIES,
	OCS_HW_TOPOLOGY,	/**< auto, nport, loop */
	OCS_HW_WWN_NODE,
	OCS_HW_WWN_PORT,
	OCS_HW_FW_REV,
	OCS_HW_FW_REV2,
	OCS_HW_IPL,
	OCS_HW_VPD,
	OCS_HW_VPD_LEN,
	OCS_HW_MODE,		/**< initiator, target, both */
	OCS_HW_LINK_SPEED,
	OCS_HW_IF_TYPE,
	OCS_HW_SLI_REV,
	OCS_HW_SLI_FAMILY,
	OCS_HW_RQ_PROCESS_LIMIT,
	OCS_HW_RQ_DEFAULT_BUFFER_SIZE,
	OCS_HW_AUTO_XFER_RDY_CAPABLE,
	OCS_HW_AUTO_XFER_RDY_XRI_CNT,
	OCS_HW_AUTO_XFER_RDY_SIZE,
	OCS_HW_AUTO_XFER_RDY_BLK_SIZE,
	OCS_HW_AUTO_XFER_RDY_T10_ENABLE,
	OCS_HW_AUTO_XFER_RDY_P_TYPE,
	OCS_HW_AUTO_XFER_RDY_REF_TAG_IS_LBA,
	OCS_HW_AUTO_XFER_RDY_APP_TAG_VALID,
	OCS_HW_AUTO_XFER_RDY_APP_TAG_VALUE,
	OCS_HW_DIF_CAPABLE,
	OCS_HW_DIF_SEED,
	OCS_HW_DIF_MODE,
	OCS_HW_DIF_MULTI_SEPARATE,
	OCS_HW_DUMP_MAX_SIZE,
	OCS_HW_DUMP_READY,
	OCS_HW_DUMP_PRESENT,
	OCS_HW_RESET_REQUIRED,
	OCS_HW_FW_ERROR,
	OCS_HW_FW_READY,
	OCS_HW_HIGH_LOGIN_MODE,
	OCS_HW_PREREGISTER_SGL,
	OCS_HW_HW_REV1,
	OCS_HW_HW_REV2,
	OCS_HW_HW_REV3,
	OCS_HW_LINKCFG,
	OCS_HW_ETH_LICENSE,
	OCS_HW_LINK_MODULE_TYPE,
	OCS_HW_NUM_CHUTES,
	OCS_HW_WAR_VERSION,
	OCS_HW_DISABLE_AR_TGT_DIF,
	OCS_HW_EMULATE_I_ONLY_AAB, /**< emulate IAAB=0 for initiator-commands only */
	OCS_HW_EMULATE_TARGET_WQE_TIMEOUT, /**< enable driver timeouts for target WQEs */
	OCS_HW_LINK_CONFIG_SPEED,
	OCS_HW_CONFIG_TOPOLOGY,
	OCS_HW_BOUNCE,
	OCS_HW_PORTNUM,
	OCS_HW_BIOS_VERSION_STRING,
	OCS_HW_RQ_SELECT_POLICY,
	OCS_HW_SGL_CHAINING_CAPABLE,
	OCS_HW_SGL_CHAINING_ALLOWED,
	OCS_HW_SGL_CHAINING_HOST_ALLOCATED,
	OCS_HW_SEND_FRAME_CAPABLE,
	OCS_HW_RQ_SELECTION_POLICY,
	OCS_HW_RR_QUANTA,
	OCS_HW_FILTER_DEF,
	OCS_HW_MAX_VPORTS,
	OCS_ESOC,
	OCS_HW_FW_TIMED_OUT,
} ocs_hw_property_e;

enum {
	OCS_HW_TOPOLOGY_AUTO,
	OCS_HW_TOPOLOGY_NPORT,
	OCS_HW_TOPOLOGY_LOOP,
	OCS_HW_TOPOLOGY_NONE,
	OCS_HW_TOPOLOGY_MAX
};

enum {
	OCS_HW_MODE_INITIATOR,
	OCS_HW_MODE_TARGET,
	OCS_HW_MODE_BOTH,
	OCS_HW_MODE_MAX
};

/**
 * @brief Port protocols
 */

typedef enum {
	OCS_HW_PORT_PROTOCOL_ISCSI,
	OCS_HW_PORT_PROTOCOL_FCOE,
	OCS_HW_PORT_PROTOCOL_FC,
	OCS_HW_PORT_PROTOCOL_OTHER,
} ocs_hw_port_protocol_e;

#define OCS_HW_MAX_PROFILES	40
/**
 * @brief A Profile Descriptor
 */
typedef struct {
	uint32_t	profile_index;
	uint32_t	profile_id;
	char		profile_description[512];
} ocs_hw_profile_descriptor_t;

/**
 * @brief A Profile List
 */
typedef struct {
	uint32_t			num_descriptors;
	ocs_hw_profile_descriptor_t	descriptors[OCS_HW_MAX_PROFILES];
} ocs_hw_profile_list_t;


/**
 * @brief Defines DIF operation modes
 */
enum {
	OCS_HW_DIF_MODE_INLINE,
	OCS_HW_DIF_MODE_SEPARATE,
};

/**
 * @brief Defines the type of RQ buffer
 */
typedef enum {
	OCS_HW_RQ_BUFFER_TYPE_HDR,
	OCS_HW_RQ_BUFFER_TYPE_PAYLOAD,
	OCS_HW_RQ_BUFFER_TYPE_MAX,
} ocs_hw_rq_buffer_type_e;

/**
 * @brief Defines a wrapper for the RQ payload buffers so that we can place it
 *        back on the proper queue.
 */
typedef struct {
	uint16_t rqindex;
	ocs_dma_t dma;
} ocs_hw_rq_buffer_t;

/**
 * @brief T10 DIF operations.
 */
typedef enum {
	OCS_HW_DIF_OPER_DISABLED,
	OCS_HW_SGE_DIF_OP_IN_NODIF_OUT_CRC,
	OCS_HW_SGE_DIF_OP_IN_CRC_OUT_NODIF,
	OCS_HW_SGE_DIF_OP_IN_NODIF_OUT_CHKSUM,
	OCS_HW_SGE_DIF_OP_IN_CHKSUM_OUT_NODIF,
	OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CRC,
	OCS_HW_SGE_DIF_OP_IN_CHKSUM_OUT_CHKSUM,
	OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CHKSUM,
	OCS_HW_SGE_DIF_OP_IN_CHKSUM_OUT_CRC,
	OCS_HW_SGE_DIF_OP_IN_RAW_OUT_RAW,
} ocs_hw_dif_oper_e;

#define OCS_HW_DIF_OPER_PASS_THRU	OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CRC
#define OCS_HW_DIF_OPER_STRIP		OCS_HW_SGE_DIF_OP_IN_CRC_OUT_NODIF
#define OCS_HW_DIF_OPER_INSERT		OCS_HW_SGE_DIF_OP_IN_NODIF_OUT_CRC

/**
 * @brief T10 DIF block sizes.
 */
typedef enum {
	OCS_HW_DIF_BK_SIZE_512,
	OCS_HW_DIF_BK_SIZE_1024,
	OCS_HW_DIF_BK_SIZE_2048,
	OCS_HW_DIF_BK_SIZE_4096,
	OCS_HW_DIF_BK_SIZE_520,
	OCS_HW_DIF_BK_SIZE_4104,
	OCS_HW_DIF_BK_SIZE_NA = 0
} ocs_hw_dif_blk_size_e;

/**
 * @brief Link configurations.
 */
typedef enum {
	OCS_HW_LINKCFG_4X10G = 0,
	OCS_HW_LINKCFG_1X40G,
	OCS_HW_LINKCFG_2X16G,
	OCS_HW_LINKCFG_4X8G,
	OCS_HW_LINKCFG_4X1G,
	OCS_HW_LINKCFG_2X10G,
	OCS_HW_LINKCFG_2X10G_2X8G,

	/* must be last */
	OCS_HW_LINKCFG_NA,
} ocs_hw_linkcfg_e;

/**
 * @brief link module types
 *
 * (note: these just happen to match SLI4 values)
 */

enum {
	OCS_HW_LINK_MODULE_TYPE_1GB = 0x0004,
	OCS_HW_LINK_MODULE_TYPE_2GB = 0x0008,
	OCS_HW_LINK_MODULE_TYPE_4GB = 0x0040,
	OCS_HW_LINK_MODULE_TYPE_8GB = 0x0080,
	OCS_HW_LINK_MODULE_TYPE_10GB = 0x0100,
	OCS_HW_LINK_MODULE_TYPE_16GB = 0x0200,
	OCS_HW_LINK_MODULE_TYPE_32GB = 0x0400,
};

/**
 * @brief T10 DIF information passed to the transport.
 */
typedef struct ocs_hw_dif_info_s {
	ocs_hw_dif_oper_e dif_oper;
	ocs_hw_dif_blk_size_e blk_size;
	uint32_t ref_tag_cmp;
	uint32_t ref_tag_repl;
	uint32_t app_tag_cmp:16,
		app_tag_repl:16;
	uint32_t check_ref_tag:1,
		check_app_tag:1,
		check_guard:1,
		auto_incr_ref_tag:1,
		repl_app_tag:1,
		repl_ref_tag:1,
		dif:2,
		dif_separate:1,

		/* If the APP TAG is 0xFFFF, disable checking the REF TAG and CRC fields */
		disable_app_ffff:1,

		/* if the APP TAG is 0xFFFF and REF TAG is 0xFFFF_FFFF, disable checking the received CRC field. */
		disable_app_ref_ffff:1,

		:21;
	uint16_t dif_seed;
} ocs_hw_dif_info_t;
typedef enum {
	OCS_HW_ELS_REQ,	/**< ELS request */
	OCS_HW_ELS_RSP,	/**< ELS response */
	OCS_HW_ELS_RSP_SID,	/**< ELS response, override the S_ID */
	OCS_HW_FC_CT,		/**< FC Common Transport */
	OCS_HW_FC_CT_RSP,	/**< FC Common Transport Response */
	OCS_HW_BLS_ACC,	/**< BLS accept (BA_ACC) */
	OCS_HW_BLS_ACC_SID,	/**< BLS accept (BA_ACC), override the S_ID */
	OCS_HW_BLS_RJT,	/**< BLS reject (BA_RJT) */
	OCS_HW_BCAST,		/**< Class 3 broadcast sequence */
	OCS_HW_IO_TARGET_READ,
	OCS_HW_IO_TARGET_WRITE,
	OCS_HW_IO_TARGET_RSP,
	OCS_HW_IO_INITIATOR_READ,
	OCS_HW_IO_INITIATOR_WRITE,
	OCS_HW_IO_INITIATOR_NODATA,
	OCS_HW_IO_DNRX_REQUEUE,
	OCS_HW_IO_MAX,
} ocs_hw_io_type_e;

typedef enum {
	OCS_HW_IO_STATE_FREE,
	OCS_HW_IO_STATE_INUSE,
	OCS_HW_IO_STATE_WAIT_FREE,
	OCS_HW_IO_STATE_WAIT_SEC_HIO,
} ocs_hw_io_state_e;

/* Descriptive strings for the HW IO request types (note: these must always
 * match up with the ocs_hw_io_type_e declaration) */
#define OCS_HW_IO_TYPE_STRINGS \
	"ELS request", \
	"ELS response", \
	"ELS response(set SID)", \
	"FC CT request", \
	"BLS accept", \
	"BLS accept(set SID)", \
	"BLS reject", \
	"target read", \
	"target write", \
	"target response", \
	"initiator read", \
	"initiator write", \
	"initiator nodata",

/**
 * @brief HW command context.
 *
 * Stores the state for the asynchronous commands sent to the hardware.
 */
typedef struct ocs_command_ctx_s {
	ocs_list_t	link;
	/**< Callback function */
	int32_t		(*cb)(struct ocs_hw_s *, int32_t, uint8_t *, void *);
	void		*arg;	/**< Argument for callback */
	uint8_t		*buf;	/**< buffer holding command / results */
	void		*ctx;	/**< upper layer context */
} ocs_command_ctx_t;

typedef struct ocs_hw_sgl_s {
	uintptr_t	addr;
	size_t		len;
} ocs_hw_sgl_t;

/**
 * @brief HW callback type
 *
 * Typedef for HW "done" callback.
 */
typedef int32_t	(*ocs_hw_done_t)(struct ocs_hw_io_s *, ocs_remote_node_t *, uint32_t len, int32_t status, uint32_t ext, void *ul_arg);


typedef union ocs_hw_io_param_u {
	struct {
		uint16_t ox_id;
		uint16_t rx_id;
		uint8_t  payload[12];	/**< big enough for ABTS BA_ACC */
	} bls;
	struct {
		uint32_t s_id;
		uint16_t ox_id;
		uint16_t rx_id;
		uint8_t  payload[12];	/**< big enough for ABTS BA_ACC */
	} bls_sid;
	struct {
		uint8_t	r_ctl;
		uint8_t	type;
		uint8_t	df_ctl;
		uint8_t timeout;
	} bcast;
	struct {
		uint16_t ox_id;
		uint8_t timeout;
	} els;
	struct {
		uint32_t s_id;
		uint16_t ox_id;
		uint8_t timeout;
	} els_sid;
	struct {
		uint8_t	r_ctl;
		uint8_t	type;
		uint8_t	df_ctl;
		uint8_t timeout;
	} fc_ct;
	struct {
		uint8_t	r_ctl;
		uint8_t	type;
		uint8_t	df_ctl;
		uint8_t timeout;
		uint16_t ox_id;
	} fc_ct_rsp;
	struct {
		uint32_t offset;
		uint16_t ox_id;
		uint16_t flags;
		uint8_t	cs_ctl;
		ocs_hw_dif_oper_e dif_oper;
		ocs_hw_dif_blk_size_e blk_size;
		uint8_t	timeout;
		uint32_t app_id;
	} fcp_tgt;
	struct {
		ocs_dma_t	*cmnd;
		ocs_dma_t	*rsp;
		ocs_hw_dif_oper_e dif_oper;
		ocs_hw_dif_blk_size_e blk_size;
		uint32_t	cmnd_size;
		uint16_t	flags;
		uint8_t		timeout;
		uint32_t	first_burst;
	} fcp_ini;
} ocs_hw_io_param_t;

/**
 * @brief WQ steering mode
 */
typedef enum {
	OCS_HW_WQ_STEERING_CLASS,
	OCS_HW_WQ_STEERING_REQUEST,
	OCS_HW_WQ_STEERING_CPU,
} ocs_hw_wq_steering_e;

/**
 * @brief HW wqe object
 */
typedef struct {
	uint32_t	abort_wqe_submit_needed:1,	/**< set if abort wqe needs to be submitted */
			send_abts:1,			/**< set to 1 to have hardware to automatically send ABTS */
			auto_xfer_rdy_dnrx:1,		/**< TRUE if DNRX was set on this IO */
			:29;
	uint32_t	id;
	uint32_t	abort_reqtag;
	ocs_list_link_t link;
	uint8_t 	*wqebuf;			/**< work queue entry buffer */
} ocs_hw_wqe_t;

/**
 * @brief HW IO object.
 *
 * Stores the per-IO information necessary for both the lower (SLI) and upper
 * layers (ocs).
 */
struct ocs_hw_io_s {
	/* Owned by HW */
	ocs_list_link_t	link;		/**< used for busy, wait_free, free lists */
	ocs_list_link_t	wqe_link;	/**< used for timed_wqe list */
	ocs_list_link_t	dnrx_link;	/**< used for io posted dnrx list */
	ocs_hw_io_state_e state;	/**< state of IO: free, busy, wait_free */
	ocs_hw_wqe_t	wqe;		/**< Work queue object, with link for pending */
	ocs_lock_t	axr_lock;	/**< Lock to synchronize TRSP and AXT Data/Cmd Cqes */
	ocs_hw_t	*hw;		/**< pointer back to hardware context */
	ocs_remote_node_t	*rnode;
	struct ocs_hw_auto_xfer_rdy_buffer_s *axr_buf;
	ocs_dma_t	xfer_rdy;
	uint16_t	type;
	uint32_t	port_owned_abort_count; /**< IO abort count */
	hw_wq_t	*wq;		/**< WQ assigned to the exchange */
	uint32_t	xbusy;		/**< Exchange is active in FW */
	ocs_hw_done_t  done;		/**< Function called on IO completion */
	void		*arg;		/**< argument passed to "IO done" callback */
	ocs_hw_done_t  abort_done;	/**< Function called on abort completion */
	void		*abort_arg;	/**< argument passed to "abort done" callback */
	ocs_ref_t	ref;		/**< refcount object */
	size_t		length;		/**< needed for bug O127585: length of IO */
	uint8_t		tgt_wqe_timeout; /**< timeout value for target WQEs */
	uint64_t	submit_ticks;	/**< timestamp when current WQE was submitted */

	uint32_t	status_saved:1, /**< if TRUE, latched status should be returned */
			abort_in_progress:1, /**< if TRUE, abort is in progress */
			quarantine:1,	/**< set if IO to be quarantined */
			quarantine_first_phase:1,	/**< set if first phase of IO */
			is_port_owned:1,	/**< set if POST_XRI was used to send XRI to th chip */
			auto_xfer_rdy_dnrx:1,	/**< TRUE if DNRX was set on this IO */
			:26;
	uint32_t	saved_status;	/**< latched status */
	uint32_t	saved_len;	/**< latched length */
	uint32_t	saved_ext;	/**< latched extended status */

	hw_eq_t	*eq;		/**< EQ that this HIO came up on */
	ocs_hw_wq_steering_e	wq_steering;	/**< WQ steering mode request */
	uint8_t		wq_class;	/**< WQ class if steering mode is Class */

	/* Owned by SLI layer */
	uint16_t	reqtag;		/**< request tag for this HW IO */
	uint32_t	abort_reqtag;	/**< request tag for an abort of this HW IO (note: this is a 32 bit value
					     to allow us to use UINT32_MAX as an uninitialized value) */
	uint32_t	indicator;	/**< XRI */
	ocs_dma_t	def_sgl;	/**< default scatter gather list */
	uint32_t	def_sgl_count;	/**< count of SGEs in default SGL */
	ocs_dma_t	*sgl;		/**< pointer to current active SGL */
	uint32_t	sgl_count;	/**< count of SGEs in io->sgl */
	uint32_t	first_data_sge;	/**< index of first data SGE */
	ocs_dma_t	*ovfl_sgl;	/**< overflow SGL */
	uint32_t	ovfl_sgl_count;	/**< count of SGEs in default SGL */
	sli4_lsp_sge_t	*ovfl_lsp;	/**< pointer to overflow segment length */
	ocs_hw_io_t	*ovfl_io;	/**< Used for SGL chaining on skyhawk */
	uint32_t	n_sge;		/**< number of active SGEs */
	uint32_t	sge_offset;

	/* BZ 161832 Workaround: */
	struct ocs_hw_io_s	*sec_hio; /**< Secondary HW IO context */
	ocs_hw_io_param_t sec_iparam;	/**< Secondary HW IO context saved iparam */
	uint32_t	sec_len;	/**< Secondary HW IO context saved len */

	/* Owned by upper layer */
	void		*ul_io;		/**< where upper layer can store reference to its IO */
};

typedef enum {
	OCS_HW_PORT_INIT,
	OCS_HW_PORT_SHUTDOWN,
	OCS_HW_PORT_SET_LINK_CONFIG,
} ocs_hw_port_e;

/**
 * @brief Fabric/Domain events
 */
typedef enum {
	OCS_HW_DOMAIN_ALLOC_OK,	/**< domain successfully allocated */
	OCS_HW_DOMAIN_ALLOC_FAIL,	/**< domain allocation failed */
	OCS_HW_DOMAIN_ATTACH_OK,	/**< successfully attached to domain */
	OCS_HW_DOMAIN_ATTACH_FAIL,	/**< domain attach failed */
	OCS_HW_DOMAIN_FREE_OK,		/**< successfully freed domain */
	OCS_HW_DOMAIN_FREE_FAIL,	/**< domain free failed */
	OCS_HW_DOMAIN_LOST,		/**< previously discovered domain no longer available */
	OCS_HW_DOMAIN_FOUND,		/**< new domain discovered */
	OCS_HW_DOMAIN_CHANGED,		/**< previously discovered domain properties have changed */
} ocs_hw_domain_event_e;

typedef enum {
	OCS_HW_PORT_ALLOC_OK,		/**< port successfully allocated */
	OCS_HW_PORT_ALLOC_FAIL,	/**< port allocation failed */
	OCS_HW_PORT_ATTACH_OK,		/**< successfully attached to port */
	OCS_HW_PORT_ATTACH_FAIL,	/**< port attach failed */
	OCS_HW_PORT_FREE_OK,		/**< successfully freed port */
	OCS_HW_PORT_FREE_FAIL,		/**< port free failed */
} ocs_hw_port_event_e;

typedef enum {
	OCS_HW_NODE_ATTACH_OK,
	OCS_HW_NODE_ATTACH_FAIL,
	OCS_HW_NODE_FREE_OK,
	OCS_HW_NODE_FREE_FAIL,
	OCS_HW_NODE_FREE_ALL_OK,
	OCS_HW_NODE_FREE_ALL_FAIL,
} ocs_hw_remote_node_event_e;

typedef enum {
	OCS_HW_CB_DOMAIN,
	OCS_HW_CB_PORT,
	OCS_HW_CB_REMOTE_NODE,
	OCS_HW_CB_UNSOLICITED,
	OCS_HW_CB_BOUNCE,
	OCS_HW_CB_MAX,			/**< must be last */
} ocs_hw_callback_e;

/**
 * @brief HW unsolicited callback status
 */
typedef enum {
	OCS_HW_UNSOL_SUCCESS,
	OCS_HW_UNSOL_ERROR,
	OCS_HW_UNSOL_ABTS_RCVD,
	OCS_HW_UNSOL_MAX,		/**< must be last */
} ocs_hw_unsol_status_e;

/**
 * @brief Node group rpi reference
 */
typedef struct {
	ocs_atomic_t rpi_count;
	ocs_atomic_t rpi_attached;
} ocs_hw_rpi_ref_t;

/**
 * @brief HW link stat types
 */
typedef enum {
	OCS_HW_LINK_STAT_LINK_FAILURE_COUNT,
	OCS_HW_LINK_STAT_LOSS_OF_SYNC_COUNT,
	OCS_HW_LINK_STAT_LOSS_OF_SIGNAL_COUNT,
	OCS_HW_LINK_STAT_PRIMITIVE_SEQ_COUNT,
	OCS_HW_LINK_STAT_INVALID_XMIT_WORD_COUNT,
	OCS_HW_LINK_STAT_CRC_COUNT,
	OCS_HW_LINK_STAT_PRIMITIVE_SEQ_TIMEOUT_COUNT,
	OCS_HW_LINK_STAT_ELASTIC_BUFFER_OVERRUN_COUNT,
	OCS_HW_LINK_STAT_ARB_TIMEOUT_COUNT,
	OCS_HW_LINK_STAT_ADVERTISED_RCV_B2B_CREDIT,
	OCS_HW_LINK_STAT_CURR_RCV_B2B_CREDIT,
	OCS_HW_LINK_STAT_ADVERTISED_XMIT_B2B_CREDIT,
	OCS_HW_LINK_STAT_CURR_XMIT_B2B_CREDIT,
	OCS_HW_LINK_STAT_RCV_EOFA_COUNT,
	OCS_HW_LINK_STAT_RCV_EOFDTI_COUNT,
	OCS_HW_LINK_STAT_RCV_EOFNI_COUNT,
	OCS_HW_LINK_STAT_RCV_SOFF_COUNT,
	OCS_HW_LINK_STAT_RCV_DROPPED_NO_AER_COUNT,
	OCS_HW_LINK_STAT_RCV_DROPPED_NO_RPI_COUNT,
	OCS_HW_LINK_STAT_RCV_DROPPED_NO_XRI_COUNT,
	OCS_HW_LINK_STAT_MAX,		/**< must be last */
} ocs_hw_link_stat_e;

typedef enum {
	OCS_HW_HOST_STAT_TX_KBYTE_COUNT,
	OCS_HW_HOST_STAT_RX_KBYTE_COUNT,
	OCS_HW_HOST_STAT_TX_FRAME_COUNT,
	OCS_HW_HOST_STAT_RX_FRAME_COUNT,
	OCS_HW_HOST_STAT_TX_SEQ_COUNT,
	OCS_HW_HOST_STAT_RX_SEQ_COUNT,
	OCS_HW_HOST_STAT_TOTAL_EXCH_ORIG,
	OCS_HW_HOST_STAT_TOTAL_EXCH_RESP,
	OCS_HW_HOSY_STAT_RX_P_BSY_COUNT,
	OCS_HW_HOST_STAT_RX_F_BSY_COUNT,
	OCS_HW_HOST_STAT_DROP_FRM_DUE_TO_NO_RQ_BUF_COUNT,
	OCS_HW_HOST_STAT_EMPTY_RQ_TIMEOUT_COUNT,
	OCS_HW_HOST_STAT_DROP_FRM_DUE_TO_NO_XRI_COUNT,
	OCS_HW_HOST_STAT_EMPTY_XRI_POOL_COUNT,
	OCS_HW_HOST_STAT_MAX /* MUST BE LAST */
} ocs_hw_host_stat_e;

typedef enum {
	OCS_HW_STATE_UNINITIALIZED,		/* power-on, no allocations, no initializations */
	OCS_HW_STATE_QUEUES_ALLOCATED,		/* chip is reset, allocations are complete (queues not registered) */
	OCS_HW_STATE_ACTIVE,			/* chip is up an running */
	OCS_HW_STATE_RESET_IN_PROGRESS,	/* chip is being reset */
	OCS_HW_STATE_TEARDOWN_IN_PROGRESS,	/* teardown has been started */
} ocs_hw_state_e;

/**
 * @brief Defines a general FC sequence object, consisting of a header, payload buffers
 *	  and a HW IO in the case of port owned XRI
 */
typedef struct {
	ocs_hw_t *hw;			/**< HW that owns this sequence */
	/* sequence information */
	uint8_t fcfi;		/**< FCFI associated with sequence */
	uint8_t auto_xrdy;	/**< If auto XFER_RDY was generated */
	uint8_t out_of_xris;	/**< If IO would have been assisted if XRIs were available */
	ocs_hw_rq_buffer_t *header;
	ocs_hw_rq_buffer_t *payload;	/**< received frame payload buffer */

	/* other "state" information from the SRB (sequence coalescing) */
	ocs_hw_unsol_status_e status;
	uint32_t xri;		/**< XRI associated with sequence; sequence coalescing only */
	ocs_hw_io_t *hio;	/**< HW IO */

	ocs_list_link_t link;
	void *hw_priv;		/**< HW private context */
} ocs_hw_sequence_t;

/**
 * @brief Structure to track optimized write buffers posted to chip owned XRIs.
 *
 * Note: The rqindex will be set the following "fake" indexes. This will be used
 *       when the buffer is returned via ocs_seq_free() to make the buffer available
 *       for re-use on another XRI.
 *
 *       The dma->alloc pointer on the dummy header will be used to get back to this structure when the buffer is freed.
 *
 *       More of these object may be allocated on the fly if more XRIs are pushed to the chip.
 */
#define OCS_HW_RQ_INDEX_DUMMY_HDR	0xFF00
#define OCS_HW_RQ_INDEX_DUMMY_DATA	0xFF01
typedef struct ocs_hw_auto_xfer_rdy_buffer_s {
	fc_header_t hdr;		/**< used to build a dummy data header for unsolicited processing */
	ocs_hw_rq_buffer_t header;	/**< Points to the dummy data header */
	ocs_hw_rq_buffer_t payload;	/**< received frame payload buffer */
	ocs_hw_sequence_t seq;         /**< sequence for passing the buffers */
	uint8_t data_cqe;
	uint8_t cmd_cqe;

	/* fields saved from the command header that are needed when the data arrives */
	uint8_t fcfi;

	/* To handle outof order completions save AXR cmd and data cqes */
	uint8_t call_axr_cmd;
	uint8_t call_axr_data;
	ocs_hw_sequence_t *cmd_seq;
} ocs_hw_auto_xfer_rdy_buffer_t;

/**
 * @brief Node group rpi reference
 */
typedef struct {
	uint8_t overflow;
	uint32_t counter;
} ocs_hw_link_stat_counts_t;

/**
 * @brief HW object describing fc host stats
 */
typedef struct {
	uint32_t counter;
} ocs_hw_host_stat_counts_t;

#define TID_HASH_BITS	8
#define TID_HASH_LEN	(1U << TID_HASH_BITS)

typedef struct ocs_hw_iopt_s {
	char		name[32];
	uint32_t	instance_index;
	ocs_thread_t	iopt_thread;
	ocs_cbuf_t	*iopt_free_queue;	/* multiple reader, multiple writer */
	ocs_cbuf_t	*iopt_work_queue;
	ocs_array_t	*iopt_cmd_array;
} ocs_hw_iopt_t;

typedef enum {
	HW_CQ_HANDLER_LOCAL,
	HW_CQ_HANDLER_THREAD,
} hw_cq_handler_e;

#include "ocs_hw_queues.h"

/**
 * @brief Stucture used for the hash lookup of queue IDs
 */
typedef struct {
	uint32_t id:16,
		in_use:1,
		index:15;
} ocs_queue_hash_t;

/**
 * @brief Define the fields required to implement the skyhawk DIF quarantine.
 */
#define OCS_HW_QUARANTINE_QUEUE_DEPTH	4

typedef struct {
	uint32_t	quarantine_index;
	ocs_hw_io_t	*quarantine_ios[OCS_HW_QUARANTINE_QUEUE_DEPTH];
} ocs_quarantine_info_t;

/**
 * @brief Define the WQ callback object
 */
typedef struct {
	uint16_t instance_index;	/**< use for request tag */
	void (*callback)(void *arg, uint8_t *cqe, int32_t status);
	void *arg;
} hw_wq_callback_t;

typedef struct {
	uint64_t fwrev;

	/* Control Declarations here ...*/

	uint8_t retain_tsend_io_length;

	/* Use unregistered RPI */
	uint8_t use_unregistered_rpi;
	uint32_t unregistered_rid;
	uint32_t unregistered_index;

	uint8_t disable_ar_tgt_dif;	/* Disable auto response if target DIF */
	uint8_t disable_dump_loc;
	uint8_t use_dif_quarantine;
	uint8_t use_dif_sec_xri;

	uint8_t override_fcfi;

	uint8_t fw_version_too_low;

	uint8_t sglc_misreported;

	uint8_t ignore_send_frame;

} ocs_hw_workaround_t;



/**
 * @brief HW object
 */
struct ocs_hw_s {
	ocs_os_handle_t	os;
	sli4_t		sli;
	uint16_t	ulp_start;
	uint16_t	ulp_max;
	uint32_t	dump_size;
	ocs_hw_state_e state;
	uint8_t		hw_setup_called;
	uint8_t		sliport_healthcheck;
	uint16_t        watchdog_timeout;
	ocs_lock_t	watchdog_lock;

	/** HW configuration, subject to ocs_hw_set()  */
	struct {
		uint32_t	n_eq; /**< number of event queues */
		uint32_t	n_cq; /**< number of completion queues */
		uint32_t	n_mq; /**< number of mailbox queues */
		uint32_t	n_rq; /**< number of receive queues */
		uint32_t	n_wq; /**< number of work queues */
		uint32_t	n_io; /**< total number of IO objects */
		uint32_t	n_sgl;/**< length of SGL */
		uint32_t	speed;	/** requested link speed in Mbps */
		uint32_t	topology;  /** requested link topology */
		uint32_t	rq_default_buffer_size;	/** size of the buffers for first burst */
		uint32_t	auto_xfer_rdy_xri_cnt;	/** Initial XRIs to post to chip at initialization */
		uint32_t	auto_xfer_rdy_size;	/** max size IO to use with this feature */
		uint8_t		auto_xfer_rdy_blk_size_chip;	/** block size to use with this feature */
		uint8_t         esoc;
		uint16_t	dif_seed; /** The seed for the DIF CRC calculation */
		uint16_t	auto_xfer_rdy_app_tag_value;
		uint8_t		dif_mode; /**< DIF mode to use */
		uint8_t		i_only_aab; /** Enable initiator-only auto-abort */
		uint8_t		emulate_tgt_wqe_timeout; /** Enable driver target wqe timeouts */
		uint32_t	bounce:1;
		const char	*queue_topology;		/**< Queue topology string */
		uint8_t		auto_xfer_rdy_t10_enable;	/** Enable t10 PI for auto xfer ready */
		uint8_t		auto_xfer_rdy_p_type;	/** p_type for auto xfer ready */
		uint8_t		auto_xfer_rdy_ref_tag_is_lba;
		uint8_t		auto_xfer_rdy_app_tag_valid;
		uint8_t		rq_selection_policy;		/** MRQ RQ selection policy */
		uint8_t		rr_quanta;			/** RQ quanta if rq_selection_policy == 2 */
		uint32_t	filter_def[SLI4_CMD_REG_FCFI_NUM_RQ_CFG];
	} config;

	/* calculated queue sizes for each type */
	uint32_t	num_qentries[SLI_QTYPE_MAX];

	/* Storage for SLI queue objects */
	sli4_queue_t	wq[OCS_HW_MAX_NUM_WQ];
	sli4_queue_t	rq[OCS_HW_MAX_NUM_RQ];
	uint16_t	hw_rq_lookup[OCS_HW_MAX_NUM_RQ];
	sli4_queue_t	mq[OCS_HW_MAX_NUM_MQ];
	sli4_queue_t	cq[OCS_HW_MAX_NUM_CQ];
	sli4_queue_t	eq[OCS_HW_MAX_NUM_EQ];

	/* HW queue */
	uint32_t	eq_count;
	uint32_t	cq_count;
	uint32_t	mq_count;
	uint32_t	wq_count;
	uint32_t	rq_count;			/**< count of SLI RQs */
	ocs_list_t	eq_list;

	ocs_queue_hash_t cq_hash[OCS_HW_Q_HASH_SIZE];
	ocs_queue_hash_t rq_hash[OCS_HW_Q_HASH_SIZE];
	ocs_queue_hash_t wq_hash[OCS_HW_Q_HASH_SIZE];

	/* Storage for HW queue objects */
	hw_wq_t	*hw_wq[OCS_HW_MAX_NUM_WQ];
	hw_rq_t	*hw_rq[OCS_HW_MAX_NUM_RQ];
	hw_mq_t	*hw_mq[OCS_HW_MAX_NUM_MQ];
	hw_cq_t	*hw_cq[OCS_HW_MAX_NUM_CQ];
	hw_eq_t	*hw_eq[OCS_HW_MAX_NUM_EQ];
	uint32_t	hw_rq_count;			/**< count of hw_rq[] entries */
	uint32_t	hw_mrq_count;			/**< count of multirq RQs */

	ocs_varray_t	*wq_class_array[OCS_HW_MAX_WQ_CLASS];	/**< pool per class WQs */
	ocs_varray_t	*wq_cpu_array[OCS_HW_MAX_WQ_CPU];	/**< pool per CPU WQs */

	/* Sequence objects used in incoming frame processing */
	ocs_array_t	*seq_pool;

	/* Auto XFER RDY Buffers - protect with io_lock */
	uint32_t	auto_xfer_rdy_enabled:1,	/**< TRUE if auto xfer rdy is enabled */
			:31;
	ocs_pool_t	*auto_xfer_rdy_buf_pool;	/**< pool of ocs_hw_auto_xfer_rdy_buffer_t objects */

	/** Maintain an ordered, linked list of outstanding HW commands. */
	ocs_lock_t	cmd_lock;
	ocs_list_t	cmd_head;
	ocs_list_t	cmd_pending;
	uint32_t	cmd_head_count;


	sli4_link_event_t link;
	ocs_hw_linkcfg_e linkcfg; /**< link configuration setting */
	uint32_t eth_license;	   /**< Ethernet license; to enable FCoE on Lancer */

	struct {
		/**
		 * Function + argument used to notify upper layer of domain events.
		 *
		 * The final argument to the callback is a generic data pointer:
		 *  - ocs_domain_record_t on OCS_HW_DOMAIN_FOUND
		 *  - ocs_domain_t on OCS_HW_DOMAIN_ALLOC_FAIL, OCS_HW_DOMAIN_ALLOC_OK,
		 * OCS_HW_DOMAIN_FREE_FAIL, OCS_HW_DOMAIN_FREE_OK,
		 * OCS_HW_DOMAIN_ATTACH_FAIL, OCS_HW_DOMAIN_ATTACH_OK, and
		 * OCS_HW_DOMAIN_LOST.
		 */
		int32_t	(*domain)(void *, ocs_hw_domain_event_e, void *);
		/**
		 * Function + argument used to notify upper layers of port events.
		 *
		 * The final argument to the callback is a pointer to the effected
		 * SLI port for all events.
		 */
		int32_t (*port)(void *, ocs_hw_port_event_e, void *);
		/** Function + argument used to announce arrival of unsolicited frames */
		int32_t (*unsolicited)(void *, ocs_hw_sequence_t *);
		int32_t (*rnode)(void *, ocs_hw_remote_node_event_e, void *);
		int32_t (*bounce)(void (*)(void *arg), void *arg, uint32_t s_id, uint32_t d_id, uint32_t ox_id);
	} callback;
	struct {
		void *domain;
		void *port;
		void *unsolicited;
		void *rnode;
		void *bounce;
	} args;

	/* OCS domain objects index by FCFI */
	int32_t		first_domain_idx;		/* Workaround for srb->fcfi == 0 */
	ocs_domain_t	*domains[SLI4_MAX_FCFI];

	/* Table of FCFI values index by FCF_index */
	uint16_t	fcf_index_fcfi[SLI4_MAX_FCF_INDEX];

	uint16_t	fcf_indicator;

	ocs_hw_io_t	**io;		/**< pointer array of IO objects */
	uint8_t		*wqe_buffs;	/**< array of WQE buffs mapped to IO objects */	

	ocs_lock_t	io_lock;		/**< IO lock to synchronize list access */
	ocs_lock_t	io_abort_lock;		/**< IO lock to synchronize IO aborting */
	ocs_list_t	io_inuse;		/**< List of IO objects in use */
	ocs_list_t	io_timed_wqe;		/**< List of IO objects with a timed target WQE */
	ocs_list_t	io_wait_free;		/**< List of IO objects waiting to be freed */
	ocs_list_t	io_free;		/**< List of IO objects available for allocation */
	ocs_list_t	io_port_owned;		/**< List of IO objects posted for chip use */
	ocs_list_t	io_port_dnrx;		/**< List of IO objects needing auto xfer rdy buffers */

	ocs_dma_t	loop_map;

	ocs_dma_t	xfer_rdy;

	ocs_dma_t	dump_sges;

	ocs_dma_t	rnode_mem;

	ocs_dma_t	domain_dmem; 	/*domain dma mem for service params */
	ocs_dma_t	fcf_dmem; 	/*dma men for fcf */

	ocs_hw_rpi_ref_t *rpi_ref;

	char		*hw_war_version;
	ocs_hw_workaround_t workaround;

	ocs_atomic_t io_alloc_failed_count;

#if defined(OCS_DEBUG_QUEUE_HISTORY)
	ocs_hw_q_hist_t q_hist;
#endif

	ocs_list_t	sec_hio_wait_list;	/**< BZ 161832 Workaround: Secondary HW IO context wait list */
	uint32_t	sec_hio_wait_count;	/**< BZ 161832 Workaround: Count of IOs that were put on the
						 * Secondary HW IO wait list
						 */

#define HW_MAX_TCMD_THREADS		16
	ocs_hw_qtop_t	*qtop;					/**< pointer to queue topology */

	uint32_t	tcmd_wq_submit[OCS_HW_MAX_NUM_WQ];	/**< stat: wq sumbit count */
	uint32_t	tcmd_wq_complete[OCS_HW_MAX_NUM_WQ];	/**< stat: wq complete count */

	ocs_timer_t	wqe_timer;		/**< Timer to periodically check for WQE timeouts */
	ocs_timer_t	watchdog_timer;		/**< Timer for heartbeat */
	bool            expiration_logged;
	uint32_t	in_active_wqe_timer:1,	/**< TRUE if currently in active wqe timer handler */
			active_wqe_timer_shutdown:1, /** TRUE if wqe timer is to be shutdown */
			:30;

	ocs_list_t	iopc_list;		/**< list of IO processing contexts */
	ocs_lock_t	iopc_list_lock;		/**< lock for iopc_list */

	ocs_pool_t	*wq_reqtag_pool;	/**< pool of hw_wq_callback_t objects */

	ocs_atomic_t	send_frame_seq_id;	/**< send frame sequence ID */
};


typedef enum {
	OCS_HW_IO_INUSE_COUNT,
	OCS_HW_IO_FREE_COUNT,
	OCS_HW_IO_WAIT_FREE_COUNT,
	OCS_HW_IO_PORT_OWNED_COUNT,
	OCS_HW_IO_N_TOTAL_IO_COUNT,
} ocs_hw_io_count_type_e;

typedef void (*tcmd_cq_handler)(ocs_hw_t *hw, uint32_t cq_idx, void *cq_handler_arg);

/*
 * HW queue data structures
 */

struct hw_eq_s {
	ocs_list_link_t link;		/**< must be first */
	sli4_qtype_e type;		/**< must be second */
	uint32_t instance;
	uint32_t entry_count;
	uint32_t entry_size;
	ocs_hw_t *hw;
	sli4_queue_t *queue;
	ocs_list_t cq_list;
#if OCS_STAT_ENABLE
	uint32_t use_count;
#endif
	ocs_varray_t *wq_array;		/*<< array of WQs */
};

struct hw_cq_s {
	ocs_list_link_t link;		/*<< must be first */
	sli4_qtype_e type;		/**< must be second */
	uint32_t instance;		/*<< CQ instance (cq_idx) */
	uint32_t entry_count;		/*<< Number of entries */
	uint32_t entry_size;		/*<< entry size */
	hw_eq_t *eq;			/*<< parent EQ */
	sli4_queue_t *queue;		/**< pointer to SLI4 queue */
	ocs_list_t q_list;		/**< list of children queues */

#if OCS_STAT_ENABLE
	uint32_t use_count;
#endif
};

typedef struct {
	ocs_list_link_t link;		/*<< must be first */
	sli4_qtype_e type;		/*<< must be second */
} hw_q_t;

struct hw_mq_s {
	ocs_list_link_t link;		/*<< must be first */
	sli4_qtype_e type;		/*<< must be second */
	uint32_t instance;

	uint32_t entry_count;
	uint32_t entry_size;
	hw_cq_t *cq;
	sli4_queue_t *queue;

#if OCS_STAT_ENABLE
	uint32_t use_count;
#endif
};

struct hw_wq_s {
	ocs_list_link_t link;		/*<< must be first */
	sli4_qtype_e type;		/*<< must be second */
	uint32_t instance;
	ocs_hw_t *hw;

	uint32_t entry_count;
	uint32_t entry_size;
	hw_cq_t *cq;
	sli4_queue_t *queue;
	uint32_t class;
	uint8_t ulp;

	/* WQ consumed */
	uint32_t wqec_set_count;		/*<< how often IOs are submitted with wqce set */
	uint32_t wqec_count;			/*<< current wqce counter */
	uint32_t free_count;			/*<< free count */
	uint32_t total_submit_count;		/*<< total submit count */
	ocs_list_t pending_list;		/*<< list of IOs pending for this WQ */

	/*
	 * ---Skyhawk only ---
	 * BZ 160124 - Driver must quarantine XRIs for target writes and
	 * initiator read when using DIF separates. Throw them on a
	 * queue until another 4 similar requests are completed to ensure they
	 * are flushed from the internal chip cache before being re-used.
	 * The must be a separate queue per CQ because the actual chip completion
	 * order cannot be determined. Since each WQ has a separate CQ, use the wq
	 * associated with the IO.
	 *
	 * Note: Protected by queue->lock
	 */
	ocs_quarantine_info_t quarantine_info;

	/*
	 * HW IO allocated for use with Send Frame
	 */
	ocs_hw_io_t *send_frame_io;

	/* Stats */
#if OCS_STAT_ENABLE
	uint32_t use_count;			/*<< use count */
	uint32_t wq_pending_count;		/*<< count of HW IOs that were queued on the WQ pending list */
#endif
};

struct hw_rq_s {
	ocs_list_link_t link;			/*<< must be first */
	sli4_qtype_e type;			/*<< must be second */
	uint32_t instance;

	uint32_t entry_count;
	uint32_t hdr_entry_size;
	uint32_t first_burst_entry_size;
	uint32_t data_entry_size;
	uint8_t ulp;
	bool is_mrq;
	uint32_t base_mrq_id;

	hw_cq_t *cq;

	uint8_t filter_mask;			/* Filter mask value */
	sli4_queue_t *hdr;
	sli4_queue_t *first_burst;
	sli4_queue_t *data;

	ocs_hw_rq_buffer_t *hdr_buf;
	ocs_hw_rq_buffer_t *fb_buf;
	ocs_hw_rq_buffer_t *payload_buf;

	ocs_hw_sequence_t **rq_tracker;	/* RQ tracker for this RQ */
#if OCS_STAT_ENABLE
	uint32_t use_count;
	uint32_t hdr_use_count;
	uint32_t fb_use_count;
	uint32_t payload_use_count;
#endif
};

typedef struct ocs_hw_global_s {
	const char	*queue_topology_string;			/**< queue topology string */
} ocs_hw_global_t;
extern ocs_hw_global_t hw_global;

extern hw_eq_t *hw_new_eq(ocs_hw_t *hw, uint32_t entry_count);
extern hw_cq_t *hw_new_cq(hw_eq_t *eq, uint32_t entry_count);
extern uint32_t hw_new_cq_set(hw_eq_t *eqs[], hw_cq_t *cqs[], uint32_t num_cqs, uint32_t entry_count);
extern hw_mq_t *hw_new_mq(hw_cq_t *cq, uint32_t entry_count);
extern hw_wq_t *hw_new_wq(hw_cq_t *cq, uint32_t entry_count, uint32_t class, uint32_t ulp);
extern hw_rq_t *hw_new_rq(hw_cq_t *cq, uint32_t entry_count, uint32_t ulp);
extern uint32_t hw_new_rq_set(hw_cq_t *cqs[], hw_rq_t *rqs[], uint32_t num_rq_pairs, uint32_t entry_count, uint32_t ulp);
extern void hw_del_eq(hw_eq_t *eq);
extern void hw_del_cq(hw_cq_t *cq);
extern void hw_del_mq(hw_mq_t *mq);
extern void hw_del_wq(hw_wq_t *wq);
extern void hw_del_rq(hw_rq_t *rq);
extern void hw_queue_dump(ocs_hw_t *hw);
extern void hw_queue_teardown(ocs_hw_t *hw);
extern int32_t hw_route_rqe(ocs_hw_t *hw, ocs_hw_sequence_t *seq);
extern int32_t ocs_hw_queue_hash_find(ocs_queue_hash_t *, uint16_t);
extern ocs_hw_rtn_e ocs_hw_setup(ocs_hw_t *, ocs_os_handle_t, sli4_port_type_e);
extern ocs_hw_rtn_e ocs_hw_init(ocs_hw_t *);
extern ocs_hw_rtn_e ocs_hw_teardown(ocs_hw_t *);
extern ocs_hw_rtn_e ocs_hw_reset(ocs_hw_t *, ocs_hw_reset_e);
extern int32_t ocs_hw_get_num_eq(ocs_hw_t *);
extern ocs_hw_rtn_e ocs_hw_get(ocs_hw_t *, ocs_hw_property_e, uint32_t *);
extern void *ocs_hw_get_ptr(ocs_hw_t *, ocs_hw_property_e);
extern ocs_hw_rtn_e ocs_hw_set(ocs_hw_t *, ocs_hw_property_e, uint32_t);
extern ocs_hw_rtn_e ocs_hw_set_ptr(ocs_hw_t *, ocs_hw_property_e, void*);
extern int32_t ocs_hw_event_check(ocs_hw_t *, uint32_t);
extern int32_t ocs_hw_process(ocs_hw_t *, uint32_t, uint32_t);
extern ocs_hw_rtn_e ocs_hw_command(ocs_hw_t *, uint8_t *, uint32_t, void *, void *);
extern ocs_hw_rtn_e ocs_hw_callback(ocs_hw_t *, ocs_hw_callback_e, void *, void *);
extern ocs_hw_rtn_e ocs_hw_port_alloc(ocs_hw_t *, ocs_sli_port_t *, ocs_domain_t *, uint8_t *);
extern ocs_hw_rtn_e ocs_hw_port_attach(ocs_hw_t *, ocs_sli_port_t *, uint32_t);
typedef void (*ocs_hw_port_control_cb_t)(int32_t status, uintptr_t value, void *arg);
extern ocs_hw_rtn_e ocs_hw_port_control(ocs_hw_t *, ocs_hw_port_e, uintptr_t, ocs_hw_port_control_cb_t, void *);
extern ocs_hw_rtn_e ocs_hw_port_free(ocs_hw_t *, ocs_sli_port_t *);
extern ocs_hw_rtn_e ocs_hw_domain_alloc(ocs_hw_t *, ocs_domain_t *, uint32_t, uint32_t);
extern ocs_hw_rtn_e ocs_hw_domain_attach(ocs_hw_t *, ocs_domain_t *, uint32_t);
extern ocs_hw_rtn_e ocs_hw_domain_free(ocs_hw_t *, ocs_domain_t *);
extern ocs_hw_rtn_e ocs_hw_domain_force_free(ocs_hw_t *, ocs_domain_t *);
extern ocs_domain_t * ocs_hw_domain_get(ocs_hw_t *, uint16_t);
extern ocs_hw_rtn_e ocs_hw_node_alloc(ocs_hw_t *, ocs_remote_node_t *, uint32_t, ocs_sli_port_t *);
extern ocs_hw_rtn_e ocs_hw_node_free_all(ocs_hw_t *);
extern ocs_hw_rtn_e ocs_hw_node_attach(ocs_hw_t *, ocs_remote_node_t *, ocs_dma_t *);
extern ocs_hw_rtn_e ocs_hw_node_detach(ocs_hw_t *, ocs_remote_node_t *);
extern ocs_hw_rtn_e ocs_hw_node_free_resources(ocs_hw_t *, ocs_remote_node_t *);
extern ocs_hw_rtn_e ocs_hw_node_group_alloc(ocs_hw_t *, ocs_remote_node_group_t *);
extern ocs_hw_rtn_e ocs_hw_node_group_attach(ocs_hw_t *, ocs_remote_node_group_t *, ocs_remote_node_t *);
extern ocs_hw_rtn_e ocs_hw_node_group_free(ocs_hw_t *, ocs_remote_node_group_t *);
extern ocs_hw_io_t *ocs_hw_io_alloc(ocs_hw_t *);
extern ocs_hw_io_t *ocs_hw_io_activate_port_owned(ocs_hw_t *, ocs_hw_io_t *);
extern int32_t ocs_hw_io_free(ocs_hw_t *, ocs_hw_io_t *);
extern uint8_t ocs_hw_io_inuse(ocs_hw_t *hw, ocs_hw_io_t *io);
typedef int32_t (*ocs_hw_srrs_cb_t)(ocs_hw_io_t *io, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *arg);
extern ocs_hw_rtn_e ocs_hw_srrs_send(ocs_hw_t *, ocs_hw_io_type_e, ocs_hw_io_t *, ocs_dma_t *, uint32_t, ocs_dma_t *, ocs_remote_node_t *, ocs_hw_io_param_t *, ocs_hw_srrs_cb_t, void *);
extern ocs_hw_rtn_e ocs_hw_io_send(ocs_hw_t *, ocs_hw_io_type_e, ocs_hw_io_t *, uint32_t, ocs_hw_io_param_t *, ocs_remote_node_t *, void *, void *);
extern ocs_hw_rtn_e _ocs_hw_io_send(ocs_hw_t *hw, ocs_hw_io_type_e type, ocs_hw_io_t *io,
				      uint32_t len, ocs_hw_io_param_t *iparam, ocs_remote_node_t *rnode,
				      void *cb, void *arg);
extern ocs_hw_rtn_e ocs_hw_io_register_sgl(ocs_hw_t *, ocs_hw_io_t *, ocs_dma_t *, uint32_t);
extern ocs_hw_rtn_e ocs_hw_io_init_sges(ocs_hw_t *hw, ocs_hw_io_t *io, ocs_hw_io_type_e type);
extern ocs_hw_rtn_e ocs_hw_io_add_seed_sge(ocs_hw_t *hw, ocs_hw_io_t *io, ocs_hw_dif_info_t *dif_info);
extern ocs_hw_rtn_e ocs_hw_io_add_sge(ocs_hw_t *, ocs_hw_io_t *, uintptr_t, uint32_t);
extern ocs_hw_rtn_e ocs_hw_io_add_dif_sge(ocs_hw_t *hw, ocs_hw_io_t *io, uintptr_t addr);
extern ocs_hw_rtn_e ocs_hw_io_abort(ocs_hw_t *, ocs_hw_io_t *, uint32_t, void *, void *);
extern int32_t ocs_hw_io_get_xid(ocs_hw_t *, ocs_hw_io_t *);
extern uint32_t ocs_hw_io_get_count(ocs_hw_t *, ocs_hw_io_count_type_e);
extern uint32_t ocs_hw_get_rqes_produced_count(ocs_hw_t *hw);

typedef void (*ocs_hw_fw_cb_t)(int32_t status, uint32_t bytes_written, uint32_t change_status, void *arg);
extern ocs_hw_rtn_e ocs_hw_firmware_write(ocs_hw_t *, ocs_dma_t *, uint32_t, uint32_t, int, ocs_hw_fw_cb_t, void*);

/* Function for retrieving SFP data */
typedef void (*ocs_hw_sfp_cb_t)(void *, int32_t, uint32_t, uint32_t *, void *);
extern ocs_hw_rtn_e ocs_hw_get_sfp(ocs_hw_t *, uint16_t, ocs_hw_sfp_cb_t, void *);

/* Function for retrieving temperature data */
typedef void (*ocs_hw_temp_cb_t)(int32_t status,
				  uint32_t curr_temp,
				  uint32_t crit_temp_thrshld,
				  uint32_t warn_temp_thrshld,
				  uint32_t norm_temp_thrshld,
				  uint32_t fan_off_thrshld,
				  uint32_t fan_on_thrshld,
				  void *arg);
extern ocs_hw_rtn_e ocs_hw_get_temperature(ocs_hw_t *, ocs_hw_temp_cb_t, void*);

/* Function for retrieving link statistics */
typedef void (*ocs_hw_link_stat_cb_t)(int32_t status,
				       uint32_t num_counters,
				       ocs_hw_link_stat_counts_t *counters,
				       void *arg);
extern ocs_hw_rtn_e ocs_hw_get_link_stats(ocs_hw_t *,
					    uint8_t req_ext_counters,
					    uint8_t clear_overflow_flags,
					    uint8_t clear_all_counters,
					    ocs_hw_link_stat_cb_t, void*);
/* Function for retrieving host statistics */
typedef void (*ocs_hw_host_stat_cb_t)(int32_t status,
				       uint32_t num_counters,
				       ocs_hw_host_stat_counts_t *counters,
				       void *arg);
extern ocs_hw_rtn_e ocs_hw_get_host_stats(ocs_hw_t *hw, uint8_t cc, ocs_hw_host_stat_cb_t, void *arg);

extern ocs_hw_rtn_e ocs_hw_raise_ue(ocs_hw_t *, uint8_t);
typedef void (*ocs_hw_dump_get_cb_t)(int32_t status, uint32_t bytes_read, uint8_t eof, void *arg);
extern ocs_hw_rtn_e ocs_hw_dump_get(ocs_hw_t *, ocs_dma_t *, uint32_t, uint32_t, ocs_hw_dump_get_cb_t, void *);
extern ocs_hw_rtn_e ocs_hw_set_dump_location(ocs_hw_t *, uint32_t, ocs_dma_t *, uint8_t);

typedef void (*ocs_get_port_protocol_cb_t)(int32_t status, ocs_hw_port_protocol_e port_protocol, void *arg);
extern ocs_hw_rtn_e ocs_hw_get_port_protocol(ocs_hw_t *hw, uint32_t pci_func, ocs_get_port_protocol_cb_t mgmt_cb, void* ul_arg);
typedef void (*ocs_set_port_protocol_cb_t)(int32_t status,  void *arg);
extern ocs_hw_rtn_e ocs_hw_set_port_protocol(ocs_hw_t *hw, ocs_hw_port_protocol_e profile,
					       uint32_t pci_func, ocs_set_port_protocol_cb_t mgmt_cb,
					       void* ul_arg);

typedef void (*ocs_get_profile_list_cb_t)(int32_t status,  ocs_hw_profile_list_t*, void *arg);
extern ocs_hw_rtn_e ocs_hw_get_profile_list(ocs_hw_t *hw, ocs_get_profile_list_cb_t mgmt_cb, void *arg);
typedef void (*ocs_get_active_profile_cb_t)(int32_t status,  uint32_t active_profile, void *arg);
extern ocs_hw_rtn_e ocs_hw_get_active_profile(ocs_hw_t *hw, ocs_get_active_profile_cb_t mgmt_cb, void *arg);
typedef void (*ocs_set_active_profile_cb_t)(int32_t status, void *arg);
extern ocs_hw_rtn_e ocs_hw_set_active_profile(ocs_hw_t *hw, ocs_set_active_profile_cb_t mgmt_cb,
		uint32_t profile_id, void *arg);
typedef void (*ocs_get_nvparms_cb_t)(int32_t status, uint8_t *wwpn, uint8_t *wwnn, uint8_t hard_alpa,
		uint32_t preferred_d_id, void *arg);
extern ocs_hw_rtn_e ocs_hw_get_nvparms(ocs_hw_t *hw, ocs_get_nvparms_cb_t mgmt_cb, void *arg);
typedef void (*ocs_set_nvparms_cb_t)(int32_t status, void *arg);
extern ocs_hw_rtn_e ocs_hw_set_nvparms(ocs_hw_t *hw, ocs_set_nvparms_cb_t mgmt_cb, uint8_t *wwpn,
		uint8_t *wwnn, uint8_t hard_alpa, uint32_t preferred_d_id, void *arg);
extern int32_t ocs_hw_eq_process(ocs_hw_t *hw, hw_eq_t *eq, uint32_t max_isr_time_msec);
extern void ocs_hw_cq_process(ocs_hw_t *hw, hw_cq_t *cq);
extern void ocs_hw_wq_process(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe, int32_t status, uint16_t rid);
extern void ocs_hw_xabt_process(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe, uint16_t rid);
extern int32_t hw_wq_write(hw_wq_t *wq, ocs_hw_wqe_t *wqe);

typedef void (*ocs_hw_dump_clear_cb_t)(int32_t status, void *arg);
extern ocs_hw_rtn_e ocs_hw_dump_clear(ocs_hw_t *, ocs_hw_dump_clear_cb_t, void *);

extern uint8_t ocs_hw_is_io_port_owned(ocs_hw_t *hw, ocs_hw_io_t *io);


extern uint8_t ocs_hw_is_xri_port_owned(ocs_hw_t *hw, uint32_t xri);
extern ocs_hw_io_t * ocs_hw_io_lookup(ocs_hw_t *hw, uint32_t indicator);
extern uint32_t ocs_hw_xri_move_to_port_owned(ocs_hw_t *hw, uint32_t num_xri);
extern ocs_hw_rtn_e ocs_hw_xri_move_to_host_owned(ocs_hw_t *hw, uint8_t num_xri);
extern int32_t ocs_hw_reque_xri(ocs_hw_t *hw, ocs_hw_io_t *io);


typedef struct {
	/* structure elements used by HW */
	ocs_hw_t *hw;			/**> pointer to HW */
	hw_wq_callback_t *wqcb;	/**> WQ callback object, request tag */
	ocs_hw_wqe_t wqe;		/**> WQE buffer object (may be queued on WQ pending list) */
	void (*callback)(int32_t status, void *arg);	/**> final callback function */
	void *arg;			/**> final callback argument */

	/* General purpose elements */
	ocs_hw_sequence_t *seq;
	ocs_dma_t payload;		/**> a payload DMA buffer */
} ocs_hw_send_frame_context_t;


#define OCS_HW_OBJECT_G5              0xfeaa0001
#define OCS_HW_OBJECT_G6              0xfeaa0003
#define OCS_FILE_TYPE_GROUP            0xf7
#define OCS_FILE_ID_GROUP              0xa2
struct ocs_hw_grp_hdr {
	uint32_t size;          
	uint32_t magic_number;  
	uint32_t word2;         
	uint8_t rev_name[128];
        uint8_t date[12];
        uint8_t revision[32];
};                              


ocs_hw_rtn_e
ocs_hw_send_frame(ocs_hw_t *hw, fc_header_le_t *hdr, uint8_t sof, uint8_t eof, ocs_dma_t *payload,
		   ocs_hw_send_frame_context_t *ctx,
		   void (*callback)(void *arg, uint8_t *cqe, int32_t status), void *arg);

/* RQ completion handlers for RQ pair mode */
extern int32_t ocs_hw_rqpair_process_rq(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe);
extern ocs_hw_rtn_e ocs_hw_rqpair_sequence_free(ocs_hw_t *hw, ocs_hw_sequence_t *seq);
extern int32_t ocs_hw_rqpair_process_auto_xfr_rdy_cmd(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe);
extern int32_t ocs_hw_rqpair_process_auto_xfr_rdy_data(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe);
extern ocs_hw_rtn_e ocs_hw_rqpair_init(ocs_hw_t *hw);
extern ocs_hw_rtn_e ocs_hw_rqpair_auto_xfer_rdy_buffer_alloc(ocs_hw_t *hw, uint32_t num_buffers);
extern uint8_t ocs_hw_rqpair_auto_xfer_rdy_buffer_post(ocs_hw_t *hw, ocs_hw_io_t *io, int reuse_buf);
extern ocs_hw_rtn_e ocs_hw_rqpair_auto_xfer_rdy_move_to_port(ocs_hw_t *hw, ocs_hw_io_t *io);
extern void ocs_hw_rqpair_auto_xfer_rdy_move_to_host(ocs_hw_t *hw, ocs_hw_io_t *io);
extern void ocs_hw_rqpair_teardown(ocs_hw_t *hw);

extern ocs_hw_rtn_e ocs_hw_rx_allocate(ocs_hw_t *hw);
extern ocs_hw_rtn_e ocs_hw_rx_post(ocs_hw_t *hw);
extern void ocs_hw_rx_free(ocs_hw_t *hw);

extern void ocs_hw_unsol_process_bounce(void *arg);

typedef int32_t (*ocs_hw_async_cb_t)(ocs_hw_t *hw, int32_t status, uint8_t *mqe, void *arg);
extern int32_t ocs_hw_async_call(ocs_hw_t *hw, ocs_hw_async_cb_t callback, void *arg);

static inline void
ocs_hw_sequence_copy(ocs_hw_sequence_t *dst, ocs_hw_sequence_t *src)
{
	/* Copy the src to dst, then zero out the linked list link */
	*dst = *src;
	ocs_memset(&dst->link, 0, sizeof(dst->link));
}

static inline ocs_hw_rtn_e
ocs_hw_sequence_free(ocs_hw_t *hw, ocs_hw_sequence_t *seq)
{
	/* Only RQ pair mode is supported */
	return ocs_hw_rqpair_sequence_free(hw, seq);
}

/* HW WQ request tag API */
extern ocs_hw_rtn_e ocs_hw_reqtag_init(ocs_hw_t *hw);
extern hw_wq_callback_t *ocs_hw_reqtag_alloc(ocs_hw_t *hw,
					       void (*callback)(void *arg, uint8_t *cqe, int32_t status), void *arg);
extern void ocs_hw_reqtag_free(ocs_hw_t *hw, hw_wq_callback_t *wqcb);
extern hw_wq_callback_t *ocs_hw_reqtag_get_instance(ocs_hw_t *hw, uint32_t instance_index);
extern void ocs_hw_reqtag_reset(ocs_hw_t *hw);


extern uint32_t ocs_hw_dif_blocksize(ocs_hw_dif_info_t *dif_info);
extern int32_t ocs_hw_dif_mem_blocksize(ocs_hw_dif_info_t *dif_info, int wiretomem);
extern int32_t ocs_hw_dif_wire_blocksize(ocs_hw_dif_info_t *dif_info, int wiretomem);
extern uint32_t ocs_hw_get_def_wwn(ocs_t *ocs, uint32_t chan, uint64_t *wwpn, uint64_t *wwnn);

/* Uncomment to enable CPUTRACE */
//#define ENABLE_CPUTRACE
#ifdef ENABLE_CPUTRACE
#define CPUTRACE(t) ocs_printf("trace: %-20s %2s %-16s cpu %2d\n", __func__, t, \
	({ocs_thread_t *self = ocs_thread_self(); self != NULL ? self->name : "unknown";}), ocs_thread_getcpu());
#else
#define CPUTRACE(...)
#endif


/* Two levels of macro needed due to expansion */
#define HW_FWREV(a,b,c,d) (((uint64_t)(a) << 48) | ((uint64_t)(b) << 32) | ((uint64_t)(c) << 16) | ((uint64_t)(d)))
#define HW_FWREV_1(x) HW_FWREV(x)

#define OCS_FW_VER_STR2(a,b,c,d) #a "." #b "." #c "." #d
#define OCS_FW_VER_STR(x) OCS_FW_VER_STR2(x)

#define OCS_MIN_FW_VER_LANCER 10,4,255,0
#define OCS_MIN_FW_VER_SKYHAWK 10,4,255,0

extern void ocs_hw_workaround_setup(struct ocs_hw_s *hw);


/**
 * @brief Defines the number of the RQ buffers for each RQ
 */

#ifndef OCS_HW_RQ_NUM_HDR
#define OCS_HW_RQ_NUM_HDR		1024
#endif

#ifndef OCS_HW_RQ_NUM_PAYLOAD
#define OCS_HW_RQ_NUM_PAYLOAD			1024
#endif

/**
 * @brief Defines the size of the RQ buffers used for each RQ
 */
#ifndef OCS_HW_RQ_SIZE_HDR
#define OCS_HW_RQ_SIZE_HDR		128
#endif

#ifndef OCS_HW_RQ_SIZE_PAYLOAD
#define OCS_HW_RQ_SIZE_PAYLOAD		1024
#endif

/*
 * @brief Define the maximum number of multi-receive queues
 */
#ifndef OCS_HW_MAX_MRQS
#define OCS_HW_MAX_MRQS			8
#endif

/*
 * @brief Define count of when to set the WQEC bit in a submitted
 * WQE, causing a consummed/released completion to be posted.
 */
#ifndef OCS_HW_WQEC_SET_COUNT
#define OCS_HW_WQEC_SET_COUNT			32
#endif

/*
 * @brief Send frame timeout in seconds
 */
#ifndef OCS_HW_SEND_FRAME_TIMEOUT
#define OCS_HW_SEND_FRAME_TIMEOUT		10
#endif

/*
 * @brief FDT Transfer Hint value, reads greater than this value
 * will be segmented to implement fairness.   A value of zero disables
 * the feature.
 */
#ifndef OCS_HW_FDT_XFER_HINT
#define OCS_HW_FDT_XFER_HINT			8192
#endif

#endif /* !_OCS_HW_H */
