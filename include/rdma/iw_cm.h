/*
 * Copyright (c) 2005 Network Appliance, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
#ifndef IW_CM_H
#define IW_CM_H

#include <linux/in.h>
#include <rdma/ib_cm.h>

struct iw_cm_id;

enum iw_cm_event_type {
	IW_CM_EVENT_CONNECT_REQUEST = 1, /* connect request received */
	IW_CM_EVENT_CONNECT_REPLY,	 /* reply from active connect request */
	IW_CM_EVENT_ESTABLISHED,	 /* passive side accept successful */
	IW_CM_EVENT_DISCONNECT,		 /* orderly shutdown */
	IW_CM_EVENT_CLOSE		 /* close complete */
};

struct iw_cm_event {
	enum iw_cm_event_type event;
	int			 status;
	struct sockaddr_in local_addr;
	struct sockaddr_in remote_addr;
	void *private_data;
	void *provider_data;
	u8 private_data_len;
	u8 ord;
	u8 ird;
};

/**
 * iw_cm_handler - Function to be called by the IW CM when delivering events
 * to the client.
 *
 * @cm_id: The IW CM identifier associated with the event.
 * @event: Pointer to the event structure.
 */
typedef int (*iw_cm_handler)(struct iw_cm_id *cm_id,
			     struct iw_cm_event *event);

/**
 * iw_event_handler - Function called by the provider when delivering provider
 * events to the IW CM.  Returns either 0 indicating the event was processed
 * or -errno if the event could not be processed.
 *
 * @cm_id: The IW CM identifier associated with the event.
 * @event: Pointer to the event structure.
 */
typedef int (*iw_event_handler)(struct iw_cm_id *cm_id,
				 struct iw_cm_event *event);

struct iw_cm_id {
	iw_cm_handler		cm_handler;      /* client callback function */
	void		        *context;	 /* client cb context */
	struct ib_device	*device;
	struct sockaddr_in      local_addr;
	struct sockaddr_in	remote_addr;
	void			*provider_data;	 /* provider private data */
	iw_event_handler        event_handler;   /* cb for provider
						    events */
	/* Used by provider to add and remove refs on IW cm_id */
	void (*add_ref)(struct iw_cm_id *);
	void (*rem_ref)(struct iw_cm_id *);
};

struct iw_cm_conn_param {
	const void *private_data;
	u16 private_data_len;
	u32 ord;
	u32 ird;
	u32 qpn;
};

struct iw_cm_verbs {
	void		(*add_ref)(struct ib_qp *qp);

	void		(*rem_ref)(struct ib_qp *qp);

	struct ib_qp *	(*get_qp)(struct ib_device *device,
				  int qpn);

	int		(*connect)(struct iw_cm_id *cm_id,
				   struct iw_cm_conn_param *conn_param);

	int		(*accept)(struct iw_cm_id *cm_id,
				  struct iw_cm_conn_param *conn_param);

	int		(*reject)(struct iw_cm_id *cm_id,
				  const void *pdata, u8 pdata_len);

	int		(*create_listen)(struct iw_cm_id *cm_id,
					 int backlog);

	int		(*destroy_listen)(struct iw_cm_id *cm_id);
};

/**
 * iw_create_cm_id - Create an IW CM identifier.
 *
 * @device: The IB device on which to create the IW CM identier.
 * @event_handler: User callback invoked to report events associated with the
 *   returned IW CM identifier.
 * @context: User specified context associated with the id.
 */
struct iw_cm_id *iw_create_cm_id(struct ib_device *device,
				 iw_cm_handler cm_handler, void *context);

/**
 * iw_destroy_cm_id - Destroy an IW CM identifier.
 *
 * @cm_id: The previously created IW CM identifier to destroy.
 *
 * The client can assume that no events will be delivered for the CM ID after
 * this function returns.
 */
void iw_destroy_cm_id(struct iw_cm_id *cm_id);

/**
 * iw_cm_bind_qp - Unbind the specified IW CM identifier and QP
 *
 * @cm_id: The IW CM idenfier to unbind from the QP.
 * @qp: The QP
 *
 * This is called by the provider when destroying the QP to ensure
 * that any references held by the IWCM are released. It may also
 * be called by the IWCM when destroying a CM_ID to that any
 * references held by the provider are released.
 */
void iw_cm_unbind_qp(struct iw_cm_id *cm_id, struct ib_qp *qp);

/**
 * iw_cm_get_qp - Return the ib_qp associated with a QPN
 *
 * @ib_device: The IB device
 * @qpn: The queue pair number
 */
struct ib_qp *iw_cm_get_qp(struct ib_device *device, int qpn);

/**
 * iw_cm_listen - Listen for incoming connection requests on the
 * specified IW CM id.
 *
 * @cm_id: The IW CM identifier.
 * @backlog: The maximum number of outstanding un-accepted inbound listen
 *   requests to queue.
 *
 * The source address and port number are specified in the IW CM identifier
 * structure.
 */
int iw_cm_listen(struct iw_cm_id *cm_id, int backlog);

/**
 * iw_cm_accept - Called to accept an incoming connect request.
 *
 * @cm_id: The IW CM identifier associated with the connection request.
 * @iw_param: Pointer to a structure containing connection establishment
 *   parameters.
 *
 * The specified cm_id will have been provided in the event data for a
 * CONNECT_REQUEST event. Subsequent events related to this connection will be
 * delivered to the specified IW CM identifier prior and may occur prior to
 * the return of this function. If this function returns a non-zero value, the
 * client can assume that no events will be delivered to the specified IW CM
 * identifier.
 */
int iw_cm_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *iw_param);

/**
 * iw_cm_reject - Reject an incoming connection request.
 *
 * @cm_id: Connection identifier associated with the request.
 * @private_daa: Pointer to data to deliver to the remote peer as part of the
 *   reject message.
 * @private_data_len: The number of bytes in the private_data parameter.
 *
 * The client can assume that no events will be delivered to the specified IW
 * CM identifier following the return of this function. The private_data
 * buffer is available for reuse when this function returns.
 */
int iw_cm_reject(struct iw_cm_id *cm_id, const void *private_data,
		 u8 private_data_len);

/**
 * iw_cm_connect - Called to request a connection to a remote peer.
 *
 * @cm_id: The IW CM identifier for the connection.
 * @iw_param: Pointer to a structure containing connection  establishment
 *   parameters.
 *
 * Events may be delivered to the specified IW CM identifier prior to the
 * return of this function. If this function returns a non-zero value, the
 * client can assume that no events will be delivered to the specified IW CM
 * identifier.
 */
int iw_cm_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *iw_param);

/**
 * iw_cm_disconnect - Close the specified connection.
 *
 * @cm_id: The IW CM identifier to close.
 * @abrupt: If 0, the connection will be closed gracefully, otherwise, the
 *   connection will be reset.
 *
 * The IW CM identifier is still active until the IW_CM_EVENT_CLOSE event is
 * delivered.
 */
int iw_cm_disconnect(struct iw_cm_id *cm_id, int abrupt);

/**
 * iw_cm_init_qp_attr - Called to initialize the attributes of the QP
 * associated with a IW CM identifier.
 *
 * @cm_id: The IW CM identifier associated with the QP
 * @qp_attr: Pointer to the QP attributes structure.
 * @qp_attr_mask: Pointer to a bit vector specifying which QP attributes are
 *   valid.
 */
int iw_cm_init_qp_attr(struct iw_cm_id *cm_id, struct ib_qp_attr *qp_attr,
		       int *qp_attr_mask);

#endif /* IW_CM_H */
