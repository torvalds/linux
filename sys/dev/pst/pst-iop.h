/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001,2002,2003 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* misc defines */
MALLOC_DECLARE(M_PSTIOP);
#define I2O_IOP_OUTBOUND_FRAME_COUNT	32
#define I2O_IOP_OUTBOUND_FRAME_SIZE	0x20

/* structure defs */
struct out_mfa_buf {
    u_int32_t	buf[I2O_IOP_OUTBOUND_FRAME_SIZE];
};

struct iop_softc {
    struct resource		*r_mem;
    struct resource		*r_irq;
    caddr_t			ibase;
    caddr_t			obase;
    u_int32_t			phys_obase;
    struct i2o_registers	*reg;
    struct i2o_status_get_reply	*status;
    int				lct_count;
    struct i2o_lct_entry	*lct;
    int				ism;
    device_t			dev;
    struct mtx			mtx;
    int				outstanding;
    void			*handle;
    struct intr_config_hook	*iop_delayed_attach;
};

/* structure at start of IOP shared mem */
struct i2o_registers {
    volatile u_int32_t	apic_select;
    volatile u_int32_t	reserved0;
    volatile u_int32_t	apic_winreg;
    volatile u_int32_t	reserved1;
    volatile u_int32_t	iqueue_reg0; 
    volatile u_int32_t	iqueue_reg1;
    volatile u_int32_t	oqueue_reg0;
    volatile u_int32_t	oqueue_reg1;
    volatile u_int32_t	iqueue_event;
    volatile u_int32_t	iqueue_intr_status;
    volatile u_int32_t	iqueue_intr_mask;
    volatile u_int32_t	oqueue_event;
    volatile u_int32_t	oqueue_intr_status;
    volatile u_int32_t	oqueue_intr_mask;
#define I2O_OUT_INTR_QUEUE				0x08
#define I2O_OUT_INTR_BELL				0x04
#define I2O_OUT_INTR_MSG1				0x02
#define I2O_OUT_INTR_MSG0				0x01

    volatile u_int64_t	reserved2;
    volatile u_int32_t	iqueue;
    volatile u_int32_t	oqueue;
    volatile u_int64_t	reserved3;
    volatile u_int64_t	mac_addr;
    volatile u_int32_t	ip_addr;
    volatile u_int32_t	ip_mask;
};

/* Scatter/Gather List management  */
struct i2o_sgl {
    u_int32_t		count:24;
#define I2O_SGL_CNT_MASK				0xffffff

    u_int32_t		flags:8;
#define I2O_SGL_SIMPLE					0x10
#define I2O_SGL_PAGELIST				0x20
#define I2O_SGL_CHAIN					0x30
#define I2O_SGL_ATTRIBUTE				0x7c
#define I2O_SGL_BC0					0x01
#define I2O_SGL_BC1					0x02
#define I2O_SGL_DIR					0x04
#define I2O_SGL_LA					0x08
#define I2O_SGL_EOB					0x40
#define I2O_SGL_END					0x80

    u_int32_t		phys_addr[1];
} __packed;

#define I2O_SGL_MAX_SEGS	((I2O_IOP_OUTBOUND_FRAME_SIZE - (8 + 2)) + 1)

/* i2o command codes */
#define I2O_UTIL_NOP					0x00
#define I2O_UTIL_PARAMS_GET				0x06
#define I2O_UTIL_CLAIM					0x09
#define I2O_UTIL_CONFIG_DIALOG				0x10
#define I2O_UTIL_EVENT_REGISTER				0x13
#define I2O_BSA_BLOCK_READ				0x30
#define I2O_BSA_BLOCK_WRITE				0x31
#define I2O_BSA_CACHE_FLUSH				0x37
#define I2O_EXEC_STATUS_GET				0xa0
#define I2O_EXEC_OUTBOUND_INIT				0xa1
#define I2O_EXEC_LCT_NOTIFY				0xa2
#define I2O_EXEC_SYSTAB_SET				0xa3
#define I2O_EXEC_IOP_RESET				0xbd
#define I2O_EXEC_SYS_ENABLE				0xd1
#define I2O_PRIVATE_MESSAGE				0xff

