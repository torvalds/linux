/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * Copyright (c) 2015 Mariusz Zaborski <oshogbo@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/nv.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "libcasper.h"
#include "libcasper_impl.h"

/*
 * Currently there is only one service_connection per service.
 * In the future we may want multiple connections from multiple clients
 * per one service instance, but it has to be carefully designed.
 * The problem is that we may restrict/sandbox service instance according
 * to the limits provided. When new connection comes in with different
 * limits we won't be able to access requested resources.
 * Not to mention one process will serve to mutiple mutually untrusted
 * clients and compromise of this service instance by one of its clients
 * can lead to compromise of the other clients.
 */

/*
 * Client connections to the given service.
 */
#define	SERVICE_CONNECTION_MAGIC	0x5e91c0ec
struct service_connection {
	int		 sc_magic;
	cap_channel_t	*sc_chan;
	nvlist_t	*sc_limits;
	TAILQ_ENTRY(service_connection) sc_next;
};

#define	SERVICE_MAGIC	0x5e91ce
struct service {
	int			 s_magic;
	char			*s_name;
	uint64_t		 s_flags;
	service_limit_func_t	*s_limit;
	service_command_func_t	*s_command;
	TAILQ_HEAD(, service_connection) s_connections;
};

struct service *
service_alloc(const char *name, service_limit_func_t *limitfunc,
    service_command_func_t *commandfunc, uint64_t flags)
{
	struct service *service;

	service = malloc(sizeof(*service));
	if (service == NULL)
		return (NULL);
	service->s_name = strdup(name);
	if (service->s_name == NULL) {
		free(service);
		return (NULL);
	}
	service->s_limit = limitfunc;
	service->s_command = commandfunc;
	service->s_flags = flags;
	TAILQ_INIT(&service->s_connections);
	service->s_magic = SERVICE_MAGIC;

	return (service);
}

void
service_free(struct service *service)
{
	struct service_connection *sconn;

	assert(service->s_magic == SERVICE_MAGIC);

	service->s_magic = 0;
	while ((sconn = service_connection_first(service)) != NULL)
		service_connection_remove(service, sconn);
	free(service->s_name);
	free(service);
}

struct service_connection *
service_connection_add(struct service *service, int sock,
    const nvlist_t *limits)
{
	struct service_connection *sconn;
	int serrno;

	assert(service->s_magic == SERVICE_MAGIC);

	sconn = malloc(sizeof(*sconn));
	if (sconn == NULL)
		return (NULL);
	sconn->sc_chan = cap_wrap(sock,
	    service_get_channel_flags(service));
	if (sconn->sc_chan == NULL) {
		serrno = errno;
		free(sconn);
		errno = serrno;
		return (NULL);
	}
	if (limits == NULL) {
		sconn->sc_limits = NULL;
	} else {
		sconn->sc_limits = nvlist_clone(limits);
		if (sconn->sc_limits == NULL) {
			serrno = errno;
			(void)cap_unwrap(sconn->sc_chan, NULL);
			free(sconn);
			errno = serrno;
			return (NULL);
		}
	}
	sconn->sc_magic = SERVICE_CONNECTION_MAGIC;
	TAILQ_INSERT_TAIL(&service->s_connections, sconn, sc_next);
	return (sconn);
}

void
service_connection_remove(struct service *service,
    struct service_connection *sconn)
{

	assert(service->s_magic == SERVICE_MAGIC);
	assert(sconn->sc_magic == SERVICE_CONNECTION_MAGIC);

	TAILQ_REMOVE(&service->s_connections, sconn, sc_next);
	sconn->sc_magic = 0;
	nvlist_destroy(sconn->sc_limits);
	cap_close(sconn->sc_chan);
	free(sconn);
}

int
service_connection_clone(struct service *service,
    struct service_connection *sconn)
{
	struct service_connection *newsconn;
	int serrno, sock[2];

