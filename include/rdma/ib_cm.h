/*
 * Copyright (c) 2004, 2005 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#if !defined(IB_CM_H)
#define IB_CM_H

#include <rdma/ib_mad.h>
#include <rdma/ib_sa.h>

/* ib_cm and ib_user_cm modules share /sys/class/infiniband_cm */
extern struct class cm_class;

enum ib_cm_state {
	IB_CM_IDLE,
	IB_CM_LISTEN,
	IB_CM_REQ_SENT,
	IB_CM_REQ_RCVD,
	IB_CM_MRA_REQ_SENT,
	IB_CM_MRA_REQ_RCVD,
	IB_CM_REP_SENT,
	IB_CM_REP_RCVD,
	IB_CM_MRA_REP_SENT,
	IB_CM_MRA_REP_RCVD,
	IB_CM_ESTABLISHED,
	IB_CM_DREQ_SENT,
	IB_CM_DREQ_RCVD,
	IB_CM_TIMEWAIT,
	IB_CM_SIDR_REQ_SENT,
	IB_CM_SIDR_REQ_RCVD
};

enum ib_cm_lap_state {
	IB_CM_LAP_UNINIT,
	IB_CM_LAP_IDLE,
	IB_CM_LAP_SENT,
	IB_CM_LAP_RCVD,
	IB_CM_MRA_LAP_SENT,
	IB_CM_MRA_LAP_RCVD,
};

enum ib_cm_event_type {
	IB_CM_REQ_ERROR,
	IB_CM_REQ_RECEIVED,
	IB_CM_REP_ERROR,
	IB_CM_REP_RECEIVED,
	IB_CM_RTU_RECEIVED,
	IB_CM_USER_ESTABLISHED,
	IB_CM_DREQ_ERROR,
	IB_CM_DREQ_RECEIVED,
	IB_CM_DREP_RECEIVED,
	IB_CM_TIMEWAIT_EXIT,
	IB_CM_MRA_RECEIVED,
	IB_CM_REJ_RECEIVED,
	IB_CM_LAP_ERROR,
	IB_CM_LAP_RECEIVED,
	IB_CM_APR_RECEIVED,
	IB_CM_SIDR_REQ_ERROR,
	IB_CM_SIDR_REQ_RECEIVED,
	IB_CM_SIDR_REP_RECEIVED
};

enum ib_cm_data_size {
	IB_CM_REQ_PRIVATE_DATA_SIZE	 = 92,
	IB_CM_MRA_PRIVATE_DATA_SIZE	 = 222,
	IB_CM_REJ_PRIVATE_DATA_SIZE	 = 148,
	IB_CM_REP_PRIVATE_DATA_SIZE	 = 196,
	IB_CM_RTU_PRIVATE_DATA_SIZE	 = 224,
	IB_CM_DREQ_PRIVATE_DATA_SIZE	 = 220,
	IB_CM_DREP_PRIVATE_DATA_SIZE	 = 224,
	IB_CM_REJ_ARI_LENGTH		 = 72,
	IB_CM_LAP_PRIVATE_DATA_SIZE	 = 168,
	IB_CM_APR_PRIVATE_DATA_SIZE	 = 148,
	IB_CM_APR_INFO_LENGTH		 = 72,
	IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE = 216,
	IB_CM_SIDR_REP_PRIVATE_DATA_SIZE = 136,
	IB_CM_SIDR_REP_INFO_LENGTH	 = 72,
};

struct ib_cm_id;

struct ib_cm_req_event_param {
	struct ib_cm_id		*listen_id;

	/* P_Key that was used by the GMP's BTH header */
	u16			bth_pkey;

	u8			port;

	struct sa_path_rec	*primary_path;
	struct sa_path_rec	*alternate_path;

	__be64			remote_ca_guid;
	u32			remote_qkey;
	u32			remote_qpn;
	enum ib_qp_type		qp_type;

