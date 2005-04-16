/* 
 * iSCSI transport class definitions
 *
 * Copyright (C) IBM Corporation, 2004
 * Copyright (C) Mike Christie, 2004
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef SCSI_TRANSPORT_ISCSI_H
#define SCSI_TRANSPORT_ISCSI_H

#include <linux/config.h>
#include <linux/in6.h>
#include <linux/in.h>

struct scsi_transport_template;

struct iscsi_class_session {
	uint8_t isid[6];
	uint16_t tsih;
	int header_digest;		/* 1 CRC32, 0 None */
	int data_digest;		/* 1 CRC32, 0 None */
	uint16_t tpgt;
	union {
		struct in6_addr sin6_addr;
		struct in_addr sin_addr;
	} u;
	sa_family_t addr_type;		/* must be AF_INET or AF_INET6 */
	uint16_t port;			/* must be in network byte order */
	int initial_r2t;		/* 1 Yes, 0 No */
	int immediate_data;		/* 1 Yes, 0 No */
	uint32_t max_recv_data_segment_len;
	uint32_t max_burst_len;
	uint32_t first_burst_len;
	uint16_t def_time2wait;
	uint16_t def_time2retain;
	uint16_t max_outstanding_r2t;
	int data_pdu_in_order;		/* 1 Yes, 0 No */
	int data_sequence_in_order;	/* 1 Yes, 0 No */
	int erl;
};

/*
 * accessor macros
 */
#define iscsi_isid(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->isid)
#define iscsi_tsih(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->tsih)
#define iscsi_header_digest(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->header_digest)
#define iscsi_data_digest(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->data_digest)
#define iscsi_port(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->port)
#define iscsi_addr_type(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->addr_type)
#define iscsi_sin_addr(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->u.sin_addr)
#define iscsi_sin6_addr(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->u.sin6_addr)
#define iscsi_tpgt(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->tpgt)
#define iscsi_initial_r2t(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->initial_r2t)
#define iscsi_immediate_data(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->immediate_data)
#define iscsi_max_recv_data_segment_len(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->max_recv_data_segment_len)
#define iscsi_max_burst_len(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->max_burst_len)
#define iscsi_first_burst_len(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->first_burst_len)
#define iscsi_def_time2wait(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->def_time2wait)
#define iscsi_def_time2retain(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->def_time2retain)
#define iscsi_max_outstanding_r2t(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->max_outstanding_r2t)
#define iscsi_data_pdu_in_order(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->data_pdu_in_order)
#define iscsi_data_sequence_in_order(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->data_sequence_in_order)
#define iscsi_erl(x) \
	(((struct iscsi_class_session *)&(x)->starget_data)->erl)

/*
 * The functions by which the transport class and the driver communicate
 */
struct iscsi_function_template {
	/*
	 * target attrs
	 */
	void (*get_isid)(struct scsi_target *);
	void (*get_tsih)(struct scsi_target *);
	void (*get_header_digest)(struct scsi_target *);
	void (*get_data_digest)(struct scsi_target *);
	void (*get_port)(struct scsi_target *);
	void (*get_tpgt)(struct scsi_target *);
	/*
	 * In get_ip_address the lld must set the address and
	 * the address type
	 */
	void (*get_ip_address)(struct scsi_target *);
	/*
	 * The lld should snprintf the name or alias to the buffer
	 */
	ssize_t (*get_target_name)(struct scsi_target *, char *, ssize_t);
	ssize_t (*get_target_alias)(struct scsi_target *, char *, ssize_t);
	void (*get_initial_r2t)(struct scsi_target *);
	void (*get_immediate_data)(struct scsi_target *);
	void (*get_max_recv_data_segment_len)(struct scsi_target *);
	void (*get_max_burst_len)(struct scsi_target *);
	void (*get_first_burst_len)(struct scsi_target *);
	void (*get_def_time2wait)(struct scsi_target *);
	void (*get_def_time2retain)(struct scsi_target *);
	void (*get_max_outstanding_r2t)(struct scsi_target *);
	void (*get_data_pdu_in_order)(struct scsi_target *);
	void (*get_data_sequence_in_order)(struct scsi_target *);
	void (*get_erl)(struct scsi_target *);

	/*
	 * host atts
	 */

	/*
	 * The lld should snprintf the name or alias to the buffer
	 */
	ssize_t (*get_initiator_alias)(struct Scsi_Host *, char *, ssize_t);
	ssize_t (*get_initiator_name)(struct Scsi_Host *, char *, ssize_t);
	/*
	 * The driver sets these to tell the transport class it
	 * wants the attributes displayed in sysfs.  If the show_ flag
	 * is not set, the attribute will be private to the transport
	 * class. We could probably just test if a get_ fn was set
	 * since we only use the values for sysfs but this is how
	 * fc does it too.
	 */
	unsigned long show_isid:1;
	unsigned long show_tsih:1;
	unsigned long show_header_digest:1;
	unsigned long show_data_digest:1;
	unsigned long show_port:1;
	unsigned long show_tpgt:1;
	unsigned long show_ip_address:1;
	unsigned long show_target_name:1;
	unsigned long show_target_alias:1;
	unsigned long show_initial_r2t:1;
	unsigned long show_immediate_data:1;
	unsigned long show_max_recv_data_segment_len:1;
	unsigned long show_max_burst_len:1;
	unsigned long show_first_burst_len:1;
	unsigned long show_def_time2wait:1;
	unsigned long show_def_time2retain:1;
	unsigned long show_max_outstanding_r2t:1;
	unsigned long show_data_pdu_in_order:1;
	unsigned long show_data_sequence_in_order:1;
	unsigned long show_erl:1;
	unsigned long show_initiator_name:1;
	unsigned long show_initiator_alias:1;
};

struct scsi_transport_template *iscsi_attach_transport(struct iscsi_function_template *);
void iscsi_release_transport(struct scsi_transport_template *);

#endif
