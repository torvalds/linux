/*
 * Copyright (c) 1983, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 */
/*-
 * Copyright (c) 2004, 2008, 2009 Silicon Graphics International Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */


#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)nfsstat.c	8.2 (Berkeley) 3/31/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <nfs/nfsproto.h>
#include <nfsclient/nfs.h>
#include <nfsserver/nfs.h>
#include <nfs/nfssvc.h>

#include <fs/nfs/nfsport.h>

#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <nlist.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <devstat.h>
#include <err.h>

#include <libxo/xo.h>

static int widemode = 0;
static int zflag = 0;
static int printtitle = 1;
static struct nfsstatsv1 ext_nfsstats;
static int extra_output = 0;

static void intpr(int, int);
static void printhdr(int, int, int);
static void usage(void);
static char *sperc1(int, int);
static char *sperc2(int, int);
static void exp_intpr(int, int, int);
static void exp_sidewaysintpr(u_int, int, int, int);
static void compute_new_stats(struct nfsstatsv1 *cur_stats,
    struct nfsstatsv1 *prev_stats, int curop, long double etime,
    long double *mbsec, long double *kb_per_transfer,
    long double *transfers_per_second, long double *ms_per_transfer,
    uint64_t *queue_len, long double *busy_pct);

#define DELTA(field)	(nfsstats.field - lastst.field)

#define	STAT_TYPE_READ		0
#define	STAT_TYPE_WRITE		1
#define	STAT_TYPE_COMMIT	2
#define	NUM_STAT_TYPES		3

struct stattypes {
	int stat_type;
	int nfs_type;
};
static struct stattypes statstruct[] = {
	{STAT_TYPE_READ, NFSV4OP_READ},
	{STAT_TYPE_WRITE, NFSV4OP_WRITE},
	{STAT_TYPE_COMMIT, NFSV4OP_COMMIT}
};

#define	STAT_TYPE_TO_NFS(stat_type)	statstruct[stat_type].nfs_type

#define	NFSSTAT_XO_VERSION	"1"

