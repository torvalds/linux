/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)amq.c	8.1 (Berkeley) 6/7/93
 *	$Id: amq.c,v 1.22 2021/11/15 15:14:24 millert Exp $
 */

/*
 * Automounter query tool
 */

#include "am.h"
#include "amq.h"
#include <stdio.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>

static int privsock(int);

static int flush_flag;
static int minfo_flag;
static int unmount_flag;
static int stats_flag;
static int getvers_flag;
static char *debug_opts;
static char *logfile;
static char *xlog_optstr;
static char localhost[] = "localhost";
static char *def_server = localhost;

static struct timeval tmo = { 10, 0 };
#define	TIMEOUT tmo

enum show_opt { Full, Stats, Calc, Short, ShowDone };

/*
 * If (e) is Calc then just calculate the sizes
 * Otherwise display the mount node on stdout
 */
static void
show_mti(amq_mount_tree *mt, enum show_opt e, int *mwid, int *dwid,
    int *twid)
{
	switch (e) {
	case Calc: {
		int mw = strlen(mt->mt_mountinfo);
		int dw = strlen(mt->mt_directory);
		int tw = strlen(mt->mt_type);

		if (mw > *mwid)
			*mwid = mw;
		if (dw > *dwid)
			*dwid = dw;
		if (tw > *twid)
			*twid = tw;
		break;
	    }

	case Full: {
		time_t t = mt->mt_mounttime;

		struct tm *tp = localtime(&t);

		printf("%-*.*s %-*.*s %-*.*s %s\n\t%-5d %-7d %-6d"
		    " %-7d %-7d %-6d %02d/%02d/%02d %02d:%02d:%02d\n",
		    *dwid, *dwid, *mt->mt_directory ? mt->mt_directory : "/",
		    *twid, *twid, mt->mt_type, *mwid, *mwid, 
		    mt->mt_mountinfo, mt->mt_mountpoint, mt->mt_mountuid,
		    mt->mt_getattr, mt->mt_lookup, mt->mt_readdir,
		    mt->mt_readlink, mt->mt_statfs,
		    tp->tm_year > 99 ? tp->tm_year - 100 : tp->tm_year,
		    tp->tm_mon+1, tp->tm_mday,
		    tp->tm_hour, tp->tm_min, tp->tm_sec);
		break;
	    }

	case Stats: {
		time_t t = mt->mt_mounttime;

		struct tm *tp = localtime(&t);

		printf("%-*.*s %-5d %-7d %-6d %-7d %-7d %-6d"
		    " %02d/%02d/%02d %02d:%02d:%02d\n",
		    *dwid, *dwid, *mt->mt_directory ? mt->mt_directory : "/",
		    mt->mt_mountuid, mt->mt_getattr, mt->mt_lookup,
		    mt->mt_readdir, mt->mt_readlink, mt->mt_statfs,
		    tp->tm_year > 99 ? tp->tm_year - 100 : tp->tm_year,
		    tp->tm_mon+1, tp->tm_mday,
		    tp->tm_hour, tp->tm_min, tp->tm_sec);
		break;
	    }

	case Short: {
		printf("%-*.*s %-*.*s %-*.*s %s\n",
		    *dwid, *dwid, *mt->mt_directory ? mt->mt_directory : "/",
		    *twid, *twid, mt->mt_type, *mwid, *mwid,
		    mt->mt_mountinfo, mt->mt_mountpoint);
		break;
	    }

	default:
		break;
	}
}

/*
 * Display a mount tree.
 */
static void
show_mt(amq_mount_tree *mt, enum show_opt e, int *mwid, int *dwid,
    int *pwid)
{
	while (mt) {
		show_mti(mt, e, mwid, dwid, pwid);
		show_mt(mt->mt_next, e, mwid, dwid, pwid);
		mt = mt->mt_child;
	}
}

static void
show_mi(amq_mount_info_list *ml, enum show_opt e, int *mwid,
    int *dwid, int *twid)
{
	int i;

	switch (e) {
	case Calc: {
		for (i = 0; i < ml->amq_mount_info_list_len; i++) {
			amq_mount_info *mi = &ml->amq_mount_info_list_val[i];
			int mw = strlen(mi->mi_mountinfo);
			int dw = strlen(mi->mi_mountpt);
			int tw = strlen(mi->mi_type);

			if (mw > *mwid)
				*mwid = mw;
			if (dw > *dwid)
				*dwid = dw;
			if (tw > *twid)
				*twid = tw;
		}
		break;
	    }

	case Full: {
		for (i = 0; i < ml->amq_mount_info_list_len; i++) {
			amq_mount_info *mi = &ml->amq_mount_info_list_val[i];
			printf("%-*.*s %-*.*s %-*.*s %-3d %s is %s",
			    *mwid, *mwid, mi->mi_mountinfo,
			    *dwid, *dwid, mi->mi_mountpt,
			    *twid, *twid, mi->mi_type,
			    mi->mi_refc, mi->mi_fserver,
			    mi->mi_up > 0 ? "up" :
			    mi->mi_up < 0 ? "starting" : "down");
			if (mi->mi_error > 0) {
				printf(" (%s)", strerror(mi->mi_error));
			} else if (mi->mi_error < 0) {
				fputs(" (in progress)", stdout);
			}
			fputc('\n', stdout);
		}
		break;
	    }
	default:
		break;
	}
}

