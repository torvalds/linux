/*
 * testcode/perf.c - debug program to estimate name server performance.
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
 * This program estimates DNS name server performance.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <signal.h>
#include "util/log.h"
#include "util/locks.h"
#include "util/net_help.h"
#include "util/data/msgencode.h"
#include "util/data/msgreply.h"
#include "util/data/msgparse.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "sldns/str2wire.h"
#include <sys/time.h>

/** usage information for perf */
static void usage(char* nm) 
{
	printf("usage: %s [options] server\n", nm);
	printf("server: ip address of server, IP4 or IP6.\n");
	printf("	If not on port %d add @port.\n", UNBOUND_DNS_PORT);
	printf("-d sec	duration of test in whole seconds (0: wait for ^C)\n");
	printf("-a str	query to ask, interpreted as a line from qfile\n");
	printf("-f fnm	query list to read from file\n");
	printf("	every line has format: qname qclass qtype [+-]{E}\n");
	printf("	where + means RD set, E means EDNS enabled\n");
	printf("-q 	quiet mode, print only final qps\n");
	exit(1);
}

struct perfinfo;
struct perfio;

/** Global info for perf */
struct perfinfo { 
	/** need to exit */
	volatile int exit;
	/** all purpose buffer (for UDP send and receive) */
	sldns_buffer* buf;

	/** destination */
	struct sockaddr_storage dest;
	/** length of dest socket addr */
	socklen_t destlen;

	/** when did this time slice start */
	struct timeval since;
	/** number of queries received in that time */
	size_t numrecv;
	/** number of queries sent out in that time */
	size_t numsent;

	/** duration of test in seconds */
	int duration;
	/** quiet mode? */
	int quiet;

	/** when did the total test start */
	struct timeval start;
	/** total number recvd */
	size_t total_recv;
	/** total number sent */
	size_t total_sent;
	/** numbers by rcode */
	size_t by_rcode[32];
	
	/** number of I/O ports */
	size_t io_num;
	/** I/O ports array */
	struct perfio* io;
	/** max fd value in io ports */
	int maxfd;
	/** readset */
	fd_set rset;

	/** size of querylist */
	size_t qlist_size;
	/** allocated size of qlist array */
	size_t qlist_capacity;
	/** list of query packets (data) */
	uint8_t** qlist_data;
	/** list of query packets (length of a packet) */
	size_t* qlist_len;
	/** index into querylist, for walking the list */
	size_t qlist_idx;
};

/** I/O port for perf */
struct perfio {
	/** id number */
	size_t id;
	/** file descriptor of socket */
	int fd;
	/** timeout value */
	struct timeval timeout;
	/** ptr back to perfinfo */
	struct perfinfo* info;
};

/** number of msec between starting io ports */
#define START_IO_INTERVAL 10
/** number of msec timeout on io ports */
#define IO_TIMEOUT 10

/** signal handler global info */
static struct perfinfo* sig_info;

/** signal handler for user quit */
static RETSIGTYPE perf_sigh(int sig)
{
	log_assert(sig_info);
	if(!sig_info->quiet)
		printf("exit on signal %d\n", sig);
	sig_info->exit = 1;
}

/** timeval compare, t1 < t2 */
static int
perf_tv_smaller(struct timeval* t1, struct timeval* t2) 
{
#ifndef S_SPLINT_S
	if(t1->tv_sec < t2->tv_sec)
		return 1;
	if(t1->tv_sec == t2->tv_sec &&
		t1->tv_usec < t2->tv_usec)
		return 1;
#endif
	return 0;
}

/** timeval add, t1 += t2 */
static void
perf_tv_add(struct timeval* t1, struct timeval* t2) 
{
#ifndef S_SPLINT_S
	t1->tv_sec += t2->tv_sec;
	t1->tv_usec += t2->tv_usec;
	while(t1->tv_usec >= 1000000) {
		t1->tv_usec -= 1000000;
		t1->tv_sec++;
	}
#endif
}

