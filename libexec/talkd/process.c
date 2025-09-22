/*	$OpenBSD: process.c,v 1.23 2016/03/16 15:41:10 krw Exp $	*/

/*
 * Copyright (c) 1983 Regents of the University of California.
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

/*
 * process.c handles the requests, which can be of three types:
 *	ANNOUNCE - announce to a user that a talk is wanted
 *	LEAVE_INVITE - insert the request into the table
 *	LOOK_UP - look up to see if a request is waiting in
 *		  in the table for the local user
 *	DELETE - delete invitation
 */
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <protocols/talkd.h>

#include <ctype.h>
#include <limits.h>
#include <netdb.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <utmp.h>

#include "talkd.h"

#define	satosin(sa)	((struct sockaddr_in *)(sa))

void
process_request(CTL_MSG *mp, CTL_RESPONSE *rp)
{
	CTL_MSG *ptr;
	char *s;

	rp->vers = TALK_VERSION;
	rp->type = mp->type;
	rp->id_num = htonl(0);
	if (mp->vers != TALK_VERSION) {
		syslog(LOG_WARNING, "Bad protocol version %d", mp->vers);
		rp->answer = BADVERSION;
		return;
	}
	mp->id_num = ntohl(mp->id_num);
	if (ntohs(mp->addr.sa_family) != AF_INET) {
		syslog(LOG_WARNING, "Bad address, family %d",
		    ntohs(mp->addr.sa_family));
		rp->answer = BADADDR;
		return;
	}
	if (ntohs(mp->ctl_addr.sa_family) != AF_INET) {
		syslog(LOG_WARNING, "Bad control address, family %d",
		    ntohs(mp->ctl_addr.sa_family));
		rp->answer = BADCTLADDR;
		return;
	}
	for (s = mp->l_name; *s; s++)
		if (!isprint((unsigned char)*s)) {
			syslog(LOG_NOTICE, "Illegal user name. Aborting");
			rp->answer = FAILED;
			return;
		}
	if (memcmp(&satosin(&rp->addr)->sin_addr,
	    &satosin(&mp->ctl_addr)->sin_addr,
	    sizeof(struct in_addr))) {
		char buf1[32], buf2[32];

		strlcpy(buf1, inet_ntoa(satosin(&rp->addr)->sin_addr),
		    sizeof(buf1));
		strlcpy(buf2, inet_ntoa(satosin(&mp->ctl_addr)->sin_addr),
		    sizeof(buf2));
		syslog(LOG_WARNING, "addresses are different, %s != %s",
		    buf1, buf2);
	}
	rp->addr.sa_family = 0;
	mp->pid = ntohl(mp->pid);
	if (debug)
		print_request("process_request", mp);
	switch (mp->type) {

	case ANNOUNCE:
		do_announce(mp, rp);
		break;

	case LEAVE_INVITE:
		ptr = find_request(mp);
		if (ptr != NULL) {
			rp->id_num = htonl(ptr->id_num);
			rp->answer = SUCCESS;
		} else
			insert_table(mp, rp);
		break;

	case LOOK_UP:
		ptr = find_match(mp);
		if (ptr != NULL) {
			rp->id_num = htonl(ptr->id_num);
			rp->addr = ptr->addr;
			rp->addr.sa_family = ptr->addr.sa_family;
			rp->answer = SUCCESS;
		} else
			rp->answer = NOT_HERE;
		break;

	case DELETE:
		rp->answer = delete_invite(mp->id_num);
		break;

	default:
		rp->answer = UNKNOWN_REQUEST;
		break;
	}
	if (debug)
		print_response("process_request", rp);
}

void
do_announce(CTL_MSG *mp, CTL_RESPONSE *rp)
{
	struct hostent *hp;
	CTL_MSG *ptr;
	int result;

	/* see if the user is logged */
	result = find_user(mp->r_name, mp->r_tty, sizeof(mp->r_tty));
	if (result != SUCCESS) {
		rp->answer = result;
		return;
	}
	hp = gethostbyaddr((char *)&satosin(&mp->ctl_addr)->sin_addr,
		sizeof(struct in_addr), AF_INET);
	if (hp == NULL) {
		rp->answer = MACHINE_UNKNOWN;
		return;
	}
	ptr = find_request(mp);
	if (ptr == (CTL_MSG *) 0) {
		insert_table(mp, rp);
		rp->answer = announce(mp, hp->h_name);
		return;
	}
	if (mp->id_num > ptr->id_num) {
		/*
		 * This is an explicit re-announce, so update the id_num
		 * field to avoid duplicates and re-announce the talk.
		 */
		ptr->id_num = new_id();
		rp->id_num = htonl(ptr->id_num);
		rp->answer = announce(mp, hp->h_name);
	} else {
		/* a duplicated request, so ignore it */
		rp->id_num = htonl(ptr->id_num);
		rp->answer = SUCCESS;
	}
}

/*
 * Search utmp for the local user
 */
int
find_user(char *name, char *tty, size_t ttyl)
{
	struct utmp ubuf, ubuf1;
	int status;
	FILE *fp;
	char line[UT_LINESIZE+1];
	char ftty[PATH_MAX];
	time_t	idle, now;

	time(&now);
	idle = INT_MAX;
	if ((fp = fopen(_PATH_UTMP, "r")) == NULL) {
		fprintf(stderr, "talkd: can't read %s.\n", _PATH_UTMP);
		return (FAILED);
	}
#define SCMPN(a, b)	strncmp(a, b, sizeof(a))
	status = NOT_HERE;
	(void) strlcpy(ftty, _PATH_DEV, sizeof(ftty));
	while (fread((char *) &ubuf, sizeof(ubuf), 1, fp) == 1)
		if (SCMPN(ubuf.ut_name, name) == 0) {
			if (*tty == '\0') {
				/* no particular tty was requested */
				struct stat statb;

				memcpy(line, ubuf.ut_line, UT_LINESIZE);
				line[sizeof(line)-1] = '\0';
				ftty[sizeof(_PATH_DEV)-1] = '\0';
				strlcat(ftty, line, sizeof(ftty));
				if (stat(ftty, &statb) == 0) {
					if (!(statb.st_mode & S_IWGRP)) {
						if (status == NOT_HERE)
							status = PERMISSION_DENIED;
					} else if (now - statb.st_atime < idle) {
						idle = now - statb.st_atime;
						status = SUCCESS;
						ubuf1 = ubuf;
					}
				}
			} else if (SCMPN(ubuf.ut_line, tty) == 0) {
				status = SUCCESS;
				break;
			}
		}
	fclose(fp);
	if (*tty == '\0' && status == SUCCESS) {
		memcpy(line, ubuf1.ut_line, UT_LINESIZE);
		line[sizeof(line)-1] = '\0';
		strlcpy(tty, line, ttyl);
	}
	return (status);
}
