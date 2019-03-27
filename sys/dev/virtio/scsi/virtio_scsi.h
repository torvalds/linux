/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *
 * $FreeBSD$
 */

#ifndef _VIRTIO_SCSI_H
#define _VIRTIO_SCSI_H

/* Feature bits */
#define VIRTIO_SCSI_F_INOUT	0x0001	/* Single request can contain both
					 * read and write buffers */
#define VIRTIO_SCSI_F_HOTPLUG	0x0002	/* Host should enable hot plug/unplug
					 * of new LUNs and targets.
					 */

#define VIRTIO_SCSI_CDB_SIZE	32
#define VIRTIO_SCSI_SENSE_SIZE	96

/* SCSI command request, followed by data-out */
struct virtio_scsi_cmd_req {
	uint8_t lun[8];		/* Logical Unit Number */
	uint64_t tag;		/* Command identifier */
	uint8_t task_attr;	/* Task attribute */
	uint8_t prio;
	uint8_t crn;
	uint8_t cdb[VIRTIO_SCSI_CDB_SIZE];
} __packed;

/* Response, followed by sense data and data-in */
struct virtio_scsi_cmd_resp {
	uint32_t sense_len;		/* Sense data length */
	uint32_t resid;			/* Residual bytes in data buffer */
	uint16_t status_qualifier;	/* Status qualifier */
	uint8_t status;			/* Command completion status */
	uint8_t response;		/* Response values */
	uint8_t sense[VIRTIO_SCSI_SENSE_SIZE];
} __packed;

/* Task Management Request */
struct virtio_scsi_ctrl_tmf_req {
	uint32_t type;
	uint32_t subtype;
	uint8_t lun[8];
	uint64_t tag;
} __packed;

struct virtio_scsi_ctrl_tmf_resp {
	uint8_t response;
} __packed;

/* Asynchronous notification query/subscription */
struct virtio_scsi_ctrl_an_req {
	uint32_t type;
	uint8_t lun[8];
	uint32_t event_requested;
} __packed;

struct virtio_scsi_ctrl_an_resp {
	uint32_t event_actual;
	uint8_t response;
} __packed;

struct virtio_scsi_event {
	uint32_t event;
	uint8_t lun[8];
	uint32_t reason;
} __packed;

struct virtio_scsi_config {
	uint32_t num_queues;
	uint32_t seg_max;
	uint32_t max_sectors;
	uint32_t cmd_per_lun;
	uint32_t event_info_size;
	uint32_t sense_size;
	uint32_t cdb_size;
	uint16_t max_channel;
	uint16_t max_target;
	uint32_t max_lun;
} __packed;

/* Response codes */
#define VIRTIO_SCSI_S_OK                       0
#define VIRTIO_SCSI_S_FUNCTION_COMPLETE        0
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

/* Reasons of transport reset event */
#define VIRTIO_SCSI_EVT_RESET_HARD             0
#define VIRTIO_SCSI_EVT_RESET_RESCAN           1
#define VIRTIO_SCSI_EVT_RESET_REMOVED          2

#define VIRTIO_SCSI_S_SIMPLE                   0
#define VIRTIO_SCSI_S_ORDERED                  1
#define VIRTIO_SCSI_S_HEAD                     2
#define VIRTIO_SCSI_S_ACA                      3

#endif /* _VIRTIO_SCSI_H */
