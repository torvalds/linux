/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */

#include <linux/bpf.h>      /* must be before include bpf/... */
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h> /* for struct ethhdr   */
#include <linux/in.h>       /* for IPPROTO_UDP     */
#include <linux/ip.h>       /* for struct iphdr    */
#include <linux/ipv6.h>     /* for struct ipv6hdr  */
#include <linux/udp.h>      /* for struct udphdr   */

#define DEFAULT_ACTION XDP_PASS
#define DNS_PORT 53

// Define XSK MAP to store the descriptors of the user-space sockets
struct {
  __uint(type, BPF_MAP_TYPE_XSKMAP);
  __type(key, __u32);
  __type(value, __u32);
  __uint(max_entries, 128);
  /* The commented line about pinning must stay, as it is used to generate
   * a second bpf program */
  /* __uint(pinning, LIBBPF_PIN_BY_NAME); // SEDUNCOMMENTTHIS */
} xsks_map SEC(".maps");

struct vlanhdr {
  __u16 tci;
  __u16 encap_proto;
};

struct cursor {
  void *pos;
  void *end;
};

static __always_inline void cursor_init(struct cursor *c, struct xdp_md *ctx) {
  c->end = (void *)(long)ctx->data_end;
  c->pos = (void *)(long)ctx->data;
}

#define PARSE_FUNC_DECLARATION(STRUCT)                                         \
  static __always_inline struct STRUCT *parse_##STRUCT(struct cursor *c) {     \
    struct STRUCT *ret = c->pos;                                               \
    if (c->pos + sizeof(struct STRUCT) > c->end)                               \
      return 0;                                                                \
    c->pos += sizeof(struct STRUCT);                                           \
    return ret;                                                                \
  }

PARSE_FUNC_DECLARATION(ethhdr)
PARSE_FUNC_DECLARATION(vlanhdr)
PARSE_FUNC_DECLARATION(iphdr)
PARSE_FUNC_DECLARATION(ipv6hdr)
PARSE_FUNC_DECLARATION(udphdr)

// just fixing my editor highlight
#ifdef __NOTHING
#define JUST_FIXING_MY_SYNTAX_HIGHLIGHTING
#endif

static __always_inline struct ethhdr *
parse_eth(struct cursor *c, __u16 *eth_proto) {
  struct ethhdr *eth;

  if (!(eth = parse_ethhdr(c)))
    return 0;

  *eth_proto = eth->h_proto;
  if (*eth_proto == __bpf_htons(ETH_P_8021Q) ||
      *eth_proto == __bpf_htons(ETH_P_8021AD)) {
    struct vlanhdr *vlan;

    if (!(vlan = parse_vlanhdr(c)))
      return 0;

    *eth_proto = vlan->encap_proto;
    if (*eth_proto == __bpf_htons(ETH_P_8021Q) ||
        *eth_proto == __bpf_htons(ETH_P_8021AD)) {
      if (!(vlan = parse_vlanhdr(c)))
        return 0;

      *eth_proto = vlan->encap_proto;
      // TODO: check whether more tags are used?
    }
  }
  return eth;
}

SEC("xdp")
int xdp_dns_redirect(struct xdp_md *ctx) {

  struct cursor c;
  struct ethhdr *eth;
  __u16 eth_proto;
  struct iphdr *ipv4;
  struct ipv6hdr *ipv6;
  struct udphdr *udp;

  __u32 index = ctx->rx_queue_index;

  cursor_init(&c, ctx);

  if (!(eth = parse_eth(&c, &eth_proto)))
    return DEFAULT_ACTION;

  if (eth_proto == __bpf_htons(ETH_P_IP)) {
    if (!(ipv4 = parse_iphdr(&c)) || ipv4->protocol != IPPROTO_UDP)
      return DEFAULT_ACTION;

    if (!(udp = parse_udphdr(&c)) || udp->dest != __bpf_htons(DNS_PORT))
      return DEFAULT_ACTION;

    // NOTE: Maybe not use goto and just have the redirect call here and in IPv6?
    goto redirect_map;

  } else if (eth_proto == __bpf_htons(ETH_P_IPV6)) {
    if (!(ipv6 = parse_ipv6hdr(&c)) || ipv6->nexthdr != IPPROTO_UDP)
      return DEFAULT_ACTION;

    if (!(udp = parse_udphdr(&c)) || udp->dest != __bpf_htons(DNS_PORT))
      return DEFAULT_ACTION;

    goto redirect_map;

  } else {
    return DEFAULT_ACTION;
  }

redirect_map:
  if (bpf_map_lookup_elem(&xsks_map, &index)) {
    return (int) bpf_redirect_map(&xsks_map, index, XDP_PASS);
  }

  return XDP_PASS;
}

// License needs to be GPL compatible to use bpf_printk. As we're not using
// that currently, the license could be solely BSD.
char _license[] SEC("license") = "Dual BSD/GPL";
