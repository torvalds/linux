/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file has definitions for the hub and snac interfaces.
 *
 * Copyright (C) 1992 - 1997, 1999, 2000 Silcon Graphics, Inc.
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_SGI_SN_AGENT_H
#define _ASM_SGI_SN_AGENT_H

#include <linux/config.h>
#include <linux/topology.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>

#if defined(CONFIG_SGI_IP27)
#include <asm/sn/sn0/hub.h>
#elif defined(CONFIG_SGI_IP35)
#include <asm/sn/sn1/hub.h>
#endif	/* !CONFIG_SGI_IP27 && !CONFIG_SGI_IP35 */

/*
 * NIC register macros
 */

#if defined(CONFIG_SGI_IP27)
#define HUB_NIC_ADDR(_cpuid) 						   \
	REMOTE_HUB_ADDR(COMPACT_TO_NASID_NODEID(cpu_to_node(_cpuid)),       \
		MD_MLAN_CTL)
#endif

#define SET_HUB_NIC(_my_cpuid, _val) 				  	   \
	(HUB_S(HUB_NIC_ADDR(_my_cpuid), (_val)))

#define SET_MY_HUB_NIC(_v) 					           \
	SET_HUB_NIC(cpuid(), (_v))

#define GET_HUB_NIC(_my_cpuid) 						   \
	(HUB_L(HUB_NIC_ADDR(_my_cpuid)))

#define GET_MY_HUB_NIC() 						   \
	GET_HUB_NIC(cpuid())

#endif /* _ASM_SGI_SN_AGENT_H */
