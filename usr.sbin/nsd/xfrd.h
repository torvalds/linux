/*
 * xfrd.h - XFR (transfer) Daemon header file. Coordinates SOA updates.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef XFRD_H
#define XFRD_H

#ifndef USE_MINI_EVENT
#  ifdef HAVE_EVENT_H
#    include <event.h>
#  else
#    include <event2/event.h>
#    include "event2/event_struct.h"
#    include "event2/event_compat.h"
#  endif
#else
#  include "mini_event.h"
#endif
#include "rbtree.h"
#include "namedb.h"
#include "options.h"
#include "dns.h"
#include "tsig.h"

struct nsd;
struct region;
struct buffer;
struct xfrd_tcp;
struct xfrd_tcp_set;
struct notify_zone;
struct udb_ptr;
typedef struct xfrd_state xfrd_state_type;
typedef struct xfrd_xfr xfrd_xfr_type;
typedef struct xfrd_zone xfrd_zone_type;
typedef struct xfrd_soa xfrd_soa_type;
/*
 * The global state for the xfrd daemon process.
 * The time_t times are epochs in secs since 1970, absolute times.
 */
struct xfrd_state {
	/* time when daemon was last started */
	time_t xfrd_start_time;
	struct region* region;
	struct event_base* event_base;
	struct nsd* nsd;

	struct xfrd_tcp_set* tcp_set;
	/* packet buffer for udp packets */
	struct buffer* packet;
	/* udp waiting list, double linked list */
	struct xfrd_zone *udp_waiting_first, *udp_waiting_last;
	/* number of udp sockets (for sending queries) in use */
	size_t udp_use_num;
	/* activated waiting list, double linked list */
	struct xfrd_zone *activated_first;

	/* current time is cached */
	uint8_t got_time;
	time_t current_time;

	/* counter for xfr file numbers */
	uint64_t xfrfilenumber;

	/* the zonestat array size that we last saw and is safe to use */
	unsigned zonestat_safe;
	/* size currently of the clear array */
	size_t zonestat_clear_num;
	/* array of malloced entries with cumulative cleared stat values */
	struct nsdst** zonestat_clear;
	/* array of child_count size with cumulative cleared stat values */
	struct nsdst* stat_clear;

	/* timer for NSD reload */
	struct timeval reload_timeout;
	struct event reload_handler;
	int reload_added;
	/* last reload must have caught all zone updates before this time */
	time_t reload_cmd_last_sent;
	time_t reload_cmd_first_sent;
	uint8_t reload_failed;
	uint8_t can_send_reload;
	pid_t reload_pid;
	/* timeout for lost sigchild and reaping children */
	struct event child_timer;
	int child_timer_added;

	/* timeout event for zonefiles_write events */
	struct event write_timer;
	/* set to 1 if zones have received xfrs since the last write_timer */
	int write_zonefile_needed;

	/* communication channel with server_main */
	struct event ipc_handler;
	int ipc_handler_flags;
	/* 2 * nsd->child_count communication channels from the serve childs */
	struct event    *notify_events;
	struct xfrd_tcp *notify_pipes;
	/* sending ipc to server_main */
	uint8_t need_to_send_shutdown;
	uint8_t need_to_send_reload;
	uint8_t need_to_send_stats;
	uint8_t need_to_send_quit;
	uint8_t	ipc_send_blocked;
	struct udb_ptr* last_task;

	/* xfrd shutdown flag */
	uint8_t shutdown;

	/* tree of zones, by apex name, contains xfrd_zone_type*. Only secondary zones. */
	rbtree_type *zones;

	/* tree of zones, by apex name, contains notify_zone*. All zones. */
	rbtree_type *notify_zones;
	/* number of notify_zone active using UDP socket */
	int notify_udp_num;
	/* first and last notify_zone* entries waiting for a UDP socket */
	struct notify_zone *notify_waiting_first, *notify_waiting_last;

	/* tree of catalog consumer zones. Processing is disabled if > 1. */
	rbtree_type *catalog_consumer_zones;

	/* tree of updated catalog producer zones for which the content to serve */
	rbtree_type *catalog_producer_zones;
};

/*
 * XFR daemon SOA information kept in network format.
 * This is in packet order.
 */
