/*
 * dnstap/dnstap_collector.c -- nsd collector process for dnstap information
 *
 * Copyright (c) 2018, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
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
#include "dnstap/dnstap_collector.h"
#include "dnstap/dnstap.h"
#include "util.h"
#include "nsd.h"
#include "region-allocator.h"
#include "buffer.h"
#include "namedb.h"
#include "options.h"
#include "remote.h"

#include "udb.h"
#include "rrl.h"

struct dt_collector* dt_collector_create(struct nsd* nsd)
{
	int i, sv[2];
	struct dt_collector* dt_col = (struct dt_collector*)xalloc_zero(
		sizeof(*dt_col));
	dt_col->count = nsd->child_count * 2;
	dt_col->dt_env = NULL;
	dt_col->region = region_create(xalloc, free);
	dt_col->send_buffer = buffer_create(dt_col->region,
		/* msglen + is_response + addrlen + is_tcp + packetlen + packet + zonelen + zone + spare + local_addr + addr */
		4+1+4+1+4+TCP_MAX_MESSAGE_LEN+4+MAXHOSTNAMELEN + 32 +
#ifdef INET6
		sizeof(struct sockaddr_storage) + sizeof(struct sockaddr_storage)
#else
		sizeof(struct sockaddr_in) + sizeof(struct sockaddr_in)
#endif
		);

	/* open communication channels in struct nsd */
	nsd->dt_collector_fd_send = (int*)xalloc_array_zero(dt_col->count,
		sizeof(int));
	nsd->dt_collector_fd_recv = (int*)xalloc_array_zero(dt_col->count,
		sizeof(int));
	for(i=0; i<dt_col->count; i++) {
		int sv[2];
		int bufsz = buffer_capacity(dt_col->send_buffer);
		sv[0] = -1; /* For receiving by parent (dnstap-collector) */
		sv[1] = -1; /* For sending   by child  (server childs) */
		if(socketpair(AF_UNIX, SOCK_DGRAM
#ifdef SOCK_NONBLOCK
			| SOCK_NONBLOCK
#endif
			, 0, sv) < 0) {
			error("dnstap_collector: cannot create communication channel: %s",
				strerror(errno));
		}
#ifndef SOCK_NONBLOCK
		if (fcntl(sv[0], F_SETFL, O_NONBLOCK) == -1) {
			log_msg(LOG_ERR, "dnstap_collector receive fd fcntl "
				"failed: %s", strerror(errno));
		}
		if (fcntl(sv[1], F_SETFL, O_NONBLOCK) == -1) {
			log_msg(LOG_ERR, "dnstap_collector send fd fcntl "
				"failed: %s", strerror(errno));
		}
#endif
		if(setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz))) {
			log_msg(LOG_ERR, "setting dnstap_collector "
				"receive buffer size failed: %s", strerror(errno));
		}
		if(setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz))) {
			log_msg(LOG_ERR, "setting dnstap_collector "
				"send buffer size failed: %s", strerror(errno));
		}
		nsd->dt_collector_fd_recv[i] = sv[0];
		nsd->dt_collector_fd_send[i] = sv[1];
	}
	nsd->dt_collector_fd_swap = nsd->dt_collector_fd_send + nsd->child_count;

	/* open socketpair */
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
		error("dnstap_collector: cannot create socketpair: %s",
			strerror(errno));
	}
	if(fcntl(sv[0], F_SETFL, O_NONBLOCK) == -1) {
		log_msg(LOG_ERR, "fcntl failed: %s", strerror(errno));
	}
	if(fcntl(sv[1], F_SETFL, O_NONBLOCK) == -1) {
		log_msg(LOG_ERR, "fcntl failed: %s", strerror(errno));
	}
	dt_col->cmd_socket_dt = sv[0];
	dt_col->cmd_socket_nsd = sv[1];

	return dt_col;
}

void dt_collector_destroy(struct dt_collector* dt_col, struct nsd* nsd)
{
	if(!dt_col) return;
	free(nsd->dt_collector_fd_recv);
	nsd->dt_collector_fd_recv = NULL;
	if (nsd->dt_collector_fd_send < nsd->dt_collector_fd_swap)
		free(nsd->dt_collector_fd_send);
	else
		free(nsd->dt_collector_fd_swap);
	nsd->dt_collector_fd_send = NULL;
	nsd->dt_collector_fd_swap = NULL;
	region_destroy(dt_col->region);
	free(dt_col);
}

