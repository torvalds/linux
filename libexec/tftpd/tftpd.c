/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)tftpd.c	8.1 (Berkeley) 6/4/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Trivial file transfer protocol server.
 *
 * This version includes many modifications by Jim Guyton
 * <guyton@rand-unix>.
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/tftp.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "tftp-file.h"
#include "tftp-io.h"
#include "tftp-utils.h"
#include "tftp-transfer.h"
#include "tftp-options.h"

#ifdef	LIBWRAP
#include <tcpd.h>
#endif

static void	tftp_wrq(int peer, char *, ssize_t);
static void	tftp_rrq(int peer, char *, ssize_t);

/*
 * Null-terminated directory prefix list for absolute pathname requests and
 * search list for relative pathname requests.
 *
 * MAXDIRS should be at least as large as the number of arguments that
 * inetd allows (currently 20).
 */
#define MAXDIRS	20
static struct dirlist {
	const char	*name;
	int	len;
} dirs[MAXDIRS+1];
static int	suppress_naks;
static int	logging;
static int	ipchroot;
static int	create_new = 0;
static const char *newfile_format = "%Y%m%d";
static int	increase_name = 0;
static mode_t	mask = S_IWGRP | S_IWOTH;

struct formats;
static void	tftp_recvfile(int peer, const char *mode);
static void	tftp_xmitfile(int peer, const char *mode);
static int	validate_access(int peer, char **, int);
static char	peername[NI_MAXHOST];

static FILE *file;

static struct formats {
	const char	*f_mode;
	int	f_convert;
} formats[] = {
	{ "netascii",	1 },
	{ "octet",	0 },
	{ NULL,		0 }
};

