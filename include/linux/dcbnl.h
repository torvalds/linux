/*
 * Copyright (c) 2008, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Author: Lucy Liu <lucy.liu@intel.com>
 */

#ifndef __LINUX_DCBNL_H__
#define __LINUX_DCBNL_H__

#define DCB_PROTO_VERSION 1

struct dcbmsg {
	unsigned char      dcb_family;
	__u8               cmd;
	__u16              dcb_pad;
};

/**
 * enum dcbnl_commands - supported DCB commands
 *
 * @DCB_CMD_UNDEFINED: unspecified command to catch errors
 * @DCB_CMD_GSTATE: request the state of DCB in the device
 * @DCB_CMD_SSTATE: set the state of DCB in the device
 * @DCB_CMD_PGTX_GCFG: request the priority group configuration for Tx
 * @DCB_CMD_PGTX_SCFG: set the priority group configuration for Tx
 * @DCB_CMD_PGRX_GCFG: request the priority group configuration for Rx
 * @DCB_CMD_PGRX_SCFG: set the priority group configuration for Rx
 * @DCB_CMD_PFC_GCFG: request the priority flow control configuration
 * @DCB_CMD_PFC_SCFG: set the priority flow control configuration
 * @DCB_CMD_SET_ALL: apply all changes to the underlying device
 * @DCB_CMD_GPERM_HWADDR: get the permanent MAC address of the underlying
 *                        device.  Only useful when using bonding.
 */
enum dcbnl_commands {
	DCB_CMD_UNDEFINED,

	DCB_CMD_GSTATE,
	DCB_CMD_SSTATE,

	DCB_CMD_PGTX_GCFG,
	DCB_CMD_PGTX_SCFG,
	DCB_CMD_PGRX_GCFG,
	DCB_CMD_PGRX_SCFG,

	DCB_CMD_PFC_GCFG,
	DCB_CMD_PFC_SCFG,

	DCB_CMD_SET_ALL,
	DCB_CMD_GPERM_HWADDR,

	__DCB_CMD_ENUM_MAX,
	DCB_CMD_MAX = __DCB_CMD_ENUM_MAX - 1,
};


/**
 * enum dcbnl_attrs - DCB top-level netlink attributes
 *
 * @DCB_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_ATTR_IFNAME: interface name of the underlying device (NLA_STRING)
 * @DCB_ATTR_STATE: enable state of DCB in the device (NLA_U8)
 * @DCB_ATTR_PFC_STATE: enable state of PFC in the device (NLA_U8)
 * @DCB_ATTR_PFC_CFG: priority flow control configuration (NLA_NESTED)
 * @DCB_ATTR_NUM_TC: number of traffic classes supported in the device (NLA_U8)
 * @DCB_ATTR_PG_CFG: priority group configuration (NLA_NESTED)
 * @DCB_ATTR_SET_ALL: bool to commit changes to hardware or not (NLA_U8)
 * @DCB_ATTR_PERM_HWADDR: MAC address of the physical device (NLA_NESTED)
 */
enum dcbnl_attrs {
	DCB_ATTR_UNDEFINED,

	DCB_ATTR_IFNAME,
	DCB_ATTR_STATE,
	DCB_ATTR_PFC_STATE,
	DCB_ATTR_PFC_CFG,
	DCB_ATTR_NUM_TC,
	DCB_ATTR_PG_CFG,
	DCB_ATTR_SET_ALL,
	DCB_ATTR_PERM_HWADDR,

	__DCB_ATTR_ENUM_MAX,
	DCB_ATTR_MAX = __DCB_ATTR_ENUM_MAX - 1,
};

/**
 * enum dcbnl_pfc_attrs - DCB Priority Flow Control user priority nested attrs
 *
 * @DCB_PFC_UP_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_PFC_UP_ATTR_0: Priority Flow Control value for User Priority 0 (NLA_U8)
 * @DCB_PFC_UP_ATTR_1: Priority Flow Control value for User Priority 1 (NLA_U8)
 * @DCB_PFC_UP_ATTR_2: Priority Flow Control value for User Priority 2 (NLA_U8)
 * @DCB_PFC_UP_ATTR_3: Priority Flow Control value for User Priority 3 (NLA_U8)
 * @DCB_PFC_UP_ATTR_4: Priority Flow Control value for User Priority 4 (NLA_U8)
 * @DCB_PFC_UP_ATTR_5: Priority Flow Control value for User Priority 5 (NLA_U8)
 * @DCB_PFC_UP_ATTR_6: Priority Flow Control value for User Priority 6 (NLA_U8)
 * @DCB_PFC_UP_ATTR_7: Priority Flow Control value for User Priority 7 (NLA_U8)
 * @DCB_PFC_UP_ATTR_MAX: highest attribute number currently defined
 * @DCB_PFC_UP_ATTR_ALL: apply to all priority flow control attrs (NLA_FLAG)
 *
 */
enum dcbnl_pfc_up_attrs {
	DCB_PFC_UP_ATTR_UNDEFINED,

