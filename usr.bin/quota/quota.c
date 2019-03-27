/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "from: @(#)quota.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */

/*
 * Disk quota reporting program.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/socket.h>

#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpcsvc/rquota.h>

#include <ufs/ufs/quota.h>

#include <ctype.h>
#include <err.h>
#include <fstab.h>
#include <grp.h>
#include <libutil.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *qfextension[] = INITQFNAMES;

struct quotause {
	struct	quotause *next;
	long	flags;
	struct	dqblk dqblk;
	char	fsname[MAXPATHLEN + 1];
};

static char *timeprt(int64_t seconds);
static struct quotause *getprivs(long id, int quotatype);
static void usage(void);
static int showuid(u_long uid);
static int showgid(u_long gid);
static int showusrname(char *name);
static int showgrpname(char *name);
static int showquotas(int type, u_long id, const char *name);
static void showrawquotas(int type, u_long id, struct quotause *qup);
static void heading(int type, u_long id, const char *name, const char *tag);
static int getufsquota(struct fstab *fs, struct quotause *qup, long id,
	int quotatype);
static int getnfsquota(struct statfs *fst, struct quotause *qup, long id,
	int quotatype);
static enum clnt_stat callaurpc(char *host, int prognum, int versnum, int procnum, 
	xdrproc_t inproc, char *in, xdrproc_t outproc, char *out);
static int alldigits(char *s);

static int	hflag;
static int	lflag;
static int	rflag;
static int	qflag;
static int	vflag;
static char	*filename = NULL;

int
main(int argc, char *argv[])
{
	int ngroups; 
	gid_t mygid, gidset[NGROUPS];
	int i, ch, gflag = 0, uflag = 0, errflag = 0;

	while ((ch = getopt(argc, argv, "f:ghlrquv")) != -1) {
		switch(ch) {
		case 'f':
			filename = optarg;
			break;
		case 'g':
			gflag++;
			break;
		case 'h':
			hflag++;
			break;
		case 'l':
			lflag++;
			break;
		case 'q':
			qflag++;
			break;
		case 'r':
			rflag++;
			break;
		case 'u':
			uflag++;
			break;
		case 'v':
			vflag++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (!uflag && !gflag)
		uflag++;
	if (argc == 0) {
		if (uflag)
			errflag += showuid(getuid());
		if (gflag) {
			mygid = getgid();
			ngroups = getgroups(NGROUPS, gidset);
			if (ngroups < 0)
				err(1, "getgroups");
			errflag += showgid(mygid);
			for (i = 0; i < ngroups; i++)
				if (gidset[i] != mygid)
					errflag += showgid(gidset[i]);
		}
		return(errflag);
	}
	if (uflag && gflag)
		usage();
	if (uflag) {
		for (; argc > 0; argc--, argv++) {
			if (alldigits(*argv))
				errflag += showuid(atoi(*argv));
			else
				errflag += showusrname(*argv);
		}
		return(errflag);
	}
	if (gflag) {
		for (; argc > 0; argc--, argv++) {
			if (alldigits(*argv))
				errflag += showgid(atoi(*argv));
			else
				errflag += showgrpname(*argv);
		}
	}
	return(errflag);
}

static void
usage(void)
{

	fprintf(stderr, "%s\n%s\n%s\n",
	    "usage: quota [-ghlu] [-f path] [-v | -q | -r]",
	    "       quota [-hlu] [-f path] [-v | -q | -r] user ...",
	    "       quota -g [-hl] [-f path] [-v | -q | -r] group ...");
	exit(1);
}

/*
 * Print out quotas for a specified user identifier.
 */
static int
showuid(u_long uid)
{
	struct passwd *pwd = getpwuid(uid);
	const char *name;

	if (pwd == NULL)
		name = "(no account)";
	else
		name = pwd->pw_name;
	return(showquotas(USRQUOTA, uid, name));
}

/*
 * Print out quotas for a specifed user name.
 */
static int
showusrname(char *name)
{
	struct passwd *pwd = getpwnam(name);

	if (pwd == NULL) {
		warnx("%s: unknown user", name);
		return(1);
	}
	return(showquotas(USRQUOTA, pwd->pw_uid, name));
}

