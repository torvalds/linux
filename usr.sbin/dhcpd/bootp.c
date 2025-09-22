/*	$OpenBSD: bootp.c,v 1.18 2017/02/13 22:33:39 krw Exp $	*/

/*
 * BOOTP Protocol support.
 */

/*
 * Copyright (c) 1995, 1996, 1998, 1999 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/socket.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

void
bootp(struct packet *packet)
{
	struct host_decl *hp, *host = NULL;
	struct packet outgoing;
	struct dhcp_packet raw;
	struct sockaddr_in to;
	struct in_addr from;
	struct tree_cache *options[256];
	struct shared_network *s;
	struct subnet *subnet = NULL;
	struct lease *lease;
	struct iaddr ip_address;
	int i;

	if (packet->raw->op != BOOTREQUEST)
		return;

	log_info("BOOTREQUEST from %s via %s%s",
	    print_hw_addr(packet->raw->htype, packet->raw->hlen,
	    packet->raw->chaddr), packet->raw->giaddr.s_addr ?
	    inet_ntoa(packet->raw->giaddr) : packet->interface->name,
	    packet->options_valid ? "" : " (non-rfc1048)");

	if (!locate_network(packet))
		return;

	hp = find_hosts_by_haddr(packet->raw->htype, packet->raw->chaddr,
	    packet->raw->hlen);

	s = packet->shared_network;
	lease = find_lease(packet, s, 0);

	/*
	 * Find an IP address in the host_decl that matches the specified
	 * network.
	 */
	if (hp)
		subnet = find_host_for_network(&hp, &ip_address, s);

	if (!subnet) {
		/*
		 * We didn't find an applicable host declaration. Just in case
		 * we may be able to dynamically assign an address, see if
		 * there's a host declaration that doesn't have an ip address
		 * associated with it.
		 */
		if (hp)
			for (; hp; hp = hp->n_ipaddr)
				if (!hp->fixed_addr) {
					host = hp;
					break;
				}

		if (host && (!host->group->allow_booting)) {
			log_info("Ignoring excluded BOOTP client %s",
			    host->name ?  host->name :
			    print_hw_addr (packet->raw->htype,
			    packet->raw->hlen, packet->raw->chaddr));
			return;
		}

		if (host && (!host->group->allow_bootp)) {
			log_info("Ignoring BOOTP request from client %s",
			    host->name ? host->name :
			    print_hw_addr(packet->raw->htype,
			    packet->raw->hlen, packet->raw->chaddr));
			return;
		}

		/*
		 * If we've been told not to boot unknown clients, and we
		 * didn't find any host record for this client, ignore it.
		 */
		if (!host && !(s->group->boot_unknown_clients)) {
			log_info("Ignoring unknown BOOTP client %s via %s",
			    print_hw_addr(packet->raw->htype,
			    packet->raw->hlen, packet->raw->chaddr),
			    packet->raw->giaddr.s_addr ?
			    inet_ntoa(packet->raw->giaddr) :
			    packet->interface->name);
			return;
		}

		/*
		 * If we've been told not to boot with bootp on this network,
		 * ignore it.
		 */
		if (!host && !(s->group->allow_bootp)) {
			log_info("Ignoring BOOTP request from client %s via "
			    "%s", print_hw_addr(packet->raw->htype,
			    packet->raw->hlen, packet->raw->chaddr),
			    packet->raw->giaddr.s_addr ?
			    inet_ntoa(packet->raw->giaddr) :
			    packet->interface->name);
			return;
		}

		/*
		 * If the packet is from a host we don't know and there are no
		 * dynamic bootp addresses on the network it came in on, drop
		 * it on the floor.
		 */
		if (!(s->group->dynamic_bootp)) {
lose:
			log_info("No applicable record for BOOTP host %s via "
			    "%s", print_hw_addr(packet->raw->htype,
			    packet->raw->hlen, packet->raw->chaddr),
			    packet->raw->giaddr.s_addr ?
			    inet_ntoa(packet->raw->giaddr) :
			    packet->interface->name);
			return;
		}

		/*
		 * If a lease has already been assigned to this client and it's
		 * still okay to use dynamic bootp on that lease, reassign it.
		 */
		if (lease) {
			/*
			 * If this lease can be used for dynamic bootp, do so.
			 */
			if ((lease->flags & DYNAMIC_BOOTP_OK)) {
				/*
				 * If it's not a DYNAMIC_BOOTP lease, release
				 * it before reassigning it so that we don't
				 * get a lease conflict.
				 */
				if (!(lease->flags & BOOTP_LEASE))
					release_lease(lease);

				lease->host = host;
				ack_lease(packet, lease, 0, 0);
				return;
			}

			 /*
			  * If dynamic BOOTP is no longer allowed for this
			  * lease, set it free.
			  */
			release_lease(lease);
		}

		/*
		 * If there are dynamic bootp addresses that might be
		 * available, try to snag one.
		 */
		for (lease = s->last_lease;
		    lease && lease->ends <= cur_time;
		    lease = lease->prev) {
			if ((lease->flags & DYNAMIC_BOOTP_OK)) {
				lease->host = host;
				ack_lease(packet, lease, 0, 0);
				return;
			}
		}
		goto lose;
	}

	/* Make sure we're allowed to boot this client. */
	if (hp && (!hp->group->allow_booting)) {
		log_info("Ignoring excluded BOOTP client %s", hp->name);
		return;
	}

	/* Make sure we're allowed to boot this client with bootp. */
	if (hp && (!hp->group->allow_bootp)) {
		log_info("Ignoring BOOTP request from client %s", hp->name);
		return;
	}

	/* Set up the outgoing packet... */
	memset(&outgoing, 0, sizeof outgoing);
	memset(&raw, 0, sizeof raw);
	outgoing.raw = &raw;

	/*
	 * If we didn't get a known vendor magic number on the way in, just
	 * copy the input options to the output.
	 */
	if (!packet->options_valid && !subnet->group->always_reply_rfc1048 &&
	    (!hp || !hp->group->always_reply_rfc1048)) {
		memcpy(outgoing.raw->options, packet->raw->options,
		    DHCP_OPTION_LEN);
		outgoing.packet_length = BOOTP_MIN_LEN;
	} else {
		struct tree_cache netmask_tree;   /*  -- RBF */

		/*
		 * Come up with a list of options that we want to send to this
		 * client. Start with the per-subnet options, and then override
		 * those with client-specific options.
		 */

		memcpy(options, subnet->group->options, sizeof(options));

		for (i = 0; i < 256; i++)
			if (hp->group->options[i])
				options[i] = hp->group->options[i];

		/*
		 * Use the subnet mask from the subnet declaration if no other
		 * mask has been provided.
		 */
		if (!options[DHO_SUBNET_MASK]) {
			options[DHO_SUBNET_MASK] = &netmask_tree;
			netmask_tree.flags = TC_TEMPORARY;
			netmask_tree.value = lease->subnet->netmask.iabuf;
			netmask_tree.len = lease->subnet->netmask.len;
			netmask_tree.buf_size = lease->subnet->netmask.len;
			netmask_tree.timeout = -1;
			netmask_tree.tree = NULL;
		}

		/*
		 * Pack the options into the buffer. Unlike DHCP, we can't pack
		 * options into the filename and server name buffers.
		 */

		outgoing.packet_length = cons_options(packet, outgoing.raw,
		    0, options, 0, 0, 1, NULL, 0);

		if (outgoing.packet_length < BOOTP_MIN_LEN)
			outgoing.packet_length = BOOTP_MIN_LEN;
	}

	/* Take the fields that we care about... */
	raw.op = BOOTREPLY;
	raw.htype = packet->raw->htype;
	raw.hlen = packet->raw->hlen;
	memcpy(raw.chaddr, packet->raw->chaddr, sizeof(raw.chaddr));
	raw.hops = packet->raw->hops;
	raw.xid = packet->raw->xid;
	raw.secs = packet->raw->secs;
	raw.flags = packet->raw->flags;
	raw.ciaddr = packet->raw->ciaddr;
	memcpy(&raw.yiaddr, ip_address.iabuf, sizeof(raw.yiaddr));

	/* Figure out the address of the next server. */
	if (hp && hp->group->next_server.len)
		memcpy(&raw.siaddr, hp->group->next_server.iabuf, 4);
	else if (subnet->group->next_server.len)
		memcpy(&raw.siaddr, subnet->group->next_server.iabuf, 4);
	else if (subnet->interface_address.len)
		memcpy(&raw.siaddr, subnet->interface_address.iabuf, 4);
	else
		raw.siaddr = packet->interface->primary_address;

	raw.giaddr = packet->raw->giaddr;
	if (hp->group->server_name)
		strncpy(raw.sname, hp->group->server_name, sizeof(raw.sname));
	else if (subnet->group->server_name)
		strncpy(raw.sname, subnet->group->server_name,
		    sizeof(raw.sname));

	if (hp->group->filename)
		strncpy(raw.file, hp->group->filename, sizeof(raw.file));
	else if (subnet->group->filename)
		strncpy(raw.file, subnet->group->filename, sizeof(raw.file));
	else
		memcpy(raw.file, packet->raw->file, sizeof(raw.file));

	from = packet->interface->primary_address;

	/* Report what we're doing... */
	log_info("BOOTREPLY for %s to %s (%s) via %s", piaddr(ip_address),
	    hp->name, print_hw_addr(packet->raw->htype, packet->raw->hlen,
	    packet->raw->chaddr), packet->raw->giaddr.s_addr ?
	    inet_ntoa(packet->raw->giaddr) : packet->interface->name);

	/* Set up the parts of the address that are in common. */
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
	to.sin_len = sizeof(to);
#endif

	/* If this was gatewayed, send it back to the gateway... */
	if (raw.giaddr.s_addr) {
		to.sin_addr = raw.giaddr;
		to.sin_port = server_port;

		(void) packet->interface->send_packet(packet->interface, &raw,
		    outgoing.packet_length, from, &to, packet->haddr);
		return;
	}

	/*
	 * If it comes from a client that already knows its address and is not
	 * requesting a broadcast response, and we can unicast to a client
	 * without using the ARP protocol, sent it directly to that client.
	 */
	else if (!(raw.flags & htons(BOOTP_BROADCAST))) {
		to.sin_addr = raw.yiaddr;
		to.sin_port = client_port;
	} else {
		/* Otherwise, broadcast it on the local network. */
		to.sin_addr.s_addr = INADDR_BROADCAST;
		to.sin_port = client_port; /* XXX */
	}

	errno = 0;
	(void) packet->interface->send_packet(packet->interface, &raw,
	    outgoing.packet_length, from, &to, packet->haddr);
}