void dt_collector_close(struct dt_collector* dt_col, struct nsd* nsd)
{
	int i, *fd_send;
	if(!dt_col) return;
	if(dt_col->cmd_socket_dt != -1) {
		close(dt_col->cmd_socket_dt);
		dt_col->cmd_socket_dt = -1;
	}
	if(dt_col->cmd_socket_nsd != -1) {
		close(dt_col->cmd_socket_nsd);
		dt_col->cmd_socket_nsd = -1;
	}
	fd_send = nsd->dt_collector_fd_send < nsd->dt_collector_fd_swap
	        ? nsd->dt_collector_fd_send : nsd->dt_collector_fd_swap;
	for(i=0; i<dt_col->count; i++) {
		if(nsd->dt_collector_fd_recv[i] != -1) {
			close(nsd->dt_collector_fd_recv[i]);
			nsd->dt_collector_fd_recv[i] = -1;
		}
		if(fd_send[i] != -1) {
			close(fd_send[i]);
			fd_send[i] = -1;
		}
	}
}

/* handle command from nsd to dt collector.
 * mostly, check for fd closed, this means we have to exit */
void
dt_handle_cmd_from_nsd(int ATTR_UNUSED(fd), short event, void* arg)
{
	struct dt_collector* dt_col = (struct dt_collector*)arg;
	if((event&EV_READ) != 0) {
		event_base_loopexit(dt_col->event_base, NULL);
	}
}

/* receive data from fd into buffer, 1 when message received, -1 on error */
static int recv_into_buffer(int fd, struct buffer* buf)
{
	size_t msglen;
	ssize_t r;

	assert(buffer_position(buf) == 0);
	r = recv(fd, buffer_current(buf), buffer_capacity(buf), MSG_DONTWAIT);
	if(r == -1) {
		if(errno == EAGAIN || errno == EINTR || errno == EMSGSIZE) {
			/* continue to receive a message later */
			return 0;
		}
		log_msg(LOG_ERR, "dnstap collector: receive failed: %s",
			strerror(errno));
		return -1;
	}
	if(r == 0) {
		/* Remote end closed the connection? */
		log_msg(LOG_ERR, "dnstap collector: remote closed connection");
		return -1;
	}
	assert(r > 4);
	msglen = buffer_read_u32_at(buf, 0);
	if(msglen != (size_t)(r - 4)) {
		/* Is this still possible now the communication channel is of
		 * type SOCK_DGRAM? I think not, but better safe than sorry. */
		log_msg(LOG_ERR, "dnstap collector: out of sync (msglen: %u)",
			(unsigned int) msglen);
		return 0;
	}
	buffer_skip(buf, r);
	buffer_flip(buf);
	return 1;
}

/* submit the content of the buffer received to dnstap */
static void
dt_submit_content(struct dt_env* dt_env, struct buffer* buf)
{
	uint8_t is_response, is_tcp;
#ifdef INET6
	struct sockaddr_storage local_addr, addr;
#else
	struct sockaddr_in local_addr, addr;
#endif
	socklen_t addrlen;
	size_t pktlen;
	uint8_t* data;
	size_t zonelen;
	uint8_t* zone;

	/* parse content from buffer */
	if(!buffer_available(buf, 4+1+4)) return;
	buffer_skip(buf, 4); /* skip msglen */
	is_response = buffer_read_u8(buf);
	addrlen = buffer_read_u32(buf);
	if(addrlen > sizeof(local_addr) || addrlen > sizeof(addr)) return;
	if(!buffer_available(buf, 2*addrlen)) return;
	buffer_read(buf, &local_addr, addrlen);
	buffer_read(buf, &addr, addrlen);
	if(!buffer_available(buf, 1+4)) return;
	is_tcp = buffer_read_u8(buf);
	pktlen = buffer_read_u32(buf);
	if(!buffer_available(buf, pktlen)) return;
	data = buffer_current(buf);
	buffer_skip(buf, pktlen);
	if(!buffer_available(buf, 4)) return;
	zonelen = buffer_read_u32(buf);
	if(zonelen == 0) {
		zone = NULL;
	} else {
		if(zonelen > MAXDOMAINLEN) return;
		if(!buffer_available(buf, zonelen)) return;
		zone = buffer_current(buf);
		buffer_skip(buf, zonelen);
	}

	/* submit it */
	if(is_response) {
		dt_msg_send_auth_response(dt_env, &local_addr, &addr, is_tcp, zone,
			zonelen, data, pktlen);
	} else {
		dt_msg_send_auth_query(dt_env, &local_addr, &addr, is_tcp, zone,
			zonelen, data, pktlen);
	}
}