	u32			starting_psn;
	u8			responder_resources;
	u8			initiator_depth;
	unsigned int		local_cm_response_timeout:5;
	unsigned int		flow_control:1;
	unsigned int		remote_cm_response_timeout:5;
	unsigned int		retry_count:3;
	unsigned int		rnr_retry_count:3;
	unsigned int		srq:1;
};

struct ib_cm_rep_event_param {
	__be64			remote_ca_guid;
	u32			remote_qkey;
	u32			remote_qpn;
	u32			starting_psn;
	u8			responder_resources;
	u8			initiator_depth;
	unsigned int		target_ack_delay:5;
	unsigned int		failover_accepted:2;
	unsigned int		flow_control:1;
	unsigned int		rnr_retry_count:3;
	unsigned int		srq:1;
};

enum ib_cm_rej_reason {
	IB_CM_REJ_NO_QP				= 1,
	IB_CM_REJ_NO_EEC			= 2,
	IB_CM_REJ_NO_RESOURCES			= 3,
	IB_CM_REJ_TIMEOUT			= 4,
	IB_CM_REJ_UNSUPPORTED			= 5,
	IB_CM_REJ_INVALID_COMM_ID		= 6,
	IB_CM_REJ_INVALID_COMM_INSTANCE		= 7,
	IB_CM_REJ_INVALID_SERVICE_ID		= 8,
	IB_CM_REJ_INVALID_TRANSPORT_TYPE	= 9,
	IB_CM_REJ_STALE_CONN			= 10,
	IB_CM_REJ_RDC_NOT_EXIST			= 11,
	IB_CM_REJ_INVALID_GID			= 12,
	IB_CM_REJ_INVALID_LID			= 13,
	IB_CM_REJ_INVALID_SL			= 14,
	IB_CM_REJ_INVALID_TRAFFIC_CLASS		= 15,
	IB_CM_REJ_INVALID_HOP_LIMIT		= 16,
	IB_CM_REJ_INVALID_PACKET_RATE		= 17,
	IB_CM_REJ_INVALID_ALT_GID		= 18,
	IB_CM_REJ_INVALID_ALT_LID		= 19,
	IB_CM_REJ_INVALID_ALT_SL		= 20,
	IB_CM_REJ_INVALID_ALT_TRAFFIC_CLASS	= 21,
	IB_CM_REJ_INVALID_ALT_HOP_LIMIT		= 22,
	IB_CM_REJ_INVALID_ALT_PACKET_RATE	= 23,
	IB_CM_REJ_PORT_CM_REDIRECT		= 24,
	IB_CM_REJ_PORT_REDIRECT			= 25,
	IB_CM_REJ_INVALID_MTU			= 26,
	IB_CM_REJ_INSUFFICIENT_RESP_RESOURCES	= 27,
	IB_CM_REJ_CONSUMER_DEFINED		= 28,
	IB_CM_REJ_INVALID_RNR_RETRY		= 29,
	IB_CM_REJ_DUPLICATE_LOCAL_COMM_ID	= 30,
	IB_CM_REJ_INVALID_CLASS_VERSION		= 31,
	IB_CM_REJ_INVALID_FLOW_LABEL		= 32,
	IB_CM_REJ_INVALID_ALT_FLOW_LABEL	= 33
};

struct ib_cm_rej_event_param {
	enum ib_cm_rej_reason	reason;
	void			*ari;
	u8			ari_length;
};

struct ib_cm_mra_event_param {
	u8	service_timeout;
};

struct ib_cm_lap_event_param {
	struct sa_path_rec	*alternate_path;
};

