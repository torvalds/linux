#ifndef __LINUX_PIM_H
#define __LINUX_PIM_H

#include <linux/skbuff.h>
#include <asm/byteorder.h>

/* Message types - V1 */
#define PIM_V1_VERSION		cpu_to_be32(0x10000000)
#define PIM_V1_REGISTER		1

/* Message types - V2 */
#define PIM_VERSION		2

/* RFC7761, sec 4.9:
 *  Type
 *        Types for specific PIM messages.  PIM Types are:
 *
 *  Message Type                          Destination
 *  ---------------------------------------------------------------------
 *  0 = Hello                             Multicast to ALL-PIM-ROUTERS
 *  1 = Register                          Unicast to RP
 *  2 = Register-Stop                     Unicast to source of Register
 *                                        packet
 *  3 = Join/Prune                        Multicast to ALL-PIM-ROUTERS
 *  4 = Bootstrap                         Multicast to ALL-PIM-ROUTERS
 *  5 = Assert                            Multicast to ALL-PIM-ROUTERS
 *  6 = Graft (used in PIM-DM only)       Unicast to RPF'(S)
 *  7 = Graft-Ack (used in PIM-DM only)   Unicast to source of Graft
 *                                        packet
 *  8 = Candidate-RP-Advertisement        Unicast to Domain's BSR
 */
enum {
	PIM_TYPE_HELLO,
	PIM_TYPE_REGISTER,
	PIM_TYPE_REGISTER_STOP,
	PIM_TYPE_JOIN_PRUNE,
	PIM_TYPE_BOOTSTRAP,
	PIM_TYPE_ASSERT,
	PIM_TYPE_GRAFT,
	PIM_TYPE_GRAFT_ACK,
	PIM_TYPE_CANDIDATE_RP_ADV
};

#define PIM_NULL_REGISTER	cpu_to_be32(0x40000000)

/* RFC7761, sec 4.9:
 * The PIM header common to all PIM messages is:
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |PIM Ver| Type  |   Reserved    |           Checksum            |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct pimhdr {
	__u8	type;
	__u8	reserved;
	__be16	csum;
};

/* PIMv2 register message header layout (ietf-draft-idmr-pimvsm-v2-00.ps */
struct pimreghdr {
	__u8	type;
	__u8	reserved;
	__be16	csum;
	__be32	flags;
};

int pim_rcv_v1(struct sk_buff *skb);

static inline bool ipmr_pimsm_enabled(void)
{
	return IS_BUILTIN(CONFIG_IP_PIMSM_V1) || IS_BUILTIN(CONFIG_IP_PIMSM_V2);
}

static inline struct pimhdr *pim_hdr(const struct sk_buff *skb)
{
	return (struct pimhdr *)skb_transport_header(skb);
}

static inline u8 pim_hdr_version(const struct pimhdr *pimhdr)
{
	return pimhdr->type >> 4;
}

static inline u8 pim_hdr_type(const struct pimhdr *pimhdr)
{
	return pimhdr->type & 0xf;
}

/* check if the address is 224.0.0.13, RFC7761 sec 4.3.1 */
static inline bool pim_ipv4_all_pim_routers(__be32 addr)
{
	return addr == htonl(0xE000000D);
}
#endif
