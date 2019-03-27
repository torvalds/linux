/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013 The FreeBSD Foundation
 * Copyright (c) 2015-2017 Mariusz Zaborski <oshogbo@FreeBSD.org>
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

#ifndef	_LIBCASPER_H_
#define	_LIBCASPER_H_

#ifdef HAVE_CASPER
#define WITH_CASPER
#endif

#include <sys/types.h>
#include <sys/nv.h>

#include <stdlib.h>
#include <unistd.h>

#define	CASPER_NO_UNIQ	0x00000001

#ifndef	_NVLIST_T_DECLARED
#define	_NVLIST_T_DECLARED
struct nvlist;

typedef struct nvlist nvlist_t;
#endif

#ifndef	_CAP_CHANNEL_T_DECLARED
#define	_CAP_CHANNEL_T_DECLARED
#ifdef WITH_CASPER
struct cap_channel;

typedef struct cap_channel cap_channel_t;
#define	CASPER_SUPPORT	(1)
#else
struct cap_channel {
	int cch_fd;
	int cch_flags;
};
typedef struct cap_channel cap_channel_t;
#define	CASPER_SUPPORT	(0)
#endif /* ! WITH_CASPER */
#endif /* ! _CAP_CHANNEL_T_DECLARED */

__BEGIN_DECLS

#ifdef WITH_CASPER
int cap_channel_flags(const cap_channel_t *chan);
#else
static inline int
cap_channel_flags(const cap_channel_t *chan)
{

	return (chan->cch_flags);
}
#endif

static inline int
channel_nvlist_flags(const cap_channel_t *chan)
{
	int flags;

	flags = 0;
	if ((cap_channel_flags(chan) & CASPER_NO_UNIQ) != 0)
		flags |= NV_FLAG_NO_UNIQUE;

	return (flags);
}

/*
 * The functions opens unrestricted communication channel to Casper.
 */
#ifdef WITH_CASPER
cap_channel_t *cap_init(void);
#else
static inline cap_channel_t *
cap_init(void)
{
	cap_channel_t *chan;

	chan = (cap_channel_t *)malloc(sizeof(*chan));
	if (chan != NULL) {
		chan->cch_fd = -1;
	}
	return (chan);
}
#endif

/*
 * The functions to communicate with service.
 */
#ifdef WITH_CASPER
cap_channel_t	*cap_service_open(const cap_channel_t *chan, const char *name);
int		 cap_service_limit(const cap_channel_t *chan,
		    const char * const *names, size_t nnames);
#else
#define	cap_service_open(chan, name)		(cap_init())
#define	cap_service_limit(chan, names, nnames)	(0)
#endif

/*
 * The function creates cap_channel_t based on the given socket.
 */
#ifdef WITH_CASPER
cap_channel_t *cap_wrap(int sock, int flags);
#else
static inline cap_channel_t *
cap_wrap(int sock, int flags)
{
	cap_channel_t *chan;

	chan = cap_init();
	if (chan != NULL) {
		chan->cch_fd = sock;
		chan->cch_flags = flags;
	}
	return (chan);
}
#endif

/*
 * The function returns communication socket and frees cap_channel_t.
 */
#ifdef WITH_CASPER
int	cap_unwrap(cap_channel_t *chan, int *flags);
#else
static inline int
cap_unwrap(cap_channel_t *chan)
{
	int fd;

	fd = chan->cch_fd;
	free(chan);
	return (fd);
}
#endif

/*
 * The function clones the given capability.
 */
#ifdef WITH_CASPER
cap_channel_t *cap_clone(const cap_channel_t *chan);
#else
static inline cap_channel_t *
cap_clone(const cap_channel_t *chan)
{
	cap_channel_t *newchan;

	newchan = cap_init();
	if (newchan == NULL) {
		return (NULL);
	}

	if (chan->cch_fd == -1) {
		newchan->cch_fd = -1;
	} else {
		newchan->cch_fd = dup(chan->cch_fd);
		if (newchan->cch_fd < 0) {
			free(newchan);
			newchan = NULL;
		}
	}
	newchan->cch_flags = chan->cch_flags;

	return (newchan);
}
#endif

/*
 * The function closes the given capability.
 */
#ifdef WITH_CASPER
void	cap_close(cap_channel_t *chan);
#else
static inline void
cap_close(cap_channel_t *chan)
{

	if (chan->cch_fd >= 0) {
		close(chan->cch_fd);
	}
	free(chan);
}
#endif

/*
 * The function returns socket descriptor associated with the given
 * cap_channel_t for use with select(2)/kqueue(2)/etc.
 */
#ifdef WITH_CASPER
int	cap_sock(const cap_channel_t *chan);
#else
#define	cap_sock(chan)	(chan->cch_fd)
#endif

/*
 * The function limits the given capability.
 * It always destroys 'limits' on return.
 */
#ifdef WITH_CASPER
int	cap_limit_set(const cap_channel_t *chan, nvlist_t *limits);
#else
#define	cap_limit_set(chan, limits)	(0)
#endif

/*
 * The function returns current limits of the given capability.
 */
#ifdef WITH_CASPER
int	cap_limit_get(const cap_channel_t *chan, nvlist_t **limitsp);
#else
static inline int
cap_limit_get(const cap_channel_t *chan __unused, nvlist_t **limitsp)
{

	*limitsp = nvlist_create(channel_nvlist_flags(chan));
	return (0);
}
#endif

/*
 * Function sends nvlist over the given capability.
 */
#ifdef WITH_CASPER
int	cap_send_nvlist(const cap_channel_t *chan, const nvlist_t *nvl);
#else
#define	cap_send_nvlist(chan, nvl)	(0)
#endif

/*
 * Function receives nvlist over the given capability.
 */
#ifdef WITH_CASPER
nvlist_t *cap_recv_nvlist(const cap_channel_t *chan);
#else
#define	cap_recv_nvlist(chan)		(nvlist_create(chan->cch_flags))
#endif

/*
 * Function sends the given nvlist, destroys it and receives new nvlist in
 * response over the given capability.
 */
#ifdef WITH_CASPER
nvlist_t *cap_xfer_nvlist(const cap_channel_t *chan, nvlist_t *nvl);
#else
static inline nvlist_t *
cap_xfer_nvlist(const cap_channel_t *chan, nvlist_t *nvl)
{

	nvlist_destroy(nvl);
	return (nvlist_create(channel_nvlist_flags(chan)));
}
#endif

__END_DECLS

#endif	/* !_LIBCASPER_H_ */
