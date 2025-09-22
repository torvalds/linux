/*
 * daemon/worker.h - worker that handles a pending list of requests.
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
 * This file describes the worker structure that holds a list of 
 * pending requests and handles them.
 */

#ifndef DAEMON_WORKER_H
#define DAEMON_WORKER_H

#include "libunbound/worker.h"
#include "util/netevent.h"
#include "util/locks.h"
#include "util/alloc.h"
#include "util/data/msgreply.h"
#include "util/data/msgparse.h"
#include "daemon/stats.h"
#include "util/module.h"
#include "dnstap/dnstap.h"
struct listen_dnsport;
struct outside_network;
struct config_file;
struct daemon;
struct listen_port;
struct ub_randstate;
struct regional;
struct tube;
struct daemon_remote;
struct query_info;

/** worker commands */
enum worker_commands {
	/** make the worker quit */
	worker_cmd_quit,
	/** obtain statistics */
	worker_cmd_stats,
	/** obtain statistics without statsclear */
	worker_cmd_stats_noreset,
	/** execute remote control command */
	worker_cmd_remote,
	/** for fast-reload, perform stop */
	worker_cmd_reload_stop,
	/** for fast-reload, start again */
	worker_cmd_reload_start,
	/** for fast-reload, poll to make sure worker has released data */
	worker_cmd_reload_poll
};

/**
 * Structure holding working information for unbound.
 * Holds globally visible information.
 */
struct worker {
	/** the thread number (in daemon array). First in struct for debug. */
	int thread_num;
	/** global shared daemon structure */
	struct daemon* daemon;
	/** thread id */
	ub_thread_type thr_id;
#ifdef HAVE_GETTID
	/** thread tid, the LWP id. */
	pid_t thread_tid;
#endif
	/** pipe, for commands for this worker */
	struct tube* cmd;
	/** the event base this worker works with */
	struct comm_base* base;
	/** the frontside listening interface where request events come in */
	struct listen_dnsport* front;
	/** the backside outside network interface to the auth servers */
	struct outside_network* back;
	/** ports to be used by this worker. */
	int* ports;
	/** number of ports for this worker */
	int numports;
	/** the signal handler */
	struct comm_signal* comsig;
	/** commpoint to listen to commands. */
	struct comm_point* cmd_com;
	/** timer for statistics */
	struct comm_timer* stat_timer;
	/** ratelimit for errors, time value */
	time_t err_limit_time;
	/** ratelimit for errors, packet count */
	unsigned int err_limit_count;

	/** random() table for this worker. */
	struct ub_randstate* rndstate;
	/** do we need to restart or quit (on signal) */
	int need_to_exit;
	/** allocation cache for this thread */
	struct alloc_cache *alloc;
	/** per thread statistics */
	struct ub_server_stats stats;
	/** thread scratch regional */
	struct regional* scratchpad;

	/** module environment passed to modules, changed for this thread */
	struct module_env env;

#ifdef USE_DNSTAP
	/** dnstap environment, changed for this thread */
	struct dt_env dtenv;
#endif
	/** reuse existing cache on reload if other conditions allow it. */
	int reuse_cache;
};

/**
 * Create the worker structure. Bare bones version, zeroed struct,
 * with backpointers only. Use worker_init on it later.
 * @param daemon: the daemon that this worker thread is part of.
 * @param id: the thread number from 0.. numthreads-1.
 * @param ports: the ports it is allowed to use, array.
 * @param n: the number of ports.
 * @return: the new worker or NULL on alloc failure.
 */
struct worker* worker_create(struct daemon* daemon, int id, int* ports, int n);

/**
 * Initialize worker.
 * Allocates event base, listens to ports
 * @param worker: worker to initialize, created with worker_create.
 * @param cfg: configuration settings.
 * @param ports: list of shared query ports.
 * @param do_sigs: if true, worker installs signal handlers.
 * @return: false on error.
 */
int worker_init(struct worker* worker, struct config_file *cfg, 
	struct listen_port* ports, int do_sigs);

/**
 * Make worker work.
 */
void worker_work(struct worker* worker);

/**
 * Delete worker.
 */
void worker_delete(struct worker* worker);

/**
 * Send a command to a worker. Uses blocking writes.
 * @param worker: worker to send command to.
 * @param cmd: command to send.
 */
void worker_send_cmd(struct worker* worker, enum worker_commands cmd);

/**
 * Init worker stats - includes server_stats_init, outside network and mesh.
 * @param worker: the worker to init
 */
void worker_stats_clear(struct worker* worker);

#endif /* DAEMON_WORKER_H */
