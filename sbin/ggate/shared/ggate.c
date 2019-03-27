/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/disk.h>
#include <sys/stat.h>
#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <libgen.h>
#include <libutil.h>
#include <netdb.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdint.h>
#include <libgeom.h>

#include <geom/gate/g_gate.h>
#include "ggate.h"


int g_gate_devfd = -1;
int g_gate_verbose = 0;


void
g_gate_vlog(int priority, const char *message, va_list ap)
{

	if (g_gate_verbose) {
		const char *prefix;

		switch (priority) {
		case LOG_ERR:
			prefix = "error";
			break;
		case LOG_WARNING:
			prefix = "warning";
			break;
		case LOG_NOTICE:
			prefix = "notice";
			break;
		case LOG_INFO:
			prefix = "info";
			break;
		case LOG_DEBUG:
			prefix = "debug";
			break;
		default:
			prefix = "unknown";
		}

		printf("%s: ", prefix);
		vprintf(message, ap);
		printf("\n");
	} else {
		if (priority != LOG_DEBUG)
			vsyslog(priority, message, ap);
	}
}

void
g_gate_log(int priority, const char *message, ...)
{
	va_list ap;

	va_start(ap, message);
	g_gate_vlog(priority, message, ap);
	va_end(ap);
}

void
g_gate_xvlog(const char *message, va_list ap)
{

	g_gate_vlog(LOG_ERR, message, ap);
	g_gate_vlog(LOG_ERR, "Exiting.", ap);
	exit(EXIT_FAILURE);
}

void
g_gate_xlog(const char *message, ...)
{
	va_list ap;

	va_start(ap, message);
	g_gate_xvlog(message, ap);
	/* NOTREACHED */
	va_end(ap);
	exit(EXIT_FAILURE);
}

off_t
g_gate_mediasize(int fd)
{
	off_t mediasize;
	struct stat sb;

	if (fstat(fd, &sb) == -1)
		g_gate_xlog("fstat(): %s.", strerror(errno));
	if (S_ISCHR(sb.st_mode)) {
		if (ioctl(fd, DIOCGMEDIASIZE, &mediasize) == -1) {
			g_gate_xlog("Can't get media size: %s.",
			    strerror(errno));
		}
	} else if (S_ISREG(sb.st_mode)) {
		mediasize = sb.st_size;
	} else {
		g_gate_xlog("Unsupported file system object.");
	}
	return (mediasize);
}

unsigned
g_gate_sectorsize(int fd)
{
	unsigned secsize;
	struct stat sb;

	if (fstat(fd, &sb) == -1)
		g_gate_xlog("fstat(): %s.", strerror(errno));
	if (S_ISCHR(sb.st_mode)) {
		if (ioctl(fd, DIOCGSECTORSIZE, &secsize) == -1) {
			g_gate_xlog("Can't get sector size: %s.",
			    strerror(errno));
		}
	} else if (S_ISREG(sb.st_mode)) {
		secsize = 512;
	} else {
		g_gate_xlog("Unsupported file system object.");
	}
	return (secsize);
}

void
g_gate_open_device(void)
{

	g_gate_devfd = open("/dev/" G_GATE_CTL_NAME, O_RDWR);
	if (g_gate_devfd == -1)
		err(EXIT_FAILURE, "open(/dev/%s)", G_GATE_CTL_NAME);
}

void
g_gate_close_device(void)
{

	close(g_gate_devfd);
}

void
g_gate_ioctl(unsigned long req, void *data)
{

	if (ioctl(g_gate_devfd, req, data) == -1) {
		g_gate_xlog("%s: ioctl(/dev/%s): %s.", getprogname(),
		    G_GATE_CTL_NAME, strerror(errno));
	}
}

void
g_gate_destroy(int unit, int force)
{
	struct g_gate_ctl_destroy ggio;

	ggio.gctl_version = G_GATE_VERSION;
	ggio.gctl_unit = unit;
	ggio.gctl_force = force;
	g_gate_ioctl(G_GATE_CMD_DESTROY, &ggio);
}

void
g_gate_load_module(void)
{

	if (modfind("g_gate") == -1) {
		/* Not present in kernel, try loading it. */
		if (kldload("geom_gate") == -1 || modfind("g_gate") == -1) {
			if (errno != EEXIST) {
				errx(EXIT_FAILURE,
				    "geom_gate module not available!");
			}
		}
	}
}

/*
 * When we send from ggatec packets larger than 32kB, performance drops
 * significantly (eg. to 256kB/s over 1Gbit/s link). This is not a problem
 * when data is send from ggated. I don't know why, so for now I limit
 * size of packets send from ggatec to 32kB by defining MAX_SEND_SIZE
 * in ggatec Makefile.
 */
#ifndef	MAX_SEND_SIZE
#define	MAX_SEND_SIZE	MAXPHYS
#endif
ssize_t
g_gate_send(int s, const void *buf, size_t len, int flags)
{
	ssize_t done = 0, done2;
	const unsigned char *p = buf;

	while (len > 0) {
		done2 = send(s, p, MIN(len, MAX_SEND_SIZE), flags);
		if (done2 == 0)
			break;
		else if (done2 == -1) {
			if (errno == EAGAIN) {
				printf("%s: EAGAIN\n", __func__);
				continue;
			}
			done = -1;
			break;
		}
		done += done2;
		p += done2;
		len -= done2;
	}
	return (done);
}