int
main(int argc, char *argv[])
{
	struct tftphdr *tp;
	int		peer;
	socklen_t	peerlen, len;
	ssize_t		n;
	int		ch;
	char		*chroot_dir = NULL;
	struct passwd	*nobody;
	const char	*chuser = "nobody";
	char		recvbuffer[MAXPKTSIZE];
	int		allow_ro = 1, allow_wo = 1;

	tzset();			/* syslog in localtime */
	acting_as_client = 0;

	tftp_openlog("tftpd", LOG_PID | LOG_NDELAY, LOG_FTP);
	while ((ch = getopt(argc, argv, "cCd:F:lnoOp:s:u:U:wW")) != -1) {
		switch (ch) {
		case 'c':
			ipchroot = 1;
			break;
		case 'C':
			ipchroot = 2;
			break;
		case 'd':
			if (atoi(optarg) != 0)
				debug += atoi(optarg);
			else
				debug |= debug_finds(optarg);
			break;
		case 'F':
			newfile_format = optarg;
			break;
		case 'l':
			logging = 1;
			break;
		case 'n':
			suppress_naks = 1;
			break;
		case 'o':
			options_rfc_enabled = 0;
			break;
		case 'O':
			options_extra_enabled = 0;
			break;
		case 'p':
			packetdroppercentage = atoi(optarg);
			tftp_log(LOG_INFO,
			    "Randomly dropping %d out of 100 packets",
			    packetdroppercentage);
			break;
		case 's':
			chroot_dir = optarg;
			break;
		case 'u':
			chuser = optarg;
			break;
		case 'U':
			mask = strtol(optarg, NULL, 0);
			break;
		case 'w':
			create_new = 1;
			break;
		case 'W':
			create_new = 1;
			increase_name = 1;
			break;
		default:
			tftp_log(LOG_WARNING,
				"ignoring unknown option -%c", ch);
		}
	}
	if (optind < argc) {
		struct dirlist *dirp;

		/* Get list of directory prefixes. Skip relative pathnames. */
		for (dirp = dirs; optind < argc && dirp < &dirs[MAXDIRS];
		     optind++) {
			if (argv[optind][0] == '/') {
				dirp->name = argv[optind];
				dirp->len  = strlen(dirp->name);
				dirp++;
			}
		}
	}
	else if (chroot_dir) {
		dirs->name = "/";
		dirs->len = 1;
	}
	if (ipchroot > 0 && chroot_dir == NULL) {
		tftp_log(LOG_ERR, "-c requires -s");
		exit(1);
	}

	umask(mask);

	{
		int on = 1;
		if (ioctl(0, FIONBIO, &on) < 0) {
			tftp_log(LOG_ERR, "ioctl(FIONBIO): %s", strerror(errno));
			exit(1);
		}
	}

	/* Find out who we are talking to and what we are going to do */
	peerlen = sizeof(peer_sock);
	n = recvfrom(0, recvbuffer, MAXPKTSIZE, 0,
	    (struct sockaddr *)&peer_sock, &peerlen);
	if (n < 0) {
		tftp_log(LOG_ERR, "recvfrom: %s", strerror(errno));
		exit(1);
	}
	getnameinfo((struct sockaddr *)&peer_sock, peer_sock.ss_len,
	    peername, sizeof(peername), NULL, 0, NI_NUMERICHOST);

	/*
	 * Now that we have read the message out of the UDP
	 * socket, we fork and exit.  Thus, inetd will go back
	 * to listening to the tftp port, and the next request
	 * to come in will start up a new instance of tftpd.
	 *
	 * We do this so that inetd can run tftpd in "wait" mode.
	 * The problem with tftpd running in "nowait" mode is that
	 * inetd may get one or more successful "selects" on the
	 * tftp port before we do our receive, so more than one
	 * instance of tftpd may be started up.  Worse, if tftpd
	 * break before doing the above "recvfrom", inetd would
	 * spawn endless instances, clogging the system.
	 */
	{
		int i, pid;

		for (i = 1; i < 20; i++) {
		    pid = fork();
		    if (pid < 0) {
				sleep(i);
				/*
				 * flush out to most recently sent request.
				 *
				 * This may drop some request, but those
				 * will be resent by the clients when
				 * they timeout.  The positive effect of
				 * this flush is to (try to) prevent more
				 * than one tftpd being started up to service
				 * a single request from a single client.
				 */
				peerlen = sizeof peer_sock;
				i = recvfrom(0, recvbuffer, MAXPKTSIZE, 0,
				    (struct sockaddr *)&peer_sock, &peerlen);
				if (i > 0) {
					n = i;
				}
		    } else {
				break;
		    }
		}
		if (pid < 0) {
			tftp_log(LOG_ERR, "fork: %s", strerror(errno));
			exit(1);
		} else if (pid != 0) {
			exit(0);
		}
	}

#ifdef	LIBWRAP
	/*
	 * See if the client is allowed to talk to me.
	 * (This needs to be done before the chroot())
	 */
	{
		struct request_info req;

		request_init(&req, RQ_CLIENT_ADDR, peername, 0);
		request_set(&req, RQ_DAEMON, "tftpd", 0);

		if (hosts_access(&req) == 0) {
			if (debug&DEBUG_ACCESS)
				tftp_log(LOG_WARNING,
				    "Access denied by 'tftpd' entry "
				    "in /etc/hosts.allow");

			/*
			 * Full access might be disabled, but maybe the
			 * client is allowed to do read-only access.
			 */
			request_set(&req, RQ_DAEMON, "tftpd-ro", 0);
			allow_ro = hosts_access(&req);

			request_set(&req, RQ_DAEMON, "tftpd-wo", 0);
			allow_wo = hosts_access(&req);

			if (allow_ro == 0 && allow_wo == 0) {
				tftp_log(LOG_WARNING,
				    "Unauthorized access from %s", peername);
				exit(1);
			}

			if (debug&DEBUG_ACCESS) {
				if (allow_ro)
					tftp_log(LOG_WARNING,
					    "But allowed readonly access "
					    "via 'tftpd-ro' entry");
				if (allow_wo)
					tftp_log(LOG_WARNING,
					    "But allowed writeonly access "
					    "via 'tftpd-wo' entry");
			}
		} else
			if (debug&DEBUG_ACCESS)
				tftp_log(LOG_WARNING,
				    "Full access allowed"
				    "in /etc/hosts.allow");
	}
#endif

	/*
	 * Since we exit here, we should do that only after the above
	 * recvfrom to keep inetd from constantly forking should there
	 * be a problem.  See the above comment about system clogging.
	 */
	if (chroot_dir) {
		if (ipchroot > 0) {
			char *tempchroot;
			struct stat sb;
			int statret;
			struct sockaddr_storage ss;
			char hbuf[NI_MAXHOST];

			statret = -1;
			memcpy(&ss, &peer_sock, peer_sock.ss_len);
			unmappedaddr((struct sockaddr_in6 *)&ss);
			getnameinfo((struct sockaddr *)&ss, ss.ss_len,
				    hbuf, sizeof(hbuf), NULL, 0,
				    NI_NUMERICHOST);
			asprintf(&tempchroot, "%s/%s", chroot_dir, hbuf);
			if (ipchroot == 2)
				statret = stat(tempchroot, &sb);
			if (ipchroot == 1 ||
			    (statret == 0 && (sb.st_mode & S_IFDIR)))
				chroot_dir = tempchroot;
		}
		/* Must get this before chroot because /etc might go away */
		if ((nobody = getpwnam(chuser)) == NULL) {
			tftp_log(LOG_ERR, "%s: no such user", chuser);
			exit(1);
		}
		if (chroot(chroot_dir)) {
			tftp_log(LOG_ERR, "chroot: %s: %s",
			    chroot_dir, strerror(errno));
			exit(1);
		}
		chdir("/");
		if (setgroups(1, &nobody->pw_gid) != 0) {
			tftp_log(LOG_ERR, "setgroups failed");
			exit(1);
		}
		if (setuid(nobody->pw_uid) != 0) {
			tftp_log(LOG_ERR, "setuid failed");
			exit(1);
		}
	}

	len = sizeof(me_sock);
	if (getsockname(0, (struct sockaddr *)&me_sock, &len) == 0) {
		switch (me_sock.ss_family) {
		case AF_INET:
			((struct sockaddr_in *)&me_sock)->sin_port = 0;
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)&me_sock)->sin6_port = 0;
			break;
		default:
			/* unsupported */
			break;
		}
	} else {
		memset(&me_sock, 0, sizeof(me_sock));
		me_sock.ss_family = peer_sock.ss_family;
		me_sock.ss_len = peer_sock.ss_len;
	}
	close(0);
	close(1);
	peer = socket(peer_sock.ss_family, SOCK_DGRAM, 0);
	if (peer < 0) {
		tftp_log(LOG_ERR, "socket: %s", strerror(errno));
		exit(1);
	}
	if (bind(peer, (struct sockaddr *)&me_sock, me_sock.ss_len) < 0) {
		tftp_log(LOG_ERR, "bind: %s", strerror(errno));
		exit(1);
	}

	tp = (struct tftphdr *)recvbuffer;
	tp->th_opcode = ntohs(tp->th_opcode);
	if (tp->th_opcode == RRQ) {
		if (allow_ro)
			tftp_rrq(peer, tp->th_stuff, n - 1);
		else {
			tftp_log(LOG_WARNING,
			    "%s read access denied", peername);
			exit(1);
		}
	} else if (tp->th_opcode == WRQ) {
		if (allow_wo)
			tftp_wrq(peer, tp->th_stuff, n - 1);
		else {
			tftp_log(LOG_WARNING,
			    "%s write access denied", peername);
			exit(1);
		}
	} else
		send_error(peer, EBADOP);
	exit(1);
}