struct xfrd_soa {
	/* name of RR is zone apex dname */
	uint16_t type; /* = TYPE_SOA */
	uint16_t klass; /* = CLASS_IN */
	uint32_t ttl;
	uint16_t rdata_count; /* = 7 */
	/* format is 1 octet length, + wireformat dname.
	   one more octet since parse_dname_wire_from_packet needs it.
	   maximum size is allocated to avoid memory alloc/free. */
	uint8_t prim_ns[MAXDOMAINLEN + 2];
	uint8_t email[MAXDOMAINLEN + 2];
	uint32_t serial;
	uint32_t refresh;
	uint32_t retry;
	uint32_t expire;
	uint32_t minimum;
} ATTR_PACKED;


/*
 * XFRD state for a single zone
 */
struct xfrd_zone {
	rbnode_type node;

	/* name of the zone */
	const dname_type* apex;
	const char* apex_str;

	/* Three types of soas:
	 * NSD: in use by running server
	 * disk: stored on disk in db/diff file
	 * notified: from notification, could be available on a master.
	 * And the time the soa was acquired (start time for timeouts).
	 * If the time==0, no SOA is available.
	 */
	xfrd_soa_type soa_nsd;
	time_t soa_nsd_acquired;
	xfrd_soa_type soa_disk;
	time_t soa_disk_acquired;
	xfrd_soa_type soa_notified;
	time_t soa_notified_acquired;

	enum xfrd_zone_state {
		xfrd_zone_ok,
		xfrd_zone_refreshing,
		xfrd_zone_expired
	} state;

	/* master to try to transfer from, number for persistence */
	struct acl_options* master;
	int master_num;
	int next_master; /* -1 or set by notify where to try next */
	/* round of xfrattempts, -1 is waiting for timeout */
	int round_num;
	struct zone_options* zone_options;
	int fresh_xfr_timeout;

	/* handler for timeouts */
	struct timeval timeout;
	struct event zone_handler;
	int zone_handler_flags;
	int event_added;

	/* tcp connection zone is using, or -1 */
	int tcp_conn;
	/* zone is waiting for a tcp connection */
	uint8_t tcp_waiting;
	/* next zone in waiting list */
	xfrd_zone_type* tcp_waiting_next;
	xfrd_zone_type* tcp_waiting_prev;
	/* zone is in its tcp send queue */
	uint8_t in_tcp_send;
	/* next zone in tcp send queue */
	xfrd_zone_type* tcp_send_next;
	xfrd_zone_type* tcp_send_prev;
	/* zone is waiting for a udp connection (tcp is preferred) */
	uint8_t udp_waiting;
	/* next zone in waiting list for UDP */
	xfrd_zone_type* udp_waiting_next;
	xfrd_zone_type* udp_waiting_prev;
	/* zone has been activated to run now (after the other events
	 * but before blocking in select again) */
	uint8_t is_activated;
	xfrd_zone_type* activated_next;
	xfrd_zone_type* activated_prev;

	/* xfr message handling data */
	/* query id */
	uint16_t query_id;
	xfrd_xfr_type *latest_xfr;

	int multi_master_first_master; /* >0: first check master_num */
	int multi_master_update_check; /* -1: not update >0: last update master_num */
} ATTR_PACKED;

/*
 * State for a single zone XFR
 */
struct xfrd_xfr {
	xfrd_xfr_type *next;
	xfrd_xfr_type *prev;
	uint16_t query_type;
	uint8_t sent; /* written to tasklist (tri-state) */
	time_t acquired; /* time xfr was acquired */
	uint32_t msg_seq_nr; /* number of messages already handled */
	uint32_t msg_old_serial, msg_new_serial; /* host byte order */
	size_t msg_rr_count;
	uint8_t msg_is_ixfr; /* 1:IXFR detected. 2:middle IXFR SOA seen. */
	tsig_record_type tsig; /* tsig state for IXFR/AXFR */
	uint64_t xfrfilenumber; /* identifier for file to store xfr into,
	                           valid if msg_seq_nr nonzero */
};

enum xfrd_packet_result {
	xfrd_packet_bad, /* drop the packet/connection */
	xfrd_packet_drop, /* drop the connection, but not report bad */
	xfrd_packet_more, /* more packets to follow on tcp */
	xfrd_packet_notimpl, /* server responded with NOTIMPL or FORMATERR */
	xfrd_packet_tcp, /* try tcp connection */
	xfrd_packet_transfer, /* server responded with transfer*/
	xfrd_packet_newlease /* no changes, soa OK */
};

