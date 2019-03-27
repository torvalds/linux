/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2001-2003 Roman V. Palagin <romanp@unshadow.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $SourceForge: flowctl.c,v 1.15 2004/08/31 20:24:58 glebius Exp $
 */

#ifndef lint
static const char rcs_id[] =
    "@(#) $FreeBSD$";
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <net/if.h>
#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <netgraph.h>
#include <netgraph/netflow/ng_netflow.h>

#define	CISCO_SH_FLOW_HEADER	"SrcIf         SrcIPaddress    " \
"DstIf         DstIPaddress    Pr SrcP DstP  Pkts\n"
#define	CISCO_SH_FLOW	"%-13s %-15s %-13s %-15s %2u %4.4x %4.4x %6lu\n"

/* human-readable IPv4 header */
#define	CISCO_SH_FLOW_HHEADER	"SrcIf         SrcIPaddress    " \
"DstIf         DstIPaddress    Proto  SrcPort  DstPort     Pkts\n"
#define	CISCO_SH_FLOW_H	"%-13s %-15s %-13s %-15s %5u %8d %8d %8lu\n"

#define	CISCO_SH_FLOW6_HEADER	"SrcIf         SrcIPaddress                   " \
"DstIf         DstIPaddress                   Pr SrcP DstP  Pkts\n"
#define	CISCO_SH_FLOW6		"%-13s %-30s %-13s %-30s %2u %4.4x %4.4x %6lu\n"

/* Human-readable IPv6 headers */
#define	CISCO_SH_FLOW6_HHEADER	"SrcIf         SrcIPaddress                         " \
"DstIf         DstIPaddress                         Proto  SrcPort  DstPort     Pkts\n"
#define	CISCO_SH_FLOW6_H	"%-13s %-36s %-13s %-36s %5u %8d %8d %8lu\n"

#define	CISCO_SH_VERB_FLOW_HEADER "SrcIf          SrcIPaddress    " \
"DstIf          DstIPaddress    Pr TOS Flgs  Pkts\n" \
"Port Msk AS                    Port Msk AS    NextHop              B/Pk  Active\n"

#define	CISCO_SH_VERB_FLOW "%-14s %-15s %-14s %-15s %2u %3x %4x %6lu\n" \
	"%4.4x /%-2u %-5u                 %4.4x /%-2u %-5u %-15s %9u %8u\n\n"

#define	CISCO_SH_VERB_FLOW6_HEADER "SrcIf          SrcIPaddress                   " \
"DstIf          DstIPaddress                   Pr TOS Flgs  Pkts\n" \
"Port Msk AS                    Port Msk AS    NextHop                             B/Pk  Active\n"

#define	CISCO_SH_VERB_FLOW6 "%-14s %-30s %-14s %-30s %2u %3x %4x %6lu\n" \
	"%4.4x /%-2u %-5u                 %4.4x /%-2u %-5u %-30s %9u %8u\n\n"
#ifdef INET
static void flow_cache_print(struct ngnf_show_header *resp);
static void flow_cache_print_verbose(struct ngnf_show_header *resp);
#endif
#ifdef INET6 
static void flow_cache_print6(struct ngnf_show_header *resp);
static void flow_cache_print6_verbose(struct ngnf_show_header *resp);
#endif
static void ctl_show(int, char **);
#if defined(INET) || defined(INET6)
static void do_show(int, void (*func)(struct ngnf_show_header *));
#endif
static void help(void);
static void execute_command(int, char **);

struct ip_ctl_cmd {
	char	*cmd_name;
	void	(*cmd_func)(int argc, char **argv);
};

struct ip_ctl_cmd cmds[] = {
    {"show",	ctl_show},
    {NULL,	NULL},
};

int	cs, human = 0;
char	*ng_path;

int
main(int argc, char **argv)
{
	int c;
	char sname[NG_NODESIZ];
	int rcvbuf = SORCVBUF_SIZE;

	/* parse options */
	while ((c = getopt(argc, argv, "d:")) != -1) {
		switch (c) {
		case 'd':	/* set libnetgraph debug level. */
			NgSetDebug(atoi(optarg));
			break;
		}
	}

	argc -= optind;
	argv += optind;
	ng_path = argv[0];
	if (ng_path == NULL || (strlen(ng_path) > NG_PATHSIZ))
		help();
	argc--;
	argv++;

	/* create control socket. */
	snprintf(sname, sizeof(sname), "flowctl%i", getpid());

	if (NgMkSockNode(sname, &cs, NULL) == -1)
		err(1, "NgMkSockNode");

	/* set receive buffer size */
	if (setsockopt(cs, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int)) == -1)
		err(1, "setsockopt(SOL_SOCKET, SO_RCVBUF)");

	/* parse and execute command */
	execute_command(argc, argv);

	close(cs);
	
	exit(0);
}

