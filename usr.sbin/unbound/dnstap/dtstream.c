/*
 * dnstap/dtstream.c - Frame Streams thread for unbound DNSTAP
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
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
 *
 */

/**
 * \file
 *
 * An implementation of the Frame Streams data transport protocol for
 * the Unbound DNSTAP message logging facility.
 */

#include "config.h"
#include "dnstap/dtstream.h"
#include "dnstap/dnstap_fstrm.h"
#include "util/config_file.h"
#include "util/ub_event.h"
#include "util/net_help.h"
#include "services/outside_network.h"
#include "sldns/sbuffer.h"
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#include <fcntl.h>
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

/** number of messages to process in one output callback */
#define DTIO_MESSAGES_PER_CALLBACK 100
/** the msec to wait for reconnect (if not immediate, the first attempt) */
#define DTIO_RECONNECT_TIMEOUT_MIN 10
/** the msec to wait for reconnect max after backoff */
#define DTIO_RECONNECT_TIMEOUT_MAX 1000
/** the msec to wait for reconnect slow, to stop busy spinning on reconnect */
#define DTIO_RECONNECT_TIMEOUT_SLOW 1000
/** number of messages before wakeup of thread */
#define DTIO_MSG_FOR_WAKEUP 32

/** maximum length of received frame */
#define DTIO_RECV_FRAME_MAX_LEN 1000

struct stop_flush_info;
/** DTIO command channel commands */
enum {
	/** DTIO command channel stop */
	DTIO_COMMAND_STOP = 0,
	/** DTIO command channel wakeup */
	DTIO_COMMAND_WAKEUP = 1
} dtio_channel_command;

/** open the output channel */
static void dtio_open_output(struct dt_io_thread* dtio);
/** add output event for read and write */
static int dtio_add_output_event_write(struct dt_io_thread* dtio);
/** start reconnection attempts */
static void dtio_reconnect_enable(struct dt_io_thread* dtio);
/** stop from stop_flush event loop */
static void dtio_stop_flush_exit(struct stop_flush_info* info);
/** setup a start control message */
static int dtio_control_start_send(struct dt_io_thread* dtio);
#ifdef HAVE_SSL
/** enable briefly waiting for a read event, for SSL negotiation */
static int dtio_enable_brief_read(struct dt_io_thread* dtio);
/** enable briefly waiting for a write event, for SSL negotiation */
static int dtio_enable_brief_write(struct dt_io_thread* dtio);
#endif

struct dt_msg_queue*
dt_msg_queue_create(struct comm_base* base)
{
	struct dt_msg_queue* mq = calloc(1, sizeof(*mq));
	if(!mq) return NULL;
	mq->maxsize = 1*1024*1024; /* set max size of buffer, per worker,
		about 1 M should contain 64K messages with some overhead,
		or a whole bunch smaller ones */
	mq->wakeup_timer = comm_timer_create(base, mq_wakeup_cb, mq);
	if(!mq->wakeup_timer) {
		free(mq);
		return NULL;
	}
	lock_basic_init(&mq->lock);
	lock_protect(&mq->lock, mq, sizeof(*mq));
	return mq;
}

/** clear the message list, caller must hold the lock */
static void
dt_msg_queue_clear(struct dt_msg_queue* mq)
{
	struct dt_msg_entry* e = mq->first, *next=NULL;
	while(e) {
		next = e->next;
		free(e->buf);
		free(e);
		e = next;
	}
	mq->first = NULL;
	mq->last = NULL;
	mq->cursize = 0;
	mq->msgcount = 0;
}

void
dt_msg_queue_delete(struct dt_msg_queue* mq)
{
	if(!mq) return;
	lock_basic_destroy(&mq->lock);
	dt_msg_queue_clear(mq);
	comm_timer_delete(mq->wakeup_timer);
	free(mq);
}

/** make the dtio wake up by sending a wakeup command */
static void dtio_wakeup(struct dt_io_thread* dtio)
{
	uint8_t cmd = DTIO_COMMAND_WAKEUP;
	if(!dtio) return;
	if(!dtio->started) return;

	while(1) {
		ssize_t r = write(dtio->commandpipe[1], &cmd, sizeof(cmd));
		if(r == -1) {
#ifndef USE_WINSOCK
			if(errno == EINTR || errno == EAGAIN)
				continue;
#else
			if(WSAGetLastError() == WSAEINPROGRESS)
				continue;
			if(WSAGetLastError() == WSAEWOULDBLOCK)
				continue;
#endif
			log_err("dnstap io wakeup: write: %s",
				sock_strerror(errno));
			break;
		}
		break;
	}
}

void
mq_wakeup_cb(void* arg)
{
	struct dt_msg_queue* mq = (struct dt_msg_queue*)arg;

	lock_basic_lock(&mq->dtio->wakeup_timer_lock);
	mq->dtio->wakeup_timer_enabled = 0;
	lock_basic_unlock(&mq->dtio->wakeup_timer_lock);
	dtio_wakeup(mq->dtio);
}

/** start timer to wakeup dtio because there is content in the queue */
static void
dt_msg_queue_start_timer(struct dt_msg_queue* mq, int wakeupnow)
{
	struct timeval tv = {0};
	/* Start a timer to process messages to be logged.
	 * If we woke up the dtio thread for every message, the wakeup
	 * messages take up too much processing power.  If the queue
	 * fills up the wakeup happens immediately.  The timer wakes it up
	 * if there are infrequent messages to log. */

	/* we cannot start a timer in dtio thread, because it is a different
	 * thread and its event base is in use by the other thread, it would
	 * give race conditions if we tried to modify its event base,
	 * and locks would wait until it woke up, and this is what we do. */

	/* do not start the timer if a timer already exists, perhaps
	 * in another worker.  So this variable is protected by a lock in
	 * dtio. */

	/* If we need to wakeupnow, 0 the timer to force the callback. */
	lock_basic_lock(&mq->dtio->wakeup_timer_lock);
	if(mq->dtio->wakeup_timer_enabled) {
		if(wakeupnow) {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			comm_timer_set(mq->wakeup_timer, &tv);
		}
		lock_basic_unlock(&mq->dtio->wakeup_timer_lock);
		return;
	}
	mq->dtio->wakeup_timer_enabled = 1; /* we are going to start one */

	/* start the timer, in mq, in the event base of our worker */
	if(!wakeupnow) {
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		/* If it is already set, keep it running. */
		if(!comm_timer_is_set(mq->wakeup_timer))
	comm_timer_set(mq->wakeup_timer, &tv);
	} else {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		comm_timer_set(mq->wakeup_timer, &tv);
	}
	lock_basic_unlock(&mq->dtio->wakeup_timer_lock);
}

void
dt_msg_queue_submit(struct dt_msg_queue* mq, void* buf, size_t len)
{
	int wakeupnow = 0, wakeupstarttimer = 0;
	struct dt_msg_entry* entry;

	/* check conditions */
	if(!buf) return;
	if(len == 0) {
		/* it is not possible to log entries with zero length,
		 * because the framestream protocol does not carry it.
		 * However the protobuf serialization does not create zero
		 * length datagrams for dnstap, so this should not happen. */
		free(buf);
		return;
	}
	if(!mq) {
		free(buf);
		return;
	}

	/* allocate memory for queue entry */
	entry = malloc(sizeof(*entry));
	if(!entry) {
		log_err("out of memory logging dnstap");
		free(buf);
		return;
	}
	entry->next = NULL;
	entry->buf = buf;
	entry->len = len;

	/* acquire lock */
	lock_basic_lock(&mq->lock);
	/* if list was empty, start timer for (eventual) wakeup,
	 * or if dtio is not writing now an eventual wakeup is needed. */
	if(mq->first == NULL || !mq->dtio->event_added_is_write)
		wakeupstarttimer = 1;
	/* if list contains more than wakeupnum elements, wakeup now,
	 * or if list is (going to be) almost full */
	if(mq->msgcount == DTIO_MSG_FOR_WAKEUP ||
		(mq->cursize < mq->maxsize * 9 / 10 &&
		mq->cursize+len >= mq->maxsize * 9 / 10))
		wakeupnow = 1;
	/* see if it is going to fit */
	if(mq->cursize + len > mq->maxsize) {
		/* buffer full, or congested. */
		/* drop */
		lock_basic_unlock(&mq->lock);
		free(buf);
		free(entry);
		return;
	}
	mq->cursize += len;
	mq->msgcount ++;
	/* append to list */
	if(mq->last) {
		mq->last->next = entry;
	} else {
		mq->first = entry;
	}
	mq->last = entry;
	/* release lock */
	lock_basic_unlock(&mq->lock);

	if(wakeupnow || wakeupstarttimer) {
		dt_msg_queue_start_timer(mq, wakeupnow);
	}
}

struct dt_io_thread* dt_io_thread_create(void)
{
	struct dt_io_thread* dtio = calloc(1, sizeof(*dtio));
	lock_basic_init(&dtio->wakeup_timer_lock);
	lock_protect(&dtio->wakeup_timer_lock, &dtio->wakeup_timer_enabled,
		sizeof(dtio->wakeup_timer_enabled));
	return dtio;
}

