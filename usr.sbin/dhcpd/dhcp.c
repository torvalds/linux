/*	$OpenBSD: dhcp.c,v 1.57 2017/07/11 10:28:24 reyk Exp $ */

/*
 * Copyright (c) 1995, 1996, 1997, 1998, 1999
 * The Internet Software Consortium.   All rights reserved.
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

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"
#include "sync.h"

int outstanding_pings;

static char dhcp_message[256];

void
dhcp(struct packet *packet, int is_udpsock)
{
	if (!locate_network(packet) && packet->packet_type != DHCPREQUEST)
		return;

	if (is_udpsock && packet->packet_type != DHCPINFORM) {
		log_info("Unable to handle a DHCP message type=%d on UDP "
		    "socket", packet->packet_type);
		return;
	}

	switch (packet->packet_type) {
	case DHCPDISCOVER:
		dhcpdiscover(packet);
		break;

	case DHCPREQUEST:
		dhcprequest(packet);
		break;

	case DHCPRELEASE:
		dhcprelease(packet);
		break;

	case DHCPDECLINE:
		dhcpdecline(packet);
		break;

	case DHCPINFORM:
		dhcpinform(packet);
		break;

	default:
		break;
	}
}

void
dhcpdiscover(struct packet *packet)
{
	struct lease *lease = find_lease(packet, packet->shared_network, 0);
	struct host_decl *hp;

	log_info("DHCPDISCOVER from %s via %s",
	    print_hw_addr(packet->raw->htype, packet->raw->hlen,
	    packet->raw->chaddr),
	    packet->raw->giaddr.s_addr ? inet_ntoa(packet->raw->giaddr) :
	    packet->interface->name);

	/* Sourceless packets don't make sense here. */
	if (!packet->shared_network) {
		log_info("Packet from unknown subnet: %s",
		    inet_ntoa(packet->raw->giaddr));
		return;
	}

	/* If we didn't find a lease, try to allocate one... */
	if (!lease) {
		lease = packet->shared_network->last_lease;

		/*
		 * If there are no leases in that subnet that have
		 * expired, we have nothing to offer this client.
		 */
		if (!lease || lease->ends > cur_time) {
			log_info("no free leases on subnet %s",
			    packet->shared_network->name);
			return;
		}

		/*
		 * If we find an abandoned lease, take it, but print a
		 * warning message, so that if it continues to lose,
		 * the administrator will eventually investigate.
		 */
		if ((lease->flags & ABANDONED_LEASE)) {
			struct lease *lp;

			/* See if we can find an unabandoned lease first. */
			for (lp = lease; lp; lp = lp->prev) {
				if (lp->ends > cur_time)
					break;
				if (!(lp->flags & ABANDONED_LEASE)) {
					lease = lp;
					break;
				}
			}

			/*
			 * If we can't find an unabandoned lease,
			 * reclaim the abandoned lease.
			 */
			if ((lease->flags & ABANDONED_LEASE)) {
				log_warnx("Reclaiming abandoned IP address %s.",
				    piaddr(lease->ip_addr));
				lease->flags &= ~ABANDONED_LEASE;

				pfmsg('L', lease); /* unabandon address */
			}
		}

		/* Try to find a host_decl that matches the client
		   identifier or hardware address on the packet, and
		   has no fixed IP address.   If there is one, hang
		   it off the lease so that its option definitions
		   can be used. */
		if (((packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len != 0) &&
		    ((hp = find_hosts_by_uid(
		    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].data,
		    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len)) !=
		    NULL)) ||
		    ((hp = find_hosts_by_haddr(packet->raw->htype,
		    packet->raw->chaddr, packet->raw->hlen)) != NULL)) {
			for (; hp; hp = hp->n_ipaddr) {
				if (!hp->fixed_addr) {
					lease->host = hp;
					break;
				}
			}
		} else
			lease->host = NULL;
	}

	/* If this subnet won't boot unknown clients, ignore the
	   request. */
	if (!lease->host &&
	    !lease->subnet->group->boot_unknown_clients) {
		log_info("Ignoring unknown client %s",
		    print_hw_addr(packet->raw->htype, packet->raw->hlen,
		    packet->raw->chaddr));
	} else if (lease->host && !lease->host->group->allow_booting) {
		log_info("Declining to boot client %s",
		    lease->host->name ? lease->host->name :
		    print_hw_addr(packet->raw->htype, packet->raw->hlen,
		    packet->raw->chaddr));
	} else
		ack_lease(packet, lease, DHCPOFFER, cur_time + 120);
}

void
dhcprequest(struct packet *packet)
{
	struct lease *lease;
	struct iaddr cip;
	struct subnet *subnet;
	int ours = 0;

	cip.len = 4;
	if (packet->options[DHO_DHCP_REQUESTED_ADDRESS].len == 4)
		memcpy(cip.iabuf,
		    packet->options[DHO_DHCP_REQUESTED_ADDRESS].data, 4);
	else
		memcpy(cip.iabuf, &packet->raw->ciaddr.s_addr, 4);
	subnet = find_subnet(cip);

	/* Find the lease that matches the address requested by the client. */

	if (subnet)
		lease = find_lease(packet, subnet->shared_network, &ours);
	else
		lease = NULL;

	log_info("DHCPREQUEST for %s from %s via %s", piaddr(cip),
	    print_hw_addr(packet->raw->htype, packet->raw->hlen,
	    packet->raw->chaddr),
	    packet->raw->giaddr.s_addr ? inet_ntoa(packet->raw->giaddr) :
	    packet->interface->name);

	/* If a client on a given network REQUESTs a lease on an
	 * address on a different network, NAK it.  If the Requested
	 * Address option was used, the protocol says that it must
	 * have been broadcast, so we can trust the source network
	 * information.
	 *
	 * If ciaddr was specified and Requested Address was not, then
	 * we really only know for sure what network a packet came from
	 * if it came through a BOOTP gateway - if it came through an
	 * IP router, we'll just have to assume that it's cool.
	 *
	 * If we don't think we know where the packet came from, it
	 * came through a gateway from an unknown network, so it's not
	 * from a RENEWING client.  If we recognize the network it
	 * *thinks* it's on, we can NAK it even though we don't
	 * recognize the network it's *actually* on; otherwise we just
	 * have to ignore it.
	 *
	 * We don't currently try to take advantage of access to the
	 * raw packet, because it's not available on all platforms.
	 * So a packet that was unicast to us through a router from a
	 * RENEWING client is going to look exactly like a packet that
	 * was broadcast to us from an INIT-REBOOT client.
	 *
	 * Since we can't tell the difference between these two kinds
	 * of packets, if the packet appears to have come in off the
	 * local wire, we have to treat it as if it's a RENEWING
	 * client.  This means that we can't NAK a RENEWING client on
	 * the local wire that has a bogus address.  The good news is
	 * that we won't ACK it either, so it should revert to INIT
	 * state and send us a DHCPDISCOVER, which we *can* work with.
	 *
	 * Because we can't detect that a RENEWING client is on the
	 * wrong wire, it's going to sit there trying to renew until
	 * it gets to the REBIND state, when we *can* NAK it because
	 * the packet will get to us through a BOOTP gateway.  We
	 * shouldn't actually see DHCPREQUEST packets from RENEWING
	 * clients on the wrong wire anyway, since their idea of their
	 * local router will be wrong.  In any case, the protocol
	 * doesn't really allow us to NAK a DHCPREQUEST from a
	 * RENEWING client, so we can punt on this issue.
	 */
	if (!packet->shared_network ||
	    (packet->raw->ciaddr.s_addr && packet->raw->giaddr.s_addr) ||
	    (packet->options[DHO_DHCP_REQUESTED_ADDRESS].len == 4 &&
	    !packet->raw->ciaddr.s_addr)) {

		/*
		 * If we don't know where it came from but we do know
		 * where it claims to have come from, it didn't come
		 * from there.   Fry it.
		 */
		if (!packet->shared_network) {
			if (subnet &&
			    subnet->shared_network->group->authoritative) {
				nak_lease(packet, &cip);
				return;
			}
			/* Otherwise, ignore it. */
			return;
		}

		/*
		 * If we do know where it came from and it asked for an
		 * address that is not on that shared network, nak it.
		 */
		subnet = find_grouped_subnet(packet->shared_network, cip);
		if (!subnet) {
			if (packet->shared_network->group->authoritative)
				nak_lease(packet, &cip);
			return;
		}
	}

	/*
	 * If we found a lease for the client but it's not the one the
	 * client asked for, don't send it - some other server probably
	 * made the cut.
	 */
	if (lease && !addr_eq(lease->ip_addr, cip)) {
		/*
		 * If we found the address the client asked for, but
		 * it wasn't what got picked, the lease belongs to us,
		 * so we should NAK it.
		 */
		if (ours)
			nak_lease(packet, &cip);
		return;
	}

	/*
	 * If the address the client asked for is ours, but it wasn't
	 * available for the client, NAK it.
	 */
	if (!lease && ours) {
		nak_lease(packet, &cip);
		return;
	}

	/* If we're not allowed to serve this client anymore, don't. */
	if (lease && !lease->host &&
	    !lease->subnet->group->boot_unknown_clients) {
		log_info("Ignoring unknown client %s",
		    print_hw_addr(packet->raw->htype, packet->raw->hlen,
		    packet->raw->chaddr));
		return;
	} else if (lease && lease->host && !lease->host->group->allow_booting)
		{
		log_info("Declining to renew client %s",
		    lease->host->name ? lease->host->name :
		    print_hw_addr(packet->raw->htype, packet->raw->hlen,
		    packet->raw->chaddr));
		return;
	}

	/*
	 * If we own the lease that the client is asking for,
	 * and it's already been assigned to the client, ack it.
	 */
	if (lease &&
	    ((lease->uid_len && lease->uid_len ==
	    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len &&
	    !memcmp(packet->options[DHO_DHCP_CLIENT_IDENTIFIER].data,
	    lease->uid, lease->uid_len)) ||
	    (lease->hardware_addr.hlen == packet->raw->hlen &&
	    lease->hardware_addr.htype == packet->raw->htype &&
	    !memcmp(lease->hardware_addr.haddr, packet->raw->chaddr,
	    packet->raw->hlen)))) {
		ack_lease(packet, lease, DHCPACK, 0);
		sync_lease(lease);
		return;
	}

	/*
	 * At this point, the client has requested a lease, and it's
	 * available, but it wasn't assigned to the client, which
	 * means that the client probably hasn't gone through the
	 * DHCPDISCOVER part of the protocol.  We are within our
	 * rights to send a DHCPNAK.   We can also send a DHCPACK.
	 * The thing we probably should not do is to remain silent.
	 * For now, we'll just assign the lease to the client anyway.
	 */
	if (lease) {
		ack_lease(packet, lease, DHCPACK, 0);
		sync_lease(lease);
	}
}

