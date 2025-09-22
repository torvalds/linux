/*
 * xfrd-notify.c - notify sending routines
 *
 * Copyright (c) 2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "xfrd-notify.h"
#include "xfrd.h"
#include "xfrd-tcp.h"
#include "packet.h"

#define XFRD_NOTIFY_RETRY_TIMOUT 3 /* seconds between retries sending NOTIFY */

/* start sending notifies */
static void notify_enable(struct notify_zone* zone,
	struct xfrd_soa* new_soa);
/* setup the notify active state */
static void setup_notify_active(struct notify_zone* zone);

/* handle zone notify send */
static void xfrd_handle_notify_send(int fd, short event, void* arg);

static int xfrd_notify_send_udp(struct notify_zone* zone, int index);

static void
notify_send_disable(struct notify_zone* zone)
{
	zone->notify_send_enable = 0;
	event_del(&zone->notify_send_handler);
	if(zone->notify_send_handler.ev_fd != -1) {
		close(zone->notify_send_handler.ev_fd);
		zone->notify_send_handler.ev_fd = -1;
	}
}

static void
notify_send6_disable(struct notify_zone* zone)
{
	zone->notify_send6_enable = 0;
	event_del(&zone->notify_send6_handler);
	if(zone->notify_send6_handler.ev_fd != -1) {
		close(zone->notify_send6_handler.ev_fd);
		zone->notify_send6_handler.ev_fd = -1;
	}
}

void
notify_disable(struct notify_zone* zone)
{
	zone->notify_current = 0;
	/* if added, then remove */
	if(zone->notify_send_enable) {
		notify_send_disable(zone);
	}
	if(zone->notify_send6_enable) {
		notify_send6_disable(zone);
	}

	if(xfrd->notify_udp_num == XFRD_MAX_UDP_NOTIFY) {
		/* find next waiting and needy zone */
		while(xfrd->notify_waiting_first) {
			/* snip off */
			struct notify_zone* wz = xfrd->notify_waiting_first;
			assert(wz->is_waiting);
			wz->is_waiting = 0;
			xfrd->notify_waiting_first = wz->waiting_next;
			if(wz->waiting_next)
				wz->waiting_next->waiting_prev = NULL;
			if(xfrd->notify_waiting_last == wz)
				xfrd->notify_waiting_last = NULL;
			/* see if this zone needs notify sending */
			if(wz->notify_current) {
				DEBUG(DEBUG_XFRD,1, (LOG_INFO,
					"xfrd: zone %s: notify off waiting list.",
					zone->apex_str)	);
				setup_notify_active(wz);
				return;
			}
		}
	}
	xfrd->notify_udp_num--;
}

void
init_notify_send(rbtree_type* tree, region_type* region,
	struct zone_options* options)
{
	struct notify_zone* not = (struct notify_zone*)
		region_alloc(region, sizeof(struct notify_zone));
	memset(not, 0, sizeof(struct notify_zone));
	not->apex = options->node.key;
	not->apex_str = options->name;
	not->node.key = not->apex;
	not->options = options;

	/* if master zone and have a SOA */
	not->current_soa = (struct xfrd_soa*)region_alloc(region,
		sizeof(struct xfrd_soa));
	memset(not->current_soa, 0, sizeof(struct xfrd_soa));

	not->notify_send_handler.ev_fd = -1;
	not->notify_send6_handler.ev_fd = -1;
	not->is_waiting = 0;

	not->notify_send_enable = 0;
	not->notify_send6_enable = 0;
	tsig_create_record_custom(&not->notify_tsig, NULL, 0, 0, 4);
	not->notify_current = 0;
	rbtree_insert(tree, (rbnode_type*)not);
}

void
xfrd_del_notify(xfrd_state_type* xfrd, const dname_type* dname)
{
	/* find it */
	struct notify_zone* not = (struct notify_zone*)rbtree_delete(
		xfrd->notify_zones, dname);
	if(!not)
		return;

	/* waiting list */
	if(not->is_waiting) {
		if(not->waiting_prev)
			not->waiting_prev->waiting_next = not->waiting_next;
		else	xfrd->notify_waiting_first = not->waiting_next;
		if(not->waiting_next)
			not->waiting_next->waiting_prev = not->waiting_prev;
		else	xfrd->notify_waiting_last = not->waiting_prev;
		not->is_waiting = 0;
	}

	/* event */
	if(not->notify_send_enable || not->notify_send6_enable) {
		notify_disable(not);
	}

	/* del tsig */
	tsig_delete_record(&not->notify_tsig, NULL);

	/* free it */
	region_recycle(xfrd->region, not->current_soa, sizeof(xfrd_soa_type));
	/* the apex is recycled when the zone_options.node.key is removed */
	region_recycle(xfrd->region, not, sizeof(*not));
}

