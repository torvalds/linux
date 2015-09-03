/*
 *  FiberChannel transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  ========
 *
 *  Copyright (C) 2004-2007   James Smart, Emulex Corporation
 *    Rewrite for host, target, device, and remote port attributes,
 *    statistics, and service functions...
 *
 */
#ifndef SCSI_TRANSPORT_FC_H
#define SCSI_TRANSPORT_FC_H

#include <linux/sched.h>
#include <scsi/scsi.h>
#include <scsi/scsi_netlink.h>

struct scsi_transport_template;

/*
 * FC Port definitions - Following FC HBAAPI guidelines
 *
 * Note: Not all binary values for the different fields match HBAAPI.
 *  Instead, we use densely packed ordinal values or enums.
 *  We get away with this as we never present the actual binary values
 *  externally. For sysfs, we always present the string that describes
 *  the value. Thus, an admin doesn't need a magic HBAAPI decoder ring
 *  to understand the values. The HBAAPI user-space library is free to
 *  convert the strings into the HBAAPI-specified binary values.
 *
 * Note: Not all HBAAPI-defined values are contained in the definitions
 *  below. Those not appropriate to an fc_host (e.g. FCP initiator) have
 *  been removed.
 */

/*
 * fc_port_type: If you alter this, you also need to alter scsi_transport_fc.c
 * (for the ascii descriptions).
 */
enum fc_port_type {
	FC_PORTTYPE_UNKNOWN,
	FC_PORTTYPE_OTHER,
	FC_PORTTYPE_NOTPRESENT,
	FC_PORTTYPE_NPORT,		/* Attached to FPort */
	FC_PORTTYPE_NLPORT,		/* (Public) Loop w/ FLPort */
	FC_PORTTYPE_LPORT,		/* (Private) Loop w/o FLPort */
	FC_PORTTYPE_PTP,		/* Point to Point w/ another NPort */
	FC_PORTTYPE_NPIV,		/* VPORT based on NPIV */
};


/*
 * fc_port_state: If you alter this, you also need to alter scsi_transport_fc.c
 * (for the ascii descriptions).
 */
enum fc_port_state {
	FC_PORTSTATE_UNKNOWN,
	FC_PORTSTATE_NOTPRESENT,
	FC_PORTSTATE_ONLINE,
	FC_PORTSTATE_OFFLINE,		/* User has taken Port Offline */
	FC_PORTSTATE_BLOCKED,
	FC_PORTSTATE_BYPASSED,
	FC_PORTSTATE_DIAGNOSTICS,
	FC_PORTSTATE_LINKDOWN,
	FC_PORTSTATE_ERROR,
	FC_PORTSTATE_LOOPBACK,
	FC_PORTSTATE_DELETED,
};


/*
 * fc_vport_state: If you alter this, you also need to alter
 * scsi_transport_fc.c (for the ascii descriptions).
 */
enum fc_vport_state {
	FC_VPORT_UNKNOWN,
	FC_VPORT_ACTIVE,
	FC_VPORT_DISABLED,
	FC_VPORT_LINKDOWN,
	FC_VPORT_INITIALIZING,
	FC_VPORT_NO_FABRIC_SUPP,
	FC_VPORT_NO_FABRIC_RSCS,
	FC_VPORT_FABRIC_LOGOUT,
	FC_VPORT_FABRIC_REJ_WWN,
	FC_VPORT_FAILED,
};



/*
 * FC Classes of Service
 * Note: values are not enumerated, as they can be "or'd" together
 * for reporting (e.g. report supported_classes). If you alter this list,
 * you also need to alter scsi_transport_fc.c (for the ascii descriptions).
 */
#define FC_COS_UNSPECIFIED		0
#define FC_COS_CLASS1			2
#define FC_COS_CLASS2			4
#define FC_COS_CLASS3			8
#define FC_COS_CLASS4			0x10
#define FC_COS_CLASS6			0x40

