#ifndef __USB_UAS_H__
#define __USB_UAS_H__

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

/* Common header for all IUs */
struct iu {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
};

enum {
	IU_ID_COMMAND		= 0x01,
	IU_ID_STATUS		= 0x03,
	IU_ID_RESPONSE		= 0x04,
	IU_ID_TASK_MGMT		= 0x05,
	IU_ID_READ_READY	= 0x06,
	IU_ID_WRITE_READY	= 0x07,
};

enum {
	TMF_ABORT_TASK          = 0x01,
	TMF_ABORT_TASK_SET      = 0x02,
	TMF_CLEAR_TASK_SET      = 0x04,
	TMF_LOGICAL_UNIT_RESET  = 0x08,
	TMF_I_T_NEXUS_RESET     = 0x10,
	TMF_CLEAR_ACA           = 0x40,
	TMF_QUERY_TASK          = 0x80,
	TMF_QUERY_TASK_SET      = 0x81,
	TMF_QUERY_ASYNC_EVENT   = 0x82,
};

enum {
	RC_TMF_COMPLETE         = 0x00,
	RC_INVALID_INFO_UNIT    = 0x02,
	RC_TMF_NOT_SUPPORTED    = 0x04,
	RC_TMF_FAILED           = 0x05,
	RC_TMF_SUCCEEDED        = 0x08,
	RC_INCORRECT_LUN        = 0x09,
	RC_OVERLAPPED_TAG       = 0x0a,
};

struct command_iu {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
	__u8 prio_attr;
	__u8 rsvd5;
	__u8 len;
	__u8 rsvd7;
	struct scsi_lun lun;
	__u8 cdb[16];	/* XXX: Overflow-checking tools may misunderstand */
};

struct task_mgmt_iu {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
	__u8 function;
	__u8 rsvd2;
	__be16 task_tag;
	struct scsi_lun lun;
};

/*
 * Also used for the Read Ready and Write Ready IUs since they have the
 * same first four bytes
 */
struct sense_iu {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
	__be16 status_qual;
	__u8 status;
	__u8 rsvd7[7];
	__be16 len;
	__u8 sense[SCSI_SENSE_BUFFERSIZE];
};

struct response_ui {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
	__be16 add_response_info;
	__u8 response_code;
};

struct usb_pipe_usage_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;

	__u8  bPipeID;
	__u8  Reserved;
} __attribute__((__packed__));

enum {
	CMD_PIPE_ID		= 1,
	STATUS_PIPE_ID		= 2,
	DATA_IN_PIPE_ID		= 3,
	DATA_OUT_PIPE_ID	= 4,

	UAS_SIMPLE_TAG		= 0,
	UAS_HEAD_TAG		= 1,
	UAS_ORDERED_TAG		= 2,
	UAS_ACA			= 4,
};
#endif
