/*
 * dnstap/unbound-dnstap-socket.c - debug program that listens for DNSTAP logs.
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
 */

/**
 * \file
 *
 * This program listens on a DNSTAP socket for logged messages.
 */
#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include "dnstap/dtstream.h"
#include "dnstap/dnstap_fstrm.h"
#include "util/log.h"
#include "util/ub_event.h"
#include "util/net_help.h"
#include "services/listen_dnsport.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "sldns/pkthdr.h"
#ifdef USE_DNSTAP
#include <protobuf-c/protobuf-c.h>
#include "dnstap/dnstap.pb-c.h"
#endif /* USE_DNSTAP */
#include "util/config_file.h"

/** listen backlog on TCP connections for dnstap logs */
#define LISTEN_BACKLOG 16

/** usage information for streamtcp */
static void usage(char* argv[])
{
	printf("usage: %s [options]\n", argv[0]);
	printf(" 	Listen to dnstap messages\n");
	printf("stdout has dnstap log, stderr has verbose server log\n");
	printf("-u <socketpath> listen to unix socket with this file name\n");
	printf("-s <serverip[@port]> listen for TCP on the IP and port\n");
	printf("-t <serverip[@port]> listen for TLS on IP and port\n");
	printf("-x <server.key> server key file for TLS service\n");
	printf("-y <server.pem> server cert file for TLS service\n");
	printf("-z <verify.pem> cert file to verify client connections\n");
	printf("-l 		long format for DNS printout\n");
	printf("-v 		more verbose log output\n");
	printf("-c			internal unit test and exit\n");
	printf("-h 		this help text\n");
	exit(1);
}

/** long format option, for multiline printout per message */
static int longformat = 0;

struct tap_socket_list;
struct tap_socket;
/** main tap callback data */
struct main_tap_data {
	/** the event base (to loopexit) */
	struct ub_event_base* base;
	/** the list of accept sockets */
	struct tap_socket_list* acceptlist;
};

/* list of data */
struct tap_data_list {
	/** next in list */
	struct tap_data_list* next;
	/** the data */
	struct tap_data* d;
};

/** tap callback variables */
struct tap_data {
	/** the fd */
	int fd;
	/** the ub event */
	struct ub_event* ev;
	/** the SSL for TLS streams */
	SSL* ssl;
	/** is the ssl handshake done */
	int ssl_handshake_done;
	/** we are briefly waiting to write (in the struct event) */
	int ssl_brief_write;
	/** string that identifies the socket (or NULL), like IP address */
	char* id;
	/** have we read the length, and how many bytes of it */
	int len_done;
	/** have we read the data, and how many bytes of it */
	size_t data_done;
	/** are we reading a control frame */
	int control_frame;
	/** are we bi-directional (if false, uni-directional) */
	int is_bidirectional;
	/** data of the frame */
	uint8_t* frame;
	/** length of this frame */
	size_t len;
	/** back pointer to the tap_data_list entry;
	 * used to NULL the forward pointer to this data
	 * when this data is freed. */
	struct tap_data_list* data_list;
};

/** list of sockets */
struct tap_socket_list {
	/** next in list */
	struct tap_socket_list* next;
	/** the socket */
	struct tap_socket* s;
};

/** tap socket */
struct tap_socket {
	/** fd of socket */
	int fd;
	/** the event for it */
	struct ub_event *ev;
	/** has the event been added */
	int ev_added;
	/** the callback, for the event, ev_cb(fd, bits, arg) */
	void (*ev_cb)(int, short, void*);
	/** data element, (arg for the tap_socket struct) */
	void* data;
	/** socketpath, if this is an AF_LOCAL socket */
	char* socketpath;
	/** IP, if this is a TCP socket */
	char* ip;
	/** for a TLS socket, the tls context */
	SSL_CTX* sslctx;
	/** dumb way to deal with memory leaks:
	 * tap_data was only freed on errors and not during exit leading to
	 * false positives when testing for memory leaks. */
	struct tap_data_list* data_list;
};

/** try to delete tail entries from the list if all of them have no data */
static void tap_data_list_try_to_free_tail(struct tap_data_list* list)
{
	struct tap_data_list* current = list;
	log_assert(!list->d);
	if(!list->next) /* we are the last, we can't remove ourselves */
		return;
	list = list->next;
	while(list) {
		if(list->d) /* a tail entry still has data; return */
			return;
		list = list->next;
	}
	/* keep the next */
	list = current->next;
	/* the tail will be removed; but not ourselves */
	current->next = NULL;
	while(list) {
		current = list;
		list = list->next;
		free(current);
	}
}

/** delete the tap structure */
static void tap_data_free(struct tap_data* data, int free_tail)
{
	if(!data)
		return;
	if(data->ev) {
		ub_event_del(data->ev);
		ub_event_free(data->ev);
	}
#ifdef HAVE_SSL
	SSL_free(data->ssl);
#endif
	sock_close(data->fd);
	free(data->id);
	free(data->frame);
	if(data->data_list) {
		data->data_list->d = NULL;
		if(free_tail)
			tap_data_list_try_to_free_tail(data->data_list);
	}
	free(data);
}

/** insert tap_data in the tap_data_list */
static int tap_data_list_insert(struct tap_data_list** liststart,
	struct tap_data* d)
{
	struct tap_data_list* entry = (struct tap_data_list*)
		malloc(sizeof(*entry));
	if(!entry)
		return 0;
	entry->next = *liststart;
	entry->d = d;
	d->data_list = entry;
	*liststart = entry;
	return 1;
}

/** delete the tap_data_list and free any remaining tap_data */
static void tap_data_list_delete(struct tap_data_list* list)
{
	struct tap_data_list* e = list, *next;
	while(e) {
		next = e->next;
		if(e->d) {
			tap_data_free(e->d, 0);
			e->d = NULL;
		}
		free(e);
		e = next;
	}
}

/** del the tap event */
static void tap_socket_delev(struct tap_socket* s)
{
	if(!s) return;
	if(!s->ev) return;
	if(!s->ev_added) return;
	ub_event_del(s->ev);
	s->ev_added = 0;
}

/** close the tap socket */
static void tap_socket_close(struct tap_socket* s)
{
	if(!s) return;
	if(s->fd == -1) return;
	sock_close(s->fd);
	s->fd = -1;
}