/*
 * FC Port Speeds
 * Note: values are not enumerated, as they can be "or'd" together
 * for reporting (e.g. report supported_speeds). If you alter this list,
 * you also need to alter scsi_transport_fc.c (for the ascii descriptions).
 */
#define FC_PORTSPEED_UNKNOWN		0 /* Unknown - transceiver
					     incapable of reporting */
#define FC_PORTSPEED_1GBIT		1
#define FC_PORTSPEED_2GBIT		2
#define FC_PORTSPEED_10GBIT		4
#define FC_PORTSPEED_4GBIT		8
#define FC_PORTSPEED_8GBIT		0x10
#define FC_PORTSPEED_16GBIT		0x20
#define FC_PORTSPEED_32GBIT		0x40
#define FC_PORTSPEED_20GBIT		0x80
#define FC_PORTSPEED_40GBIT		0x100
#define FC_PORTSPEED_50GBIT		0x200
#define FC_PORTSPEED_100GBIT		0x400
#define FC_PORTSPEED_25GBIT		0x800
#define FC_PORTSPEED_NOT_NEGOTIATED	(1 << 15) /* Speed not established */

/*
 * fc_tgtid_binding_type: If you alter this, you also need to alter
 * scsi_transport_fc.c (for the ascii descriptions).
 */
enum fc_tgtid_binding_type  {
	FC_TGTID_BIND_NONE,
	FC_TGTID_BIND_BY_WWPN,
	FC_TGTID_BIND_BY_WWNN,
	FC_TGTID_BIND_BY_ID,
};

/*
 * FC Port Roles
 * Note: values are not enumerated, as they can be "or'd" together
 * for reporting (e.g. report roles). If you alter this list,
 * you also need to alter scsi_transport_fc.c (for the ascii descriptions).
 */
#define FC_PORT_ROLE_UNKNOWN			0x00
#define FC_PORT_ROLE_FCP_TARGET			0x01
#define FC_PORT_ROLE_FCP_INITIATOR		0x02
#define FC_PORT_ROLE_IP_PORT			0x04

/* The following are for compatibility */
#define FC_RPORT_ROLE_UNKNOWN			FC_PORT_ROLE_UNKNOWN
#define FC_RPORT_ROLE_FCP_TARGET		FC_PORT_ROLE_FCP_TARGET
#define FC_RPORT_ROLE_FCP_INITIATOR		FC_PORT_ROLE_FCP_INITIATOR
#define FC_RPORT_ROLE_IP_PORT			FC_PORT_ROLE_IP_PORT


/* Macro for use in defining Virtual Port attributes */
#define FC_VPORT_ATTR(_name,_mode,_show,_store)		\
struct device_attribute dev_attr_vport_##_name = 	\
	__ATTR(_name,_mode,_show,_store)

/*
 * fc_vport_identifiers: This set of data contains all elements
 * to uniquely identify and instantiate a FC virtual port.
 *
 * Notes:
 *   symbolic_name: The driver is to append the symbolic_name string data
 *      to the symbolic_node_name data that it generates by default.
 *      the resulting combination should then be registered with the switch.
 *      It is expected that things like Xen may stuff a VM title into
 *      this field.
 */
#define FC_VPORT_SYMBOLIC_NAMELEN		64
struct fc_vport_identifiers {
	u64 node_name;
	u64 port_name;
	u32 roles;
	bool disable;
	enum fc_port_type vport_type;	/* only FC_PORTTYPE_NPIV allowed */
	char symbolic_name[FC_VPORT_SYMBOLIC_NAMELEN];
};