int
main(int argc, char **argv)
{
	u_int interval;
	int clientOnly = -1;
	int serverOnly = -1;
	int newStats = 0;
	int ch;
	char *memf, *nlistf;
	int mntlen, i;
	char buf[1024];
	struct statfs *mntbuf;
	struct nfscl_dumpmntopts dumpmntopts;

	interval = 0;
	memf = nlistf = NULL;

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		exit(1);

	xo_set_version(NFSSTAT_XO_VERSION);

	while ((ch = getopt(argc, argv, "cdEesWM:mN:w:zq")) != -1)
		switch(ch) {
		case 'M':
			memf = optarg;
			break;
		case 'm':
			/* Display mount options for NFS mount points. */
			mntlen = getmntinfo(&mntbuf, MNT_NOWAIT);
			for (i = 0; i < mntlen; i++) {
				if (strcmp(mntbuf->f_fstypename, "nfs") == 0) {
					dumpmntopts.ndmnt_fname =
					    mntbuf->f_mntonname;
					dumpmntopts.ndmnt_buf = buf;
					dumpmntopts.ndmnt_blen = sizeof(buf);
					if (nfssvc(NFSSVC_DUMPMNTOPTS,
					    &dumpmntopts) >= 0)
						printf("%s on %s\n%s\n",
						    mntbuf->f_mntfromname,
						    mntbuf->f_mntonname, buf);
					else if (errno == EPERM)
						errx(1, "Only priviledged users"
						    " can use the -m option");
				}
				mntbuf++;
			}
			exit(0);
		case 'N':
			nlistf = optarg;
			break;
		case 'W':
			widemode = 1;
			break;
		case 'w':
			interval = atoi(optarg);
			break;
		case 'c':
			clientOnly = 1;
			if (serverOnly < 0)
				serverOnly = 0;
			break;
		case 'd':
			newStats = 1;
			if (interval == 0)
				interval = 1;
			break;
		case 's':
			serverOnly = 1;
			if (clientOnly < 0)
				clientOnly = 0;
			break;
		case 'z':
			zflag = 1;
			break;
		case 'E':
			if (extra_output != 0)
				xo_err(1, "-e and -E are mutually exclusive");
			extra_output = 2;
			break;
		case 'e':
			if (extra_output != 0)
				xo_err(1, "-e and -E are mutually exclusive");
			extra_output = 1;
			break;
		case 'q':
			printtitle = 0;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

#define	BACKWARD_COMPATIBILITY
#ifdef	BACKWARD_COMPATIBILITY
	if (*argv) {
		interval = atoi(*argv);
		if (*++argv) {
			nlistf = *argv;
			if (*++argv)
				memf = *argv;
		}
	}
#endif
	if (modfind("nfscommon") < 0)
		xo_err(1, "NFS client/server not loaded");

	if (interval) {
		exp_sidewaysintpr(interval, clientOnly, serverOnly,
		    newStats);
	} else {
		xo_open_container("nfsstat");
		if (extra_output != 0)
			exp_intpr(clientOnly, serverOnly, extra_output - 1);
		else
			intpr(clientOnly, serverOnly);
		xo_close_container("nfsstat");
	}

	xo_finish();
	exit(0);
}

/*
 * Print a description of the nfs stats.
 */
static void
intpr(int clientOnly, int serverOnly)
{
	int nfssvc_flag;

	nfssvc_flag = NFSSVC_GETSTATS | NFSSVC_NEWSTRUCT;
	if (zflag != 0) {
		if (clientOnly != 0)
			nfssvc_flag |= NFSSVC_ZEROCLTSTATS;
		if (serverOnly != 0)
			nfssvc_flag |= NFSSVC_ZEROSRVSTATS;
	}
	ext_nfsstats.vers = NFSSTATS_V1;
	if (nfssvc(nfssvc_flag, &ext_nfsstats) < 0)
		xo_err(1, "Can't get stats");
	if (clientOnly) {
		xo_open_container("clientstats");

		if (printtitle)
			xo_emit("{T:Client Info:\n");

		xo_open_container("operations");
		xo_emit("{T:Rpc Counts:}\n");

		xo_emit("{T:Getattr/%13.13s}{T:Setattr/%13.13s}"
		    "{T:Lookup/%13.13s}{T:Readlink/%13.13s}"
		    "{T:Read/%13.13s}{T:Write/%13.13s}"
		  "{T:Create/%13.13s}{T:Remove/%13.13s}\n");
		xo_emit("{:getattr/%13ju}{:setattr/%13ju}"
		    "{:lookup/%13ju}{:readlink/%13ju}"
		    "{:read/%13ju}{:write/%13ju}"
		    "{:create/%13ju}{:remove/%13ju}\n",
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_GETATTR],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_SETATTR],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LOOKUP],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READLINK],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READ],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_WRITE],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_CREATE],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_REMOVE]);

		xo_emit("{T:Rename/%13.13s}{T:Link/%13.13s}"
		    "{T:Symlink/%13.13s}{T:Mkdir/%13.13s}"
		    "{T:Rmdir/%13.13s}{T:Readdir/%13.13s}"
		  "{T:RdirPlus/%13.13s}{T:Access/%13.13s}\n");
		xo_emit("{:rename/%13ju}{:link/%13ju}"
		    "{:symlink/%13ju}{:mkdir/%13ju}"
		    "{:rmdir/%13ju}{:readdir/%13ju}"
		    "{:rdirplus/%13ju}{:access/%13ju}\n",
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_RENAME],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LINK],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_SYMLINK],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_MKDIR],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_RMDIR],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READDIR],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READDIRPLUS],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_ACCESS]);

		xo_emit("{T:Mknod/%13.13s}{T:Fsstat/%13.13s}"
		    "{T:Fsinfo/%13.13s}{T:PathConf/%13.13s}"
		    "{T:Commit/%13.13s}\n");
		xo_emit("{:mknod/%13ju}{:fsstat/%13ju}"
		    "{:fsinfo/%13ju}{:pathconf/%13ju}"
		    "{:commit/%13ju}\n",
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_MKNOD],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_FSSTAT],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_FSINFO],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_PATHCONF],
			(uintmax_t)ext_nfsstats.rpccnt[NFSPROC_COMMIT]);

		xo_close_container("operations");

		xo_open_container("rpcs");
		xo_emit("{T:Rpc Info:}\n");

		xo_emit("{T:TimedOut/%13.13s}{T:Invalid/%13.13s}"
		    "{T:X Replies/%13.13s}{T:Retries/%13.13s}"
		    "{T:Requests/%13.13s}\n");
		xo_emit("{:timedout/%13ju}{:invalid/%13ju}"
		    "{:xreplies/%13ju}{:retries/%13ju}"
		    "{:requests/%13ju}\n",
			(uintmax_t)ext_nfsstats.rpctimeouts,
			(uintmax_t)ext_nfsstats.rpcinvalid,
			(uintmax_t)ext_nfsstats.rpcunexpected,
			(uintmax_t)ext_nfsstats.rpcretries,
			(uintmax_t)ext_nfsstats.rpcrequests);
		xo_close_container("rpcs");

		xo_open_container("cache");
		xo_emit("{T:Cache Info:}\n");

		xo_emit("{T:Attr Hits/%13.13s}{T:Attr Misses/%13.13s}"
		    "{T:Lkup Hits/%13.13s}{T:Lkup Misses/%13.13s}"
		    "{T:BioR Hits/%13.13s}{T:BioR Misses/%13.13s}"
		    "{T:BioW Hits/%13.13s}{T:BioW Misses/%13.13s}\n");
		xo_emit("{:attrhits/%13ju}{:attrmisses/%13ju}"
		    "{:lkuphits/%13ju}{:lkupmisses/%13ju}"
		    "{:biorhits/%13ju}{:biormisses/%13ju}"
		    "{:biowhits/%13ju}{:biowmisses/%13ju}\n",
		    (uintmax_t)ext_nfsstats.attrcache_hits,
		    (uintmax_t)ext_nfsstats.attrcache_misses,
		    (uintmax_t)ext_nfsstats.lookupcache_hits,
		    (uintmax_t)ext_nfsstats.lookupcache_misses,
		    (uintmax_t)(ext_nfsstats.biocache_reads -
		    ext_nfsstats.read_bios),
		    (uintmax_t)ext_nfsstats.read_bios,
		    (uintmax_t)(ext_nfsstats.biocache_writes -
		    ext_nfsstats.write_bios),
		    (uintmax_t)ext_nfsstats.write_bios);

		xo_emit("{T:BioRL Hits/%13.13s}{T:BioRL Misses/%13.13s}"
		    "{T:BioD Hits/%13.13s}{T:BioD Misses/%13.13s}"
		    "{T:DirE Hits/%13.13s}{T:DirE Misses/%13.13s}"
		    "{T:Accs Hits/%13.13s}{T:Accs Misses/%13.13s}\n");
		xo_emit("{:biosrlhits/%13ju}{:biorlmisses/%13ju}"
		    "{:biodhits/%13ju}{:biodmisses/%13ju}"
		    "{:direhits/%13ju}{:diremisses/%13ju}"
		    "{:accshits/%13ju}{:accsmisses/%13ju}\n",
		    (uintmax_t)(ext_nfsstats.biocache_readlinks -
		    ext_nfsstats.readlink_bios),
		    (uintmax_t)ext_nfsstats.readlink_bios,
		    (uintmax_t)(ext_nfsstats.biocache_readdirs -
		    ext_nfsstats.readdir_bios),
		    (uintmax_t)ext_nfsstats.readdir_bios,
		    (uintmax_t)ext_nfsstats.direofcache_hits,
		    (uintmax_t)ext_nfsstats.direofcache_misses,
		    (uintmax_t)ext_nfsstats.accesscache_hits,
		    (uintmax_t)ext_nfsstats.accesscache_misses);

		xo_close_container("cache");

		xo_close_container("clientstats");
	}
	if (serverOnly) {
		xo_open_container("serverstats");

		xo_emit("{T:Server Info:}\n");
		xo_open_container("operations");

		xo_emit("{T:Getattr/%13.13s}{T:Setattr/%13.13s}"
		    "{T:Lookup/%13.13s}{T:Readlink/%13.13s}"
		    "{T:Read/%13.13s}{T:Write/%13.13s}"
		    "{T:Create/%13.13s}{T:Remove/%13.13s}\n");
		xo_emit("{:getattr/%13ju}{:setattr/%13ju}"
		    "{:lookup/%13ju}{:readlink/%13ju}"
		    "{:read/%13ju}{:write/%13ju}"
		    "{:create/%13ju}{:remove/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_GETATTR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SETATTR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LOOKUP],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READLINK],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READ],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_WRITE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_CREATE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_REMOVE]);

		xo_emit("{T:Rename/%13.13s}{T:Link/%13.13s}"
		    "{T:Symlink/%13.13s}{T:Mkdir/%13.13s}"
		    "{T:Rmdir/%13.13s}{T:Readdir/%13.13s}"
		    "{T:RdirPlus/%13.13s}{T:Access/%13.13s}\n");
		xo_emit("{:rename/%13ju}{:link/%13ju}"
		    "{:symlink/%13ju}{:mkdir/%13ju}"
		    "{:rmdir/%13ju}{:readdir/%13ju}"
		    "{:rdirplus/%13ju}{:access/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RENAME],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LINK],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SYMLINK],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_MKDIR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RMDIR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READDIR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READDIRPLUS],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_ACCESS]);

		xo_emit("{T:Mknod/%13.13s}{T:Fsstat/%13.13s}"
		    "{T:Fsinfo/%13.13s}{T:PathConf/%13.13s}"
		    "{T:Commit/%13.13s}\n");
		xo_emit("{:mknod/%13ju}{:fsstat/%13ju}"
		    "{:fsinfo/%13ju}{:pathconf/%13ju}"
		    "{:commit/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_MKNOD],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_FSSTAT],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_FSINFO],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_PATHCONF],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_COMMIT]);

		xo_close_container("operations");

		xo_open_container("server");
		xo_emit("{T:Server Re-Failed:}\n");
		xo_emit("{T:retfailed/%17ju}\n", (uintmax_t)ext_nfsstats.srvrpc_errs);

		xo_emit("{T:Server Faults:}\n");
		xo_emit("{T:faults/%13ju}\n", (uintmax_t)ext_nfsstats.srv_errs);

		xo_emit("{T:Server Write Gathering:/%13.13s}\n");

		xo_emit("{T:WriteOps/%13.13s}{T:WriteRPC/%13.13s}"
		    "{T:Opsaved/%13.13s}\n");
		xo_emit("{:writeops/%13ju}{:writerpc/%13ju}"
		    "{:opsaved/%13ju}\n",
		/*
		 * The new client doesn't do write gathering. It was
		 * only useful for NFSv2.
		 */
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_WRITE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_WRITE], 0);

		xo_close_container("server");

		xo_open_container("cache");
		xo_emit("{T:Server Cache Stats:/%13.13s}\n");
		xo_emit("{T:Inprog/%13.13s}{T:Idem/%13.13s}"
		    "{T:Non-Idem/%13.13s}{T:Misses/%13.13s}\n");
		xo_emit("{:inprog/%13ju}{:idem/%13ju}"
		    "{:nonidem/%13ju}{:misses/%13ju}\n",
			(uintmax_t)ext_nfsstats.srvcache_inproghits,
			(uintmax_t)ext_nfsstats.srvcache_idemdonehits,
			(uintmax_t)ext_nfsstats.srvcache_nonidemdonehits,
			(uintmax_t)ext_nfsstats.srvcache_misses);
		xo_close_container("cache");

		xo_close_container("serverstats");
	}
}

static void
printhdr(int clientOnly, int serverOnly, int newStats)
{

	if (newStats) {
		printf(" [%s Read %s]  [%s Write %s]  "
		    "%s[=========== Total ============]\n"
		    " KB/t   tps    MB/s%s  KB/t   tps    MB/s%s  "
		    "%sKB/t   tps    MB/s    ms  ql  %%b",
		    widemode ? "========" : "=====",
		    widemode ? "========" : "=====",
		    widemode ? "========" : "=====",
		    widemode ? "======="  : "====",
		    widemode ? "[Commit ]  " : "",
		    widemode ? "    ms" : "",
		    widemode ? "    ms" : "",
		    widemode ? "tps    ms  " : "");
	} else {
		printf("%s%6.6s %6.6s %6.6s %6.6s %6.6s %6.6s %6.6s %6.6s",
		    ((serverOnly && clientOnly) ? "        " : " "),
		    "GtAttr", "Lookup", "Rdlink", "Read", "Write", "Rename",
		    "Access", "Rddir");
		if (widemode && clientOnly) {
			printf(" Attr Lkup BioR BioW Accs BioD");
		}
	}
	printf("\n");
	fflush(stdout);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: nfsstat [-cdemszW] [-M core] [-N system] [-w wait]\n");
	exit(1);
}

static char SPBuf[64][8];
static int SPIndex;

static char * 
sperc1(int hits, int misses)
{
	char *p = SPBuf[SPIndex];

	if (hits + misses) {
		sprintf(p, "%3d%%", 
		    (int)(char)((quad_t)hits * 100 / (hits + misses)));
	} else {
		sprintf(p, "   -");
	}
	SPIndex = (SPIndex + 1) & 63;
	return(p);
}

static char * 
sperc2(int ttl, int misses)
{
	char *p = SPBuf[SPIndex];

	if (ttl) {
		sprintf(p, "%3d%%",
		    (int)(char)((quad_t)(ttl - misses) * 100 / ttl));
	} else {
		sprintf(p, "   -");
	}
	SPIndex = (SPIndex + 1) & 63;
	return(p);
}

#define DELTA_T(field)					\
	devstat_compute_etime(&cur_stats->field,	\
	(prev_stats ? &prev_stats->field : NULL))

/*
 * XXX KDM mostly copied from ctlstat.  We should commonize the code (and
 * the devstat code) somehow.
 */
static void
compute_new_stats(struct nfsstatsv1 *cur_stats,
		  struct nfsstatsv1 *prev_stats, int curop,
		  long double etime, long double *mbsec,
		  long double *kb_per_transfer,
		  long double *transfers_per_second,
		  long double *ms_per_transfer, uint64_t *queue_len,
		  long double *busy_pct)
{
	uint64_t total_bytes = 0, total_operations = 0;
	struct bintime total_time_bt;
	struct timespec total_time_ts;

	bzero(&total_time_bt, sizeof(total_time_bt));
	bzero(&total_time_ts, sizeof(total_time_ts));

	total_bytes = cur_stats->srvbytes[curop];
	total_operations = cur_stats->srvops[curop];
	if (prev_stats != NULL) {
		total_bytes -= prev_stats->srvbytes[curop];
		total_operations -= prev_stats->srvops[curop];
	}

	*mbsec = total_bytes;
	*mbsec /= 1024 * 1024;
	if (etime > 0.0) {
		*busy_pct = DELTA_T(busytime);
		if (*busy_pct < 0)
			*busy_pct = 0;
		*busy_pct /= etime;
		*busy_pct *= 100;
		if (*busy_pct < 0)
			*busy_pct = 0;
		*mbsec /= etime;
	} else {
		*busy_pct = 0;
		*mbsec = 0;
	}
	*kb_per_transfer = total_bytes;
	*kb_per_transfer /= 1024;
	if (total_operations > 0)
		*kb_per_transfer /= total_operations;
	else
		*kb_per_transfer = 0;
	if (etime > 0.0) {
		*transfers_per_second = total_operations;
		*transfers_per_second /= etime;
	} else {
		*transfers_per_second = 0.0;
	}
                        
	if (total_operations > 0) {
		*ms_per_transfer = DELTA_T(srvduration[curop]);
		*ms_per_transfer /= total_operations;
		*ms_per_transfer *= 1000;
	} else
		*ms_per_transfer = 0.0;

	*queue_len = cur_stats->srvstartcnt - cur_stats->srvdonecnt;
}

/*
 * Print a description of the nfs stats for the client/server,
 * including NFSv4.1.
 */
static void
exp_intpr(int clientOnly, int serverOnly, int nfs41)
{
	int nfssvc_flag;

	xo_open_container("nfsv4");

	nfssvc_flag = NFSSVC_GETSTATS | NFSSVC_NEWSTRUCT;
	if (zflag != 0) {
		if (clientOnly != 0)
			nfssvc_flag |= NFSSVC_ZEROCLTSTATS;
		if (serverOnly != 0)
			nfssvc_flag |= NFSSVC_ZEROSRVSTATS;
	}
	ext_nfsstats.vers = NFSSTATS_V1;
	if (nfssvc(nfssvc_flag, &ext_nfsstats) < 0)
		xo_err(1, "Can't get stats");
	if (clientOnly != 0) {
		xo_open_container("clientstats");

		xo_open_container("operations");
		if (printtitle) {
			xo_emit("{T:Client Info:}\n");
			xo_emit("{T:RPC Counts:}\n");
		}
		xo_emit("{T:Getattr/%13.13s}{T:Setattr/%13.13s}"
		    "{T:Lookup/%13.13s}{T:Readlink/%13.13s}"
		    "{T:Read/%13.13s}{T:Write/%13.13s}\n");
		xo_emit("{:getattr/%13ju}{:setattr/%13ju}{:lookup/%13ju}"
		    "{:readlink/%13ju}{:read/%13ju}{:write/%13ju}\n",
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_GETATTR],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_SETATTR],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LOOKUP],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READLINK],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READ],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_WRITE]);
		xo_emit("{T:Create/%13.13s}{T:Remove/%13.13s}"
		    "{T:Rename/%13.13s}{T:Link/%13.13s}"
		    "{T:Symlink/%13.13s}{T:Mkdir/%13.13s}\n");
		xo_emit("{:create/%13ju}{:remove/%13ju}{:rename/%13ju}"
		  "{:link/%13ju}{:symlink/%13ju}{:mkdir/%13ju}\n",
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_CREATE],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_REMOVE],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_RENAME],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LINK],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_SYMLINK],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_MKDIR]);
		xo_emit("{T:Rmdir/%13.13s}{T:Readdir/%13.13s}"
		    "{T:RdirPlus/%13.13s}{T:Access/%13.13s}"
		    "{T:Mknod/%13.13s}{T:Fsstat/%13.13s}\n");
		xo_emit("{:rmdir/%13ju}{:readdir/%13ju}{:rdirplus/%13ju}"
		    "{:access/%13ju}{:mknod/%13ju}{:fsstat/%13ju}\n",
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_RMDIR],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READDIR],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READDIRPLUS],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_ACCESS],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_MKNOD],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_FSSTAT]);
		xo_emit("{T:FSinfo/%13.13s}{T:pathConf/%13.13s}"
		    "{T:Commit/%13.13s}{T:SetClId/%13.13s}"
		    "{T:SetClIdCf/%13.13s}{T:Lock/%13.13s}\n");
		xo_emit("{:fsinfo/%13ju}{:pathconf/%13ju}{:commit/%13ju}"
		    "{:setclientid/%13ju}{:setclientidcf/%13ju}{:lock/%13ju}\n",
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_FSINFO],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_PATHCONF],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_COMMIT],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_SETCLIENTID],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_SETCLIENTIDCFRM],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LOCK]);
		xo_emit("{T:LockT/%13.13s}{T:LockU/%13.13s}"
		    "{T:Open/%13.13s}{T:OpenCfr/%13.13s}\n");
		xo_emit("{:lockt/%13ju}{:locku/%13ju}"
		    "{:open/%13ju}{:opencfr/%13ju}\n",
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LOCKT],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LOCKU],
		    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_OPEN],
		  (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_OPENCONFIRM]);

		if (nfs41) {
			xo_open_container("nfsv41");

			xo_emit("{T:OpenDownGr/%13.13s}{T:Close/%13.13s}\n");
			xo_emit("{:opendowngr/%13ju}{:close/%13ju}\n",
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_OPENDOWNGRADE],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_CLOSE]);

			xo_emit("{T:RelLckOwn/%13.13s}{T:FreeStateID/%13.13s}"
			    "{T:PutRootFH/%13.13s}{T:DelegRet/%13.13s}"
			    "{T:GetAcl/%13.13s}{T:SetAcl/%13.13s}\n");
			xo_emit("{:rellckown/%13ju}{:freestateid/%13ju}"
			    "{:getacl/%13ju}{:delegret/%13ju}"
			    "{:getacl/%13ju}{:setacl/%13ju}\n",
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_RELEASELCKOWN],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_FREESTATEID],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_PUTROOTFH],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_DELEGRETURN],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_GETACL],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_SETACL]);

			xo_emit("{T:ExchangeId/%13.13s}{T:CreateSess/%13.13s}"
			    "{T:DestroySess/%13.13s}{T:DestroyClId/%13.13s}"
			    "{T:LayoutGet/%13.13s}{T:GetDevInfo/%13.13s}\n");
			xo_emit("{:exchangeid/%13ju}{:createsess/%13ju}"
			    "{:destroysess/%13ju}{:destroyclid/%13ju}"
			    "{:layoutget/%13ju}{:getdevinfo/%13ju}\n",
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_EXCHANGEID],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_CREATESESSION],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_DESTROYSESSION],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_DESTROYCLIENT],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LAYOUTGET],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_GETDEVICEINFO]);

			xo_emit("{T:LayoutCommit/%13.13s}{T:LayoutReturn/%13.13s}"
			    "{T:ReclaimCompl/%13.13s}{T:ReadDataS/%13.13s}"
			    "{T:WriteDataS/%13.13s}{T:CommitDataS/%13.13s}\n");
			xo_emit("{:layoutcomit/%13ju}{:layoutreturn/%13ju}"
			    "{:reclaimcompl/%13ju}{:readdatas/%13ju}"
			    "{:writedatas/%13ju}{:commitdatas/%13ju}\n",
			  (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LAYOUTCOMMIT],
			  (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_LAYOUTRETURN],
			  (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_RECLAIMCOMPL],
			  (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_READDS],
			  (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_WRITEDS],
			  (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_COMMITDS]);

			xo_emit("{T:OpenLayout/%13.13s}{T:CreateLayout/%13.13s}\n");
			xo_emit("{:openlayout/%13ju}{:createlayout/%13ju}\n",
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_OPENLAYGET],
			    (uintmax_t)ext_nfsstats.rpccnt[NFSPROC_CREATELAYGET]);

			xo_close_container("nfsv41");
		}
		xo_close_container("operations");

		xo_open_container("client");
		xo_emit("{T:OpenOwner/%13.13s}{T:Opens/%13.13s}"
		    "{T:LockOwner/%13.13s}{T:Locks/%13.13s}"
		    "{T:Delegs/%13.13s}{T:LocalOwn/%13.13s}\n");
		xo_emit("{:openowner/%13ju}{:opens/%13ju}"
		    "{:lockowner/%13ju}{:locks/%13ju}"
		    "{:delegs/%13ju}{:localown/%13ju}\n",
		    (uintmax_t)ext_nfsstats.clopenowners,
		    (uintmax_t)ext_nfsstats.clopens,
		    (uintmax_t)ext_nfsstats.cllockowners,
		    (uintmax_t)ext_nfsstats.cllocks,
		    (uintmax_t)ext_nfsstats.cldelegates,
		    (uintmax_t)ext_nfsstats.cllocalopenowners);

		xo_emit("{T:LocalOpen/%13.13s}{T:LocalLown/%13.13s}"
		    "{T:LocalLock\n");
		xo_emit("{:localopen/%13ju}{:locallown/%13ju}"
		    "{:locallock/%13ju}\n",
		    (uintmax_t)ext_nfsstats.cllocalopens,
		    (uintmax_t)ext_nfsstats.cllocallockowners,
		    (uintmax_t)ext_nfsstats.cllocallocks);
		xo_close_container("client");

		xo_open_container("rpc");
		if (printtitle)
			xo_emit("{T:Rpc Info:}\n");
		xo_emit("{T:TimedOut/%13.13s}{T:Invalid/%13.13s}"
		    "{T:X Replies/%13.13s}{T:Retries/%13.13s}"
		    "{T:Requests/%13.13s}\n");
		xo_emit("{:timedout/%13ju}{:invalid/%13ju}"
		    "{:xreplies/%13ju}{:retries/%13ju}"
		    "{:requests/%13ju}\n",
		    (uintmax_t)ext_nfsstats.rpctimeouts,
		    (uintmax_t)ext_nfsstats.rpcinvalid,
		    (uintmax_t)ext_nfsstats.rpcunexpected,
		    (uintmax_t)ext_nfsstats.rpcretries,
		    (uintmax_t)ext_nfsstats.rpcrequests);
		xo_close_container("rpc");

		xo_open_container("cache");
		if (printtitle)
			xo_emit("{T:Cache Info:}\n");
		xo_emit("{T:Attr Hits/%13.13s}{T:Attr Misses/%13.13s}"
		    "{T:Lkup Hits/%13.13s}{T:Lkup Misses/%13.13s}\n");
		xo_emit("{:attrhits/%13ju}{:attrmisses/%13ju}"
		    "{:lkuphits/%13ju}{:lkupmisses/%13ju}\n",
		    (uintmax_t)ext_nfsstats.attrcache_hits,
		    (uintmax_t)ext_nfsstats.attrcache_misses,
		    (uintmax_t)ext_nfsstats.lookupcache_hits,
		    (uintmax_t)ext_nfsstats.lookupcache_misses);

		xo_emit("{T:BioR Hits/%13.13s}{T:BioR Misses/%13.13s}"
		    "{T:BioW Hits/%13.13s}{T:BioW Misses/%13.13s}\n");
		xo_emit("{:biorhits/%13ju}{:biormisses/%13ju}"
		    "{:biowhits/%13ju}{:biowmisses/%13ju}\n",
		    (uintmax_t)(ext_nfsstats.biocache_reads -
		    ext_nfsstats.read_bios),
		    (uintmax_t)ext_nfsstats.read_bios,
		    (uintmax_t)(ext_nfsstats.biocache_writes -
		    ext_nfsstats.write_bios),
		    (uintmax_t)ext_nfsstats.write_bios);

		xo_emit("{T:BioRL Hits/%13.13s}{T:BioRL Misses/%13.13s}"
		    "{T:BioD Hits/%13.13s}{T:BioD Misses/%13.13s}\n");
		xo_emit("{:biorlhits/%13ju}{:biorlmisses/%13ju}"
		    "{:biodhits/%13ju}{:biodmisses/%13ju}\n",
		    (uintmax_t)(ext_nfsstats.biocache_readlinks -
		    ext_nfsstats.readlink_bios),
		    (uintmax_t)ext_nfsstats.readlink_bios,
		    (uintmax_t)(ext_nfsstats.biocache_readdirs -
		    ext_nfsstats.readdir_bios),
		    (uintmax_t)ext_nfsstats.readdir_bios);

		xo_emit("{T:DirE Hits/%13.13s}{T:DirE Misses/%13.13s}\n");
		xo_emit("{:direhits/%13ju}{:diremisses/%13ju}\n",
		    (uintmax_t)ext_nfsstats.direofcache_hits,
		    (uintmax_t)ext_nfsstats.direofcache_misses);
		xo_open_container("cache");

		xo_close_container("clientstats");
	}
	if (serverOnly != 0) {
		xo_open_container("serverstats");

		xo_open_container("operations");
		if (printtitle)
			xo_emit("{T:Server Info:}\n");
		xo_emit("{T:Getattr/%13.13s}{T:Setattr/%13.13s}"
		    "{T:Lookup/%13.13s}{T:Readlink/%13.13s}"
		    "{T:Read/%13.13s}{T:Write/%13.13s}\n");
		xo_emit("{:getattr/%13ju}{:setattr/%13ju}{:lookup/%13ju}"
		    "{:readlink/%13ju}{:read/%13ju}{:write/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_GETATTR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SETATTR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LOOKUP],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READLINK],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READ],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_WRITE]);
		xo_emit("{T:Create/%13.13s}{T:Remove/%13.13s}"
		    "{T:Rename/%13.13s}{T:Link/%13.13s}"
		    "{T:Symlink/%13.13s}{T:Mkdir/%13.13s}\n");
		xo_emit("{:create/%13ju}{:remove/%13ju}{:rename/%13ju}"
		    "{:link/%13ju}{:symlink/%13ju}{:mkdir/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_V3CREATE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_REMOVE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RENAME],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LINK],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SYMLINK],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_MKDIR]);
		xo_emit("{T:Rmdir/%13.13s}{T:Readdir/%13.13s}"
		    "{T:RdirPlus/%13.13s}{T:Access/%13.13s}"
		    "{T:Mknod/%13.13s}{T:Fsstat/%13.13s}\n");
		xo_emit("{:rmdir/%13ju}{:readdir/%13ju}{:rdirplus/%13ju}"
		    "{:access/%13ju}{:mknod/%13ju}{:fsstat/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RMDIR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READDIR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_READDIRPLUS],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_ACCESS],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_MKNOD],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_FSSTAT]);
		xo_emit("{T:FSinfo/%13.13s}{T:pathConf/%13.13s}"
		    "{T:Commit/%13.13s}{T:LookupP/%13.13s}"
		    "{T:SetClId/%13.13s}{T:SetClIdCf/%13.13s}\n");
		xo_emit("{:fsinfo/%13ju}{:pathconf/%13ju}{:commit/%13ju}"
		    "{:lookupp/%13ju}{:setclientid/%13ju}{:setclientidcfrm/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_FSINFO],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_PATHCONF],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_COMMIT],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LOOKUPP],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SETCLIENTID],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SETCLIENTIDCFRM]);
		xo_emit("{T:Open/%13.13s}{T:OpenAttr/%13.13s}"
		    "{T:OpenDwnGr/%13.13s}{T:OpenCfrm/%13.13s}"
		    "{T:DelePurge/%13.13s}{T:DelRet/%13.13s}\n");
		xo_emit("{:open/%13ju}{:openattr/%13ju}{:opendwgr/%13ju}"
		    "{:opencfrm/%13ju}{:delepurge/%13ju}{:delreg/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_OPEN],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_OPENATTR],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_OPENDOWNGRADE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_OPENCONFIRM],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_DELEGPURGE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_DELEGRETURN]);
		xo_emit("{T:GetFH/%13.13s}{T:Lock/%13.13s}"
		    "{T:LockT/%13.13s}{T:LockU/%13.13s}"
		    "{T:Close/%13.13s}{T:Verify/%13.13s}\n");
		xo_emit("{:getfh/%13ju}{:lock/%13ju}{:lockt/%13ju}"
		    "{:locku/%13ju}{:close/%13ju}{:verify/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_GETFH],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LOCK],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LOCKT],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LOCKU],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_CLOSE],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_VERIFY]);
		xo_emit("{T:NVerify/%13.13s}{T:PutFH/%13.13s}"
		    "{T:PutPubFH/%13.13s}{T:PutRootFH/%13.13s}"
		    "{T:Renew/%13.13s}{T:RestoreFH/%13.13s}\n");
		xo_emit("{:nverify/%13ju}{:putfh/%13ju}{:putpubfh/%13ju}"
		    "{:putrootfh/%13ju}{:renew/%13ju}{:restore/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_NVERIFY],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_PUTFH],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_PUTPUBFH],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_PUTROOTFH],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RENEW],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RESTOREFH]);
		xo_emit("{T:SaveFH/%13.13s}{T:Secinfo/%13.13s}"
		    "{T:RelLockOwn/%13.13s}{T:V4Create/%13.13s}\n");
		xo_emit("{:savefh/%13ju}{:secinfo/%13ju}{:rellockown/%13ju}"
		    "{:v4create/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SAVEFH],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SECINFO],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RELEASELCKOWN],
		    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_CREATE]);
		if (nfs41) {
			xo_open_container("nfsv41");
			xo_emit("{T:BackChannelCtrl/%13.13s}{T:BindConnToSess/%13.13s}"
			    "{T:ExchangeID/%13.13s}{T:CreateSess/%13.13s}"
			    "{T:DestroySess/%13.13s}{T:FreeStateID/%13.13s}\n");
			xo_emit("{:backchannelctrl/%13ju}{:bindconntosess/%13ju}"
			    "{:exchangeid/%13ju}{:createsess/%13ju}"
			    "{:destroysess/%13ju}{:freestateid/%13ju}\n",
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_BACKCHANNELCTL],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_BINDCONNTOSESS],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_EXCHANGEID],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_CREATESESSION],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_DESTROYSESSION],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_FREESTATEID]),

			xo_emit("{T:GetDirDeleg/%13.13s}{T:GetDevInfo/%13.13s}"
			    "{T:GetDevList/%13.13s}{T:layoutCommit/%13.13s}"
			    "{T:LayoutGet/%13.13s}{T:LayoutReturn/%13.13s}\n");
			xo_emit("{:getdirdeleg/%13ju}{:getdevinfo/%13ju}"
			    "{:getdevlist/%13ju}{:layoutcommit/%13ju}"
			    "{:layoutget/%13ju}{:layoutreturn/%13ju}\n",
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_GETDIRDELEG],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_GETDEVINFO],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_GETDEVLIST],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LAYOUTCOMMIT],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LAYOUTGET],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_LAYOUTRETURN]);

			xo_emit("{T:SecInfNoName/%13.13s}{T:Sequence/%13.13s}"
			    "{T:SetSSV/%13.13s}{T:TestStateID/%13.13s}"
			    "{T:WantDeleg/%13.13s}{T:DestroyClId/%13.13s}\n");
			xo_emit("{:secinfnoname/%13ju}{:sequence/%13ju}"
			    "{:setssv/%13ju}{:teststateid/%13ju}{:wantdeleg/%13ju}"
			    "{:destroyclid/%13ju}\n",
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SECINFONONAME],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SEQUENCE],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_SETSSV],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_TESTSTATEID],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_WANTDELEG],
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_DESTROYCLIENTID]);

			xo_emit("{T:ReclaimCompl/%13.13s}\n");
			xo_emit("{:reclaimcompl/%13ju}\n",
			    (uintmax_t)ext_nfsstats.srvrpccnt[NFSV4OP_RECLAIMCOMPL]);

			xo_close_container("nfsv41");
		}

		xo_close_container("operations");

		if (printtitle)
			xo_emit("{T:Server:}\n");
		xo_open_container("server");
		xo_emit("{T:Retfailed/%13.13s}{T:Faults/%13.13s}"
		    "{T:Clients/%13.13s}\n");
		xo_emit("{:retfailed/%13ju}{:faults/%13ju}{:clients/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srv_errs,
		    (uintmax_t)ext_nfsstats.srvrpc_errs,
		    (uintmax_t)ext_nfsstats.srvclients);
		xo_emit("{T:OpenOwner/%13.13s}{T:Opens/%13.13s}"
		    "{T:LockOwner/%13.13s}{T:Locks/%13.13s}"
		    "{T:Delegs/%13.13s}\n");
		xo_emit("{:openowner/%13ju}{:opens/%13ju}{:lockowner/%13ju}"
		  "{:locks/%13ju}{:delegs/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvopenowners,
		    (uintmax_t)ext_nfsstats.srvopens,
		    (uintmax_t)ext_nfsstats.srvlockowners,
		    (uintmax_t)ext_nfsstats.srvlocks,
		    (uintmax_t)ext_nfsstats.srvdelegates);
		xo_close_container("server");

		if (printtitle)
			xo_emit("{T:Server Cache Stats:}\n");
		xo_open_container("cache");
		xo_emit("{T:Inprog/%13.13s}{T:Idem/%13.13s}"
		    "{T:Non-idem/%13.13s}{T:Misses/%13.13s}"
		    "{T:CacheSize/%13.13s}{T:TCPPeak/%13.13s}\n");
		xo_emit("{:inprog/%13ju}{:idem/%13ju}{:nonidem/%13ju}"
		    "{:misses/%13ju}{:cachesize/%13ju}{:tcppeak/%13ju}\n",
		    (uintmax_t)ext_nfsstats.srvcache_inproghits,
		    (uintmax_t)ext_nfsstats.srvcache_idemdonehits,
		    (uintmax_t)ext_nfsstats.srvcache_nonidemdonehits,
		    (uintmax_t)ext_nfsstats.srvcache_misses,
		    (uintmax_t)ext_nfsstats.srvcache_size,
		    (uintmax_t)ext_nfsstats.srvcache_tcppeak);
		xo_close_container("cache");

		xo_close_container("serverstats");
	}

	xo_close_container("nfsv4");
}