/* handle input from worker for dnstap */
void
dt_handle_input(int fd, short event, void* arg)
{
	struct dt_collector_input* dt_input = (struct dt_collector_input*)arg;
	if((event&EV_READ) != 0) {
		/* receive */
		int r = recv_into_buffer(fd, dt_input->buffer);
		if(r == 0)
			return;
		else if(r < 0) {
			event_base_loopexit(dt_input->dt_collector->event_base, NULL);
			return;
		}
		/* once data is complete, send it to dnstap */
		VERBOSITY(4, (LOG_INFO, "dnstap collector: received msg len %d",
			(int)buffer_remaining(dt_input->buffer)));
		if(dt_input->dt_collector->dt_env) {
			dt_submit_content(dt_input->dt_collector->dt_env,
				dt_input->buffer);
		}
		
		/* clear buffer for next message */
		buffer_clear(dt_input->buffer);
	}
}

/* init dnstap */
static void dt_init_dnstap(struct dt_collector* dt_col, struct nsd* nsd)
{
	int num_workers = 1;
#ifdef HAVE_CHROOT
	if(nsd->chrootdir && nsd->chrootdir[0]) {
		int l = strlen(nsd->chrootdir)-1; /* ends in trailing slash */
		if (nsd->options->dnstap_socket_path &&
			nsd->options->dnstap_socket_path[0] == '/' &&
			strncmp(nsd->options->dnstap_socket_path,
				nsd->chrootdir, l) == 0)
			nsd->options->dnstap_socket_path += l;
	}
#endif
	dt_col->dt_env = dt_create(nsd->options->dnstap_socket_path,
		nsd->options->dnstap_ip, num_workers, nsd->options->dnstap_tls,
		nsd->options->dnstap_tls_server_name,
		nsd->options->dnstap_tls_cert_bundle,
		nsd->options->dnstap_tls_client_key_file,
		nsd->options->dnstap_tls_client_cert_file);
	if(!dt_col->dt_env) {
		log_msg(LOG_ERR, "could not create dnstap env");
		return;
	}
	dt_apply_cfg(dt_col->dt_env, nsd->options);
	dt_init(dt_col->dt_env);
}

/* cleanup dt collector process for exit */
static void dt_collector_cleanup(struct dt_collector* dt_col, struct nsd* nsd)
{
	int i;
	dt_delete(dt_col->dt_env);
	event_del(dt_col->cmd_event);
	for(i=0; i<dt_col->count; i++) {
		event_del(dt_col->inputs[i].event);
	}
	dt_collector_close(dt_col, nsd);
	event_base_free(dt_col->event_base);
#ifdef MEMCLEAN
	free(dt_col->cmd_event);
	if(dt_col->inputs) {
		for(i=0; i<dt_col->count; i++) {
			free(dt_col->inputs[i].event);
		}
		free(dt_col->inputs);
	}
	dt_collector_destroy(dt_col, nsd);
	daemon_remote_delete(nsd->rc); /* ssl-delete secret keys */
	nsd_options_destroy(nsd->options);
	region_destroy(nsd->region);
#endif
}

/* attach events to the event base to listen to the workers and cmd channel */
static void dt_attach_events(struct dt_collector* dt_col, struct nsd* nsd)
{
	int i;
	/* create event base */
	dt_col->event_base = nsd_child_event_base();
	if(!dt_col->event_base) {
		error("dnstap collector: event_base create failed");
	}

	/* add command handler */
	dt_col->cmd_event = (struct event*)xalloc_zero(
		sizeof(*dt_col->cmd_event));
	event_set(dt_col->cmd_event, dt_col->cmd_socket_dt,
		EV_PERSIST|EV_READ, dt_handle_cmd_from_nsd, dt_col);
	if(event_base_set(dt_col->event_base, dt_col->cmd_event) != 0)
		log_msg(LOG_ERR, "dnstap collector: event_base_set failed");
	if(event_add(dt_col->cmd_event, NULL) != 0)
		log_msg(LOG_ERR, "dnstap collector: event_add failed");
	
	/* add worker input handlers */
	dt_col->inputs = xalloc_array_zero(dt_col->count,
		sizeof(*dt_col->inputs));
	for(i=0; i<dt_col->count; i++) {
		dt_col->inputs[i].dt_collector = dt_col;
		dt_col->inputs[i].event = (struct event*)xalloc_zero(
			sizeof(struct event));
		event_set(dt_col->inputs[i].event,
			nsd->dt_collector_fd_recv[i], EV_PERSIST|EV_READ,
			dt_handle_input, &dt_col->inputs[i]);
		if(event_base_set(dt_col->event_base,
			dt_col->inputs[i].event) != 0)
			log_msg(LOG_ERR, "dnstap collector: event_base_set failed");
		if(event_add(dt_col->inputs[i].event, NULL) != 0)
			log_msg(LOG_ERR, "dnstap collector: event_add failed");
		
		dt_col->inputs[i].buffer = buffer_create(dt_col->region,
			/* msglen + is_response + addrlen + is_tcp + packetlen + packet + zonelen + zone + spare + local_addr + addr */
			4+1+4+1+4+TCP_MAX_MESSAGE_LEN+4+MAXHOSTNAMELEN + 32 +
#ifdef INET6
			sizeof(struct sockaddr_storage) + sizeof(struct sockaddr_storage)
#else
			sizeof(struct sockaddr_in) + sizeof(struct sockaddr_in)
#endif
		);
		assert(buffer_capacity(dt_col->inputs[i].buffer) ==
			buffer_capacity(dt_col->send_buffer));
	}
}