/*
 * FC Virtual Port Attributes
 *
 * This structure exists for each FC port is a virtual FC port. Virtual
 * ports share the physical link with the Physical port. Each virtual
 * ports has a unique presence on the SAN, and may be instantiated via
 * NPIV, Virtual Fabrics, or via additional ALPAs. As the vport is a
 * unique presence, each vport has it's own view of the fabric,
 * authentication privilege, and priorities.
 *
 * A virtual port may support 1 or more FC4 roles. Typically it is a
 * FCP Initiator. It could be a FCP Target, or exist sole for an IP over FC
 * roles. FC port attributes for the vport will be reported on any
 * fc_host class object allocated for an FCP Initiator.
 *
 * --
 *
 * Fixed attributes are not expected to change. The driver is
 * expected to set these values after receiving the fc_vport structure
 * via the vport_create() call from the transport.
 * The transport fully manages all get functions w/o driver interaction.
 *
 * Dynamic attributes are expected to change. The driver participates
 * in all get/set operations via functions provided by the driver.
 *
 * Private attributes are transport-managed values. They are fully
 * managed by the transport w/o driver interaction.
 */

struct fc_vport {
	/* Fixed Attributes */

	/* Dynamic Attributes */

	/* Private (Transport-managed) Attributes */
	enum fc_vport_state vport_state;
	enum fc_vport_state vport_last_state;
	u64 node_name;
	u64 port_name;
	u32 roles;
	u32 vport_id;		/* Admin Identifier for the vport */
	enum fc_port_type vport_type;
	char symbolic_name[FC_VPORT_SYMBOLIC_NAMELEN];

	/* exported data */
	void *dd_data;			/* Used for driver-specific storage */

	/* internal data */
	struct Scsi_Host *shost;	/* Physical Port Parent */
	unsigned int channel;
	u32 number;
	u8 flags;
	struct list_head peers;
	struct device dev;
	struct work_struct vport_delete_work;
} __attribute__((aligned(sizeof(unsigned long))));

/* bit field values for struct fc_vport "flags" field: */
#define FC_VPORT_CREATING		0x01
#define FC_VPORT_DELETING		0x02
#define FC_VPORT_DELETED		0x04
#define FC_VPORT_DEL			0x06	/* Any DELETE state */

#define	dev_to_vport(d)				\
	container_of(d, struct fc_vport, dev)
#define transport_class_to_vport(dev)		\
	dev_to_vport(dev->parent)
#define vport_to_shost(v)			\
	(v->shost)
#define vport_to_shost_channel(v)		\
	(v->channel)
#define vport_to_parent(v)			\
	(v->dev.parent)


/* Error return codes for vport_create() callback */
#define VPCERR_UNSUPPORTED		-ENOSYS		/* no driver/adapter
							   support */
#define VPCERR_BAD_WWN			-ENOTUNIQ	/* driver validation
							   of WWNs failed */
#define VPCERR_NO_FABRIC_SUPP		-EOPNOTSUPP	/* Fabric connection
							   is loop or the
							   Fabric Port does
							   not support NPIV */

/*
 * fc_rport_identifiers: This set of data contains all elements
 * to uniquely identify a remote FC port. The driver uses this data
 * to report the existence of a remote FC port in the topology. Internally,
 * the transport uses this data for attributes and to manage consistent
 * target id bindings.
 */
struct fc_rport_identifiers {
	u64 node_name;
	u64 port_name;
	u32 port_id;
	u32 roles;
};


/* Macro for use in defining Remote Port attributes */
#define FC_RPORT_ATTR(_name,_mode,_show,_store)				\
struct device_attribute dev_attr_rport_##_name = 	\
	__ATTR(_name,_mode,_show,_store)


/*
 * FC Remote Port Attributes
 *
 * This structure exists for each remote FC port that a LLDD notifies
 * the subsystem of.  A remote FC port may or may not be a SCSI Target,
 * also be a SCSI initiator, IP endpoint, etc. As such, the remote
 * port is considered a separate entity, independent of "role" (such
 * as scsi target).
 *
 * --
 *
 * Attributes are based on HBAAPI V2.0 definitions. Only those
 * attributes that are determinable by the local port (aka Host)
 * are contained.
 *
 * Fixed attributes are not expected to change. The driver is
 * expected to set these values after successfully calling
 * fc_remote_port_add(). The transport fully manages all get functions
 * w/o driver interaction.
 *
 * Dynamic attributes are expected to change. The driver participates
 * in all get/set operations via functions provided by the driver.
 *
 * Private attributes are transport-managed values. They are fully
 * managed by the transport w/o driver interaction.
 */