void dt_io_thread_delete(struct dt_io_thread* dtio)
{
	struct dt_io_list_item* item, *nextitem;
	if(!dtio) return;
	lock_basic_destroy(&dtio->wakeup_timer_lock);
	item=dtio->io_list;
	while(item) {
		nextitem = item->next;
		free(item);
		item = nextitem;
	}
	free(dtio->socket_path);
	free(dtio->ip_str);
	free(dtio->tls_server_name);
	free(dtio->client_key_file);
	free(dtio->client_cert_file);
	if(dtio->ssl_ctx) {
#ifdef HAVE_SSL
		SSL_CTX_free(dtio->ssl_ctx);
#endif
	}
	free(dtio);
}

int dt_io_thread_apply_cfg(struct dt_io_thread* dtio, struct config_file *cfg)
{
	if(!cfg->dnstap) {
		log_warn("cannot setup dnstap because dnstap-enable is no");
		return 0;
	}

	/* what type of connectivity do we have */
	if(cfg->dnstap_ip && cfg->dnstap_ip[0]) {
		if(cfg->dnstap_tls)
			dtio->upstream_is_tls = 1;
		else	dtio->upstream_is_tcp = 1;
	} else {
		dtio->upstream_is_unix = 1;
	}
	dtio->is_bidirectional = cfg->dnstap_bidirectional;

	if(dtio->upstream_is_unix) {
		char* nm;
		if(!cfg->dnstap_socket_path ||
			cfg->dnstap_socket_path[0]==0) {
			log_err("dnstap setup: no dnstap-socket-path for "
				"socket connect");
			return 0;
		}
		nm = cfg->dnstap_socket_path;
		if(cfg->chrootdir && cfg->chrootdir[0] && strncmp(nm,
			cfg->chrootdir, strlen(cfg->chrootdir)) == 0)
			nm += strlen(cfg->chrootdir);
		free(dtio->socket_path);
		dtio->socket_path = strdup(nm);
		if(!dtio->socket_path) {
			log_err("dnstap setup: malloc failure");
			return 0;
		}
	}

	if(dtio->upstream_is_tcp || dtio->upstream_is_tls) {
		if(!cfg->dnstap_ip || cfg->dnstap_ip[0] == 0) {
			log_err("dnstap setup: no dnstap-ip for TCP connect");
			return 0;
		}
		free(dtio->ip_str);
		dtio->ip_str = strdup(cfg->dnstap_ip);
		if(!dtio->ip_str) {
			log_err("dnstap setup: malloc failure");
			return 0;
		}
	}

	if(dtio->upstream_is_tls) {
#ifdef HAVE_SSL
		if(cfg->dnstap_tls_server_name &&
			cfg->dnstap_tls_server_name[0]) {
			free(dtio->tls_server_name);
			dtio->tls_server_name = strdup(
				cfg->dnstap_tls_server_name);
			if(!dtio->tls_server_name) {
				log_err("dnstap setup: malloc failure");
				return 0;
			}
			if(!check_auth_name_for_ssl(dtio->tls_server_name))
				return 0;
		}
		if(cfg->dnstap_tls_client_key_file &&
			cfg->dnstap_tls_client_key_file[0]) {
			dtio->use_client_certs = 1;
			free(dtio->client_key_file);
			dtio->client_key_file = strdup(
				cfg->dnstap_tls_client_key_file);
			if(!dtio->client_key_file) {
				log_err("dnstap setup: malloc failure");
				return 0;
			}
			if(!cfg->dnstap_tls_client_cert_file ||
				cfg->dnstap_tls_client_cert_file[0]==0) {
				log_err("dnstap setup: client key "
					"authentication enabled with "
					"dnstap-tls-client-key-file, but "
					"no dnstap-tls-client-cert-file "
					"is given");
				return 0;
			}
			free(dtio->client_cert_file);
			dtio->client_cert_file = strdup(
				cfg->dnstap_tls_client_cert_file);
			if(!dtio->client_cert_file) {
				log_err("dnstap setup: malloc failure");
				return 0;
			}
		} else {
			dtio->use_client_certs = 0;
			dtio->client_key_file = NULL;
			dtio->client_cert_file = NULL;
		}

		if(cfg->dnstap_tls_cert_bundle) {
			dtio->ssl_ctx = connect_sslctx_create(
				dtio->client_key_file,
				dtio->client_cert_file,
				cfg->dnstap_tls_cert_bundle, 0);
		} else {
			dtio->ssl_ctx = connect_sslctx_create(
				dtio->client_key_file,
				dtio->client_cert_file,
				cfg->tls_cert_bundle, cfg->tls_win_cert);
		}
		if(!dtio->ssl_ctx) {
			log_err("could not setup SSL CTX");
			return 0;
		}
		dtio->tls_use_sni = cfg->tls_use_sni;
#endif /* HAVE_SSL */
	}
	return 1;
}

int dt_io_thread_register_queue(struct dt_io_thread* dtio,
        struct dt_msg_queue* mq)
{
	struct dt_io_list_item* item = malloc(sizeof(*item));
	if(!item) return 0;
	lock_basic_lock(&mq->lock);
	mq->dtio = dtio;
	lock_basic_unlock(&mq->lock);
	item->queue = mq;
	item->next = dtio->io_list;
	dtio->io_list = item;
	dtio->io_list_iter = NULL;
	return 1;
}

void dt_io_thread_unregister_queue(struct dt_io_thread* dtio,
        struct dt_msg_queue* mq)
{
	struct dt_io_list_item* item, *prev=NULL;
	if(!dtio) return;
	item = dtio->io_list;
	while(item) {
		if(item->queue == mq) {
			/* found it */
			if(prev) prev->next = item->next;
			else dtio->io_list = item->next;
			/* the queue itself only registered, not deleted */
			lock_basic_lock(&item->queue->lock);
			item->queue->dtio = NULL;
			lock_basic_unlock(&item->queue->lock);
			free(item);
			dtio->io_list_iter = NULL;
			return;
		}
		prev = item;
		item = item->next;
	}
}

/** pick a message from the queue, the routine locks and unlocks,
 * returns true if there is a message */
static int dt_msg_queue_pop(struct dt_msg_queue* mq, void** buf,
	size_t* len)
{
	lock_basic_lock(&mq->lock);
	if(mq->first) {
		struct dt_msg_entry* entry = mq->first;
		mq->first = entry->next;
		if(!entry->next) mq->last = NULL;
		mq->cursize -= entry->len;
		mq->msgcount --;
		lock_basic_unlock(&mq->lock);

		*buf = entry->buf;
		*len = entry->len;
		free(entry);
		return 1;
	}
	lock_basic_unlock(&mq->lock);
	return 0;
}

/** find message in queue, false if no message, true if message to send */
static int dtio_find_in_queue(struct dt_io_thread* dtio,
	struct dt_msg_queue* mq)
{
	void* buf=NULL;
	size_t len=0;
	if(dt_msg_queue_pop(mq, &buf, &len)) {
		dtio->cur_msg = buf;
		dtio->cur_msg_len = len;
		dtio->cur_msg_done = 0;
		dtio->cur_msg_len_done = 0;
		return 1;
	}
	return 0;
}

/** find a new message to write, search message queues, false if none */
static int dtio_find_msg(struct dt_io_thread* dtio)
{
	struct dt_io_list_item *spot, *item;

	spot = dtio->io_list_iter;
	/* use the next queue for the next message lookup,
	 * if we hit the end(NULL) the NULL restarts the iter at start. */
	if(spot)
		dtio->io_list_iter = spot->next;
	else if(dtio->io_list)
		dtio->io_list_iter = dtio->io_list->next;

	/* scan from spot to end-of-io_list */
	item = spot;
	while(item) {
		if(dtio_find_in_queue(dtio, item->queue))
			return 1;
		item = item->next;
	}
	/* scan starting at the start-of-list (to wrap around the end) */
	item = dtio->io_list;
	while(item) {
		if(dtio_find_in_queue(dtio, item->queue))
			return 1;
		item = item->next;
	}
	return 0;
}

/** callback for the dnstap reconnect, to start reconnecting to output */
void dtio_reconnect_timeout_cb(int ATTR_UNUSED(fd),
	short ATTR_UNUSED(bits), void* arg)
{
	struct dt_io_thread* dtio = (struct dt_io_thread*)arg;
	dtio->reconnect_is_added = 0;
	verbose(VERB_ALGO, "dnstap io: reconnect timer");

	dtio_open_output(dtio);
	if(dtio->event) {
		if(!dtio_add_output_event_write(dtio))
			return;
		/* nothing wrong so far, wait on the output event */
		return;
	}
	/* exponential backoff and retry on timer */
	dtio_reconnect_enable(dtio);
}

