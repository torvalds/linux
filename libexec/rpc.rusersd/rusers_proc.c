/*	$OpenBSD: rusers_proc.c,v 1.27 2019/06/28 13:32:53 deraadt Exp $	*/

/*-
 *  Copyright (c) 1993 John Brezak
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <paths.h>
#include <utmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpcsvc/rusers.h>	/* New version */
#include <rpcsvc/rnusers.h>	/* Old version */

extern int utmp_fd;

typedef char ut_line_t[UT_LINESIZE+1];
typedef char ut_name_t[UT_NAMESIZE+1];
typedef char ut_host_t[UT_HOSTSIZE+1];

struct rusers_utmp utmps[MAXUSERS];
struct utmpidle *utmp_idlep[MAXUSERS];
struct utmpidle utmp_idle[MAXUSERS];
struct ru_utmp *ru_utmpp[MAXUSERS];
struct ru_utmp ru_utmp[MAXUSERS];
ut_line_t line[MAXUSERS];
ut_name_t name[MAXUSERS];
ut_host_t host[MAXUSERS];

int *rusers_num_svc(void *, struct svc_req *);
struct utmpidlearr *rusersproc_names_2_svc(void *, struct svc_req *);
struct utmpidlearr *rusersproc_allnames_2_svc(void *, struct svc_req *);
struct utmparr *rusersproc_names_1_svc(void *, struct svc_req *);
struct utmparr *rusersproc_allnames_1_svc(void *, struct svc_req *);
void rusers_service(struct svc_req *, SVCXPRT *);

extern int from_inetd;

FILE *ufp;

static long
getidle(char *tty, int len)
{
	char devname[PATH_MAX];
	struct stat st;
	long idle;
	time_t now;

	snprintf(devname, sizeof devname, "%s/%.*s", _PATH_DEV,
	    len, tty);
	if (stat(devname, &st) == -1) {
#ifdef DEBUG
		printf("%s: %m\n", devname);
#endif
		return (0);
	}
	time(&now);
#ifdef DEBUG
	printf("%s: now=%lld atime=%lld\n", devname, (long long)now,
	    (long long)st.st_atime);
#endif
	idle = now - st.st_atime;
	idle = (idle + 30) / 60; /* secs->mins */
	if (idle < 0)
		idle = 0;

	return (idle);
}

int *
rusers_num_svc(void *arg, struct svc_req *rqstp)
{
	static int num_users = 0;
	struct utmp usr;
	int fd;

	fd = dup(utmp_fd);
	if (fd == -1) {
		syslog(LOG_ERR, "%m");
		return (0);
	}
	lseek(fd, 0, SEEK_SET);
	ufp = fdopen(fd, "r");
	if (!ufp) {
		close(fd);
		syslog(LOG_ERR, "%m");
		return (0);
	}

	/* only entries with both name and line fields */
	while (fread(&usr, sizeof(usr), 1, ufp) == 1)
		if (*usr.ut_name && *usr.ut_line)
			num_users++;

	fclose(ufp);
	return (&num_users);
}

static utmp_array *
do_names_3(int all)
{
	static utmp_array ut;
	struct utmp usr;
	int fd, nusers = 0;

	bzero(&ut, sizeof(ut));
	ut.utmp_array_val = &utmps[0];

	fd = dup(utmp_fd);
	if (fd == -1) {
		syslog(LOG_ERR, "%m");
		return (0);
	}
	lseek(fd, 0, SEEK_SET);
	ufp = fdopen(fd, "r");
	if (!ufp) {
		close(fd);
		syslog(LOG_ERR, "%m");
		return (NULL);
	}

	/* only entries with both name and line fields */
	while (fread(&usr, sizeof(usr), 1, ufp) == 1 &&
	    nusers < MAXUSERS)
		if (*usr.ut_name && *usr.ut_line) {
			utmps[nusers].ut_type = RUSERS_USER_PROCESS;
			utmps[nusers].ut_time = usr.ut_time;
			utmps[nusers].ut_idle = getidle(usr.ut_line,
			    sizeof usr.ut_line);
			utmps[nusers].ut_line = line[nusers];
			memset(line[nusers], 0, sizeof(line[nusers]));
			memcpy(line[nusers], usr.ut_line, UT_LINESIZE);
			line[nusers][UT_LINESIZE] = '\0';
			utmps[nusers].ut_user = name[nusers];
			memset(name[nusers], 0, sizeof(name[nusers]));
			memcpy(name[nusers], usr.ut_name, UT_NAMESIZE);
			name[nusers][UT_NAMESIZE] = '\0';
			utmps[nusers].ut_host = host[nusers];
			memset(host[nusers], 0, sizeof(host[nusers]));
			memcpy(host[nusers], usr.ut_host, UT_HOSTSIZE);
			host[nusers][UT_HOSTSIZE] = '\0';
			nusers++;
		}
	ut.utmp_array_len = nusers;

	fclose(ufp);
	return (&ut);
}

