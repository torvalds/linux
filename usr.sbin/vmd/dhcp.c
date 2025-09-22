/*	$OpenBSD: dhcp.c,v 1.14 2024/09/26 01:45:13 jsg Exp $	*/

/*
 * Copyright (c) 2017 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "dhcp.h"
#include "virtio.h"
#include "vmd.h"

#define OPTIONS_OFFSET	offsetof(struct dhcp_packet, options)
#define OPTIONS_MAX_LEN	\
	(1500 - sizeof(struct ip) - sizeof(struct udphdr) - OPTIONS_OFFSET)

static const uint8_t broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

ssize_t
dhcp_request(struct virtio_dev *dev, char *buf, size_t buflen, char **obuf)
{
	struct vionet_dev	*vionet = NULL;
	unsigned char		*respbuf = NULL, *op, *oe, dhcptype = 0;
	unsigned char		*opts = NULL;
	ssize_t			 offset, optslen, respbuflen = 0;
	struct packet_ctx	 pc;
	struct dhcp_packet	 req, resp;
	struct in_addr		 server_addr, mask, client_addr, requested_addr;
	size_t			 len, resplen, o;
	uint32_t		 ltime;
	struct vmd_vm		*vm;
	const char		*hostname = NULL;

	if (dev->dev_type != VMD_DEVTYPE_NET)
		fatalx("%s: not a network device", __func__);
	vionet = &dev->vionet;

	if (buflen < BOOTP_MIN_LEN + ETHER_HDR_LEN ||
	    buflen > 1500 + ETHER_HDR_LEN)
		return (-1);

	memset(&pc, 0, sizeof(pc));
	if ((offset = decode_hw_header(buf, buflen, 0, &pc, HTYPE_ETHER)) < 0)
		return (-1);

	if (memcmp(pc.pc_dmac, broadcast, ETHER_ADDR_LEN) != 0 &&
	    memcmp(pc.pc_dmac, vionet->hostmac, ETHER_ADDR_LEN) != 0)
		return (-1);

	if (memcmp(pc.pc_smac, vionet->mac, ETHER_ADDR_LEN) != 0)
		return (-1);

	if ((offset = decode_udp_ip_header(buf, buflen, offset, &pc)) < 0)
		return (-1);

	if (ntohs(ss2sin(&pc.pc_src)->sin_port) != CLIENT_PORT ||
	    ntohs(ss2sin(&pc.pc_dst)->sin_port) != SERVER_PORT)
		return (-1);

	/* Only populate the base DHCP fields. Options are parsed separately. */
	if ((size_t)offset + OPTIONS_OFFSET > buflen)
		return (-1);
	memset(&req, 0, sizeof(req));
	memcpy(&req, buf + offset, OPTIONS_OFFSET);

	if (req.op != BOOTREQUEST ||
	    req.htype != pc.pc_htype ||
	    req.hlen != ETHER_ADDR_LEN ||
	    memcmp(vionet->mac, req.chaddr, req.hlen) != 0)
		return (-1);

	/* Ignore unsupported requests for now */
	if (req.ciaddr.s_addr != 0 || req.file[0] != '\0' || req.hops != 0)
		return (-1);

	/*
	 * If packet has data that could be DHCP options, check for the cookie
	 * and then see if the region is still long enough to contain at least
	 * one variable length option (3 bytes). If not, fallback to BOOTP.
	 */
	optslen = buflen - offset - OPTIONS_OFFSET;
	if (optslen > DHCP_OPTIONS_COOKIE_LEN + 3 &&
	    optslen < (ssize_t)OPTIONS_MAX_LEN) {
		opts = buf + offset + OPTIONS_OFFSET;

		if (memcmp(opts, DHCP_OPTIONS_COOKIE,
			DHCP_OPTIONS_COOKIE_LEN) == 0) {
			memset(&requested_addr, 0, sizeof(requested_addr));
			op = opts + DHCP_OPTIONS_COOKIE_LEN;
			oe = opts + optslen;
			while (*op != DHO_END && op + 1 < oe) {
				if (op[0] == DHO_PAD) {
					op++;
					continue;
				}
				if (op + 2 + op[1] > oe)
					break;
				if (op[0] == DHO_DHCP_MESSAGE_TYPE &&
				    op[1] == 1)
					dhcptype = op[2];
				else if (op[0] == DHO_DHCP_REQUESTED_ADDRESS &&
				    op[1] == sizeof(requested_addr))
					memcpy(&requested_addr, &op[2],
					    sizeof(requested_addr));
				op += 2 + op[1];
			}
		}
	}

	memset(&resp, 0, sizeof(resp));
	resp.op = BOOTREPLY;
	resp.htype = req.htype;
	resp.hlen = req.hlen;
	resp.xid = req.xid;

	if (vionet->pxeboot) {
		strlcpy(resp.file, "auto_install", sizeof resp.file);
		vm = vm_getbyvmid(dev->vm_vmid);
		if (vm && res_hnok(vm->vm_params.vmc_params.vcp_name))
			hostname = vm->vm_params.vmc_params.vcp_name;
	}

	if ((client_addr.s_addr = vm_priv_addr(&vionet->local_prefix,
	    dev->vm_vmid, vionet->idx, 1)) == 0)
		return (-1);
	memcpy(&resp.yiaddr, &client_addr,
	    sizeof(client_addr));
	memcpy(&ss2sin(&pc.pc_dst)->sin_addr, &client_addr,
	    sizeof(client_addr));
	ss2sin(&pc.pc_dst)->sin_port = htons(CLIENT_PORT);

	if ((server_addr.s_addr = vm_priv_addr(&vionet->local_prefix,
	    dev->vm_vmid, vionet->idx, 0)) == 0)
		return (-1);
	memcpy(&resp.siaddr, &server_addr, sizeof(server_addr));
	memcpy(&ss2sin(&pc.pc_src)->sin_addr, &server_addr,
	    sizeof(server_addr));
	ss2sin(&pc.pc_src)->sin_port = htons(SERVER_PORT);

	/* Packet is already allocated */
	if (*obuf != NULL)
		goto fail;

	respbuflen = sizeof(resp);
	if ((respbuf = calloc(1, respbuflen)) == NULL)
		goto fail;

	memcpy(&pc.pc_dmac, vionet->mac, sizeof(pc.pc_dmac));
	memcpy(&resp.chaddr, vionet->mac, resp.hlen);
	memcpy(&pc.pc_smac, vionet->mac, sizeof(pc.pc_smac));
	pc.pc_smac[5]++;
	if ((offset = assemble_hw_header(respbuf, respbuflen, 0,
	    &pc, HTYPE_ETHER)) < 0) {
		log_debug("%s: assemble_hw_header failed", __func__);
		goto fail;
	}

	/* Add BOOTP Vendor Extensions (DHCP options) */
	memcpy(&resp.options, DHCP_OPTIONS_COOKIE, DHCP_OPTIONS_COOKIE_LEN);
	o = DHCP_OPTIONS_COOKIE_LEN;

	/* Did we receive a DHCP request or was it just BOOTP? */
	if (dhcptype) {
		/*
		 * There is no need for a real state machine as we always
		 * answer with the same client IP and options for the VM.
		 */
		if (dhcptype == DHCPDISCOVER)
			dhcptype = DHCPOFFER;
		else if (dhcptype == DHCPREQUEST &&
		    (requested_addr.s_addr == 0 ||
		    client_addr.s_addr == requested_addr.s_addr))
			dhcptype = DHCPACK;
		else
			dhcptype = DHCPNAK;

		resp.options[o++] = DHO_DHCP_MESSAGE_TYPE;
		resp.options[o++] = sizeof(dhcptype);
		memcpy(&resp.options[o], &dhcptype, sizeof(dhcptype));
		o += sizeof(dhcptype);

		/* Our lease never changes, use the maximum lease time */
		resp.options[o++] = DHO_DHCP_LEASE_TIME;
		resp.options[o++] = sizeof(ltime);
		ltime = ntohl(0xffffffff);
		memcpy(&resp.options[o], &ltime, sizeof(ltime));
		o += sizeof(ltime);

		resp.options[o++] = DHO_DHCP_SERVER_IDENTIFIER;
		resp.options[o++] = sizeof(server_addr);
		memcpy(&resp.options[o], &server_addr, sizeof(server_addr));
		o += sizeof(server_addr);
	}

	resp.options[o++] = DHO_SUBNET_MASK;
	resp.options[o++] = sizeof(mask);
	mask.s_addr = htonl(0xfffffffe);
	memcpy(&resp.options[o], &mask, sizeof(mask));
	o += sizeof(mask);

	resp.options[o++] = DHO_ROUTERS;
	resp.options[o++] = sizeof(server_addr);
	memcpy(&resp.options[o], &server_addr, sizeof(server_addr));
	o += sizeof(server_addr);

	resp.options[o++] = DHO_DOMAIN_NAME_SERVERS;
	resp.options[o++] = sizeof(server_addr);
	memcpy(&resp.options[o], &server_addr, sizeof(server_addr));
	o += sizeof(server_addr);

	if (hostname != NULL && (len = strlen(hostname)) > 1) {
		/* Check if there's still room for the option type and len (2),
		 * hostname, and a final to-be-added DHO_END (1). */
		if (o + 2 + len + 1 > sizeof(resp.options)) {
			log_debug("%s: hostname too long", __func__);
			goto fail;
		}
		resp.options[o++] = DHO_HOST_NAME;
		resp.options[o++] = len;
		memcpy(&resp.options[o], hostname, len);
		o += len;
	}

	resp.options[o++] = DHO_END;

	resplen = OPTIONS_OFFSET + o;

	/* Minimum packet size */
	if (resplen < BOOTP_MIN_LEN)
		resplen = BOOTP_MIN_LEN;

	if ((offset = assemble_udp_ip_header(respbuf, respbuflen, offset, &pc,
	    (unsigned char *)&resp, resplen)) < 0) {
		log_debug("%s: assemble_udp_ip_header failed", __func__);
		goto fail;
	}

	memcpy(respbuf + offset, &resp, resplen);
	respbuflen = offset + resplen;

	*obuf = respbuf;
	return (respbuflen);
 fail:
	free(respbuf);
	return (-1);
}
