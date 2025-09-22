/*
 * dnstap/dnstap_collector.h -- nsd collector process for dnstap information
 *
 * Copyright (c) 2018, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef DNSTAP_COLLECTOR_H
#define DNSTAP_COLLECTOR_H
struct dt_env;
struct nsd;
struct event_base;
struct event;
struct dt_collector_input;
struct zone;
struct buffer;
struct region;

/* information for the dnstap collector process. It collects information
 * for dnstap from the worker processes.  And writes them to the dnstap
 * socket. */
struct dt_collector {
	/* dnstap env for the write to the dnstap socket */
	struct dt_env* dt_env;
	/* number of workers to collect from */
	int count;
	/* socketpair for communication between (xfrd) and the
	 * dnstap collector process.  If closed, the collector process
	 * exits.  The collector closes the other side of the socketpair, so
	 * that if xfrd exits, so does the dnstap collector */
	int cmd_socket_dt, cmd_socket_nsd;
	/* the pid of the dt collector process (0 on that process) */
	pid_t dt_pid;
	/* in the collector process, the event base */
	struct event_base* event_base;
	/* in the collector process, the cmd handle event */
	struct event* cmd_event;
	/* in the collector process, array size count of input per worker */
	struct dt_collector_input* inputs;
	/* region for buffers */
	struct region* region;
	/* buffer for sending data to the collector */
	struct buffer* send_buffer;
};

/* information per worker to get input from that worker. */
struct dt_collector_input {
	/* the collector this is part of (for use in callbacks) */
	struct dt_collector* dt_collector;
	/* the event to listen to the datagrams to process for that worker*/
	struct event* event;
	/* buffer to store the datagrams while they are read in */
	struct buffer* buffer;
};

/* create dt_collector process structure and dt_env */
struct dt_collector* dt_collector_create(struct nsd* nsd);
/* destroy the dt_collector structure */
void dt_collector_destroy(struct dt_collector* dt_col, struct nsd* nsd);
/* close file descriptors */
void dt_collector_close(struct dt_collector* dt_col, struct nsd* nsd);
/* start the collector process */
void dt_collector_start(struct dt_collector* dt_col, struct nsd* nsd);

/* submit auth query from worker.  It attempts to send it to the collector,
 * if the nonblocking fails, then it silently skips it.  So it does not block
 * on the log.
 */
void dt_collector_submit_auth_query(struct nsd* nsd,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	socklen_t addrlen, int is_tcp, struct buffer* packet);

/* submit auth response from worker.  It attempts to send it to the collector,
 * if the nonblocking fails, then it silently skips it.  So it does not block
 * on the log.
 */
void dt_collector_submit_auth_response(struct nsd* nsd,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	socklen_t addrlen, int is_tcp, struct buffer* packet,
	struct zone* zone);

#endif /* DNSTAP_COLLECTOR_H */
