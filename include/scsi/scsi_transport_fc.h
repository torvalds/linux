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
 *  Copyright (C) 2004-2005   James Smart, Emulex Corporation
 *    Rewrite for host, target, device, and remote port attributes,
 *    statistics, and service functions...
 *
 */
#ifndef SCSI_TRANSPORT_FC_H
#define SCSI_TRANSPORT_FC_H

#include <linux/config.h>
#include <linux/sched.h>
#include <scsi/scsi.h>

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
#define FC_PORTSPEED_4GBIT		4
#define FC_PORTSPEED_10GBIT		8
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
 * FC Remote Port Roles
 * Note: values are not enumerated, as they can be "or'd" together
 * for reporting (e.g. report roles). If you alter this list,
 * you also need to alter scsi_transport_fc.c (for the ascii descriptions).
 */
#define FC_RPORT_ROLE_UNKNOWN			0x00
#define FC_RPORT_ROLE_FCP_TARGET		0x01
#define FC_RPORT_ROLE_FCP_INITIATOR		0x02
#define FC_RPORT_ROLE_IP_PORT			0x04


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
struct class_device_attribute class_device_attr_rport_##_name = 	\
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

	/* exported data */
	void *dd_data;			/* Used for driver-specific storage */

	/* internal data */
	unsigned int channel;
	u32 number;
	struct list_head peers;
	struct device dev;
 	struct work_struct dev_loss_work;
 	struct work_struct scan_work;
} __attribute__((aligned(sizeof(unsigned long))));

#define	dev_to_rport(d)				\
	container_of(d, struct fc_rport, dev)
#define transport_class_to_rport(classdev)	\
	dev_to_rport(classdev->dev)
#define rport_to_shost(r)			\
	dev_to_shost(r->dev.parent)

/*
 * FC SCSI Target Attributes
 *
 * The SCSI Target is considered an extention of a remote port (as
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
	u32 supported_classes;
	u8  supported_fc4s[FC_FC4_LIST_SIZE];
	char symbolic_name[FC_SYMBOLIC_NAME_SIZE];
	u32 supported_speeds;
	u32 maxframe_size;
	char serial_number[FC_SERIAL_NUMBER_SIZE];

	/* Dynamic Attributes */
	u32 port_id;
	enum fc_port_type port_type;
	enum fc_port_state port_state;
	u8  active_fc4s[FC_FC4_LIST_SIZE];
	u32 speed;
	u64 fabric_name;

	/* Private (Transport-managed) Attributes */
	enum fc_tgtid_binding_type  tgtid_bind_type;

	/* internal data */
	struct list_head rports;
	struct list_head rport_bindings;
	u32 next_rport_number;
	u32 next_target_id;
	u8 flags;
 	struct work_struct rport_del_work;
};

/* values for struct fc_host_attrs "flags" field: */
#define FC_SHOST_RPORT_DEL_SCHEDULED	0x01


#define fc_host_node_name(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->node_name)
#define fc_host_port_name(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->port_name)
#define fc_host_supported_classes(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->supported_classes)
#define fc_host_supported_fc4s(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->supported_fc4s)
#define fc_host_symbolic_name(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->symbolic_name)
#define fc_host_supported_speeds(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->supported_speeds)
#define fc_host_maxframe_size(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->maxframe_size)
#define fc_host_serial_number(x)	\
	(((struct fc_host_attrs *)(x)->shost_data)->serial_number)
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
#define fc_host_tgtid_bind_type(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->tgtid_bind_type)
#define fc_host_rports(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->rports)
#define fc_host_rport_bindings(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->rport_bindings)
#define fc_host_next_rport_number(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->next_rport_number)
#define fc_host_next_target_id(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->next_target_id)
#define fc_host_flags(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->flags)
#define fc_host_rport_del_work(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->rport_del_work)


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

	struct fc_host_statistics * (*get_fc_host_stats)(struct Scsi_Host *);
	void	(*reset_fc_host_stats)(struct Scsi_Host *);

	int	(*issue_fc_host_lip)(struct Scsi_Host *);

	/* allocation lengths for host-specific data */
	u32	 			dd_fcrport_size;

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
	unsigned long	show_host_supported_classes:1;
	unsigned long	show_host_supported_fc4s:1;
	unsigned long	show_host_symbolic_name:1;
	unsigned long	show_host_supported_speeds:1;
	unsigned long	show_host_maxframe_size:1;
	unsigned long	show_host_serial_number:1;
	/* host dynamic attributes */
	unsigned long	show_host_port_id:1;
	unsigned long	show_host_port_type:1;
	unsigned long	show_host_port_state:1;
	unsigned long	show_host_active_fc4s:1;
	unsigned long	show_host_speed:1;
	unsigned long	show_host_fabric_name:1;
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
		result = 0;
		break;
	case FC_PORTSTATE_BLOCKED:
		result = DID_BUS_BUSY << 16;
		break;
	default:
		result = DID_NO_CONNECT << 16;
		break;
	}
	return result;
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

static inline u64 wwn_to_u64(u8 *wwn)
{
	return (u64)wwn[0] << 56 | (u64)wwn[1] << 48 |
	    (u64)wwn[2] << 40 | (u64)wwn[3] << 32 |
	    (u64)wwn[4] << 24 | (u64)wwn[5] << 16 |
	    (u64)wwn[6] <<  8 | (u64)wwn[7];
}

#endif /* SCSI_TRANSPORT_FC_H */