ssize_t
g_gate_recv(int s, void *buf, size_t len, int flags)
{
	ssize_t done;

	do {
		done = recv(s, buf, len, flags);
	} while (done == -1 && errno == EAGAIN);
	return (done);
}

int nagle = 1;
unsigned rcvbuf = G_GATE_RCVBUF;
unsigned sndbuf = G_GATE_SNDBUF;

void
g_gate_socket_settings(int sfd)
{
	struct timeval tv;
	int bsize, on;

	/* Socket settings. */
	on = 1;
	if (nagle) {
		if (setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &on,
		    sizeof(on)) == -1) {
			g_gate_xlog("setsockopt() error: %s.", strerror(errno));
		}
	}
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1)
		g_gate_xlog("setsockopt(SO_REUSEADDR): %s.", strerror(errno));
	bsize = rcvbuf;
	if (setsockopt(sfd, SOL_SOCKET, SO_RCVBUF, &bsize, sizeof(bsize)) == -1)
		g_gate_xlog("setsockopt(SO_RCVBUF): %s.", strerror(errno));
	bsize = sndbuf;
	if (setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &bsize, sizeof(bsize)) == -1)
		g_gate_xlog("setsockopt(SO_SNDBUF): %s.", strerror(errno));
	tv.tv_sec = 8;
	tv.tv_usec = 0;
	if (setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
		g_gate_log(LOG_ERR, "setsockopt(SO_SNDTIMEO) error: %s.",
		    strerror(errno));
	}
	if (setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
		g_gate_log(LOG_ERR, "setsockopt(SO_RCVTIMEO) error: %s.",
		    strerror(errno));
	}
}

#ifdef LIBGEOM
static struct gclass *
find_class(struct gmesh *mesh, const char *name)
{
	struct gclass *class;

	LIST_FOREACH(class, &mesh->lg_class, lg_class) {
		if (strcmp(class->lg_name, name) == 0)
			return (class);
	}
	return (NULL);
}

static const char *
get_conf(struct ggeom *gp, const char *name)
{
	struct gconfig *conf;

	LIST_FOREACH(conf, &gp->lg_config, lg_config) {
		if (strcmp(conf->lg_name, name) == 0)
			return (conf->lg_val);
	}
	return (NULL);
}

static void
show_config(struct ggeom *gp, int verbose)
{
	struct gprovider *pp;
	char buf[5];

	pp = LIST_FIRST(&gp->lg_provider);
	if (pp == NULL)
		return;
	if (!verbose) {
		printf("%s\n", pp->lg_name);
		return;
	}
	printf("       NAME: %s\n", pp->lg_name);
	printf("       info: %s\n", get_conf(gp, "info"));
	printf("     access: %s\n", get_conf(gp, "access"));
	printf("    timeout: %s\n", get_conf(gp, "timeout"));
	printf("queue_count: %s\n", get_conf(gp, "queue_count"));
	printf(" queue_size: %s\n", get_conf(gp, "queue_size"));
	printf(" references: %s\n", get_conf(gp, "ref"));
	humanize_number(buf, sizeof(buf), (int64_t)pp->lg_mediasize, "",
	    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
	printf("  mediasize: %jd (%s)\n", (intmax_t)pp->lg_mediasize, buf);
	printf(" sectorsize: %u\n", pp->lg_sectorsize);
	printf("       mode: %s\n", pp->lg_mode);
	printf("\n");
}

void
g_gate_list(int unit, int verbose)
{
	struct gmesh mesh;
	struct gclass *class;
	struct ggeom *gp;
	char name[64];
	int error;

	error = geom_gettree(&mesh);
	if (error != 0)
		exit(EXIT_FAILURE);
	class = find_class(&mesh, G_GATE_CLASS_NAME);
	if (class == NULL) {
		geom_deletetree(&mesh);
		exit(EXIT_SUCCESS);
	}
	if (unit >= 0) {
		snprintf(name, sizeof(name), "%s%d", G_GATE_PROVIDER_NAME,
		    unit);
	}
	LIST_FOREACH(gp, &class->lg_geom, lg_geom) {
		if (unit != -1 && strcmp(gp->lg_name, name) != 0)
			continue;
		show_config(gp, verbose);
	}
	geom_deletetree(&mesh);
	exit(EXIT_SUCCESS);
}
#endif	/* LIBGEOM */

in_addr_t
g_gate_str2ip(const char *str)
{
	struct hostent *hp;
	in_addr_t ip;

	ip = inet_addr(str);
	if (ip != INADDR_NONE) {
		/* It is a valid IP address. */
		return (ip);
	}
	/* Check if it is a valid host name. */
	hp = gethostbyname(str);
	if (hp == NULL)
		return (INADDR_NONE);
	return (((struct in_addr *)(void *)hp->h_addr)->s_addr);
}
