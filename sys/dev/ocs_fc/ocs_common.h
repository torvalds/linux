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
 * Contains declarations shared between the alex layer and HW/SLI4
 */


#if !defined(__OCS_COMMON_H__)
#define __OCS_COMMON_H__

#include "ocs_sm.h"
#include "ocs_utils.h"

#define OCS_CTRLMASK_XPORT_DISABLE_AUTORSP_TSEND	(1U << 0)
#define OCS_CTRLMASK_XPORT_DISABLE_AUTORSP_TRECEIVE	(1U << 1)
#define OCS_CTRLMASK_XPORT_ENABLE_TARGET_RSCN		(1U << 3)
#define OCS_CTRLMASK_TGT_ALWAYS_VERIFY_DIF		(1U << 4)
#define OCS_CTRLMASK_TGT_SET_DIF_REF_TAG_CRC		(1U << 5)
#define OCS_CTRLMASK_TEST_CHAINED_SGLS			(1U << 6)
#define OCS_CTRLMASK_ISCSI_ISNS_ENABLE			(1U << 7)
#define OCS_CTRLMASK_ENABLE_FABRIC_EMULATION		(1U << 8)
#define OCS_CTRLMASK_INHIBIT_INITIATOR			(1U << 9)
#define OCS_CTRLMASK_CRASH_RESET			(1U << 10)

#define enable_target_rscn(ocs) \
	((ocs->ctrlmask & OCS_CTRLMASK_XPORT_ENABLE_TARGET_RSCN) != 0)

/* Used for error injection testing. */
typedef enum {
	NO_ERR_INJECT = 0,
	INJECT_DROP_CMD,
	INJECT_FREE_DROPPED,
	INJECT_DROP_DATA,
	INJECT_DROP_RESP,
	INJECT_DELAY_CMD,
} ocs_err_injection_e;

#define MAX_OCS_DEVICES                 64

typedef enum {OCS_XPORT_FC, OCS_XPORT_ISCSI} ocs_xport_e;

#define OCS_SERVICE_PARMS_LENGTH		0x74
#define OCS_DISPLAY_NAME_LENGTH			32
#define OCS_DISPLAY_BUS_INFO_LENGTH		16

#define OCS_WWN_LENGTH				32

typedef struct ocs_hw_s ocs_hw_t;
typedef struct ocs_domain_s ocs_domain_t;
typedef struct ocs_sli_port_s ocs_sli_port_t;
typedef struct ocs_sli_port_s ocs_sport_t;
typedef struct ocs_remote_node_s ocs_remote_node_t;
typedef struct ocs_remote_node_group_s ocs_remote_node_group_t;
typedef struct ocs_node_s ocs_node_t;
typedef struct ocs_io_s ocs_io_t;
typedef struct ocs_xport_s ocs_xport_t;
typedef struct ocs_node_cb_s ocs_node_cb_t;
typedef struct ocs_ns_s ocs_ns_t;

/* Node group data structure */
typedef struct ocs_node_group_dir_s ocs_node_group_dir_t;

#include "ocs_cam.h"

/*--------------------------------------------------
 * Shared HW/SLI objects
 *
 * Several objects used by the HW/SLI layers are communal; part of the
 * object is for the sole use of the lower layers, but implementations
 * are free to add their own fields if desired.
 */

/**
 * @brief Description of discovered Fabric Domain
 *
 * @note Not all fields are valid for all mediums (FC/ethernet).
 */
typedef struct ocs_domain_record_s {
	uint32_t	index;		/**< FCF table index (used in REG_FCFI) */
	uint32_t	priority;	/**< FCF reported priority */
	uint8_t		address[6];	/**< Switch MAC/FC address */
	uint8_t		wwn[8];		/**< Switch WWN */
	union {
		uint8_t	vlan[512];	/**< bitmap of valid VLAN IDs */
		uint8_t	loop[128];	/**< FC-AL position map */
	} map;
	uint32_t	speed;		/**< link speed */
	uint32_t	fc_id;		/**< our ports fc_id */
	uint32_t	is_fc:1,	/**< Connection medium is native FC */
			is_ethernet:1,	/**< Connection medium is ethernet (FCoE) */
			is_loop:1,	/**< Topology is FC-AL */
			is_nport:1,	/**< Topology is N-PORT */
			:28;
} ocs_domain_record_t;

/**
 * @brief Node group directory entry
 */
struct ocs_node_group_dir_s {
	uint32_t instance_index;		/*<< instance index */
	ocs_sport_t *sport;			/*<< pointer to sport */
	uint8_t service_params[OCS_SERVICE_PARMS_LENGTH];	/**< Login parameters */
	ocs_list_link_t link;			/**< linked list link */
	ocs_list_t node_group_list;		/**< linked list of node groups */
	uint32_t node_group_list_count;		/**< current number of elements on the node group list */
	uint32_t next_idx;			/*<< index of the next node group in list */
};

