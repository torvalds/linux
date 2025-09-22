/*
 * remote.h - remote control for the NSD daemon.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains the remote control functionality for the daemon.
 * The remote control can be performed using either the commandline
 * nsd-control tool, or a SSLv3/TLS capable web browser. 
 * The channel is secured using SSLv3 or TLSv1, and certificates.
 * Both the server and the client(control tool) have their own keys.
 */

#ifndef DAEMON_REMOTE_H
#define DAEMON_REMOTE_H
struct xfrd_state;
struct nsd_options;

#ifdef BIND8_STATS
struct nsdst;
struct remote_stream;
struct evbuffer;
#endif /* BIND8_STATS */

/* private, defined in remote.c to keep ssl.h out of this header */
struct daemon_remote;

/* the remote control needs less backlog than the tcp53 service */
#define TCP_BACKLOG_REMOTE 16 /* listen() tcp backlog */

/**
 * Create new remote control state for the daemon.
 * Also setups the control port.
 * @param cfg: config file with key file settings.
 * @return new state, or NULL on failure.
 */
struct daemon_remote* daemon_remote_create(struct nsd_options* cfg);

/**
 * remote control state to delete.
 * @param rc: state to delete.
 */
void daemon_remote_delete(struct daemon_remote* rc);

/**
 * Close remote control ports.  Clears up busy connections.
 * Does not delete the rc itself, or the ssl context (with its keys).
 * @param rc: state to close.
 */
void daemon_remote_close(struct daemon_remote* rc);

/**
 * Open and create listening ports for remote control.
 * @param rc: rc state that contains list of accept port sockets.
 * @param cfg: config options.
 * @return false on failure.
 */
int daemon_remote_open_ports(struct daemon_remote* rc,
	struct nsd_options* cfg);

/**
 * Setup comm points for accepting remote control connections.
 * @param rc: state
 * @param xfrd: the process that hosts the control connection.
 *	The rc is attached to its event base.
 */
void daemon_remote_attach(struct daemon_remote* rc, struct xfrd_state* xfrd);

/**
 * Create and bind local listening socket
 * @param path: path to the socket.
 * @param noproto: on error, this is set true if cause is that local sockets
 *	are not supported.
 * @return: the socket. -1 on error.
 */
int create_local_accept_sock(const char* path, int* noproto);

void xfrd_reload_config(struct xfrd_state *xfrd);

#ifdef BIND8_STATS

/**
 * Allocate stats temp arrays, for taking a coherent snapshot of the
 * statistics values at that time.
 * @param xfrd: the process that hosts the control connection.
 * @param stats: where to store the stats pointer
 * @param zonestats: where to store the zonestats pointer
 */
void process_stats_alloc(struct xfrd_state* xfrd,
                         struct nsdst** stats,
                         struct nsdst** zonestats);

/**
 * Grab a copy of the statistics, at this particular time.
 * @param xfrd: the process that hosts the control connection.
 * @param stattime: where to write the timeofday
 * @param stats: where to copy the stats to
 * @param zonestats: where to copy the zonestats to
 */
void process_stats_grab(struct xfrd_state* xfrd,
                        struct timeval* stattime,
                        struct nsdst* stats,
                        struct nsdst** zonestats);
/**
 * Add the old and new processes stat values into the first part of the
 * array of stats
 * @param xfrd: the process that hosts the control connection.
 * @param stats: the stats pointer
 */
void process_stats_add_old_new(struct xfrd_state* xfrd, struct nsdst* stats);

/**
 * Manage clearing of stats, a cumulative count of cleared statistics
 * @param xfrd: the process that hosts the control connection.
 * @param stats: the stats pointer
 * @param peek: whether to reset the stats time (0) or not (1)
 */
void
process_stats_manage_clear(struct xfrd_state* xfrd,
                           struct nsdst* stats,
                           int peek);

/**
 * Add up the statistics to get the total over the server children.
 * @param xfrd: the process that hosts the control connection.
 * @param total: where to store the total data
 * @param stats: the stats pointer
 */
void process_stats_add_total(struct xfrd_state* xfrd,
                             struct nsdst* total,
                             struct nsdst* stats);

/**
 * Process the statistics and output them
 * @param ssl: the remote stream to write normal remote-control output to
 * @param evbuf: the HTTP buffer to write prometheus metrics output to
 * @param xfrd: the process that hosts the control connection.
 * @param peek: whether to reset the stats time (0) or not (1)
 */
void process_stats(struct remote_stream* ssl,
                   struct evbuffer* evbuf,
                   struct xfrd_state* xfrd,
                   int peek);

#ifdef USE_ZONE_STATS
/**
 * Process the zonestat statistics and output them
 * @param ssl: the remote stream to write normal remote-control output to
 * @param evbuf: the HTTP buffer to write prometheus metrics output to
 * @param xfrd: the process that hosts the control connection.
 * @param clear: whether to reset the stats time
 * @param zonestats: the zonestats pointer
 */
void zonestat_print(struct remote_stream *ssl, struct evbuffer *evbuf,
                    struct xfrd_state *xfrd, int clear,
                    struct nsdst **zonestats);
#endif /*USE_ZONE_STATS*/

const char* opcode2str(int o);

void timeval_subtract(struct timeval *d, const struct timeval *end,
                      const struct timeval *start);

#endif /* BIND8_STATS */

#endif /* DAEMON_REMOTE_H */
