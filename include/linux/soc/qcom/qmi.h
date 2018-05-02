// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017, Linaro Ltd.
 */
#ifndef __QMI_HELPERS_H__
#define __QMI_HELPERS_H__

#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/qrtr.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct socket;

/**
 * qmi_header - wireformat header of QMI messages
 * @type:	type of message
 * @txn_id:	transaction id
 * @msg_id:	message id
 * @msg_len:	length of message payload following header
 */
struct qmi_header {
	u8 type;
	u16 txn_id;
	u16 msg_id;
	u16 msg_len;
} __packed;

#define QMI_REQUEST	0
#define QMI_RESPONSE	2
#define QMI_INDICATION	4

#define QMI_COMMON_TLV_TYPE 0

enum qmi_elem_type {
	QMI_EOTI,
	QMI_OPT_FLAG,
	QMI_DATA_LEN,
	QMI_UNSIGNED_1_BYTE,
	QMI_UNSIGNED_2_BYTE,
	QMI_UNSIGNED_4_BYTE,
	QMI_UNSIGNED_8_BYTE,
	QMI_SIGNED_2_BYTE_ENUM,
	QMI_SIGNED_4_BYTE_ENUM,
	QMI_STRUCT,
	QMI_STRING,
};

enum qmi_array_type {
	NO_ARRAY,
	STATIC_ARRAY,
	VAR_LEN_ARRAY,
};

/**
 * struct qmi_elem_info - describes how to encode a single QMI element
 * @data_type:	Data type of this element.
 * @elem_len:	Array length of this element, if an array.
 * @elem_size:	Size of a single instance of this data type.
 * @array_type:	Array type of this element.
 * @tlv_type:	QMI message specific type to identify which element
 *		is present in an incoming message.
 * @offset:	Specifies the offset of the first instance of this
 *		element in the data structure.
 * @ei_array:	Null-terminated array of @qmi_elem_info to describe nested
 *		structures.
 */
struct qmi_elem_info {
	enum qmi_elem_type data_type;
	u32 elem_len;
	u32 elem_size;
	enum qmi_array_type array_type;
	u8 tlv_type;
	u32 offset;
	struct qmi_elem_info *ei_array;
};

#define QMI_RESULT_SUCCESS_V01			0
#define QMI_RESULT_FAILURE_V01			1

#define QMI_ERR_NONE_V01			0
#define QMI_ERR_MALFORMED_MSG_V01		1
#define QMI_ERR_NO_MEMORY_V01			2
#define QMI_ERR_INTERNAL_V01			3
#define QMI_ERR_CLIENT_IDS_EXHAUSTED_V01	5
#define QMI_ERR_INVALID_ID_V01			41
#define QMI_ERR_ENCODING_V01			58
#define QMI_ERR_INCOMPATIBLE_STATE_V01		90
#define QMI_ERR_NOT_SUPPORTED_V01		94

/**
 * qmi_response_type_v01 - common response header (decoded)
 * @result:	result of the transaction
 * @error:	error value, when @result is QMI_RESULT_FAILURE_V01
 */
struct qmi_response_type_v01 {
	u16 result;
	u16 error;
};

extern struct qmi_elem_info qmi_response_type_v01_ei[];

/**
 * struct qmi_service - context to track lookup-results
 * @service:	service type
 * @version:	version of the @service
 * @instance:	instance id of the @service
 * @node:	node of the service
 * @port:	port of the service
 * @priv:	handle for client's use
 * @list_node:	list_head for house keeping
 */
struct qmi_service {
	unsigned int service;
	unsigned int version;
	unsigned int instance;

	unsigned int node;
	unsigned int port;

	void *priv;
	struct list_head list_node;
};

struct qmi_handle;

/**
 * struct qmi_ops - callbacks for qmi_handle
 * @new_server:		inform client of a new_server lookup-result, returning
 *                      successfully from this call causes the library to call
 *                      @del_server as the service is removed from the
 *                      lookup-result. @priv of the qmi_service can be used by
 *                      the client
 * @del_server:		inform client of a del_server lookup-result
 * @net_reset:		inform client that the name service was restarted and
 *                      that and any state needs to be released
 * @msg_handler:	invoked for incoming messages, allows a client to
 *                      override the usual QMI message handler
 * @bye:                inform a client that all clients from a node are gone
 * @del_client:         inform a client that a particular client is gone
 */