/** delete tap socket */
static void tap_socket_delete(struct tap_socket* s)
{
	if(!s) return;
#ifdef HAVE_SSL
	SSL_CTX_free(s->sslctx);
#endif
	tap_data_list_delete(s->data_list);
	ub_event_free(s->ev);
	free(s->socketpath);
	free(s->ip);
	free(s);
}

/** create new socket (unconnected, not base-added), or NULL malloc fail */
static struct tap_socket* tap_socket_new_local(char* socketpath,
	void (*ev_cb)(int, short, void*), void* data)
{
	struct tap_socket* s = calloc(1, sizeof(*s));
	if(!s) {
		log_err("malloc failure");
		return NULL;
	}
	s->socketpath = strdup(socketpath);
	if(!s->socketpath) {
		free(s);
		log_err("malloc failure");
		return NULL;
	}
	s->fd = -1;
	s->ev_cb = ev_cb;
	s->data = data;
	return s;
}

/** create new socket (unconnected, not base-added), or NULL malloc fail */
static struct tap_socket* tap_socket_new_tcpaccept(char* ip,
	void (*ev_cb)(int, short, void*), void* data)
{
	struct tap_socket* s = calloc(1, sizeof(*s));
	if(!s) {
		log_err("malloc failure");
		return NULL;
	}
	s->ip = strdup(ip);
	if(!s->ip) {
		free(s);
		log_err("malloc failure");
		return NULL;
	}
	s->fd = -1;
	s->ev_cb = ev_cb;
	s->data = data;
	return s;
}

/** create new socket (unconnected, not base-added), or NULL malloc fail */
static struct tap_socket* tap_socket_new_tlsaccept(char* ip,
	void (*ev_cb)(int, short, void*), void* data, char* server_key,
	char* server_cert, char* verifypem)
{
	struct tap_socket* s = calloc(1, sizeof(*s));
	if(!s) {
		log_err("malloc failure");
		return NULL;
	}
	s->ip = strdup(ip);
	if(!s->ip) {
		free(s);
		log_err("malloc failure");
		return NULL;
	}
	s->fd = -1;
	s->ev_cb = ev_cb;
	s->data = data;
	s->sslctx = listen_sslctx_create(server_key, server_cert, verifypem,
		NULL, NULL, 0, 0, 0);
	if(!s->sslctx) {
		log_err("could not create ssl context");
		free(s->ip);
		free(s);
		return NULL;
	}
	return s;
}

/** setup tcp accept socket on IP string */
static int make_tcp_accept(char* ip)
{
#ifdef SO_REUSEADDR
	int on = 1;
#endif
	struct sockaddr_storage addr;
	socklen_t len;
	int s;

	memset(&addr, 0, sizeof(addr));
	len = (socklen_t)sizeof(addr);
	if(!extstrtoaddr(ip, &addr, &len, UNBOUND_DNS_PORT)) {
		log_err("could not parse IP '%s'", ip);
		return -1;
	}

	if((s = socket(addr.ss_family, SOCK_STREAM, 0)) == -1) {
		log_err("can't create socket: %s", sock_strerror(errno));
		return -1;
	}
#ifdef SO_REUSEADDR
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on,
		(socklen_t)sizeof(on)) < 0) {
		log_err("setsockopt(.. SO_REUSEADDR ..) failed: %s",
			sock_strerror(errno));
		sock_close(s);
		return -1;
	}
#endif /* SO_REUSEADDR */
	if(bind(s, (struct sockaddr*)&addr, len) != 0) {
		log_err_addr("can't bind socket", sock_strerror(errno),
			&addr, len);
		sock_close(s);
		return -1;
	}
	if(!fd_set_nonblock(s)) {
		sock_close(s);
		return -1;
	}
	if(listen(s, LISTEN_BACKLOG) == -1) {
		log_err("can't listen: %s", sock_strerror(errno));
		sock_close(s);
		return -1;
	}
	return s;
}

/** setup socket on event base */
static int tap_socket_setup(struct tap_socket* s, struct ub_event_base* base)
{
	if(s->socketpath) {
		/* AF_LOCAL accept socket */
		s->fd = create_local_accept_sock(s->socketpath, NULL, 0);
		if(s->fd == -1) {
			log_err("could not create local socket");
			return 0;
		}
	} else if(s->ip || s->sslctx) {
		/* TCP accept socket */
		s->fd = make_tcp_accept(s->ip);
		if(s->fd == -1) {
			log_err("could not create tcp socket");
			return 0;
		}
	}
	s->ev = ub_event_new(base, s->fd, UB_EV_READ | UB_EV_PERSIST,
		s->ev_cb, s);
	if(!s->ev) {
		log_err("could not ub_event_new");
		return 0;
	}
	if(ub_event_add(s->ev, NULL) != 0) {
		log_err("could not ub_event_add");
		return 0;
	}
	s->ev_added = 1;
	return 1;
}

/** add tap socket to list */
static int tap_socket_list_insert(struct tap_socket_list** liststart,
	struct tap_socket* s)
{
	struct tap_socket_list* entry = (struct tap_socket_list*)
		malloc(sizeof(*entry));
	if(!entry)
		return 0;
	entry->next = *liststart;
	entry->s = s;
	*liststart = entry;
	return 1;
}

/** delete the list */
static void tap_socket_list_delete(struct tap_socket_list* list)
{
	struct tap_socket_list* e = list, *next;
	while(e) {
		next = e->next;
		tap_socket_delev(e->s);
		tap_socket_close(e->s);
		tap_socket_delete(e->s);
		free(e);
		e = next;
	}
}

/** setup accept events */
static int tap_socket_list_addevs(struct tap_socket_list* list,
	struct ub_event_base* base)
{
	struct tap_socket_list* entry;
	for(entry = list; entry; entry = entry->next) {
		if(!tap_socket_setup(entry->s, base)) {
			log_err("could not setup socket");
			return 0;
		}
	}
	return 1;
}

#ifdef USE_DNSTAP
/** log control frame contents */
static void log_control_frame(uint8_t* pkt, size_t len)
{
	char* desc;
	if(verbosity == 0) return;
	desc = fstrm_describe_control(pkt, len);
	if(!desc) {
		log_err("out of memory");
		return;
	}
	log_info("control frame %s", desc);
	free(desc);
}