/*
 * Print out quotas for a specified group identifier.
 */
static int
showgid(u_long gid)
{
	struct group *grp = getgrgid(gid);
	const char *name;

	if (grp == NULL)
		name = "(no entry)";
	else
		name = grp->gr_name;
	return(showquotas(GRPQUOTA, gid, name));
}

/*
 * Print out quotas for a specifed group name.
 */
static int
showgrpname(char *name)
{
	struct group *grp = getgrnam(name);

	if (grp == NULL) {
		warnx("%s: unknown group", name);
		return(1);
	}
	return(showquotas(GRPQUOTA, grp->gr_gid, name));
}

static void
prthumanval(int len, u_int64_t bytes)
{
	char buf[len + 1];

	/*
	 * Limit the width to 5 bytes as that is what users expect.
	 */
	humanize_number(buf, MIN(sizeof(buf), 5), bytes, "",
			HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);

	(void)printf(" %*s", len, buf);
}

static int
showquotas(int type, u_long id, const char *name)
{
	struct quotause *qup;
	struct quotause *quplist;
	const char *msgi, *msgb;
	const char *nam;
	char *bgrace = NULL, *igrace = NULL;
	int lines = 0, overquota = 0;
	static time_t now;

	if (now == 0)
		time(&now);
	quplist = getprivs(id, type);
	for (qup = quplist; qup; qup = qup->next) {
		msgi = NULL;
		if (qup->dqblk.dqb_ihardlimit &&
		    qup->dqblk.dqb_curinodes >= qup->dqblk.dqb_ihardlimit) {
			overquota++;
			msgi = "File limit reached on";
		}
		else if (qup->dqblk.dqb_isoftlimit &&
		    qup->dqblk.dqb_curinodes >= qup->dqblk.dqb_isoftlimit) {
			overquota++;
			if (qup->dqblk.dqb_itime > now)
				msgi = "In file grace period on";
			else
				msgi = "Over file quota on";
		}
		msgb = NULL;
		if (qup->dqblk.dqb_bhardlimit &&
		    qup->dqblk.dqb_curblocks >= qup->dqblk.dqb_bhardlimit) {
			overquota++;
			msgb = "Block limit reached on";
		}
		else if (qup->dqblk.dqb_bsoftlimit &&
		    qup->dqblk.dqb_curblocks >= qup->dqblk.dqb_bsoftlimit) {
			overquota++;
			if (qup->dqblk.dqb_btime > now)
				msgb = "In block grace period on";
			else
				msgb = "Over block quota on";
		}
		if (rflag) {
			showrawquotas(type, id, qup);
			continue;
		}
		if (!vflag &&
		    qup->dqblk.dqb_isoftlimit == 0 &&
		    qup->dqblk.dqb_ihardlimit == 0 &&
		    qup->dqblk.dqb_bsoftlimit == 0 &&
		    qup->dqblk.dqb_bhardlimit == 0)
			continue;
		if (qflag) {
			if ((msgi != NULL || msgb != NULL) &&
			    lines++ == 0)
				heading(type, id, name, "");
			if (msgi != NULL)
				printf("\t%s %s\n", msgi, qup->fsname);
			if (msgb != NULL)
				printf("\t%s %s\n", msgb, qup->fsname);
			continue;
		}
		if (!vflag &&
		    qup->dqblk.dqb_curblocks == 0 &&
		    qup->dqblk.dqb_curinodes == 0)
			continue;
		if (lines++ == 0)
			heading(type, id, name, "");
		nam = qup->fsname;
		if (strlen(qup->fsname) > 15) {
			printf("%s\n", qup->fsname);
			nam = "";
		} 
		printf("%-15s", nam);
		if (hflag) {
			prthumanval(7, dbtob(qup->dqblk.dqb_curblocks));
			printf("%c", (msgb == NULL) ? ' ' : '*');
			prthumanval(7, dbtob(qup->dqblk.dqb_bsoftlimit));
			prthumanval(7, dbtob(qup->dqblk.dqb_bhardlimit));
		} else {
			printf(" %7ju%c %7ju %7ju",
			    (uintmax_t)dbtob(qup->dqblk.dqb_curblocks)
				/ 1024,
			    (msgb == NULL) ? ' ' : '*',
			    (uintmax_t)dbtob(qup->dqblk.dqb_bsoftlimit)
				/ 1024,
			    (uintmax_t)dbtob(qup->dqblk.dqb_bhardlimit)
				/ 1024);
		}
		if (msgb != NULL)
			bgrace = timeprt(qup->dqblk.dqb_btime);
		if (msgi != NULL)
			igrace = timeprt(qup->dqblk.dqb_itime);
		printf("%8s %6ju%c %6ju %6ju%8s\n"
			, (msgb == NULL) ? "" : bgrace
			, (uintmax_t)qup->dqblk.dqb_curinodes
			, (msgi == NULL) ? ' ' : '*'
			, (uintmax_t)qup->dqblk.dqb_isoftlimit
			, (uintmax_t)qup->dqblk.dqb_ihardlimit
			, (msgi == NULL) ? "" : igrace
		);
		if (msgb != NULL)
			free(bgrace);
		if (msgi != NULL)
			free(igrace);
	}
	if (!qflag && !rflag && lines == 0)
		heading(type, id, name, "none");
	return (overquota);
}