/** timeval subtract, t1 -= t2 */
static void
perf_tv_subtract(struct timeval* t1, struct timeval* t2) 
{
#ifndef S_SPLINT_S
	t1->tv_sec -= t2->tv_sec;
	if(t1->tv_usec >= t2->tv_usec) {
		t1->tv_usec -= t2->tv_usec;
	} else {
		t1->tv_sec--;
		t1->tv_usec = 1000000-(t2->tv_usec-t1->tv_usec);
	}
#endif
}


/** setup perf test environment */
static void
perfsetup(struct perfinfo* info)
{
	size_t i;
	if(gettimeofday(&info->start, NULL) < 0)
		fatal_exit("gettimeofday: %s", strerror(errno));
	sig_info = info;
	if( signal(SIGINT, perf_sigh) == SIG_ERR || 
#ifdef SIGQUIT
		signal(SIGQUIT, perf_sigh) == SIG_ERR ||
#endif
#ifdef SIGHUP
		signal(SIGHUP, perf_sigh) == SIG_ERR ||
#endif
#ifdef SIGBREAK
		signal(SIGBREAK, perf_sigh) == SIG_ERR ||
#endif
		signal(SIGTERM, perf_sigh) == SIG_ERR)
		fatal_exit("could not bind to signal");
	info->io = (struct perfio*)calloc(info->io_num, sizeof(struct perfio));
	if(!info->io) fatal_exit("out of memory");
#ifndef S_SPLINT_S
	FD_ZERO(&info->rset);
#endif
	info->since = info->start;
	for(i=0; i<info->io_num; i++) {
		info->io[i].id = i;
		info->io[i].info = info;
		info->io[i].fd = socket(
			addr_is_ip6(&info->dest, info->destlen)?
			AF_INET6:AF_INET, SOCK_DGRAM, 0);
		if(info->io[i].fd == -1) {
			fatal_exit("socket: %s", sock_strerror(errno));
		}
		if(info->io[i].fd > info->maxfd)
			info->maxfd = info->io[i].fd;
#ifndef S_SPLINT_S
		FD_SET(FD_SET_T info->io[i].fd, &info->rset);
		info->io[i].timeout.tv_usec = ((START_IO_INTERVAL*i)%1000)
						*1000;
		info->io[i].timeout.tv_sec = (START_IO_INTERVAL*i)/1000;
		perf_tv_add(&info->io[i].timeout, &info->since);
#endif
	}
}

/** cleanup perf test environment */
static void
perffree(struct perfinfo* info)
{
	size_t i;
	if(!info) return;
	if(info->io) {
		for(i=0; i<info->io_num; i++) {
			sock_close(info->io[i].fd);
		}
		free(info->io);
	}
	for(i=0; i<info->qlist_size; i++)
		free(info->qlist_data[i]);
	free(info->qlist_data);
	free(info->qlist_len);
}

/** send new query for io */
static void
perfsend(struct perfinfo* info, size_t n, struct timeval* now)
{
	ssize_t r;
	r = sendto(info->io[n].fd, (void*)info->qlist_data[info->qlist_idx],
		info->qlist_len[info->qlist_idx], 0,
		(struct sockaddr*)&info->dest, info->destlen);
	/*log_hex("send", info->qlist_data[info->qlist_idx],
		info->qlist_len[info->qlist_idx]);*/
	if(r == -1) {
		log_err("sendto: %s", sock_strerror(errno));
	} else if(r != (ssize_t)info->qlist_len[info->qlist_idx]) {
		log_err("partial sendto");
	}
	info->qlist_idx = (info->qlist_idx+1) % info->qlist_size;
	info->numsent++;

	info->io[n].timeout.tv_sec = IO_TIMEOUT/1000;
	info->io[n].timeout.tv_usec = (IO_TIMEOUT%1000)*1000;
	perf_tv_add(&info->io[n].timeout, now);
}