/* basic message layout */
struct i2o_basic_message {
    u_int8_t		version:4;
    u_int8_t		offset:4;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
} __packed;

/* basic reply layout */
struct i2o_single_reply {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
#define I2O_MESSAGE_FLAGS_STATIC			0x01
#define I2O_MESSAGE_FLAGS_64BIT				0x02
#define I2O_MESSAGE_FLAGS_MULTIPLE			0x10
#define I2O_MESSAGE_FLAGS_FAIL				0x20
#define I2O_MESSAGE_FLAGS_LAST				0x40
#define I2O_MESSAGE_FLAGS_REPLY				0x80

    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int16_t		detailed_status;
#define I2O_DETAIL_STATUS_SUCCESS			0x0000
#define I2O_DETAIL_STATUS_BAD_KEY			0x0002
#define I2O_DETAIL_STATUS_TCL_ERROR			0x0003
#define I2O_DETAIL_STATUS_REPLY_BUFFER_FULL		0x0004
#define I2O_DETAIL_STATUS_NO_SUCH_PAGE			0x0005
#define I2O_DETAIL_STATUS_INSUFFICIENT_RESOURCE_SOFT	0x0006
#define I2O_DETAIL_STATUS_INSUFFICIENT_RESOURCE_HARD	0x0007
#define I2O_DETAIL_STATUS_CHAIN_BUFFER_TOO_LARGE	0x0009
#define I2O_DETAIL_STATUS_UNSUPPORTED_FUNCTION		0x000a
#define I2O_DETAIL_STATUS_DEVICE_LOCKED			0x000b
#define I2O_DETAIL_STATUS_DEVICE_RESET			0x000c
#define I2O_DETAIL_STATUS_INAPPROPRIATE_FUNCTION	0x000d
#define I2O_DETAIL_STATUS_INVALID_INITIATOR_ADDRESS	0x000e
#define I2O_DETAIL_STATUS_INVALID_MESSAGE_FLAGS		0x000f
#define I2O_DETAIL_STATUS_INVALID_OFFSET		0x0010
#define I2O_DETAIL_STATUS_INVALID_PARAMETER		0x0011
#define I2O_DETAIL_STATUS_INVALID_REQUEST		0x0012
#define I2O_DETAIL_STATUS_INVALID_TARGET_ADDRESS	0x0013
#define I2O_DETAIL_STATUS_MESSAGE_TOO_LARGE		0x0014
#define I2O_DETAIL_STATUS_MESSAGE_TOO_SMALL		0x0015
#define I2O_DETAIL_STATUS_MISSING_PARAMETER		0x0016
#define I2O_DETAIL_STATUS_TIMEOUT			0x0017
#define I2O_DETAIL_STATUS_UNKNOWN_ERROR			0x0018
#define I2O_DETAIL_STATUS_UNKNOWN_FUNCTION		0x0019
#define I2O_DETAIL_STATUS_UNSUPPORTED_VERSION		0x001a
#define I2O_DETAIL_STATUS_DEVICE_BUSY			0x001b
#define I2O_DETAIL_STATUS_DEVICE_NOT_AVAILABLE		0x001c