static void
execute_command(int argc, char **argv)
{
	int cindex = -1;
	int i;

	if (!argc)
		help();
	for (i = 0; cmds[i].cmd_name != NULL; i++)
		if (!strncmp(argv[0], cmds[i].cmd_name, strlen(argv[0]))) {
			if (cindex != -1)
				errx(1, "ambiguous command: %s", argv[0]);
			cindex = i;
		}
	if (cindex == -1)
		errx(1, "bad command: %s", argv[0]);
	argc--;
	argv++;
	(*cmds[cindex].cmd_func)(argc, argv);
}

static void
ctl_show(int argc, char **argv)
{
	int ipv4, ipv6, verbose = 0;

	ipv4 = feature_present("inet");
	ipv6 = feature_present("inet6");

	if (argc > 0 && !strncmp(argv[0], "ipv4", 4)) {
		ipv6 = 0;
		argc--;
		argv++;
	}
	if (argc > 0 && !strncmp(argv[0], "ipv6", 4)) {
		ipv4 = 0;
		argc--;
		argv++;
	}

	if (argc > 0 && !strncmp(argv[0], "verbose", strlen(argv[0])))
		verbose = 1;

	if (argc > 0 && !strncmp(argv[0], "human", strlen(argv[0])))
		human = 1;

#ifdef INET
	if (ipv4) {
		if (verbose)
			do_show(4, &flow_cache_print_verbose);
		else
			do_show(4, &flow_cache_print);
	}
#endif

#ifdef INET6
	if (ipv6) {
		if (verbose)
			do_show(6, &flow_cache_print6_verbose);
		else
			do_show(6, &flow_cache_print6);
	}
#endif
}

#if defined(INET) || defined(INET6)
static void
do_show(int version, void (*func)(struct ngnf_show_header *))
{
	char buf[SORCVBUF_SIZE];
	struct ng_mesg *ng_mesg;
	struct ngnf_show_header req, *resp;
	int token, nread;

	ng_mesg = (struct ng_mesg *)buf;
	req.version = version;
	req.hash_id = req.list_id = 0;

	for (;;) {
		/* request set of accounting records */
		token = NgSendMsg(cs, ng_path, NGM_NETFLOW_COOKIE,
		    NGM_NETFLOW_SHOW, (void *)&req, sizeof(req));
		if (token == -1)
			err(1, "NgSendMsg(NGM_NETFLOW_SHOW)");

		/* read reply */
		nread = NgRecvMsg(cs, ng_mesg, SORCVBUF_SIZE, NULL);
		if (nread == -1)
			err(1, "NgRecvMsg() failed");

		if (ng_mesg->header.token != token)
			err(1, "NgRecvMsg(NGM_NETFLOW_SHOW): token mismatch");

		resp = (struct ngnf_show_header *)ng_mesg->data;
		if ((ng_mesg->header.arglen < (sizeof(*resp))) ||
		    (ng_mesg->header.arglen < (sizeof(*resp) +
		    (resp->nentries * sizeof(struct flow_entry_data)))))
			err(1, "NgRecvMsg(NGM_NETFLOW_SHOW): arglen too small");

		(*func)(resp);

		if (resp->hash_id != 0)
			req.hash_id = resp->hash_id;
		else
			break;
		req.list_id = resp->list_id;
	}
}
#endif

#ifdef INET
static void
flow_cache_print(struct ngnf_show_header *resp)
{
	struct flow_entry_data *fle;
	char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
	char src_if[IFNAMSIZ], dst_if[IFNAMSIZ];
	int i;

	if (resp->version != 4)
		errx(EX_SOFTWARE, "%s: version mismatch: %u",
		    __func__, resp->version);

	if (resp->nentries > 0)
		printf(human ? CISCO_SH_FLOW_HHEADER : CISCO_SH_FLOW_HEADER);

	fle = (struct flow_entry_data *)(resp + 1);
	for (i = 0; i < resp->nentries; i++, fle++) {
		inet_ntop(AF_INET, &fle->r.r_src, src, sizeof(src));
		inet_ntop(AF_INET, &fle->r.r_dst, dst, sizeof(dst));
		printf(human ? CISCO_SH_FLOW_H : CISCO_SH_FLOW,
			if_indextoname(fle->fle_i_ifx, src_if),
			src,
			if_indextoname(fle->fle_o_ifx, dst_if),
			dst,
			fle->r.r_ip_p,
			ntohs(fle->r.r_sport),
			ntohs(fle->r.r_dport),
			fle->packets);
			
	}
}
#endif