void
dhcprelease(struct packet *packet)
{
	char ciaddrbuf[INET_ADDRSTRLEN];
	struct lease *lease;
	struct iaddr cip;
	int i;

	/*
	 * DHCPRELEASE must not specify address in requested-address
	 * option, but old protocol specs weren't explicit about this,
	 * so let it go.
	 */
	if (packet->options[DHO_DHCP_REQUESTED_ADDRESS].len) {
		log_info("DHCPRELEASE from %s specified requested-address.",
		    print_hw_addr(packet->raw->htype, packet->raw->hlen,
		    packet->raw->chaddr));
	}

	i = DHO_DHCP_CLIENT_IDENTIFIER;
	if (packet->options[i].len) {
		lease = find_lease_by_uid(packet->options[i].data,
		    packet->options[i].len);

		/*
		 * See if we can find a lease that matches the
		 * IP address the client is claiming.
		 */
		for (; lease; lease = lease->n_uid) {
			if (!memcmp(&packet->raw->ciaddr,
			    lease->ip_addr.iabuf, 4)) {
				break;
			}
		}
	} else {
		/*
		* The client is supposed to pass a valid client-identifier,
		 * but the spec on this has changed historically, so try the
		 * IP address in ciaddr if the client-identifier fails.
		 */
		cip.len = 4;
		memcpy(cip.iabuf, &packet->raw->ciaddr, 4);
		lease = find_lease_by_ip_addr(cip);
	}

	/* Can't do >1 inet_ntoa() in a printf()! */
	strlcpy(ciaddrbuf, inet_ntoa(packet->raw->ciaddr), sizeof(ciaddrbuf));

	log_info("DHCPRELEASE of %s from %s via %s (%sfound)",
	    ciaddrbuf,
	    print_hw_addr(packet->raw->htype, packet->raw->hlen,
	    packet->raw->chaddr),
	    packet->raw->giaddr.s_addr ? inet_ntoa(packet->raw->giaddr) :
	    packet->interface->name,
	    lease ? "" : "not ");

	/* If we're already acking this lease, don't do it again. */
	if (lease && lease->state) {
		log_info("DHCPRELEASE already acking lease %s",
		    piaddr(lease->ip_addr));
		return;
	}

	/* If we found a lease, release it. */
	if (lease && lease->ends > cur_time) {
		/*
		 * First, we ping this lease to see if it's still
		 * there. if it is, we don't release it. This avoids
		 * the problem of spoofed releases being used to liberate
		 * addresses from the server.
		 */
		if (!lease->releasing) {
			log_info("DHCPRELEASE of %s from %s via %s (found)",
			    ciaddrbuf,
			    print_hw_addr(packet->raw->htype,
			    packet->raw->hlen, packet->raw->chaddr),
			    packet->raw->giaddr.s_addr ?
			    inet_ntoa(packet->raw->giaddr) :
			    packet->interface->name);

			lease->releasing = 1;
			add_timeout(cur_time + 1, lease_ping_timeout, lease);
			icmp_echorequest(&(lease->ip_addr));
			++outstanding_pings;
		} else {
			log_info("DHCPRELEASE of %s from %s via %s ignored "
			    "(release already pending)",
			    ciaddrbuf,
			    print_hw_addr(packet->raw->htype,
			    packet->raw->hlen, packet->raw->chaddr),
			    packet->raw->giaddr.s_addr ?
			    inet_ntoa(packet->raw->giaddr) :
			    packet->interface->name);
		}
	} else {
		log_info("DHCPRELEASE of %s from %s via %s for nonexistent "
		    "lease", ciaddrbuf, print_hw_addr(packet->raw->htype,
		    packet->raw->hlen, packet->raw->chaddr),
		    packet->raw->giaddr.s_addr ?
		    inet_ntoa(packet->raw->giaddr) : packet->interface->name);
	}
}

void
dhcpdecline(struct packet *packet)
{
	struct lease *lease;
	struct iaddr cip;

	/* DHCPDECLINE must specify address. */
	if (packet->options[DHO_DHCP_REQUESTED_ADDRESS].len != 4)
		return;

	cip.len = 4;
	memcpy(cip.iabuf,
	    packet->options[DHO_DHCP_REQUESTED_ADDRESS].data, 4);
	lease = find_lease_by_ip_addr(cip);

	log_info("DHCPDECLINE on %s from %s via %s",
	    piaddr(cip), print_hw_addr(packet->raw->htype,
	    packet->raw->hlen, packet->raw->chaddr),
	    packet->raw->giaddr.s_addr ? inet_ntoa(packet->raw->giaddr) :
	    packet->interface->name);

	/* If we're already acking this lease, don't do it again. */
	if (lease && lease->state) {
		log_info("DHCPDECLINE already acking lease %s",
		    piaddr(lease->ip_addr));
		return;
	}

	/* If we found a lease, mark it as unusable and complain. */
	if (lease)
		abandon_lease(lease, "declined.");
}

