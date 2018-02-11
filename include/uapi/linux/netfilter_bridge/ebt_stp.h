/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_BRIDGE_EBT_STP_H
#define __LINUX_BRIDGE_EBT_STP_H

#include <linux/types.h>

#define EBT_STP_TYPE		0x0001

#define EBT_STP_FLAGS		0x0002
#define EBT_STP_ROOTPRIO	0x0004
#define EBT_STP_ROOTADDR	0x0008
#define EBT_STP_ROOTCOST	0x0010
#define EBT_STP_SENDERPRIO	0x0020
#define EBT_STP_SENDERADDR	0x0040
#define EBT_STP_PORT		0x0080
#define EBT_STP_MSGAGE		0x0100
#define EBT_STP_MAXAGE		0x0200
#define EBT_STP_HELLOTIME	0x0400
#define EBT_STP_FWDD		0x0800

#define EBT_STP_MASK		0x0fff
#define EBT_STP_CONFIG_MASK	0x0ffe

#define EBT_STP_MATCH "stp"

struct ebt_stp_config_info {
	__u8 flags;
	__u16 root_priol, root_priou;
	char root_addr[6], root_addrmsk[6];
	__u32 root_costl, root_costu;
	__u16 sender_priol, sender_priou;
	char sender_addr[6], sender_addrmsk[6];
	__u16 portl, portu;
	__u16 msg_agel, msg_ageu;
	__u16 max_agel, max_ageu;
	__u16 hello_timel, hello_timeu;
	__u16 forward_delayl, forward_delayu;
};

struct ebt_stp_info {
	__u8 type;
	struct ebt_stp_config_info config;
	__u16 bitmask;
	__u16 invflags;
};

#endif
