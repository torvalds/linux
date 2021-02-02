/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for generic netlink based configuration of an SMC-R PNET table
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Thomas Richter <tmricht@linux.vnet.ibm.com>
 */

#ifndef _UAPI_LINUX_SMC_H_
#define _UAPI_LINUX_SMC_H_

/* Netlink SMC_PNETID attributes */
enum {
	SMC_PNETID_UNSPEC,
	SMC_PNETID_NAME,
	SMC_PNETID_ETHNAME,
	SMC_PNETID_IBNAME,
	SMC_PNETID_IBPORT,
	__SMC_PNETID_MAX,
	SMC_PNETID_MAX = __SMC_PNETID_MAX - 1
};

enum {				/* SMC PNET Table commands */
	SMC_PNETID_GET = 1,
	SMC_PNETID_ADD,
	SMC_PNETID_DEL,
	SMC_PNETID_FLUSH
};

#define SMCR_GENL_FAMILY_NAME		"SMC_PNETID"
#define SMCR_GENL_FAMILY_VERSION	1

/* gennetlink interface to access non-socket information from SMC module */
#define SMC_GENL_FAMILY_NAME		"SMC_GEN_NETLINK"
#define SMC_GENL_FAMILY_VERSION		1

#define SMC_PCI_ID_STR_LEN		16 /* Max length of pci id string */

/* SMC_GENL_FAMILY commands */
enum {
	SMC_NETLINK_GET_SYS_INFO = 1,
	SMC_NETLINK_GET_LGR_SMCR,
	SMC_NETLINK_GET_LINK_SMCR,
	SMC_NETLINK_GET_LGR_SMCD,
	SMC_NETLINK_GET_DEV_SMCD,
	SMC_NETLINK_GET_DEV_SMCR,
};

/* SMC_GENL_FAMILY top level attributes */
enum {
	SMC_GEN_UNSPEC,
	SMC_GEN_SYS_INFO,		/* nest */
	SMC_GEN_LGR_SMCR,		/* nest */
	SMC_GEN_LINK_SMCR,		/* nest */
	SMC_GEN_LGR_SMCD,		/* nest */
	SMC_GEN_DEV_SMCD,		/* nest */
	SMC_GEN_DEV_SMCR,		/* nest */
	__SMC_GEN_MAX,
	SMC_GEN_MAX = __SMC_GEN_MAX - 1
};

/* SMC_GEN_SYS_INFO attributes */
enum {
	SMC_NLA_SYS_UNSPEC,
	SMC_NLA_SYS_VER,		/* u8 */
	SMC_NLA_SYS_REL,		/* u8 */
	SMC_NLA_SYS_IS_ISM_V2,		/* u8 */
	SMC_NLA_SYS_LOCAL_HOST,		/* string */
	SMC_NLA_SYS_SEID,		/* string */
	__SMC_NLA_SYS_MAX,
	SMC_NLA_SYS_MAX = __SMC_NLA_SYS_MAX - 1
};

/* SMC_NLA_LGR_V2 nested attributes */
enum {
	SMC_NLA_LGR_V2_VER,		/* u8 */
	SMC_NLA_LGR_V2_REL,		/* u8 */
	SMC_NLA_LGR_V2_OS,		/* u8 */
	SMC_NLA_LGR_V2_NEG_EID,		/* string */
	SMC_NLA_LGR_V2_PEER_HOST,	/* string */
};

/* SMC_GEN_LGR_SMCR attributes */
enum {
	SMC_NLA_LGR_R_UNSPEC,
	SMC_NLA_LGR_R_ID,		/* u32 */
	SMC_NLA_LGR_R_ROLE,		/* u8 */
	SMC_NLA_LGR_R_TYPE,		/* u8 */
	SMC_NLA_LGR_R_PNETID,		/* string */
	SMC_NLA_LGR_R_VLAN_ID,		/* u8 */
	SMC_NLA_LGR_R_CONNS_NUM,	/* u32 */
	__SMC_NLA_LGR_R_MAX,
	SMC_NLA_LGR_R_MAX = __SMC_NLA_LGR_R_MAX - 1
};