static int
reply_pkt_is_ack(struct notify_zone* zone, buffer_type* packet, int index)
{
	if((OPCODE(packet) != OPCODE_NOTIFY) ||
		(QR(packet) == 0)) {
		log_msg(LOG_ERR, "xfrd: zone %s: received bad notify reply opcode/flags from %s",
			zone->apex_str, zone->pkts[index].dest->ip_address_spec);

		return 0;
	}
	/* we know it is OPCODE NOTIFY, QUERY_REPLY and for this zone */
	if(ID(packet) != zone->pkts[index].notify_query_id) {
		log_msg(LOG_ERR, "xfrd: zone %s: received notify-ack with bad ID from %s",
			zone->apex_str, zone->pkts[index].dest->ip_address_spec);
		return 0;
	}
	/* could check tsig, but why. The reply does not cause processing. */
	if(RCODE(packet) != RCODE_OK) {
		log_msg(LOG_ERR, "xfrd: zone %s: received notify response error %s from %s",
			zone->apex_str, rcode2str(RCODE(packet)),
			zone->pkts[index].dest->ip_address_spec);
		if(RCODE(packet) == RCODE_IMPL)
			return 1; /* rfc1996: notimpl notify reply: consider retries done */
		return 0;
	}
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: zone %s: host %s acknowledges notify",
		zone->apex_str, zone->pkts[index].dest->ip_address_spec));
	return 1;
}

/* compare sockaddr and acl_option addr and port numbers */
static int
cmp_addr_equal(struct sockaddr* a, socklen_t a_len, struct acl_options* dest)
{
	if(dest) {
		unsigned int destport = ((dest->port == 0)?
			(unsigned)atoi(TCP_PORT):dest->port);
#ifdef INET6
		struct sockaddr_storage* a1 = (struct sockaddr_storage*)a;
		if(a1->ss_family == AF_INET6 && dest->is_ipv6) {
			struct sockaddr_in6* a2 = (struct sockaddr_in6*)a;
			if(a_len < sizeof(struct sockaddr_in6))
				return 0; /* too small */
			if(ntohs(a2->sin6_port) != destport)
				return 0; /* different port number */
			if(memcmp(&a2->sin6_addr, &dest->addr.addr6,
				sizeof(struct in6_addr)) != 0)
				return 0; /* different address */
			return 1;
		}
		if(a1->ss_family == AF_INET6 || dest->is_ipv6)
			return 0; /* different address family */
		else {
#endif /* INET6 */
			struct sockaddr_in* a3 = (struct sockaddr_in*)a;
			if(a_len < sizeof(struct sockaddr_in))
				return 0; /* too small */
			if(ntohs(a3->sin_port) != destport)
				return 0; /* different port number */
			if(memcmp(&a3->sin_addr, &dest->addr.addr,
				sizeof(struct in_addr)) != 0)
				return 0; /* different address */
			return 1;
#ifdef INET6
		}
#endif
	}
	return 0;
}

static void
notify_pkt_done(struct notify_zone* zone, int index)
{
	zone->pkts[index].dest = NULL;
	zone->pkts[index].notify_retry = 0;
	zone->pkts[index].send_time = 0;
	zone->pkts[index].notify_query_id = 0;
	zone->notify_pkt_count--;
}

static void
notify_pkt_retry(struct notify_zone* zone, int index)
{
	if(++zone->pkts[index].notify_retry >=
		zone->options->pattern->notify_retry) {
		log_msg(LOG_ERR, "xfrd: zone %s: max notify send count reached, %s unreachable",
			zone->apex_str,
			zone->pkts[index].dest->ip_address_spec);
		notify_pkt_done(zone, index);
		return;
	}
	if(!xfrd_notify_send_udp(zone, index)) {
		notify_pkt_retry(zone, index);
	}
}