	if (socketpair(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sock) < 0)
		return (-1);

	newsconn = service_connection_add(service, sock[0],
	    service_connection_get_limits(sconn));
	if (newsconn == NULL) {
		serrno = errno;
		close(sock[0]);
		close(sock[1]);
		errno = serrno;
		return (-1);
	}

	return (sock[1]);
}

struct service_connection *
service_connection_first(struct service *service)
{
	struct service_connection *sconn;

	assert(service->s_magic == SERVICE_MAGIC);

	sconn = TAILQ_FIRST(&service->s_connections);
	assert(sconn == NULL ||
	    sconn->sc_magic == SERVICE_CONNECTION_MAGIC);
	return (sconn);
}

struct service_connection *
service_connection_next(struct service_connection *sconn)
{

	assert(sconn->sc_magic == SERVICE_CONNECTION_MAGIC);

	sconn = TAILQ_NEXT(sconn, sc_next);
	assert(sconn == NULL ||
	    sconn->sc_magic == SERVICE_CONNECTION_MAGIC);
	return (sconn);
}

cap_channel_t *
service_connection_get_chan(const struct service_connection *sconn)
{

	assert(sconn->sc_magic == SERVICE_CONNECTION_MAGIC);

	return (sconn->sc_chan);
}

int
service_connection_get_sock(const struct service_connection *sconn)
{

	assert(sconn->sc_magic == SERVICE_CONNECTION_MAGIC);

	return (cap_sock(sconn->sc_chan));
}

const nvlist_t *
service_connection_get_limits(const struct service_connection *sconn)
{

	assert(sconn->sc_magic == SERVICE_CONNECTION_MAGIC);

	return (sconn->sc_limits);
}

void
service_connection_set_limits(struct service_connection *sconn,
    nvlist_t *limits)
{

	assert(sconn->sc_magic == SERVICE_CONNECTION_MAGIC);

	nvlist_destroy(sconn->sc_limits);
	sconn->sc_limits = limits;
}

void
service_message(struct service *service, struct service_connection *sconn)
{
	nvlist_t *nvlin, *nvlout;
	const char *cmd;
	int error, flags;

	flags = 0;
	if ((service->s_flags & CASPER_SERVICE_NO_UNIQ_LIMITS) != 0)
		flags = NV_FLAG_NO_UNIQUE;

	nvlin = cap_recv_nvlist(service_connection_get_chan(sconn));
	if (nvlin == NULL) {
		service_connection_remove(service, sconn);
		return;
	}

	error = EDOOFUS;
	nvlout = nvlist_create(flags);

	cmd = nvlist_get_string(nvlin, "cmd");
	if (strcmp(cmd, "limit_set") == 0) {
		nvlist_t *nvllim;

		nvllim = nvlist_take_nvlist(nvlin, "limits");
		if (service->s_limit == NULL) {
			error = EOPNOTSUPP;
		} else {
			error = service->s_limit(
			    service_connection_get_limits(sconn), nvllim);
		}
		if (error == 0) {
			service_connection_set_limits(sconn, nvllim);
			/* Function consumes nvllim. */
		} else {
			nvlist_destroy(nvllim);
		}
	} else if (strcmp(cmd, "limit_get") == 0) {
		const nvlist_t *nvllim;

		nvllim = service_connection_get_limits(sconn);
		if (nvllim != NULL)
			nvlist_add_nvlist(nvlout, "limits", nvllim);
		else
			nvlist_add_null(nvlout, "limits");
		error = 0;
	} else if (strcmp(cmd, "clone") == 0) {
		int sock;

		sock = service_connection_clone(service, sconn);
		if (sock == -1) {
			error = errno;
		} else {
			nvlist_move_descriptor(nvlout, "sock", sock);
			error = 0;
		}
	} else {
		error = service->s_command(cmd,
		    service_connection_get_limits(sconn), nvlin, nvlout);
	}

	nvlist_destroy(nvlin);
	nvlist_add_number(nvlout, "error", (uint64_t)error);

	if (cap_send_nvlist(service_connection_get_chan(sconn), nvlout) == -1)
		service_connection_remove(service, sconn);

	nvlist_destroy(nvlout);
}

