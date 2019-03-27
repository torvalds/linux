/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013 The FreeBSD Foundation
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
#include <sys/socket.h>
#include <sys/nv.h>
#include <sys/procdesc.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libcasper.h"
#include "libcasper_impl.h"

#define	CASPER_VALID_FLAGS	(CASPER_NO_UNIQ)

/*
 * Structure describing communication channel between two separated processes.
 */
#define	CAP_CHANNEL_MAGIC	0xcac8a31
struct cap_channel {
	/*
	 * Magic value helps to ensure that a pointer to the right structure is
	 * passed to our functions.
	 */
	int	cch_magic;
	/* Socket descriptor for IPC. */
	int	cch_sock;
	/* Process descriptor for casper. */
	int	cch_pd;
	/* Flags to communicate with casper. */
	int	cch_flags;
};

static bool
cap_add_pd(cap_channel_t *chan, int pd)
{

	if (!fd_is_valid(pd))
		return (false);
	chan->cch_pd = pd;
	return (true);
}

int
cap_channel_flags(const cap_channel_t *chan)
{

	return (chan->cch_flags);
}

cap_channel_t *
cap_init(void)
{
	pid_t pid;
	int sock[2], serrno, pfd;
	bool ret;
	cap_channel_t *chan;

	if (socketpair(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0,
	    sock) == -1) {
		return (NULL);
	}

	pid = pdfork(&pfd, 0);
	if (pid == 0) {
		/* Child. */
		close(sock[0]);
		casper_main_loop(sock[1]);
		/* NOTREACHED. */
	} else if (pid > 0) {
		/* Parent. */
		close(sock[1]);
		chan = cap_wrap(sock[0], 0);
		if (chan == NULL) {
			serrno = errno;
			close(sock[0]);
			close(pfd);
			errno = serrno;
			return (NULL);
		}
		ret = cap_add_pd(chan, pfd);
		assert(ret);
		return (chan);
	}

	/* Error. */
	serrno = errno;
	close(sock[0]);
	close(sock[1]);
	errno = serrno;
	return (NULL);
}

cap_channel_t *
cap_wrap(int sock, int flags)
{
	cap_channel_t *chan;

	if (!fd_is_valid(sock))
		return (NULL);

	if ((flags & CASPER_VALID_FLAGS) != flags)
		return (NULL);

	chan = malloc(sizeof(*chan));
	if (chan != NULL) {
		chan->cch_sock = sock;
		chan->cch_pd = -1;
		chan->cch_flags = flags;
		chan->cch_magic = CAP_CHANNEL_MAGIC;
	}

	return (chan);
}

int
cap_unwrap(cap_channel_t *chan, int *flags)
{
	int sock;

	assert(chan != NULL);
	assert(chan->cch_magic == CAP_CHANNEL_MAGIC);

	sock = chan->cch_sock;
	if (chan->cch_pd != -1)
		close(chan->cch_pd);
	if (flags != NULL)
		*flags = chan->cch_flags;
	chan->cch_magic = 0;
	free(chan);

	return (sock);
}

cap_channel_t *
cap_clone(const cap_channel_t *chan)
{
	cap_channel_t *newchan;
	nvlist_t *nvl;
	int newsock;

	assert(chan != NULL);
	assert(chan->cch_magic == CAP_CHANNEL_MAGIC);

	nvl = nvlist_create(channel_nvlist_flags(chan));
	nvlist_add_string(nvl, "cmd", "clone");
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL)
		return (NULL);
	if (nvlist_get_number(nvl, "error") != 0) {
		errno = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		return (NULL);
	}
	newsock = nvlist_take_descriptor(nvl, "sock");
	nvlist_destroy(nvl);
	newchan = cap_wrap(newsock, chan->cch_flags);
	if (newchan == NULL) {
		int serrno;

		serrno = errno;
		close(newsock);
		errno = serrno;
	}

	return (newchan);
}

void
cap_close(cap_channel_t *chan)
{

	assert(chan != NULL);
	assert(chan->cch_magic == CAP_CHANNEL_MAGIC);

	chan->cch_magic = 0;
	if (chan->cch_pd != -1)
		close(chan->cch_pd);
	close(chan->cch_sock);
	free(chan);
}

