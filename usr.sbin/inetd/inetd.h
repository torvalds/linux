/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/queue.h>

#include <netinet/in.h>

#include <stdio.h>

#define BUFSIZE 8192
#define LINESIZ 72

#define NORM_TYPE	0
#define MUX_TYPE	1
#define MUXPLUS_TYPE	2
#define FAITH_TYPE	4
#define ISMUX(sep)	(((sep)->se_type == MUX_TYPE) || \
			 ((sep)->se_type == MUXPLUS_TYPE))
#define ISMUXPLUS(sep)	((sep)->se_type == MUXPLUS_TYPE)

struct procinfo {
	LIST_ENTRY(procinfo) pr_link;
	pid_t		pr_pid;		/* child pid */
	struct conninfo	*pr_conn;
};

struct conninfo {
	LIST_ENTRY(conninfo) co_link;
	struct sockaddr_storage	co_addr;	/* source address */
	int		co_numchild;	/* current number of children */
	struct procinfo	**co_proc;	/* array of child proc entry */
};

#define PERIPSIZE	256

struct	servtab {
	char	*se_service;		/* name of service */
	int	se_socktype;		/* type of socket to use */
	int	se_family;		/* address family */
	char	*se_proto;		/* protocol used */
	int	se_maxchild;		/* max number of children */
	int	se_maxcpm;		/* max connects per IP per minute */
	int	se_numchild;		/* current number of children */
	pid_t	*se_pids;		/* array of child pids */
	char	*se_user;		/* user name to run as */
	char    *se_group;              /* group name to run as */
#ifdef  LOGIN_CAP
	char    *se_class;              /* login class name to run with */
#endif
	struct	biltin *se_bi;		/* if built-in, description */
	char	*se_server;		/* server program */
	char	*se_server_name;	/* server program without path */
#define	MAXARGV 20
	char	*se_argv[MAXARGV+1];	/* program arguments */
#ifdef IPSEC
	char	*se_policy;		/* IPsec policy string */
#endif
	int	se_fd;			/* open descriptor */
	union {				/* bound address */
		struct	sockaddr se_un_ctrladdr;
		struct	sockaddr_in se_un_ctrladdr4;
		struct	sockaddr_in6 se_un_ctrladdr6;
	        struct  sockaddr_un se_un_ctrladdr_un;
	} se_un;
#define se_ctrladdr	se_un.se_un_ctrladdr
#define se_ctrladdr4	se_un.se_un_ctrladdr4
#define se_ctrladdr6	se_un.se_un_ctrladdr6
#define se_ctrladdr_un   se_un.se_un_ctrladdr_un
  	socklen_t	se_ctrladdr_size;
	uid_t	se_sockuid;		/* Owner for unix domain socket */
	gid_t	se_sockgid;		/* Group for unix domain socket */
	mode_t	se_sockmode;		/* Mode for unix domain socket */
	u_char	se_type;		/* type: normal, mux, or mux+ */
	u_char	se_checked;		/* looked at during merge */
	u_char	se_accept;		/* i.e., wait/nowait mode */
	u_char	se_rpc;			/* ==1 if RPC service */
	int	se_rpc_prog;		/* RPC program number */
	u_int	se_rpc_lowvers;		/* RPC low version */
	u_int	se_rpc_highvers;	/* RPC high version */
	int	se_count;		/* number started since se_time */
	struct	timespec se_time;	/* start of se_count */
	struct	servtab *se_next;
	struct se_flags {
		u_int se_nomapped : 1;
		u_int se_reset : 1;
	} se_flags;
	int	se_maxperip;		/* max number of children per src */
	LIST_HEAD(, conninfo) se_conn[PERIPSIZE];
};

#define	se_nomapped		se_flags.se_nomapped
#define	se_reset		se_flags.se_reset

int		check_loop(const struct sockaddr *, const struct servtab *sep);
void		inetd_setproctitle(const char *, int);
struct servtab *tcpmux(int);

extern int	 debug;
extern struct servtab *servtab;

typedef void (bi_fn_t)(int, struct servtab *);

struct biltin {
	const char *bi_service;		/* internally provided service name */
	int	bi_socktype;		/* type of socket supported */
	short	bi_fork;		/* 1 if should fork before call */
	int	bi_maxchild;		/* max number of children, -1=default */
	bi_fn_t	*bi_fn;			/* function which performs it */
};
extern struct biltin biltins[];
