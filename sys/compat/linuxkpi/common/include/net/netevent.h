/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_NET_NETEVENT_H_
#define	_LINUX_NET_NETEVENT_H_

#include <sys/types.h>
#include <sys/eventhandler.h>

#include <linux/notifier.h>

enum netevent_notif_type {
	NETEVENT_NEIGH_UPDATE = 0,
#if 0 /* Unsupported events. */
	NETEVENT_PMTU_UPDATE,
	NETEVENT_REDIRECT,
#endif
};

struct llentry;

static inline void
_handle_arp_update_event(void *arg, struct llentry *lle, int evt __unused)
{
	struct notifier_block *nb;

	nb = arg;
	nb->notifier_call(nb, NETEVENT_NEIGH_UPDATE, lle);
}

static inline int
register_netevent_notifier(struct notifier_block *nb)
{
	nb->tags[NETEVENT_NEIGH_UPDATE] = EVENTHANDLER_REGISTER(
	    lle_event, _handle_arp_update_event, nb, 0);
	return (0);
}

static inline int
unregister_netevent_notifier(struct notifier_block *nb)
{

	EVENTHANDLER_DEREGISTER(lle_event, nb->tags[NETEVENT_NEIGH_UPDATE]);

	return (0);
}

#endif /* _LINUX_NET_NETEVENT_H_ */
