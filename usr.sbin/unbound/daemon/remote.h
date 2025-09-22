/*
 * daemon/remote.h - remote control for the unbound daemon.
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
 * unbound-control tool, or a SSLv3/TLS capable web browser. 
 * The channel is secured using SSLv3 or TLSv1, and certificates.
 * Both the server and the client(control tool) have their own keys.
 */

#ifndef DAEMON_REMOTE_H
#define DAEMON_REMOTE_H
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#include "util/locks.h"
struct config_file;
struct listen_list;
struct listen_port;
struct worker;
struct comm_reply;
struct comm_point;
struct daemon_remote;
struct config_strlist_head;

/** number of milliseconds timeout on incoming remote control handshake */
#define REMOTE_CONTROL_TCP_TIMEOUT 120000

/**
 * a busy control command connection, SSL state
 */
struct rc_state {
	/** the next item in list */
	struct rc_state* next;
	/** the commpoint */
	struct comm_point* c;
	/** in the handshake part */
	enum { rc_none, rc_hs_read, rc_hs_write } shake_state;
#ifdef HAVE_SSL
	/** the ssl state */
	SSL* ssl;
#endif
	/** file descriptor */
        int fd;
	/** the rc this is part of */
	struct daemon_remote* rc;
};

/**
 * The remote control tool state.
 * The state is only created for the first thread, other threads
 * are called from this thread.  Only the first threads listens to
 * the control port.  The other threads do not, but are called on the
 * command channel(pipe) from the first thread.
 */
struct daemon_remote {
	/** the worker for this remote control */
	struct worker* worker;
	/** commpoints for accepting remote control connections */
	struct listen_list* accept_list;
	/* if certificates are used */
	int use_cert;
	/** number of active commpoints that are handling remote control */
	int active;
	/** max active commpoints */
	int max_active;
	/** current commpoints busy; should be a short list, malloced */
	struct rc_state* busy_list;
#ifdef HAVE_SSL
	/** the SSL context for creating new SSL streams */
	SSL_CTX* ctx;
#endif
};

/**
 * Connection to print to, either SSL or plain over fd
 */
struct remote_stream {
#ifdef HAVE_SSL
	/** SSL structure, nonNULL if using SSL */
	SSL* ssl;
#endif
	/** file descriptor for plain transfer */
	int fd;
};
typedef struct remote_stream RES;

/**
 * Notification status. This is exchanged between the fast reload thread
 * and the server thread, over the commpair sockets.
 */
enum fast_reload_notification {
	/** nothing, not used */
	fast_reload_notification_none = 0,
	/** the fast reload thread is done */
	fast_reload_notification_done = 1,
	/** the fast reload thread is done but with an error, it failed */
	fast_reload_notification_done_error = 2,
	/** the fast reload thread is told to exit by the server thread.
	 * Sent on server quit while the reload is running. */
	fast_reload_notification_exit = 3,
	/** the fast reload thread has exited, after being told to exit */
	fast_reload_notification_exited = 4,
	/** the fast reload thread has information to print out */
	fast_reload_notification_printout = 5,
	/** stop as part of the reload the thread and other threads */
	fast_reload_notification_reload_stop = 6,
	/** ack the stop as part of the reload, and also ack start */
	fast_reload_notification_reload_ack = 7,
	/** resume from stop as part of the reload */
	fast_reload_notification_reload_start = 8,
	/** the fast reload thread wants the mainthread to poll workers,
	 * after the reload, sent when nopause is used */
	fast_reload_notification_reload_nopause_poll = 9
};

/**
 * Fast reload printout queue. Contains a list of strings, that need to be
 * printed over the file descriptor.
 */
struct fast_reload_printq {
	/** if this item is in a list, the previous and next */
	struct fast_reload_printq *prev, *next;
	/** if this item is in a list, it is true. */
	int in_list;
	/** list of strings to printout */
	struct config_strlist_head* to_print;
	/** the current item to print. It is malloced. NULL if none. */
	char* client_item;
	/** The length, strlen, of the client_item, that has to be sent. */
	int client_len;
	/** The number of bytes sent of client_item. */
	int client_byte_count;
	/** the comm point for the client connection, the remote control
	 * client. */
	struct comm_point* client_cp;
	/** the remote control connection to print output to. */
	struct remote_stream remote;
	/** the worker that the event is added in */
	struct worker* worker;
};

/**
 * Fast reload auth zone change. Keeps track if an auth zone was removed,
 * added or changed. This is needed because workers can have events for
 * dealing with auth zones, like transfers, and those have to be removed
 * too, not just the auth zone structure from the tree. */
struct fast_reload_auth_change {
	/** next in the list of auth zone changes. */
	struct fast_reload_auth_change* next;
	/** the zone in the old config */
	struct auth_zone* old_z;
	/** the zone in the new config */
	struct auth_zone* new_z;
	/** if the zone was deleted */
	int is_deleted;
	/** if the zone was added */
	int is_added;
	/** if the zone has been changed */
	int is_changed;
};

/**
 * Fast reload thread structure
 */
struct fast_reload_thread {
	/** the thread number for the dtio thread,
	 * must be first to cast thread arg to int* in checklock code. */
	int threadnum;
	/** communication socket pair, that sends commands */
	int commpair[2];
	/** thread id, of the io thread */
	ub_thread_type tid;
	/** if the io processing has started */
	int started;
	/** if the thread has to quit */
	int need_to_quit;
	/** verbosity of the fast_reload command, the number of +v options */
	int fr_verb;
	/** option to not pause threads during reload */
	int fr_nopause;
	/** option to drop mesh queries */
	int fr_drop_mesh;