/** convert mtype to string */
static const char* mtype_to_str(enum _Dnstap__Message__Type mtype)
{
	switch(mtype) {
		case DNSTAP__MESSAGE__TYPE__AUTH_QUERY:
			return "AUTH_QUERY";
		case DNSTAP__MESSAGE__TYPE__AUTH_RESPONSE:
			return "AUTH_RESPONSE";
		case DNSTAP__MESSAGE__TYPE__RESOLVER_QUERY:
			return "RESOLVER_QUERY";
		case DNSTAP__MESSAGE__TYPE__RESOLVER_RESPONSE:
			return "RESOLVER_RESPONSE";
		case DNSTAP__MESSAGE__TYPE__CLIENT_QUERY:
			return "CLIENT_QUERY";
		case DNSTAP__MESSAGE__TYPE__CLIENT_RESPONSE:
			return "CLIENT_RESPONSE";
		case DNSTAP__MESSAGE__TYPE__FORWARDER_QUERY:
			return "FORWARDER_QUERY";
		case DNSTAP__MESSAGE__TYPE__FORWARDER_RESPONSE:
			return "FORWARDER_RESPONSE";
		case DNSTAP__MESSAGE__TYPE__STUB_QUERY:
			return "STUB_QUERY";
		case DNSTAP__MESSAGE__TYPE__STUB_RESPONSE:
			return "STUB_RESPONSE";
		default: break;
	}
	return "unknown_message_type";
}

/** convert type address to a string ip4 or ip6, malloced or NULL on fail */
static char* str_of_addr(ProtobufCBinaryData address)
{
	char buf[64];
	socklen_t len = sizeof(buf);
	if(address.len == 4) {
		if(inet_ntop(AF_INET, address.data, buf, len)!=0)
			return strdup(buf);
	} else if(address.len == 16) {
		if(inet_ntop(AF_INET6, address.data, buf, len)!=0)
			return strdup(buf);
	}
	return NULL;
}

/** convert message buffer (of dns bytes) to the first qname, type, class,
 * malloced or NULL on fail */
static char* q_of_msg(ProtobufCBinaryData message)
{
	char buf[300];
	/* header, name, type, class minimum to get the query tuple */
	if(message.len < 12 + 1 + 4 + 4) return NULL;
	if(LDNS_QDCOUNT(message.data) < 1) return NULL;
	if(sldns_wire2str_rrquestion_buf(message.data+12, message.len-12,
		buf, sizeof(buf)) != 0) {
		/* remove trailing newline, tabs to spaces */
		/* remove the newline: */
		if(buf[0] != 0) buf[strlen(buf)-1]=0;
		/* remove first tab (before type) */
		if(strrchr(buf, '\t')) *strrchr(buf, '\t')=' ';
		/* remove second tab (before class) */
		if(strrchr(buf, '\t')) *strrchr(buf, '\t')=' ';
		return strdup(buf);
	}
	return NULL;
}

/** convert possible string or hex data to string. malloced or NULL */
static char* possible_str(ProtobufCBinaryData str)
{
	int is_str = 1;
	size_t i;
	for(i=0; i<str.len; i++) {
		if(!isprint((unsigned char)str.data[i]))
			is_str = 0;
	}
	if(is_str) {
		char* res = malloc(str.len+1);
		if(res) {
			memmove(res, str.data, str.len);
			res[str.len] = 0;
			return res;
		}
	} else {
		const char* hex = "0123456789ABCDEF";
		char* res = malloc(str.len*2+1);
		if(res) {
			for(i=0; i<str.len; i++) {
				res[i*2] = hex[(str.data[i]&0xf0)>>4];
				res[i*2+1] = hex[str.data[i]&0x0f];
			}
			res[str.len*2] = 0;
			return res;
		}
	}
	return NULL;
}

/** convert timeval to string, malloced or NULL */
static char* tv_to_str(protobuf_c_boolean has_time_sec, uint64_t time_sec,
	protobuf_c_boolean has_time_nsec, uint32_t time_nsec)
{
	char buf[64], buf2[256];
	struct timeval tv;
	time_t time_t_sec;
	memset(&tv, 0, sizeof(tv));
	if(has_time_sec) tv.tv_sec = time_sec;
	if(has_time_nsec) tv.tv_usec = time_nsec/1000;

	buf[0]=0;
	time_t_sec = tv.tv_sec;
	(void)ctime_r(&time_t_sec, buf);
	snprintf(buf2, sizeof(buf2), "%u.%9.9u %s",
		(unsigned)time_sec, (unsigned)time_nsec, buf);
	return strdup(buf2);
}

/** log data frame contents */
static void log_data_frame(uint8_t* pkt, size_t len)
{
	Dnstap__Dnstap* d = dnstap__dnstap__unpack(NULL, len, pkt);
	const char* mtype = NULL;
	char* maddr=NULL, *qinf=NULL;
	if(!d) {
		log_err("could not unpack");
		return;
	}
	if(d->base.descriptor != &dnstap__dnstap__descriptor) {
		log_err("wrong base descriptor");
		dnstap__dnstap__free_unpacked(d, NULL);
		return;
	}
	if(d->type != DNSTAP__DNSTAP__TYPE__MESSAGE) {
		log_err("dnstap type not type_message");
		dnstap__dnstap__free_unpacked(d, NULL);
		return;
	}
	if(d->message) {
		mtype = mtype_to_str(d->message->type);
		if(d->message->has_query_address)
			maddr = str_of_addr(d->message->query_address);
		else if(d->message->has_response_address)
			maddr = str_of_addr(d->message->response_address);
		if(d->message->has_query_message)
			qinf = q_of_msg(d->message->query_message);
		else if(d->message->has_response_message)
			qinf = q_of_msg(d->message->response_message);

	} else {
		mtype = "nomessage";
	}
	
	printf("%s%s%s%s%s\n", mtype, (maddr?" ":""), (maddr?maddr:""),
		(qinf?" ":""), (qinf?qinf:""));
	free(maddr);
	free(qinf);

	if(longformat) {
		char* id=NULL, *vs=NULL;
		if(d->has_identity) {
			id=possible_str(d->identity);
		}
		if(d->has_version) {
			vs=possible_str(d->version);
		}
		if(id || vs)
			printf("identity: %s%s%s\n", (id?id:""),
				(id&&vs?" ":""), (vs?vs:""));
		free(id);
		free(vs);

		if(d->message && d->message->has_query_message &&
			d->message->query_message.data) {
			char* qmsg = sldns_wire2str_pkt(
				d->message->query_message.data,
				d->message->query_message.len);
			if(qmsg) {
				printf("query_message:\n%s", qmsg);
				free(qmsg);
			}
		}
		if(d->message && d->message->has_query_time_sec) {
			char* qtv = tv_to_str(d->message->has_query_time_sec,
				d->message->query_time_sec,
				d->message->has_query_time_nsec,
				d->message->query_time_nsec);
			if(qtv) {
				printf("query_time: %s\n", qtv);
				free(qtv);
			}
		}
		if(d->message && d->message->has_response_message &&
			d->message->response_message.data) {
			char* rmsg = sldns_wire2str_pkt(
				d->message->response_message.data,
				d->message->response_message.len);
			if(rmsg) {
				printf("response_message:\n%s", rmsg);
				free(rmsg);
			}
		}
		if(d->message && d->message->has_response_time_sec) {
			char* rtv = tv_to_str(d->message->has_response_time_sec,
				d->message->response_time_sec,
				d->message->has_response_time_nsec,
				d->message->response_time_nsec);
			if(rtv) {
				printf("response_time: %s\n", rtv);
				free(rtv);
			}
		}
	}
	fflush(stdout);
	dnstap__dnstap__free_unpacked(d, NULL);
}
#endif /* USE_DNSTAP */