    u_int8_t		retry_count;
    u_int8_t		status;
#define I2O_REPLY_STATUS_SUCCESS			0x00
#define I2O_REPLY_STATUS_ABORT_DIRTY			0x01
#define I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER		0x02
#define I2O_REPLY_STATUS_ABORT_PARTIAL_TRANSFER		0x03
#define I2O_REPLY_STATUS_ERROR_DIRTY			0x04
#define I2O_REPLY_STATUS_ERROR_NO_DATA_TRANSFER		0x05
#define I2O_REPLY_STATUS_ERROR_PARTIAL_TRANSFER		0x06
#define I2O_REPLY_STATUS_PROCESS_ABORT_DIRTY		0x08
#define I2O_REPLY_STATUS_PROCESS_ABORT_NO_DATA_TRANSFER 0x09
#define I2O_REPLY_STATUS_PROCESS_ABORT_PARTIAL_TRANSFER 0x0a
#define I2O_REPLY_STATUS_TRANSACTION_ERROR		0x0b
#define I2O_REPLY_STATUS_PROGRESS_REPORT		0x80

    u_int32_t		donecount;
} __packed;

struct i2o_fault_reply {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int8_t		lowest_version;
    u_int8_t		highest_version;
    u_int8_t		severity;
#define I2O_SEVERITY_FORMAT_ERROR			0x01
#define I2O_SEVERITY_PATH_ERROR				0x02
#define I2O_SEVERITY_PATH_STATE				0x04
#define I2O_SEVERITY_CONGESTION				0x08

    u_int8_t		failure_code;
#define I2O_FAILURE_CODE_TRANSPORT_SERVICE_SUSPENDED	0x81
#define I2O_FAILURE_CODE_TRANSPORT_SERVICE_TERMINATED	0x82
#define I2O_FAILURE_CODE_TRANSPORT_CONGESTION		0x83
#define I2O_FAILURE_CODE_TRANSPORT_FAIL			0x84
#define I2O_FAILURE_CODE_TRANSPORT_STATE_ERROR		0x85
#define I2O_FAILURE_CODE_TRANSPORT_TIME_OUT		0x86
#define I2O_FAILURE_CODE_TRANSPORT_ROUTING_FAILURE	0x87
#define I2O_FAILURE_CODE_TRANSPORT_INVALID_VERSION	0x88
#define I2O_FAILURE_CODE_TRANSPORT_INVALID_OFFSET	0x89
#define I2O_FAILURE_CODE_TRANSPORT_INVALID_MSG_FLAGS	0x8A
#define I2O_FAILURE_CODE_TRANSPORT_FRAME_TOO_SMALL	0x8B
#define I2O_FAILURE_CODE_TRANSPORT_FRAME_TOO_LARGE	0x8C
#define I2O_FAILURE_CODE_TRANSPORT_INVALID_TARGET_ID	0x8D
#define I2O_FAILURE_CODE_TRANSPORT_INVALID_INITIATOR_ID 0x8E
#define I2O_FAILURE_CODE_TRANSPORT_INVALID_INITIATOR_CONTEXT	0x8F
#define I2O_FAILURE_CODE_TRANSPORT_UNKNOWN_FAILURE	0xFF

    u_int32_t		failing_iop_id:12;
    u_int32_t		reserved:4;
    u_int32_t		failing_host_unit_id:16;
    u_int32_t		age_limit;
    u_int64_t		preserved_mfa;
} __packed;

struct i2o_exec_iop_reset_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int8_t		reserved[16];
    u_int32_t		status_word_low_addr;
    u_int32_t		status_word_high_addr;
} __packed;

struct i2o_exec_status_get_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int8_t		reserved[16];
    u_int32_t		reply_buf_low_addr;
    u_int32_t		reply_buf_high_addr;
    u_int32_t		reply_buf_length;
} __packed;

struct i2o_status_get_reply {
    u_int16_t		organization_id;
    u_int16_t		reserved;
    u_int32_t		iop_id:12;
    u_int32_t		reserved1:4;
    u_int32_t		host_unit_id:16;
    u_int32_t		segment_number:12;
    u_int32_t		i2o_version:4;
    u_int32_t		iop_state:8;
#define I2O_IOP_STATE_INITIALIZING			0x01
#define I2O_IOP_STATE_RESET				0x02
#define I2O_IOP_STATE_HOLD				0x04
#define I2O_IOP_STATE_READY				0x05
#define I2O_IOP_STATE_OPERATIONAL			0x08
#define I2O_IOP_STATE_FAILED				0x10
#define I2O_IOP_STATE_FAULTED				0x11

