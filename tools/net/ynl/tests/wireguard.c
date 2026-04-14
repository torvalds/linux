// SPDX-License-Identifier: GPL-2.0
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ynl.h>

#include "wireguard-user.h"

static void print_allowed_ip(const struct wireguard_wgallowedip *aip)
{
	char addr_out[INET6_ADDRSTRLEN];

	if (!inet_ntop(aip->family, aip->ipaddr, addr_out, sizeof(addr_out))) {
		addr_out[0] = '?';
		addr_out[1] = '\0';
	}
	printf("\t\t\t%s/%u\n", addr_out, aip->cidr_mask);
}

/* Only printing public key in this demo. For better key formatting,
 * use the constant-time implementation as found in wireguard-tools.
 */
static void print_peer_header(const struct wireguard_wgpeer *peer)
{
	unsigned int len = peer->_len.public_key;
	uint8_t *key = peer->public_key;
	unsigned int i;

	if (len != 32)
		return;
	printf("\tPeer ");
	for (i = 0; i < len; i++)
		printf("%02x", key[i]);
	printf(":\n");
}

static void print_peer(const struct wireguard_wgpeer *peer)
{
	unsigned int i;

	print_peer_header(peer);
	printf("\t\tData: rx: %llu / tx: %llu bytes\n",
	       peer->rx_bytes, peer->tx_bytes);
	printf("\t\tAllowed IPs:\n");
	for (i = 0; i < peer->_count.allowedips; i++)
		print_allowed_ip(&peer->allowedips[i]);
}

static void build_request(struct wireguard_get_device_req *req, char *arg)
{
	char *endptr;
	int ifindex;

	ifindex = strtol(arg, &endptr, 0);
	if (endptr != arg + strlen(arg) || errno != 0)
		ifindex = 0;
	if (ifindex > 0)
		wireguard_get_device_req_set_ifindex(req, ifindex);
	else
		wireguard_get_device_req_set_ifname(req, arg);
}

int main(int argc, char **argv)
{
	struct wireguard_get_device_list *devs;
	struct wireguard_get_device_req *req;
	struct ynl_error yerr;
	struct ynl_sock *ys;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <ifindex|ifname>\n", argv[0]);
		return 1;
	}

	ys = ynl_sock_create(&ynl_wireguard_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 2;
	}

	req = wireguard_get_device_req_alloc();
	build_request(req, argv[1]);

	devs = wireguard_get_device_dump(ys, req);
	if (!devs) {
		fprintf(stderr, "YNL (%d): %s\n", ys->err.code, ys->err.msg);
		wireguard_get_device_req_free(req);
		ynl_sock_destroy(ys);
		return 3;
	}

	ynl_dump_foreach(devs, d) {
		unsigned int i;

		printf("Interface %d: %s\n", d->ifindex, d->ifname);
		for (i = 0; i < d->_count.peers; i++)
			print_peer(&d->peers[i]);
	}

	wireguard_get_device_list_free(devs);
	wireguard_get_device_req_free(req);
	ynl_sock_destroy(ys);

	return 0;
}