static void
xfrd_handle_notify_reply(struct notify_zone* zone, buffer_type* packet, 
	struct sockaddr* src, socklen_t srclen)
{
	int i;
	for(i=0; i<NOTIFY_CONCURRENT_MAX; i++) {
		/* is this entry in use */
		if(!zone->pkts[i].dest)
			continue;
		/* based on destination */
		if(!cmp_addr_equal(src, srclen, zone->pkts[i].dest))
			continue;
		if(reply_pkt_is_ack(zone, packet, i)) {
			/* is done */
			notify_pkt_done(zone, i);
			return;
		} else {
			/* retry */
			notify_pkt_retry(zone, i);
			return;
		}
	}
}

static int
xfrd_notify_send_udp(struct notify_zone* zone, int index)
{
	int apex_compress = 0;
	buffer_type* packet = xfrd_get_temp_buffer();
	if(!zone->pkts[index].dest) return 0;
	/* send NOTIFY to secondary. */
	xfrd_setup_packet(packet, TYPE_SOA, CLASS_IN, zone->apex,
		qid_generate(), &apex_compress);
	zone->pkts[index].notify_query_id = ID(packet);
	OPCODE_SET(packet, OPCODE_NOTIFY);
	AA_SET(packet);
	if(zone->current_soa->serial != 0) {
		/* add current SOA to answer section */
		ANCOUNT_SET(packet, 1);
		xfrd_write_soa_buffer(packet, zone->apex, zone->current_soa,
			apex_compress);
	}
	if(zone->pkts[index].dest->key_options) {
		xfrd_tsig_sign_request(packet, &zone->notify_tsig, zone->pkts[index].dest);
	}
	buffer_flip(packet);

	if((zone->pkts[index].dest->is_ipv6
		&& zone->notify_send6_handler.ev_fd == -1) ||
		(!zone->pkts[index].dest->is_ipv6
		&& zone->notify_send_handler.ev_fd == -1)) {
		/* open fd */
		int fd = xfrd_send_udp(zone->pkts[index].dest, packet,
			zone->options->pattern->outgoing_interface);
		if(fd == -1) {
			log_msg(LOG_ERR, "xfrd: zone %s: could not send notify #%d to %s",
				zone->apex_str, zone->pkts[index].notify_retry,
				zone->pkts[index].dest->ip_address_spec);
			return 0;
		}
		if(zone->pkts[index].dest->is_ipv6)
			zone->notify_send6_handler.ev_fd = fd;
		else	zone->notify_send_handler.ev_fd = fd;
	} else {
		/* send on existing fd */
#ifdef INET6
        	struct sockaddr_storage to;
#else
        	struct sockaddr_in to;
#endif /* INET6 */
		int fd;
		socklen_t to_len = xfrd_acl_sockaddr_to(
			zone->pkts[index].dest, &to);
		if(zone->pkts[index].dest->is_ipv6
		&& zone->notify_send6_handler.ev_fd != -1)
			fd = zone->notify_send6_handler.ev_fd;
		else if (zone->notify_send_handler.ev_fd != -1)
			fd = zone->notify_send_handler.ev_fd;
		else {
			log_msg(LOG_ERR, "xfrd notify: sendto %s failed %s",
				zone->pkts[index].dest->ip_address_spec,
				"invalid file descriptor");
			return 0;
		}
		if(sendto(fd,
			buffer_current(packet), buffer_remaining(packet), 0,
			(struct sockaddr*)&to, to_len) == -1) {
			log_msg(LOG_ERR, "xfrd notify: sendto %s failed %s",
				zone->pkts[index].dest->ip_address_spec,
				strerror(errno));
			return 0;
		}
	}
	zone->pkts[index].send_time = time(NULL);
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: zone %s: sent notify #%d to %s",
		zone->apex_str, zone->pkts[index].notify_retry,
		zone->pkts[index].dest->ip_address_spec));
	return 1;
}

static void
notify_timeout_check(struct notify_zone* zone)
{
	time_t now = time(NULL);
	int i;
	for(i=0; i<NOTIFY_CONCURRENT_MAX; i++) {
		if(!zone->pkts[i].dest)
			continue;
		if(now >= zone->pkts[i].send_time + XFRD_NOTIFY_RETRY_TIMOUT) {
			notify_pkt_retry(zone, i);
		}
	}
}