static void
showrawquotas(int type, u_long id, struct quotause *qup)
{
	time_t t;

	printf("Raw %s quota information for id %lu on %s\n",
	    type == USRQUOTA ? "user" : "group", id, qup->fsname);
	printf("block hard limit:     %ju\n",
	    (uintmax_t)qup->dqblk.dqb_bhardlimit);
	printf("block soft limit:     %ju\n",
	    (uintmax_t)qup->dqblk.dqb_bsoftlimit);
	printf("current block count:  %ju\n",
	    (uintmax_t)qup->dqblk.dqb_curblocks);
	printf("i-node hard limit:    %ju\n",
	    (uintmax_t)qup->dqblk.dqb_ihardlimit);
	printf("i-node soft limit:    %ju\n",
	    (uintmax_t)qup->dqblk.dqb_isoftlimit);
	printf("current i-node count: %ju\n",
	    (uintmax_t)qup->dqblk.dqb_curinodes);
	printf("block grace time:     %jd",
	    (intmax_t)qup->dqblk.dqb_btime);
	if (qup->dqblk.dqb_btime != 0) {
		t = qup->dqblk.dqb_btime;
		printf(" %s", ctime(&t));
	} else {
		printf("\n");
	}
	printf("i-node grace time:    %jd", (intmax_t)qup->dqblk.dqb_itime);
	if (qup->dqblk.dqb_itime != 0) {
		t = qup->dqblk.dqb_itime;
		printf(" %s", ctime(&t));
	} else {
		printf("\n");
	}
}


static void
heading(int type, u_long id, const char *name, const char *tag)
{

	printf("Disk quotas for %s %s (%cid %lu): %s\n", qfextension[type],
	    name, *qfextension[type], id, tag);
	if (!qflag && tag[0] == '\0') {
		printf("%-15s %7s %8s %7s %7s %6s %7s %6s%8s\n"
			, "Filesystem"
			, "usage"
			, "quota"
			, "limit"
			, "grace"
			, "files"
			, "quota"
			, "limit"
			, "grace"
		);
	}
}

/*
 * Calculate the grace period and return a printable string for it.
 */
static char *
timeprt(int64_t seconds)
{
	time_t hours, minutes;
	char *buf;
	static time_t now;

	if (now == 0)
		time(&now);
	if (now > seconds) {
		if ((buf = strdup("none")) == NULL)
			errx(1, "strdup() failed in timeprt()");
		return (buf);
	}
	seconds -= now;
	minutes = (seconds + 30) / 60;
	hours = (minutes + 30) / 60;
	if (hours >= 36) {
		if (asprintf(&buf, "%lddays", ((long)hours + 12) / 24) < 0)
			errx(1, "asprintf() failed in timeprt(1)");
		return (buf);
	}
	if (minutes >= 60) {
		if (asprintf(&buf, "%2ld:%ld", (long)minutes / 60,
		    (long)minutes % 60) < 0)
			errx(1, "asprintf() failed in timeprt(2)");
		return (buf);
	}
	if (asprintf(&buf, "%2ld", (long)minutes) < 0)
		errx(1, "asprintf() failed in timeprt(3)");
	return (buf);
}