/* the dnstap collector process main routine */
static void dt_collector_run(struct dt_collector* dt_col, struct nsd* nsd)
{
	/* init dnstap */
	VERBOSITY(1, (LOG_INFO, "dnstap collector started"));
	dt_init_dnstap(dt_col, nsd);
	dt_attach_events(dt_col, nsd);

	/* run */
	if(event_base_loop(dt_col->event_base, 0) == -1) {
		error("dnstap collector: event_base_loop failed");
	}

	/* cleanup and done */
	VERBOSITY(1, (LOG_INFO, "dnstap collector stopped"));
	dt_collector_cleanup(dt_col, nsd);
	exit(0);
}

void dt_collector_start(struct dt_collector* dt_col, struct nsd* nsd)
{
	int i, *fd_send;
	/* fork */
	dt_col->dt_pid = fork();
	if(dt_col->dt_pid == -1) {
		error("dnstap_collector: fork failed: %s", strerror(errno));
	}
	if(dt_col->dt_pid == 0) {
		/* the dt collector process is this */
		/* close the nsd side of the command channel */
		close(dt_col->cmd_socket_nsd);
		dt_col->cmd_socket_nsd = -1;

		/* close the send side of the communication channels */
		assert(nsd->dt_collector_fd_send < nsd->dt_collector_fd_swap);
		fd_send = nsd->dt_collector_fd_send < nsd->dt_collector_fd_swap
			? nsd->dt_collector_fd_send : nsd->dt_collector_fd_swap;
		for(i=0; i<dt_col->count; i++) {
			if(fd_send[i] != -1) {
				close(fd_send[i]);
				fd_send[i] = -1;
			}
		}
#ifdef HAVE_SETPROCTITLE
		setproctitle("dnstap_collector");
#endif
#ifdef USE_LOG_PROCESS_ROLE
                log_set_process_role("dnstap_collector");
#endif
		/* Free serve process specific memory pages */
#ifdef RATELIMIT
		rrl_mmap_deinit_keep_mmap();
#endif
		udb_base_free_keep_mmap(nsd->task[0]);
		udb_base_free_keep_mmap(nsd->task[1]);
		namedb_close(nsd->db);

		dt_collector_run(dt_col, nsd);
		/* NOTREACH */
		exit(0);
	} else {
		/* the parent continues on, with starting NSD */
		/* close the dt side of the command channel */
		close(dt_col->cmd_socket_dt);
		dt_col->cmd_socket_dt = -1;

		/* close the receive side of the communication channels */
		for(i=0; i<dt_col->count; i++) {
			if(nsd->dt_collector_fd_recv[i] != -1) {
				close(nsd->dt_collector_fd_recv[i]);
				nsd->dt_collector_fd_recv[i] = -1;
			}
		}
	}
}