static void
reduce_path(char *fn)
{
	char *slash, *ptr;

	/* Reduce all "/+./" to "/" (just in case we've got "/./../" later */
	while ((slash = strstr(fn, "/./")) != NULL) {
		for (ptr = slash; ptr > fn && ptr[-1] == '/'; ptr--)
			;
		slash += 2;
		while (*slash)
			*++ptr = *++slash;
	}

	/* Now reduce all "/something/+../" to "/" */
	while ((slash = strstr(fn, "/../")) != NULL) {
		if (slash == fn)
			break;
		for (ptr = slash; ptr > fn && ptr[-1] == '/'; ptr--)
			;
		for (ptr--; ptr >= fn; ptr--)
			if (*ptr == '/')
				break;
		if (ptr < fn)
			break;
		slash += 3;
		while (*slash)
			*++ptr = *++slash;
	}
}

static char *
parse_header(int peer, char *recvbuffer, ssize_t size,
	char **filename, char **mode)
{
	char	*cp;
	int	i;
	struct formats *pf;

	*mode = NULL;
	cp = recvbuffer;

	i = get_field(peer, recvbuffer, size);
	if (i >= PATH_MAX) {
		tftp_log(LOG_ERR, "Bad option - filename too long");
		send_error(peer, EBADOP);
		exit(1);
	}
	*filename = recvbuffer;
	tftp_log(LOG_INFO, "Filename: '%s'", *filename);
	cp += i;

	i = get_field(peer, cp, size);
	*mode = cp;
	cp += i;

	/* Find the file transfer mode */
	for (cp = *mode; *cp; cp++)
		if (isupper(*cp))
			*cp = tolower(*cp);
	for (pf = formats; pf->f_mode; pf++)
		if (strcmp(pf->f_mode, *mode) == 0)
			break;
	if (pf->f_mode == NULL) {
		tftp_log(LOG_ERR,
		    "Bad option - Unknown transfer mode (%s)", *mode);
		send_error(peer, EBADOP);
		exit(1);
	}
	tftp_log(LOG_INFO, "Mode: '%s'", *mode);

	return (cp + 1);
}