utmp_array *
rusersproc_names_3_svc(void *arg, struct svc_req *rqstp)
{
	return (do_names_3(0));
}

utmp_array *
rusersproc_allnames_3_svc(void *arg, struct svc_req *rqstp)
{
	return (do_names_3(1));
}

static struct utmpidlearr *
do_names_2(int all)
{
	static struct utmpidlearr ut;
	struct utmp usr;
	int fd, nusers = 0;

	bzero(&ut, sizeof(ut));
	ut.uia_arr = utmp_idlep;
	ut.uia_cnt = 0;

	fd = dup(utmp_fd);
	if (fd == -1) {
		syslog(LOG_ERR, "%m");
		return (0);
	}
	lseek(fd, 0, SEEK_SET);
	ufp = fdopen(fd, "r");
	if (!ufp) {
		close(fd);
		syslog(LOG_ERR, "%m");
		return (NULL);
	}

	/* only entries with both name and line fields */
	while (fread(&usr, sizeof(usr), 1, ufp) == 1 &&
	    nusers < MAXUSERS)
		if (*usr.ut_name && *usr.ut_line) {
			utmp_idlep[nusers] = &utmp_idle[nusers];
			utmp_idle[nusers].ui_utmp.ut_time = usr.ut_time;
			utmp_idle[nusers].ui_idle = getidle(usr.ut_line,
			    sizeof usr.ut_line);
			utmp_idle[nusers].ui_utmp.ut_line = line[nusers];
			memset(line[nusers], 0, sizeof(line[nusers]));
			memcpy(line[nusers], usr.ut_line, UT_LINESIZE);
			line[nusers][UT_LINESIZE] = '\0';
			utmp_idle[nusers].ui_utmp.ut_name = name[nusers];
			memset(name[nusers], 0, sizeof(name[nusers]));
			memcpy(name[nusers], usr.ut_name, UT_NAMESIZE);
			name[nusers][UT_NAMESIZE] = '\0';
			utmp_idle[nusers].ui_utmp.ut_host = host[nusers];
			memset(host[nusers], 0, sizeof(host[nusers]));
			memcpy(host[nusers], usr.ut_host, UT_HOSTSIZE);
			host[nusers][UT_HOSTSIZE] = '\0';
			nusers++;
		}

	ut.uia_cnt = nusers;
	fclose(ufp);
	return (&ut);
}

struct utmpidlearr *
rusersproc_names_2_svc(void *arg, struct svc_req *rqstp)
{
	return (do_names_2(0));
}

struct utmpidlearr *
rusersproc_allnames_2_svc(void *arg, struct svc_req *rqstp)
{
	return (do_names_2(1));
}