void
dhcpinform(struct packet *packet)
{
	struct lease lease;
	struct iaddr cip;
	struct subnet *subnet;

	/*
	 * ciaddr should be set to client's IP address but
	 * not all clients are standards compliant.
	 */
	cip.len = 4;
	if (packet->raw->ciaddr.s_addr && !packet->raw->giaddr.s_addr) {
		if (memcmp(&packet->raw->ciaddr.s_addr,
		    packet->client_addr.iabuf, 4) != 0) {
			log_info("DHCPINFORM from %s but ciaddr %s is not "
			    "consistent with actual address",
			    piaddr(packet->client_addr),
			    inet_ntoa(packet->raw->ciaddr));
			return;
		}
		memcpy(cip.iabuf, &packet->raw->ciaddr.s_addr, 4);
	} else
		memcpy(cip.iabuf, &packet->client_addr.iabuf, 4);

	log_info("DHCPINFORM from %s", piaddr(cip));

	/* Find the lease that matches the address requested by the client. */
	subnet = find_subnet(cip);
	if (!subnet)
		return;

	/* Sourceless packets don't make sense here. */
	if (!subnet->shared_network) {
		log_info("Packet from unknown subnet: %s",
		    inet_ntoa(packet->raw->giaddr));
		return;
	}

	/* Use a fake lease entry */
	memset(&lease, 0, sizeof(lease));
	lease.subnet = subnet;
	lease.shared_network = subnet->shared_network;

	if (packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len)
		lease.host = find_hosts_by_uid(
		    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].data,
		    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len);

	lease.starts = lease.timestamp = lease.ends = MIN_TIME;
	lease.flags = INFORM_NOLEASE;
	ack_lease(packet, &lease, DHCPACK, 0);
	if (lease.state != NULL)
		free_lease_state(lease.state, "ack_lease");
}

void
nak_lease(struct packet *packet, struct iaddr *cip)
{
	struct sockaddr_in to;
	struct in_addr from;
	ssize_t result;
	int i;
	struct dhcp_packet raw;
	unsigned char nak = DHCPNAK;
	struct packet outgoing;
	struct tree_cache *options[256];
	struct tree_cache dhcpnak_tree, dhcpmsg_tree;
	struct tree_cache client_tree, server_tree;

	memset(options, 0, sizeof options);
	memset(&outgoing, 0, sizeof outgoing);
	memset(&raw, 0, sizeof raw);
	outgoing.raw = &raw;

	/* Set DHCP_MESSAGE_TYPE to DHCPNAK */
	i = DHO_DHCP_MESSAGE_TYPE;
	options[i] = &dhcpnak_tree;
	options[i]->value = &nak;
	options[i]->len = sizeof nak;
	options[i]->buf_size = sizeof nak;
	options[i]->timeout = -1;
	options[i]->tree = NULL;
	options[i]->flags = 0;

	/* Set DHCP_MESSAGE to whatever the message is */
	i = DHO_DHCP_MESSAGE;
	options[i] = &dhcpmsg_tree;
	options[i]->value = (unsigned char *)dhcp_message;
	options[i]->len = strlen(dhcp_message);
	options[i]->buf_size = strlen(dhcp_message);
	options[i]->timeout = -1;
	options[i]->tree = NULL;
	options[i]->flags = 0;

	/* Include server identifier in the NAK. At least one
	 * router vendor depends on it when using dhcp relay proxy mode.
	 */
	i = DHO_DHCP_SERVER_IDENTIFIER;
	if (packet->options[i].len) {
		options[i] = &server_tree;
		options[i]->value = packet->options[i].data,
		options[i]->len = packet->options[i].len;
		options[i]->buf_size = packet->options[i].len;
		options[i]->timeout = -1;
		options[i]->tree = NULL;
		options[i]->flags = 0;
	}

	/* Echo back the client-identifier as RFC 6842 mandates. */
	i = DHO_DHCP_CLIENT_IDENTIFIER;
	if (packet->options[i].len) {
		options[i] = &client_tree;
		options[i]->value = packet->options[i].data,
		options[i]->len = packet->options[i].len;
		options[i]->buf_size = packet->options[i].len;
		options[i]->timeout = -1;
		options[i]->tree = NULL;
		options[i]->flags = 0;
	}

	/* Do not use the client's requested parameter list. */
	i = DHO_DHCP_PARAMETER_REQUEST_LIST;
	if (packet->options[i].data) {
		packet->options[i].len = 0;
		free(packet->options[i].data);
		packet->options[i].data = NULL;
	}

	/* Set up the option buffer... */
	outgoing.packet_length = cons_options(packet, outgoing.raw,
	    0, options, 0, 0, 0, NULL, 0);

/*	memset(&raw.ciaddr, 0, sizeof raw.ciaddr);*/
	raw.siaddr = packet->interface->primary_address;
	raw.giaddr = packet->raw->giaddr;
	memcpy(raw.chaddr, packet->raw->chaddr, sizeof raw.chaddr);
	raw.hlen = packet->raw->hlen;
	raw.htype = packet->raw->htype;
	raw.xid = packet->raw->xid;
	raw.secs = packet->raw->secs;
	raw.flags = packet->raw->flags | htons(BOOTP_BROADCAST);
	raw.hops = packet->raw->hops;
	raw.op = BOOTREPLY;

	/* Report what we're sending... */
	log_info("DHCPNAK on %s to %s via %s", piaddr(*cip),
	    print_hw_addr(packet->raw->htype, packet->raw->hlen,
	    packet->raw->chaddr), packet->raw->giaddr.s_addr ?
	    inet_ntoa(packet->raw->giaddr) : packet->interface->name);

	/* Set up the common stuff... */
	memset(&to, 0, sizeof to);
	to.sin_family = AF_INET;
	to.sin_len = sizeof to;

	from = packet->interface->primary_address;

	/* Make sure that the packet is at least as big as a BOOTP packet. */
	if (outgoing.packet_length < BOOTP_MIN_LEN)
		outgoing.packet_length = BOOTP_MIN_LEN;

	/*
	 * If this was gatewayed, send it back to the gateway.
	 * Otherwise, broadcast it on the local network.
	 */
	if (raw.giaddr.s_addr) {
		to.sin_addr = raw.giaddr;
		to.sin_port = server_port;

		result = packet->interface->send_packet(packet->interface, &raw,
		    outgoing.packet_length, from, &to, packet->haddr);
		if (result == -1)
			log_warn("send_fallback");
		return;
	} else {
		to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		to.sin_port = client_port;
	}

	errno = 0;
	result = packet->interface->send_packet(packet->interface, &raw,
	    outgoing.packet_length, from, &to, NULL);
}