/*
 * WRQ - receive a file from the client
 */
void
tftp_wrq(int peer, char *recvbuffer, ssize_t size)
{
	char *cp;
	int has_options = 0, ecode;
	char *filename, *mode;
	char fnbuf[PATH_MAX];

	cp = parse_header(peer, recvbuffer, size, &filename, &mode);
	size -= (cp - recvbuffer) + 1;

	strlcpy(fnbuf, filename, sizeof(fnbuf));
	reduce_path(fnbuf);
	filename = fnbuf;

	if (size > 0) {
		if (options_rfc_enabled)
			has_options = !parse_options(peer, cp, size);
		else
			tftp_log(LOG_INFO, "Options found but not enabled");
	}

	ecode = validate_access(peer, &filename, WRQ);
	if (ecode == 0) {
		if (has_options)
			send_oack(peer);
		else
			send_ack(peer, 0);
	}
	if (logging) {
		tftp_log(LOG_INFO, "%s: write request for %s: %s", peername,
			    filename, errtomsg(ecode));
	}

	if (ecode) {
		send_error(peer, ecode);
		exit(1);
	}
	tftp_recvfile(peer, mode);
	exit(0);
}

/*
 * RRQ - send a file to the client
 */
void
tftp_rrq(int peer, char *recvbuffer, ssize_t size)
{
	char *cp;
	int has_options = 0, ecode;
	char *filename, *mode;
	char	fnbuf[PATH_MAX];

	cp = parse_header(peer, recvbuffer, size, &filename, &mode);
	size -= (cp - recvbuffer) + 1;

	strlcpy(fnbuf, filename, sizeof(fnbuf));
	reduce_path(fnbuf);
	filename = fnbuf;

	if (size > 0) {
		if (options_rfc_enabled)
			has_options = !parse_options(peer, cp, size);
		else
			tftp_log(LOG_INFO, "Options found but not enabled");
	}

	ecode = validate_access(peer, &filename, RRQ);
	if (ecode == 0) {
		if (has_options) {
			int n;
			char lrecvbuffer[MAXPKTSIZE];
			struct tftphdr *rp = (struct tftphdr *)lrecvbuffer;

			send_oack(peer);
			n = receive_packet(peer, lrecvbuffer, MAXPKTSIZE,
				NULL, timeoutpacket);
			if (n < 0) {
				if (debug&DEBUG_SIMPLE)
					tftp_log(LOG_DEBUG, "Aborting: %s",
					    rp_strerror(n));
				return;
			}
			if (rp->th_opcode != ACK) {
				if (debug&DEBUG_SIMPLE)
					tftp_log(LOG_DEBUG,
					    "Expected ACK, got %s on OACK",
					    packettype(rp->th_opcode));
				return;
			}
		}
	}

	if (logging)
		tftp_log(LOG_INFO, "%s: read request for %s: %s", peername,
			    filename, errtomsg(ecode));

	if (ecode) {
		/*
		 * Avoid storms of naks to a RRQ broadcast for a relative
		 * bootfile pathname from a diskless Sun.
		 */
		if (suppress_naks && *filename != '/' && ecode == ENOTFOUND)
			exit(0);
		send_error(peer, ecode);
		exit(1);
	}
	tftp_xmitfile(peer, mode);
}