enum ib_cm_apr_status {
	IB_CM_APR_SUCCESS,
	IB_CM_APR_INVALID_COMM_ID,
	IB_CM_APR_UNSUPPORTED,
	IB_CM_APR_REJECT,
	IB_CM_APR_REDIRECT,
	IB_CM_APR_IS_CURRENT,
	IB_CM_APR_INVALID_QPN_EECN,
	IB_CM_APR_INVALID_LID,
	IB_CM_APR_INVALID_GID,
	IB_CM_APR_INVALID_FLOW_LABEL,
	IB_CM_APR_INVALID_TCLASS,
	IB_CM_APR_INVALID_HOP_LIMIT,
	IB_CM_APR_INVALID_PACKET_RATE,
	IB_CM_APR_INVALID_SL
};

struct ib_cm_apr_event_param {
	enum ib_cm_apr_status	ap_status;
	void			*apr_info;
	u8			info_len;
};

struct ib_cm_sidr_req_event_param {
	struct ib_cm_id		*listen_id;
	__be64			service_id;
	/* P_Key that was used by the GMP's BTH header */
	u16			bth_pkey;
	u8			port;
	u16			pkey;
};

enum ib_cm_sidr_status {
	IB_SIDR_SUCCESS,
	IB_SIDR_UNSUPPORTED,
	IB_SIDR_REJECT,
	IB_SIDR_NO_QP,
	IB_SIDR_REDIRECT,
	IB_SIDR_UNSUPPORTED_VERSION
};

struct ib_cm_sidr_rep_event_param {
	enum ib_cm_sidr_status	status;
	u32			qkey;
	u32			qpn;
	void			*info;
	const struct ib_gid_attr *sgid_attr;
	u8			info_len;
};

struct ib_cm_event {
	enum ib_cm_event_type	event;
	union {
		struct ib_cm_req_event_param	req_rcvd;
		struct ib_cm_rep_event_param	rep_rcvd;
		/* No data for RTU received events. */
		struct ib_cm_rej_event_param	rej_rcvd;
		struct ib_cm_mra_event_param	mra_rcvd;
		struct ib_cm_lap_event_param	lap_rcvd;
		struct ib_cm_apr_event_param	apr_rcvd;
		/* No data for DREQ/DREP received events. */
		struct ib_cm_sidr_req_event_param sidr_req_rcvd;
		struct ib_cm_sidr_rep_event_param sidr_rep_rcvd;
		enum ib_wc_status		send_status;
	} param;

	void			*private_data;
};

#define CM_REQ_ATTR_ID		cpu_to_be16(0x0010)
#define CM_MRA_ATTR_ID		cpu_to_be16(0x0011)
#define CM_REJ_ATTR_ID		cpu_to_be16(0x0012)
#define CM_REP_ATTR_ID		cpu_to_be16(0x0013)
#define CM_RTU_ATTR_ID		cpu_to_be16(0x0014)
#define CM_DREQ_ATTR_ID		cpu_to_be16(0x0015)
#define CM_DREP_ATTR_ID		cpu_to_be16(0x0016)
#define CM_SIDR_REQ_ATTR_ID	cpu_to_be16(0x0017)
#define CM_SIDR_REP_ATTR_ID	cpu_to_be16(0x0018)
#define CM_LAP_ATTR_ID		cpu_to_be16(0x0019)
#define CM_APR_ATTR_ID		cpu_to_be16(0x001A)

/**
 * ib_cm_handler - User-defined callback to process communication events.
 * @cm_id: Communication identifier associated with the reported event.
 * @event: Information about the communication event.
 *
 * IB_CM_REQ_RECEIVED and IB_CM_SIDR_REQ_RECEIVED communication events
 * generated as a result of listen requests result in the allocation of a
 * new @cm_id.  The new @cm_id is returned to the user through this callback.
 * Clients are responsible for destroying the new @cm_id.  For peer-to-peer
 * IB_CM_REQ_RECEIVED and all other events, the returned @cm_id corresponds
 * to a user's existing communication identifier.
 *
 * Users may not call ib_destroy_cm_id while in the context of this callback;
 * however, returning a non-zero value instructs the communication manager to
 * destroy the @cm_id after the callback completes.
 */
typedef int (*ib_cm_handler)(struct ib_cm_id *cm_id,
			     struct ib_cm_event *event);