/** attempt to reconnect to the output, after a timeout */
static void dtio_reconnect_enable(struct dt_io_thread* dtio)
{
	struct timeval tv;
	int msec;
	if(dtio->want_to_exit) return;
	if(dtio->reconnect_is_added)
		return; /* already done */

	/* exponential backoff, store the value for next timeout */
	msec = dtio->reconnect_timeout;
	if(msec == 0) {
		dtio->reconnect_timeout = DTIO_RECONNECT_TIMEOUT_MIN;
	} else {
		dtio->reconnect_timeout = msec*2;
		if(dtio->reconnect_timeout > DTIO_RECONNECT_TIMEOUT_MAX)
			dtio->reconnect_timeout = DTIO_RECONNECT_TIMEOUT_MAX;
	}
	verbose(VERB_ALGO, "dnstap io: set reconnect attempt after %d msec",
		msec);

	/* setup wait timer */
	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = msec/1000;
	tv.tv_usec = (msec%1000)*1000;
	if(ub_timer_add(dtio->reconnect_timer, dtio->event_base,
		&dtio_reconnect_timeout_cb, dtio, &tv) != 0) {
		log_err("dnstap io: could not reconnect ev timer add");
		return;
	}
	dtio->reconnect_is_added = 1;
}

/** remove dtio reconnect timer */
static void dtio_reconnect_del(struct dt_io_thread* dtio)
{
	if(!dtio->reconnect_is_added)
		return;
	ub_timer_del(dtio->reconnect_timer);
	dtio->reconnect_is_added = 0;
}

/** clear the reconnect exponential backoff timer.
 * We have successfully connected so we can try again with short timeouts. */
static void dtio_reconnect_clear(struct dt_io_thread* dtio)
{
	dtio->reconnect_timeout = 0;
	dtio_reconnect_del(dtio);
}

/** reconnect slowly, because we already know we have to wait for a bit */
static void dtio_reconnect_slow(struct dt_io_thread* dtio, int msec)
{
	dtio_reconnect_del(dtio);
	dtio->reconnect_timeout = msec;
	dtio_reconnect_enable(dtio);
}

/** delete the current message in the dtio, and reset counters */
static void dtio_cur_msg_free(struct dt_io_thread* dtio)
{
	free(dtio->cur_msg);
	dtio->cur_msg = NULL;
	dtio->cur_msg_len = 0;
	dtio->cur_msg_done = 0;
	dtio->cur_msg_len_done = 0;
}

/** delete the buffer and counters used to read frame */
static void dtio_read_frame_free(struct dt_frame_read_buf* rb)
{
	if(rb->buf) {
		free(rb->buf);
		rb->buf = NULL;
	}
	rb->buf_count = 0;
	rb->buf_cap = 0;
	rb->frame_len = 0;
	rb->frame_len_done = 0;
	rb->control_frame = 0;
}

/** del the output file descriptor event for listening */
static void dtio_del_output_event(struct dt_io_thread* dtio)
{
	if(!dtio->event_added)
		return;
	ub_event_del(dtio->event);
	dtio->event_added = 0;
	dtio->event_added_is_write = 0;
}

/** close dtio socket and set it to -1 */
static void dtio_close_fd(struct dt_io_thread* dtio)
{
	sock_close(dtio->fd);
	dtio->fd = -1;
}

/** close and stop the output file descriptor event */
static void dtio_close_output(struct dt_io_thread* dtio)
{
	if(!dtio->event)
		return;
	ub_event_free(dtio->event);
	dtio->event = NULL;
	if(dtio->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(dtio->ssl);
		SSL_free(dtio->ssl);
		dtio->ssl = NULL;
#endif
	}
	dtio_close_fd(dtio);

	/* if there is a (partial) message, discard it
	 * we cannot send (the remainder of) it, and a new
	 * connection needs to start with a control frame. */
	if(dtio->cur_msg) {
		dtio_cur_msg_free(dtio);
	}

	dtio->ready_frame_sent = 0;
	dtio->accept_frame_received = 0;
	dtio_read_frame_free(&dtio->read_frame);

	dtio_reconnect_enable(dtio);
}

/** check for pending nonblocking connect errors,
 * returns 1 if it is okay. -1 on error (close it), 0 to try later */
static int dtio_check_nb_connect(struct dt_io_thread* dtio)
{
	int error = 0;
	socklen_t len = (socklen_t)sizeof(error);
	if(!dtio->check_nb_connect)
		return 1; /* everything okay */
	if(getsockopt(dtio->fd, SOL_SOCKET, SO_ERROR, (void*)&error,
		&len) < 0) {
#ifndef USE_WINSOCK
		error = errno; /* on solaris errno is error */
#else
		error = WSAGetLastError();
#endif
	}
#ifndef USE_WINSOCK
#if defined(EINPROGRESS) && defined(EWOULDBLOCK)
	if(error == EINPROGRESS || error == EWOULDBLOCK)
		return 0; /* try again later */
#endif
#else
	if(error == WSAEINPROGRESS) {
		return 0; /* try again later */
	} else if(error == WSAEWOULDBLOCK) {
		ub_winsock_tcp_wouldblock((dtio->stop_flush_event?
			dtio->stop_flush_event:dtio->event), UB_EV_WRITE);
		return 0; /* try again later */
	}
#endif
	if(error != 0) {
		char* to = dtio->socket_path;
		if(!to) to = dtio->ip_str;
		if(!to) to = "";
		log_err("dnstap io: failed to connect to \"%s\": %s",
			to, sock_strerror(error));
		return -1; /* error, close it */
	}

	if(dtio->ip_str)
		verbose(VERB_DETAIL, "dnstap io: connected to %s",
			dtio->ip_str);
	else if(dtio->socket_path)
		verbose(VERB_DETAIL, "dnstap io: connected to \"%s\"",
			dtio->socket_path);
	dtio_reconnect_clear(dtio);
	dtio->check_nb_connect = 0;
	return 1; /* everything okay */
}

#ifdef HAVE_SSL
/** write to ssl output
 * returns number of bytes written, 0 if nothing happened,
 * try again later, or -1 if the channel is to be closed. */
static int dtio_write_ssl(struct dt_io_thread* dtio, uint8_t* buf,
	size_t len)
{
	int r;
	ERR_clear_error();
	r = SSL_write(dtio->ssl, buf, len);
	if(r <= 0) {
		int want = SSL_get_error(dtio->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			/* closed */
			return -1;
		} else if(want == SSL_ERROR_WANT_READ) {
			/* we want a brief read event */
			dtio_enable_brief_read(dtio);
			return 0;
		} else if(want == SSL_ERROR_WANT_WRITE) {
			/* write again later */
			return 0;
		} else if(want == SSL_ERROR_SYSCALL) {
#ifdef EPIPE
			if(errno == EPIPE && verbosity < 2)
				return -1; /* silence 'broken pipe' */
#endif
#ifdef ECONNRESET
			if(errno == ECONNRESET && verbosity < 2)
				return -1; /* silence reset by peer */
#endif
			if(errno != 0) {
				log_err("dnstap io, SSL_write syscall: %s",
					strerror(errno));
			}
			return -1;
		}
		log_crypto_err_io("dnstap io, could not SSL_write", want);
		return -1;
	}
	return r;
}
#endif /* HAVE_SSL */

/** write buffer to output.
 * returns number of bytes written, 0 if nothing happened,
 * try again later, or -1 if the channel is to be closed. */
static int dtio_write_buf(struct dt_io_thread* dtio, uint8_t* buf,
	size_t len)
{
	ssize_t ret;
	if(dtio->fd == -1)
		return -1;
#ifdef HAVE_SSL
	if(dtio->ssl)
		return dtio_write_ssl(dtio, buf, len);
#endif
	ret = send(dtio->fd, (void*)buf, len, 0);
	if(ret == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 0;
#else
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 0;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock((dtio->stop_flush_event?
				dtio->stop_flush_event:dtio->event),
				UB_EV_WRITE);
			return 0;
		}
#endif
		log_err("dnstap io: failed send: %s", sock_strerror(errno));
		return -1;
	}
	return ret;
}

#ifdef HAVE_WRITEV
/** write with writev, len and message, in one write, if possible.
 * return true if message is done, false if incomplete */
static int dtio_write_with_writev(struct dt_io_thread* dtio)
{
	uint32_t sendlen = htonl(dtio->cur_msg_len);
	struct iovec iov[2];
	ssize_t r;
	iov[0].iov_base = ((uint8_t*)&sendlen)+dtio->cur_msg_len_done;
	iov[0].iov_len = sizeof(sendlen)-dtio->cur_msg_len_done;
	iov[1].iov_base = dtio->cur_msg;
	iov[1].iov_len = dtio->cur_msg_len;
	log_assert(iov[0].iov_len > 0);
	r = writev(dtio->fd, iov, 2);
	if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 0;
#else
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 0;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock((dtio->stop_flush_event?
				dtio->stop_flush_event:dtio->event),
				UB_EV_WRITE);
			return 0;
		}
#endif
		log_err("dnstap io: failed writev: %s", sock_strerror(errno));
		/* close the channel */
		dtio_del_output_event(dtio);
		dtio_close_output(dtio);
		return 0;
	}
	/* written r bytes */
	dtio->cur_msg_len_done += r;
	if(dtio->cur_msg_len_done < 4)
		return 0;
	if(dtio->cur_msg_len_done > 4) {
		dtio->cur_msg_done = dtio->cur_msg_len_done-4;
		dtio->cur_msg_len_done = 4;
	}
	if(dtio->cur_msg_done < dtio->cur_msg_len)
		return 0;
	return 1;
}
#endif /* HAVE_WRITEV */