void
ack_lease(struct packet *packet, struct lease *lease, unsigned int offer,
    time_t when)
{
	struct lease lt;
	struct lease_state *state;
	time_t lease_time, offered_lease_time, max_lease_time, default_lease_time;
	struct class *vendor_class, *user_class;
	int ulafdr, echo_client_id, i;

	/* If we're already acking this lease, don't do it again. */
	if (lease->state) {
		if ((lease->flags & STATIC_LEASE) ||
		    cur_time - lease->timestamp < 60) {
			log_info("already acking lease %s",
			    piaddr(lease->ip_addr));
			return;
		}
		free_lease_state(lease->state, "ACK timed out");
		lease->state = NULL;
	}

	i = DHO_DHCP_CLASS_IDENTIFIER;
	if (packet->options[i].len) {
		vendor_class = find_class(0, packet->options[i].data,
		    packet->options[i].len);
	} else
		vendor_class = NULL;

	i = DHO_DHCP_USER_CLASS_ID;
	if (packet->options[i].len) {
		user_class = find_class(1, packet->options[i].data,
		    packet->options[i].len);
	} else
		user_class = NULL;

	/*
	 * If there is not a specific host entry, and either the
	 * vendor class or user class (if they exist) deny booting,
	 * then bug out.
	 */
	if (!lease->host) {
		if (vendor_class && !vendor_class->group->allow_booting) {
			log_debug("Booting denied by vendor class");
			return;
		}

		if (user_class && !user_class->group->allow_booting) {
			log_debug("Booting denied by user class");
			return;
		}
	}

	/* Allocate a lease state structure... */
	state = new_lease_state("ack_lease");
	if (!state)
		fatalx("unable to allocate lease state!");
	memset(state, 0, sizeof *state);
	state->got_requested_address = packet->got_requested_address;
	state->shared_network = packet->interface->shared_network;

	/* Remember if we got a server identifier option. */
	i = DHO_DHCP_SERVER_IDENTIFIER;
	if (packet->options[i].len)
		state->got_server_identifier = 1;

	/* Replace the old lease hostname with the new one, if it's changed. */
	i = DHO_HOST_NAME;
	if (packet->options[i].len && lease->client_hostname &&
	    (strlen(lease->client_hostname) == packet->options[i].len) &&
	    !memcmp(lease->client_hostname, packet->options[i].data,
	    packet->options[i].len)) {
	} else if (packet->options[i].len) {
		free(lease->client_hostname);
		lease->client_hostname = malloc( packet->options[i].len + 1);
		if (!lease->client_hostname)
			fatalx("no memory for client hostname.\n");
		memcpy(lease->client_hostname, packet->options[i].data,
		    packet->options[i].len);
		lease->client_hostname[packet->options[i].len] = 0;
	} else if (lease->client_hostname) {
		free(lease->client_hostname);
		lease->client_hostname = 0;
	}

	/* Replace the lease client identifier with a new one. */
	i = DHO_DHCP_CLIENT_IDENTIFIER;
	if (packet->options[i].len && lease->client_identifier &&
	    lease->client_identifier_len == packet->options[i].len &&
	    !memcmp(lease->client_identifier, packet->options[i].data,
	    packet->options[i].len)) {
		/* Same client identifier. */
	} else if (packet->options[i].len) {
		free(lease->client_identifier);
		lease->client_identifier = malloc(packet->options[i].len);
		if (!lease->client_identifier)
			fatalx("no memory for client identifier.\n");
		lease->client_identifier_len = packet->options[i].len;
		memcpy(lease->client_identifier, packet->options[i].data,
		    packet->options[i].len);
	} else if (lease->client_identifier) {
		free(lease->client_identifier);
		lease->client_identifier = NULL;
		lease->client_identifier_len = 0;
	}

	/*
	 * Choose a filename; first from the host_decl, if any, then from
	 * the user class, then from the vendor class.
	 */
	if (lease->host && lease->host->group->filename)
		strlcpy(state->filename, lease->host->group->filename,
		    sizeof state->filename);
	else if (user_class && user_class->group->filename)
		strlcpy(state->filename, user_class->group->filename,
		    sizeof state->filename);
	else if (vendor_class && vendor_class->group->filename)
		strlcpy(state->filename, vendor_class->group->filename,
		    sizeof state->filename);
	else if (packet->raw->file[0])
		strlcpy(state->filename, packet->raw->file,
		    sizeof state->filename);
	else if (lease->subnet->group->filename)
		strlcpy(state->filename, lease->subnet->group->filename,
		    sizeof state->filename);
	else
		strlcpy(state->filename, "", sizeof state->filename);

	/* Choose a server name as above. */
	if (lease->host && lease->host->group->server_name)
		state->server_name = lease->host->group->server_name;
	else if (user_class && user_class->group->server_name)
		state->server_name = user_class->group->server_name;
	else if (vendor_class && vendor_class->group->server_name)
		state->server_name = vendor_class->group->server_name;
	else if (lease->subnet->group->server_name)
		state->server_name = lease->subnet->group->server_name;
	else state->server_name = NULL;

	/*
	 * At this point, we have a lease that we can offer the client.
	 * Now we construct a lease structure that contains what we want,
	 * and call supersede_lease to do the right thing with it.
	 */
	memset(&lt, 0, sizeof lt);

	/*
	 * Use the ip address of the lease that we finally found in
	 * the database.
	 */
	lt.ip_addr = lease->ip_addr;

	/* Start now. */
	lt.starts = cur_time;

	/* Figure out maximum lease time. */
	if (lease->host && lease->host->group->max_lease_time)
		max_lease_time = lease->host->group->max_lease_time;
	else
		max_lease_time = lease->subnet->group->max_lease_time;

	/* Figure out default lease time. */
	if (lease->host && lease->host->group->default_lease_time)
		default_lease_time = lease->host->group->default_lease_time;
	else
		default_lease_time = lease->subnet->group->default_lease_time;

	/*
	 * Figure out how long a lease to assign.    If this is a
	 * dynamic BOOTP lease, its duration must be infinite.
	 */
	if (offer) {
		i = DHO_DHCP_LEASE_TIME;
		if (packet->options[i].len == 4) {
			lease_time = getULong( packet->options[i].data);

			/*
			 * Don't let the client ask for a longer lease than
			 * is supported for this subnet or host.
			 *
			 * time_t is signed, so really large numbers come
			 * back as negative.  Don't allow lease_time of 0,
			 * either.
			 */
			if (lease_time < 1 || lease_time > max_lease_time)
				lease_time = max_lease_time;
		} else
			lease_time = default_lease_time;

		state->offered_expiry = cur_time + lease_time;
		if (when)
			lt.ends = when;
		else
			lt.ends = state->offered_expiry;
	} else {
		if (lease->host &&
		    lease->host->group->bootp_lease_length)
			lt.ends = (cur_time +
			    lease->host->group->bootp_lease_length);
		else if (lease->subnet->group->bootp_lease_length)
			lt.ends = (cur_time +
			    lease->subnet->group->bootp_lease_length);
		else if (lease->host &&
		    lease->host->group->bootp_lease_cutoff)
			lt.ends = lease->host->group->bootp_lease_cutoff;
		else
			lt.ends = lease->subnet->group->bootp_lease_cutoff;
		state->offered_expiry = lt.ends;
		lt.flags = BOOTP_LEASE;
	}

	/* Record the uid, if given... */
	i = DHO_DHCP_CLIENT_IDENTIFIER;
	if (packet->options[i].len) {
		if (packet->options[i].len <= sizeof lt.uid_buf) {
			memcpy(lt.uid_buf, packet->options[i].data,
			    packet->options[i].len);
			lt.uid = lt.uid_buf;
			lt.uid_max = sizeof lt.uid_buf;
			lt.uid_len = packet->options[i].len;
		} else {
			lt.uid_max = lt.uid_len = packet->options[i].len;
			lt.uid = malloc(lt.uid_max);
			if (!lt.uid)
				fatalx("can't allocate memory for large uid.");
			memcpy(lt.uid, packet->options[i].data, lt.uid_len);
		}
	}

	lt.host = lease->host;
	lt.subnet = lease->subnet;
	lt.shared_network = lease->shared_network;

	/* Don't call supersede_lease on a mocked-up lease. */
	if (lease->flags & (STATIC_LEASE | INFORM_NOLEASE)) {
		/* Copy the hardware address into the static lease
		   structure. */
		lease->hardware_addr.hlen = packet->raw->hlen;
		lease->hardware_addr.htype = packet->raw->htype;
		memcpy(lease->hardware_addr.haddr, packet->raw->chaddr,
		    sizeof packet->raw->chaddr); /* XXX */
	} else {
		/* Record the hardware address, if given... */
		lt.hardware_addr.hlen = packet->raw->hlen;
		lt.hardware_addr.htype = packet->raw->htype;
		memcpy(lt.hardware_addr.haddr, packet->raw->chaddr,
		    sizeof packet->raw->chaddr);

		/* Install the new information about this lease in the
		   database.  If this is a DHCPACK or a dynamic BOOTREPLY
		   and we can't write the lease, don't ACK it (or BOOTREPLY
		   it) either. */

		if (!(supersede_lease(lease, &lt, !offer ||
		    offer == DHCPACK) || (offer && offer != DHCPACK))) {
			free_lease_state(state, "ack_lease: !supersede_lease");
			return;
		}
	}

	/* Remember the interface on which the packet arrived. */
	state->ip = packet->interface;

	/* Set a flag if this client is a lame Microsoft client that NUL
	   terminates string options and expects us to do likewise. */
	i = DHO_HOST_NAME;
	if (packet->options[i].len &&
	    packet->options[i].data[packet->options[i].len - 1] == '\0')
		lease->flags |= MS_NULL_TERMINATION;
	else
		lease->flags &= ~MS_NULL_TERMINATION;

	/* Remember the giaddr, xid, secs, flags and hops. */
	state->giaddr = packet->raw->giaddr;
	state->ciaddr = packet->raw->ciaddr;
	state->xid = packet->raw->xid;
	state->secs = packet->raw->secs;
	state->bootp_flags = packet->raw->flags;
	state->hops = packet->raw->hops;
	state->offer = offer;
	memcpy(&state->haddr, packet->haddr, sizeof state->haddr);

	/* Figure out what options to send to the client: */

	/* Start out with the subnet options... */
	memcpy(state->options, lease->subnet->group->options,
	    sizeof state->options);

	/* Vendor and user classes are only supported for DHCP clients. */
	if (state->offer) {
		/* If we have a vendor class, install those options,
		   superseding any subnet options. */
		if (vendor_class) {
			for (i = 0; i < 256; i++)
				if (vendor_class->group->options[i])
					state->options[i] =
					    vendor_class->group->options[i];
		}

		/* If we have a user class, install those options,
		   superseding any subnet and vendor class options. */
		if (user_class) {
			for (i = 0; i < 256; i++)
				if (user_class->group->options[i])
					state->options[i] =
					    user_class->group->options[i];
		}

	}

	/* If we have a host_decl structure, install the associated
	   options, superseding anything that's in the way. */
	if (lease->host) {
		for (i = 0; i < 256; i++)
			if (lease->host->group->options[i])
				state->options[i] =
				    lease->host->group->options[i];
	}

	/* Get the Maximum Message Size option from the packet, if one
	   was sent. */
	i = DHO_DHCP_MAX_MESSAGE_SIZE;
	if (packet->options[i].data &&
	    packet->options[i].len == sizeof(u_int16_t))
		state->max_message_size = getUShort(packet->options[i].data);
	/* Otherwise, if a maximum message size was specified, use that. */
	else if (state->options[i] && state->options[i]->value)
		state->max_message_size = getUShort(state->options[i]->value);

	/* Save the parameter request list if there is one. */
	i = DHO_DHCP_PARAMETER_REQUEST_LIST;
	if (packet->options[i].data) {
		state->prl = calloc(1, packet->options[i].len);
		if (!state->prl)
			log_warnx("no memory for parameter request list");
		else {
			memcpy(state->prl, packet->options[i].data,
			    packet->options[i].len);
			state->prl_len = packet->options[i].len;
		}
	}

	/* If we didn't get a hostname from an option somewhere, see if
	   we can get one from the lease. */
	i = DHO_HOST_NAME;
	if (!state->options[i] && lease->hostname) {
		state->options[i] = new_tree_cache("hostname");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value = (unsigned char *)lease->hostname;
		state->options[i]->len = strlen(lease->hostname);
		state->options[i]->buf_size = state->options[i]->len;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;
	}

	/*
	 * Now, if appropriate, put in DHCP-specific options that
	 * override those.
	 */
	if (state->offer) {
		i = DHO_DHCP_MESSAGE_TYPE;
		state->options[i] = new_tree_cache("message-type");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value = &state->offer;
		state->options[i]->len = sizeof state->offer;
		state->options[i]->buf_size = sizeof state->offer;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;

		i = DHO_DHCP_SERVER_IDENTIFIER;
		if (!state->options[i]) {
		 use_primary:
			state->options[i] = new_tree_cache("server-id");
			state->options[i]->value =
			    (unsigned char *)&state->ip->primary_address;
			state->options[i]->len =
			    sizeof state->ip->primary_address;
			state->options[i]->buf_size = state->options[i]->len;
			state->options[i]->timeout = -1;
			state->options[i]->tree = NULL;
			state->from.len = sizeof state->ip->primary_address;
			memcpy(state->from.iabuf, &state->ip->primary_address,
			    state->from.len);
		} else {
			/* Find the value of the server identifier... */
			if (!tree_evaluate(state->options[i]))
				goto use_primary;
			if (!state->options[i]->value ||
			    (state->options[i]->len >
			    sizeof state->from.iabuf))
				goto use_primary;

			state->from.len = state->options[i]->len;
			memcpy(state->from.iabuf, state->options[i]->value,
			    state->from.len);
		}
		/*
		 * Do not ACK a REQUEST intended for another server.
		 */
		if (packet->options[i].len == 4) {
			if (state->options[i]->len != 4 ||
			    memcmp(packet->options[i].data,
			    state->options[i]->value, 4) != 0) {
				free_lease_state(state, "ack_lease: "
				    "server identifier");
				return;
			}
		}

		/* If we used the vendor class the client specified, we
		   have to return it. */
		if (vendor_class) {
			i = DHO_DHCP_CLASS_IDENTIFIER;
			state->options[i] = new_tree_cache("class-identifier");
			state->options[i]->flags = TC_TEMPORARY;
			state->options[i]->value =
				(unsigned char *)vendor_class->name;
			state->options[i]->len = strlen(vendor_class->name);
			state->options[i]->buf_size = state->options[i]->len;
			state->options[i]->timeout = -1;
			state->options[i]->tree = NULL;
		}

		/* If we used the user class the client specified, we
		   have to return it. */
		if (user_class) {
			i = DHO_DHCP_USER_CLASS_ID;
			state->options[i] = new_tree_cache("user-class");
			state->options[i]->flags = TC_TEMPORARY;
			state->options[i]->value =
				(unsigned char *)user_class->name;
			state->options[i]->len = strlen(user_class->name);
			state->options[i]->buf_size = state->options[i]->len;
			state->options[i]->timeout = -1;
			state->options[i]->tree = NULL;
		}
	}

	/* for DHCPINFORM, don't include lease time parameters */
	if (state->offer && (lease->flags & INFORM_NOLEASE) == 0) {

		/* Sanity check the lease time. */
		if ((state->offered_expiry - cur_time) < 15)
			offered_lease_time = default_lease_time;
		else if (state->offered_expiry - cur_time > max_lease_time)
			offered_lease_time = max_lease_time;
		else
			offered_lease_time =
			    state->offered_expiry - cur_time;

		putULong((unsigned char *)&state->expiry, offered_lease_time);
		i = DHO_DHCP_LEASE_TIME;
		state->options[i] = new_tree_cache("lease-expiry");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value = (unsigned char *)&state->expiry;
		state->options[i]->len = sizeof state->expiry;
		state->options[i]->buf_size = sizeof state->expiry;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;

		/* Renewal time is lease time * 0.5. */
		offered_lease_time /= 2;
		putULong((unsigned char *)&state->renewal, offered_lease_time);
		i = DHO_DHCP_RENEWAL_TIME;
		state->options[i] = new_tree_cache("renewal-time");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value =
			(unsigned char *)&state->renewal;
		state->options[i]->len = sizeof state->renewal;
		state->options[i]->buf_size = sizeof state->renewal;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;


		/* Rebinding time is lease time * 0.875. */
		offered_lease_time += (offered_lease_time / 2 +
		    offered_lease_time / 4);
		putULong((unsigned char *)&state->rebind, offered_lease_time);
		i = DHO_DHCP_REBINDING_TIME;
		state->options[i] = new_tree_cache("rebind-time");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value = (unsigned char *)&state->rebind;
		state->options[i]->len = sizeof state->rebind;
		state->options[i]->buf_size = sizeof state->rebind;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;
	}

	/* Use the subnet mask from the subnet declaration if no other
	   mask has been provided. */
	i = DHO_SUBNET_MASK;
	if (!state->options[i]) {
		state->options[i] = new_tree_cache("subnet-mask");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value = lease->subnet->netmask.iabuf;
		state->options[i]->len = lease->subnet->netmask.len;
		state->options[i]->buf_size = lease->subnet->netmask.len;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;
	}

	/* If so directed, use the leased IP address as the router address.
	   This supposedly makes Win95 machines ARP for all IP addresses,
	   so if the local router does proxy arp, you win. */

	ulafdr = 0;
	if (lease->host) {
		if (lease->host->group->use_lease_addr_for_default_route)
			ulafdr = 1;
	} else if (user_class) {
		if (user_class->group->use_lease_addr_for_default_route)
			ulafdr = 1;
	} else if (vendor_class) {
		if (vendor_class->group->use_lease_addr_for_default_route)
			ulafdr = 1;
	} else if (lease->subnet->group->use_lease_addr_for_default_route)
		ulafdr = 1;
	else
		ulafdr = 0;

	i = DHO_ROUTERS;
	if (ulafdr && !state->options[i]) {
		state->options[i] = new_tree_cache("routers");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value = lease->ip_addr.iabuf;
		state->options[i]->len = lease->ip_addr.len;
		state->options[i]->buf_size = lease->ip_addr.len;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;
	}

	/*
	 * RFC 3046: MUST NOT echo relay agent information if the server
	 * does not understand/use the data. We don't.
	 */
	i = DHO_RELAY_AGENT_INFORMATION;
	memset(&state->options[i], 0, sizeof(state->options[i]));

	/* Echo back the client-identifier as RFC 6842 mandates. */
	if (lease->host)
		echo_client_id = lease->host->group->echo_client_id;
	else if (user_class)
		echo_client_id = user_class->group->echo_client_id;
	else if (vendor_class)
		echo_client_id = vendor_class->group->echo_client_id;
	else
		echo_client_id = lease->subnet->group->echo_client_id;
	i = DHO_DHCP_CLIENT_IDENTIFIER;
	if (lease->client_identifier && echo_client_id) {
		state->options[i] = new_tree_cache("dhcp-client-identifier");
		state->options[i]->flags = TC_TEMPORARY;
		state->options[i]->value = lease->client_identifier;
		state->options[i]->len = lease->client_identifier_len;
		state->options[i]->buf_size = lease->client_identifier_len;
		state->options[i]->timeout = -1;
		state->options[i]->tree = NULL;
	} else
		memset(&state->options[i], 0, sizeof(state->options[i]));

	lease->state = state;

	/* If this is a DHCPOFFER, ping the lease address before actually
	   sending the offer. */
	if (offer == DHCPOFFER && !(lease->flags & STATIC_LEASE) &&
	    cur_time - lease->timestamp > 60) {
		lease->timestamp = cur_time;
		icmp_echorequest(&lease->ip_addr);
		add_timeout(cur_time + 1, lease_ping_timeout, lease);
		++outstanding_pings;
	} else {
		lease->timestamp = cur_time;
		dhcp_reply(lease);
	}
}