int
cap_sock(const cap_channel_t *chan)
{

	assert(chan != NULL);
	assert(chan->cch_magic == CAP_CHANNEL_MAGIC);

	return (chan->cch_sock);
}

int
cap_limit_set(const cap_channel_t *chan, nvlist_t *limits)
{
	nvlist_t *nvlmsg;
	int error;

	nvlmsg = nvlist_create(channel_nvlist_flags(chan));
	nvlist_add_string(nvlmsg, "cmd", "limit_set");
	nvlist_add_nvlist(nvlmsg, "limits", limits);
	nvlmsg = cap_xfer_nvlist(chan, nvlmsg);
	if (nvlmsg == NULL) {
		nvlist_destroy(limits);
		return (-1);
	}
	error = (int)nvlist_get_number(nvlmsg, "error");
	nvlist_destroy(nvlmsg);
	nvlist_destroy(limits);
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

int
cap_limit_get(const cap_channel_t *chan, nvlist_t **limitsp)
{
	nvlist_t *nvlmsg;
	int error;

	nvlmsg = nvlist_create(channel_nvlist_flags(chan));
	nvlist_add_string(nvlmsg, "cmd", "limit_get");
	nvlmsg = cap_xfer_nvlist(chan, nvlmsg);
	if (nvlmsg == NULL)
		return (-1);
	error = (int)nvlist_get_number(nvlmsg, "error");
	if (error != 0) {
		nvlist_destroy(nvlmsg);
		errno = error;
		return (-1);
	}
	if (nvlist_exists_null(nvlmsg, "limits"))
		*limitsp = NULL;
	else
		*limitsp = nvlist_take_nvlist(nvlmsg, "limits");
	nvlist_destroy(nvlmsg);
	return (0);
}

int
cap_send_nvlist(const cap_channel_t *chan, const nvlist_t *nvl)
{

	assert(chan != NULL);
	assert(chan->cch_magic == CAP_CHANNEL_MAGIC);

	return (nvlist_send(chan->cch_sock, nvl));
}

nvlist_t *
cap_recv_nvlist(const cap_channel_t *chan)
{

	assert(chan != NULL);
	assert(chan->cch_magic == CAP_CHANNEL_MAGIC);

	return (nvlist_recv(chan->cch_sock,
	    channel_nvlist_flags(chan)));
}

nvlist_t *
cap_xfer_nvlist(const cap_channel_t *chan, nvlist_t *nvl)
{

	assert(chan != NULL);
	assert(chan->cch_magic == CAP_CHANNEL_MAGIC);

	return (nvlist_xfer(chan->cch_sock, nvl,
	    channel_nvlist_flags(chan)));
}

cap_channel_t *
cap_service_open(const cap_channel_t *chan, const char *name)
{
	cap_channel_t *newchan;
	nvlist_t *nvl;
	int sock, error;
	int flags;

	sock = -1;

	nvl = nvlist_create(channel_nvlist_flags(chan));
	nvlist_add_string(nvl, "cmd", "open");
	nvlist_add_string(nvl, "service", name);
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL)
		return (NULL);
	error = (int)nvlist_get_number(nvl, "error");
	if (error != 0) {
		nvlist_destroy(nvl);
		errno = error;
		return (NULL);
	}
	sock = nvlist_take_descriptor(nvl, "chanfd");
	flags = nvlist_take_number(nvl, "chanflags");
	assert(sock >= 0);
	nvlist_destroy(nvl);
	nvl = NULL;
	newchan = cap_wrap(sock, flags);
	if (newchan == NULL)
		goto fail;
	return (newchan);
fail:
	error = errno;
	close(sock);
	errno = error;
	return (NULL);
}

int
cap_service_limit(const cap_channel_t *chan, const char * const *names,
    size_t nnames)
{
	nvlist_t *limits;
	unsigned int i;

	limits = nvlist_create(channel_nvlist_flags(chan));
	for (i = 0; i < nnames; i++)
		nvlist_add_null(limits, names[i]);
	return (cap_limit_set(chan, limits));
}