/** write more of the length, preceding the data frame.
 * return true if message is done, false if incomplete. */
static int dtio_write_more_of_len(struct dt_io_thread* dtio)
{
	uint32_t sendlen;
	int r;
	if(dtio->cur_msg_len_done >= 4)
		return 1;
#ifdef HAVE_WRITEV
	if(!dtio->ssl) {
		/* we try writev for everything.*/
		return dtio_write_with_writev(dtio);
	}
#endif /* HAVE_WRITEV */
	sendlen = htonl(dtio->cur_msg_len);
	r = dtio_write_buf(dtio,
		((uint8_t*)&sendlen)+dtio->cur_msg_len_done,
		sizeof(sendlen)-dtio->cur_msg_len_done);
	if(r == -1) {
		/* close the channel */
		dtio_del_output_event(dtio);
		dtio_close_output(dtio);
		return 0;
	} else if(r == 0) {
		/* try again later */
		return 0;
	}
	dtio->cur_msg_len_done += r;
	if(dtio->cur_msg_len_done < 4)
		return 0;
	return 1;
}

/** write more of the data frame.
 * return true if message is done, false if incomplete. */
static int dtio_write_more_of_data(struct dt_io_thread* dtio)
{
	int r;
	if(dtio->cur_msg_done >= dtio->cur_msg_len)
		return 1;
	r = dtio_write_buf(dtio,
		((uint8_t*)dtio->cur_msg)+dtio->cur_msg_done,
		dtio->cur_msg_len - dtio->cur_msg_done);
	if(r == -1) {
		/* close the channel */
		dtio_del_output_event(dtio);
		dtio_close_output(dtio);
		return 0;
	} else if(r == 0) {
		/* try again later */
		return 0;
	}
	dtio->cur_msg_done += r;
	if(dtio->cur_msg_done < dtio->cur_msg_len)
		return 0;
	return 1;
}

/** write more of the current message. false if incomplete, true if
 * the message is done */
static int dtio_write_more(struct dt_io_thread* dtio)
{
	if(dtio->cur_msg_len_done < 4) {
		if(!dtio_write_more_of_len(dtio))
			return 0;
	}
	if(dtio->cur_msg_done < dtio->cur_msg_len) {
		if(!dtio_write_more_of_data(dtio))
			return 0;
	}
	return 1;
}

/** Receive bytes from dtio->fd, store in buffer. Returns 0: closed,
 * -1: continue, >0: number of bytes read into buffer */
static ssize_t receive_bytes(struct dt_io_thread* dtio, void* buf, size_t len) {
	ssize_t r;
	r = recv(dtio->fd, (void*)buf, len, MSG_DONTWAIT);
	if(r == -1) {
		char* to = dtio->socket_path;
		if(!to) to = dtio->ip_str;
		if(!to) to = "";
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return -1; /* try later */
#else
		if(WSAGetLastError() == WSAEINPROGRESS) {
			return -1; /* try later */
		} else if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(
				(dtio->stop_flush_event?
				dtio->stop_flush_event:dtio->event),
				UB_EV_READ);
			return -1; /* try later */
		}
#endif
		if(dtio->reconnect_timeout > DTIO_RECONNECT_TIMEOUT_MIN &&
			verbosity < 4)
			return 0; /* no log retries on low verbosity */
		log_err("dnstap io: output closed, recv %s: %s", to,
			strerror(errno));
		/* and close below */
		return 0;
	}
	if(r == 0) {
		if(dtio->reconnect_timeout > DTIO_RECONNECT_TIMEOUT_MIN &&
			verbosity < 4)
			return 0; /* no log retries on low verbosity */
		verbose(VERB_DETAIL, "dnstap io: output closed by the other side");
		/* and close below */
		return 0;
	}
	/* something was received */
	return r;
}

#ifdef HAVE_SSL
/** Receive bytes over TLS from dtio->fd, store in buffer. Returns 0: closed,
 * -1: continue, >0: number of bytes read into buffer */
static int ssl_read_bytes(struct dt_io_thread* dtio, void* buf, size_t len)
{
	int r;
	ERR_clear_error();
	r = SSL_read(dtio->ssl, buf, len);
	if(r <= 0) {
		int want = SSL_get_error(dtio->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			if(dtio->reconnect_timeout > DTIO_RECONNECT_TIMEOUT_MIN &&
				verbosity < 4)
				return 0; /* no log retries on low verbosity */
			verbose(VERB_DETAIL, "dnstap io: output closed by the "
				"other side");
			return 0;
		} else if(want == SSL_ERROR_WANT_READ) {
			/* continue later */
			return -1;
		} else if(want == SSL_ERROR_WANT_WRITE) {
			(void)dtio_enable_brief_write(dtio);
			return -1;
		} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
			if(dtio->reconnect_timeout > DTIO_RECONNECT_TIMEOUT_MIN &&
				errno == ECONNRESET && verbosity < 4)
				return 0; /* silence reset by peer */
#endif
			if(errno != 0)
				log_err("SSL_read syscall: %s",
					strerror(errno));
			verbose(VERB_DETAIL, "dnstap io: output closed by the "
				"other side");
			return 0;
		}
		log_crypto_err_io("could not SSL_read", want);
		verbose(VERB_DETAIL, "dnstap io: output closed by the "
				"other side");
		return 0;
	}
	return r;
}
#endif /* HAVE_SSL */

/** check if the output fd has been closed,
 * it returns false if the stream is closed. */
static int dtio_check_close(struct dt_io_thread* dtio)
{
	/* we don't want to read any packets, but if there are we can
	 * discard the input (ignore it).  Ignore of unknown (control)
	 * packets is okay for the framestream protocol.  And also, the
	 * read call can return that the stream has been closed by the
	 * other side. */
	uint8_t buf[1024];
	int r = -1;


	if(dtio->fd == -1) return 0;

	while(r != 0) {
		/* not interested in buffer content, overwrite */
		r = receive_bytes(dtio, (void*)buf, sizeof(buf));
		if(r == -1)
			return 1;
	}
	/* the other end has been closed */
	/* close the channel */
	dtio_del_output_event(dtio);
	dtio_close_output(dtio);
	return 0;
}

/** Read accept frame. Returns -1: continue reading, 0: closed,
 * 1: valid accept received. */
static int dtio_read_accept_frame(struct dt_io_thread* dtio)
{
	int r;
	size_t read_frame_done;
	while(dtio->read_frame.frame_len_done < 4) {
#ifdef HAVE_SSL
		if(dtio->ssl) {
			r = ssl_read_bytes(dtio,
				(uint8_t*)&dtio->read_frame.frame_len+
				dtio->read_frame.frame_len_done,
				4-dtio->read_frame.frame_len_done);
		} else {
#endif
			r = receive_bytes(dtio,
				(uint8_t*)&dtio->read_frame.frame_len+
				dtio->read_frame.frame_len_done,
				4-dtio->read_frame.frame_len_done);
#ifdef HAVE_SSL
		}
#endif
		if(r == -1)
			return -1; /* continue reading */
		if(r == 0) {
			 /* connection closed */
			goto close_connection;
		}
		dtio->read_frame.frame_len_done += r;
		if(dtio->read_frame.frame_len_done < 4)
			return -1; /* continue reading */

		if(dtio->read_frame.frame_len == 0) {
			dtio->read_frame.frame_len_done = 0;
			dtio->read_frame.control_frame = 1;
			continue;
		}
		dtio->read_frame.frame_len = ntohl(dtio->read_frame.frame_len);
		if(dtio->read_frame.frame_len > DTIO_RECV_FRAME_MAX_LEN) {
			verbose(VERB_OPS, "dnstap: received frame exceeds max "
				"length of %d bytes, closing connection",
				DTIO_RECV_FRAME_MAX_LEN);
			goto close_connection;
		}
		dtio->read_frame.buf = calloc(1, dtio->read_frame.frame_len);
		dtio->read_frame.buf_cap = dtio->read_frame.frame_len;
		if(!dtio->read_frame.buf) {
			log_err("dnstap io: out of memory (creating read "
				"buffer)");
			goto close_connection;
		}
	}
	if(dtio->read_frame.buf_count < dtio->read_frame.frame_len) {
#ifdef HAVE_SSL
		if(dtio->ssl) {
			r = ssl_read_bytes(dtio, dtio->read_frame.buf+
				dtio->read_frame.buf_count,
				dtio->read_frame.buf_cap-
				dtio->read_frame.buf_count);
		} else {
#endif
			r = receive_bytes(dtio, dtio->read_frame.buf+
				dtio->read_frame.buf_count,
				dtio->read_frame.buf_cap-
				dtio->read_frame.buf_count);
#ifdef HAVE_SSL
		}
#endif
		if(r == -1)
			return -1; /* continue reading */
		if(r == 0) {
			 /* connection closed */
			goto close_connection;
		}
		dtio->read_frame.buf_count += r;
		if(dtio->read_frame.buf_count < dtio->read_frame.frame_len)
			return -1; /* continue reading */
	}

	/* Complete frame received, check if this is a valid ACCEPT control
	 * frame. */
	if(dtio->read_frame.frame_len < 4) {
		verbose(VERB_OPS, "dnstap: invalid data received");
		goto close_connection;
	}
	if(sldns_read_uint32(dtio->read_frame.buf) !=
		FSTRM_CONTROL_FRAME_ACCEPT) {
		verbose(VERB_ALGO, "dnstap: invalid control type received, "
			"ignored");
		dtio->ready_frame_sent = 0;
		dtio->accept_frame_received = 0;
		dtio_read_frame_free(&dtio->read_frame);
		return -1;
	}
	read_frame_done = 4; /* control frame type */

	/* Iterate over control fields, ignore unknown types.
	 * Need to be able to read at least 8 bytes (control field type +
	 * length). */
	while(read_frame_done+8 < dtio->read_frame.frame_len) {
		uint32_t type = sldns_read_uint32(dtio->read_frame.buf +
			read_frame_done);
		uint32_t len = sldns_read_uint32(dtio->read_frame.buf +
			read_frame_done + 4);
		if(type == FSTRM_CONTROL_FIELD_TYPE_CONTENT_TYPE) {
			if(len == strlen(DNSTAP_CONTENT_TYPE) &&
				read_frame_done+8+len <=
				dtio->read_frame.frame_len &&
				memcmp(dtio->read_frame.buf + read_frame_done +
					+ 8, DNSTAP_CONTENT_TYPE, len) == 0) {
				if(!dtio_control_start_send(dtio)) {
					verbose(VERB_OPS, "dnstap io: out of "
					 "memory while sending START frame");
					goto close_connection;
				}
				dtio->accept_frame_received = 1;
				if(!dtio_add_output_event_write(dtio))
					goto close_connection;
				return 1;
			} else {
				/* unknown content type */
				verbose(VERB_ALGO, "dnstap: ACCEPT frame "
					"contains unknown content type, "
					"closing connection");
				goto close_connection;
			}
		}
		/* unknown option, try next */
		read_frame_done += 8+len;
	}


close_connection:
	dtio_del_output_event(dtio);
	dtio_reconnect_slow(dtio, DTIO_RECONNECT_TIMEOUT_SLOW);
	dtio_close_output(dtio);
	return 0;
}

