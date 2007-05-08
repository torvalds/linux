/*
 * SCSI target kernel/user interface
 *
 * Copyright (C) 2005 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2005 Mike Christie <michaelc@cs.wisc.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifndef __SCSI_TARGET_IF_H
#define __SCSI_TARGET_IF_H

/* user -> kernel */
#define	TGT_UEVENT_CMD_RSP	0x0001
#define	TGT_UEVENT_TSK_MGMT_RSP	0x0002

/* kernel -> user */
#define	TGT_KEVENT_CMD_REQ	0x1001
#define	TGT_KEVENT_CMD_DONE	0x1002
#define	TGT_KEVENT_TSK_MGMT_REQ	0x1003

struct tgt_event_hdr {
	uint16_t version;
	uint16_t status;
	uint16_t type;
	uint16_t len;
} __attribute__ ((aligned (sizeof(uint64_t))));

struct tgt_event {
	struct tgt_event_hdr hdr;

	union {
		/* user-> kernel */
		struct {
			int host_no;
			int result;
			aligned_u64 tag;
			aligned_u64 uaddr;
			aligned_u64 sense_uaddr;
			uint32_t len;
			uint32_t sense_len;
			uint8_t rw;
		} cmd_rsp;
		struct {
			int host_no;
			aligned_u64 mid;
			int result;
		} tsk_mgmt_rsp;


		/* kernel -> user */
		struct {
			int host_no;
			uint32_t data_len;
			uint8_t scb[16];
			uint8_t lun[8];
			int attribute;
			aligned_u64 tag;
		} cmd_req;
		struct {
			int host_no;
			aligned_u64 tag;
			int result;
		} cmd_done;
		struct {
			int host_no;
			int function;
			aligned_u64 tag;
			uint8_t lun[8];
			aligned_u64 mid;
		} tsk_mgmt_req;
	} p;
} __attribute__ ((aligned (sizeof(uint64_t))));

#define TGT_RING_SIZE (1UL << 16)

#endif
