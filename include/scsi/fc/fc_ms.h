/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2011 Intel Corporation. All rights reserved.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _FC_MS_H_
#define	_FC_MS_H_

#include <linux/types.h>

/*
 * Fibre Channel Services - Management Service (MS)
 * From T11.org FC-GS-4 Rev 7.91 February 4, 2004
 */

/*
 * Fabric Device Management Interface
 */

/*
 * Common-transport sub-type for FDMI
 */
#define	FC_FDMI_SUBTYPE	    0x10 /* fs_ct_hdr.ct_fs_subtype */

/*
 * Management server FDMI specifications.
 */
#define	FDMI_V1	    1 /* FDMI version 1 specifications */
#define	FDMI_V2	    2 /* FDMI version 2 specifications */

/*
 * Management server FDMI Requests.
 */
enum fc_fdmi_req {
	FC_FDMI_GRHL = 0x0100,	/* Get Registered HBA List */
	FC_FDMI_GHAT = 0x0101,	/* Get HBA Attributes */
	FC_FDMI_GRPL = 0x0102,	/* Get Registered Port List */
	FC_FDMI_GPAT = 0x0110,	/* Get Port Attributes */
	FC_FDMI_RHBA = 0x0200,	/* Register HBA */
	FC_FDMI_RHAT = 0x0201,	/* Register HBA Attributes */
	FC_FDMI_RPRT = 0x0210,	/* Register Port */
	FC_FDMI_RPA = 0x0211,	/* Register Port Attributes */
	FC_FDMI_DHBA = 0x0300,	/* Deregister HBA */
	FC_FDMI_DHAT = 0x0301,	/* Deregister HBA Attributes */
	FC_FDMI_DPRT = 0x0310,	/* Deregister Port */
	FC_FDMI_DPA = 0x0311,	/* Deregister Port Attributes */
};

/*
 * HBA Attribute Entry Type
 */
enum fc_fdmi_hba_attr_type {
	FC_FDMI_HBA_ATTR_NODENAME = 0x0001,
	FC_FDMI_HBA_ATTR_MANUFACTURER = 0x0002,
	FC_FDMI_HBA_ATTR_SERIALNUMBER = 0x0003,
	FC_FDMI_HBA_ATTR_MODEL = 0x0004,
	FC_FDMI_HBA_ATTR_MODELDESCRIPTION = 0x0005,
	FC_FDMI_HBA_ATTR_HARDWAREVERSION = 0x0006,
	FC_FDMI_HBA_ATTR_DRIVERVERSION = 0x0007,
	FC_FDMI_HBA_ATTR_OPTIONROMVERSION = 0x0008,
	FC_FDMI_HBA_ATTR_FIRMWAREVERSION = 0x0009,
	FC_FDMI_HBA_ATTR_OSNAMEVERSION = 0x000A,
	FC_FDMI_HBA_ATTR_MAXCTPAYLOAD = 0x000B,
	FC_FDMI_HBA_ATTR_NODESYMBLNAME = 0x000C,
	FC_FDMI_HBA_ATTR_VENDORSPECIFICINFO = 0x000D,
	FC_FDMI_HBA_ATTR_NUMBEROFPORTS = 0x000E,
	FC_FDMI_HBA_ATTR_FABRICNAME = 0x000F,
	FC_FDMI_HBA_ATTR_BIOSVERSION = 0x0010,
	FC_FDMI_HBA_ATTR_BIOSSTATE = 0x0011,
	FC_FDMI_HBA_ATTR_VENDORIDENTIFIER = 0x00E0,
};

/*
 * HBA Attribute Length
 */
#define FC_FDMI_HBA_ATTR_NODENAME_LEN		8
#define FC_FDMI_HBA_ATTR_MANUFACTURER_LEN	64
#define FC_FDMI_HBA_ATTR_SERIALNUMBER_LEN	64
#define FC_FDMI_HBA_ATTR_MODEL_LEN		64
#define FC_FDMI_HBA_ATTR_MODELDESCR_LEN		64
#define FC_FDMI_HBA_ATTR_HARDWAREVERSION_LEN	64
#define FC_FDMI_HBA_ATTR_DRIVERVERSION_LEN	64
#define FC_FDMI_HBA_ATTR_OPTIONROMVERSION_LEN	64
#define FC_FDMI_HBA_ATTR_FIRMWAREVERSION_LEN	64
#define FC_FDMI_HBA_ATTR_OSNAMEVERSION_LEN	128
#define FC_FDMI_HBA_ATTR_MAXCTPAYLOAD_LEN	4
#define FC_FDMI_HBA_ATTR_NODESYMBLNAME_LEN	64
#define FC_FDMI_HBA_ATTR_VENDORSPECIFICINFO_LEN	4
#define FC_FDMI_HBA_ATTR_NUMBEROFPORTS_LEN	4
#define FC_FDMI_HBA_ATTR_FABRICNAME_LEN	8
#define FC_FDMI_HBA_ATTR_BIOSVERSION_LEN	64
#define FC_FDMI_HBA_ATTR_BIOSSTATE_LEN    4
#define FC_FDMI_HBA_ATTR_VENDORIDENTIFIER_LEN 8

/*
 * Port Attribute Type
 */
