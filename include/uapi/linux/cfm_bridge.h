/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_CFM_BRIDGE_H_
#define _UAPI_LINUX_CFM_BRIDGE_H_

#include <linux/types.h>
#include <linux/if_ether.h>

#define CFM_MAID_LENGTH		48

/* MEP domain */
enum br_cfm_domain {
	BR_CFM_PORT,
	BR_CFM_VLAN,
};

/* MEP direction */
enum br_cfm_mep_direction {
	BR_CFM_MEP_DIRECTION_DOWN,
	BR_CFM_MEP_DIRECTION_UP,
};

#endif