struct ib_cm_id {
	ib_cm_handler		cm_handler;
	void			*context;
	struct ib_device	*device;
	__be64			service_id;
	__be64			service_mask;
	enum ib_cm_state	state;		/* internal CM/debug use */
	enum ib_cm_lap_state	lap_state;	/* internal CM/debug use */
	__be32			local_id;
	__be32			remote_id;
	u32			remote_cm_qpn;  /* 1 unless redirected */
};

/**
 * ib_create_cm_id - Allocate a communication identifier.
 * @device: Device associated with the cm_id.  All related communication will
 * be associated with the specified device.
 * @cm_handler: Callback invoked to notify the user of CM events.
 * @context: User specified context associated with the communication
 *   identifier.
 *
 * Communication identifiers are used to track connection states, service
 * ID resolution requests, and listen requests.
 */
struct ib_cm_id *ib_create_cm_id(struct ib_device *device,
				 ib_cm_handler cm_handler,
				 void *context);

/**
 * ib_destroy_cm_id - Destroy a connection identifier.
 * @cm_id: Connection identifier to destroy.
 *
 * This call blocks until the connection identifier is destroyed.
 */
void ib_destroy_cm_id(struct ib_cm_id *cm_id);

#define IB_SERVICE_ID_AGN_MASK	cpu_to_be64(0xFF00000000000000ULL)
#define IB_CM_ASSIGN_SERVICE_ID	cpu_to_be64(0x0200000000000000ULL)
#define IB_CMA_SERVICE_ID	cpu_to_be64(0x0000000001000000ULL)
#define IB_CMA_SERVICE_ID_MASK	cpu_to_be64(0xFFFFFFFFFF000000ULL)
#define IB_SDP_SERVICE_ID	cpu_to_be64(0x0000000000010000ULL)
#define IB_SDP_SERVICE_ID_MASK	cpu_to_be64(0xFFFFFFFFFFFF0000ULL)

/**
 * ib_cm_listen - Initiates listening on the specified service ID for
 *   connection and service ID resolution requests.
 * @cm_id: Connection identifier associated with the listen request.
 * @service_id: Service identifier matched against incoming connection
 *   and service ID resolution requests.  The service ID should be specified
 *   network-byte order.  If set to IB_CM_ASSIGN_SERVICE_ID, the CM will
 *   assign a service ID to the caller.
 * @service_mask: Mask applied to service ID used to listen across a
 *   range of service IDs.  If set to 0, the service ID is matched
 *   exactly.  This parameter is ignored if %service_id is set to
 *   IB_CM_ASSIGN_SERVICE_ID.
 */
int ib_cm_listen(struct ib_cm_id *cm_id, __be64 service_id,
		 __be64 service_mask);

struct ib_cm_id *ib_cm_insert_listen(struct ib_device *device,
				     ib_cm_handler cm_handler,
				     __be64 service_id);

struct ib_cm_req_param {
	struct sa_path_rec	*primary_path;
	struct sa_path_rec	*alternate_path;
	const struct ib_gid_attr *ppath_sgid_attr;
	__be64			service_id;
	u32			qp_num;
	enum ib_qp_type		qp_type;
	u32			starting_psn;
	const void		*private_data;
	u8			private_data_len;
	u8			peer_to_peer;
	u8			responder_resources;
	u8			initiator_depth;
	u8			remote_cm_response_timeout;
	u8			flow_control;
	u8			local_cm_response_timeout;
	u8			retry_count;
	u8			rnr_retry_count;
	u8			max_cm_retries;
	u8			srq;
};

/**
 * ib_send_cm_req - Sends a connection request to the remote node.
 * @cm_id: Connection identifier that will be associated with the
 *   connection request.
 * @param: Connection request information needed to establish the
 *   connection.
 */
int ib_send_cm_req(struct ib_cm_id *cm_id,
		   struct ib_cm_req_param *param);

