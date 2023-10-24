// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "cgroup_tcp_skb.h"

char _license[] SEC("license") = "GPL";

__u16 g_sock_port = 0;
__u32 g_sock_state = 0;
int g_unexpected = 0;
__u32 g_packet_count = 0;

int needed_tcp_pkt(struct __sk_buff *skb, struct tcphdr *tcph)
{
	struct ipv6hdr ip6h;

	if (skb->protocol != bpf_htons(ETH_P_IPV6))
		return 0;
	if (bpf_skb_load_bytes(skb, 0, &ip6h, sizeof(ip6h)))
		return 0;

	if (ip6h.nexthdr != IPPROTO_TCP)
		return 0;

	if (bpf_skb_load_bytes(skb, sizeof(ip6h), tcph, sizeof(*tcph)))
		return 0;

	if (tcph->source != bpf_htons(g_sock_port) &&
	    tcph->dest != bpf_htons(g_sock_port))
		return 0;

	return 1;
}

/* Run accept() on a socket in the cgroup to receive a new connection. */
static int egress_accept(struct tcphdr *tcph)
{
	if (g_sock_state ==  SYN_RECV_SENDING_SYN_ACK) {
		if (tcph->fin || !tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = SYN_RECV;
		return 1;
	}

	return 0;
}

static int ingress_accept(struct tcphdr *tcph)
{
	switch (g_sock_state) {
	case INIT:
		if (!tcph->syn || tcph->fin || tcph->ack)
			g_unexpected++;
		else
			g_sock_state = SYN_RECV_SENDING_SYN_ACK;
		break;
	case SYN_RECV:
		if (tcph->fin || tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = ESTABLISHED;
		break;
	default:
		return 0;
	}

	return 1;
}

/* Run connect() on a socket in the cgroup to start a new connection. */
static int egress_connect(struct tcphdr *tcph)
{
	if (g_sock_state == INIT) {
		if (!tcph->syn || tcph->fin || tcph->ack)
			g_unexpected++;
		else
			g_sock_state = SYN_SENT;
		return 1;
	}

	return 0;
}

static int ingress_connect(struct tcphdr *tcph)
{
	if (g_sock_state == SYN_SENT) {
		if (tcph->fin || !tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = ESTABLISHED;
		return 1;
	}

	return 0;
}

/* The connection is closed by the peer outside the cgroup. */
static int egress_close_remote(struct tcphdr *tcph)
{
	switch (g_sock_state) {
	case ESTABLISHED:
		break;
	case CLOSE_WAIT_SENDING_ACK:
		if (tcph->fin || tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = CLOSE_WAIT;
		break;
	case CLOSE_WAIT:
		if (!tcph->fin)
			g_unexpected++;
		else
			g_sock_state = LAST_ACK;
		break;
	default:
		return 0;
	}

	return 1;
}

static int ingress_close_remote(struct tcphdr *tcph)
{
	switch (g_sock_state) {
	case ESTABLISHED:
		if (tcph->fin)
			g_sock_state = CLOSE_WAIT_SENDING_ACK;
		break;
	case LAST_ACK:
		if (tcph->fin || tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = CLOSED;
		break;
	default:
		return 0;
	}

	return 1;
}

/* The connection is closed by the endpoint inside the cgroup. */
static int egress_close_local(struct tcphdr *tcph)
{
	switch (g_sock_state) {
	case ESTABLISHED:
		if (tcph->fin)
			g_sock_state = FIN_WAIT1;
		break;
	case TIME_WAIT_SENDING_ACK:
		if (tcph->fin || tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = TIME_WAIT;
		break;
	default:
		return 0;
	}

	return 1;
}

static int ingress_close_local(struct tcphdr *tcph)
{
	switch (g_sock_state) {
	case ESTABLISHED:
		break;
	case FIN_WAIT1:
		if (tcph->fin || tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = FIN_WAIT2;
		break;
	case FIN_WAIT2:
		if (!tcph->fin || tcph->syn || !tcph->ack)
			g_unexpected++;
		else
			g_sock_state = TIME_WAIT_SENDING_ACK;
		break;
	default:
		return 0;
	}

	return 1;
}

/* Check the types of outgoing packets of a server socket to make sure they
 * are consistent with the state of the server socket.
 *
 * The connection is closed by the client side.
 */
SEC("cgroup_skb/egress")
int server_egress(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Egress of the server socket. */
	if (egress_accept(&tcph) || egress_close_remote(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}

/* Check the types of incoming packets of a server socket to make sure they
 * are consistent with the state of the server socket.
 *
 * The connection is closed by the client side.
 */
SEC("cgroup_skb/ingress")
int server_ingress(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Ingress of the server socket. */
	if (ingress_accept(&tcph) || ingress_close_remote(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}

/* Check the types of outgoing packets of a server socket to make sure they
 * are consistent with the state of the server socket.
 *
 * The connection is closed by the server side.
 */
SEC("cgroup_skb/egress")
int server_egress_srv(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Egress of the server socket. */
	if (egress_accept(&tcph) || egress_close_local(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}

/* Check the types of incoming packets of a server socket to make sure they
 * are consistent with the state of the server socket.
 *
 * The connection is closed by the server side.
 */
SEC("cgroup_skb/ingress")
int server_ingress_srv(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Ingress of the server socket. */
	if (ingress_accept(&tcph) || ingress_close_local(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}

/* Check the types of outgoing packets of a client socket to make sure they
 * are consistent with the state of the client socket.
 *
 * The connection is closed by the server side.
 */
SEC("cgroup_skb/egress")
int client_egress_srv(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Egress of the server socket. */
	if (egress_connect(&tcph) || egress_close_remote(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}

/* Check the types of incoming packets of a client socket to make sure they
 * are consistent with the state of the client socket.
 *
 * The connection is closed by the server side.
 */
SEC("cgroup_skb/ingress")
int client_ingress_srv(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Ingress of the server socket. */
	if (ingress_connect(&tcph) || ingress_close_remote(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}

/* Check the types of outgoing packets of a client socket to make sure they
 * are consistent with the state of the client socket.
 *
 * The connection is closed by the client side.
 */
SEC("cgroup_skb/egress")
int client_egress(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Egress of the server socket. */
	if (egress_connect(&tcph) || egress_close_local(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}

/* Check the types of incoming packets of a client socket to make sure they
 * are consistent with the state of the client socket.
 *
 * The connection is closed by the client side.
 */
SEC("cgroup_skb/ingress")
int client_ingress(struct __sk_buff *skb)
{
	struct tcphdr tcph;

	if (!needed_tcp_pkt(skb, &tcph))
		return 1;

	g_packet_count++;

	/* Ingress of the server socket. */
	if (ingress_connect(&tcph) || ingress_close_local(&tcph))
		return 1;

	g_unexpected++;
	return 1;
}