typedef enum {
	OCS_SPORT_TOPOLOGY_UNKNOWN=0,
	OCS_SPORT_TOPOLOGY_FABRIC,
	OCS_SPORT_TOPOLOGY_P2P,
	OCS_SPORT_TOPOLOGY_LOOP,
} ocs_sport_topology_e;

/**
 * @brief SLI Port object
 *
 * The SLI Port object represents the connection between the driver and the
 * FC/FCoE domain. In some topologies / hardware, it is possible to have
 * multiple connections to the domain via different WWN. Each would require
 * a separate SLI port object.
 */
struct ocs_sli_port_s {

	ocs_t *ocs;				/**< pointer to ocs */
	uint32_t tgt_id;			/**< target id */
	uint32_t index;				/**< ??? */
	uint32_t instance_index;
	char display_name[OCS_DISPLAY_NAME_LENGTH]; /**< sport display name */
	ocs_domain_t *domain;			/**< current fabric domain */
	uint32_t	is_vport:1;		/**< this SPORT is a virtual port */
	uint64_t	wwpn;			/**< WWPN from HW (host endian) */
	uint64_t	wwnn;			/**< WWNN from HW (host endian) */
	ocs_list_t node_list;			/**< list of nodes */
	ocs_scsi_ini_sport_t ini_sport;		/**< initiator backend private sport data */
	ocs_scsi_tgt_sport_t tgt_sport;		/**< target backend private sport data */
	void	*tgt_data;			/**< target backend private pointer */
	void	*ini_data;			/**< initiator backend private pointer */
	ocs_mgmt_functions_t *mgmt_functions;

	/*
	 * Members private to HW/SLI
	 */
	ocs_sm_ctx_t	ctx;		/**< state machine context */
	ocs_hw_t	*hw;		/**< pointer to HW */
	uint32_t	indicator;	/**< VPI */
	uint32_t	fc_id;		/**< FC address */
	ocs_dma_t	dma;		/**< memory for Service Parameters */

	uint8_t		wwnn_str[OCS_WWN_LENGTH];	/**< WWN (ASCII) */
	uint64_t	sli_wwpn;	/**< WWPN (wire endian) */
	uint64_t	sli_wwnn;	/**< WWNN (wire endian) */
	uint32_t	sm_free_req_pending:1;	/**< Free request received while waiting for attach response */

	/*
	 * Implementation specific fields allowed here
	 */
	ocs_sm_ctx_t	sm;			/**< sport context state machine */
	sparse_vector_t lookup;			/**< fc_id to node lookup object */
	ocs_list_link_t link;
	uint32_t	enable_ini:1,		/**< SCSI initiator enabled for this node */
			enable_tgt:1,		/**< SCSI target enabled for this node */
			enable_rscn:1,		/**< This SPORT will be expecting RSCN */
			shutting_down:1,	/**< sport in process of shutting down */
			p2p_winner:1;		/**< TRUE if we're the point-to-point winner */
	ocs_sport_topology_e topology;		/**< topology: fabric/p2p/unknown */
	uint8_t		service_params[OCS_SERVICE_PARMS_LENGTH]; /**< Login parameters */
	uint32_t	p2p_remote_port_id;	/**< Remote node's port id for p2p */
	uint32_t	p2p_port_id;		/**< our port's id */

	/* List of remote node group directory entries (used by high login mode) */
	ocs_lock_t	node_group_lock;
	uint32_t	node_group_dir_next_instance; /**< HLM next node group directory instance value */
	uint32_t	node_group_next_instance; /**< HLM next node group instance value */
	ocs_list_t	node_group_dir_list;
};

/**
 * @brief Fibre Channel domain object
 *
 * This object is a container for the various SLI components needed
 * to connect to the domain of a FC or FCoE switch
 */
struct ocs_domain_s {

	ocs_t *ocs;				/**< pointer back to ocs */
	uint32_t instance_index;		/**< unique instance index value */
	char display_name[OCS_DISPLAY_NAME_LENGTH]; /**< Node display name */
	ocs_list_t sport_list;			/**< linked list of SLI ports */
	ocs_scsi_ini_domain_t ini_domain;	/**< initiator backend private domain data */
	ocs_scsi_tgt_domain_t tgt_domain;	/**< target backend private domain data */
	ocs_mgmt_functions_t *mgmt_functions;