struct fc_rport {	/* aka fc_starget_attrs */
	/* Fixed Attributes */
	u32 maxframe_size;
	u32 supported_classes;

	/* Dynamic Attributes */
	u32 dev_loss_tmo;	/* Remote Port loss timeout in seconds. */

	/* Private (Transport-managed) Attributes */
	u64 node_name;
	u64 port_name;
	u32 port_id;
	u32 roles;
	enum fc_port_state port_state;	/* Will only be ONLINE or UNKNOWN */
	u32 scsi_target_id;
	u32 fast_io_fail_tmo;

	/* exported data */
	void *dd_data;			/* Used for driver-specific storage */

	/* internal data */
	unsigned int channel;
	u32 number;
	u8 flags;
	struct list_head peers;
	struct device dev;
 	struct delayed_work dev_loss_work;
 	struct work_struct scan_work;
 	struct delayed_work fail_io_work;
 	struct work_struct stgt_delete_work;
	struct work_struct rport_delete_work;
	struct request_queue *rqst_q;	/* bsg support */
} __attribute__((aligned(sizeof(unsigned long))));

/* bit field values for struct fc_rport "flags" field: */
#define FC_RPORT_DEVLOSS_PENDING	0x01
#define FC_RPORT_SCAN_PENDING		0x02
#define FC_RPORT_FAST_FAIL_TIMEDOUT	0x04
#define FC_RPORT_DEVLOSS_CALLBK_DONE	0x08

#define	dev_to_rport(d)				\
	container_of(d, struct fc_rport, dev)
#define transport_class_to_rport(dev)	\
	dev_to_rport(dev->parent)
#define rport_to_shost(r)			\
	dev_to_shost(r->dev.parent)

/*
 * FC SCSI Target Attributes
 *
 * The SCSI Target is considered an extension of a remote port (as
 * a remote port can be more than a SCSI Target). Within the scsi
 * subsystem, we leave the Target as a separate entity. Doing so
 * provides backward compatibility with prior FC transport api's,
 * and lets remote ports be handled entirely within the FC transport
 * and independently from the scsi subsystem. The drawback is that
 * some data will be duplicated.
 */

struct fc_starget_attrs {	/* aka fc_target_attrs */
	/* Dynamic Attributes */
	u64 node_name;
	u64 port_name;
	u32 port_id;
};

#define fc_starget_node_name(x) \
	(((struct fc_starget_attrs *)&(x)->starget_data)->node_name)
#define fc_starget_port_name(x)	\
	(((struct fc_starget_attrs *)&(x)->starget_data)->port_name)
#define fc_starget_port_id(x) \
	(((struct fc_starget_attrs *)&(x)->starget_data)->port_id)

#define starget_to_rport(s)			\
	scsi_is_fc_rport(s->dev.parent) ? dev_to_rport(s->dev.parent) : NULL


/*
 * FC Local Port (Host) Statistics
 */

/* FC Statistics - Following FC HBAAPI v2.0 guidelines */
struct fc_host_statistics {
	/* port statistics */
	u64 seconds_since_last_reset;
	u64 tx_frames;
	u64 tx_words;
	u64 rx_frames;
	u64 rx_words;
	u64 lip_count;
	u64 nos_count;
	u64 error_frames;
	u64 dumped_frames;
	u64 link_failure_count;
	u64 loss_of_sync_count;
	u64 loss_of_signal_count;
	u64 prim_seq_protocol_err_count;
	u64 invalid_tx_word_count;
	u64 invalid_crc_count;

	/* fc4 statistics  (only FCP supported currently) */
	u64 fcp_input_requests;
	u64 fcp_output_requests;
	u64 fcp_control_requests;
	u64 fcp_input_megabytes;
	u64 fcp_output_megabytes;
	u64 fcp_packet_alloc_failures;	/* fcp packet allocation failures */
	u64 fcp_packet_aborts;		/* fcp packet aborted */
	u64 fcp_frame_alloc_failures;	/* fcp frame allocation failures */

