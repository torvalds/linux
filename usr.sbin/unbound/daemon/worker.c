/*
 * daemon/worker.c - worker that handles a pending list of requests.
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
 * This file implements the worker that handles callbacks on events, for
 * pending requests.
 */
#include "config.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/random.h"
#include "daemon/worker.h"
#include "daemon/daemon.h"
#include "daemon/remote.h"
#include "daemon/acl_list.h"
#include "util/netevent.h"
#include "util/config_file.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/storage/slabhash.h"
#include "services/listen_dnsport.h"
#include "services/outside_network.h"
#include "services/outbound_list.h"
#include "services/cache/rrset.h"
#include "services/cache/infra.h"
#include "services/cache/dns.h"
#include "services/authzone.h"
#include "services/mesh.h"
#include "services/localzone.h"
#include "services/rpz.h"
#include "util/data/msgparse.h"
#include "util/data/msgencode.h"
#include "util/data/dname.h"
#include "util/fptr_wlist.h"
#include "util/proxy_protocol.h"
#include "util/tube.h"
#include "util/edns.h"
#include "util/timeval_func.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_hints.h"
#include "iterator/iter_utils.h"
#include "validator/autotrust.h"
#include "validator/val_anchor.h"
#include "respip/respip.h"
#include "libunbound/context.h"
#include "libunbound/libworker.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "util/shm_side/shm_main.h"
#include "dnscrypt/dnscrypt.h"
#include "dnstap/dtstream.h"

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <signal.h>
#ifdef UB_ON_WINDOWS
#include "winrc/win_svc.h"
#endif

/** Size of an UDP datagram */
#define NORMAL_UDP_SIZE	512 /* bytes */
/** ratelimit for error responses */
#define ERROR_RATELIMIT 100 /* qps */

/**
 * seconds to add to prefetch leeway.  This is a TTL that expires old rrsets
 * earlier than they should in order to put the new update into the cache.
 * This additional value is to make sure that if not all TTLs are equal in
 * the message to be updated(and replaced), that rrsets with up to this much
 * extra TTL are also replaced.  This means that the resulting new message
 * will have (most likely) this TTL at least, avoiding very small 'split
 * second' TTLs due to operators choosing relative primes for TTLs (or so).
 * Also has to be at least one to break ties (and overwrite cached entry).
 */
#define PREFETCH_EXPIRY_ADD 60

/** Report on memory usage by this thread and global */
static void
worker_mem_report(struct worker* ATTR_UNUSED(worker),
	struct serviced_query* ATTR_UNUSED(cur_serv))
{
#ifdef UNBOUND_ALLOC_STATS
	/* measure memory leakage */
	extern size_t unbound_mem_alloc, unbound_mem_freed;
	/* debug func in validator module */
	size_t total, front, back, mesh, msg, rrset, infra, ac, superac;
	size_t me, iter, val, anch;
	int i;
#ifdef CLIENT_SUBNET
	size_t subnet = 0;
#endif /* CLIENT_SUBNET */
	if(verbosity < VERB_ALGO)
		return;
	front = listen_get_mem(worker->front);
	back = outnet_get_mem(worker->back);
	msg = slabhash_get_mem(worker->env.msg_cache);
	rrset = slabhash_get_mem(&worker->env.rrset_cache->table);
	infra = infra_get_mem(worker->env.infra_cache);
	mesh = mesh_get_mem(worker->env.mesh);
	ac = alloc_get_mem(worker->alloc);
	superac = alloc_get_mem(&worker->daemon->superalloc);
	anch = anchors_get_mem(worker->env.anchors);
	iter = 0;
	val = 0;
	for(i=0; i<worker->env.mesh->mods.num; i++) {
		fptr_ok(fptr_whitelist_mod_get_mem(worker->env.mesh->
			mods.mod[i]->get_mem));
		if(strcmp(worker->env.mesh->mods.mod[i]->name, "validator")==0)
			val += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#ifdef CLIENT_SUBNET
		else if(strcmp(worker->env.mesh->mods.mod[i]->name,
			"subnetcache")==0)
			subnet += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#endif /* CLIENT_SUBNET */
		else	iter += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
	}
	me = sizeof(*worker) + sizeof(*worker->base) + sizeof(*worker->comsig)
		+ comm_point_get_mem(worker->cmd_com)
		+ sizeof(worker->rndstate)
		+ regional_get_mem(worker->scratchpad)
		+ sizeof(*worker->env.scratch_buffer)
		+ sldns_buffer_capacity(worker->env.scratch_buffer);
	if(worker->daemon->env->fwds)
		log_info("forwards=%u", (unsigned)forwards_get_mem(worker->env.fwds));
	if(worker->daemon->env->hints)
		log_info("hints=%u", (unsigned)hints_get_mem(worker->env.hints));
	if(worker->thread_num == 0)
		me += acl_list_get_mem(worker->daemon->acl);
	if(cur_serv) {
		me += serviced_get_mem(cur_serv);
	}
	total = front+back+mesh+msg+rrset+infra+iter+val+ac+superac+me;
#ifdef CLIENT_SUBNET
	total += subnet;
	log_info("Memory conditions: %u front=%u back=%u mesh=%u msg=%u "
		"rrset=%u infra=%u iter=%u val=%u subnet=%u anchors=%u "
		"alloccache=%u globalalloccache=%u me=%u",
		(unsigned)total, (unsigned)front, (unsigned)back,
		(unsigned)mesh, (unsigned)msg, (unsigned)rrset, (unsigned)infra,
		(unsigned)iter, (unsigned)val,
		(unsigned)subnet, (unsigned)anch, (unsigned)ac,
		(unsigned)superac, (unsigned)me);
#else /* no CLIENT_SUBNET */
	log_info("Memory conditions: %u front=%u back=%u mesh=%u msg=%u "
		"rrset=%u infra=%u iter=%u val=%u anchors=%u "
		"alloccache=%u globalalloccache=%u me=%u",
		(unsigned)total, (unsigned)front, (unsigned)back,
		(unsigned)mesh, (unsigned)msg, (unsigned)rrset,
		(unsigned)infra, (unsigned)iter, (unsigned)val, (unsigned)anch,
		(unsigned)ac, (unsigned)superac, (unsigned)me);
#endif /* CLIENT_SUBNET */
	log_info("Total heap memory estimate: %u  total-alloc: %u  "
		"total-free: %u", (unsigned)total,
		(unsigned)unbound_mem_alloc, (unsigned)unbound_mem_freed);
#else /* no UNBOUND_ALLOC_STATS */
	size_t val = 0;
#ifdef CLIENT_SUBNET
	size_t subnet = 0;
#endif /* CLIENT_SUBNET */
	int i;
	if(verbosity < VERB_QUERY)
		return;
	for(i=0; i<worker->env.mesh->mods.num; i++) {
		fptr_ok(fptr_whitelist_mod_get_mem(worker->env.mesh->
			mods.mod[i]->get_mem));
		if(strcmp(worker->env.mesh->mods.mod[i]->name, "validator")==0)
			val += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#ifdef CLIENT_SUBNET
		else if(strcmp(worker->env.mesh->mods.mod[i]->name,
			"subnetcache")==0)
			subnet += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#endif /* CLIENT_SUBNET */
	}
#ifdef CLIENT_SUBNET
	verbose(VERB_QUERY, "cache memory msg=%u rrset=%u infra=%u val=%u "
		"subnet=%u",
		(unsigned)slabhash_get_mem(worker->env.msg_cache),
		(unsigned)slabhash_get_mem(&worker->env.rrset_cache->table),
		(unsigned)infra_get_mem(worker->env.infra_cache),
		(unsigned)val, (unsigned)subnet);
#else /* no CLIENT_SUBNET */
	verbose(VERB_QUERY, "cache memory msg=%u rrset=%u infra=%u val=%u",
		(unsigned)slabhash_get_mem(worker->env.msg_cache),
		(unsigned)slabhash_get_mem(&worker->env.rrset_cache->table),
		(unsigned)infra_get_mem(worker->env.infra_cache),
		(unsigned)val);
#endif /* CLIENT_SUBNET */
#endif /* UNBOUND_ALLOC_STATS */
}

void
worker_send_cmd(struct worker* worker, enum worker_commands cmd)
{
	uint32_t c = (uint32_t)htonl(cmd);
	if(!tube_write_msg(worker->cmd, (uint8_t*)&c, sizeof(c), 0)) {
		log_err("worker send cmd %d failed", (int)cmd);
	}
}

int
worker_handle_service_reply(struct comm_point* c, void* arg, int error,
	struct comm_reply* reply_info)
{
	struct outbound_entry* e = (struct outbound_entry*)arg;
	struct worker* worker = e->qstate->env->worker;
	struct serviced_query *sq = e->qsent;

	verbose(VERB_ALGO, "worker svcd callback for qstate %p", e->qstate);
	if(error != 0) {
		mesh_report_reply(worker->env.mesh, e, reply_info, error);
		worker_mem_report(worker, sq);
		return 0;
	}
	/* sanity check. */
	if(!LDNS_QR_WIRE(sldns_buffer_begin(c->buffer))
		|| LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) !=
			LDNS_PACKET_QUERY
		|| LDNS_QDCOUNT(sldns_buffer_begin(c->buffer)) > 1) {
		/* error becomes timeout for the module as if this reply
		 * never arrived. */
		verbose(VERB_ALGO, "worker: bad reply handled as timeout");
		mesh_report_reply(worker->env.mesh, e, reply_info,
			NETEVENT_TIMEOUT);
		worker_mem_report(worker, sq);
		return 0;
	}
	mesh_report_reply(worker->env.mesh, e, reply_info, NETEVENT_NOERROR);
	worker_mem_report(worker, sq);
	return 0;
}

/** ratelimit error replies
 * @param worker: the worker struct with ratelimit counter
 * @param err: error code that would be wanted.
 * @return value of err if okay, or -1 if it should be discarded instead.
 */
static int
worker_err_ratelimit(struct worker* worker, int err)
{
	if(worker->err_limit_time == *worker->env.now) {
		/* see if limit is exceeded for this second */
		if(worker->err_limit_count++ > ERROR_RATELIMIT)
			return -1;
	} else {
		/* new second, new limits */
		worker->err_limit_time = *worker->env.now;
		worker->err_limit_count = 1;
	}
	return err;
}

/**
 * Structure holding the result of the worker_check_request function.
 * Based on configuration it could be called up to four times; ideally should
 * be called once.
 */
struct check_request_result {
	int checked;
	int value;
};
/** check request sanity.
 * @param pkt: the wire packet to examine for sanity.
 * @param worker: parameters for checking.
 * @param out: struct to update with the result.
*/
static void
worker_check_request(sldns_buffer* pkt, struct worker* worker,
	struct check_request_result* out)
{
	if(out->checked) return;
	out->checked = 1;
	if(sldns_buffer_limit(pkt) < LDNS_HEADER_SIZE) {
		verbose(VERB_QUERY, "request too short, discarded");
		out->value = -1;
		return;
	}
	if(sldns_buffer_limit(pkt) > NORMAL_UDP_SIZE &&
		worker->daemon->cfg->harden_large_queries) {
		verbose(VERB_QUERY, "request too large, discarded");
		out->value = -1;
		return;
	}
	if(LDNS_QR_WIRE(sldns_buffer_begin(pkt))) {
		verbose(VERB_QUERY, "request has QR bit on, discarded");
		out->value = -1;
		return;
	}
	if(LDNS_TC_WIRE(sldns_buffer_begin(pkt))) {
		LDNS_TC_CLR(sldns_buffer_begin(pkt));
		verbose(VERB_QUERY, "request bad, has TC bit on");
		out->value = worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
		return;
	}
	if(LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_QUERY &&
		LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_NOTIFY) {
		verbose(VERB_QUERY, "request unknown opcode %d",
			LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)));
		out->value = worker_err_ratelimit(worker, LDNS_RCODE_NOTIMPL);
		return;
	}
	if(LDNS_QDCOUNT(sldns_buffer_begin(pkt)) != 1) {
		verbose(VERB_QUERY, "request wrong nr qd=%d",
			LDNS_QDCOUNT(sldns_buffer_begin(pkt)));
		out->value = worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
		return;
	}
	if(LDNS_ANCOUNT(sldns_buffer_begin(pkt)) != 0 &&
		(LDNS_ANCOUNT(sldns_buffer_begin(pkt)) != 1 ||
		LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_NOTIFY)) {
		verbose(VERB_QUERY, "request wrong nr an=%d",
			LDNS_ANCOUNT(sldns_buffer_begin(pkt)));
		out->value = worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
		return;
	}
	if(LDNS_NSCOUNT(sldns_buffer_begin(pkt)) != 0) {
		verbose(VERB_QUERY, "request wrong nr ns=%d",
			LDNS_NSCOUNT(sldns_buffer_begin(pkt)));
		out->value = worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
		return;
	}
	if(LDNS_ARCOUNT(sldns_buffer_begin(pkt)) > 1) {
		verbose(VERB_QUERY, "request wrong nr ar=%d",
			LDNS_ARCOUNT(sldns_buffer_begin(pkt)));
		out->value = worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
		return;
	}
	out->value = 0;
	return;
}

