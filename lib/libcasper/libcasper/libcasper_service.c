/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2015 Mariusz Zaborski <oshogbo@FreeBSD.org>
 * Copyright (c) 2017 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libcasper_impl.h"
#include "zygote.h"

struct casper_service {
	struct service			*cs_service;
	TAILQ_ENTRY(casper_service)	 cs_next;
};

static TAILQ_HEAD(, casper_service) casper_services =
    TAILQ_HEAD_INITIALIZER(casper_services);

#define	CORE_CASPER_NAME		"core.casper"
#define	CSERVICE_IS_CORE(service)	\
	(strcmp(service_name(service->cs_service), CORE_CASPER_NAME) == 0)

static struct casper_service *
service_find(const char *name)
{
	struct casper_service *casserv;

	TAILQ_FOREACH(casserv, &casper_services, cs_next) {
		if (strcmp(service_name(casserv->cs_service), name) == 0)
			break;
	}
	return (casserv);
}

struct casper_service *
service_register(const char *name, service_limit_func_t *limitfunc,
   service_command_func_t *commandfunc, uint64_t flags)
{
	struct casper_service *casserv;

	if (commandfunc == NULL)
		return (NULL);
	if (name == NULL || name[0] == '\0')
		return (NULL);
	if (service_find(name) != NULL)
		return (NULL);

	casserv = malloc(sizeof(*casserv));
	if (casserv == NULL)
		return (NULL);

	casserv->cs_service = service_alloc(name, limitfunc, commandfunc,
	    flags);
	if (casserv->cs_service == NULL) {
		free(casserv);
		return (NULL);
	}
	TAILQ_INSERT_TAIL(&casper_services, casserv, cs_next);

	return (casserv);
}

static bool
casper_allowed_service(const nvlist_t *limits, const char *service)
{

	if (limits == NULL)
		return (true);

	if (nvlist_exists_null(limits, service))
		return (true);

	return (false);
}

static int
casper_limit(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{
	const char *name;
	int type;
	void *cookie;

	cookie = NULL;
	while ((name = nvlist_next(newlimits, &type, &cookie)) != NULL) {
		if (type != NV_TYPE_NULL)
			return (EINVAL);
		if (!casper_allowed_service(oldlimits, name))
			return (ENOTCAPABLE);
	}

	return (0);
}

void
service_execute(int chanfd)
{
	struct casper_service *casserv;
	struct service *service;
	const char *servname;
	nvlist_t *nvl;
	int procfd;

	nvl = nvlist_recv(chanfd, 0);
	if (nvl == NULL)
		_exit(1);
	if (!nvlist_exists_string(nvl, "service"))
		_exit(1);
	servname = nvlist_get_string(nvl, "service");
	casserv = service_find(servname);
	if (casserv == NULL)
		_exit(1);
	service = casserv->cs_service;
	procfd = nvlist_take_descriptor(nvl, "procfd");
	nvlist_destroy(nvl);

	service_start(service, chanfd, procfd);
	/* Not reached. */
	_exit(1);
}

static int
casper_command(const char *cmd, const nvlist_t *limits, nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	struct casper_service *casserv;
	const char *servname;
	nvlist_t *nvl;
	int chanfd, procfd, error;

	if (strcmp(cmd, "open") != 0)
		return (EINVAL);
	if (!nvlist_exists_string(nvlin, "service"))
		return (EINVAL);

	servname = nvlist_get_string(nvlin, "service");
	casserv = service_find(servname);
	if (casserv == NULL)
		return (ENOENT);

	if (!casper_allowed_service(limits, servname))
		return (ENOTCAPABLE);

	if (zygote_clone_service_execute(&chanfd, &procfd) == -1)
		return (errno);

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "service", servname);
	nvlist_move_descriptor(nvl, "procfd", procfd);
	if (nvlist_send(chanfd, nvl) == -1) {
		error = errno;
		nvlist_destroy(nvl);
		close(chanfd);
		return (error);
	}
	nvlist_destroy(nvl);

	nvlist_move_descriptor(nvlout, "chanfd", chanfd);
	nvlist_add_number(nvlout, "chanflags",
	    service_get_channel_flags(casserv->cs_service));

	return (0);
}

static void
service_register_core(int fd)
{
	struct casper_service *casserv;
	struct service_connection *sconn;

	casserv = service_register(CORE_CASPER_NAME, casper_limit,
	    casper_command, 0);
	sconn = service_connection_add(casserv->cs_service, fd, NULL);
	if (sconn == NULL) {
		close(fd);
		abort();
	}
}

void
casper_main_loop(int fd)
{
	fd_set fds;
	struct casper_service *casserv;
	struct service_connection *sconn, *sconntmp;
	int sock, maxfd, ret;

	if (zygote_init() < 0)
		_exit(1);

	/*
	 * Register core services.
	 */
	service_register_core(fd);

	for (;;) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		maxfd = -1;
		TAILQ_FOREACH(casserv, &casper_services, cs_next) {
			/* We handle only core services. */
			if (!CSERVICE_IS_CORE(casserv))
				continue;
			for (sconn = service_connection_first(casserv->cs_service);
			    sconn != NULL;
			    sconn = service_connection_next(sconn)) {
				sock = service_connection_get_sock(sconn);
				FD_SET(sock, &fds);
				maxfd = sock > maxfd ? sock : maxfd;
			}
		}
		if (maxfd == -1) {
			/* Nothing to do. */
			_exit(0);
		}
		maxfd++;


		assert(maxfd <= (int)FD_SETSIZE);
		ret = select(maxfd, &fds, NULL, NULL, NULL);
		assert(ret == -1 || ret > 0);	/* select() cannot timeout */
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			_exit(1);
		}

		TAILQ_FOREACH(casserv, &casper_services, cs_next) {
			/* We handle only core services. */
			if (!CSERVICE_IS_CORE(casserv))
				continue;
			for (sconn = service_connection_first(casserv->cs_service);
			    sconn != NULL; sconn = sconntmp) {
				/*
				 * Prepare for connection to be removed from
				 * the list on failure.
				 */
				sconntmp = service_connection_next(sconn);
				sock = service_connection_get_sock(sconn);
				if (FD_ISSET(sock, &fds)) {
					service_message(casserv->cs_service,
					    sconn);
				}
			}
		}
	}
}
