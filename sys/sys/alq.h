/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2002, Jeffrey Roberson <jeff@freebsd.org>
 * Copyright (c) 2008-2009, Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010, The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by Lawrence Stewart under sponsorship from the FreeBSD Foundation.
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
 *
 */
#ifndef _SYS_ALQ_H_
#define	_SYS_ALQ_H_

/*
 * Opaque type for the Async. Logging Queue
 */
struct alq;

/* The thread for the logging daemon */
extern struct thread *ald_thread;

/*
 * Async. Logging Entry
 */
struct ale {
	intptr_t	ae_bytesused;	/* # bytes written to ALE. */
	char		*ae_data;	/* Write ptr. */
	int		ae_pad;		/* Unused, compat. */
};

/* Flag options. */
#define	ALQ_NOWAIT	0x0001	/* ALQ may not sleep. */
#define	ALQ_WAITOK	0x0002	/* ALQ may sleep. */
#define	ALQ_NOACTIVATE	0x0004	/* Don't activate ALQ after write. */
#define	ALQ_ORDERED	0x0010	/* Maintain write ordering between threads. */

/* Suggested mode for file creation. */
#define	ALQ_DEFAULT_CMODE	0600

/*
 * alq_open_flags:  Creates a new queue
 *
 * Arguments:
 *	alq	Storage for a pointer to the newly created queue.
 *	file	The filename to open for logging.
 *	cred	Credential to authorize open and I/O with.
 *	cmode	Creation mode for file, if new.
 *	size	The size of the queue in bytes.
 *	flags	ALQ_ORDERED
 * Returns:
 *	error from open or 0 on success
 */
struct ucred;
int alq_open_flags(struct alq **alqp, const char *file, struct ucred *cred, int cmode,
	    int size, int flags);
int alq_open(struct alq **alqp, const char *file, struct ucred *cred, int cmode,
	    int size, int count);

/*
 * alq_writen:  Write data into the queue
 *
 * Arguments:
 *	alq	The queue we're writing to
 *	data	The entry to be recorded
 *	len	The number of bytes to write from *data
 *	flags	(ALQ_NOWAIT || ALQ_WAITOK), ALQ_NOACTIVATE
 *
 * Returns:
 *	EWOULDBLOCK if:
 *		Waitok is ALQ_NOWAIT and the queue is full.
 *		The system is shutting down.
 *	0 on success.
 */
int alq_writen(struct alq *alq, void *data, int len, int flags);
int alq_write(struct alq *alq, void *data, int flags);

/*
 * alq_flush:  Flush the queue out to disk
 */
void alq_flush(struct alq *alq);

/*
 * alq_close:  Flush the queue and free all resources.
 */
void alq_close(struct alq *alq);

/*
 * alq_getn:  Return an entry for direct access
 *
 * Arguments:
 *	alq	The queue to retrieve an entry from
 *	len	Max number of bytes required
 *	flags	(ALQ_NOWAIT || ALQ_WAITOK)
 *
 * Returns:
 *	The next available ale on success.
 *	NULL if:
 *		flags is ALQ_NOWAIT and the queue is full.
 *		The system is shutting down.
 *
 * This leaves the queue locked until a subsequent alq_post.
 */
struct ale *alq_getn(struct alq *alq, int len, int flags);
struct ale *alq_get(struct alq *alq, int flags);

/*
 * alq_post_flags:  Schedule the ale retrieved by alq_get/alq_getn for writing.
 *	alq	The queue to post the entry to.
 *	ale	An asynch logging entry returned by alq_get.
 *	flags	ALQ_NOACTIVATE
 */
void alq_post_flags(struct alq *alq, struct ale *ale, int flags);

static __inline void
alq_post(struct alq *alq, struct ale *ale)
{
	alq_post_flags(alq, ale, 0);
}

#endif	/* _SYS_ALQ_H_ */
