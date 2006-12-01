/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 *
 */

#if !defined(RDMA_CM_H)
#define RDMA_CM_H

#include <linux/socket.h>
#include <linux/in6.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_sa.h>

/*
 * Upon receiving a device removal event, users must destroy the associated
 * RDMA identifier and release all resources allocated with the device.
 */
enum rdma_cm_event_type {
	RDMA_CM_EVENT_ADDR_RESOLVED,
	RDMA_CM_EVENT_ADDR_ERROR,
	RDMA_CM_EVENT_ROUTE_RESOLVED,
	RDMA_CM_EVENT_ROUTE_ERROR,
	RDMA_CM_EVENT_CONNECT_REQUEST,
	RDMA_CM_EVENT_CONNECT_RESPONSE,
	RDMA_CM_EVENT_CONNECT_ERROR,
	RDMA_CM_EVENT_UNREACHABLE,
	RDMA_CM_EVENT_REJECTED,
	RDMA_CM_EVENT_ESTABLISHED,
	RDMA_CM_EVENT_DISCONNECTED,
	RDMA_CM_EVENT_DEVICE_REMOVAL,
};

enum rdma_port_space {
	RDMA_PS_SDP  = 0x0001,
	RDMA_PS_TCP  = 0x0106,
	RDMA_PS_UDP  = 0x0111,
	RDMA_PS_SCTP = 0x0183
};

struct rdma_addr {
	struct sockaddr src_addr;
	u8		src_pad[sizeof(struct sockaddr_in6) -
				sizeof(struct sockaddr)];
	struct sockaddr dst_addr;
	u8		dst_pad[sizeof(struct sockaddr_in6) -
				sizeof(struct sockaddr)];
	struct rdma_dev_addr dev_addr;
};

struct rdma_route {
	struct rdma_addr addr;
	struct ib_sa_path_rec *path_rec;
	int num_paths;
};

struct rdma_conn_param {
	const void *private_data;
	u8 private_data_len;
	u8 responder_resources;
	u8 initiator_depth;
	u8 flow_control;
	u8 retry_count;		/* ignored when accepting */
	u8 rnr_retry_count;
	/* Fields below ignored if a QP is created on the rdma_cm_id. */
	u8 srq;
	u32 qp_num;
};

struct rdma_cm_event {
	enum rdma_cm_event_type	 event;
	int			 status;
	union {
		struct rdma_conn_param	conn;
	} param;
};

struct rdma_cm_id;

/**
 * rdma_cm_event_handler - Callback used to report user events.
 *
 * Notes: Users may not call rdma_destroy_id from this callback to destroy
 *   the passed in id, or a corresponding listen id.  Returning a
 *   non-zero value from the callback will destroy the passed in id.
 */
typedef int (*rdma_cm_event_handler)(struct rdma_cm_id *id,
				     struct rdma_cm_event *event);

struct rdma_cm_id {
	struct ib_device	*device;
	void			*context;
	struct ib_qp		*qp;
	rdma_cm_event_handler	 event_handler;
	struct rdma_route	 route;
	enum rdma_port_space	 ps;
	u8			 port_num;
};

/**
 * rdma_create_id - Create an RDMA identifier.
 *
 * @event_handler: User callback invoked to report events associated with the
 *   returned rdma_id.
 * @context: User specified context associated with the id.
 * @ps: RDMA port space.
 */
struct rdma_cm_id *rdma_create_id(rdma_cm_event_handler event_handler,
				  void *context, enum rdma_port_space ps);

/**
  * rdma_destroy_id - Destroys an RDMA identifier.
  *
  * @id: RDMA identifier.
  *
  * Note: calling this function has the effect of canceling in-flight
  * asynchronous operations associated with the id.
  */
void rdma_destroy_id(struct rdma_cm_id *id);