/**
 * Send fast-reload acknowledgement to the mainthread in one byte.
 * This signals that this worker has received the previous command.
 * The worker is waiting if that is after a reload_stop command.
 * Or the worker has briefly processed the event itself, and in doing so
 * released data pointers to old config, after a reload_poll command.
 */
static void
worker_send_reload_ack(struct worker* worker)
{
	/* If this is clipped to 8 bits because thread_num>255, then that
	 * is not a problem, the receiver counts the number of bytes received.
	 * The number is informative only. */
	uint8_t c = (uint8_t)worker->thread_num;
	ssize_t ret;
	while(1) {
		ret = send(worker->daemon->fast_reload_thread->commreload[1],
			(void*)&c, 1, 0);
		if(ret == -1) {
			if(
#ifndef USE_WINSOCK
				errno == EINTR || errno == EAGAIN
#  ifdef EWOULDBLOCK
				|| errno == EWOULDBLOCK
#  endif
#else
				WSAGetLastError() == WSAEINTR ||
				WSAGetLastError() == WSAEINPROGRESS ||
				WSAGetLastError() == WSAEWOULDBLOCK
#endif
				)
				continue; /* Try again. */
			log_err("worker reload ack reply: send failed: %s",
				sock_strerror(errno));
			break;
		}
		break;
	}
}

/** stop and wait to resume the worker */
static void
worker_stop_and_wait(struct worker* worker)
{
	uint8_t* buf = NULL;
	uint32_t len = 0, cmd;
	worker_send_reload_ack(worker);
	/* wait for reload */
	if(!tube_read_msg(worker->cmd, &buf, &len, 0)) {
		log_err("worker reload read reply failed");
		return;
	}
	if(len != sizeof(uint32_t)) {
		log_err("worker reload reply, bad control msg length %d",
			(int)len);
		free(buf);
		return;
	}
	cmd = sldns_read_uint32(buf);
	free(buf);
	if(cmd == worker_cmd_quit) {
		/* quit anyway */
		verbose(VERB_ALGO, "reload reply, control cmd quit");
		comm_base_exit(worker->base);
		return;
	}
	if(cmd != worker_cmd_reload_start) {
		log_err("worker reload reply, wrong reply command");
	}
	if(worker->daemon->fast_reload_drop_mesh) {
		verbose(VERB_ALGO, "worker: drop mesh queries after reload");
		mesh_delete_all(worker->env.mesh);
	}
	fast_reload_worker_pickup_changes(worker);
	worker_send_reload_ack(worker);
	verbose(VERB_ALGO, "worker resume after reload");
}

void
worker_handle_control_cmd(struct tube* ATTR_UNUSED(tube), uint8_t* msg,
	size_t len, int error, void* arg)
{
	struct worker* worker = (struct worker*)arg;
	enum worker_commands cmd;
	if(error != NETEVENT_NOERROR) {
		free(msg);
		if(error == NETEVENT_CLOSED)
			comm_base_exit(worker->base);
		else	log_info("control event: %d", error);
		return;
	}
	if(len != sizeof(uint32_t)) {
		fatal_exit("bad control msg length %d", (int)len);
	}
	cmd = sldns_read_uint32(msg);
	free(msg);
	switch(cmd) {
	case worker_cmd_quit:
		verbose(VERB_ALGO, "got control cmd quit");
		comm_base_exit(worker->base);
		break;
	case worker_cmd_stats:
		verbose(VERB_ALGO, "got control cmd stats");
		server_stats_reply(worker, 1);
		break;
	case worker_cmd_stats_noreset:
		verbose(VERB_ALGO, "got control cmd stats_noreset");
		server_stats_reply(worker, 0);
		break;
	case worker_cmd_remote:
		verbose(VERB_ALGO, "got control cmd remote");
		daemon_remote_exec(worker);
		break;
	case worker_cmd_reload_stop:
		verbose(VERB_ALGO, "got control cmd reload_stop");
		worker_stop_and_wait(worker);
		break;
	case worker_cmd_reload_poll:
		verbose(VERB_ALGO, "got control cmd reload_poll");
		fast_reload_worker_pickup_changes(worker);
		worker_send_reload_ack(worker);
		break;
	default:
		log_err("bad command %d", (int)cmd);
		break;
	}
}

/** check if a delegation is secure */
static enum sec_status
check_delegation_secure(struct reply_info *rep)
{
	/* return smallest security status */
	size_t i;
	enum sec_status sec = sec_status_secure;
	enum sec_status s;
	size_t num = rep->an_numrrsets + rep->ns_numrrsets;
	/* check if answer and authority are OK */
	for(i=0; i<num; i++) {
		s = ((struct packed_rrset_data*)rep->rrsets[i]->entry.data)
			->security;
		if(s < sec)
			sec = s;
	}
	/* in additional, only unchecked triggers revalidation */
	for(i=num; i<rep->rrset_count; i++) {
		s = ((struct packed_rrset_data*)rep->rrsets[i]->entry.data)
			->security;
		if(s == sec_status_unchecked)
			return s;
	}
	return sec;
}

/** remove nonsecure from a delegation referral additional section */
static void
deleg_remove_nonsecure_additional(struct reply_info* rep)
{
	/* we can simply edit it, since we are working in the scratch region */
	size_t i;
	enum sec_status s;

	for(i = rep->an_numrrsets+rep->ns_numrrsets; i<rep->rrset_count; i++) {
		s = ((struct packed_rrset_data*)rep->rrsets[i]->entry.data)
			->security;
		if(s != sec_status_secure) {
			memmove(rep->rrsets+i, rep->rrsets+i+1,
				sizeof(struct ub_packed_rrset_key*)*
				(rep->rrset_count - i - 1));
			rep->ar_numrrsets--;
			rep->rrset_count--;
			i--;
		}
	}
}

/** answer nonrecursive query from the cache */
static int
answer_norec_from_cache(struct worker* worker, struct query_info* qinfo,
	uint16_t id, uint16_t flags, struct comm_reply* repinfo,
	struct edns_data* edns)
{
	/* for a nonrecursive query return either:
	 * 	o an error (servfail; we try to avoid this)
	 * 	o a delegation (closest we have; this routine tries that)
	 * 	o the answer (checked by answer_from_cache)
	 *
	 * So, grab a delegation from the rrset cache.
	 * Then check if it needs validation, if so, this routine fails,
	 * so that iterator can prime and validator can verify rrsets.
	 */
	uint16_t udpsize = edns->udp_size;
	int secure = 0;
	time_t timenow = *worker->env.now;
	int has_cd_bit = (flags&BIT_CD);
	int must_validate = (!has_cd_bit || worker->env.cfg->ignore_cd)
		&& worker->env.need_to_validate;
	struct dns_msg *msg = NULL;
	struct delegpt *dp;

	dp = dns_cache_find_delegation(&worker->env, qinfo->qname,
		qinfo->qname_len, qinfo->qtype, qinfo->qclass,
		worker->scratchpad, &msg, timenow, 0, NULL, 0);
	if(!dp) { /* no delegation, need to reprime */
		return 0;
	}
	/* In case we have a local alias, copy it into the delegation message.
	 * Shallow copy should be fine, as we'll be done with msg in this
	 * function. */
	msg->qinfo.local_alias = qinfo->local_alias;
	if(must_validate) {
		switch(check_delegation_secure(msg->rep)) {
		case sec_status_unchecked:
			/* some rrsets have not been verified yet, go and
			 * let validator do that */
			return 0;
		case sec_status_bogus:
		case sec_status_secure_sentinel_fail:
			/* some rrsets are bogus, reply servfail */
			edns->edns_version = EDNS_ADVERTISED_VERSION;
			edns->udp_size = EDNS_ADVERTISED_SIZE;
			edns->ext_rcode = 0;
			edns->bits &= EDNS_DO;
			if(!inplace_cb_reply_servfail_call(&worker->env, qinfo, NULL,
				msg->rep, LDNS_RCODE_SERVFAIL, edns, repinfo, worker->scratchpad,
				worker->env.now_tv))
					return 0;
			/* Attach the cached EDE (RFC8914) */
			if(worker->env.cfg->ede &&
				msg->rep->reason_bogus != LDNS_EDE_NONE) {
				edns_opt_list_append_ede(&edns->opt_list_out,
					worker->scratchpad, msg->rep->reason_bogus,
					msg->rep->reason_bogus_str);
			}
			error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL,
				&msg->qinfo, id, flags, edns);
			if(worker->stats.extended) {
				worker->stats.ans_bogus++;
				worker->stats.ans_rcode[LDNS_RCODE_SERVFAIL]++;
			}
			return 1;
		case sec_status_secure:
			/* all rrsets are secure */
			/* remove non-secure rrsets from the add. section*/
			if(worker->env.cfg->val_clean_additional)
				deleg_remove_nonsecure_additional(msg->rep);
			secure = 1;
			break;
		case sec_status_indeterminate:
		case sec_status_insecure:
		default:
			/* not secure */
			secure = 0;
			break;
		}
	}
	/* return this delegation from the cache */
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;
	if(worker->env.cfg->disable_edns_do && (edns->bits & EDNS_DO))
		edns->edns_present = 0;
	if(!inplace_cb_reply_cache_call(&worker->env, qinfo, NULL, msg->rep,
		(int)(flags&LDNS_RCODE_MASK), edns, repinfo, worker->scratchpad,
		worker->env.now_tv))
			return 0;
	msg->rep->flags |= BIT_QR|BIT_RA;
	/* Attach the cached EDE (RFC8914) if CD bit is set and the answer is
	 * bogus. */
	if(worker->env.cfg->ede && has_cd_bit &&
		(check_delegation_secure(msg->rep) == sec_status_bogus ||
		check_delegation_secure(msg->rep) == sec_status_secure_sentinel_fail) &&
		msg->rep->reason_bogus != LDNS_EDE_NONE) {
		edns_opt_list_append_ede(&edns->opt_list_out,
			worker->scratchpad, msg->rep->reason_bogus,
			msg->rep->reason_bogus_str);
	}
	if(!reply_info_answer_encode(&msg->qinfo, msg->rep, id, flags,
		repinfo->c->buffer, 0, 1, worker->scratchpad,
		udpsize, edns, (int)(edns->bits & EDNS_DO), secure)) {
		if(!inplace_cb_reply_servfail_call(&worker->env, qinfo, NULL, NULL,
			LDNS_RCODE_SERVFAIL, edns, repinfo, worker->scratchpad,
			worker->env.now_tv))
				edns->opt_list_inplace_cb_out = NULL;
		error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL,
			&msg->qinfo, id, flags, edns);
	}
	if(worker->stats.extended) {
		if(secure) worker->stats.ans_secure++;
		server_stats_insrcode(&worker->stats, repinfo->c->buffer);
	}
	return 1;
}

/** Apply, if applicable, a response IP action to a cached answer.
 * If the answer is rewritten as a result of an action, '*encode_repp' will
 * point to the reply info containing the modified answer.  '*encode_repp' will
 * be intact otherwise.
 * It returns 1 on success, 0 otherwise. */
static int
apply_respip_action(struct worker* worker, const struct query_info* qinfo,
	struct respip_client_info* cinfo, struct reply_info* rep,
	struct sockaddr_storage* addr, socklen_t addrlen,
	struct ub_packed_rrset_key** alias_rrset,
	struct reply_info** encode_repp, struct auth_zones* az)
{
	struct respip_action_info actinfo = {0, 0, 0, 0, NULL, 0, NULL};
	actinfo.action = respip_none;

	if(qinfo->qtype != LDNS_RR_TYPE_A &&
		qinfo->qtype != LDNS_RR_TYPE_AAAA &&
		qinfo->qtype != LDNS_RR_TYPE_ANY)
		return 1;