struct qmi_ops {
	int (*new_server)(struct qmi_handle *qmi, struct qmi_service *svc);
	void (*del_server)(struct qmi_handle *qmi, struct qmi_service *svc);
	void (*net_reset)(struct qmi_handle *qmi);
	void (*msg_handler)(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			    const void *data, size_t count);
	void (*bye)(struct qmi_handle *qmi, unsigned int node);
	void (*del_client)(struct qmi_handle *qmi,
			   unsigned int node, unsigned int port);
};

/**
 * struct qmi_txn - transaction context
 * @qmi:	QMI handle this transaction is associated with
 * @id:		transaction id
 * @lock:	for synchronization between handler and waiter of messages
 * @completion:	completion object as the transaction receives a response
 * @result:	result code for the completed transaction
 * @ei:		description of the QMI encoded response (optional)
 * @dest:	destination buffer to decode message into (optional)
 */
struct qmi_txn {
	struct qmi_handle *qmi;

	int id;

	struct mutex lock;
	struct completion completion;
	int result;

	struct qmi_elem_info *ei;
	void *dest;
};

/**
 * struct qmi_msg_handler - description of QMI message handler
 * @type:	type of message
 * @msg_id:	message id
 * @ei:		description of the QMI encoded message
 * @decoded_size:	size of the decoded object
 * @fn:		function to invoke as the message is decoded
 */
struct qmi_msg_handler {
	unsigned int type;
	unsigned int msg_id;

	struct qmi_elem_info *ei;

	size_t decoded_size;
	void (*fn)(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
		   struct qmi_txn *txn, const void *decoded);
};

/**
 * struct qmi_handle - QMI context
 * @sock:	socket handle
 * @sock_lock:	synchronization of @sock modifications
 * @sq:		sockaddr of @sock
 * @work:	work for handling incoming messages
 * @wq:		workqueue to post @work on
 * @recv_buf:	scratch buffer for handling incoming messages
 * @recv_buf_size:	size of @recv_buf
 * @lookups:		list of registered lookup requests
 * @lookup_results:	list of lookup-results advertised to the client
 * @services:		list of registered services (by this client)
 * @ops:	reference to callbacks
 * @txns:	outstanding transactions
 * @txn_lock:	lock for modifications of @txns
 * @handlers:	list of handlers for incoming messages
 */
struct qmi_handle {
	struct socket *sock;
	struct mutex sock_lock;

	struct sockaddr_qrtr sq;

	struct work_struct work;
	struct workqueue_struct *wq;

	void *recv_buf;
	size_t recv_buf_size;

	struct list_head lookups;
	struct list_head lookup_results;
	struct list_head services;

	struct qmi_ops ops;

	struct idr txns;
	struct mutex txn_lock;

	const struct qmi_msg_handler *handlers;
};

int qmi_add_lookup(struct qmi_handle *qmi, unsigned int service,
		   unsigned int version, unsigned int instance);
int qmi_add_server(struct qmi_handle *qmi, unsigned int service,
		   unsigned int version, unsigned int instance);

int qmi_handle_init(struct qmi_handle *qmi, size_t max_msg_len,
		    const struct qmi_ops *ops,
		    const struct qmi_msg_handler *handlers);
void qmi_handle_release(struct qmi_handle *qmi);

ssize_t qmi_send_request(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			 struct qmi_txn *txn, int msg_id, size_t len,
			 struct qmi_elem_info *ei, const void *c_struct);
ssize_t qmi_send_response(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			  struct qmi_txn *txn, int msg_id, size_t len,
			  struct qmi_elem_info *ei, const void *c_struct);
ssize_t qmi_send_indication(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			    int msg_id, size_t len, struct qmi_elem_info *ei,
			    const void *c_struct);

void *qmi_encode_message(int type, unsigned int msg_id, size_t *len,
			 unsigned int txn_id, struct qmi_elem_info *ei,
			 const void *c_struct);

int qmi_decode_message(const void *buf, size_t len,
		       struct qmi_elem_info *ei, void *c_struct);

int qmi_txn_init(struct qmi_handle *qmi, struct qmi_txn *txn,
		 struct qmi_elem_info *ei, void *c_struct);
int qmi_txn_wait(struct qmi_txn *txn, unsigned long timeout);
void qmi_txn_cancel(struct qmi_txn *txn);

#endif
