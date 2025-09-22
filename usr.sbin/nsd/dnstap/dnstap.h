/* dnstap support for NSD */

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

#ifndef NSD_DNSTAP_H
#define NSD_DNSTAP_H

#include "dnstap/dnstap_config.h"

#ifdef USE_DNSTAP

struct nsd_options;
struct fstrm_io;
struct fstrm_queue;
struct dt_tls_writer;

struct dt_env {
	/** dnstap I/O thread */
	struct fstrm_iothr *iothr;

	/** dnstap I/O thread input queue */
	struct fstrm_iothr_queue *ioq;

	/** dnstap "identity" field, NULL if disabled */
	char *identity;

	/** dnstap "version" field, NULL if disabled */
	char *version;

	/** length of "identity" field */
	unsigned len_identity;

	/** length of "version" field */
	unsigned len_version;

	/** whether to log Message/AUTH_QUERY */
	unsigned log_auth_query_messages : 1;
	/** whether to log Message/AUTH_RESPONSE */
	unsigned log_auth_response_messages : 1;

	/** tls writer object, or NULL */
	struct dt_tls_writer* tls_writer;
};

/**
 * Create dnstap environment object. Afterwards, call dt_apply_cfg() to fill in
 * the config variables and dt_init() to fill in the per-worker state. Each
 * worker needs a copy of this object but with its own I/O queue (the fq field
 * of the structure) to ensure lock-free access to its own per-worker circular
 * queue.  Duplicate the environment object if more than one worker needs to
 * share access to the dnstap I/O socket.
 * @param socket_path: path to dnstap logging socket, must be non-NULL if used.
 * @param ip: if NULL or "" use socket path, otherwise IP or IP@port.
 * @param num_workers: number of worker threads, must be > 0.
 * @param tls: set to true to use TLS, otherwise, TCP. Used when ip is set.
 * @param tls_server_name: name for authenticating the upstream server, or
 * 	NULL or "".
 * @param tls_cert_bundle: pem bundle to verify server with. Or NULL or "".
 * @param tls_client_key_file: key file for client authentication. Or NULL
 * 	or "".
 * @param tls_client_cert_file: cert file for client authentication. Or NULL
 * 	or "".
 * @return dt_env object, NULL on failure.
 */
struct dt_env *
dt_create(const char *socket_path, char* ip, unsigned num_workers,
	int tls, char* tls_server_name, char* tls_cert_bundle,
	char* tls_client_key_file, char* tls_client_cert_file);

/**
 * Apply config settings.
 * @param env: dnstap environment object.
 * @param cfg: new config settings.
 */
void
dt_apply_cfg(struct dt_env *env, struct nsd_options *cfg);

/**
 * Initialize per-worker state in dnstap environment object.
 * @param env: dnstap environment object to initialize, created with dt_create().
 * @return: true on success, false on failure.
 */
int
dt_init(struct dt_env *env);

/**
 * Delete dnstap environment object. Closes dnstap I/O socket and deletes all
 * per-worker I/O queues.
 */
void
dt_delete(struct dt_env *env);

/**
 * Create and send a new dnstap "Message" event of type AUTH_QUERY.
 * @param env: dnstap environment object.
 * @param local_addr: address/port of server (local address).
 * @param addr: address/port of client.
 * @param is_tcp: true for tcp, false for udp.
 * @param zone: zone name, or NULL. in wireformat.
 * @param zonelen: length of zone in bytes.
 * @param pkt: query message.
 * @param pktlen: length of pkt.
 */
void
dt_msg_send_auth_query(struct dt_env *env,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	int is_tcp, uint8_t* zone, size_t zonelen, uint8_t* pkt, size_t pktlen);

/**
 * Create and send a new dnstap "Message" event of type AUTH_RESPONSE.
 * @param env: dnstap environment object.
 * @param local_addr: address/port of server (local address).
 * @param addr: address/port of client.
 * @param is_tcp: true for tcp, false for udp.
 * @param zone: zone name, or NULL. in wireformat.
 * @param zonelen: length of zone in bytes.
 * @param pkt: response message.
 * @param pktlen: length of pkt.
 */
void
dt_msg_send_auth_response(struct dt_env *env,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	int is_tcp, uint8_t* zone, size_t zonelen, uint8_t* pkt, size_t pktlen);

#endif /* USE_DNSTAP */

#endif /* NSD_DNSTAP_H */