/** got reply for io */
static void
perfreply(struct perfinfo* info, size_t n, struct timeval* now)
{
	ssize_t r;
	r = recv(info->io[n].fd, (void*)sldns_buffer_begin(info->buf),
		sldns_buffer_capacity(info->buf), 0);
	if(r == -1) {
		log_err("recv: %s", sock_strerror(errno));
	} else {
		info->by_rcode[LDNS_RCODE_WIRE(sldns_buffer_begin(
			info->buf))]++;
		info->numrecv++;
	}
	/*sldns_buffer_set_limit(info->buf, r);
	log_buf(0, "reply", info->buf);*/
	perfsend(info, n, now);
}

/** got timeout for io */
static void
perftimeout(struct perfinfo* info, size_t n, struct timeval* now)
{
	/* may not be a dropped packet, this is also used to start
	 * up the sending IOs */
	perfsend(info, n, now);
}

/** print nice stats about qps */
static void
stat_printout(struct perfinfo* info, struct timeval* now, 
	struct timeval* elapsed)
{
	/* calculate qps */
	double dt, qps = 0;
#ifndef S_SPLINT_S
	dt = (double)(elapsed->tv_sec*1000000 + elapsed->tv_usec) / 1000000;
#endif
	if(dt > 0.001)
		qps = (double)(info->numrecv) / dt;
	if(!info->quiet)
		printf("qps: %g\n", qps);
	/* setup next slice */
	info->since = *now;
	info->total_sent += info->numsent;
	info->total_recv += info->numrecv;
	info->numrecv = 0;
	info->numsent = 0;
}

/** wait for new events for performance test */
static void
perfselect(struct perfinfo* info)
{
	fd_set rset = info->rset;
	struct timeval timeout, now;
	int num;
	size_t i;
	if(gettimeofday(&now, NULL) < 0)
		fatal_exit("gettimeofday: %s", strerror(errno));
	/* time to exit? */
	if(info->duration > 0) {
		timeout = now;
		perf_tv_subtract(&timeout, &info->start);
		if((int)timeout.tv_sec >= info->duration) {
			info->exit = 1;
			return;
		}
	}
	/* time for stats printout? */
	timeout = now;
	perf_tv_subtract(&timeout, &info->since);
	if(timeout.tv_sec > 0) {
		stat_printout(info, &now, &timeout);
	}
	/* see what is closest port to timeout; or if there is a timeout */
	timeout = info->io[0].timeout;
	for(i=0; i<info->io_num; i++) {
		if(perf_tv_smaller(&info->io[i].timeout, &now)) {
			perftimeout(info, i, &now);
			return;
		}
		if(perf_tv_smaller(&info->io[i].timeout, &timeout)) {
			timeout = info->io[i].timeout;
		}
	}
	perf_tv_subtract(&timeout, &now);
	
	num = select(info->maxfd+1, &rset, NULL, NULL, &timeout);
	if(num == -1) {
		if(errno == EAGAIN || errno == EINTR)
			return;
		log_err("select: %s", strerror(errno));
	}

	/* handle new events */
	for(i=0; num && i<info->io_num; i++) {
		if(FD_ISSET(info->io[i].fd, &rset)) {
			perfreply(info, i, &now);
			num--;
		}
	}
}

/** show end stats */
static void
perfendstats(struct perfinfo* info)
{
	double dt, qps;
	struct timeval timeout, now;
	int i, lost; 
	if(gettimeofday(&now, NULL) < 0)
		fatal_exit("gettimeofday: %s", strerror(errno));
	timeout = now;
	perf_tv_subtract(&timeout, &info->since);
	stat_printout(info, &now, &timeout);
	
	timeout = now;
	perf_tv_subtract(&timeout, &info->start);
	dt = (double)(timeout.tv_sec*1000000 + timeout.tv_usec) / 1000000.0;
	qps = (double)(info->total_recv) / dt;
	lost = (int)(info->total_sent - info->total_recv) - (int)info->io_num;
	if(!info->quiet) {
		printf("overall time: 	%g sec\n", 
			(double)timeout.tv_sec + 
			(double)timeout.tv_usec/1000000.);
		if(lost > 0) 
			printf("Packets lost: 	%d\n", (int)lost);
	
		for(i=0; i<(int)(sizeof(info->by_rcode)/sizeof(size_t)); i++)
		{
			if(info->by_rcode[i] > 0) {
				char rc[16];
				sldns_wire2str_rcode_buf(i, rc, sizeof(rc));
				printf("%d(%5s): 	%u replies\n",
					i, rc, (unsigned)info->by_rcode[i]);
			}
		}
	}
	printf("average qps: 	%g\n", qps);
}