static void
compute_totals(struct nfsstatsv1 *total_stats, struct nfsstatsv1 *cur_stats)
{
	int i;

	bzero(total_stats, sizeof(*total_stats));
	for (i = 0; i < (NFSV42_NOPS + NFSV4OP_FAKENOPS); i++) {
		total_stats->srvbytes[0] += cur_stats->srvbytes[i];
		total_stats->srvops[0] += cur_stats->srvops[i];
		bintime_add(&total_stats->srvduration[0],
			    &cur_stats->srvduration[i]);
		total_stats->srvrpccnt[i] = cur_stats->srvrpccnt[i];
	}
	total_stats->srvstartcnt = cur_stats->srvstartcnt;
	total_stats->srvdonecnt = cur_stats->srvdonecnt;
	total_stats->busytime = cur_stats->busytime;

}

/*
 * Print a running summary of nfs statistics for the experimental client and/or
 * server.
 * Repeat display every interval seconds, showing statistics
 * collected over that interval.  Assumes that interval is non-zero.
 * First line printed at top of screen is always cumulative.
 */
static void
exp_sidewaysintpr(u_int interval, int clientOnly, int serverOnly,
    int newStats)
{
	struct nfsstatsv1 nfsstats, lastst, *ext_nfsstatsp;
	struct nfsstatsv1 curtotal, lasttotal;
	struct timespec ts, lastts;
	int hdrcnt = 1;

	ext_nfsstatsp = &lastst;
	ext_nfsstatsp->vers = NFSSTATS_V1;
	if (nfssvc(NFSSVC_GETSTATS | NFSSVC_NEWSTRUCT, ext_nfsstatsp) < 0)
		err(1, "Can't get stats");
	clock_gettime(CLOCK_MONOTONIC, &lastts);
	compute_totals(&lasttotal, ext_nfsstatsp);
	sleep(interval);

	for (;;) {
		ext_nfsstatsp = &nfsstats;
		ext_nfsstatsp->vers = NFSSTATS_V1;
		if (nfssvc(NFSSVC_GETSTATS | NFSSVC_NEWSTRUCT, ext_nfsstatsp)
		    < 0)
			err(1, "Can't get stats");
		clock_gettime(CLOCK_MONOTONIC, &ts);

		if (--hdrcnt == 0) {
			printhdr(clientOnly, serverOnly, newStats);
			if (newStats)
				hdrcnt = 20;
			else if (clientOnly && serverOnly)
				hdrcnt = 10;
			else
				hdrcnt = 20;
		}
		if (clientOnly && newStats == 0) {
		    printf("%s %6ju %6ju %6ju %6ju %6ju %6ju %6ju %6ju",
			((clientOnly && serverOnly) ? "Client:" : ""),
			(uintmax_t)DELTA(rpccnt[NFSPROC_GETATTR]),
			(uintmax_t)DELTA(rpccnt[NFSPROC_LOOKUP]),
			(uintmax_t)DELTA(rpccnt[NFSPROC_READLINK]),
			(uintmax_t)DELTA(rpccnt[NFSPROC_READ]),
			(uintmax_t)DELTA(rpccnt[NFSPROC_WRITE]),
			(uintmax_t)DELTA(rpccnt[NFSPROC_RENAME]),
			(uintmax_t)DELTA(rpccnt[NFSPROC_ACCESS]),
			(uintmax_t)(DELTA(rpccnt[NFSPROC_READDIR]) +
			DELTA(rpccnt[NFSPROC_READDIRPLUS]))
		    );
		    if (widemode) {
			    printf(" %s %s %s %s %s %s",
				sperc1(DELTA(attrcache_hits),
				    DELTA(attrcache_misses)),
				sperc1(DELTA(lookupcache_hits), 
				    DELTA(lookupcache_misses)),
				sperc2(DELTA(biocache_reads),
				    DELTA(read_bios)),
				sperc2(DELTA(biocache_writes),
				    DELTA(write_bios)),
				sperc1(DELTA(accesscache_hits),
				    DELTA(accesscache_misses)),
				sperc2(DELTA(biocache_readdirs),
				    DELTA(readdir_bios))
			    );
		    }
		    printf("\n");
		}

		if (serverOnly && newStats) {
			long double cur_secs, last_secs, etime;
			long double mbsec;
			long double kb_per_transfer;
			long double transfers_per_second;
			long double ms_per_transfer;
			uint64_t queue_len;
			long double busy_pct;
			int i;

			cur_secs = ts.tv_sec +
			    ((long double)ts.tv_nsec / 1000000000);
			last_secs = lastts.tv_sec +
			    ((long double)lastts.tv_nsec / 1000000000);
			etime = cur_secs - last_secs;

			compute_totals(&curtotal, &nfsstats);

			for (i = 0; i < NUM_STAT_TYPES; i++) {
				compute_new_stats(&nfsstats, &lastst,
				    STAT_TYPE_TO_NFS(i), etime, &mbsec,
				    &kb_per_transfer,
				    &transfers_per_second,
				    &ms_per_transfer, &queue_len,
				    &busy_pct);

				if (i == STAT_TYPE_COMMIT) {
					if (widemode == 0)
						continue;

					printf("%2.0Lf %7.2Lf ",
					    transfers_per_second,
					    ms_per_transfer);
				} else {
					printf("%5.2Lf %5.0Lf %7.2Lf ",
					    kb_per_transfer,
					    transfers_per_second, mbsec);
					if (widemode)
						printf("%5.2Lf ",
						    ms_per_transfer);
				}
			}

			compute_new_stats(&curtotal, &lasttotal, 0, etime,
			    &mbsec, &kb_per_transfer, &transfers_per_second,
			    &ms_per_transfer, &queue_len, &busy_pct);

			printf("%5.2Lf %5.0Lf %7.2Lf %5.2Lf %3ju %3.0Lf\n",
			    kb_per_transfer, transfers_per_second, mbsec,
			    ms_per_transfer, queue_len, busy_pct);
		} else if (serverOnly) {
		    printf("%s %6ju %6ju %6ju %6ju %6ju %6ju %6ju %6ju",
			((clientOnly && serverOnly) ? "Server:" : ""),
			(uintmax_t)DELTA(srvrpccnt[NFSV4OP_GETATTR]),
			(uintmax_t)DELTA(srvrpccnt[NFSV4OP_LOOKUP]),
			(uintmax_t)DELTA(srvrpccnt[NFSV4OP_READLINK]),
			(uintmax_t)DELTA(srvrpccnt[NFSV4OP_READ]),
			(uintmax_t)DELTA(srvrpccnt[NFSV4OP_WRITE]),
			(uintmax_t)DELTA(srvrpccnt[NFSV4OP_RENAME]),
			(uintmax_t)DELTA(srvrpccnt[NFSV4OP_ACCESS]),
			(uintmax_t)(DELTA(srvrpccnt[NFSV4OP_READDIR]) +
			DELTA(srvrpccnt[NFSV4OP_READDIRPLUS])));
		    printf("\n");
		}
		bcopy(&nfsstats, &lastst, sizeof(lastst));
		bcopy(&curtotal, &lasttotal, sizeof(lasttotal));
		lastts = ts;
		fflush(stdout);
		sleep(interval);
	}
	/*NOTREACHED*/
}
