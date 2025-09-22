/*
 * daemon/stats.h - collect runtime performance indicators.
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
 * This file describes the data structure used to collect runtime performance
 * numbers. These 'statistics' may be of interest to the operator.
 */

#ifndef DAEMON_STATS_H
#define DAEMON_STATS_H
#include "util/timehist.h"
struct worker;
struct config_file;
struct comm_point;
struct comm_reply;
struct edns_data;
struct sldns_buffer;

/* stats struct */
#include "libunbound/unbound.h"

/** 
 * Initialize server stats to 0.
 * @param stats: what to init (this is alloced by the caller).
 * @param cfg: with extended statistics option.
 */
void server_stats_init(struct ub_server_stats* stats, struct config_file* cfg);

/** add query if it missed the cache */
void server_stats_querymiss(struct ub_server_stats* stats, struct worker* worker);

/** add query if was cached and also resulted in a prefetch */
void server_stats_prefetch(struct ub_server_stats* stats, struct worker* worker);

/** display the stats to the log */
void server_stats_log(struct ub_server_stats* stats, struct worker* worker,
	int threadnum);

/**
 * Obtain the stats info for a given thread. Uses pipe to communicate.
 * @param worker: the worker that is executing (the first worker).
 * @param who: on who to get the statistics info.
 * @param s: the stats block to fill in.
 * @param reset: if stats can be reset.
 */
void server_stats_obtain(struct worker* worker, struct worker* who,
	struct ub_stats_info* s, int reset);

/**
 * Compile stats into structure for this thread worker.
 * Also clears the statistics counters (if that is set by config file).
 * @param worker: the worker to compile stats for, also the executing worker.
 * @param s: stats block.
 * @param reset: if true, depending on config stats are reset.
 * 	if false, statistics are not reset.
 */
void server_stats_compile(struct worker* worker, struct ub_stats_info* s, 
	int reset);

/**
 * Send stats over comm tube in reply to query cmd
 * @param worker: this worker.
 * @param reset: if true, depending on config stats are reset.
 * 	if false, statistics are not reset.
 */
void server_stats_reply(struct worker* worker, int reset);

/**
 * Addup stat blocks.
 * @param total: sum of the two entries.
 * @param a: to add to it.
 */
void server_stats_add(struct ub_stats_info* total, struct ub_stats_info* a);

/**
 * Add stats for this query
 * @param stats: the stats
 * @param c: commpoint with type and buffer.
 * @param qtype: query type
 * @param qclass: query class
 * @param edns: edns record
 * @param repinfo: reply info with remote address
 */
void server_stats_insquery(struct ub_server_stats* stats, struct comm_point* c,
	uint16_t qtype, uint16_t qclass, struct edns_data* edns, 
	struct comm_reply* repinfo);

/**
 * Add rcode for this query.
 * @param stats: the stats
 * @param buf: buffer with rcode. If buffer is length0: not counted.
 */
void server_stats_insrcode(struct ub_server_stats* stats, struct sldns_buffer* buf);

/**
 * Add DNS Cookie stats for this query
 * @param stats: the stats
 * @param edns: edns record
 */
void server_stats_downstream_cookie(struct ub_server_stats* stats,
	struct edns_data* edns);
#endif /* DAEMON_STATS_H */
