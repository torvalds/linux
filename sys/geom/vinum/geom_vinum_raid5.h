/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _GEOM_VINUM_RAID5_H_
#define	_GEOM_VINUM_RAID5_H_

/*
 * A single RAID5 request usually needs more than one I/O transaction,
 * depending on the state of the associated subdisks and the direction of the
 * transaction (read or write).
 */

struct gv_raid5_packet {
	caddr_t	data;		/* Data buffer of this sub-request- */
	off_t	length;		/* Size of data buffer. */
	off_t	lockbase;	/* Deny access to our plex offset. */

	struct bio	*bio;	/* Pointer to the original bio. */
	struct bio	*parity;  /* The bio containing the parity data. */
	struct bio	*waiting; /* A bio that need to wait for other bios. */

	TAILQ_HEAD(,gv_bioq)		bits; /* List of subrequests. */
	TAILQ_ENTRY(gv_raid5_packet)	list; /* Entry in plex's packet list. */
};

struct gv_raid5_packet * gv_raid5_start(struct gv_plex *, struct bio *,
		caddr_t, off_t, off_t);
int	gv_stripe_active(struct gv_plex *, struct bio *);

#endif /* !_GEOM_VINUM_RAID5_H_ */