/** perform the performance test */
static void
perfmain(struct perfinfo* info)
{
	perfsetup(info);
	while(!info->exit) {
		perfselect(info);
	}
	perfendstats(info);
	perffree(info);
}

/** parse a query line to a packet into buffer */
static int
qlist_parse_line(sldns_buffer* buf, char* p)
{
	char nm[1024], cl[1024], tp[1024], fl[1024];
	int r; 
	int rec = 1, edns = 0;
	struct query_info qinfo;
	nm[0] = 0; cl[0] = 0; tp[0] = 0; fl[0] = 0;
	r = sscanf(p, " %1023s %1023s %1023s %1023s", nm, cl, tp, fl);
	if(r != 3 && r != 4)
		return 0;
	/*printf("nm='%s', cl='%s', tp='%s', fl='%s'\n", nm, cl, tp, fl);*/
	if(strcmp(tp, "IN") == 0 || strcmp(tp, "CH") == 0) {
		qinfo.qtype = sldns_get_rr_type_by_name(cl);
		qinfo.qclass = sldns_get_rr_class_by_name(tp);
		if((qinfo.qtype == 0 && strcmp(cl, "TYPE0") != 0) ||
			(qinfo.qclass == 0 && strcmp(tp, "CLASS0") != 0)) {
			return 0;
		}
	} else {
		qinfo.qtype = sldns_get_rr_type_by_name(tp);
		qinfo.qclass = sldns_get_rr_class_by_name(cl);
		if((qinfo.qtype == 0 && strcmp(tp, "TYPE0") != 0) ||
			(qinfo.qclass == 0 && strcmp(cl, "CLASS0") != 0)) {
			return 0;
		}
	}
	if(fl[0] == '+') rec = 1;
	else if(fl[0] == '-') rec = 0;
	else if(fl[0] == 'E') edns = 1;
	if((fl[0] == '+' || fl[0] == '-') && fl[1] == 'E')
		edns = 1;
	qinfo.qname = sldns_str2wire_dname(nm, &qinfo.qname_len);
	if(!qinfo.qname)
		return 0;
	qinfo.local_alias = NULL;
	qinfo_query_encode(buf, &qinfo);
	sldns_buffer_write_u16_at(buf, 0, 0); /* zero ID */
	if(rec) LDNS_RD_SET(sldns_buffer_begin(buf));
	if(edns) {
		struct edns_data ed;
		memset(&ed, 0, sizeof(ed));
		ed.edns_present = 1;
		ed.udp_size = EDNS_ADVERTISED_SIZE;
		/* Set DO bit in all EDNS datagrams ... */
		ed.bits = EDNS_DO;
		attach_edns_record(buf, &ed);
	}
	free(qinfo.qname);
	return 1;
}

/** grow query list capacity */
static void
qlist_grow_capacity(struct perfinfo* info)
{
	size_t newcap = (size_t)((info->qlist_capacity==0)?16:
		info->qlist_capacity*2);
	uint8_t** d = (uint8_t**)calloc(newcap, sizeof(uint8_t*));
	size_t* l = (size_t*)calloc(newcap, sizeof(size_t));
	if(!d || !l) fatal_exit("out of memory");
	if(info->qlist_data && info->qlist_capacity)
		memcpy(d, info->qlist_data, sizeof(uint8_t*)*
			info->qlist_capacity);
	if(info->qlist_len && info->qlist_capacity)
		memcpy(l, info->qlist_len, sizeof(size_t)*
			info->qlist_capacity);
	free(info->qlist_data);
	free(info->qlist_len);
	info->qlist_data = d;
	info->qlist_len = l;
	info->qlist_capacity = newcap;
}