	DCB_PFC_UP_ATTR_0,
	DCB_PFC_UP_ATTR_1,
	DCB_PFC_UP_ATTR_2,
	DCB_PFC_UP_ATTR_3,
	DCB_PFC_UP_ATTR_4,
	DCB_PFC_UP_ATTR_5,
	DCB_PFC_UP_ATTR_6,
	DCB_PFC_UP_ATTR_7,
	DCB_PFC_UP_ATTR_ALL,

	__DCB_PFC_UP_ATTR_ENUM_MAX,
	DCB_PFC_UP_ATTR_MAX = __DCB_PFC_UP_ATTR_ENUM_MAX - 1,
};

/**
 * enum dcbnl_pg_attrs - DCB Priority Group attributes
 *
 * @DCB_PG_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_PG_ATTR_TC_0: Priority Group Traffic Class 0 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_1: Priority Group Traffic Class 1 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_2: Priority Group Traffic Class 2 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_3: Priority Group Traffic Class 3 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_4: Priority Group Traffic Class 4 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_5: Priority Group Traffic Class 5 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_6: Priority Group Traffic Class 6 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_7: Priority Group Traffic Class 7 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_MAX: highest attribute number currently defined
 * @DCB_PG_ATTR_TC_ALL: apply to all traffic classes (NLA_NESTED)
 * @DCB_PG_ATTR_BW_ID_0: Percent of link bandwidth for Priority Group 0 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_1: Percent of link bandwidth for Priority Group 1 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_2: Percent of link bandwidth for Priority Group 2 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_3: Percent of link bandwidth for Priority Group 3 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_4: Percent of link bandwidth for Priority Group 4 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_5: Percent of link bandwidth for Priority Group 5 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_6: Percent of link bandwidth for Priority Group 6 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_7: Percent of link bandwidth for Priority Group 7 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_MAX: highest attribute number currently defined
 * @DCB_PG_ATTR_BW_ID_ALL: apply to all priority groups (NLA_FLAG)
 *
 */
enum dcbnl_pg_attrs {
	DCB_PG_ATTR_UNDEFINED,

	DCB_PG_ATTR_TC_0,
	DCB_PG_ATTR_TC_1,
	DCB_PG_ATTR_TC_2,
	DCB_PG_ATTR_TC_3,
	DCB_PG_ATTR_TC_4,
	DCB_PG_ATTR_TC_5,
	DCB_PG_ATTR_TC_6,
	DCB_PG_ATTR_TC_7,
	DCB_PG_ATTR_TC_MAX,
	DCB_PG_ATTR_TC_ALL,

	DCB_PG_ATTR_BW_ID_0,
	DCB_PG_ATTR_BW_ID_1,
	DCB_PG_ATTR_BW_ID_2,
	DCB_PG_ATTR_BW_ID_3,
	DCB_PG_ATTR_BW_ID_4,
	DCB_PG_ATTR_BW_ID_5,
	DCB_PG_ATTR_BW_ID_6,
	DCB_PG_ATTR_BW_ID_7,
	DCB_PG_ATTR_BW_ID_MAX,
	DCB_PG_ATTR_BW_ID_ALL,

	__DCB_PG_ATTR_ENUM_MAX,
	DCB_PG_ATTR_MAX = __DCB_PG_ATTR_ENUM_MAX - 1,
};

/**
 * enum dcbnl_tc_attrs - DCB Traffic Class attributes
 *
 * @DCB_TC_ATTR_PARAM_UNDEFINED: unspecified attribute to catch errors
 * @DCB_TC_ATTR_PARAM_PGID: (NLA_U8) Priority group the traffic class belongs to
 *                          Valid values are:  0-7
 * @DCB_TC_ATTR_PARAM_UP_MAPPING: (NLA_U8) Traffic class to user priority map
 *                                Some devices may not support changing the
 *                                user priority map of a TC.
 * @DCB_TC_ATTR_PARAM_STRICT_PRIO: (NLA_U8) Strict priority setting
 *                                 0 - none
 *                                 1 - group strict
 *                                 2 - link strict
 * @DCB_TC_ATTR_PARAM_BW_PCT: optional - (NLA_U8) If supported by the device and
 *                            not configured to use link strict priority,
 *                            this is the percentage of bandwidth of the
 *                            priority group this traffic class belongs to
 * @DCB_TC_ATTR_PARAM_ALL: (NLA_FLAG) all traffic class parameters
 *
 */
enum dcbnl_tc_attrs {
	DCB_TC_ATTR_PARAM_UNDEFINED,

	DCB_TC_ATTR_PARAM_PGID,
	DCB_TC_ATTR_PARAM_UP_MAPPING,
	DCB_TC_ATTR_PARAM_STRICT_PRIO,
	DCB_TC_ATTR_PARAM_BW_PCT,
	DCB_TC_ATTR_PARAM_ALL,

	__DCB_TC_ATTR_PARAM_ENUM_MAX,
	DCB_TC_ATTR_PARAM_MAX = __DCB_TC_ATTR_PARAM_ENUM_MAX - 1,
};

/**
 * enum dcb_general_attr_values - general DCB attribute values
 *
 * @DCB_ATTR_UNDEFINED: value used to indicate an attribute is not supported
 *
 */
enum dcb_general_attr_values {
	DCB_ATTR_VALUE_UNDEFINED = 0xff
};


#endif /* __LINUX_DCBNL_H__ */