/*
   Division of the (portably: 1024) max number of sockets that can be open.
   The sum of the below numbers should be below the user limit for sockets
   open, or you see errors in your logfile.
   And it should be below FD_SETSIZE, to be able to select() on replies.
   Note that also some sockets are used for writing the ixfr.db, xfrd.state
   files and for the pipes to the main parent process.

   For xfrd_tcp_max, 128 is the default number of TCP AXFR/IXFR concurrent
   connections. Each entry has 64Kb buffer preallocated.
*/
#define XFRD_MAX_UDP 128 /* max number of UDP sockets at a time for IXFR */
#define XFRD_MAX_UDP_NOTIFY 128 /* max concurrent UDP sockets for NOTIFY */

#define XFRD_TRANSFER_TIMEOUT_START 10 /* empty zone timeout is between x and 2*x seconds */
#define XFRD_TRANSFER_TIMEOUT_MAX 86400 /* empty zone timeout max expbackoff */
#define XFRD_LOWERBOUND_REFRESH 1 /* seconds, smallest refresh timeout */
#define XFRD_LOWERBOUND_RETRY 1 /* seconds, smallest retry timeout */

/*
 * return refresh period
 * within configured and defined lower and upper bounds
 */
static inline time_t
within_refresh_bounds(xfrd_zone_type* zone, time_t refresh)
{
	return (time_t)zone->zone_options->pattern->max_refresh_time < refresh
	     ? (time_t)zone->zone_options->pattern->max_refresh_time
	     : (time_t)zone->zone_options->pattern->min_refresh_time > refresh
	     ? (time_t)zone->zone_options->pattern->min_refresh_time
	     : XFRD_LOWERBOUND_REFRESH > refresh
	     ? XFRD_LOWERBOUND_REFRESH : refresh;
}

/*
 * return the zone's refresh period (from the on disk stored SOA)
 * within configured and defined lower and upper bounds
 */
static inline time_t
bound_soa_disk_refresh(xfrd_zone_type* zone)
{
	return within_refresh_bounds(zone, ntohl(zone->soa_disk.refresh));
}

/*
 * return retry period
 * within configured and defined lower and upper bounds
 */
static inline time_t
within_retry_bounds(xfrd_zone_type* zone, time_t retry)
{
	return (time_t)zone->zone_options->pattern->max_retry_time < retry
	     ? (time_t)zone->zone_options->pattern->max_retry_time
	     : (time_t)zone->zone_options->pattern->min_retry_time > retry
	     ? (time_t)zone->zone_options->pattern->min_retry_time
	     : XFRD_LOWERBOUND_RETRY > retry
	     ? XFRD_LOWERBOUND_RETRY : retry;
}

/*
 * return the zone's retry period (from the on disk stored SOA)
 * within configured and defined lower and upper bounds
 */
static inline time_t
bound_soa_disk_retry(xfrd_zone_type* zone)
{
	return within_retry_bounds(zone, ntohl(zone->soa_disk.retry));
}

/*
 * return expire period
 * within configured and defined lower bounds
 */
static inline time_t
within_expire_bounds(xfrd_zone_type* zone, time_t expire)
{
	switch (zone->zone_options->pattern->min_expire_time_expr) {
	case EXPIRE_TIME_HAS_VALUE:
		return (time_t)zone->zone_options->pattern->min_expire_time > expire
		     ? (time_t)zone->zone_options->pattern->min_expire_time : expire;

	case REFRESHPLUSRETRYPLUS1:
		return bound_soa_disk_refresh(zone) + bound_soa_disk_retry(zone) + 1 > expire
		     ? bound_soa_disk_refresh(zone) + bound_soa_disk_retry(zone) + 1 : expire;
	default:
		return expire;
	}
}

/* return the zone's expire period (from the on disk stored SOA) */
static inline time_t
bound_soa_disk_expire(xfrd_zone_type* zone)
{
	return within_expire_bounds(zone, ntohl(zone->soa_disk.expire));
}

/* return the zone's expire period (from the SOA in use by the running server) */
static inline time_t
bound_soa_nsd_expire(xfrd_zone_type* zone)
{
	return within_expire_bounds(zone, ntohl(zone->soa_nsd.expire));
}

extern xfrd_state_type* xfrd;

/* start xfrd, new start. Pass socket to server_main. */
void xfrd_init(int socket, struct nsd* nsd, int shortsoa, int reload_active,
	pid_t nsd_pid);

/* add new slave zone, dname(from zone_opt) and given options */
void xfrd_init_slave_zone(xfrd_state_type* xfrd, struct zone_options* zone_opt);

/* delete slave zone */
void xfrd_del_slave_zone(xfrd_state_type* xfrd, const dname_type* dname);