    u_int32_t		messenger_type:8;
    u_int16_t		inbound_mframe_size;
    u_int8_t		init_code;
    u_int8_t		reserved2;
    u_int32_t		max_inbound_mframes;
    u_int32_t		current_ibound_mframes;
    u_int32_t		max_outbound_mframes;
    u_int8_t		product_idstring[24];
    u_int32_t		expected_lct_size;
    u_int32_t		iop_capabilities;
    u_int32_t		desired_private_memsize;
    u_int32_t		current_private_memsize;
    u_int32_t		current_private_membase;
    u_int32_t		desired_private_iosize;
    u_int32_t		current_private_iosize;
    u_int32_t		current_private_iobase;
    u_int8_t		reserved3[3];
    u_int8_t		sync_byte;
} __packed;

struct i2o_exec_init_outqueue_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int32_t		host_pagesize;
    u_int8_t		init_code;
    u_int8_t		reserved;
    u_int16_t		queue_framesize;
    struct i2o_sgl	sgl[2];
} __packed;

#define I2O_EXEC_OUTBOUND_INIT_IN_PROGRESS		0x01
#define I2O_EXEC_OUTBOUND_INIT_REJECTED			0x02
#define I2O_EXEC_OUTBOUND_INIT_FAILED			0x03
#define I2O_EXEC_OUTBOUND_INIT_COMPLETE			0x04

struct i2o_exec_systab_set_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int32_t		iop_id:12;
#define I2O_EXEC_SYS_TAB_IOP_ID_LOCAL_IOP		0x000	  
#define I2O_EXEC_SYS_TAB_IOP_ID_LOCAL_HOST		0x001
#define I2O_EXEC_SYS_TAB_IOP_ID_UNKNOWN_IOP		0xfff  

    u_int32_t		reserved1:4;
    u_int32_t		host_unit_id:16;
#define I2O_EXEC_SYS_TAB_HOST_UNIT_ID_LOCAL_UNIT	0x0000 
#define I2O_EXEC_SYS_TAB_HOST_UNIT_ID_UNKNOWN_UNIT	0xffff

    u_int32_t		segment_number:12;
#define I2O_EXEC_SYS_TAB_SEG_NUMBER_LOCAL_SEGMENT	0x000
#define I2O_EXEC_SYS_TAB_SEG_NUMBER_UNKNOWN_SEGMENT	0xfff

    u_int32_t		reserved2:4;
    u_int32_t		reserved3:8;
    struct i2o_sgl	sgl[3];
} __packed;

struct i2o_exec_systab {
    u_int8_t		entries;
    u_int8_t		version;
#define	   I2O_RESOURCE_MANAGER_VERSION			0

    u_int16_t		reserved1;
    u_int32_t		change_id;
    u_int64_t		reserved2;
    u_int16_t		organization_id;
    u_int16_t		reserved3;
    u_int32_t		iop_id:12;
    u_int32_t		reserved4:20;
    u_int32_t		segment_number:12;
    u_int32_t		i2o_version:4;
    u_int32_t		iop_state:8;
    u_int32_t		messenger_type:8;
    u_int16_t		inbound_mframe_size;
    u_int16_t		reserved5;
    u_int32_t		last_changed;
    u_int32_t		iop_capabilities;
    u_int64_t		messenger_info;
} __packed;

struct i2o_exec_get_lct_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int32_t		class;
    u_int32_t		last_change_id;
    struct i2o_sgl	sgl;
} __packed;

#define I2O_TID_IOP					0x000
#define I2O_TID_HOST					0x001
#define I2O_TID_NONE					0xfff