	/* fc exches statistics */
	u64 fc_no_free_exch;		/* no free exch memory */
	u64 fc_no_free_exch_xid;	/* no free exch id */
	u64 fc_xid_not_found;		/* exch not found for a response */
	u64 fc_xid_busy;		/* exch exist for new a request */
	u64 fc_seq_not_found;		/* seq is not found for exchange */
	u64 fc_non_bls_resp;		/* a non BLS response frame with
					   a sequence responder in new exch */
};


/*
 * FC Event Codes - Polled and Async, following FC HBAAPI v2.0 guidelines
 */

/*
 * fc_host_event_code: If you alter this, you also need to alter
 * scsi_transport_fc.c (for the ascii descriptions).
 */
enum fc_host_event_code  {
	FCH_EVT_LIP			= 0x1,
	FCH_EVT_LINKUP			= 0x2,
	FCH_EVT_LINKDOWN		= 0x3,
	FCH_EVT_LIPRESET		= 0x4,
	FCH_EVT_RSCN			= 0x5,
	FCH_EVT_ADAPTER_CHANGE		= 0x103,
	FCH_EVT_PORT_UNKNOWN		= 0x200,
	FCH_EVT_PORT_OFFLINE		= 0x201,
	FCH_EVT_PORT_ONLINE		= 0x202,
	FCH_EVT_PORT_FABRIC		= 0x204,
	FCH_EVT_LINK_UNKNOWN		= 0x500,
	FCH_EVT_VENDOR_UNIQUE		= 0xffff,
};


/*
 * FC Local Port (Host) Attributes
 *
 * Attributes are based on HBAAPI V2.0 definitions.
 * Note: OSDeviceName is determined by user-space library
 *
 * Fixed attributes are not expected to change. The driver is
 * expected to set these values after successfully calling scsi_add_host().
 * The transport fully manages all get functions w/o driver interaction.
 *
 * Dynamic attributes are expected to change. The driver participates
 * in all get/set operations via functions provided by the driver.
 *
 * Private attributes are transport-managed values. They are fully
 * managed by the transport w/o driver interaction.
 */

#define FC_FC4_LIST_SIZE		32
#define FC_SYMBOLIC_NAME_SIZE		256
#define FC_VERSION_STRING_SIZE		64
#define FC_SERIAL_NUMBER_SIZE		80

struct fc_host_attrs {
	/* Fixed Attributes */
	u64 node_name;
	u64 port_name;
	u64 permanent_port_name;
	u32 supported_classes;
	u8  supported_fc4s[FC_FC4_LIST_SIZE];
	u32 supported_speeds;
	u32 maxframe_size;
	u16 max_npiv_vports;
	char serial_number[FC_SERIAL_NUMBER_SIZE];
	char manufacturer[FC_SERIAL_NUMBER_SIZE];
	char model[FC_SYMBOLIC_NAME_SIZE];
	char model_description[FC_SYMBOLIC_NAME_SIZE];
	char hardware_version[FC_VERSION_STRING_SIZE];
	char driver_version[FC_VERSION_STRING_SIZE];
	char firmware_version[FC_VERSION_STRING_SIZE];
	char optionrom_version[FC_VERSION_STRING_SIZE];

	/* Dynamic Attributes */
	u32 port_id;
	enum fc_port_type port_type;
	enum fc_port_state port_state;
	u8  active_fc4s[FC_FC4_LIST_SIZE];
	u32 speed;
	u64 fabric_name;
	char symbolic_name[FC_SYMBOLIC_NAME_SIZE];
	char system_hostname[FC_SYMBOLIC_NAME_SIZE];
	u32 dev_loss_tmo;

	/* Private (Transport-managed) Attributes */
	enum fc_tgtid_binding_type  tgtid_bind_type;