	if(!respip_rewrite_reply(qinfo, cinfo, rep, encode_repp, &actinfo,
		alias_rrset, 0, worker->scratchpad, az, NULL,
		worker->env.views, worker->env.respip_set))
		return 0;

	/* xxx_deny actions mean dropping the reply, unless the original reply
	 * was redirected to response-ip data. */
	if(actinfo.action == respip_always_deny ||
		((actinfo.action == respip_deny ||
		actinfo.action == respip_inform_deny) &&
		*encode_repp == rep))
		*encode_repp = NULL;

	/* If address info is returned, it means the action should be an
	 * 'inform' variant and the information should be logged. */
	if(actinfo.addrinfo) {
		respip_inform_print(&actinfo, qinfo->qname,
			qinfo->qtype, qinfo->qclass, qinfo->local_alias,
			addr, addrlen);

		if(worker->stats.extended && actinfo.rpz_used) {
			if(actinfo.rpz_disabled)
				worker->stats.rpz_action[RPZ_DISABLED_ACTION]++;
			if(actinfo.rpz_cname_override)
				worker->stats.rpz_action[RPZ_CNAME_OVERRIDE_ACTION]++;
			else
				worker->stats.rpz_action[
					respip_action_to_rpz_action(actinfo.action)]++;
		}
	}

	return 1;
}

/** answer query from the cache.
 * Normally, the answer message will be built in repinfo->c->buffer; if the
 * answer is supposed to be suppressed or the answer is supposed to be an
 * incomplete CNAME chain, the buffer is explicitly cleared to signal the
 * caller as such.  In the latter case *partial_rep will point to the incomplete
 * reply, and this function is (possibly) supposed to be called again with that
 * *partial_rep value to complete the chain.  In addition, if the query should
 * be completely dropped, '*need_drop' will be set to 1. */
static int
answer_from_cache(struct worker* worker, struct query_info* qinfo,
	struct respip_client_info* cinfo, int* need_drop, int* is_expired_answer,
	int* is_secure_answer, struct ub_packed_rrset_key** alias_rrset,
	struct reply_info** partial_repp,
	struct reply_info* rep, uint16_t id, uint16_t flags,
	struct comm_reply* repinfo, struct edns_data* edns)
{
	time_t timenow = *worker->env.now;
	uint16_t udpsize = edns->udp_size;
	struct reply_info* encode_rep = rep;
	struct reply_info* partial_rep = *partial_repp;
	int has_cd_bit = (flags&BIT_CD);
	int must_validate = (!has_cd_bit || worker->env.cfg->ignore_cd)
		&& worker->env.need_to_validate;
	*partial_repp = NULL;  /* avoid accidental further pass */

	/* Check TTL */
	if(rep->ttl < timenow) {
		/* Check if we need to serve expired now */
		if(worker->env.cfg->serve_expired &&
			/* if serve-expired-client-timeout is set, serve
			 * an expired record without attempting recursion
			 * if the serve_expired_norec_ttl is set for the record
			 * as we know that recursion is currently failing. */
			(!worker->env.cfg->serve_expired_client_timeout ||
			 timenow < rep->serve_expired_norec_ttl)
#ifdef USE_CACHEDB
			&& !(worker->env.cachedb_enabled &&
			  worker->env.cfg->cachedb_check_when_serve_expired)
#endif
			) {
				if(!reply_info_can_answer_expired(rep, timenow))
					return 0;
				if(!rrset_array_lock(rep->ref, rep->rrset_count, 0))
					return 0;
				*is_expired_answer = 1;
		} else {
			/* the rrsets may have been updated in the meantime.
			 * we will refetch the message format from the
			 * authoritative server
			 */
			return 0;
		}
	} else {
		if(!rrset_array_lock(rep->ref, rep->rrset_count, timenow))
			return 0;
	}
	/* locked and ids and ttls are OK. */

	/* check CNAME chain (if any) */
	if(rep->an_numrrsets > 0 && (rep->rrsets[0]->rk.type ==
		htons(LDNS_RR_TYPE_CNAME) || rep->rrsets[0]->rk.type ==
		htons(LDNS_RR_TYPE_DNAME))) {
		if(!reply_check_cname_chain(qinfo, rep)) {
			/* cname chain invalid, redo iterator steps */
			verbose(VERB_ALGO, "Cache reply: cname chain broken");
			goto bail_out;
		}
	}
	/* check security status of the cached answer */
	if(must_validate && (rep->security == sec_status_bogus ||
		rep->security == sec_status_secure_sentinel_fail)) {
		/* BAD cached */
		edns->edns_version = EDNS_ADVERTISED_VERSION;
		edns->udp_size = EDNS_ADVERTISED_SIZE;
		edns->ext_rcode = 0;
		edns->bits &= EDNS_DO;
		if(worker->env.cfg->disable_edns_do && (edns->bits & EDNS_DO))
			edns->edns_present = 0;
		if(!inplace_cb_reply_servfail_call(&worker->env, qinfo, NULL, rep,
			LDNS_RCODE_SERVFAIL, edns, repinfo, worker->scratchpad,
			worker->env.now_tv))
			goto bail_out;
		/* Attach the cached EDE (RFC8914) */
		if(worker->env.cfg->ede && rep->reason_bogus != LDNS_EDE_NONE) {
			edns_opt_list_append_ede(&edns->opt_list_out,
					worker->scratchpad, rep->reason_bogus,
					rep->reason_bogus_str);
		}
		error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL,
			qinfo, id, flags, edns);
		rrset_array_unlock_touch(worker->env.rrset_cache,
			worker->scratchpad, rep->ref, rep->rrset_count);
		if(worker->stats.extended) {
			worker->stats.ans_bogus ++;
			worker->stats.ans_rcode[LDNS_RCODE_SERVFAIL] ++;
		}
		return 1;
	} else if(rep->security == sec_status_unchecked && must_validate) {
		verbose(VERB_ALGO, "Cache reply: unchecked entry needs "
			"validation");
		goto bail_out; /* need to validate cache entry first */
	} else if(rep->security == sec_status_secure) {
		if(reply_all_rrsets_secure(rep)) {
			*is_secure_answer = 1;
		} else {
			if(must_validate) {
				verbose(VERB_ALGO, "Cache reply: secure entry"
					" changed status");
				goto bail_out; /* rrset changed, re-verify */
			}
			*is_secure_answer = 0;
		}
	} else *is_secure_answer = 0;

	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;
	if(worker->env.cfg->disable_edns_do && (edns->bits & EDNS_DO))
		edns->edns_present = 0;
	*alias_rrset = NULL; /* avoid confusion if caller set it to non-NULL */
	if((worker->daemon->use_response_ip || worker->daemon->use_rpz) &&
		!partial_rep && !apply_respip_action(worker, qinfo, cinfo, rep,
		&repinfo->client_addr, repinfo->client_addrlen, alias_rrset,
		&encode_rep, worker->env.auth_zones)) {
		goto bail_out;
	} else if(partial_rep &&
		!respip_merge_cname(partial_rep, qinfo, rep, cinfo,
		must_validate, &encode_rep, worker->scratchpad,
		worker->env.auth_zones, worker->env.views,
		worker->env.respip_set)) {
		goto bail_out;
	}
	if(encode_rep != rep) {
		/* if rewritten, it can't be considered "secure" */
		*is_secure_answer = 0;
	}
	if(!encode_rep || *alias_rrset) {
		if(!encode_rep)
			*need_drop = 1;
		else {
			/* If a partial CNAME chain is found, we first need to
			 * make a copy of the reply in the scratchpad so we
			 * can release the locks and lookup the cache again. */
			*partial_repp = reply_info_copy(encode_rep, NULL,
				worker->scratchpad);
			if(!*partial_repp)
				goto bail_out;
		}
	} else {
		if(*is_expired_answer == 1 &&
			worker->env.cfg->ede_serve_expired && worker->env.cfg->ede) {
			EDNS_OPT_LIST_APPEND_EDE(&edns->opt_list_out,
				worker->scratchpad, LDNS_EDE_STALE_ANSWER, "");
		}
		/* Attach the cached EDE (RFC8914) if CD bit is set and the
		 * answer is bogus. */
		if(*is_secure_answer == 0 &&
			worker->env.cfg->ede && has_cd_bit &&
			encode_rep->reason_bogus != LDNS_EDE_NONE) {
			edns_opt_list_append_ede(&edns->opt_list_out,
				worker->scratchpad, encode_rep->reason_bogus,
				encode_rep->reason_bogus_str);
		}
		if(!inplace_cb_reply_cache_call(&worker->env, qinfo, NULL, encode_rep,
			(int)(flags&LDNS_RCODE_MASK), edns, repinfo, worker->scratchpad,
			worker->env.now_tv))
			goto bail_out;
		if(!reply_info_answer_encode(qinfo, encode_rep, id, flags,
			repinfo->c->buffer, timenow, 1, worker->scratchpad,
			udpsize, edns, (int)(edns->bits & EDNS_DO),
			*is_secure_answer)) {
			if(!inplace_cb_reply_servfail_call(&worker->env, qinfo,
				NULL, NULL, LDNS_RCODE_SERVFAIL, edns, repinfo,
				worker->scratchpad, worker->env.now_tv))
					edns->opt_list_inplace_cb_out = NULL;
			error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL,
				qinfo, id, flags, edns);
		}
	}
	/* cannot send the reply right now, because blocking network syscall
	 * is bad while holding locks. */
	rrset_array_unlock_touch(worker->env.rrset_cache, worker->scratchpad,
		rep->ref, rep->rrset_count);
	/* go and return this buffer to the client */
	return 1;

bail_out:
	rrset_array_unlock_touch(worker->env.rrset_cache,
		worker->scratchpad, rep->ref, rep->rrset_count);
	return 0;
}

/** Reply to client and perform prefetch to keep cache up to date. */
static void
reply_and_prefetch(struct worker* worker, struct query_info* qinfo,
	uint16_t flags, struct comm_reply* repinfo, time_t leeway, int noreply,
	int rpz_passthru, struct edns_option* opt_list)
{
	(void)opt_list;
	/* first send answer to client to keep its latency
	 * as small as a cachereply */
	if(!noreply) {
		if(repinfo->c->tcp_req_info) {
			sldns_buffer_copy(
				repinfo->c->tcp_req_info->spool_buffer,
				repinfo->c->buffer);
		}
		comm_point_send_reply(repinfo);
	}
	server_stats_prefetch(&worker->stats, worker);
#ifdef CLIENT_SUBNET
	/* Check if the subnet module is enabled. In that case pass over the
	 * comm_reply information for ECS generation later. The mesh states are
	 * unique when subnet is enabled. */
	if(modstack_find(&worker->env.mesh->mods, "subnetcache") != -1
		&& worker->env.unique_mesh) {
		mesh_new_prefetch(worker->env.mesh, qinfo, flags, leeway +
			PREFETCH_EXPIRY_ADD, rpz_passthru,
			&repinfo->client_addr, opt_list);
		return;
	}
#endif
	/* create the prefetch in the mesh as a normal lookup without
	 * client addrs waiting, which has the cache blacklisted (to bypass
	 * the cache and go to the network for the data). */
	/* this (potentially) runs the mesh for the new query */
	mesh_new_prefetch(worker->env.mesh, qinfo, flags, leeway +
		PREFETCH_EXPIRY_ADD, rpz_passthru, NULL, NULL);
}

/**
 * Fill CH class answer into buffer. Keeps query.
 * @param pkt: buffer
 * @param str: string to put into text record (<255).
 * 	array of strings, every string becomes a text record.
 * @param num: number of strings in array.
 * @param edns: edns reply information.
 * @param worker: worker with scratch region.
 * @param repinfo: reply information for a communication point.
 */