/**
 * rdma_bind_addr - Bind an RDMA identifier to a source address and
 *   associated RDMA device, if needed.
 *
 * @id: RDMA identifier.
 * @addr: Local address information.  Wildcard values are permitted.
 *
 * This associates a source address with the RDMA identifier before calling
 * rdma_listen.  If a specific local address is given, the RDMA identifier will
 * be bound to a local RDMA device.
 */
int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr);

/**
 * rdma_resolve_addr - Resolve destination and optional source addresses
 *   from IP addresses to an RDMA address.  If successful, the specified
 *   rdma_cm_id will be bound to a local device.
 *
 * @id: RDMA identifier.
 * @src_addr: Source address information.  This parameter may be NULL.
 * @dst_addr: Destination address information.
 * @timeout_ms: Time to wait for resolution to complete.
 */
int rdma_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
		      struct sockaddr *dst_addr, int timeout_ms);

/**
 * rdma_resolve_route - Resolve the RDMA address bound to the RDMA identifier
 *   into route information needed to establish a connection.
 *
 * This is called on the client side of a connection.
 * Users must have first called rdma_resolve_addr to resolve a dst_addr
 * into an RDMA address before calling this routine.
 */
int rdma_resolve_route(struct rdma_cm_id *id, int timeout_ms);

/**
 * rdma_create_qp - Allocate a QP and associate it with the specified RDMA
 * identifier.
 *
 * QPs allocated to an rdma_cm_id will automatically be transitioned by the CMA
 * through their states.
 */
int rdma_create_qp(struct rdma_cm_id *id, struct ib_pd *pd,
		   struct ib_qp_init_attr *qp_init_attr);

/**
 * rdma_destroy_qp - Deallocate the QP associated with the specified RDMA
 * identifier.
 *
 * Users must destroy any QP associated with an RDMA identifier before
 * destroying the RDMA ID.
 */
void rdma_destroy_qp(struct rdma_cm_id *id);

/**
 * rdma_init_qp_attr - Initializes the QP attributes for use in transitioning
 *   to a specified QP state.
 * @id: Communication identifier associated with the QP attributes to
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
 *
 * Users that wish to have their QP automatically transitioned through its
 * states can associate a QP with the rdma_cm_id by calling rdma_create_qp().
 */
int rdma_init_qp_attr(struct rdma_cm_id *id, struct ib_qp_attr *qp_attr,
		       int *qp_attr_mask);

/**
 * rdma_connect - Initiate an active connection request.
 *
 * Users must have resolved a route for the rdma_cm_id to connect with
 * by having called rdma_resolve_route before calling this routine.
 */
int rdma_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param);

/**
 * rdma_listen - This function is called by the passive side to
 *   listen for incoming connection requests.
 *
 * Users must have bound the rdma_cm_id to a local address by calling
 * rdma_bind_addr before calling this routine.
 */
int rdma_listen(struct rdma_cm_id *id, int backlog);

/**
 * rdma_accept - Called to accept a connection request or response.
 * @id: Connection identifier associated with the request.
 * @conn_param: Information needed to establish the connection.  This must be
 *   provided if accepting a connection request.  If accepting a connection
 *   response, this parameter must be NULL.
 *
 * Typically, this routine is only called by the listener to accept a connection
 * request.  It must also be called on the active side of a connection if the
 * user is performing their own QP transitions.
 *
 * In the case of error, a reject message is sent to the remote side and the
 * state of the qp associated with the id is modified to error, such that any
 * previously posted receive buffers would be flushed.
 */
int rdma_accept(struct rdma_cm_id *id, struct rdma_conn_param *conn_param);

/**
 * rdma_reject - Called to reject a connection request or response.
 */
int rdma_reject(struct rdma_cm_id *id, const void *private_data,
		u8 private_data_len);

/**
 * rdma_disconnect - This function disconnects the associated QP and
 *   transitions it into the error state.
 */
int rdma_disconnect(struct rdma_cm_id *id);

#endif /* RDMA_CM_H */