enum fc_fdmi_port_attr_type {
	FC_FDMI_PORT_ATTR_FC4TYPES = 0x0001,
	FC_FDMI_PORT_ATTR_SUPPORTEDSPEED = 0x0002,
	FC_FDMI_PORT_ATTR_CURRENTPORTSPEED = 0x0003,
	FC_FDMI_PORT_ATTR_MAXFRAMESIZE = 0x0004,
	FC_FDMI_PORT_ATTR_OSDEVICENAME = 0x0005,
	FC_FDMI_PORT_ATTR_HOSTNAME = 0x0006,
	FC_FDMI_PORT_ATTR_NODENAME = 0x0007,
	FC_FDMI_PORT_ATTR_PORTNAME = 0x0008,
	FC_FDMI_PORT_ATTR_SYMBOLICNAME = 0x0009,
	FC_FDMI_PORT_ATTR_PORTTYPE = 0x000A,
	FC_FDMI_PORT_ATTR_SUPPORTEDCLASSSRVC = 0x000B,
	FC_FDMI_PORT_ATTR_FABRICNAME = 0x000C,
	FC_FDMI_PORT_ATTR_CURRENTFC4TYPE = 0x000D,
	FC_FDMI_PORT_ATTR_PORTSTATE = 0x101,
	FC_FDMI_PORT_ATTR_DISCOVEREDPORTS = 0x102,
	FC_FDMI_PORT_ATTR_PORTID = 0x103,
};

/*
 * Port Attribute Length
 */
#define FC_FDMI_PORT_ATTR_FC4TYPES_LEN		32
#define FC_FDMI_PORT_ATTR_SUPPORTEDSPEED_LEN	4
#define FC_FDMI_PORT_ATTR_CURRENTPORTSPEED_LEN	4
#define FC_FDMI_PORT_ATTR_MAXFRAMESIZE_LEN	4
#define FC_FDMI_PORT_ATTR_OSDEVICENAME_LEN	256
#define FC_FDMI_PORT_ATTR_HOSTNAME_LEN		256
#define FC_FDMI_PORT_ATTR_NODENAME_LEN		8
#define FC_FDMI_PORT_ATTR_PORTNAME_LEN		8
#define FC_FDMI_PORT_ATTR_SYMBOLICNAME_LEN	256
#define FC_FDMI_PORT_ATTR_PORTTYPE_LEN		4
#define FC_FDMI_PORT_ATTR_SUPPORTEDCLASSSRVC_LEN	4
#define FC_FDMI_PORT_ATTR_FABRICNAME_LEN	8
#define FC_FDMI_PORT_ATTR_CURRENTFC4TYPE_LEN	32
#define FC_FDMI_PORT_ATTR_PORTSTATE_LEN		4
#define FC_FDMI_PORT_ATTR_DISCOVEREDPORTS_LEN	4
#define FC_FDMI_PORT_ATTR_PORTID_LEN		4


/*
 * HBA Attribute ID
 */
struct fc_fdmi_hba_identifier {
	__be64		id;
};

/*
 * Port Name
 */
struct fc_fdmi_port_name {
	__be64		portname;
};

/*
 * Attribute Entry Block for HBA/Port Attributes
 */
#define FC_FDMI_ATTR_ENTRY_HEADER_LEN	4
struct fc_fdmi_attr_entry {
	__be16		type;
	__be16		len;
	__u8		value[];
} __attribute__((__packed__));

/*
 * Common for HBA/Port Attributes
 */
struct fs_fdmi_attrs {
	__be32				numattrs;
	struct fc_fdmi_attr_entry	attr[];
} __attribute__((__packed__));

/*
 * Registered Port List
 */
struct fc_fdmi_rpl {
	__be32				numport;
	struct fc_fdmi_port_name	port[1];
} __attribute__((__packed__));

/*
 * Register HBA (RHBA)
 */
struct fc_fdmi_rhba {
	struct fc_fdmi_hba_identifier hbaid;
	struct fc_fdmi_rpl		 port;
	struct fs_fdmi_attrs		 hba_attrs;
} __attribute__((__packed__));

/*
 * Register HBA Attributes (RHAT)
 */
struct fc_fdmi_rhat {
	struct fc_fdmi_hba_identifier hbaid;
	struct fs_fdmi_attrs		 hba_attrs;
} __attribute__((__packed__));

/*
 * Register Port (RPRT)
 */
struct fc_fdmi_rprt {
	struct fc_fdmi_hba_identifier hbaid;
	struct fc_fdmi_port_name	 port;
	struct fs_fdmi_attrs		 hba_attrs;
} __attribute__((__packed__));

/*
 * Register Port Attributes (RPA)
 */
struct fc_fdmi_rpa {
	struct fc_fdmi_port_name	 port;
	struct fs_fdmi_attrs		 hba_attrs;
} __attribute__((__packed__));

/*
 * Deregister Port (DPRT)
 */
struct fc_fdmi_dprt {
	struct fc_fdmi_port_name	 port;
} __attribute__((__packed__));

/*
 * Deregister Port Attributes (DPA)
 */
struct fc_fdmi_dpa {
	struct fc_fdmi_port_name	 port;
	struct fs_fdmi_attrs		 hba_attrs;
} __attribute__((__packed__));

/*
 * Deregister HBA Attributes (DHAT)
 */
struct fc_fdmi_dhat {
	struct fc_fdmi_hba_identifier hbaid;
} __attribute__((__packed__));

/*
 * Deregister HBA (DHBA)
 */
struct fc_fdmi_dhba {
	struct fc_fdmi_hba_identifier hbaid;
} __attribute__((__packed__));

#endif /* _FC_MS_H_ */
