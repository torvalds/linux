/*
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LINUX_VIRTIO_SCSI_H
#define _LINUX_VIRTIO_SCSI_H

#include <linux/virtio_types.h>

#define VIRTIO_SCSI_CDB_SIZE   32
#define VIRTIO_SCSI_SENSE_SIZE 96

/* SCSI command request, followed by data-out */
struct virtio_scsi_cmd_req {
	__u8 lun[8];		/* Logical Unit Number */
	__virtio64 tag;		/* Command identifier */
	__u8 task_attr;		/* Task attribute */
	__u8 prio;		/* SAM command priority field */
	__u8 crn;
	__u8 cdb[VIRTIO_SCSI_CDB_SIZE];
} __attribute__((packed));

/* SCSI command request, followed by protection information */
struct virtio_scsi_cmd_req_pi {
	__u8 lun[8];		/* Logical Unit Number */
	__virtio64 tag;		/* Command identifier */
	__u8 task_attr;		/* Task attribute */
	__u8 prio;		/* SAM command priority field */
	__u8 crn;
	__virtio32 pi_bytesout;	/* DataOUT PI Number of bytes */
	__virtio32 pi_bytesin;		/* DataIN PI Number of bytes */
	__u8 cdb[VIRTIO_SCSI_CDB_SIZE];
} __attribute__((packed));

/* Response, followed by sense data and data-in */
struct virtio_scsi_cmd_resp {
	__virtio32 sense_len;		/* Sense data length */
	__virtio32 resid;		/* Residual bytes in data buffer */
	__virtio16 status_qualifier;	/* Status qualifier */
	__u8 status;		/* Command completion status */
	__u8 response;		/* Response values */
	__u8 sense[VIRTIO_SCSI_SENSE_SIZE];
} __attribute__((packed));

/* Task Management Request */
struct virtio_scsi_ctrl_tmf_req {
	__virtio32 type;
	__virtio32 subtype;
	__u8 lun[8];
	__virtio64 tag;
} __attribute__((packed));

struct virtio_scsi_ctrl_tmf_resp {
	__u8 response;
} __attribute__((packed));

/* Asynchronous notification query/subscription */
struct virtio_scsi_ctrl_an_req {
	__virtio32 type;
	__u8 lun[8];
	__virtio32 event_requested;
} __attribute__((packed));

struct virtio_scsi_ctrl_an_resp {
	__virtio32 event_actual;
	__u8 response;
} __attribute__((packed));

struct virtio_scsi_event {
	__virtio32 event;
	__u8 lun[8];
	__virtio32 reason;
} __attribute__((packed));

struct virtio_scsi_config {
	__u32 num_queues;
	__u32 seg_max;
	__u32 max_sectors;
	__u32 cmd_per_lun;
	__u32 event_info_size;
	__u32 sense_size;
	__u32 cdb_size;
	__u16 max_channel;
	__u16 max_target;
	__u32 max_lun;
} __attribute__((packed));

/* Feature Bits */
#define VIRTIO_SCSI_F_INOUT                    0
#define VIRTIO_SCSI_F_HOTPLUG                  1
#define VIRTIO_SCSI_F_CHANGE                   2
#define VIRTIO_SCSI_F_T10_PI                   3

/* Response codes */
#define VIRTIO_SCSI_S_OK                       0
#define VIRTIO_SCSI_S_OVERRUN                  1
#define VIRTIO_SCSI_S_ABORTED                  2
#define VIRTIO_SCSI_S_BAD_TARGET               3
#define VIRTIO_SCSI_S_RESET                    4
#define VIRTIO_SCSI_S_BUSY                     5
#define VIRTIO_SCSI_S_TRANSPORT_FAILURE        6
#define VIRTIO_SCSI_S_TARGET_FAILURE           7
#define VIRTIO_SCSI_S_NEXUS_FAILURE            8
#define VIRTIO_SCSI_S_FAILURE                  9
#define VIRTIO_SCSI_S_FUNCTION_SUCCEEDED       10
#define VIRTIO_SCSI_S_FUNCTION_REJECTED        11
#define VIRTIO_SCSI_S_INCORRECT_LUN            12

/* Controlq type codes.  */
#define VIRTIO_SCSI_T_TMF                      0
#define VIRTIO_SCSI_T_AN_QUERY                 1
#define VIRTIO_SCSI_T_AN_SUBSCRIBE             2

/* Valid TMF subtypes.  */
#define VIRTIO_SCSI_T_TMF_ABORT_TASK           0
#define VIRTIO_SCSI_T_TMF_ABORT_TASK_SET       1
#define VIRTIO_SCSI_T_TMF_CLEAR_ACA            2
#define VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET       3
#define VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET      4
#define VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET   5
#define VIRTIO_SCSI_T_TMF_QUERY_TASK           6
#define VIRTIO_SCSI_T_TMF_QUERY_TASK_SET       7

/* Events.  */
#define VIRTIO_SCSI_T_EVENTS_MISSED            0x80000000
#define VIRTIO_SCSI_T_NO_EVENT                 0
#define VIRTIO_SCSI_T_TRANSPORT_RESET          1
#define VIRTIO_SCSI_T_ASYNC_NOTIFY             2
#define VIRTIO_SCSI_T_PARAM_CHANGE             3

/* Reasons of transport reset event */
#define VIRTIO_SCSI_EVT_RESET_HARD             0
#define VIRTIO_SCSI_EVT_RESET_RESCAN           1
#define VIRTIO_SCSI_EVT_RESET_REMOVED          2

#define VIRTIO_SCSI_S_SIMPLE                   0
#define VIRTIO_SCSI_S_ORDERED                  1
#define VIRTIO_SCSI_S_HEAD                     2
#define VIRTIO_SCSI_S_ACA                      3


#endif /* _LINUX_VIRTIO_SCSI_H */