/** receive bytes from fd, prints errors if bad,
 * returns 0: closed/error, -1: continue, >0 number of bytes */
static ssize_t receive_bytes(struct tap_data* data, int fd, void* buf,
	size_t len)
{
	ssize_t ret = recv(fd, buf, len, MSG_DONTWAIT);
	if(ret == 0) {
		/* closed */
		if(verbosity) log_info("dnstap client stream closed from %s",
			(data->id?data->id:""));
		return 0;
	} else if(ret == -1) {
		/* error */
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return -1;
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAEINPROGRESS)
			return -1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(data->ev, UB_EV_READ);
			return -1;
		}
#endif
		log_err("could not recv: %s", sock_strerror(errno));
		if(verbosity) log_info("dnstap client stream closed from %s",
			(data->id?data->id:""));
		return 0;
	}
	return ret;
}

/* define routine for have_ssl only to avoid unused function warning */
#ifdef HAVE_SSL
/** set to wait briefly for a write event, for one event call */
static void tap_enable_brief_write(struct tap_data* data)
{
	ub_event_del(data->ev);
	ub_event_del_bits(data->ev, UB_EV_READ);
	ub_event_add_bits(data->ev, UB_EV_WRITE);
	if(ub_event_add(data->ev, NULL) != 0)
		log_err("could not ub_event_add in tap_enable_brief_write");
	data->ssl_brief_write = 1;
}
#endif /* HAVE_SSL */

/* define routine for have_ssl only to avoid unused function warning */
#ifdef HAVE_SSL
/** stop the brief wait for a write event. back to reading. */
static void tap_disable_brief_write(struct tap_data* data)
{
	ub_event_del(data->ev);
	ub_event_del_bits(data->ev, UB_EV_WRITE);
	ub_event_add_bits(data->ev, UB_EV_READ);
	if(ub_event_add(data->ev, NULL) != 0)
		log_err("could not ub_event_add in tap_disable_brief_write");
	data->ssl_brief_write = 0;
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/** receive bytes over ssl stream, prints errors if bad,
 * returns 0: closed/error, -1: continue, >0 number of bytes */
static ssize_t ssl_read_bytes(struct tap_data* data, void* buf, size_t len)
{
	int r;
	ERR_clear_error();
	r = SSL_read(data->ssl, buf, len);
	if(r <= 0) {
		int want = SSL_get_error(data->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			/* closed */
			if(verbosity) log_info("dnstap client stream closed from %s",
				(data->id?data->id:""));
			return 0;
		} else if(want == SSL_ERROR_WANT_READ) {
			/* continue later */
			return -1;
		} else if(want == SSL_ERROR_WANT_WRITE) {
			/* set to briefly write */
			tap_enable_brief_write(data);
			return -1;
		} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
			if(errno == ECONNRESET && verbosity < 2)
				return 0; /* silence reset by peer */
#endif
			if(errno != 0)
				log_err("SSL_read syscall: %s",
					strerror(errno));
			if(verbosity) log_info("dnstap client stream closed from %s",
				(data->id?data->id:""));
			return 0;
		}
		log_crypto_err_io("could not SSL_read", want);
		if(verbosity) log_info("dnstap client stream closed from %s",
			(data->id?data->id:""));
		return 0;
	}
	return r;
}
#endif /* HAVE_SSL */

/** receive bytes on the tap connection, prints errors if bad,
 * returns 0: closed/error, -1: continue, >0 number of bytes */
static ssize_t tap_receive(struct tap_data* data, void* buf, size_t len)
{
#ifdef HAVE_SSL
	if(data->ssl)
		return ssl_read_bytes(data, buf, len);
#endif
	return receive_bytes(data, data->fd, buf, len);
}

/** reply with ACCEPT control frame to bidirectional client,
 * returns 0 on error */
static int reply_with_accept(struct tap_data* data)
{
#ifdef USE_DNSTAP
	/* len includes the escape and framelength */
	size_t len = 0;
	void* acceptframe = fstrm_create_control_frame_accept(
		DNSTAP_CONTENT_TYPE, &len);
	if(!acceptframe) {
		log_err("out of memory");
		return 0;
	}

	fd_set_block(data->fd);
	if(data->ssl) {
#ifdef HAVE_SSL
		int r;
		if((r=SSL_write(data->ssl, acceptframe, len)) <= 0) {
			int r2;
			if((r2=SSL_get_error(data->ssl, r)) == SSL_ERROR_ZERO_RETURN)
				log_err("SSL_write, peer closed connection");
			else
				log_crypto_err_io("could not SSL_write", r2);
			fd_set_nonblock(data->fd);
			free(acceptframe);
			return 0;
		}
#endif
	} else {
		if(send(data->fd, acceptframe, len, 0) == -1) {
			log_err("send failed: %s", sock_strerror(errno));
			fd_set_nonblock(data->fd);
			free(acceptframe);
			return 0;
		}
	}
	if(verbosity) log_info("sent control frame(accept) content-type:(%s)",
			DNSTAP_CONTENT_TYPE);

	fd_set_nonblock(data->fd);
	free(acceptframe);
	return 1;
#else
	log_err("no dnstap compiled, no reply");
	(void)data;
	return 0;
#endif
}