/*
 * Find the next value for YYYYMMDD.nn when the file to be written should
 * be unique. Due to the limitations of nn, we will fail if nn reaches 100.
 * Besides, that is four updates per hour on a file, which is kind of
 * execessive anyway.
 */
static int
find_next_name(char *filename, int *fd)
{
	int i;
	time_t tval;
	size_t len;
	struct tm lt;
	char yyyymmdd[MAXPATHLEN];
	char newname[MAXPATHLEN];

	/* Create the YYYYMMDD part of the filename */
	time(&tval);
	lt = *localtime(&tval);
	len = strftime(yyyymmdd, sizeof(yyyymmdd), newfile_format, &lt);
	if (len == 0) {
		syslog(LOG_WARNING,
			"Filename suffix too long (%d characters maximum)",
			MAXPATHLEN);
		return (EACCESS);
	}

	/* Make sure the new filename is not too long */
	if (strlen(filename) > MAXPATHLEN - len - 5) {
		syslog(LOG_WARNING,
			"Filename too long (%zd characters, %zd maximum)",
			strlen(filename), MAXPATHLEN - len - 5);
		return (EACCESS);
	}

	/* Find the first file which doesn't exist */
	for (i = 0; i < 100; i++) {
		sprintf(newname, "%s.%s.%02d", filename, yyyymmdd, i);
		*fd = open(newname,
		    O_WRONLY | O_CREAT | O_EXCL, 
		    S_IRUSR | S_IWUSR | S_IRGRP |
		    S_IWGRP | S_IROTH | S_IWOTH);
		if (*fd > 0)
			return 0;
	}

	return (EEXIST);
}

/*
 * Validate file access.  Since we
 * have no uid or gid, for now require
 * file to exist and be publicly
 * readable/writable.
 * If we were invoked with arguments
 * from inetd then the file must also be
 * in one of the given directory prefixes.
 * Note also, full path name must be
 * given as we have no login directory.
 */
