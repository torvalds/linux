/* PTP classifier
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

/* The below program is the bpf_asm (tools/net/) representation of
 * the opcode array in the ptp_filter structure.
 *
 * For convenience, this can easily be altered and reviewed with
 * bpf_asm and bpf_dbg, e.g. `./bpf_asm -c prog` where prog is a
 * simple file containing the below program:
 *
 * ldh [12]                        ; load ethertype
 *
 * ; PTP over UDP over IPv4 over Ethernet
 * test_ipv4:
 *   jneq #0x800, test_ipv6        ; ETH_P_IP ?
 *   ldb [23]                      ; load proto
 *   jneq #17, drop_ipv4           ; IPPROTO_UDP ?
 *   ldh [20]                      ; load frag offset field
 *   jset #0x1fff, drop_ipv4       ; don't allow fragments
 *   ldxb 4*([14]&0xf)             ; load IP header len
 *   ldh [x + 16]                  ; load UDP dst port
 *   jneq #319, drop_ipv4          ; is port PTP_EV_PORT ?
 *   ldh [x + 22]                  ; load payload
 *   and #0xf                      ; mask PTP_CLASS_VMASK
 *   or #0x10                      ; PTP_CLASS_IPV4
 *   ret a                         ; return PTP class
 *   drop_ipv4: ret #0x0           ; PTP_CLASS_NONE
 *
 * ; PTP over UDP over IPv6 over Ethernet
 * test_ipv6:
 *   jneq #0x86dd, test_8021q      ; ETH_P_IPV6 ?
 *   ldb [20]                      ; load proto
 *   jneq #17, drop_ipv6           ; IPPROTO_UDP ?
 *   ldh [56]                      ; load UDP dst port
 *   jneq #319, drop_ipv6          ; is port PTP_EV_PORT ?
 *   ldh [62]                      ; load payload
 *   and #0xf                      ; mask PTP_CLASS_VMASK
 *   or #0x20                      ; PTP_CLASS_IPV6
 *   ret a                         ; return PTP class
 *   drop_ipv6: ret #0x0           ; PTP_CLASS_NONE
 *
 * ; PTP over 802.1Q over Ethernet
 * test_8021q:
 *   jneq #0x8100, test_ieee1588   ; ETH_P_8021Q ?
 *   ldh [16]                      ; load inner type
 *   jneq #0x88f7, drop_ieee1588   ; ETH_P_1588 ?
 *   ldb [18]                      ; load payload
 *   and #0x8                      ; as we don't have ports here, test
 *   jneq #0x0, drop_ieee1588      ; for PTP_GEN_BIT and drop these
 *   ldh [18]                      ; reload payload
 *   and #0xf                      ; mask PTP_CLASS_VMASK
 *   or #0x40                      ; PTP_CLASS_V2_VLAN
 *   ret a                         ; return PTP class
 *
 * ; PTP over Ethernet
 * test_ieee1588:
 *   jneq #0x88f7, drop_ieee1588   ; ETH_P_1588 ?
 *   ldb [14]                      ; load payload
 *   and #0x8                      ; as we don't have ports here, test
 *   jneq #0x0, drop_ieee1588      ; for PTP_GEN_BIT and drop these
 *   ldh [14]                      ; reload payload
 *   and #0xf                      ; mask PTP_CLASS_VMASK
 *   or #0x30                      ; PTP_CLASS_L2
 *   ret a                         ; return PTP class
 *   drop_ieee1588: ret #0x0       ; PTP_CLASS_NONE
 */

#include <linux/skbuff.h>
#include <linux/filter.h>
#include <linux/ptp_classify.h>

static struct sk_filter *ptp_insns __read_mostly;

unsigned int ptp_classify_raw(const struct sk_buff *skb)
{
	return SK_RUN_FILTER(ptp_insns, skb);
}
EXPORT_SYMBOL_GPL(ptp_classify_raw);

void __init ptp_classifier_init(void)
{
	static struct sock_filter ptp_filter[] __initdata = {
		{ 0x28,  0,  0, 0x0000000c },
		{ 0x15,  0, 12, 0x00000800 },
		{ 0x30,  0,  0, 0x00000017 },
		{ 0x15,  0,  9, 0x00000011 },
		{ 0x28,  0,  0, 0x00000014 },
		{ 0x45,  7,  0, 0x00001fff },
		{ 0xb1,  0,  0, 0x0000000e },
		{ 0x48,  0,  0, 0x00000010 },
		{ 0x15,  0,  4, 0x0000013f },
		{ 0x48,  0,  0, 0x00000016 },
		{ 0x54,  0,  0, 0x0000000f },
		{ 0x44,  0,  0, 0x00000010 },
		{ 0x16,  0,  0, 0x00000000 },
		{ 0x06,  0,  0, 0x00000000 },
		{ 0x15,  0,  9, 0x000086dd },
		{ 0x30,  0,  0, 0x00000014 },
		{ 0x15,  0,  6, 0x00000011 },
		{ 0x28,  0,  0, 0x00000038 },
		{ 0x15,  0,  4, 0x0000013f },
		{ 0x28,  0,  0, 0x0000003e },
		{ 0x54,  0,  0, 0x0000000f },
		{ 0x44,  0,  0, 0x00000020 },
		{ 0x16,  0,  0, 0x00000000 },
		{ 0x06,  0,  0, 0x00000000 },
		{ 0x15,  0,  9, 0x00008100 },
		{ 0x28,  0,  0, 0x00000010 },
		{ 0x15,  0, 15, 0x000088f7 },
		{ 0x30,  0,  0, 0x00000012 },
		{ 0x54,  0,  0, 0x00000008 },
		{ 0x15,  0, 12, 0x00000000 },
		{ 0x28,  0,  0, 0x00000012 },
		{ 0x54,  0,  0, 0x0000000f },
		{ 0x44,  0,  0, 0x00000040 },
		{ 0x16,  0,  0, 0x00000000 },
		{ 0x15,  0,  7, 0x000088f7 },
		{ 0x30,  0,  0, 0x0000000e },
		{ 0x54,  0,  0, 0x00000008 },
		{ 0x15,  0,  4, 0x00000000 },
		{ 0x28,  0,  0, 0x0000000e },
		{ 0x54,  0,  0, 0x0000000f },
		{ 0x44,  0,  0, 0x00000030 },
		{ 0x16,  0,  0, 0x00000000 },
		{ 0x06,  0,  0, 0x00000000 },
	};
	struct sock_fprog ptp_prog = {
		.len = ARRAY_SIZE(ptp_filter), .filter = ptp_filter,
	};

	BUG_ON(sk_unattached_filter_create(&ptp_insns, &ptp_prog));
}