/** reply with FINISH control frame to bidirectional client,
 * returns 0 on error */
static int reply_with_finish(struct tap_data* data)
{
#ifdef USE_DNSTAP
	size_t len = 0;
	void* finishframe = fstrm_create_control_frame_finish(&len);
	if(!finishframe) {
		log_err("out of memory");
		return 0;
	}

	fd_set_block(data->fd);
	if(data->ssl) {
#ifdef HAVE_SSL
		int r;
		if((r=SSL_write(data->ssl, finishframe, len)) <= 0) {
			int r2;
			if((r2=SSL_get_error(data->ssl, r)) == SSL_ERROR_ZERO_RETURN)
				log_err("SSL_write, peer closed connection");
			else
				log_crypto_err_io("could not SSL_write", r2);
			fd_set_nonblock(data->fd);
			free(finishframe);
			return 0;
		}
#endif
	} else {
		if(send(data->fd, finishframe, len, 0) == -1) {
			log_err("send failed: %s", sock_strerror(errno));
			fd_set_nonblock(data->fd);
			free(finishframe);
			return 0;
		}
	}
	if(verbosity) log_info("sent control frame(finish)");

	fd_set_nonblock(data->fd);
	free(finishframe);
	return 1;
#else
	log_err("no dnstap compiled, no reply");
	(void)data;
	return 0;
#endif
}

#ifdef HAVE_SSL
/** check SSL peer certificate, return 0 on fail */
static int tap_check_peer(struct tap_data* data)
{
	if((SSL_get_verify_mode(data->ssl)&SSL_VERIFY_PEER)) {
		/* verification */
		if(SSL_get_verify_result(data->ssl) == X509_V_OK) {
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
			X509* x = SSL_get1_peer_certificate(data->ssl);
#else
			X509* x = SSL_get_peer_certificate(data->ssl);
#endif
			if(!x) {
				if(verbosity) log_info("SSL connection %s"
					" failed no certificate", data->id);
				return 0;
			}
			if(verbosity)
				log_cert(VERB_ALGO, "peer certificate", x);
#ifdef HAVE_SSL_GET0_PEERNAME
			if(SSL_get0_peername(data->ssl)) {
				if(verbosity) log_info("SSL connection %s "
					"to %s authenticated", data->id,
					SSL_get0_peername(data->ssl));
			} else {
#endif
				if(verbosity) log_info("SSL connection %s "
					"authenticated", data->id);
#ifdef HAVE_SSL_GET0_PEERNAME
			}
#endif
			X509_free(x);
		} else {
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
			X509* x = SSL_get1_peer_certificate(data->ssl);
#else
			X509* x = SSL_get_peer_certificate(data->ssl);
#endif
			if(x) {
				if(verbosity)
					log_cert(VERB_ALGO, "peer certificate", x);
				X509_free(x);
			}
			if(verbosity) log_info("SSL connection %s failed: "
				"failed to authenticate", data->id);
			return 0;
		}
	} else {
		/* unauthenticated, the verify peer flag was not set
		 * in ssl when the ssl object was created from ssl_ctx */
		if(verbosity) log_info("SSL connection %s", data->id);
	}
	return 1;
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/** perform SSL handshake, return 0 to wait for events, 1 if done */
static int tap_handshake(struct tap_data* data)
{
	int r;
	if(data->ssl_brief_write) {
		/* write condition has been satisfied, back to reading */
		tap_disable_brief_write(data);
	}
	if(data->ssl_handshake_done)
		return 1;

	ERR_clear_error();
	r = SSL_do_handshake(data->ssl);
	if(r != 1) {
		int want = SSL_get_error(data->ssl, r);
		if(want == SSL_ERROR_WANT_READ) {
			return 0;
		} else if(want == SSL_ERROR_WANT_WRITE) {
			tap_enable_brief_write(data);
			return 0;
		} else if(r == 0) {
			/* closed */
			tap_data_free(data, 1);
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
				log_err("SSL_handshake syscall: %s",
					strerror(errno));
			tap_data_free(data, 1);
			return 0;
		} else {
			unsigned long err = ERR_get_error();
			if(!squelch_err_ssl_handshake(err)) {
				log_crypto_err_code("ssl handshake failed",
					err);
				verbose(VERB_OPS, "ssl handshake failed "
					"from %s", data->id);
			}
			tap_data_free(data, 1);
			return 0;
		}
	}
	/* check peer verification */
	data->ssl_handshake_done = 1;
	if(!tap_check_peer(data)) {
		/* closed */
		tap_data_free(data, 1);
		return 0;
	}
	return 1;
}
#endif /* HAVE_SSL */

/** callback for dnstap listener */
void dtio_tap_callback(int ATTR_UNUSED(fd), short ATTR_UNUSED(bits), void* arg)
{
	struct tap_data* data = (struct tap_data*)arg;
	if(verbosity>=3) log_info("tap callback");
#ifdef HAVE_SSL
	if(data->ssl && (!data->ssl_handshake_done ||
		data->ssl_brief_write)) {
		if(!tap_handshake(data))
			return;
	}
#endif
	while(data->len_done < 4) {
		uint32_t l = (uint32_t)data->len;
		ssize_t ret = tap_receive(data,
			((uint8_t*)&l)+data->len_done, 4-data->len_done);
		if(verbosity>=4) log_info("s recv %d", (int)ret);
		if(ret == 0) {
			/* closed or error */
			tap_data_free(data, 1);
			return;
		} else if(ret == -1) {
			/* continue later */
			return;
		}
		data->len_done += ret;
		data->len = (size_t)l;
		if(data->len_done < 4)
			return; /* continue later */
		data->len = (size_t)(ntohl(l));
		if(verbosity>=3) log_info("length is %d", (int)data->len);
		if(data->len == 0) {
			/* it is a control frame */
			data->control_frame = 1;
			/* read controlframelen */
			data->len_done = 0;
		} else {
			/* allocate frame size */
			data->frame = calloc(1, data->len);
			if(!data->frame) {
				log_err("out of memory");
				tap_data_free(data, 1);
				return;
			}
		}
	}

	/* we want to read the full length now */
	if(data->data_done < data->len) {
		ssize_t r = tap_receive(data, data->frame + data->data_done,
			data->len - data->data_done);
		if(verbosity>=4) log_info("f recv %d", (int)r);
		if(r == 0) {
			/* closed or error */
			tap_data_free(data, 1);
			return;
		} else if(r == -1) {
			/* continue later */
			return;
		}
		data->data_done += r;
		if(data->data_done < data->len)
			return; /* continue later */
	}

	/* we are done with a frame */
	if(verbosity>=3) log_info("received %sframe len %d",
		(data->control_frame?"control ":""), (int)data->len);
#ifdef USE_DNSTAP
	if(data->control_frame)
		log_control_frame(data->frame, data->len);
	else	log_data_frame(data->frame, data->len);
#endif

	if(data->len >= 4 && sldns_read_uint32(data->frame) ==
		FSTRM_CONTROL_FRAME_READY) {
		data->is_bidirectional = 1;
		if(verbosity) log_info("bidirectional stream");
		if(!reply_with_accept(data)) {
			tap_data_free(data, 1);
			return;
		}
	} else if(data->len >= 4 && sldns_read_uint32(data->frame) ==
		FSTRM_CONTROL_FRAME_STOP && data->is_bidirectional) {
		if(!reply_with_finish(data)) {
			tap_data_free(data, 1);
			return;
		}
	}

	/* prepare for next frame */
	free(data->frame);
	data->frame = NULL;
	data->control_frame = 0;
	data->len = 0;
	data->len_done = 0;
	data->data_done = 0;
}

/** callback for main listening file descriptor */
void dtio_mainfdcallback(int fd, short ATTR_UNUSED(bits), void* arg)
{
	struct tap_socket* tap_sock = (struct tap_socket*)arg;
	struct main_tap_data* maindata = (struct main_tap_data*)
		tap_sock->data;
	struct tap_data* data;
	char* id = NULL;
	struct sockaddr_storage addr;
	socklen_t addrlen = (socklen_t)sizeof(addr);
	int s;
	memset(&addr, 0, sizeof(addr));
	s = accept(fd, (struct sockaddr*)&addr, &addrlen);
	if(s == -1) {
#ifndef USE_WINSOCK
		/* EINTR is signal interrupt. others are closed connection. */
		if(     errno == EINTR || errno == EAGAIN
#ifdef EWOULDBLOCK
			|| errno == EWOULDBLOCK
#endif
#ifdef ECONNABORTED
			|| errno == ECONNABORTED
#endif
#ifdef EPROTO
			|| errno == EPROTO
#endif /* EPROTO */
			)
			return;
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAECONNRESET)
			return;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(maindata->ev, UB_EV_READ);
			return;
		}
#endif
		log_err_addr("accept failed", sock_strerror(errno), &addr,
			addrlen);
		return;
	}
	fd_set_nonblock(s);
	if(verbosity) {
		if(addr.ss_family == AF_LOCAL) {
#ifdef HAVE_SYS_UN_H
			struct sockaddr_un* usock = calloc(1, sizeof(struct sockaddr_un) + 1);
			if(usock) {
				socklen_t ulen = sizeof(struct sockaddr_un);
				if(getsockname(fd, (struct sockaddr*)usock, &ulen) != -1) {
					log_info("accepted new dnstap client from %s", usock->sun_path);
					id = strdup(usock->sun_path);
				} else {
					log_info("accepted new dnstap client");
				}
				free(usock);
			} else {
				log_info("accepted new dnstap client");
			}
#endif /* HAVE_SYS_UN_H */
		} else if(addr.ss_family == AF_INET ||
			addr.ss_family == AF_INET6) {
			char ip[256];
			addr_to_str(&addr, addrlen, ip, sizeof(ip));
			log_info("accepted new dnstap client from %s", ip);
			id = strdup(ip);
		} else {
			log_info("accepted new dnstap client");
		}
	}
	
	data = calloc(1, sizeof(*data));
	if(!data) fatal_exit("out of memory");
	data->fd = s;
	data->id = id;
	if(tap_sock->sslctx) {
		data->ssl = incoming_ssl_fd(tap_sock->sslctx, data->fd);
		if(!data->ssl) fatal_exit("could not SSL_new");
	}
	data->ev = ub_event_new(maindata->base, s, UB_EV_READ | UB_EV_PERSIST,
		&dtio_tap_callback, data);
	if(!data->ev) fatal_exit("could not ub_event_new");
	if(ub_event_add(data->ev, NULL) != 0) fatal_exit("could not ub_event_add");
	if(!tap_data_list_insert(&tap_sock->data_list, data))
		fatal_exit("could not tap_data_list_insert");
}