struct ib_cm_rep_param {
	u32		qp_num;
	u32		starting_psn;
	const void	*private_data;
	u8		private_data_len;
	u8		responder_resources;
	u8		initiator_depth;
	u8		failover_accepted;
	u8		flow_control;
	u8		rnr_retry_count;
	u8		srq;
};

/**
 * ib_send_cm_rep - Sends a connection reply in response to a connection
 *   request.
 * @cm_id: Connection identifier that will be associated with the
 *   connection request.
 * @param: Connection reply information needed to establish the
 *   connection.
 */
int ib_send_cm_rep(struct ib_cm_id *cm_id,
		   struct ib_cm_rep_param *param);

/**
 * ib_send_cm_rtu - Sends a connection ready to use message in response
 *   to a connection reply message.
 * @cm_id: Connection identifier associated with the connection request.
 * @private_data: Optional user-defined private data sent with the
 *   ready to use message.
 * @private_data_len: Size of the private data buffer, in bytes.
 */
int ib_send_cm_rtu(struct ib_cm_id *cm_id,
		   const void *private_data,
		   u8 private_data_len);

/**
 * ib_send_cm_dreq - Sends a disconnection request for an existing
 *   connection.
 * @cm_id: Connection identifier associated with the connection being
 *   released.
 * @private_data: Optional user-defined private data sent with the
 *   disconnection request message.
 * @private_data_len: Size of the private data buffer, in bytes.
 */
int ib_send_cm_dreq(struct ib_cm_id *cm_id,
		    const void *private_data,
		    u8 private_data_len);

/**
 * ib_send_cm_drep - Sends a disconnection reply to a disconnection request.
 * @cm_id: Connection identifier associated with the connection being
 *   released.
 * @private_data: Optional user-defined private data sent with the
 *   disconnection reply message.
 * @private_data_len: Size of the private data buffer, in bytes.
 *
 * If the cm_id is in the correct state, the CM will transition the connection
 * to the timewait state, even if an error occurs sending the DREP message.
 */
int ib_send_cm_drep(struct ib_cm_id *cm_id,
		    const void *private_data,
		    u8 private_data_len);

/**
 * ib_cm_notify - Notifies the CM of an event reported to the consumer.
 * @cm_id: Connection identifier to transition to established.
 * @event: Type of event.
 *
 * This routine should be invoked by users to notify the CM of relevant
 * communication events.  Events that should be reported to the CM and
 * when to report them are:
 *
 * IB_EVENT_COMM_EST - Used when a message is received on a connected
 *    QP before an RTU has been received.
 * IB_EVENT_PATH_MIG - Notifies the CM that the connection has failed over
 *   to the alternate path.
 */
int ib_cm_notify(struct ib_cm_id *cm_id, enum ib_event_type event);

/**
 * ib_send_cm_rej - Sends a connection rejection message to the
 *   remote node.
 * @cm_id: Connection identifier associated with the connection being
 *   rejected.
 * @reason: Reason for the connection request rejection.
 * @ari: Optional additional rejection information.
 * @ari_length: Size of the additional rejection information, in bytes.
 * @private_data: Optional user-defined private data sent with the
 *   rejection message.
 * @private_data_len: Size of the private data buffer, in bytes.
 */
int ib_send_cm_rej(struct ib_cm_id *cm_id,
		   enum ib_cm_rej_reason reason,
		   void *ari,
		   u8 ari_length,
		   const void *private_data,
		   u8 private_data_len);

#define IB_CM_MRA_FLAG_DELAY 0x80  /* Send MRA only after a duplicate msg */

/**
 * ib_send_cm_mra - Sends a message receipt acknowledgement to a connection
 *   message.
 * @cm_id: Connection identifier associated with the connection message.
 * @service_timeout: The lower 5-bits specify the maximum time required for
 *   the sender to reply to the connection message.  The upper 3-bits
 *   specify additional control flags.
 * @private_data: Optional user-defined private data sent with the
 *   message receipt acknowledgement.
 * @private_data_len: Size of the private data buffer, in bytes.
 */
