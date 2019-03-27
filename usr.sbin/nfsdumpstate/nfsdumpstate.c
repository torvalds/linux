/*-
 * Copyright (c) 2009 Rick Macklem, University of Guelph
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include <nfs/nfssvc.h>

#include <fs/nfs/rpcv2.h>
#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfskpiport.h>
#include <fs/nfs/nfs.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	DUMPSIZE	10000

static void dump_lockstate(char *);
static void dump_openstate(void);
static void usage(void);
static char *open_flags(uint32_t);
static char *deleg_flags(uint32_t);
static char *lock_flags(uint32_t);
static char *client_flags(uint32_t);

static struct nfsd_dumpclients dp[DUMPSIZE];
static struct nfsd_dumplocks lp[DUMPSIZE];
static char flag_string[20];

int
main(int argc, char **argv)
{
	int ch, openstate;
	char *lockfile;

	if (modfind("nfsd") < 0)
		errx(1, "nfsd not loaded - self terminating");
	openstate = 0;
	lockfile = NULL;
	while ((ch = getopt(argc, argv, "ol:")) != -1)
		switch (ch) {
		case 'o':
			openstate = 1;
			break;
		case 'l':
			lockfile = optarg;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (openstate == 0 && lockfile == NULL)
		openstate = 1;
	else if (openstate != 0 && lockfile != NULL)
		errx(1, "-o and -l cannot both be specified");

	/*
	 * For -o, dump all open/lock state.
	 * For -l, dump lock state for that file.
	 */
	if (openstate != 0)
		dump_openstate();
	else
		dump_lockstate(lockfile);
	exit(0);
}

static void
usage(void)
{

	errx(1, "usage: nfsdumpstate [-o] [-l]");
}

/*
 * Dump all open/lock state.
 */
static void
dump_openstate(void)
{
	struct nfsd_dumplist dumplist;
	int cnt, i;

	dumplist.ndl_size = DUMPSIZE;
	dumplist.ndl_list = (void *)dp;
	if (nfssvc(NFSSVC_DUMPCLIENTS, &dumplist) < 0)
		errx(1, "Can't perform dump clients syscall");

	printf("%-13s %9.9s %9.9s %9.9s %9.9s %9.9s %9.9s %-15s %s\n",
	    "Flags", "OpenOwner", "Open", "LockOwner",
	    "Lock", "Deleg", "OldDeleg", "Clientaddr", "ClientID");
	/*
	 * Loop through results, printing them out.
	 */
	cnt = 0;
	while (dp[cnt].ndcl_clid.nclid_idlen > 0 && cnt < DUMPSIZE) {
		printf("%-13s ", client_flags(dp[cnt].ndcl_flags));
		printf("%9d %9d %9d %9d %9d %9d ",
		    dp[cnt].ndcl_nopenowners,
		    dp[cnt].ndcl_nopens,
		    dp[cnt].ndcl_nlockowners,
		    dp[cnt].ndcl_nlocks,
		    dp[cnt].ndcl_ndelegs,
		    dp[cnt].ndcl_nolddelegs);
		if (dp[cnt].ndcl_addrfam == AF_INET)
			printf("%-15s ",
			    inet_ntoa(dp[cnt].ndcl_cbaddr.sin_addr));
		for (i = 0; i < dp[cnt].ndcl_clid.nclid_idlen; i++)
			printf("%02x", dp[cnt].ndcl_clid.nclid_id[i]);
		printf("\n");
		cnt++;
	}
}

/*
 * Dump the lock state for a file.
 */