/** setup local accept sockets */
static void setup_local_list(struct main_tap_data* maindata,
	struct config_strlist_head* local_list)
{
	struct config_strlist* item;
	for(item = local_list->first; item; item = item->next) {
		struct tap_socket* s;
		s = tap_socket_new_local(item->str, &dtio_mainfdcallback,
			maindata);
		if(!s) fatal_exit("out of memory");
		if(!tap_socket_list_insert(&maindata->acceptlist, s))
			fatal_exit("out of memory");
	}
}

/** setup tcp accept sockets */
static void setup_tcp_list(struct main_tap_data* maindata,
	struct config_strlist_head* tcp_list)
{
	struct config_strlist* item;
	for(item = tcp_list->first; item; item = item->next) {
		struct tap_socket* s;
		s = tap_socket_new_tcpaccept(item->str, &dtio_mainfdcallback,
			maindata);
		if(!s) fatal_exit("out of memory");
		if(!tap_socket_list_insert(&maindata->acceptlist, s))
			fatal_exit("out of memory");
	}
}

/** setup tls accept sockets */
static void setup_tls_list(struct main_tap_data* maindata,
	struct config_strlist_head* tls_list, char* server_key,
	char* server_cert, char* verifypem)
{
	struct config_strlist* item;
	for(item = tls_list->first; item; item = item->next) {
		struct tap_socket* s;
		s = tap_socket_new_tlsaccept(item->str, &dtio_mainfdcallback,
			maindata, server_key, server_cert, verifypem);
		if(!s) fatal_exit("out of memory");
		if(!tap_socket_list_insert(&maindata->acceptlist, s))
			fatal_exit("out of memory");
	}
}

/** signal variable */
static struct ub_event_base* sig_base = NULL;
/** do we have to quit */
int sig_quit = 0;
/** signal handler for user quit */
static RETSIGTYPE main_sigh(int sig)
{
	if(!sig_quit) {
		char str[] = "exit on signal   \n";
		str[15] = '0' + (sig/10)%10;
		str[16] = '0' + sig%10;
		/* simple cast to void will not silence Wunused-result */
		(void)!write(STDERR_FILENO, str, strlen(str));
	}
	if(sig_base) {
		ub_event_base_loopexit(sig_base);
		sig_base = NULL;
	}
	sig_quit = 1;
}