/*
 * Collect the requested quota information.
 */
static struct quotause *
getprivs(long id, int quotatype)
{
	struct quotause *qup, *quptail = NULL;
	struct fstab *fs;
	struct quotause *quphead;
	struct statfs *fst;
	int nfst, i;
	struct statfs sfb;

	qup = quphead = (struct quotause *)0;

	if (filename != NULL && statfs(filename, &sfb) != 0)
		err(1, "cannot statfs %s", filename);
	nfst = getmntinfo(&fst, MNT_NOWAIT);
	if (nfst == 0)
		errx(2, "no filesystems mounted!");
	setfsent();
	for (i = 0; i < nfst; i++) {
		if (qup == NULL) {
			if ((qup = (struct quotause *)malloc(sizeof *qup))
			    == NULL)
				errx(2, "out of memory");
		}
		/*
		 * See if the user requested a specific file system
		 * or specified a file inside a mounted file system.
		 */
		if (filename != NULL &&
		    strcmp(sfb.f_mntonname, fst[i].f_mntonname) != 0)
			continue;
		if (strcmp(fst[i].f_fstypename, "nfs") == 0) {
			if (lflag)
				continue;
			if (getnfsquota(&fst[i], qup, id, quotatype) == 0)
				continue;
		} else if (strcmp(fst[i].f_fstypename, "ufs") == 0) {
			/*
			 * XXX
			 * UFS filesystems must be in /etc/fstab, and must
			 * indicate that they have quotas on (?!) This is quite
			 * unlike SunOS where quotas can be enabled/disabled
			 * on a filesystem independent of /etc/fstab, and it
			 * will still print quotas for them.
			 */
			if ((fs = getfsspec(fst[i].f_mntfromname)) == NULL)
				continue;
			if (getufsquota(fs, qup, id, quotatype) == 0)
				continue;
		} else
			continue;
		strcpy(qup->fsname, fst[i].f_mntonname);
		if (quphead == NULL)
			quphead = qup;
		else
			quptail->next = qup;
		quptail = qup;
		quptail->next = 0;
		qup = NULL;
	}
	if (qup)
		free(qup);
	endfsent();
	return (quphead);
}

/*
 * Check to see if a particular quota is available.
 */
static int
getufsquota(struct fstab *fs, struct quotause *qup, long id, int quotatype)
{
	struct quotafile *qf;

	if ((qf = quota_open(fs, quotatype, O_RDONLY)) == NULL)
		return (0);
	if (quota_read(qf, &qup->dqblk, id) != 0)
		return (0);
	quota_close(qf);
	return (1);
}