/* put data for sending to the collector process into the buffer */
static int
prep_send_data(struct buffer* buf, uint8_t is_response,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	socklen_t addrlen, int is_tcp, struct buffer* packet,
	struct zone* zone)
{
	buffer_clear(buf);
#ifdef INET6
	if(local_addr->ss_family != addr->ss_family)
		return 0; /* must be same length to send */
#else
	if(local_addr->sin_family != addr->sin_family)
		return 0; /* must be same length to send */
#endif
	if(!buffer_available(buf, 4+1+4+2*addrlen+1+4+buffer_remaining(packet)))
		return 0; /* does not fit in send_buffer, log is dropped */
	buffer_skip(buf, 4); /* the length of the message goes here */
	buffer_write_u8(buf, is_response);
	buffer_write_u32(buf, addrlen);
	buffer_write(buf, local_addr, (size_t)addrlen);
	buffer_write(buf, addr, (size_t)addrlen);
	buffer_write_u8(buf, (is_tcp?1:0));
	buffer_write_u32(buf, buffer_remaining(packet));
	buffer_write(buf, buffer_begin(packet), buffer_remaining(packet));
	if(zone && zone->apex && domain_dname(zone->apex)) {
		if(!buffer_available(buf, 4 + domain_dname(zone->apex)->name_size))
			return 0;
		buffer_write_u32(buf, domain_dname(zone->apex)->name_size);
		buffer_write(buf, dname_name(domain_dname(zone->apex)),
			domain_dname(zone->apex)->name_size);
	} else {
		if(!buffer_available(buf, 4))
			return 0;
		buffer_write_u32(buf, 0);
	}

	buffer_flip(buf);
	/* write length of message */
	buffer_write_u32_at(buf, 0, buffer_remaining(buf)-4);
	return 1;
}

/* attempt to send buffer to socket, if it blocks do not send it.
 * return 0 on success, -1 on error */
static int attempt_to_send(int s, uint8_t* data, size_t len)
{
	ssize_t r;
	if(len == 0)
		return 0;
	r = send(s, data, len, MSG_DONTWAIT | MSG_NOSIGNAL);
	if(r == -1) {
		if(errno == EAGAIN || errno == EINTR ||
				errno == ENOBUFS || errno == EMSGSIZE) {
			/* check if pipe is full, if the nonblocking fd blocks,
			 * then drop the message */
			return 0;
		}
		/* some sort of error, print it */
		log_msg(LOG_ERR, "dnstap collector: send failed: %s",
			strerror(errno));
		return -1;
	}
	assert(r > 0);
	if(r > 0) {
		assert((size_t)r == len);
		return 0;
	}
	/* Other end closed the channel? */
	log_msg(LOG_ERR, "dnstap collector: server child closed the channel");
	return -1;
}

void dt_collector_submit_auth_query(struct nsd* nsd,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	socklen_t addrlen, int is_tcp, struct buffer* packet)
{
	if(!nsd->dt_collector) return;
	if(!nsd->options->dnstap_log_auth_query_messages) return;
	if(nsd->dt_collector_fd_send[nsd->this_child->child_num] == -1) return;
	VERBOSITY(4, (LOG_INFO, "dnstap submit auth query"));

	/* marshal data into send buffer */
	if(!prep_send_data(nsd->dt_collector->send_buffer, 0, local_addr, addr, addrlen,
		is_tcp, packet, NULL))
		return; /* probably did not fit in buffer */

	/* attempt to send data; do not block */
	if(attempt_to_send(nsd->dt_collector_fd_send[nsd->this_child->child_num],
			buffer_begin(nsd->dt_collector->send_buffer),
			buffer_remaining(nsd->dt_collector->send_buffer))) {
		/* Something went wrong sending to the socket. Don't send to
		 * this socket again. */
		close(nsd->dt_collector_fd_send[nsd->this_child->child_num]);
		nsd->dt_collector_fd_send[nsd->this_child->child_num] = -1;
	}
}

void dt_collector_submit_auth_response(struct nsd* nsd,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	socklen_t addrlen, int is_tcp, struct buffer* packet,
	struct zone* zone)
{
	if(!nsd->dt_collector) return;
	if(!nsd->options->dnstap_log_auth_response_messages) return;
	if(nsd->dt_collector_fd_send[nsd->this_child->child_num] == -1) return;
	VERBOSITY(4, (LOG_INFO, "dnstap submit auth response"));

	/* marshal data into send buffer */
	if(!prep_send_data(nsd->dt_collector->send_buffer, 1, local_addr, addr, addrlen,
		is_tcp, packet, zone))
		return; /* probably did not fit in buffer */

	/* attempt to send data; do not block */
	if(attempt_to_send(nsd->dt_collector_fd_send[nsd->this_child->child_num],
			buffer_begin(nsd->dt_collector->send_buffer),
			buffer_remaining(nsd->dt_collector->send_buffer))) {
		/* Something went wrong sending to the socket. Don't send to
		 * this socket again. */
		close(nsd->dt_collector_fd_send[nsd->this_child->child_num]);
		nsd->dt_collector_fd_send[nsd->this_child->child_num] = -1;
	}
}