/** setup query list in info */
static void
qlist_add_line(struct perfinfo* info, char* line, int no)
{
	if(!qlist_parse_line(info->buf, line)) {
		printf("error parsing query %d: %s\n", no, line);
		exit(1);
	}
	sldns_buffer_write_u16_at(info->buf, 0, (uint16_t)info->qlist_size); 
	if(info->qlist_size + 1 > info->qlist_capacity) {
		qlist_grow_capacity(info);
	}
	info->qlist_len[info->qlist_size] = sldns_buffer_limit(info->buf);
	info->qlist_data[info->qlist_size] = memdup(
		sldns_buffer_begin(info->buf), sldns_buffer_limit(info->buf));
	if(!info->qlist_data[info->qlist_size])
		fatal_exit("out of memory");
	info->qlist_size ++;
}

/** setup query list in info */
static void
qlist_read_file(struct perfinfo* info, char* fname)
{
	char buf[1024];
	char *p;
	FILE* in = fopen(fname, "r");
	int lineno = 0;
	if(!in) {
		perror(fname);
		exit(1);
	}
	while(fgets(buf, (int)sizeof(buf), in)) {
		lineno++;
		buf[sizeof(buf)-1] = 0;
		p = buf;
		while(*p == ' ' || *p == '\t')
			p++;
		if(p[0] == 0 || p[0] == '\n' || p[0] == ';' || p[0] == '#')
			continue;
		qlist_add_line(info, p, lineno);
	}
	printf("Read %s, got %u queries\n", fname, (unsigned)info->qlist_size);
	fclose(in);
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** main program for perf */
int main(int argc, char* argv[]) 
{
	char* nm = argv[0];
	int c;
	struct perfinfo info;
#ifdef USE_WINSOCK
	int r;
	WSADATA wsa_data;
#endif

	/* defaults */
	memset(&info, 0, sizeof(info));
	info.io_num = 16;

	checklock_start();
	log_init(NULL, 0, NULL);
	log_ident_set("perf");
#ifdef USE_WINSOCK
	if((r = WSAStartup(MAKEWORD(2,2), &wsa_data)) != 0)
		fatal_exit("WSAStartup failed: %s", wsa_strerror(r));
#endif

	info.buf = sldns_buffer_new(65553);
	if(!info.buf) fatal_exit("out of memory");

	/* parse the options */
	while( (c=getopt(argc, argv, "d:ha:f:q")) != -1) {
		switch(c) {
		case 'q':
			info.quiet = 1;
			break;
		case 'd':
			if(atoi(optarg)==0 && strcmp(optarg, "0")!=0) {
				printf("-d not a number %s", optarg);
				exit(1);
			}
			info.duration = atoi(optarg);
			break;
		case 'a':
			qlist_add_line(&info, optarg, 0);
			break;
		case 'f':
			qlist_read_file(&info, optarg);
			break;
		case '?':
		case 'h':
		default:
			usage(nm);
		}
	}
	argc -= optind;
	argv += optind;

	if(argc != 1) {
		printf("error: pass server IP address on commandline.\n");
		usage(nm);
	}
	if(!extstrtoaddr(argv[0], &info.dest, &info.destlen, UNBOUND_DNS_PORT)) {
		printf("Could not parse ip: %s\n", argv[0]);
		exit(1);
	}
	if(info.qlist_size == 0) {
		printf("No queries to make, use -f or -a.\n");
		exit(1);
	}
	
	/* do the performance test */
	perfmain(&info);

	sldns_buffer_free(info.buf);
#ifdef USE_WINSOCK
	WSACleanup();
#endif
	checklock_stop();
	return 0;
}