static void
notify_start_pkts(struct notify_zone* zone)
{
	int i;
	if(!zone->notify_current) return; /* no more acl to send to */
	for(i=0; i<NOTIFY_CONCURRENT_MAX; i++) {
		/* while loop, in case the retries all fail, and we can
		 * start another on this slot, or run out of notify acls */
		while(zone->pkts[i].dest==NULL && zone->notify_current) {
			zone->pkts[i].dest = zone->notify_current;
			zone->notify_current = zone->notify_current->next;
			zone->pkts[i].notify_retry = 0;
			zone->pkts[i].notify_query_id = 0;
			zone->pkts[i].send_time = 0;
			zone->notify_pkt_count++;
			if(!xfrd_notify_send_udp(zone, i)) {
				notify_pkt_retry(zone, i);
			}
		}
	}
}

static void
notify_setup_event(struct notify_zone* zone)
{
	if(zone->notify_send_handler.ev_fd != -1) {
		int fd = zone->notify_send_handler.ev_fd;
		if(zone->notify_send_enable) {
			event_del(&zone->notify_send_handler);
		}
		zone->notify_timeout.tv_sec = XFRD_NOTIFY_RETRY_TIMOUT;
		memset(&zone->notify_send_handler, 0,
			sizeof(zone->notify_send_handler));
		event_set(&zone->notify_send_handler, fd, EV_READ | EV_TIMEOUT,
			xfrd_handle_notify_send, zone);
		if(event_base_set(xfrd->event_base, &zone->notify_send_handler) != 0)
			log_msg(LOG_ERR, "notify_send: event_base_set failed");
		if(event_add(&zone->notify_send_handler, &zone->notify_timeout) != 0)
			log_msg(LOG_ERR, "notify_send: event_add failed");
		zone->notify_send_enable = 1;
	}
	if(zone->notify_send6_handler.ev_fd != -1) {
		int fd = zone->notify_send6_handler.ev_fd;
		if(zone->notify_send6_enable) {
			event_del(&zone->notify_send6_handler);
		}
		zone->notify_timeout.tv_sec = XFRD_NOTIFY_RETRY_TIMOUT;
		memset(&zone->notify_send6_handler, 0,
			sizeof(zone->notify_send6_handler));
		event_set(&zone->notify_send6_handler, fd, EV_READ | EV_TIMEOUT,
			xfrd_handle_notify_send, zone);
		if(event_base_set(xfrd->event_base, &zone->notify_send6_handler) != 0)
			log_msg(LOG_ERR, "notify_send: event_base_set failed");
		if(event_add(&zone->notify_send6_handler, &zone->notify_timeout) != 0)
			log_msg(LOG_ERR, "notify_send: event_add failed");
		zone->notify_send6_enable = 1;
	}
}

static void
xfrd_handle_notify_send(int fd, short event, void* arg)
{
	struct notify_zone* zone = (struct notify_zone*)arg;
	buffer_type* packet = xfrd_get_temp_buffer();
	if(zone->is_waiting) {
		DEBUG(DEBUG_XFRD,1, (LOG_INFO,
			"xfrd: notify waiting, skipped, %s", zone->apex_str));
		return;
	}
	if((event & EV_READ)) {
		struct sockaddr_storage src;
		socklen_t srclen = (socklen_t)sizeof(src);
		DEBUG(DEBUG_XFRD,1, (LOG_INFO,
			"xfrd: zone %s: read notify ACK", zone->apex_str));
		assert(fd != -1);
		if(xfrd_udp_read_packet(packet, fd, (struct sockaddr*)&src,
			&srclen)) {
			/* find entry, send retry or make entry NULL */
			xfrd_handle_notify_reply(zone, packet,
				(struct sockaddr*)&src, srclen);
		}
	}
	if((event & EV_TIMEOUT)) {
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: zone %s: notify timeout",
			zone->apex_str));
		/* timeout, try again */
	}

	/* see which pkts have timeouted, retry or NULL them */
	notify_timeout_check(zone);

	/* start new packets if we have empty space */
	notify_start_pkts(zone);

	/* see if we are done */
	if(!zone->notify_current && !zone->notify_pkt_count) {
		/* we are done */
		DEBUG(DEBUG_XFRD,1, (LOG_INFO,
			"xfrd: zone %s: no more notify-send acls. stop notify.",
			zone->apex_str));
		notify_disable(zone);
		return;
	}

	notify_setup_event(zone);
}