struct i2o_lct_entry {
    u_int32_t		entry_size:16;
    u_int32_t		local_tid:12;
    u_int32_t		reserved:4;
    u_int32_t		change_id;
    u_int32_t		device_flags;
    u_int32_t		class:12;
#define I2O_CLASS_EXECUTIVE				0x000
#define I2O_CLASS_DDM					0x001
#define I2O_CLASS_RANDOM_BLOCK_STORAGE			0x010
#define I2O_CLASS_SEQUENTIAL_STORAGE			0x011
#define I2O_CLASS_LAN					0x020
#define I2O_CLASS_WAN					0x030
#define I2O_CLASS_FIBRE_CHANNEL_PORT			0x040
#define I2O_CLASS_FIBRE_CHANNEL_PERIPHERAL		0x041
#define I2O_CLASS_SCSI_PERIPHERAL			0x051
#define I2O_CLASS_ATE_PORT				0x060
#define I2O_CLASS_ATE_PERIPHERAL			0x061
#define I2O_CLASS_FLOPPY_CONTROLLER			0x070
#define I2O_CLASS_FLOPPY_DEVICE				0x071
#define I2O_CLASS_BUS_ADAPTER_PORT			0x080
#define I2O_CLASS_MATCH_ANYCLASS			0xffffffff

    u_int32_t		class_version:4;
    u_int32_t		class_org:16;
    u_int32_t		sub_class;
#define I2O_SUBCLASS_i960				0x001
#define I2O_SUBCLASS_HDM				0x020
#define I2O_SUBCLASS_ISM				0x021

    u_int32_t		user_tid:12;
    u_int32_t		parent_tid:12;
    u_int32_t		bios_info:8;
    u_int8_t		identity_tag[8];
    u_int32_t		event_capabilities;
} __packed;

#define I2O_LCT_ENTRYSIZE (sizeof(struct i2o_lct_entry)/sizeof(u_int32_t))

struct i2o_get_lct_reply {
    u_int32_t		table_size:16;
    u_int32_t		boot_device:12;
    u_int32_t		lct_version:4;
    u_int32_t		iop_flags;
    u_int32_t		current_change_id;
    struct i2o_lct_entry entry[1];
} __packed;

struct i2o_util_get_param_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int32_t		operation_flags;
    struct i2o_sgl	sgl[2];
} __packed;

struct i2o_get_param_template {
    u_int16_t		operation;
#define I2O_PARAMS_OPERATION_FIELD_GET			0x0001
#define I2O_PARAMS_OPERATION_LIST_GET			0x0002
#define I2O_PARAMS_OPERATION_MORE_GET			0x0003
#define I2O_PARAMS_OPERATION_SIZE_GET			0x0004
#define I2O_PARAMS_OPERATION_TABLE_GET			0x0005
#define I2O_PARAMS_OPERATION_FIELD_SET			0x0006
#define I2O_PARAMS_OPERATION_LIST_SET			0x0007
#define I2O_PARAMS_OPERATION_ROW_ADD			0x0008
#define I2O_PARAMS_OPERATION_ROW_DELETE			0x0009
#define I2O_PARAMS_OPERATION_TABLE_CLEAR		0x000A

    u_int16_t		group;
#define I2O_BSA_DEVICE_INFO_GROUP_NO			0x0000
#define I2O_BSA_OPERATIONAL_CONTROL_GROUP_NO		0x0001
#define I2O_BSA_POWER_CONTROL_GROUP_NO			0x0002
#define I2O_BSA_CACHE_CONTROL_GROUP_NO			0x0003
#define I2O_BSA_MEDIA_INFO_GROUP_NO			0x0004
#define I2O_BSA_ERROR_LOG_GROUP_NO			0x0005

#define I2O_UTIL_PARAMS_DESCRIPTOR_GROUP_NO		0xF000
#define I2O_UTIL_PHYSICAL_DEVICE_TABLE_GROUP_NO		0xF001
#define I2O_UTIL_CLAIMED_TABLE_GROUP_NO			0xF002
#define I2O_UTIL_USER_TABLE_GROUP_NO			0xF003
#define I2O_UTIL_PRIVATE_MESSAGE_EXTENSIONS_GROUP_NO	0xF005
#define I2O_UTIL_AUTHORIZED_USER_TABLE_GROUP_NO		0xF006
#define I2O_UTIL_DEVICE_IDENTITY_GROUP_NO		0xF100
#define I2O_UTIL_DDM_IDENTITY_GROUP_NO			0xF101
#define I2O_UTIL_USER_INFORMATION_GROUP_NO		0xF102
#define I2O_UTIL_SGL_OPERATING_LIMITS_GROUP_NO		0xF103
#define I2O_UTIL_SENSORS_GROUP_NO			0xF200