static void
chaos_replystr(sldns_buffer* pkt, char** str, int num, struct edns_data* edns,
	struct worker* worker, struct comm_reply* repinfo)
{
	int i;
	unsigned int rd = LDNS_RD_WIRE(sldns_buffer_begin(pkt));
	unsigned int cd = LDNS_CD_WIRE(sldns_buffer_begin(pkt));
	size_t udpsize = edns->udp_size;
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->bits &= EDNS_DO;
	if(!inplace_cb_reply_local_call(&worker->env, NULL, NULL, NULL,
		LDNS_RCODE_NOERROR, edns, repinfo, worker->scratchpad,
		worker->env.now_tv))
			edns->opt_list_inplace_cb_out = NULL;
	sldns_buffer_clear(pkt);
	sldns_buffer_skip(pkt, (ssize_t)sizeof(uint16_t)); /* skip id */
	sldns_buffer_write_u16(pkt, (uint16_t)(BIT_QR|BIT_RA));
	if(rd) LDNS_RD_SET(sldns_buffer_begin(pkt));
	if(cd) LDNS_CD_SET(sldns_buffer_begin(pkt));
	sldns_buffer_write_u16(pkt, 1); /* qdcount */
	sldns_buffer_write_u16(pkt, (uint16_t)num); /* ancount */
	sldns_buffer_write_u16(pkt, 0); /* nscount */
	sldns_buffer_write_u16(pkt, 0); /* arcount */
	(void)query_dname_len(pkt); /* skip qname */
	sldns_buffer_skip(pkt, (ssize_t)sizeof(uint16_t)); /* skip qtype */
	sldns_buffer_skip(pkt, (ssize_t)sizeof(uint16_t)); /* skip qclass */
	for(i=0; i<num; i++) {
		size_t len = strlen(str[i]);
		if(len>255) len=255; /* cap size of TXT record */
		if(sldns_buffer_position(pkt)+2+2+2+4+2+1+len+
			calc_edns_field_size(edns) > udpsize) {
			sldns_buffer_write_u16_at(pkt, 6, i); /* ANCOUNT */
			LDNS_TC_SET(sldns_buffer_begin(pkt));
			break;
		}
		sldns_buffer_write_u16(pkt, 0xc00c); /* compr ptr to query */
		sldns_buffer_write_u16(pkt, LDNS_RR_TYPE_TXT);
		sldns_buffer_write_u16(pkt, LDNS_RR_CLASS_CH);
		sldns_buffer_write_u32(pkt, 0); /* TTL */
		sldns_buffer_write_u16(pkt, sizeof(uint8_t) + len);
		sldns_buffer_write_u8(pkt, len);
		sldns_buffer_write(pkt, str[i], len);
	}
	sldns_buffer_flip(pkt);
	if(sldns_buffer_capacity(pkt) >=
		sldns_buffer_limit(pkt)+calc_edns_field_size(edns))
		attach_edns_record(pkt, edns);
}

/** Reply with one string */
static void
chaos_replyonestr(sldns_buffer* pkt, const char* str, struct edns_data* edns,
	struct worker* worker, struct comm_reply* repinfo)
{
	chaos_replystr(pkt, (char**)&str, 1, edns, worker, repinfo);
}

/**
 * Create CH class trustanchor answer.
 * @param pkt: buffer
 * @param edns: edns reply information.
 * @param w: worker with scratch region.
 * @param repinfo: reply information for a communication point.
 */
static void
chaos_trustanchor(sldns_buffer* pkt, struct edns_data* edns, struct worker* w,
	struct comm_reply* repinfo)
{
#define TA_RESPONSE_MAX_TXT 16 /* max number of TXT records */
#define TA_RESPONSE_MAX_TAGS 32 /* max number of tags printed per zone */
	char* str_array[TA_RESPONSE_MAX_TXT];
	uint16_t tags[TA_RESPONSE_MAX_TAGS];
	int num = 0;
	struct trust_anchor* ta;

	if(!w->env.need_to_validate) {
		/* no validator module, reply no trustanchors */
		chaos_replystr(pkt, NULL, 0, edns, w, repinfo);
		return;
	}

	/* fill the string with contents */
	lock_basic_lock(&w->env.anchors->lock);
	RBTREE_FOR(ta, struct trust_anchor*, w->env.anchors->tree) {
		char* str;
		size_t i, numtag, str_len = 255;
		if(num == TA_RESPONSE_MAX_TXT) continue;
		str = (char*)regional_alloc(w->scratchpad, str_len);
		if(!str) continue;
		lock_basic_lock(&ta->lock);
		numtag = anchor_list_keytags(ta, tags, TA_RESPONSE_MAX_TAGS);
		if(numtag == 0) {
			/* empty, insecure point */
			lock_basic_unlock(&ta->lock);
			continue;
		}
		str_array[num] = str;
		num++;

		/* spool name of anchor */
		(void)sldns_wire2str_dname_buf(ta->name, ta->namelen, str, str_len);
		str_len -= strlen(str); str += strlen(str);
		/* spool tags */
		for(i=0; i<numtag; i++) {
			snprintf(str, str_len, " %u", (unsigned)tags[i]);
			str_len -= strlen(str); str += strlen(str);
		}
		lock_basic_unlock(&ta->lock);
	}
	lock_basic_unlock(&w->env.anchors->lock);

	chaos_replystr(pkt, str_array, num, edns, w, repinfo);
	regional_free_all(w->scratchpad);
}

/**
 * Answer CH class queries.
 * @param w: worker
 * @param qinfo: query info. Pointer into packet buffer.
 * @param edns: edns info from query.
 * @param repinfo: reply information for a communication point.
 * @param pkt: packet buffer.
 * @return: true if a reply is to be sent.
 */
static int
answer_chaos(struct worker* w, struct query_info* qinfo,
	struct edns_data* edns, struct comm_reply* repinfo, sldns_buffer* pkt)
{
	struct config_file* cfg = w->env.cfg;
	if(qinfo->qtype != LDNS_RR_TYPE_ANY && qinfo->qtype != LDNS_RR_TYPE_TXT)
		return 0;
	if(query_dname_compare(qinfo->qname,
		(uint8_t*)"\002id\006server") == 0 ||
		query_dname_compare(qinfo->qname,
		(uint8_t*)"\010hostname\004bind") == 0)
	{
		if(cfg->hide_identity)
			return 0;
		if(cfg->identity==NULL || cfg->identity[0]==0) {
			char buf[MAXHOSTNAMELEN+1];
			if (gethostname(buf, MAXHOSTNAMELEN) == 0) {
				buf[MAXHOSTNAMELEN] = 0;
				chaos_replyonestr(pkt, buf, edns, w, repinfo);
			} else 	{
				log_err("gethostname: %s", strerror(errno));
				chaos_replyonestr(pkt, "no hostname", edns, w, repinfo);
			}
		}
		else 	chaos_replyonestr(pkt, cfg->identity, edns, w, repinfo);
		return 1;
	}
	if(query_dname_compare(qinfo->qname,
		(uint8_t*)"\007version\006server") == 0 ||
		query_dname_compare(qinfo->qname,
		(uint8_t*)"\007version\004bind") == 0)
	{
		if(cfg->hide_version)
			return 0;
		if(cfg->version==NULL || cfg->version[0]==0)
			chaos_replyonestr(pkt, PACKAGE_STRING, edns, w, repinfo);
		else 	chaos_replyonestr(pkt, cfg->version, edns, w, repinfo);
		return 1;
	}
	if(query_dname_compare(qinfo->qname,
		(uint8_t*)"\013trustanchor\007unbound") == 0)
	{
		if(cfg->hide_trustanchor)
			return 0;
		chaos_trustanchor(pkt, edns, w, repinfo);
		return 1;
	}

	return 0;
}

/**
 * Answer notify queries.  These are notifies for authoritative zones,
 * the reply is an ack that the notify has been received.  We need to check
 * access permission here.
 * @param w: worker
 * @param qinfo: query info. Pointer into packet buffer.
 * @param edns: edns info from query.
 * @param addr: client address.
 * @param addrlen: client address length.
 * @param pkt: packet buffer.
 */
static void
answer_notify(struct worker* w, struct query_info* qinfo,
	struct edns_data* edns, sldns_buffer* pkt,
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	int refused = 0;
	int rcode = LDNS_RCODE_NOERROR;
	uint32_t serial = 0;
	int has_serial;
	if(!w->env.auth_zones) return;
	has_serial = auth_zone_parse_notify_serial(pkt, &serial);
	if(auth_zones_notify(w->env.auth_zones, &w->env, qinfo->qname,
		qinfo->qname_len, qinfo->qclass, addr,
		addrlen, has_serial, serial, &refused)) {
		rcode = LDNS_RCODE_NOERROR;
	} else {
		if(refused)
			rcode = LDNS_RCODE_REFUSED;
		else	rcode = LDNS_RCODE_SERVFAIL;
	}

	if(verbosity >= VERB_DETAIL) {
		char buf[380];
		char zname[LDNS_MAX_DOMAINLEN];
		char sr[25];
		dname_str(qinfo->qname, zname);
		sr[0]=0;
		if(has_serial)
			snprintf(sr, sizeof(sr), "serial %u ",
				(unsigned)serial);
		if(rcode == LDNS_RCODE_REFUSED)
			snprintf(buf, sizeof(buf),
				"refused NOTIFY %sfor %s from", sr, zname);
		else if(rcode == LDNS_RCODE_SERVFAIL)
			snprintf(buf, sizeof(buf),
				"servfail for NOTIFY %sfor %s from", sr, zname);
		else	snprintf(buf, sizeof(buf),
				"received NOTIFY %sfor %s from", sr, zname);
		log_addr(VERB_DETAIL, buf, addr, addrlen);
	}
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;
	error_encode(pkt, rcode, qinfo,
		*(uint16_t*)(void *)sldns_buffer_begin(pkt),
		sldns_buffer_read_u16_at(pkt, 2), edns);
	LDNS_OPCODE_SET(sldns_buffer_begin(pkt), LDNS_PACKET_NOTIFY);
}

static int
deny_refuse(struct comm_point* c, enum acl_access acl,
	enum acl_access deny, enum acl_access refuse,
	struct worker* worker, struct comm_reply* repinfo,
	struct acl_addr* acladdr, int ede,
	struct check_request_result* check_result)
{
	if(acl == deny) {
		if(verbosity >= VERB_ALGO) {
			log_acl_action("dropped", &repinfo->client_addr,
				repinfo->client_addrlen, acl, acladdr);
			log_buf(VERB_ALGO, "dropped", c->buffer);
		}
		comm_point_drop_reply(repinfo);
		if(worker->stats.extended)
			worker->stats.unwanted_queries++;
		return 0;
	} else if(acl == refuse) {
		size_t opt_rr_mark;

		if(verbosity >= VERB_ALGO) {
			log_acl_action("refused", &repinfo->client_addr,
				repinfo->client_addrlen, acl, acladdr);
			log_buf(VERB_ALGO, "refuse", c->buffer);
		}

		if(worker->stats.extended)
			worker->stats.unwanted_queries++;
		worker_check_request(c->buffer, worker, check_result);
		if(check_result->value != 0) {
			if(check_result->value != -1) {
				LDNS_QR_SET(sldns_buffer_begin(c->buffer));
				LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
					check_result->value);
				return 1;
			}
			comm_point_drop_reply(repinfo);
			return 0;
		}
		/* worker_check_request() above guarantees that the buffer contains at
		 * least a header and that qdcount == 1
		 */
		log_assert(sldns_buffer_limit(c->buffer) >= LDNS_HEADER_SIZE
			&& LDNS_QDCOUNT(sldns_buffer_begin(c->buffer)) == 1);

		sldns_buffer_set_position(c->buffer, LDNS_HEADER_SIZE); /* skip header */