/** add the output file descriptor event for listening, read only */
static int dtio_add_output_event_read(struct dt_io_thread* dtio)
{
	if(!dtio->event)
		return 0;
	if(dtio->event_added && !dtio->event_added_is_write)
		return 1;
	/* we have to (re-)register the event */
	if(dtio->event_added)
		ub_event_del(dtio->event);
	ub_event_del_bits(dtio->event, UB_EV_WRITE);
	if(ub_event_add(dtio->event, NULL) != 0) {
		log_err("dnstap io: out of memory (adding event)");
		dtio->event_added = 0;
		dtio->event_added_is_write = 0;
		/* close output and start reattempts to open it */
		dtio_close_output(dtio);
		return 0;
	}
	dtio->event_added = 1;
	dtio->event_added_is_write = 0;
	return 1;
}

/** add the output file descriptor event for listening, read and write */
static int dtio_add_output_event_write(struct dt_io_thread* dtio)
{
	if(!dtio->event)
		return 0;
	if(dtio->event_added && dtio->event_added_is_write)
		return 1;
	/* we have to (re-)register the event */
	if(dtio->event_added)
		ub_event_del(dtio->event);
	ub_event_add_bits(dtio->event, UB_EV_WRITE);
	if(ub_event_add(dtio->event, NULL) != 0) {
		log_err("dnstap io: out of memory (adding event)");
		dtio->event_added = 0;
		dtio->event_added_is_write = 0;
		/* close output and start reattempts to open it */
		dtio_close_output(dtio);
		return 0;
	}
	dtio->event_added = 1;
	dtio->event_added_is_write = 1;
	return 1;
}

/** put the dtio thread to sleep */
static void dtio_sleep(struct dt_io_thread* dtio)
{
	/* unregister the event polling for write, because there is
	 * nothing to be written */
	(void)dtio_add_output_event_read(dtio);

	/* Set wakeuptimer enabled off; so that the next worker thread that
	 * wants to log starts a timer if needed, since the writer thread
	 * has gone to sleep. */
	lock_basic_lock(&dtio->wakeup_timer_lock);
	dtio->wakeup_timer_enabled = 0;
	lock_basic_unlock(&dtio->wakeup_timer_lock);
}