	/* Declarations private to HW/SLI */
	ocs_hw_t	*hw;		/**< pointer to HW */
	ocs_sm_ctx_t	sm;		/**< state machine context */
	uint32_t	fcf;		/**< FC Forwarder table index */
	uint32_t	fcf_indicator;	/**< FCFI */
	uint32_t	vlan_id;	/**< VLAN tag for this domain */
	uint32_t	indicator;	/**< VFI */
	ocs_dma_t	dma;		/**< memory for Service Parameters */
	uint32_t	req_rediscover_fcf:1;	/**< TRUE if fcf rediscover is needed (in response
						 * to Vlink Clear async event */

	/* Declarations private to FC transport */
	uint64_t	fcf_wwn;	/**< WWN for FCF/switch */
	ocs_list_link_t link;
	ocs_sm_ctx_t	drvsm;		/**< driver domain sm context */
	uint32_t	attached:1,	/**< set true after attach completes */
			is_fc:1,	/**< is FC */
			is_loop:1,	/**< is loop topology */
			is_nlport:1,	/**< is public loop */
			domain_found_pending:1,	/**< A domain found is pending, drec is updated */
			req_domain_free:1,	/**< True if domain object should be free'd */
			req_accept_frames:1,	/**< set in domain state machine to enable frames */
			domain_notify_pend:1;  /** Set in domain SM to avoid duplicate node event post */
	ocs_domain_record_t pending_drec; /**< Pending drec if a domain found is pending */
	uint8_t		service_params[OCS_SERVICE_PARMS_LENGTH]; /**< any sports service parameters */
	uint8_t		flogi_service_params[OCS_SERVICE_PARMS_LENGTH]; /**< Fabric/P2p service parameters from FLOGI */
	uint8_t		femul_enable;	/**< TRUE if Fabric Emulation mode is enabled */

	/* Declarations shared with back-ends */
	sparse_vector_t lookup;		/**< d_id to node lookup object */
	ocs_lock_t	lookup_lock;

	ocs_sli_port_t	*sport;		/**< Pointer to first (physical) SLI port (also at the head of sport_list) */
	uint32_t	sport_instance_count; /**< count of sport instances */

	/* Fabric Emulation */
	ocs_bitmap_t *portid_pool;
	ocs_ns_t *ocs_ns;			/*>> Directory(Name) services data */
};

/**
 * @brief Remote Node object
 *
 * This object represents a connection between the SLI port and another
 * Nx_Port on the fabric. Note this can be either a well known port such
 * as a F_Port (i.e. ff:ff:fe) or another N_Port.
 */
struct ocs_remote_node_s {
	/*
	 * Members private to HW/SLI
	 */
	uint32_t	indicator;	/**< RPI */
	uint32_t	index;
	uint32_t	fc_id;		/**< FC address */

	uint32_t	attached:1,	/**< true if attached */
			node_group:1,	/**< true if in node group */
			free_group:1;	/**< true if the node group should be free'd */

	ocs_sli_port_t	*sport;		/**< associated SLI port */

	/*
	 * Implementation specific fields allowed here
	 */
	void *node;			/**< associated node */
};

struct ocs_remote_node_group_s {
	/*
	 * Members private to HW/SLI
	 */
	uint32_t	indicator;	/**< RPI */
	uint32_t	index;

	/*
	 * Implementation specific fields allowed here
	 */


	uint32_t instance_index;		/*<< instance index */
	ocs_node_group_dir_t *node_group_dir;	/*<< pointer to the node group directory */
	ocs_list_link_t link;			/*<< linked list link */
};

typedef enum {
	OCS_NODE_SHUTDOWN_DEFAULT = 0,
	OCS_NODE_SHUTDOWN_EXPLICIT_LOGO,
	OCS_NODE_SHUTDOWN_IMPLICIT_LOGO,
} ocs_node_shutd_rsn_e;

typedef enum {
	OCS_NODE_SEND_LS_ACC_NONE = 0,
	OCS_NODE_SEND_LS_ACC_PLOGI,
	OCS_NODE_SEND_LS_ACC_PRLI,
} ocs_node_send_ls_acc_e;

/**
 * @brief FC Node object
 *
 */
struct ocs_node_s {

	ocs_t *ocs;				/**< pointer back to ocs structure */
	uint32_t instance_index;		/**< unique instance index value */
	char display_name[OCS_DISPLAY_NAME_LENGTH]; /**< Node display name */
	ocs_sport_t *sport;
	uint32_t hold_frames:1;			/**< hold incoming frames if true */
	ocs_rlock_t lock;			/**< node wide lock */
	ocs_lock_t active_ios_lock;		/**< active SCSI and XPORT I/O's for this node */
	ocs_list_t active_ios;			/**< active I/O's for this node */
	uint32_t max_wr_xfer_size;		/**< Max write IO size per phase for the transport */
	ocs_scsi_ini_node_t ini_node;		/**< backend initiator private node data */
	ocs_scsi_tgt_node_t tgt_node;		/**< backend target private node data */
	ocs_mgmt_functions_t *mgmt_functions;