/*
 * Display general mount statistics
 */
static void
show_ms(amq_mount_stats *ms)
{
	printf("requests  stale     mount     mount     unmount\n"
	    "deferred  fhandles  ok        failed    failed\n"
	    "%-9d %-9d %-9d %-9d %-9d\n",
	    ms->as_drops, ms->as_stale, ms->as_mok, ms->as_merr, ms->as_uerr);
}

static bool_t
xdr_pri_free(xdrproc_t xdr_args, void *args_ptr)
{
	XDR xdr;

	xdr.x_op = XDR_FREE;
	return ((*xdr_args)(&xdr, args_ptr));
}

/*
 * MAIN
 */
int
main(int argc, char *argv[])
{
	int nodefault = 0, opt_ch, errs = 0, s;
	struct sockaddr_in server_addr;
	struct hostent *hp;
	CLIENT *clnt;
	char *server;

	/*
	 * Parse arguments
	 */
	while ((opt_ch = getopt(argc, argv, "fh:l:msuvx:D:")) != -1)
		switch (opt_ch) {
		case 'f':
			flush_flag = 1;
			nodefault = 1;
			break;

		case 'h':
			def_server = optarg;
			break;

		case 'l':
			logfile = optarg;
			nodefault = 1;
			break;

		case 'm':
			minfo_flag = 1;
			nodefault = 1;
			break;

		case 's':
			stats_flag = 1;
			nodefault = 1;
			break;

		case 'u':
			unmount_flag = 1;
			nodefault = 1;
			break;

		case 'v':
			getvers_flag = 1;
			nodefault = 1;
			break;

		case 'x':
			xlog_optstr = optarg;
			nodefault = 1;
			break;

		case 'D':
			debug_opts = optarg;
			nodefault = 1;
			break;

		default:
			errs = 1;
			break;
		}

	if (optind == argc) {
		if (unmount_flag)
			errs = 1;
	}

	if (errs) {
show_usage:
		fprintf(stderr, "usage: %s [-fmsuv] [-h hostname] "
	    "[directory ...]\n", __progname);
		exit(1);
	}

	server = def_server;

	/*
	 * Get address of server
	 */
	if ((hp = gethostbyname(server)) == 0 && strcmp(server, localhost) != 0) {
		fprintf(stderr, "%s: Can't get address of %s\n", __progname, server);
		exit(1);
	}
	bzero(&server_addr, sizeof server_addr);
	server_addr.sin_family = AF_INET;
	if (hp) {
		bcopy(hp->h_addr, &server_addr.sin_addr,
			sizeof(server_addr.sin_addr));
	} else {
		/* fake "localhost" */
		server_addr.sin_addr.s_addr = htonl(0x7f000001);
	}

	/*
	 * Create RPC endpoint
	 */
	s = privsock(SOCK_STREAM);
	clnt = clnttcp_create(&server_addr, AMQ_PROGRAM, AMQ_VERSION, &s, 0, 0);
	if (clnt == 0) {
		close(s);
		s = privsock(SOCK_DGRAM);
		clnt = clntudp_create(&server_addr, AMQ_PROGRAM,
		    AMQ_VERSION, TIMEOUT, &s);
	}
	if (clnt == 0) {
		fprintf(stderr, "%s: ", __progname);
		clnt_pcreateerror(server);
		exit(1);
	}

	/*
	 * Control debugging
	 */
	if (debug_opts) {
		int *rc;
		amq_setopt opt;
		opt.as_opt = AMOPT_DEBUG;
		opt.as_str = debug_opts;
		rc = amqproc_setopt_57(&opt, clnt);
		if (rc && *rc < 0) {
			fprintf(stderr,
			    "%s: daemon not compiled for debug", __progname);
			errs = 1;
		} else if (!rc || *rc > 0) {
			fprintf(stderr,
			    "%s: debug setting for \"%s\" failed\n",
			    __progname, debug_opts);
			errs = 1;
		}
	}

	/*
	 * Control logging
	 */
	if (xlog_optstr) {
		int *rc;
		amq_setopt opt;
		opt.as_opt = AMOPT_XLOG;
		opt.as_str = xlog_optstr;
		rc = amqproc_setopt_57(&opt, clnt);
		if (!rc || *rc) {
			fprintf(stderr, "%s: setting log level to \"%s\" failed\n",
			    __progname, xlog_optstr);
			errs = 1;
		}
	}

	/*
	 * Control log file
	 */
	if (logfile) {
		int *rc;
		amq_setopt opt;
		opt.as_opt = AMOPT_LOGFILE;
		opt.as_str = logfile;
		rc = amqproc_setopt_57(&opt, clnt);
		if (!rc || *rc) {
			fprintf(stderr, "%s: setting logfile to \"%s\" failed\n",
			    __progname, logfile);
			errs = 1;
		}
	}

	/*
	 * Flush map cache
	 */
	if (flush_flag) {
		int *rc;
		amq_setopt opt;
		opt.as_opt = AMOPT_FLUSHMAPC;
		opt.as_str = "";
		rc = amqproc_setopt_57(&opt, clnt);
		if (!rc || *rc) {
			fprintf(stderr,
			    "%s: amd on %s cannot flush the map cache\n",
			    __progname, server);
			errs = 1;
		}
	}

	/*
	 * Mount info
	 */
	if (minfo_flag) {
		int dummy;
		amq_mount_info_list *ml = amqproc_getmntfs_57(&dummy, clnt);
		if (ml) {
			int mwid = 0, dwid = 0, twid = 0;
			show_mi(ml, Calc, &mwid, &dwid, &twid);
			mwid++; dwid++; twid++;
			show_mi(ml, Full, &mwid, &dwid, &twid);
		} else {
			fprintf(stderr, "%s: amd on %s cannot provide mount info\n",
			    __progname, server);
		}
	}

	/*
	 * Get Version
	 */
	if (getvers_flag) {
		amq_string *spp = amqproc_getvers_57(NULL, clnt);
		if (spp && *spp) {
			printf("%s.\n", *spp);
			free(*spp);
		} else {
			fprintf(stderr, "%s: failed to get version information\n",
			    __progname);
			errs = 1;
		}
	}

	/*
	 * Apply required operation to all remaining arguments
	 */
	if (optind < argc) {
		do {
			char *fs = argv[optind++];
			if (unmount_flag) {
				/*
				 * Unmount request
				 */
				amqproc_umnt_57(&fs, clnt);
			} else {
				/*
				 * Stats request
				 */
				amq_mount_tree_p *mtp = amqproc_mnttree_57(&fs, clnt);
				if (mtp) {
					amq_mount_tree *mt = *mtp;
					if (mt) {
						int mwid = 0, dwid = 0, twid = 0;

						show_mt(mt, Calc, &mwid, &dwid, &twid);
						mwid++;
						dwid++;
						twid++;

						printf("%-*.*s Uid   Getattr "
						    "Lookup RdDir   RdLnk   "
						    "Statfs Mounted@\n",
						    dwid, dwid, "What");
						show_mt(mt, Stats, &mwid, &dwid, &twid);
					} else {
						fprintf(stderr,
						    "%s: %s not automounted\n",
						    __progname, fs);
					}
					xdr_pri_free(xdr_amq_mount_tree_p, mtp);
				} else {
					fprintf(stderr, "%s: ", __progname);
					clnt_perror(clnt, server);
					errs = 1;
				}
			}
		} while (optind < argc);
	} else if (unmount_flag) {
		goto show_usage;
	} else if (stats_flag) {
		amq_mount_stats *ms = amqproc_stats_57(NULL, clnt);
		if (ms) {
			show_ms(ms);
		} else {
			fprintf(stderr, "%s: ", __progname);
			clnt_perror(clnt, server);
			errs = 1;
		}
	} else if (!nodefault) {
		amq_mount_tree_list *mlp = amqproc_export_57(NULL, clnt);
		if (mlp) {
			enum show_opt e = Calc;
			int mwid = 0, dwid = 0, pwid = 0;

			while (e != ShowDone) {
				int i;

				for (i = 0; i < mlp->amq_mount_tree_list_len; i++) {
					show_mt(mlp->amq_mount_tree_list_val[i],
					    e, &mwid, &dwid, &pwid);
				}
				mwid++;
				dwid++;
				pwid++;
				if (e == Calc)
					e = Short;
				else if (e == Short)
					e = ShowDone;
			}
		} else {
			fprintf(stderr, "%s: ", __progname);
			clnt_perror(clnt, server);
			errs = 1;
		}
	}

	exit(errs);
}