	/* internal data */
	struct list_head rports;
	struct list_head rport_bindings;
	struct list_head vports;
	u32 next_rport_number;
	u32 next_target_id;
	u32 next_vport_number;
	u16 npiv_vports_inuse;

	/* work queues for rport state manipulation */
	char work_q_name[20];
	struct workqueue_struct *work_q;
	char devloss_work_q_name[20];
	struct workqueue_struct *devloss_work_q;

	/* bsg support */
	struct request_queue *rqst_q;
};

#define shost_to_fc_host(x) \
	((struct fc_host_attrs *)(x)->shost_data)

#define fc_host_node_name(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->node_name)
#define fc_host_port_name(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->port_name)
#define fc_host_permanent_port_name(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->permanent_port_name)
#define fc_host_supported_classes(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->supported_classes)
#define fc_host_supported_fc4s(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->supported_fc4s)
#define fc_host_supported_speeds(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->supported_speeds)
#define fc_host_maxframe_size(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->maxframe_size)
#define fc_host_max_npiv_vports(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->max_npiv_vports)
#define fc_host_serial_number(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->serial_number)
#define fc_host_manufacturer(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->manufacturer)
#define fc_host_model(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->model)
#define fc_host_model_description(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->model_description)
#define fc_host_hardware_version(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->hardware_version)
#define fc_host_driver_version(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->driver_version)
#define fc_host_firmware_version(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->firmware_version)
#define fc_host_optionrom_version(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->optionrom_version)
#define fc_host_port_id(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->port_id)
#define fc_host_port_type(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->port_type)
#define fc_host_port_state(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->port_state)
#define fc_host_active_fc4s(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->active_fc4s)
#define fc_host_speed(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->speed)
#define fc_host_fabric_name(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->fabric_name)
#define fc_host_symbolic_name(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->symbolic_name)
#define fc_host_system_hostname(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->system_hostname)
#define fc_host_tgtid_bind_type(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->tgtid_bind_type)
#define fc_host_rports(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->rports)
#define fc_host_rport_bindings(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->rport_bindings)
#define fc_host_vports(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->vports)
#define fc_host_next_rport_number(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->next_rport_number)
#define fc_host_next_target_id(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->next_target_id)
#define fc_host_next_vport_number(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->next_vport_number)
#define fc_host_npiv_vports_inuse(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->npiv_vports_inuse)
#define fc_host_work_q_name(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->work_q_name)
#define fc_host_work_q(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->work_q)
#define fc_host_devloss_work_q_name(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->devloss_work_q_name)
#define fc_host_devloss_work_q(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->devloss_work_q)
#define fc_host_dev_loss_tmo(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->dev_loss_tmo)


struct fc_bsg_buffer {
	unsigned int payload_len;
	int sg_cnt;
	struct scatterlist *sg_list;
};

/* Values for fc_bsg_job->state_flags (bitflags) */
#define FC_RQST_STATE_INPROGRESS	0
#define FC_RQST_STATE_DONE		1

struct fc_bsg_job {
	struct Scsi_Host *shost;
	struct fc_rport *rport;
	struct device *dev;
	struct request *req;
	spinlock_t job_lock;
	unsigned int state_flags;
	unsigned int ref_cnt;
	void (*job_done)(struct fc_bsg_job *);

	struct fc_bsg_request *request;
	struct fc_bsg_reply *reply;
	unsigned int request_len;
	unsigned int reply_len;
	/*
	 * On entry : reply_len indicates the buffer size allocated for
	 * the reply.
	 *
	 * Upon completion : the message handler must set reply_len
	 *  to indicates the size of the reply to be returned to the
	 *  caller.
	 */

	/* DMA payloads for the request/response */
	struct fc_bsg_buffer request_payload;
	struct fc_bsg_buffer reply_payload;

	void *dd_data;			/* Used for driver-specific storage */
};


/* The functions by which the transport class and the driver communicate */
struct fc_function_template {
	void    (*get_rport_dev_loss_tmo)(struct fc_rport *);
	void	(*set_rport_dev_loss_tmo)(struct fc_rport *, u32);