#ifdef INET6
static void
flow_cache_print6(struct ngnf_show_header *resp)
{
	struct flow6_entry_data *fle6;
	char src6[INET6_ADDRSTRLEN], dst6[INET6_ADDRSTRLEN];
	char src_if[IFNAMSIZ], dst_if[IFNAMSIZ];
	int i;

	if (resp->version != 6)
		errx(EX_SOFTWARE, "%s: version mismatch: %u",
		    __func__, resp->version);

	if (resp->nentries > 0)
		printf(human ? CISCO_SH_FLOW6_HHEADER : CISCO_SH_FLOW6_HEADER);

	fle6 = (struct flow6_entry_data *)(resp + 1);
	for (i = 0; i < resp->nentries; i++, fle6++) {
		inet_ntop(AF_INET6, &fle6->r.src.r_src6, src6, sizeof(src6));
		inet_ntop(AF_INET6, &fle6->r.dst.r_dst6, dst6, sizeof(dst6));
		printf(human ? CISCO_SH_FLOW6_H : CISCO_SH_FLOW6,
			if_indextoname(fle6->fle_i_ifx, src_if),
			src6,
			if_indextoname(fle6->fle_o_ifx, dst_if),
			dst6,
			fle6->r.r_ip_p,
			ntohs(fle6->r.r_sport),
			ntohs(fle6->r.r_dport),
			fle6->packets);
			
	}
}
#endif

#ifdef INET
static void
flow_cache_print_verbose(struct ngnf_show_header *resp)
{
	struct flow_entry_data *fle;
	char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN], next[INET_ADDRSTRLEN];
	char src_if[IFNAMSIZ], dst_if[IFNAMSIZ];
	int i;

	if (resp->version != 4)
		errx(EX_SOFTWARE, "%s: version mismatch: %u",
		    __func__, resp->version);

	printf(CISCO_SH_VERB_FLOW_HEADER);

	fle = (struct flow_entry_data *)(resp + 1);
	for (i = 0; i < resp->nentries; i++, fle++) {
		inet_ntop(AF_INET, &fle->r.r_src, src, sizeof(src));
		inet_ntop(AF_INET, &fle->r.r_dst, dst, sizeof(dst));
		inet_ntop(AF_INET, &fle->next_hop, next, sizeof(next));
		printf(CISCO_SH_VERB_FLOW,
			if_indextoname(fle->fle_i_ifx, src_if),
			src,
			if_indextoname(fle->fle_o_ifx, dst_if),
			dst,
			fle->r.r_ip_p,
			fle->r.r_tos,
			fle->tcp_flags,
			fle->packets,
			ntohs(fle->r.r_sport),
			fle->src_mask,
			0,
			ntohs(fle->r.r_dport),
			fle->dst_mask,
			0,
			next,
			(u_int)(fle->bytes / fle->packets),
			0);
			
	}
}
#endif

#ifdef INET6
static void
flow_cache_print6_verbose(struct ngnf_show_header *resp)
{
	struct flow6_entry_data *fle6;
	char src6[INET6_ADDRSTRLEN], dst6[INET6_ADDRSTRLEN], next6[INET6_ADDRSTRLEN];
	char src_if[IFNAMSIZ], dst_if[IFNAMSIZ];
	int i;

	if (resp->version != 6)
		errx(EX_SOFTWARE, "%s: version mismatch: %u",
		    __func__, resp->version);

	printf(CISCO_SH_VERB_FLOW6_HEADER);

	fle6 = (struct flow6_entry_data *)(resp + 1);
	for (i = 0; i < resp->nentries; i++, fle6++) {
		inet_ntop(AF_INET6, &fle6->r.src.r_src6, src6, sizeof(src6));
		inet_ntop(AF_INET6, &fle6->r.dst.r_dst6, dst6, sizeof(dst6));
		inet_ntop(AF_INET6, &fle6->n.next_hop6, next6, sizeof(next6));
		printf(CISCO_SH_VERB_FLOW6,
			if_indextoname(fle6->fle_i_ifx, src_if),
			src6,
			if_indextoname(fle6->fle_o_ifx, dst_if),
			dst6,
			fle6->r.r_ip_p,
			fle6->r.r_tos,
			fle6->tcp_flags,
			fle6->packets,
			ntohs(fle6->r.r_sport),
			fle6->src_mask,
			0,
			ntohs(fle6->r.r_dport),
			fle6->dst_mask,
			0,
			next6,
			(u_int)(fle6->bytes / fle6->packets),
			0);
	}
}
#endif

static void
help(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-d level] nodename command\n", __progname);
	exit (0);
}