static int
fd_add(fd_set *fdsp, int maxfd, int fd)
{

	FD_SET(fd, fdsp);
	return (fd > maxfd ? fd : maxfd);
}

const char *
service_name(struct service *service)
{

	assert(service->s_magic == SERVICE_MAGIC);
	return (service->s_name);
}

int
service_get_channel_flags(struct service *service)
{
	int flags;

	assert(service->s_magic == SERVICE_MAGIC);
	flags = 0;

	if ((service->s_flags & CASPER_SERVICE_NO_UNIQ_LIMITS) != 0)
		flags |= CASPER_NO_UNIQ;

	return (flags);
}

static void
stdnull(void)
{
	int fd;

	fd = open(_PATH_DEVNULL, O_RDWR);
	if (fd == -1)
		errx(1, "Unable to open %s", _PATH_DEVNULL);

	if (setsid() == -1)
		errx(1, "Unable to detach from session");

	if (dup2(fd, STDIN_FILENO) == -1)
		errx(1, "Unable to cover stdin");
	if (dup2(fd, STDOUT_FILENO) == -1)
		errx(1, "Unable to cover stdout");
	if (dup2(fd, STDERR_FILENO) == -1)
		errx(1, "Unable to cover stderr");

	if (fd > STDERR_FILENO)
		close(fd);
}

static void
service_clean(int sock, int procfd, uint64_t flags)
{
	int fd, maxfd, minfd;

	assert(sock > STDERR_FILENO);
	assert(procfd > STDERR_FILENO);
	assert(sock != procfd);

	if ((flags & CASPER_SERVICE_STDIO) == 0)
		stdnull();

	if ((flags & CASPER_SERVICE_FD) == 0) {
		if (procfd > sock) {
			maxfd = procfd;
			minfd = sock;
		} else {
			maxfd = sock;
			minfd = procfd;
		}

		for (fd = STDERR_FILENO + 1; fd < maxfd; fd++) {
			if (fd != minfd)
				close(fd);
		}
		closefrom(maxfd + 1);
	}
}

void
service_start(struct service *service, int sock, int procfd)
{
	struct service_connection *sconn, *sconntmp;
	fd_set fds;
	int maxfd, nfds;

	assert(service != NULL);
	assert(service->s_magic == SERVICE_MAGIC);
	setproctitle("%s", service->s_name);
	service_clean(sock, procfd, service->s_flags);

	if (service_connection_add(service, sock, NULL) == NULL)
		_exit(1);

	for (;;) {
		FD_ZERO(&fds);
		maxfd = -1;
		for (sconn = service_connection_first(service); sconn != NULL;
		    sconn = service_connection_next(sconn)) {
			maxfd = fd_add(&fds, maxfd,
			    service_connection_get_sock(sconn));
		}

		assert(maxfd >= 0);
		assert(maxfd + 1 <= (int)FD_SETSIZE);
		nfds = select(maxfd + 1, &fds, NULL, NULL, NULL);
		if (nfds < 0) {
			if (errno != EINTR)
				_exit(1);
			continue;
		} else if (nfds == 0) {
			/* Timeout. */
			abort();
		}

		for (sconn = service_connection_first(service); sconn != NULL;
		    sconn = sconntmp) {
			/*
			 * Prepare for connection to be removed from the list
			 * on failure.
			 */
			sconntmp = service_connection_next(sconn);
			if (FD_ISSET(service_connection_get_sock(sconn), &fds))
				service_message(service, sconn);
		}
		if (service_connection_first(service) == NULL) {
			/*
			 * No connections left, exiting.
			 */
			break;
		}
	}

	_exit(0);
}