static void
setup_notify_active(struct notify_zone* zone)
{
	zone->notify_pkt_count = 0;
	memset(zone->pkts, 0, sizeof(zone->pkts));
	zone->notify_current = zone->options->pattern->notify;
	zone->notify_timeout.tv_sec = 0;
	zone->notify_timeout.tv_usec = 0;

	if(zone->notify_send_enable)
		notify_send_disable(zone);
	memset(&zone->notify_send_handler, 0,
		sizeof(zone->notify_send_handler));
	event_set(&zone->notify_send_handler, -1, EV_TIMEOUT,
		xfrd_handle_notify_send, zone);
	if(event_base_set(xfrd->event_base, &zone->notify_send_handler) != 0)
		log_msg(LOG_ERR, "notifysend: event_base_set failed");
	if(evtimer_add(&zone->notify_send_handler, &zone->notify_timeout) != 0)
		log_msg(LOG_ERR, "notifysend: evtimer_add failed");
	zone->notify_send_enable = 1;
}

static void
notify_enable(struct notify_zone* zone, struct xfrd_soa* new_soa)
{
	if(!zone->options->pattern->notify) {
		return; /* no notify acl, nothing to do */
	}

	if(new_soa == NULL)
		memset(zone->current_soa, 0, sizeof(xfrd_soa_type));
	else
		memcpy(zone->current_soa, new_soa, sizeof(xfrd_soa_type));
	if(zone->is_waiting)
		return;

	if(xfrd->notify_udp_num < XFRD_MAX_UDP_NOTIFY) {
		setup_notify_active(zone);
		xfrd->notify_udp_num++;
		return;
	}
	/* put it in waiting list */
	zone->notify_current = zone->options->pattern->notify;
	zone->is_waiting = 1;
	zone->waiting_next = NULL;
	zone->waiting_prev = xfrd->notify_waiting_last;
	if(xfrd->notify_waiting_last) {
		xfrd->notify_waiting_last->waiting_next = zone;
	} else {
		xfrd->notify_waiting_first = zone;
	}
	xfrd->notify_waiting_last = zone;
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: zone %s: notify on waiting list.",
		zone->apex_str));
}

void
xfrd_notify_start(struct notify_zone* zone, struct xfrd_state* xfrd)
{
	xfrd_zone_type* xz;
	if(zone->is_waiting || zone->notify_send_enable ||
		zone->notify_send6_enable)
		return;
	xz = (xfrd_zone_type*)rbtree_search(xfrd->zones, zone->apex);
	if(xz && xz->soa_nsd_acquired)
		notify_enable(zone, &xz->soa_nsd);
	else	notify_enable(zone, NULL);
}

void
xfrd_send_notify(rbtree_type* tree, const dname_type* apex, struct xfrd_soa* new_soa)
{
	/* lookup the zone */
	struct notify_zone* zone = (struct notify_zone*)
		rbtree_search(tree, apex);
	assert(zone);
	if(zone->notify_send_enable || zone->notify_send6_enable)
		notify_disable(zone);

	notify_enable(zone, new_soa);
}

void
notify_handle_master_zone_soainfo(rbtree_type* tree,
	const dname_type* apex, struct xfrd_soa* new_soa)
{
	/* lookup the zone */
	struct notify_zone* zone = (struct notify_zone*)
		rbtree_search(tree, apex);
	if(!zone) return; /* got SOAINFO but zone was deleted meanwhile */

	/* check if SOA changed */
	if( (new_soa == NULL && zone->current_soa->serial == 0) ||
		(new_soa && new_soa->serial == zone->current_soa->serial))
		return;
	if(zone->notify_send_enable || zone->notify_send6_enable)
		notify_disable(zone);
	notify_enable(zone, new_soa);
}

void
close_notify_fds(rbtree_type* tree)
{
	struct notify_zone* zone;
	RBTREE_FOR(zone, struct notify_zone*, tree)
	{
		if(zone->notify_send_enable || zone->notify_send6_enable)
			notify_send_disable(zone);
	}
}