	/* Declarations private to HW/SLI */
	ocs_remote_node_t	rnode;		/**< Remote node */

	/* Declarations private to FC transport */
	ocs_sm_ctx_t		sm;		/**< state machine context */
	uint32_t		evtdepth;	/**< current event posting nesting depth */
	uint32_t		req_free:1,	/**< this node is to be free'd */
				attached:1,	/**< node is attached (REGLOGIN complete) */
				fcp_enabled:1,	/**< node is enabled to handle FCP */
				rscn_pending:1,	/**< for name server node RSCN is pending */
				send_plogi:1,	/**< if initiator, send PLOGI at node initialization */
				send_plogi_acc:1,/**< send PLOGI accept, upon completion of node attach */
				io_alloc_enabled:1, /**< TRUE if ocs_scsi_io_alloc() and ocs_els_io_alloc() are enabled */
				sent_prli:1;    /**< if initiator, sent prli. */
	ocs_node_send_ls_acc_e	send_ls_acc;	/**< type of LS acc to send */
	ocs_io_t		*ls_acc_io;	/**< SCSI IO for LS acc */
	uint32_t		ls_acc_oxid;	/**< OX_ID for pending accept */
	uint32_t		ls_acc_did;	/**< D_ID for pending accept */
	ocs_node_shutd_rsn_e	shutdown_reason;/**< reason for node shutdown */
	ocs_dma_t		sparm_dma_buf;	/**< service parameters buffer */
	uint8_t			service_params[OCS_SERVICE_PARMS_LENGTH]; /**< plogi/acc frame from remote device */
	ocs_lock_t		pend_frames_lock; /**< lock for inbound pending frames list */
	ocs_list_t		pend_frames;	/**< inbound pending frames list */
	uint32_t		pend_frames_processed;	/**< count of frames processed in hold frames interval */
	uint32_t		ox_id_in_use;	/**< used to verify one at a time us of ox_id */
	uint32_t		els_retries_remaining;	/**< for ELS, number of retries remaining */
	uint32_t		els_req_cnt;	/**< number of outstanding ELS requests */
	uint32_t		els_cmpl_cnt;	/**< number of outstanding ELS completions */
	uint32_t		abort_cnt;	/**< Abort counter for debugging purpose */

	char current_state_name[OCS_DISPLAY_NAME_LENGTH]; /**< current node state */
	char prev_state_name[OCS_DISPLAY_NAME_LENGTH]; /**< previous node state */
	ocs_sm_event_t		current_evt;	/**< current event */
	ocs_sm_event_t		prev_evt;	/**< current event */
	uint32_t		targ:1,		/**< node is target capable */
				init:1,		/**< node is initiator capable */
				refound:1,	/**< Handle node refound case when node is being deleted  */
				fcp2device:1,    /* FCP2 device */
				reserved:4,
				fc_type:8;
	ocs_list_t		els_io_pend_list;   /**< list of pending (not yet processed) ELS IOs */
	ocs_list_t		els_io_active_list; /**< list of active (processed) ELS IOs */

	ocs_sm_function_t	nodedb_state;	/**< Node debugging, saved state */

	ocs_timer_t		gidpt_delay_timer;	/**< GIDPT delay timer */
	time_t			time_last_gidpt_msec;	/**< Start time of last target RSCN GIDPT  */

	/* WWN */
	char wwnn[OCS_WWN_LENGTH];		/**< remote port WWN (uses iSCSI naming) */
	char wwpn[OCS_WWN_LENGTH];		/**< remote port WWN (uses iSCSI naming) */

	/* Statistics */
	uint32_t		chained_io_count;	/**< count of IOs with chained SGL's */

	ocs_list_link_t		link;		/**< node list link */

	ocs_remote_node_group_t	*node_group;	/**< pointer to node group (if HLM enabled) */
};

/**
 * @brief Virtual port specification
 *
 * Collection of the information required to restore a virtual port across
 * link events
 */

typedef struct ocs_vport_spec_s ocs_vport_spec_t;
struct ocs_vport_spec_s {
	uint32_t domain_instance;		/*>> instance index of this domain for the sport */
	uint64_t wwnn;				/*>> node name */
	uint64_t wwpn;				/*>> port name */
	uint32_t fc_id;				/*>> port id */
	uint32_t enable_tgt:1,			/*>> port is a target */
		enable_ini:1;			/*>> port is an initiator */
	ocs_list_link_t link;			/*>> link */
	void	*tgt_data;			/**< target backend pointer */
	void	*ini_data;			/**< initiator backend pointer */
	ocs_sport_t *sport;			/**< Used to match record after attaching for update */
};


#endif /* __OCS_COMMON_H__*/