static int
getnfsquota(struct statfs *fst, struct quotause *qup, long id, int quotatype)
{
	struct ext_getquota_args gq_args;
	struct getquota_args old_gq_args;
	struct getquota_rslt gq_rslt;
	struct dqblk *dqp = &qup->dqblk;
	struct timeval tv;
	char *cp, host[NI_MAXHOST];
	enum clnt_stat call_stat;

	if (fst->f_flags & MNT_LOCAL)
		return (0);

	/*
	 * must be some form of "hostname:/path"
	 */
	cp = fst->f_mntfromname;
	do {
		cp = strrchr(cp, ':');
	} while (cp != NULL && *(cp + 1) != '/');
	if (cp == NULL) {
		warnx("cannot find hostname for %s", fst->f_mntfromname);
		return (0);
	}
	memset(host, 0, sizeof(host));
	memcpy(host, fst->f_mntfromname, cp - fst->f_mntfromname);
	host[sizeof(host) - 1] = '\0';
 
	/* Avoid attempting the RPC for special amd(8) filesystems. */
	if (strncmp(fst->f_mntfromname, "pid", 3) == 0 &&
	    strchr(fst->f_mntfromname, '@') != NULL)
		return (0);

	gq_args.gqa_pathp = cp + 1;
	gq_args.gqa_id = id;
	gq_args.gqa_type = quotatype;

	call_stat = callaurpc(host, RQUOTAPROG, EXT_RQUOTAVERS,
			      RQUOTAPROC_GETQUOTA, (xdrproc_t)xdr_ext_getquota_args, (char *)&gq_args,
			      (xdrproc_t)xdr_getquota_rslt, (char *)&gq_rslt);
	if (call_stat == RPC_PROGVERSMISMATCH) {
		if (quotatype == USRQUOTA) {
			old_gq_args.gqa_pathp = cp + 1;
			old_gq_args.gqa_uid = id;
			call_stat = callaurpc(host, RQUOTAPROG, RQUOTAVERS,
					      RQUOTAPROC_GETQUOTA, (xdrproc_t)xdr_getquota_args, (char *)&old_gq_args,
					      (xdrproc_t)xdr_getquota_rslt, (char *)&gq_rslt);
		} else {
			/* Old rpc quota does not support group type */
			return (0);
		}
	}
	if (call_stat != 0)
		return (call_stat);

	switch (gq_rslt.status) {
	case Q_NOQUOTA:
		break;
	case Q_EPERM:
		warnx("quota permission error, host: %s",
			fst->f_mntfromname);
		break;
	case Q_OK:
		gettimeofday(&tv, NULL);
			/* blocks*/
		dqp->dqb_bhardlimit =
		    ((uint64_t)gq_rslt.getquota_rslt_u.gqr_rquota.rq_bhardlimit *
		    gq_rslt.getquota_rslt_u.gqr_rquota.rq_bsize) / DEV_BSIZE;
		dqp->dqb_bsoftlimit =
		    ((uint64_t)gq_rslt.getquota_rslt_u.gqr_rquota.rq_bsoftlimit *
		    gq_rslt.getquota_rslt_u.gqr_rquota.rq_bsize) / DEV_BSIZE;
		dqp->dqb_curblocks =
		    ((uint64_t)gq_rslt.getquota_rslt_u.gqr_rquota.rq_curblocks *
		    gq_rslt.getquota_rslt_u.gqr_rquota.rq_bsize) / DEV_BSIZE;
			/* inodes */
		dqp->dqb_ihardlimit =
			gq_rslt.getquota_rslt_u.gqr_rquota.rq_fhardlimit;
		dqp->dqb_isoftlimit =
			gq_rslt.getquota_rslt_u.gqr_rquota.rq_fsoftlimit;
		dqp->dqb_curinodes =
			gq_rslt.getquota_rslt_u.gqr_rquota.rq_curfiles;
			/* grace times */
		dqp->dqb_btime =
		    tv.tv_sec + gq_rslt.getquota_rslt_u.gqr_rquota.rq_btimeleft;
		dqp->dqb_itime =
		    tv.tv_sec + gq_rslt.getquota_rslt_u.gqr_rquota.rq_ftimeleft;
		return (1);
	default:
		warnx("bad rpc result, host: %s", fst->f_mntfromname);
		break;
	}

	return (0);
}
 
static enum clnt_stat
callaurpc(char *host, int prognum, int versnum, int procnum,
    xdrproc_t inproc, char *in, xdrproc_t outproc, char *out)
{
	enum clnt_stat clnt_stat;
	struct timeval timeout, tottimeout;
 
	CLIENT *client = NULL;

 	client = clnt_create(host, prognum, versnum, "udp");
	if (client == NULL)
		return ((int)rpc_createerr.cf_stat);
	timeout.tv_usec = 0;
	timeout.tv_sec = 6;
	CLNT_CONTROL(client, CLSET_RETRY_TIMEOUT, (char *)(void *)&timeout);

	client->cl_auth = authunix_create_default();
	tottimeout.tv_sec = 25;
	tottimeout.tv_usec = 0;
	clnt_stat = clnt_call(client, procnum, inproc, in,
	    outproc, out, tottimeout);
	return (clnt_stat);
}

static int
alldigits(char *s)
{
	int c;

	c = *s++;
	do {
		if (!isdigit(c))
			return (0);
	} while ((c = *s++));
	return (1);
}