		/* check additional section is present and that we respond with EDEs */
		if(LDNS_ARCOUNT(sldns_buffer_begin(c->buffer)) != 1
			|| !ede) {
			LDNS_QDCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ANCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_NSCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_QR_SET(sldns_buffer_begin(c->buffer));
			LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
				LDNS_RCODE_REFUSED);
			sldns_buffer_set_position(c->buffer, LDNS_HEADER_SIZE);
			sldns_buffer_flip(c->buffer);
			return 1;
		}

		if (!query_dname_len(c->buffer)) {
			LDNS_QDCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ANCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_NSCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_QR_SET(sldns_buffer_begin(c->buffer));
			LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
				LDNS_RCODE_FORMERR);
			sldns_buffer_set_position(c->buffer, LDNS_HEADER_SIZE);
			sldns_buffer_flip(c->buffer);
			return 1;
		}
		/* space available for query type and class? */
		if (sldns_buffer_remaining(c->buffer) < 2 * sizeof(uint16_t)) {
                        LDNS_QR_SET(sldns_buffer_begin(c->buffer));
                        LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
				 LDNS_RCODE_FORMERR);
			LDNS_QDCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ANCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_NSCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			sldns_buffer_set_position(c->buffer, LDNS_HEADER_SIZE);
                        sldns_buffer_flip(c->buffer);
			return 1;
		}
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
			LDNS_RCODE_REFUSED);

		sldns_buffer_skip(c->buffer, (ssize_t)sizeof(uint16_t)); /* skip qtype */

		sldns_buffer_skip(c->buffer, (ssize_t)sizeof(uint16_t)); /* skip qclass */

		/* The OPT RR to be returned should come directly after
		 * the query, so mark this spot.
		 */
		opt_rr_mark = sldns_buffer_position(c->buffer);

		/* Skip through the RR records */
		if(LDNS_ANCOUNT(sldns_buffer_begin(c->buffer)) != 0 ||
			LDNS_NSCOUNT(sldns_buffer_begin(c->buffer)) != 0) {
			if(!skip_pkt_rrs(c->buffer,
				((int)LDNS_ANCOUNT(sldns_buffer_begin(c->buffer)))+
				((int)LDNS_NSCOUNT(sldns_buffer_begin(c->buffer))))) {
				LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
					LDNS_RCODE_FORMERR);
				LDNS_ANCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
				LDNS_NSCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
				LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
				sldns_buffer_set_position(c->buffer, opt_rr_mark);
				sldns_buffer_flip(c->buffer);
				return 1;
			}
		}
		/* Do we have a valid OPT RR here? If not return REFUSED (could be a valid TSIG or something so no FORMERR) */
		/* domain name must be the root of length 1. */
		if(sldns_buffer_remaining(c->buffer) < 1 || *sldns_buffer_current(c->buffer) != 0) {
			LDNS_ANCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_NSCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			sldns_buffer_set_position(c->buffer, opt_rr_mark);
			sldns_buffer_flip(c->buffer);
			return 1;
		} else {
			sldns_buffer_skip(c->buffer, 1); /* skip root label */
		}
		if(sldns_buffer_remaining(c->buffer) < 2 ||
			sldns_buffer_read_u16(c->buffer) != LDNS_RR_TYPE_OPT) {
			LDNS_ANCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_NSCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			sldns_buffer_set_position(c->buffer, opt_rr_mark);
			sldns_buffer_flip(c->buffer);
			return 1;
		}
		/* Write OPT RR directly after the query,
		 * so without the (possibly skipped) Answer and NS RRs
		 */
		LDNS_ANCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
		LDNS_NSCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
		sldns_buffer_clear(c->buffer); /* reset write limit */
		sldns_buffer_set_position(c->buffer, opt_rr_mark);

		/* Check if OPT record can be written
		 * 17 == root label (1) + RR type (2) + UDP Size (2)
		 *     + Fields (4) + rdata len (2) + EDE Option code (2)
		 *     + EDE Option length (2) + EDE info-code (2)
		 */
		if (sldns_buffer_available(c->buffer, 17) == 0) {
			LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 0);
			sldns_buffer_flip(c->buffer);
			return 1;
		}

		LDNS_ARCOUNT_SET(sldns_buffer_begin(c->buffer), 1);

		/* root label */
		sldns_buffer_write_u8(c->buffer, 0);
		sldns_buffer_write_u16(c->buffer, LDNS_RR_TYPE_OPT);
		sldns_buffer_write_u16(c->buffer, EDNS_ADVERTISED_SIZE);

		/* write OPT Record TTL Field */
		sldns_buffer_write_u32(c->buffer, 0);

		/* write rdata len: EDE option + length + info-code */
		sldns_buffer_write_u16(c->buffer, 6);

		/* write OPTIONS; add EDE option code */
		sldns_buffer_write_u16(c->buffer, LDNS_EDNS_EDE);

		/* write single EDE option length (for just 1 info-code) */
		sldns_buffer_write_u16(c->buffer, 2);

		/* write single EDE info-code */
		sldns_buffer_write_u16(c->buffer, LDNS_EDE_PROHIBITED);

		sldns_buffer_flip(c->buffer);

		verbose(VERB_ALGO, "attached EDE code: %d", LDNS_EDE_PROHIBITED);

		return 1;

	}

	return -1;
}

static int
deny_refuse_all(struct comm_point* c, enum acl_access* acl,
	struct worker* worker, struct comm_reply* repinfo,
	struct acl_addr** acladdr, int ede, int check_proxy,
	struct check_request_result* check_result)
{
	if(check_proxy) {
		*acladdr = acl_addr_lookup(worker->daemon->acl,
			&repinfo->remote_addr, repinfo->remote_addrlen);
	} else {
		*acladdr = acl_addr_lookup(worker->daemon->acl,
			&repinfo->client_addr, repinfo->client_addrlen);
	}
	/* If there is no ACL based on client IP use the interface ACL. */
	if(!(*acladdr) && c->socket) {
		*acladdr = c->socket->acl;
	}
	*acl = acl_get_control(*acladdr);
	return deny_refuse(c, *acl, acl_deny, acl_refuse, worker, repinfo,
		*acladdr, ede, check_result);
}

static int
deny_refuse_non_local(struct comm_point* c, enum acl_access acl,
	struct worker* worker, struct comm_reply* repinfo,
	struct acl_addr* acladdr, int ede,
	struct check_request_result* check_result)
{
	return deny_refuse(c, acl, acl_deny_non_local, acl_refuse_non_local,
		worker, repinfo, acladdr, ede, check_result);
}

/* Check if the query is blocked by source IP rate limiting.
 * Returns 1 if it passes the check, 0 otherwise. */
static int
check_ip_ratelimit(struct worker* worker, struct sockaddr_storage* addr,
	socklen_t addrlen, int has_cookie, sldns_buffer* pkt)
{
	if(!infra_ip_ratelimit_inc(worker->env.infra_cache, addr, addrlen,
			*worker->env.now, has_cookie,
			worker->env.cfg->ip_ratelimit_backoff, pkt)) {
		/* See if we can pass through with slip factor */
		if(!has_cookie && worker->env.cfg->ip_ratelimit_factor != 0 &&
			ub_random_max(worker->env.rnd,
			worker->env.cfg->ip_ratelimit_factor) == 0) {
			char addrbuf[128];
			addr_to_str(addr, addrlen, addrbuf, sizeof(addrbuf));
			verbose(VERB_QUERY, "ip_ratelimit allowed through for "
				"ip address %s because of slip in "
				"ip_ratelimit_factor", addrbuf);
			return 1;
		}
		return 0;
	}
	return 1;
}

int
worker_handle_request(struct comm_point* c, void* arg, int error,
	struct comm_reply* repinfo)
{
	struct worker* worker = (struct worker*)arg;
	int ret;
	hashvalue_type h;
	struct lruhash_entry* e;
	struct query_info qinfo;
	struct edns_data edns;
	struct edns_option* original_edns_list = NULL;
	enum acl_access acl;
	struct acl_addr* acladdr;
	int pre_edns_ip_ratelimit = 1;
	int rc = 0;
	int need_drop = 0;
	int is_expired_answer = 0;
	int is_secure_answer = 0;
	int rpz_passthru = 0;
	long long wait_queue_time = 0;
	/* We might have to chase a CNAME chain internally, in which case
	 * we'll have up to two replies and combine them to build a complete
	 * answer.  These variables control this case. */
	struct ub_packed_rrset_key* alias_rrset = NULL;
	struct reply_info* partial_rep = NULL;
	struct query_info* lookup_qinfo = &qinfo;
	struct query_info qinfo_tmp; /* placeholder for lookup_qinfo */
	struct respip_client_info* cinfo = NULL, cinfo_tmp;
	struct timeval wait_time;
	struct check_request_result check_result = {0,0};
	memset(&qinfo, 0, sizeof(qinfo));

	if((error != NETEVENT_NOERROR && error != NETEVENT_DONE)|| !repinfo) {
		/* some bad tcp query DNS formats give these error calls */
		verbose(VERB_ALGO, "handle request called with err=%d", error);
		return 0;
	}

	if (worker->env.cfg->sock_queue_timeout && timeval_isset(&c->recv_tv)) {
		timeval_subtract(&wait_time, worker->env.now_tv, &c->recv_tv);
		wait_queue_time = wait_time.tv_sec * 1000000 +  wait_time.tv_usec;
		if (worker->stats.max_query_time_us < wait_queue_time)
			worker->stats.max_query_time_us = wait_queue_time;
		if(wait_queue_time >
			(long long)(worker->env.cfg->sock_queue_timeout * 1000000)) {
			/* count and drop queries that were sitting in the socket queue too long */
			worker->stats.num_queries_timed_out++;
			return 0;
		}
	}

#ifdef USE_DNSCRYPT
	repinfo->max_udp_size = worker->daemon->cfg->max_udp_size;
	if(!dnsc_handle_curved_request(worker->daemon->dnscenv, repinfo)) {
		worker->stats.num_query_dnscrypt_crypted_malformed++;
		return 0;
	}
	if(c->dnscrypt && !repinfo->is_dnscrypted) {
		char buf[LDNS_MAX_DOMAINLEN];
		/* Check if this is unencrypted and asking for certs */
		worker_check_request(c->buffer, worker, &check_result);
		if(check_result.value != 0) {
			verbose(VERB_ALGO,
				"dnscrypt: worker check request: bad query.");
			log_addr(VERB_CLIENT,"from",&repinfo->client_addr,
				repinfo->client_addrlen);
			comm_point_drop_reply(repinfo);
			return 0;
		}
		if(!query_info_parse(&qinfo, c->buffer)) {
			verbose(VERB_ALGO,
				"dnscrypt: worker parse request: formerror.");
			log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
				repinfo->client_addrlen);
			comm_point_drop_reply(repinfo);
			return 0;
		}
		dname_str(qinfo.qname, buf);
		if(!(qinfo.qtype == LDNS_RR_TYPE_TXT &&
			strcasecmp(buf,
			worker->daemon->dnscenv->provider_name) == 0)) {
			verbose(VERB_ALGO,
				"dnscrypt: not TXT \"%s\". Received: %s \"%s\"",
				worker->daemon->dnscenv->provider_name,
				sldns_rr_descript(qinfo.qtype)->_name,
				buf);
			comm_point_drop_reply(repinfo);
			worker->stats.num_query_dnscrypt_cleartext++;
			return 0;
		}
		worker->stats.num_query_dnscrypt_cert++;
		sldns_buffer_rewind(c->buffer);
	} else if(c->dnscrypt && repinfo->is_dnscrypted) {
		worker->stats.num_query_dnscrypt_crypted++;
	}
#endif
#ifdef USE_DNSTAP
	/*
	 * sending src (client)/dst (local service) addresses over DNSTAP from incoming request handler
	 */
	if(worker->dtenv.log_client_query_messages) {
		log_addr(VERB_ALGO, "request from client", &repinfo->client_addr, repinfo->client_addrlen);
		log_addr(VERB_ALGO, "to local addr", (void*)repinfo->c->socket->addr, repinfo->c->socket->addrlen);
		dt_msg_send_client_query(&worker->dtenv, &repinfo->client_addr, (void*)repinfo->c->socket->addr, c->type, c->ssl, c->buffer,
		((worker->env.cfg->sock_queue_timeout && timeval_isset(&c->recv_tv))?&c->recv_tv:NULL));
	}