static void
dump_lockstate(char *fname)
{
	struct nfsd_dumplocklist dumplocklist;
	int cnt, i;

	dumplocklist.ndllck_size = DUMPSIZE;
	dumplocklist.ndllck_list = (void *)lp;
	dumplocklist.ndllck_fname = fname;
	if (nfssvc(NFSSVC_DUMPLOCKS, &dumplocklist) < 0)
		errx(1, "Can't dump locks for %s\n", fname);

	printf("%-11s %-36s %-15s %s\n",
	    "Open/Lock",
	    "          Stateid or Lock Range",
	    "Clientaddr",
	    "Owner and ClientID");
	/*
	 * Loop through results, printing them out.
	 */
	cnt = 0;
	while (lp[cnt].ndlck_clid.nclid_idlen > 0 && cnt < DUMPSIZE) {
		if (lp[cnt].ndlck_flags & NFSLCK_OPEN)
			printf("%-11s %9d %08x %08x %08x ",
			    open_flags(lp[cnt].ndlck_flags),
			    lp[cnt].ndlck_stateid.seqid,
			    lp[cnt].ndlck_stateid.other[0],
			    lp[cnt].ndlck_stateid.other[1],
			    lp[cnt].ndlck_stateid.other[2]);
		else if (lp[cnt].ndlck_flags & (NFSLCK_DELEGREAD |
		    NFSLCK_DELEGWRITE))
			printf("%-11s %9d %08x %08x %08x ",
			    deleg_flags(lp[cnt].ndlck_flags),
			    lp[cnt].ndlck_stateid.seqid,
			    lp[cnt].ndlck_stateid.other[0],
			    lp[cnt].ndlck_stateid.other[1],
			    lp[cnt].ndlck_stateid.other[2]);
		else
			printf("%-11s  %17jd %17jd ",
			    lock_flags(lp[cnt].ndlck_flags),
			    lp[cnt].ndlck_first,
			    lp[cnt].ndlck_end);
		if (lp[cnt].ndlck_addrfam == AF_INET)
			printf("%-15s ",
			    inet_ntoa(lp[cnt].ndlck_cbaddr.sin_addr));
		else
			printf("%-15s ", "  ");
		for (i = 0; i < lp[cnt].ndlck_owner.nclid_idlen; i++)
			printf("%02x", lp[cnt].ndlck_owner.nclid_id[i]);
		printf(" ");
		for (i = 0; i < lp[cnt].ndlck_clid.nclid_idlen; i++)
			printf("%02x", lp[cnt].ndlck_clid.nclid_id[i]);
		printf("\n");
		cnt++;
	}
}

/*
 * Parse the Open/Lock flag bits and create a string to be printed.
 */
static char *
open_flags(uint32_t flags)
{
	int i, j;

	strlcpy(flag_string, "Open ", sizeof (flag_string));
	i = 5;
	if (flags & NFSLCK_READACCESS)
		flag_string[i++] = 'R';
	if (flags & NFSLCK_WRITEACCESS)
		flag_string[i++] = 'W';
	flag_string[i++] = ' ';
	flag_string[i++] = 'D';
	flag_string[i] = 'N';
	j = i;
	if (flags & NFSLCK_READDENY)
		flag_string[i++] = 'R';
	if (flags & NFSLCK_WRITEDENY)
		flag_string[i++] = 'W';
	if (i == j)
		i++;
	flag_string[i] = '\0';
	return (flag_string);
}

static char *
deleg_flags(uint32_t flags)
{

	if (flags & NFSLCK_DELEGREAD)
		strlcpy(flag_string, "Deleg R", sizeof (flag_string));
	else
		strlcpy(flag_string, "Deleg W", sizeof (flag_string));
	return (flag_string);
}

static char *
lock_flags(uint32_t flags)
{

	if (flags & NFSLCK_READ)
		strlcpy(flag_string, "Lock R", sizeof (flag_string));
	else
		strlcpy(flag_string, "Lock W", sizeof (flag_string));
	return (flag_string);
}

static char *
client_flags(uint32_t flags)
{

	flag_string[0] = '\0';
	if (flags & LCL_NEEDSCONFIRM)
		strlcat(flag_string, "NC ", sizeof (flag_string));
	if (flags & LCL_CALLBACKSON)
		strlcat(flag_string, "CB ", sizeof (flag_string));
	if (flags & LCL_GSS)
		strlcat(flag_string, "GSS ", sizeof (flag_string));
	if (flags & LCL_ADMINREVOKED)
		strlcat(flag_string, "REV", sizeof (flag_string));
	return (flag_string);
}