/** setup and run the server to listen to DNSTAP messages */
static void
setup_and_run(struct config_strlist_head* local_list,
	struct config_strlist_head* tcp_list,
	struct config_strlist_head* tls_list, char* server_key,
	char* server_cert, char* verifypem)
{
	time_t secs = 0;
	struct timeval now;
	struct main_tap_data* maindata;
	struct ub_event_base* base;
	const char *evnm="event", *evsys="", *evmethod="";

	maindata = calloc(1, sizeof(*maindata));
	if(!maindata) fatal_exit("out of memory");
	memset(&now, 0, sizeof(now));
	base = ub_default_event_base(1, &secs, &now);
	if(!base) fatal_exit("could not create ub_event base");
	maindata->base = base;
	sig_base = base;
	if(sig_quit) {
		ub_event_base_free(base);
		free(maindata);
		return;
	}
	ub_get_event_sys(base, &evnm, &evsys, &evmethod);
	if(verbosity) log_info("%s %s uses %s method", evnm, evsys, evmethod);

	setup_local_list(maindata, local_list);
	setup_tcp_list(maindata, tcp_list);
	setup_tls_list(maindata, tls_list, server_key, server_cert,
		verifypem);
	if(!tap_socket_list_addevs(maindata->acceptlist, base))
		fatal_exit("could not setup accept events");
	if(verbosity) log_info("start of service");

	ub_event_base_dispatch(base);
	sig_base = NULL;

	if(verbosity) log_info("end of service");
	tap_socket_list_delete(maindata->acceptlist);
	ub_event_base_free(base);
	free(maindata);
}