#endif
	/* Check deny/refuse ACLs */
	if(repinfo->is_proxied) {
		if((ret=deny_refuse_all(c, &acl, worker, repinfo, &acladdr,
			worker->env.cfg->ede, 1, &check_result)) != -1) {
			if(ret == 1)
				goto send_reply;
			return ret;
		}
	}
	if((ret=deny_refuse_all(c, &acl, worker, repinfo, &acladdr,
		worker->env.cfg->ede, 0, &check_result)) != -1) {
		if(ret == 1)
			goto send_reply;
		return ret;
	}

	worker_check_request(c->buffer, worker, &check_result);
	if(check_result.value != 0) {
		verbose(VERB_ALGO, "worker check request: bad query.");
		log_addr(VERB_CLIENT,"from",&repinfo->client_addr, repinfo->client_addrlen);
		if(check_result.value != -1) {
			LDNS_QR_SET(sldns_buffer_begin(c->buffer));
			LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
				check_result.value);
			return 1;
		}
		comm_point_drop_reply(repinfo);
		return 0;
	}

	worker->stats.num_queries++;
	pre_edns_ip_ratelimit = !worker->env.cfg->do_answer_cookie
		|| sldns_buffer_limit(c->buffer) < LDNS_HEADER_SIZE
		|| LDNS_ARCOUNT(sldns_buffer_begin(c->buffer)) == 0;

	/* If the IP rate limiting check needs extra EDNS information (e.g.,
	 * DNS Cookies) postpone the check until after EDNS is parsed. */
	if(pre_edns_ip_ratelimit) {
		/* NOTE: we always check the repinfo->client_address.
		 *       IP ratelimiting is implicitly disabled for proxies. */
		if(!check_ip_ratelimit(worker, &repinfo->client_addr,
			repinfo->client_addrlen, 0, c->buffer)) {
			worker->stats.num_queries_ip_ratelimited++;
			comm_point_drop_reply(repinfo);
			return 0;
		}
	}

	if(!query_info_parse(&qinfo, c->buffer)) {
		verbose(VERB_ALGO, "worker parse request: formerror.");
		log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
			repinfo->client_addrlen);
		memset(&qinfo, 0, sizeof(qinfo)); /* zero qinfo.qname */
		if(worker_err_ratelimit(worker, LDNS_RCODE_FORMERR) == -1) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		sldns_buffer_rewind(c->buffer);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
			LDNS_RCODE_FORMERR);
		goto send_reply;
	}
	if(worker->env.cfg->log_queries) {
		char ip[128];
		addr_to_str(&repinfo->client_addr, repinfo->client_addrlen, ip, sizeof(ip));
		log_query_in(ip, qinfo.qname, qinfo.qtype, qinfo.qclass);
	}
	if(qinfo.qtype == LDNS_RR_TYPE_AXFR ||
		qinfo.qtype == LDNS_RR_TYPE_IXFR) {
		verbose(VERB_ALGO, "worker request: refused zone transfer.");
		log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
			repinfo->client_addrlen);
		sldns_buffer_rewind(c->buffer);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
			LDNS_RCODE_REFUSED);
		if(worker->stats.extended) {
			worker->stats.qtype[qinfo.qtype]++;
		}
		goto send_reply;
	}
	if(qinfo.qtype == LDNS_RR_TYPE_OPT ||
		qinfo.qtype == LDNS_RR_TYPE_TSIG ||
		qinfo.qtype == LDNS_RR_TYPE_TKEY ||
		qinfo.qtype == LDNS_RR_TYPE_MAILA ||
		qinfo.qtype == LDNS_RR_TYPE_MAILB ||
		(qinfo.qtype >= 128 && qinfo.qtype <= 248)) {
		verbose(VERB_ALGO, "worker request: formerror for meta-type.");
		log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
			repinfo->client_addrlen);
		if(worker_err_ratelimit(worker, LDNS_RCODE_FORMERR) == -1) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		sldns_buffer_rewind(c->buffer);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
			LDNS_RCODE_FORMERR);
		if(worker->stats.extended) {
			worker->stats.qtype[qinfo.qtype]++;
		}
		goto send_reply;
	}
	if((ret=parse_edns_from_query_pkt(
			c->buffer, &edns, worker->env.cfg, c, repinfo,
			(worker->env.now ? *worker->env.now : time(NULL)),
			worker->scratchpad,
			worker->daemon->cookie_secrets)) != 0) {
		struct edns_data reply_edns;
		verbose(VERB_ALGO, "worker parse edns: formerror.");
		log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
			repinfo->client_addrlen);
		memset(&reply_edns, 0, sizeof(reply_edns));
		reply_edns.edns_present = 1;
		error_encode(c->buffer, ret, &qinfo,
			*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
			sldns_buffer_read_u16_at(c->buffer, 2), &reply_edns);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}
	if(edns.edns_present) {
		if(edns.edns_version != 0) {
			edns.opt_list_in = NULL;
			edns.opt_list_out = NULL;
			edns.opt_list_inplace_cb_out = NULL;
			verbose(VERB_ALGO, "query with bad edns version.");
			log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
				repinfo->client_addrlen);
			extended_error_encode(c->buffer, EDNS_RCODE_BADVERS, &qinfo,
				*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
				sldns_buffer_read_u16_at(c->buffer, 2), 0, &edns);
			regional_free_all(worker->scratchpad);
			goto send_reply;
		}
		if(edns.udp_size < NORMAL_UDP_SIZE &&
		   worker->daemon->cfg->harden_short_bufsize) {
			verbose(VERB_QUERY, "worker request: EDNS bufsize %d ignored",
				(int)edns.udp_size);
			log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
				repinfo->client_addrlen);
			edns.udp_size = NORMAL_UDP_SIZE;
		}
	}

	/* Get stats for cookies */
	server_stats_downstream_cookie(&worker->stats, &edns);

	/* If the IP rate limiting check was postponed, check now. */
	if(!pre_edns_ip_ratelimit) {
		/* NOTE: we always check the repinfo->client_address.
		 *       IP ratelimiting is implicitly disabled for proxies. */
		if(!check_ip_ratelimit(worker, &repinfo->client_addr,
			repinfo->client_addrlen, edns.cookie_valid,
			c->buffer)) {
			worker->stats.num_queries_ip_ratelimited++;
			comm_point_drop_reply(repinfo);
			return 0;
		}
	}

	/* "if, else if" sequence below deals with downstream DNS Cookies */
	if(acl != acl_allow_cookie)
		; /* pass; No cookie downstream processing whatsoever */

	else if(edns.cookie_valid)
		; /* pass; Valid cookie is good! */

	else if(c->type != comm_udp)
		; /* pass; Stateful transport */

	else if(edns.cookie_present) {
		/* Cookie present, but not valid: Cookie was bad! */
		extended_error_encode(c->buffer,
			LDNS_EXT_RCODE_BADCOOKIE, &qinfo,
			*(uint16_t*)(void *)
			sldns_buffer_begin(c->buffer),
			sldns_buffer_read_u16_at(c->buffer, 2),
			0, &edns);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	} else {
		/* Cookie required, but no cookie present on UDP */
		verbose(VERB_ALGO, "worker request: "
			"need cookie or stateful transport");
		log_addr(VERB_ALGO, "from",&repinfo->remote_addr
		                          , repinfo->remote_addrlen);
		EDNS_OPT_LIST_APPEND_EDE(&edns.opt_list_out,
			worker->scratchpad, LDNS_EDE_OTHER,
			"DNS Cookie needed for UDP replies");
		error_encode(c->buffer,
			(LDNS_RCODE_REFUSED|BIT_TC), &qinfo,
			*(uint16_t*)(void *)
			sldns_buffer_begin(c->buffer),
			sldns_buffer_read_u16_at(c->buffer, 2),
			&edns);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}

	if(edns.udp_size > worker->daemon->cfg->max_udp_size &&
		c->type == comm_udp) {
		verbose(VERB_QUERY,
			"worker request: max UDP reply size modified"
			" (%d to max-udp-size)", (int)edns.udp_size);
		log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
			repinfo->client_addrlen);
		edns.udp_size = worker->daemon->cfg->max_udp_size;
	}
	if(edns.udp_size < LDNS_HEADER_SIZE) {
		verbose(VERB_ALGO, "worker request: edns is too small.");
		log_addr(VERB_CLIENT, "from", &repinfo->client_addr,
			repinfo->client_addrlen);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_TC_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer),
			LDNS_RCODE_SERVFAIL);
		sldns_buffer_set_position(c->buffer, LDNS_HEADER_SIZE);
		sldns_buffer_write_at(c->buffer, 4,
			(uint8_t*)"\0\0\0\0\0\0\0\0", 8);
		sldns_buffer_flip(c->buffer);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}
	if(worker->stats.extended)
		server_stats_insquery(&worker->stats, c, qinfo.qtype,
			qinfo.qclass, &edns, repinfo);
	if(c->type != comm_udp)
		edns.udp_size = 65535; /* max size for TCP replies */
	if(qinfo.qclass == LDNS_RR_CLASS_CH && answer_chaos(worker, &qinfo,
		&edns, repinfo, c->buffer)) {
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}
	if(LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) ==
		LDNS_PACKET_NOTIFY) {
		answer_notify(worker, &qinfo, &edns, c->buffer,
			&repinfo->client_addr, repinfo->client_addrlen);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}
	if(local_zones_answer(worker->daemon->local_zones, &worker->env, &qinfo,
		&edns, c->buffer, worker->scratchpad, repinfo, acladdr->taglist,
		acladdr->taglen, acladdr->tag_actions,
		acladdr->tag_actions_size, acladdr->tag_datas,
		acladdr->tag_datas_size, worker->daemon->cfg->tagname,
		worker->daemon->cfg->num_tags, acladdr->view)) {
		regional_free_all(worker->scratchpad);
		if(sldns_buffer_limit(c->buffer) == 0) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		goto send_reply;
	}
	if(worker->env.auth_zones &&
		rpz_callback_from_worker_request(worker->env.auth_zones,
		&worker->env, &qinfo, &edns, c->buffer, worker->scratchpad,
		repinfo, acladdr->taglist, acladdr->taglen, &worker->stats,
		&rpz_passthru)) {
		regional_free_all(worker->scratchpad);
		if(sldns_buffer_limit(c->buffer) == 0) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		goto send_reply;
	}
	if(worker->env.auth_zones &&
		auth_zones_answer(worker->env.auth_zones, &worker->env,
		&qinfo, &edns, repinfo, c->buffer, worker->scratchpad)) {
		regional_free_all(worker->scratchpad);
		if(sldns_buffer_limit(c->buffer) == 0) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		/* set RA for everyone that can have recursion (based on
		 * access control list) */
		if(LDNS_RD_WIRE(sldns_buffer_begin(c->buffer)) &&
		   acl != acl_deny_non_local && acl != acl_refuse_non_local)
			LDNS_RA_SET(sldns_buffer_begin(c->buffer));
		goto send_reply;
	}

	/* We've looked in our local zones. If the answer isn't there, we
	 * might need to bail out based on ACLs now. */
	if((ret=deny_refuse_non_local(c, acl, worker, repinfo, acladdr,
		worker->env.cfg->ede, &check_result)) != -1)
	{
		regional_free_all(worker->scratchpad);
		if(ret == 1)
			goto send_reply;
		return ret;
	}

	/* If this request does not have the recursion bit set, verify
	 * ACLs allow the recursion bit to be treated as set. */
	if(!(LDNS_RD_WIRE(sldns_buffer_begin(c->buffer))) &&
		acl == acl_allow_setrd ) {
		LDNS_RD_SET(sldns_buffer_begin(c->buffer));
	}

	/* If this request does not have the recursion bit set, verify
	 * ACLs allow the snooping. */
	if(!(LDNS_RD_WIRE(sldns_buffer_begin(c->buffer))) &&
		acl != acl_allow_snoop ) {
		if(worker->env.cfg->ede) {
			EDNS_OPT_LIST_APPEND_EDE(&edns.opt_list_out,
				worker->scratchpad, LDNS_EDE_NOT_AUTHORITATIVE, "");
		}
		error_encode(c->buffer, LDNS_RCODE_REFUSED, &qinfo,
			*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
			sldns_buffer_read_u16_at(c->buffer, 2), &edns);
		regional_free_all(worker->scratchpad);
		log_addr(VERB_ALGO, "refused nonrec (cache snoop) query from",
			&repinfo->client_addr, repinfo->client_addrlen);

		goto send_reply;
	}

	/* If we've found a local alias, replace the qname with the alias
	 * target before resolving it. */
	if(qinfo.local_alias) {
		struct ub_packed_rrset_key* rrset = qinfo.local_alias->rrset;
		struct packed_rrset_data* d = rrset->entry.data;

		/* Sanity check: our current implementation only supports
		 * a single CNAME RRset as a local alias. */
		if(qinfo.local_alias->next ||
			rrset->rk.type != htons(LDNS_RR_TYPE_CNAME) ||
			d->count != 1) {
			log_err("assumption failure: unexpected local alias");
			regional_free_all(worker->scratchpad);
			return 0; /* drop it */
		}
		qinfo.qname = d->rr_data[0] + 2;
		qinfo.qname_len = d->rr_len[0] - 2;
	}

	/* If we may apply IP-based actions to the answer, build the client
	 * information.  As this can be expensive, skip it if there is
	 * absolutely no possibility of it. */
	if((worker->daemon->use_response_ip || worker->daemon->use_rpz) &&
		(qinfo.qtype == LDNS_RR_TYPE_A ||
		qinfo.qtype == LDNS_RR_TYPE_AAAA ||
		qinfo.qtype == LDNS_RR_TYPE_ANY)) {
		cinfo_tmp.taglist = acladdr->taglist;
		cinfo_tmp.taglen = acladdr->taglen;
		cinfo_tmp.tag_actions = acladdr->tag_actions;
		cinfo_tmp.tag_actions_size = acladdr->tag_actions_size;
		cinfo_tmp.tag_datas = acladdr->tag_datas;
		cinfo_tmp.tag_datas_size = acladdr->tag_datas_size;
		cinfo_tmp.view = acladdr->view;
		cinfo_tmp.view_name = NULL;
		cinfo = &cinfo_tmp;
	}

	/* Keep the original edns list around. The pointer could change if there is
	 * a cached answer (through the inplace callback function there).
	 * No need to actually copy the contents as they shouldn't change.
	 * Used while prefetching and subnet is enabled. */
	original_edns_list = edns.opt_list_in;
