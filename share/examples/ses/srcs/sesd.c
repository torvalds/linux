/* $FreeBSD$ */
/*
 * Copyright (c) 2000 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Matthew Jacob
 * Feral Software
 * mjacob@feral.com
 */
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_enc.h>

#define	ALLSTAT (SES_ENCSTAT_UNRECOV | SES_ENCSTAT_CRITICAL | \
	SES_ENCSTAT_NONCRITICAL | SES_ENCSTAT_INFO)

/*
 * Monitor named SES devices and note (via syslog) any changes in status.
 */

int
main(int a, char **v)
{
	static const char *usage =
	    "usage: %s [ -c ] [ -d ] [ -t pollinterval ] device [ device ]\n";
	int fd, polltime, dev, nodaemon, clear, c;
	encioc_enc_status_t stat, nstat, *carray;

	if (a < 2) {
		fprintf(stderr, usage, *v);
		return (1);
	}

	nodaemon = 0;
	polltime = 30;
	clear = 0;
	while ((c = getopt(a, v, "cdt:")) != -1) {
		switch (c) {
		case 'c':
			clear = 1;
			break;
		case 'd':
			nodaemon = 1;
			break;
		case 't':
			polltime = atoi(optarg);
			break;
		default:
			fprintf(stderr, usage, *v);
			return (1);
		}
	}

	carray = malloc(a);
	if (carray == NULL) {
		perror("malloc");
		return (1);
	}
	for (dev = optind; dev < a; dev++)
		carray[dev] = (encioc_enc_status_t) -1;

	/*
	 * Check to make sure we can open all devices
	 */
	for (dev = optind; dev < a; dev++) {
		fd = open(v[dev], O_RDWR);
		if (fd < 0) {
			perror(v[dev]);
			return (1);
		}
		if (ioctl(fd, ENCIOC_INIT, NULL) < 0) {
			fprintf(stderr, "%s: ENCIOC_INIT fails- %s\n",
			    v[dev], strerror(errno));
			return (1);
		}
		(void) close(fd);
	}
	if (nodaemon == 0) {
		if (daemon(0, 0) < 0) {
			perror("daemon");
			return (1);
		}
		openlog("sesd", LOG_CONS, LOG_USER);
	} else {
		openlog("sesd", LOG_CONS|LOG_PERROR, LOG_USER);
	}

	for (;;) {
		for (dev = optind; dev < a; dev++) {
			fd = open(v[dev], O_RDWR);
			if (fd < 0) {
				syslog(LOG_ERR, "%s: %m", v[dev]);
				continue;
			}

			/*
			 * Get the actual current enclosure status.
			 */
			if (ioctl(fd, ENCIOC_GETENCSTAT, (caddr_t) &stat) < 0) {
				syslog(LOG_ERR,
				    "%s: ENCIOC_GETENCSTAT- %m", v[dev]);
				(void) close(fd);
				continue;
			}
			if (stat != 0 && clear) {
				nstat = 0;
				if (ioctl(fd, ENCIOC_SETENCSTAT,
				    (caddr_t) &nstat) < 0) {
					syslog(LOG_ERR,
					    "%s: ENCIOC_SETENCSTAT- %m", v[dev]);
				}
			}
			(void) close(fd);

			if (stat == carray[dev])
				continue;

			carray[dev] = stat;
			if ((stat & ALLSTAT) == 0) {
				syslog(LOG_NOTICE,
				    "%s: Enclosure Status OK", v[dev]);
			}
			if (stat & SES_ENCSTAT_INFO) {
				syslog(LOG_NOTICE,
				    "%s: Enclosure Has Information", v[dev]);
			}
			if (stat & SES_ENCSTAT_NONCRITICAL) {
				syslog(LOG_WARNING,
				    "%s: Enclosure Non-Critical", v[dev]);
			}
			if (stat & SES_ENCSTAT_CRITICAL) {
				syslog(LOG_CRIT,
				    "%s: Enclosure Critical", v[dev]);
			}
			if (stat & SES_ENCSTAT_UNRECOV) {
				syslog(LOG_ALERT,
				    "%s: Enclosure Unrecoverable", v[dev]);
			}
		}
		sleep(polltime);
	}
	/* NOTREACHED */
}
