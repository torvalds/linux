/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_TB_H
#define LINUX_DEVICE_ID_TB_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define TBSVC_MATCH_PROTOCOL_KEY	0x0001
#define TBSVC_MATCH_PROTOCOL_ID		0x0002
#define TBSVC_MATCH_PROTOCOL_VERSION	0x0004
#define TBSVC_MATCH_PROTOCOL_REVISION	0x0008

/**
 * struct tb_service_id - Thunderbolt service identifiers
 * @match_flags: Flags used to match the structure
 * @protocol_key: Protocol key the service supports
 * @protocol_id: Protocol id the service supports
 * @protocol_version: Version of the protocol
 * @protocol_revision: Revision of the protocol software
 * @driver_data: Driver specific data
 *
 * Thunderbolt XDomain services are exposed as devices where each device
 * carries the protocol information the service supports. Thunderbolt
 * XDomain service drivers match against that information.
 */
struct tb_service_id {
	__u32 match_flags;
	char protocol_key[8 + 1];
	__u32 protocol_id;
	__u32 protocol_version;
	__u32 protocol_revision;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_TB_H */