lookup_cache:
	/* Lookup the cache.  In case we chase an intermediate CNAME chain
	 * this is a two-pass operation, and lookup_qinfo is different for
	 * each pass.  We should still pass the original qinfo to
	 * answer_from_cache(), however, since it's used to build the reply. */
	if(!edns_bypass_cache_stage(edns.opt_list_in, &worker->env)) {
		is_expired_answer = 0;
		is_secure_answer = 0;
		h = query_info_hash(lookup_qinfo, sldns_buffer_read_u16_at(c->buffer, 2));
		if((e=slabhash_lookup(worker->env.msg_cache, h, lookup_qinfo, 0))) {
			struct reply_info* rep = (struct reply_info*)e->data;
			/* answer from cache - we have acquired a readlock on it */
			if(answer_from_cache(worker, &qinfo, cinfo, &need_drop,
				&is_expired_answer, &is_secure_answer,
				&alias_rrset, &partial_rep, rep,
				*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
				sldns_buffer_read_u16_at(c->buffer, 2), repinfo,
				&edns)) {
				/* prefetch it if the prefetch TTL expired.
				 * Note that if there is more than one pass
				 * its qname must be that used for cache
				 * lookup. */
				if((worker->env.cfg->prefetch &&
					rep->prefetch_ttl <= *worker->env.now) ||
					(worker->env.cfg->serve_expired &&
					rep->ttl < *worker->env.now  &&
					!(*worker->env.now < rep->serve_expired_norec_ttl))) {
					time_t leeway = rep->ttl - *worker->env.now;
					if(rep->ttl < *worker->env.now)
						leeway = 0;
					lock_rw_unlock(&e->lock);

					reply_and_prefetch(worker, lookup_qinfo,
						sldns_buffer_read_u16_at(c->buffer, 2),
						repinfo, leeway,
						(partial_rep || need_drop),
						rpz_passthru,
						original_edns_list);
					if(!partial_rep) {
						rc = 0;
						regional_free_all(worker->scratchpad);
						goto send_reply_rc;
					}
				} else if(!partial_rep) {
					lock_rw_unlock(&e->lock);
					regional_free_all(worker->scratchpad);
					goto send_reply;
				} else {
					/* Note that we've already released the
					 * lock if we're here after prefetch. */
					lock_rw_unlock(&e->lock);
				}
				/* We've found a partial reply ending with an
				 * alias.  Replace the lookup qinfo for the
				 * alias target and lookup the cache again to
				 * (possibly) complete the reply.  As we're
				 * passing the "base" reply, there will be no
				 * more alias chasing. */
				memset(&qinfo_tmp, 0, sizeof(qinfo_tmp));
				get_cname_target(alias_rrset, &qinfo_tmp.qname,
					&qinfo_tmp.qname_len);
				if(!qinfo_tmp.qname) {
					log_err("unexpected: invalid answer alias");
					regional_free_all(worker->scratchpad);
					return 0; /* drop query */
				}
				qinfo_tmp.qtype = qinfo.qtype;
				qinfo_tmp.qclass = qinfo.qclass;
				lookup_qinfo = &qinfo_tmp;
				goto lookup_cache;
			}
			verbose(VERB_ALGO, "answer from the cache failed");
			lock_rw_unlock(&e->lock);
		}

		if(!LDNS_RD_WIRE(sldns_buffer_begin(c->buffer))) {
			if(answer_norec_from_cache(worker, &qinfo,
				*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
				sldns_buffer_read_u16_at(c->buffer, 2), repinfo,
				&edns)) {
				regional_free_all(worker->scratchpad);
				goto send_reply;
			}
			verbose(VERB_ALGO, "answer norec from cache -- "
				"need to validate or not primed");
		}
	}
	sldns_buffer_rewind(c->buffer);
	server_stats_querymiss(&worker->stats, worker);

	if(verbosity >= VERB_CLIENT) {
		if(c->type == comm_udp)
			log_addr(VERB_CLIENT, "udp request from",
				&repinfo->client_addr, repinfo->client_addrlen);
		else	log_addr(VERB_CLIENT, "tcp request from",
				&repinfo->client_addr, repinfo->client_addrlen);
	}

	/* grab a work request structure for this new request */
	mesh_new_client(worker->env.mesh, &qinfo, cinfo,
		sldns_buffer_read_u16_at(c->buffer, 2),
		&edns, repinfo, *(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
		rpz_passthru);
	regional_free_all(worker->scratchpad);
	worker_mem_report(worker, NULL);
	return 0;

send_reply:
	rc = 1;
send_reply_rc:
	if(need_drop) {
		comm_point_drop_reply(repinfo);
		return 0;
	}
	if(is_expired_answer) {
		worker->stats.ans_expired++;
	}
	server_stats_insrcode(&worker->stats, c->buffer);
	if(worker->stats.extended) {
		if(is_secure_answer) worker->stats.ans_secure++;
	}
#ifdef USE_DNSTAP
	/*
	 * sending src (client)/dst (local service) addresses over DNSTAP from send_reply code label (when we serviced local zone for ex.)
	 */
	if(worker->dtenv.log_client_response_messages && rc !=0) {
		log_addr(VERB_ALGO, "from local addr", (void*)repinfo->c->socket->addr, repinfo->c->socket->addrlen);
		log_addr(VERB_ALGO, "response to client", &repinfo->client_addr, repinfo->client_addrlen);
		dt_msg_send_client_response(&worker->dtenv, &repinfo->client_addr, (void*)repinfo->c->socket->addr, c->type, c->ssl, c->buffer);
	}
#endif
	if(worker->env.cfg->log_replies)
	{
		struct timeval tv;
		memset(&tv, 0, sizeof(tv));
		if(qinfo.local_alias && qinfo.local_alias->rrset &&
			qinfo.local_alias->rrset->rk.dname) {
			/* log original qname, before the local alias was
			 * used to resolve that CNAME to something else */
			qinfo.qname = qinfo.local_alias->rrset->rk.dname;
			log_reply_info(NO_VERBOSE, &qinfo,
				&repinfo->client_addr, repinfo->client_addrlen,
				tv, 1, c->buffer,
				(worker->env.cfg->log_destaddr?(void*)repinfo->c->socket->addr:NULL),
				c->type, c->ssl);
		} else {
			log_reply_info(NO_VERBOSE, &qinfo,
				&repinfo->client_addr, repinfo->client_addrlen,
				tv, 1, c->buffer,
				(worker->env.cfg->log_destaddr?(void*)repinfo->c->socket->addr:NULL),
				c->type, c->ssl);
		}
	}
#ifdef USE_DNSCRYPT
	if(!dnsc_handle_uncurved_request(repinfo)) {
		return 0;
	}
#endif
	return rc;
}

void
worker_sighandler(int sig, void* arg)
{
	/* note that log, print, syscalls here give race conditions.
	 * And cause hangups if the log-lock is held by the application. */
	struct worker* worker = (struct worker*)arg;
	switch(sig) {
#ifdef SIGHUP
		case SIGHUP:
			comm_base_exit(worker->base);
			break;
#endif
#ifdef SIGBREAK
		case SIGBREAK:
#endif
		case SIGINT:
			worker->need_to_exit = 1;
			comm_base_exit(worker->base);
			break;
#ifdef SIGQUIT
		case SIGQUIT:
			worker->need_to_exit = 1;
			comm_base_exit(worker->base);
			break;
#endif
		case SIGTERM:
			worker->need_to_exit = 1;
			comm_base_exit(worker->base);
			break;
		default:
			/* unknown signal, ignored */
			break;
	}
}

/** restart statistics timer for worker, if enabled */
static void
worker_restart_timer(struct worker* worker)
{
	if(worker->env.cfg->stat_interval > 0) {
		struct timeval tv;
#ifndef S_SPLINT_S
		tv.tv_sec = worker->env.cfg->stat_interval;
		tv.tv_usec = 0;
#endif
		comm_timer_set(worker->stat_timer, &tv);
	}
}

void worker_stat_timer_cb(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	server_stats_log(&worker->stats, worker, worker->thread_num);
	mesh_stats(worker->env.mesh, "mesh has");
	worker_mem_report(worker, NULL);
	/* SHM is enabled, process data to SHM */
	if (worker->daemon->cfg->shm_enable) {
		shm_main_run(worker);
	}
	if(!worker->daemon->cfg->stat_cumulative) {
		worker_stats_clear(worker);
	}
	/* start next timer */
	worker_restart_timer(worker);
}

void worker_probe_timer_cb(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	struct timeval tv;
#ifndef S_SPLINT_S
	tv.tv_sec = (time_t)autr_probe_timer(&worker->env);
	tv.tv_usec = 0;
#endif
	if(tv.tv_sec != 0)
		comm_timer_set(worker->env.probe_timer, &tv);
}

struct worker*
worker_create(struct daemon* daemon, int id, int* ports, int n)
{
	unsigned int seed;
	struct worker* worker = (struct worker*)calloc(1,
		sizeof(struct worker));
	if(!worker)
		return NULL;
	worker->numports = n;
	worker->ports = (int*)memdup(ports, sizeof(int)*n);
	if(!worker->ports) {
		free(worker);
		return NULL;
	}
	worker->daemon = daemon;
	worker->thread_num = id;
	if(!(worker->cmd = tube_create())) {
		free(worker->ports);
		free(worker);
		return NULL;
	}
	/* create random state here to avoid locking trouble in RAND_bytes */
	if(!(worker->rndstate = ub_initstate(daemon->rand))) {
		log_err("could not init random numbers.");
		tube_delete(worker->cmd);
		free(worker->ports);
		free(worker);
		return NULL;
	}
	explicit_bzero(&seed, sizeof(seed));
	return worker;
}

