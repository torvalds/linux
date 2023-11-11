/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_BRIDGE_EBT_VLAN_H
#define __LINUX_BRIDGE_EBT_VLAN_H

#include <linux/types.h>

#define EBT_VLAN_ID	0x01
#define EBT_VLAN_PRIO	0x02
#define EBT_VLAN_ENCAP	0x04
#define EBT_VLAN_MASK (EBT_VLAN_ID | EBT_VLAN_PRIO | EBT_VLAN_ENCAP)
#define EBT_VLAN_MATCH "vlan"

struct ebt_vlan_info {
	__u16 id;		/* VLAN ID {1-4095} */
	__u8 prio;		/* VLAN User Priority {0-7} */
	__be16 encap;		/* VLAN Encapsulated frame code {0-65535} */
	__u8 bitmask;		/* Args bitmask bit 1=1 - ID arg,
				   bit 2=1 User-Priority arg, bit 3=1 encap*/
	__u8 invflags;		/* Inverse bitmask  bit 1=1 - inversed ID arg, 
				   bit 2=1 - inversed Pirority arg */
};

#endif