/* disable ixfr for a while for zone->master */
void xfrd_disable_ixfr(xfrd_zone_type* zone);

/* get the current time epoch. Cached for speed. */
time_t xfrd_time(void);

/*
 * Handle final received packet from network.
 * returns enum of packet discovery results
 */
enum xfrd_packet_result xfrd_handle_received_xfr_packet(
	xfrd_zone_type* zone, buffer_type* packet);

/* set timer to specific value */
void xfrd_set_timer(xfrd_zone_type* zone, time_t t);
/* set refresh timer of zone to refresh at time now */
void xfrd_set_refresh_now(xfrd_zone_type* zone);
/* unset the timer - no more timeouts, for when zone is queued */
void xfrd_unset_timer(xfrd_zone_type* zone);
/* remove the 'refresh now', remove it from the activated list */
void xfrd_deactivate_zone(xfrd_zone_type* z);

/*
 * Make a new request to next master server.
 * uses next_master if set (and a fresh set of rounds).
 * otherwise, starts new round of requests if none started already.
 * starts next round of requests if at last master.
 * if too many rounds of requests, sets timer for next retry.
 */
void xfrd_make_request(xfrd_zone_type* zone);

/*
 * send packet via udp (returns UDP fd source socket) to acl addr.
 * returns -1 on failure.
 */
int xfrd_send_udp(struct acl_options* acl, buffer_type* packet,
	struct acl_options* ifc);

/*
 * read from udp port packet into buffer, returns 0 on failure
 */
int xfrd_udp_read_packet(buffer_type* packet, int fd, struct sockaddr* src,
	socklen_t* srclen);

/*
 * Release udp socket that a zone is using
 */
void xfrd_udp_release(xfrd_zone_type* zone);

/*
 * Get a static buffer for temporary use (to build a packet).
 */
struct buffer* xfrd_get_temp_buffer(void);

/*
 * TSIG sign outgoing request. Call if acl has a key.
 */
void xfrd_tsig_sign_request(buffer_type* packet, struct tsig_record* tsig,
        struct acl_options* acl);

/* handle incoming soa information (NSD is running it, time acquired=guess).
   Pass soa=NULL,acquired=now if NSD has nothing loaded for the zone
   (i.e. zonefile was deleted). */
void xfrd_handle_incoming_soa(xfrd_zone_type* zone, xfrd_soa_type* soa,
	time_t acquired);
/* handle a packet passed along ipc route. acl is the one that accepted
   the packet. The packet is the network blob received. acl_xfr is 
   provide-xfr acl matching notify sender or -1 */
void xfrd_handle_passed_packet(buffer_type* packet,
	int acl_num, int acl_xfr);

/* try to reopen the logfile. */
void xfrd_reopen_logfile(void);

/* free namedb for xfrd usage */
void xfrd_free_namedb(struct nsd* nsd);

/* copy SOA info from rr to soa struct. */
void xfrd_copy_soa(xfrd_soa_type* soa, rr_type* rr);

/* check for failed updates - it is assumed that now the reload has
   finished, and all zone SOAs have been sent. */
void xfrd_check_failed_updates(void);

void
xfrd_prepare_updates_for_reload(void);

/*
 * Prepare zones for a reload, this sets the times on the zones to be
 * before the current time, so the reload happens after.
 */
void xfrd_prepare_zones_for_reload(void);

/* Bind a local interface to a socket descriptor, return 1 on success */
int xfrd_bind_local_interface(int sockd, struct acl_options* ifc,
	struct acl_options* acl, int tcp);

/* process results and soa info from reload */
void xfrd_process_task_result(xfrd_state_type* xfrd, struct udb_base* taskudb);

/* set to reload right away (for user controlled reload events) */
void xfrd_set_reload_now(xfrd_state_type* xfrd);

/* send expiry notifications to nsd */
void xfrd_send_expire_notification(xfrd_zone_type* zone);

/* handle incoming notify (soa or NULL) and start zone xfr if necessary */
void xfrd_handle_notify_and_start_xfr(xfrd_zone_type* zone, xfrd_soa_type* soa);

/* handle zone timeout, event */
void xfrd_handle_zone(int fd, short event, void* arg);

const char* xfrd_pretty_time(time_t v);

xfrd_xfr_type *xfrd_prepare_zone_xfr(xfrd_zone_type *zone, uint16_t query_type);

void xfrd_delete_zone_xfr(xfrd_zone_type *zone, xfrd_xfr_type *xfr);

#endif /* XFRD_H */
