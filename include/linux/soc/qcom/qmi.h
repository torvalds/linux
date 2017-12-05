// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017, Linaro Ltd.
 */
#ifndef __QMI_HELPERS_H__
#define __QMI_HELPERS_H__

#include <linux/types.h>

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

void *qmi_encode_message(int type, unsigned int msg_id, size_t *len,
			 unsigned int txn_id, struct qmi_elem_info *ei,
			 const void *c_struct);

int qmi_decode_message(const void *buf, size_t len,
		       struct qmi_elem_info *ei, void *c_struct);

#endif
