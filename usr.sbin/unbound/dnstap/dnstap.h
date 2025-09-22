/* dnstap support for Unbound */

/*
 * Copyright (c) 2013-2014, Farsight Security, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UNBOUND_DNSTAP_H
#define UNBOUND_DNSTAP_H

#include "dnstap/dnstap_config.h"

#ifdef USE_DNSTAP

#include "util/locks.h"
struct config_file;
struct sldns_buffer;
struct dt_msg_queue;

struct dt_env {
	/** the io thread (made by the struct daemon) */
	struct dt_io_thread* dtio;

	/** valid in worker struct, not in daemon struct, the per-worker
	 * message list */
	struct dt_msg_queue* msgqueue;

	/** dnstap "identity" field, NULL if disabled */
	char *identity;

	/** dnstap "version" field, NULL if disabled */
	char *version;

	/** length of "identity" field */
	unsigned len_identity;

	/** length of "version" field */
	unsigned len_version;

	/** whether to log Message/RESOLVER_QUERY */
	unsigned log_resolver_query_messages : 1;
	/** whether to log Message/RESOLVER_RESPONSE */
	unsigned log_resolver_response_messages : 1;
	/** whether to log Message/CLIENT_QUERY */
	unsigned log_client_query_messages : 1;
	/** whether to log Message/CLIENT_RESPONSE */
	unsigned log_client_response_messages : 1;
	/** whether to log Message/FORWARDER_QUERY */
	unsigned log_forwarder_query_messages : 1;
	/** whether to log Message/FORWARDER_RESPONSE */
	unsigned log_forwarder_response_messages : 1;

	/** lock on sample count */
	lock_basic_type sample_lock;
	/** rate limit value from config, samples 1/N messages */
	unsigned int sample_rate;
	/** rate limit counter */
	unsigned int sample_rate_count;
};

/**
 * Create dnstap environment object. Afterwards, call dt_apply_cfg() to fill in
 * the config variables and dt_init() to fill in the per-worker state. Each
 * worker needs a copy of this object but with its own I/O queue (the fq field
 * of the structure) to ensure lock-free access to its own per-worker circular
 * queue.  Duplicate the environment object if more than one worker needs to
 * share access to the dnstap I/O socket.
 * @param cfg: with config settings.
 * @return dt_env object, NULL on failure.
 */
struct dt_env *
dt_create(struct config_file* cfg);

/**
 * Apply config settings.
 * @param env: dnstap environment object.
 * @param cfg: new config settings.
 */
void
dt_apply_cfg(struct dt_env *env, struct config_file *cfg);

/**
 * Apply config settings for log enable for message types.
 * @param env: dnstap environment object.
 * @param cfg: new config settings.
 */
void dt_apply_logcfg(struct dt_env *env, struct config_file *cfg);

/**
 * Initialize per-worker state in dnstap environment object.
 * @param env: dnstap environment object to initialize, created with dt_create().
 * @param base: event base for wakeup timer.
 * @return: true on success, false on failure.
 */
int
dt_init(struct dt_env *env, struct comm_base* base);

/**
 * Deletes the per-worker state created by dt_init
 */
void dt_deinit(struct dt_env *env);

/**
 * Delete dnstap environment object. Closes dnstap I/O socket and deletes all
 * per-worker I/O queues.
 */
void
dt_delete(struct dt_env *env);

/**
 * Create and send a new dnstap "Message" event of type CLIENT_QUERY.
 * @param env: dnstap environment object.
 * @param qsock: address/port of client.
 * @param rsock: local (service) address/port.
 * @param cptype: comm_udp or comm_tcp.
 * @param qmsg: query message.
 * @param tstamp: timestamp or NULL if none provided.
 */
void
dt_msg_send_client_query(struct dt_env *env,
			 struct sockaddr_storage *qsock,
			 struct sockaddr_storage *rsock,
			 enum comm_point_type cptype,
			 void *cpssl,
			 struct sldns_buffer *qmsg,
			 struct timeval* tstamp);

/**
 * Create and send a new dnstap "Message" event of type CLIENT_RESPONSE.
 * @param env: dnstap environment object.
 * @param qsock: address/port of client.
 * @param rsock: local (service) address/port.
 * @param cptype: comm_udp or comm_tcp.
 * @param rmsg: response message.
 */
void
dt_msg_send_client_response(struct dt_env *env,
			    struct sockaddr_storage *qsock,
			    struct sockaddr_storage *rsock,
			    enum comm_point_type cptype,
			    void *cpssl,
			    struct sldns_buffer *rmsg);

/**
 * Create and send a new dnstap "Message" event of type RESOLVER_QUERY or
 * FORWARDER_QUERY. The type used is dependent on the value of the RD bit
 * in the query header.
 * @param env: dnstap environment object.
 * @param rsock: address/port of server (upstream) the query is being sent to.
 * @param qsock: address/port of server (local) the query is being sent from.
 * @param cptype: comm_udp or comm_tcp.
 * @param zone: query zone.
 * @param zone_len: length of zone.
 * @param qmsg: query message.
 */
void
dt_msg_send_outside_query(struct dt_env *env,
			  struct sockaddr_storage *rsock,
			  struct sockaddr_storage *qsock,
			  enum comm_point_type cptype,
			  void *cpssl,
			  uint8_t *zone, size_t zone_len,
			  struct sldns_buffer *qmsg);

/**
 * Create and send a new dnstap "Message" event of type RESOLVER_RESPONSE or
 * FORWARDER_RESPONSE. The type used is dependent on the value of the RD bit
 * in the query header.
 * @param env: dnstap environment object.
 * @param rsock: address/port of server (upstream) the response was received from.
 * @param qsock: address/port of server (local) the response was received to.
 * @param cptype: comm_udp or comm_tcp.
 * @param zone: query zone.
 * @param zone_len: length of zone.
 * @param qbuf: outside_network's qbuf key.
 * @param qbuf_len: length of outside_network's qbuf key.
 * @param qtime: time query message was sent.
 * @param rtime: time response message was sent.
 * @param rmsg: response message.
 */
void
dt_msg_send_outside_response(struct dt_env *env,
			     struct sockaddr_storage *rsock,
			     struct sockaddr_storage *qsock,
			     enum comm_point_type cptype,
			     void *cpssl,
			     uint8_t *zone, size_t zone_len,
			     uint8_t *qbuf, size_t qbuf_len,
			     const struct timeval *qtime,
			     const struct timeval *rtime,
			     struct sldns_buffer *rmsg);

#endif /* USE_DNSTAP */

#endif /* UNBOUND_DNSTAP_H */