    u_int16_t		field_count;
    u_int16_t		pad;
} __packed;

struct i2o_get_param_operation {
    u_int16_t		operation_count;
    u_int16_t		reserved;
    struct i2o_get_param_template operation[1];
} __packed;
    
struct i2o_get_param_reply {
    u_int16_t		result_count;
    u_int16_t		reserved;
    u_int16_t		block_size;
    u_int8_t		block_status;
    u_int8_t		error_info_size;
    u_int32_t		result[1];
} __packed;

struct i2o_device_identity {
    u_int32_t		class;
    u_int16_t		owner;
    u_int16_t		parent;
    u_int8_t		vendor[16];
    u_int8_t		product[16];
    u_int8_t		description[16];
    u_int8_t		revision[8];
    u_int8_t		sn_format;
    u_int8_t		serial[256];
} __packed;

struct i2o_bsa_device {
    u_int8_t		device_type;
    u_int8_t		path_count;
    u_int16_t		power_state;
    u_int32_t		block_size;
    u_int64_t		capacity;
    u_int32_t		capabilities;
    u_int32_t		state;
} __packed;

struct i2o_util_claim_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int16_t		claim_flags;
    u_int8_t		reserved;
    u_int8_t		claim_type;
} __packed;

struct i2o_util_event_register_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int32_t		event_mask;
} __packed;

struct i2o_util_event_reply_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int32_t		event_mask;
    u_int32_t		event_data[1];
} __packed;

struct i2o_util_config_dialog_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int32_t		page_number;
    struct i2o_sgl	sgl[2];
} __packed;

struct i2o_private_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int16_t		function_code;
    u_int16_t		organization_id;
    struct i2o_sgl	in_sgl;
    struct i2o_sgl	out_sgl;
} __packed;

struct i2o_bsa_rw_block_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int16_t		control_flags;
    u_int8_t		time_multiplier;
    u_int8_t		fetch_ahead;
    u_int32_t		bytecount;
    u_int64_t		lba;
    struct i2o_sgl	sgl;
} __packed;

struct i2o_bsa_cache_flush_message {
    u_int8_t		version_offset;
    u_int8_t		message_flags;
    u_int16_t		message_size;
    u_int32_t		target_address:12;
    u_int32_t		initiator_address:12;
    u_int32_t		function:8;
    u_int32_t		initiator_context;
    u_int32_t		transaction_context;
    u_int16_t		control_flags;
    u_int8_t		time_multiplier;
    u_int8_t		reserved;
} __packed;

/* prototypes */
int iop_init(struct iop_softc *);
void iop_attach(void *);
void iop_intr(void *);
int iop_reset(struct iop_softc *);
int iop_init_outqueue(struct iop_softc *);
int iop_get_lct(struct iop_softc *);
struct i2o_get_param_reply *iop_get_util_params(struct iop_softc *,int,int,int);
u_int32_t iop_get_mfa(struct iop_softc *);
void iop_free_mfa(struct iop_softc *, int);
int iop_queue_wait_msg(struct iop_softc *, int, struct i2o_basic_message *);
int iop_create_sgl(struct i2o_basic_message *, caddr_t, int, int); 

/* global prototypes */
int pst_add_raid(struct iop_softc *, struct i2o_lct_entry *);
