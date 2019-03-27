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
 *
 * $FreeBSD$
 */

#ifndef	_LIBCASPER_IMPL_H_
#define	_LIBCASPER_IMPL_H_

#include <stdbool.h>

#include "libcasper.h"
#include "libcasper_service.h"

struct service;
struct service_connection;

bool fd_is_valid(int fd);

/* Private service functions. */
struct service	*service_alloc(const char *name,
		    service_limit_func_t *limitfunc,
		    service_command_func_t *commandfunc, uint64_t flags);
void		 service_free(struct service *service);
void		 service_message(struct service *service,
		    struct service_connection *sconn);
void		 service_start(struct service *service, int sock, int procfd);
const char	*service_name(struct service *service);
int		 service_get_channel_flags(struct service *service);

/* Private service connection functions. */
struct service_connection	*service_connection_add(struct service *service,
				    int sock, const nvlist_t *limits);
void				 service_connection_remove(
				    struct service *service,
				    struct service_connection *sconn);
int				 service_connection_clone(
				    struct service *service,
				    struct service_connection *sconn);
struct service_connection	*service_connection_first(
				    struct service *service);
struct service_connection	*service_connection_next(
				    struct service_connection *sconn);
cap_channel_t			*service_connection_get_chan(
				    const struct service_connection *sconn);
int				 service_connection_get_sock(
				    const struct service_connection *sconn);
const nvlist_t			*service_connection_get_limits(
				    const struct service_connection *sconn);
void				 service_connection_set_limits(
				    struct service_connection *sconn,
				    nvlist_t *limits);

/* Private libcasper functions. */
void	casper_main_loop(int fd);

#endif	/* !_LIBCASPER_IMPL_H_ */