	void	(*get_starget_node_name)(struct scsi_target *);
	void	(*get_starget_port_name)(struct scsi_target *);
	void 	(*get_starget_port_id)(struct scsi_target *);

	void 	(*get_host_port_id)(struct Scsi_Host *);
	void	(*get_host_port_type)(struct Scsi_Host *);
	void	(*get_host_port_state)(struct Scsi_Host *);
	void	(*get_host_active_fc4s)(struct Scsi_Host *);
	void	(*get_host_speed)(struct Scsi_Host *);
	void	(*get_host_fabric_name)(struct Scsi_Host *);
	void	(*get_host_symbolic_name)(struct Scsi_Host *);
	void	(*set_host_system_hostname)(struct Scsi_Host *);

	struct fc_host_statistics * (*get_fc_host_stats)(struct Scsi_Host *);
	void	(*reset_fc_host_stats)(struct Scsi_Host *);

	int	(*issue_fc_host_lip)(struct Scsi_Host *);

	void    (*dev_loss_tmo_callbk)(struct fc_rport *);
	void	(*terminate_rport_io)(struct fc_rport *);

	void	(*set_vport_symbolic_name)(struct fc_vport *);
	int  	(*vport_create)(struct fc_vport *, bool);
	int	(*vport_disable)(struct fc_vport *, bool);
	int  	(*vport_delete)(struct fc_vport *);

	/* target-mode drivers' functions */
	int     (* tsk_mgmt_response)(struct Scsi_Host *, u64, u64, int);
	int     (* it_nexus_response)(struct Scsi_Host *, u64, int);

	/* bsg support */
	int	(*bsg_request)(struct fc_bsg_job *);
	int	(*bsg_timeout)(struct fc_bsg_job *);

	/* allocation lengths for host-specific data */
	u32	 			dd_fcrport_size;
	u32	 			dd_fcvport_size;
	u32				dd_bsg_size;

	/*
	 * The driver sets these to tell the transport class it
	 * wants the attributes displayed in sysfs.  If the show_ flag
	 * is not set, the attribute will be private to the transport
	 * class
	 */

	/* remote port fixed attributes */
	unsigned long	show_rport_maxframe_size:1;
	unsigned long	show_rport_supported_classes:1;
	unsigned long   show_rport_dev_loss_tmo:1;

	/*
	 * target dynamic attributes
	 * These should all be "1" if the driver uses the remote port
	 * add/delete functions (so attributes reflect rport values).
	 */
	unsigned long	show_starget_node_name:1;
	unsigned long	show_starget_port_name:1;
	unsigned long	show_starget_port_id:1;

	/* host fixed attributes */
	unsigned long	show_host_node_name:1;
	unsigned long	show_host_port_name:1;
	unsigned long	show_host_permanent_port_name:1;
	unsigned long	show_host_supported_classes:1;
	unsigned long	show_host_supported_fc4s:1;
	unsigned long	show_host_supported_speeds:1;
	unsigned long	show_host_maxframe_size:1;
	unsigned long	show_host_serial_number:1;
	unsigned long	show_host_manufacturer:1;
	unsigned long	show_host_model:1;
	unsigned long	show_host_model_description:1;
	unsigned long	show_host_hardware_version:1;
	unsigned long	show_host_driver_version:1;
	unsigned long	show_host_firmware_version:1;
	unsigned long	show_host_optionrom_version:1;
	/* host dynamic attributes */
	unsigned long	show_host_port_id:1;
	unsigned long	show_host_port_type:1;
	unsigned long	show_host_port_state:1;
	unsigned long	show_host_active_fc4s:1;
	unsigned long	show_host_speed:1;
	unsigned long	show_host_fabric_name:1;
	unsigned long	show_host_symbolic_name:1;
	unsigned long	show_host_system_hostname:1;

	unsigned long	disable_target_scan:1;
};