#ifdef HAVE_SSL
/** enable the brief read condition */
static int dtio_enable_brief_read(struct dt_io_thread* dtio)
{
	dtio->ssl_brief_read = 1;
	if(dtio->stop_flush_event) {
		ub_event_del(dtio->stop_flush_event);
		ub_event_del_bits(dtio->stop_flush_event, UB_EV_WRITE);
		if(ub_event_add(dtio->stop_flush_event, NULL) != 0) {
			log_err("dnstap io, stop flush, could not ub_event_add");
			return 0;
		}
		return 1;
	}
	return dtio_add_output_event_read(dtio);
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/** disable the brief read condition */
static int dtio_disable_brief_read(struct dt_io_thread* dtio)
{
	dtio->ssl_brief_read = 0;
	if(dtio->stop_flush_event) {
		ub_event_del(dtio->stop_flush_event);
		ub_event_add_bits(dtio->stop_flush_event, UB_EV_WRITE);
		if(ub_event_add(dtio->stop_flush_event, NULL) != 0) {
			log_err("dnstap io, stop flush, could not ub_event_add");
			return 0;
		}
		return 1;
	}
	return dtio_add_output_event_write(dtio);
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/** enable the brief write condition */
static int dtio_enable_brief_write(struct dt_io_thread* dtio)
{
	dtio->ssl_brief_write = 1;
	return dtio_add_output_event_write(dtio);
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/** disable the brief write condition */
static int dtio_disable_brief_write(struct dt_io_thread* dtio)
{
	dtio->ssl_brief_write = 0;
	return dtio_add_output_event_read(dtio);
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/** check peer verification after ssl handshake connection, false if closed*/
static int dtio_ssl_check_peer(struct dt_io_thread* dtio)
{
	if((SSL_get_verify_mode(dtio->ssl)&SSL_VERIFY_PEER)) {
		/* verification */
		if(SSL_get_verify_result(dtio->ssl) == X509_V_OK) {
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
			X509* x = SSL_get1_peer_certificate(dtio->ssl);
#else
			X509* x = SSL_get_peer_certificate(dtio->ssl);
#endif
			if(!x) {
				verbose(VERB_ALGO, "dnstap io, %s, SSL "
					"connection failed no certificate",
					dtio->ip_str);
				return 0;
			}
			log_cert(VERB_ALGO, "dnstap io, peer certificate",
				x);
#ifdef HAVE_SSL_GET0_PEERNAME
			if(SSL_get0_peername(dtio->ssl)) {
				verbose(VERB_ALGO, "dnstap io, %s, SSL "
					"connection to %s authenticated",
					dtio->ip_str,
					SSL_get0_peername(dtio->ssl));
			} else {
#endif
				verbose(VERB_ALGO, "dnstap io, %s, SSL "
					"connection authenticated",
					dtio->ip_str);
#ifdef HAVE_SSL_GET0_PEERNAME
			}
#endif
			X509_free(x);
		} else {
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
			X509* x = SSL_get1_peer_certificate(dtio->ssl);
#else
			X509* x = SSL_get_peer_certificate(dtio->ssl);
#endif
			if(x) {
				log_cert(VERB_ALGO, "dnstap io, peer "
					"certificate", x);
				X509_free(x);
			}
			verbose(VERB_ALGO, "dnstap io, %s, SSL connection "
				"failed: failed to authenticate",
				dtio->ip_str);
			return 0;
		}
	} else {
		/* unauthenticated, the verify peer flag was not set
		 * in ssl when the ssl object was created from ssl_ctx */
		verbose(VERB_ALGO, "dnstap io, %s, SSL connection",
			dtio->ip_str);
	}
	return 1;
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/** perform ssl handshake, returns 1 if okay, 0 to stop */
static int dtio_ssl_handshake(struct dt_io_thread* dtio,
	struct stop_flush_info* info)
{
	int r;
	if(dtio->ssl_brief_read) {
		/* assume the brief read condition is satisfied,
		 * if we need more or again, we can set it again */
		if(!dtio_disable_brief_read(dtio)) {
			if(info) dtio_stop_flush_exit(info);
			return 0;
		}
	}
	if(dtio->ssl_handshake_done)
		return 1;

	ERR_clear_error();
	r = SSL_do_handshake(dtio->ssl);
	if(r != 1) {
		int want = SSL_get_error(dtio->ssl, r);
		if(want == SSL_ERROR_WANT_READ) {
			/* we want to read on the connection */
			if(!dtio_enable_brief_read(dtio)) {
				if(info) dtio_stop_flush_exit(info);
				return 0;
			}
			return 0;
		} else if(want == SSL_ERROR_WANT_WRITE) {
			/* we want to write on the connection */
			return 0;
		} else if(r == 0) {
			/* closed */
			if(info) dtio_stop_flush_exit(info);
			dtio_del_output_event(dtio);
			dtio_reconnect_slow(dtio, DTIO_RECONNECT_TIMEOUT_SLOW);
			dtio_close_output(dtio);
			return 0;
		} else if(want == SSL_ERROR_SYSCALL) {
			/* SYSCALL and errno==0 means closed uncleanly */
			int silent = 0;
#ifdef EPIPE
			if(errno == EPIPE && verbosity < 2)
				silent = 1; /* silence 'broken pipe' */
#endif
#ifdef ECONNRESET
			if(errno == ECONNRESET && verbosity < 2)
				silent = 1; /* silence reset by peer */
#endif
			if(errno == 0)
				silent = 1;
			if(!silent)
				log_err("dnstap io, SSL_handshake syscall: %s",
					strerror(errno));
			/* closed */
			if(info) dtio_stop_flush_exit(info);
			dtio_del_output_event(dtio);
			dtio_reconnect_slow(dtio, DTIO_RECONNECT_TIMEOUT_SLOW);
			dtio_close_output(dtio);
			return 0;
		} else {
			unsigned long err = ERR_get_error();
			if(!squelch_err_ssl_handshake(err)) {
				log_crypto_err_io_code("dnstap io, ssl handshake failed",
					want, err);
				verbose(VERB_OPS, "dnstap io, ssl handshake failed "
					"from %s", dtio->ip_str);
			}
			/* closed */
			if(info) dtio_stop_flush_exit(info);
			dtio_del_output_event(dtio);
			dtio_reconnect_slow(dtio, DTIO_RECONNECT_TIMEOUT_SLOW);
			dtio_close_output(dtio);
			return 0;
		}

	}
	/* check peer verification */
	dtio->ssl_handshake_done = 1;

	if(!dtio_ssl_check_peer(dtio)) {
		/* closed */
		if(info) dtio_stop_flush_exit(info);
		dtio_del_output_event(dtio);
		dtio_reconnect_slow(dtio, DTIO_RECONNECT_TIMEOUT_SLOW);
		dtio_close_output(dtio);
		return 0;
	}
	return 1;
}
#endif /* HAVE_SSL */

/** callback for the dnstap events, to write to the output */
void dtio_output_cb(int ATTR_UNUSED(fd), short bits, void* arg)
{
	struct dt_io_thread* dtio = (struct dt_io_thread*)arg;
	int i;

	if(dtio->check_nb_connect) {
		int connect_err = dtio_check_nb_connect(dtio);
		if(connect_err == -1) {
			/* close the channel */
			dtio_del_output_event(dtio);
			dtio_close_output(dtio);
			return;
		} else if(connect_err == 0) {
			/* try again later */
			return;
		}
		/* nonblocking connect check passed, continue */
	}

#ifdef HAVE_SSL
	if(dtio->ssl &&
		(!dtio->ssl_handshake_done || dtio->ssl_brief_read)) {
		if(!dtio_ssl_handshake(dtio, NULL))
			return;
	}
#endif

	if((bits&UB_EV_READ || dtio->ssl_brief_write)) {
#ifdef HAVE_SSL
		if(dtio->ssl_brief_write)
			(void)dtio_disable_brief_write(dtio);
#endif
		if(dtio->ready_frame_sent && !dtio->accept_frame_received) {
			if(dtio_read_accept_frame(dtio) <= 0)
				return;
		} else if(!dtio_check_close(dtio))
			return;
	}

	/* loop to process a number of messages.  This improves throughput,
	 * because selecting on write-event if not needed for busy messages
	 * (dnstap log) generation and if they need to all be written back.
	 * The write event is usually not blocked up.  But not forever,
	 * because the event loop needs to stay responsive for other events.
	 * If there are no (more) messages, or if the output buffers get
	 * full, it returns out of the loop. */
	for(i=0; i<DTIO_MESSAGES_PER_CALLBACK; i++) {
		/* see if there are messages that need writing */
		if(!dtio->cur_msg) {
			if(!dtio_find_msg(dtio)) {
				if(i == 0) {
					/* no messages on the first iteration,
					 * the queues are all empty */
					dtio_sleep(dtio);
					/* After putting to sleep, see if
					 * a message is in a message queue,
					 * if so, resume service. Stops a
					 * race condition where a thread could
					 * have one message but the dtio
					 * also just went to sleep. With the
					 * message queued between the
					 * dtio_find_msg and dtio_sleep
					 * calls. */
					if(dtio_find_msg(dtio)) {
						if(!dtio_add_output_event_write(dtio))
							return;
					}
				}
				if(!dtio->cur_msg)
				return; /* nothing to do */
			}
		}

		/* write it */
		if(dtio->cur_msg_done < dtio->cur_msg_len) {
			if(!dtio_write_more(dtio))
				return;
		}

		/* done with the current message */
		dtio_cur_msg_free(dtio);

		/* If this is a bidirectional stream the first message will be
		 * the READY control frame. We can only continue writing after
		 * receiving an ACCEPT control frame. */
		if(dtio->is_bidirectional && !dtio->ready_frame_sent) {
			dtio->ready_frame_sent = 1;
			(void)dtio_add_output_event_read(dtio);
			break;
		}
	}
}

/** callback for the dnstap commandpipe, to stop the dnstap IO */
void dtio_cmd_cb(int fd, short ATTR_UNUSED(bits), void* arg)
{
	struct dt_io_thread* dtio = (struct dt_io_thread*)arg;
	uint8_t cmd;
	ssize_t r;
	if(dtio->want_to_exit)
		return;
	r = read(fd, &cmd, sizeof(cmd));
	if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return; /* ignore this */
#else
		if(WSAGetLastError() == WSAEINPROGRESS)
			return;
		if(WSAGetLastError() == WSAEWOULDBLOCK)
			return;
#endif
		log_err("dnstap io: failed to read: %s", sock_strerror(errno));
		/* and then fall through to quit the thread */
	} else if(r == 0) {
		verbose(VERB_ALGO, "dnstap io: cmd channel closed");
	} else if(r == 1 && cmd == DTIO_COMMAND_STOP) {
		verbose(VERB_ALGO, "dnstap io: cmd channel cmd quit");
	} else if(r == 1 && cmd == DTIO_COMMAND_WAKEUP) {
		verbose(VERB_ALGO, "dnstap io: cmd channel cmd wakeup");

		if(dtio->is_bidirectional && !dtio->accept_frame_received) {
			verbose(VERB_ALGO, "dnstap io: cmd wakeup ignored, "
				"waiting for ACCEPT control frame");
			return;
		}

		/* reregister event */
		if(!dtio_add_output_event_write(dtio))
			return;
		return;
	} else if(r == 1) {
		verbose(VERB_ALGO, "dnstap io: cmd channel unknown command");
	}
	dtio->want_to_exit = 1;
	if(ub_event_base_loopexit((struct ub_event_base*)dtio->event_base)
		!= 0) {
		log_err("dnstap io: could not loopexit");
	}
}

#ifndef THREADS_DISABLED
/** setup the event base for the dnstap io thread */
static void dtio_setup_base(struct dt_io_thread* dtio, time_t* secs,
	struct timeval* now)
{
	memset(now, 0, sizeof(*now));
	dtio->event_base = ub_default_event_base(0, secs, now);
	if(!dtio->event_base) {
		fatal_exit("dnstap io: could not create event_base");
	}
}
#endif /* THREADS_DISABLED */

/** setup the cmd event for dnstap io */
static void dtio_setup_cmd(struct dt_io_thread* dtio)
{
	struct ub_event* cmdev;
	fd_set_nonblock(dtio->commandpipe[0]);
	cmdev = ub_event_new(dtio->event_base, dtio->commandpipe[0],
		UB_EV_READ | UB_EV_PERSIST, &dtio_cmd_cb, dtio);
	if(!cmdev) {
		fatal_exit("dnstap io: out of memory");
	}
	dtio->command_event = cmdev;
	if(ub_event_add(cmdev, NULL) != 0) {
		fatal_exit("dnstap io: out of memory (adding event)");
	}
}

/** setup the reconnect event for dnstap io */
static void dtio_setup_reconnect(struct dt_io_thread* dtio)
{
	dtio_reconnect_clear(dtio);
	dtio->reconnect_timer = ub_event_new(dtio->event_base, -1,
		UB_EV_TIMEOUT, &dtio_reconnect_timeout_cb, dtio);
	if(!dtio->reconnect_timer) {
		fatal_exit("dnstap io: out of memory");
	}
}

/**
 * structure to keep track of information during stop flush
 */
struct stop_flush_info {
	/** the event base during stop flush */
	struct ub_event_base* base;
	/** did we already want to exit this stop-flush event base */
	int want_to_exit_flush;
	/** has the timer fired */
	int timer_done;
	/** the dtio */
	struct dt_io_thread* dtio;
	/** the stop control frame */
	void* stop_frame;
	/** length of the stop frame */
	size_t stop_frame_len;
	/** how much we have done of the stop frame */
	size_t stop_frame_done;
};

/** exit the stop flush base */
static void dtio_stop_flush_exit(struct stop_flush_info* info)
{
	if(info->want_to_exit_flush)
		return;
	info->want_to_exit_flush = 1;
	if(ub_event_base_loopexit(info->base) != 0) {
		log_err("dnstap io: could not loopexit");
	}
}

/** send the stop control,
 * return true if completed the frame. */
static int dtio_control_stop_send(struct stop_flush_info* info)
{
	struct dt_io_thread* dtio = info->dtio;
	int r;
	if(info->stop_frame_done >= info->stop_frame_len)
		return 1;
	r = dtio_write_buf(dtio, ((uint8_t*)info->stop_frame) +
		info->stop_frame_done, info->stop_frame_len -
		info->stop_frame_done);
	if(r == -1) {
		verbose(VERB_ALGO, "dnstap io: stop flush: output closed");
		dtio_stop_flush_exit(info);
		return 0;
	}
	if(r == 0) {
		/* try again later, or timeout */
		return 0;
	}
	info->stop_frame_done += r;
	if(info->stop_frame_done < info->stop_frame_len)
		return 0; /* not done yet */
	return 1;
}

void dtio_stop_timer_cb(int ATTR_UNUSED(fd), short ATTR_UNUSED(bits),
	void* arg)
{
	struct stop_flush_info* info = (struct stop_flush_info*)arg;
	if(info->want_to_exit_flush)
		return;
	verbose(VERB_ALGO, "dnstap io: stop flush timer expired, stop flush");
	info->timer_done = 1;
	dtio_stop_flush_exit(info);
}

void dtio_stop_ev_cb(int ATTR_UNUSED(fd), short bits, void* arg)
{
	struct stop_flush_info* info = (struct stop_flush_info*)arg;
	struct dt_io_thread* dtio = info->dtio;
	if(info->want_to_exit_flush)
		return;
	if(dtio->check_nb_connect) {
		/* we don't start the stop_flush if connect still
		 * in progress, but the check code is here, just in case */
		int connect_err = dtio_check_nb_connect(dtio);
		if(connect_err == -1) {
			/* close the channel, exit the stop flush */
			dtio_stop_flush_exit(info);
			dtio_del_output_event(dtio);
			dtio_close_output(dtio);
			return;
		} else if(connect_err == 0) {
			/* try again later */
			return;
		}
		/* nonblocking connect check passed, continue */
	}
#ifdef HAVE_SSL
	if(dtio->ssl &&
		(!dtio->ssl_handshake_done || dtio->ssl_brief_read)) {
		if(!dtio_ssl_handshake(dtio, info))
			return;
	}
#endif

	if((bits&UB_EV_READ)) {
		if(!dtio_check_close(dtio)) {
			if(dtio->fd == -1) {
				verbose(VERB_ALGO, "dnstap io: "
					"stop flush: output closed");
				dtio_stop_flush_exit(info);
			}
			return;
		}
	}
	/* write remainder of last frame */
	if(dtio->cur_msg) {
		if(dtio->cur_msg_done < dtio->cur_msg_len) {
			if(!dtio_write_more(dtio)) {
				if(dtio->fd == -1) {
					verbose(VERB_ALGO, "dnstap io: "
						"stop flush: output closed");
					dtio_stop_flush_exit(info);
				}
				return;
			}
		}
		verbose(VERB_ALGO, "dnstap io: stop flush completed "
			"last frame");
		dtio_cur_msg_free(dtio);
	}
	/* write stop frame */
	if(info->stop_frame_done < info->stop_frame_len) {
		if(!dtio_control_stop_send(info))
			return;
		verbose(VERB_ALGO, "dnstap io: stop flush completed "
			"stop control frame");
	}
	/* when last frame and stop frame are sent, exit */
	dtio_stop_flush_exit(info);
}

/** flush at end, last packet and stop control */
static void dtio_control_stop_flush(struct dt_io_thread* dtio)
{
	/* briefly attempt to flush the previous packet to the output,
	 * this could be a partial packet, or even the start control frame */
	time_t secs = 0;
	struct timeval now;
	struct stop_flush_info info;
	struct timeval tv;
	struct ub_event* timer, *stopev;

	if(dtio->fd == -1 || dtio->check_nb_connect) {
		/* no connection or we have just connected, so nothing is
		 * sent yet, so nothing to stop or flush */
		return;
	}
	if(dtio->ssl && !dtio->ssl_handshake_done) {
		/* no SSL connection has been established yet */
		return;
	}

	memset(&info, 0, sizeof(info));
	memset(&now, 0, sizeof(now));
	info.dtio = dtio;
	info.base = ub_default_event_base(0, &secs, &now);
	if(!info.base) {
		log_err("dnstap io: malloc failure");
		return;
	}
	timer = ub_event_new(info.base, -1, UB_EV_TIMEOUT,
		&dtio_stop_timer_cb, &info);
	if(!timer) {
		log_err("dnstap io: malloc failure");
		ub_event_base_free(info.base);
		return;
	}
	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = 2;
	if(ub_timer_add(timer, info.base, &dtio_stop_timer_cb, &info,
		&tv) != 0) {
		log_err("dnstap io: cannot event_timer_add");
		ub_event_free(timer);
		ub_event_base_free(info.base);
		return;
	}
	stopev = ub_event_new(info.base, dtio->fd, UB_EV_READ |
		UB_EV_WRITE | UB_EV_PERSIST, &dtio_stop_ev_cb, &info);
	if(!stopev) {
		log_err("dnstap io: malloc failure");
		ub_timer_del(timer);
		ub_event_free(timer);
		ub_event_base_free(info.base);
		return;
	}
	if(ub_event_add(stopev, NULL) != 0) {
		log_err("dnstap io: cannot event_add");
		ub_event_free(stopev);
		ub_timer_del(timer);
		ub_event_free(timer);
		ub_event_base_free(info.base);
		return;
	}
	info.stop_frame = fstrm_create_control_frame_stop(
		&info.stop_frame_len);
	if(!info.stop_frame) {
		log_err("dnstap io: malloc failure");
		ub_event_del(stopev);
		ub_event_free(stopev);
		ub_timer_del(timer);
		ub_event_free(timer);
		ub_event_base_free(info.base);
		return;
	}
	dtio->stop_flush_event = stopev;

	/* wait briefly, or until finished */
	verbose(VERB_ALGO, "dnstap io: stop flush started");
	if(ub_event_base_dispatch(info.base) < 0) {
		log_err("dnstap io: dispatch flush failed, errno is %s",
			strerror(errno));
	}
	verbose(VERB_ALGO, "dnstap io: stop flush ended");
	free(info.stop_frame);
	dtio->stop_flush_event = NULL;
	ub_event_del(stopev);
	ub_event_free(stopev);
	ub_timer_del(timer);
	ub_event_free(timer);
	ub_event_base_free(info.base);
}

/** perform desetup and free stuff when the dnstap io thread exits */
static void dtio_desetup(struct dt_io_thread* dtio)
{
	dtio_control_stop_flush(dtio);
	dtio_del_output_event(dtio);
	dtio_close_output(dtio);
	ub_event_del(dtio->command_event);
	ub_event_free(dtio->command_event);
#ifndef USE_WINSOCK
	close(dtio->commandpipe[0]);
#else
	_close(dtio->commandpipe[0]);
#endif
	dtio->commandpipe[0] = -1;
	dtio_reconnect_del(dtio);
	ub_event_free(dtio->reconnect_timer);
	dtio_cur_msg_free(dtio);
#ifndef THREADS_DISABLED
	ub_event_base_free(dtio->event_base);
#endif
}

/** setup a start control message */
static int dtio_control_start_send(struct dt_io_thread* dtio)
{
	log_assert(dtio->cur_msg == NULL && dtio->cur_msg_len == 0);
	dtio->cur_msg = fstrm_create_control_frame_start(DNSTAP_CONTENT_TYPE,
		&dtio->cur_msg_len);
	if(!dtio->cur_msg) {
		return 0;
	}
	/* setup to send the control message */
	/* set that the buffer needs to be sent, but the length
	 * of that buffer is already written, that way the buffer can
	 * start with 0 length and then the length of the control frame
	 * in it */
	dtio->cur_msg_done = 0;
	dtio->cur_msg_len_done = 4;
	return 1;
}

/** setup a ready control message */
static int dtio_control_ready_send(struct dt_io_thread* dtio)
{
	log_assert(dtio->cur_msg == NULL && dtio->cur_msg_len == 0);
	dtio->cur_msg = fstrm_create_control_frame_ready(DNSTAP_CONTENT_TYPE,
		&dtio->cur_msg_len);
	if(!dtio->cur_msg) {
		return 0;
	}
	/* setup to send the control message */
	/* set that the buffer needs to be sent, but the length
	 * of that buffer is already written, that way the buffer can
	 * start with 0 length and then the length of the control frame
	 * in it */
	dtio->cur_msg_done = 0;
	dtio->cur_msg_len_done = 4;
	return 1;
}

/** open the output file descriptor for af_local */
static int dtio_open_output_local(struct dt_io_thread* dtio)
{
#ifdef HAVE_SYS_UN_H
	struct sockaddr_un s;
	dtio->fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if(dtio->fd == -1) {
		log_err("dnstap io: failed to create socket: %s",
			sock_strerror(errno));
		return 0;
	}
	memset(&s, 0, sizeof(s));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
        /* this member exists on BSDs, not Linux */
        s.sun_len = (unsigned)sizeof(s);
#endif
	s.sun_family = AF_LOCAL;
	/* length is 92-108, 104 on FreeBSD */
        (void)strlcpy(s.sun_path, dtio->socket_path, sizeof(s.sun_path));
	fd_set_nonblock(dtio->fd);
	if(connect(dtio->fd, (struct sockaddr*)&s, (socklen_t)sizeof(s))
		== -1) {
		char* to = dtio->socket_path;
		if(dtio->reconnect_timeout > DTIO_RECONNECT_TIMEOUT_MIN &&
			verbosity < 4) {
			dtio_close_fd(dtio);
			return 0; /* no log retries on low verbosity */
		}
		log_err("dnstap io: failed to connect to \"%s\": %s",
			to, sock_strerror(errno));
		dtio_close_fd(dtio);
		return 0;
	}
	return 1;
#else
	log_err("cannot create af_local socket");
	return 0;
#endif /* HAVE_SYS_UN_H */
}

/** open the output file descriptor for af_inet and af_inet6 */
static int dtio_open_output_tcp(struct dt_io_thread* dtio)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
	memset(&addr, 0, sizeof(addr));
	addrlen = (socklen_t)sizeof(addr);

	if(!extstrtoaddr(dtio->ip_str, &addr, &addrlen, UNBOUND_DNS_PORT)) {
		log_err("could not parse IP '%s'", dtio->ip_str);
		return 0;
	}
	dtio->fd = socket(addr.ss_family, SOCK_STREAM, 0);
	if(dtio->fd == -1) {
		log_err("can't create socket: %s", sock_strerror(errno));
		return 0;
	}
	fd_set_nonblock(dtio->fd);
	if(connect(dtio->fd, (struct sockaddr*)&addr, addrlen) == -1) {
		if(errno == EINPROGRESS)
			return 1; /* wait until connect done*/
		if(dtio->reconnect_timeout > DTIO_RECONNECT_TIMEOUT_MIN &&
			verbosity < 4) {
			dtio_close_fd(dtio);
			return 0; /* no log retries on low verbosity */
		}
#ifndef USE_WINSOCK
		if(tcp_connect_errno_needs_log(
			(struct sockaddr *)&addr, addrlen)) {
			log_err("dnstap io: failed to connect to %s: %s",
				dtio->ip_str, strerror(errno));
		}
#else
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAEWOULDBLOCK)
			return 1; /* wait until connect done*/
		if(tcp_connect_errno_needs_log(
			(struct sockaddr *)&addr, addrlen)) {
			log_err("dnstap io: failed to connect to %s: %s",
				dtio->ip_str, wsa_strerror(WSAGetLastError()));
		}
#endif
		dtio_close_fd(dtio);
		return 0;
	}
	return 1;
}

/** setup the SSL structure for new connection */
static int dtio_setup_ssl(struct dt_io_thread* dtio)
{
	dtio->ssl = outgoing_ssl_fd(dtio->ssl_ctx, dtio->fd);
	if(!dtio->ssl) return 0;
	dtio->ssl_handshake_done = 0;
	dtio->ssl_brief_read = 0;

	if(!set_auth_name_on_ssl(dtio->ssl, dtio->tls_server_name,
		dtio->tls_use_sni)) {
		return 0;
	}
	return 1;
}

/** open the output file descriptor */
static void dtio_open_output(struct dt_io_thread* dtio)
{
	struct ub_event* ev;
	if(dtio->upstream_is_unix) {
		if(!dtio_open_output_local(dtio)) {
			dtio_reconnect_enable(dtio);
			return;
		}
	} else if(dtio->upstream_is_tcp || dtio->upstream_is_tls) {
		if(!dtio_open_output_tcp(dtio)) {
			dtio_reconnect_enable(dtio);
			return;
		}
		if(dtio->upstream_is_tls) {
			if(!dtio_setup_ssl(dtio)) {
				dtio_close_fd(dtio);
				dtio_reconnect_enable(dtio);
				return;
			}
		}
	}
	dtio->check_nb_connect = 1;

	/* the EV_READ is to read ACCEPT control messages, and catch channel
	 * close. EV_WRITE is to write packets */
	ev = ub_event_new(dtio->event_base, dtio->fd,
		UB_EV_READ | UB_EV_WRITE | UB_EV_PERSIST, &dtio_output_cb,
		dtio);
	if(!ev) {
		log_err("dnstap io: out of memory");
		if(dtio->ssl) {
#ifdef HAVE_SSL
			SSL_free(dtio->ssl);
			dtio->ssl = NULL;
#endif
		}
		dtio_close_fd(dtio);
		dtio_reconnect_enable(dtio);
		return;
	}
	dtio->event = ev;

	/* setup protocol control message to start */
	if((!dtio->is_bidirectional && !dtio_control_start_send(dtio)) ||
		(dtio->is_bidirectional && !dtio_control_ready_send(dtio)) ) {
		log_err("dnstap io: out of memory");
		ub_event_free(dtio->event);
		dtio->event = NULL;
		if(dtio->ssl) {
#ifdef HAVE_SSL
			SSL_free(dtio->ssl);
			dtio->ssl = NULL;
#endif
		}
		dtio_close_fd(dtio);
		dtio_reconnect_enable(dtio);
		return;
	}
}

/** perform the setup of the writer thread on the established event_base */
static void dtio_setup_on_base(struct dt_io_thread* dtio)
{
	dtio_setup_cmd(dtio);
	dtio_setup_reconnect(dtio);
	dtio_open_output(dtio);
	if(!dtio_add_output_event_write(dtio))
		return;
}

#ifndef THREADS_DISABLED
/** the IO thread function for the DNSTAP IO */
static void* dnstap_io(void* arg)
{
	struct dt_io_thread* dtio = (struct dt_io_thread*)arg;
	time_t secs = 0;
	struct timeval now;
	log_thread_set(&dtio->threadnum);

	/* setup */
	verbose(VERB_ALGO, "start dnstap io thread");
	dtio_setup_base(dtio, &secs, &now);
	dtio_setup_on_base(dtio);

	/* run */
	if(ub_event_base_dispatch(dtio->event_base) < 0) {
		log_err("dnstap io: dispatch failed, errno is %s",
			strerror(errno));
	}

	/* cleanup */
	verbose(VERB_ALGO, "stop dnstap io thread");
	dtio_desetup(dtio);
	return NULL;
}
#endif /* THREADS_DISABLED */

int dt_io_thread_start(struct dt_io_thread* dtio, void* event_base_nothr,
	int numworkers)
{
	/* set up the thread, can fail */
#ifndef USE_WINSOCK
	if(pipe(dtio->commandpipe) == -1) {
		log_err("failed to create pipe: %s", strerror(errno));
		return 0;
	}
#else
	if(_pipe(dtio->commandpipe, 4096, _O_BINARY) == -1) {
		log_err("failed to create _pipe: %s",
			wsa_strerror(WSAGetLastError()));
		return 0;
	}
#endif

	/* start the thread */
	dtio->threadnum = numworkers+1;
	dtio->started = 1;
#ifndef THREADS_DISABLED
	ub_thread_create(&dtio->tid, dnstap_io, dtio);
	(void)event_base_nothr;
#else
	dtio->event_base = event_base_nothr;
	dtio_setup_on_base(dtio);
#endif
	return 1;
}

void dt_io_thread_stop(struct dt_io_thread* dtio)
{
#ifndef THREADS_DISABLED
	uint8_t cmd = DTIO_COMMAND_STOP;
#endif
	if(!dtio) return;
	if(!dtio->started) return;
	verbose(VERB_ALGO, "dnstap io: send stop cmd");

#ifndef THREADS_DISABLED
	while(1) {
		ssize_t r = write(dtio->commandpipe[1], &cmd, sizeof(cmd));
		if(r == -1) {
#ifndef USE_WINSOCK
			if(errno == EINTR || errno == EAGAIN)
				continue;
#else
			if(WSAGetLastError() == WSAEINPROGRESS)
				continue;
			if(WSAGetLastError() == WSAEWOULDBLOCK)
				continue;
#endif
			log_err("dnstap io stop: write: %s",
				sock_strerror(errno));
			break;
		}
		break;
	}
	dtio->started = 0;
#endif /* THREADS_DISABLED */

#ifndef USE_WINSOCK
	close(dtio->commandpipe[1]);
#else
	_close(dtio->commandpipe[1]);
#endif
	dtio->commandpipe[1] = -1;
#ifndef THREADS_DISABLED
	ub_thread_join(dtio->tid);
#else
	dtio->want_to_exit = 1;
	dtio_desetup(dtio);
#endif
}