/*
 * udpresport creates a datagram socket and attempts to bind it to a
 * secure port.
 * returns: The bound socket, or -1 to indicate an error.
 */
static int
inetresport(int ty)
{
	struct sockaddr_in addr;
	int alport, sock;

	/* Use internet address family */
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	if ((sock = socket(AF_INET, ty, 0)) < 0)
		return -1;
	for (alport = IPPORT_RESERVED-1; alport > IPPORT_RESERVED/2 + 1; alport--) {
		addr.sin_port = htons((u_short)alport);
		if (bind(sock, (struct sockaddr *)&addr, sizeof (addr)) >= 0)
			return sock;
		if (errno != EADDRINUSE) {
			close(sock);
			return -1;
		}
	}
	close(sock);
	errno = EAGAIN;
	return -1;
}

/*
 * Privsock() calls inetresport() to attempt to bind a socket to a secure
 * port.  If inetresport() fails, privsock returns a magic socket number which
 * indicates to RPC that it should make its own socket.
 * returns: A privileged socket # or RPC_ANYSOCK.
 */
static int
privsock(int ty)
{
	int sock = inetresport(ty);

	if (sock < 0) {
		errno = 0;
		/* Couldn't get a secure port, let RPC make an insecure one */
		sock = RPC_ANYSOCK;
	}
	return sock;
}
