/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2019, 2020 Cloudflare */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>

struct gre_base_hdr {
	uint16_t flags;
	uint16_t protocol;
} __attribute__((packed));

struct guehdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint8_t hlen : 5, control : 1, variant : 2;
#else
	uint8_t variant : 2, control : 1, hlen : 5;
#endif
	uint8_t proto_ctype;
	uint16_t flags;
};

struct unigue {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint8_t _r : 2, last_hop_gre : 1, forward_syn : 1, version : 4;
#else
	uint8_t version : 4, forward_syn : 1, last_hop_gre : 1, _r : 2;
#endif
	uint8_t reserved;
	uint8_t next_hop;
	uint8_t hop_count;
	// Next hops go here
} __attribute__((packed));

typedef struct {
	struct ethhdr eth;
	struct iphdr ip;
	struct gre_base_hdr gre;
} __attribute__((packed)) encap_gre_t;

typedef struct {
	struct ethhdr eth;
	struct iphdr ip;
	struct udphdr udp;
	struct guehdr gue;
	struct unigue unigue;
} __attribute__((packed)) encap_headers_t;