void
dhcp_reply(struct lease *lease)
{
	char ciaddrbuf[INET_ADDRSTRLEN];
	int bufs = 0, packet_length, i;
	struct dhcp_packet raw;
	struct sockaddr_in to;
	struct in_addr from;
	struct lease_state *state = lease->state;
	int nulltp, bootpp;
	u_int8_t *prl;
	int prl_len;

	if (!state)
		fatalx("dhcp_reply was supplied lease with no state!");

	/* Compose a response for the client... */
	memset(&raw, 0, sizeof raw);

	/* Copy in the filename if given; otherwise, flag the filename
	   buffer as available for options. */
	if (state->filename[0])
		strlcpy(raw.file, state->filename, sizeof raw.file);
	else
		bufs |= 1;

	/* Copy in the server name if given; otherwise, flag the
	   server_name buffer as available for options. */
	if (state->server_name)
		strlcpy(raw.sname, state->server_name, sizeof raw.sname);
	else
		bufs |= 2; /* XXX */

	memcpy(raw.chaddr, lease->hardware_addr.haddr, sizeof raw.chaddr);
	raw.hlen = lease->hardware_addr.hlen;
	raw.htype = lease->hardware_addr.htype;

	/* See if this is a Microsoft client that NUL-terminates its
	   strings and expects us to do likewise... */
	if (lease->flags & MS_NULL_TERMINATION)
		nulltp = 1;
	else
		nulltp = 0;

	/* See if this is a bootp client... */
	if (state->offer)
		bootpp = 0;
	else
		bootpp = 1;

	if (state->options[DHO_DHCP_PARAMETER_REQUEST_LIST] &&
	    state->options[DHO_DHCP_PARAMETER_REQUEST_LIST]->value) {
		prl = state->options[DHO_DHCP_PARAMETER_REQUEST_LIST]->value;
		prl_len = state->options[DHO_DHCP_PARAMETER_REQUEST_LIST]->len;
	} else if (state->prl) {
		prl = state->prl;
		prl_len = state->prl_len;
	} else {
		prl = NULL;
		prl_len = 0;
	}

	/* Insert such options as will fit into the buffer. */
	packet_length = cons_options(NULL, &raw, state->max_message_size,
	    state->options, bufs, nulltp, bootpp, prl, prl_len);

	/* Having done the cons_options(), we can release the tree_cache
	   entries. */
	for (i = 0; i < 256; i++) {
		if (state->options[i] &&
		    state->options[i]->flags & TC_TEMPORARY)
			free_tree_cache(state->options[i]);
	}

	memcpy(&raw.ciaddr, &state->ciaddr, sizeof raw.ciaddr);
	if ((lease->flags & INFORM_NOLEASE) == 0)
		memcpy(&raw.yiaddr, lease->ip_addr.iabuf, 4);

	/* Figure out the address of the next server. */
	if (lease->host && lease->host->group->next_server.len)
		memcpy(&raw.siaddr, lease->host->group->next_server.iabuf, 4);
	else if (lease->subnet->group->next_server.len)
		memcpy(&raw.siaddr, lease->subnet->group->next_server.iabuf,
		    4);
	else if (lease->subnet->interface_address.len)
		memcpy(&raw.siaddr, lease->subnet->interface_address.iabuf, 4);
	else
		raw.siaddr = state->ip->primary_address;

	raw.giaddr = state->giaddr;

	raw.xid = state->xid;
	raw.secs = state->secs;
	raw.flags = state->bootp_flags;
	raw.hops = state->hops;
	raw.op = BOOTREPLY;

	/* Can't do >1 inet_ntoa() in a printf()! */
	strlcpy(ciaddrbuf, inet_ntoa(state->ciaddr), sizeof(ciaddrbuf));

	/* Say what we're doing... */
	if ((state->offer == DHCPACK) && (lease->flags & INFORM_NOLEASE))
		log_info("DHCPACK to %s (%s) via %s",
		    ciaddrbuf,
		    print_hw_addr(lease->hardware_addr.htype,
		        lease->hardware_addr.hlen, lease->hardware_addr.haddr),
		    state->giaddr.s_addr ? inet_ntoa(state->giaddr) :
		        state->ip->name);
	else
		log_info("%s on %s to %s via %s",
		    (state->offer ? (state->offer == DHCPACK ? "DHCPACK" :
			"DHCPOFFER") : "BOOTREPLY"),
		    piaddr(lease->ip_addr),
		    print_hw_addr(lease->hardware_addr.htype,
		        lease->hardware_addr.hlen, lease->hardware_addr.haddr),
		    state->giaddr.s_addr ? inet_ntoa(state->giaddr) :
		        state->ip->name);

	memset(&to, 0, sizeof to);
	to.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
	to.sin_len = sizeof to;
#endif

	/* Make sure outgoing packets are at least as big
	   as a BOOTP packet. */
	if (packet_length < BOOTP_MIN_LEN)
		packet_length = BOOTP_MIN_LEN;

	/* If this was gatewayed, send it back to the gateway... */
	if (raw.giaddr.s_addr) {
		to.sin_addr = raw.giaddr;
		to.sin_port = server_port;

		memcpy(&from, state->from.iabuf, sizeof from);

		(void) state->ip->send_packet(state->ip, &raw,
		    packet_length, from, &to, &state->haddr);

		free_lease_state(state, "dhcp_reply gateway");
		lease->state = NULL;
		return;

	/* If the client is RENEWING, unicast to the client using the
	   regular IP stack.  Some clients, particularly those that
	   follow RFC1541, are buggy, and send both ciaddr and
	   server-identifier.  We deal with this situation by assuming
	   that if we got both dhcp-server-identifier and ciaddr, and
	   giaddr was not set, then the client is on the local
	   network, and we can therefore unicast or broadcast to it
	   successfully.  A client in REQUESTING state on another
	   network that's making this mistake will have set giaddr,
	   and will therefore get a relayed response from the above
	   code. */
	} else if (raw.ciaddr.s_addr &&
	    !((state->got_server_identifier ||
	    (raw.flags & htons(BOOTP_BROADCAST))) &&
	    /* XXX This won't work if giaddr isn't zero, but it is: */
	    (state->shared_network == lease->shared_network)) &&
	    state->offer == DHCPACK) {
		to.sin_addr = raw.ciaddr;
		to.sin_port = client_port;

	/* If it comes from a client that already knows its address
	   and is not requesting a broadcast response, and we can
	   unicast to a client without using the ARP protocol, sent it
	   directly to that client. */
	} else if (!(raw.flags & htons(BOOTP_BROADCAST))) {
		to.sin_addr = raw.yiaddr;
		to.sin_port = client_port;

	/* Otherwise, broadcast it on the local network. */
	} else {
		to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		to.sin_port = client_port;
		memset(&state->haddr, 0xff, sizeof state->haddr);
	}

	memcpy(&from, state->from.iabuf, sizeof from);

	(void) state->ip->send_packet(state->ip, &raw, packet_length,
	    from, &to, &state->haddr);

	free_lease_state(state, "dhcp_reply");
	lease->state = NULL;
}

