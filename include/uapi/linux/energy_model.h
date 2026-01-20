/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/em.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_ENERGY_MODEL_H
#define _UAPI_LINUX_ENERGY_MODEL_H

#define EM_FAMILY_NAME		"em"
#define EM_FAMILY_VERSION	1

enum {
	EM_A_PDS_PD = 1,

	__EM_A_PDS_MAX,
	EM_A_PDS_MAX = (__EM_A_PDS_MAX - 1)
};

enum {
	EM_A_PD_PAD = 1,
	EM_A_PD_PD_ID,
	EM_A_PD_FLAGS,
	EM_A_PD_CPUS,

	__EM_A_PD_MAX,
	EM_A_PD_MAX = (__EM_A_PD_MAX - 1)
};

enum {
	EM_A_PD_TABLE_PD_ID = 1,
	EM_A_PD_TABLE_PS,

	__EM_A_PD_TABLE_MAX,
	EM_A_PD_TABLE_MAX = (__EM_A_PD_TABLE_MAX - 1)
};

enum {
	EM_A_PS_PAD = 1,
	EM_A_PS_PERFORMANCE,
	EM_A_PS_FREQUENCY,
	EM_A_PS_POWER,
	EM_A_PS_COST,
	EM_A_PS_FLAGS,

	__EM_A_PS_MAX,
	EM_A_PS_MAX = (__EM_A_PS_MAX - 1)
};

enum {
	EM_CMD_GET_PDS = 1,
	EM_CMD_GET_PD_TABLE,
	EM_CMD_PD_CREATED,
	EM_CMD_PD_UPDATED,
	EM_CMD_PD_DELETED,

	__EM_CMD_MAX,
	EM_CMD_MAX = (__EM_CMD_MAX - 1)
};

#define EM_MCGRP_EVENT	"event"

#endif /* _UAPI_LINUX_ENERGY_MODEL_H */