int
worker_init(struct worker* worker, struct config_file *cfg,
	struct listen_port* ports, int do_sigs)
{
#ifdef USE_DNSTAP
	struct dt_env* dtenv = &worker->dtenv;
#else
	void* dtenv = NULL;
#endif
#ifdef HAVE_GETTID
	worker->thread_tid = gettid();
#endif
	worker->need_to_exit = 0;
	worker->base = comm_base_create(do_sigs);
	if(!worker->base) {
		log_err("could not create event handling base");
		worker_delete(worker);
		return 0;
	}
	comm_base_set_slow_accept_handlers(worker->base, &worker_stop_accept,
		&worker_start_accept, worker);
	if(do_sigs) {
#ifdef SIGHUP
		ub_thread_sig_unblock(SIGHUP);
#endif
#ifdef SIGBREAK
		ub_thread_sig_unblock(SIGBREAK);
#endif
		ub_thread_sig_unblock(SIGINT);
#ifdef SIGQUIT
		ub_thread_sig_unblock(SIGQUIT);
#endif
		ub_thread_sig_unblock(SIGTERM);
#ifndef LIBEVENT_SIGNAL_PROBLEM
		worker->comsig = comm_signal_create(worker->base,
			worker_sighandler, worker);
		if(!worker->comsig
#ifdef SIGHUP
			|| !comm_signal_bind(worker->comsig, SIGHUP)
#endif
#ifdef SIGQUIT
			|| !comm_signal_bind(worker->comsig, SIGQUIT)
#endif
			|| !comm_signal_bind(worker->comsig, SIGTERM)
#ifdef SIGBREAK
			|| !comm_signal_bind(worker->comsig, SIGBREAK)
#endif
			|| !comm_signal_bind(worker->comsig, SIGINT)) {
			log_err("could not create signal handlers");
			worker_delete(worker);
			return 0;
		}
#endif /* LIBEVENT_SIGNAL_PROBLEM */
		if(!daemon_remote_open_accept(worker->daemon->rc,
			worker->daemon->rc_ports, worker)) {
			worker_delete(worker);
			return 0;
		}
#ifdef UB_ON_WINDOWS
		wsvc_setup_worker(worker);
#endif /* UB_ON_WINDOWS */
	} else { /* !do_sigs */
		worker->comsig = NULL;
	}
#ifdef USE_DNSTAP
	if(cfg->dnstap) {
		log_assert(worker->daemon->dtenv != NULL);
		memcpy(&worker->dtenv, worker->daemon->dtenv, sizeof(struct dt_env));
		if(!dt_init(&worker->dtenv, worker->base))
			fatal_exit("dt_init failed");
	}
#endif
	worker->front = listen_create(worker->base, ports,
		cfg->msg_buffer_size, (int)cfg->incoming_num_tcp,
		cfg->do_tcp_keepalive
			? cfg->tcp_keepalive_timeout
			: cfg->tcp_idle_timeout,
		cfg->harden_large_queries, cfg->http_max_streams,
		cfg->http_endpoint, cfg->http_notls_downstream,
		worker->daemon->tcl, worker->daemon->listen_dot_sslctx,
		worker->daemon->listen_doh_sslctx,
		worker->daemon->listen_quic_sslctx,
		dtenv, worker->daemon->doq_table, worker->env.rnd,
		cfg, worker_handle_request, worker);
	if(!worker->front) {
		log_err("could not create listening sockets");
		worker_delete(worker);
		return 0;
	}
	worker->back = outside_network_create(worker->base,
		cfg->msg_buffer_size, (size_t)cfg->outgoing_num_ports,
		cfg->out_ifs, cfg->num_out_ifs, cfg->do_ip4, cfg->do_ip6,
		cfg->do_tcp?cfg->outgoing_num_tcp:0, cfg->ip_dscp,
		worker->daemon->env->infra_cache, worker->rndstate,
		cfg->use_caps_bits_for_id, worker->ports, worker->numports,
		cfg->unwanted_threshold, cfg->outgoing_tcp_mss,
		&worker_alloc_cleanup, worker,
		cfg->do_udp || cfg->udp_upstream_without_downstream,
		worker->daemon->connect_dot_sslctx, cfg->delay_close,
		cfg->tls_use_sni, dtenv, cfg->udp_connect,
		cfg->max_reuse_tcp_queries, cfg->tcp_reuse_timeout,
		cfg->tcp_auth_query_timeout);
	if(!worker->back) {
		log_err("could not create outgoing sockets");
		worker_delete(worker);
		return 0;
	}
	iterator_set_ip46_support(&worker->daemon->mods, worker->daemon->env,
		worker->back);
	/* start listening to commands */
	if(!tube_setup_bg_listen(worker->cmd, worker->base,
		&worker_handle_control_cmd, worker)) {
		log_err("could not create control compt.");
		worker_delete(worker);
		return 0;
	}
	worker->stat_timer = comm_timer_create(worker->base,
		worker_stat_timer_cb, worker);
	if(!worker->stat_timer) {
		log_err("could not create statistics timer");
	}

	/* we use the msg_buffer_size as a good estimate for what the
	 * user wants for memory usage sizes */
	worker->scratchpad = regional_create_custom(cfg->msg_buffer_size);
	if(!worker->scratchpad) {
		log_err("malloc failure");
		worker_delete(worker);
		return 0;
	}

	server_stats_init(&worker->stats, cfg);
	worker->alloc = worker->daemon->worker_allocs[worker->thread_num];
	alloc_set_id_cleanup(worker->alloc, &worker_alloc_cleanup, worker);
	worker->env = *worker->daemon->env;
	comm_base_timept(worker->base, &worker->env.now, &worker->env.now_tv);
	worker->env.worker = worker;
	worker->env.worker_base = worker->base;
	worker->env.send_query = &worker_send_query;
	worker->env.alloc = worker->alloc;
	worker->env.outnet = worker->back;
	worker->env.rnd = worker->rndstate;
	/* If case prefetch is triggered, the corresponding mesh will clear
	 * the scratchpad for the module env in the middle of request handling.
	 * It would be prone to a use-after-free kind of bug, so we avoid
	 * sharing it with worker's own scratchpad at the cost of having
	 * one more pad per worker. */
	worker->env.scratch = regional_create_custom(cfg->msg_buffer_size);
	if(!worker->env.scratch) {
		log_err("malloc failure");
		worker_delete(worker);
		return 0;
	}
	worker->env.mesh = mesh_create(&worker->daemon->mods, &worker->env);
	if(!worker->env.mesh) {
		log_err("malloc failure");
		worker_delete(worker);
		return 0;
	}
	/* Pass on daemon variables that we would need in the mesh area */
	worker->env.mesh->use_response_ip = worker->daemon->use_response_ip;
	worker->env.mesh->use_rpz = worker->daemon->use_rpz;

	worker->env.detach_subs = &mesh_detach_subs;
	worker->env.attach_sub = &mesh_attach_sub;
	worker->env.add_sub = &mesh_add_sub;
	worker->env.kill_sub = &mesh_state_delete;
	worker->env.detect_cycle = &mesh_detect_cycle;
	worker->env.scratch_buffer = sldns_buffer_new(cfg->msg_buffer_size);
	if(!worker->env.scratch_buffer) {
		log_err("malloc failure");
		worker_delete(worker);
		return 0;
	}
	/* one probe timer per process -- if we have 5011 anchors */
	if(autr_get_num_anchors(worker->env.anchors) > 0
#ifndef THREADS_DISABLED
		&& worker->thread_num == 0
#endif
		) {
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		worker->env.probe_timer = comm_timer_create(worker->base,
			worker_probe_timer_cb, worker);
		if(!worker->env.probe_timer) {
			log_err("could not create 5011-probe timer");
		} else {
			/* let timer fire, then it can reset itself */
			comm_timer_set(worker->env.probe_timer, &tv);
		}
	}
	/* zone transfer tasks, setup once per process, if any */
	if(worker->env.auth_zones
#ifndef THREADS_DISABLED
		&& worker->thread_num == 0
#endif
		) {
		auth_xfer_pickup_initial(worker->env.auth_zones, &worker->env);
		auth_zones_pickup_zonemd_verify(worker->env.auth_zones,
			&worker->env);
	}
#ifdef USE_DNSTAP
	if(worker->daemon->cfg->dnstap
#ifndef THREADS_DISABLED
		&& worker->thread_num == 0
#endif
		) {
		if(!dt_io_thread_start(dtenv->dtio, comm_base_internal(
			worker->base), worker->daemon->num)) {
			log_err("could not start dnstap io thread");
			worker_delete(worker);
			return 0;
		}
	}
#endif /* USE_DNSTAP */
	worker_mem_report(worker, NULL);
	/* if statistics enabled start timer */
	if(worker->env.cfg->stat_interval > 0) {
		verbose(VERB_ALGO, "set statistics interval %d secs",
			worker->env.cfg->stat_interval);
		worker_restart_timer(worker);
	}
	pp_init(&sldns_write_uint16, &sldns_write_uint32);
	return 1;
}

void
worker_work(struct worker* worker)
{
	comm_base_dispatch(worker->base);
}

void
worker_delete(struct worker* worker)
{
	if(!worker)
		return;
	if(worker->env.mesh && verbosity >= VERB_OPS) {
		server_stats_log(&worker->stats, worker, worker->thread_num);
		mesh_stats(worker->env.mesh, "mesh has");
		worker_mem_report(worker, NULL);
	}
	outside_network_quit_prepare(worker->back);
	mesh_delete(worker->env.mesh);
	sldns_buffer_free(worker->env.scratch_buffer);
	listen_delete(worker->front);
	outside_network_delete(worker->back);
	comm_signal_delete(worker->comsig);
	tube_delete(worker->cmd);
	comm_timer_delete(worker->stat_timer);
	comm_timer_delete(worker->env.probe_timer);
	free(worker->ports);
	if(worker->thread_num == 0) {
#ifdef UB_ON_WINDOWS
		wsvc_desetup_worker(worker);
#endif /* UB_ON_WINDOWS */
	}
#ifdef USE_DNSTAP
	if(worker->daemon->cfg->dnstap
#ifndef THREADS_DISABLED
		&& worker->thread_num == 0
#endif
		) {
		dt_io_thread_stop(worker->dtenv.dtio);
	}
	dt_deinit(&worker->dtenv);
#endif /* USE_DNSTAP */
	comm_base_delete(worker->base);
	ub_randfree(worker->rndstate);
	/* don't touch worker->alloc, as it's maintained in daemon */
	regional_destroy(worker->env.scratch);
	regional_destroy(worker->scratchpad);
	free(worker);
}

struct outbound_entry*
worker_send_query(struct query_info* qinfo, uint16_t flags, int dnssec,
	int want_dnssec, int nocaps, int check_ratelimit,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* zone,
	size_t zonelen, int tcp_upstream, int ssl_upstream, char* tls_auth_name,
	struct module_qstate* q, int* was_ratelimited)
{
	struct worker* worker = q->env->worker;
	struct outbound_entry* e = (struct outbound_entry*)regional_alloc(
		q->region, sizeof(*e));
	if(!e)
		return NULL;
	e->qstate = q;
	e->qsent = outnet_serviced_query(worker->back, qinfo, flags, dnssec,
		want_dnssec, nocaps, check_ratelimit, tcp_upstream,
		ssl_upstream, tls_auth_name, addr, addrlen, zone, zonelen, q,
		worker_handle_service_reply, e, worker->back->udp_buff, q->env,
		was_ratelimited);
	if(!e->qsent) {
		return NULL;
	}
	return e;
}

void
worker_alloc_cleanup(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	slabhash_clear(&worker->env.rrset_cache->table);
	slabhash_clear(worker->env.msg_cache);
}

void worker_stats_clear(struct worker* worker)
{
	server_stats_init(&worker->stats, worker->env.cfg);
	mesh_stats_clear(worker->env.mesh);
	worker->back->unwanted_replies = 0;
	worker->back->num_tcp_outgoing = 0;
	worker->back->num_udp_outgoing = 0;
}

void worker_start_accept(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	listen_start_accept(worker->front);
	if(worker->thread_num == 0)
		daemon_remote_start_accept(worker->daemon->rc);
}

void worker_stop_accept(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	listen_stop_accept(worker->front);
	if(worker->thread_num == 0)
		daemon_remote_stop_accept(worker->daemon->rc);
}

/* --- fake callbacks for fptr_wlist to work --- */
struct outbound_entry* libworker_send_query(
	struct query_info* ATTR_UNUSED(qinfo),
	uint16_t ATTR_UNUSED(flags), int ATTR_UNUSED(dnssec),
	int ATTR_UNUSED(want_dnssec), int ATTR_UNUSED(nocaps),
	int ATTR_UNUSED(check_ratelimit),
	struct sockaddr_storage* ATTR_UNUSED(addr), socklen_t ATTR_UNUSED(addrlen),
	uint8_t* ATTR_UNUSED(zone), size_t ATTR_UNUSED(zonelen), int ATTR_UNUSED(tcp_upstream),
	int ATTR_UNUSED(ssl_upstream), char* ATTR_UNUSED(tls_auth_name),
	struct module_qstate* ATTR_UNUSED(q), int* ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
	return 0;
}

int libworker_handle_service_reply(struct comm_point* ATTR_UNUSED(c),
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

void libworker_handle_control_cmd(struct tube* ATTR_UNUSED(tube),
        uint8_t* ATTR_UNUSED(buffer), size_t ATTR_UNUSED(len),
        int ATTR_UNUSED(error), void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void libworker_fg_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode),
	sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

void libworker_bg_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode),
	sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

void libworker_event_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode),
	sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

int context_query_cmp(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
	log_assert(0);
	return 0;
}

int order_lock_cmp(const void* ATTR_UNUSED(e1), const void* ATTR_UNUSED(e2))
{
	log_assert(0);
	return 0;
}

int codeline_cmp(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
	log_assert(0);
	return 0;
}

#ifdef USE_DNSTAP
void dtio_tap_callback(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev),
	void* ATTR_UNUSED(arg))
{
	log_assert(0);
}
#endif

#ifdef USE_DNSTAP
void dtio_mainfdcallback(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev),
	void* ATTR_UNUSED(arg))
{
	log_assert(0);
}
#endif

#ifdef HAVE_NGTCP2
void doq_client_event_cb(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev),
	void* ATTR_UNUSED(arg))
{
	log_assert(0);
}
#endif

#ifdef HAVE_NGTCP2
void doq_client_timer_cb(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev),
	void* ATTR_UNUSED(arg))
{
	log_assert(0);
}
#endif
