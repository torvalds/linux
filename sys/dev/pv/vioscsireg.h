/*	$OpenBSD: vioscsireg.h,v 1.2 2019/03/24 18:21:12 sf Exp $	*/
/*
 * Copyright (c) 2013 Google Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Configuration registers */
#define VIRTIO_SCSI_CONFIG_NUM_QUEUES		0 /* 32bit */
#define VIRTIO_SCSI_CONFIG_SEG_MAX		4 /* 32bit */
#define VIRTIO_SCSI_CONFIG_MAX_SECTORS		8 /* 32bit */
#define VIRTIO_SCSI_CONFIG_CMD_PER_LUN		12 /* 32bit */
#define VIRTIO_SCSI_CONFIG_EVENT_INFO_SIZE	16 /* 32bit */
#define VIRTIO_SCSI_CONFIG_SENSE_SIZE		20 /* 32bit */
#define VIRTIO_SCSI_CONFIG_CDB_SIZE		24 /* 32bit */
#define VIRTIO_SCSI_CONFIG_MAX_CHANNEL		28 /* 16bit */
#define VIRTIO_SCSI_CONFIG_MAX_TARGET		30 /* 16bit */
#define VIRTIO_SCSI_CONFIG_MAX_LUN		32 /* 32bit */

/* Feature bits */
#define VIRTIO_SCSI_F_INOUT			(1ULL<<0)
#define VIRTIO_SCSI_F_HOTPLUG			(1ULL<<1)

/* Response status values */
#define VIRTIO_SCSI_S_OK			0
#define VIRTIO_SCSI_S_OVERRUN			1
#define VIRTIO_SCSI_S_ABORTED			2
#define VIRTIO_SCSI_S_BAD_TARGET		3
#define VIRTIO_SCSI_S_RESET			4
#define VIRTIO_SCSI_S_BUSY			5
#define VIRTIO_SCSI_S_TRANSPORT_FAILURE		6
#define VIRTIO_SCSI_S_TARGET_FAILURE		7
#define VIRTIO_SCSI_S_NEXUS_FAILURE		8
#define VIRTIO_SCSI_S_FAILURE			9

/* Task attributes */
#define VIRTIO_SCSI_S_SIMPLE			0
#define VIRTIO_SCSI_S_ORDERED			1
#define VIRTIO_SCSI_S_HEAD			2
#define VIRTIO_SCSI_S_ACA			3

/* Request header structure */
struct virtio_scsi_req_hdr {
	uint8_t		lun[8];
	uint64_t	id;
	uint8_t		task_attr;
	uint8_t		prio;
	uint8_t		crn;
	uint8_t		cdb[32];
} __packed;
/* Followed by data-out. */

/* Response header structure */
struct virtio_scsi_res_hdr {
	uint32_t	sense_len;
	uint32_t	residual;
	uint16_t	status_qualifier;
	uint8_t		status;
	uint8_t		response;
	uint8_t		sense[96];
} __packed;
/* Followed by data-in. */