int
validate_access(int peer, char **filep, int mode)
{
	struct stat stbuf;
	int	fd;
	int	error;
	struct dirlist *dirp;
	static char pathname[MAXPATHLEN];
	char *filename = *filep;

	/*
	 * Prevent tricksters from getting around the directory restrictions
	 */
	if (strstr(filename, "/../"))
		return (EACCESS);

	if (*filename == '/') {
		/*
		 * Allow the request if it's in one of the approved locations.
		 * Special case: check the null prefix ("/") by looking
		 * for length = 1 and relying on the arg. processing that
		 * it's a /.
		 */
		for (dirp = dirs; dirp->name != NULL; dirp++) {
			if (dirp->len == 1 ||
			    (!strncmp(filename, dirp->name, dirp->len) &&
			     filename[dirp->len] == '/'))
				    break;
		}
		/* If directory list is empty, allow access to any file */
		if (dirp->name == NULL && dirp != dirs)
			return (EACCESS);
		if (stat(filename, &stbuf) < 0)
			return (errno == ENOENT ? ENOTFOUND : EACCESS);
		if ((stbuf.st_mode & S_IFMT) != S_IFREG)
			return (ENOTFOUND);
		if (mode == RRQ) {
			if ((stbuf.st_mode & S_IROTH) == 0)
				return (EACCESS);
		} else {
			if ((stbuf.st_mode & S_IWOTH) == 0)
				return (EACCESS);
		}
	} else {
		int err;

		/*
		 * Relative file name: search the approved locations for it.
		 * Don't allow write requests that avoid directory
		 * restrictions.
		 */

		if (!strncmp(filename, "../", 3))
			return (EACCESS);

		/*
		 * If the file exists in one of the directories and isn't
		 * readable, continue looking. However, change the error code
		 * to give an indication that the file exists.
		 */
		err = ENOTFOUND;
		for (dirp = dirs; dirp->name != NULL; dirp++) {
			snprintf(pathname, sizeof(pathname), "%s/%s",
				dirp->name, filename);
			if (stat(pathname, &stbuf) == 0 &&
			    (stbuf.st_mode & S_IFMT) == S_IFREG) {
				if (mode == RRQ) {
					if ((stbuf.st_mode & S_IROTH) != 0)
						break;
				} else {
					if ((stbuf.st_mode & S_IWOTH) != 0)
						break;
				}
				err = EACCESS;
			}
		}
		if (dirp->name != NULL)
			*filep = filename = pathname;
		else if (mode == RRQ)
			return (err);
		else if (err != ENOTFOUND || !create_new)
			return (err);
	}

	/*
	 * This option is handled here because it (might) require(s) the
	 * size of the file.
	 */
	option_tsize(peer, NULL, mode, &stbuf);

	if (mode == RRQ)
		fd = open(filename, O_RDONLY);
	else {
		if (create_new) {
			if (increase_name) {
				error = find_next_name(filename, &fd);
				if (error > 0)
					return (error + 100);
			} else
				fd = open(filename,
				    O_WRONLY | O_TRUNC | O_CREAT, 
				    S_IRUSR | S_IWUSR | S_IRGRP | 
				    S_IWGRP | S_IROTH | S_IWOTH );
		} else
			fd = open(filename, O_WRONLY | O_TRUNC);
	}
	if (fd < 0)
		return (errno + 100);
	file = fdopen(fd, (mode == RRQ)? "r":"w");
	if (file == NULL) {
		close(fd);
		return (errno + 100);
	}
	return (0);
}

static void
tftp_xmitfile(int peer, const char *mode)
{
	uint16_t block;
	time_t now;
	struct tftp_stats ts;

	memset(&ts, 0, sizeof(ts));
	now = time(NULL);
	if (debug&DEBUG_SIMPLE)
		tftp_log(LOG_DEBUG, "Transmitting file");

	read_init(0, file, mode);
	block = 1;
	tftp_send(peer, &block, &ts);
	read_close();
	if (debug&DEBUG_SIMPLE)
		tftp_log(LOG_INFO, "Sent %jd bytes in %jd seconds",
		    (intmax_t)ts.amount, (intmax_t)time(NULL) - now);
}

static void
tftp_recvfile(int peer, const char *mode)
{
	uint16_t block;
	struct timeval now1, now2;
	struct tftp_stats ts;

	gettimeofday(&now1, NULL);
	if (debug&DEBUG_SIMPLE)
		tftp_log(LOG_DEBUG, "Receiving file");

	write_init(0, file, mode);

	block = 0;
	tftp_receive(peer, &block, &ts, NULL, 0);

	gettimeofday(&now2, NULL);

	if (debug&DEBUG_SIMPLE) {
		double f;
		if (now1.tv_usec > now2.tv_usec) {
			now2.tv_usec += 1000000;
			now2.tv_sec--;
		}

		f = now2.tv_sec - now1.tv_sec +
		    (now2.tv_usec - now1.tv_usec) / 100000.0;
		tftp_log(LOG_INFO,
		    "Download of %jd bytes in %d blocks completed after %0.1f seconds\n",
		    (intmax_t)ts.amount, block, f);
	}

	return;
}