int ib_send_cm_mra(struct ib_cm_id *cm_id,
		   u8 service_timeout,
		   const void *private_data,
		   u8 private_data_len);

/**
 * ib_send_cm_lap - Sends a load alternate path request.
 * @cm_id: Connection identifier associated with the load alternate path
 *   message.
 * @alternate_path: A path record that identifies the alternate path to
 *   load.
 * @private_data: Optional user-defined private data sent with the
 *   load alternate path message.
 * @private_data_len: Size of the private data buffer, in bytes.
 */
int ib_send_cm_lap(struct ib_cm_id *cm_id,
		   struct sa_path_rec *alternate_path,
		   const void *private_data,
		   u8 private_data_len);

/**
 * ib_cm_init_qp_attr - Initializes the QP attributes for use in transitioning
 *   to a specified QP state.
 * @cm_id: Communication identifier associated with the QP attributes to
 *   initialize.
 * @qp_attr: On input, specifies the desired QP state.  On output, the
 *   mandatory and desired optional attributes will be set in order to
 *   modify the QP to the specified state.
 * @qp_attr_mask: The QP attribute mask that may be used to transition the
 *   QP to the specified state.
 *
 * Users must set the @qp_attr->qp_state to the desired QP state.  This call
 * will set all required attributes for the given transition, along with
 * known optional attributes.  Users may override the attributes returned from
 * this call before calling ib_modify_qp.
 */
int ib_cm_init_qp_attr(struct ib_cm_id *cm_id,
		       struct ib_qp_attr *qp_attr,
		       int *qp_attr_mask);

/**
 * ib_send_cm_apr - Sends an alternate path response message in response to
 *   a load alternate path request.
 * @cm_id: Connection identifier associated with the alternate path response.
 * @status: Reply status sent with the alternate path response.
 * @info: Optional additional information sent with the alternate path
 *   response.
 * @info_length: Size of the additional information, in bytes.
 * @private_data: Optional user-defined private data sent with the
 *   alternate path response message.
 * @private_data_len: Size of the private data buffer, in bytes.
 */
int ib_send_cm_apr(struct ib_cm_id *cm_id,
		   enum ib_cm_apr_status status,
		   void *info,
		   u8 info_length,
		   const void *private_data,
		   u8 private_data_len);

struct ib_cm_sidr_req_param {
	struct sa_path_rec	*path;
	const struct ib_gid_attr *sgid_attr;
	__be64			service_id;
	int			timeout_ms;
	const void		*private_data;
	u8			private_data_len;
	u8			max_cm_retries;
};

/**
 * ib_send_cm_sidr_req - Sends a service ID resolution request to the
 *   remote node.
 * @cm_id: Communication identifier that will be associated with the
 *   service ID resolution request.
 * @param: Service ID resolution request information.
 */
int ib_send_cm_sidr_req(struct ib_cm_id *cm_id,
			struct ib_cm_sidr_req_param *param);

struct ib_cm_sidr_rep_param {
	u32			qp_num;
	u32			qkey;
	enum ib_cm_sidr_status	status;
	const void		*info;
	u8			info_length;
	const void		*private_data;
	u8			private_data_len;
};

/**
 * ib_send_cm_sidr_rep - Sends a service ID resolution reply to the
 *   remote node.
 * @cm_id: Communication identifier associated with the received service ID
 *   resolution request.
 * @param: Service ID resolution reply information.
 */
int ib_send_cm_sidr_rep(struct ib_cm_id *cm_id,
			struct ib_cm_sidr_rep_param *param);

/**
 * ibcm_reject_msg - return a pointer to a reject message string.
 * @reason: Value returned in the REJECT event status field.
 */
const char *__attribute_const__ ibcm_reject_msg(int reason);

#endif /* IB_CM_H */
