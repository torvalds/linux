/*
 * daemon/daemon.h - collection of workers that handles requests.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
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
 * The daemon consists of global settings and a number of workers.
 */

#ifndef DAEMON_H
#define DAEMON_H

#include "util/locks.h"
#include "util/alloc.h"
#include "services/modstack.h"
struct config_file;
struct worker;
struct listen_port;
struct slabhash;
struct module_env;
struct rrset_cache;
struct acl_list;
struct local_zones;
struct views;
struct ub_randstate;
struct daemon_remote;
struct respip_set;
struct shm_main_info;
struct doq_table;
struct cookie_secrets;
struct fast_reload_thread;
struct fast_reload_printq;

#include "dnstap/dnstap_config.h"
#ifdef USE_DNSTAP
struct dt_env;
#endif

#include "dnscrypt/dnscrypt_config.h"
#ifdef USE_DNSCRYPT
struct dnsc_env;
#endif

/**
 * Structure holding worker list.
 * Holds globally visible information.
 */
struct daemon {
	/** The config settings */
	struct config_file* cfg;
	/** the chroot dir in use, NULL if none */
	char* chroot;
	/** pidfile that is used */
	char* pidfile;
	/** port number that has ports opened. */
	int listening_port;
	/** array of listening ports, opened.  Listening ports per worker,
	 * or just one element[0] shared by the worker threads. */
	struct listen_port** ports;
	/** size of ports array */
	size_t num_ports;
	/** reuseport is enabled if true */
	int reuseport;
	/** port number for remote that has ports opened. */
	int rc_port;
	/** listening ports for remote control */
	struct listen_port* rc_ports;
	/** remote control connections management (for first worker) */
	struct daemon_remote* rc;
	/** ssl context for listening to dnstcp over ssl */
	void* listen_dot_sslctx;
	/** ssl context for connecting to dnstcp over ssl */
	void* connect_dot_sslctx;
	/** ssl context for listening to DoH */
	void* listen_doh_sslctx;
	/** ssl context for listening to quic */
	void* listen_quic_sslctx;
	/** num threads allocated */
	int num;
	/** num threads allocated in the previous config or 0 at first */
	int old_num;
	/** the worker entries */
	struct worker** workers;
	/** per-worker allocation cache */
	struct alloc_cache **worker_allocs;
	/** do we need to exit unbound (or is it only a reload?) */
	int need_to_exit;
	/** master random table ; used for port div between threads on reload*/
	struct ub_randstate* rand;
	/** master allocation cache */
	struct alloc_cache superalloc;
	/** the module environment master value, copied and changed by threads*/
	struct module_env* env;
	/** stack of module callbacks */
	struct module_stack mods;
	/** The module stack has been inited */
	int mods_inited;
	/** access control, which client IPs are allowed to connect */
	struct acl_list* acl;
	/** access control, which interfaces are allowed to connect */
	struct acl_list* acl_interface;
	/** TCP connection limit, limit connections from client IPs */
	struct tcl_list* tcl;
	/** local authority zones */
	struct local_zones* local_zones;
	/** last time of statistics printout */
	struct timeval time_last_stat;
	/** time when daemon started */
	struct timeval time_boot;
#ifdef USE_DNSTAP
	/** the dnstap environment master value, copied and changed by threads*/
	struct dt_env* dtenv;
#endif
	struct shm_main_info* shm_info;
	/** some response-ip tags or actions are configured if true */
	int use_response_ip;
	/** some RPZ policies are configured */
	int use_rpz;
#ifdef USE_DNSCRYPT
	/** the dnscrypt environment */
	struct dnsc_env* dnscenv;
#endif
	/** the doq connection table */
	struct doq_table* doq_table;
	/** reuse existing cache on reload if other conditions allow it. */
	int reuse_cache;
	/** the EDNS cookie secrets from the cookie-secret-file */
	struct cookie_secrets* cookie_secrets;
	/** the fast reload thread, or NULL */
	struct fast_reload_thread* fast_reload_thread;
	/** the fast reload printq list */
	struct fast_reload_printq* fast_reload_printq_list;
	/** the fast reload option to drop mesh queries, true if so. */
	int fast_reload_drop_mesh;
	/** for fast reload, if the tcl, tcp connection limits, has
	 * changes for workers */
	int fast_reload_tcl_has_changes;
	/** config file name */
	char* cfgfile;
};

/**
 * Initialize daemon structure.
 * @return: The daemon structure, or NULL on error.
 */
struct daemon* daemon_init(void);

/**
 * Open shared listening ports (if needed).
 * The cfg member pointer must have been set for the daemon.
 * @param daemon: the daemon.
 * @return: false on error.
 */
int daemon_open_shared_ports(struct daemon* daemon);

/**
 * Do daemon setup that needs privileges
 * like opening privileged ports or opening device files.
 * The cfg member pointer must have been set for the daemon.
 * @param daemon: the daemon.
 * @return: false on error.
 */
int daemon_privileged(struct daemon* daemon);

/**
 * Fork workers and start service.
 * When the routine exits, it is no longer forked.
 * @param daemon: the daemon.
 */
void daemon_fork(struct daemon* daemon);

/**
 * Close off the worker thread information.
 * Bring the daemon back into state ready for daemon_fork again.
 * @param daemon: the daemon.
 */
void daemon_cleanup(struct daemon* daemon);

/**
 * Delete workers, close listening ports.
 * @param daemon: the daemon.
 */
void daemon_delete(struct daemon* daemon);

/**
 * Apply config settings.
 * @param daemon: the daemon.
 * @param cfg: new config settings.
 */
void daemon_apply_cfg(struct daemon* daemon, struct config_file* cfg);

/**
 * Setup acl list to have entries for the port list.
 * @param list: the acl interface
 * @param port_list: list of open ports, or none.
 * @return false on failure
 */
int setup_acl_for_ports(struct acl_list* list, struct listen_port* port_list);

#endif /* DAEMON_H */