static struct utmparr *
do_names_1(int all)
{
	static struct utmparr ut;
	struct utmp usr;
	int fd, nusers = 0;

	bzero(&ut, sizeof(ut));
	ut.uta_arr = ru_utmpp;
	ut.uta_cnt = 0;

	fd = dup(utmp_fd);
	if (fd == -1) {
		syslog(LOG_ERR, "%m");
		return (0);
	}
	lseek(fd, 0, SEEK_SET);
	ufp = fdopen(fd, "r");
	if (!ufp) {
		close(fd);
		syslog(LOG_ERR, "%m");
		return (NULL);
	}

	/* only entries with both name and line fields */
	while (fread(&usr, sizeof(usr), 1, ufp) == 1 &&
	    nusers < MAXUSERS)
		if (*usr.ut_name && *usr.ut_line) {
			ru_utmpp[nusers] = &ru_utmp[nusers];
			ru_utmp[nusers].ut_time = usr.ut_time;
			ru_utmp[nusers].ut_line = line[nusers];
			memcpy(line[nusers], usr.ut_line, UT_LINESIZE);
			line[nusers][UT_LINESIZE] = '\0';
			ru_utmp[nusers].ut_name = name[nusers];
			memcpy(name[nusers], usr.ut_name, UT_NAMESIZE);
			name[nusers][UT_NAMESIZE] = '\0';
			ru_utmp[nusers].ut_host = host[nusers];
			memcpy(host[nusers], usr.ut_host, UT_HOSTSIZE);
			host[nusers][UT_HOSTSIZE] = '\0';
			nusers++;
		}

	ut.uta_cnt = nusers;
	fclose(ufp);
	return (&ut);
}

struct utmparr *
rusersproc_names_1_svc(void *arg, struct svc_req *rqstp)
{
	return (do_names_1(0));
}

struct utmparr *
rusersproc_allnames_1_svc(void *arg, struct svc_req *rqstp)
{
	return (do_names_1(1));
}

void
rusers_service(struct svc_req *rqstp, SVCXPRT *transp)
{
	char *(*local)(void *, struct svc_req *);
	xdrproc_t xdr_argument, xdr_result;
	union {
		int fill;
	} argument;
	char *result;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void)svc_sendreply(transp, xdr_void, (char *)NULL);
		goto leave;

	case RUSERSPROC_NUM:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_int;
		switch (rqstp->rq_vers) {
		case RUSERSVERS_3:
		case RUSERSVERS_IDLE:
		case RUSERSVERS_ORIG:
			local = (char *(*)(void *, struct svc_req *))
			    rusers_num_svc;
			break;
		default:
			svcerr_progvers(transp, RUSERSVERS_IDLE, RUSERSVERS_3);
			goto leave;
			/*NOTREACHED*/
		}
		break;

	case RUSERSPROC_NAMES:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_utmp_array;
		switch (rqstp->rq_vers) {
		case RUSERSVERS_3:
			local = (char *(*)(void *, struct svc_req *))
			    rusersproc_names_3_svc;
			break;

		case RUSERSVERS_IDLE:
			xdr_result = (xdrproc_t)xdr_utmpidlearr;
			local = (char *(*)(void *, struct svc_req *))
			    rusersproc_names_2_svc;
			break;

		case RUSERSVERS_ORIG:
			xdr_result = (xdrproc_t)xdr_utmpidlearr;
			local = (char *(*)(void *, struct svc_req *))
			    rusersproc_names_1_svc;
			break;

		default:
			svcerr_progvers(transp, RUSERSVERS_IDLE, RUSERSVERS_3);
			goto leave;
			/*NOTREACHED*/
		}
		break;

	case RUSERSPROC_ALLNAMES:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_utmp_array;
		switch (rqstp->rq_vers) {
		case RUSERSVERS_3:
			local = (char *(*)(void *, struct svc_req *))
			    rusersproc_allnames_3_svc;
			break;

		case RUSERSVERS_IDLE:
			xdr_result = (xdrproc_t)xdr_utmpidlearr;
			local = (char *(*)(void *, struct svc_req *))
			    rusersproc_allnames_2_svc;
			break;

		case RUSERSVERS_ORIG:
			xdr_result = (xdrproc_t)xdr_utmpidlearr;
			local = (char *(*)(void *, struct svc_req *))
			    rusersproc_allnames_1_svc;
			break;

		default:
			svcerr_progvers(transp, RUSERSVERS_IDLE, RUSERSVERS_3);
			goto leave;
			/*NOTREACHED*/
		}
		break;

	default:
		svcerr_noproc(transp);
		goto leave;
	}
	bzero((char *)&argument, sizeof(argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t)&argument)) {
		svcerr_decode(transp);
		goto leave;
	}
	result = (*local)(&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result))
		svcerr_systemerr(transp);

	if (!svc_freeargs(transp, xdr_argument, (caddr_t)&argument)) {
		syslog(LOG_ERR, "unable to free arguments");
		exit(1);
	}
leave:
	if (from_inetd)
		exit(0);
}
