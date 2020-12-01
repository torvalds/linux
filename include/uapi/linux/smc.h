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

/* SMC_GENL_FAMILY commands */
enum {
	SMC_NETLINK_GET_SYS_INFO = 1,
};

/* SMC_GENL_FAMILY top level attributes */
enum {
	SMC_GEN_UNSPEC,
	SMC_GEN_SYS_INFO,		/* nest */
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

#endif /* _UAPI_LINUX_SMC_H */
