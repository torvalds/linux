#ifndef __LINUX_BRIDGE_EBT_STP_H
#define __LINUX_BRIDGE_EBT_STP_H

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

struct ebt_stp_config_info
{
	uint8_t flags;
	uint16_t root_priol, root_priou;
	char root_addr[6], root_addrmsk[6];
	uint32_t root_costl, root_costu;
	uint16_t sender_priol, sender_priou;
	char sender_addr[6], sender_addrmsk[6];
	uint16_t portl, portu;
	uint16_t msg_agel, msg_ageu;
	uint16_t max_agel, max_ageu;
	uint16_t hello_timel, hello_timeu;
	uint16_t forward_delayl, forward_delayu;
};

struct ebt_stp_info
{
	uint8_t type;
	struct ebt_stp_config_info config;
	uint16_t bitmask;
	uint16_t invflags;
};

#endif