/* internal unit tests */
static int internal_unittest()
{
	/* unit test tap_data_list_try_to_free_tail() */
#define unit_tap_datas_max 5
	struct tap_data* datas[unit_tap_datas_max];
	struct tap_data_list* list;
	struct tap_socket* socket = calloc(1, sizeof(*socket));
	size_t i = 0;
	log_assert(socket);
	log_assert(unit_tap_datas_max>2); /* needed for the test */
	for(i=0; i<unit_tap_datas_max; i++) {
		datas[i] = calloc(1, sizeof(struct tap_data));
		log_assert(datas[i]);
		log_assert(tap_data_list_insert(&socket->data_list, datas[i]));
	}
	/* sanity base check */
	list = socket->data_list;
	for(i=0; list; i++) list = list->next;
	log_assert(i==unit_tap_datas_max);

	/* Free the last data, tail cannot be erased */
	list = socket->data_list;
	while(list->next) list = list->next;
	free(list->d);
	list->d = NULL;
	tap_data_list_try_to_free_tail(list);
	list = socket->data_list;
	for(i=0; list; i++) list = list->next;
	log_assert(i==unit_tap_datas_max);

	/* Free the third to last data, tail cannot be erased */
	list = socket->data_list;
	for(i=0; i<unit_tap_datas_max-3; i++) list = list->next;
	free(list->d);
	list->d = NULL;
	tap_data_list_try_to_free_tail(list);
	list = socket->data_list;
	for(i=0; list; i++) list = list->next;
	log_assert(i==unit_tap_datas_max);

	/* Free the second to last data, try to remove tail from the third
	 * again, tail (last 2) should be removed */
	list = socket->data_list;
	for(i=0; i<unit_tap_datas_max-2; i++) list = list->next;
	free(list->d);
	list->d = NULL;
	list = socket->data_list;
	while(list->d) list = list->next;
	tap_data_list_try_to_free_tail(list);
	list = socket->data_list;
	for(i=0; list; i++) list = list->next;
	log_assert(i==unit_tap_datas_max-2);

	/* Free all the remaining data, try to remove tail from the start,
	 * only the start should remain */
	list = socket->data_list;
	while(list) {
		free(list->d);
		list->d = NULL;
		list = list->next;
	}
	tap_data_list_try_to_free_tail(socket->data_list);
	list = socket->data_list;
	for(i=0; list; i++) list = list->next;
	log_assert(i==1);

	/* clean up */
	tap_data_list_delete(socket->data_list);
	free(socket);

	/* Start again. Add two elements */
	socket = calloc(1, sizeof(*socket));
	log_assert(socket);
	for(i=0; i<2; i++) {
		datas[i] = calloc(1, sizeof(struct tap_data));
		log_assert(datas[i]);
		log_assert(tap_data_list_insert(&socket->data_list, datas[i]));
	}
	/* sanity base check */
	list = socket->data_list;
	for(i=0; list; i++) list = list->next;
	log_assert(i==2);

	/* Free the last data, tail cannot be erased */
	list = socket->data_list;
	while(list->next) list = list->next;
	free(list->d);
	list->d = NULL;
	tap_data_list_try_to_free_tail(list);
	list = socket->data_list;
	for(i=0; list; i++) list = list->next;
	log_assert(i==2);

	/* clean up */
	tap_data_list_delete(socket->data_list);
	free(socket);

	if(log_get_lock()) {
		lock_basic_destroy((lock_basic_type*)log_get_lock());
	}
	checklock_stop();
#ifdef USE_WINSOCK
	WSACleanup();
#endif
	return 0;
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** main program for streamtcp */
int main(int argc, char** argv) 
{
	int c;
	int usessl = 0;
	struct config_strlist_head local_list;
	struct config_strlist_head tcp_list;
	struct config_strlist_head tls_list;
	char* server_key = NULL, *server_cert = NULL, *verifypem = NULL;
#ifdef USE_WINSOCK
	WSADATA wsa_data;
	if(WSAStartup(MAKEWORD(2,2), &wsa_data) != 0) {
		printf("WSAStartup failed\n");
		return 1;
	}
#endif
	if(signal(SIGINT, main_sigh) == SIG_ERR ||
#ifdef SIGQUIT
		signal(SIGQUIT, main_sigh) == SIG_ERR ||
#endif
#ifdef SIGHUP
		signal(SIGHUP, main_sigh) == SIG_ERR ||
#endif
#ifdef SIGBREAK
		signal(SIGBREAK, main_sigh) == SIG_ERR ||
#endif
		signal(SIGTERM, main_sigh) == SIG_ERR)
		fatal_exit("could not bind to signal");
	memset(&local_list, 0, sizeof(local_list));
	memset(&tcp_list, 0, sizeof(tcp_list));
	memset(&tls_list, 0, sizeof(tls_list));

	/* lock debug start (if any) */
	checklock_start();
	log_ident_set("unbound-dnstap-socket");
	log_init(0, 0, 0);

#ifdef SIGPIPE
	if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("could not install signal handler for SIGPIPE");
		return 1;
	}
#endif

	/* command line options */
	while( (c=getopt(argc, argv, "hcls:t:u:vx:y:z:")) != -1) {
		switch(c) {
			case 'u':
				if(!cfg_strlist_append(&local_list,
					strdup(optarg)))
					fatal_exit("out of memory");
				break;
			case 's':
				if(!cfg_strlist_append(&tcp_list,
					strdup(optarg)))
					fatal_exit("out of memory");
				break;
			case 't':
				if(!cfg_strlist_append(&tls_list,
					strdup(optarg)))
					fatal_exit("out of memory");
				usessl = 1;
				break;
			case 'x':
				server_key = optarg;
				usessl = 1;
				break;
			case 'y':
				server_cert = optarg;
				usessl = 1;
				break;
			case 'z':
				verifypem = optarg;
				usessl = 1;
				break;
			case 'l':
				longformat = 1;
				break;
			case 'v':
				verbosity++;
				break;
			case 'c':
#ifndef UNBOUND_DEBUG
				fatal_exit("-c option needs compilation with "
					"--enable-debug");
#endif
				return internal_unittest();
			case 'h':
			case '?':
			default:
				usage(argv);
		}
	}
	/* argc -= optind; not using further arguments */
	/* argv += optind; not using further arguments */

	if(usessl) {
#ifdef HAVE_SSL
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
		ERR_load_SSL_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
#  ifndef S_SPLINT_S
		OpenSSL_add_all_algorithms();
#  endif
#else
		OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
			| OPENSSL_INIT_ADD_ALL_DIGESTS
			| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
		(void)SSL_library_init();
#else
		(void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
#endif
#endif /* HAVE_SSL */
	}
	setup_and_run(&local_list, &tcp_list, &tls_list, server_key,
		server_cert, verifypem);
	config_delstrlist(local_list.first);
	config_delstrlist(tcp_list.first);
	config_delstrlist(tls_list.first);

	if(log_get_lock()) {
		lock_basic_destroy((lock_basic_type*)log_get_lock());
	}
	checklock_stop();
#ifdef USE_WINSOCK
	WSACleanup();
#endif
	return 0;
}

/***--- definitions to make fptr_wlist work. ---***/
/* These are callbacks, similar to smallapp callbacks, except the debug
 * tool callbacks are not in it */
struct tube;
struct query_info;
#include "util/data/packed_rrset.h"
#include "daemon/worker.h"
#include "daemon/remote.h"
#include "util/fptr_wlist.h"
#include "libunbound/context.h"

void worker_handle_control_cmd(struct tube* ATTR_UNUSED(tube),
	uint8_t* ATTR_UNUSED(buffer), size_t ATTR_UNUSED(len),
	int ATTR_UNUSED(error), void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

int worker_handle_request(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

int worker_handle_service_reply(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

int remote_accept_callback(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

int remote_control_callback(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

void worker_sighandler(int ATTR_UNUSED(sig), void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

struct outbound_entry* worker_send_query(
	struct query_info* ATTR_UNUSED(qinfo), uint16_t ATTR_UNUSED(flags),
	int ATTR_UNUSED(dnssec), int ATTR_UNUSED(want_dnssec),
	int ATTR_UNUSED(nocaps), int ATTR_UNUSED(check_ratelimit),
	struct sockaddr_storage* ATTR_UNUSED(addr),
	socklen_t ATTR_UNUSED(addrlen), uint8_t* ATTR_UNUSED(zone),
	size_t ATTR_UNUSED(zonelen), int ATTR_UNUSED(tcp_upstream),
	int ATTR_UNUSED(ssl_upstream), char* ATTR_UNUSED(tls_auth_name),
	struct module_qstate* ATTR_UNUSED(q), int* ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
	return 0;
}

#ifdef UB_ON_WINDOWS
void
worker_win_stop_cb(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev), void* 
	ATTR_UNUSED(arg)) {
	log_assert(0);
}

void
wsvc_cron_cb(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}
#endif /* UB_ON_WINDOWS */

void 
worker_alloc_cleanup(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

struct outbound_entry* libworker_send_query(
	struct query_info* ATTR_UNUSED(qinfo), uint16_t ATTR_UNUSED(flags),
	int ATTR_UNUSED(dnssec), int ATTR_UNUSED(want_dnssec),
	int ATTR_UNUSED(nocaps), int ATTR_UNUSED(check_ratelimit),
	struct sockaddr_storage* ATTR_UNUSED(addr),
	socklen_t ATTR_UNUSED(addrlen), uint8_t* ATTR_UNUSED(zone),
	size_t ATTR_UNUSED(zonelen), int ATTR_UNUSED(tcp_upstream),
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
	struct sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

void libworker_bg_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode), 
	struct sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

void libworker_event_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode), 
	struct sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

int context_query_cmp(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
	log_assert(0);
	return 0;
}

void worker_stat_timer_cb(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void worker_probe_timer_cb(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void worker_start_accept(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void worker_stop_accept(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

/** keep track of lock id in lock-verify application */
struct order_id {
        /** the thread id that created it */
        int thr;
        /** the instance number of creation */
        int instance;
};

int order_lock_cmp(const void* e1, const void* e2)
{
        const struct order_id* o1 = e1;
        const struct order_id* o2 = e2;
        if(o1->thr < o2->thr) return -1;
        if(o1->thr > o2->thr) return 1;
        if(o1->instance < o2->instance) return -1;
        if(o1->instance > o2->instance) return 1;
        return 0;
}

int
codeline_cmp(const void* a, const void* b)
{
        return strcmp(a, b);
}

int replay_var_compare(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
        log_assert(0);
        return 0;
}

void remote_get_opt_ssl(char* ATTR_UNUSED(str), void* ATTR_UNUSED(arg))
{
        log_assert(0);
}

void fast_reload_service_cb(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev),
	void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

int fast_reload_client_callback(struct comm_point* ATTR_UNUSED(c),
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

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
