#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/filter.h>

#include "bpf_helpers.h"

/* compiler workaround */
#define _htonl __builtin_bswap32

static inline void set_dst_mac(struct __sk_buff *skb, char *mac)
{
	bpf_skb_store_bytes(skb, 0, mac, ETH_ALEN, 1);
}

#define IP_CSUM_OFF (ETH_HLEN + offsetof(struct iphdr, check))
#define TOS_OFF (ETH_HLEN + offsetof(struct iphdr, tos))

static inline void set_ip_tos(struct __sk_buff *skb, __u8 new_tos)
{
	__u8 old_tos = load_byte(skb, BPF_LL_OFF + TOS_OFF);

	bpf_l3_csum_replace(skb, IP_CSUM_OFF, htons(old_tos), htons(new_tos), 2);
	bpf_skb_store_bytes(skb, TOS_OFF, &new_tos, sizeof(new_tos), 0);
}

#define TCP_CSUM_OFF (ETH_HLEN + sizeof(struct iphdr) + offsetof(struct tcphdr, check))
#define IP_SRC_OFF (ETH_HLEN + offsetof(struct iphdr, saddr))

#define IS_PSEUDO 0x10

static inline void set_tcp_ip_src(struct __sk_buff *skb, __u32 new_ip)
{
	__u32 old_ip = _htonl(load_word(skb, BPF_LL_OFF + IP_SRC_OFF));

	bpf_l4_csum_replace(skb, TCP_CSUM_OFF, old_ip, new_ip, IS_PSEUDO | sizeof(new_ip));
	bpf_l3_csum_replace(skb, IP_CSUM_OFF, old_ip, new_ip, sizeof(new_ip));
	bpf_skb_store_bytes(skb, IP_SRC_OFF, &new_ip, sizeof(new_ip), 0);
}

#define TCP_DPORT_OFF (ETH_HLEN + sizeof(struct iphdr) + offsetof(struct tcphdr, dest))
static inline void set_tcp_dest_port(struct __sk_buff *skb, __u16 new_port)
{
	__u16 old_port = htons(load_half(skb, BPF_LL_OFF + TCP_DPORT_OFF));

	bpf_l4_csum_replace(skb, TCP_CSUM_OFF, old_port, new_port, sizeof(new_port));
	bpf_skb_store_bytes(skb, TCP_DPORT_OFF, &new_port, sizeof(new_port), 0);
}

SEC("classifier")
int bpf_prog1(struct __sk_buff *skb)
{
	__u8 proto = load_byte(skb, BPF_LL_OFF + ETH_HLEN + offsetof(struct iphdr, protocol));
	long *value;

	if (proto == IPPROTO_TCP) {
		set_ip_tos(skb, 8);
		set_tcp_ip_src(skb, 0xA010101);
		set_tcp_dest_port(skb, 5001);
	}

	return 0;
}
char _license[] SEC("license") = "GPL";