struct lease *
find_lease(struct packet *packet, struct shared_network *share,
    int *ours)
{
	struct lease *uid_lease, *ip_lease, *hw_lease;
	struct lease *lease = NULL;
	struct iaddr cip;
	struct host_decl *hp, *host = NULL;
	struct lease *fixed_lease;

	/* Figure out what IP address the client is requesting, if any. */
	if (packet->options[DHO_DHCP_REQUESTED_ADDRESS].len == 4) {
		packet->got_requested_address = 1;
		cip.len = 4;
		memcpy(cip.iabuf,
		    packet->options[DHO_DHCP_REQUESTED_ADDRESS].data,
		    cip.len);
	} else if (packet->raw->ciaddr.s_addr) {
		cip.len = 4;
		memcpy(cip.iabuf, &packet->raw->ciaddr, 4);
	} else
		cip.len = 0;

	/* Try to find a host or lease that's been assigned to the
	   specified unique client identifier. */
	if (packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len) {
		/* First, try to find a fixed host entry for the specified
		   client identifier... */
		hp = find_hosts_by_uid(
		    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].data,
		    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len);
		if (hp) {
			host = hp;
			fixed_lease = mockup_lease(packet, share, hp);
			uid_lease = NULL;
		} else {
			uid_lease = find_lease_by_uid(
			    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].data,
			    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len);
			/* Find the lease matching this uid that's on the
			   network the packet came from (if any). */
			for (; uid_lease; uid_lease = uid_lease->n_uid)
				if (uid_lease->shared_network == share)
					break;
			fixed_lease = NULL;
			if (uid_lease && (uid_lease->flags & ABANDONED_LEASE))
				uid_lease = NULL;
		}
	} else {
		uid_lease = NULL;
		fixed_lease = NULL;
	}

	/* If we didn't find a fixed lease using the uid, try doing
	   it with the hardware address... */
	if (!fixed_lease) {
		hp = find_hosts_by_haddr(packet->raw->htype,
		    packet->raw->chaddr, packet->raw->hlen);
		if (hp) {
			host = hp; /* Save it for later. */
			fixed_lease = mockup_lease(packet, share, hp);
		}
	}

	/* If fixed_lease is present but does not match the requested
	   IP address, and this is a DHCPREQUEST, then we can't return
	   any other lease, so we might as well return now. */
	if (packet->packet_type == DHCPREQUEST && fixed_lease &&
	    (fixed_lease->ip_addr.len != cip.len ||
	    memcmp(fixed_lease->ip_addr.iabuf, cip.iabuf, cip.len))) {
		if (ours)
			*ours = 1;
		strlcpy(dhcp_message, "requested address is incorrect",
		    sizeof(dhcp_message));
		return NULL;
	}

	/* Try to find a lease that's been attached to the client's
	   hardware address... */
	hw_lease = find_lease_by_hw_addr(packet->raw->chaddr,
	    packet->raw->hlen);
	/* Find the lease that's on the network the packet came from
	   (if any). */
	for (; hw_lease; hw_lease = hw_lease->n_hw) {
		if (hw_lease->shared_network == share) {
			if ((hw_lease->flags & ABANDONED_LEASE))
				continue;
			if (packet->packet_type)
				break;
			if (hw_lease->flags &
			    (BOOTP_LEASE | DYNAMIC_BOOTP_OK))
				break;
		}
	}

	/* Try to find a lease that's been allocated to the client's
	   IP address. */
	if (cip.len)
		ip_lease = find_lease_by_ip_addr(cip);
	else
		ip_lease = NULL;

	/* If ip_lease is valid at this point, set ours to one, so that
	   even if we choose a different lease, we know that the address
	   the client was requesting was ours, and thus we can NAK it. */
	if (ip_lease && ours)
		*ours = 1;

	/* If the requested IP address isn't on the network the packet
	   came from, don't use it.  Allow abandoned leases to be matched
	   here - if the client is requesting it, there's a decent chance
	   that it's because the lease database got trashed and a client
	   that thought it had this lease answered an ARP or PING, causing the
	   lease to be abandoned.   If so, this request probably came from
	   that client. */
	if (ip_lease && (ip_lease->shared_network != share)) {
		ip_lease = NULL;
		strlcpy(dhcp_message, "requested address on bad subnet",
		    sizeof(dhcp_message));
	}

	/* Toss ip_lease if it hasn't yet expired and isn't owned by the
	   client. */
	if (ip_lease && ip_lease->ends >= cur_time && ip_lease != uid_lease) {
		int i = DHO_DHCP_CLIENT_IDENTIFIER;

		/* Make sure that ip_lease actually belongs to the client,
		   and toss it if not. */
		if ((ip_lease->uid_len && packet->options[i].data &&
		    ip_lease->uid_len == packet->options[i].len &&
		    !memcmp(packet->options[i].data, ip_lease->uid,
		    ip_lease->uid_len)) ||
		    (!ip_lease->uid_len &&
		    ip_lease->hardware_addr.htype == packet->raw->htype &&
		    ip_lease->hardware_addr.hlen == packet->raw->hlen &&
		    !memcmp(ip_lease->hardware_addr.haddr, packet->raw->chaddr,
		    ip_lease->hardware_addr.hlen))) {
			if (uid_lease) {
				if (uid_lease->ends > cur_time) {
					log_warnx("client %s has duplicate "
					    "leases on %s",
					    print_hw_addr(packet->raw->htype,
					    packet->raw->hlen,
					    packet->raw->chaddr),
					    ip_lease->shared_network->name);

					if (uid_lease &&
					   !packet->raw->ciaddr.s_addr)
						release_lease(uid_lease);
				}
				uid_lease = ip_lease;
			}
		} else {
			strlcpy(dhcp_message, "requested address is not "
			    "available", sizeof(dhcp_message));
			ip_lease = NULL;
		}

		/* If we get to here and fixed_lease is not null, that means
		   that there are both a dynamic lease and a fixed-address
		   declaration for the same IP address. */
		if (packet->packet_type == DHCPREQUEST && fixed_lease) {
			fixed_lease = NULL;
db_conflict:
			log_warnx("Both dynamic and static leases present for "
			    "%s.", piaddr(cip));
			log_warnx("Either remove host declaration %s or "
			    "remove %s", (fixed_lease && fixed_lease->host ?
			    (fixed_lease->host->name ?
			    fixed_lease->host->name : piaddr(cip)) :
			    piaddr(cip)), piaddr(cip));
			log_warnx("from the dynamic address pool for %s",
			    share->name);
			if (fixed_lease)
				ip_lease = NULL;
			strlcpy(dhcp_message, "database conflict - call for "
			    "help!", sizeof(dhcp_message));
		}
	}

	/* If we get to here with both fixed_lease and ip_lease not
	   null, then we have a configuration file bug. */
	if (packet->packet_type == DHCPREQUEST && fixed_lease && ip_lease)
		goto db_conflict;

	/* Toss hw_lease if it hasn't yet expired and the uid doesn't
	   match, except that if the hardware address matches and the
	   client is now doing dynamic BOOTP (and thus hasn't provided
	   a uid) we let the client get away with it. */
	if (hw_lease && hw_lease->ends >= cur_time && hw_lease->uid &&
	    packet->options[DHO_DHCP_CLIENT_IDENTIFIER].len &&
	    hw_lease != uid_lease)
		hw_lease = NULL;

	/* Toss extra pointers to the same lease... */
	if (hw_lease == uid_lease)
		hw_lease = NULL;
	if (ip_lease == hw_lease)
		hw_lease = NULL;
	if (ip_lease == uid_lease)
		uid_lease = NULL;

	/* If we've already eliminated the lease, it wasn't there to
	   begin with.   If we have come up with a matching lease,
	   set the message to bad network in case we have to throw it out. */
	if (!ip_lease) {
		strlcpy(dhcp_message, "requested address not available",
		    sizeof(dhcp_message));
	}

	/* Now eliminate leases that are on the wrong network... */
	if (ip_lease && share != ip_lease->shared_network) {
		if (packet->packet_type == DHCPREQUEST)
			release_lease(ip_lease);
		ip_lease = NULL;
	}
	if (uid_lease && share != uid_lease->shared_network) {
		if (packet->packet_type == DHCPREQUEST)
			release_lease(uid_lease);
		uid_lease = NULL;
	}
	if (hw_lease && share != hw_lease->shared_network) {
		if (packet->packet_type == DHCPREQUEST)
			release_lease(hw_lease);
		hw_lease = NULL;
	}

	/* If this is a DHCPREQUEST, make sure the lease we're going to return
	   matches the requested IP address.   If it doesn't, don't return a
	   lease at all. */
	if (packet->packet_type == DHCPREQUEST && !ip_lease && !fixed_lease)
		return NULL;

	/* At this point, if fixed_lease is nonzero, we can assign it to
	   this client. */
	if (fixed_lease)
		lease = fixed_lease;

	/* If we got a lease that matched the ip address and don't have
	   a better offer, use that; otherwise, release it. */
	if (ip_lease) {
		if (lease) {
			if (packet->packet_type == DHCPREQUEST)
				release_lease(ip_lease);
		} else {
			lease = ip_lease;
			lease->host = NULL;
		}
	}

	/* If we got a lease that matched the client identifier, we may want
	   to use it, but if we already have a lease we like, we must free
	   the lease that matched the client identifier. */
	if (uid_lease) {
		if (lease) {
			if (packet->packet_type == DHCPREQUEST)
				release_lease(uid_lease);
		} else {
			lease = uid_lease;
			lease->host = NULL;
		}
	}

	/* The lease that matched the hardware address is treated likewise. */
	if (hw_lease) {
		if (lease) {
			if (packet->packet_type == DHCPREQUEST)
				release_lease(hw_lease);
		} else {
			lease = hw_lease;
			lease->host = NULL;
		}
	}

	/* If we found a host_decl but no matching address, try to
	   find a host_decl that has no address, and if there is one,
	   hang it off the lease so that we can use the supplied
	   options. */
	if (lease && host && !lease->host) {
		for (; host; host = host->n_ipaddr) {
			if (!host->fixed_addr) {
				lease->host = host;
				break;
			}
		}
	}

	/* If we find an abandoned lease, take it, but print a
	   warning message, so that if it continues to lose,
	   the administrator will eventually investigate. */
	if (lease && (lease->flags & ABANDONED_LEASE)) {
		if (packet->packet_type == DHCPREQUEST) {
			log_warnx("Reclaiming REQUESTed abandoned IP address "
			    "%s.", piaddr(lease->ip_addr));
			lease->flags &= ~ABANDONED_LEASE;
		} else
			lease = NULL;
	}
	return lease;
}

/*
 * Search the provided host_decl structure list for an address that's on
 * the specified shared network.  If one is found, mock up and return a
 * lease structure for it; otherwise return the null pointer.
 */
struct lease *
mockup_lease(struct packet *packet, struct shared_network *share,
    struct host_decl *hp)
{
	static struct lease mock;

	mock.subnet = find_host_for_network(&hp, &mock.ip_addr, share);
	if (!mock.subnet)
		return (NULL);
	mock.next = mock.prev = NULL;
	mock.shared_network = mock.subnet->shared_network;
	mock.host = hp;

	if (hp->group->options[DHO_DHCP_CLIENT_IDENTIFIER]) {
		mock.uid =
		    hp->group->options[DHO_DHCP_CLIENT_IDENTIFIER]->value;
		mock.uid_len =
		    hp->group->options[DHO_DHCP_CLIENT_IDENTIFIER]->len;
	} else {
		mock.uid = NULL;
		mock.uid_len = 0;
	}

	mock.hardware_addr = hp->interface;
	mock.starts = mock.timestamp = mock.ends = MIN_TIME;
	mock.flags = STATIC_LEASE;
	return &mock;
}