/**
 * fc_remote_port_chkready - called to validate the remote port state
 *   prior to initiating io to the port.
 *
 * Returns a scsi result code that can be returned by the LLDD.
 *
 * @rport:	remote port to be checked
 **/
static inline int
fc_remote_port_chkready(struct fc_rport *rport)
{
	int result;

	switch (rport->port_state) {
	case FC_PORTSTATE_ONLINE:
		if (rport->roles & FC_PORT_ROLE_FCP_TARGET)
			result = 0;
		else if (rport->flags & FC_RPORT_DEVLOSS_PENDING)
			result = DID_IMM_RETRY << 16;
		else
			result = DID_NO_CONNECT << 16;
		break;
	case FC_PORTSTATE_BLOCKED:
		if (rport->flags & FC_RPORT_FAST_FAIL_TIMEDOUT)
			result = DID_TRANSPORT_FAILFAST << 16;
		else
			result = DID_IMM_RETRY << 16;
		break;
	default:
		result = DID_NO_CONNECT << 16;
		break;
	}
	return result;
}

static inline u64 wwn_to_u64(u8 *wwn)
{
	return (u64)wwn[0] << 56 | (u64)wwn[1] << 48 |
	    (u64)wwn[2] << 40 | (u64)wwn[3] << 32 |
	    (u64)wwn[4] << 24 | (u64)wwn[5] << 16 |
	    (u64)wwn[6] <<  8 | (u64)wwn[7];
}

static inline void u64_to_wwn(u64 inm, u8 *wwn)
{
	wwn[0] = (inm >> 56) & 0xff;
	wwn[1] = (inm >> 48) & 0xff;
	wwn[2] = (inm >> 40) & 0xff;
	wwn[3] = (inm >> 32) & 0xff;
	wwn[4] = (inm >> 24) & 0xff;
	wwn[5] = (inm >> 16) & 0xff;
	wwn[6] = (inm >> 8) & 0xff;
	wwn[7] = inm & 0xff;
}

/**
 * fc_vport_set_state() - called to set a vport's state. Saves the old state,
 *   excepting the transitory states of initializing and sending the ELS
 *   traffic to instantiate the vport on the link.
 *
 * Assumes the driver has surrounded this with the proper locking to ensure
 * a coherent state change.
 *
 * @vport:	virtual port whose state is changing
 * @new_state:  new state
 **/
static inline void
fc_vport_set_state(struct fc_vport *vport, enum fc_vport_state new_state)
{
	if ((new_state != FC_VPORT_UNKNOWN) &&
	    (new_state != FC_VPORT_INITIALIZING))
		vport->vport_last_state = vport->vport_state;
	vport->vport_state = new_state;
}

struct scsi_transport_template *fc_attach_transport(
			struct fc_function_template *);
void fc_release_transport(struct scsi_transport_template *);
void fc_remove_host(struct Scsi_Host *);
struct fc_rport *fc_remote_port_add(struct Scsi_Host *shost,
			int channel, struct fc_rport_identifiers  *ids);
void fc_remote_port_delete(struct fc_rport  *rport);
void fc_remote_port_rolechg(struct fc_rport  *rport, u32 roles);
int scsi_is_fc_rport(const struct device *);
u32 fc_get_event_number(void);
void fc_host_post_event(struct Scsi_Host *shost, u32 event_number,
		enum fc_host_event_code event_code, u32 event_data);
void fc_host_post_vendor_event(struct Scsi_Host *shost, u32 event_number,
		u32 data_len, char * data_buf, u64 vendor_id);
	/* Note: when specifying vendor_id to fc_host_post_vendor_event()
	 *   be sure to read the Vendor Type and ID formatting requirements
	 *   specified in scsi_netlink.h
	 */
struct fc_vport *fc_vport_create(struct Scsi_Host *shost, int channel,
		struct fc_vport_identifiers *);
int fc_vport_terminate(struct fc_vport *vport);
int fc_block_scsi_eh(struct scsi_cmnd *cmnd);

#endif /* SCSI_TRANSPORT_FC_H */