	/** the event that listens on the remote service worker to the
	 * commpair, it receives content from the fast reload thread. */
	void* service_event;
	/** if the event that listens on the remote service worker has
	 * been added to the comm base. */
	int service_event_is_added;
	/** the service event can read a cmd, nonblocking, so it can
	 * save the partial read cmd here */
	uint32_t service_read_cmd;
	/** the number of bytes in service_read_cmd */
	int service_read_cmd_count;
	/** the worker that the service_event is added in */
	struct worker* worker;

	/** the printout of output to the remote client. */
	struct fast_reload_printq *printq;

	/** lock on fr_output, to stop race when both remote control thread
	 * and fast reload thread use fr_output list. */
	lock_basic_type fr_output_lock;
	/** list of strings, that the fast reload thread produces that have
	 * to be printed. The remote control thread can pick them up with
	 * the lock. */
	struct config_strlist_head* fr_output;

	/** communication socket pair, to respond to the reload request */
	int commreload[2];

	/** the list of auth zone changes. */
	struct fast_reload_auth_change* auth_zone_change_list;
	/** the old tree of auth zones, to lookup. */
	struct auth_zones* old_auth_zones;
};

/**
 * Create new remote control state for the daemon.
 * @param cfg: config file with key file settings.
 * @return new state, or NULL on failure.
 */
struct daemon_remote* daemon_remote_create(struct config_file* cfg);

/**
 * remote control state to delete.
 * @param rc: state to delete.
 */
void daemon_remote_delete(struct daemon_remote* rc);

/**
 * remote control state to clear up. Busy and accept points are closed.
 * Does not delete the rc itself, or the ssl context (with its keys).
 * @param rc: state to clear.
 */
void daemon_remote_clear(struct daemon_remote* rc);

/**
 * Open and create listening ports for remote control.
 * @param cfg: config options.
 * @return list of ports or NULL on failure.
 *	can be freed with listening_ports_free().
 */
struct listen_port* daemon_remote_open_ports(struct config_file* cfg);

/**
 * Setup comm points for accepting remote control connections.
 * @param rc: state
 * @param ports: already opened ports.
 * @param worker: worker with communication base. and links to command channels.
 * @return false on error.
 */
int daemon_remote_open_accept(struct daemon_remote* rc, 
	struct listen_port* ports, struct worker* worker);

/**
 * Stop accept handlers for TCP (until enabled again)
 * @param rc: state
 */
void daemon_remote_stop_accept(struct daemon_remote* rc);

/**
 * Stop accept handlers for TCP (until enabled again)
 * @param rc: state
 */
void daemon_remote_start_accept(struct daemon_remote* rc);

/**
 * Handle nonthreaded remote cmd execution.
 * @param worker: this worker (the remote worker).
 */
void daemon_remote_exec(struct worker* worker);

#ifdef HAVE_SSL
/** 
 * Print fixed line of text over ssl connection in blocking mode
 * @param ssl: print to
 * @param text: the text.
 * @return false on connection failure.
 */
int ssl_print_text(RES* ssl, const char* text);

/** 
 * printf style printing to the ssl connection
 * @param ssl: the RES connection to print to. Blocking.
 * @param format: printf style format string.
 * @return success or false on a network failure.
 */
int ssl_printf(RES* ssl, const char* format, ...)
        ATTR_FORMAT(printf, 2, 3);

/**
 * Read until \n is encountered
 * If stream signals EOF, the string up to then is returned (without \n).
 * @param ssl: the RES connection to read from. blocking.
 * @param buf: buffer to read to.
 * @param max: size of buffer.
 * @return false on connection failure.
 */
int ssl_read_line(RES* ssl, char* buf, size_t max);
#endif /* HAVE_SSL */

/**
 * Start fast reload thread
 * @param ssl: the RES connection to print to.
 * @param worker: the remote servicing worker.
 * @param s: the rc_state that is servicing the remote control connection to
 *	the remote control client. It needs to be moved away to stay connected
 *	while the fast reload is running.
 * @param fr_verb: verbosity to print output at. 0 is nothing, 1 is some
 *	and 2 is more detail.
 * @param fr_nopause: option to not pause threads during reload.
 * @param fr_drop_mesh: option to drop mesh queries.
 */
void fast_reload_thread_start(RES* ssl, struct worker* worker,
	struct rc_state* s, int fr_verb, int fr_nopause, int fr_drop_mesh);

/**
 * Stop fast reload thread
 * @param fast_reload_thread: the thread struct.
 */
void fast_reload_thread_stop(struct fast_reload_thread* fast_reload_thread);

/** fast reload thread commands to remote service thread event callback */
void fast_reload_service_cb(int fd, short bits, void* arg);

/** fast reload callback for the remote control client connection */
int fast_reload_client_callback(struct comm_point* c, void* arg, int err,
	struct comm_reply* rep);

/** fast reload printq delete list */
void fast_reload_printq_list_delete(struct fast_reload_printq* list);

/** Pick up per worker changes after a fast reload. */
void fast_reload_worker_pickup_changes(struct worker* worker);

#endif /* DAEMON_REMOTE_H */