/* SMC_GEN_LINK_SMCR attributes */
enum {
	SMC_NLA_LINK_UNSPEC,
	SMC_NLA_LINK_ID,		/* u8 */
	SMC_NLA_LINK_IB_DEV,		/* string */
	SMC_NLA_LINK_IB_PORT,		/* u8 */
	SMC_NLA_LINK_GID,		/* string */
	SMC_NLA_LINK_PEER_GID,		/* string */
	SMC_NLA_LINK_CONN_CNT,		/* u32 */
	SMC_NLA_LINK_NET_DEV,		/* u32 */
	SMC_NLA_LINK_UID,		/* u32 */
	SMC_NLA_LINK_PEER_UID,		/* u32 */
	SMC_NLA_LINK_STATE,		/* u32 */
	__SMC_NLA_LINK_MAX,
	SMC_NLA_LINK_MAX = __SMC_NLA_LINK_MAX - 1
};

/* SMC_GEN_LGR_SMCD attributes */
enum {
	SMC_NLA_LGR_D_UNSPEC,
	SMC_NLA_LGR_D_ID,		/* u32 */
	SMC_NLA_LGR_D_GID,		/* u64 */
	SMC_NLA_LGR_D_PEER_GID,		/* u64 */
	SMC_NLA_LGR_D_VLAN_ID,		/* u8 */
	SMC_NLA_LGR_D_CONNS_NUM,	/* u32 */
	SMC_NLA_LGR_D_PNETID,		/* string */
	SMC_NLA_LGR_D_CHID,		/* u16 */
	SMC_NLA_LGR_D_PAD,		/* flag */
	SMC_NLA_LGR_V2,			/* nest */
	__SMC_NLA_LGR_D_MAX,
	SMC_NLA_LGR_D_MAX = __SMC_NLA_LGR_D_MAX - 1
};

/* SMC_NLA_DEV_PORT nested attributes */
enum {
	SMC_NLA_DEV_PORT_UNSPEC,
	SMC_NLA_DEV_PORT_PNET_USR,	/* u8 */
	SMC_NLA_DEV_PORT_PNETID,	/* string */
	SMC_NLA_DEV_PORT_NETDEV,	/* u32 */
	SMC_NLA_DEV_PORT_STATE,		/* u8 */
	SMC_NLA_DEV_PORT_VALID,		/* u8 */
	SMC_NLA_DEV_PORT_LNK_CNT,	/* u32 */
	__SMC_NLA_DEV_PORT_MAX,
	SMC_NLA_DEV_PORT_MAX = __SMC_NLA_DEV_PORT_MAX - 1
};

/* SMC_GEN_DEV_SMCD and SMC_GEN_DEV_SMCR attributes */
enum {
	SMC_NLA_DEV_UNSPEC,
	SMC_NLA_DEV_USE_CNT,		/* u32 */
	SMC_NLA_DEV_IS_CRIT,		/* u8 */
	SMC_NLA_DEV_PCI_FID,		/* u32 */
	SMC_NLA_DEV_PCI_CHID,		/* u16 */
	SMC_NLA_DEV_PCI_VENDOR,		/* u16 */
	SMC_NLA_DEV_PCI_DEVICE,		/* u16 */
	SMC_NLA_DEV_PCI_ID,		/* string */
	SMC_NLA_DEV_PORT,		/* nest */
	SMC_NLA_DEV_PORT2,		/* nest */
	SMC_NLA_DEV_IB_NAME,		/* string */
	__SMC_NLA_DEV_MAX,
	SMC_NLA_DEV_MAX = __SMC_NLA_DEV_MAX - 1
};

#endif /* _UAPI_LINUX_SMC_H */
